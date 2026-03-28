#pragma once
#pragma pack(push, 1) // Ensure no padding in structs
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"

// Packet types
enum class PacketType : uint8_t {
    NODE_DISCOVERY,
    ENTITY_DISCOVERY,
    SENSOR_STATE,
    TEXT_STATE,
    HEARTBEAT,
    NODE_NAME_ASSIGN,
    SENSOR_NAME_ASSIGN,
    PING,
    PING_ACK,
    NODE_SLEEP,
    NUM_TYPES
};

// Sensor types
enum class EntityType : uint8_t {
    TEMPERATURE = 0,
    VOLTAGE,
    HUMIDITY,
    SENSOR,
    BINARY_SENSOR,
    RSSI,
    CONTROLS,
    SWITCH,
    BUTTON,
    TEXT,   //Must be greater than the 'normal value' sensors
    NUM_TYPES
};
inline const std::string packetStrings[] = {"NODE_DISCOVERY", "ENTITY_DISCOVERY", "SENSOR_STATE", "TEXT_STATE", "HEARTBEAT", "NODE_NAME_ASSIGN", "SENSOR_NAME_ASSIGN","PING"};
inline const std::string entityStrings[] = {"temperature", "voltage", "humidity","sensor", "binary_sensor", "rssi", "controls","switch","button","text"}; 

struct packetHeader {
    PacketType type;
    char id[16];
};

struct Entity {
    char id[16];         // sensor ID
    EntityType type;    // sensor type
    uint8_t flags;      // future use
};

struct Control {
    Entity entity;
    float currValue;
    float setValue;
};

// One sensor description (used in NODE_INFO)
struct Sensor {
    Entity entity;
    union {
        float value;
        char textValue[128];
    } data;
    
};
struct TextSensor {
    Entity entity;
    char value[128];
};

struct EntityDiscoveryPacket{
    packetHeader header; // type = NODE_INFO
    Entity entity;
    char node_name[32]; 
    char name[32];
    char unit[8];
    uint16_t expiration_seconds;
    std::string getTopicPath() const {
        std::string buffer = "homeassistant/"+entityStrings[(int)entity.type]+"/espnow_"+std::string(header.id)+"/"+std::string(entity.id);
        return buffer;
    }
    std::vector<uint8_t> getPayload() const {
        std::vector<uint8_t> payload(sizeof(*this));
        memcpy(payload.data(), this, sizeof(*this));
        return payload;
    }
};

// NODE_INFO packet
struct NodeDiscoveryPacket {
    packetHeader header; // type = NODE_INFO
    char node_name[32]; // null-terminated
    uint8_t entity_count;
    std::string getTopicPath() const {
        std::string buffer = "esphome/discover/espnow_"+std::string(header.id);
        return buffer;
    }
    std::vector<uint8_t> getPayload() const {
        std::vector<uint8_t> payload(sizeof(*this));
        memcpy(payload.data(), this, sizeof(*this));
        return payload;
    }
};

struct SensorPacket {
    packetHeader header;    // = SENSOR_STATE
    Sensor sensor;          
    std::string getTopicPath() const {
        std::string buffer = "homeassistant/"+entityStrings[(int)sensor.entity.type]+"/espnow_"+std::string(header.id)+"/"+std::string(sensor.entity.id);
        return buffer;
    }
    std::vector<uint8_t> getPayload() const {
        std::vector<uint8_t> payload(sizeof(*this));
        memcpy(payload.data(), this, sizeof(*this));
        return payload;
    }
};

// HEARTBEAT packet
struct HeartbeatPacket {
    packetHeader header;    // = HEARTBEAT
    uint32_t uptime_sec;
    uint16_t hbTimeout;
    std::string getTopicPath() const {
        std::string buffer("homeassistant/espnow_"+std::string(header.id)+"/available");
        return buffer;
    }
    std::vector<uint8_t> getPayload() const {
        std::vector<uint8_t> payload(sizeof(*this));
        memcpy(payload.data(), this, sizeof(*this));
        return payload;
    }
};

// NAME_ASSIGN packet
struct NameAssignPacket {
    packetHeader header;    // = NODE_NAME_ASSIGN or SENSOR_NAME_ASSIGN
    char name[16];      // null-terminated
    std::vector<uint8_t> getPayload() const {
        std::vector<uint8_t> payload(sizeof(*this));
        memcpy(payload.data(), this, sizeof(*this));
        return payload;
    }
};
// PING packet
struct PingPacket {
    packetHeader header;    // = HEARTBEAT
    bool isGateway;
    uint16_t countCommands;
    std::vector<uint8_t> getPayload() const {
        std::vector<uint8_t> payload(sizeof(*this));
        memcpy(payload.data(), this, sizeof(*this));
        return payload;
    }
};

struct ESPNowPacket {
    int len;
    int8_t rssi;
    uint8_t src[6];
    uint8_t des[6];
    uint8_t buf[256];
};

struct MacAddr {
    uint8_t bytes[6];

    // Constructor from array
    MacAddr(const uint8_t src[6]) {
        std::memcpy(bytes, src, 6);
    }

    // Default constructor
    MacAddr() { std::memset(bytes, 0, 6); }

    // Get a pointer to the underlying bytes
    uint8_t* data() { return bytes; }
    const uint8_t* data() const { return bytes; }

    // Convert to string XX:XX:XX:XX:XX:XX
    std::string to_string() const {
        char buf[18];
        std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
        return std::string(buf);
    }

    // Comparison operators for using in std::map or std::set
    bool operator==(const MacAddr& other) const {
        return std::memcmp(bytes, other.bytes, 6) == 0;
    }
    bool operator<(const MacAddr& other) const {
        return std::memcmp(bytes, other.bytes, 6) < 0;
    }
};
#pragma pack(push, 0) 