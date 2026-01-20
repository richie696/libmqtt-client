/**
 * @file metrics.cpp
 * @brief 监控指标实现
 * 
 * 实现MQTT客户端库的监控指标功能。
 */

#include "mqtt_client/metrics/metrics.h"
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace mqtt_client {

void MqttMetrics::reset() {
    // 重置连接指标
    connection.totalConnections = 0;
    connection.successfulConnections = 0;
    connection.failedConnections = 0;
    connection.disconnections = 0;
    connection.reconnections = 0;
    connection.avgConnectionTime = std::chrono::milliseconds{0};
    
    // 重置消息指标
    message.messagesSent = 0;
    message.messagesReceived = 0;
    message.messagesFailed = 0;
    message.bytesSent = 0;
    message.bytesReceived = 0;
    message.avgLatency = std::chrono::milliseconds{0};
    
    // 重置网络指标
    network.networkErrors = 0;
    network.timeouts = 0;
    network.packetLossRate = 0.0;
    network.avgLatency = std::chrono::milliseconds{0};
    
    // 重置订阅指标
    subscription.subscriptions = 0;
    subscription.unsubscriptions = 0;
    subscription.subscriptionFailures = 0;
    
    // 重置心跳指标
    heartbeat.heartbeatsSent = 0;
    heartbeat.heartbeatsReceived = 0;
    heartbeat.heartbeatTimeouts = 0;
    
    // 重置性能指标
    performance.memoryUsage = 0;
    performance.cpuUsage = 0.0;
    performance.activeThreads = 0;
    performance.queueSize = 0;
    
    // 重置错误指标
    error.totalErrors = 0;
    error.errorCounts.clear();
    
    // 重置自定义指标
    customMetrics.clear();
}

std::string MqttMetrics::toString() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "=== MQTT 监控指标 ===\n\n";
    
    // 连接指标
    oss << "【连接指标】\n";
    oss << "  总连接次数: " << connection.totalConnections << "\n";
    oss << "  成功连接: " << connection.successfulConnections << "\n";
    oss << "  失败连接: " << connection.failedConnections << "\n";
    oss << "  断开连接: " << connection.disconnections << "\n";
    oss << "  重连次数: " << connection.reconnections << "\n";
    oss << "  平均连接时间: " << connection.avgConnectionTime.count() << " ms\n";
    oss << "\n";
    
    // 消息指标
    oss << "【消息指标】\n";
    oss << "  发送消息数: " << message.messagesSent << "\n";
    oss << "  接收消息数: " << message.messagesReceived << "\n";
    oss << "  失败消息数: " << message.messagesFailed << "\n";
    oss << "  发送字节数: " << message.bytesSent << "\n";
    oss << "  接收字节数: " << message.bytesReceived << "\n";
    oss << "  平均延迟: " << message.avgLatency.count() << " ms\n";
    oss << "\n";
    
    // 网络指标
    oss << "【网络指标】\n";
    oss << "  网络错误数: " << network.networkErrors << "\n";
    oss << "  超时次数: " << network.timeouts << "\n";
    oss << "  丢包率: " << (network.packetLossRate * 100.0) << "%\n";
    oss << "  平均延迟: " << network.avgLatency.count() << " ms\n";
    oss << "\n";
    
    // 订阅指标
    oss << "【订阅指标】\n";
    oss << "  订阅数: " << subscription.subscriptions << "\n";
    oss << "  取消订阅数: " << subscription.unsubscriptions << "\n";
    oss << "  订阅失败数: " << subscription.subscriptionFailures << "\n";
    oss << "\n";
    
    // 心跳指标
    oss << "【心跳指标】\n";
    oss << "  发送心跳数: " << heartbeat.heartbeatsSent << "\n";
    oss << "  接收心跳数: " << heartbeat.heartbeatsReceived << "\n";
    oss << "  心跳超时数: " << heartbeat.heartbeatTimeouts << "\n";
    oss << "\n";
    
    // 性能指标
    oss << "【性能指标】\n";
    oss << "  内存使用: " << (static_cast<double>(performance.memoryUsage) / 1024.0 / 1024.0) << " MB\n";
    oss << "  CPU使用率: " << performance.cpuUsage << "%\n";
    oss << "  活跃线程数: " << performance.activeThreads << "\n";
    oss << "  队列大小: " << performance.queueSize << "\n";
    oss << "\n";
    
    // 错误指标
    oss << "【错误指标】\n";
    oss << "  总错误数: " << error.totalErrors << "\n";
    if (!error.errorCounts.empty()) {
        oss << "  错误码统计:\n";
        for (const auto& [code, count] : error.errorCounts) {
            oss << "    " << static_cast<int>(code) << ": " << count << "\n";
        }
    }
    oss << "\n";
    
    // 自定义指标
    if (!customMetrics.empty()) {
        oss << "【自定义指标】\n";
        for (const auto& [key, value] : customMetrics) {
            oss << "  " << key << ": " << value << "\n";
        }
        oss << "\n";
    }
    
    return oss.str();
}

std::string MqttMetrics::toJson() const {
    nlohmann::json j;
    
    // 连接指标
    j["connection"] = {
        {"totalConnections", connection.totalConnections},
        {"successfulConnections", connection.successfulConnections},
        {"failedConnections", connection.failedConnections},
        {"disconnections", connection.disconnections},
        {"reconnections", connection.reconnections},
        {"avgConnectionTimeMs", connection.avgConnectionTime.count()}
    };
    
    // 消息指标
    j["message"] = {
        {"messagesSent", message.messagesSent},
        {"messagesReceived", message.messagesReceived},
        {"messagesFailed", message.messagesFailed},
        {"bytesSent", message.bytesSent},
        {"bytesReceived", message.bytesReceived},
        {"avgLatencyMs", message.avgLatency.count()}
    };
    
    // 网络指标
    j["network"] = {
        {"networkErrors", network.networkErrors},
        {"timeouts", network.timeouts},
        {"packetLossRate", network.packetLossRate},
        {"avgLatencyMs", network.avgLatency.count()}
    };
    
    // 订阅指标
    j["subscription"] = {
        {"subscriptions", subscription.subscriptions},
        {"unsubscriptions", subscription.unsubscriptions},
        {"subscriptionFailures", subscription.subscriptionFailures}
    };
    
    // 心跳指标
    j["heartbeat"] = {
        {"heartbeatsSent", heartbeat.heartbeatsSent},
        {"heartbeatsReceived", heartbeat.heartbeatsReceived},
        {"heartbeatTimeouts", heartbeat.heartbeatTimeouts}
    };
    
    // 性能指标
    j["performance"] = {
        {"memoryUsageBytes", performance.memoryUsage},
        {"cpuUsage", performance.cpuUsage},
        {"activeThreads", performance.activeThreads},
        {"queueSize", performance.queueSize}
    };
    
    // 错误指标
    nlohmann::json errorJson;
    errorJson["totalErrors"] = error.totalErrors;
    nlohmann::json errorCountsJson;
    for (const auto& [code, count] : error.errorCounts) {
        errorCountsJson[std::to_string(static_cast<int>(code))] = count;
    }
    errorJson["errorCounts"] = errorCountsJson;
    j["error"] = errorJson;
    
    // 自定义指标
    if (!customMetrics.empty()) {
        nlohmann::json customJson;
        for (const auto& [key, value] : customMetrics) {
            customJson[key] = value;
        }
        j["customMetrics"] = customJson;
    }
    
    return j.dump(2);  // 使用2个空格缩进，便于阅读
}

} // namespace mqtt_client
