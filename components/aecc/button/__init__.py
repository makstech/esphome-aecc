import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from .. import CONF_AECC_ID, AeccComponent, aecc_ns

DEPENDENCIES = ["aecc"]

AeccBackupButton = aecc_ns.class_("AeccBackupButton", button.Button, cg.Component)

CONF_BACKUP = "backup"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_AECC_ID): cv.use_id(AeccComponent),
        cv.Optional(CONF_BACKUP): button.button_schema(
            AeccBackupButton, entity_category=ENTITY_CATEGORY_CONFIG
        ).extend(cv.COMPONENT_SCHEMA),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AECC_ID])
    if CONF_BACKUP in config:
        var = await button.new_button(config[CONF_BACKUP])
        await cg.register_component(var, config[CONF_BACKUP])
        cg.add(var.set_parent(parent))
