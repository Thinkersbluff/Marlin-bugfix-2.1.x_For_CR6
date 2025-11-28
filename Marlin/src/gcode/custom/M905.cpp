/*
 * M905 - Calibrate and persist probe enable-off height
 *
 * Usage: M905 Z<start_z>
 *   Z - optional start height (mm) to begin the slow descent. Defaults to
 *       PROBE_EN_OFF_HEIGHT_DEFAULT.
 *
 * Flow:
 *  - Require homed axes (safe operation).
 *  - Move to the requested start height above the bed.
 *  - Descend in small steps until the probe triggers (LOW/active).
 *  - On trigger, step back up to previous height and verify the probe clears.
 *  - If the probe clears, accept measured_z + PROBE_EN_OFF_MARGIN as the
 *    calibrated `probe_en_off_height` and persist via settings.save().
 */

#include "../../inc/MarlinConfig.h"
#include "../gcode.h"
#include "../../module/probe.h"
#include "../../module/motion.h"
#include "../../module/endstops.h"
#include "../../module/planner.h"
#include "../../module/settings.h"
#include "../../MarlinCore.h"

void GcodeSuite::M905() {
  // Check feature availability
  #if !HAS_BED_PROBE
    SERIAL_ECHO_START(); SERIAL_ECHOLN("M905: bed probe support not compiled in");
    return;
  #endif

  // Parameter Z = start height (mm)
  const float start_z = parser.seenval('Z') ? parser.value_float() : float(PROBE_EN_OFF_HEIGHT_DEFAULT);
  // Optional tuning parameters: M = margin (mm), S = settle ms, P = persist (pass P1 to persist)
  const float margin = parser.seenval('M') ? parser.value_float() : float(PROBE_EN_OFF_MARGIN);
  const uint16_t settle_ms = parser.seenval('S') ? uint16_t(parser.value_int()) : uint16_t(M905_STEP_SETTLE_MS);
  const bool persist = parser.seenval('P') ? (parser.value_int() != 0) : false;

  // Require homed axes for safe probing movement
  if (homing_needed()) {
    SERIAL_ECHO_START(); SERIAL_ECHOLN("M905: Please home axes before running M905");
    return;
  }

  // Save current XY and Z so we can restore or report
  const float cur_x = current_position[X_AXIS];
  const float cur_y = current_position[Y_AXIS];
  const float cur_z = current_position[Z_AXIS];

  // Move to start height at current XY
  planner.synchronize();
  do_blocking_move_to_xy_z(xy_pos_t{ cur_x, cur_y }, start_z, homing_feedrate(Z_AXIS));
  planner.synchronize();

  // Guarded recovery: if probe must be inactive for tare and is active now,
  // try raising to a safe clearance (stored enable-off + margin or start_z)
  #if ENABLED(PROBE_TARE_ONLY_WHILE_INACTIVE)
    if ((endstops.probe_switch_activated())) {
      // Try an iterative upward search to find a Z where the probe becomes inactive.
      // Start at the better of stored enable-off+margin and start_z, and step up in
      // 1.0 mm increments until the probe clears or we've raised 20 mm above start_z.
      const float penh = MarlinSettings::get_probe_en_off_height();
      const float preferred = (penh > 0.0f) ? (penh + margin) : start_z;
      const float begin_raise = (preferred > start_z) ? preferred : start_z;
      const float max_raise = start_z + 20.0f; // limit to 20 mm above the start Z
      float try_z = begin_raise;

      SERIAL_ECHOLNPGM("M905: probe active, searching upward from ", try_z);

      bool cleared = false;
      planner.synchronize();
      while (try_z <= max_raise) {
        do_blocking_move_to_xy_z(xy_pos_t{ cur_x, cur_y }, try_z, homing_feedrate(Z_AXIS));
        safe_delay(settle_ms);
        if (!(endstops.probe_switch_activated())) { cleared = true; break; }
        // Step up by 1.0 mm for the next attempt
        try_z += 1.0f;
      }

      if (!cleared) {
        SERIAL_ECHOLNPGM("M905: probe did not clear within 20mm search - aborting");
        // Restore original Z and exit
        planner.synchronize();
        do_blocking_move_to(xyze_pos_t{ cur_x, cur_y, cur_z, current_position[E_AXIS] });
        planner.synchronize();
        return;
      }
      SERIAL_ECHOLNPGM("M905: probe cleared at Z=", try_z);
    }
  #endif

  // Descend in steps until probe triggers
  const float step = 0.5f; // mm per decrement
  float prev_z = start_z;
  bool found = false;
  float detected_z = start_z;

  for (float z = start_z; z >= Z_PROBE_LOW_POINT; z -= step) {
    do_blocking_move_to_xy_z(xy_pos_t{ cur_x, cur_y }, z, homing_feedrate(Z_AXIS));
    safe_delay(settle_ms);
    if ((endstops.probe_switch_activated())) {
      // Probe went active at this z
      detected_z = z;
      // Step back up to previous z to verify it clears
      do_blocking_move_to_xy_z(xy_pos_t{ cur_x, cur_y }, prev_z, homing_feedrate(Z_AXIS));
      safe_delay(settle_ms);
      if (!(endstops.probe_switch_activated())) {
        found = true;
      }
      else {
        // Still triggered at previous height -> unreliable, abort
        SERIAL_ECHOLNPGM("M905: probe still active at ", prev_z);
        break;
      }
      break;
    }
    prev_z = z;
  }

  if (!found) {
    SERIAL_ECHO_START(); SERIAL_ECHOLN("M905: Failed to detect a clean probe transition - aborting");
    // Restore original Z
    planner.synchronize();
    do_blocking_move_to(xyze_pos_t{ cur_x, cur_y, cur_z, current_position[E_AXIS] });
    planner.synchronize();
    return;
  }

  // Accept detected_z + margin as calibrated enable-off height
  const float calibrated = detected_z + margin;
  MarlinSettings::set_probe_en_off_height(calibrated);
  if (persist) {
    MarlinSettings::set_probe_en_off_margin(margin);
    MarlinSettings::set_m905_step_settle_ms(settle_ms);
  }
  const bool ok = settings.save();

  if (ok) {
    SERIAL_ECHOLNPGM("M905: Calibrated probe_en_off_height = ", calibrated, " mm (saved to EEPROM)");
  }
  else {
    SERIAL_ECHOLNPGM("M905: Calibration measured = ", calibrated, " mm (failed to save to EEPROM)");
  }

  // Restore original Z position (best-effort)
  planner.synchronize();
  do_blocking_move_to(xyze_pos_t{ cur_x, cur_y, cur_z, current_position[E_AXIS] });
  planner.synchronize();
}
