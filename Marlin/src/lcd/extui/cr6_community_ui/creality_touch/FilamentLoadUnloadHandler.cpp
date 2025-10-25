#include "../../../../inc/MarlinConfigPre.h"
#if ENABLED(DGUS_LCD_UI_CR6_COMM)

#if !ENABLED(ADVANCED_PAUSE_FEATURE)
    // Provide conservative defaults for filament-change constants expected by the UI
    #ifndef FILAMENT_CHANGE_SLOW_LOAD_LENGTH
        #define FILAMENT_CHANGE_SLOW_LOAD_LENGTH 10.0f
    #endif
    #ifndef FILAMENT_CHANGE_ALERT_BEEPS
        #define FILAMENT_CHANGE_ALERT_BEEPS 10
    #endif
#endif

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
// When ADVANCED_PAUSE_FEATURE is disabled we provide conservative, local
// defaults for a short purge before unload and a pause duration to let the
// purge finish before performing the retract. These are file-scope so the
// UI can be tweaked here without changing headers.
static float purge_length = 5.0f;        // mm to extrude before retract
static millis_t unload_delay_ms = 2000;   // ms to wait after purge before retract

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

                #if ENABLED(ADVANCED_PAUSE_FEATURE)
                    load_filament(
                        slow_load_length, fast_load_length, purge_length,
                        FILAMENT_CHANGE_ALERT_BEEPS,
                        true,                                           // show_lcd
                        thermalManager.still_heating(ExtUI::E0),        // pause_for_user
                        PAUSE_MODE_LOAD_FILAMENT                        // pause_mode
                        OPTARG(DUAL_X_CARRIAGE, 0)                      // Dual X target (0 for default)
                    );
                #else
                    // Basic fallback: perform a simple extrude to load filament.
                    // Use a conservative feedrate and update planner positions.
                    const float feedrate_mm_s = 5.0f; // conservative 5 mm/s
                    const float extrude_len = fast_load_length;
                    // Simple extrude: buffer a line that only advances E.
                    xyze_pos_t dest = current_position;
                    dest.e += extrude_len;
                    planner.buffer_line(dest, feedrate_mm_s, TERN0(HAS_EXTRUDERS, active_extruder));
                    planner.synchronize();
                    // Update planner/current E position to reflect extrusion
                    current_position[E_AXIS] = dest.e;
                    planner.set_e_position_mm(dest.e);
                #endif
        // Ensure any temporary status/infobox is cleared and the UI returns to the Feed screen
        ScreenHandler.setstatusmessagePGM(nullptr);
        // ScreenHandler.PopToOldScreen();
    }
    else if (strstr_P(command, PSTR("M702"))) {
        // For unload, call unload_filament with negative length
        const float unload_length = -ABS(length);

        SERIAL_ECHOPAIR("unload_filament: length=", unload_length);
        SERIAL_ECHOPGM("\n");
        #if ENABLED(ADVANCED_PAUSE_FEATURE)
            unload_filament(unload_length, true, PAUSE_MODE_UNLOAD_FILAMENT);
        #else
            // Basic fallback when ADVANCED_PAUSE_FEATURE is disabled:
            // 1) extrude a short purge to ensure filament exit (purge_length)
            // 2) wait unload_delay_ms for the purge to complete
            // 3) perform the retract (negative extrusion) by unload_length
            const float feedrate_mm_s = 5.0f;

            // 1) Purge: advance E by purge_length
            xyze_pos_t dest = current_position;
            dest.e += purge_length;
            planner.buffer_line(dest, feedrate_mm_s, TERN0(HAS_EXTRUDERS, active_extruder));
            planner.synchronize();
            current_position[E_AXIS] = dest.e;
            planner.set_e_position_mm(dest.e);

            // 2) Wait for the purge to finish (cooperative delay)
            GcodeSuite::dwell(unload_delay_ms);

            // 3) Retract: move E back by unload_length (unload_length is negative)
            dest = current_position;
            dest.e += unload_length; // unload_length already negative for retract
            planner.buffer_line(dest, feedrate_mm_s, TERN0(HAS_EXTRUDERS, active_extruder));
            planner.synchronize();
            current_position[E_AXIS] = dest.e;
            planner.set_e_position_mm(dest.e);
        #endif
    // Ensure any temporary status/infobox is cleared and the UI returns to the Feed screen
    ScreenHandler.setstatusmessagePGM(nullptr);
    // ScreenHandler.PopToOldScreen();
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