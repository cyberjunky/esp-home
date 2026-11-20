/*
 * lowpower.h - RTC time base, Stop-mode sleep and watchdog.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void lp_init(void);                 /* RTC (LSE, LSI fallback) + IWDG          */
uint64_t lp_now_ms(void);           /* monotonic ms since boot, runs in Stop   */
/* Sleep in Stop mode for up to ms (capped at LP_MAX_SLEEP_MS, safely below the
 * worst-case IWDG timeout of ~18 s). Returns early on button / USB interrupt.
 * The watchdog is kicked before and after. */
#define LP_MAX_SLEEP_MS 15000u
void lp_sleep_ms(uint32_t ms);
/* Wait at least ms in Stop mode, robust against early wake-ups (sensor waits). */
void lp_nap_ms(uint32_t ms);
void lp_inhibit_stop(bool on);      /* while the ESP UART is live           */
void lp_kick(void);                 /* refresh watchdog                         */
bool lp_lse_ok(void);
bool lp_rtc_kept(void);             /* RTC survived the last reset              */
uint8_t lp_reset_cause(void);       /* see RESET_* below                        */
uint32_t lp_abs_s(void);            /* RTC seconds since 2000 (keeps across resets) */

/* RTC backup registers 0..4: survive resets, no flash wear.
 *   0..3  VOC algorithm state + time + check (sensors.c)   4  free */
uint32_t lp_bkp_read(uint8_t idx);
void lp_bkp_write(uint8_t idx, uint32_t v);

enum {
  RESET_POWER = 0, RESET_PIN = 1, RESET_SOFTWARE = 2, RESET_WATCHDOG = 3,
  RESET_LOWPOWER = 4, RESET_OTHER = 5
};
