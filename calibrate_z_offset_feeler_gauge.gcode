; Z Offset Calibration using 0.152mm Feeler Gauge
; This procedure helps you calibrate M851 Z offset precisely
; 
; YOU WILL NEED:
; - 0.152mm (0.006") feeler gauge
; - Access to LCD menu for baby stepping
; OR terminal access to send M290 commands
;
; PROCEDURE:
; 1. Run this gcode file
; 2. Insert feeler gauge under nozzle when prompted
; 3. Use baby stepping to adjust nozzle down until gauge has light resistance
; 4. Record the Z position shown
; 5. Calculate and apply new offset

M117 Z Offset Calibration
M118 Starting Z Offset Calibration with 0.152mm gauge

; Disable bed leveling for accurate measurement
M420 S0
M118 Bed leveling disabled

; Home all axes
M117 Homing...
M118 Homing all axes
G28
M118 Homing complete

; Move to bed center
M117 Moving to center
M118 Moving to bed center (X117 Y117)
G1 X117 Y117 F3000

; Raise to safe height for gauge insertion
M117 Raising for gauge
M118 Raising to Z=5mm for gauge insertion
G1 Z5 F300

; PAUSE - Insert gauge
M117 INSERT GAUGE NOW
M118 ========================================
M118 ACTION REQUIRED:
M118 1. Insert 0.152mm feeler gauge under nozzle
M118 2. Use the SOLID part (avoid the mounting hole)
M118 3. Press CONTINUE when gauge is in place
M118 ========================================
M0 Insert 0.152mm feeler gauge, press CONTINUE

; Lower to near-contact height
M117 Lowering to gauge
M118 Lowering nozzle to Z=0.200mm
G1 Z0.200 F60

M117 Ready for adjustment
M118 ========================================
M118 BABY STEPPING INSTRUCTIONS (USE M290 ONLY):
M118 ========================================
M118 Send M290 commands via TERMINAL to adjust Z temporarily:
M118 - M290 Z-0.01  (move DOWN 0.01mm)
M118 - M290 Z0.01   (move UP 0.01mm)
M118 - M290 Z-0.10  (move DOWN 0.10mm - faster)
M118 - M290 Z0.10   (move UP 0.10mm - faster)
M118 
M118 WARNING: DO NOT use touchscreen baby stepping!
M118 Touchscreen modifies M851 Z and auto-saves to EEPROM.
M118 M290 is temporary and does not modify M851 Z.
M118 
M118 GOAL: Adjust Z down until gauge has LIGHT RESISTANCE
M118 - Test by sliding gauge back and forth
M118 - Too loose = nozzle too high, send M290 Z-0.01
M118 - Can't move = nozzle too low, send M290 Z0.01
M118 - Just right = slight drag like sticky note
M118 
M118 When perfect, press CONTINUE
M118 ========================================
M0 Adjust Z with M290 commands, press CONTINUE when perfect

; Report current position
M117 Recording position
M118 ========================================
M118 MEASUREMENT COMPLETE
M118 ========================================
M114 ; Report current position

; Calculate offset (done manually by user with displayed Z value)
M118 
M118 CALCULATION STEPS:
M118 1. Note Z value from M114 above (e.g., Z:0.168)
M118 2. Check baby stepping offset: M290 (displays accumulated offset)
M118 3. Calculate: Z_measured - 0.152 + baby_step_offset
M118 
M118 EXAMPLE:
M118 - M114 shows Z:0.200
M118 - You sent M290 Z-0.05 total to get perfect drag
M118 - Actual Z = 0.200 - 0.050 = 0.150
M118 - True bed = 0.150 - 0.152 = -0.002 (2 microns low)
M118 - Current M851 Z = -1.870 (check with M851)
M118 - New offset = -1.870 - (-0.002) = -1.868
M118 
M118 FORMULA: 
M118   actual_Z = M114_Z + M290_total
M118   bed_error = actual_Z - 0.152
M118   new_M851_Z = current_M851_Z - bed_error
M118 
M118 4. Apply: M851 Z[new_value]
M118 5. Save: M500
M118 6. Re-home: G28 Z
M118 7. Verify: M851
M118 ========================================

; Raise nozzle for gauge removal
M117 Raising for removal
M118 Raising to Z=10mm for gauge removal
G1 Z10 F300

M117 Remove gauge
M118 ========================================
M118 REMOVE FEELER GAUGE NOW
M118 Press CONTINUE when removed
M118 ========================================
M0 Remove feeler gauge, press CONTINUE

; Return to safe position
M117 Calibration complete
M118 ========================================
M118 Z OFFSET CALIBRATION COMPLETE
M118 ========================================
M118 Next steps:
M118 1. Calculate your new offset using formula above
M118 2. Apply with: M851 Z[your_new_value]
M118 3. Save with: M500
M118 4. Re-home: G28
M118 5. Verify: Run G1 Z0 and test with paper/gauge again
M118 ========================================

G1 Z50 F600
G1 X0 Y200 F3000
M117 Done! Check terminal
M118 Calibration procedure finished
