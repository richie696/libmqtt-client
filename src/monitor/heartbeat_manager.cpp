/**
 * @file heartbeat_manager.cpp
 * @brief 心跳管理器实现
 */

#include "mqtt_client/monitor/heartbeat_manager.h"
#include "mqtt_client/message/message_manager.h"
#include "mqtt_client/logger/logger_interface.h"
#include <fmt/core.h>
#include <chrono>
#include <ctime>

using namespace std::chrono_literals;

namespace mqtt_client {

HeartbeatManager::HeartbeatManager(MqttMessageManager& messageManager,
                                  int interval,
                                  const std::string& topic)
    : messageManager_(messageManager)
    , interval_(interval)
    , topic_(topic.empty() ? "heartbeat" : topic)
    , running_(false)
{
}

HeartbeatManager::~HeartbeatManager() {
    stop();
}

void HeartbeatManager::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    heartbeatThread_ = std::thread(&HeartbeatManager::heartbeatThread, this);
}

void HeartbeatManager::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
}

bool HeartbeatManager::isRunning() const {
    return running_.load();
}

HeartbeatManager::HeartbeatStats HeartbeatManager::getStats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

bool HeartbeatManager::sendHeartbeat() {
    // 生成心跳消息（包含时间戳）
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    // 格式化时间戳（使用 fmt::format）
    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&timeT));
    std::string payload = fmt::format("{}.{:03d}", timeStr, ms.count());
    
    auto startTime = std::chrono::steady_clock::now();
    
    // 发送心跳消息（QoS 0，不保留）
    auto result = messageManager_.publish(topic_, payload, QoS::QOS_0, false);
    
    auto endTime = std::chrono::steady_clock::now();
    long latency = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();
    
    bool success = static_cast<bool>(result);
    updateStats(success, latency);
    
    if (success) {
        LOG_DEBUG("心跳发送成功: " + topic_);
    } else {
        LOG_WARN("心跳发送失败: " + topic_);
    }
    
    return success;
}

void HeartbeatManager::heartbeatThread() {
    while (running_.load()) {
        sendHeartbeat();
        
        // 等待下次心跳
        std::this_thread::sleep_for(std::chrono::seconds{interval_});
    }
}

void HeartbeatManager::updateStats(bool success, long latency) {
    std::lock_guard lock(mutex_);
    
    if (success) {
        stats_.totalSent++;
        stats_.lastSentTime = std::time(nullptr);
        
        // 更新延迟统计
        if (stats_.averageLatency == 0) {
            stats_.averageLatency = latency;
        } else {
            // 简单移动平均
            stats_.averageLatency = (stats_.averageLatency + latency) / 2;
        }
        
        if (latency > stats_.maxLatency) {
            stats_.maxLatency = latency;
        }
    } else {
        stats_.totalFailed++;
    }
}

} // namespace mqtt_client
