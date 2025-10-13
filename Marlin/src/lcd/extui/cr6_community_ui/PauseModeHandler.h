#ifndef PAUSE_MODE_HANDLER_H
#define PAUSE_MODE_HANDLER_H

#include "../ui_api.h"

// Lightweight handler to centralize CR6-specific pause/menu behavior.
// This file provides a small facade that maps Marlin pause messages and modes
// to DGUS screens and user interactions. The goal is to avoid scattering
// pause handling logic across the codebase and to provide a single place to
// extend behavior (localization, alternate flows, conditional navigation).

namespace CR6PauseHandler {

  // Initialize the handler (if needed)
  void Init();

  // Called whenever Marlin requests a user confirmation for a pause mode.
  // The implementation should decide which DGUS screen to show and which
  // actions (setPauseMenuResponse(), setUserConfirmed(), etc.) to set.
  void HandlePauseMessage(const PauseMessage message, const PauseMode mode, uint8_t extruder);

} // namespace CR6PauseHandler

#endif // PAUSE_MODE_HANDLER_H
