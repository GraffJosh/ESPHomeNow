#pragma once
#include "espnow_def.h"

namespace esphome {
namespace espnow_node {

// Interface that all entities must implement
class ESPNowEntityInterface {
 public:
  virtual ~ESPNowEntityInterface() = default;

  // Called by the node when a packet arrives
  virtual void handle_sensor_packet(const SensorPacket &pkt) = 0;
  virtual std::string get_entity_id() = 0;
  EntityDiscoveryPacket* discoveryPacket;
};

}  // namespace espnow_node
}  // namespace esphome