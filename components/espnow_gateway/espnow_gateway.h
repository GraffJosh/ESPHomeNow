#pragma once
#include "espnow_def.h"
#include "esphome/core/component.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#include <ArduinoJson.h>
#include "espnow_def.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"


#include <map>
#include <set>
#include <string>
#include <esp_now.h>
#include <esp_wifi.h>
#include "esphome/core/automation.h"



namespace esphome {
    
    namespace espnow_gateway {
class NodeJoinedTrigger : public Trigger<std::string> {};

class ESPNowMQTTGateway : public Component {
enum class nodeState {
    ONLINE,
    OFFLINE,
    SLEEPING,
    NUMSTATES
};

static std::string state_to_string(nodeState state) {
    switch (state) {
        case nodeState::ONLINE:   return "Online";
        case nodeState::OFFLINE:  return "Offline";
        case nodeState::SLEEPING: return "Sleeping";
        default:                  return "unknown";
    }
}
struct nodeStatus{
    std::string name;
    nodeState state;
    uint32_t last_seen;
    uint16_t sleep_duration;
    nodeStatus() = default; 
    nodeStatus(std::string name) : name(name), state(nodeState::ONLINE) {}
    int getProjectedWake() const{
        if(sleep_duration > 0 )
        {
            return (sleep_duration - (millis()-last_seen)/1000);
        } else {
            return 0;
        }
    }
    std::string to_string() const{
        if(state == nodeState::SLEEPING && sleep_duration > 0 )
        {
            static char timebuf[3]; 
            memset(&timebuf,0,sizeof(timebuf));
            std::sprintf(timebuf, "%d", getProjectedWake());
            return std::string(name.substr(12) + ": sleeping for: "+ timebuf + "\n");
        }else{
            return std::string(name + ": "+ state_to_string(state) + "\n");
        }
    }
    std::string to_json() const {
        std::string json = "{";
        json += "\"name\": \"" + name + "\", ";
        json += "\"status\": \"" + state_to_string(state) + "\", ";
        json += "\"last_seen\": " + std::to_string((millis() - last_seen)/1000) + ", ";
        json += "\"sleep_duration\": " + std::to_string(sleep_duration);
    
        if (state == nodeState::SLEEPING && sleep_duration > 0) {
            json += ", \"projected_wake_in\": " + std::to_string(getProjectedWake());
        }
    
        json += "}";
        return json;
    }
};

public:

    esphome::sensor::Sensor *num_nodes_sensor_{nullptr};
    esphome::text_sensor::TextSensor *nodes_list_sensor_{nullptr};
    esphome::text_sensor::TextSensor *nodes_json_sensor_{nullptr};
    std::map<MacAddr, nodeStatus> node_names_;
    void set_num_nodes_sensor(sensor::Sensor *s) { num_nodes_sensor_ = s; }
    void set_nodes_list_sensor(text_sensor::TextSensor *s) { nodes_list_sensor_ = s; }
    void set_nodes_json_sensor(text_sensor::TextSensor *s) { nodes_json_sensor_ = s; }
    
    void setup() override;
    void loop() override;

    void set_mqtt(mqtt::MQTTClientComponent *mqtt);
    
    void initESPNow();
    void send_discovery_broadcast();
    void send_discovery(MacAddr &mac);
    void send_availability_message(std::string id);

    void add_node_joined_trigger(NodeJoinedTrigger *trigger) {this->node_joined_triggers_.push_back(trigger);}
  
protected:

    mqtt::MQTTClientComponent *mqtt_{nullptr};

    static ESPNowMQTTGateway *instance;

    std::set<std::string> subscribed_topics;

    static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len );

    void handle_packet(const ESPNowPacket *pkt);

    void handle_node_discovery( const NodeDiscoveryPacket *pkt );

    void handle_entity_discovery( const EntityDiscoveryPacket *pkt, MacAddr &mac );

    void handle_sensor_state( const SensorPacket *pkt );

    void subscribe_command_topic( const EntityDiscoveryPacket *pkt, MacAddr &mac );

    void send_ping_reply(const MacAddr &mac, uint16_t numCommands = 0);

    bool add_peer(MacAddr &mac);
    
    esp_err_t send_packet(MacAddr mac, uint8_t* data, size_t size);
    void update_sensors();
    bool node_joined(MacAddr &mac);
    bool node_sleep(MacAddr &mac,uint16_t sleep_duration = 0);
    bool node_wake(MacAddr &mac, uint16_t sleep_duration);
    std::vector<NodeJoinedTrigger *> node_joined_triggers_;
private:
    bool espnow_initialized=false;
    bool mqtt_initialized=false;
    bool espnow_callbackAttached=false;
    QueueHandle_t packet_queue;
    QueueHandle_t command_queue;
    std::map<MacAddr, std::vector<SensorPacket>> espNowCommandsQueue;
};

}
}