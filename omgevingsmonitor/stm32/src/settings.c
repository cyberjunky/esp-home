/*
 * settings.c - persistent settings in the STM32L0 data EEPROM.
 * Stored at 0x08081000 (clear of the original firmware's config area at
 * 0x08080000) with a magic and CRC; falls back to defaults if invalid.
 */
#include "settings.h"
#include <string.h>
#include "stm32l0xx_hal.h"

#define SETTINGS_ADDR 0x08081000u
#define SETTINGS_MAGIC 0x4F4D3031u /* "OM01" */

struct settings g_settings;

struct stored {
  uint32_t magic;
  struct settings s;
  uint32_t crc;
};

static const struct settings defaults = {
  .interval_batt_s = 600,  /* 10 min, stretched automatically when the battery is low */
  .interval_usb_s = 60,
  .sound_cal_10 = 0,
  .esp_timeout_s = 25,
};

static uint32_t crc32(const void *data, uint32_t len) {
  const uint8_t *p = data;
  uint32_t crc = 0xFFFFFFFFu;
  while (len--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
  }
  return ~crc;
}

void settings_load(void) {
  struct stored st;
  memcpy(&st, (const void *)SETTINGS_ADDR, sizeof st);
  if (st.magic == SETTINGS_MAGIC && st.crc == crc32(&st.s, sizeof st.s)) {
    g_settings = st.s;
  } else {
    g_settings = defaults;
  }
}

void settings_save(void) {
  struct stored st = {.magic = SETTINGS_MAGIC, .s = g_settings};
  st.crc = crc32(&st.s, sizeof st.s);
  if (memcmp(&st, (const void *)SETTINGS_ADDR, sizeof st) == 0) return;

  _Static_assert(sizeof st % 4 == 0, "word aligned");
  const uint32_t *w = (const uint32_t *)&st;
  HAL_FLASHEx_DATAEEPROM_Unlock();
  for (uint32_t i = 0; i < sizeof st / 4; i++) {
    HAL_FLASHEx_DATAEEPROM_Program(FLASH_TYPEPROGRAMDATA_WORD, SETTINGS_ADDR + i * 4, w[i]);
  }
  HAL_FLASHEx_DATAEEPROM_Lock();
}
