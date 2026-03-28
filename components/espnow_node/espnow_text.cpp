#include "espnow_text.h"
#include "espnow_node.h"
#include "esp_log.h"

namespace esphome {
namespace espnow_node {

static const char *TAG = "espnow_text";

ESPNowText::ESPNowText() : node_(nullptr) {}

void ESPNowText::set_node(ESPNowNode *node) {
    node_ = node;

    // Discovery packet
    discoveryPacket = new EntityDiscoveryPacket();
    discoveryPacket->header.type = PacketType::ENTITY_DISCOVERY;
    discoveryPacket->entity.type = EntityType::TEXT;

    safe_copy(discoveryPacket->entity.id, get_entity_id().c_str(), sizeof(discoveryPacket->entity.id));
    entity_id_ = discoveryPacket->entity.id;

    safe_copy(discoveryPacket->name, get_name().c_str(), sizeof(discoveryPacket->name));
    safe_copy(discoveryPacket->unit, "", sizeof(discoveryPacket->unit));

    if(node_)
        node_->register_entity(this);

    ESP_LOGD(TAG, "TextSensor '%s' registered with entity_id: %s", get_name().c_str(), entity_id_.c_str());
}

std::string ESPNowText::get_entity_id() {
    char buf[128];
    std::span<char, 128> idSpan(buf);
    get_object_id_to(idSpan);
    return std::string(buf);
}

void ESPNowText::publish_state(const std::string &value) {
    // Only publish if different from last value
    if (value != last_value_) {
        last_value_ = value;
        // publish_state would be implemented in the base ESPHome integration
        // this->publish_state(value);
        if (node_) node_->send_sensor_state(entity_id_.c_str(), value);
        ESP_LOGD(TAG, "TextSensor '%s' value: %s", get_name().c_str(), value.c_str());
    }
}

void ESPNowText::handle_sensor_packet(const SensorPacket &pkt) {
    if (entity_id_ == std::string(pkt.sensor.entity.id)) {
        // Convert incoming value to string
        publish_state(std::string( pkt.sensor.data.textValue));
    }

    ESP_LOGD(TAG,
        "TextSensor '%s' received packet for entity_id '%s' with value: %s",
        this->get_name().c_str(),
        pkt.sensor.entity.id,
        pkt.sensor.data.textValue
    );
}
void ESPNowText::set_lambda(std::function<std::string()> &&lambda) {
    lambda_ = std::move(lambda);
}
void ESPNowText::setup() {
    // normal setup
    Component::setup();
    // call lambda once immediately    
    if (this->lambda_) {
        auto value = this->lambda_();
        this->publish_state(value);
    }
}
void ESPNowText::loop() {
    if (lambda_) {
        publish_state(lambda_());
    }
}

}  // namespace espnow_node
}  // namespace esphome