/**
 * @file idempotency_manager.cpp
 * @brief 幂等去重管理器实现
 */

#include "mqtt_client/persistence/idempotency_manager.h"
#include "mqtt_client/persistence/persistence_manager.h"
#include "mqtt_client/core/types.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/logger/logger_interface.h"
#include <fmt/core.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstdint>
#include <algorithm>

using namespace std::chrono_literals;

namespace {
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

    void hashBytes(std::uint64_t& hash, const void* data, const std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= kFnvPrime;
        }
    }

    void hashString(std::uint64_t& hash, const std::string& value) {
        const std::uint64_t size = value.size();
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            const auto byte = static_cast<std::uint8_t>(size >> shift);
            hashBytes(hash, &byte, sizeof(byte));
        }
        hashBytes(hash, value.data(), value.size());
    }
}

namespace mqtt_client {

IdempotencyManager::IdempotencyManager(
    const std::shared_ptr<PersistenceManager> &persistenceManager,
    const time_t retentionTime,
    const time_t cleanupInterval)
    : persistenceManager_(persistenceManager)
    , retentionTime_(std::max<time_t>(1, retentionTime))
    , cleanupInterval_(std::max<time_t>(1, cleanupInterval)) {
    
    if (!persistenceManager_) {
        LOG_WARN("持久化管理器为空，幂等去重功能可能受限");
    }
    
    // 启动清理线程
    running_.store(true);
    cleanupThread_ = std::thread(&IdempotencyManager::cleanupThread, this);
    
    // 从持久化恢复
    const auto result = recover();
    LOG_INFO(fmt::format("恢复幂等去重结果: {}", result.success));
}

IdempotencyManager::~IdempotencyManager() {
    // 停止清理线程
    running_.store(false);
    if (cleanupThread_.joinable()) {
        cleanupThread_.join();
    }
    
    // 持久化去重数据
    const auto result = persist();
    LOG_INFO(fmt::format("持久化幂等去重结果：{}", result.success));
}

bool IdempotencyManager::isDuplicate(const std::string& messageHash) {
    std::lock_guard lock(mutex_);

    const auto it = duplicateRecords_.find(messageHash);
    if (it == duplicateRecords_.end()) {
        return false;  // 新消息
    }
    
    // 检查是否过期
    if (const time_t now = std::time(nullptr); now - it->second > retentionTime_) {
        // 已过期，删除记录
        duplicateRecords_.erase(it);
        return false;  // 视为新消息
    }
    
    return true;  // 重复消息
}

Result<bool> IdempotencyManager::markProcessed(const std::string& messageHash) {
    std::lock_guard lock(mutex_);

    const time_t now = std::time(nullptr);
    duplicateRecords_[messageHash] = now;
    
    LOG_DEBUG(fmt::format("标记消息已处理: {}", messageHash));
    return Result<bool>::Success(true);
}

std::string IdempotencyManager::calculateMessageHash(const MqttMessage& message) {
    return calculateMessageHash(message.topic, message.payload, message.qos);
}

std::string IdempotencyManager::calculateMessageHash(const std::string& topic,
                                                     const std::string& payload,
                                                     QoS qos) {
    // std::hash不保证跨进程或跨标准库稳定，不能作为持久化键。
    // 使用两个不同种子的FNV-1a流，并对字段加长度前缀，生成稳定的128位键。
    std::uint64_t first = 14695981039346656037ULL;
    std::uint64_t second = 7809847782465536322ULL;
    hashString(first, topic);
    hashString(first, payload);
    hashString(second, payload);
    hashString(second, topic);
    const auto qosValue = static_cast<std::uint8_t>(qos);
    hashBytes(first, &qosValue, sizeof(qosValue));
    hashBytes(second, &qosValue, sizeof(qosValue));
    return fmt::format("{:016x}{:016x}", first, second);
}

size_t IdempotencyManager::getDuplicateCount() const {
    std::lock_guard lock(mutex_);
    return duplicateRecords_.size();
}

void IdempotencyManager::setRetentionTime(time_t retentionTime) {
    std::lock_guard lock(mutex_);
    retentionTime_ = std::max<time_t>(1, retentionTime);
}

time_t IdempotencyManager::getRetentionTime() const {
    std::lock_guard lock(mutex_);
    return retentionTime_;
}

Result<bool> IdempotencyManager::cleanup() {
    std::lock_guard lock(mutex_);
    cleanupExpired();
    return Result<bool>::Success(true);
}

Result<bool> IdempotencyManager::persist() {
    if (!persistenceManager_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "持久化管理器未初始化"));
    }
    
    std::lock_guard lock(mutex_);
    
    // 清理过期数据后再持久化
    cleanupExpired();
    
    return persistenceManager_->saveIdempotencyRecords(duplicateRecords_);
}

Result<bool> IdempotencyManager::recover() {
    if (!persistenceManager_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     "持久化管理器未初始化"));
    }
    
    auto result = persistenceManager_->loadIdempotencyRecords();
    if (!result.success) {
        LOG_WARN(fmt::format("恢复幂等去重记录失败: {}", result.error.message));
        return Result<bool>::Success(true);  // 恢复失败不影响启动
    }
    
    std::lock_guard lock(mutex_);
    
    // 清理过期数据
    time_t now = std::time(nullptr);
    for (auto it = result.value.begin(); it != result.value.end();) {
        if (now - it->second > retentionTime_) {
            it = result.value.erase(it);
        } else {
            ++it;
        }
    }
    
    duplicateRecords_ = std::move(result.value);
    
    LOG_INFO(fmt::format("恢复幂等去重记录: {} 条", duplicateRecords_.size()));
    return Result<bool>::Success(true);
}

void IdempotencyManager::clear() {
    std::lock_guard lock(mutex_);
    duplicateRecords_.clear();
    LOG_INFO("清空所有幂等去重记录");
}

void IdempotencyManager::cleanupThread() {
    // 使用短周期睡眠累积到 cleanupInterval_，保证析构时能快速退出
    while (running_.load()) {
        time_t slept = 0;
        while (running_.load() && slept < cleanupInterval_) {
            std::this_thread::sleep_for(1s);
            ++slept;
        }

        if (!running_.load()) {
            break;
        }

        // 清理过期数据
        {
            std::lock_guard lock(mutex_);
            cleanupExpired();
        }

        // 持久化（可选，避免频繁写入）
        // persist();
    }
}

void IdempotencyManager::cleanupExpired() {
    time_t now = std::time(nullptr);
    auto it = duplicateRecords_.begin();
    
    size_t removedCount = 0;
    while (it != duplicateRecords_.end()) {
        if (now - it->second > retentionTime_) {
            it = duplicateRecords_.erase(it);
            removedCount++;
        } else {
            ++it;
        }
    }
    
    if (removedCount > 0) {
        LOG_DEBUG(fmt::format("清理过期幂等去重记录: {} 条", removedCount));
    }
}

} // namespace mqtt_client
