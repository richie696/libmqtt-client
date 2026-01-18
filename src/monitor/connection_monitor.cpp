/**
 * @file connection_monitor.cpp
 * @brief 连接监控器实现
 */

#include "mqtt_client/monitor/connection_monitor.h"
#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/logger/logger_interface.h"
#include <chrono>
#include <algorithm>

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
    // 先停止运行标志，让线程退出
    running_.store(false);
    
    // 等待线程退出（不获取mutex，避免死锁）
    // 注意：如果线程已经在运行，它会自然退出
    if (monitorThread_.joinable()) {
        try {
            monitorThread_.join();
        } catch (...) {
            // 忽略join异常，确保析构能完成
        }
    }
}

void ConnectionMonitor::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    lastConnectedState_ = connectionManager_.isConnected();
    if (lastConnectedState_) {
        lastConnectTime_ = std::time(nullptr);
    }
    
    monitorThread_ = std::thread(&ConnectionMonitor::monitorThread, this);
}

void ConnectionMonitor::stop() {
    // 先设置停止标志，让线程自然退出
    if (!running_.exchange(false)) {
        return;  // 已经停止
    }
    
    // 等待线程退出（不获取mutex，避免死锁）
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

bool ConnectionMonitor::isRunning() const {
    return running_.load();
}

ConnectionMonitor::ConnectionStats ConnectionMonitor::getStats() const {
    // 使用try_lock避免在析构时死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        return stats_;
    }
    // 无法获取锁，返回默认值
    ConnectionStats defaultStats;
    return defaultStats;
}

void ConnectionMonitor::setOnDisconnected(std::function<void()> callback) {
    // 使用try_lock避免在析构时死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        onDisconnected_ = callback;
    }
    // 如果无法获取锁，跳过设置（避免死锁）
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
        
        // 使用try_lock避免在析构时死锁
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (lock.owns_lock()) {
            // 检测连接状态变化
            bool wasConnected = lastConnectedState_;
            if (wasConnected && !isConnected) {
                // 连接断开
                updateStatsUnlocked(false);
                
                LOG_WARN("检测到连接断开");
                if (onDisconnected_) {
                    try {
                        onDisconnected_();
                    } catch (const std::exception& e) {
                        LOG_ERROR("断开连接回调执行失败: " + std::string(e.what()));
                    }
                }
            } else if (!wasConnected && isConnected) {
                // 连接恢复
                updateStatsUnlocked(true);
                lastConnectTime_ = std::time(nullptr);
                LOG_INFO("检测到连接恢复");
            } else if (isConnected) {
                // 连接正常，更新在线时长
                updateStatsUnlocked(true);
            }
            
            lastConnectedState_ = isConnected;
        }
        
        // 等待下次检查（使用可中断的sleep，避免长时间阻塞）
        for (int i = 0; i < checkInterval_ * 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
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

void ConnectionMonitor::updateStats(bool connected) {
    // 使用try_lock避免在析构时死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        updateStatsUnlocked(connected);
    }
    // 如果无法获取锁，跳过更新（避免死锁）
}

} // namespace mqtt_client
