/*
 * TinyUSB MSC: FAT12 RAM disk (16 x 512 B), volume label SB1STORAGE.
 * Medium hidden until TL long-hold; host eject clears lun until next TL.
 */
#include "sb1_msc.h"
#include "tusb_config.h"
#include "tusb.h"
#include "pico/time.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

#if CFG_TUD_MSC

#define MSC_TOTAL_SECTORS 16u
#define MSC_BLOCK_SIZE    512u

static uint8_t s_disk[MSC_TOTAL_SECTORS][MSC_BLOCK_SIZE];
static shared_state_t *s_sh;
static volatile bool s_host_ejected_lun;
static volatile bool s_medium_visible_to_host;

static bool sb1_msc_try_lock_shared(shared_state_t *sh) {
  return sh && sh->mutex && xSemaphoreTake(sh->mutex, 0) == pdTRUE;
}

static void sb1_msc_mark_host_detached(void) {
  s_host_ejected_lun = true;
  s_medium_visible_to_host = false;
  if (sb1_msc_try_lock_shared(s_sh)) {
    s_sh->usb_msc_mounting = false;
    s_sh->usb_msc_host_ejected = true;
    s_sh->usb_msc_medium_ready = false;
    s_sh->usb_msc_attached_until_ms = 0u;
    sb1_msc_refresh_file_list(s_sh);
    s_sh->menu_dirty = true;
    xSemaphoreGive(s_sh->mutex);
  }
}

static void write_le16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)(v >> 8);
}

static void write_le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void sb1_msc_format_ramdisk(void) {
  memset(s_disk, 0, sizeof(s_disk));
  uint8_t *b = s_disk[0];
  b[0] = 0xEB;
  b[1] = 0x3C;
  b[2] = 0x90;
  memcpy(b + 3, "MSDOS5.0", 8);
  write_le16(b + 0x0B, MSC_BLOCK_SIZE);
  b[0x0D] = 1u;
  write_le16(b + 0x0E, 1u);
  b[0x10] = 1u;
  write_le16(b + 0x11, 16u);
  write_le16(b + 0x13, MSC_TOTAL_SECTORS);
  b[0x15] = 0xF8u;
  write_le16(b + 0x16, 1u);
  write_le16(b + 0x18, 1u);
  write_le16(b + 0x1A, 1u);
  write_le32(b + 0x1C, 0u);
  b[0x24] = 0x80u;
  b[0x25] = 0u;
  b[0x26] = 0x29u;
  write_le32(b + 0x27, 0x53423130u);
  memcpy(b + 0x2B, "SB1STORAGE ", 11);
  memcpy(b + 0x36, "FAT12   ", 8);
  b[0x1FE] = 0x55u;
  b[0x1FF] = 0xAAu;

  s_disk[1][0] = 0xF8u;
  s_disk[1][1] = 0xFFu;
  s_disk[1][2] = 0xFFu;
  s_disk[1][3] = 0xFFu;
  s_disk[1][4] = 0x0Fu;

  uint8_t *r = s_disk[2];
  memcpy(r, "SB1STORAGE ", 11);
  r[11] = 0x08u;
}

void sb1_msc_init(void) {
  sb1_msc_format_ramdisk();
  s_host_ejected_lun = false;
  s_medium_visible_to_host = false;
}

void sb1_msc_set_shared(shared_state_t *sh) {
  s_sh = sh;
}

void sb1_msc_on_tl_gesture(shared_state_t *sh) {
  if (!sh) {
    return;
  }
  s_host_ejected_lun = false;
  s_medium_visible_to_host = true;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (sb1_msc_try_lock_shared(sh)) {
    sh->usb_msc_mounting = false;
    sh->usb_msc_medium_ready = true;
    sh->usb_msc_host_ejected = false;
    sh->usb_msc_attached_until_ms = now + 2500u;
    sh->menu_dirty = true;
    xSemaphoreGive(sh->mutex);
  }
}

static bool ext_is_mid(const uint8_t *x) {
  return ((x[0] | 32u) == 'm') && ((x[1] | 32u) == 'i') && ((x[2] | 32u) == 'd');
}

static bool entry_is_mid(const uint8_t *e) {
  if (e[0] == 0x00u || e[0] == 0xE5u) {
    return false;
  }
  uint8_t attr = e[11];
  if (attr == 0x08u || attr == 0x0Fu) {
    return false;
  }
  if ((attr & 0x10u) != 0u) {
    return false;
  }
  return ext_is_mid(e + 8);
}

static void entry_to_name(const uint8_t *e, char *out, size_t outsz) {
  char base[9];
  char ext[4];
  memcpy(base, e, 8);
  base[8] = '\0';
  memcpy(ext, e + 8, 3);
  ext[3] = '\0';
  int bi = 7;
  while (bi >= 0 && base[bi] == ' ') {
    base[bi--] = '\0';
  }
  int ei = 2;
  while (ei >= 0 && ext[ei] == ' ') {
    ext[ei--] = '\0';
  }
  snprintf(out, outsz, "%s.%s", base, ext);
}

void sb1_msc_refresh_file_list(shared_state_t *sh) {
  if (!sh) {
    return;
  }
  uint8_t cnt = 0;
  const uint8_t *root = s_disk[2];
  for (unsigned slot = 0; slot < 16u && cnt < SB1_MSC_FILE_LIST_MAX; slot++) {
    const uint8_t *e = root + slot * 32u;
    if (e[0] == 0x00u) {
      break;
    }
    if (!entry_is_mid(e)) {
      continue;
    }
    entry_to_name(e, sh->msc_file_list[cnt], sizeof(sh->msc_file_list[0]));
    cnt++;
  }
  sh->msc_file_list_count = cnt;
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void)lun;
  if (s_host_ejected_lun || !s_medium_visible_to_host) {
    return tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
  }
  return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  (void)lun;
  *block_count = MSC_TOTAL_SECTORS;
  *block_size = (uint16_t)MSC_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition;
  (void)lun;
  if (load_eject && !start) {
    sb1_msc_mark_host_detached();
  }
  return true;
}

void tud_umount_cb(void) {
  if (s_medium_visible_to_host) {
    sb1_msc_mark_host_detached();
  }
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  (void)lun;
  if (lba >= MSC_TOTAL_SECTORS) {
    return -1;
  }
  if (offset + bufsize > MSC_BLOCK_SIZE) {
    return -1;
  }
  memcpy(buffer, s_disk[lba] + offset, bufsize);
  return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
  (void)lun;
  return true;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  (void)lun;
  if (lba >= MSC_TOTAL_SECTORS) {
    return -1;
  }
  if (offset + bufsize > MSC_BLOCK_SIZE) {
    return -1;
  }
  memcpy(s_disk[lba] + offset, buffer, bufsize);
  return (int32_t)bufsize;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
  (void)lun;
  memset(vendor_id, ' ', 8);
  memcpy(vendor_id, "SB1", 3);
  memset(product_id, ' ', 16);
  memcpy(product_id, "SB1STORAGE", 10);
  memset(product_rev, ' ', 4);
  memcpy(product_rev, "1.0", 3);
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
  (void)lun;
  (void)scsi_cmd;
  (void)buffer;
  (void)bufsize;
  (void)tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
  return -1;
}

#else /* !CFG_TUD_MSC */

void sb1_msc_init(void) {}
void sb1_msc_set_shared(shared_state_t *sh) { (void)sh; }
void sb1_msc_on_tl_gesture(shared_state_t *sh) { (void)sh; }
void sb1_msc_refresh_file_list(shared_state_t *sh) { (void)sh; }

#endif
