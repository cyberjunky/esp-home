/*
 * energy.c - coulomb estimate from measured on-times.
 */
#include "energy.h"
#include "lowpower.h"

static uint64_t extra_uams;     /* uA*ms of extra consumers this period */
static uint64_t slept_ms;       /* time in Stop this period             */
static uint64_t period_start;
static float avg_mah_day;       /* exponential average, ~24 h           */

void energy_add(uint32_t current_ua, uint32_t ms) { extra_uams += (uint64_t)current_ua * ms; }

void energy_slept(uint32_t ms) { slept_ms += ms; }

uint16_t energy_update(void) {
  uint64_t now = lp_now_ms();
  uint64_t dt = now - period_start;
  if (dt < 1000) return energy_mah_day_10();
  uint64_t slept = slept_ms > dt ? dt : slept_ms;
  uint64_t awake = dt - slept;
  uint64_t uams = slept * I_SLEEP_UA + awake * I_RUN_UA + extra_uams;

  float mah_day = ((float)uams / (float)dt) * 24.0f / 1000.0f; /* average uA x 24 h -> mAh/day */
  float w = (float)dt / 86400000.0f;
  if (w > 1.0f) w = 1.0f;
  avg_mah_day = avg_mah_day == 0.0f ? mah_day : avg_mah_day + (mah_day - avg_mah_day) * w;

  period_start = now;
  slept_ms = 0;
  extra_uams = 0;
  return energy_mah_day_10();
}

uint16_t energy_mah_day_10(void) {
  float v = avg_mah_day * 10.0f;
  return (uint16_t)(v > 65000.0f ? 65000.0f : v + 0.5f);
}

uint16_t energy_runtime_days_10(uint8_t soc) {
  if (avg_mah_day <= 0.0f) return 0;
  float days = (float)soc / 100.0f * BATTERY_MAH / avg_mah_day;
  if (days > 999.0f) days = 999.0f;
  return (uint16_t)(days * 10.0f + 0.5f);
}
