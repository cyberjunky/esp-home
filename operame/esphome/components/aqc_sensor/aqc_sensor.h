#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aqc_sensor {

static const char *const TAG = "aqc";

// The AQC uses the MH-Z19 frame layout, but its read command is 0xC5 instead of 0x86.
// Request:  FF 01 C5 00 00 00 00 00 3A
// Response: FF 86 <CO2 high> <CO2 low> xx xx xx xx <checksum>
static const uint8_t AQC_READ_CMD[9] = {0xFF, 0x01, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3A};
static const uint32_t AQC_RESPONSE_MS = 100;
static const int AQC_WARMING_UP = 9999;

class AQCSensor : public PollingComponent, public uart::UARTDevice {
 public:
  void set_co2_sensor(sensor::Sensor *co2_sensor) { this->co2_sensor_ = co2_sensor; }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "AQC CO2 sensor");
    LOG_UPDATE_INTERVAL(this);
    LOG_SENSOR("  ", "CO2", this->co2_sensor_);
  }

  void update() override {
    // Drop anything stale, send the read command and pick up the answer without blocking the loop.
    uint8_t c;
    while (this->available() && this->read_byte(&c)) {
    }
    this->write_array(AQC_READ_CMD, sizeof(AQC_READ_CMD));
    this->set_timeout("read", AQC_RESPONSE_MS, [this]() { this->read_response_(); });
  }

 protected:
  void read_response_() {
    uint8_t r[9];
    if (this->available() < static_cast<int>(sizeof(r)) || !this->read_array(r, sizeof(r))) {
      ESP_LOGW(TAG, "No complete answer from the AQC sensor (%d bytes)", this->available());
      this->status_set_warning();
      return;
    }
    if (r[0] != 0xFF || r[1] != 0x86) {
      ESP_LOGW(TAG, "Invalid response header: 0x%02X 0x%02X", r[0], r[1]);
      this->status_set_warning();
      return;
    }
    // MH-Z19 checksum: 0xFF - (sum of bytes 1..7) + 1
    uint8_t sum = 0;
    for (int i = 1; i < 8; i++) sum += r[i];
    const uint8_t checksum = static_cast<uint8_t>(0xFF - sum + 1);
    if (r[8] != checksum) {
      ESP_LOGW(TAG, "Checksum mismatch: expected 0x%02X, got 0x%02X", checksum, r[8]);
      this->status_set_warning();
      return;
    }
    this->status_clear_warning();

    const int co2 = (r[2] << 8) | r[3];
    if (co2 == AQC_WARMING_UP) {
      ESP_LOGI(TAG, "Sensor is still warming up");
      return;
    }
    ESP_LOGD(TAG, "CO2: %d ppm", co2);
    if (this->co2_sensor_ != nullptr) this->co2_sensor_->publish_state(co2);
  }

  sensor::Sensor *co2_sensor_{nullptr};
};

}  // namespace aqc_sensor
}  // namespace esphome
