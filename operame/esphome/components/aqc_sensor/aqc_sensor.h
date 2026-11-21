#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace aqc_sensor {

class AQCSensor : public PollingComponent, public uart::UARTDevice {
 public:
  void set_co2_sensor(sensor::Sensor *co2_sensor) { co2_sensor_ = co2_sensor; }

  void setup() override {
    // Nothing special needed for setup
  }

  void update() override {
    // AQC command to read CO2 (different from MH-Z19's 0x86)
    const uint8_t command[9] = {0xFF, 0x01, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3A};
    uint8_t response[9];

    // Flush any pending data
    while (this->available()) {
      this->read();
    }

    // Send command
    this->write_array(command, sizeof(command));
    this->flush();

    // Small delay for sensor to respond
    delay(50);

    // Read response
    if (this->available() < 9) {
      ESP_LOGW("aqc", "Not enough data available from AQC sensor");
      delay(50);
      if (this->available() < 9) {
        return;
      }
    }

    size_t bytes_read = 0;
    for (int i = 0; i < 9 && this->available(); i++) {
      response[i] = this->read();
      bytes_read++;
    }

    if (bytes_read != 9) {
      ESP_LOGW("aqc", "Failed to read 9 bytes from AQC sensor, got %u", (unsigned) bytes_read);
      return;
    }

    // Validate response header
    if (response[0] != 0xFF || response[1] != 0x86) {
      ESP_LOGW("aqc", "Invalid response header from AQC: 0x%02X 0x%02X", response[0], response[1]);
      return;
    }

    // Verify checksum
    uint8_t checksum = 0xFF;
    for (int i = 0; i < 8; i++) {
      checksum -= response[i];
    }

    if (response[8] != checksum) {
      ESP_LOGW("aqc", "Checksum mismatch: expected 0x%02X, got 0x%02X", checksum, response[8]);
      return;
    }

    // Extract CO2 value (bytes 2 and 3, big-endian)
    int co2 = (response[2] << 8) | response[3];

    // The AQC returns 9999 while it initialises (400 ppm readings pass; the YAML filters range)
    if (co2 == 9999) {
      ESP_LOGI("aqc", "AQC sensor still initializing (got %d ppm)", co2);
      return;
    }

    ESP_LOGD("aqc", "AQC CO2: %d ppm", co2);
    if (this->co2_sensor_ != nullptr) {
      this->co2_sensor_->publish_state(co2);
    }
  }

 protected:
  sensor::Sensor *co2_sensor_{nullptr};
};

}  // namespace aqc_sensor
}  // namespace esphome
