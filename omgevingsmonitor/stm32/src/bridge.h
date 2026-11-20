/*
 * bridge.h - USB CDC <-> ESP32-C3 UART bridge (hold user button at reset).
 */
#pragma once

void bridge_run(void) __attribute__((noreturn));
