import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome import automation

CONF_EXPIRATION = "expiration"
AUTO_LOAD = ["sensor", "text_sensor"]


espnow_node_ns = cg.esphome_ns.namespace("espnow_node")
ESPNowNode = espnow_node_ns.class_("ESPNowNode", cg.Component)
aGatewayConnectedTrigger = espnow_node_ns.class_(
    "GatewayConnectedTrigger", automation.Trigger.template(cg.std_string)
)

aTransactionCompleteTrigger = espnow_node_ns.class_(
    "TransactionCompleteTrigger", automation.Trigger.template()
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ESPNowNode),
        cv.Optional(CONF_EXPIRATION, default="0s"): cv.positive_time_period_seconds,
        cv.Optional("on_gateway_connected"): automation.validate_automation(
            {
                cv.GenerateID(): cv.declare_id(aGatewayConnectedTrigger),
            }
        ),
        cv.Optional("on_transaction_complete"): automation.validate_automation(
            {
                cv.GenerateID(): cv.declare_id(aTransactionCompleteTrigger),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_expiration(config[CONF_EXPIRATION].total_seconds))

    if "on_gateway_connected" in config:
        for conf in config["on_gateway_connected"]:
            trigger = cg.new_Pvariable(conf[CONF_ID])
            cg.add(var.add_gateway_connected_trigger(trigger))
            await automation.build_automation(trigger, [(cg.std_string, "gateway_id")], conf)
    if "on_transaction_complete" in config:
        for conf in config["on_transaction_complete"]:
            trigger = cg.new_Pvariable(conf[CONF_ID])
            cg.add(var.add_transaction_complete_trigger(trigger))
            await automation.build_automation(trigger, [], conf)
