/**
 * @file message_manager.cpp
 * @brief MQTT消息管理器实现
 */

#include "mqtt_client/message/message_manager.h"
#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/adapter/wolfmqtt_adapter.h"
#include "mqtt_client/logger/logger_interface.h"
#include <algorithm>
#include <chrono>

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
    bool expected = false;
    if (running_.compare_exchange_strong(expected, true)) {
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
    size_t maxSize = config_.mqtt5.maximumPacketSize > 0
                     ? config_.mqtt5.maximumPacketSize
                     : 256 * 1024;  // 默认256KB
    if (payload.length() > maxSize) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::MESSAGE_TOO_LARGE,
                     "消息过大: " + std::to_string(payload.length()) + " 字节"));
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
                                            QoS qos,
                                            bool retained,
                                            int timeoutMs) {
    // 同步发布：先发布，然后等待完成（简化实现）
    // 实际实现中可以使用future/promise机制
    return publish(topic, payload, qos, retained);
}

Result<bool> MqttMessageManager::queueMessage(const std::string& topic,
                                              const std::string& payload,
                                              QoS qos,
                                              bool retained,
                                              int priority) {
    // 确保线程已启动
    startThread();
    
    QueuedMessage msg;
    msg.topic = topic;
    msg.payload = payload;
    msg.qos = qos;
    msg.retained = retained;
    msg.priority = priority;
    msg.timestamp = std::time(nullptr);
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    // 检查队列大小
    if (queue_.size() >= config_.messageQueue.maxSendQueueSize) {
        // 队列满，根据优先级决定是否丢弃
        if (msg.priority < 5) {  // 低优先级消息
            LOG_WARN("消息队列已满，丢弃低优先级消息: " + msg.topic);
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::QUEUE_FULL,
                         "消息队列已满"));
        } else {
            // 高优先级消息，丢弃队列中最旧的低优先级消息
            // 注意：priority_queue不支持直接遍历，这里简化处理
            // 实际实现中可以使用deque等容器
            LOG_WARN("消息队列已满，但保留高优先级消息: " + msg.topic);
        }
    }
    
    // 确保时间戳已设置
    if (msg.timestamp == 0) {
        msg.timestamp = std::time(nullptr);
    }
    
    // 加入队列
    queue_.push(msg);
    
    // 更新统计
    {
        std::lock_guard<std::mutex> statsLock(statsMutex_);
        stats_.totalQueued++;
    }
    
    // 通知处理线程
    queueCV_.notify_one();
    
    return Result<bool>::Success(true);
}

size_t MqttMessageManager::getQueueSize() const {
    // 使用try_to_lock避免在析构时阻塞
    std::unique_lock<std::mutex> lock(queueMutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        return queue_.size();
    }
    // 如果无法获取锁，返回0（避免阻塞）
    return 0;
}

void MqttMessageManager::clearQueue() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    // priority_queue没有clear方法，需要逐个pop
    while (!queue_.empty()) {
        queue_.pop();
    }
}

void MqttMessageManager::processQueue() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    // 处理队列中的所有消息
    while (!queue_.empty() && connectionManager_.isConnected()) {
        QueuedMessage msg = queue_.top();
        queue_.pop();
        
        // 发送消息
        auto result = sendMessage(msg);
        if (!result) {
            // 发送失败，如果未超过重试次数，重新入队
            if (msg.retryCount < config_.messageQueue.maxRetry) {
                msg.retryCount++;
                queue_.push(msg);
                
                // 更新统计
                {
                    std::lock_guard<std::mutex> statsLock(statsMutex_);
                    stats_.totalRetried++;
                }
            } else {
                // 超过重试次数，丢弃消息
                LOG_ERROR("消息重试次数超限，丢弃: " + msg.topic);
                updateStats(false);
            }
        } else {
            updateStats(true);
        }
    }
}

MqttMessageManager::MessageStats MqttMessageManager::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    MessageStats stats = stats_;
    
    // 计算成功率
    uint64_t total = stats.totalPublished + stats.totalFailed;
    if (total > 0) {
        stats.successRate = static_cast<double>(stats.totalPublished) / total;
    }
    
    return stats;
}

void MqttMessageManager::resetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
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
    std::lock_guard<std::mutex> lock(batchMutex_);
    
    if (batchQueue_.empty()) {
        return;
    }
    
    // 批量发送消息
    for (const auto& msg : batchQueue_) {
        sendMessage(msg);
    }
    
    // 清空批量队列
    batchQueue_.clear();
}

void MqttMessageManager::messageThread() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        
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
                auto result = sendMessage(msg);
                if (!result) {
                    // 发送失败，如果未超过重试次数，重新入队
                    if (msg.retryCount < config_.messageQueue.maxRetry) {
                        msg.retryCount++;
                        queueMessage(msg.topic, msg.payload, msg.qos, msg.retained, msg.priority);
                    } else {
                        LOG_ERROR("消息重试次数超限，丢弃: " + msg.topic);
                        updateStats(false);
                    }
                }
            } else {
                // 未连接，等待一段时间后重试
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

void MqttMessageManager::updateStats(bool success) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    if (success) {
        stats_.totalPublished++;
    } else {
        stats_.totalFailed++;
    }
}

} // namespace mqtt_client
