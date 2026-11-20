import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_AQI,
    DEVICE_CLASS_ATMOSPHERIC_PRESSURE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CARBON_DIOXIDE,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_PM1,
    DEVICE_CLASS_PM10,
    DEVICE_CLASS_PM25,
    DEVICE_CLASS_SOUND_PRESSURE,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLATILE_ORGANIC_COMPOUNDS_PARTS,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_HECTOPASCAL,
    UNIT_MICROGRAMS_PER_CUBIC_METER,
    UNIT_PARTS_PER_BILLION,
    UNIT_PARTS_PER_MILLION,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_VOLT,
)

from . import CONF_OMGEVINGSMONITOR_ID, OmgevingsmonitorComponent

DEPENDENCIES = ["omgevingsmonitor"]

UNIT_DBA = "dBA"
DIAG = ENTITY_CATEGORY_DIAGNOSTIC
MEAS = STATE_CLASS_MEASUREMENT

# config key: (frame key, scale, schema kwargs)
SENSORS = {
    # on-board
    "temperature": ("t", 0.01, dict(unit_of_measurement=UNIT_CELSIUS, accuracy_decimals=1,
                                    device_class=DEVICE_CLASS_TEMPERATURE, state_class=MEAS)),
    "humidity": ("h", 0.01, dict(unit_of_measurement=UNIT_PERCENT, accuracy_decimals=1,
                                 device_class=DEVICE_CLASS_HUMIDITY, state_class=MEAS)),
    "voc_index": ("vi", 1, dict(accuracy_decimals=0, icon="mdi:air-filter", state_class=MEAS)),
    "voc_raw": ("vr", 1, dict(accuracy_decimals=0, icon="mdi:flask", state_class=MEAS,
                              entity_category=DIAG)),
    "sound_level": ("la", 0.1, dict(unit_of_measurement=UNIT_DBA, accuracy_decimals=1,
                                    device_class=DEVICE_CLASS_SOUND_PRESSURE, state_class=MEAS)),
    "sound_level_max": ("lx", 0.1, dict(unit_of_measurement=UNIT_DBA, accuracy_decimals=1,
                                        device_class=DEVICE_CLASS_SOUND_PRESSURE, state_class=MEAS)),
    "sound_level_background": ("l9", 0.1, dict(unit_of_measurement=UNIT_DBA, accuracy_decimals=1,
                                               device_class=DEVICE_CLASS_SOUND_PRESSURE, state_class=MEAS)),
    "sound_level_l10": ("l1", 0.1, dict(unit_of_measurement=UNIT_DBA, accuracy_decimals=1,
                                        device_class=DEVICE_CLASS_SOUND_PRESSURE, state_class=MEAS)),
    # power
    "battery_voltage": ("vb", 0.001, dict(unit_of_measurement=UNIT_VOLT, accuracy_decimals=3,
                                          device_class=DEVICE_CLASS_VOLTAGE, state_class=MEAS)),
    "battery_level": ("bp", 1, dict(unit_of_measurement=UNIT_PERCENT, accuracy_decimals=0,
                                    device_class=DEVICE_CLASS_BATTERY, state_class=MEAS)),
    "solar_voltage": ("vs", 0.001, dict(unit_of_measurement=UNIT_VOLT, accuracy_decimals=2,
                                        device_class=DEVICE_CLASS_VOLTAGE, state_class=MEAS)),
    "solar_charging_time": ("sc", 1, dict(unit_of_measurement="min", accuracy_decimals=0,
                                          device_class=DEVICE_CLASS_DURATION, icon="mdi:solar-power",
                                          state_class=MEAS)),
    # energy estimate (no current sensor: measured on-times x typical currents)
    "energy_consumption": ("ec", 0.1, dict(unit_of_measurement="mAh/d", accuracy_decimals=1,
                                           icon="mdi:battery-arrow-down-outline", state_class=MEAS)),
    "battery_runtime": ("rd", 0.1, dict(unit_of_measurement="d", accuracy_decimals=0,
                                        icon="mdi:battery-clock-outline", state_class=MEAS)),
    "report_duration": ("ed", 0.001, dict(unit_of_measurement=UNIT_SECOND, accuracy_decimals=1,
                                          device_class=DEVICE_CLASS_DURATION, state_class=MEAS,
                                          entity_category=DIAG)),
    # optional AHT20
    "aht20_temperature": ("at", 0.01, dict(unit_of_measurement=UNIT_CELSIUS, accuracy_decimals=1,
                                           device_class=DEVICE_CLASS_TEMPERATURE, state_class=MEAS)),
    "aht20_humidity": ("ah", 0.01, dict(unit_of_measurement=UNIT_PERCENT, accuracy_decimals=1,
                                        device_class=DEVICE_CLASS_HUMIDITY, state_class=MEAS)),
    # optional BMP280 / BME280
    "pressure": ("pa", 0.01, dict(unit_of_measurement=UNIT_HECTOPASCAL, accuracy_decimals=1,
                                  device_class=DEVICE_CLASS_ATMOSPHERIC_PRESSURE, state_class=MEAS)),
    "bmp280_temperature": ("bt", 0.01, dict(unit_of_measurement=UNIT_CELSIUS, accuracy_decimals=1,
                                            device_class=DEVICE_CLASS_TEMPERATURE, state_class=MEAS)),
    "bme280_humidity": ("bh", 0.01, dict(unit_of_measurement=UNIT_PERCENT, accuracy_decimals=1,
                                         device_class=DEVICE_CLASS_HUMIDITY, state_class=MEAS)),
    # optional ENS160
    "aqi": ("aq", 1, dict(accuracy_decimals=0, device_class=DEVICE_CLASS_AQI, state_class=MEAS)),
    "tvoc": ("tv", 1, dict(unit_of_measurement=UNIT_PARTS_PER_BILLION, accuracy_decimals=0,
                           device_class=DEVICE_CLASS_VOLATILE_ORGANIC_COMPOUNDS_PARTS, state_class=MEAS)),
    "eco2": ("co", 1, dict(unit_of_measurement=UNIT_PARTS_PER_MILLION, accuracy_decimals=0,
                           device_class=DEVICE_CLASS_CARBON_DIOXIDE, state_class=MEAS)),
    # optional SEN5x
    "pm_1_0": ("p1", 0.1, dict(unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER, accuracy_decimals=1,
                               device_class=DEVICE_CLASS_PM1, state_class=MEAS)),
    "pm_2_5": ("p2", 0.1, dict(unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER, accuracy_decimals=1,
                               device_class=DEVICE_CLASS_PM25, state_class=MEAS)),
    "pm_4_0": ("p4", 0.1, dict(unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER, accuracy_decimals=1,
                               icon="mdi:chemical-weapon", state_class=MEAS)),
    "pm_10_0": ("p0", 0.1, dict(unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER, accuracy_decimals=1,
                                device_class=DEVICE_CLASS_PM10, state_class=MEAS)),
    "sen5x_temperature": ("st", 0.01, dict(unit_of_measurement=UNIT_CELSIUS, accuracy_decimals=1,
                                           device_class=DEVICE_CLASS_TEMPERATURE, state_class=MEAS)),
    "sen5x_humidity": ("sh", 0.01, dict(unit_of_measurement=UNIT_PERCENT, accuracy_decimals=1,
                                        device_class=DEVICE_CLASS_HUMIDITY, state_class=MEAS)),
    "sen5x_voc_index": ("sv", 0.1, dict(accuracy_decimals=0, icon="mdi:air-filter", state_class=MEAS)),
    "sen5x_nox_index": ("sn", 0.1, dict(accuracy_decimals=0, icon="mdi:smog", state_class=MEAS)),
    # diagnostics
    "report_interval": ("iv", 1, dict(unit_of_measurement=UNIT_SECOND, accuracy_decimals=0,
                                      device_class=DEVICE_CLASS_DURATION, entity_category=DIAG)),
    "uptime": ("up", 1, dict(unit_of_measurement=UNIT_SECOND, accuracy_decimals=0,
                             device_class=DEVICE_CLASS_DURATION, state_class=STATE_CLASS_TOTAL_INCREASING,
                             entity_category=DIAG)),
    "i2c_errors": ("ie", 1, dict(accuracy_decimals=0, icon="mdi:alert-circle-outline",
                                 state_class=MEAS, entity_category=DIAG)),
    "link_failures": ("lf", 1, dict(accuracy_decimals=0, icon="mdi:wifi-alert", state_class=MEAS,
                                    entity_category=DIAG)),
}

# computed on the ESP from temperature + humidity
COMPUTED = {
    "dew_point": ("set_dew_point_sensor", dict(unit_of_measurement=UNIT_CELSIUS, accuracy_decimals=1,
                                               device_class=DEVICE_CLASS_TEMPERATURE,
                                               icon="mdi:water-thermometer", state_class=MEAS)),
    "absolute_humidity": ("set_absolute_humidity_sensor", dict(unit_of_measurement="g/m³", accuracy_decimals=1,
                                                               icon="mdi:water", state_class=MEAS)),
}

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_OMGEVINGSMONITOR_ID): cv.use_id(OmgevingsmonitorComponent)}
).extend({cv.Optional(k): sensor.sensor_schema(**kw) for k, (_, _, kw) in SENSORS.items()}).extend(
    {cv.Optional(k): sensor.sensor_schema(**kw) for k, (_, kw) in COMPUTED.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_OMGEVINGSMONITOR_ID])
    for key, (frame_key, scale, _) in SENSORS.items():
        if key in config:
            s = await sensor.new_sensor(config[key])
            cg.add(hub.add_sensor(frame_key, scale, s))
    for key, (setter, _) in COMPUTED.items():
        if key in config:
            cg.add(getattr(hub, setter)(await sensor.new_sensor(config[key])))
