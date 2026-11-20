/*
 * stm32l0xx_it.c - core exception and EXTI handlers.
 * Peripheral handlers live with their drivers (sound.c, esp.c, bridge.c,
 * lowpower.c).
 */
#include "stm32l0xx_hal.h"

void NMI_Handler(void) {}
void HardFault_Handler(void) { NVIC_SystemReset(); }
void SVC_Handler(void) {}
void PendSV_Handler(void) {}
void SysTick_Handler(void) { HAL_IncTick(); }

void EXTI2_3_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2); }
void EXTI4_15_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7); }
