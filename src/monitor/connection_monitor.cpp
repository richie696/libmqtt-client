/**
 * @file connection_monitor.cpp
 * @brief 连接监控器实现
 */

#include "mqtt_client/monitor/connection_monitor.h"
#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/logger/logger_interface.h"
#include <chrono>
#include <algorithm>

using namespace std::chrono_literals;

namespace mqtt_client {

ConnectionMonitor::ConnectionMonitor(MqttConnectionManager& connectionManager,
                                    int checkInterval)
    : connectionManager_(connectionManager)
    , checkInterval_(checkInterval)
    , running_(false)
    , lastConnectedState_(false)
    , lastConnectTime_(0)
{
}

ConnectionMonitor::~ConnectionMonitor() {
    stop();
}

void ConnectionMonitor::start() {
    if (running_.load()) {
        return;
    }

    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }

    running_.store(true);
    const bool connected = connectionManager_.isConnected();
    {
        const std::lock_guard lock(mutex_);
        lastConnectedState_.store(connected);
        if (connected) {
            lastConnectTime_ = std::time(nullptr);
        }
    }
    
    monitorThread_ = std::thread(&ConnectionMonitor::monitorThread, this);
}

void ConnectionMonitor::stop() {
    running_.store(false);
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

bool ConnectionMonitor::isRunning() const {
    return running_.load();
}

ConnectionMonitor::ConnectionStats ConnectionMonitor::getStats() const {
    const std::lock_guard lock(mutex_);
    return stats_;
}

void ConnectionMonitor::setOnDisconnected(std::function<void()> callback) {
    const std::lock_guard lock(mutex_);
    onDisconnected_ = std::move(callback);
}

void ConnectionMonitor::monitorThread() {
    while (running_.load()) {
        // 使用try-catch保护，避免connectionManager_被析构时出错
        bool isConnected = false;
        try {
            isConnected = connectionManager_.isConnected();
        } catch (...) {
            // connectionManager_可能已被析构，退出线程
            break;
        }
        
        std::function<void()> disconnectedCallback;
        {
            const std::lock_guard lock(mutex_);
            // 检测连接状态变化
            const bool wasConnected = lastConnectedState_.load();
            if (wasConnected && !isConnected) {
                // 连接断开
                updateStatsUnlocked(false);
                
                LOG_WARN("检测到连接断开");
                disconnectedCallback = onDisconnected_;
            } else if (!wasConnected && isConnected) {
                // 连接恢复
                updateStatsUnlocked(true);
                lastConnectTime_ = std::time(nullptr);
                LOG_INFO("检测到连接恢复");
            } else if (isConnected) {
                // 连接正常，更新在线时长
                updateStatsUnlocked(true);
            }
            
            lastConnectedState_.store(isConnected);
        }

        if (disconnectedCallback) {
            try {
                disconnectedCallback();
            } catch (const std::exception& e) {
                LOG_ERROR("断开连接回调执行失败: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("断开连接回调执行失败: 未知异常");
            }
        }
        
        // 等待下次检查（使用可中断的sleep，避免长时间阻塞）
        // 使用 C++17 chrono duration 表达时间间隔，更语义化
        const auto checkDuration = std::chrono::seconds(checkInterval_);
        const auto sleepInterval = 100ms;
        auto elapsed = 0ms;
        while (elapsed < checkDuration && running_.load()) {
            std::this_thread::sleep_for(sleepInterval);
            elapsed += sleepInterval;
        }
    }

    running_.store(false);
}

void ConnectionMonitor::updateStatsUnlocked(bool connected) {
    // 注意：调用者必须已经持有mutex
    if (!connected) {
        // 连接断开
        stats_.totalDisconnects++;
        stats_.abnormalDisconnects++;  // 简化实现，所有断开都视为异常
        stats_.lastDisconnectTime = std::time(nullptr);
        
        // 计算平均在线时长
        if (lastConnectTime_ > 0) {
            time_t uptime = std::time(nullptr) - lastConnectTime_;
            if (stats_.averageUptime == 0) {
                stats_.averageUptime = uptime;
            } else {
                // 简单平均（实际可以使用更复杂的算法）
                stats_.averageUptime = (stats_.averageUptime + uptime) / 2;
            }
        }
        
        // 计算稳定性（简化实现）
        // 稳定性 = 1.0 - (异常断开次数 / 总断开次数)
        if (stats_.totalDisconnects > 0) {
            stats_.stability = 1.0 - (static_cast<double>(stats_.abnormalDisconnects) / stats_.totalDisconnects);
        }
    } else {
        // 连接正常
        if (lastConnectTime_ == 0) {
            lastConnectTime_ = std::time(nullptr);
        }
    }
}

} // namespace mqtt_client
