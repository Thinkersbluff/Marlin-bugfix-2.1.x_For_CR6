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

#include "../../../inc/MarlinConfigPre.h"

#if ENABLED(DGUS_LCD_UI_CR6_COMM)

#include "DGUSScreenHandler.h"
#include "DGUSDisplay.h"
#include "DGUSVPVariable.h"
#include "DGUSDisplayDef.h"
// Include handlers from creality_touch so we can persist handler state
#include "creality_touch/EstepsHandler.h"
#include "creality_touch/PIDHandler.h"

#include "../ui_api.h"
#include "../../../MarlinCore.h"
#include "../../../lcd/marlinui.h"
#include "../../../module/temperature.h"
#include "../../../module/motion.h"
#include "../../../module/settings.h"
#include "../../../gcode/queue.h"
#include "../../../module/planner.h"
#include "../../../sd/cardreader.h"
#include "../../../libs/duration_t.h"
#include "../../../module/printcounter.h"
#include "../../../feature/caselight.h"

// Forward declarations of M1125 helpers (defined in M1125.cpp)
bool M1125_CheckAndHandleHeaterTimeout();
uint32_t M1125_TimeoutRemainingSeconds();
uint32_t M1125_TimeoutOldRemainingAtContinue();
uint32_t M1125_TimeoutIntervalSeconds();
void M1125_TimeoutContinue();
void M1125_TimeoutContinueAction();
void M1125_TimeoutDisable();
bool M1125_IsPauseActive();

#if ENABLED(POWER_LOSS_RECOVERY)
  #include "../../../feature/powerloss.h"
#endif

#if HAS_COLOR_LEDS
  #include "../../../feature/leds/leds.h"
#endif

uint16_t DGUSScreenHandler::ConfirmVP;
bool DGUSScreenHandler::suppress_popup_pause_response = false;

void DGUSScreenHandler::SetSuppressPopupPauseResponse(bool suppress) {
  suppress_popup_pause_response = suppress;
  // Debug: log transitions of the suppression flag so we can correlate
  // with popup/show events and the ScreenChangeHook info-byte prints.
  SERIAL_ECHOPAIR("SetSuppressPopupPauseResponse -> ", suppress ? "ENABLED" : "DISABLED");
  SERIAL_ECHOLNPAIR(" current_screen=", current_screen);
}

#if ENABLED(SDSUPPORT)
  int16_t DGUSScreenHandler::top_file = 0;
  int16_t DGUSScreenHandler::file_to_print = 0;
  static ExtUI::FileList filelist;
#endif

// Storage initialization
constexpr uint8_t dwin_settings_version = 7; // Increased: new PID and ESteps fields added
creality_dwin_settings_t DGUSScreenHandler::Settings = {.settings_size = sizeof(creality_dwin_settings_t)};
DGUSLCD_Screens DGUSScreenHandler::current_screen;
DGUSLCD_Screens DGUSScreenHandler::past_screens[NUM_PAST_SCREENS] = {DGUSLCD_SCREEN_MAIN};
uint8_t DGUSScreenHandler::update_ptr;
uint16_t DGUSScreenHandler::skipVP;
bool DGUSScreenHandler::ScreenComplete;
bool DGUSScreenHandler::SaveSettingsRequested;

#if DGUS_SYNCH_OPS_ENABLED
bool DGUSScreenHandler::HasSynchronousOperation;
#else
// When synchronous ops are disabled at compile time, keep the flag defined once and initialized to false
bool DGUSScreenHandler::HasSynchronousOperation = false;
#endif

bool DGUSScreenHandler::HasScreenVersionMismatch;
uint8_t DGUSScreenHandler::MeshLevelIndex = -1;
uint8_t DGUSScreenHandler::MeshLevelIconIndex = -1;
bool DGUSScreenHandler::fwretract_available = TERN(FWRETRACT,  true, false);
bool DGUSScreenHandler::HasRGBSettings = TERN(HAS_COLOR_LEDS, true, false);

// Public accessor to allow external callers to detect whether a Confirm
// dialog is currently active (used to avoid overwriting confirm VPs).
bool DGUSScreenHandler::IsConfirmActive() {
  return (current_screen == DGUSLCD_SCREEN_CONFIRM) && (ConfirmVP != 0);
}

// Delayed status message storage (checked from loop())
// Owned buffer for delayed status message to avoid lifetime issues.
static char delayed_status_buffer[VP_M117_LEN] = {0};
static bool delayed_status_in_flash = false;
static millis_t delayed_status_until = 0;
// When a message is shown, keep a timeout to clear it (5s default)
static millis_t delayed_status_clear_at = 0;

void DGUSScreenHandler::PostDelayedStatusMessage(const char* msg, uint32_t delay_ms) {
  if (!msg) return;
  // Copy into owned buffer (truncate if necessary)
  strncpy(delayed_status_buffer, msg, sizeof(delayed_status_buffer)-1);
  delayed_status_buffer[sizeof(delayed_status_buffer)-1] = '\0';
  delayed_status_in_flash = false;
  delayed_status_until = millis() + delay_ms;
}

void DGUSScreenHandler::PostDelayedStatusMessage_P(PGM_P msg, uint32_t delay_ms) {
  if (!msg) return;
  // Copy from flash into owned buffer
  strcpy_P(delayed_status_buffer, msg);
  delayed_status_in_flash = true;
  delayed_status_until = millis() + delay_ms;
}

static_assert(GRID_MAX_POINTS_X == GRID_MAX_POINTS_Y, "Assuming bed leveling points is square");

constexpr uint16_t SkipMeshPoint = GRID_MAX_POINTS_X > MESH_LEVEL_EDGE_MAX_POINTS ? ((GRID_MAX_POINTS_X - 1) / (GRID_MAX_POINTS_X - MESH_LEVEL_EDGE_MAX_POINTS)) : 1;

void DGUSScreenHandler::sendinfoscreen(const char* line1, const char* line2, const char* line3, const char* line4, bool l1inflash, bool l2inflash, bool l3inflash, bool l4inflash) {
  DGUS_VP_Variable ramcopy;
  if (populate_VPVar(VP_MSGSTR1, &ramcopy)) {
    ramcopy.memadr = (void*) line1;
    l1inflash ? DGUSScreenHandler::DGUSLCD_SendStringToDisplayPGM(ramcopy) : DGUSScreenHandler::DGUSLCD_SendStringToDisplay(ramcopy);
  }
  if (populate_VPVar(VP_MSGSTR2, &ramcopy)) {
    ramcopy.memadr = (void*) line2;
    l2inflash ? DGUSScreenHandler::DGUSLCD_SendStringToDisplayPGM(ramcopy) : DGUSScreenHandler::DGUSLCD_SendStringToDisplay(ramcopy);
  }
  if (populate_VPVar(VP_MSGSTR3, &ramcopy)) {
    ramcopy.memadr = (void*) line3;
    l3inflash ? DGUSScreenHandler::DGUSLCD_SendStringToDisplayPGM(ramcopy) : DGUSScreenHandler::DGUSLCD_SendStringToDisplay(ramcopy);
  }
  if (populate_VPVar(VP_MSGSTR4, &ramcopy)) {
    ramcopy.memadr = (void*) line4;
    l4inflash ? DGUSScreenHandler::DGUSLCD_SendStringToDisplayPGM(ramcopy) : DGUSScreenHandler::DGUSLCD_SendStringToDisplay(ramcopy);
  }
}


void DGUSScreenHandler::Init() {
  dgusdisplay.InitDisplay();
}

void DGUSScreenHandler::RequestSaveSettings() {
  SaveSettingsRequested = true;
}

void DGUSScreenHandler::DefaultSettings() {
  Settings.settings_size = sizeof(creality_dwin_settings_t);
  Settings.settings_version = dwin_settings_version;

  Settings.led_state = false;

  Settings.display_standby = true;
  Settings.display_sound = true;

  Settings.standby_screen_brightness = 10;
  Settings.screen_brightness = 100;
  Settings.standby_time_seconds = 60;
  
  #if ENABLED(LED_COLOR_PRESETS)
  Settings.LastLEDColor = LEDLights::defaultLEDColor;
  #endif

  // Default: unset calibration temperature (0 == unset)
  Settings.calibration_temperature = 0;
  // PID defaults: unset so Init() falls back to preheat/defaults
  Settings.pid_nozzle_calibration_temperature = 0;
  Settings.pid_cycles = 0;
  Settings.pid_fan_on = false;
}

void DGUSScreenHandler::LoadSettings(const char* buff) {
  static_assert(
    ExtUI::eeprom_data_size >= sizeof(creality_dwin_settings_t),
    "Insufficient space in EEPROM for UI parameters"
  );

  // We'll accept older/smaller saved blobs and migrate them into the current struct.
  creality_dwin_settings_t eepromSettings;
  // Start with defaults (zero) so missing bytes are sane
  memset(&eepromSettings, 0, sizeof(eepromSettings));

  // Read header (settings_size + settings_version) first to determine how many bytes were stored
  const size_t header_bytes = sizeof(eepromSettings.settings_size) + sizeof(eepromSettings.settings_version);
  memcpy(&eepromSettings, buff, header_bytes);

  // Basic sanity checks
  if (eepromSettings.settings_size == 0 || eepromSettings.settings_size > ExtUI::eeprom_data_size) {
    SERIAL_ECHOLNPGM("Discarding DWIN LCD setting from EEPROM - size invalid");
    ScreenHandler.DefaultSettings();
    return;
  }

  // Copy as many bytes as the stored size contains (migrate older layouts)
  const size_t copyBytes = ((size_t)eepromSettings.settings_size < sizeof(creality_dwin_settings_t)) ? (size_t)eepromSettings.settings_size : sizeof(creality_dwin_settings_t);
  memcpy(&eepromSettings, buff, copyBytes);

  // If version mismatch, just warn but still use what we have (we've migrated fields we know)
  if (eepromSettings.settings_version != dwin_settings_version) {
    SERIAL_ECHOLNPGM("Warning: DWIN LCD setting version mismatch - attempting best-effort load");
  }

  // Copy into final location
  SERIAL_ECHOLNPGM("Loading DWIN LCD setting from EEPROM (migrated)");
  memcpy(&Settings, &eepromSettings, sizeof(creality_dwin_settings_t));

  // Apply settings
  caselight.on = Settings.led_state;
  caselight.update(Settings.led_state);

  #if HAS_COLOR_LEDS_PREFERENCES
  leds.set_color(Settings.LastLEDColor);
  #endif

  ScreenHandler.SetTouchScreenConfiguration();
}

void DGUSScreenHandler::StoreSettings(char* buff) {
  static_assert(
    ExtUI::eeprom_data_size >= sizeof(creality_dwin_settings_t),
    "Insufficient space in EEPROM for UI parameters"
  );

  // Update settings from Marlin state, if necessary
  Settings.led_state = caselight.on;

  #if HAS_COLOR_LEDS_PREFERENCES
  Settings.LastLEDColor = leds.color;
  #endif

  // Persist current calibration temperature from EstepsHandler (0 == unset)
  Settings.calibration_temperature = EstepsHandler::calibration_temperature;
  // Persist runtime PID handler settings (Nozzle PID)
  Settings.pid_nozzle_calibration_temperature = PIDHandler::calibration_temperature;
  Settings.pid_cycles = PIDHandler::cycles;
  Settings.pid_fan_on = PIDHandler::fan_on;

  // Ensure header reflects current struct
  Settings.settings_size = sizeof(creality_dwin_settings_t);
  Settings.settings_version = dwin_settings_version;

  // Write to buffer
  SERIAL_ECHOLNPGM("Saving DWIN LCD setting to EEPROM");
  memcpy(buff, &Settings, sizeof(creality_dwin_settings_t));
}

void DGUSScreenHandler::SetTouchScreenConfiguration() {
  LIMIT(Settings.screen_brightness, 10, 100); // Prevent a possible all-dark screen
  LIMIT(Settings.standby_time_seconds, 10, 655); // Prevent a possible all-dark screen for standby, yet also don't go higher than the DWIN limitation

  dgusdisplay.SetTouchScreenConfiguration(Settings.display_standby, Settings.display_sound, Settings.standby_screen_brightness, Settings.screen_brightness, Settings.standby_time_seconds);
}

void DGUSScreenHandler::KillScreenCalled() {
  // If killed, always fully wake up
  dgusdisplay.SetTouchScreenConfiguration(false, true, 100, 100, 100 /*Doesn't really matter*/);

  // Hey! Something is going on!
  Buzzer(1000 /*ignored*/, 880);
}


// Lightweight adapter used by other modules that don't want to include
// the full DGUSScreenHandler header. Forwards to the class method.
#if ENABLED(DGUS_LCD_UI_CR6_COMM)
void DGUS_Buzzer(const uint16_t duration, const uint16_t frequency) {
  DGUSScreenHandler::Buzzer(frequency, duration);
}
#endif

void DGUSScreenHandler::OnPowerlossResume() {
  GotoScreen(DGUSLCD_SCREEN_POWER_LOSS);

  // Send print filename
  dgusdisplay.WriteVariable(VP_SD_Print_Filename, PrintJobRecovery::info.sd_filename, VP_SD_FileName_LEN, true);
}

void DGUSScreenHandler::HandleUserConfirmationPopUp(uint16_t VP, const char* line1, const char* line2, const char* line3, const char* line4, bool l1, bool l2, bool l3, bool l4) {
  if (current_screen == DGUSLCD_SCREEN_CONFIRM) {
    // Already showing a pop up, so we need to cancel that first.
    PopToOldScreen();
  }

  ConfirmVP = VP;
  // Debug: record which VP is being used for the confirmation so we can
  // correlate display returns with firmware actions.
  SERIAL_ECHOLNPAIR("ConfirmVP set to ", ConfirmVP);
  sendinfoscreen(line1, line2, line3, line4, l1, l2, l3, l4);
  ScreenHandler.GotoScreen(DGUSLCD_SCREEN_CONFIRM);
}

void DGUSScreenHandler::HandleDevelopmentTestButton(DGUS_VP_Variable &var, void *val_ptr) {
  // Handle the button press only after 3 taps, so that a regular user won't tap it by accident
  static uint8_t tap_count = 0;

  if (++tap_count <= 3) return;

  // Get button value
  uint16_t button_value = swap16(*static_cast<uint16_t*>(val_ptr));

  // Act on it
  switch (button_value) {
    case VP_DEVELOPMENT_HELPER_BUTTON_ACTION_FIRMWARE_UPDATE:
      ExtUI::injectCommands_P(PSTR("M997"));
    break;

    case VP_DEVELOPMENT_HELPER_BUTTON_ACTION_TO_MAIN_MENU:
      setstatusmessagePGM(PSTR("Dev action: main menu"));
      GotoScreen(DGUSLCD_SCREEN_MAIN, false);
    break;

    case VP_DEVELOPMENT_HELPER_BUTTON_ACTION_RESET_DISPLAY:
      setstatusmessagePGM(PSTR("Dev action: reset DGUS"));
      dgusdisplay.ResetDisplay();
    break;

    default:
      setstatusmessagePGM(PSTR("Dev action: unknown"));
    break;
  }
}

void setStatusMessage(const char *msg, bool forceScrolling) {
  const bool needs_scrolling = forceScrolling || strlen(msg) > M117_STATIC_DISPLAY_LEN;

  DGUS_VP_Variable ramcopy;

  // Update static message to either NULL or the value
  if (populate_VPVar(VP_M117_STATIC, &ramcopy)) {
    ramcopy.memadr = (void*) (needs_scrolling ? NUL_STR : msg);
    DGUSScreenHandler::DGUSLCD_SendStringToDisplay(ramcopy);
  }

  // Update scrolling message to either NULL or the value
  if (populate_VPVar(VP_M117, &ramcopy)) {
    ramcopy.memadr = (void*) (needs_scrolling ? msg : NUL_STR);
    DGUSScreenHandler::DGUSLCD_SendScrollingStringToDisplay(ramcopy);
  }
}

void DGUSScreenHandler::setstatusmessage(const char *msg) {
  setStatusMessage(msg, false);
}

void DGUSScreenHandler::setstatusmessagePGM(PGM_P const msg) {
  const bool needs_scrolling = strlen_P(msg) > M117_STATIC_DISPLAY_LEN;

  DGUS_VP_Variable ramcopy;

   // Update static message to either NULL or the value
  if (populate_VPVar(VP_M117_STATIC, &ramcopy)) {
    ramcopy.memadr = (void*) (needs_scrolling ? nullptr : msg);
    DGUSLCD_SendStringToDisplayPGM(ramcopy);
  }
  
  // Update scrolling message to either NULL or the value
  if (populate_VPVar(VP_M117, &ramcopy)) {
    ramcopy.memadr = (void*) (needs_scrolling ? msg : nullptr);
    DGUSLCD_SendScrollingStringToDisplayPGM(ramcopy);
  }
}

// Send an 8 bit or 16 bit value to the display.
void DGUSScreenHandler::DGUSLCD_SendWordValueToDisplay(DGUS_VP_Variable &var) {
  if (var.memadr) {
    //DEBUG_ECHOPAIR(" DGUS_LCD_SendWordValueToDisplay ", var.VP);
    //DEBUG_ECHOLNPAIR(" data ", *(uint16_t *)var.memadr);
    if (var.size > 1)
      dgusdisplay.WriteVariable(var.VP, *(int16_t*)var.memadr);
    else
      dgusdisplay.WriteVariable(var.VP, *(int8_t*)var.memadr);
  }
}

// Send an uint8_t between 0 and 255 to the display, but scale to a percentage (0..100)
void DGUSScreenHandler::DGUSLCD_SendPercentageToDisplay(DGUS_VP_Variable &var) {
  if (var.memadr) {
    //DEBUG_ECHOPAIR(" DGUS_LCD_SendWordValueToDisplay ", var.VP);
    //DEBUG_ECHOLNPAIR(" data ", *(uint16_t *)var.memadr);
    uint16_t tmp = *(uint8_t *) var.memadr +1 ; // +1 -> avoid rounding issues for the display.
    tmp = map(tmp, 0, 255, 0, 100);
    dgusdisplay.WriteVariable(var.VP, tmp);
  }
}

// Send the current print progress to the display.
void DGUSScreenHandler::DGUSLCD_SendPrintProgressToDisplay(DGUS_VP_Variable &var) {
  uint16_t tmp = ExtUI::getProgress_percent();
  dgusdisplay.WriteVariable(var.VP, tmp);
}

// Send the current print time to the display.
// It is using a hex display for that: It expects BSD coded data in the format xxyyzz
void DGUSScreenHandler::DGUSLCD_SendPrintTimeToDisplay(DGUS_VP_Variable &var) {
  // Clear if changed and we shouldn't display
  static bool last_shouldDisplay = true;
  bool shouldDisplay = ui.get_remaining_time() == 0;
  if (last_shouldDisplay != shouldDisplay) {
    if (!shouldDisplay) {
      dgusdisplay.WriteVariable(VP_PrintTime, nullptr, var.size, true);
    }
  }

  last_shouldDisplay = shouldDisplay;
  if (!shouldDisplay) return;

  // Write if changed
  duration_t elapsed = print_job_timer.duration();

  static uint32_t last_elapsed;
  if (elapsed == last_elapsed) {
    return;
  }

  char buf[32];
  elapsed.toString(buf);
  dgusdisplay.WriteVariable(VP_PrintTime, buf, var.size, true);

  last_elapsed = elapsed.second();
}

void DGUSScreenHandler::DGUSLCD_SendPrintTimeWithRemainingToDisplay(DGUS_VP_Variable &var) {
  // Clear if changed and we shouldn't display
  static bool last_shouldDisplay = true;
  bool shouldDisplay = ui.get_remaining_time() != 0;
  if (last_shouldDisplay != shouldDisplay) {
    if (!shouldDisplay) {
      dgusdisplay.WriteVariable(VP_PrintTimeWithRemainingVisible, nullptr, var.size, true);
    }
  }

  last_shouldDisplay = shouldDisplay;
  if (!shouldDisplay) return;

  // Write if changed
  duration_t elapsed = print_job_timer.duration();

  static uint32_t last_elapsed;
  if (elapsed == last_elapsed) {
    return;
  }

  char buf[32];
  elapsed.toString(buf);
  dgusdisplay.WriteVariable(VP_PrintTimeWithRemainingVisible, buf, var.size, true);

  last_elapsed = elapsed.second();
}

// Send the current print time to the display.
// It is using a hex display for that: It expects BSD coded data in the format xxyyzz
void DGUSScreenHandler::DGUSLCD_SendPrintTimeRemainingToDisplay(DGUS_VP_Variable &var) { 
#if ENABLED(SHOW_REMAINING_TIME)
  static uint32_t lastRemainingTime = -1;
  uint32_t remaining_time = ui.get_remaining_time();
  if (lastRemainingTime == remaining_time) {
    return;
  }

  bool has_remaining_time = remaining_time != 0;

  // Update display of SPs (toggle between large and small print timer)
  if (has_remaining_time) {
    dgusdisplay.WriteVariable(VP_HideRemainingTime_Ico, ICON_REMAINING_VISIBLE);
  } else {
    dgusdisplay.WriteVariable(VP_HideRemainingTime_Ico, ICON_REMAINING_HIDDEN);
  }

  if (!has_remaining_time) {
    // Clear remaining time
    dgusdisplay.WriteVariable(VP_PrintTimeRemaining, nullptr, var.size, true);
    lastRemainingTime = remaining_time;
    return;
  }

  // Send a progress update to the display if anything is different.
  // This allows custom M117 commands to override the displayed string if desired.

  // Remaining time is seconds. When Marlin accepts a M73 R[minutes] command, it multiplies
  // the R value by 60 to make a number of seconds. But... Marlin can also predict time
  // if the M73 R command has not been used. So we should be good either way.
  duration_t remaining(remaining_time);
  constexpr size_t buffer_size = 21;

  // Write the duration
  char buffer[buffer_size] = {0};
  remaining.toString(buffer);

  dgusdisplay.WriteVariable(VP_PrintTimeRemaining, buffer, var.size, true);

  lastRemainingTime = remaining_time;
#endif
}

void DGUSScreenHandler::DGUSLCD_SendAboutFirmwareWebsite(DGUS_VP_Variable &var) {
  const char* websiteUrl = PSTR(WEBSITE_URL);

  dgusdisplay.WriteVariablePGM(var.VP, websiteUrl, var.size, true);
}

void DGUSScreenHandler::DGUSLCD_SendAboutFirmwareVersion(DGUS_VP_Variable &var) {
  const char* fwVersion = PSTR(SHORT_BUILD_VERSION);

  dgusdisplay.WriteVariablePGM(var.VP, fwVersion, var.size, true);
}

void DGUSScreenHandler::DGUSLCD_SendAboutPrintSize(DGUS_VP_Variable &var) {
  char PRINTSIZE[VP_PRINTER_BEDSIZE_LEN] = {0};
  sprintf(PRINTSIZE,"%dx%dx%d", X_BED_SIZE, Y_BED_SIZE, Z_MAX_POS);

  dgusdisplay.WriteVariablePGM(var.VP, &PRINTSIZE, sizeof(PRINTSIZE), true);
}


// Send an uint8_t between 0 and 100 to a variable scale to 0..255
void DGUSScreenHandler::DGUSLCD_PercentageToUint8(DGUS_VP_Variable &var, void *val_ptr) {
  if (var.memadr) {
    uint16_t value = swap16(*(uint16_t*)val_ptr);
    *(uint8_t*)var.memadr = map(constrain(value, 0, 100), 0, 100, 0, 255);
  }
}

// Sends a (RAM located) string to the DGUS Display
// (Note: The DGUS Display does not clear after the \0, you have to
// overwrite the remainings with spaces.// var.size has the display buffer size!
void DGUSScreenHandler::DGUSLCD_SendStringToDisplay(DGUS_VP_Variable &var) {
  char *tmp = (char*) var.memadr;
  dgusdisplay.WriteVariable(var.VP, tmp, var.size, true, DWIN_DEFAULT_FILLER_CHAR);
}

// Sends a (RAM located) string to the DGUS Display
// (Note: The DGUS Display does not clear after the \0, you have to
// overwrite the remainings with spaces.// var.size has the display buffer size!
void DGUSScreenHandler::DGUSLCD_SendScrollingStringToDisplay(DGUS_VP_Variable &var) {
  char *tmp = (char*) var.memadr;
  dgusdisplay.WriteVariable(var.VP, tmp, var.size, true, DWIN_SCROLLER_FILLER_CHAR);
}

// Sends a (flash located) string to the DGUS Display
// (Note: The DGUS Display does not clear after the \0, you have to
// overwrite the remainings with spaces.// var.size has the display buffer size!
void DGUSScreenHandler::DGUSLCD_SendStringToDisplayPGM(DGUS_VP_Variable &var) {
  char *tmp = (char*) var.memadr;
  dgusdisplay.WriteVariablePGM(var.VP, tmp, var.size, true, DWIN_DEFAULT_FILLER_CHAR);
}


// Sends a (flash located) string to the DGUS Display
// (Note: The DGUS Display does not clear after the \0, you have to
// overwrite the remainings with spaces.// var.size has the display buffer size!
void DGUSScreenHandler::DGUSLCD_SendScrollingStringToDisplayPGM(DGUS_VP_Variable &var) {
  char *tmp = (char*) var.memadr;
  dgusdisplay.WriteVariablePGM(var.VP, tmp, var.size, true, DWIN_SCROLLER_FILLER_CHAR);
}

#if HAS_PID_HEATING
  void DGUSScreenHandler::DGUSLCD_SendTemperaturePID(DGUS_VP_Variable &var) {
    float value = *(float *)var.memadr;
    float valuesend = 0;
    switch (var.VP) {
      default: return;
      #if HOTENDS >= 1
        case VP_E0_PID_P: valuesend = value; break;
        case VP_E0_PID_I: valuesend = unscalePID_i(value); break;
        case VP_E0_PID_D: valuesend = unscalePID_d(value); break;
      #endif
      #if HAS_HEATED_BED
        case VP_BED_PID_P: valuesend = value; break;
        case VP_BED_PID_I: valuesend = unscalePID_i(value); break;
        case VP_BED_PID_D: valuesend = unscalePID_d(value); break;
      #endif
    }

    valuesend *= cpow(10, 1);
    union { int16_t i; char lb[2]; } endian;

    char tmp[2];
    endian.i = valuesend;
    tmp[0] = endian.lb[1];
    tmp[1] = endian.lb[0];
    dgusdisplay.WriteVariable(var.VP, tmp, 2);
  }
#endif

// Send fan status value to the display.
#if HAS_FAN
  void DGUSScreenHandler::DGUSLCD_SendFanStatusToDisplay(DGUS_VP_Variable &var) {
    if (var.memadr) {
      DEBUG_ECHOPAIR(" DGUSLCD_SendFanStatusToDisplay ", var.VP);
      DEBUG_ECHOLNPAIR(" data ", *(uint8_t *)var.memadr);
      uint16_t data_to_send = ICON_TOGGLE_OFF;
      if (*(uint8_t *) var.memadr) data_to_send = ICON_TOGGLE_ON;
      dgusdisplay.WriteVariable(var.VP, data_to_send);
    }
  }

  void DGUSScreenHandler::DGUSLCD_SendFanSpeedToDisplay(DGUS_VP_Variable &var) {
    if (var.memadr) {
      int16_t data_to_send = static_cast<int16_t>(round(ExtUI::getTargetFan_percent(ExtUI::fan_t::FAN0)));
      dgusdisplay.WriteVariable(var.VP, data_to_send);
    }
  }
#endif

// Send heater status value to the display.
void DGUSScreenHandler::DGUSLCD_SendHeaterStatusToDisplay(DGUS_VP_Variable &var) {
  if (var.memadr) {
    DEBUG_ECHOPAIR(" DGUSLCD_SendHeaterStatusToDisplay ", var.VP);
    DEBUG_ECHOLNPAIR(" data ", *(int16_t *)var.memadr);
    uint16_t data_to_send = 0;
    if (*(int16_t *) var.memadr) data_to_send = 1;
    dgusdisplay.WriteVariable(var.VP, data_to_send);
  }
}

#if ENABLED(DGUS_UI_WAITING)
  void DGUSScreenHandler::DGUSLCD_SendWaitingStatusToDisplay(DGUS_VP_Variable &var) {
    // In FYSETC UI design there are 10 statuses to loop
    static uint16_t period = 0;
    static uint16_t index = 0;
    //DEBUG_ECHOPAIR(" DGUSLCD_SendWaitingStatusToDisplay ", var.VP);
    //DEBUG_ECHOLNPAIR(" data ", swap16(index));
    if (period++ > DGUS_UI_WAITING_STATUS_PERIOD) {
      dgusdisplay.WriteVariable(var.VP, index);
      //DEBUG_ECHOLNPAIR(" data ", swap16(index));
      if (++index >= DGUS_UI_WAITING_STATUS) index = 0;
      period = 0;
    }
  }
#endif

#if ENABLED(SDSUPPORT)

  void DGUSScreenHandler::ScreenChangeHookIfSD(DGUS_VP_Variable &var, void *val_ptr) {
    // default action executed when there is a SD card, but not printing
    if (ExtUI::isMediaInserted() && !ExtUI::isPrintingFromMedia()) {
      ScreenChangeHook(var, val_ptr);
      GotoScreen(current_screen);
      return;
    }

    // if we are printing, we jump to two screens after the requested one.
    // This should host e.g a print pause / print abort / print resume dialog.
    // This concept allows to recycle this hook for other file
    if (ExtUI::isPrintingFromMedia() && !card.flag.abort_sd_printing) {
      GotoScreen(DGUSLCD_SCREEN_SDPRINTMANIPULATION);
      return;
    }

    // Don't let the user in the dark why there is no reaction.
    if (!ExtUI::isMediaInserted()) {
      setstatusmessagePGM(GET_TEXT(MSG_NO_MEDIA));
      return;
    }
    if (card.flag.abort_sd_printing) {
      setstatusmessagePGM(GET_TEXT(MSG_MEDIA_ABORTING));
      return;
    }
  }

  void DGUSScreenHandler::DGUSLCD_SD_ScrollFilelist(DGUS_VP_Variable& var, void *val_ptr) {
    auto old_top = top_file;
    const int16_t scroll = (int16_t)swap16(*(uint16_t*)val_ptr);
    if (scroll) {
      top_file += scroll;
      DEBUG_ECHOPAIR("new topfile calculated:", top_file);
      if (top_file < 0) {
        top_file = 0;
        DEBUG_ECHOLNPGM("Top of filelist reached");
      }
      else {
        int16_t max_top = filelist.count() -  DGUS_SD_FILESPERSCREEN;
        NOLESS(max_top, 0);
        NOMORE(top_file, max_top);
      }
      DEBUG_ECHOPAIR("new topfile adjusted:", top_file);
    }
    else {
      if (!filelist.isAtRootDir()) {
        filelist.upDir();
        top_file = 0;
        ForceCompleteUpdate();
      } else {
        // Navigate back to home
        GotoScreen(DGUSLCD_SCREEN_MAIN);
      }
    }

    if (old_top != top_file) ForceCompleteUpdate();
  }

  void DGUSScreenHandler::DGUSLCD_SD_FileSelected(DGUS_VP_Variable &var, void *val_ptr) {
    uint16_t touched_nr = (int16_t)swap16(*(uint16_t*)val_ptr) + top_file;

    DEBUG_ECHOLNPAIR("Selected file: ", touched_nr);

    if (touched_nr > filelist.count()) return;
    if (!filelist.seek(touched_nr)) return;
    if (filelist.isDir()) {
      filelist.changeDir(filelist.shortFilename());
      top_file = 0;
      ForceCompleteUpdate();
      return;
    }

    // Send print filename
    dgusdisplay.WriteVariable(VP_SD_Print_Filename, filelist.filename(), VP_SD_FileName_LEN, true);

    // Setup Confirmation screen
    file_to_print = touched_nr;
    HandleUserConfirmationPopUp(VP_SD_FileSelectConfirm, PSTR("Print file"), filelist.filename(), PSTR("from SD Card?"), nullptr, true, false, true, true);
  }

  void DGUSScreenHandler::SetPrintingFromHost() {
    const char* printFromHostString = PSTR("Printing from host");
    dgusdisplay.WriteVariablePGM(VP_SD_Print_Filename, printFromHostString, VP_SD_FileName_LEN, true);
  }

  void DGUSScreenHandler::DGUSLCD_SD_StartPrint(DGUS_VP_Variable &var, void *val_ptr) {
    if (!filelist.seek(file_to_print)) return;
    // Ensure the printer is homed before starting this print. Queue a G28 first
    // so the homing completes before the SD print begins.
    queue.inject_P(G28_STR);
    ExtUI::printFile(filelist.shortFilename());
    ScreenHandler.GotoScreen(
      DGUSLCD_SCREEN_SDPRINTMANIPULATION
    );
  }

  void DGUSScreenHandler::DGUSLCD_SD_SendFilename(DGUS_VP_Variable& var) {
    uint16_t target_line = (var.VP - VP_SD_FileName0) / VP_SD_FileName_LEN;
    if (target_line > DGUS_SD_FILESPERSCREEN) return;
    char tmpfilename[VP_SD_FileName_LEN + 1] = "";
    var.memadr = (void*)tmpfilename;
    if (filelist.seek(top_file + target_line))
      snprintf_P(tmpfilename, VP_SD_FileName_LEN, PSTR("%s%c"), filelist.filename(), filelist.isDir() ? '/' : 0);
    DGUSLCD_SendStringToDisplay(var);
  }

  void DGUSScreenHandler::SDCardInserted() {
    top_file = 0;
    filelist.refresh();
    auto cs = ScreenHandler.getCurrentScreen();
    if (cs == DGUSLCD_SCREEN_MAIN || cs == DGUSLCD_SCREEN_SETUP)
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_SDFILELIST);
  }

  void DGUSScreenHandler::SDCardRemoved() {
    // Always handle media removal so the UI can react (navigate back to a safe screen)
    if (current_screen == DGUSLCD_SCREEN_SDFILELIST
        || (current_screen == DGUSLCD_SCREEN_CONFIRM && (ConfirmVP == VP_SD_AbortPrintConfirmed || ConfirmVP == VP_SD_FileSelectConfirm))
        || current_screen == DGUSLCD_SCREEN_SDPRINTMANIPULATION
    ) ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MAIN, false);
  }

  void DGUSScreenHandler::SDCardMounted() {
    // Clear any previous SD card error message when card successfully mounts
    ScreenHandler.setstatusmessage("SD Card Ready");
  }

  void DGUSScreenHandler::SDCardError() {
    DGUSScreenHandler::SDCardRemoved();
    ScreenHandler.sendinfoscreen(PSTR("NOTICE"), nullptr, PSTR("SD card error"), nullptr, true, true, true, true);
    ScreenHandler.GotoScreen(DGUSLCD_SCREEN_POPUP);
  }

#endif // SDSUPPORT

void DGUSScreenHandler::FilamentRunout() {
  ScreenHandler.sendinfoscreen(PSTR("Load new"), PSTR("filament."), PSTR(" "), PSTR("Filament Runout"), true, true, true, true);
  ScreenHandler.GotoScreen(DGUSLCD_SCREEN_POPUP);
}

void DGUSScreenHandler::OnFactoryReset() {
  ScreenHandler.DefaultSettings();
  // Ensure the default UI settings are persisted to EEPROM so this is a true factory reset
  ScreenHandler.RequestSaveSettings();
  ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MAIN);
}

// DGUS-specific buzzer implementation — always used for this UI.
void DGUSScreenHandler::Buzzer(const uint16_t frequency, const uint16_t duration) {
  // Frequency is fixed - duration is not but in 8 ms steps
  const uint8_t durationUnits = static_cast<uint8_t>(duration / 8);

  DEBUG_ECHOLNPAIR("Invoking buzzer with units: ", durationUnits);
  const unsigned char buzzerCommand[] = { 0x00, durationUnits, 0x40 /*Volume*/, 0x02 };

  // WAE_Music_Play_Set
  dgusdisplay.WriteVariable(0xA0, buzzerCommand, sizeof(buzzerCommand));
}

bool DGUSScreenHandler::HandlePendingUserConfirmation() {
  if (!ExtUI::isWaitingOnUser()) {
    return false;
  }

  // Switch to the resume screen
  // Show host-specific running screen when appropriate
  if (!ExtUI::isPrintingFromMedia()) ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING_HOST, false);
  else ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING, false);

  // We might be re-entrant here
  ExtUI::setUserConfirmed();

  return true;
}

// Synchronous-operation helpers
#if DGUS_SYNCH_OPS_ENABLED
void DGUSScreenHandler::SetSynchronousOperationStart() {
  HasSynchronousOperation = true;
  ForceCompleteUpdate();
}

void DGUSScreenHandler::SetSynchronousOperationFinish() {
  HasSynchronousOperation = false;
}

// Begin/End helpers for purge flows
void DGUSScreenHandler::BeginPurgeOperation() {
  // Start a synchronous operation and immediately update busy indicators
  SetSynchronousOperationStart();
  // Ensure the busy state is sent so the display shows the throbber now
  DGUS_VP_Variable tmp;
  if (populate_VPVar(VP_BUSY_ANIM_STATE, &tmp)) SendBusyState(tmp);
}

void DGUSScreenHandler::EndPurgeOperation() {
  // Finish and refresh UI so the busy indicators are cleared
  SetSynchronousOperationFinish();
  ForceCompleteUpdate();
}
#endif

void DGUSScreenHandler::SendBusyState(DGUS_VP_Variable &var) {
  dgusdisplay.WriteVariable(VP_BACK_BUTTON_STATE, HasSynchronousOperation ? ICON_BACK_BUTTON_DISABLED : ICON_BACK_BUTTON_ENABLED);
  dgusdisplay.WriteVariable(VP_BUSY_ANIM_STATE, HasSynchronousOperation ? ICON_THROBBER_ANIM_ON : ICON_THROBBER_ANIM_OFF);
}

void DGUSScreenHandler::OnHomingStart() {
  ScreenHandler.SetSynchronousOperationStart();
  ScreenHandler.GotoScreen(DGUSLCD_SCREEN_AUTOHOME);
}

void DGUSScreenHandler::OnHomingComplete() {
  SERIAL_ECHOLNPGM("DGUSScreenHandler::OnHomingComplete called");
  SERIAL_ECHOLNPAIR(" current_screen=", ScreenHandler.getCurrentScreen());
  SERIAL_ECHOLNPAIR(" past_screens[0]=", ScreenHandler.past_screens[0]);
  ScreenHandler.SetSynchronousOperationFinish();
  ScreenHandler.PopToOldScreen();
}

void DGUSScreenHandler::OnPrintFinished() {
  ScreenHandler.GotoScreen(DGUSLCD_SCREEN_PRINT_FINISH, false);
}

void DGUSScreenHandler::ScreenConfirmedOK(DGUS_VP_Variable &var, void *val_ptr) {
  // The display writes VP_CONFIRMED when the user presses a button on the
  // confirmation screen. The payload indicates which button was pressed.
  // If the user pressed NO (value == 0) we should NOT forward this to the
  // ConfirmVP handler (which would start actions like printing). Instead,
  // go back to the main menu. Only forward when the user explicitly
  // confirmed (non-zero value).
  uint16_t button_value = swap16(*(uint16_t*)val_ptr);

  // Debug: print the full 16-bit payload and the currently-active ConfirmVP
  SERIAL_ECHOPAIR("DWIN VP_CONFIRMED raw=0x", button_value);
  SERIAL_ECHOLNPAIR(" ConfirmVP=", ConfirmVP);

  // If Marlin is waiting on the user and we're on a POPUP/CONFIRM screen,
  // the confirm button may arrive via VP_CONFIRMED instead of VP_SCREENCHANGE.
  // Map the high-byte "info" into pause responses (Resume / Purge) the same
  // way ScreenChangeHook does, then release the wait and pop the screen.
  if (ExtUI::isWaitingOnUser() && (current_screen == DGUSLCD_SCREEN_POPUP || current_screen == DGUSLCD_SCREEN_CONFIRM)) {
   
    // Debug: show the raw value and whether suppression is active
    SERIAL_ECHOPAIR("DWIN VP_CONFIRMED value=0x", button_value);
    SERIAL_ECHOLNPAIR(" suppress_popup_pause_response=", suppress_popup_pause_response);

    // If the confirmation maps to a registered VP that provides a
    // set_by_display_handler, invoke it so the VP emulation path handles
    // any specialized logic (for example: M1125 heater-timeout).
    if (ConfirmVP) {
      DGUS_VP_Variable ramcopy;
      if (populate_VPVar(ConfirmVP, &ramcopy) && ramcopy.set_by_display_handler) {
        ramcopy.set_by_display_handler(ramcopy, val_ptr);
      }
    }

    if (!suppress_popup_pause_response) {
#if ENABLED(ADVANCED_PAUSE_FEATURE)
      switch ((uint8_t)(button_value >> 8)) {
        case 0x01: // Continue / Resume
          ExtUI::setPauseMenuResponse(PAUSE_RESPONSE_RESUME_PRINT);
          break;
        case 0x02: // Purge / Extrude more
          ExtUI::setPauseMenuResponse(PAUSE_RESPONSE_EXTRUDE_MORE);
          break;
        default:
          break;
      }
#endif // ENABLED(ADVANCED_PAUSE_FEATURE)
    }

    ExtUI::setUserConfirmed();
    PopToOldScreen();
    return;
  }

  // The DGUS Confirm dialog uses different button encodings depending on
  // the context. Empirically the SD file confirm uses: 1 = NO, 2 = YES.
  // To avoid starting prints when the user pressed NO, ensure we only
  // forward the confirmation to the emulated VP when we see the expected
  // 'confirm' code for file confirms.
  if (ConfirmVP == VP_SD_FileSelectConfirm || ConfirmVP == VP_SD_AbortPrintConfirmed) {
    if (button_value == 1) {
      // NO response to Screen#66 "Confirm?"" questions -> go back to previous screen. Do NOT perform the YES action.
      PopToOldScreen();
      return;
    }
  }

  DGUS_VP_Variable ramcopy;
  if (!populate_VPVar(ConfirmVP, &ramcopy)) return;
  if (ramcopy.set_by_display_handler) ramcopy.set_by_display_handler(ramcopy, val_ptr);
}

// Handler for dedicated M1125 heater-timeout Confirm VP. This is invoked
// via the VP helper table emulation path so both VP_CONFIRMED and
// VP_SCREENCHANGE return paths converge here.
void DGUSScreenHandler::HandleM1125TimeoutConfirm(DGUS_VP_Variable &var, void *val_ptr) {
  uint16_t raw = swap16(*(uint16_t*)val_ptr);

  // Debug logging to help correlate display payloads with behavior
  SERIAL_ECHOPAIR("M1125 timeout confirm handler raw=0x", raw);

  if (raw == 0x0002) {
    SERIAL_ECHOLNPGM("M1125 Confirm handler: YES (0x0002) -> Continue action");
    M1125_TimeoutContinueAction();
  }
  else if (raw == 0x0001) {
    SERIAL_ECHOLNPGM("M1125 Confirm handler: NO (0x0001) -> no action (allow timeout)");
    // NO: intentionally do nothing here. Let the timeout elapse.
  }

  // NOTE: Do NOT clear the Marlin wait or pop the popup here. The caller
  // (ScreenConfirmedOK / ScreenChangeHook) is responsible for releasing
  // the user wait and popping the screen exactly once. This keeps
  // the UI flow centralized and avoids duplicate pop actions.
}

#if HAS_MESH
void DGUSScreenHandler::OnMeshLevelingStart() {
  GotoScreen(DGUSLCD_SCREEN_LEVELING);
  dgusdisplay.WriteVariable(VP_MESH_SCREEN_MESSAGE_ICON, static_cast<uint16_t>(MESH_SCREEN_MESSAGE_ICON_LEVELING));

  ResetMeshValues();
  SetSynchronousOperationStart();

  MeshLevelIndex = 0;
  MeshLevelIconIndex = 0;
}

void DGUSScreenHandler::OnMeshLevelingUpdate(const int8_t x, const int8_t y, const float z) {
  SERIAL_ECHOPAIR("X: ", x);
  SERIAL_ECHOPAIR("; Y: ", y);
  SERIAL_ECHOPAIR("; Index ", MeshLevelIndex);
  SERIAL_ECHOLNPAIR("; Icon ", MeshLevelIconIndex);

  UpdateMeshValue(x, y, z);

  if (MeshLevelIndex < 0) {
    // We're not leveling
    return;
  }

  MeshLevelIndex++;
  MeshLevelIconIndex++;

  // Update icon
  dgusdisplay.WriteVariable(VP_MESH_LEVEL_STATUS, static_cast<uint16_t>(MeshLevelIconIndex + DGUS_GRID_VISUALIZATION_START_ID));

  if (MeshLevelIndex == GRID_MAX_POINTS) {
    // Done
    MeshLevelIndex = -1;

    RequestSaveSettings();
    
    if (GetPreviousScreen() == DGUSLCD_SCREEN_ZOFFSET_LEVEL) {
      // If the user is in the leveling workflow (not printing), get that hotend out of the way
      char gcodeBuffer[50] = {0};
      sprintf_P(gcodeBuffer, PSTR("G0 F3500 X%d\nG0 Y%d\nG0 Z%d\nM84"), (X_BED_SIZE / 2), (Y_BED_SIZE / 2), 35);
      queue.inject(gcodeBuffer);

      // Change text at the top
      ScreenHandler.SetViewMeshLevelState();
    } else {
      // When leveling from anywhere but the Z-offset/level screen, automatically pop back to the previous screen
      PopToOldScreen();
    }

    SetSynchronousOperationFinish();
  } else {
    // We've already updated the icon, so nothing left
  }
}
#endif
void DGUSScreenHandler::SetViewMeshLevelState() {
  dgusdisplay.WriteVariable(VP_MESH_SCREEN_MESSAGE_ICON, static_cast<uint16_t>(MESH_SCREEN_MESSAGE_ICON_VIEWING));
}
#if HAS_MESH
void DGUSScreenHandler::InitMeshValues() {
  if (ExtUI::getMeshValid()) {
    for (uint8_t x = 0; x < GRID_MAX_POINTS_X; x++) {
      for (uint8_t y = 0; y < GRID_MAX_POINTS_Y; y++) {
          float z = ExtUI::getMeshPoint({ x, y });
          UpdateMeshValue(x, y, z);
      }

      safe_delay(100);
    }

    dgusdisplay.WriteVariable(VP_MESH_LEVEL_STATUS, static_cast<uint16_t>(DGUS_GRID_VISUALIZATION_START_ID + GRID_MAX_POINTS));
  } else {
    ResetMeshValues();
  }
}

void DGUSScreenHandler::ResetMeshValues() {
  for (uint8_t x = 0; x < GRID_MAX_POINTS_X; x++) {
    for (uint8_t y = 0; y < GRID_MAX_POINTS_Y; y++) {
        UpdateMeshValue(x, y, 0);
    }

    safe_delay(100);
  }

  dgusdisplay.WriteVariable(VP_MESH_LEVEL_STATUS, static_cast<uint16_t>(DGUS_GRID_VISUALIZATION_START_ID));
}
#endif
uint16_t CreateRgb(double h, double s, double v) {
    struct {
      double h;       // angle in degrees
      double s;       // a fraction between 0 and 1
      double v;       // a fraction between 0 and 1
    } in = { h, s, v};

    double      hh, p, q, t, ff;
    long        i;
    struct {
      double r;       // a fraction between 0 and 1
      double g;       // a fraction between 0 and 1
      double b;       // a fraction between 0 and 1
      } out;

    if(in.s <= 0.0) {       // < is bogus, just shuts up warnings
        out.r = in.v;
        out.g = in.v;
        out.b = in.v;
        return 0;
    }

    hh = in.h;
    if(hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    i = (long)hh;
    ff = hh - i;
    p = in.v * (1.0 - in.s);
    q = in.v * (1.0 - (in.s * ff));
    t = in.v * (1.0 - (in.s * (1.0 - ff)));

    switch(i) {
    case 0:
        out.r = in.v;
        out.g = t;
        out.b = p;
        break;
    case 1:
        out.r = q;
        out.g = in.v;
        out.b = p;
        break;
    case 2:
        out.r = p;
        out.g = in.v;
        out.b = t;
        break;

    case 3:
        out.r = p;
        out.g = q;
        out.b = in.v;
        break;
    case 4:
        out.r = t;
        out.g = p;
        out.b = in.v;
        break;
    case 5:
    default:
        out.r = in.v;
        out.g = p;
        out.b = q;
        break;
    }
  
  return (((static_cast<uint8_t>(out.r * 255) & 0xf8)<<8) + ((static_cast<uint8_t>(out.g * 255) & 0xfc)<<3) + (static_cast<uint8_t>(out.b * 255)>>3));
}

#if HAS_MESH
void DGUSScreenHandler::UpdateMeshValue(const int8_t x, const int8_t y, const float z) {
  SERIAL_ECHOPAIR("X", x);
  SERIAL_ECHOPAIR(" Y", y);
  SERIAL_ECHO(" Z");
  SERIAL_ECHO_F(z, 4);

  // Determine the screen X and Y value
  if (x % SkipMeshPoint != 0 || y % SkipMeshPoint != 0) {
    // Skip this point
    SERIAL_ECHOLN("");
    return;
  }

  const uint8_t scrX = x / SkipMeshPoint;
  const uint8_t scrY = y / SkipMeshPoint;

  // Each Y is a full edge of X values
  const uint16_t vpAddr = VP_MESH_LEVEL_X0_Y0 + (scrY * MESH_LEVEL_VP_SIZE) + (scrX * MESH_LEVEL_VP_EDGE_SIZE);

  // ... DWIN is inconsistently truncating floats. Examples: 0.1811 becomes 0.181, 0.1810 becomes 0.180. But 0.1800 is not 0.179
  //     so we need to calculate a good number here that will not overflow
  float displayZ = z;
  
  {
    constexpr float correctionFactor = 0.0001;

    if (round(z * cpow(10,3)) == round((z + correctionFactor) * cpow(10,3))) {
      // If we don't accidently overshoot to the next number, trick the display by upping the number 0.0001 💩
      displayZ += correctionFactor;

      SERIAL_ECHO(" displayZ: ");
      SERIAL_ECHO_F(z, 4);
    }
  }

  SERIAL_ECHOLN("");

  dgusdisplay.WriteVariable(vpAddr, displayZ);

  // Set color
  const uint16_t spAddr = SP_MESH_LEVEL_X0_Y0 + (scrY * MESH_LEVEL_SP_SIZE) + (scrX * MESH_LEVEL_SP_EDGE_SIZE);

  uint16_t color = MESH_COLOR_NOT_MEASURED;

  // ... Only calculate if set
  if (abs(z) > MESH_UNSET_EPSILON) {
    // Determine color scale
    float clampedZ = max(min(z, 0.5f),-0.5f) * -1;
    float h = (clampedZ + 0.5f) * 240;

    // Convert to RGB
    color = CreateRgb(h, 1, 0.75);
  }

  dgusdisplay.SetVariableDisplayColor(spAddr, color);
}

void DGUSScreenHandler::HandleMeshPoint(DGUS_VP_Variable &var, void *val_ptr) {
  // Determine the X and Y for this mesh point
  // We can do this because we assume MESH_INPUT_SUPPORTED_X_SIZE and MESH_INPUT_SUPPORTED_Y_SIZE with MESH_INPUT_DATA_SIZE.
  // So each VP is MESH_INPUT_DATA_SIZE apart

  if (HasSynchronousOperation) {
    setstatusmessagePGM(PSTR("Wait for leveling to complete"));
    return;
  }

  const uint16_t probe_point = var.VP - VP_MESH_INPUT_X0_Y0;
  constexpr uint16_t col_size = MESH_INPUT_SUPPORTED_Y_SIZE * MESH_INPUT_DATA_SIZE;

  const uint8_t x = probe_point / col_size; // Will be 0 to 3 inclusive
  const uint8_t y = (probe_point - (x * col_size)) / MESH_INPUT_DATA_SIZE;

  int16_t rawZ = *(int16_t*)val_ptr;
  float z = swap16(rawZ) * 0.001;

  SERIAL_ECHOPAIR("Overriding mesh value. X:", x);
  SERIAL_ECHOPAIR(" Y:", y);
  SERIAL_ECHO(" Z:");
  SERIAL_ECHO_F(z, 4);
  SERIAL_ECHOPAIR(" [raw: ", rawZ);
  SERIAL_ECHOPAIR("] [point ", probe_point, "] ");
  SERIAL_ECHOPAIR(" [VP: ", var.VP);
  SERIAL_ECHOLN("]");

  UpdateMeshValue(x, y, z);
  ExtUI::setMeshPoint({ x, y }, z);

  RequestSaveSettings();
}
#endif
#if HAS_COLOR_LEDS
void DGUSScreenHandler::HandleLED(DGUS_VP_Variable &var, void *val_ptr) {
  // The display returns a 16-bit integer
  uint16_t newValue = swap16(*(uint16_t*)val_ptr);
  
  NOLESS(newValue, 0);
  NOMORE(newValue, 255);

  (*(uint8_t*)var.memadr) = static_cast<uint8_t>(newValue);
  leds.set_color(leds.color);

  SERIAL_ECHOLNPAIR("HandleLED ", newValue);
  RequestSaveSettings();

  skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
}

void DGUSScreenHandler::SendLEDToDisplay(DGUS_VP_Variable &var) {
  DGUS_VP_Variable rcpy;
  if (!populate_VPVar(var.VP, &rcpy)) {
    return;
  }

  // The display wants a 16-bit integer
  uint16_t val = *(uint8_t*)var.memadr;
  rcpy.memadr = &val;

  DGUSLCD_SendWordValueToDisplay(rcpy);
}
#endif

const uint16_t* DGUSLCD_FindScreenVPMapList(uint8_t screen) {
  const uint16_t *ret;
  const struct VPMapping *map = VPMap;
  while ((ret = (uint16_t*) pgm_read_ptr(&(map->VPList)))) {
    if (pgm_read_byte(&(map->screen)) == screen) return ret;
    map++;
  }
  return nullptr;
}

const DGUS_VP_Variable* DGUSLCD_FindVPVar(const uint16_t vp) {
  const DGUS_VP_Variable *ret = ListOfVP;
  do {
    const uint16_t vpcheck = pgm_read_word(&(ret->VP));
    if (vpcheck == 0) break;
    if (vpcheck == vp) return ret;
    ++ret;
  } while (1);

  DEBUG_ECHOLNPAIR("FindVPVar NOT FOUND ", vp);
  return nullptr;
}

void DGUSScreenHandler::ScreenChangeHookIfIdle(DGUS_VP_Variable &var, void *val_ptr) {
  if (!ExtUI::isPrinting()) {
    ScreenChangeHook(var, val_ptr);
    GotoScreen(current_screen);
  }
}

void DGUSScreenHandler::ScreenChangeHook(DGUS_VP_Variable &var, void *val_ptr) {
  uint8_t *tmp = (uint8_t*)val_ptr;

  // The keycode in target is coded as <from-frame><to-frame>, so 0x0100A means
  // from screen 1 (main) to 10 (temperature). DGUSLCD_SCREEN_POPUP is special,
  // meaning "return to previous screen"
  DGUSLCD_Screens target = (DGUSLCD_Screens)tmp[1];

  DEBUG_ECHOLNPAIR("Current screen:", current_screen);
  DEBUG_ECHOLNPAIR("Cancel target:", target);

  if (ExtUI::isWaitingOnUser() && (current_screen == DGUSLCD_SCREEN_POPUP || current_screen == DGUSLCD_SCREEN_CONFIRM)) {
    // When a popup is shown in response to Marlin waiting on the user, the
    // display usually writes to VP_SCREENCHANGE (VP 0x219F) with a two-byte
    // value. The low byte is the target screen, the high byte is optional
    // auxiliary info. Historically the code simply treated any popup button
    // as an implicit "confirm" and called setUserConfirmed().
    //
    // To support richer dialogs (for example: Continue vs Purge choices)
    // allow the high byte (info) to carry a small command that the firmware
    // can map to a PauseMenuResponse prior to confirming. Example
    // encoding (display-side): 0x01<<8 | DGUSLCD_SCREEN_POPUP  => Continue
    //                             0x02<<8 | DGUSLCD_SCREEN_POPUP  => Purge
    //
    DEBUG_ECHOLN("Executing confirmation action (popup)");

  // Compute the raw 16-bit payload and extract both bytes so we can log
  // exactly what the display is sending (some displays use different
  // byte order or only set the target without any info byte).
  uint16_t raw = swap16(*(uint16_t*)val_ptr);
  uint8_t info = tmp[0];
  uint8_t target_byte = tmp[1];

  // Debug: log full payload (raw) and both bytes, plus suppression state
  SERIAL_ECHOPAIR("DWIN VP_SCREENCHANGE raw=0x", raw);
  SERIAL_ECHOPAIR(" info=0x", info);
  SERIAL_ECHOLNPAIR(" target=0x", target_byte);
  SERIAL_ECHOLNPAIR(" suppress_popup_pause_response=", suppress_popup_pause_response);

      // If the current Confirm maps to a registered VP with a
      // set_by_display_handler, invoke it. This makes the VP helper
      // emulation path the single canonical handler for Confirm returns.
      if (ConfirmVP) {
        DGUS_VP_Variable ramcopy;
        if (populate_VPVar(ConfirmVP, &ramcopy) && ramcopy.set_by_display_handler) {
          ramcopy.set_by_display_handler(ramcopy, val_ptr);
        }
      }

    // If suppression is not set, map info codes into the pause menu
    // response so Marlin can take the corresponding action (resume vs purge).
    if (!suppress_popup_pause_response) {
#if ENABLED(ADVANCED_PAUSE_FEATURE)
      switch (info) {
        case 0x01: // display encoded "Continue" action
          ExtUI::setPauseMenuResponse(PAUSE_RESPONSE_RESUME_PRINT);
          break;
        case 0x02: // display encoded "Purge / Extrude more" action
          ExtUI::setPauseMenuResponse(PAUSE_RESPONSE_EXTRUDE_MORE);
          break;
        default:
          break;
      }
#endif
    }

    // Finally, release Marlin's wait and pop the popup screen
    ExtUI::setUserConfirmed();
    PopToOldScreen();
    return;
  }

  if (target == DGUSLCD_SCREEN_POPUP || target == DGUSLCD_SCREEN_CONFIRM || target == 0 || target == 255 /*Buggy DWIN screen sometimes just returns 255*/) {
    PopToOldScreen();
    return;
  }

  UpdateNewScreen(target);

  #ifdef DEBUG_DGUSLCD
    if (!DGUSLCD_FindScreenVPMapList(target)) DEBUG_ECHOLNPAIR("WARNING: No screen Mapping found for ", target);
  #endif
}

void DGUSScreenHandler::HandleAllHeatersOff(DGUS_VP_Variable &var, void *val_ptr) {
  ExtUI::coolDown();
  ScreenHandler.ForceCompleteUpdate(); // hint to send all data.
}

void DGUSScreenHandler::HandleTemperatureChanged(DGUS_VP_Variable &var, void *val_ptr) {
  celsius_t newvalue = swap16(*(uint16_t*)val_ptr);
  celsius_t acceptedvalue;

  switch (var.VP) {
    default: return;
    #if HOTENDS >= 1
      case VP_T_E0_Set:
        NOMORE(newvalue, thermalManager.hotend_max_target(0));
        thermalManager.setTargetHotend(newvalue, 0);
        acceptedvalue = thermalManager.degTargetHotend(0);
        break;
    #endif
    #if HAS_HEATED_BED
      case VP_T_Bed_Set:
        NOMORE(newvalue, BED_MAXTEMP);
        thermalManager.setTargetBed(newvalue);
        acceptedvalue = thermalManager.degTargetBed();
        break;
    #endif
  }

  // reply to display the new value to update the view if the new value was rejected by the Thermal Manager.
  if (newvalue != acceptedvalue && var.send_to_display_handler) var.send_to_display_handler(var);
  skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
}

void DGUSScreenHandler::HandleFanSpeedChanged(DGUS_VP_Variable &var, void *val_ptr) {
  uint16_t newValue = swap16(*(uint16_t*)val_ptr);
    
    SERIAL_ECHOLNPAIR("Fan speed changed: ", newValue);
    ExtUI::setTargetFan_percent(newValue, ExtUI::fan_t::FAN0);

    ScreenHandler.skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
}

void DGUSScreenHandler::HandleFlowRateChanged(DGUS_VP_Variable &var, void *val_ptr) {
  #if EXTRUDERS
    uint16_t newValue = swap16(*(uint16_t*)val_ptr);
    
    SERIAL_ECHOLNPAIR("Flow rate changed: ", newValue);
    ExtUI::setFlow_percent(newValue, ExtUI::E0);

    ScreenHandler.skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
  #else
    UNUSED(var); UNUSED(val_ptr);
  #endif
}

void DGUSScreenHandler::HandleManualExtrude(DGUS_VP_Variable &var, void *val_ptr) {
  DEBUG_ECHOLNPGM("HandleManualExtrude");

  int16_t movevalue = swap16(*(uint16_t*)val_ptr);
  float target = movevalue * 0.01f;
  ExtUI::extruder_t target_extruder;

  switch (var.VP) {
    #if HOTENDS >= 1
      case VP_MOVE_E0: target_extruder = ExtUI::extruder_t::E0; break;
    #endif
    #if HOTENDS >= 2
      case VP_MOVE_E1: target_extruder = ExtUI::extruder_t::E1; break;
    #endif
    default: return;
  }

  target += ExtUI::getAxisPosition_mm(target_extruder);
  ExtUI::setAxisPosition_mm(target, target_extruder);
  skipVP = var.VP;
}

void DGUSScreenHandler::HandleMotorLockUnlock(DGUS_VP_Variable &var, void *val_ptr) {
  DEBUG_ECHOLNPGM("HandleMotorLockUnlock");

  char buf[4];
  const int16_t lock = swap16(*(uint16_t*)val_ptr);
  strcpy_P(buf, lock ? PSTR("M18") : PSTR("M17"));

  //DEBUG_ECHOPAIR(" ", buf);
  queue.enqueue_one_now(buf);
}

#if ENABLED(POWER_LOSS_RECOVERY)

  void DGUSScreenHandler::TogglePowerLossRecovery(DGUS_VP_Variable &var, void *val_ptr) {
    PrintJobRecovery::enable(!PrintJobRecovery::enabled);
  }

  void DGUSScreenHandler::HandlePowerLossRecovery(DGUS_VP_Variable &var, void *val_ptr) {
    uint16_t value = swap16(*(uint16_t*)val_ptr);
    if (value) {
      queue.inject_P(PSTR("M1000"));
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_SDPRINTMANIPULATION, false);
    }
    else {
      recovery.cancel();
      ScreenHandler.GotoScreen(DGUSLCD_SCREEN_MAIN, false);
    }
  }

#endif



void DGUSScreenHandler::HandleScreenVersion(DGUS_VP_Variable &var, void *val_ptr) {
  DEBUG_ECHOLNPGM("HandleScreenVersion");
  
  uint16_t actualScreenVersion = swap16(*(uint16_t*)val_ptr);

  SERIAL_ECHOLNPAIR("DWIN version received: ", actualScreenVersion);
  SERIAL_ECHOLNPAIR("We expected DWIN version: ", EXPECTED_UI_VERSION_MAJOR);

  if (actualScreenVersion == EXPECTED_UI_VERSION_MAJOR) {
    SERIAL_ECHOLN("Screen version check passed.");
    return;
  }

  // Dump error to serial
  SERIAL_ECHOLN("WARNING: Your screen is not flashed correctly.");

  SERIAL_ECHOPAIR("We received version ", actualScreenVersion);
  SERIAL_ECHOLN("from the display");

  SERIAL_ECHOLNPAIR("This firmware needs screen version ", actualScreenVersion);
  SERIAL_ECHOLN("Please follow the release notes for flashing instructions.");

  // Will cause flashing in the loop()
  HasScreenVersionMismatch = true;

  // Show on display if user has M117 message
  if (actualScreenVersion >= 6) {
    // We have a scrolling message so we can do something more complicated
    char buffer[VP_M117_LEN] = {0};
    sprintf_P(buffer, "Please flash your TFT screen: version mismatch - build %d found but expected %d", actualScreenVersion, EXPECTED_UI_VERSION_MAJOR);
    setStatusMessage(buffer, true);
  } else {
    char buffer[VP_M117_LEN] = {0};
    sprintf_P(buffer, "Flash TFT please v%d<>v%d", actualScreenVersion, EXPECTED_UI_VERSION_MAJOR);
    setstatusmessage(buffer);
  }

  // Audio buzzer
  Buzzer(500, 500);
  for (int times=0;times<VERSION_MISMATCH_BUZZ_AMOUNT;times++) {
    safe_delay(750);
    Buzzer(500, 500);
  }
}

void DGUSScreenHandler::HandleScreenVersionMismatchLEDFlash() {
  if (!HasScreenVersionMismatch) return;

  const millis_t ms = millis();
  static millis_t next_event_ms = 0;

  if (ELAPSED(ms, next_event_ms)) {
    next_event_ms = ms + VERSION_MISMATCH_LED_FLASH_DELAY;

    caselight.on = !caselight.on;
    caselight.update(caselight.on);

    #if HAS_COLOR_LEDS
    if (caselight.on) {
      leds.set_color(LEDColorRed());
    } else {
      leds.set_color(LEDColorOff());
    }
    #endif
  }
}

void DGUSScreenHandler::HandleStepPerMMChanged(DGUS_VP_Variable &var, void *val_ptr) {
  DEBUG_ECHOLNPGM("HandleStepPerMMChanged");

  uint16_t value_raw = swap16(*(uint16_t*)val_ptr);
  DEBUG_ECHOLNPAIR("value_raw:", value_raw);
  float value = (float)value_raw/10;
  ExtUI::axis_t axis;
  switch (var.VP) {
    case VP_X_STEP_PER_MM: axis = ExtUI::axis_t::X; break;
    case VP_Y_STEP_PER_MM: axis = ExtUI::axis_t::Y; break;
    case VP_Z_STEP_PER_MM: axis = ExtUI::axis_t::Z; break;
    default: return;
  }
  DEBUG_ECHOLNPAIR_F("value:", value);
  ExtUI::setAxisSteps_per_mm(value, axis);
  DEBUG_ECHOLNPAIR_F("value_set:", ExtUI::getAxisSteps_per_mm(axis));
  ScreenHandler.skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
  return;
}

void DGUSScreenHandler::HandleStepPerMMExtruderChanged(DGUS_VP_Variable &var, void *val_ptr) {
  DEBUG_ECHOLNPGM("HandleStepPerMMExtruderChanged");

  uint16_t value_raw = swap16(*(uint16_t*)val_ptr);
  DEBUG_ECHOLNPAIR("value_raw:", value_raw);
  float value = (float)value_raw/10;
  ExtUI::extruder_t extruder;
  switch (var.VP) {
    default: return;
    #if HOTENDS >= 1
      case VP_E0_STEP_PER_MM: extruder = ExtUI::extruder_t::E0; break;
    #endif
    #if HOTENDS >= 2
      case VP_E1_STEP_PER_MM: extruder = ExtUI::extruder_t::E1; break;
    #endif
  }
  DEBUG_ECHOLNPAIR_F("value:", value);
  ExtUI::setAxisSteps_per_mm(value,extruder);
  DEBUG_ECHOLNPAIR_F("value_set:", ExtUI::getAxisSteps_per_mm(extruder));
  ScreenHandler.skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
  return;
}

#if HAS_PID_HEATING
  void DGUSScreenHandler::HandleTemperaturePIDChanged(DGUS_VP_Variable &var, void *val_ptr) {
    uint16_t rawvalue = swap16(*(uint16_t*)val_ptr);
    DEBUG_ECHOLNPAIR("V1:", rawvalue);
    float value = (float)rawvalue / 10;
    DEBUG_ECHOLNPAIR("V2:", value);
    float newvalue = 0;

    switch (var.VP) {
      default: return;
      #if HOTENDS >= 1
        case VP_E0_PID_P: newvalue = value; break;
        case VP_E0_PID_I: newvalue = scalePID_i(value); break;
        case VP_E0_PID_D: newvalue = scalePID_d(value); break;
      #endif
      #if HOTENDS >= 2
        case VP_E1_PID_P: newvalue = value; break;
        case VP_E1_PID_I: newvalue = scalePID_i(value); break;
        case VP_E1_PID_D: newvalue = scalePID_d(value); break;
      #endif
      #if HAS_HEATED_BED
        case VP_BED_PID_P: newvalue = value; break;
        case VP_BED_PID_I: newvalue = scalePID_i(value); break;
        case VP_BED_PID_D: newvalue = scalePID_d(value); break;
      #endif
    }

    DEBUG_ECHOLNPAIR_F("V3:", newvalue);
    *(float *)var.memadr = newvalue;
    ScreenHandler.skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
  }

  void DGUSScreenHandler::HandlePIDAutotune(DGUS_VP_Variable &var, void *val_ptr) {
    DEBUG_ECHOLNPGM("HandlePIDAutotune");

    char buf[32] = {0};

    switch (var.VP) {
      default: break;
      #if ENABLED(PIDTEMP)
        #if HOTENDS >= 1
          case VP_PID_AUTOTUNE_E0: // Autotune Extruder 0
            sprintf(buf, "M303 E%d C5 S210 U1", ExtUI::extruder_t::E0);
            break;
        #endif
        #if HOTENDS >= 2
          case VP_PID_AUTOTUNE_E1:
            sprintf(buf, "M303 E%d C5 S210 U1", ExtUI::extruder_t::E1);
            break;
        #endif
      #endif
      #if ENABLED(PIDTEMPBED)
        case VP_PID_AUTOTUNE_BED:
          sprintf(buf, "M303 E-1 C5 S70 U1");
          break;
      #endif
    }

    if (buf[0]) queue.enqueue_one_now(buf);

    #if ENABLED(DGUS_UI_WAITING)
      sendinfoscreen(PSTR("PID is autotuning"), PSTR("please wait"), NUL_STR, NUL_STR, true, true, true, true);
      GotoScreen(DGUSLCD_SCREEN_WAITING);
    #endif
  }
#endif

void DGUSScreenHandler::HandleFadeHeight(DGUS_VP_Variable &var, void *val_ptr) {
    DGUSLCD_SetFloatAsIntFromDisplay<1>(var, val_ptr);

    RequestSaveSettings();
    return;
}

void DGUSScreenHandler::HandlePositionChange(DGUS_VP_Variable &var, void *val_ptr) {
  DEBUG_ECHOLNPGM("HandlePositionChange");

  unsigned int speed = homing_feedrate_mm_m.x;
  float target_position = ((float)swap16(*(uint16_t*)val_ptr)) / 10.0;

  switch (var.VP) {
    default: return;

    case VP_X_POSITION:
      if (!ExtUI::canMove(ExtUI::axis_t::X)) return;
      current_position.x = min(target_position, static_cast<float>(X_MAX_POS));
      break;

    case VP_Y_POSITION:
      if (!ExtUI::canMove(ExtUI::axis_t::Y)) return;
      current_position.y = min(target_position, static_cast<float>(Y_MAX_POS));
      break;

    case VP_Z_POSITION:
      if (!ExtUI::canMove(ExtUI::axis_t::Z)) return;
      speed = homing_feedrate_mm_m.z;
      current_position.z = min(target_position, static_cast<float>(Z_MAX_POS));
      break;
  }

  line_to_current_position(MMM_TO_MMS(speed));

  ScreenHandler.ForceCompleteUpdate();
  DEBUG_ECHOLNPGM("poschg done.");
}

void DGUSScreenHandler::HandleLiveAdjustZ(DGUS_VP_Variable &var, void *val_ptr, const_float_t scalingFactor) {
  DEBUG_ECHOLNPGM("HandleLiveAdjustZ");

  float absoluteAmount = float(swap16(*(int16_t*)val_ptr))  / scalingFactor;
  float existingAmount = ExtUI::getZOffset_mm();
  float difference = (absoluteAmount - existingAmount) < 0 ? -0.01 : 0.01;

  int16_t steps = ExtUI::mmToWholeSteps(difference, ExtUI::axis_t::Z);

  ExtUI::smartAdjustAxis_steps(steps, ExtUI::axis_t::Z, true);
#if ENABLED(HAS_BED_PROBE) //  Without a probe the Z offset is applied using baby offsets, which aren't saved anyway.
  RequestSaveSettings();
#endif
  ScreenHandler.ForceCompleteUpdate();
  ScreenHandler.skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
  return;
}

// This wrapper function is needed to avoid pulling in ExtUI in DGUSScreenHandler.h
float DGUSScreenHandler::GetCurrentLifeAdjustZ() {
  return ExtUI::getZOffset_mm();
}

void DGUSScreenHandler::HandleHeaterControl(DGUS_VP_Variable &var, void *val_ptr) {
  DEBUG_ECHOLNPGM("HandleHeaterControl");

  uint8_t preheat_temp = 0;
  switch (var.VP) {
    #if HOTENDS >= 1
      case VP_E0_CONTROL:
    #endif
    #if HOTENDS >= 2
      case VP_E1_CONTROL:
    #endif
    #if HOTENDS >= 3
      case VP_E2_CONTROL:
    #endif
      preheat_temp = PREHEAT_1_TEMP_HOTEND;
      break;

    case VP_BED_CONTROL:
      preheat_temp = PREHEAT_1_TEMP_BED;
      break;
  }

  *(int16_t*)var.memadr = *(int16_t*)var.memadr > 0 ? 0 : preheat_temp;
}

void DGUSScreenHandler::HandleLEDToggle() {
  bool newState = !caselight.on;

  caselight.on = newState;
  caselight.update(newState);

  RequestSaveSettings();
  ForceCompleteUpdate();
}

void DGUSScreenHandler::HandleToggleTouchScreenMute(DGUS_VP_Variable &var, void *val_ptr) {
  Settings.display_sound = !Settings.display_sound;
  ScreenHandler.SetTouchScreenConfiguration();

  RequestSaveSettings();
  ForceCompleteUpdate();

  ScreenHandler.skipVP = var.VP; // don't overwrite value the next update time as the display might autoincrement in parallel
}

// Marlin no longer exposes probe.settings. Disable the handlers which would
// read/write probe.settings to avoid build failures and keep UI compatible.
#if 0
void DGUSScreenHandler::HandleToggleProbeHeaters(DGUS_VP_Variable &var, void *val_ptr) {
  probe.settings.turn_heaters_off = !probe.settings.turn_heaters_off;

  RequestSaveSettings();
}

void DGUSScreenHandler::HandleToggleProbeTemperatureStabilization(DGUS_VP_Variable &var, void *val_ptr) {
  probe.settings.stabilize_temperatures_after_probing = !probe.settings.stabilize_temperatures_after_probing;

  RequestSaveSettings();
}

void DGUSScreenHandler::HandleToggleProbePreheatTemp(DGUS_VP_Variable &var, void *val_ptr) {
  ScreenHandler.DGUSLCD_SetValueDirectly<uint16_t>(var, val_ptr);

  RequestSaveSettings();
}
#endif

void DGUSScreenHandler::HandleTouchScreenBrightnessSetting(DGUS_VP_Variable &var, void *val_ptr) {
  uint16_t newvalue = swap16(*(uint16_t*)val_ptr);

  SERIAL_ECHOLNPAIR("HandleTouchScreenBrightnessSetting: ", newvalue);
  Settings.screen_brightness = newvalue;
  ScreenHandler.SetTouchScreenConfiguration();

  RequestSaveSettings();
  ForceCompleteUpdate();
}

void DGUSScreenHandler::HandleTouchScreenStandbyBrightnessSetting(DGUS_VP_Variable &var, void *val_ptr) {
  uint16_t newvalue = swap16(*(uint16_t*)val_ptr);

  SERIAL_ECHOLNPAIR("HandleTouchScreenStandbyBrightnessSetting: ", newvalue);
  Settings.standby_screen_brightness = newvalue;
  ScreenHandler.SetTouchScreenConfiguration();

  RequestSaveSettings();
  ForceCompleteUpdate();
}

void DGUSScreenHandler::HandleTouchScreenStandbyTimeSetting(DGUS_VP_Variable &var, void *val_ptr) {
  uint16_t newvalue = swap16(*(uint16_t*)val_ptr);

  SERIAL_ECHOLNPAIR("HandleTouchScreenStandbyTimeSetting: ", newvalue);
  Settings.standby_time_seconds = newvalue;
  ScreenHandler.SetTouchScreenConfiguration();

  RequestSaveSettings();
  ForceCompleteUpdate();
}

void DGUSScreenHandler::HandleToggleTouchScreenStandbySetting(DGUS_VP_Variable &var, void *val_ptr) {
  SERIAL_ECHOLNPAIR("HandleToggleTouchScreenStandbySetting");

  Settings.display_standby = !Settings.display_standby;
  ScreenHandler.SetTouchScreenConfiguration();

  RequestSaveSettings();
  ForceCompleteUpdate();
}

void DGUSScreenHandler::HandleFanToggle() {
  thermalManager.fan_speed[0] = (thermalManager.fan_speed[0] > 0) ? 0 : 255;

  ForceCompleteUpdate();
}

void DGUSScreenHandler::UpdateNewScreen(DGUSLCD_Screens newscreen, bool save_current_screen) {
  SERIAL_ECHOLNPAIR("SetNewScreen: ", newscreen);

  if (save_current_screen && current_screen != DGUSLCD_SCREEN_POPUP && current_screen != DGUSLCD_SCREEN_CONFIRM) {
    SERIAL_ECHOLNPAIR("SetNewScreen (saving): ", newscreen);
    memmove(&past_screens[1], &past_screens[0], sizeof(past_screens) - 1);
    past_screens[0] = current_screen;
  }

  current_screen = newscreen;
  skipVP = 0;
  ForceCompleteUpdate();
}

void DGUSScreenHandler::PopToOldScreen() {
  DEBUG_ECHOLNPAIR("PopToOldScreen s=", past_screens[0]);

  if(past_screens[0] != 0) {
    GotoScreen(past_screens[0], false);
    memmove(&past_screens[0], &past_screens[1], sizeof(past_screens) - 1);
    past_screens[sizeof(past_screens) - 1] = DGUSLCD_SCREEN_MAIN;
  } else {
    if (ExtUI::isPrinting()) {
      // If printing from host (not from media), show the dedicated host
      // running screen so the UI clearly indicates a host-streamed job.
      if (!ExtUI::isPrintingFromMedia()) GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING_HOST, false);
      else GotoScreen(DGUSLCD_SCREEN_PRINT_RUNNING, false);
    } else {
      GotoScreen(DGUSLCD_SCREEN_MAIN, false);
    }
  }
}

void DGUSScreenHandler::OnBackButton(DGUS_VP_Variable &var, void *val_ptr) {
  // If we're busy: ignore
  if (HasSynchronousOperation) return;

  // Pop back
  uint16_t button_value = uInt16Value(val_ptr);

  PopToOldScreen();

  // Handle optional save from back button
  if (button_value == GENERIC_BACK_BUTTON_NEED_SAVE) {
    RequestSaveSettings();
  }
}

void DGUSScreenHandler::UpdateScreenVPData() {
  if (!dgusdisplay.isInitialized()) {
    return;
  }

  //DEBUG_ECHOPAIR(" UpdateScreenVPData Screen: ", current_screen);

  const uint16_t *VPList = DGUSLCD_FindScreenVPMapList(current_screen);
  if (!VPList) {
    DEBUG_ECHOLNPAIR(" NO SCREEN FOR: ", current_screen);
    ScreenComplete = true;
    return;  // nothing to do, likely a bug or boring screen.
  }

  // Round-robin updating of all VPs.
  VPList += update_ptr;

  bool sent_one = false;
  do {
    uint16_t VP = pgm_read_word(VPList);
    DEBUG_ECHOPAIR(" VP: ", VP);
    if (!VP) {
      update_ptr = 0;
      DEBUG_ECHOLNPGM(" UpdateScreenVPData done");
      ScreenComplete = true;
      return;  // Screen completed.
    }

    if (VP == skipVP) { skipVP = 0; continue; }

    DGUS_VP_Variable rcpy;
    if (populate_VPVar(VP, &rcpy)) {
      uint8_t expected_tx = 6 + rcpy.size;  // expected overhead is 6 bytes + payload.
      // Send the VP to the display, but try to avoid overrunning the Tx Buffer.
      // But send at least one VP, to avoid getting stalled.
      if (rcpy.send_to_display_handler && (!sent_one || expected_tx <= dgusdisplay.GetFreeTxBuffer())) {
        DEBUG_ECHOPAIR(" calling handler for ", rcpy.VP);
        sent_one = true;
        rcpy.send_to_display_handler(rcpy);
      }
      else {
        auto x = dgusdisplay.GetFreeTxBuffer();
        DEBUG_ECHOLNPAIR(" tx almost full: ", x);
        UNUSED(x);
        //DEBUG_ECHOPAIR(" update_ptr ", update_ptr);
        ScreenComplete = false;
        return;  // please call again!
      }
    }

  } while (++update_ptr, ++VPList, true);
}

void DGUSScreenHandler::GotoScreen(DGUSLCD_Screens screen, bool save_current_screen) {
  if (current_screen == screen) {
     // Ignore this request
     return;
  }

  DEBUG_ECHOLNPAIR("Issuing command to go to screen: ", screen);
  dgusdisplay.RequestScreen(screen);
  UpdateNewScreen(screen, save_current_screen);
}

bool DGUSScreenHandler::loop() {
  dgusdisplay.loop();

  HandleScreenVersionMismatchLEDFlash();

  const millis_t ms = millis();
  static millis_t next_event_ms = 0;

  if (ELAPSED(ms, next_event_ms) && SaveSettingsRequested) {
    // Only save settings so many times in a second - otherwise the EEPROM chip gets overloaded and the watchdog reboots the CPU
    settings.save();
    SaveSettingsRequested = false;
  }

  if (!IsScreenComplete() || ELAPSED(ms, next_event_ms)) {
    next_event_ms = ms + DGUS_UPDATE_INTERVAL_MS;

    UpdateScreenVPData();
  }

  if (dgusdisplay.isInitialized()) {
    static bool booted = false;

    if (!booted) {
      progmem_str message = GET_TEXT_F(WELCOME_MSG);
      char buff[strlen_P((const char * const)message)+1];
      strcpy_P(buff, (const char * const) message);
      ExtUI::onStatusChanged((const char *)buff);

      int16_t percentage = static_cast<int16_t>(((float) ms / (float)BOOTSCREEN_TIMEOUT) * 100);
      if (percentage > 100) percentage = 100;

      dgusdisplay.WriteVariable(VP_STARTPROGRESSBAR, percentage);
    }

    if (!booted && TERN0(POWER_LOSS_RECOVERY, recovery.valid())) {
      booted = true;
      DEBUG_ECHOLN("Power loss recovery...");
    }

    if (!booted && ELAPSED(ms, BOOTSCREEN_TIMEOUT)) {
      booted = true;
      
      #if HAS_COLOR_LEDS && !HAS_COLOR_LEDS_PREFERENCES
      leds.set_default();
      #endif
      
      // Ensure to pick up the settings
      SetTouchScreenConfiguration();

#if HAS_MESH
      // Set initial leveling status
      InitMeshValues();
#endif

      // No disabled back button
      ScreenHandler.SetSynchronousOperationFinish();

      // Ask for the screen version - HandleScreenVersion will act
      dgusdisplay.ReadVariable(VP_UI_VERSION_MAJOR);

      // Main menu
      GotoScreen(DGUSLCD_SCREEN_MAIN);
    }
  }

  // Check for any delayed status message and post it when due (owned buffer)
  const millis_t ms2 = millis();
  if (delayed_status_until && ELAPSED(ms2, delayed_status_until)) {
    // Show the owned buffer
    delayed_status_until = 0;
    if (delayed_status_buffer[0]) {
      ScreenHandler.setstatusmessage(delayed_status_buffer);
      // Schedule clearing after 10 seconds
      delayed_status_clear_at = ms2 + 10000;
    }
  }

  // Let M1125's pause heater-timeout handler run in the UI loop so that
  // the DGUS UI can display a safe message when heaters are disabled.
  if (M1125_CheckAndHandleHeaterTimeout()) {
    // Post a visible status message on the DGUS screen so the user knows
    // heaters were disabled due to pause timeout. Keep it displayed for 10s.
    ScreenHandler.PostDelayedStatusMessage_P(PSTR("Heaters disabled due to pause timeout"), 0);
    // Also make sure the paused screen reflects the heater-off state
    ScreenHandler.setstatusmessagePGM(PSTR("Heaters disabled (timeout)"));
  }

  // If a popup-based heater-timeout is pending, update the VP_M117 countdown
  // Show a non-intrusive countdown in the status line for heater timeout.
  // Behavior:
  //  - After the nozzle has been parked and the "Nozzle Parked." status
  //    has been visible for 10 seconds, begin showing a countdown:
  //      "Heaters timeout in xx seconds"
  //  - Update that countdown no more frequently than every 5 seconds.
  //  - If the graceful popup window is active (M1125 reports a short
  //    grace remaining), show that remaining instead. Clear when resume
  //    completes.
  static bool m1125_pause_was_active = false;
  static millis_t m1125_pause_start_ms = 0;
  static millis_t m1125_next_countdown_update = 0;
  const millis_t now = millis();

  const bool pause_active = M1125_IsPauseActive();

  // Detect pause start/stop transitions so we can timestamp the park event
  if (pause_active && !m1125_pause_was_active) {
    // Pause just started: record when "Nozzle Parked." was shown
    m1125_pause_start_ms = now;
    m1125_next_countdown_update = 0; // force immediate scheduling after delay
    m1125_pause_was_active = true;
  }
  else if (!pause_active && m1125_pause_was_active) {
    // Pause ended: clear any countdown/status we may have set
    m1125_pause_start_ms = 0;
    m1125_next_countdown_update = 0;
    m1125_pause_was_active = false;
    // Only clear if there's no delayed status that should take precedence
    if (!delayed_status_until) ScreenHandler.setstatusmessage("");
  }

  // While paused, maybe show the countdown after the initial 10s hold
  if (pause_active) {
    // Only begin after the "Nozzle Parked." has been visible for 10s
    if (m1125_pause_start_ms && ELAPSED(now, m1125_pause_start_ms + 10000)) {
      if (m1125_next_countdown_update == 0 || ELAPSED(now, m1125_next_countdown_update)) {
        m1125_next_countdown_update = now + 5000;

        // Prefer the short-grace remaining reported by M1125 if active
        uint32_t rem = M1125_TimeoutRemainingSeconds();
        if (rem == 0) {
          // No grace window active yet: compute remaining until the
          // initial idle timeout based on pause_start + configured interval
          const uint32_t interval = M1125_TimeoutIntervalSeconds();
          const long diff = (long)((m1125_pause_start_ms + SEC_TO_MS(interval)) - now);
          rem = (diff > 0) ? ((uint32_t)((diff + 999) / 1000)) : 0;
        }

        // Format and post the countdown, but don't override delayed_status
        if (rem > 0 && !delayed_status_until) {
          char buf[VP_M117_LEN] = {0};
          sprintf_P(buf, PSTR("Heaters timeout in %u seconds"), (unsigned)rem);
          ScreenHandler.setstatusmessage(buf);
        }
      }
    }
  }

  // If a delayed status was shown and its clear timeout expired, clear it
  if (delayed_status_clear_at && ELAPSED(ms2, delayed_status_clear_at)) {
    delayed_status_clear_at = 0;
    ScreenHandler.setstatusmessage("");
    // Clear the owned buffer
    delayed_status_buffer[0] = '\0';
  }

  return IsScreenComplete();
}

void DGUSScreenHandler::HandleMaterialPreheatPreset(DGUS_VP_Variable &var, void *val_ptr) {
  // Extract temperature value from the display (convert from big-endian to little-endian)
  const int16_t value = swap16(*(uint16_t*)val_ptr);

  // Determine which material preset and parameter to update based on VP address
  switch (var.VP) {
    case VP_PREHEAT_PLA_HOTEND_TEMP:
      ui.material_preset[0].hotend_temp = value;
      SERIAL_ECHOLNPGM("Updated PLA hotend preset to ", value);
      break;
    case VP_PREHEAT_PLA_BED_TEMP:
      ui.material_preset[0].bed_temp = value;
      SERIAL_ECHOLNPGM("Updated PLA bed preset to ", value);
      break;
    #if PREHEAT_COUNT > 1
    case VP_PREHEAT_ABS_HOTEND_TEMP:
      ui.material_preset[1].hotend_temp = value;
      SERIAL_ECHOLNPGM("Updated ABS hotend preset to ", value);
      break;
    case VP_PREHEAT_ABS_BED_TEMP:
      ui.material_preset[1].bed_temp = value;
      SERIAL_ECHOLNPGM("Updated ABS bed preset to ", value);
      break;
    #endif
    default:
      SERIAL_ECHOLNPGM("Unknown preheat preset VP: ", var.VP);
      return;
  }

  // Save settings to EEPROM to persist the change
  RequestSaveSettings();
}

void DGUSScreenHandler::DGUSLCD_SendMaterialPreheatPresetToDisplay(DGUS_VP_Variable &var) {
  int16_t value = 0;

  // Determine which material preset value to read based on VP address
  switch (var.VP) {
    case VP_PREHEAT_PLA_HOTEND_TEMP:
      value = ui.material_preset[0].hotend_temp;
      break;
    case VP_PREHEAT_PLA_BED_TEMP:
      value = ui.material_preset[0].bed_temp;
      break;
    #if PREHEAT_COUNT > 1
    case VP_PREHEAT_ABS_HOTEND_TEMP:
      value = ui.material_preset[1].hotend_temp;
      break;
    case VP_PREHEAT_ABS_BED_TEMP:
      value = ui.material_preset[1].bed_temp;
      break;
    #endif
    default:
      SERIAL_ECHOLNPGM("Unknown preheat preset VP for send: ", var.VP);
      return;
  }

  // Send the value to the display
  dgusdisplay.WriteVariable(var.VP, value);
}

#endif // HAS_DGUS_LCD
