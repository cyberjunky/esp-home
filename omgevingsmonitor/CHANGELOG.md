# Changelog

All notable changes to the Omgevingsmonitor firmware. Versions follow
[Semantic Versioning](https://semver.org/); the STM32 firmware and the ESPHome configuration
share a version number.

## 1.0.1 - 2026-11-20

### Fixed

- SGP40 returned 0 (VOC index stayed unknown): the hotplate now warms up for 170 ms before
  each measurement, following Sensirion's low-power procedure. Zero readings are ignored.

### Added

- USB bridge leaves bridge mode by itself: on USB unplug, a short press of User/VOC, or after
  10 minutes without serial traffic.

### Changed

- Optional add-on sensors are commented out in `omgevingsmonitor.yaml`; uncomment the ones you
  have fitted.
- OTA updates use the API encryption key (`ota: encryption: {}`); no separate OTA password.
- Device shows in Home Assistant as model "omgevingsmonitor" by "wots"; counters show as whole
  numbers.

## 1.0.0 - 2026-11-20

- First release: new STM32L072 firmware (Stop-mode sleep, 10 s VOC/T/RH sampling, A-weighted
  sound level, battery/solar-aware reporting) and ESPHome firmware with the `omgevingsmonitor`
  external component.
