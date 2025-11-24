/**
 * M1125 - Custom CR6 community UI Pause/Resume (P=park, R=resume)
 *
 * M1125 P  - Pause print: stop SD/host printing, save XYZ/E, park nozzle,
 *            beep 6 times, set status messages.
 * M1125 R  - Resume print: restore XYZ/E (respecting PLR/relative state),
 *            resume SD/host printing, clear status messages.
 */

#include "../../inc/MarlinConfig.h"
#include "../gcode.h"
#include "../../sd/cardreader.h"
#include "../queue.h"
#include "../../module/planner.h"
#include "../../module/stepper.h"
#include "../../module/motion.h"
#include "../../libs/nozzle.h"
#include "../../MarlinCore.h"
#include "../../lcd/marlinui.h" // for UI / BUZZ / ScreenHandler
#include "../../libs/buzzer.h"
#include "M1125.h"
// DGUS screen handler suppression API (only when CR6 DGUS UI compiled in)
#if ENABLED(DGUS_LCD_UI_CR6_COMM)
  #include "../../lcd/extui/cr6_community_ui/DGUSScreenHandler.h"
  #include "../../lcd/extui/cr6_community_ui/DGUSDisplayDef.h"
#endif

// If ADVANCED_PAUSE_FEATURE is disabled we still need a sensible default
// timeout for M1125's heater-idle behavior. Define it here so M1125 can
// operate independently of the global advanced-pause code.
#if DISABLED(ADVANCED_PAUSE_FEATURE)
#ifndef PAUSE_PARK_NOZZLE_TIMEOUT
#define PAUSE_PARK_NOZZLE_TIMEOUT           300  // (seconds) Time limit before the nozzle is turned off for safety
#endif
#endif

// Grace window after initial heater-idle timeout (seconds) before heaters are actually disabled
#ifndef M1125_TIMEOUT_GRACE_SECONDS
#define M1125_TIMEOUT_GRACE_SECONDS 30
#endif

// Optional compile-time override: force M1125 to use a local, minimal
// heater-idle implementation even when HEATER_IDLE_HANDLER is enabled.
// Define M1125_USE_LOCAL_HEATER_IDLE to 1 in your build_flags or config
// to select the local handler.
#ifndef M1125_USE_LOCAL_HEATER_IDLE
#define M1125_USE_LOCAL_HEATER_IDLE 1
#endif


// File-scope state for M1125 pause/resume and heater-timeout handling
static xyze_pos_t m1125_saved_position;
static bool m1125_have_saved_position = false;
static bool m1125_pause_active = false;
static bool m1125_heaters_disabled_by_timeout = false;

// Small API used by the DGUS UI loop to let the screen handler
// observe and finalize heater-timeout behavior for M1125 pauses.
// Timeout handling state --------------------------------------------------
static bool m1125_timeout_pending = false;            // popup shown and countdown running
static millis_t m1125_timeout_deadline_ms = 0;       // when heaters will be disabled
static uint32_t m1125_timeout_old_remaining_at_continue = 0; // remaining when Continue pressed
static bool m1125_continue_pressed = false;
// Resume state machine -----------------------------------------------------
static bool m1125_resume_pending = false;            // waiting for heaters to reach targets
static bool m1125_resume_do_sd = false;             // whether to resume SD printing when ready
static bool m1125_resume_do_host = false;           // whether to call ui.resume_print() when ready
// Resume move feedrate (mm/s). Default: 3000 mm/min -> 50 mm/s
static feedRate_t m1125_resume_feedrate_mm_s = MMM_TO_MMS(3000.0f);
// Suppress auto job timer requests from the temperature module while
// M1125 owns the paused state.
static bool m1125_suppress_auto_job_timer = false;

void M1125_SuppressAutoJobTimer() { m1125_suppress_auto_job_timer = true; }
void M1125_ClearAutoJobTimerSuppress() { m1125_suppress_auto_job_timer = false; }
bool M1125_IsAutoJobTimerSuppressed() { return m1125_suppress_auto_job_timer; }
// Forward declare the resume poll helper so it can be used in the timeout checker.
static bool m1125_poll_resume();
// Forward declare the public timeout/check helper so the resume handler
// can invoke it immediately when processing a resume request.
bool M1125_CheckAndHandleHeaterTimeout();

// Storage for any commands that were committed into the G-code ring buffer
// from the SD card prior to a pause. We preserve them across the pause and
// restore them on resume so no commands are lost.
static GCodeQueue::CommandLine m1125_saved_commands[BUFSIZE];
static uint8_t m1125_saved_cmd_count = 0;

static inline char m1125_upper(const char c) {
  return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

static bool m1125_command_matches(const char *cmd, const char *target) {
  if (!cmd || !*cmd) return false;
  while (*cmd == ' ') ++cmd;
  const char *t = target;
  while (*t) {
    if (!*cmd || m1125_upper(*cmd) != *t) return false;
    ++cmd;
    ++t;
  }
  const char tail = *cmd;
  return tail == '\0' || tail == ' ' || tail == '\t' || tail == ';';
}

static bool m1125_should_skip_saved_command(const char *cmd) {
  if (!cmd || !*cmd) return true;
  return m1125_command_matches(cmd, "M600") || m1125_command_matches(cmd, "M1125");
}
// Forward declarations for Continue action helpers (defined later)
void M1125_TimeoutContinueAction();
void M1125_TimeoutContinueRecovery();
// Saved heater targets only needed when heater-idle timers are used. Prefer
// the global HEATER_IDLE_HANDLER unless the local override is enabled.
#if HEATER_IDLE_HANDLER && !M1125_USE_LOCAL_HEATER_IDLE
  #if HAS_HOTEND
    static celsius_t m1125_saved_target_hotend[HOTENDS] = { 0 };
  #endif
  #if HAS_HEATED_BED
    static celsius_t m1125_saved_target_bed = 0;
  #endif
#else
// Minimal, local heater-idle timer implementation for builds that do
// not enable HEATER_IDLE_HANDLER. This gives M1125 a self-contained
// timeout/timed_out API so the DGUS popup polling functions work
// without pulling in the full advanced-pause/probing heater code.
struct LocalIdleTimer {
  millis_t deadline_ms{0};
  bool active{false};
  bool timed_out{false};

  void start(millis_t ms) {
    deadline_ms = millis() + ms;
    active = true;
    timed_out = false;
  }

  void reset() {
    active = false;
    timed_out = false;
    deadline_ms = 0;
  }

  // Should be called periodically by M1125_CheckAndHandleHeaterTimeout
  void update() {
    if (!active || timed_out) return;
    if (ELAPSED(millis(), deadline_ms)) {
      timed_out = true;
      active = false;
    }
  }
};

  #if HAS_HOTEND
    static LocalIdleTimer m1125_local_hotend_idle[HOTENDS] = {};
  #endif
  #if HAS_HEATED_BED
    static LocalIdleTimer m1125_local_bed_idle = {};
  #endif

  #if HAS_HOTEND
    static celsius_t m1125_saved_target_hotend[HOTENDS] = { 0 };
  #endif
  #if HAS_HEATED_BED
    static celsius_t m1125_saved_target_bed = 0;
  #endif
#endif

#if ENABLED(POWER_LOSS_RECOVERY)
  #include "../../feature/powerloss.h"
#endif

#if ENABLED(HOST_ACTION_COMMANDS)
  #include "../../feature/host_actions.h"
#endif

#include "../../feature/print_source.h"

// Define as a GcodeSuite method like other gcode handlers
void GcodeSuite::M1125() {
  // Parameter P = pause, R = resume
  const bool hasP = parser.seen('P');
  const bool hasR = parser.seen('R');

  // (m1125 state kept at file scope)

  if (hasP && !hasR) {
    // --- PAUSE / PARK ---
    const bool sd_printing = card.isFileOpen() && card.isStillPrinting();

    // If M1125 already owns the paused state, ignore duplicate pauses.
    if (m1125_pause_active) {
      SERIAL_ECHOLNPGM("[DEBUG] M1125: pause requested but pause already active - ignoring");
      return;
    }

    // Canonical print-source: mark where this print came from so UI can
    // choose the correct Host-vs-SD paused screen.
    if (sd_printing) {
      PrintSource::set_printing_from_sd();
    }
    else {
      PrintSource::set_printing_from_host();
    }

  // Suppress DGUS popup-to-pause-response mapping while M1125 owns pause state
  // This prevents DGUS handlers from calling ExtUI::setPauseMenuResponse()
  // and thereby entering Marlin's advanced pause handshake while we're using
  // the deterministic M1125 flow.
#if ENABLED(DGUS_LCD_UI_CR6_COMM)
  DGUSScreenHandler::SetSuppressPopupPauseResponse(true);
#endif

  // Suppress any automatic starting of the print job timer while M1125
  // has taken manual control of pause state.
  M1125_SuppressAutoJobTimer();

  // DEBUG: Trace entering pause path and SD state
  SERIAL_ECHOLNPGM("[DEBUG] M1125: pause requested");
  SERIAL_ECHOLNPGM("[DEBUG] card.isFileOpen()", card.isFileOpen());
  SERIAL_ECHOLNPGM("[DEBUG] card.isStillPrinting()", card.isStillPrinting());

  // IMPORTANT: use the direct pause sequence instead of calling Marlin's
  // `pause_print()` here. The CR6/DGUS UI implements its own two-button
  // confirm and popup flow and Marlin's ADVANCED_PAUSE_FEATURE (pause_mode)
  // interacts poorly with that UI. Calling `pause_print()` would hand the
  // pause to Marlin's pause-mode machinery (prompts, pause_menu_response,
  // and resume paths) and can re-enable behavior we intentionally avoid.
  // Keep the direct sequence (card pause, save position, park, timer pause,
  // host prompts, etc.) so the CR6 flow stays deterministic and does not
  // resurrect Marlin's advanced pause handshake.
    // If printing from SD, stop the SD print flag and remove any already-
    // prefetched/committed SD commands from the ring buffer so they do not
    // execute after we return from the pause handler. This implements the
    // "rewind-and-clear" strategy: save file index, pause, clear queue,
    // then restore the file index so resume can continue from the same
    // location later.
    if (sd_printing) {
      const uint32_t saved_sd_index = card.getIndex();
      SERIAL_ECHOLNPAIR("[DEBUG] M1125: saving sdpos = ", saved_sd_index);
      // Prevent further SD reads
      card.pauseSDPrint();
      // Copy any already-committed commands out of the ring buffer so we
      // can restore them on resume. This avoids losing commands that were
      // pre-fetched from the SD file before we paused.
      planner.synchronize();
      {
        const uint8_t start = queue.ring_buffer.index_r;
        const uint8_t len = queue.ring_buffer.length;
        m1125_saved_cmd_count = 0;
        uint8_t filtered_cmds = 0;
        for (uint8_t i = 0; i < len; ++i) {
          uint8_t pos = start + i;
          if (pos >= BUFSIZE) pos -= BUFSIZE;
          const GCodeQueue::CommandLine &src = queue.ring_buffer.commands[pos];
          if (m1125_should_skip_saved_command(src.buffer)) {
            if (!filtered_cmds) PORT_REDIRECT(SerialMask::All);
            ++filtered_cmds;
            SERIAL_ECHOPGM("[DEBUG] M1125: filtering saved SD cmd -> ");
            SERIAL_ECHOLN(src.buffer);
            continue;
          }
          if (m1125_saved_cmd_count < BUFSIZE)
            m1125_saved_commands[m1125_saved_cmd_count++] = src;
        }

        queue.ring_buffer.clear();
      }
      // Restore the file position so resume will restart from the same place
      card.setIndex(saved_sd_index);
      if (m1125_saved_cmd_count) {
        PORT_REDIRECT(SerialMask::All);
        SERIAL_ECHOLNPAIR("[DEBUG] M1125: preserved ", m1125_saved_cmd_count);
        SERIAL_ECHOLNPGM(" queued SD commands for resume");
        SERIAL_ECHOLNPAIR("[DEBUG] M1125: saved ", m1125_saved_cmd_count, " SD command(s) for resume:");
        for (uint8_t i = 0; i < m1125_saved_cmd_count; ++i) {
          SERIAL_ECHOPGM("  [");
          SERIAL_ECHO(i);
          SERIAL_ECHOPGM("] ");
          SERIAL_ECHOLN(m1125_saved_commands[i].buffer);
        }
      }
      else {
        SERIAL_ECHOLNPGM("[DEBUG] M1125: no SD commands preserved (all filtered)");
      }
    }
    print_job_timer.pause();
    // Force the DGUS CR6 UI to show the paused screen immediately so the
    // display does not remain on the host-running view while we perform
    // the park and beep sequence. The stopwatch.pause() call triggers the
    // ExtUI callback in most UIs, but in some timing scenarios the display
    // update can lag behind; force the screen change for deterministic UX.
    #if ENABLED(DGUS_LCD_UI_CR6_COMM)
      DGUSScreenHandler::GotoScreen(PrintSource::printingFromHost() ? DGUSLCD_SCREEN_PRINT_PAUSED_HOST : DGUSLCD_SCREEN_PRINT_PAUSED);
    #endif
    // Save current position into a private M1125 slot to avoid clashes
    // with global SAVED_POSITIONS (G60/G61) usage by other code.
    m1125_saved_position = current_position;
    m1125_have_saved_position = true;
    SERIAL_ECHOLNPGM("[DEBUG] M1125: saved position X=", m1125_saved_position.x);
    SERIAL_ECHOLNPGM("[DEBUG] M1125: saved position Y=", m1125_saved_position.y);
    SERIAL_ECHOLNPGM("[DEBUG] M1125: saved position Z=", m1125_saved_position.z);
    SERIAL_ECHOLNPGM("[DEBUG] M1125: saved position E=", m1125_saved_position.e);

    // As soon as possible mark the job timer paused so the UI can reflect
    // the paused state earlier (prevents the display from still showing
    // PRINTING while we perform the park and beep sequence).


    // Notify UI: Parking
    ui.set_status_P(PSTR("Parking Nozzle..."));

  #if HAS_EXTRUDERS
    // Perform the retract + slight Z lift and XY wipe move synchronously
    // so the moves complete before we continue to the park. This mirrors
    // the injected sequence (G91; G1 E-2 Z0.2 F2400; G1 X5 Y5 F3000; G90)
    // but uses blocking firmware moves to guarantee ordering. The saved
    // `m1125_saved_position` was captured earlier and is intentionally
    // left unchanged so resume will restore the original print position
    // (and restore E for SD prints) as before.
    planner.synchronize();
    // Compute feedrates (do_blocking_move_to expects mm/s)
    const feedRate_t retract_feed_mm_s = MMM_TO_MMS(2400.0f);
    const feedRate_t wipe_feed_mm_s    = MMM_TO_MMS(3000.0f);

    // Capture current coordinates (saved position remains unchanged)
    const float cur_x = current_position[X_AXIS];
    const float cur_y = current_position[Y_AXIS];
    const float cur_z = current_position[Z_AXIS];
    const float cur_e = current_position[E_AXIS];

    SERIAL_ECHOLNPGM("[DEBUG] M1125: pre-retract Z=", cur_z, " E=", cur_e);

    // Retract E by 2 mm while raising Z by 0.2 mm
    do_blocking_move_to(xyze_pos_t{ cur_x, cur_y, cur_z + 0.2f, cur_e - 2.0f }, retract_feed_mm_s);
    planner.synchronize();

    // Move to wipe point (X5 Y5) at the requested feedrate; keep current Z/E
    do_blocking_move_to(xyze_pos_t{ 5.0f, 5.0f, current_position[Z_AXIS], current_position[E_AXIS] }, wipe_feed_mm_s);
    planner.synchronize();

    SERIAL_ECHOLNPGM("[DEBUG] M1125: post-wipe Z=", current_position[Z_AXIS], " E=", current_position[E_AXIS]);
  #endif

    #if ENABLED(NOZZLE_PARK_FEATURE)
        // Park the nozzle using Nozzle::park (z_action 0 = use park defaults)
        Nozzle::park(0, NOZZLE_PARK_POINT);
    #else
        // If nozzle park not available, do a conservative coordinate move: raise Z
        planner.synchronize();
        const float zraise = NOZZLE_PARK_Z_RAISE_MIN;
        do_blocking_move_to(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS] + zraise, current_position[E_AXIS]);
    #endif

    // Beep 6 times if possible (best-effort). Use ScreenHandler/BUZZ like other gcode files.
    // Add a short safe_delay between beeps so they are audible as separate tones
    for (uint8_t i = 0; i < 6; ++i) {
      // BUZZ takes (duration, frequency)
      BUZZ(200, 100);
      // 200 ms gap between beeps to make them distinct. Use safe_delay so
      // background tasks and watchdog handling continue to run.
      safe_delay(200);
    }


    ui.set_status_P(PSTR("Nozzle Parked."));

    // Start heater idle timers and save current targets so we can
    // re-apply them on resume if the heaters are automatically disabled
    // by the pause timeout. Use the same timeout value as advanced pause.
    #if HEATER_IDLE_HANDLER && !M1125_USE_LOCAL_HEATER_IDLE
      const millis_t nozzle_timeout = SEC_TO_MS(PAUSE_PARK_NOZZLE_TIMEOUT);
      HOTEND_LOOP() {
        m1125_saved_target_hotend[e] = thermalManager.degTargetHotend(e);
        thermalManager.heater_idle[e].start(nozzle_timeout);
      }
    #if HAS_HEATED_BED
      m1125_saved_target_bed = thermalManager.degTargetBed();
      thermalManager.heater_idle[thermalManager.IDLE_INDEX_BED].start(nozzle_timeout);
    #endif
    #else
      // Local idle timers: save targets and start simple per-heater timers
      const millis_t nozzle_timeout = SEC_TO_MS(PAUSE_PARK_NOZZLE_TIMEOUT);
    #if HAS_HOTEND
      HOTEND_LOOP() {
        m1125_saved_target_hotend[e] = thermalManager.degTargetHotend(e);
        m1125_local_hotend_idle[e].start(nozzle_timeout);
      }
    #endif
    #if HAS_HEATED_BED
      m1125_saved_target_bed = thermalManager.degTargetBed();
      m1125_local_bed_idle.start(nozzle_timeout);
    #endif
    #endif // HEATER_IDLE_HANDLER

    // Mark M1125 pause active so background watcher can act on timeouts
    m1125_pause_active = true;
    m1125_heaters_disabled_by_timeout = false;
    // If we paused an SD print then do additional SD-specific actions
    if (sd_printing) {
      #if ENABLED(POWER_LOSS_RECOVERY) && DISABLED(DGUS_LCD_UI_MKS)
        if (recovery.enabled) recovery.save(true);
      #endif

  // Avoid calling Marlin's `reset_status()` for the CR6 DGUS UI here.
  // `ui.reset_status()` queries printingIsPaused() which becomes true
  // because we paused the job timer above; that causes Marlin to set
  // the generic "Print Paused" message and overwrite the "Nozzle Parked."
  // M1125 must keep its own status string visible, so skip the reset
  // when the DGUS CR6 UI is in use.
    #if DISABLED(DGUS_LCD_UI_CR6_COMM)
      IF_DISABLED(DWIN_CREALITY_LCD, ui.reset_status());
    #else
      /* Intentionally omitted for DGUS CR6 UI to preserve "Nozzle Parked." */
    #endif

    #if ENABLED(HOST_ACTION_COMMANDS)
      // Notify host prompt support if enabled, but only call HostUI::pause()
      // for host-driven pauses. SD-driven pauses should not flip the
      // canonical PrintSource to HOST nor inject host pause actions.
      TERN_(HOST_PROMPT_SUPPORT, hostui.prompt_open(PROMPT_PAUSE_RESUME, F("Pause SD"), F("Resume")));
      #ifdef ACTION_ON_PAUSE
        if (!sd_printing) {
          hostui.pause();
        }
      #endif
    #endif
    }
    return;
  }

  if (hasR && !hasP) {
    // --- RESUME ---
    ui.set_status_P(PSTR("Resuming print..."));

    if (m1125_have_saved_position) {
      // Don't move the nozzle back immediately on RESUME. Re-apply heater
      // targets first (below) and defer any motion until heaters reach the
      // requested targets. The poll helper `m1125_poll_resume()` will
      // finalize the resume and perform the saved-position restore once
      // temperatures are within the allowed window.
      SERIAL_ECHOLNPGM("[DEBUG] M1125: resume requested; saved position available X=", m1125_saved_position.x);
      SERIAL_ECHOLNPGM("[DEBUG] M1125: resume requested; saved position Y=", m1125_saved_position.y);
      SERIAL_ECHOLNPGM("[DEBUG] M1125: resume requested; saved position Z=", m1125_saved_position.z);
      // Leave m1125_have_saved_position true until the poll finalizes the resume
      // so the finalizer knows to restore the nozzle position at the right time.
    }

    // Optional resume feedrate parameter (F) in mm/min. Convert to mm/s for
    // do_blocking_move_to which expects mm/s. If F is not provided, use the
    // stored default value.
    if (parser.seenval('F')) {
      const float feed_mm_per_min = parser.value_float();
      m1125_resume_feedrate_mm_s = MMM_TO_MMS(feed_mm_per_min);
      SERIAL_ECHOLNPAIR("[DEBUG] M1125: resume feedrate set to mm/s=", m1125_resume_feedrate_mm_s);
    }

    // If M1125 previously started idle timers, reset them and restore
    // saved heater targets if we auto-disabled them due to timeout.
    // Reset idle timers and (re-)apply saved heater targets *before* resuming.
    // We re-apply saved targets unconditionally (if non-zero) so the printer
    // always returns to the same thermal state that was present when paused.
    #if HEATER_IDLE_HANDLER && !M1125_USE_LOCAL_HEATER_IDLE
      HOTEND_LOOP() {
        thermalManager.reset_hotend_idle_timer(e);
        if (m1125_saved_target_hotend[e] > 0)
          thermalManager.setTargetHotend(m1125_saved_target_hotend[e], e);
      }
    #if HAS_HEATED_BED
      thermalManager.reset_bed_idle_timer();
      if (m1125_saved_target_bed > 0)
        thermalManager.setTargetBed(m1125_saved_target_bed);
    #endif
    #else
      // Local idle timers: reset and re-apply saved targets (if any)
      #if HAS_HOTEND
        HOTEND_LOOP() {
          m1125_local_hotend_idle[e].reset();
          if (m1125_saved_target_hotend[e] > 0)
            thermalManager.setTargetHotend(m1125_saved_target_hotend[e], e);
        }
      #endif
      #if HAS_HEATED_BED
        m1125_local_bed_idle.reset();
        if (m1125_saved_target_bed > 0)
          thermalManager.setTargetBed(m1125_saved_target_bed);
      #endif
    #endif // HEATER_IDLE_HANDLER

    // Non-blocking resume: set state so the periodic poll will wait for
    // heaters to reach their targets and finalize resume when ready.
    // NOTE: Do NOT clear `m1125_pause_active` or start/resume the job here.
    // The periodic poll `M1125_CheckAndHandleHeaterTimeout()` (which calls
    // `m1125_poll_resume()`) is responsible for finalizing the resume once
    // saved heater targets are reached. Leaving `m1125_pause_active` true
    // preserves the M1125-owned pause semantics (including suppression of
    // the auto-job timer) until resume is completed.
    m1125_resume_pending = true;
    // Decide whether to resume SD printing or host-controlled printing when ready.
    // Use the canonical PrintSource set at pause time rather than the current
    // card state. A file may be open (card.isFileOpen()) even when the active
    // print was driven by the host; using PrintSource prevents incorrectly
    // restoring E (extruder) when resuming a host-controlled print.
    m1125_resume_do_sd = PrintSource::printingFromSDCard();
    m1125_resume_do_host = PrintSource::printingFromHost();

    // Do not perform any immediate resume/start actions here. The poll helper
    // will perform the actual resume (startOrResumeFilePrinting, ui.resume_print,
    // print_job_timer.start()) once temperatures match the saved targets.
    // Trigger an immediate poll of the resume state machine so host-driven
    // resumes (for example via OctoPrint) complete even when the DGUS UI
    // polling path is not actively calling the timeout helper. This ensures
    // resume finalization (clearing m1125_pause_active) happens promptly
    // when temperatures are already at target.
    M1125_CheckAndHandleHeaterTimeout();
  return;
  }

  // If neither P nor R provided, print usage
  SERIAL_ECHO_START(); SERIAL_ECHOLN("Usage: M1125 P  (pause/park)  or M1125 R  (resume)");
}

// Internal helper: post the DGUS popup that prompts the user (programmable popup)
static void m1125_post_timeout_popup() {
#if ENABLED(DGUS_LCD_UI_CR6_COMM)
  // Replace the single-button popup with a Confirm dialog (Screen#66)
  // so the user gets a YES/NO choice. Populate the Confirm message
  // fields (VP_MSGSTR1..4) per the requested design.
  DGUSScreenHandler::SetSuppressPopupPauseResponse(true);

  char line1[VP_MSGSTR1_LEN + 1] = {0};
  char line2[VP_MSGSTR2_LEN + 1] = {0};
  char line3[VP_MSGSTR3_LEN + 1] = {0};
  char line4[VP_MSGSTR4_LEN + 1] = {0};

  // Compute remaining seconds until deadline (guard for underflow)
  long diff = (long)(m1125_timeout_deadline_ms - millis());
  const uint32_t rem = (diff > 0) ? ((uint32_t)((diff + 999) / 1000)) : 0;
  const uint32_t interval = PAUSE_PARK_NOZZLE_TIMEOUT;

  // VP_MSGSTR1 = "in xx seconds"
  snprintf(line1, sizeof(line1), "in %u seconds", (unsigned)rem);
  // VP_MSGSTR2 = "Extend timeout"
  snprintf(line2, sizeof(line2), "Extend timeout");
  // VP_MSGSTR3 = "by yy seconds?"
  snprintf(line3, sizeof(line3), "by %u seconds?", (unsigned)interval);
  // VP_MSGSTR4 = "Heaters Timeout"
  snprintf(line4, sizeof(line4), "Heaters Timeout");

  // Use an existing virtual Confirm VP so the Confirm screen is shown.
  // The Confirm handlers will route the YES/NO result back through
  // ScreenConfirmedOK/ScreenChangeHook where we already handle Continue.
  DGUSScreenHandler::HandleUserConfirmationPopUp(VP_M1125_TIMEOUT_CONFIRM, line1, line2, line3, line4, false, false, false, false);
  // Mark that the UI is waiting so Confirm handlers are engaged.
  wait_for_user = true;
#endif
}

// Returns true if this call disabled heaters (so caller can update UI)
bool M1125_CheckAndHandleHeaterTimeout() {
  if (!m1125_pause_active) return false;

  bool any_timed_out = false;
  #if HEATER_IDLE_HANDLER && !M1125_USE_LOCAL_HEATER_IDLE
    HOTEND_LOOP() any_timed_out |= thermalManager.heater_idle[e].timed_out;
    #if HAS_HEATED_BED
      any_timed_out |= thermalManager.heater_idle[thermalManager.IDLE_INDEX_BED].timed_out;
    #endif
  #else
  // Update and check local idle timers
  #if HAS_HOTEND
    HOTEND_LOOP() {
      m1125_local_hotend_idle[e].update();
      any_timed_out |= m1125_local_hotend_idle[e].timed_out;
    }
  #endif
  #if HAS_HEATED_BED
    m1125_local_bed_idle.update();
    any_timed_out |= m1125_local_bed_idle.timed_out;
  #endif
  #endif

  // If a heater-idle timeout just occurred, and we haven't already shown the
  // popup, set the pending state and schedule the final disable after the
  // configured interval.
  if (any_timed_out && !m1125_heaters_disabled_by_timeout && !m1125_timeout_pending) {
    m1125_timeout_pending = true;
    // Start a short grace window during which the user can press Continue.
    m1125_timeout_deadline_ms = millis() + (M1125_TIMEOUT_GRACE_SECONDS * 1000UL);
    m1125_continue_pressed = false;
    m1125_timeout_old_remaining_at_continue = 0;
    // Post the popup to the DGUS display (best-effort)
    m1125_post_timeout_popup();
    return false;
  }

  // If pending and deadline passed, disable heaters now as the safe fallback.
  if (m1125_timeout_pending && !m1125_heaters_disabled_by_timeout && ELAPSED(millis(), m1125_timeout_deadline_ms)) {
    // Before disabling, refresh our saved targets from the current
    // thermalManager state. This covers the case where the user changed
    // targets via the UI while paused and then allowed the timeout to
    // elapse; we want Continue to re-apply the *latest* requested values.
    #if HAS_HOTEND
      HOTEND_LOOP() m1125_saved_target_hotend[e] = thermalManager.degTargetHotend(e);
    #endif
    #if HAS_HEATED_BED
      m1125_saved_target_bed = thermalManager.degTargetBed();
    #endif

    thermalManager.disable_all_heaters();
    m1125_heaters_disabled_by_timeout = true;
    m1125_timeout_pending = false;
    m1125_continue_pressed = false;
    // Also process any pending resume requests while we're polled.
    m1125_poll_resume();
    return true;
  }

  // Poll the resume state machine in the common polling path so the
  // resume can finalize even when no timeout action happened this call.
  m1125_poll_resume();

  return false;
}

// Called each poll to advance any pending resume state machine. Returns true
// if resume was completed here (so caller can update UI if needed).
static bool m1125_poll_resume() {
  if (!m1125_resume_pending) return false;

  // Check hotends and bed: if any saved target (we re-applied earlier) is not
  // yet reached, keep waiting. Use the same TEMP_WINDOW used elsewhere.
  #if HAS_HOTEND
    HOTEND_LOOP() {
      // If no target (0) skip
      const celsius_t tgt = m1125_saved_target_hotend[e];
      if (tgt > 0) {
        const int whole = thermalManager.wholeDegHotend(e);
        if (ABS(whole - int(tgt)) > (TEMP_WINDOW)) {
          // Still heating: update UI so user sees that resume is pending
          // because we are waiting for the heaters to reach target.
          ui.set_status_P(PSTR("Resuming print... waiting for heater..."));
          return false; // still heating
        }
      }
    }
  #endif
  #if HAS_HEATED_BED
    if (m1125_saved_target_bed > 0) {
      const int whole_bed = thermalManager.wholeDegBed();
      if (ABS(whole_bed - int(m1125_saved_target_bed)) > (TEMP_BED_WINDOW)) {
        ui.set_status_P(PSTR("Resuming print... waiting for heater..."));
        return false;
      }
    }
  #endif

  // All targets reached (or none were set). Finalize resume.
  m1125_resume_pending = false;

  // Restore saved position now that heaters have reached their targets.
  if (m1125_have_saved_position) {
    SERIAL_ECHOLNPGM("[DEBUG] M1125: finalizing resume - restoring position X=", m1125_saved_position.x);
    SERIAL_ECHOLNPGM("[DEBUG] M1125: finalizing resume - restoring position Y=", m1125_saved_position.y);
    SERIAL_ECHOLNPGM("[DEBUG] M1125: finalizing resume - restoring position Z=", m1125_saved_position.z);
    planner.synchronize();
    // Use user-specified resume feedrate (mm/s) when moving back from the
    // nozzle park point to the saved print position. Use the xyze overload
    // to avoid macro/overload ambiguity.
    // Extra debug: log current vs saved before the blocking move so we can
    // diagnose cases where Z is not restored.
    PORT_REDIRECT(SerialMask::All);
    SERIAL_ECHOLNPGM("[DEBUG] M1125: about to move -> saved_pos = ", m1125_saved_position.x, ", ", m1125_saved_position.y, ", ", m1125_saved_position.z);
    SERIAL_ECHOLNPGM("[DEBUG] M1125: about to move -> current_pos = ", current_position[X_AXIS], ", ", current_position[Y_AXIS], ", ", current_position[Z_AXIS]);
    do_blocking_move_to(xyze_pos_t{ m1125_saved_position.x, m1125_saved_position.y, m1125_saved_position.z, current_position[E_AXIS] }, m1125_resume_feedrate_mm_s);
    planner.synchronize();
    PORT_REDIRECT(SerialMask::All);
    SERIAL_ECHOLNPGM("[DEBUG] M1125: after restore move - current_pos = ", current_position[X_AXIS], ", ", current_position[Y_AXIS], ", ", current_position[Z_AXIS]);
    // Restore extruder position (E) - account for planner/extruder states.
    const float saved_e = m1125_saved_position.e;
    // If we are resuming an SD print, restore the saved E so SD file
    // absolute extrusion positions continue correctly. If we are
    // resuming a host-controlled print (OctoPrint/Host commands may
    // have changed E while paused), skip restoring E to avoid forcing
    // a large absolute extruder correction that can cause motor noise.
    if (m1125_resume_do_sd) {
      current_position[E_AXIS] = saved_e;
      planner.set_e_position_mm(saved_e);
    }
    else {
      // Host resume: leave current_position[E_AXIS] as-is. Log for debug.
      SERIAL_ECHOLNPGM("[DEBUG] M1125: host resume - skipping E restore");
    }
    m1125_have_saved_position = false;
  }

  // Clear suppression now that resume is actually completed so the
  // temperature module can auto-start the job timer again if appropriate.
  M1125_ClearAutoJobTimerSuppress();

  // Clear saved targets now that they are satisfied
  #if HAS_HOTEND
    HOTEND_LOOP() m1125_saved_target_hotend[e] = 0;
  #endif
  #if HAS_HEATED_BED
    m1125_saved_target_bed = 0;
  #endif

  // Perform the actual resume actions that were deferred earlier.
  if (m1125_resume_do_sd) {
    if (!card.isStillPrinting()) {
      // Before resuming SD printing, restore any commands that were
      // preserved from the ring buffer at pause-time so they execute in
      // the original order and no commands are lost.
      if (m1125_saved_cmd_count) {
          PORT_REDIRECT(SerialMask::All);
          for (uint8_t i = 0; i < m1125_saved_cmd_count; ++i) {
            // Copy saved command back into the ring buffer at the write pos
            const uint8_t wp = queue.ring_buffer.index_w;
            queue.ring_buffer.commands[wp] = m1125_saved_commands[i];
            queue.ring_buffer.advance_w();

            SERIAL_ECHOPGM("[DEBUG] M1125: restoring saved SD cmd[");
            SERIAL_ECHO(i);
            SERIAL_ECHOPGM("] -> ");
            SERIAL_ECHOLN(m1125_saved_commands[i].buffer);
          }
        SERIAL_ECHOLNPAIR("[DEBUG] M1125: restored ", m1125_saved_cmd_count);
        SERIAL_ECHOLNPGM(" queued SD commands on resume");
        m1125_saved_cmd_count = 0;
      }
      // Mark canonical source and resume SD print
      PrintSource::set_printing_from_sd();
      card.startOrResumeFilePrinting();
      startOrResumeJob();
    }
  }

  if (m1125_resume_do_host) {
    // Mark canonical source and resume host-driven print
    PrintSource::set_printing_from_host();
    ui.resume_print();
  }

  // Restart job timer so UI and status logic return to running state.
  print_job_timer.start();

  // Re-enable DGUS popup mapping if needed
  #if ENABLED(DGUS_LCD_UI_CR6_COMM)
    DGUSScreenHandler::SetSuppressPopupPauseResponse(false);
  #endif

  // Clear pause bookkeeping
  m1125_pause_active = false;
  m1125_heaters_disabled_by_timeout = false;

  // Clear status
  ui.set_status_P(PSTR(""));

  return true;
}

// Helper getters for DGUS UI polling -------------------------------------------------
uint32_t M1125_TimeoutRemainingSeconds() {
  if (!m1125_timeout_pending) return 0;
  const long diff = (long)(m1125_timeout_deadline_ms - millis());
  return (diff > 0) ? ((uint32_t)((diff + 999) / 1000)) : 0;
}

uint32_t M1125_TimeoutOldRemainingAtContinue() { return m1125_timeout_old_remaining_at_continue; }

uint32_t M1125_TimeoutIntervalSeconds() { return PAUSE_PARK_NOZZLE_TIMEOUT; }

// Called when the DGUS popup Continue button is pressed for the heater-timeout popup
void M1125_TimeoutContinue() {
  // If there's no pending timeout but heaters were disabled earlier by the
  // timeout, treat Continue as a recovery request so we re-apply saved
  // heater targets. This covers the case where the popup was shown and the
  // user waited until the heaters were disabled; pressing Continue should
  // restart heating.
  if (!m1125_timeout_pending) {
    if (m1125_heaters_disabled_by_timeout) {
      M1125_TimeoutContinueRecovery();
    }
    return;
  }
  // Capture remaining seconds at the moment Continue was pressed (this is the short-grace remaining)
  m1125_timeout_old_remaining_at_continue = M1125_TimeoutRemainingSeconds();
  // Add the configured full timeout interval to the captured remaining value
  m1125_timeout_old_remaining_at_continue += PAUSE_PARK_NOZZLE_TIMEOUT;
  // Extend the deadline by one full timeout interval (user chose to restart timers)
  m1125_timeout_deadline_ms += SEC_TO_MS(PAUSE_PARK_NOZZLE_TIMEOUT);
  m1125_continue_pressed = true;
  // Inform the user the timers were extended so the UI provides feedback
  // Use the UI status string to preserve DGUS "Nozzle Parked." text if possible
  ui.set_status_P(PSTR("Heater timers extended."));
  // Debug: show remaining and new deadline for logging
  SERIAL_ECHOLNPAIR("M1125: Continue pressed, old remaining(s)=", m1125_timeout_old_remaining_at_continue - PAUSE_PARK_NOZZLE_TIMEOUT);
  SERIAL_ECHOLNPAIR("M1125: extended deadline ms=", m1125_timeout_deadline_ms);
}

// (actual implementations appear later in this file)

// Public entry used by DGUS Continue action. This will attempt to recover
// from either a pending timeout (extending it) or from already-disabled
// heaters (re-applying targets and restarting timers).
void M1125_TimeoutContinueAction() {
  // First behave as the pre-existing Continue (extend deadline if pending)
  M1125_TimeoutContinue();
  // Then, if heaters were disabled previously, re-enable them
  M1125_TimeoutContinueRecovery();
}

void M1125_TimeoutContinueRecovery() {

  // Re-apply saved targets and restart timers based on global/local mode.
  #if HEATER_IDLE_HANDLER && !M1125_USE_LOCAL_HEATER_IDLE
    const millis_t nozzle_timeout = SEC_TO_MS(PAUSE_PARK_NOZZLE_TIMEOUT);
    #if HAS_HOTEND
      HOTEND_LOOP() {
        if (m1125_saved_target_hotend[e] > 0) {
          thermalManager.setTargetHotend(m1125_saved_target_hotend[e], e);
        }
        thermalManager.heater_idle[e].start(nozzle_timeout);
      }
    #endif
    #if HAS_HEATED_BED
      if (m1125_saved_target_bed > 0) thermalManager.setTargetBed(m1125_saved_target_bed);
      thermalManager.heater_idle[thermalManager.IDLE_INDEX_BED].start(nozzle_timeout);
    #endif
  #else
    const millis_t nozzle_timeout = SEC_TO_MS(PAUSE_PARK_NOZZLE_TIMEOUT);
    #if HAS_HOTEND
      HOTEND_LOOP() {
        if (m1125_saved_target_hotend[e] > 0) {
          SERIAL_ECHOLNPAIR("[DEBUG] M1125: re-applying saved hotend target[", e);
          SERIAL_ECHOLNPAIR("] = ", m1125_saved_target_hotend[e]);
          thermalManager.setTargetHotend(m1125_saved_target_hotend[e], e);
        }
        m1125_local_hotend_idle[e].start(nozzle_timeout);
      }
    #endif
    #if HAS_HEATED_BED
      if (m1125_saved_target_bed > 0) {
        SERIAL_ECHOLNPAIR("[DEBUG] M1125: re-applying saved bed target = ", m1125_saved_target_bed);
        thermalManager.setTargetBed(m1125_saved_target_bed);
      }
      m1125_local_bed_idle.start(nozzle_timeout);
    #endif
  #endif


  // Mark heaters as no longer disabled by timeout
  m1125_heaters_disabled_by_timeout = false;

  // The UI/popup state should be cleared; if a popup was showing, leave it to
  // the DGUS handler to close on Continue. We don't manipulate m1125_timeout_pending
  // here because Continue already caused the popup handler to call the original
  // M1125_TimeoutContinue() which extended the deadline.
}

// Minimal accessor for other modules (debug/instrumentation) to detect
// whether M1125 currently owns the paused state. Used only for logging
// and debugging; keeps linkage internal to this translation unit.
bool M1125_IsPauseActive() { return m1125_pause_active; }

// Abort/clear any M1125 pause state. Called when a print is cancelled
// or aborted so leftover timers/pending resume state do not present
// heater-timeout popups or leave M1125 in a half-active state.
void M1125_AbortPause() {
  // Clear pause bookkeeping and pending resume
  m1125_pause_active = false;
  m1125_resume_pending = false;
  m1125_have_saved_position = false;
  m1125_saved_cmd_count = 0;
  m1125_timeout_pending = false;
  m1125_heaters_disabled_by_timeout = false;

  // Reset local idle timers and saved targets
  #if HEATER_IDLE_HANDLER && !M1125_USE_LOCAL_HEATER_IDLE
    HOTEND_LOOP() { m1125_saved_target_hotend[e] = 0; thermalManager.reset_hotend_idle_timer(e); }
    #if HAS_HEATED_BED
      m1125_saved_target_bed = 0; thermalManager.reset_bed_idle_timer();
    #endif
  #else
    #if HAS_HOTEND
      HOTEND_LOOP() { m1125_local_hotend_idle[e].reset(); m1125_saved_target_hotend[e] = 0; }
    #endif
    #if HAS_HEATED_BED
      m1125_local_bed_idle.reset(); m1125_saved_target_bed = 0;
    #endif
  #endif

  // Clear suppression and any UI hook that was preventing popup responses
  M1125_ClearAutoJobTimerSuppress();
  #if ENABLED(DGUS_LCD_UI_CR6_COMM)
    DGUSScreenHandler::SetSuppressPopupPauseResponse(false);
  #endif
  ui.set_status_P(PSTR(""));

  // Clear saved command buffer (m1125_saved_cmd_count is set to 0 above so
  // the saved buffer will be ignored). Avoid calling methods on CommandLine
  // whose API may be internal to the ring buffer implementation.

  // Debug
  PORT_REDIRECT(SerialMask::All);
  SERIAL_ECHOLNPGM("[DEBUG] M1125: AbortPause() called - M1125 state cleared");
}

