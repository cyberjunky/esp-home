/*
 * lowpower.c - RTC time base, Stop-mode sleep and watchdog.
 *
 * Stop mode with low-power regulator, ULP (VREFINT off) and RTC wake-up
 * timer: the STM32L072 itself draws ~1 uA here. The RTC calendar doubles
 * as a monotonic clock because SysTick does not run in Stop.
 */
#include "lowpower.h"
#include "board.h"
#include "stm32l0xx_hal.h"

RTC_HandleTypeDef hrtc;
static IWDG_HandleTypeDef hiwdg;
static bool lse_ok;
static uint32_t rtc_hz = 32768;     /* RTCCLK */
static uint8_t reset_cause;

static uint32_t day_ms_last;
static uint64_t day_base_ms;

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

static void rtc_clock_init(void) {
  HAL_PWR_EnableBkUpAccess();
  /* Fresh backup domain on every boot: we don't keep calendar time across
   * resets, and it lets us pick the RTC clock source freely. */
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

void lp_init(void) {
  capture_reset_cause();
  rtc_clock_init();

  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 7;                       /* 32768/8 = 4096 Hz: 244 us resolution */
  hrtc.Init.SynchPrediv = (rtc_hz / 8) - 1;         /* -> 1 Hz                              */
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  HAL_RTC_Init(&hrtc);
  RTC_TimeTypeDef t = {0};
  RTC_DateTypeDef d = {.WeekDay = RTC_WEEKDAY_MONDAY, .Month = 1, .Date = 1, .Year = 26};
  HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
  HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
  /* Read calendar directly (no shadow-register resync after every Stop) */
  HAL_RTCEx_EnableBypassShadow(&hrtc);

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

static uint32_t rtc_day_ms(void) {
  uint32_t ssr, tr, ssr2, tr2;
  do { /* bypass-shadow reads must be done twice for consistency */
    ssr = RTC->SSR;
    tr = RTC->TR;
    ssr2 = RTC->SSR;
    tr2 = RTC->TR;
  } while (ssr != ssr2 || tr != tr2);
  uint32_t h = ((tr >> 20) & 0x3) * 10 + ((tr >> 16) & 0xF);
  uint32_t m = ((tr >> 12) & 0x7) * 10 + ((tr >> 8) & 0xF);
  uint32_t s = ((tr >> 4) & 0x7) * 10 + (tr & 0xF);
  uint32_t prediv_s = (RTC->PRER & RTC_PRER_PREDIV_S) + 1;
  uint32_t sub_ms = ((prediv_s - 1 - ssr) * 1000u) / prediv_s;
  return ((h * 60 + m) * 60 + s) * 1000u + sub_ms;
}

uint64_t lp_now_ms(void) {
  uint32_t ms = rtc_day_ms();
  if (ms < day_ms_last) day_base_ms += 86400000ull; /* midnight roll-over */
  day_ms_last = ms;
  return day_base_ms + ms;
}

void lp_sleep_ms(uint32_t ms) {
  lp_kick();
  if (ms < 2) return;
  if (ms > LP_MAX_SLEEP_MS) ms = LP_MAX_SLEEP_MS;
  uint32_t ticks = (uint32_t)(((uint64_t)ms * (rtc_hz / 16)) / 1000u);
  if (ticks > 0xFFFF) ticks = 0xFFFF;
  if (ticks < 2) ticks = 2;

  lp_now_ms(); /* keep roll-over tracking fresh */
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, ticks - 1, RTC_WAKEUPCLOCK_RTCCLK_DIV16);
  __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  HAL_ResumeTick();

  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
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
