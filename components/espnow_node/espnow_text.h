#pragma once
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "espnow_entity_interface.h"
#include "esphome/core/log.h"
#include <string>
#include <functional>

namespace esphome {
namespace espnow_node {

class ESPNowNode;
struct LogString;

class ESPNowText : public esphome::text_sensor::TextSensor, public Component  , public ESPNowEntityInterface {
public:
    ESPNowText();

    void set_node(ESPNowNode *node);
    void publish_state(const std::string &value);
    void handle_sensor_packet(const SensorPacket &pkt);
    void set_component_source(const esphome::LogString *source) { component_source_ = source; }
    // void set_entity_id(const std::string &id) { entity_id_ = id; }
    std::string get_entity_id();
    void set_lambda(std::function<std::string()> &&lambda);
    void setup();
    void loop();
protected:
    ESPNowNode *node_;
    std::string entity_id_;
    std::function<std::string()> lambda_;
    std::string last_value_;
    const esphome::LogString *component_source_{nullptr};
};

}  // namespace espnow_node
}  // namespace esphome