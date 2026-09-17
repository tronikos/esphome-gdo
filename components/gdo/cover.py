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
CONF_TRIPLE_PRESS_ACTION = "triple_press_action"
CONF_PRESS_WHILE_CLOSING = "press_while_closing"

PRESS_WHILE_CLOSING_OPEN = "open"
PRESS_WHILE_CLOSING_STOP = "stop"

PressWhileClosing = gdo_ns.enum("PressWhileClosing")
PRESS_WHILE_CLOSING = {
    PRESS_WHILE_CLOSING_OPEN: PressWhileClosing.PRESS_WHILE_CLOSING_OPENS,
    PRESS_WHILE_CLOSING_STOP: PressWhileClosing.PRESS_WHILE_CLOSING_STOPS,
}


def _validate_triple_press_action(config):
    """Require the triple press on openers that stop a closing door.

    Such an opener sends the door against the direction it last moved in, so
    getting a door that was stopped part-way to carry on the same way needs
    three presses: start it back, stop it, then go.
    """
    if (
        config[CONF_PRESS_WHILE_CLOSING] == PRESS_WHILE_CLOSING_STOP
        and CONF_TRIPLE_PRESS_ACTION not in config
    ):
        raise cv.Invalid(
            f"'{CONF_TRIPLE_PRESS_ACTION}' is required with "
            f"'{CONF_PRESS_WHILE_CLOSING}: {PRESS_WHILE_CLOSING_STOP}', since "
            "resuming travel in the direction the door was already going takes "
            "three presses on such an opener.",
            path=[CONF_TRIPLE_PRESS_ACTION],
        )
    return config


CONFIG_SCHEMA = cv.All(
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
            cv.Optional(CONF_TRIPLE_PRESS_ACTION): automation.validate_automation(
                single=True
            ),
            # What the opener does when the button is pressed while the door is
            # closing. Every other press is the same on all openers.
            cv.Optional(CONF_PRESS_WHILE_CLOSING, default="open"): cv.enum(
                PRESS_WHILE_CLOSING, lower=True
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    # With no endstop at all this degrades to a plain time-based cover that can
    # never correct its drift, which is almost certainly a config mistake.
    cv.has_at_least_one_key(CONF_OPEN_ENDSTOP, CONF_CLOSE_ENDSTOP),
    _validate_triple_press_action,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)

    if CONF_OPEN_ENDSTOP in config:
        endstop = await cg.get_variable(config[CONF_OPEN_ENDSTOP])
        cg.add(var.set_open_endstop(endstop))
    cg.add(var.set_open_duration(config[CONF_OPEN_DURATION]))
    if CONF_CLOSE_ENDSTOP in config:
        endstop = await cg.get_variable(config[CONF_CLOSE_ENDSTOP])
        cg.add(var.set_close_endstop(endstop))
    cg.add(var.set_close_duration(config[CONF_CLOSE_DURATION]))
    cg.add(var.set_press_while_closing(config[CONF_PRESS_WHILE_CLOSING]))

    await automation.build_automation(
        var.get_single_press_trigger(), [], config[CONF_SINGLE_PRESS_ACTION]
    )
    await automation.build_automation(
        var.get_double_press_trigger(), [], config[CONF_DOUBLE_PRESS_ACTION]
    )
    if CONF_TRIPLE_PRESS_ACTION in config:
        await automation.build_automation(
            var.get_triple_press_trigger(), [], config[CONF_TRIPLE_PRESS_ACTION]
        )
