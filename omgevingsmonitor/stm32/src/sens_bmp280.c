/*
 * sens_bmp280.c - Bosch BMP280 / BME280 (optional, 0x76 or 0x77).
 * Forced mode: the chip sleeps between reports (~0.1 uA).
 * Compensation follows the integer reference formulas from the datasheet.
 */
#include "i2c_bus.h"
#include "lowpower.h"
#include "sensors.h"

static uint8_t addr;
static bool bme;
static struct {
  uint16_t T1; int16_t T2, T3;
  uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
  uint8_t H1, H3; int16_t H2, H4, H5; int8_t H6;
} c;

static uint16_t u16le(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }

bool bmp280_probe(bool *is_bme) {
  static const uint8_t addrs[2] = {0x76, 0x77};
  for (int i = 0; i < 2; i++) {
    uint8_t id;
    if (!i2c_probe(BUS_ONBOARD, addrs[i])) continue;
    if (!i2c_read_reg(BUS_ONBOARD, addrs[i], 0xD0, &id, 1)) continue;
    if (id != 0x58 && id != 0x60) continue;
    addr = addrs[i];
    bme = id == 0x60;
    uint8_t b[26];
    if (!i2c_read_reg(BUS_ONBOARD, addr, 0x88, b, 24)) return false;
    c.T1 = u16le(b); c.T2 = (int16_t)u16le(b + 2); c.T3 = (int16_t)u16le(b + 4);
    c.P1 = u16le(b + 6);
    c.P2 = (int16_t)u16le(b + 8);  c.P3 = (int16_t)u16le(b + 10); c.P4 = (int16_t)u16le(b + 12);
    c.P5 = (int16_t)u16le(b + 14); c.P6 = (int16_t)u16le(b + 16); c.P7 = (int16_t)u16le(b + 18);
    c.P8 = (int16_t)u16le(b + 20); c.P9 = (int16_t)u16le(b + 22);
    if (bme) {
      uint8_t h[7];
      if (!i2c_read_reg(BUS_ONBOARD, addr, 0xA1, &c.H1, 1)) return false;
      if (!i2c_read_reg(BUS_ONBOARD, addr, 0xE1, h, 7)) return false;
      c.H2 = (int16_t)u16le(h);
      c.H3 = h[2];
      c.H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F));
      c.H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));
      c.H6 = (int8_t)h[6];
    }
    *is_bme = bme;
    return true;
  }
  return false;
}

bool bmp280_measure(int16_t *t_c100, uint32_t *p_pa, uint16_t *rh_100) {
  uint8_t v;
  if (bme) {
    v = 0x01; /* humidity x1 (must precede ctrl_meas) */
    i2c_write_reg(BUS_ONBOARD, addr, 0xF2, &v, 1);
  }
  v = 0x00; /* no IIR filter, standby n/a */
  i2c_write_reg(BUS_ONBOARD, addr, 0xF5, &v, 1);
  v = (1 << 5) | (3 << 2) | 1; /* T x1, P x4, forced */
  if (!i2c_write_reg(BUS_ONBOARD, addr, 0xF4, &v, 1)) return false;
  lp_nap_ms(20);

  uint8_t b[8];
  if (!i2c_read_reg(BUS_ONBOARD, addr, 0xF7, b, bme ? 8 : 6)) return false;
  int32_t adc_p = (int32_t)((uint32_t)b[0] << 12 | (uint32_t)b[1] << 4 | b[2] >> 4);
  int32_t adc_t = (int32_t)((uint32_t)b[3] << 12 | (uint32_t)b[4] << 4 | b[5] >> 4);
  if (adc_t == 0x80000) return false; /* measurement skipped */

  int32_t var1 = ((((adc_t >> 3) - ((int32_t)c.T1 << 1))) * c.T2) >> 11;
  int32_t var2 = (((((adc_t >> 4) - c.T1) * ((adc_t >> 4) - c.T1)) >> 12) * c.T3) >> 14;
  int32_t t_fine = var1 + var2;
  *t_c100 = (int16_t)((t_fine * 5 + 128) >> 8);

  int64_t p1 = (int64_t)t_fine - 128000;
  int64_t p2 = p1 * p1 * c.P6;
  p2 += (p1 * c.P5) << 17;
  p2 += ((int64_t)c.P4) << 35;
  p1 = ((p1 * p1 * c.P3) >> 8) + ((p1 * c.P2) << 12);
  p1 = ((((int64_t)1) << 47) + p1) * c.P1 >> 33;
  if (p1 == 0) return false;
  int64_t p = 1048576 - adc_p;
  p = (((p << 31) - p2) * 3125) / p1;
  p1 = ((int64_t)c.P9 * (p >> 13) * (p >> 13)) >> 25;
  p2 = ((int64_t)c.P8 * p) >> 19;
  p = ((p + p1 + p2) >> 8) + ((int64_t)c.P7 << 4);
  *p_pa = (uint32_t)(p / 256);

  if (bme && rh_100) {
    int32_t adc_h = (int32_t)((uint32_t)b[6] << 8 | b[7]);
    int32_t x = t_fine - 76800;
    x = (((((adc_h << 14) - ((int32_t)c.H4 << 20) - ((int32_t)c.H5 * x)) + 16384) >> 15) *
         (((((((x * c.H6) >> 10) * (((x * c.H3) >> 11) + 32768)) >> 10) + 2097152) * c.H2 + 8192) >> 14));
    x = x - (((((x >> 15) * (x >> 15)) >> 7) * c.H1) >> 4);
    x = x < 0 ? 0 : (x > 419430400 ? 419430400 : x);
    *rh_100 = (uint16_t)(((uint32_t)(x >> 12) * 100u) >> 10); /* Q22.10 %RH -> x100 */
  }
  return true;
}
