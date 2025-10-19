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

/**
 * dgus_creality_lcd.cpp
 *
 * DGUS implementation written by coldtobi in 2019 for Marlin
 */

#include "../../../inc/MarlinConfigPre.h"

#if ENABLED(DGUS_LCD_UI_CR6_COMM)

#include "../ui_api.h"
#include "../../marlinui.h"
#include "DGUSDisplay.h"
#include "DGUSDisplayDef.h"
#include "DGUSScreenHandler.h"
#include "./creality_touch/PIDHandler.h"
#include "./creality_touch/MeshValidationHandler.h"
// Centralized pause-mode handler for CR6 UI.
#include "PauseModeHandler.h"

#if ENABLED(POWER_LOSS_RECOVERY)
  #include "../../../feature/powerloss.h"
#endif

extern const char NUL_STR[];

namespace ExtUI {

  void onStartup() {
    ScreenHandler.Init();
    ScreenHandler.UpdateScreenVPData();
  }

  void onIdle() { ScreenHandler.loop(); }

  void onPrinterKilled(PGM_P const error, PGM_P const component) {
    ScreenHandler.sendinfoscreen(GET_TEXT(MSG_HALTED), error, GET_TEXT(MSG_PLEASE_RESET), GET_TEXT(MSG_PLEASE_RESET), true, true, true, true);

    if (strcmp_P(error, GET_TEXT(MSG_ERR_MAXTEMP)) == 0 || strcmp_P(error, GET_TEXT(MSG_THERMAL_RUNAWAY)) == 0)     {
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_THERMAL_RUNAWAY);
    } else if (strcmp_P(error, GET_TEXT(MSG_HEATING_FAILED_LCD)) == 0) {
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_HEATING_FAILED);
    }else if (strcmp_P(error, GET_TEXT(MSG_ERR_MINTEMP)) == 0) {
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_THERMISTOR_ERROR);
    } else {
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_KILL);
    }

    ScreenHandler.KillScreenCalled();
    while (!ScreenHandler.loop());  // Wait while anything is left to be sent
}

  void onMediaInserted() { TERN_(SDSUPPORT, ScreenHandler.SDCardInserted()); }
  void onMediaError()    { TERN_(SDSUPPORT, ScreenHandler.SDCardError()); }
  void onMediaRemoved()  { TERN_(SDSUPPORT, ScreenHandler.SDCardRemoved()); }

  void onPlayTone(const uint16_t frequency, const uint16_t duration) {
    if (ScreenHandler.getCurrentScreen() == DGUSLCD_SCREEN_FEED) {
        // We're in the feed (load filament) workflow - no beep - there is no confirmation
        return;
    }

    ScreenHandler.Buzzer(frequency, duration);
  }

bool hasPrintTimer = false;

  void onPrintTimerStarted() {
    hasPrintTimer = true;

    if (!IS_SD_FILE_OPEN() && !(PrintJobRecovery::valid() && PrintJobRecovery::exists())) {
      ScreenHandler.SetPrintingFromHost();
    }

#if ENABLED(LCD_SET_PROGRESS_MANUALLY)
    ui.progress_reset();
#endif

    ScreenHandler.SetViewMeshLevelState();
    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING);
  }

  void onPrintTimerPaused() {
    // Handle M28 Pause SD print - But only if we're not waiting on a user
    if (ExtUI::isPrintingFromMediaPaused() && ScreenHandler.getCurrentScreen() == DGUSLCD_SCREEN_PRINT_RUNNING && !ExtUI::isWaitingOnUser()) {
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_PAUSED);
    }
  }

  void onPrintTimerStopped() {
    hasPrintTimer = false;

    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_FINISH);
  }

  void onFilamentRunout(const extruder_t extruder) {
    // Only navigate to filament runout screen when we don't use M600 for changing the filament - otherwise it gets confusing for the user
    if (strcmp_P(FILAMENT_RUNOUT_SCRIPT, PSTR("M600")) != 0) {
      ScreenHandler.FilamentRunout();
    }
  }

  void onUserConfirmed() {
    DEBUG_ECHOLN("User confirmation invoked");

    ExtUI::setUserConfirmed();
  }

  void onUserConfirmRequired(const char * const msg) {
    if (!msg) {
      // Cancellation - if we're showing a popup then pop back
      if (ScreenHandler.getCurrentScreen() == DGUSLCD_SCREEN_POPUP) {
        DEBUG_ECHOLNPAIR("User confirmation canceled");
        ScreenHandler.setstatusmessagePGM(nullptr);
        ScreenHandler.PopToOldScreen();
      }
      return;
    }

    DEBUG_ECHOLNPAIR("User confirmation requested: ", msg);
    // Skip VP updates for messages that should show normal screens instead of popups
    if (ExtUI::pauseModeStatus == PAUSE_MESSAGE_PARKING ||
        ExtUI::pauseModeStatus == PAUSE_MESSAGE_CHANGING ||
        (ExtUI::pauseModeStatus == PAUSE_MESSAGE_WAITING &&
        (ExtUI::getPauseMode() == PAUSE_MODE_CHANGE_FILAMENT ||
        ExtUI::getPauseMode() == PAUSE_MODE_LOAD_FILAMENT ||
        ExtUI::getPauseMode() == PAUSE_MODE_UNLOAD_FILAMENT))) {
      SERIAL_ECHOLNPGM("onUserConfirmRequired: PARKING/CHANGING/WAITING(filament) - skip VP update, show normal screen");
      // Let the centralized pause handler process the message to show PRINT_PAUSED screen
      CR6PauseHandler::HandlePauseMessage(ExtUI::pauseModeStatus, ExtUI::getPauseMode(), 0);
      return;
    }

    // If a Confirm dialog is already displayed, avoid overwriting its
    // text with a subsequent pause message that arrives immediately.
    // Some pause flows emit multiple messages in quick succession (e.g.
    // "Press Button" followed by "Nozzle Parked"). If we overwrite the
    // Confirm screen's VPs the user never sees the resume prompt. To
    // preserve the user's ability to act on the Confirm dialog, skip
    // updating the display when we're already showing Screen#66 (Confirm)
    // and a ConfirmVP has been set.
    if (ScreenHandler.getCurrentScreen() == DGUSLCD_SCREEN_CONFIRM && ScreenHandler.IsConfirmActive()) {
      SERIAL_ECHOLNPGM("onUserConfirmRequired: Confirm already active - skipping VP update to avoid overwrite");
      // Still let the centralized pause handler process the logical pause
      // message (it might toggle suppression or other state), but do not
      // modify the visible VP lines.
      CR6PauseHandler::HandlePauseMessage(ExtUI::pauseModeStatus, ExtUI::getPauseMode(), 0);
      return;
    }
    // Previously the FEED (load/unload) screen auto-confirmed here which
    // prevented the centralized PauseModeHandler from receiving the
    // pause prompt. Remove the auto-confirm so the handler can present the
    // appropriate prompt/response dialog even while the user is in FEED.

    // Ensure the DGUS popup/confirm VPs contain the raw Marlin message and
    // a short pause-mode header so Screen#66 (Confirm) or other popup
    // screens will display the exact Marlin text and the pause-mode name.
    // Copy the pause-mode label out of flash into RAM so we can pass a
    // simple `const char*` to sendinfoscreen without type mismatches.
    char pause_label[VP_MSGSTR4_LEN + 1] = {0};
    FSTR_P src_label = GET_TEXT_F(MSG_FILAMENT_CHANGE_HEADER_PAUSE);
    switch (ExtUI::getPauseMode()) {
      case PAUSE_MODE_CHANGE_FILAMENT: src_label = GET_TEXT_F(MSG_FILAMENT_CHANGE_HEADER); break;
      case PAUSE_MODE_LOAD_FILAMENT:   src_label = GET_TEXT_F(MSG_FILAMENT_CHANGE_HEADER_LOAD); break;
      case PAUSE_MODE_UNLOAD_FILAMENT: src_label = GET_TEXT_F(MSG_FILAMENT_CHANGE_HEADER_UNLOAD); break;
      default: break;
    }
  // Copy from flash into RAM. Strings are short (header text), so strcpy_P is safe here.
  // Use FTOP() to convert FSTR_P (FlashStringHelper) to a PGM pointer as used elsewhere in the codebase.
  strcpy_P(pause_label, FTOP(src_label));
    pause_label[VP_MSGSTR4_LEN] = '\0';

    // Populate the display message buffers (VP_MSGSTR1..4) so the Confirm/Popup
    // screens show the Marlin-provided message and the pause-mode header.
    // Clear VP_MSGSTR1..3 first to avoid leftover garbage if the incoming
    // Marlin message contains fewer than three lines. Then copy a bounded
    // amount from PROGMEM and parse it safely for embedded NULs or '\n'.
    {
      // Clear lines 1..3 on the display (preserve pause_label in VP_MSGSTR4)
      ScreenHandler.sendinfoscreen(nullptr, nullptr, nullptr, pause_label, false, false, false, false);

      // Prepare per-line buffers and a bounded message buffer for safe parsing
      char line1[VP_MSGSTR1_LEN + 1] = {0};
      char line2[VP_MSGSTR2_LEN + 1] = {0};
      char line3[VP_MSGSTR3_LEN + 1] = {0};

      constexpr size_t MSGBUF_LEN = (VP_MSGSTR1_LEN + VP_MSGSTR2_LEN + VP_MSGSTR3_LEN) + 4;
      char msgbuf[MSGBUF_LEN];
      memset(msgbuf, 0, sizeof(msgbuf));

      // Copy up to MSGBUF_LEN-1 bytes from PROGMEM (or RAM) into msgbuf. Use memcpy_P
      // to allow embedded NULs to be copied intact when the source is in flash.
      PGM_P pmsg = (PGM_P)msg;
      if (pmsg) {
        // If msg is in PROGMEM, copy from flash; otherwise memcpy from RAM
        memcpy_P(msgbuf, pmsg, MSGBUF_LEN - 1);
        msgbuf[MSGBUF_LEN - 1] = '\0';
      } else {
        // Fallback: if msg is a RAM pointer (unlikely for GET_TEXT_F) use strncpy
        strncpy(msgbuf, msg ? msg : "", MSGBUF_LEN - 1);
        msgbuf[MSGBUF_LEN - 1] = '\0';
      }

      // First, try parsing embedded NUL-delimited strings (MSG_2_LINE / MSG_3_LINE)
      // by scanning msgbuf for NUL separators.
      char *p = msgbuf;
      size_t remaining = MSGBUF_LEN;
      if (p && p[0]) {
        // Copy first line up to VP_MSGSTR1_LEN
        size_t l1 = strnlen(p, remaining);
        if (l1) {
          strncpy(line1, p, min(l1, (size_t)VP_MSGSTR1_LEN));
          line1[VP_MSGSTR1_LEN] = '\0';
        }
        // advance past first NUL if present
        if (l1 < remaining) {
          p += l1 + 1;
          remaining -= (l1 + 1);
          if (p && remaining && p[0]) {
            size_t l2 = strnlen(p, remaining);
            if (l2) {
              strncpy(line2, p, min(l2, (size_t)VP_MSGSTR2_LEN));
              line2[VP_MSGSTR2_LEN] = '\0';
            }
            if (l2 < remaining) {
              p += l2 + 1;
              remaining -= (l2 + 1);
              if (p && remaining && p[0]) {
                size_t l3 = strnlen(p, remaining);
                if (l3) {
                  strncpy(line3, p, min(l3, (size_t)VP_MSGSTR3_LEN));
                  line3[VP_MSGSTR3_LEN] = '\0';
                }
              }
            }
          }
        }
      }

      // If we only found one line (no embedded NULs after the first), fall back
      // to splitting on '\n' within msgbuf to support newline-separated strings.
      if (!line2[0] && !line3[0]) {
        // Work on a writable copy (msgbuf already writable)
        char *nl = strchr(msgbuf, '\n');
        if (nl) {
          *nl = '\0';
          strncpy(line1, msgbuf, VP_MSGSTR1_LEN);
          line1[VP_MSGSTR1_LEN] = '\0';
          char *p2 = nl + 1;
          nl = strchr(p2, '\n');
          if (nl) {
            *nl = '\0';
            strncpy(line2, p2, VP_MSGSTR2_LEN);
            line2[VP_MSGSTR2_LEN] = '\0';
            strncpy(line3, nl + 1, VP_MSGSTR3_LEN);
            line3[VP_MSGSTR3_LEN] = '\0';
          } else {
            strncpy(line2, p2, VP_MSGSTR2_LEN);
            line2[VP_MSGSTR2_LEN] = '\0';
          }
        } else {
          // Single-line message (msgbuf contains the whole string)
          strncpy(line1, msgbuf, VP_MSGSTR1_LEN);
          line1[VP_MSGSTR1_LEN] = '\0';
        }
      }

      // Debugging: print what we'll send to the display
      SERIAL_ECHOLNPAIR("Pause popup lines:", line1);
      SERIAL_ECHOLNPAIR("Pause popup lines 2:", line2);
      SERIAL_ECHOLNPAIR("Pause popup lines 3:", line3);

      // Send only non-empty lines (use nullptr for empty lines to avoid leftover text)
      ScreenHandler.sendinfoscreen(line1[0] ? line1 : nullptr,
                                   line2[0] ? line2 : nullptr,
                                   line3[0] ? line3 : nullptr,
                                   pause_label,
                                   false, false, false, false);
    }

    // Delegate to centralized, mode-aware pause handler for the rest of the flow.
    CR6PauseHandler::HandlePauseMessage(ExtUI::pauseModeStatus, ExtUI::getPauseMode(), 0);
  }

  void onStatusChanged(const char * const msg) { ScreenHandler.setstatusmessage(msg); }

  void onFactoryReset() {
    ScreenHandler.OnFactoryReset();
  }

   void onHomingStart() {
    ScreenHandler.OnHomingStart();
  }

  void onHomingComplete() {
    SERIAL_ECHOLNPGM("ExtUI::onHomingComplete invoked");
    ScreenHandler.OnHomingComplete();
  }

  // Ensure the DGUS UI receives generic leveling start/done events so the
  // screen handler can present the mesh capture UI and update points live.
  void onLevelingStart() {
    SERIAL_ECHOLNPGM("ExtUI::onLevelingStart invoked - forwarding to OnMeshLevelingStart");
    ScreenHandler.OnMeshLevelingStart();
  }

  void onLevelingDone() {
    SERIAL_ECHOLNPGM("ExtUI::onLevelingDone invoked - finishing mesh UI");
    // There is no explicit OnMeshLevelingFinish() in the handler; use the
    // existing logic that finishes when the last point is captured. If the
    // handler needs an explicit finish action, PopToOldScreen() is safe here.
    ScreenHandler.PopToOldScreen();
  }

  // The core ExtUI API expects onHomingDone(). Some older CR6 code used
  // the name onHomingComplete(). Provide the correctly-named symbol so the
  // G28 homing code (which calls ExtUI::onHomingDone()) invokes the DGUS
  // handler. Keep the legacy name as well for compatibility.
  void onHomingDone() {
    SERIAL_ECHOLNPGM("ExtUI::onHomingDone invoked (forwarding to OnHomingComplete)");
    ScreenHandler.OnHomingComplete();
  }

  void onPrintFinished() {
    ScreenHandler.OnPrintFinished();
  }

  void onStoreSettings(char *buff) {
    ScreenHandler.StoreSettings(buff);
  }

  void onLoadSettings(const char *buff) {
    ScreenHandler.LoadSettings(buff);
  }

  void onPostprocessSettings() {
    // Called after loading or resetting stored settings
  }

  void onConfigurationStoreWritten(bool success) {
    // Called after the entire EEPROM has been written,
    // whether successful or not.
  }

  void onConfigurationStoreRead(bool success) {
    // Called after the entire EEPROM has been read,
    // whether successful or not.
  }

  #if HAS_MESH
    void onMeshLevelingStart() {
      ScreenHandler.OnMeshLevelingStart();
    }

    void onMeshUpdate(const int8_t xpos, const int8_t ypos, const float zval) {
      ScreenHandler.OnMeshLevelingUpdate(xpos, ypos, zval);
    }

    void onMeshUpdate(const int8_t xpos, const int8_t ypos, const ExtUI::probe_state_t state) {
      ScreenHandler.OnMeshLevelingUpdate(xpos, ypos, 0);
    }
  #endif

  #if ENABLED(POWER_LOSS_RECOVERY)
    void onPowerLossResume() {
      // Called on resume from power-loss
      ScreenHandler.OnPowerlossResume();
    }
  #endif


  #if HAS_PID_HEATING
    void onPidTuning(const pidresult_t rst) {
      // Called for temperature PID tuning result
      switch (rst) {
        case PID_STARTED:
          // It has no use switching to the PID screen. It really isn't that informative.
          break;
        case PID_BED_STARTED:
          // There is no BED_PID functionality in the CR6_COMM UI.
          break;
        case PID_CHAMBER_STARTED:
          // There is no CHAMBER_PID functionality in the CR6_COMM UI.
          break;
        case PID_BAD_EXTRUDER_NUM:
          PIDHandler::result_message = GET_TEXT(MSG_PID_BAD_EXTRUDER_NUM);
          ScreenHandler.setstatusmessagePGM(PIDHandler::result_message);
          break;
        case PID_TEMP_TOO_HIGH:
          PIDHandler::result_message = GET_TEXT(MSG_PID_TEMP_TOO_HIGH);
          ScreenHandler.setstatusmessagePGM(PIDHandler::result_message);
          break;
        case PID_TUNING_TIMEOUT:
          PIDHandler::result_message = GET_TEXT(MSG_PID_TIMEOUT);
          ScreenHandler.setstatusmessagePGM(PIDHandler::result_message);
        break;
        case PID_DONE:
          PIDHandler::result_message = GET_TEXT(MSG_PID_AUTOTUNE_DONE);
          ScreenHandler.setstatusmessagePGM(PIDHandler::result_message);
        break;
      }
    }
  #endif

  void onSteppersDisabled() {
  }

  void onSteppersEnabled() {
  }

  void onMeshValidationStarting() {
    MeshValidationHandler::OnMeshValidationStart();
  }

  void onMeshValidationFinished() {
    MeshValidationHandler::OnMeshValidationFinish();
  }
}
#endif // HAS_DGUS_LCD_UI_CR6_COMM