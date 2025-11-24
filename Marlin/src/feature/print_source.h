/**
 * print_source.h
 *
 * Small helper to provide a canonical print-source state for the
 * firmware: whether the active print is coming from the Host (serial/OctoPrint)
 * or from the media (SD card / USB). This centralizes checks so UI and
 * pause/resume flows can make deterministic decisions.
 */
#pragma once

#include "../inc/MarlinConfig.h"

namespace PrintSource {
  // Minimal header-only implementation. Use an internal function-local
  // static to hold the canonical source so we don't require a separate
  // translation unit. Functions are inline to avoid multiple-definition
  // issues when included across many files.

  enum class Source : uint8_t { NONE = 0, HOST = 1, SD = 2 };

  static inline Source &source_instance() {
    static Source s = Source::NONE;
    return s;
  }

  static inline void set_printing_from_host() {
    source_instance() = Source::HOST;
    PORT_REDIRECT(SerialMask::All);
    SERIAL_ECHOLNPGM("[DEBUG] PrintSource: set to HOST");
  }

  static inline void set_printing_from_sd() {
    source_instance() = Source::SD;
    PORT_REDIRECT(SerialMask::All);
    SERIAL_ECHOLNPGM("[DEBUG] PrintSource: set to SD");
  }

  static inline void clear_printing_source() {
    source_instance() = Source::NONE;
    PORT_REDIRECT(SerialMask::All);
    SERIAL_ECHOLNPGM("[DEBUG] PrintSource: cleared");
  }

  static inline bool printingFromHost() { return source_instance() == Source::HOST; }
  static inline bool printingFromSDCard() { return source_instance() == Source::SD; }

} // namespace PrintSource
