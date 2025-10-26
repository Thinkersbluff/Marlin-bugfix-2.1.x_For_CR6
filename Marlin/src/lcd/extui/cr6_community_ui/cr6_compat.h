// Minimal compatibility shims for CR6 community DGUS UI when built against Marlin 2.1
#pragma once

// Debug helpers
#include "../../../core/debug_out.h"

// Ensure ExtUI declarations available for shims
#include "../ui_api.h"

// Use size_t for loop counters when iterating over sizeof() results
#include <stddef.h>

// Some builds expect DEBUG_ECHOLNPAIR/DEBUG_ECHOLNPAIR_F to exist.
#ifndef DEBUG_ECHOLNPAIR
  #define DEBUG_ECHOLNPAIR(...) DEBUG_ECHOLN(__VA_ARGS__)
#endif
#ifndef DEBUG_ECHOLNPAIR_F
  #define DEBUG_ECHOLNPAIR_F(...) DEBUG_ECHOLNPGM(__VA_ARGS__)
#endif

// Some code uses DEBUG_ECHOPAIR - alias to existing macro
#ifndef DEBUG_ECHOPAIR
  #define DEBUG_ECHOPAIR(...) DEBUG_ECHOLNPAIR(__VA_ARGS__)
#endif

// Serial echo pair helpers used in some CR6 UI files
#ifndef SERIAL_ECHOLNPAIR
  #define SERIAL_ECHOLNPAIR(...) SERIAL_ECHOLN(__VA_ARGS__)
#endif
#ifndef SERIAL_ECHOPAIR
  #define SERIAL_ECHOPAIR(...) SERIAL_ECHO(__VA_ARGS__)
#endif

// Provide const_float_t if not present (alias to float)
#ifndef CONST_FLOAT_T_DEFINED
  typedef float const_float_t;
  #define CONST_FLOAT_T_DEFINED
#endif

// Provide LOOP_L_N macro used for byte-wise loops if missing
// Use size_t for the index and cast N to size_t to avoid signed/unsigned
// comparison warnings when N comes from sizeof() or other size_t expressions.
#ifndef LOOP_L_N
#  define LOOP_L_N(I,N) for (size_t I = 0; I < (size_t)(N); ++I)
#endif

// EITHER macro is an alias to ANY for preprocessor expressions
#ifndef EITHER
  #define EITHER(...) ANY(__VA_ARGS__)
#endif

// If the CR6 orientation macro isn't defined, default to 0
#ifndef DGUS_LCD_UI_CR6_COMM_ORIENTATION
  #define DGUS_LCD_UI_CR6_COMM_ORIENTATION 0
#endif

// Provide SERIAL_GET_TX_BUFFER_FREE if a HAL doesn't define it for LCD serial
#ifndef SERIAL_GET_TX_BUFFER_FREE
  #if defined(LCD_SERIAL) && defined(LCD_SERIAL_AVAILABLE_FOR_WRITE)
    #define SERIAL_GET_TX_BUFFER_FREE() (LCD_SERIAL_AVAILABLE_FOR_WRITE())
  #elif defined(LCD_SERIAL) && defined(LCD_SERIAL_AVAILABLE)
    #define SERIAL_GET_TX_BUFFER_FREE() (LCD_SERIAL.availableForWrite())
  #else
    // As a last resort assume 64 bytes are free. This is conservative but prevents build failures.
    #define SERIAL_GET_TX_BUFFER_FREE() (64)
  #endif
#endif

// ExtUI API compatibility: legacy CR6 UI expects getMeshValid(), setCancelState(), resetCancelState()
// Implement small inline wrappers that forward to existing methods when available.
#ifndef EXTUI_LEGACY_COMPAT
#define EXTUI_LEGACY_COMPAT
namespace ExtUI {

  #if HAS_MESH
    // Some CR6 display implementations add an ExtUI::onMeshLevelingStart() callback
    // but that name is not part of the core ui_api.h declaration. Provide a forward
    // declaration so cr6_compat can call it when present without requiring changes
    // to the core headers.
    void onMeshLevelingStart();
  #endif
  inline bool getMeshValid() {
    #if ANY(HAS_LEVELING, HAS_MESH)
      return ExtUI::getLevelingIsValid();
    #else
      return false;
    #endif
  }
  inline void setCancelState() { /* legacy shim: nothing special, UI handlers check flags */ }
  inline void resetCancelState() { /* legacy shim: nothing special */ }
}
#endif

// Some helper aliases used by CR6 UI
#ifndef IS_SD_FILE_OPEN
  // CardReader has isFileOpen() in this tree
  #define IS_SD_FILE_OPEN() (CardReader::isFileOpen())
#endif

#ifndef IS_SD_PRINTING
  #define IS_SD_PRINTING() (CardReader::flag.sdprinting)
#endif

#ifndef SERIAL_ECHO_F
#  define SERIAL_ECHO_F(val, prec) SERIAL_ECHO(val)
#endif

#ifndef ExtUI_isWaitingOnUser
  namespace ExtUI { inline bool isWaitingOnUser() { return awaitingUserConfirm(); } }
#endif

#ifndef ExtUI_isMediaInserted
  namespace ExtUI { inline bool isMediaInserted() { return CardReader::isInserted(); } }
#endif

// Language message fallbacks (some builds reference these symbols directly)
#ifndef MSG_THERMAL_RUNAWAY
  #define MSG_THERMAL_RUNAWAY MSG_ERR_MAXTEMP
#endif
#ifndef MSG_HEATING_FAILED_LCD
  #define MSG_HEATING_FAILED_LCD MSG_ERR_MAXTEMP
#endif

// PID / message compatibility: older CR6 code expects these symbols
#ifndef PID_BAD_EXTRUDER_NUM
#  ifdef PID_BAD_HEATER_ID
#    define PID_BAD_EXTRUDER_NUM PID_BAD_HEATER_ID
#  else
#    define PID_BAD_EXTRUDER_NUM PID_BAD_HEATER_ID
#  endif
#endif

#ifndef MSG_PID_BAD_EXTRUDER_NUM
#  define MSG_PID_BAD_EXTRUDER_NUM MSG_ERR_MAXTEMP
#endif

// Small typedef used by DGUSScreenHandler for program-memory strings
// Match the project's FSTR_P alias so GET_TEXT_F() (which expands to FPSTR)
// can be assigned to this type. Try several fallbacks to match the core
// Arduino/Marlin definitions so compilation works regardless of include order.
#if defined(FSTR_P)
  typedef FSTR_P progmem_str;
#elif defined(FPSTR) || defined(ARDUINO)
  // If FPSTR is available (or we're in an Arduino build), use the Arduino
  // flash string helper type which FPSTR/GET_TEXT_F typically return.
  typedef const __FlashStringHelper* progmem_str;
#else
  typedef const char* progmem_str;
#endif

// Probe settings shim: CR6 UI references probe.settings.*. Probe exists in this tree
// so we only provide a compile-time check alias if needed.

// Provide missing ExtUI callbacks expected by core Marlin. Some builds call
// ExtUI::onXYZ() even if the ExtUI implementation doesn't implement them.
// Provide lightweight forwarders / no-op defaults here to avoid link errors.
namespace ExtUI {

  // Declare ExtUI callbacks that may be referenced by core modules.
  // CR6 compatibility .cpp provides non-inline definitions so core TUs
  // that don't include this header will still link correctly.
  void onSetPowerLoss(const bool onoff);

  // Leveling events
  void onLevelingStart();
  void onLevelingDone();

  // Homing events
  void onHomingDone();

  // Min extrusion temp changed
  void onSetMinExtrusionTemp(const celsius_t t);

  // Firmware flash event
  void onFirmwareFlash();

  // Print done event
  void onPrintDone();

  // PID/autotune events
  #if HAS_PID_HEATING
    void onPIDTuning(const pidresult_t rst);
    void onStartM303(const int count, const heater_id_t hid, const celsius_t temp);
  #endif

  // Generic notification stubs used by core
#if ENABLED(ADVANCED_PAUSE_FEATURE)
  void onPauseMode(PauseMessage m, PauseMode mm, uint8_t extruder);
#endif
  void onMediaMounted();
  void onSettingsStored(bool ok);
  void onSettingsLoaded(bool ok);
  void onAxisEnabled(const axis_t a);
  void onAxisDisabled(const axis_t a);
  void onMaxTempError(const heater_id_t h);
  void onMinTempError(const heater_id_t h);
  void onHeatingError(const heater_id_t h);

} // namespace ExtUI

#if !ENABLED(ADVANCED_PAUSE_FEATURE)
// Provide lightweight fallbacks so CR6 UI code that references pause enums
// and a pause-mode status compiles even when ADVANCED_PAUSE_FEATURE is off.
namespace ExtUI {
  // Minimal numeric constants matching pause.h when ADVANCED_PAUSE_FEATURE is enabled
  enum PauseMessageFallback : char {
    PAUSE_MESSAGE_PARKING = 0,
    PAUSE_MESSAGE_CHANGING,
    PAUSE_MESSAGE_WAITING,
    PAUSE_MESSAGE_INSERT,
    PAUSE_MESSAGE_LOAD,
    PAUSE_MESSAGE_UNLOAD,
    PAUSE_MESSAGE_PURGE,
    PAUSE_MESSAGE_OPTION,
    PAUSE_MESSAGE_RESUME,
    PAUSE_MESSAGE_HEAT,
    PAUSE_MESSAGE_HEATING,
    PAUSE_MESSAGE_STATUS,
    PAUSE_MESSAGE_COUNT
  };

  enum PauseModeFallback : char {
    PAUSE_MODE_SAME = 0,
    PAUSE_MODE_PAUSE_PRINT,
    PAUSE_MODE_CHANGE_FILAMENT,
    PAUSE_MODE_LOAD_FILAMENT,
    PAUSE_MODE_UNLOAD_FILAMENT
  };

  // Provide a modifiable pauseModeStatus so UI code can still read it.
  extern PauseMessageFallback pauseModeStatus;
  inline PauseModeFallback getPauseMode() { return PAUSE_MODE_SAME; }
}
#endif

// Provide unqualified symbols/macros that older CR6 UI code expects to be
// present in the global namespace. These map to the namespaced ExtUI:: enums
// defined above so files that reference plain PAUSE_MESSAGE_* or PAUSE_MODE_*
// constants still compile when ADVANCED_PAUSE_FEATURE is disabled.
#if !ENABLED(ADVANCED_PAUSE_FEATURE)
  // Pause messages
  #define PAUSE_MESSAGE_PARKING    ExtUI::PAUSE_MESSAGE_PARKING
  #define PAUSE_MESSAGE_CHANGING   ExtUI::PAUSE_MESSAGE_CHANGING
  #define PAUSE_MESSAGE_WAITING    ExtUI::PAUSE_MESSAGE_WAITING
  #define PAUSE_MESSAGE_INSERT     ExtUI::PAUSE_MESSAGE_INSERT
  #define PAUSE_MESSAGE_LOAD       ExtUI::PAUSE_MESSAGE_LOAD
  #define PAUSE_MESSAGE_UNLOAD     ExtUI::PAUSE_MESSAGE_UNLOAD
  #define PAUSE_MESSAGE_PURGE      ExtUI::PAUSE_MESSAGE_PURGE
  #define PAUSE_MESSAGE_OPTION     ExtUI::PAUSE_MESSAGE_OPTION
  #define PAUSE_MESSAGE_RESUME     ExtUI::PAUSE_MESSAGE_RESUME
  #define PAUSE_MESSAGE_HEAT       ExtUI::PAUSE_MESSAGE_HEAT
  #define PAUSE_MESSAGE_HEATING    ExtUI::PAUSE_MESSAGE_HEATING
  #define PAUSE_MESSAGE_STATUS     ExtUI::PAUSE_MESSAGE_STATUS

  // Pause modes
  #define PAUSE_MODE_SAME             ExtUI::PAUSE_MODE_SAME
  #define PAUSE_MODE_PAUSE_PRINT      ExtUI::PAUSE_MODE_PAUSE_PRINT
  #define PAUSE_MODE_CHANGE_FILAMENT  ExtUI::PAUSE_MODE_CHANGE_FILAMENT
  #define PAUSE_MODE_LOAD_FILAMENT    ExtUI::PAUSE_MODE_LOAD_FILAMENT
  #define PAUSE_MODE_UNLOAD_FILAMENT  ExtUI::PAUSE_MODE_UNLOAD_FILAMENT

  // Filament-change related configuration defaults (conservative values)
  #ifndef FILAMENT_CHANGE_SLOW_LOAD_LENGTH
    #define FILAMENT_CHANGE_SLOW_LOAD_LENGTH 100.0f
  #endif
  #ifndef FILAMENT_CHANGE_ALERT_BEEPS
    #define FILAMENT_CHANGE_ALERT_BEEPS 1
  #endif

  // Provide minimal no-op stubs for load_filament / unload_filament so UI code
  // that calls these functions still links when ADVANCED_PAUSE_FEATURE is off.
  // Match the real prototypes closely so argument counts align.
  static inline bool load_filament(
    const float slow_load_length = 0,
    const float fast_load_length = 0,
    const float purge_length = 0,
    const int8_t max_beep_count = 0,
    const bool show_lcd = false,
    const bool pause_for_user = false,
    int /*mode*/ = PAUSE_MODE_SAME
  ) { (void)slow_load_length; (void)fast_load_length; (void)purge_length; (void)max_beep_count; (void)show_lcd; (void)pause_for_user; return false; }

  static inline bool unload_filament(
    const float unload_length,
    const bool show_lcd = false,
    int /*mode*/ = PAUSE_MODE_SAME
  ) { (void)unload_length; (void)show_lcd; return false; }
#endif

