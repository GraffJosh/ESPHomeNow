import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_LAMBDA
from esphome.components import sensor, text_sensor

from . import espnow_node_ns, ESPNowNode

ESPNowText = espnow_node_ns.class_("ESPNowText", cg.Component)

CONFIG_SCHEMA = text_sensor.text_sensor_schema(ESPNowText).extend(
    {
        cv.GenerateID("node_id"): cv.use_id(ESPNowNode),
        cv.Optional(CONF_LAMBDA): cv.lambda_,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await text_sensor.register_text_sensor(var, config)

    node = await cg.get_variable(config["node_id"])
    cg.add(var.set_node(node))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            return_type=cg.std_string,
            parameters=[],
        )
        cg.add(var.set_lambda(lambda_))
