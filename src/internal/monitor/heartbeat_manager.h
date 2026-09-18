/**
 * @file heartbeat_manager.h
 * @brief 心跳管理器
 * 
 * 定期发送心跳消息，监控连接健康状态。
 */

#ifndef MQTT_CLIENT_MONITOR_HEARTBEAT_MANAGER_H
#define MQTT_CLIENT_MONITOR_HEARTBEAT_MANAGER_H

#include "mqtt_client/core/types.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <ctime>

namespace mqtt_client {

// 前向声明
class MqttMessageManager;

/**
 * @brief 心跳管理器
 * 
 * 定期发送心跳消息，监控连接健康状态。
 */
class HeartbeatManager {
public:
    /**
     * @brief 心跳统计信息
     */
    struct HeartbeatStats {
        int totalSent = 0;              ///< 总发送次数
        int totalFailed = 0;            ///< 总失败次数
        long averageLatency = 0;        ///< 平均延迟(ms)
        long maxLatency = 0;            ///< 最大延迟(ms)
        time_t lastSentTime = 0;        ///< 最后发送时间
    };
    
    /**
     * @brief 构造函数
     * 
     * @param messageManager 消息管理器
     * @param interval 心跳间隔（秒）
     * @param topic 心跳主题（可选）
     */
    explicit HeartbeatManager(MqttMessageManager& messageManager,
                             int interval = 30,
                             const std::string& topic = "");
    
    /**
     * @brief 析构函数
     */
    ~HeartbeatManager();
    
    // 禁止拷贝和赋值
    HeartbeatManager(const HeartbeatManager&) = delete;
    HeartbeatManager& operator=(const HeartbeatManager&) = delete;
    
    /**
     * @brief 启动心跳
     */
    void start();
    
    /**
     * @brief 停止心跳
     */
    void stop();
    
    /**
     * @brief 检查是否正在运行
     * 
     * @return true 正在运行
     * @return false 未运行
     */
    [[nodiscard]] bool isRunning() const;
    
    /**
     * @brief 获取心跳统计信息
     * 
     * @return HeartbeatStats 统计信息
     */
    [[nodiscard]] HeartbeatStats getStats() const;
    
    /**
     * @brief 手动发送心跳
     * 
     * @return bool 发送结果
     */
    [[nodiscard]] bool sendHeartbeat();

private:
    /**
     * @brief 心跳线程
     */
    void heartbeatThread();
    
    /**
     * @brief 更新统计信息
     * 
     * @param success 是否成功
     * @param latency 延迟（毫秒）
     */
    void updateStats(bool success, long latency);
    
    MqttMessageManager& messageManager_;  ///< 消息管理器
    int interval_;                        ///< 心跳间隔（秒）
    std::string topic_;                   ///< 心跳主题
    
    std::atomic<bool> running_;           ///< 是否正在运行
    
    mutable std::mutex mutex_;            ///< 互斥锁
    std::mutex waitMutex_;                ///< 心跳等待锁
    std::condition_variable waitCv_;      ///< 用于立即中断等待
    HeartbeatStats stats_;                ///< 统计信息
    
    std::thread heartbeatThread_;         ///< 心跳线程
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_MONITOR_HEARTBEAT_MANAGER_H
