/**
 * @file persistence_manager.cpp
 * @brief 持久化管理器实现
 */

#include "mqtt_client/persistence/persistence_manager.h"
#include "mqtt_client/persistence/storage_engine.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/logger/logger_interface.h"
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
namespace mqtt_client {

using json = nlohmann::json;

// Base64编码/解码辅助函数
namespace {
    std::string base64_encode(const std::string& data) {
        // 简单的base64编码实现（可以使用库函数）
        // 这里使用标准库的base64编码
        static constexpr char base64_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::string encoded;
        int val = 0, valb = -6;
        for (const unsigned char c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                encoded.push_back(base64_chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) {
            encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
        }
        while (encoded.size() % 4) {
            encoded.push_back('=');
        }
        return encoded;
    }
    
    std::string base64_decode(const std::string& data) {
        // 简单的base64解码实现
        static constexpr char base64_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        
        std::string decoded;
        int val = 0, valb = -8;
        for (const char c : data) {
            if (c == '=') break;
            const char* pos = strchr(base64_chars, c);
            if (pos == nullptr) continue;
            // base64 字符表只有64个字符，索引范围 0-63，可以安全转换为 int
            val = (val << 6) + static_cast<int>(pos - base64_chars);
            valb += 6;
            if (valb >= 0) {
                decoded.push_back(static_cast<char>(val >> valb & 0xFF));
                valb -= 8;
            }
        }
        return decoded;
    }
}

PersistenceManager::PersistenceManager(
    const std::string& storagePath,
    const std::shared_ptr<StorageEngine> &storageEngine)
    : storagePath_(storagePath)
    , storageEngine_(storageEngine ? storageEngine : std::make_shared<FileStorageEngine>(storagePath)) {
}

Result<bool> PersistenceManager::saveSendQueue(const std::vector<MqttMessage>& messages) {
    try {
        json j;
        j["version"] = "1.0";
        j["timestamp"] = std::time(nullptr);
        j["messages"] = json::array();
        
        for (const auto& msg : messages) {
            json msgJson;
            msgJson["topic"] = msg.topic;
            msgJson["payload"] = base64_encode(msg.payload);  // Base64编码
            msgJson["qos"] = static_cast<int>(msg.qos);
            msgJson["retain"] = msg.retain;
            if (!msg.properties.isEmpty()) {
                json propsJson;
                if (msg.properties.messageExpiryInterval.has_value()) {
                    propsJson["messageExpiryInterval"] = msg.properties.messageExpiryInterval.value();
                }
                if (msg.properties.contentType.has_value()) {
                    propsJson["contentType"] = msg.properties.contentType.value();
                }
                if (msg.properties.responseTopic.has_value()) {
                    propsJson["responseTopic"] = msg.properties.responseTopic.value();
                }
                if (msg.properties.correlationData.has_value()) {
                    propsJson["correlationData"] = msg.properties.correlationData.value();
                }
                if (!msg.properties.userProperties.empty()) {
                    propsJson["userProperties"] = msg.properties.userProperties;
                }
                msgJson["properties"] = propsJson;
            }
            j["messages"].push_back(msgJson);
        }
        
        std::string data = j.dump(2);
        return storageEngine_->write(getSendQueuePath(), data);
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "保存发送队列异常: " + std::string(e.what())));
    }
}

Result<std::vector<MqttMessage>> PersistenceManager::loadSendQueue() {
    try {
        auto result = storageEngine_->read(getSendQueuePath());
        if (!result.success) {
            // 文件不存在，返回空队列
            return Result<std::vector<MqttMessage>>::Success({});
        }
        
        json j = json::parse(result.value);
        std::vector<MqttMessage> messages;
        
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& msgJson : j["messages"]) {
                MqttMessage msg;
                msg.topic = msgJson.value("topic", "");
                std::string payloadBase64 = msgJson.value("payload", "");
                msg.payload = base64_decode(payloadBase64);
                msg.qos = static_cast<QoS>(msgJson.value("qos", 0));
                msg.retain = msgJson.value("retain", false);
                
                if (msgJson.contains("properties")) {
                    const auto& propsJson = msgJson["properties"];
                    if (propsJson.contains("messageExpiryInterval")) {
                        msg.properties.messageExpiryInterval = propsJson["messageExpiryInterval"].get<int>();
                    }
                    if (propsJson.contains("contentType")) {
                        msg.properties.contentType = propsJson["contentType"].get<std::string>();
                    }
                    if (propsJson.contains("responseTopic")) {
                        msg.properties.responseTopic = propsJson["responseTopic"].get<std::string>();
                    }
                    if (propsJson.contains("correlationData")) {
                        msg.properties.correlationData = propsJson["correlationData"].get<std::string>();
                    }
                    if (propsJson.contains("userProperties")) {
                        msg.properties.userProperties = propsJson["userProperties"].get<std::map<std::string, std::string>>();
                    }
                }
                
                messages.push_back(msg);
            }
        }
        
        LOG_DEBUG(fmt::format("加载发送队列: {} 条消息", messages.size()));
        return Result<std::vector<MqttMessage>>::Success(messages);
    } catch (const std::exception& e) {
        return Result<std::vector<MqttMessage>>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "加载发送队列异常: " + std::string(e.what())));
    }
}

Result<bool> PersistenceManager::saveReceiveQueue(const std::vector<MqttMessage>& messages) {
    try {
        json j;
        j["version"] = "1.0";
        j["timestamp"] = std::time(nullptr);
        j["messages"] = json::array();
        
        for (const auto& msg : messages) {
            json msgJson;
            msgJson["topic"] = msg.topic;
            msgJson["payload"] = base64_encode(msg.payload);
            msgJson["qos"] = static_cast<int>(msg.qos);
            msgJson["retain"] = msg.retain;
            if (!msg.properties.isEmpty()) {
                json propsJson;
                if (msg.properties.messageExpiryInterval.has_value()) {
                    propsJson["messageExpiryInterval"] = msg.properties.messageExpiryInterval.value();
                }
                if (msg.properties.contentType.has_value()) {
                    propsJson["contentType"] = msg.properties.contentType.value();
                }
                if (msg.properties.responseTopic.has_value()) {
                    propsJson["responseTopic"] = msg.properties.responseTopic.value();
                }
                if (msg.properties.correlationData.has_value()) {
                    propsJson["correlationData"] = msg.properties.correlationData.value();
                }
                if (!msg.properties.userProperties.empty()) {
                    propsJson["userProperties"] = msg.properties.userProperties;
                }
                msgJson["properties"] = propsJson;
            }
            j["messages"].push_back(msgJson);
        }
        
        std::string data = j.dump(2);
        return storageEngine_->write(getReceiveQueuePath(), data);
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "保存接收队列异常: " + std::string(e.what())));
    }
}

Result<std::vector<MqttMessage>> PersistenceManager::loadReceiveQueue() {
    try {
        auto result = storageEngine_->read(getReceiveQueuePath());
        if (!result.success) {
            return Result<std::vector<MqttMessage>>::Success({});
        }
        
        json j = json::parse(result.value);
        std::vector<MqttMessage> messages;
        
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& msgJson : j["messages"]) {
                MqttMessage msg;
                msg.topic = msgJson.value("topic", "");
                std::string payloadBase64 = msgJson.value("payload", "");
                msg.payload = base64_decode(payloadBase64);
                msg.qos = static_cast<QoS>(msgJson.value("qos", 0));
                msg.retain = msgJson.value("retain", false);
                
                if (msgJson.contains("properties")) {
                    const auto& propsJson = msgJson["properties"];
                    if (propsJson.contains("messageExpiryInterval")) {
                        msg.properties.messageExpiryInterval = propsJson["messageExpiryInterval"].get<int>();
                    }
                    if (propsJson.contains("contentType")) {
                        msg.properties.contentType = propsJson["contentType"].get<std::string>();
                    }
                    if (propsJson.contains("responseTopic")) {
                        msg.properties.responseTopic = propsJson["responseTopic"].get<std::string>();
                    }
                    if (propsJson.contains("correlationData")) {
                        msg.properties.correlationData = propsJson["correlationData"].get<std::string>();
                    }
                    if (propsJson.contains("userProperties")) {
                        msg.properties.userProperties = propsJson["userProperties"].get<std::map<std::string, std::string>>();
                    }
                }
                
                messages.push_back(msg);
            }
        }
        
        LOG_DEBUG(fmt::format("加载接收队列: {} 条消息", messages.size()));
        return Result<std::vector<MqttMessage>>::Success(messages);
    } catch (const std::exception& e) {
        return Result<std::vector<MqttMessage>>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "加载接收队列异常: " + std::string(e.what())));
    }
}

Result<bool> PersistenceManager::saveSubscriptions(const std::vector<Subscription>& subs) {
    try {
        json j;
        j["version"] = "1.0";
        j["timestamp"] = std::time(nullptr);
        j["subscriptions"] = json::array();
        
        for (const auto& sub : subs) {
            json subJson;
            subJson["topic"] = sub.topic;
            subJson["qos"] = static_cast<int>(sub.qos);
            subJson["subscribedTime"] = sub.subscribedTime;
            // 注意：回调函数无法序列化，需要在恢复时重新设置
            j["subscriptions"].push_back(subJson);
        }
        
        std::string data = j.dump(2);
        return storageEngine_->write(getSubscriptionsPath(), data);
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "保存订阅信息异常: " + std::string(e.what())));
    }
}

Result<std::vector<Subscription>> PersistenceManager::loadSubscriptions() {
    try {
        auto result = storageEngine_->read(getSubscriptionsPath());
        if (!result.success) {
            return Result<std::vector<Subscription>>::Success({});
        }
        
        json j = json::parse(result.value);
        std::vector<Subscription> subs;
        
        if (j.contains("subscriptions") && j["subscriptions"].is_array()) {
            for (const auto& subJson : j["subscriptions"]) {
                Subscription sub;
                sub.topic = subJson.value("topic", "");
                sub.qos = static_cast<QoS>(subJson.value("qos", 0));
                sub.subscribedTime = subJson.value("subscribedTime", 0);
                // 回调函数需要在恢复时重新设置
                subs.push_back(sub);
            }
        }
        
        LOG_DEBUG(fmt::format("加载订阅信息: {} 个订阅", subs.size()));
        return Result<std::vector<Subscription>>::Success(subs);
    } catch (const std::exception& e) {
        return Result<std::vector<Subscription>>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "加载订阅信息异常: " + std::string(e.what())));
    }
}

Result<bool> PersistenceManager::saveClientState(const ClientState& state) {
    try {
        json j;
        j["version"] = "1.0";
        j["timestamp"] = std::time(nullptr);
        j["clientId"] = state.clientId;
        j["serverAddress"] = state.serverAddress;
        j["port"] = state.port;
        j["connected"] = state.connected;
        j["lastConnectedTime"] = state.lastConnectedTime;
        j["reconnectAttempts"] = state.reconnectAttempts;
        
        std::string data = j.dump(2);
        return storageEngine_->write(getClientStatePath(), data);
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "保存客户端状态异常: " + std::string(e.what())));
    }
}

Result<ClientState> PersistenceManager::loadClientState() {
    try {
        auto result = storageEngine_->read(getClientStatePath());
        if (!result.success) {
            ClientState state;
            return Result<ClientState>::Success(state);
        }
        
        json j = json::parse(result.value);
        ClientState state;
        state.clientId = j.value("clientId", "");
        state.serverAddress = j.value("serverAddress", "");
        state.port = j.value("port", 0);
        state.connected = j.value("connected", false);
        state.lastConnectedTime = j.value("lastConnectedTime", 0);
        state.reconnectAttempts = j.value("reconnectAttempts", 0);
        
        return Result<ClientState>::Success(state);
    } catch (const std::exception& e) {
        return Result<ClientState>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "加载客户端状态异常: " + std::string(e.what())));
    }
}

Result<bool> PersistenceManager::saveIdempotencyRecords(const std::map<std::string, time_t>& records) {
    try {
        json j;
        j["version"] = "1.0";
        j["timestamp"] = std::time(nullptr);
        j["records"] = json::array();
        
        for (const auto& [hash, timestamp] : records) {
            json recordJson;
            recordJson["messageHash"] = hash;
            recordJson["timestamp"] = timestamp;
            j["records"].push_back(recordJson);
        }
        
        std::string data = j.dump(2);
        return storageEngine_->write(getIdempotencyRecordsPath(), data);
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "保存幂等去重记录异常: " + std::string(e.what())));
    }
}

Result<std::map<std::string, time_t>> PersistenceManager::loadIdempotencyRecords() {
    try {
        auto result = storageEngine_->read(getIdempotencyRecordsPath());
        if (!result.success) {
            return Result<std::map<std::string, time_t>>::Success({});
        }
        
        json j = json::parse(result.value);
        std::map<std::string, time_t> records;
        
        if (j.contains("records") && j["records"].is_array()) {
            for (const auto& recordJson : j["records"]) {
                std::string hash = recordJson.value("messageHash", "");
                time_t timestamp = recordJson.value("timestamp", 0);
                if (!hash.empty()) {
                    records[hash] = timestamp;
                }
            }
        }
        
        LOG_DEBUG(fmt::format("加载幂等去重记录: {} 条记录", records.size()));
        return Result<std::map<std::string, time_t>>::Success(records);
    } catch (const std::exception& e) {
        return Result<std::map<std::string, time_t>>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "加载幂等去重记录异常: " + std::string(e.what())));
    }
}

Result<bool> PersistenceManager::clear() {
    try {
        return storageEngine_->clear();
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "清理持久化数据异常: " + std::string(e.what())));
    }
}

Result<bool> PersistenceManager::cleanupExpired(time_t expiryTime) {
    // 清理过期的幂等去重记录
    try {
        auto recordsResult = loadIdempotencyRecords();
        if (!recordsResult.success) {
            return Result<bool>::Success(true);
        }
        
        std::map<std::string, time_t> records = recordsResult.value;
        time_t now = std::time(nullptr);
        
        auto it = records.begin();
        while (it != records.end()) {
            if (now - it->second > expiryTime) {
                it = records.erase(it);
            } else {
                ++it;
            }
        }
        
        return saveIdempotencyRecords(records);
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "清理过期数据异常: " + std::string(e.what())));
    }
}

Result<RecoveryData> PersistenceManager::fastRecover() {
    RecoveryData data;
    
    // 恢复发送队列
    auto sendQueueResult = loadSendQueue();
    if (sendQueueResult.success) {
        data.sendQueue = sendQueueResult.value;
    }
    
    // 恢复接收队列
    auto receiveQueueResult = loadReceiveQueue();
    if (receiveQueueResult.success) {
        data.receiveQueue = receiveQueueResult.value;
    }
    
    // 恢复订阅信息
    auto subscriptionsResult = loadSubscriptions();
    if (subscriptionsResult.success) {
        data.subscriptions = subscriptionsResult.value;
    }
    
    // 恢复幂等去重记录
    auto idempotencyResult = loadIdempotencyRecords();
    if (idempotencyResult.success) {
        data.idempotencyRecords = idempotencyResult.value;
    }
    
    return Result<RecoveryData>::Success(data);
}

bool PersistenceManager::hasRecoveryData() const {
    return storageEngine_->exists(getSendQueuePath()) ||
           storageEngine_->exists(getReceiveQueuePath()) ||
           storageEngine_->exists(getSubscriptionsPath()) ||
           storageEngine_->exists(getIdempotencyRecordsPath());
}

std::string PersistenceManager::getSendQueuePath() {
    return "send_queue.json";
}

std::string PersistenceManager::getReceiveQueuePath() {
    return "receive_queue.json";
}

std::string PersistenceManager::getSubscriptionsPath() {
    return "subscriptions.json";
}

std::string PersistenceManager::getClientStatePath() {
    return "client_state.json";
}

std::string PersistenceManager::getIdempotencyRecordsPath() {
    return "idempotency_records.json";
}

} // namespace mqtt_client
