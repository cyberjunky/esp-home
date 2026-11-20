# Building and flashing

Everything goes over the USB-C port. You don't need an ST-Link or a USB-serial adapter.

## Tooling (once)

```powershell
py -3.14 -m venv .venv
.\.venv\Scripts\pip install esphome        # also installs PlatformIO
```

PlatformIO downloads the ARM toolchain and STM32Cube L0 package on the first build.

For STM32 DFU on Windows, use **one** of these:

* **STM32CubeProgrammer** (includes the ST DFU driver). Recommended if you already have it.
* **dfu-util** (PlatformIO installs it). It needs the WinUSB driver bound to "STM32 BOOTLOADER", which you can do once with [Zadig](https://zadig.akeo.ie/).

## Order matters the first time

ESPHome has to go on the ESP **before** the new STM32 firmware is running. Otherwise
the ESP still runs the old AT firmware and the STM32 never gets a hello.

### 1. ESPHome → ESP32-C3

1. `cd esphome`, then `copy secrets.example.yaml secrets.yaml` and fill it in. Generate the API key as described in the file.
2. Put the board in bridge mode: **hold User/VOC, press and release Reset/Status, keep User/VOC held for ~1 s**.
   The status LED turns blue and a new COM port appears. Both the stock firmware and this one support this.
3. Flash:
   ```powershell
   ..\.venv\Scripts\esphome run omgevingsmonitor.yaml --device COM7
   ```
   esptool resets the ESP into its bootloader through DTR/RTS. If that fails, the bridge
   already starts the ESP in download mode, so `--before no_reset` also works with plain esptool.
4. Press Reset/Status to leave bridge mode.

### 2. STM32 firmware

1. Enter the ROM bootloader: **hold Boot/dB, press and release Reset/Status, then release Boot/dB**.
   The board enumerates as "STM32 BOOTLOADER" (DFU).
2. Flash with one of:
   ```powershell
   cd stm32
   ..\.venv\Scripts\pio run -t upload                  # dfu-util
   # or STM32CubeProgrammer CLI:
   STM32_Programmer_CLI -c port=USB1 -w .pio\build\omgevingsmonitor\firmware.bin 0x08000000 -v -s
   ```
3. Press Reset/Status. The status LED flashes green once, and about 30 s later the first report reaches Home Assistant.

## Later updates

* **ESPHome**: long-press User/VOC (3 s, LED magenta) or switch on
  `input_boolean.omgevingsmonitor_maintenance`. The ESP then stays on for 15 min, so
  `esphome run omgevingsmonitor.yaml` can update it over Wi-Fi and `esphome logs` works.
* **STM32**: DFU as above. Settings in the EEPROM survive.

## Build options (stm32/platformio.ini `build_flags`)

| Flag | Default | Meaning |
| --- | --- | --- |
| `LOG_LEVEL` | 2 | debug UART on PA9 (0 = off) |
| `FAST_TICK_S` | 10 | VOC / T / RH sample period |
| `SEN5X_BATT_PERIOD_S` | 3600 | PM measurement period on battery (0 = never) |
| `SEN5X_BATT_MIN_SOC` | 50 | ... only above this battery % |
| `ENS160_ON_BATTERY` | 0 | run the ENS160 on battery as well |
| `SOUND_MEASURE_MS` | 2000 | sound measurement window per report |
| `SOUND_CAL_DB` | 0.0f | per-unit microphone calibration offset |
| `VBAT_DIVIDER` / `VSOL_DIVIDER` | 2 / 3 | ADC divider ratios |
