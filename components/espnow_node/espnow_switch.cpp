#include "espnow_switch.h"
#include "espnow_node.h"

namespace esphome {
namespace espnow_node {
static const char *TAG = "espnow_switch";

ESPNowSwitch::ESPNowSwitch() : node_(nullptr) {}
void ESPNowSwitch::set_node(ESPNowNode *node) { 
    node_ = node; 
    discoveryPacket = new EntityDiscoveryPacket();
    discoveryPacket->header.type = PacketType::ENTITY_DISCOVERY;
    discoveryPacket->entity.type = EntityType::SWITCH;
    
    safe_copy(discoveryPacket->entity.id,get_entity_id(),sizeof(discoveryPacket->entity.id));
    entity_id_ = discoveryPacket->entity.id;
    ESP_LOGD(TAG,"Creating packet:%d, %s",sizeof(discoveryPacket->entity.id), discoveryPacket->entity.id);
    
    safe_copy(discoveryPacket->name,get_name(),sizeof(discoveryPacket->name));
    safe_copy(discoveryPacket->unit,"",sizeof(discoveryPacket->unit));
    node_->register_entity(this);
}

// void ESPNowSwitch::set_entity_id(const std::string &id) { 
//     strcpy(discoveryPacket->entity.id, id.c_str());
// }
std::string ESPNowSwitch::get_entity_id() { 
    char buf[128];
    std::span<char, 128> idSpan(buf);
    get_object_id_to(idSpan);
    // Create a std::string from the span (makes a copy)
    std::string temp_str(buf);
    return temp_str; 
}
void ESPNowSwitch::write_state(bool state) {
    // tell ESPHome/HA the switch state changed
    this->publish_state(state);
    if (node_) node_->send_sensor_state(entity_id_.c_str(), state ? 1.0f : 0.0f);
    ESP_LOGD(TAG, "Switch '%s' state changed to: %s", this->get_name().c_str(), ONOFF(state));
}

void ESPNowSwitch::handle_sensor_packet(const SensorPacket &pkt) {
    if (entity_id_ == std::string(pkt.sensor.entity.id))
        write_state(pkt.sensor.data.value != 0);
    ESP_LOGD(TAG, "Switch '%s' received sensor packet for entity_id '%s' with value: %f", this->get_name().c_str(), pkt.sensor.entity.id, pkt.sensor.data.value);
}

}  // namespace espnow_node
}  // namespace esphome