/**
 * @file subscription_manager.cpp
 * @brief MQTT订阅管理器实现
 */

#include "mqtt_client/subscription/subscription_manager.h"
#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/adapter/wolfmqtt_adapter.h"
#include "mqtt_client/logger/logger_interface.h"
#include <fmt/core.h>
#include <algorithm>

namespace mqtt_client {

MqttSubscriptionManager::MqttSubscriptionManager(MqttConnectionManager& connectionManager,
                                                 [[maybe_unused]] const MqttConfig& config)
    : connectionManager_(connectionManager)
{
}

MqttSubscriptionManager::~MqttSubscriptionManager() {
    std::unique_lock lock(mutex_);
    subscriptions_.clear();
}

Result<bool> MqttSubscriptionManager::subscribe(std::string_view topic,
                                                MessageCallback callback,
                                                QoS qos) {
    // 验证主题
    if (topic.empty() || topic.length() > 65535) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INVALID_TOPIC,
                     "主题无效: 空或过长"));
    }
    
    // 验证主题过滤器
    if (!validateTopicFilter(topic)) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INVALID_TOPIC,
                     "主题过滤器无效: " + std::string(topic)));
    }
    
    // 检查连接状态
    if (!connectionManager_.isConnected()) {
        Subscription sub;
        sub.topic = std::string(topic);
        sub.callback = callback;
        sub.qos = qos;
        sub.subscribedTime = std::time(nullptr);

        const std::string savedTopic = sub.topic;
        std::unique_lock lock(mutex_);
        subscriptions_[sub.topic] = std::move(sub);
        LOG_DEBUG(fmt::format("未连接，保存订阅信息: {}", savedTopic));
        return Result<bool>::Success(true);
    }
    
    // 通过适配器订阅
    auto* adapter = connectionManager_.getAdapter();
    if (!adapter) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "适配器未初始化"));
    }
    
    // 底层操作期间不能持有订阅锁。wolfMQTT等待SUBACK时可能同时收到
    // 业务消息，并在同一线程回调dispatchMessage。
    const auto result = adapter->subscribe(topic, qos);
    if (!result) {
        return result;
    }
    
    // 保存订阅信息
    Subscription sub;
    sub.topic = std::string(topic);
    sub.callback = callback;
    sub.qos = qos;
    sub.subscribedTime = std::time(nullptr);
    
    {
        std::unique_lock lock(mutex_);
        subscriptions_[sub.topic] = sub;
    }
    
    LOG_DEBUG(fmt::format("订阅成功: {} (QoS: {})", sub.topic, static_cast<int>(qos)));
    
    return Result<bool>::Success(true);
}

Result<bool> MqttSubscriptionManager::unsubscribe(std::string_view topic) {
    {
        std::shared_lock lock(mutex_);
        if (subscriptions_.find(std::string(topic)) == subscriptions_.end()) {
            LOG_DEBUG(fmt::format("主题未订阅: {}", topic));
            return Result<bool>::Success(true);
        }
    }

    // 如果已连接，通过适配器取消订阅
    if (connectionManager_.isConnected()) {
        auto* adapter = connectionManager_.getAdapter();
        if (adapter) {
            auto result = adapter->unsubscribe(topic);
            if (!result) {
                LOG_WARN(fmt::format("取消订阅失败: {}", topic));
                return result;
            }
        }
    }

    // 从缓存中移除
    {
        std::unique_lock lock(mutex_);
        subscriptions_.erase(std::string(topic));
    }
    
    LOG_DEBUG(fmt::format("取消订阅成功: {}", topic));
    
    return Result<bool>::Success(true);
}

void MqttSubscriptionManager::unsubscribeAll() {
    std::vector<std::string> topics;
    {
        std::shared_lock lock(mutex_);
        topics.reserve(subscriptions_.size());
        for (const auto& [topic, sub] : subscriptions_) {
            topics.push_back(topic);
        }
    }

    std::vector<std::string> succeeded;
    // 如果已连接，取消所有订阅
    if (connectionManager_.isConnected()) {
        auto* adapter = connectionManager_.getAdapter();
        if (adapter) {
            for (const auto& topic : topics) {
                if (auto result = adapter->unsubscribe(topic); result) {
                    succeeded.push_back(topic);
                } else {
                    LOG_WARN(fmt::format("取消订阅失败: {}", topic));
                }
            }
        }
    } else {
        succeeded = topics;
    }

    {
        std::unique_lock lock(mutex_);
        for (const auto& topic : succeeded) {
            subscriptions_.erase(topic);
        }
    }
    LOG_DEBUG("已取消所有订阅");
}

Result<bool> MqttSubscriptionManager::resubscribeAll() {
    if (!connectionManager_.isConnected()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法恢复订阅"));
    }
    
    auto* adapter = connectionManager_.getAdapter();
    if (!adapter) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "适配器未初始化"));
    }
    
    std::vector<std::pair<std::string, QoS>> subscriptions;
    {
        std::shared_lock lock(mutex_);
        subscriptions.reserve(subscriptions_.size());
        for (const auto& [topic, sub] : subscriptions_) {
            subscriptions.emplace_back(topic, sub.qos);
        }
    }

    bool allSucceeded = true;
    for (const auto& subscription : subscriptions) {
        const auto& topic = subscription.first;
        const auto qos = subscription.second;
        auto result = adapter->subscribe(topic, qos);
        if (!result) {
            LOG_WARN(fmt::format("恢复订阅失败: {}", topic));
            allSucceeded = false;
        } else {
            std::unique_lock lock(mutex_);
            if (auto it = subscriptions_.find(topic); it != subscriptions_.end()) {
                it->second.subscribedTime = std::time(nullptr);
            }
            LOG_DEBUG(fmt::format("恢复订阅成功: {}", topic));
        }
    }

    if (!allSucceeded) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::SUBSCRIBE_FAILED, "部分主题恢复订阅失败"));
    }
    return Result<bool>::Success(true);
}

void MqttSubscriptionManager::dispatchMessage(std::string_view topic,
                                             std::string_view payload,
                                             const MqttProperties& properties) {
    std::vector<MessageCallback> callbacks;
    {
        std::shared_lock lock(mutex_);

        for (const auto& [filter, sub] : subscriptions_) {
            if (topicMatches(filter, topic) && sub.callback) {
                callbacks.push_back(sub.callback);
            }
        }
    }

    for (const auto& callback : callbacks) {
        try {
            callback(topic, payload, properties);
        } catch (const std::exception& e) {
            LOG_ERROR(fmt::format("消息回调执行失败: {}", e.what()));
        } catch (...) {
            LOG_ERROR("消息回调执行失败: 未知异常");
        }
    }
}

bool MqttSubscriptionManager::isSubscribed(std::string_view topic) const {
    std::shared_lock lock(mutex_);  // 读操作，使用 shared_lock
    return subscriptions_.find(std::string(topic)) != subscriptions_.end();
}

std::vector<std::string> MqttSubscriptionManager::getSubscribedTopics() const {
    std::shared_lock lock(mutex_);
    
    std::vector<std::string> topics;
    topics.reserve(subscriptions_.size());
    
    for (const auto& [topic, sub] : subscriptions_) {
        topics.push_back(topic);
    }
    
    return topics;
}

size_t MqttSubscriptionManager::getSubscriptionCount() const {
    std::shared_lock lock(mutex_);
    return subscriptions_.size();
}

Result<bool> MqttSubscriptionManager::saveSubscription(std::string_view topic,
                                                      MessageCallback callback,
                                                      QoS qos) {
    std::unique_lock lock(mutex_);  // 写操作，使用 unique_lock
    
    Subscription sub;
    sub.topic = std::string(topic);
    sub.callback = callback;
    sub.qos = qos;
    sub.subscribedTime = std::time(nullptr);
    
    subscriptions_[sub.topic] = sub;
    
    return Result<bool>::Success(true);
}

bool MqttSubscriptionManager::topicMatches(std::string_view filter, std::string_view topic) const {
    // 如果过滤器就是主题，直接匹配
    if (filter == topic) {
        return true;
    }
    
    // 如果过滤器不包含通配符，不匹配
    if (filter.find('+') == std::string_view::npos && filter.find('#') == std::string_view::npos) {
        return false;
    }
    
    // 使用 string_view 逐级匹配而不分配临时字符串
    std::string_view filterView = filter;
    std::string_view topicView = topic;
    
    while (!filterView.empty() && !topicView.empty()) {
        auto filterPos = filterView.find('/');
        auto topicPos = topicView.find('/');
        
        std::string_view filterLevel = (filterPos == std::string_view::npos)
            ? filterView
            : filterView.substr(0, filterPos);
        std::string_view topicLevel = (topicPos == std::string_view::npos)
            ? topicView
            : topicView.substr(0, topicPos);
        
        if (filterLevel == "#") {
            // 多级通配符，匹配剩余所有级别
            return true;
        } else if (filterLevel == "+") {
            // 单级通配符，匹配当前级别，继续下一级
            if (filterPos == std::string_view::npos) {
                filterView = std::string_view{};
            } else {
                filterView.remove_prefix(filterPos + 1);
            }
            if (topicPos == std::string_view::npos) {
                topicView = std::string_view{};
            } else {
                topicView.remove_prefix(topicPos + 1);
            }
        } else if (filterLevel == topicLevel) {
            // 精确匹配，继续下一级
            if (filterPos == std::string_view::npos) {
                filterView = std::string_view{};
            } else {
                filterView.remove_prefix(filterPos + 1);
            }
            if (topicPos == std::string_view::npos) {
                topicView = std::string_view{};
            } else {
                topicView.remove_prefix(topicPos + 1);
            }
        } else {
            // 不匹配
            return false;
        }
    }
    
    // 处理剩余的过滤器和主题级别
    if (!filterView.empty()) {
        // 如果剩余的是单级通配符并且主题已经结束，匹配
        if (filterView == "+" && topicView.empty()) {
            return true;
        }
        return false;
    }
    
    // 如果主题还有剩余，不匹配（除非过滤器以#结尾，
    // 但这种情况在上面的循环中已被处理）
    return topicView.empty();
}

bool MqttSubscriptionManager::validateTopicFilter(std::string_view topic) const {
    // 检查空主题
    if (topic.empty()) {
        return false;
    }
    
    // 检查长度
    if (topic.length() > 65535) {
        return false;
    }
    
    // 检查多级通配符#的位置（必须在末尾）
    size_t hashPos = topic.find('#');
    if (hashPos != std::string::npos) {
        // #必须在末尾，且前面必须是/
        if (hashPos != topic.length() - 1) {
            return false;
        }
        if (hashPos > 0 && topic[hashPos - 1] != '/') {
            return false;
        }
    }
    
    // 检查单级通配符+的使用
    size_t plusPos = topic.find('+');
    while (plusPos != std::string::npos) {
        // +前后必须是/或字符串边界
        if (plusPos > 0 && topic[plusPos - 1] != '/') {
            return false;
        }
        if (plusPos < topic.length() - 1 && topic[plusPos + 1] != '/') {
            return false;
        }
        plusPos = topic.find('+', plusPos + 1);
    }
    
    return true;
}

} // namespace mqtt_client
