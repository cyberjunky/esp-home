/*
 * sens_sen5x.c - Sensirion SEN50/54/55 particulate sensor (optional).
 * Lives on I2C2 and the 5 V boost converter, both switched off when idle.
 */
#include "board.h"
#include "i2c_bus.h"
#include "lowpower.h"
#include "sensors.h"

#define SEN5X_ADDR 0x69

bool sen5x_power_on_probe(uint8_t *type) {
  board_boost(true);
  lp_nap_ms(100); /* boost + sensor power-up */
  if (!sensirion_cmd(BUS_EXT, SEN5X_ADDR, 0xD014, 0, 0)) goto fail; /* product name */
  lp_nap_ms(50);
  uint16_t w[3];
  if (!sensirion_read_words(BUS_EXT, SEN5X_ADDR, w, 3)) goto fail;
  /* "SEN55" -> w[0]='SE' w[1]='N5' w[2]='5\0' */
  if ((w[0] >> 8) != 'S' || (w[1] >> 8) != 'N') goto fail;
  *type = (uint8_t)(50 + ((w[2] >> 8) - '0'));
  if ((w[1] & 0xFF) != '5') *type = 50;
  return true;
fail:
  sen5x_power_off();
  return false;
}

bool sen5x_start(void) {
  bool ok = sensirion_cmd(BUS_EXT, SEN5X_ADDR, 0x0021, 0, 0);
  lp_nap_ms(50);
  return ok;
}

bool sen5x_stop(void) {
  bool ok = sensirion_cmd(BUS_EXT, SEN5X_ADDR, 0x0104, 0, 0);
  lp_nap_ms(200);
  return ok;
}

bool sen5x_read(struct readings *r) {
  if (!sensirion_cmd(BUS_EXT, SEN5X_ADDR, 0x0202, 0, 0)) return false; /* data ready? */
  lp_nap_ms(20);
  uint16_t ready;
  if (!sensirion_read_words(BUS_EXT, SEN5X_ADDR, &ready, 1) || !(ready & 1)) return false;

  if (!sensirion_cmd(BUS_EXT, SEN5X_ADDR, 0x03C4, 0, 0)) return false;
  lp_nap_ms(20);
  uint16_t w[8];
  if (!sensirion_read_words(BUS_EXT, SEN5X_ADDR, w, 8)) return false;

  if (w[1] != 0xFFFF) {
    r->pm1_10 = w[0];
    r->pm25_10 = w[1];
    r->pm4_10 = w[2];
    r->pm10_10 = w[3];
    r->valid |= V_PM;
  }
  if ((int16_t)w[4] != 0x7FFF && (int16_t)w[5] != 0x7FFF) {
    r->sen_rh_100 = w[4];
    r->sen_t_c100 = (int16_t)((int16_t)w[5] / 2); /* x200 -> x100 */
    r->valid |= V_SEN_RHT;
  }
  if ((int16_t)w[6] != 0x7FFF && w[6] != 0) {
    r->sen_voc_10 = w[6];
    r->valid |= V_SEN_VOC;
  }
  if ((int16_t)w[7] != 0x7FFF && w[7] != 0) {
    r->sen_nox_10 = w[7];
    r->valid |= V_SEN_NOX;
  }
  return true;
}

void sen5x_power_off(void) {
  i2c_close(BUS_EXT);
  board_boost(false);
}
