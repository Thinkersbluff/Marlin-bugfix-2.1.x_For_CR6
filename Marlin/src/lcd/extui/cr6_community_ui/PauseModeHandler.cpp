#include "PauseModeHandler.h"
#include "DGUSDisplay.h"
#include "DGUSScreenHandler.h"
#include "creality_touch/DGUSDisplayDef.h"

namespace CR6PauseHandler {

void Init() {
  // No-op for now. Reserved for future setup (e.g., load localized strings).
}

void HandlePauseMessage(const PauseMessage message, const PauseMode mode, uint8_t extruder) {
  // Default behavior: delegate to the existing DGUS mapping in dgus_creality_lcd.
  // This function exists to centralize logic; callers can switch to using this
  // handler instead of calling the DGUS-specific code directly.

  // Example simple dispatch (mirror of existing behavior). Keep lightweight.
  switch (message) {
    // PAUSE_MESSAGE_WAITING
    // English: "Pause this print?" (used when Marlin requests permission to pause)
    // Expected user action: Confirm => UI should call the pause action (e.g. ExtUI::pausePrint())
    //                      Cancel  => do nothing (printer continues)
    // Implementation note: we present the two-button pause dialog; the dialog's
    // Confirm handler should call ExtUI::pausePrint() (this matches existing
    // `PrintPauseDialogHandler` behavior).
    case PAUSE_MESSAGE_WAITING:
      // PAUSE_MESSAGE_WAITING can be used for two different intents:
      //  - A generic "Pause this print?" confirmation (PAUSE_MODE_PAUSE_PRINT)
      //  - A filament-change continuation prompt when the pause mode indicates
      //    we're in a filament-change flow (PAUSE_MODE_CHANGE_FILAMENT / LOAD / UNLOAD)
      // Use the provided `mode` to choose the correct UI so the Continue button
      // triggers the intended firmware action.
  // For PAUSE_MESSAGE_WAITING show the programmable popup. The Marlin
  // message has already been copied into VP_MSGSTR1..4 by
  // ExtUI::onUserConfirmRequired(), so do not overwrite it here.
  // Suppress mapping the popup info byte to a PauseMenuResponse so
  // pressing Continue here does not accidentally signal "Resume".
  DGUSScreenHandler::SetSuppressPopupPauseResponse(true);
  ScreenHandler.GotoScreen(DGUSLCD_SCREEN_POPUP);
      break;

  // PAUSE_MESSAGE_INSERT
  // English: "Insert filament" / "Load filament to continue".
  // Behavior: Marlin is paused and simply asks the user to insert or route filament.
  // Expected UI: a single confirmation control ("Continue" / "Done") is sufficient.
  // When the user has inserted filament, the UI should call ExtUI::setUserConfirmed()
  // to tell Marlin the insertion step is complete. This does NOT need to set a
  // pause_menu_response for Resume vs Purge — it is only signaling that the insert
  // operation finished and Marlin can proceed with the next step in the filament
  // change sequence.
    case PAUSE_MESSAGE_INSERT:
      // Show popup for insert and suppress mapping so Continue advances
      // the insert flow (setUserConfirmed()) instead of being interpreted
      // as a Resume command.
      DGUSScreenHandler::SetSuppressPopupPauseResponse(true);
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_POPUP);
      break;
  // Use the programmable popup (#63) which provides a title (VP_MSGSTR4)
  // and three lines (VP_MSGSTR1..3) for text. For `PAUSE_MESSAGE_INSERT` the
  // display only needs a single "Continue" action (the user inserted filament
  // and signals completion). Implement this by encoding the popup's Continue
  // button to write to VP_SCREENCHANGE (0x219F) with a two-byte value where
  // low byte = target screen (e.g. DGUSLCD_SCREEN_PRINT_PAUSED or 0x3F for the
  // popup) and high byte = an "info" code consumed by firmware.
  //
  // Encoding examples (write 16-bit value to VP_SCREENCHANGE):
  //  - INSERT (single Continue): 0x01 << 8 | 0x3F == 0x013F  -> info=0x01
  //    Firmware will interpret info=0x01 as a Continue signal and will call
  //    ExtUI::setUserConfirmed() to advance the insert flow.
  //  - OPTION (two-button Continue/Purge): encode the Continue button as
  //    0x01 << 8 | 0x3F (0x013F) and the Purge button as 0x02 << 8 | 0x3F
  //    (0x023F). Firmware maps info=0x02 to PAUSE_RESPONSE_EXTRUDE_MORE
  //    before calling ExtUI::setUserConfirmed(), so Marlin performs the purge
  //    action and typically returns to OPTION/PURGE state afterward.
  //
  // Keep each VP line under VP_MSGSTR*_LEN (currently 0x20 / 32
  // characters). The example below uses short phrases tailored to the
  // popup layout.
  // The Marlin-provided message is already in VP_MSGSTR1..4; show the popup.
  ScreenHandler.GotoScreen(DGUSLCD_SCREEN_CONFIRM);
      break;

  // PAUSE_MESSAGE_OPTION / PAUSE_MESSAGE_PURGE
  // English intent: Marlin is asking whether to Resume (Continue) or to Purge/Extrude
  // more filament before continuing. This is the two-button decision point.
  // Typical flow (as implemented in other ExtUI backends):
  //  - The UI shows a two-button dialog: Continue (Resume) and Purge (Extrude More).
  //  - If the user selects Continue, the UI should set pause_menu_response to
  //    PAUSE_RESPONSE_RESUME_PRINT and call ExtUI::setUserConfirmed() — Marlin will
  //    then resume. In that case Marlin will NOT emit PAUSE_MESSAGE_PURGE next.
  //  - If the user selects Purge, the UI should set pause_menu_response to
  //    PAUSE_RESPONSE_EXTRUDE_MORE and call ExtUI::setUserConfirmed(); Marlin will
  //    perform the purge/extrude operation and typically re-enter the OPTION state
  //    (or emit PAUSE_MESSAGE_PURGE / PAUSE_MESSAGE_OPTION again) so the user can
  //    choose to Purge more or Resume. In practice Marlin may emit PAUSE_MESSAGE_PURGE
  //    (showing purge-specific text) and then PAUSE_MESSAGE_OPTION again until the
  //    user chooses Resume.
  // After the user makes a choice, always call ExtUI::setUserConfirmed() so Marlin
  // can act on the selected response.
    case PAUSE_MESSAGE_PURGE:
      // Use the new INFOBOX screen for purge so the user is not shown actionable
      // buttons while Marlin is busy purging. `DGUSLCD_SCREEN_INFOBOX` is a
      // programmable information screen (ID 62) that supports:
      //  - VP_MSGSTR1..VP_MSGSTR4 for up to four text lines
      //  - VP_M117 and VP_M117_STATIC for dynamic/static M117-style messages
      //  - an animated throbber icon to indicate activity
      //
      // Mark a synchronous operation so the ScreenHandler will enable the
      // busy/throbber animation and disable interactive controls (back button).
      // Call `SetSynchronousOperationFinish()` once the purge completes (in the
      // code path that handles purge completion) so the UI returns to normal.
    ScreenHandler.BeginPurgeOperation();

    // The Marlin purge message is expected to be provided in the message VP
    // by onUserConfirmRequired(); show the infobox (with busy animation)
    // while the purge operation is in progress.
    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_INFOBOX);
      break;
    // The duplicate purge info was removed; handled above.
    break;
    // PAUSE_MESSAGE_HEAT / PAUSE_MESSAGE_HEATING
    // English: "Heat" / "Reheating" (Marlin is re-heating nozzle/bed before continuing)
    // Expected user action: usually none – Marlin will wait for temperature. Show the
    // paused screen and a status message so the user knows the job is paused while
    // the hotend/bed reaches target. No explicit user confirmation is required until
    // higher-level code asks for it.
    case PAUSE_MESSAGE_HEAT:
      // Heater timeout occurred while waiting for the user. Show the
      // programmable popup so the user can press Continue to begin the
      // re-heat/rehoming flow. Suppress mapping the popup button into
      // PAUSE_RESPONSE_* so pressing Continue only releases the wait and
      // lets Marlin handle re-heating.
      DGUSScreenHandler::SetSuppressPopupPauseResponse(true);
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_POPUP);
      // Do NOT clear suppression here; it will be cleared by the
      // ScreenChangeHook when the popup is handled (pop returns).
      break;
    case PAUSE_MESSAGE_HEATING:
      // Clear suppression so subsequent popups behave normally.
      DGUSScreenHandler::SetSuppressPopupPauseResponse(false);
      // While actively heating, show info box to indicate progress.
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_INFOBOX);
      break;
    // PAUSE_MESSAGE_PARKING / PAUSE_MESSAGE_CHANGING / PAUSE_MESSAGE_UNLOAD / PAUSE_MESSAGE_LOAD
    // English: Typically used for the filament-change flow. Example messages: "Parking",
    // "Changing filament", "Unload filament", "Load filament".
    // Expected user actions and Marlin interaction:
    //  - Marlin may park the nozzle and then ask the user to unload/load filament.
    //  - The UI should show the paused screen and a status message. From there the user
    //    can navigate to the FEED (load/unload) workflow if they want to do filament ops.
    //  - After completing load/unload, the UI must call ExtUI::setUserConfirmed() (and
    //    possibly set pause_menu_response) so Marlin can proceed with resume or purging.
    // Important: do not auto-enter FEED here; present the paused screen to give users
    // the option to navigate to Tune/Feed or other actions.
    case PAUSE_MESSAGE_PARKING:
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_INFOBOX);
      break;
    case PAUSE_MESSAGE_CHANGING:
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_INFOBOX);
      break;
    case PAUSE_MESSAGE_UNLOAD:
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_INFOBOX);
      break;
    case PAUSE_MESSAGE_LOAD:
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_INFOBOX);
      break;

    // PAUSE_MESSAGE_RESUME / PAUSE_MESSAGE_STATUS / default
    // These messages generally indicate Marlin expects an immediate resume or is
    // reporting a status. The proper response is typically to resume the print.
    // We set the pause_menu_response to RESUME and call setUserConfirmed() to release
    // the hold so Marlin continues.
    case PAUSE_MESSAGE_RESUME:
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_INFOBOX);
      break;
    case PAUSE_MESSAGE_STATUS:
    default:
      ExtUI::setPauseMenuResponse(PAUSE_RESPONSE_RESUME_PRINT);
      ExtUI::setUserConfirmed();
      break;
  }
}

} // namespace CR6PauseHandler
