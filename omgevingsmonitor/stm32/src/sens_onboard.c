/*
 * sens_onboard.c - WSEN-HIDS (T/RH) and SGP40 (VOC raw) drivers.
 *
 * WSEN-HIDS 2525020210002 uses the SHT4x command set.
 * SGP40: one humidity-compensated measurement (30 ms heater pulse), then
 * the heater is switched off again -> ~3 mA for 30 ms every 10 s.
 */
#include "energy.h"
#include "i2c_bus.h"
#include "lowpower.h"
#include "sensors.h"

#define HIDS_ADDR 0x44
#define SGP40_ADDR 0x59

bool hids_measure(int16_t *t_c100, uint16_t *rh_100) {
  uint8_t cmd = 0xFD; /* high precision, no heater */
  if (!i2c_write(BUS_ONBOARD, HIDS_ADDR, &cmd, 1)) return false;
  lp_nap_ms(10);
  uint8_t b[6];
  if (!i2c_read(BUS_ONBOARD, HIDS_ADDR, b, 6)) return false;
  if (crc8_sensirion(b, 2) != b[2] || crc8_sensirion(b + 3, 2) != b[5]) return false;
  uint32_t traw = (uint32_t)b[0] << 8 | b[1];
  uint32_t hraw = (uint32_t)b[3] << 8 | b[4];
  /* T = -45 + 175 * raw / 65535 ; RH = -6 + 125 * raw / 65535 */
  *t_c100 = (int16_t)(-4500 + (int32_t)((traw * 17500u) / 65535u));
  int32_t rh = -600 + (int32_t)((hraw * 12500u) / 65535u);
  if (rh < 0) rh = 0;
  if (rh > 10000) rh = 10000;
  *rh_100 = (uint16_t)rh;
  return true;
}

bool sgp40_probe(void) {
  if (!sensirion_cmd(BUS_ONBOARD, SGP40_ADDR, 0x3682, 0, 0)) return false; /* serial */
  lp_nap_ms(2);
  uint16_t w[3];
  return sensirion_read_words(BUS_ONBOARD, SGP40_ADDR, w, 3);
}

bool sgp40_measure_raw(uint16_t rh_100, int16_t t_c100, uint16_t *raw) {
  uint16_t args[2];
  args[0] = (uint16_t)(((uint32_t)rh_100 * 65535u) / 10000u);
  int32_t t = t_c100 < -4500 ? -4500 : (t_c100 > 13000 ? 13000 : t_c100);
  args[1] = (uint16_t)(((uint32_t)(t + 4500) * 65535u) / 17500u);
  /* Sensirion low-power procedure: the first measure command switches the
   * hotplate on; it needs ~170 ms to reach temperature before a valid
   * measurement. Reading straight after an idle period returns 0. */
  if (!sensirion_cmd(BUS_ONBOARD, SGP40_ADDR, 0x260F, args, 2)) return false;
  lp_nap_ms(30);
  uint16_t w = 0;
  sensirion_read_words(BUS_ONBOARD, SGP40_ADDR, &w, 1); /* discard: heater warming up */
  lp_nap_ms(140);
  bool ok = sensirion_cmd(BUS_ONBOARD, SGP40_ADDR, 0x260F, args, 2);
  if (ok) {
    lp_nap_ms(30);
    ok = sensirion_read_words(BUS_ONBOARD, SGP40_ADDR, &w, 1);
  }
  sensirion_cmd(BUS_ONBOARD, SGP40_ADDR, 0x3615, 0, 0); /* heater off -> idle */
  energy_add(I_HEATER_UA, 200);
  if (!ok || w == 0) return false;
  *raw = w;
  return true;
}
