/**
 * @file reconnect_manager.cpp
 * @brief 重连管理器实现
 */

#include "mqtt_client/reconnect/reconnect_manager.h"
#include "mqtt_client/logger/logger_interface.h"
#include <algorithm>
#include <chrono>
#include <random>

using namespace std::chrono_literals;

namespace mqtt_client {

ReconnectManager::ReconnectManager(const MqttConfig::ReconnectConfig& config)
    : config_(config)
    , attemptCount_(0)
    , reconnecting_(false)
    , shouldStop_(false)
    , totalRetryTime_(0)
{
}

ReconnectManager::~ReconnectManager() {
    stopReconnect();
}

bool ReconnectManager::startReconnect(const std::function<bool()>& connectFunc) {
    std::lock_guard threadLock(threadMutex_);
    std::unique_lock lock(mutex_);
    if (reconnecting_.load()) {
        LOG_DEBUG("重连已在进行中");
        return false;
    }

    // 上一轮线程即使已经自然结束，std::thread 仍然是 joinable。
    // 必须先回收，之后才能给 reconnectThread_ 重新赋值。
    if (reconnectThread_.joinable()) {
        lock.unlock();
        reconnectThread_.join();
        lock.lock();
    }
    
    shouldStop_.store(false);
    reconnecting_.store(true);
    
    // 启动重连线程
    reconnectThread_ = std::thread(&ReconnectManager::reconnectThread, this, connectFunc);
    
    return true;
}

void ReconnectManager::stopReconnect() {
    std::lock_guard threadLock(threadMutex_);
    {
        std::lock_guard lock(mutex_);
        shouldStop_.store(true);
        reconnecting_.store(false);
    }
    
    // 通知重连线程
    cv_.notify_all();
    
    // 等待线程结束
    if (reconnectThread_.joinable() &&
        reconnectThread_.get_id() != std::this_thread::get_id()) {
        reconnectThread_.join();
    }
}

bool ReconnectManager::isReconnecting() const {
    return reconnecting_.load();
}

int ReconnectManager::getAttemptCount() const {
    return attemptCount_.load();
}

long ReconnectManager::getTotalRetryTime() const {
    return totalRetryTime_.load();
}

long ReconnectManager::getNextRetryInterval() const {
    int attempt = attemptCount_.load();
    return calculateBackoffInterval(attempt);
}

void ReconnectManager::updateConfig(const MqttConfig::ReconnectConfig& newConfig) {
    std::lock_guard lock(mutex_);
    config_ = newConfig;
}

void ReconnectManager::resetAttempts() {
    std::lock_guard lock(mutex_);
    attemptCount_.store(0);
    totalRetryTime_.store(0);
}

void ReconnectManager::setOnReconnectAttempt(const std::function<void(int, int, long)> &callback) {
    std::lock_guard lock(mutex_);
    onReconnectAttempt_ = callback;
}

long ReconnectManager::calculateBackoffInterval(int attempt) const {
    MqttConfig::ReconnectConfig config;
    {
        std::lock_guard lock(mutex_);
        config = config_;
    }
    long interval = config.baseInterval;
    
    if (config.enableExponentialBackoff) {
        // 指数退避: baseInterval * 2^attempt
        // 限制attempt最大值，避免溢出
        int safeAttempt = std::min(attempt, 10);
        interval = config.baseInterval * (1L << safeAttempt);
        
        // 限制最大间隔
        interval = std::min(interval, config.maxInterval);
    }
    
    // 添加随机抖动: interval * (minJitter ~ maxJitter)
    if (config.enableJitter) {
        std::lock_guard lock(randomMutex_);
        std::mt19937 gen(randomDevice_());
        std::uniform_real_distribution dis(config.minJitter, config.maxJitter);
        const double jitterFactor = dis(gen);
        
        interval = static_cast<long>(static_cast<double>(interval) * jitterFactor);
    }
    
    LOG_DEBUG("计算退避间隔: attempt=" + std::to_string(attempt) +
             ", base=" + std::to_string(config.baseInterval) + "ms" +
             ", final=" + std::to_string(interval) + "ms");
    
    return interval;
}

void ReconnectManager::reconnectThread(const std::function<bool()>& connectFunc) {
    attemptCount_.store(0);
    
    while (!shouldStop_.load()) {
        int attempt = attemptCount_.load();
        MqttConfig::ReconnectConfig config;
        std::function<void(int, int, long)> attemptCallback;
        {
            std::lock_guard lock(mutex_);
            config = config_;
            attemptCallback = onReconnectAttempt_;
        }
        
        // 检查最大尝试次数
        if (config.maxAttempts > 0 && attempt >= config.maxAttempts) {
            LOG_WARN("达到最大重连次数: " + std::to_string(config.maxAttempts));
            reconnecting_.store(false);
            break;
        }
        
        // 如果不是第一次尝试，等待退避间隔
        if (attempt > 0) {
            long interval = calculateBackoffInterval(attempt);
            totalRetryTime_.fetch_add(interval);
            
            // 调用重连回调
            if (attemptCallback) {
                try {
                    attemptCallback(attempt, config.maxAttempts, interval);
                } catch (const std::exception& e) {
                    LOG_ERROR("重连回调执行失败: " + std::string(e.what()));
                }
            }
            
            // 等待退避间隔（可被中断）
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, interval * 1ms, [this] {
                return shouldStop_.load();
            });
            
            if (shouldStop_.load()) {
                break;
            }
        }
        
        // 尝试连接
        LOG_DEBUG("尝试重连: 第 " + std::to_string(attempt + 1) + " 次");
        
        bool success = false;
        try {
            success = connectFunc();
        } catch (const std::exception& e) {
            LOG_ERROR("连接函数执行失败: " + std::string(e.what()));
            success = false;
        }
        
        if (success) {
            // 连接成功
            LOG_INFO("重连成功: 尝试 " + std::to_string(attempt + 1) + " 次");
            reconnecting_.store(false);
            attemptCount_.store(0);
            totalRetryTime_.store(0);
            break;
        } else {
            // 连接失败，增加尝试次数
            attemptCount_.fetch_add(1);
            LOG_WARN("重连失败: 第 " + std::to_string(attempt + 1) + " 次尝试");
        }
    }
    
    reconnecting_.store(false);
}

} // namespace mqtt_client
