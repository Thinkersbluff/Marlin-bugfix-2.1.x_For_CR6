
#include "../../../../inc/MarlinConfigPre.h"

#if ENABLED(DGUS_LCD_UI_CR6_COMM)

#include "../DGUSDisplayDef.h"
#include "../DGUSDisplay.h"
#include "../DGUSScreenHandler.h"

#include "PIDHandler.h"

#include "../../ui_api.h"
#include "../../../marlinui.h"

#include "../../../../module/temperature.h"
#include "../../../../module/settings.h"
#include "../../../../module/planner.h"
#include "../../../../gcode/gcode.h"

// Storage init
uint16_t PIDHandler::cycles = 0;
celsius_t PIDHandler::calibration_temperature = 0;
bool PIDHandler::fan_on = false;
PGM_P PIDHandler::result_message = nullptr;

void PIDHandler::Init() {
    // Reset
    // Default cycles
    if (DGUSScreenHandler::Settings.pid_cycles != 0) {
        cycles = DGUSScreenHandler::Settings.pid_cycles;
    }
    else {
        cycles = 3;
    }

    // Default fan preference
    if (DGUSScreenHandler::Settings.pid_fan_on) {
        fan_on = DGUSScreenHandler::Settings.pid_fan_on;
    }
    else {
        fan_on = ExtUI::getTargetFan_percent(ExtUI::fan_t::FAN0) > 10;
    }

    // Prefer stored PID nozzle calibration temperature if present
    if (DGUSScreenHandler::Settings.pid_nozzle_calibration_temperature != 0) {
        calibration_temperature = DGUSScreenHandler::Settings.pid_nozzle_calibration_temperature;
    }
    else {
      // Use configured PLA temps + 15 degrees when preheat presets are available
      #if HAS_PREHEAT
        calibration_temperature = ExtUI::getMaterial_preset_E(0) + 15;
      #else
        // Fallback to the current target hotend temperature if PREHEAT is not enabled
        calibration_temperature = ExtUI::getTargetTemp_celsius(ExtUI::E0) + 15;
      #endif
    }

    // Welcome message (transient)
    DGUSScreenHandler::PostDelayedStatusMessage_P(PSTR("Ready"), 0);
}


void PIDHandler::HandleStartButton(DGUS_VP_Variable &var, void *val_ptr) {

    // Validate
    if (calibration_temperature < EXTRUDE_MINTEMP) {
        SetStatusMessage(PSTR("Invalid temperature set"));
        return;
    }

    if (calibration_temperature > HEATER_0_MAXTEMP) {
        SetStatusMessage(PSTR("Invalid temperature set"));
        return;
    }

    if (cycles < 1) {
        SetStatusMessage(PSTR("Invalid number of cycles"));
        return;
    }

    // Synchronous operation - disable back button
    DGUSSynchronousOperation syncOperation;
    syncOperation.start();

    // Fan
    const auto prev_fan_percentage = ExtUI::getActualFan_percent(ExtUI::fan_t::FAN0);
    const uint8_t fan_speed = fan_on ? 255 : 0;

    // Set-up command
    SetStatusMessage(PSTR("PID tuning. Please wait..."));

    char cmd[64]; // Add a G4 to allow the fan speed to take effect
    sprintf_P(cmd, PSTR("M106 S%d\nG4 S2\nM303 S%d C%d U1"), fan_speed, calibration_temperature, cycles);
    SERIAL_ECHOLNPAIR("Executing: ", cmd);

    ExtUI::injectCommands(cmd);
    while (queue.has_commands_queued()) queue.advance();

    // Done
    ExtUI::setTargetFan_percent(prev_fan_percentage, ExtUI::fan_t::FAN0);

    ScreenHandler.Buzzer(0, 250);
    settings.save();
    syncOperation.done();

    if (result_message) DGUSScreenHandler::PostDelayedStatusMessage_P(result_message, 0);
}

void PIDHandler::SetStatusMessage(PGM_P statusMessage) {
    ScreenHandler.setstatusmessagePGM(statusMessage);
}

#endif