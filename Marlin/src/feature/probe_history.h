/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2025 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
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
#pragma once

/**
 * probe_history.h - M48 probe repeatability history processing
 * 
 * Loads M48 history from EEPROM at boot and computes baseline trigger height
 * for use in M1128 early contamination detection.
 */

#include "../inc/MarlinConfig.h"

#if ENABLED(Z_MIN_PROBE_REPEATABILITY_TEST) && ENABLED(EEPROM_SETTINGS)

class ProbeHistory {
public:
  // Initialize and load M48 history from EEPROM
  static void init();
  
  // Get the baseline trigger height (trimmed median of historical means)
  static float get_baseline_trigger_height();
  
  // Check if we have valid history data
  static bool has_valid_history();
  
private:
  static float baseline_trigger_height;
  static bool history_valid;
};

extern ProbeHistory probe_history;

#endif // Z_MIN_PROBE_REPEATABILITY_TEST && EEPROM_SETTINGS
