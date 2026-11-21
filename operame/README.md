# Operame: ESPHome firmware

ESPHome firmware for the [Operame](https://github.com/revspace/operame) CO2 meter, for units
fitted with the **AQC** CO2 sensor instead of the MH-Z19.

* CO2 in ppm on the TTGO T-Display, colour coded: green below 700 ppm, yellow from 700, red from
  800 (adjustable with `co2_warning` and `co2_critical`). A blue frame means Home Assistant is
  connected.
* CO2, Wi-Fi signal, uptime, IP address, ESPHome version and both buttons in Home Assistant, plus
  a restart button and a dimmable display backlight.
* Wi-Fi fallback access point with captive portal, and Improv over serial for the web flasher.

## Hardware

| Part | Connection |
| --- | --- |
| TTGO T-Display (ESP32) | ST7789V 135×240 over SPI: CLK GPIO18, MOSI GPIO19, CS GPIO5, DC GPIO16, RST GPIO23, backlight GPIO4 |
| AQC CO2 sensor | UART 9600 baud: sensor TX → GPIO27 (RX), sensor RX ← GPIO26 (TX), same pins as the MH-Z19 |
| Buttons | GPIO0 and GPIO35 (on the T-Display) |

## The AQC sensor

The AQC speaks a variant of the MH-Z19 protocol: the read command is `FF 01 C5 00 00 00 00 00 3A`
instead of `FF 01 86 …`. The answer has the same layout as the MH-Z19 (`FF 86`, CO2 high and low
byte, checksum), so the stock `mhz19` component can't read it. The `aqc_sensor` external component
in [`esphome/components/aqc_sensor/`](esphome/components/aqc_sensor/) sends the AQC command
every 10 s, reads the answer 100 ms later without blocking ESPHome's main loop, checks the header
and checksum, and skips the value 9999 the sensor gives while it warms up. The YAML drops readings outside 400–5000 ppm and averages the last 5.

## Getting started

1. Copy `esphome/secrets.example.yaml` to `esphome/secrets.yaml` and fill it in.
2. Set `device_name` under `substitutions` in `esphome/operame.yaml` to the name you want in Home
   Assistant (default `operame-hobbykamer`). Changing it on a device that is already adopted
   gives it a new name and entities.
3. Flash over USB the first time, then over the air:

   ```sh
   cd operame/esphome
   esphome run operame.yaml
   ```

OTA updates are authenticated and encrypted with the API key; there is no separate OTA password.

## Upgrading from the old configuration

The previous configuration used the Arduino framework and an OTA password. This one uses ESP-IDF
(ESPHome's default) and OTA encryption with the API key. Flash it **once over USB**:

* The two frameworks use different partition tables, and an OTA update can't change the partition
  table.
* A USB flash doesn't need the old OTA password.

```sh
cd operame/esphome
esphome run operame.yaml --device COM5   # your T-Display's serial port
```

Keep the same `api_key`, then Home Assistant reconnects without re-adopting. After this, updates
work over the air again. The `esphome_ota` entry in `secrets.yaml` is no longer used.

Other changes from the old configuration: the display uses the `mipi_spi` platform (`st7789v` is
removed in ESPHome 2027.5), the backlight is a dimmable light in Home Assistant, and Wi-Fi signal,
uptime, IP address and version are diagnostic entities.
