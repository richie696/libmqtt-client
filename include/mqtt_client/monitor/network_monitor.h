/**
 * @file network_monitor.h
 * @brief 网络监控器
 * 
 * 定时检测网络连通性，评估网络质量，检测网络恢复。
 */

#ifndef MQTT_CLIENT_MONITOR_NETWORK_MONITOR_H
#define MQTT_CLIENT_MONITOR_NETWORK_MONITOR_H

#include "mqtt_client/core/types.h"
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <ctime>

namespace mqtt_client {

/**
 * @brief 网络监控器
 * 
 * 定时检测网络连通性，评估网络质量，检测网络恢复。
 */
class NetworkMonitor {
public:
    /**
     * @brief 网络统计信息
     */
    struct NetworkStats {
        bool available = false;              ///< 网络是否可达
        long latency = -1;                   ///< 延迟(ms)
        double packetLoss = 0.0;             ///< 丢包率(0.0-1.0)
        time_t lastCheckTime = 0;            ///< 最后检测时间
        int consecutiveFailures = 0;         ///< 连续失败次数
    };
    
    /**
     * @brief 构造函数
     * 
     * @param host 服务器地址
     * @param port 服务器端口
     * @param checkInterval 检查间隔（秒）
     */
    explicit NetworkMonitor(const std::string& host, int port, int checkInterval = 5);
    
    /**
     * @brief 析构函数
     */
    ~NetworkMonitor();
    
    // 禁止拷贝和赋值
    NetworkMonitor(const NetworkMonitor&) = delete;
    NetworkMonitor& operator=(const NetworkMonitor&) = delete;
    
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
    bool isRunning() const;
    
    /**
     * @brief 检查网络是否可用
     * 
     * @return true 网络可用
     * @return false 网络不可用
     */
    bool isNetworkAvailable() const;
    
    /**
     * @brief 获取网络统计信息
     * 
     * @return NetworkStats 统计信息
     */
    NetworkStats getStats() const;
    
    /**
     * @brief 获取网络质量
     * 
     * @return NetworkQuality 网络质量
     */
    NetworkQuality getQuality() const;
    
    /**
     * @brief 设置网络恢复回调
     * 
     * @param callback 回调函数
     */
    void setOnNetworkRecovered(std::function<void()> callback);
    
    /**
     * @brief 设置网络丢失回调
     * 
     * @param callback 回调函数
     */
    void setOnNetworkLost(std::function<void()> callback);
    
    /**
     * @brief 设置网络质量变化回调
     * 
     * @param callback 回调函数
     */
    void setOnQualityChanged(std::function<void(NetworkQuality)> callback);

private:
    /**
     * @brief 监控线程
     */
    void monitorThread();
    
    /**
     * @brief 检查网络连通性
     * 
     * @return true 网络可达
     * @return false 网络不可达
     */
    bool checkNetworkConnectivity();
    
    /**
     * @brief 测量延迟
     * 
     * @return long 延迟（毫秒），-1表示失败
     */
    long measureLatency();
    
    /**
     * @brief 更新统计信息
     * 
     * @param available 是否可用
     * @param latency 延迟（毫秒）
     */
    void updateStats(bool available, long latency);
    
    /**
     * @brief 计算网络质量
     * 
     * @param stats 统计信息
     * @return NetworkQuality 网络质量
     */
    NetworkQuality calculateQuality(const NetworkStats& stats) const;
    
    std::string host_;              ///< 服务器地址
    int port_;                      ///< 服务器端口
    int checkInterval_;             ///< 检查间隔（秒）
    
    std::atomic<bool> running_;     ///< 是否正在运行
    std::atomic<bool> networkAvailable_;  ///< 网络是否可用
    
    mutable std::mutex mutex_;      ///< 互斥锁
    NetworkStats stats_;            ///< 统计信息
    NetworkQuality lastQuality_;    ///< 上次网络质量
    
    std::thread monitorThread_;     ///< 监控线程
    
    // 回调函数
    std::function<void()> onNetworkRecovered_;
    std::function<void()> onNetworkLost_;
    std::function<void(NetworkQuality)> onQualityChanged_;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_MONITOR_NETWORK_MONITOR_H
