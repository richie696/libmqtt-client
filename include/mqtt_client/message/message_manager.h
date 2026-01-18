/**
 * @file message_manager.h
 * @brief MQTT消息管理器
 * 
 * 管理消息发布队列、重试机制、优先级和统计信息。
 */

#ifndef MQTT_CLIENT_MESSAGE_MESSAGE_MANAGER_H
#define MQTT_CLIENT_MESSAGE_MESSAGE_MANAGER_H

#include "mqtt_client/core/types.h"
#include "mqtt_client/core/result.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/config/config.h"
#include <string>
#include <queue>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <ctime>
#include <cstdint>

namespace mqtt_client {

// 前向声明
class MqttConnectionManager;

/**
 * @brief MQTT消息队列项结构
 * 
 * 用于消息管理器内部的消息队列，包含额外的管理信息。
 */
struct QueuedMessage {
    std::string topic;          ///< 主题
    std::string payload;        ///< 消息内容
    QoS qos = QoS::QOS_0;      ///< QoS等级
    bool retained = false;      ///< 是否保留
    int priority = 5;           ///< 优先级(0-9, 9最高)
    int retryCount = 0;         ///< 重试次数
    time_t timestamp = 0;       ///< 时间戳
    std::string messageId;      ///< 消息ID（可选）
    
    /**
     * @brief 比较函数（用于优先级队列）
     * 
     * 注意：priority_queue是最大堆，所以operator<应该返回true表示优先级更低
     * 我们希望优先级高的在前，所以优先级高的应该返回false
     */
    bool operator<(const QueuedMessage& other) const {
        // 优先级高的在前（返回false），相同优先级按时间戳排序（早的在前）
        if (priority != other.priority) {
            return priority < other.priority;  // 优先级低的返回true（排在后面）
        }
        // 相同优先级，时间戳小的在前（早的在前）
        return timestamp > other.timestamp;  // 时间戳大的返回true（排在后面）
    }
    
    /**
     * @brief 相等比较（用于完整性）
     */
    bool operator==(const QueuedMessage& other) const {
        return topic == other.topic &&
               payload == other.payload &&
               qos == other.qos &&
               retained == other.retained &&
               priority == other.priority &&
               timestamp == other.timestamp;
    }
};

/**
 * @brief MQTT消息管理器
 * 
 * 负责管理消息发布队列、重试机制、优先级和统计信息。
 */
class MqttMessageManager {
public:
    /**
     * @brief 构造函数
     * 
     * @param connectionManager 连接管理器
     * @param config MQTT配置
     */
    explicit MqttMessageManager(MqttConnectionManager& connectionManager,
                                const MqttConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~MqttMessageManager();
    
    // 禁止拷贝和赋值
    MqttMessageManager(const MqttMessageManager&) = delete;
    MqttMessageManager& operator=(const MqttMessageManager&) = delete;
    
    /**
     * @brief 发布消息（异步）
     * 
     * @param topic 主题
     * @param payload 消息内容
     * @param qos QoS等级
     * @param retained 是否保留
     * @param priority 优先级（0-9，9最高，默认5）
     * @return Result<bool> 发布结果
     */
    Result<bool> publish(const std::string& topic,
                         const std::string& payload,
                         QoS qos = QoS::QOS_0,
                         bool retained = false,
                         int priority = 5);
    
    /**
     * @brief 发布消息（同步，带超时）
     * 
     * @param topic 主题
     * @param payload 消息内容
     * @param qos QoS等级
     * @param retained 是否保留
     * @param timeoutMs 超时时间（毫秒）
     * @return Result<bool> 发布结果
     */
    Result<bool> publishSync(const std::string& topic,
                            const std::string& payload,
                            QoS qos = QoS::QOS_0,
                            bool retained = false,
                            int timeoutMs = 5000);
    
    /**
     * @brief 将消息加入队列（用于离线时缓存）
     * 
     * @param topic 主题
     * @param payload 消息内容
     * @param qos QoS等级
     * @param retained 是否保留
     * @param priority 优先级
     * @return Result<bool> 入队结果
     */
    Result<bool> queueMessage(const std::string& topic,
                              const std::string& payload,
                              QoS qos = QoS::QOS_0,
                              bool retained = false,
                              int priority = 5);
    
    /**
     * @brief 获取队列大小
     * 
     * @return size_t 队列大小
     */
    size_t getQueueSize() const;
    
    /**
     * @brief 清空队列
     */
    void clearQueue();
    
    /**
     * @brief 处理队列中的消息
     * 
     * 处理队列中待发送的消息（在连接恢复后调用）
     */
    void processQueue();
    
    /**
     * @brief 消息统计信息
     */
    struct MessageStats {
        uint64_t totalPublished = 0;    ///< 总发布数
        uint64_t totalFailed = 0;       ///< 总失败数
        uint64_t totalRetried = 0;       ///< 总重试数
        uint64_t totalQueued = 0;       ///< 总入队数
        double successRate = 0.0;       ///< 成功率
    };
    
    /**
     * @brief 获取统计信息
     * 
     * @return MessageStats 统计信息
     */
    MessageStats getStats() const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStats();

private:
    /**
     * @brief 发送消息（内部实现）
     * 
     * @param msg 消息对象
     * @return Result<bool> 发送结果
     */
    Result<bool> sendMessage(const QueuedMessage& msg);
    
    /**
     * @brief 处理批量消息
     */
    void processBatch();
    
    /**
     * @brief 启动消息处理线程（延迟启动，避免构造时mutex问题）
     */
    void startThread();
    
    /**
     * @brief 消息处理线程
     */
    void messageThread();
    
    /**
     * @brief 更新统计信息
     * 
     * @param success 是否成功
     */
    void updateStats(bool success);
    
    // 连接管理器
    MqttConnectionManager& connectionManager_;
    
    // 配置
    const MqttConfig& config_;
    
    // 消息队列（优先级队列）
    // 使用vector作为底层容器，std::less作为比较器（最大堆）
    std::priority_queue<QueuedMessage, std::vector<QueuedMessage>, std::less<QueuedMessage>> queue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCV_;
    
    // 批量处理队列
    std::vector<QueuedMessage> batchQueue_;
    mutable std::mutex batchMutex_;
    
    // 统计信息
    mutable std::mutex statsMutex_;
    MessageStats stats_;
    
    // 线程控制
    std::atomic<bool> running_;
    std::thread messageThread_;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_MESSAGE_MESSAGE_MANAGER_H
