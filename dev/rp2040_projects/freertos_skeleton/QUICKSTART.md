# SB1 / Waveshare RP2040 Zero — quick start

## Enter setup mode (long-hold on Select)

1. **Hold** the **Select** button (GPIO2, panel **BL** / bottom-left LED region) for **3 seconds** without releasing (**1 s per step**).
2. During the hold, **BL stays off**; corner progress lights **TL → TL+TR → TL+TR+BR** at **1 s**, **2 s**, and **3 s** (see `config.h`: `SETUP_HOLD_*`).
3. At **3 s**, all three corners light briefly (**50 ms**), then all four discrete LEDs (**BL, TL, TR, BR**) flash **1 s**.
4. Firmware calls **`sb1_enter_setup_mode()`** (stub on RP2040; prints to UART — use **ESP32-C3** for real Wi‑Fi / SoftAP provisioning).

**Cancel:** Release **before** 3 s — progress LEDs clear; short press (`< 600 ms`) or long press (`≥ 600 ms`, menu) behaves as before.

**After success:** The next **release** is ignored (no extra menu event) so the long hold does not also count as a menu long press.

## UART

Serial console is enabled on UART (see `CMakeLists.txt` / `stdio`) for `SB1: setup mode entered...` log line.

## Pins

See [`src/config.h`](src/config.h): `LED_BLUE_*_GPIO`, `BTN_SELECT_GPIO`.
