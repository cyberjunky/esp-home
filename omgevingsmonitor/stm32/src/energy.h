/*
 * energy.h - coulomb estimate from measured on-times.
 *
 * There is no current sensor on the board. Instead every consumer's on-time
 * is measured and multiplied by its typical current. That gives the average
 * consumption (mAh/day) and the battery runtime without any sun.
 */
#pragma once
#include <stdint.h>

#ifndef BATTERY_MAH
#define BATTERY_MAH 2000
#endif

/* typical currents in uA - tune I_SLEEP_UA after measuring the board */
#define I_SLEEP_UA    60u       /* Stop mode: MCU, LDO, SGP40 idle, dividers */
#define I_RUN_UA      1800u     /* MCU running at 16 MHz                       */
#define I_ESP_UA      80000u    /* ESP32-C3 average while connecting/reporting */
#define I_HEATER_UA   3500u     /* SGP40 hotplate                              */
#define I_MIC_UA      600u      /* ICS-43434 while clocked                     */
#define I_BOOST_UA    110000u   /* SEN5x via 5 V boost, seen from the battery  */

void energy_add(uint32_t current_ua, uint32_t ms);   /* extra consumer on-time */
void energy_slept(uint32_t ms);                      /* time spent in Stop     */

/* Called once per report: closes the period, updates the 24 h average.
 * Returns consumption in mAh/day x10 (0 until the first period closed). */
uint16_t energy_update(void);
uint16_t energy_mah_day_10(void);
/* Runtime without sun for the given state of charge, in days x10 (capped). */
uint16_t energy_runtime_days_10(uint8_t soc);
