"""De Omgevingsmonitor - ESPHome hub for the STM32L072 sensor/power controller.

The STM32 powers the ESP32-C3 only to report. This component receives the
STM32's data frame over UART, publishes it, waits until Home Assistant is
connected, sends HA-side settings back and tells the STM32 it may cut power.
"""

import esphome.codegen as cg
from esphome.components import binary_sensor, sensor, uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@omgevingsmonitor"]
DEPENDENCIES = ["uart", "api"]
MULTI_CONF = False

CONF_OMGEVINGSMONITOR_ID = "omgevingsmonitor_id"
CONF_INTERVAL_BATTERY_SOURCE = "interval_battery_source"
CONF_INTERVAL_USB_SOURCE = "interval_usb_source"
CONF_MAINTENANCE_SOURCE = "maintenance_source"
CONF_HA_SETTLE_TIME = "ha_settle_time"

om_ns = cg.esphome_ns.namespace("omgevingsmonitor")
OmgevingsmonitorComponent = om_ns.class_("OmgevingsmonitorComponent", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OmgevingsmonitorComponent),
            # Optional Home Assistant helpers (imported with the `homeassistant`
            # sensor / binary_sensor platforms) that let HA steer the device.
            cv.Optional(CONF_INTERVAL_BATTERY_SOURCE): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_INTERVAL_USB_SOURCE): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_MAINTENANCE_SOURCE): cv.use_id(binary_sensor.BinarySensor),
            # Time to wait after HA connected, so it has received all states
            # and sent the helper values, before the ESP is switched off.
            cv.Optional(CONF_HA_SETTLE_TIME, default="1200ms"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "omgevingsmonitor", baud_rate=115200, require_rx=True, require_tx=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_ha_settle_time(config[CONF_HA_SETTLE_TIME]))
    if CONF_INTERVAL_BATTERY_SOURCE in config:
        s = await cg.get_variable(config[CONF_INTERVAL_BATTERY_SOURCE])
        cg.add(var.set_interval_battery_source(s))
    if CONF_INTERVAL_USB_SOURCE in config:
        s = await cg.get_variable(config[CONF_INTERVAL_USB_SOURCE])
        cg.add(var.set_interval_usb_source(s))
    if CONF_MAINTENANCE_SOURCE in config:
        s = await cg.get_variable(config[CONF_MAINTENANCE_SOURCE])
        cg.add(var.set_maintenance_source(s))
