import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import binary_sensor, cover
from esphome.const import (
    CONF_CLOSE_DURATION,
    CONF_CLOSE_ENDSTOP,
    CONF_ID,
    CONF_OPEN_DURATION,
    CONF_OPEN_ENDSTOP,
)

gdo_ns = cg.esphome_ns.namespace("gdo")
GdoCover = gdo_ns.class_("GdoCover", cover.Cover, cg.Component)

CONF_SINGLE_PRESS_ACTION = "single_press_action"
CONF_DOUBLE_PRESS_ACTION = "double_press_action"

CONFIG_SCHEMA = (
    cover.cover_schema(GdoCover)
    .extend(
        {
            cv.Optional(CONF_OPEN_ENDSTOP): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_OPEN_DURATION): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CLOSE_ENDSTOP): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_CLOSE_DURATION): cv.positive_time_period_milliseconds,
            cv.Required(CONF_SINGLE_PRESS_ACTION): automation.validate_automation(
                single=True
            ),
            cv.Required(CONF_DOUBLE_PRESS_ACTION): automation.validate_automation(
                single=True
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)

    if CONF_OPEN_ENDSTOP in config:
        bin = await cg.get_variable(config[CONF_OPEN_ENDSTOP])
        cg.add(var.set_open_endstop(bin))
    cg.add(var.set_open_duration(config[CONF_OPEN_DURATION]))
    if CONF_CLOSE_ENDSTOP in config:
        bin = await cg.get_variable(config[CONF_CLOSE_ENDSTOP])
        cg.add(var.set_close_endstop(bin))
    cg.add(var.set_close_duration(config[CONF_CLOSE_DURATION]))

    await automation.build_automation(
        var.get_single_press_trigger(), [], config[CONF_SINGLE_PRESS_ACTION]
    )
    await automation.build_automation(
        var.get_double_press_trigger(), [], config[CONF_DOUBLE_PRESS_ACTION]
    )
