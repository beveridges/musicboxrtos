# Nano 33 IoT - Wi-Fi / BLE scan + HW-61 LCD

Nano 33 IoT sketch with:
- Pot + button UI
- Wi-Fi and BLE scan
- BLE central connection to ESP32-C3 (`dev/esp32c3_musicbox_nimble`)
- Optional 16x2 LCD with HW-61 (PCF8574) I2C backpack

## Wiring (Nano 33 IoT)

- Pot (10k linear): wiper -> `A0`, ends -> `3.3V` and `GND`
- Button: one side -> `D2`, other side -> `GND` (`INPUT_PULLUP`)
- LCD backpack: `SDA -> A4`, `SCL -> A5`, `VCC -> 3.3V`, `GND -> GND`

## Level shifter + LCD (step-by-step)

Use this when you need to run the LCD backpack at 5V. If your LCD works at 3.3V,
you can skip the shifter and use the direct wiring above.

1) Power off USB and external supplies before wiring.
2) Choose one power mode:
   - Direct mode (preferred here): LCD `VCC -> 3.3V` (no level shifter).
   - Shifted mode: LCD `VCC -> 5V` and use a bidirectional I2C level shifter.
3) Make ground common first:
   - Nano `GND` -> shifter `GND`
   - Nano `GND` -> LCD `GND`
4) If using a level shifter, wire shifter rails:
   - `LV -> 3.3V` (from Nano)
   - `HV -> 5V`
5) Wire I2C low-voltage side (Nano side):
   - Nano `A4 (SDA)` -> shifter `LV SDA` (or channel A-LV)
   - Nano `A5 (SCL)` -> shifter `LV SCL` (or channel B-LV)
6) Wire I2C high-voltage side (LCD side):
   - shifter `HV SDA` (or A-HV) -> LCD `SDA`
   - shifter `HV SCL` (or B-HV) -> LCD `SCL`
7) If not using a shifter, wire directly:
   - Nano `A4 -> LCD SDA`
   - Nano `A5 -> LCD SCL`
8) Upload `i2c_scanner/i2c_scanner.ino` and open Serial Monitor.
9) Confirm LCD address appears (`0x27` typical, sometimes `0x3F`).
10) Upload `lcd_diag_0x27/lcd_diag_0x27.ino`.
11) Turn the backpack contrast trimmer until text is visible.
12) If scanner reports `0x3F`, change LCD constructor address from `0x27` to `0x3F`.

Quick checks:
- No device found: confirm GND is shared and SDA/SCL are not swapped.
- Device found but blank: adjust contrast and verify backlight jumper/pin.
- Flaky behavior: shorten wires and avoid noisy USB power.

## HW-61 LCD notes

- Backpack type: PCF8574-style (HW-61)
- Typical address: `0x27` (sometimes `0x3F`)
- In this project, LCD is enabled and set to `0x27` in:
  - `nano33_iot_wifi_ble_scanner.ino`

If I2C scan shows:
- `0x27` -> LCD backpack
- `0x60` and `0x6A` -> Nano 33 IoT onboard devices (expected)

## Quick diagnostics

- I2C scan sketch: `i2c_scanner/i2c_scanner.ino`
- LCD-only sketch: `lcd_diag_0x27/lcd_diag_0x27.ino`

Use the LCD diagnostic sketch to verify text appears before running the full app.

## Required libraries

- `WiFiNINA`
- `ArduinoBLE`
- `Arduino_SpiNINA` (required by ArduinoBLE transport on Nano 33 IoT)
- `LiquidCrystal_I2C` (for LCD path)

## NINA firmware

BLE needs NINA firmware `>= 3.0.0`.
Check with `CheckFirmwareVersion` example and update using:
- IDE 2.x: `Tools -> Firmware Updater`

## BLE identifiers

- Peripheral advertises as: `SB1 MIDI INTERFACE` (Nano central GAP name: `SB1ControllerUI`)
- NUS service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX/TX chars: `...0002...` / `...0003...`
