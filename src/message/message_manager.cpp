/**
 * @file message_manager.cpp
 * @brief MQTT消息管理器实现
 */

#include "mqtt_client/message/message_manager.h"
#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/adapter/wolfmqtt_adapter.h"
#include "mqtt_client/logger/logger_interface.h"
#include <fmt/core.h>
#include <chrono>

using namespace std::chrono_literals;

namespace mqtt_client {

MqttMessageManager::MqttMessageManager(MqttConnectionManager& connectionManager,
                                       const MqttConfig& config)
    : connectionManager_(connectionManager)
    , config_(config)
    , running_(false)
{
    // 延迟启动消息处理线程，避免在构造时立即访问connectionManager_
    // 线程将在第一次调用publish()或queueMessage()时启动
    // 这样可以避免在对象构造时出现mutex问题
}

void MqttMessageManager::startThread() {
    // 使用原子操作和双重检查锁定模式，确保线程只启动一次
    if (bool expected = false; running_.compare_exchange_strong(expected, true)) {
        messageThread_ = std::thread(&MqttMessageManager::messageThread, this);
    }
}

MqttMessageManager::~MqttMessageManager() {
    // 停止消息处理线程
    running_.store(false);
    queueCV_.notify_all();
    
    if (messageThread_.joinable()) {
        messageThread_.join();
    }
}

Result<bool> MqttMessageManager::publish(const std::string& topic,
                                        const std::string& payload,
                                        QoS qos,
                                        bool retained,
                                        int priority) {
    // 确保线程已启动
    startThread();
    
    // 验证消息
    if (topic.empty() || topic.length() > 65535) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INVALID_TOPIC,
                     "主题无效: 空或过长"));
    }
    
    // 检查消息大小
    const size_t maxSize = config_.mqtt5.maximumPacketSize > 0
                     ? config_.mqtt5.maximumPacketSize
                     : 256 * 1024;  // 默认256KB
    if (payload.length() > maxSize) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::MESSAGE_TOO_LARGE,
                     fmt::format("消息过大: {} 字节", payload.length())));
    }
    
    // 创建消息对象
    QueuedMessage msg;
    msg.topic = topic;
    msg.payload = payload;
    msg.qos = qos;
    msg.retained = retained;
    msg.priority = priority;
    msg.timestamp = std::time(nullptr);
    
    // 检查连接状态
    if (!connectionManager_.isConnected()) {
        // 如果启用持久化，将消息加入队列
        if (config_.persistence.buffer.enableSendPersistence) {
            return queueMessage(topic, payload, qos, retained, priority);
        }
        
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法发布消息"));
    }
    
    // 如果启用批量处理，加入批量队列
    if (config_.performance.batch.enableBatchSend) {
        std::lock_guard lock(batchMutex_);
        batchQueue_.push_back(msg);
        
        // 检查是否达到批量大小
        if (batchQueue_.size() >= config_.performance.batch.maxBatchSize) {
            processBatch();
        }
        
        return Result<bool>::Success(true);
    }
    
    // 直接发送
    return sendMessage(msg);
}

Result<bool> MqttMessageManager::publishSync(const std::string& topic,
                                            const std::string& payload,
                                            const QoS qos,
                                            const bool retained,
                                            [[maybe_unused]] int timeoutMs) {
    // 同步发布：先发布，然后等待完成（简化实现）
    // 实际实现中可以使用future/promise机制
    return publish(topic, payload, qos, retained);
}

Result<bool> MqttMessageManager::queueMessage(const std::string& topic,
                                              const std::string& payload,
                                              const QoS qos,
                                              const bool retained,
                                              const int priority) {
    // 确保线程已启动
    startThread();
    
    QueuedMessage msg;
    msg.topic = topic;
    msg.payload = payload;
    msg.qos = qos;
    msg.retained = retained;
    msg.priority = priority;
    msg.timestamp = std::time(nullptr);
    std::lock_guard lock(queueMutex_);
    
    // 检查队列大小（工业级策略：明确的丢弃规则）
    if (queue_.size() >= config_.messageQueue.maxSendQueueSize) {
        // 1. 低优先级消息：直接丢弃新消息，保护队列中已有的更重要消息
        if (msg.priority < 5) {  // 低优先级消息阈值，可根据配置调整
            LOG_WARN(fmt::format("消息队列已满，丢弃低优先级消息: {}", msg.topic));
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::QUEUE_FULL,
                         "消息队列已满，已丢弃低优先级消息"));
        }

        // 2. 高优先级消息：尝试淘汰队列中优先级最低且最早进入队列的消息
        //    这样可以在队列上限内优先保留更重要的消息
        std::vector<QueuedMessage> buffer;
        buffer.reserve(queue_.size());

        // 将当前队列元素转移到临时缓冲区
        while (!queue_.empty()) {
            buffer.push_back(queue_.top());
            queue_.pop();
        }

        // 选择一个要淘汰的候选：优先级最低，其次时间戳最早
        auto dropIt = buffer.begin();
        for (auto it = buffer.begin(); it != buffer.end(); ++it) {
            if (it->priority < dropIt->priority ||
                (it->priority == dropIt->priority && it->timestamp < dropIt->timestamp)) {
                dropIt = it;
            }
        }

        const QueuedMessage dropped = *dropIt;

        // 记录被淘汰的消息 （便于运维排查）
        LOG_WARN(fmt::format("消息队列已满，淘汰低优先级消息: topic={}, priority={}; 保留新高优先级消息: topic={}, priority={}",
                             dropped.topic, dropped.priority, msg.topic, msg.priority));

        // 从缓冲区中移除被淘汰的消息
        buffer.erase(dropIt);

        // 重新构建优先级队列（不含被淘汰消息）
        for (const auto& q : buffer) {
            queue_.push(q);
        }
        // 注意：此时队列大小为 maxSendQueueSize - 1，为新消息留出了空间
    }
    
    // 确保时间戳已设置
    if (msg.timestamp == 0) {
        msg.timestamp = std::time(nullptr);
    }
    
    // 加入队列
    queue_.push(msg);
    
    // 更新统计
    {
        std::lock_guard statsLock(statsMutex_);
        stats_.totalQueued++;
    }
    
    // 通知处理线程
    queueCV_.notify_one();
    
    return Result<bool>::Success(true);
}

size_t MqttMessageManager::getQueueSize() const {
    // 使用try_to_lock避免在析构时阻塞
    const std::unique_lock lock(queueMutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        return queue_.size();
    }
    // 如果无法获取锁，返回0（避免阻塞）
    return 0;
}

void MqttMessageManager::clearQueue() {
    std::lock_guard lock(queueMutex_);
    
    // priority_queue没有clear方法，需要逐个pop
    while (!queue_.empty()) {
        queue_.pop();
    }
}

void MqttMessageManager::processQueue() {
    std::lock_guard lock(queueMutex_);
    
    // 处理队列中的所有消息
    while (!queue_.empty() && connectionManager_.isConnected()) {
        QueuedMessage msg = queue_.top();
        queue_.pop();
        
        // 发送消息
        if (const auto result = sendMessage(msg); !result) {
            // 发送失败，如果未超过重试次数，重新入队
            if (msg.retryCount < config_.messageQueue.maxRetry) {
                msg.retryCount++;
                queue_.push(msg);
                
                // 更新统计
                {
                    std::lock_guard statsLock(statsMutex_);
                    stats_.totalRetried++;
                }
            } else {
                // 超过重试次数，丢弃消息
                LOG_ERROR(fmt::format("消息重试次数超限，丢弃: {}", msg.topic));
                updateStats(false);
            }
        } else {
            updateStats(true);
        }
    }
}

MqttMessageManager::MessageStats MqttMessageManager::getStats() const {
    std::lock_guard lock(statsMutex_);
    
    MessageStats stats = stats_;
    
    // 计算成功率
    if (const uint64_t total = stats.totalPublished + stats.totalFailed; total > 0) {
        stats.successRate = static_cast<double>(stats.totalPublished) / static_cast<double>(total);
    }
    
    return stats;
}

void MqttMessageManager::resetStats() {
    std::lock_guard lock(statsMutex_);
    stats_ = MessageStats();
}

Result<bool> MqttMessageManager::sendMessage(const QueuedMessage& msg) {
    // 获取适配器（使用try-catch保护，避免访问已销毁的对象）
    try {
        auto* adapter = connectionManager_.getAdapter();
        if (!adapter) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::NOT_INITIALIZED,
                         "适配器未初始化"));
        }
        
        // 通过适配器发送消息
        auto result = adapter->publish(msg.topic, msg.payload, msg.qos, msg.retained);
        
        if (result) {
            updateStats(true);
        } else {
            updateStats(false);
        }
        
        return result;
    } catch (...) {
        // 如果connectionManager_已被销毁，返回失败
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "连接管理器已销毁"));
    }
}

void MqttMessageManager::processBatch() {
    std::lock_guard lock(batchMutex_);
    
    if (batchQueue_.empty()) {
        return;
    }
    
    // 批量发送消息
    for (const auto& msg : batchQueue_) {
        if (const auto result = sendMessage(msg); !result.success) {
            LOG_ERROR(fmt::format("批量消息中存在失败消息：{}", msg.messageId));
        }
    }
    
    // 清空批量队列
    batchQueue_.clear();
}

void MqttMessageManager::messageThread() {
    while (running_.load()) {
        std::unique_lock lock(queueMutex_);
        
        // 等待队列中有消息或停止信号
        queueCV_.wait(lock, [this] {
            return !queue_.empty() || !running_.load();
        });
        
        if (!running_.load()) {
            break;
        }
        
        // 处理队列中的消息
        if (!queue_.empty()) {
            // 检查连接状态（使用try-catch保护，避免访问已销毁的对象）
            bool isConnected = false;
            try {
                isConnected = connectionManager_.isConnected();
            } catch (...) {
                // 如果connectionManager_已被销毁，停止处理
                break;
            }
            
            if (isConnected) {
                QueuedMessage msg = queue_.top();
                queue_.pop();
                lock.unlock();
                
                // 发送消息
                if (const auto result = sendMessage(msg); !result) {
                    // 发送失败，如果未超过重试次数，重新入队
                    if (msg.retryCount < config_.messageQueue.maxRetry) {
                        msg.retryCount++;
                        if (auto sendResult = queueMessage(msg.topic, msg.payload, msg.qos, msg.retained, msg.priority); !sendResult.success) {
                            LOG_ERROR(fmt::format("消息重试入队失败，丢弃：{}", msg.messageId));
                        }
                    } else {
                        LOG_ERROR(fmt::format("消息重试次数超限，丢弃: {}", msg.topic));
                        updateStats(false);
                    }
                }
            } else {
                // 未连接，等待一段时间后重试
                lock.unlock();
                std::this_thread::sleep_for(100ms);
            }
        }
    }
}

void MqttMessageManager::updateStats(const bool success) {
    std::lock_guard lock(statsMutex_);
    
    if (success) {
        stats_.totalPublished++;
    } else {
        stats_.totalFailed++;
    }
}

} // namespace mqtt_client
