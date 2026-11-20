/*
 * link.c - reporting session with the ESP32-C3 running ESPHome.
 *
 *   STM32                                ESP32-C3 (ESPHome)
 *   power on ---------------------------> boots (~0.3 s), joins Wi-Fi
 *                  <-------------------- $H*crc        (hello, every 250 ms)
 *   $D,k=v,...*crc ---------------------> publishes sensors to HA
 *                  <-------------------- $C,ib=..,iu=..,mt=..*crc (HA settings)
 *                  <-------------------- $Z*crc        (HA got the data)
 *   power off
 *
 * Lines: '$' TYPE [',' key '=' int]* '*' CRC16-CCITT(hex4) '\n'
 * Everything else on the wire (ROM boot log etc.) is ignored.
 */
#include "link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp.h"
#include "lowpower.h"
#include "stm32l0xx_hal.h"

#define BAUD 115200u

extern void main_service(void); /* keeps sensor ticks running meanwhile */

static uint16_t crc16(const char *s, uint16_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= (uint16_t)(uint8_t)*s++ << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

uint16_t frame_begin(char *buf, uint16_t size, char type) {
  return (uint16_t)snprintf(buf, size, "$%c", type);
}

uint16_t frame_add(char *buf, uint16_t size, uint16_t pos, const char *key, int32_t value) {
  if (pos >= size) return pos;
  return (uint16_t)(pos + snprintf(buf + pos, size - pos, ",%s=%ld", key, (long)value));
}

uint16_t frame_end(char *buf, uint16_t size, uint16_t pos) {
  if (pos + 7 >= size) pos = (uint16_t)(size - 8);
  uint16_t crc = crc16(buf + 1, (uint16_t)(pos - 1));
  return (uint16_t)(pos + snprintf(buf + pos, size - pos, "*%04X\n", crc));
}

/* --- line receiver ------------------------------------------------------ */
static char line[96];
static uint8_t line_len;
static bool in_line;

static bool poll_line(void) {
  int c;
  while ((c = esp_uart_getc()) >= 0) {
    if (c == '$') {
      in_line = true;
      line_len = 0;
    } else if (in_line && (c == '\n' || c == '\r')) {
      in_line = false;
      line[line_len] = 0;
      return true;
    } else if (in_line) {
      if (line_len < sizeof line - 1) line[line_len++] = (char)c;
      else in_line = false;
    }
  }
  return false;
}

/* validates "TYPE...*CRC"; returns pointer to payload start or NULL */
static char *check_line(void) {
  char *star = strrchr(line, '*');
  if (!star || strlen(star) != 5) return NULL;
  uint16_t want = (uint16_t)strtoul(star + 1, NULL, 16);
  if (crc16(line, (uint16_t)(star - line)) != want) return NULL;
  *star = 0;
  return line;
}

static void parse_config(char *p, struct link_config *cfg) {
  /* p = "C,ib=600,iu=60,mt=0" */
  char *tok = strtok(p + 1, ",");
  while (tok) {
    char *eq = strchr(tok, '=');
    if (eq) {
      *eq = 0;
      long v = strtol(eq + 1, NULL, 10);
      if (!strcmp(tok, "ib") && v >= 60 && v <= 21600) {
        cfg->have_interval_batt = true;
        cfg->interval_batt_s = (uint16_t)v;
      } else if (!strcmp(tok, "iu") && v >= 10 && v <= 21600) {
        cfg->have_interval_usb = true;
        cfg->interval_usb_s = (uint16_t)v;
      } else if (!strcmp(tok, "mt") && v >= 0 && v <= 3600) {
        cfg->maintenance_s = (uint16_t)v;
      }
    }
    tok = strtok(NULL, ",");
  }
}

enum link_result link_session(frame_fn make_frame, uint32_t refresh_ms, uint16_t timeout_s,
                              uint16_t local_maint_s, struct link_config *cfg) {
  static char frame[320];
  memset(cfg, 0, sizeof *cfg);

  lp_inhibit_stop(true);
  esp_power_on(false);
  esp_uart_open(BAUD);
  in_line = false;

  uint32_t t0 = HAL_GetTick();
  uint32_t deadline = t0 + timeout_s * 1000u;
  uint32_t keep_until = t0 + local_maint_s * 1000u;
  uint32_t last_frame = 0;
  bool heard = false, sent = false, done = false;

  for (;;) {
    lp_kick();
    main_service();
    uint32_t now = HAL_GetTick();

    while (poll_line()) {
      char *p = check_line();
      if (!p) continue;
      heard = true;
      if (p[0] == 'H') { /* hello: (re)send current data */
        uint16_t n = make_frame(frame, sizeof frame);
        esp_uart_write(frame, n);
        sent = true;
        last_frame = now;
      } else if (p[0] == 'C') {
        parse_config(p, cfg);
        uint32_t until = now + cfg->maintenance_s * 1000u;
        if (cfg->maintenance_s && (int32_t)(until - keep_until) > 0) keep_until = until;
        if (!cfg->maintenance_s && !local_maint_s) keep_until = now;
      } else if (p[0] == 'Z' && sent) {
        done = true;
      }
    }

    bool maint = (int32_t)(keep_until - now) > 0;
    if (maint && sent && refresh_ms && now - last_frame >= refresh_ms) {
      uint16_t n = make_frame(frame, sizeof frame);
      esp_uart_write(frame, n);
      last_frame = now;
    }
    if (done && !maint) break;
    if (!maint && (int32_t)(now - deadline) > 0) break;
    __WFI();
  }

  HAL_Delay(done ? 150 : 0); /* let ESPHome close the API connection cleanly */
  esp_power_off();
  lp_inhibit_stop(false);
  if (done) return LINK_DONE;
  return heard ? LINK_TIMEOUT : LINK_NO_ESP;
}
