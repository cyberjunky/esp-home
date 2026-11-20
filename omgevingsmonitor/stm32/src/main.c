/*
 * De Omgevingsmonitor - lean, solar-friendly firmware for the STM32L072.
 *
 * The STM32 owns sensing and energy management. Every FAST_TICK_S it wakes
 * for ~50 ms (T/RH + SGP40 VOC). Every report interval it measures sound and
 * the optional sensors, powers the ESP32-C3 (ESPHome), hands it one data
 * frame, and cuts its power as soon as Home Assistant has the data.
 *
 * Energy policy (battery):
 *   SoC >= 60 % (or charging >= 40 %) : base interval (default 10 min)
 *   40..60 % x2 | 25..40 % x4 | 10..25 % x6
 *   <  10 %  : SURVIVAL - no Wi-Fi, no VOC sampling; resumes at >= 20 %
 *   Wi-Fi/HA unreachable: interval backs off x2 / x4 after repeated failures
 * USB: fixed short interval, SEN5x/ENS160 run continuously.
 */
#include <string.h>
#include "board.h"
#include "bridge.h"
#include "i2c_bus.h"
#include "link.h"
#include "log.h"
#include "lowpower.h"
#include "power.h"
#include "sensors.h"
#include "settings.h"
#include "sound.h"

#ifndef FAST_TICK_S
#define FAST_TICK_S 10
#endif
#define POWER_CHECK_MS 60000u
#define SURVIVAL_ENTER_SOC 10
#define SURVIVAL_EXIT_SOC 20
#define LOCAL_MAINT_S 900       /* long button press: ESP awake 15 min for OTA */
#define MAX_INTERVAL_S 10800

enum mode { MODE_NORMAL = 0, MODE_SAVING = 1, MODE_SURVIVAL = 2, MODE_USB = 3 };

static struct readings g_r;
static struct power_status g_pwr;
static uint64_t next_tick_ms, next_report_ms, next_power_ms;
static uint8_t mode = MODE_NORMAL;
static uint8_t fail_streak;
static uint8_t last_link = 0xFF;
static uint16_t cur_interval_s;
static uint32_t reports;
static bool maint_active;

/* ------------------------------------------------------------------------ */
/* scheduling helpers (also used from link.c / sensors.c while busy)         */
/* ------------------------------------------------------------------------ */
void main_service(void) {
  if (mode == MODE_SURVIVAL) return;
  uint64_t now = lp_now_ms();
  if (now >= next_tick_ms) {
    sensors_fast_tick(&g_r);
    next_tick_ms += FAST_TICK_S * 1000u;
    if (next_tick_ms <= now) next_tick_ms = now + FAST_TICK_S * 1000u;
  }
}

void main_wait_with_ticks(uint32_t ms) {
  uint64_t end = lp_now_ms() + ms;
  for (;;) {
    main_service();
    uint64_t now = lp_now_ms();
    if (now >= end) break;
    uint64_t wake = end;
    if (mode != MODE_SURVIVAL && next_tick_ms < wake) wake = next_tick_ms;
    lp_sleep_ms(wake > now ? (uint32_t)(wake - now) : 1);
  }
}

/* ------------------------------------------------------------------------ */
/* policy                                                                     */
/* ------------------------------------------------------------------------ */
static void update_mode(void) {
  if (g_pwr.usb) {
    mode = MODE_USB;
    return;
  }
  uint8_t soc = g_pwr.soc;
  if (mode == MODE_SURVIVAL) {
    if (soc < SURVIVAL_EXIT_SOC) return;
    LOGI("leaving survival, soc %u", soc);
    next_tick_ms = lp_now_ms();
    next_report_ms = lp_now_ms() + 5000;
  } else if (soc < SURVIVAL_ENTER_SOC) {
    LOGI("entering survival, soc %u", soc);
    mode = MODE_SURVIVAL;
    return;
  }
  mode = (soc >= 60 || (g_pwr.charge == CHG_CHARGING && soc >= 40)) ? MODE_NORMAL : MODE_SAVING;
}

static uint16_t policy_interval(void) {
  if (mode == MODE_USB) return g_settings.interval_usb_s;
  uint32_t iv = g_settings.interval_batt_s;
  uint8_t soc = g_pwr.soc;
  bool charging = g_pwr.charge == CHG_CHARGING;
  if (!(soc >= 60 || (charging && soc >= 40))) iv *= soc >= 40 ? 2 : (soc >= 25 ? 4 : 6);
  if (fail_streak >= 3) iv *= 4;
  else if (fail_streak >= 2) iv *= 2;
  return (uint16_t)(iv > MAX_INTERVAL_S ? MAX_INTERVAL_S : iv);
}

/* ------------------------------------------------------------------------ */
/* data frame (keys documented in docs/protocol.md)                          */
/* ------------------------------------------------------------------------ */
static uint16_t make_frame(char *b, uint16_t sz) {
  const struct readings *r = &g_r;
  uint16_t n = frame_begin(b, sz, 'D');
  n = frame_add(b, sz, n, "fw", FW_VERSION_NUM);
  n = frame_add(b, sz, n, "up", (int32_t)(lp_now_ms() / 1000u));
  n = frame_add(b, sz, n, "rs", lp_reset_cause());
  n = frame_add(b, sz, n, "ie", i2c_error_count());
  n = frame_add(b, sz, n, "pr", (int32_t)g_present);
  n = frame_add(b, sz, n, "md", mode);
  n = frame_add(b, sz, n, "iv", cur_interval_s);
  n = frame_add(b, sz, n, "lf", fail_streak);
  n = frame_add(b, sz, n, "mt", maint_active);
  n = frame_add(b, sz, n, "vb", g_pwr.vbat_mv);
  n = frame_add(b, sz, n, "bp", g_pwr.soc);
  n = frame_add(b, sz, n, "vs", g_pwr.vsolar_mv);
  n = frame_add(b, sz, n, "us", g_pwr.usb);
  n = frame_add(b, sz, n, "cs", g_pwr.charge);
  if (r->valid & V_HIDS) {
    n = frame_add(b, sz, n, "t", r->t_c100);
    n = frame_add(b, sz, n, "h", r->rh_100);
  }
  if (r->valid & V_VOC_RAW) n = frame_add(b, sz, n, "vr", r->voc_raw);
  if (r->valid & V_VOC_INDEX) n = frame_add(b, sz, n, "vi", r->voc_index);
  if (r->valid & V_SOUND) {
    n = frame_add(b, sz, n, "la", r->la_10);
    n = frame_add(b, sz, n, "lx", r->lmax_10);
  }
  if (r->valid & V_AHT20) {
    n = frame_add(b, sz, n, "at", r->aht_t_c100);
    n = frame_add(b, sz, n, "ah", r->aht_rh_100);
  }
  if (r->valid & V_BMP) {
    n = frame_add(b, sz, n, "bt", r->bmp_t_c100);
    n = frame_add(b, sz, n, "pa", (int32_t)r->p_pa);
  }
  if (r->valid & V_BME_RH) n = frame_add(b, sz, n, "bh", r->bme_rh_100);
  if (r->valid & V_ENS160) {
    n = frame_add(b, sz, n, "es", r->ens_status);
    if (r->ens_status == 0) {
      n = frame_add(b, sz, n, "aq", r->ens_aqi);
      n = frame_add(b, sz, n, "tv", r->ens_tvoc);
      n = frame_add(b, sz, n, "co", r->ens_eco2);
    }
  }
  if (r->valid & V_PM) {
    n = frame_add(b, sz, n, "p1", r->pm1_10);
    n = frame_add(b, sz, n, "p2", r->pm25_10);
    n = frame_add(b, sz, n, "p4", r->pm4_10);
    n = frame_add(b, sz, n, "p0", r->pm10_10);
  }
  if (r->valid & V_SEN_RHT) {
    n = frame_add(b, sz, n, "st", r->sen_t_c100);
    n = frame_add(b, sz, n, "sh", r->sen_rh_100);
  }
  if (r->valid & V_SEN_VOC) n = frame_add(b, sz, n, "sv", r->sen_voc_10);
  if (r->valid & V_SEN_NOX) n = frame_add(b, sz, n, "sn", r->sen_nox_10);
  return frame_end(b, sz, n);
}

/* ------------------------------------------------------------------------ */
/* one report                                                                 */
/* ------------------------------------------------------------------------ */
static void do_report(uint16_t local_maint_s) {
  power_measure(&g_pwr); /* rest voltage, before the ESP loads the battery */
  update_mode();
  sensors_power_policy(g_pwr.usb);
  cur_interval_s = policy_interval();

  g_r.valid &= ~V_SOUND;
  if (sound_measure(&g_r.la_10, &g_r.lmax_10)) {
    g_r.valid |= V_SOUND;
    g_present |= HAS_MIC;
  }
  sensors_report(&g_r, g_pwr.usb, g_pwr.soc);

  maint_active = local_maint_s > 0;
  struct link_config cfg;
  enum link_result res = link_session(make_frame, 30000, g_settings.esp_timeout_s, local_maint_s, &cfg);
  maint_active = false;
  last_link = res;
  reports++;
  if (res == LINK_DONE) {
    fail_streak = 0;
    bool changed = false;
    if (cfg.have_interval_batt && cfg.interval_batt_s != g_settings.interval_batt_s) {
      g_settings.interval_batt_s = cfg.interval_batt_s;
      changed = true;
    }
    if (cfg.have_interval_usb && cfg.interval_usb_s != g_settings.interval_usb_s) {
      g_settings.interval_usb_s = cfg.interval_usb_s;
      changed = true;
    }
    if (changed) settings_save();
  } else if (fail_streak < 10) {
    fail_streak++;
  }
  LOGI("report %lu: link=%d vb=%u soc=%u mode=%u", (unsigned long)reports, res, g_pwr.vbat_mv,
       g_pwr.soc, mode);

  cur_interval_s = policy_interval();
  next_report_ms = lp_now_ms() + cur_interval_s * 1000u;
}

/* ------------------------------------------------------------------------ */
/* button: short press = report now (+ battery colour), long = maintenance   */
/* ------------------------------------------------------------------------ */
static void handle_button(void) {
  g_button_irq = false;
  uint32_t t0 = HAL_GetTick();
  bool long_press = false;
  while (board_button_pressed()) {
    lp_kick();
    if (HAL_GetTick() - t0 > 3000) {
      long_press = true;
      break;
    }
    HAL_Delay(10);
  }
  if (HAL_GetTick() - t0 < 30 && !long_press) return; /* bounce */

  power_measure(&g_pwr);
  if (long_press) {
    board_blink(LED_MAGENTA, 500);
    do_report(LOCAL_MAINT_S);
  } else {
    enum led_color c = g_pwr.usb ? LED_BLUE : g_pwr.soc >= 50 ? LED_GREEN : g_pwr.soc >= 20 ? LED_YELLOW : LED_RED;
    board_blink(c, 400);
    if (mode != MODE_SURVIVAL || g_pwr.usb) do_report(0);
  }
}

/* ------------------------------------------------------------------------ */

void HAL_Delay(uint32_t ms) { /* sleep (not spin) between SysTicks */
  uint32_t t0 = HAL_GetTick();
  while (HAL_GetTick() - t0 < ms) __WFI();
}

int main(void) {
  HAL_Init();
  board_clock_init();
  board_gpio_init();

  /* user button held during reset -> USB bridge for flashing the ESP */
  if (board_button_pressed()) {
    HAL_Delay(50);
    if (board_button_pressed()) bridge_run();
  }

  lp_init();
  settings_load();
  LOGI("Omgevingsmonitor " FW_VERSION ", reset %u, lse %u", lp_reset_cause(), lp_lse_ok());
  board_blink(LED_GREEN, 150);

  sensors_probe();
  LOGI("sensors 0x%02lx sen5x %u", (unsigned long)g_present, g_sen5x_type);
  power_measure(&g_pwr);
  update_mode();
  sensors_power_policy(g_pwr.usb);

  uint64_t now = lp_now_ms();
  next_tick_ms = now;
  next_report_ms = now + 3 * FAST_TICK_S * 1000u; /* first report after a few VOC samples */
  next_power_ms = now + POWER_CHECK_MS;

  for (;;) {
    lp_kick();
    main_service();

    if (g_button_irq) handle_button();
    if (g_vusb_irq) {
      g_vusb_irq = false;
      bool was_usb = g_pwr.usb;
      power_measure(&g_pwr);
      update_mode();
      sensors_power_policy(g_pwr.usb);
      if (g_pwr.usb != was_usb) next_report_ms = lp_now_ms() + 2000;
    }

    now = lp_now_ms();
    if (now >= next_power_ms) {
      uint8_t old = mode;
      power_measure(&g_pwr);
      update_mode();
      /* say goodbye to HA once when entering survival, if there's energy left */
      if (mode == MODE_SURVIVAL && old != MODE_SURVIVAL && g_pwr.vbat_mv > 3550) do_report(0);
      next_power_ms = now + POWER_CHECK_MS;
    }

    if (mode != MODE_SURVIVAL && lp_now_ms() >= next_report_ms) do_report(0);

    /* sleep until the next thing to do */
    now = lp_now_ms();
    uint64_t wake = next_power_ms;
    if (mode != MODE_SURVIVAL) {
      if (next_tick_ms < wake) wake = next_tick_ms;
      if (next_report_ms < wake) wake = next_report_ms;
    }
    if (wake > now && !g_button_irq && !g_vusb_irq) lp_sleep_ms((uint32_t)(wake - now));
  }
}
