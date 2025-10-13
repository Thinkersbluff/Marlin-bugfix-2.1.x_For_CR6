
#include "../../../../inc/MarlinConfigPre.h"

#if ENABLED(DGUS_LCD_UI_CR6_COMM)

#include "../DGUSDisplayDef.h"
#include "../DGUSDisplay.h"
#include "../DGUSScreenHandler.h"

#include "EstepsHandler.h"

#include "../../ui_api.h"
#include "../../../marlinui.h"

#include "../../../../module/temperature.h"
#include "../../../../module/settings.h"
#include "../../../../module/planner.h"
#include "../../../../gcode/gcode.h"

// Storage init
float EstepsHandler::set_esteps = 0;
float EstepsHandler::calculated_esteps = 0;
float EstepsHandler::remaining_filament = 0;
float EstepsHandler::mark_filament_mm = 0;
float EstepsHandler::filament_to_extrude = 0;
// Default calibration temperature: start unset (0). Init() will prefer stored UI setting when available.
celsius_t EstepsHandler::calibration_temperature = 0;

void EstepsHandler::Init() {
    // Use steps
    set_esteps = ExtUI::getAxisSteps_per_mm(ExtUI::E0);
    calculated_esteps = 0;

    // Reset
    filament_to_extrude = 100;
    mark_filament_mm = 120;
    remaining_filament = 0;

    // Use configured PLA temps + 10 degrees when preheat presets are available
        // Prefer a stored UI-specific calibration temperature if the DGUS screen saved it
        if (DGUSScreenHandler::Settings.calibration_temperature != 0) {
                calibration_temperature = DGUSScreenHandler::Settings.calibration_temperature;
        }
        else {
            // Use configured PLA temps + 10 degrees when preheat presets are available
            #if HAS_PREHEAT
                calibration_temperature = ExtUI::getMaterial_preset_E(0) + 10;
            #else
                // Fallback to the current target hotend temperature if PREHEAT is not enabled
                calibration_temperature = ExtUI::getTargetTemp_celsius(ExtUI::E0) + 10;
            #endif
        }

    // Welcome message
    SetStatusMessage(PSTR("Ready"));
}


void EstepsHandler::HandleStartButton(DGUS_VP_Variable &var, void *val_ptr) {
//#if ADVANCED_PAUSE_PURGE_LENGTH == 0
    // static_assert(ADVANCED_PAUSE_PURGE_LENGTH == 0, "Assuming PURGE_LENGTH is 0 so we can use M701");
//#else
    // ADVANCED_PAUSE_PURGE_LENGTH != 0 on this build; disable the behaviour that relies on M701
// #warning "CR6 UI: EstepsHandler expects ADVANCED_PAUSE_PURGE_LENGTH==0; behaviour modified"
//#endif

    // Validate
    if (calibration_temperature < EXTRUDE_MINTEMP) {
        SetStatusMessage(PSTR("Invalid temperature set"));
        return;
    }

    if (filament_to_extrude < 10) {
        SetStatusMessage(PSTR("Invalid extrusion length set"));
        return;
    }

    if (mark_filament_mm < filament_to_extrude) {
        SetStatusMessage(PSTR("Invalid mark length set"));
        return;
    }

    // Synchronous operation - disable back button
    DGUSSynchronousOperation syncOperation;
    syncOperation.start();

    // Prepare
    bool zAxisWasRelative = GcodeSuite::axis_is_relative(Z_AXIS);
    bool eAxisWasRelative = GcodeSuite::axis_is_relative(E_AXIS);
#if ENABLED(LIN_ADVANCE)
    float kFactor = planner.extruder_advance_K[0];
    planner.extruder_advance_K[0] = 0;
#endif
    GcodeSuite::set_e_relative();
    GcodeSuite::set_relative_mode(true);
    

    ExtUI::injectCommands_P("G0 Z5 F150");
    queue.advance();

    // Heat up if necessary
    if (abs(ExtUI::getActualTemp_celsius(ExtUI::E0) - calibration_temperature) > 2) {
        thermalManager.setTargetHotend(calibration_temperature, ExtUI::H0);

        SetStatusMessage(PSTR("Heating up..."));
        thermalManager.wait_for_hotend(ExtUI::H0, false);
    }

    planner.synchronize();
   
    // Set-up command
    SetStatusMessage(PSTR("Extruding..."));

    SERIAL_ECHOLNPAIR("filament_to_extrude: ", filament_to_extrude);
    char cmd[64];
    char lengthStr[16];
    dtostrf(filament_to_extrude, 1, 1, lengthStr);  // Convert float to string with 1 decimal place
    snprintf(cmd, sizeof(cmd), "G1 E%s F50", lengthStr);
    SERIAL_ECHOLNPAIR("Command: ", cmd);

    ExtUI::injectCommands(cmd);
    queue.advance();
    planner.synchronize();

    // Restore position
    ExtUI::injectCommands_P("G0 Z-5 F150");
    queue.advance();
    planner.synchronize();

    // Restore defaults
    if (!zAxisWasRelative) GcodeSuite::set_relative_mode(false);
    if (!eAxisWasRelative) GcodeSuite::set_e_absolute();
#if ENABLED(LIN_ADVANCE)
    planner.extruder_advance_K[0] = kFactor;
#endif
    // Done
    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_ESTEPS_CALIBRATION_RESULTS, false);
    ScreenHandler.Buzzer(0, 250);
    syncOperation.done();
    // Show transient instruction to the user and auto-clear
    DGUSScreenHandler::PostDelayedStatusMessage_P(PSTR("Measure remaining filament"), 0);
}

void EstepsHandler::HandleApplyButton(DGUS_VP_Variable &var, void *val_ptr) {
    if (abs(calculated_esteps) < 1) {
        // User intented to set e-steps directly
        ExtUI::setAxisSteps_per_mm(set_esteps, ExtUI::E0);
    } else {
        ExtUI::setAxisSteps_per_mm(calculated_esteps, ExtUI::E0);
    }

    SaveSettingsAndReturn(true);
}

void EstepsHandler::HandleBackButton(DGUS_VP_Variable &var, void *val_ptr) {
    // User intented to set e-steps directly
    ExtUI::setAxisSteps_per_mm(set_esteps, ExtUI::E0);

    SaveSettingsAndReturn(false);
}

void EstepsHandler::SaveSettingsAndReturn(bool fullConfirm) {
    // Save & reset
    settings.save();

    if (fullConfirm) ScreenHandler.Buzzer(0, 250);
    
    ScreenHandler.PopToOldScreen();
    if (fullConfirm) ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MAIN, false);

    DGUSScreenHandler::PostDelayedStatusMessage_P(PSTR("New e-steps value saved"), 0);
}

void EstepsHandler::HandleRemainingFilament(DGUS_VP_Variable &var, void *val_ptr) {
    ScreenHandler.DGUSLCD_SetFloatAsIntFromDisplay<1>(var, val_ptr);

    // Calculate
    constexpr float precision = 0.01;
    float actualExtrusion = mark_filament_mm - remaining_filament;
    if (actualExtrusion < (-precision)) {
        DGUSScreenHandler::PostDelayedStatusMessage_P(PSTR("Mark filament further"), 0);
        return;
    }

    if (actualExtrusion < precision) {
        DGUSScreenHandler::PostDelayedStatusMessage_P(PSTR("E-steps are correct"), 0);
        calculated_esteps = set_esteps;
        return;
    }

    float current_steps = ExtUI::getAxisSteps_per_mm(ExtUI::E0);
    SERIAL_ECHOLNPAIR("Current steps: ", current_steps);
    SERIAL_ECHOLNPAIR("Actual extrusion: ", actualExtrusion);

    float new_steps = (current_steps * filament_to_extrude) / actualExtrusion;
    SERIAL_ECHOLNPAIR("New steps: ", new_steps);

    calculated_esteps = new_steps;

    // Status update
    DGUSScreenHandler::PostDelayedStatusMessage_P(PSTR("Calculated new e-steps"), 0);
}

void EstepsHandler::SetStatusMessage(PGM_P statusMessage) {
    ScreenHandler.setstatusmessagePGM(statusMessage);
}

#endif