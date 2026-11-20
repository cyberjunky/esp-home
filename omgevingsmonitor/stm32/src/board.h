/*
 * board.h - pin map and low-level board helpers for De Omgevingsmonitor.
 *
 * Pin map taken from the original CubeMX project (MJSGadget.ioc).
 * LEDs are active-low (pin low = LED on). The ESP32-C3 sits on its own
 * switched supply (Wireless_PSU_EN) with a power-good feedback line.
 */
#pragma once
#include "stm32l0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define FW_VERSION      "1.0.1"
#define FW_VERSION_NUM  101      /* reported to Home Assistant as fw=101 -> 1.0.1 */

/* --- ESP32-C3 ------------------------------------------------------------ */
#define ESP_EN_PORT        GPIOH
#define ESP_EN_PIN         GPIO_PIN_0       /* chip enable, active high          */
#define ESP_BOOT_PORT      GPIOH
#define ESP_BOOT_PIN       GPIO_PIN_1       /* GPIO9 strap: high = run, low = dl  */
#define ESP_PSU_EN_PORT    GPIOC
#define ESP_PSU_EN_PIN     GPIO_PIN_11      /* ESP supply enable                  */
#define ESP_PSU_PG_PORT    GPIOA
#define ESP_PSU_PG_PIN     GPIO_PIN_8       /* ESP supply power-good              */
/* LPUART1 <-> ESP UART0 (download + data port). RX/TX are swapped in HW config. */
#define ESP_UART_PORT      GPIOC
#define ESP_UART_PINS      (GPIO_PIN_0 | GPIO_PIN_1)

/* --- 5 V boost (SEN5x supply) ------------------------------------------- */
#define BOOST_EN_PORT      GPIOC
#define BOOST_EN_PIN       GPIO_PIN_12

/* --- Power sensing ------------------------------------------------------- */
#define VUSB_PORT          GPIOA
#define VUSB_PIN           GPIO_PIN_7       /* high when USB 5 V present          */
#define CHG_STAT_PORT      GPIOC
#define CHG_STAT_PIN       GPIO_PIN_9       /* MCP73831 STAT (tri-state)          */
#define VBAT_ADC_CH        ADC_CHANNEL_14   /* PC4, divider 1:2                    */
#define VSOL_ADC_CH        ADC_CHANNEL_15   /* PC5, divider 1:3                    */

/* --- Buttons ------------------------------------------------------------- */
#define BTN_USER_PORT      GPIOD
#define BTN_USER_PIN       GPIO_PIN_2       /* active low, internal pull-up       */

/* --- Status LED "C" (other two RGB LEDs are left unused / off) ----------- */
#define LED_R_PORT         GPIOB
#define LED_R_PIN          GPIO_PIN_0
#define LED_G_PORT         GPIOB
#define LED_G_PIN          GPIO_PIN_8
#define LED_B_PORT         GPIOB
#define LED_B_PIN          GPIO_PIN_9

/* --- Debug log UART (USART1 TX on PA9) ----------------------------------- */
#define LOG_UART_PORT      GPIOA
#define LOG_UART_TX_PIN    GPIO_PIN_9

enum led_color {
  LED_OFF = 0, LED_RED = 1, LED_GREEN = 2, LED_YELLOW = 3,
  LED_BLUE = 4, LED_MAGENTA = 5, LED_CYAN = 6, LED_WHITE = 7
};

void board_clock_init(void);        /* HSI16, Range 2, wake from Stop on HSI16 */
void board_clock_usb(void);         /* Range 1 + HSI48/CRS for the USB bridge   */
void board_gpio_init(void);         /* park every pin in its lowest-power state */
void board_led(enum led_color c);
void board_blink(enum led_color c, uint32_t ms);
bool board_button_pressed(void);
bool board_usb_present(void);
void board_boost(bool on);

/* button / VUSB edge flags, set from EXTI ISR, cleared by reader */
extern volatile bool g_button_irq;
extern volatile bool g_vusb_irq;
