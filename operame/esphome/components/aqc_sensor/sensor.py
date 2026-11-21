import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_CO2,
    DEVICE_CLASS_CARBON_DIOXIDE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PARTS_PER_MILLION,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]

CODEOWNERS = ["@cyberjunky"]

aqc_sensor_ns = cg.esphome_ns.namespace("aqc_sensor")
AQCSensor = aqc_sensor_ns.class_(
    "AQCSensor", cg.PollingComponent, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AQCSensor),
            cv.Optional(CONF_CO2): sensor.sensor_schema(
                unit_of_measurement=UNIT_PARTS_PER_MILLION,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_CARBON_DIOXIDE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if co2_config := config.get(CONF_CO2):
        sens = await sensor.new_sensor(co2_config)
        cg.add(var.set_co2_sensor(sens))
