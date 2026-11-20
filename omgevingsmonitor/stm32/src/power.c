/*
 * power.c - battery / solar / USB / charger measurement.
 *
 * The ADC is referenced against VREFINT (factory calibrated at 3.0 V), so
 * readings are independent of the exact rail voltage. Hardware oversampling
 * x16 averages out noise from the high-impedance dividers. The ADC is
 * powered only for the ~1 ms a measurement takes.
 */
#include "power.h"
#include "board.h"
#include "stm32l0xx_hal.h"

/* Divider ratios on the board (from the original firmware) */
#ifndef VBAT_DIVIDER
#define VBAT_DIVIDER 2
#endif
#ifndef VSOL_DIVIDER
#define VSOL_DIVIDER 3
#endif

#define VREFINT_CAL_ADDR ((uint16_t *)0x1FF80078u) /* measured at VDDA = 3.0 V */

static ADC_HandleTypeDef hadc;

static uint32_t adc_read(uint32_t channel) {
  ADC_ChannelConfTypeDef ch = {.Channel = channel, .Rank = ADC_RANK_CHANNEL_NUMBER};
  HAL_ADC_ConfigChannel(&hadc, &ch);
  HAL_ADC_Start(&hadc);
  uint32_t v = 0;
  if (HAL_ADC_PollForConversion(&hadc, 10) == HAL_OK) v = HAL_ADC_GetValue(&hadc);
  HAL_ADC_Stop(&hadc);
  ch.Rank = ADC_RANK_NONE;
  HAL_ADC_ConfigChannel(&hadc, &ch);
  return v;
}

static uint8_t charger_state(void) {
  /* MCP73831 STAT is tri-state: low = charging, high = done, Hi-Z = no input.
   * Probe with pull-up, then pull-down; only a driven pin reads the same. */
  GPIO_InitTypeDef g = {.Pin = CHG_STAT_PIN, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_PULLUP};
  HAL_GPIO_Init(CHG_STAT_PORT, &g);
  for (volatile int i = 0; i < 200; i++) {}
  GPIO_PinState up = HAL_GPIO_ReadPin(CHG_STAT_PORT, CHG_STAT_PIN);
  g.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(CHG_STAT_PORT, &g);
  for (volatile int i = 0; i < 200; i++) {}
  GPIO_PinState down = HAL_GPIO_ReadPin(CHG_STAT_PORT, CHG_STAT_PIN);
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(CHG_STAT_PORT, &g);

  if (up == GPIO_PIN_RESET && down == GPIO_PIN_RESET) return CHG_CHARGING;
  if (up == GPIO_PIN_SET && down == GPIO_PIN_SET) return CHG_FULL;
  return CHG_OFF;
}

/* Rest-voltage -> state of charge for a single Li-ion/LiPo cell */
static const struct { uint16_t mv; uint8_t pct; } soc_table[] = {
  {4180, 100}, {4100, 90}, {4020, 80}, {3950, 70}, {3870, 60}, {3840, 50},
  {3800, 40}, {3770, 30}, {3730, 20}, {3690, 10}, {3610, 5}, {3300, 0},
};

static uint8_t soc_from_mv(uint16_t mv) {
  if (mv >= soc_table[0].mv) return 100;
  for (unsigned i = 1; i < sizeof soc_table / sizeof soc_table[0]; i++) {
    if (mv >= soc_table[i].mv) {
      uint32_t span_mv = soc_table[i - 1].mv - soc_table[i].mv;
      uint32_t span_pct = soc_table[i - 1].pct - soc_table[i].pct;
      return (uint8_t)(soc_table[i].pct + (mv - soc_table[i].mv) * span_pct / span_mv);
    }
  }
  return 0;
}

void power_measure(struct power_status *p) {
  __HAL_RCC_ADC1_CLK_ENABLE();
  hadc.Instance = ADC1;
  hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc.Init.Resolution = ADC_RESOLUTION_12B;
  hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
  hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc.Init.LowPowerAutoWait = DISABLE;
  hadc.Init.LowPowerAutoPowerOff = DISABLE;
  hadc.Init.ContinuousConvMode = DISABLE;
  hadc.Init.DiscontinuousConvMode = DISABLE;
  hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc.Init.DMAContinuousRequests = DISABLE;
  hadc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc.Init.SamplingTime = ADC_SAMPLETIME_160CYCLES_5;
  hadc.Init.OversamplingMode = ENABLE;
  hadc.Init.Oversample.Ratio = ADC_OVERSAMPLING_RATIO_16;
  hadc.Init.Oversample.RightBitShift = ADC_RIGHTBITSHIFT_4;
  hadc.Init.Oversample.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
  hadc.Init.LowPowerFrequencyMode = DISABLE;
  HAL_ADC_Init(&hadc);
  HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED);

  /* VREFINT is off in Stop (ULP) - wait for it to come back */
  uint32_t t0 = HAL_GetTick();
  while (!(PWR->CSR & PWR_CSR_VREFINTRDYF) && HAL_GetTick() - t0 < 5) {}

  uint32_t vref = adc_read(ADC_CHANNEL_VREFINT);
  uint32_t vdda = vref ? (3000u * (*VREFINT_CAL_ADDR)) / vref : 3000u;
  uint32_t bat = adc_read(VBAT_ADC_CH);
  uint32_t sol = adc_read(VSOL_ADC_CH);

  HAL_ADC_DeInit(&hadc);
  ADC->CCR &= ~(ADC_CCR_VREFEN | ADC_CCR_TSEN);
  __HAL_RCC_ADC1_CLK_DISABLE();

  p->vdda_mv = (uint16_t)vdda;
  p->vbat_mv = (uint16_t)(bat * vdda * VBAT_DIVIDER / 4095u);
  p->vsolar_mv = (uint16_t)(sol * vdda * VSOL_DIVIDER / 4095u);
  p->usb = board_usb_present();
  p->charge = charger_state();
  p->soc = soc_from_mv(p->vbat_mv);
}
