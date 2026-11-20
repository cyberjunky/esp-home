/*
 * link.h - one reporting session with the ESPHome firmware on the ESP32-C3.
 * Wire protocol: docs/protocol.md
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct link_config {          /* received from Home Assistant via the ESP */
  bool have_interval_batt;  uint16_t interval_batt_s;
  bool have_interval_usb;   uint16_t interval_usb_s;
  uint16_t maintenance_s;     /* keep ESP powered this long (OTA / logs) */
};

enum link_result { LINK_DONE = 0, LINK_TIMEOUT = 1, LINK_NO_ESP = 2 };

/* frame builder: called on start and (in maintenance) for every refresh */
typedef uint16_t (*frame_fn)(char *buf, uint16_t size);

/* Power the ESP, deliver frames, collect config, power it down again.
 * local_maint_s > 0 keeps the ESP awake regardless of what HA says. */
enum link_result link_session(frame_fn make_frame, uint32_t refresh_ms, uint16_t timeout_s,
                              uint16_t local_maint_s, struct link_config *cfg);

/* helpers for building frames */
uint16_t frame_begin(char *buf, uint16_t size, char type);
uint16_t frame_add(char *buf, uint16_t size, uint16_t pos, const char *key, int32_t value);
uint16_t frame_end(char *buf, uint16_t size, uint16_t pos);
