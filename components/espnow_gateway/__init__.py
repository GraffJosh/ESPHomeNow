import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, UNIT_EMPTY, ICON_EMPTY
from esphome.components import sensor, text_sensor
from esphome import automation

AUTO_LOAD = ["sensor", "text_sensor"]
espnow_gateway_ns = cg.esphome_ns.namespace("espnow_gateway")

ESPNowMQTTGateway = espnow_gateway_ns.class_("ESPNowMQTTGateway", cg.Component)
NodeJoinedTrigger = espnow_gateway_ns.class_(
    "NodeJoinedTrigger", automation.Trigger.template(cg.std_string)
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ESPNowMQTTGateway),
        cv.Optional("num_nodes"): sensor.sensor_schema(),
        cv.Optional("nodes_list"): text_sensor.text_sensor_schema(),
        cv.Optional("nodes_json"): text_sensor.text_sensor_schema(),
        cv.Optional("on_node_joined"): automation.validate_automation(
            {
                cv.GenerateID(): cv.declare_id(NodeJoinedTrigger),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if "num_nodes" in config:
        sens = await sensor.new_sensor(config["num_nodes"])
        cg.add(var.set_num_nodes_sensor(sens))

    if "nodes_list" in config:
        ts = await text_sensor.new_text_sensor(config["nodes_list"])
        cg.add(var.set_nodes_list_sensor(ts))

    if "nodes_json" in config:
        ts = await text_sensor.new_text_sensor(config["nodes_json"])
        cg.add(var.set_nodes_json_sensor(ts))

    if "on_node_joined" in config:
        for conf in config["on_node_joined"]:
            trigger = cg.new_Pvariable(conf[CONF_ID])
            cg.add(var.add_node_joined_trigger(trigger))
            await automation.build_automation(trigger, [(cg.std_string, "node_name")], conf)
