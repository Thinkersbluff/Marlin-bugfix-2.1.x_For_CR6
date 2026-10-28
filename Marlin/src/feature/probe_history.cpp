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

#include "../inc/MarlinConfig.h"
#include "probe_history.h"

#if ENABLED(Z_MIN_PROBE_REPEATABILITY_TEST) && ENABLED(EEPROM_SETTINGS)
#include "../HAL/shared/eeprom_api.h"
#include "../module/settings.h"
#include "../core/serial.h"

ProbeHistory probe_history;

float ProbeHistory::baseline_trigger_height = NAN;
bool ProbeHistory::history_valid = false;

void ProbeHistory::init() {
  // M48 history structures (must match M48.cpp)
  struct HistHeader { uint32_t magic; uint8_t count; uint8_t idx; uint16_t reserved; };
  struct ProbeRepeatEntry { float median_trigger; float sigma_trigger; uint16_t n_samples; float measured_range; uint32_t ts; };
  
  constexpr uint32_t PROBE_HIST_MAGIC = 0x50525248; // 'PRRH'
  constexpr int PROBE_HIST_N = 8;
  constexpr int MIN_HISTORY_FOR_BASELINE = 4; // Need at least 4 entries for reliable baseline
  
  const int base = MarlinSettings::eeprom_offset() + MarlinSettings::datasize();
  int pos = base;
  
  HistHeader hdr;
  persistentStore.access_start();
  
  if (persistentStore.read_data(pos, (uint8_t*)&hdr, sizeof(hdr))) {
    SERIAL_ECHOLNPGM("ProbeHistory: no history in EEPROM");
    persistentStore.access_finish();
    history_valid = false;
    baseline_trigger_height = NAN;
    return;
  }
  
  if (hdr.magic != PROBE_HIST_MAGIC) {
    SERIAL_ECHOLNPGM("ProbeHistory: magic mismatch");
    persistentStore.access_finish();
    history_valid = false;
    baseline_trigger_height = NAN;
    return;
  }
  
  if (hdr.count < MIN_HISTORY_FOR_BASELINE) {
    SERIAL_ECHOLNPGM("ProbeHistory: insufficient data (", hdr.count, " entries, need ", MIN_HISTORY_FOR_BASELINE, ")");
    persistentStore.access_finish();
    history_valid = false;
    baseline_trigger_height = NAN;
    return;
  }
  
  // Read all median_trigger values from history
  float medians[PROBE_HIST_N];
  uint8_t count = 0;
  
  for (uint8_t i = 0; i < hdr.count; ++i) {
    const uint8_t idx = (hdr.idx + PROBE_HIST_N - hdr.count + i) % PROBE_HIST_N;
    const int entry_pos = base + sizeof(hdr) + idx * sizeof(ProbeRepeatEntry);
    
    ProbeRepeatEntry entry;
    persistentStore.read_data(entry_pos, (uint8_t*)&entry, sizeof(entry));
    
    if (!isnan(entry.median_trigger)) {
      medians[count++] = entry.median_trigger;
    }
  }
  
  persistentStore.access_finish();
  
  if (count < MIN_HISTORY_FOR_BASELINE) {
    SERIAL_ECHOLNPGM("ProbeHistory: insufficient valid entries");
    history_valid = false;
    baseline_trigger_height = NAN;
    return;
  }
  
  // Sort medians for trimmed median calculation
  for (uint8_t a = 0; a < count; ++a) {
    for (uint8_t b = a + 1; b < count; ++b) {
      if (medians[b] < medians[a]) {
        const float t = medians[a];
        medians[a] = medians[b];
        medians[b] = t;
      }
    }
  }
  
  // Compute trimmed median: discard highest and lowest if we have enough data
  float baseline;
  if (count > 4) {
    // Trim: skip first and last (already sorted), then compute median of middle values
    const uint8_t trimmed_count = count - 2;
    const uint8_t mid = trimmed_count / 2;
    baseline = (trimmed_count & 1) 
      ? medians[1 + mid]  // Odd count: middle value (offset by 1 for trim)
      : (medians[1 + mid - 1] + medians[1 + mid]) / 2.0f;  // Even count: average of two middle values
  }
  else {
    // Just use median for small datasets
    baseline = (count & 1) ? medians[count / 2] : (medians[count / 2 - 1] + medians[count / 2]) / 2.0f;
  }
  
  baseline_trigger_height = baseline;
  history_valid = true;
  
  SERIAL_ECHOLNPGM("ProbeHistory: loaded ", count, " entries, baseline=", p_float_t(baseline, 3), " mm");
}

float ProbeHistory::get_baseline_trigger_height() {
  return baseline_trigger_height;
}

bool ProbeHistory::has_valid_history() {
  return history_valid;
}

#endif // Z_MIN_PROBE_REPEATABILITY_TEST && EEPROM_SETTINGS
