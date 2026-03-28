import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button

from . import espnow_node_ns, ESPNowNode

AUTO_LOAD = ["button"]

ESPNowButton = espnow_node_ns.class_("ESPNowButton", button.Button, cg.Component)

CONFIG_SCHEMA = button.button_schema(ESPNowButton).extend(
    {
        cv.GenerateID("node_id"): cv.use_id(ESPNowNode),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[button.CONF_ID])
    await cg.register_component(var, config)
    await button.register_button(var, config)
    node = await cg.get_variable(config["node_id"])
    cg.add(var.set_node(node))
