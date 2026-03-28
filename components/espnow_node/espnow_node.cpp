#include "espnow_node.h"
#define CONFIG_ESPNOW_WAKE_WINDOW 25
#define CONFIG_ESPNOW_WAKE_INTERVAL 500
#define CONFIG_ESPNOW_ENABLE_POWER_SAVE 0

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
esp_err_t espnow_set_channel(uint8_t _primaryChan)
{
  esp_err_t ret = ESP_OK;
  ret = esp_wifi_set_promiscuous(true);
  wifi_second_chan_t secondChan = WIFI_SECOND_CHAN_NONE;
  ret = esp_wifi_set_channel(_primaryChan, secondChan);
  ret = esp_wifi_set_promiscuous(false);
  return ret;
}
static void wifi_init(int channel, bool wifiConfiguredExternally)
{
  if (!wifiConfiguredExternally){
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
  } 
  // Force STA mode (ESPHome already uses STA, but this is safe)
  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA));
  // Ensure Wi-Fi is started (ESPHome may have it disabled at boot)
  ESP_ERROR_CHECK( esp_wifi_start());
  // Set channel BEFORE esp_now_init
  espnow_set_channel(channel);
  // Optional: enable LR if you want it
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
}
namespace esphome {
namespace espnow_node {

  
  static const char *TAG = "espnow_node";
  #define MAX_ISR_PACKETS 10
  static ESPNowPacket packet_pool[MAX_ISR_PACKETS];
  static bool packet_used[MAX_ISR_PACKETS] = {0};
  static uint8_t broadcast_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  ESPNowNode* ESPNowNode::instance = nullptr;

void ESPNowNode::register_entity(ESPNowEntityInterface *entity) {
  std::string id(entity->discoveryPacket->entity.id,strnlen(entity->discoveryPacket->entity.id,sizeof(entity->discoveryPacket->entity.id)));
  entities_[id] = entity;
  ESP_LOGD(TAG,"Registering Entity: %s",id.c_str());
}
void ESPNowNode::publish_all_entities()
{
  if (node_id_ == "0000") {
    ESP_LOGW(TAG,"Request to publish entities before init, rejecting");
    return;
  }
  for (const auto& [key, entity] : entities_) {
    ESP_LOGD(TAG,"Publishing discovery for: %s",key.c_str());
    publish_entity_discovery(entity->discoveryPacket);
    delay(50);
  }
}
void ESPNowNode::connect(int channel, bool wifiConfiguredExternally) {
  esp_err_t ret;
  // Init the wifi interface for espnow communications
  wifi_init(channel, wifiConfiguredExternally);
  // Init ESP-NOW
  ret = esp_now_deinit();  
  ret = esp_now_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error initializing ESP-NOW %s",espnow_get_error_string(ret).c_str());
    return;
  }
  #if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK( esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW) );
    ESP_ERROR_CHECK( esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL) );
  #endif
  
  add_peer(broadcast_mac); 
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  char buf[5] = {0};
  snprintf(buf, sizeof(buf), "%02x%02x", mac[4], mac[5]);
  node_id_ = std::string(buf);
  
  ret = esp_now_register_recv_cb(recv_cb);
  
  heartbeatPacket.header.type = PacketType::HEARTBEAT;
  safe_copy(heartbeatPacket.header.id, node_id_.c_str(),sizeof(heartbeatPacket.header.id));
  heartbeatPacket.hbTimeout = expiration_seconds_;
  if(expiration_seconds_ )
  {
    set_interval("heartbeat", (expiration_seconds_ - 1) * 1000, [this]() {send_heartbeat();});
  }

  scan_channels(channel_, 1000);
}
void ESPNowNode::sleepNode(uint16_t seconds){
  PingPacket pkt;
  pkt.header.type = PacketType::NODE_SLEEP;
  pkt.isGateway = false;
  pkt.countCommands = seconds;
  safe_copy(pkt.header.id, node_id_,sizeof(pkt.header.id));
  send_packet(gatewayMac,pkt.getPayload().data(),pkt.getPayload().size());
  ESP_LOGD(TAG,"Going to sleep for %d seconds!", seconds);
  delay(50);
}
void ESPNowNode::disconnect(){
  sleepNode();
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  // 1️⃣ Stop ESP-NOW if running
  esp_now_deinit();
  channel_locked_ = false;
  espnow_initialized = false;
}
esp_err_t ESPNowNode::send_packet(uint8_t* mac, uint8_t* data, size_t size)
{
  esp_err_t ret = ESP_ERR_ESPNOW_NOT_INIT;
  if(channel_locked_)
  {
    ret = esp_now_send(mac,data,size);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error sending ESPNow Packet %s",espnow_get_error_string(ret).c_str());
    }
  }else{
    ESP_LOGD(TAG,"ESPNow channel unlocked, can't send packet");
  }
  return ret;
}

void ESPNowNode::recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  // Find a free packet in the pool
  ESPNowPacket* p = nullptr;
  for (int i = 0; i < MAX_ISR_PACKETS; i++) {
      if (!packet_used[i]) {
          packet_used[i] = true;
          p = &packet_pool[i];
          break;
      }
  }

  if (!p) return; // no free packet

  // Copy packet data
  memcpy(&p->buf[0], data, len < sizeof(p->buf) ? len : sizeof(p->buf));
  memcpy(p->src, info->src_addr, 6);
  memcpy(p->des, info->des_addr, 6);
  p->len = len;

  // Send pointer to queue
  xQueueSendFromISR(instance->packet_queue, &p, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}





void ESPNowNode::setup() {
  esp_err_t ret;
  instance = this;
  
  ESP_LOGI(TAG,"Initializing ESPNow Node");
  packet_queue = xQueueCreate(MAX_ISR_PACKETS, sizeof(ESPNowPacket*));
  connect(1, true);
}

void ESPNowNode::loop() {
  ESPNowPacket* p;
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
}

void ESPNowNode::handle_packet(const ESPNowPacket *pkt)
{
  if (pkt->len == 0){  
    return;
  }
  // ESP_LOGD(TAG,"Packet Type: %u", pkt->buf[0]);
  ESP_LOGD(TAG,"Packet Mac: %02x:%02x:%02x:%02x:%02x:%02x", pkt->src[0], pkt->src[1], pkt->src[2], pkt->src[3], pkt->src[4], pkt->src[5]);
  
  PacketType type = (PacketType)pkt->buf[0];
  
  if (!esp_now_is_peer_exist(pkt->src)) {
      add_peer((uint8_t*) pkt->src);
  }
  switch (type) {
    case PacketType::SENSOR_STATE: {
      if (pkt->len < sizeof(SensorPacket)) {
        ESP_LOGW(TAG, "Invalid SENSOR_STATE packet");
        return;
      }
      auto *packetForSensor = (SensorPacket *)pkt->buf;

      auto it = entities_.find(packetForSensor->sensor.entity.id);
      if (it != entities_.end()) {
          it->second->handle_sensor_packet(*packetForSensor);
      }else{
          ESP_LOGW(TAG,"Received sensor state for unknown entity: %s", packetForSensor->sensor.entity.id);
      }
      break;
    }
    case PacketType::PING_ACK:{
      watchdog_pet = true;
      channel_locked_ = true;
      auto *pingRespPkt = (PingPacket *)pkt->buf;
      ESP_LOGD(TAG,"PING RESP, expecting: %d commands.", pingRespPkt->countCommands);
      if(pingRespPkt->isGateway){
        memcpy(gatewayMac, pkt->src,sizeof(gatewayMac));
      }
      if(pingRespPkt->countCommands == 0)
      {
        for (auto *t : transaction_complete_triggers_) {
          t->trigger();
        }
      }
      break;
    }
    case PacketType::NODE_DISCOVERY:{
      memcpy(gatewayMac, pkt->src,sizeof(gatewayMac));
      publish_all_entities();
      // delay(50);
      set_timeout(250, [this]() { send_ping(); });
      break;
    }
    default:
      ESP_LOGW("espnow_node", "Unknown packet type %d", (int)type);
      break;
  }
}

bool ESPNowNode::add_peer(uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) {
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
    memcpy(peer->peer_addr, mac, ESP_NOW_ETH_ALEN);
    esp_err_t ret = esp_now_add_peer(peer) ;
    free(peer);
    if (ret != ESP_OK) { // 12395 = peer already exists
        ESP_LOGW(TAG,"esp_now_add_peer %02x:%02x:%02x:%02x:%02x:%02x failed: %d, %s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ret, espnow_get_error_string(ret).c_str());
        return false;
    } else {
        ESP_LOGI(TAG,"Peer %02x:%02x:%02x:%02x:%02x:%02x added", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return true;
    }
}

void ESPNowNode::scan_channels(uint8_t hintChannel, uint16_t timeout_ms) {
  channel_ = hintChannel;
  if(!scanning_)
  {
    scanning_ = true;
    scan_step(0,timeout_ms);
    if(channel_locked_){
      for (auto *t : gateway_connected_triggers_) {
        MacAddr gateway(gatewayMac);
        t->trigger(gateway.to_string());
      }
    }
    scanning_ = false;
  }else{
    ESP_LOGD(TAG,"Already scanning, preventing duplication");
  }
}

void ESPNowNode::scan_step(uint8_t iteration,uint16_t timeout_ms) {
  static uint8_t start_channel_;
  if (channel_locked_) {
    ESP_LOGI(TAG, "Channel locked! %d", channel_);
    return;
  }
  if (iteration >= 16) {
    ESP_LOGW(TAG, "No gateway found, using channel: %d", channel_);
    return;
  }
  if (iteration == 0) {
    start_channel_ = channel_;
  }
  channel_ = ((start_channel_ - 1 + iteration) % 16) + 1;

  ESP_LOGD(TAG, "Trying channel %d...", channel_);
  esp_err_t ret = espnow_set_channel(channel_);
  if (ret == ESP_OK) send_ping();
  
  iteration++;
  set_timeout(timeout_ms, [this, iteration, timeout_ms]() { scan_step(iteration, timeout_ms); });
}


void ESPNowNode::send_ping(){
  esp_err_t ret;
  uint8_t ping_payload = (uint8_t)PacketType::PING; // simple payload to indicate ping
  ret = esp_now_send(broadcast_mac, &ping_payload,1); // NULL = broadcast
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to send ping: %s",espnow_get_error_string(ret).c_str());
  }
}

void ESPNowNode::send_heartbeat()
{
  ESP_LOGD(TAG,"Publishing HB for node: %s to mac: %s",heartbeatPacket.header.id,MacAddr(gatewayMac).to_string().c_str());
    // std::vector<uint8_t> payload = heartbeatPacket.getPayload();
    send_packet(gatewayMac, reinterpret_cast<uint8_t*>(&heartbeatPacket),sizeof(heartbeatPacket));
    watchdog_pet = false;
    watchdogTimeout = 1500;
    set_timeout(watchdogSequence, watchdogTimeout, [this]() { 
    if (!watchdog_pet) {
      channel_locked_ = false;
      scan_channels(channel_, 1000);
    }
  });
}

void ESPNowNode::publish_entity_discovery(EntityDiscoveryPacket *pkt)
{
  ESP_LOGD(TAG,"Publishing discovery for: %s",pkt->entity.id);
  pkt->expiration_seconds = expiration_seconds_;
  strcpy(pkt->header.id, node_id_.c_str());
  strcpy(pkt->node_name,node_name_.c_str());
  // std::vector<uint8_t> payload = pkt->getPayload();
  // send_packet(broadcast_mac, payload.data(),payload.size());
  send_packet(broadcast_mac, reinterpret_cast<uint8_t*>(pkt),sizeof(*pkt));
}
void ESPNowNode::send_sensor_state(const char *entity_id, float value){
  SensorPacket pkt;
  
  pkt.header.type = PacketType::SENSOR_STATE;
  safe_copy(pkt.header.id, node_id_,sizeof(pkt.header.id));
  safe_copy(pkt.sensor.entity.id, std::string(entity_id),sizeof(pkt.sensor.entity.id));
  pkt.sensor.entity.type = EntityType::SENSOR;
  pkt.sensor.data.value = value;
  // std::vector<uint8_t> payload = pkt.getPayload();
  // send_packet(gatewayMac, payload.data(),payload.size());
  send_packet(gatewayMac, reinterpret_cast<uint8_t*>(&pkt),sizeof(pkt));
}
void ESPNowNode::send_sensor_state(const char *entity_id, std::string value){
  SensorPacket pkt;
  
  pkt.header.type = PacketType::SENSOR_STATE;
  safe_copy(pkt.header.id, node_id_,sizeof(pkt.header.id));
  safe_copy(pkt.sensor.entity.id, std::string(entity_id),sizeof(pkt.sensor.entity.id));
  pkt.sensor.entity.type = EntityType::TEXT;
  safe_copy(pkt.sensor.data.textValue, value,sizeof(pkt.sensor.data.textValue));
  send_packet(gatewayMac, reinterpret_cast<uint8_t*>(&pkt),sizeof(pkt));
}

}  // namespace espnow_node
}  // namespace esphome