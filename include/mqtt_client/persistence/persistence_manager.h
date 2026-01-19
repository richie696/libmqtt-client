/**
 * @file persistence_manager.h
 * @brief 持久化管理器
 * 
 * 统一管理消息队列、订阅信息、状态的持久化
 */

#ifndef MQTT_CLIENT_PERSISTENCE_PERSISTENCE_MANAGER_H
#define MQTT_CLIENT_PERSISTENCE_PERSISTENCE_MANAGER_H

#include "mqtt_client/persistence/storage_engine.h"
#include "mqtt_client/core/types.h"
#include "mqtt_client/core/result.h"
#include "mqtt_client/subscription/subscription_manager.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <ctime>

namespace mqtt_client {

// 前向声明
struct Message;
struct Subscription;
struct ClientState;

/**
 * @brief 恢复数据
 */
struct RecoveryData {
    std::vector<MqttMessage> sendQueue;              ///< 发送队列
    std::vector<MqttMessage> receiveQueue;           ///< 接收队列
    std::vector<Subscription> subscriptions;         ///< 订阅信息
    std::map<std::string, time_t> idempotencyRecords; ///< 幂等去重记录
};

/**
 * @brief 客户端状态
 */
struct ClientState {
    std::string clientId;                             ///< 客户端ID
    std::string serverAddress;                        ///< 服务器地址
    int port = 0;                                     ///< 端口
    bool connected = false;                           ///< 是否已连接
    time_t lastConnectedTime = 0;                    ///< 最后连接时间
    int reconnectAttempts = 0;                        ///< 重连尝试次数
};

/**
 * @brief 持久化管理器
 * 
 * 统一管理消息队列、订阅信息、状态的持久化
 */
class PersistenceManager {
public:
    /**
     * @brief 构造函数
     * 
     * @param storagePath 存储路径
     * @param storageEngine 存储引擎（可选，默认使用文件存储）
     */
    explicit PersistenceManager(
        const std::string& storagePath,
        const std::shared_ptr<StorageEngine> &storageEngine = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~PersistenceManager() = default;
    
    // ========== 消息队列持久化 ==========
    
    /**
     * @brief 保存发送队列
     */
    [[nodiscard]] Result<bool> saveSendQueue(const std::vector<MqttMessage>& messages);
    
    /**
     * @brief 加载发送队列
     */
    [[nodiscard]] Result<std::vector<MqttMessage>> loadSendQueue();
    
    /**
     * @brief 保存接收队列
     */
    [[nodiscard]] Result<bool> saveReceiveQueue(const std::vector<MqttMessage>& messages);
    
    /**
     * @brief 加载接收队列
     */
    [[nodiscard]] Result<std::vector<MqttMessage>> loadReceiveQueue();
    
    // ========== 订阅信息持久化 ==========
    
    /**
     * @brief 保存订阅信息
     */
    [[nodiscard]] Result<bool> saveSubscriptions(const std::vector<Subscription>& subs);
    
    /**
     * @brief 加载订阅信息
     */
    [[nodiscard]] Result<std::vector<Subscription>> loadSubscriptions();
    
    // ========== 状态持久化 ==========
    
    /**
     * @brief 保存客户端状态
     */
    [[nodiscard]] Result<bool> saveClientState(const ClientState& state);
    
    /**
     * @brief 加载客户端状态
     */
    [[nodiscard]] Result<ClientState> loadClientState();
    
    // ========== 幂等去重持久化 ==========
    
    /**
     * @brief 保存幂等去重记录
     */
    [[nodiscard]] Result<bool> saveIdempotencyRecords(const std::map<std::string, time_t>& records);
    
    /**
     * @brief 加载幂等去重记录
     */
    [[nodiscard]] Result<std::map<std::string, time_t>> loadIdempotencyRecords();
    
    // ========== 清理 ==========
    
    /**
     * @brief 清理所有持久化数据
     */
    [[nodiscard]] Result<bool> clear();
    
    /**
     * @brief 清理过期的持久化数据
     */
    [[nodiscard]] Result<bool> cleanupExpired(time_t expiryTime);
    
    // ========== 快速恢复 ==========
    
    /**
     * @brief 快速恢复所有数据
     */
    [[nodiscard]] Result<RecoveryData> fastRecover();
    
    /**
     * @brief 检查是否有待恢复的数据
     */
    [[nodiscard]] bool hasRecoveryData() const;
    
private:
    std::string storagePath_;
    std::shared_ptr<StorageEngine> storageEngine_;
    
    // 文件路径（静态方法，不依赖实例状态）
    static std::string getSendQueuePath();
    static std::string getReceiveQueuePath();
    static std::string getSubscriptionsPath();
    static std::string getClientStatePath();
    static std::string getIdempotencyRecordsPath();
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_PERSISTENCE_PERSISTENCE_MANAGER_H
