import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_CONFIG,
    UNIT_WATT,
)

import esphome.final_validate as fv

from .. import CONF_AECC_ID, CONF_ZERO_EXPORT, AeccComponent, aecc_ns

DEPENDENCIES = ["aecc"]

AeccNumber = aecc_ns.class_("AeccNumber", number.Number, cg.Component)
ControlParam = aecc_ns.enum("ControlParam", is_class=True)

CONF_CONTROL = "control"
CONF_SCALE = "scale"
CONF_INTERVAL = "interval"

# The whole optimiser interface. The grid target defaults to a floor of zero, so an
# optimiser cannot command an export by accident; lower min_value deliberately where
# exporting is wanted and permitted.
CONTROLS = {
    "grid_target_w": (ControlParam.GRID_TARGET, 0, 20000, 1),
    "max_discharge_w": (ControlParam.MAX_DISCHARGE, 0, 2500, 10),
    "max_charge_w": (ControlParam.MAX_CHARGE, 0, 2500, 10),
}

BASE = number.number_schema(
    AeccNumber,
    unit_of_measurement=UNIT_WATT,
    device_class=DEVICE_CLASS_POWER,
    entity_category=ENTITY_CATEGORY_CONFIG,
)


def _validate(config):
    has_control = CONF_CONTROL in config
    has_register = CONF_ADDRESS in config
    if has_control == has_register:
        raise cv.Invalid("give exactly one of 'control' or 'address'")
    if has_register and CONF_MAX_VALUE not in config:
        raise cv.Invalid("a register-backed number needs max_value")
    return config


def _final_validate(config):
    if CONF_CONTROL not in config:
        return config
    hub = fv.full_config.get().get("aecc")
    if isinstance(hub, dict) and CONF_ZERO_EXPORT not in hub:
        raise cv.Invalid(
            f"'{config[CONF_CONTROL]}' is a control-loop parameter, but zero_export is "
            f"not configured on the aecc hub"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate

CONFIG_SCHEMA = cv.All(
    BASE.extend(
        {
            cv.GenerateID(CONF_AECC_ID): cv.use_id(AeccComponent),
            cv.Optional(CONF_CONTROL): cv.one_of(*CONTROLS, lower=True),
            cv.Optional(CONF_ADDRESS): cv.hex_uint16_t,
            cv.Optional(CONF_MIN_VALUE): cv.float_,
            cv.Optional(CONF_MAX_VALUE): cv.float_,
            cv.Optional(CONF_STEP): cv.positive_float,
            cv.Optional(CONF_SCALE, default=1.0): cv.float_,
            cv.Optional(CONF_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AECC_ID])

    if CONF_CONTROL in config:
        param, lo, hi, step = CONTROLS[config[CONF_CONTROL]]
        lo = config.get(CONF_MIN_VALUE, lo)
        hi = config.get(CONF_MAX_VALUE, hi)
        step = config.get(CONF_STEP, step)
    else:
        param = ControlParam.NONE
        lo = config.get(CONF_MIN_VALUE, 0)
        hi = config[CONF_MAX_VALUE]
        step = config.get(CONF_STEP, 1)

    var = await number.new_number(config, min_value=lo, max_value=hi, step=step)
    await cg.register_component(var, config)
    cg.add(var.set_parent(parent))
    cg.add(var.set_control_param(param))
    cg.add(var.set_scale(config[CONF_SCALE]))
    cg.add(var.set_interval(config[CONF_INTERVAL]))
    if CONF_ADDRESS in config:
        cg.add(var.set_address(config[CONF_ADDRESS]))
