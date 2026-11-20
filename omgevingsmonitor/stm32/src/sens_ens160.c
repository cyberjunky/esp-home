/*
 * sens_ens160.c - ScioSense ENS160 MOX gas sensor (optional, 0x52/0x53).
 * Needs continuous operation (~3 min warm-up) so it only runs on USB power
 * by default; otherwise it is kept in deep sleep (~10 uA).
 */
#include "i2c_bus.h"
#include "lowpower.h"
#include "sensors.h"

static uint8_t addr;

#define REG_PART_ID 0x00
#define REG_OPMODE 0x10
#define REG_TEMP_IN 0x13
#define REG_STATUS 0x20
#define REG_AQI 0x21

bool ens160_probe(void) {
  static const uint8_t addrs[2] = {0x52, 0x53};
  for (int i = 0; i < 2; i++) {
    uint8_t id[2];
    if (!i2c_probe(BUS_ONBOARD, addrs[i])) continue;
    if (!i2c_read_reg(BUS_ONBOARD, addrs[i], REG_PART_ID, id, 2)) continue;
    if ((id[0] | id[1] << 8) != 0x0160) continue;
    addr = addrs[i];
    ens160_mode(false);
    return true;
  }
  return false;
}

void ens160_mode(bool run) {
  uint8_t m = run ? 0x02 : 0x00; /* standard : deep sleep */
  i2c_write_reg(BUS_ONBOARD, addr, REG_OPMODE, &m, 1);
}

bool ens160_compensate(int16_t t_c100, uint16_t rh_100) {
  uint16_t t = (uint16_t)(((int32_t)t_c100 + 27315) * 64 / 100); /* Kelvin * 64 */
  uint16_t h = (uint16_t)((uint32_t)rh_100 * 512u / 100u);       /* %RH * 512   */
  uint8_t b[4] = {(uint8_t)t, (uint8_t)(t >> 8), (uint8_t)h, (uint8_t)(h >> 8)};
  return i2c_write_reg(BUS_ONBOARD, addr, REG_TEMP_IN, b, 4);
}

bool ens160_read(uint8_t *aqi, uint16_t *tvoc, uint16_t *eco2, uint8_t *status) {
  uint8_t st;
  if (!i2c_read_reg(BUS_ONBOARD, addr, REG_STATUS, &st, 1)) return false;
  uint8_t b[5];
  if (!i2c_read_reg(BUS_ONBOARD, addr, REG_AQI, b, 5)) return false;
  *status = (st >> 2) & 0x03; /* 0 normal, 1 warm-up, 2 initial start-up, 3 invalid */
  *aqi = b[0] & 0x07;
  *tvoc = (uint16_t)(b[1] | b[2] << 8);
  *eco2 = (uint16_t)(b[3] | b[4] << 8);
  return true;
}
