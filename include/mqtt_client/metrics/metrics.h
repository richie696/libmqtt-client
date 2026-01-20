/**
 * @file metrics.h
 * @brief 监控指标定义
 * 
 * 定义MQTT客户端库的监控指标结构和相关功能。
 */

#ifndef MQTT_CLIENT_METRICS_METRICS_H
#define MQTT_CLIENT_METRICS_METRICS_H

#include "mqtt_client/core/error.h"
#include <string>
#include <map>
#include <chrono>

namespace mqtt_client {

/**
 * @brief 监控指标结构
 */
struct MqttMetrics {
    // 连接指标
    struct {
        int totalConnections = 0;           ///< 总连接次数
        int successfulConnections = 0;      ///< 成功连接次数
        int failedConnections = 0;          ///< 失败连接次数
        int disconnections = 0;             ///< 断开连接次数
        int reconnections = 0;              ///< 重连次数
        std::chrono::milliseconds avgConnectionTime{0};  ///< 平均连接时间
    } connection;
    
    // 消息指标
    struct {
        int messagesSent = 0;                ///< 发送消息数
        int messagesReceived = 0;           ///< 接收消息数
        int messagesFailed = 0;             ///< 失败消息数
        int bytesSent = 0;                  ///< 发送字节数
        int bytesReceived = 0;              ///< 接收字节数
        std::chrono::milliseconds avgLatency{0};  ///< 平均延迟
    } message;
    
    // 网络指标
    struct {
        int networkErrors = 0;              ///< 网络错误数
        int timeouts = 0;                   ///< 超时次数
        double packetLossRate = 0.0;        ///< 丢包率
        std::chrono::milliseconds avgLatency{0};  ///< 平均延迟
    } network;
    
    // 订阅指标
    struct {
        int subscriptions = 0;              ///< 订阅数
        int unsubscriptions = 0;            ///< 取消订阅数
        int subscriptionFailures = 0;        ///< 订阅失败数
    } subscription;
    
    // 心跳指标
    struct {
        int heartbeatsSent = 0;             ///< 发送心跳数
        int heartbeatsReceived = 0;         ///< 接收心跳数
        int heartbeatTimeouts = 0;          ///< 心跳超时数
    } heartbeat;
    
    // 性能指标
    struct {
        size_t memoryUsage = 0;             ///< 内存使用（字节）
        double cpuUsage = 0.0;               ///< CPU使用率（%）
        int activeThreads = 0;               ///< 活跃线程数
        int queueSize = 0;                  ///< 队列大小
    } performance;
    
    // 错误指标
    struct {
        int totalErrors = 0;                 ///< 总错误数
        std::map<MqttErrorCode, int> errorCounts;  ///< 各错误码的计数
    } error;
    
    // 业务指标（用户自定义）
    std::map<std::string, int> customMetrics;
    
    /**
     * @brief 重置所有指标
     */
    void reset();
    
    /**
     * @brief 转换为字符串表示
     * 
     * @return std::string 格式化的指标字符串
     */
    [[nodiscard]] std::string toString() const;
    
    /**
     * @brief 转换为JSON格式
     * 
     * @return std::string JSON格式的指标字符串
     */
    [[nodiscard]] std::string toJson() const;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_METRICS_METRICS_H
