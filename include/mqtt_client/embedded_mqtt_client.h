/**
 * @file embedded_mqtt_client.h
 * @brief 嵌入式MQTT客户端主类
 * 
 * 整合所有功能模块，提供统一的MQTT客户端接口。
 */

#ifndef MQTT_CLIENT_EMBEDDED_MQTT_CLIENT_H
#define MQTT_CLIENT_EMBEDDED_MQTT_CLIENT_H

#include "mqtt_client/core/types.h"
#include "mqtt_client/core/result.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/config/config.h"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <mutex>
#include <memory>

// 前向声明回调类型
namespace mqtt_client {
    using MessageCallback = std::function<void(std::string_view topic,
                                              std::string_view payload,
                                              const MqttProperties& properties)>;
    using ConnectionCallback = std::function<void(ConnectionState status,
                                                  const std::string& reason)>;
    using ErrorCallback = std::function<void(const MqttError& error)>;
}

namespace mqtt_client {

// 前向声明
class MqttConnectionManager;
class MqttMessageManager;
class MqttSubscriptionManager;
class NetworkMonitor;
class ConnectionMonitor;
class HeartbeatManager;
class ReconnectManager;
class WolfMqttAdapter;
class PersistenceManager;
class IdempotencyManager;

/**
 * @brief 嵌入式MQTT客户端
 * 
 * 整合所有功能模块，提供统一的MQTT客户端接口。
 * 支持MQTT 3.1.1和5.0协议。
 */
class EmbeddedMqttClient {
public:
    /**
     * @brief 默认构造函数（延迟初始化模式）
     * 
     * 创建客户端实例，但不初始化。
     * 适用于配置从服务器下发的场景。
     * 
     * @note 使用此构造函数后，必须调用 initialize() 进行初始化
     */
    EmbeddedMqttClient();
    
    /**
     * @brief 带配置的构造函数（立即初始化模式）
     * 
     * 使用配置立即初始化客户端。
     * 适用于配置文件已存在的场景。
     * 
     * @param config MQTT配置对象
     */
    explicit EmbeddedMqttClient(const MqttConfig& config);
    
    /**
     * @brief 析构函数
     * 
     * 自动断开连接并清理资源。
     */
    ~EmbeddedMqttClient();
    
    // 禁止拷贝和赋值
    EmbeddedMqttClient(const EmbeddedMqttClient&) = delete;
    EmbeddedMqttClient& operator=(const EmbeddedMqttClient&) = delete;
    
    // ========== 初始化和配置 ==========
    
    /**
     * @brief 初始化客户端（延迟初始化）
     * 
     * 使用配置初始化客户端。
     * 必须在调用 connect() 之前调用。
     * 
     * @param config MQTT配置对象
     * @return Result<bool> 初始化结果
     */
    [[nodiscard]] Result<bool> initialize(const MqttConfig& config);
    
    /**
     * @brief 检查是否已初始化
     * 
     * @return true 已初始化
     * @return false 未初始化
     */
    bool isInitialized() const noexcept;
    
    /**
     * @brief 清理客户端（释放资源）
     * 
     * 清理后可以重新调用 initialize() 初始化
     */
    void cleanup();
    
    /**
     * @brief 更新配置（热更新）
     * 
     * 更新客户端配置，部分配置需要重新连接才能生效。
     * 
     * @param config 新的配置对象
     * @return Result<bool> 更新结果
     */
    [[nodiscard]] Result<bool> updateConfig(const MqttConfig& config);
    
    /**
     * @brief 获取当前配置
     * 
     * @return const MqttConfig& 当前配置的引用
     */
    [[nodiscard]] const MqttConfig& getConfig() const noexcept;
    
    // ========== 连接管理 ==========
    
    /**
     * @brief 连接到MQTT服务器
     * 
     * @return Result<bool> 连接结果
     */
    [[nodiscard]] Result<bool> connect();
    
    /**
     * @brief 断开连接
     * 
     * @param force 是否强制断开（不发送DISCONNECT包）
     * @return Result<bool> 断开结果
     */
    [[nodiscard]] Result<bool> disconnect(bool force = false);
    
    /**
     * @brief 重新连接
     * 
     * @return Result<bool> 重连结果
     */
    [[nodiscard]] Result<bool> reconnect();
    
    /**
     * @brief 检查是否已连接
     * 
     * @return true 已连接
     * @return false 未连接
     */
    [[nodiscard]] bool isConnected() const noexcept;
    
    /**
     * @brief 获取连接状态
     * 
     * @return ConnectionState 连接状态
     */
    [[nodiscard]] ConnectionState getState() const;
    
    /**
     * @brief 获取网络质量
     * 
     * @return NetworkQuality 网络质量
     */
    [[nodiscard]] NetworkQuality getNetworkQuality() const;
    
    // ========== 消息发布 ==========
    
    /**
     * @brief 发布消息
     * 
     * @param topic 主题名称
     * @param payload 消息内容
     * @param qos QoS等级
     * @param retain 是否保留消息
     * @return Result<bool> 发布结果
     */
    [[nodiscard]] Result<bool> publish(std::string_view topic,
                                       std::string_view payload,
                                       QoS qos = QoS::QOS_0,
                                       bool retain = false);
    
    /**
     * @brief 发布消息（带属性，MQTT 5.0）
     * 
     * @param topic 主题名称
     * @param payload 消息内容
     * @param properties MQTT 5.0属性
     * @param qos QoS等级
     * @param retain 是否保留消息
     * @return Result<bool> 发布结果
     */
    [[nodiscard]] Result<bool> publish(std::string_view topic,
                                       std::string_view payload,
                                       const MqttProperties& properties,
                                       QoS qos = QoS::QOS_0,
                                       bool retain = false);
    
    // ========== 消息订阅 ==========
    
    /**
     * @brief 订阅主题
     * 
     * @param topic 主题名称（支持通配符）
     * @param callback 消息回调函数
     * @param qos QoS等级
     * @return Result<bool> 订阅结果
     */
    [[nodiscard]] Result<bool> subscribe(std::string_view topic,
                                         MessageCallback callback,
                                         QoS qos = QoS::QOS_0);
    
    /**
     * @brief 取消订阅
     * 
     * @param topic 主题名称
     * @return Result<bool> 取消订阅结果
     */
    [[nodiscard]] Result<bool> unsubscribe(std::string_view topic) const;
    
    /**
     * @brief 获取已订阅的主题列表
     * 
     * @return std::vector<std::string> 已订阅的主题列表
     */
    [[nodiscard]] std::vector<std::string> getSubscribedTopics() const;
    
    // ========== 事件回调 ==========
    
    /**
     * @brief 设置连接状态回调
     * 
     * @param callback 连接状态回调函数
     */
    void setConnectionCallback(const ConnectionCallback &callback);
    
    /**
     * @brief 设置错误回调
     * 
     * @param callback 错误回调函数
     */
    void setErrorCallback(const ErrorCallback &callback);

private:
    /**
     * @brief 初始化子组件
     * 
     * @return Result<bool> 初始化结果
     */
    [[nodiscard]] Result<bool> initializeComponents();
    
    /**
     * @brief 清理子组件
     */
    void cleanupComponents();
    
    /**
     * @brief 设置组件回调
     */
    void setupCallbacks();
    
    // 子组件
    std::unique_ptr<WolfMqttAdapter> wolfAdapter_;
    std::unique_ptr<MqttConnectionManager> connectionManager_;
    std::unique_ptr<MqttMessageManager> messageManager_;
    std::unique_ptr<MqttSubscriptionManager> subscriptionManager_;
    std::unique_ptr<NetworkMonitor> networkMonitor_;
    std::unique_ptr<ConnectionMonitor> connectionMonitor_;
    std::unique_ptr<HeartbeatManager> heartbeatManager_;
    std::unique_ptr<ReconnectManager> reconnectManager_;
    
    // 持久化和幂等去重
    std::shared_ptr<PersistenceManager> persistenceManager_;
    std::shared_ptr<IdempotencyManager> idempotencyManager_;
    
    // 配置和状态
    MqttConfig config_;
    std::atomic<bool> initialized_;
    std::atomic<bool> connected_;
    
    // 回调函数
    ConnectionCallback connectionCallback_;
    ErrorCallback errorCallback_;
    
    // 线程安全
    mutable std::mutex mutex_;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_EMBEDDED_MQTT_CLIENT_H
