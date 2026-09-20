import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

import esphome.final_validate as fv

from .. import CONF_AECC_ID, CONF_CONTROL, AeccComponent, aecc_ns

DEPENDENCIES = ["aecc"]

AeccBinarySensor = aecc_ns.class_("AeccBinarySensor", binary_sensor.BinarySensor, cg.Component)
Health = aecc_ns.enum("Health", is_class=True)

# No device class: these read true when healthy, which is the inverse of every
# health-shaped class Home Assistant offers.
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
    if "control_effective" in config and CONF_CONTROL not in hub:
        raise cv.Invalid("control_effective reports on the control loop, but control: is "
                         "not configured")
    # The controller owns the meter, so reading it needs both.
    if "meter_ok" in config and ("meter" not in hub or CONF_CONTROL not in hub):
        raise cv.Invalid("meter_ok needs a meter and control:")
    if "ems_ready" in config and CONF_CONTROL not in hub:
        raise cv.Invalid("ems_ready needs control: — the scheduler is only asserted while "
                         "a mode is running")
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
