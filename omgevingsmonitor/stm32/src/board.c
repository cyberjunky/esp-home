/*
 * board.c - clocks, pin parking, LED, buttons.
 *
 * Power rule: every pin that is not actively needed is ANALOG. That is the
 * lowest-leakage state, and it guarantees we never back-power the switched
 * ESP32 or SEN5x rails through their I/O pins (a likely cause of the ~2.3 mA
 * standby current seen with the original firmware).
 */
#include "board.h"

volatile bool g_button_irq;
volatile bool g_vusb_irq;

static void wait_vos(void) {
  while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOS)) {
  }
}

void board_clock_init(void) {
  __HAL_RCC_PWR_CLK_ENABLE();

  /* HSI16 first (we may come from MSI after reset) */
  RCC_OscInitTypeDef osc = {0};
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState = RCC_HSI_ON;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) NVIC_SystemReset();

  RCC_ClkInitTypeDef clk = {0};
  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) NVIC_SystemReset();

  /* Range 2 (1.5 V core) is enough for 16 MHz with 1 wait state */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
  wait_vos();

  __HAL_RCC_MSI_DISABLE();
  /* After Stop, resume directly on HSI16 - no clock re-init needed */
  __HAL_RCC_WAKEUPSTOP_CLK_CONFIG(RCC_STOP_WAKEUPCLOCK_HSI);

#ifndef KEEP_DEBUG_IN_STOP
  /* A debugger session leaves DBG_STOP set until power-on reset, which keeps
   * clocks alive in Stop (mA instead of uA). Always clear it. */
  __HAL_RCC_DBGMCU_CLK_ENABLE();
  DBGMCU->CR = 0;
  __HAL_RCC_DBGMCU_CLK_DISABLE();
#endif
}

void board_clock_usb(void) {
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1); /* USB needs Range 1 */
  wait_vos();

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  RCC_OscInitTypeDef osc = {0};
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  osc.HSI48State = RCC_HSI48_ON;
  osc.PLL.PLLState = RCC_PLL_NONE;
  HAL_RCC_OscConfig(&osc);

  RCC_PeriphCLKInitTypeDef pclk = {0};
  pclk.PeriphClockSelection = RCC_PERIPHCLK_USB;
  pclk.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  HAL_RCCEx_PeriphCLKConfig(&pclk);

  /* Trim HSI48 from USB SOF packets - no crystal needed */
  __HAL_RCC_CRS_CLK_ENABLE();
  RCC_CRSInitTypeDef crs = {0};
  crs.Prescaler = RCC_CRS_SYNC_DIV1;
  crs.Source = RCC_CRS_SYNC_SOURCE_USB;
  crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
  crs.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
  crs.HSI48CalibrationValue = 0x20;
  HAL_RCCEx_CRSConfig(&crs);
}

static void pin_out(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level) {
  HAL_GPIO_WritePin(port, pin, level);
  GPIO_InitTypeDef g = {.Pin = pin, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                        .Speed = GPIO_SPEED_FREQ_LOW};
  HAL_GPIO_Init(port, &g);
}

static void pin_analog(GPIO_TypeDef *port, uint16_t pin) {
  GPIO_InitTypeDef g = {.Pin = pin, .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL};
  HAL_GPIO_Init(port, &g);
}

void board_gpio_init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  /* Everything analog, except SWD (PA13/PA14, AF0). LSE pins PC14/15 are
   * owned by the oscillator regardless of MODER. */
  GPIOA->MODER = 0xEBFFFFFFu;
  GPIOB->MODER = 0xFFFFFFFFu;
  GPIOC->MODER = 0xFFFFFFFFu;
  GPIOD->MODER = 0xFFFFFFFFu;
  GPIOH->MODER = 0xFFFFFFFFu;
  GPIOA->PUPDR = 0x24000000u; /* SWD defaults: PA13 pull-up, PA14 pull-down */
  GPIOB->PUPDR = 0;
  GPIOC->PUPDR = 0;
  GPIOD->PUPDR = 0;
  GPIOH->PUPDR = 0;

  /* Switched rails firmly off */
  pin_out(ESP_PSU_EN_PORT, ESP_PSU_EN_PIN, GPIO_PIN_RESET);
  pin_out(BOOST_EN_PORT, BOOST_EN_PIN, GPIO_PIN_RESET);

  /* Microphone clocks held low so the ICS-43434 stays in power-down */
  pin_out(GPIOB, GPIO_PIN_12 | GPIO_PIN_13, GPIO_PIN_RESET);

  /* User button: pull-up, wake on press */
  GPIO_InitTypeDef g = {0};
  g.Pin = BTN_USER_PIN;
  g.Mode = GPIO_MODE_IT_FALLING;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_USER_PORT, &g);

  /* USB 5 V detect: wake on plug / unplug */
  g.Pin = VUSB_PIN;
  g.Mode = GPIO_MODE_IT_RISING_FALLING;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(VUSB_PORT, &g);

  HAL_NVIC_SetPriority(EXTI2_3_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin) {
  if (pin == BTN_USER_PIN) g_button_irq = true;
  if (pin == VUSB_PIN) g_vusb_irq = true;
}

static void led_pin(GPIO_TypeDef *port, uint16_t pin, bool on) {
  if (on)
    pin_out(port, pin, GPIO_PIN_RESET); /* active low */
  else
    pin_analog(port, pin);
}

void board_led(enum led_color c) {
  led_pin(LED_R_PORT, LED_R_PIN, c & 1);
  led_pin(LED_G_PORT, LED_G_PIN, c & 2);
  led_pin(LED_B_PORT, LED_B_PIN, c & 4);
}

void board_blink(enum led_color c, uint32_t ms) {
  board_led(c);
  HAL_Delay(ms);
  board_led(LED_OFF);
}

bool board_button_pressed(void) {
  return HAL_GPIO_ReadPin(BTN_USER_PORT, BTN_USER_PIN) == GPIO_PIN_RESET;
}

bool board_usb_present(void) {
  return HAL_GPIO_ReadPin(VUSB_PORT, VUSB_PIN) == GPIO_PIN_SET;
}

void board_boost(bool on) {
  HAL_GPIO_WritePin(BOOST_EN_PORT, BOOST_EN_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
