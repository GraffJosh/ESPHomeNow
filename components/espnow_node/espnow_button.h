#pragma once
#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "espnow_entity_interface.h"
#include "util.h"

namespace esphome {
namespace espnow_node {

class ESPNowNode;

class ESPNowButton : public esphome::button::Button,
                     public Component,
                     public ESPNowEntityInterface {
 public:
  ESPNowButton();

  void set_node(ESPNowNode *node);

  // Called when Home Assistant / ESPHome presses the button
  void press_action() override;

  // Handle incoming ESP-NOW packets (optional depending on your use case)
  void handle_sensor_packet(const SensorPacket &pkt);

  std::string get_entity_id();

 protected:
  ESPNowNode *node_;
  std::string entity_id_;
};

}  // namespace espnow_node
}  // namespace esphome