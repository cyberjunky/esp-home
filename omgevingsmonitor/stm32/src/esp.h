/*
 * esp.h - ESP32-C3 power control and its UART (LPUART1 <-> ESP UART0).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void esp_power_on(bool download_mode);
void esp_power_off(void);
void esp_set_lines(bool en, bool boot);      /* raw EN / GPIO9 control (bridge) */

void esp_uart_open(uint32_t baud);
void esp_uart_close(void);
void esp_uart_write(const void *data, uint16_t len);
int esp_uart_getc(void);                     /* -1 when empty */
uint16_t esp_uart_available(void);
