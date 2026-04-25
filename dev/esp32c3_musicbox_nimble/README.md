# ESP32-C3 Super Mini — NimBLE GATT (Nordic UART) + optional UART to RP2040

ESP-IDF firmware for **ESP32-C3** that advertises as **`SB1 MIDI INTERFACE`**, exposes the **Nordic UART Service** UUIDs used by `arduino/nano33_iot_wifi_ble_scanner`, and optionally bridges **NUS RX** to **UART1** (toward an RP2040) and **UART RX** back to **NUS TX** notifications.

## IDs (match Nano sketch)

| Item | UUID |
|------|------|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (write from central) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (notify to central) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

## Device name (NVS)

The BLE advertised name comes from **`device_name`** in NVS (`config_store.c`), default **`SB1 MIDI INTERFACE`** (`DEFAULT_NAME`), used by advertising in `main.c`. If a phone or the Nano still shows an old label (e.g. “MusicBox”), the ESP32 may still have a previous name in NVS.

**Fix:** use the SB1 HTTP config UI/API to set `device_name` to the desired string (must match the Nano sketch’s `SB1_BLE_FILTER_NAME` if you rely on name filtering), or **erase NVS** (full chip erase / `nvs_flash_erase` / IDF menuconfig erase) and reflash so defaults apply.

## Build (ESP-IDF 5.x)

```bash
cd dev/esp32c3_musicbox_nimble
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

**No display on the C3?** See [FLASH_AND_VERIFY.md](FLASH_AND_VERIFY.md) for why you must flash, serial log checks, and phone BLE verification (nRF Connect / NUS).

## UART bridge (optional)

Default: **GPIO8 = TX**, **GPIO9 = RX** (ESP32-C3), 115200 8N1. Set `CONFIG_MUSICBOX_UART_BRIDGE=n` in `sdkconfig` to disable UART and only run BLE + console log.

When the Nano writes to **NUS RX**, bytes are copied to UART TX. Bytes received on UART RX are sent as **NUS TX** notifications (max ~20 bytes per chunk; increase via MTU if needed).

## Power

Use a stable **3.3 V** supply; tie **GND** with the Nano and RP2040 when bench testing.
