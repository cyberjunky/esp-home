/*
 * sensors.c - detection, 10 s fast tick (T/RH + VOC) and per-report reads.
 *
 * The SGP40 VOC index needs a steady sample stream to learn its baseline,
 * so it is sampled every FAST_TICK_S seconds - in a ~45 ms wake-up - using
 * Sensirion's low-power (10 s) mode of the gas index algorithm.
 */
#include "sensors.h"
#include "board.h"
#include "i2c_bus.h"
#include "lowpower.h"
#include "sensirion_gas_index_algorithm.h"

#ifndef FAST_TICK_S
#define FAST_TICK_S 10
#endif
#ifndef SEN5X_BATT_PERIOD_S          /* PM measurement period on battery (0 = never) */
#define SEN5X_BATT_PERIOD_S 3600
#endif
#ifndef SEN5X_BATT_MIN_SOC           /* ... and only above this state of charge     */
#define SEN5X_BATT_MIN_SOC 50
#endif
#ifndef SEN5X_WARMUP_S               /* fan spin-up before PM values are trustworthy */
#define SEN5X_WARMUP_S 30
#endif
#ifndef ENS160_ON_BATTERY            /* ENS160 draws mA continuously: USB only       */
#define ENS160_ON_BATTERY 0
#endif

uint32_t g_present;
uint8_t g_sen5x_type;

static GasIndexAlgorithmParams voc_algo;
static bool sen5x_running, ens_running;
static uint64_t sen5x_last_batt_ms;
static bool sen5x_ever_batt;
static int16_t comp_t = 2500;    /* compensation inputs for SGP40 / ENS160 */
static uint16_t comp_rh = 5000;

void sensors_probe(void) {
  int16_t t;
  uint16_t h;
  bool bme = false;
  g_present = 0;
  if (hids_measure(&t, &h)) {
    g_present |= HAS_HIDS;
    comp_t = t;
    comp_rh = h;
  }
  if (sgp40_probe()) {
    g_present |= HAS_SGP40;
    GasIndexAlgorithm_init_with_sampling_interval(&voc_algo, GasIndexAlgorithm_ALGORITHM_TYPE_VOC,
                                                  (float)FAST_TICK_S);
  }
  if (aht20_probe()) g_present |= HAS_AHT20;
  if (bmp280_probe(&bme)) g_present |= bme ? HAS_BME280 : HAS_BMP280;
  if (ens160_probe()) g_present |= HAS_ENS160;
  i2c_close(BUS_ONBOARD);

  if (sen5x_power_on_probe(&g_sen5x_type)) g_present |= HAS_SEN5X;
  sen5x_power_off();
}

void sensors_fast_tick(struct readings *r) {
  if (g_present & HAS_HIDS) {
    if (hids_measure(&r->t_c100, &r->rh_100)) {
      r->valid |= V_HIDS;
      comp_t = r->t_c100;
      comp_rh = r->rh_100;
    } else {
      r->valid &= ~V_HIDS;
    }
  }
  if (g_present & HAS_SGP40) {
    uint16_t raw;
    if (sgp40_measure_raw(comp_rh, comp_t, &raw)) {
      int32_t idx = 0;
      GasIndexAlgorithm_process(&voc_algo, raw, &idx);
      r->voc_raw = raw;
      r->valid |= V_VOC_RAW;
      if (idx > 0) { /* 0 during the algorithm's initial blackout */
        r->voc_index = (uint16_t)idx;
        r->valid |= V_VOC_INDEX;
      }
    } else {
      r->valid &= ~V_VOC_RAW;
    }
  }
  if (ens_running) ens160_compensate(comp_t, comp_rh);
  i2c_close(BUS_ONBOARD);
}

void sensors_power_policy(bool usb) {
  if (g_present & HAS_SEN5X) {
    if (usb && !sen5x_running) {
      uint8_t type;
      if (sen5x_power_on_probe(&type) && sen5x_start()) sen5x_running = true;
      else sen5x_power_off();
    } else if (!usb && sen5x_running) {
      sen5x_stop();
      sen5x_power_off();
      sen5x_running = false;
    }
  }
  if (g_present & HAS_ENS160) {
    bool run = usb || ENS160_ON_BATTERY;
    if (run != ens_running) {
      ens160_mode(run);
      ens_running = run;
    }
    i2c_close(BUS_ONBOARD);
  }
}

bool sen5x_battery_due(uint64_t now_ms, uint8_t soc) {
  if (!(g_present & HAS_SEN5X) || sen5x_running || SEN5X_BATT_PERIOD_S == 0) return false;
  if (soc < SEN5X_BATT_MIN_SOC) return false;
  return !sen5x_ever_batt || now_ms - sen5x_last_batt_ms >= (uint64_t)SEN5X_BATT_PERIOD_S * 1000u;
}

/* Wait in Stop mode while keeping the fast tick going (used for SEN5x spin-up). */
extern void main_wait_with_ticks(uint32_t ms);

void sensors_report(struct readings *r, bool usb, uint8_t soc) {
  r->valid &= ~(V_AHT20 | V_BMP | V_BME_RH | V_ENS160 | V_PM | V_SEN_RHT | V_SEN_VOC | V_SEN_NOX);

  if (g_present & HAS_AHT20 && aht20_measure(&r->aht_t_c100, &r->aht_rh_100)) r->valid |= V_AHT20;
  if (g_present & (HAS_BMP280 | HAS_BME280)) {
    if (bmp280_measure(&r->bmp_t_c100, &r->p_pa, &r->bme_rh_100)) {
      r->valid |= V_BMP;
      if (g_present & HAS_BME280) r->valid |= V_BME_RH;
    }
  }
  if (ens_running && ens160_read(&r->ens_aqi, &r->ens_tvoc, &r->ens_eco2, &r->ens_status)) {
    r->valid |= V_ENS160;
  }
  i2c_close(BUS_ONBOARD);

  if (sen5x_running) {
    sen5x_read(r);
  } else if (!usb && sen5x_battery_due(lp_now_ms(), soc)) {
    uint8_t type;
    if (sen5x_power_on_probe(&type) && sen5x_start()) {
      main_wait_with_ticks(SEN5X_WARMUP_S * 1000u);
      sen5x_read(r);
      r->valid &= ~(V_SEN_VOC | V_SEN_NOX); /* not meaningful after a short run */
      sen5x_stop();
    }
    sen5x_power_off();
    sen5x_last_batt_ms = lp_now_ms();
    sen5x_ever_batt = true;
  }
}
