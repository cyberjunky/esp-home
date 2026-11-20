/*
 * sound.h - A-weighted sound level from the ICS-43434 I2S microphone.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Run one measurement (~settle + MEASURE_MS). Returns false if the mic gave
 * no signal. Results in dB(A) x10: equivalent level and fast (125 ms) max. */
bool sound_measure(uint16_t *laeq_10, uint16_t *lamax_10);
