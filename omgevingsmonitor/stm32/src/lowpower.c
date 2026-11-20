/*
 * lowpower.c - RTC time base, Stop-mode sleep, watchdog, backup registers.
 *
 * Stop mode with low-power regulator, ULP (VREFINT off) and RTC wake-up
 * timer: the STM32L072 itself draws ~1 uA here. The RTC calendar doubles
 * as a monotonic clock because SysTick does not run in Stop.
 *
 * The RTC keeps running across resets (the backup domain stays powered by
 * the battery), so state kept in the RTC backup registers - like the VOC
 * algorithm's learned baseline - knows how old it is after a reset or flash.
 */
#include "lowpower.h"
#include "board.h"
#include "energy.h"
#include "stm32l0xx_hal.h"

#define RTC_PREDIV_A 7u   /* RTCCLK/8 = 4096 Hz -> 244 us resolution */

RTC_HandleTypeDef hrtc;
static IWDG_HandleTypeDef hiwdg;
static bool lse_ok;
static bool rtc_kept;
static uint32_t rtc_hz = 32768;     /* RTCCLK */
static uint8_t reset_cause;
static uint64_t boot_abs_ms;

static void capture_reset_cause(void) {
  uint32_t csr = RCC->CSR;
  if (csr & RCC_CSR_IWDGRSTF) reset_cause = RESET_WATCHDOG;
  else if (csr & RCC_CSR_LPWRRSTF) reset_cause = RESET_LOWPOWER;
  else if (csr & RCC_CSR_SFTRSTF) reset_cause = RESET_SOFTWARE;
  else if (csr & (RCC_CSR_PORRSTF)) reset_cause = RESET_POWER;
  else if (csr & RCC_CSR_PINRSTF) reset_cause = RESET_PIN;
  else reset_cause = RESET_OTHER;
  RCC->CSR |= RCC_CSR_RMVF;
}

uint8_t lp_reset_cause(void) { return reset_cause; }
bool lp_lse_ok(void) { return lse_ok; }
bool lp_rtc_kept(void) { return rtc_kept; }

/* Is the RTC already running the way we configure it (survived a reset)? */
static bool rtc_already_running(void) {
  uint32_t csr = RCC->CSR;
  if (!(csr & RCC_CSR_RTCEN) || !(RTC->ISR & RTC_ISR_INITS)) return false;
  uint32_t sel = csr & RCC_CSR_RTCSEL;
  if (sel == RCC_RTCCLKSOURCE_LSE && (csr & RCC_CSR_LSERDY)) {
    rtc_hz = 32768;
    lse_ok = true;
  } else if (sel == RCC_RTCCLKSOURCE_LSI) {
    rtc_hz = 37000;
    lse_ok = false;
    __HAL_RCC_LSI_ENABLE();
  } else {
    return false;
  }
  uint32_t prer = RTC->PRER;
  return ((prer >> 16) & 0x7F) == RTC_PREDIV_A && (prer & RTC_PRER_PREDIV_S) == (rtc_hz / 8) - 1;
}

static void rtc_clock_init(void) {
  /* Fresh backup domain: lets us pick the RTC clock source freely */
  __HAL_RCC_BACKUPRESET_FORCE();
  __HAL_RCC_BACKUPRESET_RELEASE();

  RCC_OscInitTypeDef osc = {0};
  osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
  osc.LSEState = RCC_LSE_ON;
  osc.PLL.PLLState = RCC_PLL_NONE;
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);
  lse_ok = HAL_RCC_OscConfig(&osc) == HAL_OK;

  RCC_PeriphCLKInitTypeDef pclk = {0};
  pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  if (lse_ok) {
    pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    rtc_hz = 32768;
  } else {
    /* LSE missing/dead: LSI (~37 kHz, +-30 %) keeps us alive */
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
    osc.LSEState = RCC_LSE_OFF;
    osc.LSIState = RCC_LSI_ON;
    HAL_RCC_OscConfig(&osc);
    pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    rtc_hz = 37000;
  }
  HAL_RCCEx_PeriphCLKConfig(&pclk);
  __HAL_RCC_RTC_ENABLE();
}

/* days since 2000-01-01 (proleptic Gregorian, years 2000-2099) */
static uint32_t days_since_2000(uint32_t y, uint32_t m, uint32_t d) {
  static const uint16_t cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  uint32_t days = y * 365u + (y + 3u) / 4u + cum[(m - 1u) % 12u] + d - 1u;
  if (m > 2 && (y % 4u) == 0) days++;
  return days;
}

static uint64_t rtc_abs_ms(void) {
  uint32_t ssr, tr, dr, ssr2, tr2, dr2;
  do { /* bypass-shadow reads must be done twice for consistency */
    ssr = RTC->SSR;
    tr = RTC->TR;
    dr = RTC->DR;
    ssr2 = RTC->SSR;
    tr2 = RTC->TR;
    dr2 = RTC->DR;
  } while (ssr != ssr2 || tr != tr2 || dr != dr2);
  uint32_t h = ((tr >> 20) & 0x3) * 10 + ((tr >> 16) & 0xF);
  uint32_t m = ((tr >> 12) & 0x7) * 10 + ((tr >> 8) & 0xF);
  uint32_t s = ((tr >> 4) & 0x7) * 10 + (tr & 0xF);
  uint32_t yy = ((dr >> 20) & 0xF) * 10 + ((dr >> 16) & 0xF);
  uint32_t mo = ((dr >> 12) & 0x1) * 10 + ((dr >> 8) & 0xF);
  uint32_t dd = ((dr >> 4) & 0x3) * 10 + (dr & 0xF);
  uint32_t prediv_s = (RTC->PRER & RTC_PRER_PREDIV_S) + 1;
  uint32_t sub_ms = ((prediv_s - 1 - ssr) * 1000u) / prediv_s;
  uint64_t days = days_since_2000(yy, mo ? mo : 1, dd ? dd : 1);
  return days * 86400000ull + (((h * 60 + m) * 60 + s) * 1000u + sub_ms);
}

void lp_init(void) {
  capture_reset_cause();
  HAL_PWR_EnableBkUpAccess();

  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

  rtc_kept = rtc_already_running();
  if (rtc_kept) {
    /* keep time and backup registers; just adopt the running RTC */
    hrtc.Init.AsynchPrediv = RTC_PREDIV_A;
    hrtc.Init.SynchPrediv = (rtc_hz / 8) - 1;
    hrtc.Lock = HAL_UNLOCKED;
    hrtc.State = HAL_RTC_STATE_READY;
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  } else {
    rtc_clock_init();
    hrtc.Init.AsynchPrediv = RTC_PREDIV_A;
    hrtc.Init.SynchPrediv = (rtc_hz / 8) - 1;
    HAL_RTC_Init(&hrtc);
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {.WeekDay = RTC_WEEKDAY_MONDAY, .Month = 1, .Date = 1, .Year = 26};
    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
  }
  /* Read calendar directly (no shadow-register resync after every Stop) */
  HAL_RTCEx_EnableBypassShadow(&hrtc);
  boot_abs_ms = rtc_abs_ms();

  HAL_NVIC_SetPriority(RTC_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(RTC_IRQn);

  /* Watchdog: LSI/256, reload 4095 -> ~28 s nominal (>= 18 s worst case).
   * IWDG keeps running in Stop, so sleeps are chunked below that. */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
  hiwdg.Init.Reload = 4095;
  HAL_IWDG_Init(&hiwdg);

  /* Stop-mode configuration */
  HAL_PWREx_EnableUltraLowPower();   /* VREFINT off in Stop          */
  HAL_PWREx_EnableFastWakeUp();      /* don't wait for VREFINT on wake */
}

void lp_kick(void) { IWDG->KR = 0x0000AAAAu; /* harmless if the IWDG isn't running */ }

uint64_t lp_now_ms(void) { return rtc_abs_ms() - boot_abs_ms; }
uint32_t lp_abs_s(void) { return (uint32_t)(rtc_abs_ms() / 1000u); }

uint32_t lp_bkp_read(uint8_t idx) { return idx < 5 ? (&RTC->BKP0R)[idx] : 0; }
void lp_bkp_write(uint8_t idx, uint32_t v) {
  if (idx < 5) (&RTC->BKP0R)[idx] = v;
}

void lp_sleep_ms(uint32_t ms) {
  lp_kick();
  if (ms < 2) return;
  if (ms > LP_MAX_SLEEP_MS) ms = LP_MAX_SLEEP_MS;
  uint32_t ticks = (uint32_t)(((uint64_t)ms * (rtc_hz / 16)) / 1000u);
  if (ticks > 0xFFFF) ticks = 0xFFFF;
  if (ticks < 2) ticks = 2;

  uint64_t t_sleep = lp_now_ms();
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, ticks - 1, RTC_WAKEUPCLOCK_RTCCLK_DIV16);
  __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  HAL_ResumeTick();

  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  energy_slept((uint32_t)(lp_now_ms() - t_sleep));
  lp_kick();
}

static bool stop_inhibit;
void lp_inhibit_stop(bool on) { stop_inhibit = on; }

void lp_nap_ms(uint32_t ms) {
  if (stop_inhibit) { /* UART to ESP active: HSI must keep running */
    HAL_Delay(ms + 1);
    return;
  }
  uint64_t end = lp_now_ms() + ms + 1;
  for (;;) {
    uint64_t now = lp_now_ms();
    if (now >= end) break;
    uint32_t left = (uint32_t)(end - now);
    if (left < 3) HAL_Delay(left); else lp_sleep_ms(left);
  }
}

void RTC_IRQHandler(void) { HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc); }
