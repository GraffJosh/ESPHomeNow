#include "espnow_button.h"
#include "espnow_node.h"

namespace esphome {
namespace espnow_node {
static const char *TAG = "espnow_button";

ESPNowButton::ESPNowButton() : node_(nullptr) {}
void ESPNowButton::set_node(ESPNowNode *node) { 
    node_ = node; 
    discoveryPacket = new EntityDiscoveryPacket();
    discoveryPacket->header.type = PacketType::ENTITY_DISCOVERY;
    discoveryPacket->entity.type = EntityType::BUTTON;
    
    safe_copy(discoveryPacket->entity.id,get_entity_id(),sizeof(discoveryPacket->entity.id));
    entity_id_ = discoveryPacket->entity.id;
    ESP_LOGD(TAG,"Creating packet:%d, %s",sizeof(discoveryPacket->entity.id), discoveryPacket->entity.id);
    
    safe_copy(discoveryPacket->name,get_name(),sizeof(discoveryPacket->name));
    safe_copy(discoveryPacket->unit,"",sizeof(discoveryPacket->unit));
    node_->register_entity(this);
}

// void ESPNowButton::set_entity_id(const std::string &id) { 
//     strcpy(discoveryPacket->entity.id, id.c_str());
// }
std::string ESPNowButton::get_entity_id() { 
    char buf[128];
    std::span<char, 128> idSpan(buf);
    get_object_id_to(idSpan);
    // Create a std::string from the span (makes a copy)
    std::string temp_str(buf);
    return temp_str; 
}
void ESPNowButton::press_action() {
    ESP_LOGD(TAG, "Button '%s' pressed", this->get_name().c_str());

    if (node_) {
        // Send a "pressed" event (stateless)
        node_->send_sensor_state(entity_id_.c_str(), 1.0f);
    }
}
void ESPNowButton::handle_sensor_packet(const SensorPacket &pkt) {
    if (entity_id_ == std::string(pkt.sensor.entity.id)) {
        ESP_LOGD(TAG,
                 "Button '%s' received trigger for entity_id '%s' (value: %f)",
                 this->get_name().c_str(),
                 pkt.sensor.entity.id,
                 pkt.sensor.data.value);

        this->press();
    }
}

}  // namespace espnow_node
}  // namespace esphome