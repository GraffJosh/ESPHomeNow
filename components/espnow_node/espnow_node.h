#pragma once

#include "esphome.h"
#include "espnow_def.h"  // shared packet definitions
#include "esphome/core/preferences.h"
#include "espnow_entity_interface.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include "esphome/core/automation.h"
namespace esphome {
namespace espnow_node {
    
class GatewayConnectedTrigger : public Trigger<std::string> {};
class TransactionCompleteTrigger : public Trigger<> {};

class ESPNowNode : public Component {
    
    public:
        std::string node_id_ = "0000";
        std::string node_name_ = "Node";

        uint8_t channel_ = 1;

        // Register a switch (or any entity)
        void register_entity(ESPNowEntityInterface *sw);

        // ESPHome lifecycle
        void setup() override;
        void loop() override;

        // ESP-NOW operations
        void scan_channels(uint8_t hintChannel,uint16_t timeout_ms);
        uint8_t getChannel() {return channel_;}
        void scan_step(uint8_t iteration, uint16_t timeout_ms);
        void send_ping();
        void send_sensor_state(const char *entity_id, float value);
        void send_sensor_state(const char *entity_id, std::string value);
        static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len);
        void handle_packet(const ESPNowPacket *pkt);
        void sendPacket(std::vector<uint8_t> payload);
        void connect(int channel,bool wifiConfiguredExternally);
        void sleepNode(uint16_t seconds=0);
        void disconnect();
        void send_heartbeat();
        void set_expiration(uint32_t seconds) { expiration_seconds_ = seconds; }
        float get_setup_priority() const {return esphome::setup_priority::LATE - 1.0f;}
        
        void add_gateway_connected_trigger(GatewayConnectedTrigger *trigger) {this->gateway_connected_triggers_.push_back(trigger);}
        void add_transaction_complete_trigger(TransactionCompleteTrigger *trigger) {this->transaction_complete_triggers_.push_back(trigger);}

    protected:
        esp_err_t send_packet(uint8_t* mac, uint8_t* data, size_t size);

        bool add_peer(uint8_t *mac);
        void publish_all_entities();
        void publish_entity_discovery(EntityDiscoveryPacket *entity);
        std::vector<GatewayConnectedTrigger *> gateway_connected_triggers_;
        std::vector<TransactionCompleteTrigger *> transaction_complete_triggers_;

    private:
        static ESPNowNode *instance;
        std::unordered_map<std::string, ESPNowEntityInterface*> entities_;
        QueueHandle_t packet_queue;
        bool channel_locked_ = false;
        bool espnow_initialized = false;
        bool watchdog_pet = false;
        bool scanning_ = false;
        const char* watchdogSequence = "Watchdog";
        uint16_t watchdogTimeout = 1500;
        uint8_t gatewayMac[6]{0};
        esphome::ESPPreferences  *prefs_;  // pointer to global preferences
        uint32_t expiration_seconds_{0};
        HeartbeatPacket heartbeatPacket;

};

}  // namespace espnow_node
}  // namespace esphome