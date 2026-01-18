/**
 * @file network_monitor.cpp
 * @brief 网络监控器实现
 */

#include "mqtt_client/monitor/network_monitor.h"
#include "mqtt_client/logger/logger_interface.h"
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    #include <cstring>
#endif

namespace mqtt_client {

NetworkMonitor::NetworkMonitor(const std::string& host, int port, int checkInterval)
    : host_(host)
    , port_(port)
    , checkInterval_(checkInterval)
    , running_(false)
    , networkAvailable_(false)
    , lastQuality_(NetworkQuality::FAIR)
{
}

NetworkMonitor::~NetworkMonitor() {
    // 先停止运行标志，让线程退出
    running_.store(false);
    
    // 等待线程退出（不获取mutex，避免死锁）
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

void NetworkMonitor::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    monitorThread_ = std::thread(&NetworkMonitor::monitorThread, this);
}

void NetworkMonitor::stop() {
    // 先设置停止标志，让线程自然退出
    if (!running_.exchange(false)) {
        return;  // 已经停止
    }
    
    // 等待线程退出（不获取mutex，避免死锁）
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
}

bool NetworkMonitor::isRunning() const {
    return running_.load();
}

bool NetworkMonitor::isNetworkAvailable() const {
    return networkAvailable_.load();
}

NetworkMonitor::NetworkStats NetworkMonitor::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

NetworkQuality NetworkMonitor::getQuality() const {
    // 使用try_lock避免在析构时死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        return calculateQuality(stats_);
    }
    // 无法获取锁，返回默认值
    return NetworkQuality::POOR;
}

void NetworkMonitor::setOnNetworkRecovered(std::function<void()> callback) {
    // 使用try_lock避免在析构时死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        onNetworkRecovered_ = callback;
    }
    // 如果无法获取锁，跳过设置（避免死锁）
}

void NetworkMonitor::setOnNetworkLost(std::function<void()> callback) {
    // 使用try_lock避免在析构时死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        onNetworkLost_ = callback;
    }
    // 如果无法获取锁，跳过设置（避免死锁）
}

void NetworkMonitor::setOnQualityChanged(std::function<void(NetworkQuality)> callback) {
    // 使用try_lock避免在析构时死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        onQualityChanged_ = callback;
    }
    // 如果无法获取锁，跳过设置（避免死锁）
}

void NetworkMonitor::monitorThread() {
    while (running_.load()) {
        bool wasAvailable = networkAvailable_.load();
        bool isAvailable = checkNetworkConnectivity();
        
        networkAvailable_.store(isAvailable);
        
        // 更新统计信息
        long latency = measureLatency();
        updateStats(isAvailable, latency);
        
        // 使用try_lock避免死锁（在析构时可能无法获取锁）
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (lock.owns_lock()) {
            // 检测网络状态变化
            if (!wasAvailable && isAvailable) {
                // 网络恢复
                LOG_INFO("网络已恢复");
                if (onNetworkRecovered_) {
                    try {
                        onNetworkRecovered_();
                    } catch (const std::exception& e) {
                        LOG_ERROR("网络恢复回调执行失败: " + std::string(e.what()));
                    }
                }
            } else if (wasAvailable && !isAvailable) {
                // 网络丢失
                LOG_WARN("网络已丢失");
                if (onNetworkLost_) {
                    try {
                        onNetworkLost_();
                    } catch (const std::exception& e) {
                        LOG_ERROR("网络丢失回调执行失败: " + std::string(e.what()));
                    }
                }
            }
            
            // 检测网络质量变化
            NetworkQuality currentQuality = calculateQuality(stats_);
            if (currentQuality != lastQuality_) {
                lastQuality_ = currentQuality;
                if (onQualityChanged_) {
                    try {
                        onQualityChanged_(currentQuality);
                    } catch (const std::exception& e) {
                        LOG_ERROR("网络质量变化回调执行失败: " + std::string(e.what()));
                    }
                }
            }
        }
        
        // 等待下次检查（使用可中断的sleep，避免长时间阻塞）
        for (int i = 0; i < checkInterval_ * 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool NetworkMonitor::checkNetworkConnectivity() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup失败");
        return false;
    }
#endif
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("创建Socket失败");
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    
    // 设置超时
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    
    // 连接测试
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port_);
    
    if (inet_pton(AF_INET, host_.c_str(), &server.sin_addr) <= 0) {
        LOG_ERROR("无效的服务器地址: " + host_);
        close(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    
    auto startTime = std::chrono::steady_clock::now();
    int result = connect(sock, (struct sockaddr*)&server, sizeof(server));
    auto endTime = std::chrono::steady_clock::now();
    
    close(sock);
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    if (result == 0) {
        long latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        updateStats(true, latency);
        return true;
    } else {
        updateStats(false, -1);
        return false;
    }
}

long NetworkMonitor::measureLatency() {
    // 通过checkNetworkConnectivity测量延迟
    // 这里简化实现，直接返回统计信息中的延迟
    // 使用try_lock避免在析构时死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        return stats_.latency;
    }
    return -1;  // 无法获取锁，返回默认值
}

void NetworkMonitor::updateStats(bool available, long latency) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    stats_.available = available;
    stats_.lastCheckTime = std::time(nullptr);
    
    if (available) {
        stats_.latency = latency;
        stats_.consecutiveFailures = 0;
    } else {
        stats_.consecutiveFailures++;
    }
    
    // 简化实现：丢包率计算需要多次检测，这里暂时不实现
    // stats_.packetLoss = ...;
}

NetworkQuality NetworkMonitor::calculateQuality(const NetworkStats& stats) const {
    if (!stats.available) {
        return NetworkQuality::POOR;  // 网络不可用时返回POOR
    }
    
    if (stats.latency < 0) {
        return NetworkQuality::FAIR;  // 延迟未知时返回FAIR
    }
    
    // 根据延迟评估网络质量
    if (stats.latency < 50) {
        return NetworkQuality::EXCELLENT;
    } else if (stats.latency < 100) {
        return NetworkQuality::GOOD;
    } else if (stats.latency < 200) {
        return NetworkQuality::FAIR;
    } else {
        return NetworkQuality::POOR;
    }
}

} // namespace mqtt_client
