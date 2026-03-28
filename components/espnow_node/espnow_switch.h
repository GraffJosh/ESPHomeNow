#pragma once
#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "espnow_entity_interface.h"
#include "util.h"

namespace esphome {
namespace espnow_node {

class ESPNowNode;

class ESPNowSwitch : public esphome::switch_::Switch, public Component,
                     public ESPNowEntityInterface {
    public:
        ESPNowSwitch();

        void set_node(ESPNowNode *node);
        void write_state(bool state);
        void handle_sensor_packet(const SensorPacket &pkt);
        std::string get_entity_id() ;
    protected:
        ESPNowNode *node_;
        std::string entity_id_;
};

}  // namespace espnow_node
}  // namespace esphome