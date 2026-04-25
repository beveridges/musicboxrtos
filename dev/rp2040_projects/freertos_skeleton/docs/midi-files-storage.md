# MIDI file storage (H.1 user story)

## Goal

Let users load Standard MIDI files (`.mid`) onto the device and see them in **UTILITY → MIDI FILES** in normal operation.

## User story

1. **Storage mode**  
   The user puts the RP2040 into a dedicated **USB storage** mode (exact gesture/menu TBD in firmware). While in this mode, the device presents a **USB Mass Storage** volume to the host PC.

2. **Volume label**  
   The removable drive appears with the label **`SB1STORAGE`** (Windows/macOS/Linux show this as the volume name).

3. **Loading files**  
   The user **drags or copies** `.mid` files onto that drive (same workflow as a USB flash drive).

4. **Normal mode**  
   After returning to **normal** operation, the same files are stored on the device’s internal filesystem (or dedicated partition). The **MIDI FILES** menu lists available files (implementation TBD: scrollable list, open/play).

5. **Playback over USB**  
   **Yes.** In normal mode, playback can send MIDI to the **USB host** using the existing USB **Device MIDI** stack (`tud_midi_stream_write` in `midi_task.c`). A Standard MIDI File parser would read events from the selected file and translate them into USB-MIDI packets (notes, CCs, tempo, etc.), which the PC sees as a normal MIDI input device.

   Limitations to plan for:

   - **Simultaneous MSC + MIDI**: USB composite devices can expose MSC and MIDI together, but **writing files while also streaming MIDI** requires careful scheduling and buffer management. Typical pattern: **either** storage mode **or** full MIDI/playback mode, or a composite with both interfaces and clear rules about when the host may cache the volume.
   - **Parsing**: SMF Type 0/1 support, running status, tempo map — scope the first milestone (e.g. Type 0 only, single track).

## Firmware work (roadmap)

| Phase | Item |
| --- | --- |
| A | Add **FatFs** (or similar) on a dedicated flash region or external flash; expose **TinyUSB MSC** device with volume label `SB1STORAGE`. |
| B | **Mode switch**: normal ↔ storage (disconnect/re-enumerate USB or composite descriptor). |
| C | **MIDI FILES** UI: list directory, optional play/stop. |
| D | **SMF player** task feeding `tud_midi_stream_write`. |

**Storage medium (pick one for phase A):** onboard QSPI + littlefs/FatFs; **SD over SPI**; or **USB MSC gadget** only (host PC holds files until copied to device flash). Hybrid: small internal catalog + optional SD.

**Safety:** flash wear limits, max file size, SMF parse bounds (streaming vs load-all).

## Related files

- USB stack: [`src/usb_descriptors.c`](../src/usb_descriptors.c), [`include/tusb_config.h`](../include/tusb_config.h) — today **CDC + MIDI** only; MSC is **not** enabled yet.
- MIDI out: [`src/midi_task.c`](../src/midi_task.c).
