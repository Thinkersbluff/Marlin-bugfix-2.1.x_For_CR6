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
#include "../../module/planner.h"
#include "../../module/stepper.h"
#include "../../module/motion.h"
#include "../../libs/nozzle.h"
#include "../../MarlinCore.h"
#include "../../lcd/marlinui.h" // for UI / BUZZ / ScreenHandler
#include "../../libs/buzzer.h"

#if ENABLED(POWER_LOSS_RECOVERY)
  #include "../../feature/powerloss.h"
#endif

#if ENABLED(HOST_ACTION_COMMANDS)
  #include "../../feature/host_actions.h"
#endif

// Define as a GcodeSuite method like other gcode handlers
void GcodeSuite::M1125() {
  // Parameter P = pause, R = resume
  const bool hasP = parser.seen('P');
  const bool hasR = parser.seen('R');

  if (hasP && !hasR) {
    // --- PAUSE / PARK ---
    const bool sd_printing = card.isFileOpen() && card.isStillPrinting();

  // IMPORTANT: use the direct pause sequence instead of calling Marlin's
  // `pause_print()` here. The CR6/DGUS UI implements its own two-button
  // confirm and popup flow and Marlin's ADVANCED_PAUSE_FEATURE (pause_mode)
  // interacts poorly with that UI. Calling `pause_print()` would hand the
  // pause to Marlin's pause-mode machinery (prompts, pause_menu_response,
  // and resume paths) and can re-enable behavior we intentionally avoid.
  // Keep the direct sequence (card pause, save position, park, timer pause,
  // host prompts, etc.) so the CR6 flow stays deterministic and does not
  // resurrect Marlin's advanced pause handshake.
    // If printing from SD, stop the SD print flag
    if (sd_printing) {
      card.pauseSDPrint();
    }

    // Save current position if SAVED_POSITIONS available (use slot 0)
#if SAVED_POSITIONS
    const uint8_t slot = 0;
    if (slot < SAVED_POSITIONS) {
      // store XYZ and E using motion stored positions
      stored_position[slot].x = current_position[X_AXIS];
      stored_position[slot].y = current_position[Y_AXIS];
      stored_position[slot].z = current_position[Z_AXIS];
      stored_position[slot].e = current_position[E_AXIS];
      did_save_position[slot] = true;
    }
#endif

    // Notify UI: Parking
  ui.set_status_P(PSTR("Parking Nozzle..."));

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
    for (uint8_t i = 0; i < 6; ++i) {
      // BUZZ takes (duration, frequency)
      BUZZ(200, 100);
    }

    ui.set_status_P(PSTR("Nozzle Parked."));

    // Pause the job timer so higher-level UI queries (isPrintingPaused)
    // observe the paused state for both SD and host-initiated prints.
    print_job_timer.pause();

    // If we paused an SD print then do additional SD-specific actions
    if (sd_printing) {
      #if ENABLED(POWER_LOSS_RECOVERY) && DISABLED(DGUS_LCD_UI_MKS)
        if (recovery.enabled) recovery.save(true);
      #endif

      IF_DISABLED(DWIN_CREALITY_LCD, ui.reset_status());

      #if ENABLED(HOST_ACTION_COMMANDS)
        TERN_(HOST_PROMPT_SUPPORT, hostui.prompt_open(PROMPT_PAUSE_RESUME, F("Pause SD"), F("Resume")));
        #ifdef ACTION_ON_PAUSE
          hostui.pause();
        #endif
      #endif
    }

    return;
  }

  if (hasR && !hasP) {
    // --- RESUME ---
  ui.set_status_P(PSTR("Resuming print..."));

#if SAVED_POSITIONS
    const uint8_t slot = 0;
    if (slot < SAVED_POSITIONS && did_save_position[slot]) {
      // Move nozzle back to stored XYZ (safely)
      planner.synchronize();
      do_blocking_move_to(stored_position[slot].x, stored_position[slot].y, stored_position[slot].z, current_position[E_AXIS]);

      // Restore extruder position (E) - account for planner/extruder states.
      const float saved_e = stored_position[slot].e;
      // Set the planner/current position E value to saved value
      current_position[E_AXIS] = saved_e;
      planner.set_e_position_mm(saved_e);
      did_save_position[slot] = false;
    }
#endif

    // Resume SD or host printing
  if (card.isFileOpen()) {
      // If file open and still not printing, resume SD printing
      if (!card.isStillPrinting()) {
        card.startOrResumeFilePrinting();
        startOrResumeJob();
      }
    }

    // Restart job timer so UI and status logic return to running state.
    print_job_timer.start();

    // If not SD printing, resume host-controlled jobs using the UI resume path.
    // Avoid calling advanced pause-mode resume_print() which can re-enter
    // Marlin's pause_mode handshake.
    if (!card.isFileOpen()) {
      ui.resume_print();
    }

    ui.set_status_P(PSTR(""));
    return;
  }

  // If neither P nor R provided, print usage
  SERIAL_ECHO_START(); SERIAL_ECHOLN("Usage: M1125 P  (pause/park)  or M1125 R  (resume)");
}
