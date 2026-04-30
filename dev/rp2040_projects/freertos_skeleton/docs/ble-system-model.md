# BLE system model (SB1 full stack)

## Role split

- **ESP32-C3**: BLE peripheral and connectivity coprocessor.
- **RP2040**: real-time UI/MIDI core and transport router.

SB1 BLE policy is **peripheral-only**. The phone/app/host initiates BLE connection. RP2040 does not ask
ESP32 to become a BLE central.

## BL long-press behavior

- Outside connectivity UI, long hold on BL enters connectivity setup gesture/UI.
- Inside connectivity UI root, long hold on BL exits connectivity UI.
- In connectivity sub-pages, long hold behaves as back.

## MIDI routing model

Routing is centralized through `sb1_midi_router`:

- **Local source** (buttons/program actions):
  - USB MIDI out when mounted.
  - BLE egress mirror over UART when BT peer connected.
- **BLE source** (framed UART from ESP32):
  - Updates BLE receive telemetry.
  - Routed by `ble_midi_sink` policy:
    - `USB`
    - `MERGE`
    - `DEVICE` (no USB echo)

Loop-prevention baseline: BLE ingress is not echoed back to BLE path by default.

## Control-plane protocol (UART text)

RP2040 -> ESP32:

- `SB1CMD,BLE,READV`
- `SB1CMD,BLE,DISCONNECT`

ESP32 -> RP2040:

- `SB1BT,0`
- `SB1BT,1,<peer_name>`
- `SB1WF,0`
- `SB1WF,1`
- `SB1BLE,STATE,<BOOTING|ADVERTISING|CONNECTED|RECOVERING|FAULT>`
- `SB1BLE,ADV,<0|1>`
- `SB1BLE,ERR,<int>`
- `SB1BLE,DISC,<int>`
- `SB1BLE,RECOV,<uint>`
- `SB1BLE,PROTO,<uint>`

`SB1BLE,PROTO` publishes control/data link protocol version. Current version: `1`.
