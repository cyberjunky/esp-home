[![GitHub Activity][commits-shield]][commits]
[![License][license-shield]](LICENSE)
![Project Maintenance][maintenance-shield]

[![Donate via PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg?style=for-the-badge&logo=paypal)](https://www.paypal.me/cyberjunkynl/)
[![Sponsor on GitHub](https://img.shields.io/badge/Sponsor-GitHub-red.svg?style=for-the-badge&logo=github)](https://github.com/sponsors/cyberjunky)

# Cyberjunky's ESP Home Projects

My collection of [ESPHome](https://esphome.io) projects for Home Assistant: new firmware for
existing gadgets and sensors, so they show up in Home Assistant as normal ESPHome devices. Each
project lives in its own folder with its configuration, firmware, documentation and changelog.

## Projects

### [Omgevingsmonitor](omgevingsmonitor/)

![ESPHome](https://img.shields.io/badge/ESPHome-2026.11-blue.svg?style=for-the-badge)
![STM32](https://img.shields.io/badge/STM32L072-firmware-green.svg?style=for-the-badge)

Lean firmware for De Omgevingsmonitor, the WoTS 2024 environmental sensor gadget by KITT
Engineering. The STM32 does the measuring and the power management, the ESP32-C3 runs ESPHome
and is only switched on to report, so the gadget can run on its small solar cell through the
winter. Temperature, humidity, VOC index, sound level in dB(A), battery and solar, plus the
optional AHT20, BMP/BME280, ENS160 and Sensirion SEN5x particulate sensor.

[:books: Documentation](omgevingsmonitor/README.md) · [:memo: Changelog](omgevingsmonitor/CHANGELOG.md) · [:wrench: Flashing](omgevingsmonitor/docs/flashing.md)

### [Operame](operame/)

CO2 monitor (Operame). Coming soon.

## Layout

```txt
<project>/
  README.md            hardware, wiring and usage
  CHANGELOG.md         changes per version
  esphome/             ESPHome configuration and external components
    <project>.yaml
    secrets.example.yaml
  docs/                diagrams and background (optional)
  <other firmware>/    for projects with a second microcontroller (optional)
```

| Folder | What's in it |
| --- | --- |
| [`omgevingsmonitor/`](omgevingsmonitor/) | ESPHome config and component, STM32L072 firmware (PlatformIO), Home Assistant helpers, docs |
| [`omgevingsmonitor/esphome/`](omgevingsmonitor/esphome/) | `omgevingsmonitor.yaml` and the `omgevingsmonitor` external component |
| [`omgevingsmonitor/stm32/`](omgevingsmonitor/stm32/) | firmware for the STM32L072: sensors, sound meter, power management |
| [`omgevingsmonitor/homeassistant/`](omgevingsmonitor/homeassistant/) | package with helpers to set the interval and maintenance mode from Home Assistant |
| [`omgevingsmonitor/docs/`](omgevingsmonitor/docs/) | architecture, protocol, hardware notes and flashing guide |
| [`omgevingsmonitor/tools/`](omgevingsmonitor/tools/) | design script for the A-weighting filter |
| [`operame/`](operame/) | Operame CO2 monitor (files follow) |

## Using a project

1. Copy `secrets.example.yaml` to `secrets.yaml` in the project's `esphome/` folder and fill in
   your own values. Git ignores `secrets.yaml`.
2. Compile and flash the configuration:

```bash
cd <project>/esphome
esphome run <project>.yaml
```

Projects with a second microcontroller (such as the Omgevingsmonitor's STM32) explain the extra
step in their own README.

## 💖 Support This Project

If you find these projects useful, please consider supporting their continued development
and maintenance:

### 🌟 Ways to Support

- **⭐ Star this repository** - Help others discover the project
- **💰 Financial Support** - Contribute to development and hosting costs
- **🐛 Report Issues** - Help improve stability and compatibility
- **📖 Spread the Word** - Share with other Home Assistant users

### 💳 Financial Support Options

[![Donate via PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg?style=for-the-badge&logo=paypal)](https://www.paypal.me/cyberjunkynl/)
[![Sponsor on GitHub](https://img.shields.io/badge/Sponsor-GitHub-red.svg?style=for-the-badge&logo=github)](https://github.com/sponsors/cyberjunky)

Every contribution, no matter the size, makes a difference and is greatly appreciated! 🙏

## Community

Questions, ideas or just want to chat? Join The Junkyard on Discord:

<a href="https://discord.gg/hUBQxRhJCU">
  <img src="https://raw.githubusercontent.com/cyberjunky/cyberjunky/main/assets/junkyard-discord.svg"
       alt="Join The Junkyard on Discord" width="600">
</a>

## License

Unless a folder contains its own `LICENSE` file, this repository is licensed under the MIT
License - see the [LICENSE](LICENSE) file for details. Third-party code keeps its own license
next to it, for example Sensirion's gas index algorithm (BSD-3-Clause) in
`omgevingsmonitor/stm32/lib/sensirion_gas_index/`.

---

[commits-shield]: https://img.shields.io/github/commit-activity/y/cyberjunky/esp-home.svg?style=for-the-badge
[commits]: https://github.com/cyberjunky/esp-home/commits/main
[license-shield]: https://img.shields.io/github/license/cyberjunky/esp-home.svg?style=for-the-badge
[maintenance-shield]: https://img.shields.io/badge/maintainer-cyberjunky-blue.svg?style=for-the-badge
