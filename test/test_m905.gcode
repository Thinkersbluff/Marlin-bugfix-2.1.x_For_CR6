; test/test_m905.gcode
; Minimal M905 exercise sequence for manual/human-in-the-loop testing
; Safety preconditions: printer powered, axes homing clear, hotend and bed temps low (no motion interference).

; 1) Home axes
G28
M119 ; report endstops

; 2) Basic calibration run using default margin/settle
; Adjust Z if your machine needs a different starting height
M905 Z15

; Pause to inspect logs and probe state
M0 ; Press button on the controller to continue (or skip if running from host)

; 3) Calibration run with explicit margin/settle (no persist)
M905 Z15 M1.2 S60
M0

; 4) Calibration run and persist tuned margin/settle to EEPROM
M905 Z15 M1.2 S60 P1
M0

; 5) Show saved settings (M503 prints settings). On some firmwares use M501 to reload
M503
M501

; Optional: Run a bed probe routine to verify behavior (uncomment if desired)
; G28
; G29

; End of test
M117 M905 test complete
