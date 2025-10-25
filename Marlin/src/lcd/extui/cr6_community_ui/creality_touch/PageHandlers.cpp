#include "../../../../inc/MarlinConfigPre.h"

#if ENABLED(DGUS_LCD_UI_CR6_COMM)

#include "../DGUSDisplayDef.h"
#include "../DGUSDisplay.h"
#include "../DGUSScreenHandler.h"

#include "../../../../module/temperature.h"
#include "../../../../module/motion.h"
#include "../../../../module/planner.h"
#include "../../../../feature/pause.h"
#include "../../../../gcode/gcode.h"
#include "../../../../MarlinCore.h"

#if ENABLED(FILAMENT_RUNOUT_SENSOR)
#include "../../../../feature/runout.h"
#endif

#include "../../../../module/settings.h"

#include "../../ui_api.h"
#include "../../../marlinui.h"

#include "PageHandlers.h"


// Definitions of page handlers

// Local storage for CR6-specific interrupted blocking-heating state.
static bool cr6_stored_blocking_wait = false;
static celsius_t cr6_stored_hotend_target = 0;
#if HAS_HEATED_BED
static celsius_t cr6_stored_bed_target = 0;
#endif

static void store_blocking_heating_cr6() {
    extern bool wait_for_heatup; // from MarlinCore.h
    cr6_stored_blocking_wait = wait_for_heatup;
    cr6_stored_hotend_target = thermalManager.degTargetHotend(0);
#if HAS_HEATED_BED
    cr6_stored_bed_target = thermalManager.degTargetBed();
#endif
}

void restore_blocking_heating_cr6() {
    if (!cr6_stored_blocking_wait) return;
        char buf[32];
        // Only restore hotend target if it was actually heating (> 0) AND is not currently heating
        // Use non-blocking M104 to avoid re-entering blocking wait
        if (cr6_stored_hotend_target > 0 && thermalManager.degTargetHotend(0) == 0) {
            snprintf(buf, sizeof(buf), "M109 S%u", (uint16_t)cr6_stored_hotend_target);
            ExtUI::injectCommands(buf);
        }
#if HAS_HEATED_BED
        // Only restore bed target if it was actually heating (> 0) AND is not currently heating
        // Use non-blocking M140 to avoid re-entering blocking wait
        if (cr6_stored_bed_target > 0 && thermalManager.degTargetBed() == 0) {
            snprintf(buf, sizeof(buf), "M190 S%u", (uint16_t)cr6_stored_bed_target);
            ExtUI::injectCommands(buf);
        }
#endif
    cr6_stored_blocking_wait = false;
    cr6_stored_hotend_target = 0;
#if HAS_HEATED_BED
    cr6_stored_bed_target = 0;
#endif
}

// (Previous logic used an accidental-confirm ignore flag here; it was
// removed per request.)

void MainMenuHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_MAINENTERKEY:
            switch (buttonValue) {
                case 1:
                    // Try to mount an unmounted card (BTT SKR board has especially some trouble sometimes)
                    card.mount();
                    ScreenHandler.SDCardInserted();
                    break;

                case 2:
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PREPARE);
                    break;

                case 3:
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_SETUP);
                    break;

                case 4:
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_CALIBRATE);
                    break;
            }
            break;
    }
}

void SetupMenuHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_PREPAREENTERKEY:
            switch(buttonValue) {
                case 5: // About
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_INFO);
                    break;

                case 7: // Reset to factory settings
                    settings.reset();
                    settings.save();

                    ExtUI::injectCommands_P(PSTR("M300"));

                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MAIN, false);
                    ScreenHandler.setstatusmessagePGM(PSTR("Restored default settings. Please turn your printer off and then on to complete the reset"));
                    break;
            }
            break;

        case VP_BUTTON_TEMPCONTROL:
            if (buttonValue == 2) ScreenHandler.GotoScreen(DGUSLCD_SCREEN_TEMP);
            break;

        case VP_BUTTON_ADJUSTENTERKEY:
            ScreenHandler.HandleLEDToggle();
            break;
    }
}

void LevelingModeHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_BEDLEVELKEY:
            switch (buttonValue) {
                case 1:
                    queue.enqueue_one_P("G28 U0");
                    queue.enqueue_one_P("G0 Z0");
                break;

                case 2:
                    // Increase Z-offset
                    ExtUI::smartAdjustAxis_steps(ExtUI::mmToWholeSteps(0.01, ExtUI::axis_t::Z), ExtUI::axis_t::Z, true);;
                    ScreenHandler.ForceCompleteUpdate();
                    ScreenHandler.RequestSaveSettings();
                    break;

                case 3:
                    // Decrease Z-offset
                    ExtUI::smartAdjustAxis_steps(ExtUI::mmToWholeSteps(-0.01, ExtUI::axis_t::Z), ExtUI::axis_t::Z, true);;
                    ScreenHandler.ForceCompleteUpdate();
                    ScreenHandler.RequestSaveSettings();
                    break;
            }

            break;

        case VP_BUTTON_PREPAREENTERKEY:
            if (buttonValue == 9) {
                #if DISABLED(HOTEND_IDLE_TIMEOUT)
                    thermalManager.disable_all_heaters();
                #endif

                ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MAIN, false);
            }
#if HAS_MESH
            if (buttonValue == 1) {
                // TODO: set state for "view leveling mesh"
                ScreenHandler.SetViewMeshLevelState();
                ScreenHandler.InitMeshValues();

                ScreenHandler.GotoScreen(DGUSLCD_SCREEN_LEVELING);
            }
#endif
            break;

        case VP_BUTTON_MAINENTERKEY:
            // Go to leveling screen
            ExtUI::injectCommands_P("G28 U0\nG29 U0");
#if HAS_MESH
            ScreenHandler.ResetMeshValues();
#endif
            dgusdisplay.WriteVariable(VP_MESH_SCREEN_MESSAGE_ICON, static_cast<uint16_t>(MESH_SCREEN_MESSAGE_ICON_LEVELING));
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_LEVELING);
            break;
    }
}

void LevelingHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_BEDLEVELKEY:
            if (!ScreenHandler.HasCurrentSynchronousOperation()) {
                ScreenHandler.PopToOldScreen();
            } else {
                ScreenHandler.setstatusmessagePGM("Wait for leveling completion...");
            }

            break;
    }
}

void TempMenuHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_ADJUSTENTERKEY:
            switch (buttonValue) {
                case 3:
                    ScreenHandler.HandleFanToggle();
                break;
            }

            break;

        case VP_BUTTON_TEMPCONTROL:
            switch (buttonValue){ 
                case 3:
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_TEMP_PLA);
                    break;

                case 4:
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_TEMP_ABS);
                    break;
            }
            break;
    }
}

void PrepareMenuHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_PREPAREENTERKEY:
            switch (buttonValue){
                case 3:
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MOVE10MM);
                    break;

                case 6:
                    // Disable steppers
                    ScreenHandler.HandleMotorLockUnlock(var, &buttonValue);
                    break;
            }
        break;

        case VP_BUTTON_HEATLOADSTARTKEY: 
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_FEED);
        break;

        case VP_BUTTON_COOLDOWN:
            ScreenHandler.HandleAllHeatersOff(var, &buttonValue);
            break;

        case VP_BUTTON_TEMPCONTROL:
            switch (buttonValue) {
                case 5:
#if HAS_PREHEAT
                    thermalManager.setTargetHotend(ExtUI::getMaterial_preset_E(0), 0);
                    thermalManager.setTargetBed(ExtUI::getMaterial_preset_B(0));
#else
                    thermalManager.setTargetHotend(ExtUI::getTargetTemp_celsius(ExtUI::E0), 0);
#if HAS_HEATED_BED
                    thermalManager.setTargetBed(ExtUI::getTargetTemp_celsius(ExtUI::heater_t::BED));
#endif
#endif

                    break;

                case 6:
                    // Some Marlin configs only expose a single preheat preset. Guard access to the second preset.
#if PREHEAT_COUNT > 1
#if HAS_PREHEAT
                    thermalManager.setTargetHotend(ExtUI::getMaterial_preset_E(1), 0);
                    thermalManager.setTargetBed(ExtUI::getMaterial_preset_B(1));
#else
                    thermalManager.setTargetHotend(ExtUI::getTargetTemp_celsius(ExtUI::E0), 0);
#if HAS_HEATED_BED
                    thermalManager.setTargetBed(ExtUI::getTargetTemp_celsius(ExtUI::heater_t::BED));
#endif
#endif
#else
                    // Fallback to preset 0 if preset 1 is unavailable
#if HAS_PREHEAT
                    thermalManager.setTargetHotend(ExtUI::getMaterial_preset_E(0), 0);
                    thermalManager.setTargetBed(ExtUI::getMaterial_preset_B(0));
#else
                    thermalManager.setTargetHotend(ExtUI::getTargetTemp_celsius(ExtUI::E0), 0);
#if HAS_HEATED_BED
                    thermalManager.setTargetBed(ExtUI::getTargetTemp_celsius(ExtUI::heater_t::BED));
#endif
#endif
#endif
                    break;
            }
            break;
    }

    ScreenHandler.ForceCompleteUpdate();
}

void TuneMenuHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_ADJUSTENTERKEY:
            switch (buttonValue) {
                case 2:
                    ScreenHandler.GotoScreen(ExtUI::isPrintingPaused() ? DGUSLCD_SCREEN_PRINT_PAUSED : DGUSLCD_SCREEN_PRINT_RUNNING, false);
                    break;

                case 3:
                    ScreenHandler.HandleFanToggle();
                break;

                case 4:
                    ScreenHandler.HandleLEDToggle();
                break;
            }
    }
}

void PrintRunningMenuHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_ADJUSTENTERKEY:
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_TUNING);
        break;

        case VP_BUTTON_PAUSEPRINTKEY:
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_DIALOG_PAUSE);
        break;

        case VP_BUTTON_STOPPRINTKEY:
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_DIALOG_STOP);
        break;
    }
}

void PrintPausedMenuHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP) {
        case VP_BUTTON_RESUMEPRINTKEY:
#if ENABLED(FILAMENT_RUNOUT_SENSOR)
                        runout.reset();
#endif
            // Mirror Pause/Stop: previously the UI presented a small confirmation
            // dialog (DIALOG_RESUME) before actually resuming. For pause-handshake
            // flows (filament change / purge) Marlin may be waiting on a user
            // confirmation; in that case we should treat this RESUME control as
            // performing the same handshake (setPauseMenuResponse + setUserConfirmed)
            // that the Confirm/Popup dialog would have done. If Marlin is not
            // waiting, preserve the old behavior and show the small resume
            // confirmation dialog.
            
            // Break any blocking heater waits during resume (like we do for pause)
            wait_for_heatup = false;
            #if HAS_RESUME_CONTINUE
            wait_for_user = false;
            #endif
            
            if (ExtUI::isWaitingOnUser()) {
            #if ENABLED(ADVANCED_PAUSE_FEATURE)
                ExtUI::setPauseMenuResponse(PAUSE_RESPONSE_RESUME_PRINT);
            #endif
                ExtUI::setUserConfirmed();
                ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING);
            }
            else {
                ScreenHandler.GotoScreen(DGUSLCD_SCREEN_DIALOG_RESUME);
            }
        break;

        case VP_BUTTON_ADJUSTENTERKEY:
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_TUNING);
        break;

        case VP_BUTTON_STOPPRINTKEY:
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_DIALOG_STOP);
        break;
    }
}

void PrintPauseDialogHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP){
        case VP_BUTTON_PAUSEPRINTKEY:
            switch (buttonValue) {
                case 2:
                    // User confirmed Pause: we will cancel any blocking waits
                    // (M109/M190/M0 etc.) by directly setting the wait flags.
                    // Save the current blocking-heating state locally so we can
                    // restore targets later from the same UI module without
                    // modifying global API.
                    store_blocking_heating_cr6();
                    
                    // Break wait loops immediately (equivalent to M108)
                    // wait_for_heatup = false;
                    #if HAS_RESUME_CONTINUE
                    wait_for_user = false;
                    #endif

                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_PAUSED);
                    ScreenHandler.setstatusmessagePGM(PSTR("Pausing print - please wait..."));
                    ExtUI::injectCommands_P(PSTR("M1125 P"));

                    break;

                case 3:
                    // User chose to NOT pause: return to the Print Running screen
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING);
                break;
            }
            break;
    }
}

// Handler for the small Resume confirmation dialog. Mirrors the Pause
// dialog pattern: case 2 confirms (perform resume/handshake), case 3
// cancels and returns the user to the paused screen.
void PrintResumeDialogHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP){
        case VP_BUTTON_RESUMEPRINTKEY:
            switch (buttonValue) {
                case 2:
                    // User confirmed Resume: perform the same logic previously
                    // located on the paused-screen resume button. For Advanced
                    // Pause flows this sets the response and clears the wait;
                    // otherwise it directly resumes.
                    #if ENABLED(FILAMENT_RUNOUT_SENSOR)
                        runout.reset();
                    #endif
                    
                    // Break any blocking heater waits during resume (like we do for pause)
                    wait_for_heatup = false;
                    #if HAS_RESUME_CONTINUE
                    wait_for_user = false;
                    #endif
                    
                    if (ExtUI::isWaitingOnUser()) {
                        #if ENABLED(ADVANCED_PAUSE_FEATURE)
                        ExtUI::setPauseMenuResponse(PAUSE_RESPONSE_RESUME_PRINT);
                        #endif
                        ExtUI::setUserConfirmed();
                    }
                    else {
                        ExtUI::injectCommands_P(PSTR("M1125 R"));
                    }
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING);
                    break;

                case 3:
                    // User chose to stay paused: return to the paused screen
                    // without changing Marlin state.
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_PAUSED);
                    break;
            }
            break;
    }
}

void PrintFinishMenuHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP){
        case VP_BUTTON_MAINENTERKEY:
            switch (buttonValue) {
                case 5:
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MAIN);
                    break;
                }
                break;
            }
        }

void FilamentRunoutHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP){
        case VP_BUTTON_RESUMEPRINTKEY:
            ExtUI::injectCommands_P(PSTR("M1125 R"));
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING);
        break;

        case VP_BUTTON_STOPPRINTKEY:
            ExtUI::stopPrint();
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MAIN);
        break;
    }
}

void PrintStopDialogHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP){
        case VP_BUTTON_STOPPRINTKEY:
            switch (buttonValue) {
                case 2:
                    // Stop is an immediate, global abort. Call ExtUI::stopPrint()
                    // unconditionally so the printer always terminates the job and
                    // runs its stop/park/cleanup path regardless of whether
                    // Advanced Pause is enabled or Marlin is waiting for a UI
                    // response. This keeps Stop behavior simple and predictable.
                    ExtUI::stopPrint();
                    // After stopping a print, show the print finish summary screen
                    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_FINISH);

                    // If axes aren't homed, show a clearer status message after a short delay
                    // so the user understands that auto-park tried to run but failed.
                    if (!all_axes_homed()) {
                        char msg[VP_M117_LEN] = {0};
                        bool first = true;
                        strcat(msg, "Cannot auto-park - axes not homed:");
                        if (!axis_was_homed(X_AXIS)) { strcat(msg, first ? " X" : ", X"); first = false; }
                        if (!axis_was_homed(Y_AXIS)) { strcat(msg, first ? " Y" : ", Y"); first = false; }
                        if (!axis_was_homed(Z_AXIS)) { strcat(msg, first ? " Z" : ", Z"); first = false; }

                        // Post after 2000 ms to avoid clobbering immediate UI transitions
                        ScreenHandler.PostDelayedStatusMessage(msg, 2000);
                    }
                break;

                case 3:
                    ScreenHandler.GotoScreen(ExtUI::isPrintingPaused() ? DGUSLCD_SCREEN_PRINT_PAUSED : DGUSLCD_SCREEN_PRINT_RUNNING);
                break;
            }
        break;
    }
}

void PreheatSettingsScreenHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    switch (var.VP){
        case VP_BUTTON_PREPAREENTERKEY:
            // Save button, save settings and go back
            ScreenHandler.RequestSaveSettings();
            ScreenHandler.PopToOldScreen();
        break;

        case VP_BUTTON_COOLDOWN: // You can't make this up
            // Back button, discard settings
            settings.load();
            ScreenHandler.PopToOldScreen();
            break;
    }
}

void MoveHandler(DGUS_VP_Variable &var, unsigned short buttonValue) {
    if (var.VP == VP_BUTTON_MOVEKEY) {
        switch (buttonValue) {
        case 1:
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MOVE10MM, false);
            break;
        case 2:
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MOVE1MM, false);
            break;
        case 3:
            ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MOVE01MM, false);
            break;
        case 4:
            // The original code temporarily modified probe.settings to disable
            // preheating for homing. Marlin no longer exposes probe.settings,
            // so skip that behavior and just execute the home command.
            ExtUI::injectCommands_P("G28");
            break;
        }
    }
}

// Register the page handlers
#define PAGE_HANDLER(SCRID, HDLRPTR) { .ScreenID=SCRID, .Handler=HDLRPTR },
const struct PageHandler PageHandlers[] PROGMEM = {
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_MAIN, MainMenuHandler)

    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_SETUP, SetupMenuHandler)

    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_ZOFFSET_LEVEL, LevelingModeHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_LEVELING, LevelingHandler)

    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_TEMP, TempMenuHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_TEMP_PLA, PreheatSettingsScreenHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_TEMP_ABS, PreheatSettingsScreenHandler)

    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_TUNING, TuneMenuHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_MOVE01MM, MoveHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_MOVE1MM, MoveHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_MOVE10MM, MoveHandler)

    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_FILAMENTRUNOUT1, FilamentRunoutHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_FILAMENTRUNOUT2, FilamentRunoutHandler)

    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_DIALOG_PAUSE, PrintPauseDialogHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_DIALOG_RESUME, PrintResumeDialogHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_DIALOG_STOP, PrintStopDialogHandler)

    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_PRINT_RUNNING, PrintRunningMenuHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_PRINT_PAUSED, PrintPausedMenuHandler)
    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_PRINT_FINISH, PrintFinishMenuHandler)


    PAGE_HANDLER(DGUSLCD_Screens::DGUSLCD_SCREEN_PREPARE, PrepareMenuHandler)

    // Terminating
    PAGE_HANDLER(static_cast<DGUSLCD_Screens>(0) ,0)
};

void DGUSCrealityDisplay_HandleReturnKeyEvent(DGUS_VP_Variable &var, void *val_ptr) {
  const struct PageHandler *map = PageHandlers;
  const uint16_t *ret;
  const DGUSLCD_Screens current_screen = DGUSScreenHandler::getCurrentScreen();

  while ((ret = (uint16_t*) pgm_read_ptr(&(map->Handler)))) {
    if ((map->ScreenID) == current_screen) {
        uint16_t button_value = uInt16Value(val_ptr);
        
        SERIAL_ECHOPAIR("Invoking handler for screen ", current_screen);
        SERIAL_ECHOLNPAIR("with VP=", var.VP, " value=", button_value);

        map->Handler(var, button_value);
        return;
    }

    map++;
  }
}

#endif
