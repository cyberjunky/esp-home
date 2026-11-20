/*
 * esp.c - ESP32-C3 power control and UART.
 *
 * The ESP has its own switched supply. When it is off, every line towards
 * it (EN, GPIO9, UART) is set to analog so nothing back-powers the module.
 */
#include "esp.h"
#include "board.h"
#include "lowpower.h"
#include "stm32l0xx_hal.h"

static UART_HandleTypeDef hlp;
static uint8_t rx_ring[1024];
static volatile uint16_t rx_head, rx_tail;
static bool uart_open;

static void out(GPIO_TypeDef *port, uint16_t pin, bool level) {
  HAL_GPIO_WritePin(port, pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
  GPIO_InitTypeDef g = {.Pin = pin, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                        .Speed = GPIO_SPEED_FREQ_LOW};
  HAL_GPIO_Init(port, &g);
}

static void analog(GPIO_TypeDef *port, uint16_t pin) {
  GPIO_InitTypeDef g = {.Pin = pin, .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL};
  HAL_GPIO_Init(port, &g);
}

void esp_set_lines(bool en, bool boot) {
  out(ESP_BOOT_PORT, ESP_BOOT_PIN, boot);
  out(ESP_EN_PORT, ESP_EN_PIN, en);
}

void esp_power_on(bool download_mode) {
  HAL_GPIO_WritePin(ESP_PSU_EN_PORT, ESP_PSU_EN_PIN, GPIO_PIN_SET);
  uint32_t t0 = HAL_GetTick();
  while (HAL_GPIO_ReadPin(ESP_PSU_PG_PORT, ESP_PSU_PG_PIN) == GPIO_PIN_RESET && HAL_GetTick() - t0 < 20) {
  }
  HAL_Delay(2);
  esp_set_lines(false, !download_mode); /* EN low, strap GPIO9 */
  HAL_Delay(5);
  out(ESP_EN_PORT, ESP_EN_PIN, true);    /* release reset */
  if (download_mode) {
    HAL_Delay(50);
    out(ESP_BOOT_PORT, ESP_BOOT_PIN, true);
  }
}

void esp_power_off(void) {
  esp_uart_close();
  out(ESP_EN_PORT, ESP_EN_PIN, false);
  HAL_GPIO_WritePin(ESP_PSU_EN_PORT, ESP_PSU_EN_PIN, GPIO_PIN_RESET);
  analog(ESP_BOOT_PORT, ESP_BOOT_PIN);
  analog(ESP_EN_PORT, ESP_EN_PIN);
}

void esp_uart_open(uint32_t baud) {
  if (uart_open) { /* baud change: start from a clean peripheral */
    HAL_NVIC_DisableIRQ(RNG_LPUART1_IRQn);
    HAL_UART_DeInit(&hlp);
    uart_open = false;
  }
  __HAL_RCC_LPUART1_CONFIG(RCC_LPUART1CLKSOURCE_HSI);
  __HAL_RCC_LPUART1_CLK_ENABLE();
  GPIO_InitTypeDef g = {.Pin = ESP_UART_PINS, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
                        .Speed = GPIO_SPEED_FREQ_HIGH, .Alternate = GPIO_AF6_LPUART1};
  HAL_GPIO_Init(ESP_UART_PORT, &g);

  hlp.Instance = LPUART1;
  hlp.Init.BaudRate = baud;
  hlp.Init.WordLength = UART_WORDLENGTH_8B;
  hlp.Init.StopBits = UART_STOPBITS_1;
  hlp.Init.Parity = UART_PARITY_NONE;
  hlp.Init.Mode = UART_MODE_TX_RX;
  hlp.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlp.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  /* board wiring needs RX/TX swapped (as in the original firmware) */
  hlp.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT | UART_ADVFEATURE_RXOVERRUNDISABLE_INIT;
  hlp.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_ENABLE;
  hlp.AdvancedInit.OverrunDisable = UART_ADVFEATURE_OVERRUN_DISABLE;
  HAL_UART_Init(&hlp);

  rx_head = rx_tail = 0;
  __HAL_UART_ENABLE_IT(&hlp, UART_IT_RXNE);
  HAL_NVIC_SetPriority(RNG_LPUART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(RNG_LPUART1_IRQn);
  uart_open = true;
}

void esp_uart_close(void) {
  if (uart_open) {
    HAL_NVIC_DisableIRQ(RNG_LPUART1_IRQn);
    HAL_UART_DeInit(&hlp);
    __HAL_RCC_LPUART1_CLK_DISABLE();
    uart_open = false;
  }
  analog(ESP_UART_PORT, ESP_UART_PINS);
}

void HAL_UART_MspInit(UART_HandleTypeDef *h) { (void)h; }
void HAL_UART_MspDeInit(UART_HandleTypeDef *h) { (void)h; }

void RNG_LPUART1_IRQHandler(void) {
  uint32_t isr = LPUART1->ISR;
  if (isr & USART_ISR_RXNE) {
    uint8_t c = (uint8_t)LPUART1->RDR;
    uint16_t next = (uint16_t)((rx_head + 1) % sizeof rx_ring);
    if (next != rx_tail) {
      rx_ring[rx_head] = c;
      rx_head = next;
    }
  }
  if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
    LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF | USART_ICR_PECF;
  }
}

void esp_uart_write(const void *data, uint16_t len) {
  /* register-level TX: no HAL state machine that can get stuck in BUSY */
  if (!uart_open) return;
  const uint8_t *p = data;
  for (uint16_t i = 0; i < len; i++) {
    uint32_t t0 = HAL_GetTick();
    while (!(LPUART1->ISR & USART_ISR_TXE)) {
      if (HAL_GetTick() - t0 > 5) return;
    }
    LPUART1->TDR = p[i];
  }
}

int esp_uart_getc(void) {
  if (rx_tail == rx_head) return -1;
  uint8_t c = rx_ring[rx_tail];
  rx_tail = (uint16_t)((rx_tail + 1) % sizeof rx_ring);
  return c;
}

uint16_t esp_uart_available(void) {
  return (uint16_t)((rx_head + sizeof rx_ring - rx_tail) % sizeof rx_ring);
}
