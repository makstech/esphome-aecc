import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

import esphome.final_validate as fv

from .. import CONF_AECC_ID, CONF_ZERO_EXPORT, AeccComponent, aecc_ns

DEPENDENCIES = ["aecc"]

AeccBinarySensor = aecc_ns.class_("AeccBinarySensor", binary_sensor.BinarySensor, cg.Component)
Health = aecc_ns.enum("Health", is_class=True)

# Inverted device class: these read true when healthy, and "problem" is shown when false.
HEALTH = {
    "control_effective": "CONTROL_EFFECTIVE",
    "meter_ok": "METER_OK",
    "ems_ready": "EMS_READY",
}


def _schema():
    return binary_sensor.binary_sensor_schema(
        AeccBinarySensor,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ).extend(cv.COMPONENT_SCHEMA)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_AECC_ID): cv.use_id(AeccComponent),
        **{cv.Optional(name): _schema() for name in HEALTH},
    }
)


def _final_validate(config):
    hub = fv.full_config.get().get("aecc")
    if not isinstance(hub, dict):
        return config
    if CONF_ZERO_EXPORT not in hub and any(k in config for k in ("control_effective", "meter_ok")):
        raise cv.Invalid("control_effective and meter_ok report on the control loop, but "
                         "zero_export is not configured")
    if "ems_ready" in config and "datalogger" not in hub:
        raise cv.Invalid("ems_ready needs a datalogger")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AECC_ID])
    for name, metric in HEALTH.items():
        if name not in config:
            continue
        var = await binary_sensor.new_binary_sensor(config[name])
        await cg.register_component(var, config[name])
        cg.add(var.set_parent(parent))
        cg.add(var.set_health(getattr(Health, metric)))
