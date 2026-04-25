# BLE Testing And MIDI Send

This checklist validates both directions:
- Central -> SB1 (writes to NUS RX and BLE MIDI char)
- SB1 -> Central (NUS TX notify and BLE MIDI notify)

## 1) Verify advertising + services

1. Flash firmware and open monitor:
   - `idf.py -p COMx flash monitor`
2. Confirm logs contain:
   - `registered RX handle=...`
   - `registered TX handle=...`
   - `registered BLE MIDI handle=...`
   - `advertising as SB1 MIDI INTERFACE` (or configured device name)
3. On a BLE scanner (nRF Connect), verify services:
   - NUS: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
   - BLE MIDI: `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`

## 2) Central -> SB1 test (send data)

Use either:
- nRF Connect (manual writes), or
- `ble_midi_test_client.py` (automated)

Manual test packets:
- NUS RX write payload: `4D 42 01`
- BLE MIDI Note On C4: `80 80 90 3C 64`
- BLE MIDI Note Off C4: `80 80 80 3C 00`

Expected monitor logs:
- `NUS RX <n> bytes`
- `BLE MIDI RX <n> bytes`

## 3) SB1 -> Central test (receive notifications)

From central, subscribe to:
- NUS TX: `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- BLE MIDI char: `7772E5DB-3868-4112-A1A9-F2669D106BE3`

Expected:
- NUS TX returns `OK` after NUS RX write.
- BLE MIDI notifications appear when UART-side MIDI bytes are received by ESP32-C3.

## 4) Reliability pass

Run repeated cycles:
- reconnect
- write NUS + BLE MIDI
- receive notifications
- disconnect

Also confirm:
- MTU event logs (`ATT MTU=...`)
- automatic disconnect around the configured session timeout (`SB1_BLE_CONN_LIMIT_S`, 60s by default)

## Automated client usage

File: `ble_midi_test_client.py`

1. Install dependency:
   - `pip install bleak`
2. Run single cycle:
   - `python ble_midi_test_client.py`
3. Run reliability loop (example 10 cycles):
   - `python ble_midi_test_client.py --cycles 10`
4. Optional explicit address:
   - `python ble_midi_test_client.py --address XX:XX:XX:XX:XX:XX`

The script will:
- verify required services/chars
- subscribe to NUS TX and BLE MIDI notifications
- send NUS payload + BLE MIDI Note On/Off
- print notify counts for each cycle
