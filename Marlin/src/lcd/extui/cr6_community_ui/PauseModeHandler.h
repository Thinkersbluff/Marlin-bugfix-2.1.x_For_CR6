#ifndef PAUSE_MODE_HANDLER_H
#define PAUSE_MODE_HANDLER_H

#include "../ui_api.h"

// Lightweight handler to centralize CR6-specific pause/menu behavior.
// This file provides a small facade that maps Marlin pause messages and modes
// to DGUS screens and user interactions. The goal is to avoid scattering
// pause handling logic across the codebase and to provide a single place to
// extend behavior (localization, alternate flows, conditional navigation).
//
// Note for maintainers: the centralized handler is only present when
// ADVANCED_PAUSE_FEATURE is enabled. Call sites should be guarded with
// #if ENABLED(ADVANCED_PAUSE_FEATURE) so this module can be safely omitted
// from builds that don't use Marlin's advanced pause machinery. This avoids
// accidental mismatched signatures and makes the codebase easier to reason
// about when advanced pause is disabled.

namespace CR6PauseHandler {

  // Initialize the handler (if needed)
  void Init();

  // Called whenever Marlin requests a user confirmation for a pause mode.
  // The implementation should decide which DGUS screen to show and which
  // actions (setPauseMenuResponse(), setUserConfirmed(), etc.) to set.
#if ENABLED(ADVANCED_PAUSE_FEATURE)
  void HandlePauseMessage(const PauseMessage message, const PauseMode mode, uint8_t extruder);
#else
  // When ADVANCED_PAUSE_FEATURE is disabled the centralized pause handler
  // is not available. Callers must guard calls with #if ENABLED(ADVANCED_PAUSE_FEATURE)
  // so that no unreferenced fallback is compiled. This avoids mismatched
  // signatures and makes removal of the fallback safe.
  // NOTE: Do not declare a fallback signature here; callers should be guarded.
#endif

} // namespace CR6PauseHandler

#endif // PAUSE_MODE_HANDLER_H
