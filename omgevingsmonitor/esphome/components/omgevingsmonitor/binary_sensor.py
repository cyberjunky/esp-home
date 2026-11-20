import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY_CHARGING,
    DEVICE_CLASS_PLUG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_OMGEVINGSMONITOR_ID, OmgevingsmonitorComponent

DEPENDENCIES = ["omgevingsmonitor"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_OMGEVINGSMONITOR_ID): cv.use_id(OmgevingsmonitorComponent),
        cv.Optional("usb_power"): binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PLUG),
        cv.Optional("charging"): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY_CHARGING
        ),
        cv.Optional("maintenance_active"): binary_sensor.binary_sensor_schema(
            icon="mdi:wrench-clock", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_OMGEVINGSMONITOR_ID])
    if c := config.get("usb_power"):
        cg.add(hub.set_usb_binary_sensor(await binary_sensor.new_binary_sensor(c)))
    if c := config.get("charging"):
        cg.add(hub.set_charging_binary_sensor(await binary_sensor.new_binary_sensor(c)))
    if c := config.get("maintenance_active"):
        cg.add(hub.set_maintenance_binary_sensor(await binary_sensor.new_binary_sensor(c)))
