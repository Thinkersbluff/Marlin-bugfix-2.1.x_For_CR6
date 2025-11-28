M905 test instructions

Purpose:
- Minimal G-code sequence to exercise the M905 calibration command and observe behavior.

Files:
- `test/test_m905.gcode` — G-code sequence with a few M905 invocations (basic, tuned, persist) and commands to report settings.

How to run (recommended safety steps):
1. Inspect the file `test/test_m905.gcode` and edit the `Z` start height if your machine needs a different safe starting point.
2. Ensure the printer bed and carriage have room to move. Disable any object/fixture blocking travel.
3. Home the printer using `G28` (script also homes at start).
4. Run the file one of these ways:
   - Copy to an SD card and select it from the printer's menu.
   - Use your host program (Pronterface/OctoPrint/Printrun) to "Send file".
   - Send the file line-by-line from the host console.

Notes and tips:
- The script includes `M0` pauses for human inspection between runs. If running fully automated, remove the `M0` lines.
- After `M905 ... P1` (persist), use `M503` and `M501` to confirm values were saved and loaded.
- If `PROBE_TARE_ONLY_WHILE_INACTIVE` is enabled and the probe is active when the run starts, `M905` will attempt an upward search up to 20 mm to find a clear Z.

Safety:
- Never run this script with the nozzle or bed hot in close proximity to printed parts or tooling that may obstruct motion.
- Stop the test immediately if unexpected behavior occurs.
