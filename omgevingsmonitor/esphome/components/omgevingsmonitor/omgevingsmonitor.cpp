#include "omgevingsmonitor.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

namespace esphome {
namespace omgevingsmonitor {

static const char *const TAG = "omgevingsmonitor";

static const uint32_t HELLO_INTERVAL_MS = 250;
static const uint32_t DONE_REPEAT_MS = 500;
static const uint32_t MAINT_REFRESH_MS = 60000;
static const uint16_t MAINT_SECONDS = 900;
static const uint32_t CLOSE_TIMEOUT_MS = 1500;

static uint16_t crc16(const char *s, size_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= static_cast<uint16_t>(static_cast<uint8_t>(*s++)) << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

void OmgevingsmonitorComponent::setup() {
  // Nothing to initialise: the STM32 decides when we run. Start saying hello.
  this->last_hello_ = 0;
}

void OmgevingsmonitorComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Omgevingsmonitor (STM32 link)");
  ESP_LOGCONFIG(TAG, "  HA settle time: %u ms", (unsigned) this->ha_settle_ms_);
#ifdef USE_SENSOR
  ESP_LOGCONFIG(TAG, "  Sensors configured: %u", (unsigned) this->sensors_.size());
#endif
}

void OmgevingsmonitorComponent::send_(const char *body) {
  char buf[96];
  uint16_t crc = crc16(body, strlen(body));
  snprintf(buf, sizeof buf, "$%s*%04X\n", body, crc);
  this->write_str(buf);
}

bool OmgevingsmonitorComponent::ha_connected_() {
#ifdef USE_API
  return api::global_api_server != nullptr && api::global_api_server->is_connected_with_state_subscription();
#else
  return false;
#endif
}

bool OmgevingsmonitorComponent::ha_wants_maintenance_() {
#ifdef USE_BINARY_SENSOR
  return this->maint_src_ != nullptr && this->maint_src_->has_state() && this->maint_src_->state;
#else
  return false;
#endif
}

void OmgevingsmonitorComponent::loop() {
  uint8_t c;
  while (this->available() && this->read_byte(&c)) {
    if (c == '$') {
      this->in_line_ = true;
      this->line_len_ = 0;
    } else if (this->in_line_ && (c == '\n' || c == '\r')) {
      this->in_line_ = false;
      this->line_[this->line_len_] = 0;
      this->handle_line_();
    } else if (this->in_line_) {
      if (this->line_len_ < sizeof(this->line_) - 1)
        this->line_[this->line_len_++] = static_cast<char>(c);
      else
        this->in_line_ = false;
    }
  }

  const uint32_t now = millis();
  if (!this->got_data_) {
    if (now - this->last_hello_ >= HELLO_INTERVAL_MS) {
      this->send_("H");
      this->last_hello_ = now;
    }
    return;
  }

  if (this->closing_since_ != 0) {
    // Wait until Home Assistant acknowledged the disconnect, so it records an *expected*
    // disconnect and keeps our entities available while we are powered off.
    bool closed = true;
#ifdef USE_API
    closed = api::global_api_server == nullptr || !api::global_api_server->is_connected();
#endif
    if ((closed || now - this->closing_since_ >= CLOSE_TIMEOUT_MS) && now - this->last_done_ >= DONE_REPEAT_MS) {
      // Home Assistant has our states and knows we are going to sleep: switch us off.
      this->send_("Z");
      this->last_done_ = now;
    }
    return;
  }

  if (!this->ha_connected_()) {
    this->connected_at_ = 0;
    return;
  }
  if (this->connected_at_ == 0) this->connected_at_ = now ? now : 1;
  if (now - this->connected_at_ < this->ha_settle_ms_) return;

  const bool maint = this->ha_wants_maintenance_();
  if (!this->cfg_sent_ || maint != this->last_maint_ || (maint && now - this->last_cfg_ >= MAINT_REFRESH_MS)) {
    std::string body = "C";
#ifdef USE_SENSOR
    if (this->interval_batt_src_ != nullptr && this->interval_batt_src_->has_state() &&
        std::isfinite(this->interval_batt_src_->state))
      body += ",ib=" + std::to_string(static_cast<int>(lroundf(this->interval_batt_src_->state * 60.0f)));
    if (this->interval_usb_src_ != nullptr && this->interval_usb_src_->has_state() &&
        std::isfinite(this->interval_usb_src_->state))
      body += ",iu=" + std::to_string(static_cast<int>(lroundf(this->interval_usb_src_->state * 60.0f)));
#endif
    body += ",mt=" + std::to_string(maint ? MAINT_SECONDS : 0);
    this->send_(body.c_str());
    if (!this->cfg_sent_ || maint != this->last_maint_)
      ESP_LOGI(TAG, "Sent settings to STM32: %s", body.c_str());
    this->cfg_sent_ = true;
    this->last_maint_ = maint;
    this->last_cfg_ = now;
  }

  if (!maint) {
    // Home Assistant has our states. Close the API connection cleanly before the STM32 cuts
    // power: a dropped connection makes HA mark a deep-sleep device's entities unavailable.
    ESP_LOGI(TAG, "Report done, disconnecting from Home Assistant");
#ifdef USE_API
    if (api::global_api_server != nullptr) api::global_api_server->on_shutdown();
#endif
    this->closing_since_ = now ? now : 1;
  }
}

void OmgevingsmonitorComponent::handle_line_() {
  char *star = strrchr(this->line_, '*');
  if (star == nullptr || strlen(star) != 5) return;
  uint16_t want = static_cast<uint16_t>(strtoul(star + 1, nullptr, 16));
  if (crc16(this->line_, star - this->line_) != want) {
    ESP_LOGW(TAG, "CRC error on frame");
    return;
  }
  *star = 0;
  if (this->line_[0] == 'D') this->handle_data_(this->line_);
}

static const char *mode_name(int m) {
  switch (m) {
    case 0: return "Normal";
    case 1: return "Energy saving";
    case 2: return "Survival (battery empty)";
    case 3: return "USB powered";
    default: return "Unknown";
  }
}

static const char *charger_name(int c) {
  switch (c) {
    case 1: return "Charging";
    case 2: return "Full";
    default: return "Not charging";
  }
}

static const char *reset_name(int r) {
  static const char *const names[] = {"Power-on", "Reset pin", "Software", "Watchdog", "Low-power", "Other"};
  return (r >= 0 && r <= 5) ? names[r] : "Unknown";
}

static const char *reason_name(int r) {
  static const char *const names[] = {"Scheduled", "Boot", "Button", "Significant change",
                                      "USB plugged/unplugged", "Maintenance"};
  return (r >= 0 && r <= 5) ? names[r] : "Unknown";
}

static std::string present_list(uint32_t p) {
  static const char *const names[] = {"WSEN-HIDS", "SGP40", "ICS-43434", "AHT20",
                                      "BMP280",    "BME280", "ENS160",   "SEN5x"};
  std::string out;
  for (int i = 0; i < 8; i++) {
    if (p & (1u << i)) {
      if (!out.empty()) out += ", ";
      out += names[i];
    }
  }
  return out.empty() ? "none" : out;
}

void OmgevingsmonitorComponent::handle_data_(char *payload) {
  this->got_data_ = true;
  this->frames_++;
  int mode = -1;
  float t = NAN, h = NAN;
  ESP_LOGD(TAG, "Frame %u: %s", (unsigned) this->frames_, payload);

  char *save = nullptr;
  for (char *tok = strtok_r(payload + 1, ",", &save); tok != nullptr; tok = strtok_r(nullptr, ",", &save)) {
    char *eq = strchr(tok, '=');
    if (eq == nullptr) continue;
    *eq = 0;
    const char *key = tok;
    long v = strtol(eq + 1, nullptr, 10);

#ifdef USE_SENSOR
    for (auto &e : this->sensors_) {
      if (strcmp(e.key, key) == 0) e.sensor->publish_state(static_cast<float>(v) * e.scale);
    }
#endif
#ifdef USE_BINARY_SENSOR
    if (strcmp(key, "us") == 0 && this->usb_bs_) this->usb_bs_->publish_state(v != 0);
    if (strcmp(key, "cs") == 0 && this->charging_bs_) this->charging_bs_->publish_state(v == 1);
    if (strcmp(key, "mt") == 0 && this->maint_bs_) this->maint_bs_->publish_state(v != 0);
    if (strcmp(key, "vx") == 0 && this->voc_restored_bs_) this->voc_restored_bs_->publish_state(v != 0);
#endif
    if (strcmp(key, "md") == 0) mode = static_cast<int>(v);
    if (strcmp(key, "t") == 0) t = v * 0.01f;
    if (strcmp(key, "h") == 0) h = v * 0.01f;
#ifdef USE_TEXT_SENSOR
    if (strcmp(key, "rr") == 0 && this->reason_ts_)
      this->reason_ts_->publish_state(reason_name(static_cast<int>(v)));
    if (strcmp(key, "md") == 0 && this->mode_ts_) this->mode_ts_->publish_state(mode_name(static_cast<int>(v)));
    if (strcmp(key, "cs") == 0 && this->charger_ts_) this->charger_ts_->publish_state(charger_name(static_cast<int>(v)));
    if (strcmp(key, "rs") == 0 && this->reset_ts_) this->reset_ts_->publish_state(reset_name(static_cast<int>(v)));
    if (strcmp(key, "pr") == 0 && this->present_ts_) this->present_ts_->publish_state(present_list(static_cast<uint32_t>(v)));
    if (strcmp(key, "fw") == 0 && this->fw_ts_) {
      char fw[16];
      snprintf(fw, sizeof fw, "%ld.%ld.%ld", v / 100, (v / 10) % 10, v % 10);
      this->fw_ts_->publish_state(fw);
    }
#endif
  }
#ifdef USE_SENSOR
  // Magnus formula (Sonntag 1990 constants), valid -45..60 degC
  if (std::isfinite(t) && std::isfinite(h) && h > 0.0f) {
    const float a = 17.62f, b = 243.12f;
    float gamma = logf(h / 100.0f) + a * t / (b + t);
    if (this->dew_point_ != nullptr) this->dew_point_->publish_state(b * gamma / (a - gamma));
    if (this->abs_humidity_ != nullptr) {
      float es = 6.112f * expf(a * t / (b + t));  // saturation vapour pressure, hPa
      this->abs_humidity_->publish_state(216.7f * (h / 100.0f * es) / (273.15f + t));
    }
  }
#endif
  ESP_LOGI(TAG, "Data frame received (mode: %s)", mode_name(mode));
}

}  // namespace omgevingsmonitor
}  // namespace esphome
