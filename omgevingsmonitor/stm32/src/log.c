/*
 * log.c - optional debug log on USART1 TX (PA9). Zero idle cost: the UART
 * clock and pin are only enabled while a line is transmitted.
 */
#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include "board.h"

#if LOG_LEVEL > 0
void log_printf(const char *fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof buf - 3) n = sizeof buf - 3;
  buf[n++] = '\r';
  buf[n++] = '\n';

  __HAL_RCC_USART1_CLK_ENABLE();
  GPIO_InitTypeDef g = {.Pin = LOG_UART_TX_PIN, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
                        .Speed = GPIO_SPEED_FREQ_LOW, .Alternate = GPIO_AF4_USART1};
  HAL_GPIO_Init(LOG_UART_PORT, &g);
  USART1->CR1 = 0;
  USART1->BRR = (HAL_RCC_GetPCLK2Freq() + 57600) / 115200;
  USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
  for (int i = 0; i < n; i++) {
    while (!(USART1->ISR & USART_ISR_TXE)) {}
    USART1->TDR = (uint8_t)buf[i];
  }
  while (!(USART1->ISR & USART_ISR_TC)) {}
  USART1->CR1 = 0;
  __HAL_RCC_USART1_CLK_DISABLE();
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(LOG_UART_PORT, &g);
}
#else
void log_printf(const char *fmt, ...) { (void)fmt; }
#endif
