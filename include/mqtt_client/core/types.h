/**
 * @file types.h
 * @brief 基础类型定义
 * 
 * 定义MQTT客户端库中使用的基础类型和枚举。
 */

#ifndef MQTT_CLIENT_CORE_TYPES_H
#define MQTT_CLIENT_CORE_TYPES_H

#include <cstdint>
#include <string>
#include <chrono>
#include <map>
#include <optional>

namespace mqtt_client {

/**
 * @brief QoS等级枚举
 */
enum class QoS : uint8_t {
    QOS_0 = 0,  ///< 最多一次（At most once）
    QOS_1 = 1,  ///< 至少一次（At least once）
    QOS_2 = 2   ///< 恰好一次（Exactly once）
};

/**
 * @brief MQTT协议版本枚举
 */
enum class MqttProtocolVersion {
    V3_1_1,  ///< MQTT 3.1.1
    V5_0     ///< MQTT 5.0
};

/**
 * @brief 连接状态枚举
 */
enum class ConnectionState {
    DISCONNECTED,   ///< 未连接
    CONNECTING,     ///< 连接中
    CONNECTED,      ///< 已连接
    DISCONNECTING,  ///< 断开中
    RECONNECTING    ///< 重连中
};

/**
 * @brief 网络质量枚举
 */
enum class NetworkQuality {
    EXCELLENT,  ///< 优秀（延迟<50ms，丢包率<1%）
    GOOD,       ///< 良好（延迟<100ms，丢包率<3%）
    FAIR,       ///< 一般（延迟<200ms，丢包率<5%）
    POOR        ///< 较差（延迟>200ms或丢包率>5%）
};

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
    TRACE = 0,  ///< 跟踪级别（最详细）
    DEBUG = 1,  ///< 调试级别
    INFO = 2,   ///< 信息级别
    WARN = 3,   ///< 警告级别
    ERROR = 4,  ///< 错误级别
    FATAL = 5   ///< 致命级别（最严重）
};

/**
 * @brief 网络类型枚举
 */
enum class NetworkType {
    PUBLIC,  ///< 公网
    VPC      ///< 内网VPC
};

/**
 * @brief MQTT 5.0属性结构
 */
struct MqttProperties {
    std::optional<int> messageExpiryInterval;      ///< 消息过期时间（秒）
    std::optional<std::string> contentType;        ///< 内容类型
    std::optional<std::string> responseTopic;       ///< 响应主题
    std::optional<std::string> correlationData;     ///< 关联数据
    std::map<std::string, std::string> userProperties;  ///< 用户属性
    
    /**
     * @brief 设置用户属性
     */
    void setUserProperty(const std::string& key, const std::string& value) {
        userProperties[key] = value;
    }
    
    /**
     * @brief 获取用户属性
     */
    std::optional<std::string> getUserProperty(const std::string& key) const {
        auto it = userProperties.find(key);
        if (it != userProperties.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    /**
     * @brief 检查是否为空
     */
    bool isEmpty() const {
        return !messageExpiryInterval.has_value() &&
               !contentType.has_value() &&
               !responseTopic.has_value() &&
               !correlationData.has_value() &&
               userProperties.empty();
    }
};

/**
 * @brief 连接状态信息
 */
struct ConnectionStateInfo {
    bool connected;                 ///< 是否已连接
    std::string serverAddress;      ///< 服务器地址
    MqttProtocolVersion protocolVersion;  ///< 协议版本
    std::chrono::system_clock::time_point connectedAt;  ///< 连接时间
    std::chrono::milliseconds lastPingTime;  ///< 上次心跳时间
    int reconnectAttempts;          ///< 重连尝试次数
};

/**
 * @brief MQTT消息结构
 */
struct MqttMessage {
    std::string topic;              ///< 主题
    std::string payload;            ///< 消息内容
    QoS qos = QoS::QOS_0;          ///< QoS等级
    bool retain = false;            ///< 是否保留
    MqttProperties properties;      ///< MQTT 5.0属性（可选）
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_CORE_TYPES_H
