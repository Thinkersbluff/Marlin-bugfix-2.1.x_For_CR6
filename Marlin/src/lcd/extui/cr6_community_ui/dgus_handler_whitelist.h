/**
 * Whitelist of DGUS VP handlers that are allowed to run even when printing from Host.
 * This prevents SD-specific handlers from running during host-streamed prints and
 * causing confusing UI or SD-side actions.
 */
#pragma once

#include "DGUSDisplayDef.h"
#include <stdint.h>

// Keep the list intentionally small. Add entries here if a handler must run
// during Host-streamed prints. Use VP identifiers from DGUSDisplayDef.h.
constexpr uint16_t DGUS_HANDLER_WHITELIST[] = {
  VP_M1125_TIMEOUT_CONFIRM, // M1125 heater-timeout confirm must be handled even for host prints
};

inline bool handler_allowed_for_host(const uint16_t vp) {
  for (auto id : DGUS_HANDLER_WHITELIST) if (id == vp) return true;
  return false;
}
