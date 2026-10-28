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

#include "../../inc/MarlinConfig.h"

#if ENABLED(Z_MIN_PROBE_REPEATABILITY_TEST)

#include "../gcode.h"
#include "../../module/motion.h"
#include "../../module/probe.h"
#include "../../lcd/marlinui.h"

#include "../../feature/bedlevel/bedlevel.h"

#if HAS_LEVELING
  #include "../../module/planner.h"
#endif

#if HAS_PTC
  #include "../../feature/probe_temp_comp.h"
#endif

#if ENABLED(EEPROM_SETTINGS)
  #include "../../HAL/shared/eeprom_api.h"
  #include "../../module/settings.h"
#endif

/**
 * M48: Z probe repeatability measurement function.
 *
 * Usage:
 *   M48 <P#> <X#> <Y#> <V#> <E> <L#> <S> <C#> <W> <H>
 *     P = Number of sampled points (4-50, default 10)
 *     X = Sample X position
 *     Y = Sample Y position
 *     V = Verbose level (0-4, default=1)
 *     E = Engage Z probe for each reading
 *     L = Number of legs of movement before probe
 *     S = Schizoid (Or Star if you prefer)
 *     C = Enable probe temperature compensation (0 or 1, default 1)
 *     W = Save result to EEPROM history (stores up to 8 runs)
 *     H = Display EEPROM history only (no probing) - defaults to H1
 *     H1 = Display EEPROM history (same as H with no value)
 *     H0 = Erase EEPROM history
 *
 * This function requires the machine to be homed before invocation.
 * 
 * Note: M48 history is automatically preserved when upgrading firmware
 * at the same EEPROM version using M502 or automatic EEPROM initialization.
 */

void GcodeSuite::M48() {

#if ENABLED(EEPROM_SETTINGS)
  // Helper to emit stored M48 history without probing
  const auto dump_history = []{
    struct HistHeader { uint32_t magic; uint8_t count; uint8_t idx; uint16_t reserved; } hdr;
    struct ProbeRepeatEntry { float median_trigger; float sigma_trigger; uint16_t n_samples; float measured_range; uint32_t ts; } entry;
    constexpr uint32_t PROBE_HIST_MAGIC = 0x50525248; // 'PRRH'
    constexpr int PROBE_HIST_N = 8;
    const int base = MarlinSettings::eeprom_offset() + MarlinSettings::datasize();
    int pos = base;
    persistentStore.access_start();
    if (persistentStore.read_data(pos, (uint8_t*)&hdr, sizeof(hdr))) {
      SERIAL_ECHOLNPGM("M48: no history found in EEPROM.");
      persistentStore.access_finish();
      return;
    }
    if (hdr.magic != PROBE_HIST_MAGIC) {
      SERIAL_ECHOLNPGM("M48: history magic mismatch - no history recorded.");
      persistentStore.access_finish();
      return;
    }
    SERIAL_ECHOPGM("M48 EEPROM history count="); SERIAL_ECHOLN(hdr.count);
    for (uint8_t i = 0; i < hdr.count; ++i) {
      const uint8_t idx = (hdr.idx + PROBE_HIST_N - hdr.count + i) % PROBE_HIST_N;
      const int entry_pos = base + sizeof(hdr) + idx * sizeof(entry);
      persistentStore.read_data(entry_pos, (uint8_t*)&entry, sizeof(entry));
      SERIAL_ECHOPGM("#"); SERIAL_ECHO(i + 1);
      SERIAL_ECHOPGM(" median="); SERIAL_ECHO(p_float_t(entry.median_trigger, 3));
      SERIAL_ECHOPGM(" sigma="); SERIAL_ECHO(p_float_t(entry.sigma_trigger, 3));
      SERIAL_ECHOPGM(" n="); SERIAL_ECHO(entry.n_samples);
      SERIAL_ECHOPGM(" range="); SERIAL_ECHO(p_float_t(entry.measured_range, 3)); SERIAL_EOL();
    }
    persistentStore.access_finish();
  };

  // Check for history operations (H parameter)
  if (parser.seen('H')
    && !(parser.seen('P') || parser.seen('X') || parser.seen('Y') || parser.seen('V')
       || parser.seen('E') || parser.seen('L') || parser.seen('S') || parser.seen('C') || parser.seen('W'))) {
    // M48 H with no value defaults to 1 (display), M48 H0 erases, M48 H1+ displays
    const int h_val = parser.seenval('H') ? parser.intval('H') : 1;
    if (h_val == 0) {
      // Erase M48 history
      struct HistHeader { uint32_t magic; uint8_t count; uint8_t idx; uint16_t reserved; } hdr;
      const int base = MarlinSettings::eeprom_offset() + MarlinSettings::datasize();
      
      hdr.magic = 0x00000000;  // Clear magic to invalidate
      hdr.count = 0;
      hdr.idx = 0;
      hdr.reserved = 0;
      
      persistentStore.access_start();
      persistentStore.write_data(base, (const uint8_t*)&hdr, sizeof(hdr));
      persistentStore.access_finish();
      
      SERIAL_ECHOLNPGM("M48: EEPROM history erased.");
    }
    else {
      // Display history
      dump_history();
    }
    return;
  }
#endif

  if (homing_needed_error()) return;

  const int8_t verbose_level = parser.byteval('V', 1);
  if (!WITHIN(verbose_level, 0, 4)) {
    SERIAL_ECHOLNPGM(GCODE_ERR_MSG("(V)erbose level implausible (0-4)."));
    return;
  }

  const int8_t n_samples = parser.byteval('P', 10);
  if (!WITHIN(n_samples, 4, 50)) {
    SERIAL_ECHOLNPGM(GCODE_ERR_MSG("Sample size not plausible (4-50)."));
    return;
  }

  const ProbePtRaise raise_after = parser.boolval('E') ? PROBE_PT_STOW : PROBE_PT_RAISE;

  // Test at the current position by default, overridden by X and Y
  const xy_pos_t test_position = {
    parser.linearval('X', current_position.x + probe.offset_xy.x),  // If no X use the probe's current X position
    parser.linearval('Y', current_position.y + probe.offset_xy.y)   // If no Y, ditto
  };

  if (!probe.can_reach(test_position)) {
    LCD_MESSAGE_MAX(MSG_M48_OUT_OF_BOUNDS);
    SERIAL_ECHOLNPGM(GCODE_ERR_MSG(" (X,Y) out of bounds."));
    return;
  }

  // Get the number of leg moves per test-point
  bool seen_L = parser.seen('L');
  uint8_t n_legs = seen_L ? parser.value_byte() : 0;
  if (n_legs > 15) {
    SERIAL_ECHOLNPGM(GCODE_ERR_MSG("Legs of movement implausible (0-15)."));
    return;
  }
  if (n_legs == 1) n_legs = 2;

  // Schizoid motion as an optional stress-test
  const bool schizoid_flag = parser.boolval('S');
  if (schizoid_flag && !seen_L) n_legs = 7;

  if (verbose_level > 0)
    SERIAL_ECHOLNPGM("M48 Z-Probe Repeatability Test");

  if (verbose_level > 2)
    SERIAL_ECHOLNPGM("Positioning the probe...");

  // Always disable Bed Level correction before probing...

  #if HAS_LEVELING
    const bool was_enabled = planner.leveling_active;
    set_bed_leveling_enabled(false);
  #endif

  TERN_(HAS_PTC, ptc.set_enabled(parser.boolval('C', true)));

  // Work with reasonable feedrates
  remember_feedrate_scaling_off();

  // Working variables
  float mean = 0.0,     // The average of all points so far, used to calculate deviation
        sigma = 0.0,    // Standard deviation of all points so far
        min = 99999.9,  // Smallest value sampled so far
        max = -99999.9, // Largest value sampled so far
        sample_set[n_samples],  // Storage for sampled values
        trigger_heights[50];    // Storage for absolute trigger heights (for EEPROM history)

  auto dev_report = [](const bool verbose, const float mean, const float sigma, const float min, const float max, const bool final=false) {
    if (verbose) {
      SERIAL_ECHO_START(); SERIAL_ECHOPGM("Deltas from Offset = Mean: "); SERIAL_ECHO(p_float_t(mean, 3));
      if (!final) { SERIAL_ECHOPGM(" Sigma: "); SERIAL_ECHO(p_float_t(sigma, 3)); }
      SERIAL_ECHOPGM(" Min: "); SERIAL_ECHO(p_float_t(min, 3));
      SERIAL_ECHOPGM(" Max: "); SERIAL_ECHO(p_float_t(max, 3));
      SERIAL_ECHOPGM(" Range: "); SERIAL_ECHO(p_float_t(max - min, 3)); SERIAL_EOL();
      if (final) SERIAL_EOL();
    }
    if (final) {
      SERIAL_ECHO_START(); SERIAL_ECHOPGM("Standard Deviation: "); SERIAL_ECHO(p_float_t(sigma, 3)); SERIAL_EOL();
      SERIAL_EOL();
    }
  };

  // Move to the first point, deploy, and probe
  const float t = probe.probe_at_point(test_position, raise_after, verbose_level);
  bool probing_good = !isnan(t);

  if (probing_good) {
    randomSeed(millis());

    float sample_sum = 0.0;

    for (uint8_t n = 0; n < n_samples; ++n) {
      #if HAS_STATUS_MESSAGE
        // Display M48 progress in the status bar
        ui.status_printf(0, F(S_FMT ": %d/%d"), GET_TEXT(MSG_M48_POINT), int(n + 1), int(n_samples));
      #endif

      // When there are "legs" of movement move around the point before probing
      if (n_legs) {

        // Pick a random direction, starting angle, and radius
        const int dir = (random(0, 10) > 5.0) ? -1 : 1;  // clockwise or counter clockwise
        float angle = random(0, 360);
        const float radius = random(
          #if ENABLED(DELTA)
            int(0.1250000000 * (PRINTABLE_RADIUS)),
            int(0.3333333333 * (PRINTABLE_RADIUS))
          #else
            int(5), int(0.125 * _MIN(X_BED_SIZE, Y_BED_SIZE))
          #endif
        );
        if (verbose_level > 3) {
          SERIAL_ECHOPGM("Start radius:", radius, " angle:", angle, " dir:");
          if (dir > 0) SERIAL_CHAR('C');
          SERIAL_ECHOLNPGM("CW");
        }

        // Move from leg to leg in rapid succession
        for (uint8_t l = 0; l < n_legs - 1; ++l) {

          // Move some distance around the perimeter
          float delta_angle;
          if (schizoid_flag) {
            // The points of a 5 point star are 72 degrees apart.
            // Skip a point and go to the next one on the star.
            delta_angle = dir * 2.0 * 72.0;
          }
          else {
            // Just move further along the perimeter.
            delta_angle = dir * (float)random(25, 45);
          }
          angle += delta_angle;

          // Trig functions work without clamping, but just to be safe...
          while (angle > 360.0) angle -= 360.0;
          while (angle < 0.0) angle += 360.0;

          // Choose the next position as an offset to chosen test position
          const xy_pos_t noz_pos = test_position - probe.offset_xy;
          xy_pos_t next_pos = {
            noz_pos.x + float(cos(RADIANS(angle))) * radius,
            noz_pos.y + float(sin(RADIANS(angle))) * radius
          };

          #if ENABLED(DELTA)
            // If the probe can't reach the point on a round bed...
            // Simply scale the numbers to bring them closer to origin.
            while (!probe.can_reach(next_pos)) {
              next_pos *= 0.8f;
              if (verbose_level > 3)
                SERIAL_ECHOLN(F("Moving inward: X"), next_pos.x, FPSTR(SP_Y_STR), next_pos.y);
            }
          #elif HAS_ENDSTOPS
            // For a rectangular bed just keep the probe in bounds
            LIMIT(next_pos.x, X_MIN_POS, X_MAX_POS);
            LIMIT(next_pos.y, Y_MIN_POS, Y_MAX_POS);
          #endif

          if (verbose_level > 3)
            SERIAL_ECHOLN(F("Going to: X"), next_pos.x, FPSTR(SP_Y_STR), next_pos.y);

          do_blocking_move_to_xy(next_pos);
        } // n_legs loop
      } // n_legs

      // Probe a single point
      const float pz = probe.probe_at_point(test_position, raise_after);

      // Break the loop if the probe fails
      probing_good = !isnan(pz);
      if (!probing_good) break;

      // Store absolute trigger height for history storage (before any offset adjustment)
      // pz contains the probe-offset-adjusted value, but we need the raw trigger Z
      trigger_heights[n] = pz - probe.offset.z;

      // Store the sample (pz is the offset-adjusted Z coordinate for statistical calculations)
      sample_set[n] = pz;

      // Keep track of the largest and smallest samples
      NOMORE(min, pz);
      NOLESS(max, pz);

      // Get the mean value of all samples thus far
      sample_sum += pz;
      mean = sample_sum / (n + 1);

      // Calculate the standard deviation so far.
      // The value after the last sample will be the final output.
      float dev_sum = 0.0;
      for (uint8_t j = 0; j <= n; ++j) dev_sum += sq(sample_set[j] - mean);
      sigma = SQRT(dev_sum / (n + 1));

      if (verbose_level > 1) {
        SERIAL_ECHO_START(); SERIAL_ECHO(n + 1); SERIAL_ECHOPGM(" of "); SERIAL_ECHO(n_samples);
        SERIAL_ECHOPGM(": z: "); SERIAL_ECHO(p_float_t(trigger_heights[n], 3)); SERIAL_CHAR(' ');
        dev_report(verbose_level > 2, mean, sigma, min, max);
        SERIAL_EOL();
      }

    } // n_samples loop
  }

  probe.stow();

  if (probing_good) {
    // Compute trimmed median from absolute trigger heights for storage (more robust than mean for historical baseline)
    // Sort trigger_heights for median calculation
    float trimmed_median = mean;  // Default to mean if not enough samples to trim
    if (n_samples > 4) {
      // Create a sorted copy of absolute trigger heights
      float sorted_triggers[50];
      for (uint8_t i = 0; i < n_samples; ++i) sorted_triggers[i] = trigger_heights[i];
      for (uint8_t a = 0; a < n_samples; ++a) {
        for (uint8_t b = a + 1; b < n_samples; ++b) {
          if (sorted_triggers[b] < sorted_triggers[a]) {
            const float t = sorted_triggers[a];
            sorted_triggers[a] = sorted_triggers[b];
            sorted_triggers[b] = t;
          }
        }
      }
      // Compute median of trimmed data (skip highest and lowest)
      const uint8_t trimmed_count = n_samples - 2;
      const uint8_t mid = trimmed_count / 2;
      trimmed_median = (trimmed_count & 1)
        ? sorted_triggers[1 + mid]  // Odd: middle value (offset by 1 for trim)
        : (sorted_triggers[1 + mid - 1] + sorted_triggers[1 + mid]) / 2.0f;  // Even: average of two middle
    }
    
    SERIAL_ECHOLNPGM("Finished!");
    dev_report(verbose_level > 0, mean, sigma, min, max, true);

#if ENABLED(EEPROM_SETTINGS)
    // Compare this M48 run with the oldest stored history entry and warn
    // (non-blocking). This comparison runs every time M48 completes and
    // does not cause writing — writing still requires W1.
    {
      struct HistHeader { uint32_t magic; uint8_t count; uint8_t idx; uint16_t reserved; } hdr;
      struct ProbeRepeatEntry { float median_trigger; float sigma_trigger; uint16_t n_samples; float measured_range; uint32_t ts; } entry;
      constexpr uint32_t PROBE_HIST_MAGIC = 0x50525248; // 'PRRH'
      constexpr int PROBE_HIST_N = 8;

      const int base = MarlinSettings::eeprom_offset() + MarlinSettings::datasize();
      int pos = base;
      persistentStore.access_start();
      if (persistentStore.read_data(pos, (uint8_t*)&hdr, sizeof(hdr))) {
        // No history present - nothing to compare
        persistentStore.access_finish();
      }
      else if (hdr.magic != PROBE_HIST_MAGIC || hdr.count == 0) {
        // Invalid or empty history
        persistentStore.access_finish();
      }
      else {
        // Oldest entry index
        const uint8_t oldest_idx = (hdr.idx + PROBE_HIST_N - hdr.count) % PROBE_HIST_N;
        const int entry_pos = base + sizeof(hdr) + oldest_idx * sizeof(entry);
        if (!persistentStore.read_data(entry_pos, (uint8_t*)&entry, sizeof(entry))) {
          const float new_median = trimmed_median;
          const float new_range = max - min;
          const float drift_thresh = _MAX(3.0f * entry.sigma_trigger, 0.05f);
          const float range_thresh = _MAX(3.0f * entry.sigma_trigger, 0.05f);
          bool warned = false;

          if (fabsf(new_median - entry.median_trigger) > drift_thresh) {
            SERIAL_ECHOLNPGM("M48: WARNING: median trigger drift vs oldest history exceeds threshold");
            SERIAL_ECHOPGM("  oldest_median="); SERIAL_ECHO(p_float_t(entry.median_trigger, 3));
            SERIAL_ECHOPGM(" new_median="); SERIAL_ECHO(p_float_t(new_median, 3));
            SERIAL_ECHOPGM(" thresh="); SERIAL_ECHOLN(p_float_t(drift_thresh, 3));
            warned = true;
          }

          if ((new_range - entry.measured_range) > range_thresh) {
            SERIAL_ECHOLNPGM("M48: WARNING: measured range increased vs oldest history");
            SERIAL_ECHOPGM("  oldest_range="); SERIAL_ECHO(p_float_t(entry.measured_range, 3));
            SERIAL_ECHOPGM(" new_range="); SERIAL_ECHO(p_float_t(new_range, 3));
            SERIAL_ECHOPGM(" thresh="); SERIAL_ECHOLN(p_float_t(range_thresh, 3));
            warned = true;
          }

          if (warned) SERIAL_ECHOLNPGM("M48: Advises: clean nozzle/drivetrain and rerun M48.");
        }
        persistentStore.access_finish();
      }
    }

    // Optional: save this M48 run to EEPROM history if requested with W1
    if (parser.boolval('W')) {
      // History layout: header then N entries
      struct HistHeader { uint32_t magic; uint8_t count; uint8_t idx; uint16_t reserved; } hdr;
      struct ProbeRepeatEntry { float median_trigger; float sigma_trigger; uint16_t n_samples; float measured_range; uint32_t ts; } entry;
      constexpr uint32_t PROBE_HIST_MAGIC = 0x50525248; // 'PRRH'
      constexpr int PROBE_HIST_N = 8;

      const int base = MarlinSettings::eeprom_offset() + MarlinSettings::datasize();
      int pos = base;
      persistentStore.access_start();
      // Read header if present
      if (persistentStore.read_data(pos, (uint8_t*)&hdr, sizeof(hdr))) {
        // read error -> initialize header
        hdr.magic = PROBE_HIST_MAGIC;
        hdr.count = 0;
        hdr.idx = 0;
        hdr.reserved = 0;
        pos = base;
        persistentStore.write_data(pos, (const uint8_t*)&hdr, sizeof(hdr));
      }

      // If header magic doesn't match, initialize
      if (hdr.magic != PROBE_HIST_MAGIC) {
        hdr.magic = PROBE_HIST_MAGIC;
        hdr.count = 0;
        hdr.idx = 0;
        hdr.reserved = 0;
        pos = base;
        persistentStore.write_data(pos, (const uint8_t*)&hdr, sizeof(hdr));
      }

      // Prepare entry (use trimmed_median for more robust historical baseline)
      entry.median_trigger = trimmed_median;
      entry.sigma_trigger = sigma;
      entry.n_samples = uint16_t(n_samples);
      entry.measured_range = max - min;
      entry.ts = millis();

      // Write entry at hdr.idx
      const int entry_pos = base + sizeof(hdr) + hdr.idx * sizeof(entry);
      persistentStore.write_data(entry_pos, (const uint8_t*)&entry, sizeof(entry));

      // Update header
      hdr.idx = (hdr.idx + 1) % PROBE_HIST_N;
      if (hdr.count < PROBE_HIST_N) hdr.count++;
      pos = base;
      persistentStore.write_data(pos, (const uint8_t*)&hdr, sizeof(hdr));

      persistentStore.access_finish();
      SERIAL_ECHOLNPGM("M48: saved repeatability result to EEPROM history.");
    }
#endif


#if ENABLED(EEPROM_SETTINGS)
  // Dump history if requested with H (when combined with probing options)
  if (parser.seen('H')) dump_history();
#endif
    #if HAS_STATUS_MESSAGE
      // Display M48 results in the status bar
      if (MAX_MESSAGE_SIZE <= 20) {
        // 12345678901234567890
        // Deviation: 0.123456
        ui.set_status_and_level(TS(GET_TEXT_F(MSG_M48_DEVIATION), F(": "), w_float_t(sigma, 2, 6)));
      } else if (MAX_MESSAGE_SIZE <= 30) {
        // 123456789012345678901234567890
        // Dev:0.12345, Max delta:0.12345
        ui.set_status_and_level(TS(GET_TEXT_F(MSG_M48_DEV), ':', w_float_t(sigma, 2, 5), F(", "), GET_TEXT(MSG_M48_MAX_DELTA), ':', w_float_t(_MAX(mean - min, max - mean), 2, 5)));
      } else {
        // 1234567890123456789012345678901234567890
        // Deviation: 1.23456, Max delta: 1.23456
        ui.set_status_and_level(TS(GET_TEXT_F(MSG_M48_DEVIATION), F(": "), w_float_t(sigma, 2, 6), F(", "), GET_TEXT(MSG_M48_MAX_DELTA), F(": "), w_float_t(_MAX(mean - min, max - mean), 2, 6)));
      }
    #endif
  }

  restore_feedrate_and_scaling();

  // Re-enable bed level correction if it had been on
  TERN_(HAS_LEVELING, set_bed_leveling_enabled(was_enabled));

  // Re-enable probe temperature correction
  TERN_(HAS_PTC, ptc.set_enabled(true));

  report_current_position();
}

#endif // Z_MIN_PROBE_REPEATABILITY_TEST
