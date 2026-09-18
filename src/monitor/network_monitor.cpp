/**
 * @file network_monitor.cpp
 * @brief 网络监控器实现
 */

#include "internal/monitor/network_monitor.h"
#include "mqtt_client/logger/logger_interface.h"
#include <chrono>
#include <utility>
#include <algorithm>
#include <numeric>
#include <deque>
#include <fmt/core.h>

using namespace std::chrono_literals;

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <cstring>
#endif

namespace mqtt_client {

NetworkMonitor::NetworkMonitor(std::string host,
                               const int port,
                               const int checkInterval,
                               const int networkTimeout)
    : host_(std::move(host))
    , port_(port)
    , checkInterval_(checkInterval)
    , networkTimeout_(networkTimeout)
    , running_(false)
    , networkAvailable_(false)
    , lastQuality_(NetworkQuality::FAIR) {
}

NetworkMonitor::~NetworkMonitor() {
    stop();
}

void NetworkMonitor::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    monitorThread_ = std::thread(&NetworkMonitor::monitorThread, this);
}

void NetworkMonitor::stop() {
    running_.store(false);
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
    std::lock_guard lock(mutex_);
    return stats_;
}

NetworkQuality NetworkMonitor::getQuality() const {
    const std::lock_guard lock(mutex_);
    return calculateQuality(stats_);
}

void NetworkMonitor::setOnNetworkRecovered(const std::function<void()> &callback) {
    const std::lock_guard lock(mutex_);
    onNetworkRecovered_ = callback;
}

void NetworkMonitor::setOnNetworkLost(const std::function<void()> &callback) {
    const std::lock_guard lock(mutex_);
    onNetworkLost_ = callback;
}

void NetworkMonitor::setOnQualityChanged(const std::function<void(NetworkQuality)> &callback) {
    const std::lock_guard lock(mutex_);
    onQualityChanged_ = callback;
}

void NetworkMonitor::monitorThread() {
    while (running_.load()) {
        const bool wasAvailable = networkAvailable_.load();
        
        // 检查网络连通性并测量延迟（checkNetworkConnectivity 内部会调用 updateStats）
        const bool isAvailable = checkNetworkConnectivity();
        
        networkAvailable_.store(isAvailable);
        
        std::function<void()> stateCallback;
        std::function<void(NetworkQuality)> qualityCallback;
        NetworkQuality currentQuality = lastQuality_;
        {
            const std::lock_guard lock(mutex_);
            // 检测网络状态变化
            if (!wasAvailable && isAvailable) {
                LOG_INFO("网络已恢复");
                stateCallback = onNetworkRecovered_;
            } else if (wasAvailable && !isAvailable) {
                LOG_WARN("网络已丢失");
                stateCallback = onNetworkLost_;
            }
            
            // 检测网络质量变化
            currentQuality = calculateQuality(stats_);
            if (currentQuality != lastQuality_) {
                lastQuality_ = currentQuality;
                qualityCallback = onQualityChanged_;
            }
        }

        if (stateCallback) {
            try {
                stateCallback();
            } catch (const std::exception& e) {
                LOG_ERROR(fmt::format("网络状态回调执行失败: {}", e.what()));
            } catch (...) {
                LOG_ERROR("网络状态回调执行失败: 未知异常");
            }
        }
        if (qualityCallback) {
            try {
                qualityCallback(currentQuality);
            } catch (const std::exception& e) {
                LOG_ERROR(fmt::format("网络质量回调执行失败: {}", e.what()));
            } catch (...) {
                LOG_ERROR("网络质量回调执行失败: 未知异常");
            }
        }
        
        // 等待下次检查（使用可中断的sleep，避免长时间阻塞）
        for (int i = 0; i < checkInterval_ * 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(100ms);
        }
    }
}

bool NetworkMonitor::checkNetworkConnectivity() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup失败");
        updateStats(false, -1);
        return false;
    }
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const std::string service = std::to_string(port_);
    const int rc = getaddrinfo(host_.c_str(), service.c_str(), &hints, &results);
    if (rc != 0 || results == nullptr) {
#ifdef _WIN32
        const char* dnsError = gai_strerrorA(rc);
#else
        const char* dnsError = gai_strerror(rc);
#endif
        LOG_ERROR(fmt::format("DNS解析失败: {} ({})", host_, dnsError));
#ifdef _WIN32
        WSACleanup();
#endif
        updateStats(false, -1);
        return false;
    }

    bool available = false;
    long measuredLatency = -1;
    for (addrinfo* address = results; address != nullptr; address = address->ai_next) {
#ifdef _WIN32
        const SOCKET sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (sock == INVALID_SOCKET) {
            continue;
        }
#else
        const int sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (sock < 0) {
            continue;
        }
#endif

        timeval timeout{};
        timeout.tv_sec = std::max(1, networkTimeout_);
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));

        const auto startTime = std::chrono::steady_clock::now();
#ifdef _WIN32
        const int connectResult = ::connect(
            sock, address->ai_addr, static_cast<int>(address->ai_addrlen));
#else
        const int connectResult = ::connect(
            sock, address->ai_addr, static_cast<socklen_t>(address->ai_addrlen));
#endif
        const auto endTime = std::chrono::steady_clock::now();
        close(sock);

        if (connectResult == 0) {
            measuredLatency = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime).count();
            available = true;
            break;
        }
    }

    freeaddrinfo(results);
#ifdef _WIN32
    WSACleanup();
#endif

    updateStats(available, measuredLatency);
    return available;
}

long NetworkMonitor::measureLatency() const {
    // 直接返回统计信息中的延迟（延迟在 checkNetworkConnectivity 中已测量并更新）
    const std::lock_guard lock(mutex_);
    return stats_.latency;
}

void NetworkMonitor::updateStats(const bool available, const long latency) {
    std::lock_guard lock(mutex_);
    
    // 更新基本状态
    stats_.available = available;
    stats_.lastCheckTime = std::time(nullptr);
    
    if (available) {
        // 网络可用：更新延迟和失败计数
        stats_.consecutiveFailures = 0;
        
        // 延迟平滑处理：使用简单移动平均（SMA）算法
        // 使用 deque 提高效率（pop_front 比 vector.erase(begin()) 更高效）
        if (latency >= 0) {  // 只处理有效的延迟值
            // 添加到历史记录
            latencyHistory_.push_back(latency);
            
            // 保持历史记录大小在限制内（deque 的 pop_front 是 O(1)）
            if (latencyHistory_.size() > LATENCY_HISTORY_SIZE) {
                latencyHistory_.pop_front();
            }
            
            // 计算平滑延迟（使用移动平均）
            if (!latencyHistory_.empty()) {
                const long sum = std::accumulate(latencyHistory_.begin(), latencyHistory_.end(), 0L);
                stats_.latency = sum / static_cast<long>(latencyHistory_.size());
            } else {
                stats_.latency = latency;
            }
        }
        
        // 更新连通性历史（用于丢包率计算）
        connectivityHistory_.push_back(true);
    } else {
        // 网络不可用：增加失败计数
        stats_.consecutiveFailures++;
        
        // 延迟保持上次有效值（不更新为-1，保持历史信息）
        // 如果历史记录为空，才设置为-1
        if (latencyHistory_.empty()) {
            stats_.latency = -1;
        }
        // 否则保持当前平滑后的延迟值
        
        // 更新连通性历史（用于丢包率计算）
        connectivityHistory_.push_back(false);
        failedChecksCount_++;  // 增加失败计数
    }
    
    // 保持连通性历史记录大小在限制内（deque 的 pop_front 是 O(1)）
    // 注意：必须在 push_back 之后检查，因为 push_back 后 size 可能超过限制
    if (connectivityHistory_.size() > PACKET_LOSS_WINDOW_SIZE) {
        // 移除最旧的记录，如果是失败，需要减少失败计数
        if (!connectivityHistory_.front()) {
            failedChecksCount_--;
        }
        connectivityHistory_.pop_front();
    }
    
    // 计算丢包率：基于连通性历史窗口（使用维护的计数器，避免每次遍历）
    // 丢包率 = 失败次数 / 总检测次数
    if (!connectivityHistory_.empty()) {
        const size_t totalChecks = connectivityHistory_.size();
        stats_.packetLoss = static_cast<double>(failedChecksCount_) / static_cast<double>(totalChecks);
        
        // 确保丢包率在有效范围内 [0.0, 1.0]
        stats_.packetLoss = std::clamp(stats_.packetLoss, 0.0, 1.0);
    } else {
        // 没有历史数据时，根据当前状态设置
        stats_.packetLoss = available ? 0.0 : 1.0;
        failedChecksCount_ = 0;  // 重置计数器
    }
}

NetworkQuality NetworkMonitor::calculateQuality(const NetworkStats& stats) {
    if (!stats.available) {
        return NetworkQuality::POOR;  // 网络不可用时返回POOR
    }
    
    if (stats.latency < 0) {
        return NetworkQuality::FAIR;  // 延迟未知时返回FAIR
    }
    
    // 根据延迟评估网络质量
    if (stats.latency < 50) {
        return NetworkQuality::EXCELLENT;
    }
    if (stats.latency < 100) {
        return NetworkQuality::GOOD;
    }
    if (stats.latency < 200) {
        return NetworkQuality::FAIR;
    }
    return NetworkQuality::POOR;
}

} // namespace mqtt_client
