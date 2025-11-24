/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "../inc/MarlinConfig.h"

#if ENABLED(HOST_ACTION_COMMANDS)

//#define DEBUG_HOST_ACTIONS

#include "host_actions.h"

#include <string.h>

#include "print_source.h"

#include "../MarlinCore.h"
#include "../gcode/queue.h"

#if ENABLED(ADVANCED_PAUSE_FEATURE)
  #include "pause.h"
  #include "../gcode/queue.h"
#endif

#if HAS_FILAMENT_SENSOR
  #include "runout.h"
#endif

HostUI hostui;

static constexpr millis_t HOSTUI_NOTIFICATION_SUPPRESS_WINDOW_MS = 5000UL;
static char hostui_last_notification[MAX_CMD_SIZE] = { 0 };
static millis_t hostui_last_notification_ms = 0;

static inline void hostui_store_notification(const char *message) {
  if (!message || !*message) {
    hostui_last_notification[0] = '\0';
    hostui_last_notification_ms = 0;
    return;
  }
  strlcpy(hostui_last_notification, message, sizeof(hostui_last_notification));
  hostui_last_notification_ms = millis();
}

static inline void hostui_store_notification_P(PGM_P message) {
  if (!message) {
    hostui_store_notification(nullptr);
    return;
  }
  char buffer[MAX_CMD_SIZE];
  uint8_t i = 0;
  for (; i < MAX_CMD_SIZE - 1; ++i) {
    const char c = pgm_read_byte(&message[i]);
    if (!c) break;
    buffer[i] = c;
  }
  buffer[i] = '\0';
  hostui_store_notification(buffer);
}


bool HostUI::should_suppress_m117(const char * const message) {
  if (!message || !*message) return false;
  if (!hostui_last_notification[0]) return false;
  if (!hostui_last_notification_ms) return false;
  if (millis() - hostui_last_notification_ms > HOSTUI_NOTIFICATION_SUPPRESS_WINDOW_MS)
    return false;
  if (strcmp(message, hostui_last_notification) != 0)
    return false;
  hostui_store_notification(nullptr);
  SERIAL_ECHOLNPGM("[DEBUG] HostUI: suppressed redundant M117 from host notification");
  return true;
}

void HostUI::action(FSTR_P const fstr, const bool eol) {
  // Suppress host-action messages for canonical SD prints. Many callers
  // (UI code, SD handlers) invoke HostUI hooks as a generic integration
  // point; when printing from SD we generally do NOT want to notify an
  // external host. This central guard silence all "//action:" lines for
  // SD-driven prints. If you need specific messages to pass through,
  // we can add a force parameter or a whitelist later.
  if (PrintSource::printingFromSDCard()) return;

  PORT_REDIRECT(SerialMask::All);
  SERIAL_ECHOPGM("//action:", fstr);
  if (eol) SERIAL_EOL();
}

#ifdef ACTION_ON_KILL
  void HostUI::kill() { action(F(ACTION_ON_KILL)); }
#endif
#ifdef ACTION_ON_PAUSE
void HostUI::pause(const bool eol/*=true*/) {
  // If the canonical print source is SD, this pause originates from
  // an SD-driven flow. Avoid emitting host-action commands in that
  // case so external listeners (OctoPrint, host integrations) are not
  // incorrectly notified for SD prints.
  if (PrintSource::printingFromSDCard()) return;

  action(F(ACTION_ON_PAUSE), eol);

  // Only mark canonical source as Host and inject an M1125 when a host
  // serial connection actually exists. If HostUI::pause() is invoked
  // by local UI code (or M1125 itself) without a host connected, we
  // must not flip the canonical PrintSource to HOST.
  bool host_serial_connected = false;
#if defined(MYSERIAL1)
  host_serial_connected = host_serial_connected || MYSERIAL1.connected();
#endif
#if defined(MYSERIAL2)
  host_serial_connected = host_serial_connected || MYSERIAL2.connected();
#endif
#if defined(MYSERIAL3)
  host_serial_connected = host_serial_connected || MYSERIAL3.connected();
#endif

  if (host_serial_connected) {
    // Only mark canonical source as Host when a host connection exists
    // and the current canonical source is not already SD. This avoids
    // accidental flips when UI code invokes host actions during an
    // SD-driven pause flow.
    if (!PrintSource::printingFromSDCard()) {
      PrintSource::set_printing_from_host();
    }
    PORT_REDIRECT(SerialMask::All);
    SERIAL_ECHOLNPGM("[DEBUG] HostUI::pause() invoked -> PrintSource::printingFromHost()=", PrintSource::printingFromHost());

    // Ensure host pause follows the deterministic M1125 pause/park flow so
    // the job timer is paused and the CR6 UI shows the paused screens.
    // Inject a single M1125 P command into the immediate command queue.
    queue.inject("M1125 P");
  }
}
#endif
#ifdef ACTION_ON_PAUSED
  void HostUI::paused(const bool eol/*=true*/) { action(F(ACTION_ON_PAUSED), eol); }
#endif
#ifdef ACTION_ON_RESUME
void HostUI::resume() {
  // Skip host notify when resuming an SD print; the SD path will
  // already perform the appropriate resume steps and we don't want
  // to confuse external host integrations.
  if (PrintSource::printingFromSDCard()) return;

  action(F(ACTION_ON_RESUME));

  // Only mark canonical source as Host when a host serial connection exists
  bool host_serial_connected = false;
#if defined(MYSERIAL1)
  host_serial_connected = host_serial_connected || MYSERIAL1.connected();
#endif
#if defined(MYSERIAL2)
  host_serial_connected = host_serial_connected || MYSERIAL2.connected();
#endif
#if defined(MYSERIAL3)
  host_serial_connected = host_serial_connected || MYSERIAL3.connected();
#endif

  if (host_serial_connected) {
    if (!PrintSource::printingFromSDCard()) {
      PrintSource::set_printing_from_host();
    }
    PORT_REDIRECT(SerialMask::All);
    SERIAL_ECHOLNPGM("[DEBUG] HostUI::resume() invoked -> PrintSource::printingFromHost()=", PrintSource::printingFromHost());
  }
}
#endif
#ifdef ACTION_ON_RESUMED
  void HostUI::resumed() { action(F(ACTION_ON_RESUMED)); }
#endif
#ifdef ACTION_ON_CANCEL
  void HostUI::cancel() { action(F(ACTION_ON_CANCEL)); }
#endif
#ifdef ACTION_ON_START
void HostUI::start() {
  // Don't announce starts to host listeners when a canonical SD print
  // is active — keep host notifications limited to actual host-driven
  // jobs.
  if (PrintSource::printingFromSDCard()) return;

  action(F(ACTION_ON_START));

  // Host-initiated start — mark canonical source
  // Only mark canonical source as Host when a host serial connection exists
  bool host_serial_connected = false;
#if defined(MYSERIAL1)
  host_serial_connected = host_serial_connected || MYSERIAL1.connected();
#endif
#if defined(MYSERIAL2)
  host_serial_connected = host_serial_connected || MYSERIAL2.connected();
#endif
#if defined(MYSERIAL3)
  host_serial_connected = host_serial_connected || MYSERIAL3.connected();
#endif

  if (host_serial_connected) {
    if (!PrintSource::printingFromSDCard()) {
      PrintSource::set_printing_from_host();
    }
    PORT_REDIRECT(SerialMask::All);
    SERIAL_ECHOLNPGM("[DEBUG] HostUI::start() invoked -> PrintSource::printingFromHost()=", PrintSource::printingFromHost());
  }
}
#endif

#if ENABLED(G29_RETRY_AND_RECOVER)
  #ifdef ACTION_ON_G29_RECOVER
    void HostUI::g29_recover() { action(F(ACTION_ON_G29_RECOVER)); }
  #endif
  #ifdef ACTION_ON_G29_FAILURE
    void HostUI::g29_failure() { action(F(ACTION_ON_G29_FAILURE)); }
  #endif
#endif

#ifdef SHUTDOWN_ACTION
  void HostUI::shutdown() { action(F(SHUTDOWN_ACTION)); }
#endif

#if ENABLED(HOST_PROMPT_SUPPORT)

  PromptReason HostUI::host_prompt_reason = PROMPT_NOT_DEFINED;

  PGMSTR(CONTINUE_STR, "Continue");
  PGMSTR(DISMISS_STR, "Dismiss");

  #if HAS_RESUME_CONTINUE
    extern bool wait_for_user;
  #endif

  void HostUI::notify(const char * const cstr) {
    PORT_REDIRECT(SerialMask::All);
    action(F("notification "), false);
    SERIAL_ECHOLN(cstr);
    hostui_store_notification(cstr);
  }

  void HostUI::notify_P(PGM_P const pstr) {
    PORT_REDIRECT(SerialMask::All);
    action(F("notification "), false);
    SERIAL_ECHOLNPGM_P(pstr);
    hostui_store_notification_P(pstr);
  }

  void HostUI::prompt(FSTR_P const ptype, const bool eol/*=true*/) {
    PORT_REDIRECT(SerialMask::All);
    action(F("prompt_"), false);
    SERIAL_ECHO(ptype);
    if (eol) SERIAL_EOL();
  }

  void HostUI::prompt_plus(const bool pgm, FSTR_P const ptype, const char * const str, const char extra_char/*='\0'*/) {
    prompt(ptype, false);
    PORT_REDIRECT(SerialMask::All);
    SERIAL_CHAR(' ');
    if (pgm)
      SERIAL_ECHOPGM_P(str);
    else
      SERIAL_ECHO(str);
    if (extra_char != '\0') SERIAL_CHAR(extra_char);
    SERIAL_EOL();
  }

  void HostUI::prompt_begin(const PromptReason reason, FSTR_P const fstr, const char extra_char/*='\0'*/) {
    prompt_end();
    host_prompt_reason = reason;
    prompt_plus(F("begin"), fstr, extra_char);
  }
  void HostUI::prompt_begin(const PromptReason reason, const char * const cstr, const char extra_char/*='\0'*/) {
    prompt_end();
    host_prompt_reason = reason;
    prompt_plus(F("begin"), cstr, extra_char);
  }

  void HostUI::prompt_end() { prompt(F("end")); }
  void HostUI::prompt_show() { prompt(F("show")); }

  void HostUI::_prompt_show(FSTR_P const btn1, FSTR_P const btn2) {
    if (btn1) prompt_button(btn1);
    if (btn2) prompt_button(btn2);
    prompt_show();
  }

  void HostUI::prompt_button(FSTR_P const fstr) { prompt_plus(F("button"), fstr); }
  void HostUI::prompt_button(const char * const cstr) { prompt_plus(F("button"), cstr); }

  void HostUI::prompt_do(const PromptReason reason, FSTR_P const fstr, FSTR_P const btn1/*=nullptr*/, FSTR_P const btn2/*=nullptr*/) {
    prompt_begin(reason, fstr);
    _prompt_show(btn1, btn2);
  }
  void HostUI::prompt_do(const PromptReason reason, const char * const cstr, FSTR_P const btn1/*=nullptr*/, FSTR_P const btn2/*=nullptr*/) {
    prompt_begin(reason, cstr);
    _prompt_show(btn1, btn2);
  }
  void HostUI::prompt_do(const PromptReason reason, FSTR_P const fstr, const char extra_char, FSTR_P const btn1/*=nullptr*/, FSTR_P const btn2/*=nullptr*/) {
    prompt_begin(reason, fstr, extra_char);
    _prompt_show(btn1, btn2);
  }
  void HostUI::prompt_do(const PromptReason reason, const char * const cstr, const char extra_char, FSTR_P const btn1/*=nullptr*/, FSTR_P const btn2/*=nullptr*/) {
    prompt_begin(reason, cstr, extra_char);
    _prompt_show(btn1, btn2);
  }

  #if ENABLED(ADVANCED_PAUSE_FEATURE)
    void HostUI::filament_load_prompt() {
      const bool disable_to_continue = TERN0(HAS_FILAMENT_SENSOR, runout.filament_ran_out);
      prompt_do(PROMPT_FILAMENT_RUNOUT, F("Paused"), F("PurgeMore"),
        disable_to_continue ? F("DisableRunout") : FPSTR(CONTINUE_STR)
      );
    }
  #endif

  //
  // Handle responses from the host, such as:
  //  - Filament runout responses: Purge More, Continue
  //  - General "Continue" response
  //  - Resume Print response
  //  - Dismissal of info
  //
  void HostUI::handle_response(const uint8_t response) {
    const PromptReason hpr = host_prompt_reason;
    host_prompt_reason = PROMPT_NOT_DEFINED;  // Reset now ahead of logic
    switch (hpr) {
      case PROMPT_FILAMENT_RUNOUT:
        switch (response) {

          case 0: // "Purge More" button
            #if ENABLED(M600_PURGE_MORE_RESUMABLE)
              pause_menu_response = PAUSE_RESPONSE_EXTRUDE_MORE;  // Simulate menu selection (menu exits, doesn't extrude more)
            #endif
            break;

          case 1: // "Continue" / "Disable Runout" button
            #if ENABLED(M600_PURGE_MORE_RESUMABLE)
              pause_menu_response = PAUSE_RESPONSE_RESUME_PRINT;  // Simulate menu selection
            #endif
            #if HAS_FILAMENT_SENSOR
              if (runout.filament_ran_out) {                      // Disable a triggered sensor
                runout.enabled = false;
                runout.reset();
              }
            #endif
            break;
        }
        break;
      case PROMPT_USER_CONTINUE:
        TERN_(HAS_RESUME_CONTINUE, wait_for_user = false);
        break;
      case PROMPT_PAUSE_RESUME:
        #if ALL(ADVANCED_PAUSE_FEATURE, HAS_MEDIA)
          extern const char M24_STR[];
          queue.inject_P(M24_STR);
        #endif
        break;
      case PROMPT_INFO:
        break;
      default: break;
    }
  }

#endif // HOST_PROMPT_SUPPORT

#endif // HOST_ACTION_COMMANDS
