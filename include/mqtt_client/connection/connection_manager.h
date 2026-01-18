/**
 * @file connection_manager.h
 * @brief MQTT连接管理器
 * 
 * 管理MQTT连接的建立、断开、重连，处理连接状态变化。
 */

#ifndef MQTT_CLIENT_CONNECTION_CONNECTION_MANAGER_H
#define MQTT_CLIENT_CONNECTION_CONNECTION_MANAGER_H

#include "mqtt_client/core/types.h"
#include "mqtt_client/core/result.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/config/config.h"
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <ctime>

namespace mqtt_client {

// 前向声明
class WolfMqttAdapter;

/**
 * @brief MQTT连接管理器
 * 
 * 负责管理MQTT连接的建立、断开、重连，以及连接状态的管理。
 */
class MqttConnectionManager {
public:
    /**
     * @brief 构造函数
     * 
     * @param config MQTT配置
     * @param adapter wolfMQTT适配器（可选，如果为nullptr则内部创建）
     */
    explicit MqttConnectionManager(const MqttConfig& config,
                                   WolfMqttAdapter* adapter = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~MqttConnectionManager();
    
    // 禁止拷贝和赋值
    MqttConnectionManager(const MqttConnectionManager&) = delete;
    MqttConnectionManager& operator=(const MqttConnectionManager&) = delete;
    
    /**
     * @brief 连接服务器
     * 
     * @return Result<bool> 连接结果
     */
    Result<bool> connect();
    
    /**
     * @brief 断开连接
     * 
     * @return Result<bool> 断开结果
     */
    Result<bool> disconnect();
    
    /**
     * @brief 重新连接
     * 
     * @return Result<bool> 重连结果
     */
    Result<bool> reconnect();
    
    /**
     * @brief 检查是否已连接
     * 
     * @return true 已连接
     * @return false 未连接
     */
    bool isConnected() const;
    
    /**
     * @brief 获取连接状态
     * 
     * @return ConnectionState 连接状态
     */
    ConnectionState getState() const;
    
    /**
     * @brief 获取上次连接时间
     * 
     * @return time_t 上次连接时间（Unix时间戳）
     */
    time_t getLastConnectTime() const;
    
    /**
     * @brief 获取重连次数
     * 
     * @return int 重连次数
     */
    int getReconnectCount() const;
    
    /**
     * @brief 重置重连计数
     */
    void resetReconnectCount();
    
    /**
     * @brief 设置连接成功回调
     * 
     * @param callback 回调函数
     */
    void setOnConnected(std::function<void()> callback);
    
    /**
     * @brief 设置连接丢失回调
     * 
     * @param callback 回调函数，参数为丢失原因
     */
    void setOnConnectionLost(std::function<void(const std::string&)> callback);
    
    /**
     * @brief 设置连接失败回调
     * 
     * @param callback 回调函数，参数为失败原因
     */
    void setOnConnectFailure(std::function<void(const std::string&)> callback);
    
    /**
     * @brief 获取wolfMQTT适配器
     * 
     * @return WolfMqttAdapter* 适配器指针（可能为nullptr）
     */
    WolfMqttAdapter* getAdapter() const;

private:
    /**
     * @brief 处理连接成功
     */
    void handleConnectSuccess();
    
    /**
     * @brief 处理连接失败
     * 
     * @param reason 失败原因
     */
    void handleConnectFailure(const std::string& reason);
    
    /**
     * @brief 处理连接丢失
     * 
     * @param cause 丢失原因
     */
    void handleConnectionLost(const std::string& cause);
    
    /**
     * @brief 更新连接状态
     * 
     * @param newState 新状态
     */
    void updateState(ConnectionState newState);
    
    // 配置
    const MqttConfig& config_;
    
    // wolfMQTT适配器（可能由外部传入或内部创建）
    // 注意：如果外部传入，使用自定义删除器，不实际删除对象
    std::unique_ptr<WolfMqttAdapter, std::function<void(WolfMqttAdapter*)>> adapter_;
    bool ownsAdapter_;  // 是否拥有适配器
    
    // 连接状态
    std::atomic<ConnectionState> state_;
    std::atomic<bool> connected_;
    std::atomic<time_t> lastConnectTime_;
    std::atomic<int> reconnectCount_;
    
    // 回调函数
    std::function<void()> onConnected_;
    std::function<void(const std::string&)> onConnectionLost_;
    std::function<void(const std::string&)> onConnectFailure_;
    
    // 线程安全
    mutable std::mutex mutex_;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_CONNECTION_CONNECTION_MANAGER_H
