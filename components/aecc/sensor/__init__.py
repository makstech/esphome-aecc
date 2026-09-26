import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS

from .. import CONF_AECC_ID, AeccComponent, AeccSensor

DEPENDENCIES = ["aecc"]

CONF_REGISTERS = "registers"
CONF_INTERVAL = "interval"
CONF_SIGNED = "signed"
CONF_SCALE = "scale"

# The readings everyone wants are created by the hub. This platform exists for a register
# the component does not model.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_AECC_ID): cv.use_id(AeccComponent),
        cv.Required(CONF_REGISTERS): cv.ensure_list(
            sensor.sensor_schema(AeccSensor).extend(
                {
                    cv.Required(CONF_ADDRESS): cv.hex_uint16_t,
                    cv.Optional(CONF_SIGNED, default=False): cv.boolean,
                    cv.Optional(CONF_SCALE, default=1.0): cv.float_,
                    cv.Optional(CONF_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
                }
            )
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AECC_ID])
    for conf in config[CONF_REGISTERS]:
        var = await sensor.new_sensor(conf)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(parent))
        cg.add(var.set_address(conf[CONF_ADDRESS]))
        cg.add(var.set_signed(conf[CONF_SIGNED]))
        cg.add(var.set_scale(conf[CONF_SCALE]))
        cg.add(var.set_interval(conf[CONF_INTERVAL]))
