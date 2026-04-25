# Menu reference (RP2040 `freertos_skeleton`)

## Instrument

- **PROGRAM CHG**: edit program number (0–127). Short = apply; long = cancel edit. **LONG=BACK** on bottom row returns to parent without changing navigation semantics beyond edit exit.
- **MIDI CC CTRL**: **CC NUMBER** and **MIDI CHAN** rows; values stored in shared state.
- **TAP TEMPO**: BPM from BR (MIDI A) tap interval; uses high-resolution time for interval.
- **CRAZY ARP**
  - **ARP ENABLED**: UI toggle `arp_enabled` (stored in shared state). **Not yet driven by `midi_task`** — treat as saved preference until the arpeggiator engine is wired.
  - **ARP RATE**: integer **1–16** (`arp_rate`). Higher = faster in the future engine; row **1=SLOW 16=FAST** on the rate edit screen.
- **LIVE MODE**: performance screen with button overlays on `sb1_livemode_buttons` art.

## Hub

- **HUB → MIDI (BLE MIDI IN)**  
  Routes inbound BLE MIDI from the ESP32 link into one of:
  - **USB**: forward toward USB host MIDI.
  - **MRG** (merge): combine with local/USB as implemented in link/MIDI tasks.
  - **DEV**: on-device only (no USB echo path), per `ble_midi_sink` in [`src/config.h`](../src/config.h).

  Requires the ESP32 UART/binary link to be up for BLE-derived traffic.

- **HUB → OSC**  
  **Not implemented.** Screen shows **NOT IMPLEMENTED** / **PLANNED BRIDGE**. Future: OSC over Wi‑Fi/Ethernet from ESP32 or similar — `osc_enabled` in `config.h` is reserved.

## Utility

- **MIDI FILES**: roadmap — USB MSC volume **`SB1STORAGE`**, file list, playback. See [`midi-files-storage.md`](midi-files-storage.md).
- **DIAGNOSTICS**: placeholder.

## System

- **AUTO OFF**, **FIRMWARE**, **ABOUT** as in firmware.
