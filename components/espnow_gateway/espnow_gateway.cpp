#include "espnow_gateway.h"
#include "esphome/core/log.h"
#include <ArduinoJson.h>
#include "espnow_def.h"
#include "esphome.h"

std::string espnow_get_error_string(esp_err_t err) {
    switch(err) {
        case ESP_OK: return "ESP_OK";
        case ESP_ERR_ESPNOW_NOT_INIT: return "ESP_ERR_ESPNOW_NOT_INIT";
        case ESP_ERR_ESPNOW_ARG: return "ESP_ERR_ESPNOW_ARG";
        case ESP_ERR_ESPNOW_NO_MEM: return "ESP_ERR_ESPNOW_NO_MEM";
        case ESP_ERR_ESPNOW_FULL: return "ESP_ERR_ESPNOW_FULL";
        case ESP_ERR_ESPNOW_NOT_FOUND: return "ESP_ERR_ESPNOW_NOT_FOUND";
        case ESP_ERR_ESPNOW_INTERNAL: return "ESP_ERR_ESPNOW_INTERNAL";
        case ESP_ERR_ESPNOW_EXIST: return "ESP_ERR_ESPNOW_EXIST";
        case ESP_ERR_ESPNOW_IF: return "ESP_ERR_ESPNOW_IF";
        case ESP_ERR_ESPNOW_CHAN: return "ESP_ERR_ESPNOW_CHAN";
        default: return "Unknown error code "+std::to_string(err);
    }
}

namespace esphome {
namespace espnow_gateway {

static const char *TAG = "espnow_gateway";
#define MAX_ISR_PACKETS 10
static ESPNowPacket packet_pool[MAX_ISR_PACKETS];
static bool packet_used[MAX_ISR_PACKETS] = {0};
static const uint8_t broadcast_bytes[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static MacAddr broadcast_mac(broadcast_bytes);

ESPNowMQTTGateway *ESPNowMQTTGateway::instance = nullptr;

inline void safe_copy(char *dst, const std::string &src, size_t dst_size) {
    size_t len = std::min(src.size(), dst_size - 1);
    memcpy(dst, src.data(), len);
    dst[len] = '\0';
}
void ESPNowMQTTGateway::set_mqtt(mqtt::MQTTClientComponent *mqtt)
{
    mqtt_ = mqtt;
    mqtt_initialized = true;
    send_discovery_broadcast();
}
void ESPNowMQTTGateway::setup()
{
    instance = this;

    ESP_LOGI(TAG,"Initializing ESPNow Gateway");
    packet_queue = xQueueCreate(MAX_ISR_PACKETS, sizeof(ESPNowPacket*));
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    ESP_LOGI(TAG, "Wi-Fi mode on boot: %d", mode);
    set_interval("UpdateHASensors", 500, [this]() {update_sensors();});
}

void ESPNowMQTTGateway::initESPNow()
{
    instance = this;

    // Debug Wi-Fi state
    wifi_mode_t mode;
    esp_err_t ret = esp_wifi_get_mode(&mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_get_mode failed: %d, %s", ret, espnow_get_error_string(ret).c_str());
    } else {
        ESP_LOGI(TAG, "Wi-Fi mode: %d", mode);
    }

    wifi_ap_record_t ap_info;
    ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Connected SSID: %s, RSSI: %d", ap_info.ssid, ap_info.rssi);
    } else {
        ESP_LOGW(TAG, "Not connected or unable to get AP info: %d", ret);
    }
    
    ret = esp_now_deinit();  // clears previous state
    ESP_LOGI(TAG, "esp_now_deinit returned: %d", ret);
    ret = esp_now_init();
    ESP_LOGI(TAG,"espnow init returned %d", ret);

    add_peer(broadcast_mac); 
    
    espnow_initialized = true;
    
}


void ESPNowMQTTGateway::loop() {
    ESPNowPacket* p;
    if(espnow_initialized && mqtt_initialized)
    {
        if(!espnow_callbackAttached)
        {
            espnow_callbackAttached = true;
            esp_now_register_recv_cb(recv_cb);
        }
        while (xQueueReceive(packet_queue, &p, 0)) {
            // ESP_LOGD(TAG,"Packet received in Loop, length: %d, type: %d", p->len, p->buf[0]);
            handle_packet(p);
            // release packet back to pool
            for(int i=0;i<MAX_ISR_PACKETS;i++){
                if(&packet_pool[i]==p){
                    packet_used[i]=false;
                    break;
                }
            }
        }
        for (auto& [mac, packetQueue] : espNowCommandsQueue) {
            auto it = node_names_.find(mac);
            if (it != node_names_.end() && it->second.state == nodeState::ONLINE) {
                if (packetQueue.size() > 0)
                {
                    for (SensorPacket& cmd : packetQueue) {
                        auto packet = cmd.getPayload();
                        esp_now_send(mac.data(), packet.data(),  packet.size());
                        delay(100);
                    }
                    packetQueue.clear();
                    send_ping_reply(mac);
                }
            }
        }
    }
}

void ESPNowMQTTGateway::recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Find a free packet in the pool
    ESPNowPacket* p = nullptr;
    for (int i = 0; i < MAX_ISR_PACKETS; i++) 
    {
        if (!packet_used[i]) 
        {
            packet_used[i] = true;
            p = &packet_pool[i];
            break;
        }
    }
    
    if (!p) return; // no free packet
    
    // Copy packet data
    memcpy(&p->buf[0], data, len < sizeof(p->buf) ? len : sizeof(p->buf));
    memcpy(p->src, info->src_addr, 6);
    p->len = len;
    
    // Send pointer to queue
    xQueueSendFromISR(instance->packet_queue, &p, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

}

void ESPNowMQTTGateway::handle_packet(const ESPNowPacket *pkt)
{
    bool unknownPeer = false;
    esp_err_t ret;
    if(pkt->len == 0)
        return;

    PacketType type = (PacketType)pkt->buf[0];
    ESP_LOGD(TAG,"Packet Type: %u", pkt->buf[0]);

    MacAddr mac(pkt->src);

    if(type == PacketType::NODE_DISCOVERY ||  type == PacketType::ENTITY_DISCOVERY ||  type == PacketType::SENSOR_STATE)
    {
        if(!mqtt_){
            ESP_LOGW(TAG,"MQTT client not set, cannot handle node discovery");
            return;
        }
    }
    unknownPeer = node_joined(mac);
    if (unknownPeer || !esp_now_is_peer_exist(mac.data())) {
        unknownPeer = true;
        add_peer(mac);
    }
    
    // ESP_LOGD(TAG,"Packet Mac: %s", mac.to_string().c_str());

    switch(type)
    {
        case PacketType::NODE_DISCOVERY:
            if(!mqtt_){
                ESP_LOGW(TAG,"MQTT client not set, cannot handle node discovery");
                break;
            }
            handle_node_discovery((NodeDiscoveryPacket*)&pkt->buf[0]);
            break;

        case PacketType::ENTITY_DISCOVERY:
            if(!mqtt_){
                ESP_LOGW(TAG,"MQTT client not set, cannot handle entity discovery");
                break;
            }
            handle_entity_discovery( (EntityDiscoveryPacket*)&pkt->buf[0], mac);
            break;

        case PacketType::SENSOR_STATE:
            if(!mqtt_){
                ESP_LOGW(TAG,"MQTT client not set, cannot handle sensor state");
                break;
            }
            SensorPacket *sensor_pkt;
            sensor_pkt = (SensorPacket*)pkt->buf;
            if (std::string(sensor_pkt->header.id) != "0000")
            {
                handle_sensor_state(sensor_pkt);
            }else{
                ESP_LOGW(TAG,"Received sensor state with invalid ID, ignoring");
            }
            break;

        case PacketType::PING:
            if(unknownPeer || node_names_[mac].state == nodeState::OFFLINE)
            {
                send_ping_reply(mac,1);
                delay(150);
                send_discovery(mac);
            }else{
                send_ping_reply(mac);
            }
            node_names_[mac].state = nodeState::ONLINE;
            break;
        case PacketType::NODE_SLEEP:
            PingPacket *sleepPkt;
            sleepPkt = (PingPacket*)pkt->buf;
            node_sleep(mac,sleepPkt->countCommands);
            break;

        case PacketType::HEARTBEAT:
        {
            HeartbeatPacket *hbPkt;
            hbPkt = (HeartbeatPacket*)pkt->buf;
            std::string msg_node_id(hbPkt->header.id);
            ESP_LOGI(TAG,"Heartbeat Node ID: %s",msg_node_id.c_str());
            node_wake(mac,hbPkt->hbTimeout);
            send_ping_reply(mac);
            send_availability_message(msg_node_id);
            break;
        }

        default:
            ESP_LOGW(TAG,"Unknown packet type %d", (int)type);
    }
}


void ESPNowMQTTGateway::subscribe_command_topic( const EntityDiscoveryPacket *pkt, MacAddr &mac)
{

    std::string topic(pkt->getTopicPath()+"/set");
    if(subscribed_topics.count(topic))
        return;
        
    subscribed_topics.insert(topic);

    SensorPacket templateCmd;
    strcpy(templateCmd.header.id, pkt->header.id);
    strcpy(templateCmd.sensor.entity.id, pkt->entity.id);
    templateCmd.header.type = PacketType::SENSOR_STATE; 
    templateCmd.sensor.entity.type = pkt->entity.type;
    mqtt_->subscribe(topic,
        [this,mac,templateCmd](const std::string &topic,
                       const std::string &payload)
    {
        SensorPacket cmd = templateCmd; // make a copy to modify
        switch (cmd.sensor.entity.type)
        {
            case EntityType::TEMPERATURE:
            case EntityType::VOLTAGE:
            case EntityType::HUMIDITY:
            case EntityType::SENSOR:
            case EntityType::BINARY_SENSOR:
            case EntityType::RSSI:
            case EntityType::CONTROLS:
            case EntityType::BUTTON:
            {
                cmd.sensor.data.value = atof(payload.c_str());
                break;
            }
            case EntityType::SWITCH:
            {
                cmd.sensor.data.value = (payload == "ON");
                break;
            }
            case EntityType::TEXT:
            {
                safe_copy(cmd.sensor.data.textValue, payload, sizeof(cmd.sensor.data.textValue));
                break;
            }
            default:
                break;
        }
            
        auto packet = cmd.getPayload();
        ESP_LOGI(TAG,"Queueing: %02x from topic: %s, for entity: %s on mac: %02x%02x",packet.data()[0],topic.c_str(),cmd.sensor.entity.id, mac.data()[4], mac.data()[5]);
        // esp_now_send((uint8_t*)mac, packet.data(),  packet.size());
        espNowCommandsQueue[mac].push_back(cmd);
    });

    ESP_LOGI(TAG,"Subscribed command topic: %s, for entity: %s",topic.c_str(),templateCmd.sensor.entity.id);
}

bool ESPNowMQTTGateway::add_peer(MacAddr &mac) {
    if (esp_now_is_peer_exist(mac.data())) {
        return true;
    }
    esp_now_peer_info_t *peer = (esp_now_peer_info_t*) malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        return false;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 0;
    peer->encrypt = false;
    memcpy(peer->peer_addr, mac.data(), ESP_NOW_ETH_ALEN);
    esp_err_t ret = esp_now_add_peer(peer) ;
    free(peer);
    if (ret != ESP_OK) { // 12395 = peer already exists
        ESP_LOGW(TAG,"esp_now_add_peer %s failed: %d, %s", mac.to_string().c_str(), ret, espnow_get_error_string(ret).c_str());
        return false;
    } else {
        ESP_LOGI(TAG,"Peer%s added", mac.to_string().c_str());
        return true;
    }
}
void ESPNowMQTTGateway::send_discovery_broadcast()
{
    send_discovery(broadcast_mac);
}
void ESPNowMQTTGateway::send_discovery(MacAddr &mac)
{
    static const uint8_t discover_payload = (uint8_t)PacketType::NODE_DISCOVERY; // simple payload to indicate ping
    esp_err_t ret = esp_now_send(mac.data(), &discover_payload,1); // NULL = broadcast
    if(ret != ESP_OK) {
        ESP_LOGW(TAG,"ESPNow Discover failed: %d, %s", ret, espnow_get_error_string(ret).c_str());
    } else {
        ESP_LOGI(TAG,"Discover message sent to node %02X:%02X",mac.data()[4],mac.data()[5]);
    }
}
void ESPNowMQTTGateway::send_ping_reply(const MacAddr &mac,uint16_t numCommands)
{
    esp_err_t ret;
    ESP_LOGD(TAG,"Ping reply to: %s", mac.to_string().c_str());
    PingPacket pingRespPkt;
    pingRespPkt.header.type = PacketType::PING_ACK;
    pingRespPkt.isGateway = true;
    pingRespPkt.countCommands = numCommands;
    if ( espNowCommandsQueue.contains(mac) ) {
        pingRespPkt.countCommands += espNowCommandsQueue[mac].size();
    }
    auto ping_payload = pingRespPkt.getPayload(); // simple payload to indicate ping reply
    
    ret = esp_now_send(mac.data(), ping_payload.data(),  ping_payload.size());
    if(ret != ESP_OK) {
        ESP_LOGW(TAG,"esp_now_send failed: %d, %s", ret, espnow_get_error_string(ret).c_str());
    }
}

void ESPNowMQTTGateway::handle_node_discovery(const NodeDiscoveryPacket *pkt)
{

    std::string topic(pkt->getTopicPath()+"/config");

    JsonDocument doc;

    doc["name"] = pkt->node_name;
    doc["platform"] = "ESPNow";
    
    std::string payload;
    serializeJson(doc,payload);
    printf("Publishing node discovery to: %s",topic.c_str());
    if (payload.empty() || topic.empty()) {
        ESP_LOGW(TAG, "Skipping MQTT publish: empty topic/payload");
        return;
    }
    mqtt_->publish(topic,payload,true);

    ESP_LOGI(TAG,"Node discovery published: %s",topic.c_str());
}

void ESPNowMQTTGateway::handle_entity_discovery(const EntityDiscoveryPacket *pkt, MacAddr &mac)
{
    
    std::string topic(pkt->getTopicPath()+"/config");
    std::string state_topic(pkt->getTopicPath()+"/state");
    std::string availability_topic("homeassistant/devices/espnow_"+std::string(pkt->header.id)+"/availability");

    JsonDocument doc;

    doc["name"] = pkt->name;
    doc["uniq_id"] = std::string(pkt->header.id) + "_" + pkt->entity.id;
    doc["stat_t"] = state_topic;
    doc["expire_after"] = pkt->expiration_seconds;
    doc["availability_topic"] = availability_topic;
    if(pkt->entity.type >= EntityType::SWITCH)
    {
        std::string cmd_topic(pkt->getTopicPath()+"/set");
        doc["cmd_t"] = cmd_topic;
        subscribe_command_topic(pkt,mac);
    }

    if(strlen(pkt->unit))
    {
        doc["unit_of_meas"] = pkt->unit;
    }

    // Unit of measurement based on sensor type
    if (pkt->entity.type == EntityType::SWITCH){
      doc["payload_on"] = "ON";
      doc["payload_off"] = "OFF";
    }
    
    if(std::string(pkt->unit) != ""){
        doc["unit_of_meas"] = std::string(pkt->unit);
      } else {
        switch(pkt->entity.type){
          case EntityType::VOLTAGE:
            doc["unit_of_meas"] = "V";
            break;
          case EntityType::TEMPERATURE:
            doc["unit_of_meas"] = "°F";
            break;
          case EntityType::HUMIDITY:
            doc["unit_of_meas"] = "%";
            break;
          case EntityType::RSSI:
            doc["unit_of_meas"] = "dBm";
            break;
        }
      }
    doc["optimistic"] = true;
    doc["dev"]["identifiers"][0] = pkt->header.id;
    doc["dev"]["name"] = std::string("ESPNow ")+ std::string(pkt->header.id);
    doc["dev"]["mf"] = "JPGIndustries";
    doc["dev"]["mdl"] = "ESPNow Node";

    std::string payload;
    serializeJson(doc,payload);
    printf("Publishing entity discovery to: %s",topic.c_str());
    if (payload.empty() || topic.empty()) {
        ESP_LOGW(TAG, "Skipping MQTT publish: empty topic/payload");
        return;
    }
    mqtt_->publish(topic,payload,true);

    ESP_LOGI(TAG,"Entity discovery published: %s",topic.c_str());
}

void ESPNowMQTTGateway::send_availability_message(std::string id)
{
    if(mqtt_)
    {
        mqtt_->publish(std::string("homeassistant/devices/espnow_")+id+std::string("/availability"),"online");
    }
}

void ESPNowMQTTGateway::handle_sensor_state(const SensorPacket *pkt)
{
    std::string topic("homeassistant/"+ entityStrings[(int)pkt->sensor.entity.type]+"/espnow_"+std::string(pkt->header.id)+"/"+std::string(pkt->sensor.entity.id)+"/state");
    std::string payload;
    switch (pkt->sensor.entity.type)
    {
        case EntityType::TEXT:
        {
            payload = std::string(pkt->sensor.data.textValue); 
            break;
        }
        case EntityType::TEMPERATURE:
        case EntityType::VOLTAGE:
        case EntityType::HUMIDITY:
        case EntityType::SENSOR:
        case EntityType::BINARY_SENSOR:
        case EntityType::RSSI:
        case EntityType::CONTROLS:
        case EntityType::SWITCH:
        case EntityType::BUTTON:
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.2f", pkt->sensor.data.value);
            payload = std::string(buf);
            break;
        }
        default:
            break;
    }
        
    if (payload.empty() || topic.empty()) {
        ESP_LOGW(TAG, "Skipping MQTT publish: empty topic/payload");
        return;
    }
    ESP_LOGD(TAG,"Publishing sensor state %s to: %s",payload.c_str(),topic.c_str());
    mqtt_->publish(topic,payload);
}

void ESPNowMQTTGateway::update_sensors() {
    // Publish number of nodes
    if (num_nodes_sensor_ != nullptr)
        num_nodes_sensor_->publish_state(node_names_.size());

    if (nodes_list_sensor_ != nullptr || nodes_json_sensor_ != nullptr) {
        bool first = true;
        std::string nodes = "";
        std::string json = "[";
        for (auto& [mac, node] : node_names_) 
        {
            nodes += node.to_string();
            // Handle timeout → OFFLINE
            if ((node.state == nodeState::SLEEPING || node.state == nodeState::ONLINE) && node.getProjectedWake() < -3) {
                ESP_LOGI(TAG, "Node: %s lost contact after %d seconds", node.name.c_str(), node.sleep_duration);
                node.state = nodeState::OFFLINE;
            }
            // Build JSON array
            if (!first) {
                json += ",";
            }
            first = false;
            json += node.to_json();
        }
        json += "]";

        // Publish legacy string sensor
        if (nodes_list_sensor_ != nullptr)
            nodes_list_sensor_->publish_state(nodes.c_str());

        // Publish JSON sensor
        if (nodes_json_sensor_ != nullptr)
            nodes_json_sensor_->publish_state(json.c_str());
    }
}
// Call this whenever a node joins or leaves
bool ESPNowMQTTGateway::node_joined(MacAddr &mac) {
    bool newNode = false;
    // Check if node already exists
    auto it = node_names_.find(mac);
    if (it == node_names_.end()) 
    {
        // MAC not in map yet, insert
        node_names_[mac] = nodeStatus(mac.to_string());
        newNode = true;
    }
    if(node_names_[mac].state != nodeState::ONLINE)
    {
        ESP_LOGD(TAG,"Node: %s woke up",node_names_[mac].name.c_str());
    }

    node_names_[mac].last_seen = millis();
    // node_names_[mac].state = nodeState::ONLINE;

    if(newNode)
    {
        ESP_LOGD(TAG,"New Node! %s",node_names_[mac].name.c_str());
        for (auto *t : node_joined_triggers_) {
            ESP_LOGD(TAG,"trigger sending");
            t->trigger(mac.to_string());
        }
    }
    update_sensors();
    return newNode;
}

bool ESPNowMQTTGateway::node_sleep(MacAddr &mac, uint16_t sleep_duration) {
    bool newNode = false;
    // Find the node                           
    auto it = node_names_.find(mac);
    if (it != node_names_.end()) {
        it->second.state = nodeState::SLEEPING;
        it->second.sleep_duration = sleep_duration;
        ESP_LOGI(TAG,"Node: %s went to sleep for %d seconds",it->second.name.c_str(),it->second.sleep_duration);
    }
    update_sensors();
    return true;
}
bool ESPNowMQTTGateway::node_wake(MacAddr &mac, uint16_t sleep_duration) {
    bool newNode = false;
    // Find the node                           
    auto it = node_names_.find(mac);
    if (it != node_names_.end()) {
        it->second.state = nodeState::ONLINE;
        it->second.sleep_duration = sleep_duration;
    }else{
        newNode = true;
    }
    update_sensors();
    return newNode;
}

}
}