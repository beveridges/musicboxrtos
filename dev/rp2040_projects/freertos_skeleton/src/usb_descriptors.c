/*
 * USB descriptors for CDC + MIDI + MSC (RP2040).
 */
#include "tusb_config.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "pico/unique_id.h"
#include <stdbool.h>
#include <string.h>

#define _PID_MAP(itf, n)  ((CFG_TUD_##itf) << (n))
#define USB_PID            (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | _PID_MAP(MIDI, 3) | _PID_MAP(VENDOR, 4))

#define USBD_VID            (0x2E8A)
#define USBD_PID_MIDI_CDC   (0x000b)

enum {
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_MIDI,
  ITF_NUM_MIDI_STREAMING,
  ITF_NUM_TOTAL_NORMAL = 4
};

#define ITF_NUM_MSC_STANDALONE 0u

#define EPNUM_CDC_NOTIF  0x81
#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82
#define EPNUM_MIDI_OUT   0x03
#define EPNUM_MIDI_IN    0x83
#define EPNUM_MSC_OUT    0x04
#define EPNUM_MSC_IN     0x84

#define CONFIG_TOTAL_LEN_NORMAL (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)
#define CONFIG_TOTAL_LEN_MSC    (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static volatile uint8_t s_usb_cfg_profile; /* 0 = CDC+MIDI, 1 = MSC-only */

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USBD_VID,
    .idProduct          = USBD_PID_MIDI_CDC,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *)&desc_device;
}

static uint8_t const desc_fs_configuration_normal[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_NORMAL, 0, CONFIG_TOTAL_LEN_NORMAL, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

static uint8_t const desc_fs_configuration_msc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN_MSC, 0x00, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC_STANDALONE, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

void usb_descriptors_set_profile_normal(void) {
  s_usb_cfg_profile = 0u;
}

void usb_descriptors_set_profile_msc_only(void) {
  s_usb_cfg_profile = 1u;
}

bool usb_descriptors_profile_is_msc_only(void) {
  return s_usb_cfg_profile != 0u;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return s_usb_cfg_profile ? desc_fs_configuration_msc : desc_fs_configuration_normal;
}

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
};

static char usbd_serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  size_t chr_count;

  switch (index) {
    case STRID_LANGID:
      _desc_str[1] = 0x0409;
      chr_count = 1;
      break;
    case STRID_SERIAL:
      if (!usbd_serial[0])
        pico_get_unique_board_id_string(usbd_serial, sizeof(usbd_serial));
      chr_count = strlen(usbd_serial);
      if (chr_count > 31) chr_count = 31;
      for (size_t i = 0; i < chr_count; i++)
        _desc_str[1 + i] = (uint16_t)usbd_serial[i];
      break;
    case STRID_MANUFACTURER: {
      const char *str = "Raspberry Pi";
      chr_count = strlen(str);
      for (size_t i = 0; i < chr_count; i++) _desc_str[1 + i] = (uint16_t)str[i];
      break;
    }
    case STRID_PRODUCT: {
      const char *str = "SB1 MIDI";
      chr_count = strlen(str);
      for (size_t i = 0; i < chr_count; i++) _desc_str[1 + i] = (uint16_t)str[i];
      break;
    }
    case 5: {
      const char *str = "SB1 Storage";
      chr_count = strlen(str);
      for (size_t i = 0; i < chr_count; i++) _desc_str[1 + i] = (uint16_t)str[i];
      break;
    }
    default:
      return NULL;
  }

  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
