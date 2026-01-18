/**
 * @file subscription_manager.h
 * @brief MQTT订阅管理器
 * 
 * 管理主题订阅、消息分发、通配符匹配和订阅恢复。
 */

#ifndef MQTT_CLIENT_SUBSCRIPTION_SUBSCRIPTION_MANAGER_H
#define MQTT_CLIENT_SUBSCRIPTION_SUBSCRIPTION_MANAGER_H

#include "mqtt_client/core/types.h"
#include "mqtt_client/core/result.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/config/config.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include <ctime>

namespace mqtt_client {

// 前向声明
class MqttConnectionManager;

/**
 * @brief 消息回调函数类型
 */
using MessageCallback = std::function<void(const std::string& topic,
                                           const std::string& payload,
                                           const MqttProperties& properties)>;

/**
 * @brief 订阅信息结构
 */
struct Subscription {
    std::string topic;              ///< 主题（可能包含通配符）
    MessageCallback callback;        ///< 消息回调函数
    QoS qos = QoS::QOS_0;          ///< QoS等级
    time_t subscribedTime = 0;      ///< 订阅时间
};

/**
 * @brief MQTT订阅管理器
 * 
 * 负责管理主题订阅、消息分发、通配符匹配和订阅恢复。
 */
class MqttSubscriptionManager {
public:
    /**
     * @brief 构造函数
     * 
     * @param connectionManager 连接管理器
     * @param config MQTT配置
     */
    explicit MqttSubscriptionManager(MqttConnectionManager& connectionManager,
                                    const MqttConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~MqttSubscriptionManager();
    
    // 禁止拷贝和赋值
    MqttSubscriptionManager(const MqttSubscriptionManager&) = delete;
    MqttSubscriptionManager& operator=(const MqttSubscriptionManager&) = delete;
    
    /**
     * @brief 订阅主题
     * 
     * @param topic 主题（支持通配符：+ 和 #）
     * @param callback 消息回调函数
     * @param qos QoS等级
     * @return Result<bool> 订阅结果
     */
    Result<bool> subscribe(const std::string& topic,
                         MessageCallback callback,
                         QoS qos = QoS::QOS_0);
    
    /**
     * @brief 取消订阅
     * 
     * @param topic 主题
     * @return Result<bool> 取消订阅结果
     */
    Result<bool> unsubscribe(const std::string& topic);
    
    /**
     * @brief 取消所有订阅
     */
    void unsubscribeAll();
    
    /**
     * @brief 重连后恢复所有订阅
     * 
     * @return Result<bool> 恢复结果
     */
    Result<bool> resubscribeAll();
    
    /**
     * @brief 分发消息
     * 
     * 根据主题匹配订阅，调用相应的回调函数。
     * 
     * @param topic 消息主题
     * @param payload 消息内容
     * @param properties MQTT 5.0属性（可选）
     */
    void dispatchMessage(const std::string& topic,
                        const std::string& payload,
                        const MqttProperties& properties = MqttProperties());
    
    /**
     * @brief 检查是否已订阅
     * 
     * @param topic 主题
     * @return true 已订阅
     * @return false 未订阅
     */
    bool isSubscribed(const std::string& topic) const;
    
    /**
     * @brief 获取所有已订阅的主题
     * 
     * @return std::vector<std::string> 主题列表
     */
    std::vector<std::string> getSubscribedTopics() const;
    
    /**
     * @brief 获取订阅数量
     * 
     * @return size_t 订阅数量
     */
    size_t getSubscriptionCount() const;
    
    /**
     * @brief 保存订阅信息（用于持久化）
     * 
     * @param topic 主题
     * @param callback 回调函数
     * @param qos QoS等级
     * @return Result<bool> 保存结果
     */
    Result<bool> saveSubscription(const std::string& topic,
                                  MessageCallback callback,
                                  QoS qos);

private:
    /**
     * @brief 主题匹配（支持通配符）
     * 
     * 支持MQTT通配符：
     * - + : 单级通配符，匹配一个主题级别
     * - # : 多级通配符，匹配零个或多个主题级别（必须在末尾）
     * 
     * @param filter 订阅过滤器（可能包含通配符）
     * @param topic 实际主题
     * @return true 匹配
     * @return false 不匹配
     */
    bool topicMatches(const std::string& filter, const std::string& topic) const;
    
    /**
     * @brief 验证主题过滤器
     * 
     * @param topic 主题过滤器
     * @return true 有效
     * @return false 无效
     */
    bool validateTopicFilter(const std::string& topic) const;
    
    // 连接管理器
    MqttConnectionManager& connectionManager_;
    
    // 配置
    const MqttConfig& config_;
    
    // 订阅缓存（主题 -> 订阅信息）
    std::unordered_map<std::string, Subscription> subscriptions_;
    
    // 线程安全
    mutable std::mutex mutex_;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_SUBSCRIPTION_SUBSCRIPTION_MANAGER_H
