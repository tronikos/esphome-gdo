import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

gdo_ns = cg.esphome_ns.namespace("gdo")
GdoBinarySensor = gdo_ns.class_(
    "GdoBinarySensor", binary_sensor.BinarySensor, cg.Component
)

CONF_INPUT_OBST = "input_obst_pin"

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(
        GdoBinarySensor,
        device_class=DEVICE_CLASS_PROBLEM,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )
    .extend(
        {
            # The obstruction line is sampled with a GPIO interrupt, so it has to
            # be a pin on the ESP itself. internal_gpio_input_pin_schema rejects
            # port-expander pins here, where they would otherwise pass validation
            # and then fail the build with an unreadable C++ conversion error.
            cv.Required(CONF_INPUT_OBST): pins.internal_gpio_input_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await binary_sensor.register_binary_sensor(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_INPUT_OBST])
    cg.add(var.set_input_obst_pin(pin))
