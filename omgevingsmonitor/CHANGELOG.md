# Changelog

All notable changes to the Omgevingsmonitor firmware. Versions follow
[Semantic Versioning](https://semver.org/); the STM32 firmware and the ESPHome configuration
share a version number.

## 1.2.1 - 2026-11-21

### Fixed

- Home Assistant showed all entities as unavailable between reports. The STM32 cut the ESP's
  power while the API connection was still open, so HA saw an unexpected disconnect. The ESP now
  closes the API connection cleanly (and waits for HA to acknowledge, max 1.5 s) before it tells
  the STM32 to switch it off; HA then keeps the values of this deep-sleep device. ESPHome only;
  the STM32 firmware stays at 1.2.0.

## 1.2.0 - 2026-11-20

### Added

- **Early reports on significant change** (temperature, humidity, VOC), rate limited, and a
  **Report reason** sensor (scheduled, boot, button, significant change, USB, maintenance).
- **VOC baseline persistence**: the gas index algorithm state is kept in RTC backup registers and
  restored after a reset or flash when less than 10 minutes old (**VOC baseline restored**).
  The RTC now keeps running across resets.
- **Dew point** and **absolute humidity**, computed in the ESPHome component.
- **Sound level background (LA90)** and **LA10**; the measurement window is now 4 s.
- **Solar charging (24 h)**: minutes the solar cell charged the battery in the last 24 hours.

## 1.1.0 - 2026-11-20

### Added

- Energy estimate without a current sensor: the STM32 measures how long every consumer is on
  (ESP, CPU, SGP40 hotplate, microphone, 5 V boost) and reports **Energy consumption**
  (mAh/day, 24 h average), **Battery runtime without sun** (days) and **Report duration**
  (how long the ESP was on for the last report).
- Home Assistant package: **battery trend** (%/day over 24 h) and **battery life at current
  trend**, which includes what the solar cell actually adds.

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
