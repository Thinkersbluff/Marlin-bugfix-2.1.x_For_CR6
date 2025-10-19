#include "../../../../inc/MarlinConfigPre.h"
#if ENABLED(DGUS_LCD_UI_CR6_COMM)

#include "../DGUSDisplayDef.h"
#include "../DGUSDisplay.h"
#include "../DGUSScreenHandler.h"

#include "FilamentLoadUnloadHandler.h"

#include "../../ui_api.h"
#include "../../../marlinui.h"

#include "../../../../module/temperature.h"
#include "../../../../module/settings.h"
#include "../../../../module/planner.h"
#include "../../../../gcode/gcode.h"

celsius_t FilamentLoadUnloadHandler::nozzle_temperature = 0;
float FilamentLoadUnloadHandler::length = 0;

void FilamentLoadUnloadHandler::Init() {
#if HAS_PREHEAT
    nozzle_temperature = ExtUI::getMaterial_preset_E(0);
#else
    nozzle_temperature = ExtUI::getTargetTemp_celsius(ExtUI::E0);
#endif
    length = 150;

    if (ExtUI::isPrinting()) {
        nozzle_temperature = ExtUI::getTargetTemp_celsius(ExtUI::extruder_t::E0);
    }
}

void FilamentLoadUnloadHandler::HandleTemperature(DGUS_VP_Variable &var, void *val_ptr) {
    ScreenHandler.DGUSLCD_SetValueDirectly<uint16_t>(var, val_ptr);

    ValidateTemperatures();
}

void FilamentLoadUnloadHandler::HandleLoadUnloadButton(DGUS_VP_Variable &var, void *val_ptr) {
    // Common for load/unload -> determine minimum temperature
    if (length < 0.1) {
        SetStatusMessage("Invalid feed length");
        return;
    }

    if (ExtUI::isPrinting() && !ExtUI::isPrintingPaused()) {
        SetStatusMessage(PSTR("Please pause print first"));
        return;
    }
   
    DGUSSynchronousOperation syncOperation;
    uint16_t button_value = uInt16Value(val_ptr);
    switch (button_value) {
        case FILCHANGE_ACTION_LOAD_BUTTON:
            syncOperation.start();

            ChangeFilamentWithTemperature(PSTR("M701 L%.1f P0"));

            syncOperation.done();
        break;

        case FILCHANGE_ACTION_UNLOAD_BUTTON:
            syncOperation.start();

            ChangeFilamentWithTemperature(PSTR("M702 U%.1f"));

            syncOperation.done();
        break;
    }

}

void FilamentLoadUnloadHandler::ValidateTemperatures() {
    LIMIT(nozzle_temperature, EXTRUDE_MINTEMP, HEATER_0_MAXTEMP - HOTEND_OVERSHOOT);
}

void FilamentLoadUnloadHandler::ChangeFilamentWithTemperature(PGM_P command) {
    // Heat if necessary
    if (ExtUI::getActualTemp_celsius(ExtUI::E0) < nozzle_temperature && abs(ExtUI::getActualTemp_celsius(ExtUI::E0) - nozzle_temperature) > THERMAL_PROTECTION_HYSTERESIS) {
        SetStatusMessage(PSTR("Heating up..."));

        uint16_t target_celsius = nozzle_temperature;
        NOMORE(target_celsius, thermalManager.hotend_max_target(0));

        thermalManager.setTargetHotend(target_celsius, ExtUI::H0);
        thermalManager.wait_for_hotend(ExtUI::H0, false);
    }

    // Perform the actual filament change operation directly instead of
    // injecting M701/M702 strings. This keeps behavior local and lets the
    // UI-controlled `length` parameter be used for both touchscreen and
    // programmatic flows.
    SetStatusMessage(PSTR("Filament load/unload..."));

    // Ensure length has a reasonable default value
    if (length < 1.0f) length = 150.0f;

    // Decide whether to load or unload based on the provided command
    if (strstr_P(command, PSTR("M701"))) {
        // load_filament(slow_load_length, fast_load_length, purge_length, ...)
        const float slow_load_length = FILAMENT_CHANGE_SLOW_LOAD_LENGTH;
        const float fast_load_length = ABS(length);
        const float purge_length = 0;

        SERIAL_ECHOPAIR("load_filament: slow=", slow_load_length);
        SERIAL_ECHOPAIR(" fast=", fast_load_length);
        SERIAL_ECHOPAIR(" purge=", purge_length);
        SERIAL_ECHOPGM("\n");

        load_filament(
            slow_load_length, fast_load_length, purge_length,
            FILAMENT_CHANGE_ALERT_BEEPS,
            true,                                           // show_lcd
            thermalManager.still_heating(ExtUI::E0),        // pause_for_user
            PAUSE_MODE_LOAD_FILAMENT                        // pause_mode
            OPTARG(DUAL_X_CARRIAGE, 0)                      // Dual X target (0 for default)
        );
        // Ensure any temporary status/infobox is cleared and the UI returns to the Feed screen
        ScreenHandler.setstatusmessagePGM(nullptr);
        ScreenHandler.PopToOldScreen();
    }
    else if (strstr_P(command, PSTR("M702"))) {
        // For unload, call unload_filament with negative length
        const float unload_length = -ABS(length);

    SERIAL_ECHOPAIR("unload_filament: length=", unload_length);
    SERIAL_ECHOPGM("\n");
    unload_filament(unload_length, true, PAUSE_MODE_UNLOAD_FILAMENT);
    // Ensure any temporary status/infobox is cleared and the UI returns to the Feed screen
    ScreenHandler.setstatusmessagePGM(nullptr);
    ScreenHandler.PopToOldScreen();
    }

    SERIAL_ECHOPGM_P("- done");

    if (ScreenHandler.Settings.display_sound) ScreenHandler.Buzzer(500, 100);
    // Show a transient completion message and let the screen handler clear it automatically
    DGUSScreenHandler::PostDelayedStatusMessage_P(PSTR("Filament load/unload complete"), 10 /*ms delay before showing*/);
}

void FilamentLoadUnloadHandler::SetStatusMessage(PGM_P statusMessage) {
    ScreenHandler.setstatusmessagePGM(statusMessage);
}

#endif