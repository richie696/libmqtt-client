/**
 * @file error.h
 * @brief 错误处理定义
 * 
 * 定义统一的错误码和错误信息结构。
 */

#ifndef MQTT_CLIENT_CORE_ERROR_H
#define MQTT_CLIENT_CORE_ERROR_H

#include <string>
#include <map>


namespace mqtt_client {

/**
 * @brief MQTT错误码枚举
 */
enum class MqttErrorCode {
    // 成功
    SUCCESS = 0,
    
    // 通用错误 (1000-1999)
    UNKNOWN_ERROR = 1000,
    INVALID_ARGUMENT = 1001,
    INVALID_STATE = 1002,
    NOT_INITIALIZED = 1003,
    NOT_CONNECTED = 1004,
    INITIALIZATION_ERROR = 1005,
    
    // 网络错误 (2000-2999)
    NETWORK_ERROR = 2000,
    CONNECTION_REFUSED = 2001,
    CONNECTION_TIMEOUT = 2002,
    NETWORK_UNREACHABLE = 2003,
    HOST_NOT_FOUND = 2004,
    SOCKET_ERROR = 2005,
    
    // MQTT协议错误 (3000-3999)
    PROTOCOL_ERROR = 3000,
    INVALID_PACKET = 3001,
    UNSUPPORTED_VERSION = 3002,
    IDENTIFIER_REJECTED = 3003,
    SERVER_UNAVAILABLE = 3004,
    BAD_USERNAME_OR_PASSWORD = 3005,
    NOT_AUTHORIZED = 3006,
    PUBLISH_FAILED = 3007,
    SUBSCRIBE_FAILED = 3008,
    UNSUBSCRIBE_FAILED = 3009,
    
    // TLS/SSL错误 (4000-4999)
    TLS_ERROR = 4000,
    CERTIFICATE_ERROR = 4001,
    CERTIFICATE_EXPIRED = 4002,
    CERTIFICATE_INVALID = 4003,
    TLS_HANDSHAKE_FAILED = 4004,
    
    // 资源错误 (5000-5999)
    RESOURCE_ERROR = 5000,
    OUT_OF_MEMORY = 5001,
    FILE_ERROR = 5002,
    THREAD_ERROR = 5003,
    QUEUE_FULL = 5004,
    PERSISTENCE_ERROR = 5005,
    
    // 配置错误 (6000-6999)
    CONFIG_ERROR = 6000,
    INVALID_CONFIG = 6001,
    CONFIG_NOT_FOUND = 6002,
    CONFIG_VALIDATION_FAILED = 6003,
    
    // 消息错误 (7000-7999)
    MESSAGE_ERROR = 7000,
    MESSAGE_TOO_LARGE = 7001,
    INVALID_TOPIC = 7002,
    INVALID_PAYLOAD = 7003
};

/**
 * @brief 错误信息结构
 */
struct MqttError {
    MqttErrorCode code;     ///< 错误码
    std::string message;     ///< 错误消息
    std::string details;     ///< 详细信息（可选）
    
    /**
     * @brief 默认构造函数
     */
    MqttError() : code(MqttErrorCode::SUCCESS), message(""), details("") {}
    
    /**
     * @brief 构造函数
     */
    MqttError(MqttErrorCode code, const std::string& message, const std::string& details = "")
        : code(code), message(message), details(details) {}
    
    /**
     * @brief 转换为字符串
     */
    [[nodiscard]] std::string toString() const {
        std::string result = "[" + std::to_string(static_cast<int>(code)) + "] " + message;
        if (!details.empty()) {
            result += " (" + details + ")";
        }
        return result;
    }
    
    /**
     * @brief 检查是否成功
     */
    [[nodiscard]] bool isSuccess() const {
        return code == MqttErrorCode::SUCCESS;
    }
    
    /**
     * @brief 检查是否失败
     */
    [[nodiscard]] bool isFailure() const {
        return !isSuccess();
    }
};

/**
 * @brief 错误码描述表
 * 
 * 使用 C++17 inline 变量，确保在多个翻译单元中只有一个定义。
 */
inline const std::map<MqttErrorCode, std::string> kErrorDescriptions = {
    {MqttErrorCode::SUCCESS, "成功"},
    {MqttErrorCode::UNKNOWN_ERROR, "未知错误"},
    {MqttErrorCode::INVALID_ARGUMENT, "无效参数"},
    {MqttErrorCode::INVALID_STATE, "无效状态"},
    {MqttErrorCode::NOT_INITIALIZED, "未初始化"},
    {MqttErrorCode::NOT_CONNECTED, "未连接"},
    {MqttErrorCode::INITIALIZATION_ERROR, "初始化错误"},
    {MqttErrorCode::NETWORK_ERROR, "网络错误"},
    {MqttErrorCode::CONNECTION_REFUSED, "连接被拒绝"},
    {MqttErrorCode::CONNECTION_TIMEOUT, "连接超时"},
    {MqttErrorCode::NETWORK_UNREACHABLE, "网络不可达"},
    {MqttErrorCode::HOST_NOT_FOUND, "主机未找到"},
    {MqttErrorCode::SOCKET_ERROR, "Socket错误"},
    {MqttErrorCode::PROTOCOL_ERROR, "协议错误"},
    {MqttErrorCode::INVALID_PACKET, "无效数据包"},
    {MqttErrorCode::UNSUPPORTED_VERSION, "不支持的版本"},
    {MqttErrorCode::IDENTIFIER_REJECTED, "标识符被拒绝"},
    {MqttErrorCode::SERVER_UNAVAILABLE, "服务器不可用"},
    {MqttErrorCode::BAD_USERNAME_OR_PASSWORD, "用户名或密码错误"},
    {MqttErrorCode::NOT_AUTHORIZED, "未授权"},
    {MqttErrorCode::PUBLISH_FAILED, "发布失败"},
    {MqttErrorCode::SUBSCRIBE_FAILED, "订阅失败"},
    {MqttErrorCode::UNSUBSCRIBE_FAILED, "取消订阅失败"},
    {MqttErrorCode::TLS_ERROR, "TLS错误"},
    {MqttErrorCode::CERTIFICATE_ERROR, "证书错误"},
    {MqttErrorCode::CERTIFICATE_EXPIRED, "证书过期"},
    {MqttErrorCode::CERTIFICATE_INVALID, "证书无效"},
    {MqttErrorCode::TLS_HANDSHAKE_FAILED, "TLS握手失败"},
    {MqttErrorCode::RESOURCE_ERROR, "资源错误"},
    {MqttErrorCode::OUT_OF_MEMORY, "内存不足"},
    {MqttErrorCode::FILE_ERROR, "文件错误"},
    {MqttErrorCode::THREAD_ERROR, "线程错误"},
    {MqttErrorCode::QUEUE_FULL, "队列已满"},
    {MqttErrorCode::PERSISTENCE_ERROR, "持久化错误"},
    {MqttErrorCode::CONFIG_ERROR, "配置错误"},
    {MqttErrorCode::INVALID_CONFIG, "无效配置"},
    {MqttErrorCode::CONFIG_NOT_FOUND, "配置未找到"},
    {MqttErrorCode::CONFIG_VALIDATION_FAILED, "配置验证失败"},
    {MqttErrorCode::MESSAGE_ERROR, "消息错误"},
    {MqttErrorCode::MESSAGE_TOO_LARGE, "消息过大"},
    {MqttErrorCode::INVALID_TOPIC, "无效主题"},
    {MqttErrorCode::INVALID_PAYLOAD, "无效负载"}
};

/**
 * @brief 获取错误码描述
 */
[[nodiscard]] inline std::string getErrorCodeDescription(MqttErrorCode code) {
    auto it = kErrorDescriptions.find(code);
    if (it != kErrorDescriptions.end()) {
        return it->second;
    }
    return "未知错误码";
}

} // namespace mqtt_client

#endif // MQTT_CLIENT_CORE_ERROR_H
