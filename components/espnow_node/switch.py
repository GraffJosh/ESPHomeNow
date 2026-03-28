import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import espnow_node_ns, ESPNowNode

ESPNowSwitch = espnow_node_ns.class_("ESPNowSwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = switch.switch_schema(ESPNowSwitch).extend(
    {
        cv.GenerateID("node_id"): cv.use_id(ESPNowNode),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[switch.CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)
    node = await cg.get_variable(config["node_id"])
    cg.add(var.set_node(node))
    # cg.add(var.set_entity_id(config[CONF_ENTITY_ID]))
    # cg.add(node.register_entity(var))
