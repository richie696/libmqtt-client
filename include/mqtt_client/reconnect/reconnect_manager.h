/**
 * @file reconnect_manager.h
 * @brief 重连管理器
 * 
 * 实现智能重连策略，包括指数退避和随机抖动。
 */

#ifndef MQTT_CLIENT_RECONNECT_RECONNECT_MANAGER_H
#define MQTT_CLIENT_RECONNECT_RECONNECT_MANAGER_H

#include "mqtt_client/config/config.h"
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>

namespace mqtt_client {

/**
 * @brief 重连管理器
 * 
 * 实现智能重连策略，包括指数退避和随机抖动，避免雷鸣群效应。
 */
class ReconnectManager {
public:
    /**
     * @brief 构造函数
     * 
     * @param config 重连配置
     */
    explicit ReconnectManager(const MqttConfig::ReconnectConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~ReconnectManager();
    
    // 禁止拷贝和赋值
    ReconnectManager(const ReconnectManager&) = delete;
    ReconnectManager& operator=(const ReconnectManager&) = delete;
    
    /**
     * @brief 开始重连
     * 
     * @param connectFunc 连接函数，返回true表示连接成功
     * @return bool 是否成功启动重连
     */
    [[nodiscard]] bool startReconnect(std::function<bool()> connectFunc);
    
    /**
     * @brief 停止重连
     */
    void stopReconnect();
    
    /**
     * @brief 检查是否正在重连
     * 
     * @return true 正在重连
     * @return false 未在重连
     */
    [[nodiscard]] bool isReconnecting() const;
    
    /**
     * @brief 获取重连尝试次数
     * 
     * @return int 尝试次数
     */
    [[nodiscard]] int getAttemptCount() const;
    
    /**
     * @brief 获取总重试时间
     * 
     * @return long 总重试时间（毫秒）
     */
    [[nodiscard]] long getTotalRetryTime() const;
    
    /**
     * @brief 获取下次重试间隔
     * 
     * @return long 下次重试间隔（毫秒）
     */
    [[nodiscard]] long getNextRetryInterval() const;
    
    /**
     * @brief 更新配置
     * 
     * @param newConfig 新配置
     */
    void updateConfig(const MqttConfig::ReconnectConfig& newConfig);
    
    /**
     * @brief 重置尝试次数
     */
    void resetAttempts();
    
    /**
     * @brief 设置重连回调
     * 
     * @param callback 回调函数，参数为(尝试次数, 最大次数, 下次间隔)
     */
    void setOnReconnectAttempt(std::function<void(int, int, long)> callback);

private:
    /**
     * @brief 计算下次重试间隔（核心算法）
     * 
     * 实现指数退避和随机抖动。
     * 
     * @param attempt 当前尝试次数
     * @return long 重试间隔（毫秒）
     */
    [[nodiscard]] long calculateBackoffInterval(int attempt) const noexcept;
    
    /**
     * @brief 重连线程
     * 
     * @param connectFunc 连接函数
     */
    void reconnectThread(std::function<bool()> connectFunc);
    
    MqttConfig::ReconnectConfig config_;  ///< 重连配置
    
    std::atomic<int> attemptCount_;       ///< 尝试次数
    std::atomic<bool> reconnecting_;     ///< 是否正在重连
    std::atomic<bool> shouldStop_;       ///< 是否应该停止
    std::atomic<long> totalRetryTime_;   ///< 总重试时间
    
    std::thread reconnectThread_;         ///< 重连线程
    mutable std::mutex mutex_;             ///< 互斥锁
    std::condition_variable cv_;          ///< 条件变量
    
    // 随机数生成器（用于抖动）
    mutable std::random_device randomDevice_;
    mutable std::mutex randomMutex_;      ///< 随机数生成器互斥锁
    
    // 回调函数
    std::function<void(int, int, long)> onReconnectAttempt_;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_RECONNECT_RECONNECT_MANAGER_H
