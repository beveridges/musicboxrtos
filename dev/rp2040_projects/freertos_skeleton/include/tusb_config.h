/*
 * TinyUSB config for freertos_skeleton: CDC + MIDI (RP2040).
 * Used instead of pico_stdio_usb so we get a USB MIDI interface.
 * CFG_TUSB_MCU and CFG_TUSB_RHPORT0_MODE are set by Pico SDK build.
 */
#ifndef FREERTOS_SKELETON_TUSB_CONFIG_H
#define FREERTOS_SKELETON_TUSB_CONFIG_H

/* Force device mode so tusb.h includes usbd + MIDI (SDK may set LIB_TINYUSB_DEVICE). */
#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE   (0x0001)  /* OPT_MODE_DEVICE */
#endif

#define CFG_TUD_CDC             (1)
#define CFG_TUD_MIDI            (1)

#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE  (64)
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE  (64)
#endif
#ifndef CFG_TUD_CDC_EP_BUFSIZE
#define CFG_TUD_CDC_EP_BUFSIZE  (64)
#endif

#ifndef CFG_TUD_MIDI_RX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE (64)
#endif
#ifndef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_TX_BUFSIZE (64)
#endif

#endif
