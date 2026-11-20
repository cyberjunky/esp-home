#pragma once

#include <vector>

#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace omgevingsmonitor {

/* Receives data frames from the STM32 (docs/protocol.md), publishes them,
 * relays Home Assistant settings back and releases the power when done. */
class OmgevingsmonitorComponent : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_ha_settle_time(uint32_t ms) { this->ha_settle_ms_ = ms; }
#ifdef USE_SENSOR
  void add_sensor(const char *key, float scale, sensor::Sensor *s) { this->sensors_.push_back({key, scale, s}); }
  void set_interval_battery_source(sensor::Sensor *s) { this->interval_batt_src_ = s; }
  void set_interval_usb_source(sensor::Sensor *s) { this->interval_usb_src_ = s; }
  void set_dew_point_sensor(sensor::Sensor *s) { this->dew_point_ = s; }
  void set_absolute_humidity_sensor(sensor::Sensor *s) { this->abs_humidity_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_usb_binary_sensor(binary_sensor::BinarySensor *s) { this->usb_bs_ = s; }
  void set_charging_binary_sensor(binary_sensor::BinarySensor *s) { this->charging_bs_ = s; }
  void set_maintenance_binary_sensor(binary_sensor::BinarySensor *s) { this->maint_bs_ = s; }
  void set_voc_restored_binary_sensor(binary_sensor::BinarySensor *s) { this->voc_restored_bs_ = s; }
  void set_maintenance_source(binary_sensor::BinarySensor *s) { this->maint_src_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_power_mode_text_sensor(text_sensor::TextSensor *s) { this->mode_ts_ = s; }
  void set_report_reason_text_sensor(text_sensor::TextSensor *s) { this->reason_ts_ = s; }
  void set_charger_text_sensor(text_sensor::TextSensor *s) { this->charger_ts_ = s; }
  void set_reset_cause_text_sensor(text_sensor::TextSensor *s) { this->reset_ts_ = s; }
  void set_firmware_text_sensor(text_sensor::TextSensor *s) { this->fw_ts_ = s; }
  void set_sensors_text_sensor(text_sensor::TextSensor *s) { this->present_ts_ = s; }
#endif

 protected:
  void handle_line_();
  void handle_data_(char *payload);
  void send_(const char *body);
  bool ha_wants_maintenance_();
  bool ha_connected_();

  char line_[512];
  size_t line_len_{0};
  bool in_line_{false};

  bool got_data_{false};
  uint32_t last_hello_{0};
  uint32_t connected_at_{0};
  uint32_t last_cfg_{0};
  uint32_t last_done_{0};
  bool cfg_sent_{false};
  bool last_maint_{false};
  uint32_t ha_settle_ms_{1200};
  uint32_t frames_{0};

#ifdef USE_SENSOR
  struct Entry {
    const char *key;
    float scale;
    sensor::Sensor *sensor;
  };
  std::vector<Entry> sensors_;
  sensor::Sensor *interval_batt_src_{nullptr};
  sensor::Sensor *interval_usb_src_{nullptr};
  sensor::Sensor *dew_point_{nullptr};
  sensor::Sensor *abs_humidity_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *usb_bs_{nullptr};
  binary_sensor::BinarySensor *charging_bs_{nullptr};
  binary_sensor::BinarySensor *maint_bs_{nullptr};
  binary_sensor::BinarySensor *voc_restored_bs_{nullptr};
  binary_sensor::BinarySensor *maint_src_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *mode_ts_{nullptr};
  text_sensor::TextSensor *reason_ts_{nullptr};
  text_sensor::TextSensor *charger_ts_{nullptr};
  text_sensor::TextSensor *reset_ts_{nullptr};
  text_sensor::TextSensor *fw_ts_{nullptr};
  text_sensor::TextSensor *present_ts_{nullptr};
#endif
};

}  // namespace omgevingsmonitor
}  // namespace esphome
