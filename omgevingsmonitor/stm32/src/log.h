/*
 * log.h - optional debug log on USART1 TX (PA9, 115200 8N1).
 * The UART is powered only while a line is being sent.
 * LOG_LEVEL: 0 off, 1 errors, 2 info, 3 debug.
 */
#pragma once

#ifndef LOG_LEVEL
#define LOG_LEVEL 0
#endif

void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#if LOG_LEVEL >= 1
#define LOGE(...) log_printf("E " __VA_ARGS__)
#else
#define LOGE(...) do {} while (0)
#endif
#if LOG_LEVEL >= 2
#define LOGI(...) log_printf("I " __VA_ARGS__)
#else
#define LOGI(...) do {} while (0)
#endif
#if LOG_LEVEL >= 3
#define LOGD(...) log_printf("D " __VA_ARGS__)
#else
#define LOGD(...) do {} while (0)
#endif
