/*
 * sens_aht20.c - Aosong AHT20 temperature / humidity (optional, 0x38).
 */
#include "i2c_bus.h"
#include "lowpower.h"
#include "sensors.h"

#define AHT20_ADDR 0x38

static uint8_t aht_crc(const uint8_t *d, uint8_t n) { return crc8_sensirion(d, n); }

bool aht20_probe(void) {
  if (!i2c_probe(BUS_ONBOARD, AHT20_ADDR)) return false;
  uint8_t st;
  uint8_t cmd = 0x71;
  if (!i2c_write(BUS_ONBOARD, AHT20_ADDR, &cmd, 1) || !i2c_read(BUS_ONBOARD, AHT20_ADDR, &st, 1))
    return false;
  if (!(st & 0x08)) { /* not calibrated -> init */
    const uint8_t init[3] = {0xBE, 0x08, 0x00};
    i2c_write(BUS_ONBOARD, AHT20_ADDR, init, 3);
    lp_nap_ms(10);
  }
  return true;
}

bool aht20_measure(int16_t *t_c100, uint16_t *rh_100) {
  const uint8_t trig[3] = {0xAC, 0x33, 0x00};
  if (!i2c_write(BUS_ONBOARD, AHT20_ADDR, trig, 3)) return false;
  lp_nap_ms(80);
  uint8_t b[7];
  for (int tries = 0; tries < 3; tries++) {
    if (!i2c_read(BUS_ONBOARD, AHT20_ADDR, b, 7)) return false;
    if (!(b[0] & 0x80)) break; /* not busy */
    lp_nap_ms(10);
  }
  if ((b[0] & 0x80) || aht_crc(b, 6) != b[6]) return false;
  uint32_t hraw = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
  uint32_t traw = (((uint32_t)b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];
  *rh_100 = (uint16_t)(((uint64_t)hraw * 10000u) >> 20);
  *t_c100 = (int16_t)((int32_t)(((uint64_t)traw * 20000u) >> 20) - 5000);
  return true;
}
