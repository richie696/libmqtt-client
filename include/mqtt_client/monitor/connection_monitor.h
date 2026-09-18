/**
 * @file connection_monitor.h
 * @brief 连接监控器
 * 
 * 定时检查MQTT连接状态，监控连接稳定性。
 */

#ifndef MQTT_CLIENT_MONITOR_CONNECTION_MONITOR_H
#define MQTT_CLIENT_MONITOR_CONNECTION_MONITOR_H

#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <ctime>

namespace mqtt_client {

// 前向声明
class MqttConnectionManager;

/**
 * @brief 连接监控器
 * 
 * 定时检查MQTT连接状态，检测断开并触发重连。
 */
class ConnectionMonitor {
public:
    /**
     * @brief 连接统计信息
     */
    struct ConnectionStats {
        int totalDisconnects = 0;       ///< 总断开次数
        int abnormalDisconnects = 0;    ///< 异常断开次数
        time_t lastDisconnectTime = 0;  ///< 最后断开时间
        long averageUptime = 0;         ///< 平均在线时长(秒)
        double stability = 1.0;          ///< 稳定性(0.0-1.0)
    };
    
    /**
     * @brief 构造函数
     * 
     * @param connectionManager 连接管理器
     * @param checkInterval 检查间隔（秒）
     */
    explicit ConnectionMonitor(MqttConnectionManager& connectionManager,
                               int checkInterval = 10);
    
    /**
     * @brief 析构函数
     */
    ~ConnectionMonitor();
    
    // 禁止拷贝和赋值
    ConnectionMonitor(const ConnectionMonitor&) = delete;
    ConnectionMonitor& operator=(const ConnectionMonitor&) = delete;
    
    /**
     * @brief 启动监控
     */
    void start();
    
    /**
     * @brief 停止监控
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
     * @brief 获取连接统计信息
     * 
     * @return ConnectionStats 统计信息
     */
    [[nodiscard]] ConnectionStats getStats() const;
    
    /**
     * @brief 设置断开连接回调
     * 
     * @param callback 回调函数
     */
    void setOnDisconnected(std::function<void()> callback);

private:
    /**
     * @brief 监控线程
     */
    void monitorThread();
    
    /**
     * @brief 更新统计信息（需要调用者持有mutex）
     * 
     * @param connected 是否连接
     */
    void updateStatsUnlocked(bool connected);
    
    MqttConnectionManager& connectionManager_;  ///< 连接管理器
    int checkInterval_;                         ///< 检查间隔（秒）
    
    std::atomic<bool> running_;                 ///< 是否正在运行
    std::atomic<bool> lastConnectedState_;      ///< 上次连接状态
    
    mutable std::mutex mutex_;                  ///< 互斥锁
    ConnectionStats stats_;                    ///< 统计信息
    time_t lastConnectTime_;                    ///< 上次连接时间
    
    std::thread monitorThread_;                 ///< 监控线程
    
    // 回调函数
    std::function<void()> onDisconnected_;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_MONITOR_CONNECTION_MONITOR_H
