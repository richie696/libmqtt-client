/**
 * @file idempotency_manager.h
 * @brief 幂等去重管理器
 * 
 * 基于消息hash值进行去重，支持持久化和自动清理
 */

#ifndef MQTT_CLIENT_PERSISTENCE_IDEMPOTENCY_MANAGER_H
#define MQTT_CLIENT_PERSISTENCE_IDEMPOTENCY_MANAGER_H

#include "internal/persistence/persistence_manager.h"
#include "mqtt_client/core/types.h"
#include "mqtt_client/core/result.h"
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <ctime>

namespace mqtt_client {

// 前向声明
struct MqttMessage;

/**
 * @brief 幂等去重管理器
 * 
 * 基于消息hash值进行去重，支持持久化和自动清理
 */
class IdempotencyManager {
public:
    /**
     * @brief 构造函数
     * 
     * @param persistenceManager 持久化管理器
     * @param retentionTime 去重数据保存时间（秒），默认5分钟
     * @param cleanupInterval 清理间隔（秒），默认1小时
     */
    explicit IdempotencyManager(
        const std::shared_ptr<PersistenceManager> &persistenceManager,
        time_t retentionTime = 5 * 60,          // 5分钟
        time_t cleanupInterval = 3600);          // 1小时
    
    /**
     * @brief 析构函数
     */
    ~IdempotencyManager();
    
    // 禁止拷贝和赋值
    IdempotencyManager(const IdempotencyManager&) = delete;
    IdempotencyManager& operator=(const IdempotencyManager&) = delete;
    
    /**
     * @brief 检查消息是否已处理（幂等检查）
     * 
     * @param messageHash 消息hash值
     * @return true=已处理（重复消息），false=未处理（新消息）
     */
    [[nodiscard]] bool isDuplicate(const std::string& messageHash);
    
    /**
     * @brief 标记消息已处理
     * 
     * @param messageHash 消息hash值
     */
    [[nodiscard]] Result<bool> markProcessed(const std::string& messageHash);
    
    /**
     * @brief 计算消息hash值
     * 
     * @param message 消息对象
     * @return 稳定的128位消息hash值
     */
    [[nodiscard]] static std::string calculateMessageHash(const MqttMessage& message);
    
    /**
     * @brief 计算消息hash值（从原始数据）
     * 
     * @param topic 主题
     * @param payload 负载
     * @param qos QoS级别
     * @return 消息hash值
     */
    [[nodiscard]] static std::string calculateMessageHash(const std::string& topic,
                                                          const std::string& payload,
                                                          QoS qos);
    
    /**
     * @brief 获取去重数据数量
     */
    [[nodiscard]] size_t getDuplicateCount() const;
    
    /**
     * @brief 设置保存时间
     */
    void setRetentionTime(time_t retentionTime);
    
    /**
     * @brief 获取保存时间
     */
    [[nodiscard]] time_t getRetentionTime() const;
    
    /**
     * @brief 手动触发清理
     */
    [[nodiscard]] Result<bool> cleanup();
    
    /**
     * @brief 持久化去重数据
     */
    [[nodiscard]] Result<bool> persist();
    
    /**
     * @brief 从持久化恢复
     */
    [[nodiscard]] Result<bool> recover();
    
    /**
     * @brief 清空所有去重数据
     */
    void clear();
    
private:
    /**
     * @brief 清理线程
     */
    void cleanupThread();
    
    /**
     * @brief 清理过期数据
     */
    void cleanupExpired();
    
    std::shared_ptr<PersistenceManager> persistenceManager_;
    
    // 去重数据存储
    // key: messageHash, value: timestamp
    std::map<std::string, time_t> duplicateRecords_;
    mutable std::mutex mutex_;
    
    // 配置
    time_t retentionTime_;      // 保存时间
    time_t cleanupInterval_;     // 清理间隔
    
    // 清理线程
    std::atomic<bool> running_{false};
    std::thread cleanupThread_;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_PERSISTENCE_IDEMPOTENCY_MANAGER_H
