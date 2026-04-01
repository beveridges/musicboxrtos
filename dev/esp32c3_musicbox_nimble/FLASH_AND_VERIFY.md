# ESP32-C3: flash requirement and verify without a display

## Do you need to flash the ESP32-C3?

**Yes.** Firmware in this folder only runs after you build with **ESP-IDF** and **flash** the board. Until then, the C3 runs whatever was already programmed (factory demo, old build, etc.).

```bash
cd dev/esp32c3_musicbox_nimble
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with your port (Windows: **Device Manager**). **ESP32-C3-Zero** usually provides USB serial; if you use an external USB-UART, connect TX/RX/GND correctly. Hold **BOOT** (or **IO9**) during **Reset** if upload fails, per your board’s datasheet.

---

## 1. Serial monitor (boot + BLE advertising)

After `idf.py ... flash monitor`, watch the log:

- **Pass:** Normal boot, NimBLE starts, line like **advertising as SB1 MIDI INTERFACE** (see `DEVICE_NAME` in `main/main.c`).
- **Fail:** Boot loop, panic, or no advertising message — fix power, COM port, drivers, or build before debugging Bluetooth.

---

## 2. Phone BLE (no display, no Nano)

Use **nRF Connect** (Nordic) or **LightBlue** (or similar).

| Step | Pass criterion |
|------|----------------|
| Scan | Device **SB1 MIDI INTERFACE** visible (name may truncate on some phones). |
| Connect | Open Nordic UART Service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`. |
| Subscribe | Enable notify on **TX** `6E400003-...`. |
| Write | Write bytes to **RX** `6E400002-...` — firmware may notify back (e.g. small ack) when connected. |

This confirms GATT end-to-end over Bluetooth only.

---

## 3. Optional: Nano 33 IoT

Flash `arduino/nano33_iot_wifi_ble_scanner/nano33_iot_wifi_ble_scanner.ino`, open **Serial Monitor**, run **BLE scan** — peripheral should match **SB1** filters; connect and watch serial for NUS activity.

---

## Troubleshooting

| Symptom | Things to check |
|---------|------------------|
| No serial | Wrong COM port, USB driver, cable (data-capable), BOOT/reset for flash mode. |
| No device in scan | Firmware not flashed, wrong board, try another scanner app, Bluetooth on. |
| Connect fails | Retry; confirm NUS UUIDs in README; distance / RF. |

---

## Nordic UART UUIDs (reference)

| Role | UUID |
|------|------|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (central writes here) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (notify to central) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
