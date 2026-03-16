# Pinclaw PCB V1.0 Hardware Notes

## Button Pin Mapping (IMPORTANT — pins are swapped vs silkscreen!)

The physical buttons on the PCB are **swapped** relative to the schematic labels:

| Physical Button | Actual GPIO | Firmware Function |
|----------------|-------------|-------------------|
| Left button (closer to USB) | D5 | PTT (Push-to-Talk, recording) |
| Right button (further from USB) | D4 | Action (Play / SD sync) |

**The schematic says D4=PTT, D5=Action, but the actual wiring is reversed.**

This means in firmware:
- `PIN_PTT` should be `5` (not `4`)
- `PIN_ACTION` should be `4` (not `5`)

## Known Issues

- Both recording (PDM mic) and playback (I2S speaker) exhibit audible electrical noise / buzzing
- Possible causes: PDM clock interference, shared power rail noise, missing decoupling caps
