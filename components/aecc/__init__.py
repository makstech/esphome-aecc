import esphome.codegen as cg
from esphome import automation
from esphome.components import uart, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_PORT, CONF_UART_ID, CONF_VALUE

CODEOWNERS = ["@makstech"]
DEPENDENCIES = ["uart"]
# datalogger.cpp parses the unit's JSON API.
AUTO_LOAD = ["json"]

aecc_ns = cg.esphome_ns.namespace("aecc")
AeccComponent = aecc_ns.class_("AeccComponent", cg.Component)
MeterSource = aecc_ns.class_("MeterSource")
Rs071Meter = aecc_ns.class_("Rs071Meter", MeterSource)
ZeroExport = aecc_ns.class_("ZeroExport")
WriteRegisterAction = aecc_ns.class_("WriteRegisterAction", automation.Action)
BackupHandler = aecc_ns.class_("BackupHandler", cg.Component)
RestoreHandler = aecc_ns.class_("RestoreHandler", cg.Component)

CONF_AECC_ID = "aecc_id"
CONF_UNIT = "unit"
CONF_METER = "meter"
CONF_ZERO_EXPORT = "zero_export"
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
CONF_RESTING_POWER = "resting_power"
CONF_RECONCILE_INTERVAL = "reconcile_interval"
CONF_URL = "url"
CONF_RESTORE_ID = "restore_id"
CONF_RESTORE_URL = "restore_url"

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
                cv.Optional(CONF_REPLY_WINDOW, default='120ms'): cv.positive_time_period_milliseconds,
            }
        ),
    },
    lower=True,
)

DATALOGGER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HOST): cv.ipv4address,
        cv.Optional(CONF_PORT, default=8080): cv.port,
        # Negative charges. This is the state a dead controller leaves the unit in, and
        # charging cannot export at any load or state of charge.
        cv.Optional(CONF_RESTING_POWER, default=-300): cv.int_range(min=-20000, max=-1),
        cv.Optional(CONF_RECONCILE_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
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
        }
    ),
    cv.requires_component("web_server"),
)

ZERO_EXPORT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ZeroExport),
        cv.Optional(CONF_RATE, default="2Hz"): cv.frequency,
        cv.Optional(CONF_FILTER_WINDOW, default="1.5s"): cv.positive_time_period_milliseconds,
        # Holds a little import rather than sitting on zero, so meter noise does not
        # cross into export. The bundled CT read below an independent meter on the same
        # phase during testing, but the sign of that offset was never pinned down -
        # confirm it against a second meter on site before trusting a small target.
        # Negative exports deliberately.
        cv.Optional(CONF_GRID_TARGET, default=60): cv.int_range(min=-20000, max=20000),
        cv.Optional(CONF_MAX_DISCHARGE, default=600): cv.int_range(min=0, max=20000),
        # Worst-case export on a sudden load drop is about (cap - baseload), because the
        # inverter needs about a second to wind down. Keep the cap near the site's
        # baseload; raising the loop rate does not help, lowering the cap does.
        cv.Optional(CONF_MAX_CHARGE, default=2400): cv.int_range(min=0, max=20000),
        cv.Optional(CONF_MIN_SOC, default=15): cv.percentage_int,
        cv.Optional(CONF_MAX_SOC, default=90): cv.percentage_int,
        cv.Optional(CONF_RAMP_UP, default=0.35): cv.float_range(min=0.01, max=1.0),
        cv.Optional(CONF_STALE_AFTER, default="5s"): cv.positive_time_period_milliseconds,
    }
)


def _validate(config):
    if CONF_ZERO_EXPORT in config and CONF_METER not in config:
        raise cv.Invalid("zero_export needs a meter to regulate against")
    if CONF_ZERO_EXPORT in config:
        ze = config[CONF_ZERO_EXPORT]
        if ze[CONF_MIN_SOC] >= ze[CONF_MAX_SOC]:
            raise cv.Invalid("min_soc must be below max_soc", path=[CONF_ZERO_EXPORT])
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AeccComponent),
            cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            # Only 1 and 255 have ever answered. A unit whose configuration has been
            # wiped answers on the broadcast addresses alone.
            cv.Optional(CONF_UNIT, default=1): cv.int_range(min=0, max=255),
            cv.Optional(CONF_METER): METER_SCHEMA,
            cv.Optional(CONF_ZERO_EXPORT): ZERO_EXPORT_SCHEMA,
            # Serving the backup over HTTP needs a web server; without it the button
            # still works and the result is fetched some other way.
            cv.Optional(CONF_BACKUP): BACKUP_SCHEMA,
            # Reaches the EMS registers, which are not on Modbus. Without it the
            # schedule slot has to be set by hand and nothing keeps it there.
            cv.Optional(CONF_DATALOGGER): DATALOGGER_SCHEMA,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    _validate,
)


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

    meter = None
    if CONF_METER in config:
        conf = config[CONF_METER]
        meter = cg.new_Pvariable(conf[CONF_ID])
        meter_bus = await cg.get_variable(conf[CONF_UART_ID])
        cg.add(meter.set_uart(meter_bus))
        cg.add(meter.set_unit(conf[CONF_UNIT]))
        cg.add(meter.set_register(conf[CONF_REGISTER]))
        cg.add(meter.set_reply_window(conf[CONF_REPLY_WINDOW]))

    if CONF_ZERO_EXPORT in config:
        conf = config[CONF_ZERO_EXPORT]
        control = cg.new_Pvariable(conf[CONF_ID])
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
