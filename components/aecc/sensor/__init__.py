import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_WATT,
)

import esphome.final_validate as fv

from .. import CONF_AECC_ID, CONF_CONTROL, AeccComponent, aecc_ns

DEPENDENCIES = ["aecc"]

AeccSensor = aecc_ns.class_("AeccSensor", sensor.Sensor, cg.Component)
Metric = aecc_ns.enum("Metric", is_class=True)

CONF_REGISTERS = "registers"
CONF_INTERVAL = "interval"
CONF_SIGNED = "signed"
CONF_SCALE = "scale"

_WATTS = dict(
    unit_of_measurement=UNIT_WATT,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_POWER,
    state_class=STATE_CLASS_MEASUREMENT,
)

# Sign conventions are the inverter's own: battery positive discharges, grid positive
# exports.
PRESETS = {
    "soc": dict(
        address=0xFE06,
        signed=False,
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_BATTERY,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    "battery_power": dict(address=0xFE07, signed=True, **_WATTS),
    "grid_power": dict(address=0xFE0A, signed=True, **_WATTS),
    "backup_load": dict(address=0xFE10, signed=False, **_WATTS),
    "setpoint": dict(address=0xFE16, signed=True, **_WATTS),
    "losses": dict(address=0xFE08, signed=False, **_WATTS),
}


# Not registers: what the control loop itself is doing. Meter age is the one to watch
# when the meter is on the network rather than on a wire.
DIAGNOSTICS = {
    "meter_power": dict(metric="GRID_W", **_WATTS),
    "meter_power_filtered": dict(metric="GRID_FILTERED_W", **_WATTS),
    "commanded_power": dict(metric="COMMAND_W", **_WATTS),
    "meter_age": dict(
        metric="METER_AGE_S",
        unit_of_measurement=UNIT_SECOND,
        accuracy_decimals=2,
        device_class=DEVICE_CLASS_DURATION,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "loop_rate": dict(
        metric="LOOP_HZ",
        unit_of_measurement=UNIT_HERTZ,
        accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}


def _diagnostic_schema(spec):
    fields = {k: v for k, v in spec.items() if k != "metric"}
    return sensor.sensor_schema(AeccSensor, **fields).extend(
        {
            cv.Optional(CONF_INTERVAL, default="5s"): cv.positive_time_period_milliseconds,
        }
    )


def _preset_schema(spec):
    fields = {k: v for k, v in spec.items() if k not in ("address", "signed")}
    return sensor.sensor_schema(AeccSensor, **fields).extend(
        {
            cv.Optional(CONF_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
        }
    )


# Listing the platform with nothing under it gives the readings almost everyone wants.
# Naming any of them switches that off, so the default is a starting point rather than
# something to work around.
DEFAULTS = {
    "soc": "Battery SOC",
    "battery_power": "Battery power",
    "grid_power": "Grid power",
    "backup_load": "Backup load",
}


def _fill_defaults(config):
    if not isinstance(config, dict):
        return config
    named = set(PRESETS) | set(DIAGNOSTICS) | {CONF_REGISTERS}
    if any(key in config for key in named):
        return config
    config = dict(config)
    for key, name in DEFAULTS.items():
        config[key] = {"name": name}
    return config


CONFIG_SCHEMA = cv.All(
    _fill_defaults,
    cv.Schema(
    {
        cv.GenerateID(CONF_AECC_ID): cv.use_id(AeccComponent),
        cv.Optional(CONF_REGISTERS): cv.ensure_list(
            sensor.sensor_schema(AeccSensor).extend(
                {
                    cv.Required(CONF_ADDRESS): cv.hex_uint16_t,
                    cv.Optional(CONF_SIGNED, default=False): cv.boolean,
                    cv.Optional(CONF_SCALE, default=1.0): cv.float_,
                    cv.Optional(CONF_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
                }
            )
        ),
        **{cv.Optional(name): _preset_schema(spec) for name, spec in PRESETS.items()},
        **{cv.Optional(name): _diagnostic_schema(spec) for name, spec in DIAGNOSTICS.items()},
    }
    ),
)


def _final_validate(config):
    hub = fv.full_config.get().get("aecc")
    if not isinstance(hub, dict):
        return config
    # The controller owns the meter, so reading it needs both.
    needs_meter = [k for k in ("meter_power", "meter_power_filtered", "meter_age") if k in config]
    if needs_meter and ("meter" not in hub or CONF_CONTROL not in hub):
        verb = "needs" if len(needs_meter) == 1 else "need"
        raise cv.Invalid(f"{', '.join(needs_meter)} {verb} a meter and control:")
    needs_control = [k for k in ("commanded_power", "loop_rate") if k in config]
    if needs_control and CONF_CONTROL not in hub:
        verb = "reports" if len(needs_control) == 1 else "report"
        raise cv.Invalid(f"{', '.join(needs_control)} {verb} on the control loop, but "
                         "control: is not configured")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def _new(conf, parent, address, is_signed, scale):
    var = await sensor.new_sensor(conf)
    await cg.register_component(var, conf)
    cg.add(var.set_parent(parent))
    cg.add(var.set_address(address))
    cg.add(var.set_signed(is_signed))
    cg.add(var.set_scale(scale))
    cg.add(var.set_interval(conf[CONF_INTERVAL]))


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AECC_ID])

    for name, spec in PRESETS.items():
        if name in config:
            await _new(config[name], parent, spec["address"], spec["signed"], 1.0)

    for name, spec in DIAGNOSTICS.items():
        if name not in config:
            continue
        conf = config[name]
        var = await sensor.new_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_metric(getattr(Metric, spec["metric"])))
        cg.add(var.set_interval(conf[CONF_INTERVAL]))

    for conf in config.get(CONF_REGISTERS, []):
        await _new(conf, parent, conf[CONF_ADDRESS], conf[CONF_SIGNED], conf[CONF_SCALE])
