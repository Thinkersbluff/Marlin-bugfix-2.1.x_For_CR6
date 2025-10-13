// Compatibility strong definitions for CR6 DGUS UI
#include "cr6_compat.h"
#include "../ui_api.h"
#include "../../marlinui.h"

namespace ExtUI {

  void onSetPowerLoss(const bool onoff) {
    #if ENABLED(POWER_LOSS_RECOVERY)
      // Default: call the generic handler if EXTENSIBLE_UI provides it.
      TERN_(EXTENSIBLE_UI, onPowerLoss());
    #else
      (void)onoff;
    #endif
  }

  void onPowerLoss() {
    // Provide a minimal no-op implementation so callers that forward to
    // ExtUI::onPowerLoss() link correctly when the display doesn't
    // implement this hook.
    // If POWER_LOSS_RECOVERY is enabled, the display's OnPowerlossResume
    // handler is used separately; keep this no-op to be safe.
  }

  #if !ENABLED(DGUS_LCD_UI_CR6_COMM)
  void onLevelingStart() {
    #if HAS_MESH
      // If the CR6 code provides a mesh-leveling start hook, call it.
      // The declaration exists in cr6_compat.h as a forward-decl.
      TERN_(EXTENSIBLE_UI, onMeshLevelingStart());
    #endif
  }

  void onLevelingDone() { /* no-op */ }
  #endif

  

  void onSetMinExtrusionTemp(const celsius_t t) { (void)t; }

  void onFirmwareFlash() { /* no-op */ }

  void onPrintDone() { /* no-op */ }

#if HAS_PID_HEATING
  void onPIDTuning(const pidresult_t rst) { (void)rst; }
  void onStartM303(const int count, const heater_id_t hid, const celsius_t temp) { (void)count; (void)hid; (void)temp; }
#endif

  void onPrinterKilled(FSTR_P const error, FSTR_P const component) { (void)error; (void)component; }
  void onPauseMode(PauseMessage m, PauseMode mm, uint8_t extruder) { stdOnPauseMode(m, mm, extruder); }
  void onMediaMounted() { /* no-op */ }
  void onSettingsStored(bool ok) { (void)ok; }
  void onSettingsLoaded(bool ok) { (void)ok; }
  void onAxisEnabled(const axis_t a) { (void)a; }
  void onAxisDisabled(const axis_t a) { (void)a; }
  void onMaxTempError(const heater_id_t h) { (void)h; }
  void onMinTempError(const heater_id_t h) { (void)h; }
  void onHeatingError(const heater_id_t h) { (void)h; }

} // namespace ExtUI

// Provide strong definitions only for MarlinUI static members that are declared
// in the headers. Match the same guards used in marlinui.h to avoid conflicts.

// Some external UI code references ui.remaining_time directly when
// SHOW_REMAINING_TIME is not enabled. The header declares the member in
// that case, so provide a single strong definition here to satisfy the
// linker when the core doesn't define it.
#if !defined(SHOW_REMAINING_TIME)
  uint32_t MarlinUI::remaining_time = 0;
#endif

// If the core doesn't provide HAS_PREHEAT, marlinui.h defines a minimal
// preheat_t and declares material_preset. Provide the strong definition
// only in that compatibility case so we don't conflict with the real
// definition when HAS_PREHEAT is enabled.
#if !defined(HAS_PREHEAT)
  MarlinUI::preheat_t MarlinUI::material_preset[PREHEAT_COUNT] = {};
#endif
