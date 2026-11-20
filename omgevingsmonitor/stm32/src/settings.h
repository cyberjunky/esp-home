/*
 * settings.h - persistent settings in the STM32L0 data EEPROM.
 * Changed from Home Assistant through the ESP (see docs/protocol.md).
 */
#pragma once
#include <stdint.h>

struct settings {
  uint16_t interval_batt_s;   /* base report interval on battery          */
  uint16_t interval_usb_s;    /* report interval on USB power             */
  int16_t sound_cal_10;       /* dB(A) offset x10 (reserved)              */
  uint16_t esp_timeout_s;     /* max awake time for one report            */
};

extern struct settings g_settings;
void settings_load(void);
void settings_save(void);    /* only writes when something changed */
