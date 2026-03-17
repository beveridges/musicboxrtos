# RP2040 Zero USB MIDI — Milestones

## Milestone 1 (current) ✓

- One button on GPIO12
- One MIDI note on/off over USB
- One NeoPixel status LED
- ISR wakes MIDI task (task notification)
- UI task shows idle/pressed state via LED
- No display yet
- Compiles cleanly for RP2040 Zero (Arduino-Pico, FreeRTOS SMP)

**Deliver this first** to get a clean RTOS core before adding more.

---

## Milestone 2 (next)

- Add Nokia 5110 display task (Adafruit_PCD8544)
- Show USB status (mounted / not mounted) and last note event
- Keep display task low priority
- Verify button-to-MIDI latency remains unaffected (display must not block MIDI path)

---

## Milestone 3 (after)

- Support 2–4 buttons
- Map each button to a different MIDI note
- Queue all events (e.g. MidiEvent struct with pressed, note, velocity, timestamp_us)
- Add simple event timestamps for latency observation
