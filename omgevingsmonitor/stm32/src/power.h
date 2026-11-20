/*
 * power.h - battery / solar / USB / charger measurement.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum charge_state { CHG_OFF = 0, CHG_CHARGING = 1, CHG_FULL = 2 };

struct power_status {
  uint16_t vdda_mv;
  uint16_t vbat_mv;
  uint16_t vsolar_mv;
  uint8_t soc;            /* 0..100 %, rest-voltage estimate (Li-ion) */
  bool usb;
  uint8_t charge;         /* enum charge_state */
};

void power_measure(struct power_status *p);
