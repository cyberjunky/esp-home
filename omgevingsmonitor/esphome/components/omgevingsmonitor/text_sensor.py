import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_OMGEVINGSMONITOR_ID, OmgevingsmonitorComponent

DEPENDENCIES = ["omgevingsmonitor"]
DIAG = ENTITY_CATEGORY_DIAGNOSTIC

TEXTS = {
    "power_mode": ("set_power_mode_text_sensor", "mdi:solar-power-variant"),
    "charger_state": ("set_charger_text_sensor", "mdi:battery-charging"),
    "reset_cause": ("set_reset_cause_text_sensor", "mdi:restart-alert"),
    "stm32_firmware": ("set_firmware_text_sensor", "mdi:chip"),
    "detected_sensors": ("set_sensors_text_sensor", "mdi:format-list-checks"),
}

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_OMGEVINGSMONITOR_ID): cv.use_id(OmgevingsmonitorComponent)}
).extend(
    {
        cv.Optional(k): text_sensor.text_sensor_schema(
            icon=icon, **({} if k == "power_mode" else {"entity_category": DIAG})
        )
        for k, (_, icon) in TEXTS.items()
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_OMGEVINGSMONITOR_ID])
    for key, (setter, _) in TEXTS.items():
        if key in config:
            cg.add(getattr(hub, setter)(await text_sensor.new_text_sensor(config[key])))
