# SB1 / Waveshare RP2040 Zero — quick start

## Bluetooth pairing — LED gestures (Select / BL)

**Main menu** still uses a **600 ms** long press on Select for navigation (see `MENU_LONG_PRESS_MS` in `config.h`). The gestures below are **separate** choreography for entering and leaving **Bluetooth pairing** on the LCD.

### Enter pairing (not already pairing)

1. **Hold** the **Select** button (GPIO2, panel **BL**) continuously until the pairing screen appears (**about 3.6 s** from first contact: **600 ms** of normal feedback, then **3.0 s** of LED choreography).
2. **First ~600 ms** (`MENU_LONG_PRESS_MS`): **BL follows** the button like a normal press so menu use still feels right; corner LEDs are unchanged.
3. **Next 3.0 s** (see `PAIR_LED_FIRST_MS`, `PAIR_LED_STEP_MS`, `PAIR_ENTRY_ALL_OFF_MS` in `config.h`):
   - **0–1 s** of this phase: all progress LEDs **off** (dark); **BL stays off** even with your finger down.
   - Then **500 ms** per step: **TL** → **TL+TR** → **TL+TR+BR** → **all four** (TL+TR+BR+**BL**).
   - At **3.0 s** of this phase: all LEDs **off** → **Bluetooth pairing** UI appears **immediately** (no need to wait for release).
4. **Cancel:** release before the pairing screen — if you release **before 600 ms**, you get a normal short press; after that, LEDs clear and you get short/long press based on **total** hold time.

**After success:** the **next** Select **release** is ignored so the hold does not also count as a normal menu long press.

### Exit pairing (only while pairing UI is shown)

Exit is **not** the 600 ms menu long press — use the **reverse** LED sequence:

1. **Hold** Select again. **All four** LEDs **on** for **1 s**.
2. Then **500 ms** per step, lights peel **off** in order: **BL** → **BR** → **TR** → **TL** (last step all off).
3. At **2.5 s** from the start of this exit hold, all **off** → return to **main menu** (`PAIR_EXIT_ALL_OFF_MS`).

**Cancel:** release Select before the exit sequence completes — return to pairing UI.

Firmware calls **`sb1_enter_setup_mode()`** on successful entry (stub on RP2040; UART log — **ESP32-C3** does real Wi‑Fi / BLE work).

## UART

Serial console uses UART0 (see `CMakeLists.txt` / `stdio`). The RP2040 **defers** `stdio_init_all()` until it sees the **ESP32 UART TX line** (into RP2040 RX / GPIO1) **idle high** for a short burst, then enables its own TX. A FreeRTOS task polls for up to **30 s** (after a **500 ms** initial wait) so slow ESP32 boot (Wi‑Fi, BLE, etc.) still works. If you never see UART logs, power or reset order may matter: the ESP32 firmware also drives **GPIO8** high **early** in `app_main` (before Wi‑Fi) so the partner line goes high as soon as the ESP is running.

`SB1: setup mode entered...` and other `printf` output only appear after UART stdio comes up (`sb1_setup.c` / `sb1_is_stdio_ready()`).

## Power-Down Backfeed (RP2040 -> ESP32-C3 UART)

If you wire RP2040 UART signals directly into the ESP32-C3 and the ESP32-C3 is fully unpowered, the ESP32-C3 GPIO input protection diodes can backfeed power through the line and cause the ESP32-C3 LEDs to glow.

This RP2040 firmware mitigates the symptom by keeping RP2040 UART TX in **high-Z** until it believes the ESP32 is powered: it samples RX with an **internal pull-down** so a floating line is not mistaken for a live partner, then requires a short run of **high** samples (ESP32 TX actively idle high).

For a robust hardware fix, still add power-off isolation between the boards (preferred: an `Ioff`-capable level shifter/buffer, or a UART isolator). As a quick mitigation, add a series resistor (start around `1k`, then adjust while watching for UART errors).

## Pins

See [`src/config.h`](src/config.h): `LED_BLUE_*_GPIO`, `BTN_SELECT_GPIO`.
