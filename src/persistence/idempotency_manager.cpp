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

using namespace std::chrono_literals;

// 使用标准库的简单 SHA-256 实现
// 注意：这是简化实现，仅用于测试。生产环境建议使用专业的加密库（如 OpenSSL）
#include <functional>

namespace {
    // 简单的 SHA-256 实现（仅用于测试）
    // 生产环境应该使用专业的加密库（如 OpenSSL）
    void simple_sha256(const unsigned char* data, size_t len, unsigned char* hash) {
        // 使用 std::hash 作为占位实现
        constexpr std::hash<std::string> hasher;
        const size_t hash_value = hasher(std::string(reinterpret_cast<const char*>(data), len));
        std::memcpy(hash, &hash_value, std::min(sizeof(hash_value), size_t(32)));
        // 填充剩余部分
        if (sizeof(hash_value) < 32) {
            std::memset(hash + sizeof(hash_value), 0, 32 - sizeof(hash_value));
        }
    }
}

namespace mqtt_client {

IdempotencyManager::IdempotencyManager(
    const std::shared_ptr<PersistenceManager> &persistenceManager,
    const time_t retentionTime,
    const time_t cleanupInterval)
    : persistenceManager_(persistenceManager)
    , retentionTime_(retentionTime)
    , cleanupInterval_(cleanupInterval) {
    
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
    // 组合消息特征
    std::string combined = fmt::format("{}|{}|{}", topic, payload, static_cast<int>(qos));
    
    // 使用简单的 hash 实现（仅用于测试）
    // 注意：生产环境建议使用专业的加密库（如 OpenSSL）进行 SHA-256 计算
    unsigned char hash[32];
    simple_sha256(reinterpret_cast<const unsigned char*>(combined.c_str()), 
                  combined.length(), hash);
    
    // 转换为十六进制字符串（使用 fmt::format）
    std::string result;
    result.reserve(64);  // SHA-256 输出 32 字节 = 64 个十六进制字符
    for (const unsigned char byte : hash) {
        result += fmt::format("{:02x}", byte);
    }
    
    return result;
}

size_t IdempotencyManager::getDuplicateCount() const {
    std::lock_guard lock(mutex_);
    return duplicateRecords_.size();
}

void IdempotencyManager::setRetentionTime(time_t retentionTime) {
    std::lock_guard lock(mutex_);
    retentionTime_ = retentionTime;
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
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds{cleanupInterval_});
        
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
