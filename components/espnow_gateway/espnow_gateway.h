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
/**
 * @class NodeJoinedTrigger
 * @brief ESPHome automation trigger for node join events.
 *
 * Fires when a new ESP-NOW node is discovered by the gateway.
 * Passes the node identifier (string) to attached automations.
 */
class NodeJoinedTrigger : public Trigger<std::string> {};

class ESPNowMQTTGateway : public Component {
/**
 * @enum nodeState
 * @brief Represents the current connectivity/power state of a node.
 *
 * ONLINE    - Node is awake and actively communicating.
 * OFFLINE   - Node has not been heard from within the expected timeframe.
 * SLEEPING  - Node is in deep sleep and expected to wake later.
 * NUMSTATES - Sentinel value for counting/iteration (not a real state).
 */
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
/**
 * @struct nodeStatus
 * @brief Tracks runtime status and metadata for a single ESP-NOW node.
 *
 * Stores identification, state, timing, and helper methods for
 * reporting node status in both human-readable and JSON formats.
 */
struct nodeStatus{
    std::string name;
    char id[16];
    /** Current node state (online/offline/sleeping) */
    nodeState state;
    uint32_t last_seen;
    uint16_t sleep_duration;
    nodeStatus() = default; 
    nodeStatus(std::string name)
        : name(name),
        state(nodeState::ONLINE),
        last_seen(millis()),
        sleep_duration(0)
    {
        memset(id, 0, sizeof(id));
    }
    /**
     * @brief Calculate remaining sleep time.
     *
     * Computes how many seconds remain until the node is expected
     * to wake based on last_seen and sleep_duration.
     *
     * @return int Seconds until wake (may be negative if overdue).
     */
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
            static char timebuf[12]; 
            memset(timebuf,0,sizeof(timebuf));
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
    /**
     * @brief Initialize internal state and create packet queue.
     *
     * Sets the global instance pointer, creates the ISR-safe packet queue,
     * logs Wi-Fi mode, and starts periodic sensor updates.
     */
    void setup() override;
    /**
     * @brief Main loop handler.
     *
     * Processes received packets from the ISR queue, dispatches them,
     * and sends any queued outbound commands to nodes.
     * Also ensures the ESP-NOW receive callback is registered.
     */
    void loop() override;
    /**
     * @brief Attach an MQTT client to the gateway and trigger discovery broadcast.
     *
     * Initializes MQTT usage and immediately sends a broadcast discovery message
     * so nodes can announce themselves.
     *
     * @param mqtt Pointer to MQTT client component.
     */
    void set_mqtt(mqtt::MQTTClientComponent *mqtt);
    /**
     * @brief Initialize ESP-NOW subsystem.
     *
     * Resets ESP-NOW state, initializes it, logs Wi-Fi/AP info, and adds
     * the broadcast peer.
     */
    void initESPNow();
    /**
     * @brief Send a broadcast discovery message to all nodes.
     */
    void send_discovery_broadcast();
    /**
     * @brief Send a discovery message to a specific node.
     *
     * @param mac Target node MAC address.
     */
    void send_discovery(MacAddr &mac);
    /**
     * @brief Publish availability status for a node.
     *
     * Sends an "online" message to the node's availability topic.
     *
     * @param id Node identifier string.
     * @param available Whether the node is available (currently always publishes "online").
     */
    void send_availability_message(std::string id, bool available = true);

    void add_node_joined_trigger(NodeJoinedTrigger *trigger) {this->node_joined_triggers_.push_back(trigger);}
  
protected:

    mqtt::MQTTClientComponent *mqtt_{nullptr};

    static ESPNowMQTTGateway *instance;

    std::set<std::string> subscribed_topics;
    
    /**
     * @brief ESP-NOW receive callback (ISR context).
     *
     * Copies incoming packet data into a preallocated pool and enqueues
     * it for processing in the main loop.
     *
     * @param info Metadata about the received packet.
     * @param data Pointer to received payload.
     * @param len Length of payload.
     */
    static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len );
    
    /**
     * @brief Handle a received ESP-NOW packet.
     *
     * Decodes packet type and dispatches to the appropriate handler.
     * Manages peer tracking, node state updates, and protocol responses.
     *
     * @param pkt Pointer to received packet.
     */
    void handle_packet(const ESPNowPacket *pkt);
    
    /**
     * @brief Handle node discovery packet.
     *
     * Publishes node metadata to MQTT for Home Assistant discovery.
     *
     * @param pkt Node discovery packet.
     */
    void handle_node_discovery( const NodeDiscoveryPacket *pkt );
    
    /**
     * @brief Handle entity discovery packet.
     *
     * Publishes entity configuration to MQTT and subscribes to command topics
     * if the entity supports control.
     *
     * @param pkt Entity discovery packet.
     * @param mac MAC address of the node.
     */
    void handle_entity_discovery( const EntityDiscoveryPacket *pkt, MacAddr &mac );
    
    /**
     * @brief Handle incoming sensor state packet.
     *
     * Converts sensor data into an MQTT payload and publishes it to the
     * appropriate Home Assistant topic.
     *
     * @param pkt Sensor packet.
     */
    void handle_sensor_state( const SensorPacket *pkt );
    
    /**
     * @brief Subscribe to MQTT command topic for a discovered entity.
     *
     * Registers an MQTT subscription and translates incoming MQTT payloads
     * into ESP-NOW command packets queued for the corresponding node.
     *
     * @param pkt Entity discovery packet describing the entity.
     * @param mac MAC address of the node.
     */
    void subscribe_command_topic( const EntityDiscoveryPacket *pkt, MacAddr &mac );
    
    /**
     * @brief Send a ping reply (ACK) to a node.
     *
     * Includes the number of pending commands queued for the node.
     *
     * @param mac Target node MAC address.
     * @param numCommands Number of commands included in this response.
     */
    void send_ping_reply(const MacAddr &mac, uint16_t numCommands = 0);
    
    /**
     * @brief Add a peer to the ESP-NOW peer list.
     *
     * If the peer does not already exist, allocates and registers it.
     *
     * @param mac MAC address of the peer.
     * @return true if peer exists or was added successfully.
     * @return false if adding failed.
     */
    bool add_peer(MacAddr &mac);
    
    esp_err_t send_packet(MacAddr mac, uint8_t* data, size_t size);
    /**
     * @brief Periodically update gateway sensor states.
     *
     * Publishes node count, node list string, and JSON representation.
     * Also handles node timeout transitions to OFFLINE.
     */
    void update_sensors();
    /**
     * @brief Handle node join or activity update.
     *
     * Inserts new nodes into the map, updates last-seen timestamp,
     * triggers join events, and refreshes sensors.
     *
     * @param mac MAC address of the node.
     * @return true if this is a newly discovered node.
     */
    bool node_joined(MacAddr &mac);
    /**
     * @brief Mark a node as sleeping.
     *
     * Updates node state and sleep duration.
     *
     * @param mac MAC address of the node.
     * @param sleep_duration Duration of sleep in seconds.
     * @return true if node was updated.
     */
    bool node_sleep(MacAddr &mac,uint16_t sleep_duration = 0);
    /**
     * @brief Mark a node as awake/online.
     *
     * Updates node state and expected sleep duration.
     *
     * @param mac MAC address of the node.
     * @param sleep_duration Expected sleep duration.
     * @return true if node was newly created.
     */
    bool node_wake(MacAddr &mac, uint16_t sleep_duration);
    /** List of registered triggers that fire when a node joins */
    std::vector<NodeJoinedTrigger *> node_joined_triggers_;
private:
    /** True once ESP-NOW subsystem has been initialized */
    bool espnow_initialized=false;
    /** True once MQTT client has been attached */
    bool mqtt_initialized=false;
    /** True once receive callback has been registered */
    bool espnow_callbackAttached=false;
    /** Queue for incoming ESP-NOW packets (ISR → main loop) */
    QueueHandle_t packet_queue;
    /** Queue for outgoing commands (currently unused or reserved) */
    QueueHandle_t command_queue;
    /**
     * @brief Per-node command queues.
     *
     * Stores pending SensorPacket commands to be sent to each node.
     * Commands are flushed when the node is online and reachable.
     */
    std::map<MacAddr, std::vector<SensorPacket>> espNowCommandsQueue;
};

}
}