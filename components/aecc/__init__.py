import logging

import esphome.codegen as cg
from esphome import automation
from esphome.components import button, number, select, switch, text, uart, web_server_base

# Aliased: aecc has a sensor/ sub-platform, and importing it rebinds the same name on this
# package, shadowing the global by the time to_code runs.
from esphome.components import binary_sensor as bs_platform
from esphome.components import sensor as sensor_platform
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_INITIAL_VALUE,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_NAME,
    CONF_PORT,
    CONF_RESTORE_VALUE,
    CONF_STEP,
    CONF_UART_ID,
    CONF_VALUE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    UNIT_PERCENT,
    UNIT_WATT,
)

from .settings import NUMBERS, SELECTS, SWITCHES
from .telemetry import DIAGNOSTICS, HEALTH, PRESETS

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@makstech"]
DEPENDENCIES = ["uart"]
# datalogger.cpp parses the unit's JSON API.
AUTO_LOAD = ["binary_sensor", "button", "json", "number", "select", "sensor", "switch", "text"]

aecc_ns = cg.esphome_ns.namespace("aecc")
AeccComponent = aecc_ns.class_("AeccComponent", cg.Component)
MeterSource = aecc_ns.class_("MeterSource")
Rs071Meter = aecc_ns.class_("Rs071Meter", MeterSource)
Controller = aecc_ns.class_("Controller")
WriteRegisterAction = aecc_ns.class_("WriteRegisterAction", automation.Action)
BackupHandler = aecc_ns.class_("BackupHandler", cg.Component)
AeccNumber = aecc_ns.class_("AeccNumber", number.Number, cg.Component)
AeccSwitch = aecc_ns.class_("AeccSwitch", switch.Switch, cg.Component)
AeccSelect = aecc_ns.class_("AeccSelect", select.Select, cg.Component)
ControlModeSelect = aecc_ns.class_("ControlModeSelect", select.Select, cg.Component)
ControlMode = aecc_ns.enum("ControlMode", is_class=True)
WorkModeSelect = aecc_ns.class_("WorkModeSelect", select.Select, cg.Component)
WorkMode = aecc_ns.enum("WorkMode", is_class=True)
DataloggerHostText = aecc_ns.class_("DataloggerHostText", text.Text, cg.Component)
DataloggerSwitch = aecc_ns.class_("DataloggerSwitch", switch.Switch, cg.Component)
AeccSensor = aecc_ns.class_("AeccSensor", sensor_platform.Sensor, cg.Component)
Metric = aecc_ns.enum("Metric", is_class=True)
AeccBinarySensor = aecc_ns.class_("AeccBinarySensor", bs_platform.BinarySensor, cg.Component)
Health = aecc_ns.enum("Health", is_class=True)
AeccBackupButton = aecc_ns.class_("AeccBackupButton", button.Button, cg.Component)
ControlParam = aecc_ns.enum("ControlParam", is_class=True)
RestoreHandler = aecc_ns.class_("RestoreHandler", cg.Component)

CONF_AECC_ID = "aecc_id"
CONF_UNIT = "unit"
CONF_METER = "meter"
CONF_CONTROL = "control"
CONF_RATE = "rate"
CONF_FILTER_WINDOW = "filter_window"
CONF_GRID_TARGET = "grid_target"
CONF_MAX_DISCHARGE = "max_discharge"
CONF_MAX_CHARGE = "max_charge"
CONF_MIN_SOC = "min_soc"
CONF_MAX_SOC = "max_soc"
CONF_RAMP_UP = "ramp_up"
CONF_STALE_AFTER = "stale_after"
CONF_REGISTER = "register"
CONF_BACKUP = "backup"
CONF_DATALOGGER = "datalogger"
CONF_HOST = "host"
CONF_REPLY_WINDOW = "reply_window"
CONF_INTERVAL = "interval"
CONF_SCALE = "scale"
CONF_REGISTERS = "registers"
CONF_RESTING_POWER = "resting_power"
CONF_RECONCILE_INTERVAL = "reconcile_interval"
CONF_URL = "url"
CONF_RESTORE_ID = "restore_id"
CONF_RESTORE_URL = "restore_url"
CONF_MODE_SELECT = "mode_select"
CONF_SETPOINT = "setpoint"
CONF_WORK_MODE_SELECT = "work_mode_select"
CONF_HOST_TEXT = "host_text"
CONF_BUTTON = "button"
CONF_ENABLE_SWITCH = "enable_switch"
CONF_MIRROR = "mirror"
CONF_SIGNED = "signed"


def _block(schema):
    """`key:` with nothing under it means the block with all of its defaults."""

    def validate(value):
        return schema(value if value is not None else {})

    return validate


def _named(schema, default_name=None):
    """A bare string is the entity's name; a mapping is the full schema."""

    def validate(value):
        if not isinstance(value, dict):
            return schema({CONF_NAME: value})
        if default_name is not None and CONF_NAME not in value and CONF_ID not in value:
            value = {CONF_NAME: default_name, **value}
        return schema(value)

    return validate


def _sensor(spec):
    """A reading. Created by default, so naming it only changes the label."""
    fields = {k: v for k, v in spec.items()
              if k not in ("address", "signed", "metric", "needs", "name", "interval")}
    return _named(
        sensor_platform.sensor_schema(AeccSensor, **fields)
        .extend({cv.Optional(CONF_INTERVAL, default=spec.get("interval", "10s")):
                cv.positive_time_period_milliseconds})
        .extend(cv.COMPONENT_SCHEMA),
        spec["name"],
    )


def _binary_sensor(spec):
    return _named(
        bs_platform.binary_sensor_schema(
            AeccBinarySensor, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ).extend(cv.COMPONENT_SCHEMA),
        spec["name"],
    )


def _reports(config, spec):
    """Whether everything a reading depends on is configured."""
    keys = {"meter": CONF_METER, "control": CONF_CONTROL, "datalogger": CONF_DATALOGGER}
    return all(keys[n] in config for n in spec.get("needs") or ())


def _telemetry_schema():
    out = {}
    for key, spec in PRESETS.items():
        out[cv.Optional(f"{key}_sensor", default=spec["name"])] = _sensor(spec)
    for key, spec in DIAGNOSTICS.items():
        out[cv.Optional(f"{key}_sensor", default=spec["name"])] = _sensor(spec)
    for key, spec in HEALTH.items():
        out[cv.Optional(f"{key}_sensor", default=spec["name"])] = _binary_sensor(spec)
    return out


def _fits_register(conf):
    if conf[CONF_SCALE] == 0:
        raise cv.Invalid("scale cannot be zero", path=[CONF_SCALE])
    lo, hi = (0, 0xFFFF) if not conf[CONF_SIGNED] else (-0x8000, 0x7FFF)
    for bound in (CONF_MIN_VALUE, CONF_MAX_VALUE):
        raw = round(conf[bound] / conf[CONF_SCALE])
        if not lo <= raw <= hi:
            raise cv.Invalid(
                f"{conf[bound]} at scale {conf[CONF_SCALE]} is {raw}, "
                f"outside a 16-bit register ({lo} to {hi})",
                path=[bound],
            )
    return conf


def _number(spec):
    """A battery setting, defaulted from settings.py so that naming it is enough."""
    fields = {k: v for k, v in spec.items() if k in ("unit_of_measurement", "device_class")}
    return _named(
        number.number_schema(AeccNumber, entity_category=ENTITY_CATEGORY_CONFIG, **fields)
        .extend(
            {
                cv.Optional(CONF_MIN_VALUE, default=spec["min_value"]): cv.float_,
                cv.Optional(CONF_MAX_VALUE, default=spec["max_value"]): cv.float_,
                cv.Optional(CONF_STEP, default=spec["step"]): cv.positive_float,
                cv.Optional(CONF_ADDRESS, default=spec["address"]): cv.hex_uint16_t,
                cv.Optional(CONF_SCALE, default=spec["scale"]): cv.float_,
                cv.Optional(CONF_MIRROR, default=spec.get("mirror", 0)): cv.hex_uint16_t,
                cv.Optional(CONF_SIGNED, default=False): cv.boolean,
                cv.Optional(CONF_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
        .add_extra(_fits_register)
    )


def _control_number(lo, hi, step, unit):
    return _named(
        number.number_schema(AeccNumber, entity_category=ENTITY_CATEGORY_CONFIG,
                             unit_of_measurement=unit)
        .extend(
            {
                cv.Optional(CONF_MIN_VALUE, default=lo): cv.float_,
                cv.Optional(CONF_MAX_VALUE, default=hi): cv.float_,
                cv.Optional(CONF_STEP, default=step): cv.positive_float,
                cv.Optional(CONF_INITIAL_VALUE): cv.float_,
                cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean,
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
    )


REGISTER_SCHEMA = (
    number.number_schema(AeccNumber, entity_category=ENTITY_CATEGORY_CONFIG)
    .extend(
        {
            cv.Required(CONF_ADDRESS): cv.hex_uint16_t,
            cv.Optional(CONF_MIN_VALUE, default=0): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=65535): cv.float_,
            cv.Optional(CONF_STEP, default=1): cv.positive_float,
            cv.Optional(CONF_SCALE, default=1): cv.float_,
            cv.Optional(CONF_MIRROR, default=0): cv.hex_uint16_t,
            cv.Optional(CONF_SIGNED, default=False): cv.boolean,
            cv.Optional(CONF_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .add_extra(_fits_register)
)


def _switch(address):
    return _named(
        switch.switch_schema(AeccSwitch, entity_category=ENTITY_CATEGORY_CONFIG,
                             default_restore_mode="DISABLED")
        .extend(
            {
                cv.Optional(CONF_ADDRESS, default=address): cv.hex_uint16_t,
                cv.Optional(CONF_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
    )


def _select(spec):
    return _named(
        select.select_schema(AeccSelect, entity_category=ENTITY_CATEGORY_CONFIG)
        .extend(
            {
                cv.Optional(CONF_ADDRESS, default=spec["address"]): cv.hex_uint16_t,
                cv.Optional(CONF_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
    )

CONTROL_NUMBERS = {
    CONF_GRID_TARGET: (_control_number(0, 3000, 10, UNIT_WATT), "GRID_TARGET"),
    CONF_MAX_DISCHARGE: (_control_number(0, 2500, 50, UNIT_WATT), "MAX_DISCHARGE"),
    CONF_MAX_CHARGE: (_control_number(0, 2500, 50, UNIT_WATT), "MAX_CHARGE"),
    CONF_MIN_SOC: (_control_number(0, 100, 1, UNIT_PERCENT), "MIN_SOC"),
    CONF_MAX_SOC: (_control_number(0, 100, 1, UNIT_PERCENT), "MAX_SOC"),
}
RESTING_POWER_NUMBER = _control_number(-2000, -1, 10, UNIT_WATT)


def _suffixed(mapping, suffix):
    return {cv.Optional(f"{key}_{suffix}"): schema for key, schema in mapping.items()}


METER_SCHEMA = cv.typed_schema(
    {
        "rs071": cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(Rs071Meter),
                cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
                cv.Optional(CONF_UNIT, default=1): cv.int_range(min=0, max=255),
                cv.Optional(CONF_REGISTER, default=12): cv.uint16_t,
                # The meter answers in tens of milliseconds; waiting the inverter's
                # window on every failed poll is what starves the loop.
                cv.Optional(CONF_REPLY_WINDOW, default="120ms"): cv.All(
                    cv.positive_time_period_milliseconds,
                    cv.Range(max=cv.TimePeriod(milliseconds=1000)),
                ),
            }
        ),
    },
    lower=True,
)

DATALOGGER_SCHEMA = cv.Schema(
    {
        # An address, a hostname, or an mDNS name; resolved on the device. Optional, so
        # `datalogger:` alone gives you the text entity to set it from Home Assistant on a
        # unit whose address you do not know yet. Bounded by what that entity can store.
        cv.Optional(CONF_HOST, default=""): cv.All(cv.string_strict, cv.Length(max=63)),
        cv.Optional(CONF_PORT, default=8080): cv.port,
        # Negative charges. This is the state a dead controller leaves the unit in, and
        # charging cannot export at any load or state of charge.
        cv.Optional(CONF_RESTING_POWER, default=-300): cv.int_range(min=-20000, max=-1),
        cv.Optional(CONF_RECONCILE_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
        cv.Optional(f"{CONF_RESTING_POWER}_number"): RESTING_POWER_NUMBER,
        # Changing the address without reflashing, for a battery that moves on DHCP.
        cv.Optional(CONF_HOST_TEXT, default="Datalogger address"): _named(
            text.text_schema(DataloggerHostText, mode="TEXT").extend(cv.COMPONENT_SCHEMA),
            "Datalogger address",
        ),
        cv.Optional(CONF_ENABLE_SWITCH, default="Datalogger"): _named(
            switch.switch_schema(DataloggerSwitch, default_restore_mode="RESTORE_DEFAULT_ON")
            .extend(cv.COMPONENT_SCHEMA),
            "Datalogger",
        ),
    }
)

BACKUP_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BackupHandler),
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
            cv.GenerateID(CONF_RESTORE_ID): cv.declare_id(RestoreHandler),
            cv.Optional(CONF_URL, default="/aecc/backup"): cv.string_strict,
            cv.Optional(CONF_RESTORE_URL, default="/aecc/restore"): cv.string_strict,
            cv.Optional(CONF_BUTTON, default="Back up configuration"): _named(
                button.button_schema(AeccBackupButton, entity_category=ENTITY_CATEGORY_CONFIG)
                .extend(cv.COMPONENT_SCHEMA)
            ),
        }
    ),
    cv.requires_component("web_server"),
)

MODES = {
    "Off": "OFF",
    "Zero export": "ZERO_EXPORT",
    "Manual": "MANUAL",
}

WORK_MODES = {
    "Self-consumption": "SELF_CONSUMPTION",
    "Custom": "CUSTOM",
}

CONTROL_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Controller),
        cv.Optional(CONF_RATE, default="2Hz"): cv.frequency,
        cv.Optional(CONF_FILTER_WINDOW, default="1.5s"): cv.positive_time_period_milliseconds,
        # Holds a little import rather than sitting on zero, because the meter reading
        # swings tens of watts between samples and the margin keeps that noise out of
        # export. Negative exports deliberately.
        cv.Optional(CONF_GRID_TARGET, default=60): cv.int_range(min=-20000, max=20000),
        # Worst-case export on a sudden load drop is about (cap - baseload), because the
        # inverter needs about a second to wind down. Keep the cap near the site's
        # baseload; raising the loop rate does not help, lowering the cap does.
        cv.Optional(CONF_MAX_DISCHARGE, default=600): cv.int_range(min=0, max=20000),
        cv.Optional(CONF_MAX_CHARGE, default=2400): cv.int_range(min=0, max=20000),
        cv.Optional(CONF_MIN_SOC, default=15): cv.percentage_int,
        cv.Optional(CONF_MAX_SOC, default=90): cv.percentage_int,
        cv.Optional(CONF_RAMP_UP, default=0.35): cv.float_range(min=0.01, max=1.0),
        cv.Optional(CONF_STALE_AFTER, default="5s"): cv.positive_time_period_milliseconds,
        # Always created, because it is the only way to start the loop.
        cv.Optional(CONF_MODE_SELECT, default="Battery mode"): _named(
            select.select_schema(ControlModeSelect).extend(cv.COMPONENT_SCHEMA)
        ),
        cv.Optional(f"{CONF_SETPOINT}_number"): _control_number(-2500, 2500, 50, UNIT_WATT),
    }
).extend(_suffixed({k: v[0] for k, v in CONTROL_NUMBERS.items()}, "number"))


def _in_number_range(conf, key, number_key, path):
    """A value outside its own number's range is clamped away at boot, silently."""
    entry = conf.get(number_key)
    if entry is None or CONF_INITIAL_VALUE in entry:
        return
    if not entry[CONF_MIN_VALUE] <= conf[key] <= entry[CONF_MAX_VALUE]:
        raise cv.Invalid(
            f"{key} is {conf[key]}, outside what {number_key} allows "
            f"({entry[CONF_MIN_VALUE]} to {entry[CONF_MAX_VALUE]})",
            path=path + [key],
        )


def _validate(config):

    if CONF_DATALOGGER in config:
        _in_number_range(config[CONF_DATALOGGER], CONF_RESTING_POWER,
                         f"{CONF_RESTING_POWER}_number", [CONF_DATALOGGER])
    if CONF_CONTROL not in config:
        return config
    # Zero export needs a meter and manual needs a number to set; with neither, the mode
    # select would offer nothing but Off.
    if CONF_METER not in config and f"{CONF_SETPOINT}_number" not in config[CONF_CONTROL]:
        raise cv.Invalid("control: can only offer Off — add a meter: for zero export, or "
                         "a setpoint_number for manual")
    control = config[CONF_CONTROL]
    if control[CONF_MIN_SOC] >= control[CONF_MAX_SOC]:
        raise cv.Invalid("min_soc must be below max_soc", path=[CONF_CONTROL])
    for key in CONTROL_NUMBERS:
        _in_number_range(control, key, f"{key}_number", [CONF_CONTROL])
    if CONF_DATALOGGER not in config:
        # The setpoint register only modulates a command the energy manager is already
        # running, so without the datalogger to hold the schedule slot the loop writes a
        # register the battery ignores.
        _LOGGER.warning(
            "control without a datalogger: the battery ignores anything commanded "
            "unless its schedule slot is set up by hand"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AeccComponent),
            cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_UNIT, default=1): cv.int_range(min=0, max=255),
            cv.Optional(CONF_METER): METER_SCHEMA,
            cv.Optional(CONF_CONTROL): _block(CONTROL_SCHEMA),
            # Serving the backup over HTTP needs a web server; without it the button
            # still works and the result is fetched some other way.
            cv.Optional(CONF_BACKUP): _block(BACKUP_SCHEMA),
            # Reaches the EMS registers, which are not on Modbus. Without it the
            # schedule slot has to be set by hand and nothing keeps it there.
            cv.Optional(CONF_DATALOGGER): _block(DATALOGGER_SCHEMA),
            cv.Optional(CONF_REGISTERS, default=[]): cv.ensure_list(REGISTER_SCHEMA),
            # The battery's own scheduler mode, which is not on Modbus. Created with the
            # datalogger, because without it Off parks the battery with no way back.
            cv.Optional(CONF_WORK_MODE_SELECT, default="Work mode"): _named(
                select.select_schema(WorkModeSelect).extend(cv.COMPONENT_SCHEMA)
            ),
        }
    )
    .extend(_suffixed({k: _number(v) for k, v in NUMBERS.items()}, "number"))
    .extend(_suffixed({k: _switch(v) for k, v in SWITCHES.items()}, "switch"))
    .extend(_suffixed({k: _select(v) for k, v in SELECTS.items()}, "select"))
    .extend(_telemetry_schema())
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    _validate,
)


async def _register_number(parent, conf):
    var = await number.new_number(
        conf,
        min_value=conf[CONF_MIN_VALUE],
        max_value=conf[CONF_MAX_VALUE],
        step=conf[CONF_STEP],
    )
    await cg.register_component(var, conf)
    cg.add(var.set_parent(parent))
    cg.add(var.set_address(conf[CONF_ADDRESS]))
    cg.add(var.set_scale(conf[CONF_SCALE]))
    cg.add(var.set_signed(conf[CONF_SIGNED]))
    if conf[CONF_MIRROR]:
        cg.add(var.set_mirror(conf[CONF_MIRROR]))
    cg.add(var.set_poll_interval(conf[CONF_INTERVAL]))
    return var


async def _control_number_to_code(parent, conf, param, fallback):
    var = await number.new_number(
        conf,
        min_value=conf[CONF_MIN_VALUE],
        max_value=conf[CONF_MAX_VALUE],
        step=conf[CONF_STEP],
    )
    await cg.register_component(var, conf)
    cg.add(var.set_parent(parent))
    cg.add(var.set_control_param(getattr(ControlParam, param)))
    cg.add(var.set_initial_value(conf.get(CONF_INITIAL_VALUE, fallback)))
    cg.add(var.set_restore_value(conf[CONF_RESTORE_VALUE]))
    return var


async def _register_telemetry(parent, config):
    for key, spec in PRESETS.items():
        conf = config.get(f"{key}_sensor")
        if conf is None:
            continue
        var = await sensor_platform.new_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_address(spec["address"]))
        cg.add(var.set_signed(spec["signed"]))
        cg.add(var.set_scale(1.0))
        cg.add(var.set_interval(conf[CONF_INTERVAL]))

    for key, spec in DIAGNOSTICS.items():
        conf = config.get(f"{key}_sensor")
        # Silently absent rather than an error: these are created by default, so a config
        # without a meter should simply not get the meter's readings.
        if conf is None or not _reports(config, spec):
            continue
        var = await sensor_platform.new_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_metric(getattr(Metric, spec["metric"])))
        cg.add(var.set_interval(conf[CONF_INTERVAL]))

    for key, spec in HEALTH.items():
        conf = config.get(f"{key}_sensor")
        if conf is None or not _reports(config, spec):
            continue
        var = await bs_platform.new_binary_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_health(getattr(Health, spec["metric"])))


async def _register_entities(parent, config):
    for key, spec in NUMBERS.items():
        conf = config.get(f"{key}_number")
        if conf is not None:
            await _register_number(parent, conf)
    for key in SWITCHES:
        conf = config.get(f"{key}_switch")
        if conf is None:
            continue
        var = await switch.new_switch(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_address(conf[CONF_ADDRESS]))
        cg.add(var.set_poll_interval(conf[CONF_INTERVAL]))
    for key, spec in SELECTS.items():
        conf = config.get(f"{key}_select")
        if conf is None:
            continue
        var = await select.new_select(conf, options=list(spec["options"].values()))
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_address(conf[CONF_ADDRESS]))
        cg.add(var.set_poll_interval(conf[CONF_INTERVAL]))
        for value, label in spec["options"].items():
            cg.add(var.add_option(value, label))
    for conf in config[CONF_REGISTERS]:
        await _register_number(parent, conf)
    # Only reachable over the datalogger's API, so there is nothing to expose without one.
    conf = config.get(CONF_WORK_MODE_SELECT) if CONF_DATALOGGER in config else None
    if conf is not None:
        var = await select.new_select(conf, options=list(WORK_MODES))
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        for label, member in WORK_MODES.items():
            cg.add(var.add_mode(getattr(WorkMode, member), label))


async def to_code(config):
    # The bus task keeps running during an update; without this it cannot be told to stop.
    cg.add_define("USE_OTA_STATE_LISTENER")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    bus = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart(bus))
    cg.add(var.set_unit(config[CONF_UNIT]))

    if CONF_DATALOGGER in config:
        conf = config[CONF_DATALOGGER]
        cg.add(var.set_datalogger_host(str(conf[CONF_HOST])))
        cg.add(var.set_datalogger_port(conf[CONF_PORT]))
        cg.add(var.set_resting_power(conf[CONF_RESTING_POWER]))
        cg.add(var.set_reconcile_interval(conf[CONF_RECONCILE_INTERVAL]))
        entry = conf.get(CONF_HOST_TEXT)
        if entry is not None:
            host_text = await text.new_text(entry)
            await cg.register_component(host_text, entry)
            cg.add(host_text.set_parent(var))
            cg.add(host_text.set_default_host(str(conf[CONF_HOST])))
        entry = conf.get(CONF_ENABLE_SWITCH)
        if entry is not None:
            dl_switch = await switch.new_switch(entry)
            await cg.register_component(dl_switch, entry)
            cg.add(dl_switch.set_parent(var))
        if f"{CONF_RESTING_POWER}_number" in conf:
            await _control_number_to_code(
                var, conf[f"{CONF_RESTING_POWER}_number"], "RESTING_POWER",
                conf[CONF_RESTING_POWER],
            )

    meter = None
    if CONF_METER in config:
        conf = config[CONF_METER]
        meter = cg.new_Pvariable(conf[CONF_ID])
        meter_bus = await cg.get_variable(conf[CONF_UART_ID])
        cg.add(meter.set_uart(meter_bus))
        cg.add(meter.set_unit(conf[CONF_UNIT]))
        cg.add(meter.set_register(conf[CONF_REGISTER]))
        cg.add(meter.set_reply_window(conf[CONF_REPLY_WINDOW]))

    if CONF_CONTROL in config:
        conf = config[CONF_CONTROL]
        control = cg.new_Pvariable(conf[CONF_ID])
        if meter is not None:
            cg.add(control.set_meter(meter))
        cg.add(control.set_rate(conf[CONF_RATE]))
        cg.add(control.set_filter_window(conf[CONF_FILTER_WINDOW]))
        cg.add(control.set_grid_target(conf[CONF_GRID_TARGET]))
        cg.add(control.set_max_discharge(conf[CONF_MAX_DISCHARGE]))
        cg.add(control.set_max_charge(conf[CONF_MAX_CHARGE]))
        cg.add(control.set_min_soc(conf[CONF_MIN_SOC]))
        cg.add(control.set_max_soc(conf[CONF_MAX_SOC]))
        cg.add(control.set_ramp_up(conf[CONF_RAMP_UP]))
        cg.add(control.set_stale_after(conf[CONF_STALE_AFTER]))
        cg.add(var.set_control(control))

        # A mode with no way to steer it is worse than no mode: zero export regulates
        # against a meter, and manual does nothing without a number to set.
        unreachable = set()
        if meter is None:
            unreachable.add("ZERO_EXPORT")
        if f"{CONF_SETPOINT}_number" not in conf:
            unreachable.add("MANUAL")
        modes = {l: m for l, m in MODES.items() if m not in unreachable}
        mode = await select.new_select(conf[CONF_MODE_SELECT], options=list(modes))
        await cg.register_component(mode, conf[CONF_MODE_SELECT])
        cg.add(mode.set_parent(var))
        for label, member in modes.items():
            cg.add(mode.add_mode(getattr(ControlMode, member), label))

        setpoint = conf.get(f"{CONF_SETPOINT}_number")
        if setpoint is not None:
            await _control_number_to_code(var, setpoint, "MANUAL_SETPOINT", 0)
        for key, (_, param) in CONTROL_NUMBERS.items():
            entry = conf.get(f"{key}_number")
            if entry is not None:
                await _control_number_to_code(var, entry, param, conf[key])

    if CONF_BACKUP in config:
        conf = config[CONF_BACKUP]
        base = await cg.get_variable(conf[CONF_WEB_SERVER_BASE_ID])
        handler = cg.new_Pvariable(conf[CONF_ID], base)
        await cg.register_component(handler, conf)
        cg.add(handler.set_parent(var))
        cg.add(handler.set_url(conf[CONF_URL]))

        restore = cg.new_Pvariable(conf[CONF_RESTORE_ID], base)
        await cg.register_component(restore, conf)
        cg.add(restore.set_parent(var))
        cg.add(restore.set_url(conf[CONF_RESTORE_URL]))

        backup_button = await button.new_button(conf[CONF_BUTTON])
        await cg.register_component(backup_button, conf[CONF_BUTTON])
        cg.add(backup_button.set_parent(var))

    await _register_entities(var, config)
    await _register_telemetry(var, config)


@automation.register_action(
    "aecc.write_register",
    WriteRegisterAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(AeccComponent),
            cv.Required(CONF_ADDRESS): cv.templatable(cv.hex_uint16_t),
            cv.Required(CONF_VALUE): cv.templatable(cv.uint16_t),
        }
    ),
    # play() queues the write and returns; nothing is deferred.
    synchronous=True,
)
async def write_register_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    cg.add(var.set_address(await cg.templatable(config[CONF_ADDRESS], args, cg.uint16)))
    cg.add(var.set_value(await cg.templatable(config[CONF_VALUE], args, cg.uint16)))
    return var
