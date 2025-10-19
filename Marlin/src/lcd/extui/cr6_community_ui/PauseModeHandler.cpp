#include "PauseModeHandler.h"
#include "DGUSDisplay.h"
#include "DGUSScreenHandler.h"
#include "creality_touch/DGUSDisplayDef.h"
#include "creality_touch/PageHandlers.h"
// Need thermalManager access to check hotend target
#include "../../../module/temperature.h"

namespace CR6PauseHandler {

// Track the current pause mode state (when available). Provide a local
// fallback for builds without ADVANCED_PAUSE_FEATURE so logic that queries
// the current mode still compiles; behavior will be simplified.
#if ENABLED(ADVANCED_PAUSE_FEATURE)
static PauseMode current_pause_mode = PAUSE_MODE_SAME;
#else
static int current_pause_mode = 0; // fallback
#endif

void Init() {
  // No-op for now. Reserved for future setup (e.g., load localized strings).
}

#if ENABLED(ADVANCED_PAUSE_FEATURE)
void HandlePauseMessage(const PauseMessage message, const PauseMode mode, uint8_t extruder) {
#else
// Fallback no-op handler when advanced pause is disabled. Keep minimal
// behavior to show paused screen on park messages so the UI remains useful.
void HandlePauseMessage(int message, int mode, uint8_t extruder) {
#endif
  // Update our tracked pause mode if explicitly set (preserve state on PAUSE_MODE_SAME)
  if (mode != PAUSE_MODE_SAME) {
    current_pause_mode = mode;
    SERIAL_ECHOLNPAIR("CR6 Pause handler: pause mode updated to:", (int)current_pause_mode);
  }
  // Use the effective pause mode for all decisions
  const auto effective_mode = (mode != PAUSE_MODE_SAME) ? mode : current_pause_mode;
  // Log the incoming pause message for debug (helps diagnose OPTION vs PURGE flows)
  SERIAL_ECHOLNPAIR("CR6 Pause handler invoked message:", (int)message);
  // Also log both the passed mode and effective mode
  SERIAL_ECHOLNPAIR("CR6 Pause handler passed mode:", (int)mode);
  SERIAL_ECHOLNPAIR("CR6 Pause handler effective mode:", (int)effective_mode);

  // Default behavior: delegate to the existing DGUS mapping in dgus_creality_lcd.
  // This function exists to centralize logic; callers can switch to using this
  // handler instead of calling the DGUS-specific code directly.

  // Example simple dispatch (mirror of existing behavior). Keep lightweight.
  // If a Confirm screen is already active, most incoming pause messages
  // should not override it. Create a small helper to Goto a screen only
  // when it's allowed (or when we are explicitly asking to show CONFIRM).
  const bool confirm_active = (ScreenHandler.getCurrentScreen() == DGUSLCD_SCREEN_CONFIRM) && DGUSScreenHandler::IsConfirmActive();
  auto goto_screen_if_allowed = [&](DGUSLCD_Screens s) {
    if (!confirm_active || s == DGUSLCD_SCREEN_CONFIRM) {
      ScreenHandler.GotoScreen(s);
    } else {
      SERIAL_ECHOLNPGM("PauseModeHandler: skip overriding active CONFIRM screen");
    }
  };

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
      //  - A generic "Pause this print?" confirmation (PAUSE_MODE_PAUSE_PRINT when not in pause context)
      //  - A filament-change continuation prompt when already in a pause/filament-change flow
      // The key insight: if we get WAITING after PARKING/CHANGING, we're already in a pause
      // context and should show PRINT_PAUSED regardless of the mode value.
      // Only show POPUP for initial pause requests (when not already paused).
      if (ExtUI::isPrintingPaused() ||
          effective_mode == PAUSE_MODE_CHANGE_FILAMENT ||
          effective_mode == PAUSE_MODE_LOAD_FILAMENT ||
          effective_mode == PAUSE_MODE_UNLOAD_FILAMENT) {
        // Show paused screen with RESUME button - we're already in a pause state
        goto_screen_if_allowed(DGUSLCD_SCREEN_PRINT_PAUSED);
      } else {
        // For initial pause requests, show popup for user confirmation
        DGUSScreenHandler::SetSuppressPopupPauseResponse(true);
        goto_screen_if_allowed(DGUSLCD_SCREEN_POPUP);
      }
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
      goto_screen_if_allowed(DGUSLCD_SCREEN_POPUP);
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
  goto_screen_if_allowed(DGUSLCD_SCREEN_POPUP);
      // Do NOT clear suppression here; it will be cleared by the
      // ScreenChangeHook when the popup is handled (pop returns).
      break;
    case PAUSE_MESSAGE_HEATING: {
      // Clear suppression so subsequent popups behave normally.
      DGUSScreenHandler::SetSuppressPopupPauseResponse(false);
      // Only present the HEATING info box when the hotend actually has a
      // non-zero target (i.e., it's being heated) OR when we're in a
      // filament-change related mode which expects reheating. For common
      // bed-first prints the nozzle target is 0 and we should not show
      // the "Nozzle heating" INFOBOX.
      if (thermalManager.degTargetHotend(active_extruder) > 0 ||
          effective_mode == PAUSE_MODE_CHANGE_FILAMENT ||
          effective_mode == PAUSE_MODE_LOAD_FILAMENT ||
          effective_mode == PAUSE_MODE_UNLOAD_FILAMENT) {
        // Act normally and show the heating info box
        goto_screen_if_allowed(DGUSLCD_SCREEN_INFOBOX);
      }
      else {
        // No nozzle heating expected; show paused screen with a status
        // message so Resume remains available and avoid showing the
        // misleading heating dialog.
        ScreenHandler.setstatusmessage("Nozzle idle");
        goto_screen_if_allowed(DGUSLCD_SCREEN_PRINT_PAUSED);
      }
      break;
    }
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
      // Present the paused screen so the user can access Feed/Tune and
      // crucially use the Resume button which now maps to the pause-handshake
      // when Marlin is waiting. This avoids hiding the resume action behind
      // an infobox which does not expose the RESUME control.
      
      // Restore any interrupted blocking heating targets immediately when parking starts
      // so the nozzle can heat back up while parked, rather than waiting for resume
      restore_blocking_heating_cr6();
      
      goto_screen_if_allowed(DGUSLCD_SCREEN_PRINT_PAUSED);
      break;
    case PAUSE_MESSAGE_CHANGING:
      // Show the "Wait for filament change to start" message during M600 initialization
      goto_screen_if_allowed(DGUSLCD_SCREEN_INFOBOX);
      break;
    case PAUSE_MESSAGE_UNLOAD:
      // Show unload message in status field instead of changing screen
      // since this is typically a background operation, not requiring
      // user interaction via RESUME button.
      ScreenHandler.setstatusmessage("Unloading filament...");
      break;
    case PAUSE_MESSAGE_LOAD:
      // Show load message in status field instead of changing screen
      // since this is typically a background operation, not requiring
      // user interaction via RESUME button.
      ScreenHandler.setstatusmessage("Loading filament...");
      break;

    // PAUSE_MESSAGE_RESUME / PAUSE_MESSAGE_STATUS / default
    // These messages generally indicate Marlin expects an immediate resume or is
    // reporting a status. The proper response is typically to resume the print.
    // We set the pause_menu_response to RESUME and call setUserConfirmed() to release
    // the hold so Marlin continues.
    case PAUSE_MESSAGE_RESUME:
      ScreenHandler.setstatusmessage("Resuming...");
      goto_screen_if_allowed(DGUSLCD_SCREEN_INFOBOX);
      break;
    case PAUSE_MESSAGE_OPTION:
      // Show a two-button confirm dialog (Continue vs Purge) only if Marlin is
      // actually waiting for user input. During resume, this is just status.
      // Only show the interactive "Load more / Filament?" dialog when we're
      // in a generic purge/option flow. For an explicit LOAD_FILAMENT mode the
      // UI should not override the Feed/Load screen with this popup; instead
      // show a status message so the user can continue using the Load UI.
      if (ExtUI::isWaitingOnUser() && effective_mode != PAUSE_MODE_LOAD_FILAMENT) {
        // User interaction required - show interactive dialog
        DGUSScreenHandler::SetSuppressPopupPauseResponse(false);
        ScreenHandler.sendinfoscreen(
          PSTR("Load more"),
          PSTR("Filament?"),
          PSTR("[No=Resume]"),
          nullptr,
          true, true, true, true
        );
        // goto_screen_if_allowed(DGUSLCD_SCREEN_CONFIRM);
        ScreenHandler.setstatusmessage("Resuming print...");
      } else {
        // Resume in progress or explicit Load flow - show status message only
        ScreenHandler.setstatusmessage("Resuming print...");
      }
      break;
    case PAUSE_MESSAGE_STATUS:
      // STATUS messages are informational only - do not set pause responses
      // or call setUserConfirmed() as this can interfere with normal operation.
      // Just ignore the message or optionally update status display.
      break;
    default:
      // For unknown messages, we should not automatically resume
      // Log the message for debugging but take no action
      SERIAL_ECHOLNPAIR("CR6 Pause handler: unknown message ", (int)message);
      break;
  }
}

} // namespace CR6PauseHandler
