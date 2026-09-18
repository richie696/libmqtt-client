/**
 * @file test_mqtt311_tcp_tls.cpp
 * @brief MQTT 3.1.1 TCP和TLS协议收发消息测试
 * 
 * 测试MQTT 3.1.1协议在TCP和TLS连接下的消息发送和接收功能
 * 包含 QoS 0、1、2 三种级别的测试
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <map>
#include "test_environment.h"

#include "mqtt_client/embedded_mqtt_client.h"
#include "mqtt_client/config/config.h"
#include "mqtt_client/config/config_manager.h"
#include "mqtt_client/core/types.h"

using namespace mqtt_client;

// 测试服务器配置
const std::string TEST_SERVER_HOST = mqtt_test::envOr("MQTT_TEST_HOST", "127.0.0.1");
const int TEST_TCP_PORT = mqtt_test::envPort("MQTT_TEST_TCP_PORT", 1883);
const int TEST_TLS_PORT = mqtt_test::envPort("MQTT_TEST_TLS_PORT", 8883);
const std::string TEST_USERNAME = mqtt_test::envOr("MQTT_TEST_USERNAME", "");
const std::string TEST_PASSWORD = mqtt_test::envOr("MQTT_TEST_PASSWORD", "");
const std::string TEST_TOPIC = mqtt_test::envOr("MQTT_TEST_TOPIC", "libmqtt-client/test");
const std::string TEST_CA_CERTIFICATE = mqtt_test::envOr("MQTT_TEST_CA_CERT", "");
const bool TEST_TLS_ENABLED = mqtt_test::envBool("MQTT_TEST_ENABLE_TLS", true);

// 消息接收状态
struct MessageState {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::pair<std::string, std::string>> receivedMessages;  // <topic, payload>
    std::atomic<bool> messageReceived{false};
    int expectedCount = 0;
    int receivedCount = 0;
    std::chrono::steady_clock::time_point receiveTime;  // 消息接收时间
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        receivedMessages.clear();
        messageReceived = false;
        receivedCount = 0;
    }
};

// 创建TCP配置
MqttConfig createTcpConfig() {
    MqttConfig config = MqttConfigManager::getDefaultConfig();
    config.server.host = TEST_SERVER_HOST;
    config.server.port = TEST_TCP_PORT;
    config.server.useSSL = false;
    config.server.connectTimeout = 30;
    config.server.keepAlive = 60;
    config.auth.username = TEST_USERNAME;
    config.auth.password = TEST_PASSWORD;
    config.basic.clientId = "mqtt311_tcp_test_" + std::to_string(std::time(nullptr));
    config.basic.version = "3.1.1";  // 使用MQTT 3.1.1
    config.basic.cleanStart = true;
    config.performance.batch.enableBatchSend = false;  // 禁用批处理，立即发送
    return config;
}

// 创建TLS配置
MqttConfig createTlsConfig() {
    MqttConfig config = createTcpConfig();
    config.server.port = TEST_TLS_PORT;
    config.server.useSSL = true;
    config.security.enableTLS = true;
    config.security.verifyCertificate = !TEST_CA_CERTIFICATE.empty();
    config.security.caCertificatePath = TEST_CA_CERTIFICATE;
    config.basic.clientId = "mqtt311_tls_test_" + std::to_string(std::time(nullptr));
    config.server.connectTimeout = 30;  // TLS连接需要更长时间
    return config;
}

// 消息回调
void onMessage(MessageState* state, const std::string& topic, const std::string& payload, 
               const MqttProperties& /*properties*/) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->receivedMessages.emplace_back(topic, payload);
    state->receivedCount++;
    state->messageReceived = true;
    state->receiveTime = std::chrono::steady_clock::now();  // 记录接收时间
    state->cv.notify_one();
    std::cout << "  [收到消息] Topic: " << topic << ", Payload: " << payload << std::endl;
}

// 等待消息接收
bool waitForMessages(MessageState* state, int timeoutSeconds = 10) {
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->cv.wait_for(lock, std::chrono::seconds(timeoutSeconds), 
                              [state] { 
                                  return state->receivedCount >= state->expectedCount; 
                              });
}

// QoS级别名称
std::string qosToString(QoS qos) {
    switch (qos) {
        case QoS::QOS_0: return "QoS 0";
        case QoS::QOS_1: return "QoS 1";
        case QoS::QOS_2: return "QoS 2";
        default: return "Unknown";
    }
}

// 测试指定QoS级别的消息收发（TCP）
bool testTcpQoS(QoS qos) {
    std::cout << "\n  --- TCP " << qosToString(qos) << " 测试 ---" << std::endl;
    
    auto config = createTcpConfig();
    config.basic.clientId = "mqtt311_tcp_qos" + std::to_string(static_cast<int>(qos)) + "_" + 
                            std::to_string(std::time(nullptr));
    EmbeddedMqttClient client(config);
    
    MessageState msgState;
    msgState.expectedCount = 1;
    msgState.reset();
    
    // 连接服务器
    auto connectResult = client.connect();
    if (!connectResult.success) {
        std::cerr << "    ✗ 连接失败: " << connectResult.error.message << std::endl;
        return false;
    }
    
    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    if (!client.isConnected()) {
        std::cerr << "    ✗ 连接状态检查失败" << std::endl;
        return false;
    }
    
    // 订阅主题（使用相同的QoS级别）
    auto subscribeResult = client.subscribe(TEST_TOPIC, 
                                             [&msgState](std::string_view topic, 
                                                        std::string_view payload,
                                                        const MqttProperties& /*properties*/) {
                                                 onMessage(&msgState, std::string(topic), std::string(payload), MqttProperties{});
                                             },
                                             qos);
    if (!subscribeResult.success) {
        std::cerr << "    ✗ 订阅失败: " << subscribeResult.error.message << std::endl;
        (void)client.disconnect();
        return false;
    }
    
    // 等待订阅确认
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 发布消息
    std::string testMessage = "MQTT 3.1.1 TCP " + qosToString(qos) + " Test - " + 
                              std::to_string(std::time(nullptr));
    
    // 记录发布开始时间
    auto publishStartTime = std::chrono::steady_clock::now();
    
    auto publishResult = client.publish(TEST_TOPIC, testMessage, qos);
    if (!publishResult.success) {
        std::cerr << "    ✗ 发布失败: " << publishResult.error.message << std::endl;
        (void)client.disconnect();
        return false;
    }
    
    // 等待消息接收（QoS 2 需要更长时间）
    int timeout = (qos == QoS::QOS_2) ? 10 : 5;
    bool waitResult = waitForMessages(&msgState, timeout);
    
    // 无论 waitForMessages 是否返回 true，都等待一小段时间，确保消息被完全添加到列表中
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 验证消息内容并计算耗时（在锁保护下）
    bool found = false;
    size_t receivedCount = 0;
    std::vector<std::pair<std::string, std::string>> receivedMsgs;
    std::chrono::steady_clock::time_point receiveTime;
    {
        std::lock_guard<std::mutex> lock(msgState.mutex);
        receivedCount = msgState.receivedMessages.size();
        receivedMsgs = msgState.receivedMessages;
        receiveTime = msgState.receiveTime;
        for (const auto& msg : msgState.receivedMessages) {
            if (msg.first == TEST_TOPIC && msg.second == testMessage) {
                found = true;
                break;
            }
        }
    }
    
    if (found) {
        // 计算总耗时（毫秒）
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            receiveTime - publishStartTime).count();
        std::cout << "    ✓ " << qosToString(qos) << " 测试通过 (耗时: " << elapsed << " ms)" << std::endl;
        (void)client.disconnect();
        return true;
    } else {
        std::cout << "    ✗ 消息内容验证失败" << std::endl;
        // 输出调试信息
        std::cout << "    期望消息: " << testMessage << std::endl;
        std::cout << "    waitForMessages 结果: " << (waitResult ? "true" : "false") << std::endl;
        std::cout << "    收到消息数: " << receivedCount << std::endl;
        for (const auto& msg : receivedMsgs) {
            std::cout << "    实际消息: Topic=" << msg.first << ", Payload=" << msg.second << std::endl;
        }
        (void)client.disconnect();
        return false;
    }
}

// 测试指定QoS级别的消息收发（TLS）
bool testTlsQoS(QoS qos) {
    std::cout << "\n  --- TLS " << qosToString(qos) << " 测试 ---" << std::endl;
    
    auto config = createTlsConfig();
    config.basic.clientId = "mqtt311_tls_qos" + std::to_string(static_cast<int>(qos)) + "_" + 
                            std::to_string(std::time(nullptr));
    EmbeddedMqttClient client(config);
    
    MessageState msgState;
    msgState.expectedCount = 1;
    msgState.reset();
    
    // 连接服务器
    auto connectResult = client.connect();
    if (!connectResult.success) {
        std::cerr << "    ✗ 连接失败: " << connectResult.error.message << std::endl;
        return false;
    }
    
    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    if (!client.isConnected()) {
        std::cerr << "    ✗ 连接状态检查失败" << std::endl;
        return false;
    }
    
    // 订阅主题（使用相同的QoS级别）
    auto subscribeResult = client.subscribe(TEST_TOPIC, 
                                             [&msgState](std::string_view topic, 
                                                        std::string_view payload,
                                                        const MqttProperties& /*properties*/) {
                                                 onMessage(&msgState, std::string(topic), std::string(payload), MqttProperties{});
                                             },
                                             qos);
    if (!subscribeResult.success) {
        std::cerr << "    ✗ 订阅失败: " << subscribeResult.error.message << std::endl;
        (void)client.disconnect();
        return false;
    }
    
    // 等待订阅确认
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 发布消息
    std::string testMessage = "MQTT 3.1.1 TLS " + qosToString(qos) + " Test - " + 
                              std::to_string(std::time(nullptr));
    
    // 记录发布开始时间
    auto publishStartTime = std::chrono::steady_clock::now();
    
    auto publishResult = client.publish(TEST_TOPIC, testMessage, qos);
    if (!publishResult.success) {
        std::cerr << "    ✗ 发布失败: " << publishResult.error.message << std::endl;
        (void)client.disconnect();
        return false;
    }
    
    // 等待消息接收（QoS 2 需要更长时间）
    int timeout = (qos == QoS::QOS_2) ? 10 : 5;
    bool waitResult = waitForMessages(&msgState, timeout);
    
    // 无论 waitForMessages 是否返回 true，都等待一小段时间，确保消息被完全添加到列表中
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 验证消息内容并计算耗时（在锁保护下）
    bool found = false;
    size_t receivedCount = 0;
    std::vector<std::pair<std::string, std::string>> receivedMsgs;
    std::chrono::steady_clock::time_point receiveTime;
    {
        std::lock_guard<std::mutex> lock(msgState.mutex);
        receivedCount = msgState.receivedMessages.size();
        receivedMsgs = msgState.receivedMessages;
        receiveTime = msgState.receiveTime;
        for (const auto& msg : msgState.receivedMessages) {
            if (msg.first == TEST_TOPIC && msg.second == testMessage) {
                found = true;
                break;
            }
        }
    }
    
    if (found) {
        // 计算总耗时（毫秒）
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            receiveTime - publishStartTime).count();
        std::cout << "    ✓ " << qosToString(qos) << " 测试通过 (耗时: " << elapsed << " ms)" << std::endl;
        (void)client.disconnect();
        return true;
    } else {
        std::cout << "    ✗ 消息内容验证失败" << std::endl;
        // 输出调试信息
        std::cout << "    期望消息: " << testMessage << std::endl;
        std::cout << "    waitForMessages 结果: " << (waitResult ? "true" : "false") << std::endl;
        std::cout << "    收到消息数: " << receivedCount << std::endl;
        for (const auto& msg : receivedMsgs) {
            std::cout << "    实际消息: Topic=" << msg.first << ", Payload=" << msg.second << std::endl;
        }
        (void)client.disconnect();
        return false;
    }
}

// 测试TCP连接的所有QoS级别
struct TcpTestResults {
    bool qos0 = false;
    bool qos1 = false;
    bool qos2 = false;
};

TcpTestResults testTcpConnection() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试 1: MQTT 3.1.1 TCP 连接" << std::endl;
    std::cout << "========================================" << std::endl;
    
    TcpTestResults results;
    
    // 测试 QoS 0
    try {
        results.qos0 = testTcpQoS(QoS::QOS_0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } catch (const std::exception& e) {
        std::cerr << "    QoS 0 测试异常: " << e.what() << std::endl;
        results.qos0 = false;
    }
    
    // 测试 QoS 1
    try {
        results.qos1 = testTcpQoS(QoS::QOS_1);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } catch (const std::exception& e) {
        std::cerr << "    QoS 1 测试异常: " << e.what() << std::endl;
        results.qos1 = false;
    }
    
    // 测试 QoS 2
    try {
        results.qos2 = testTcpQoS(QoS::QOS_2);
    } catch (const std::exception& e) {
        std::cerr << "    QoS 2 测试异常: " << e.what() << std::endl;
        results.qos2 = false;
    }
    
    std::cout << "\n  TCP测试结果:" << std::endl;
    std::cout << "    QoS 0: " << (results.qos0 ? "✓ 通过" : "✗ 失败") << std::endl;
    std::cout << "    QoS 1: " << (results.qos1 ? "✓ 通过" : "✗ 失败") << std::endl;
    std::cout << "    QoS 2: " << (results.qos2 ? "✓ 通过" : "✗ 失败") << std::endl;
    
    return results;
}

// 测试TLS连接的所有QoS级别
struct TlsTestResults {
    bool qos0 = false;
    bool qos1 = false;
    bool qos2 = false;
};

TlsTestResults testTlsConnection() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试 2: MQTT 3.1.1 TLS 连接" << std::endl;
    std::cout << "========================================" << std::endl;
    
    TlsTestResults results;
    
    // 测试 QoS 0
    try {
        results.qos0 = testTlsQoS(QoS::QOS_0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } catch (const std::exception& e) {
        std::cerr << "    QoS 0 测试异常: " << e.what() << std::endl;
        results.qos0 = false;
    }
    
    // 测试 QoS 1
    try {
        results.qos1 = testTlsQoS(QoS::QOS_1);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    } catch (const std::exception& e) {
        std::cerr << "    QoS 1 测试异常: " << e.what() << std::endl;
        results.qos1 = false;
    }
    
    // 测试 QoS 2
    try {
        results.qos2 = testTlsQoS(QoS::QOS_2);
    } catch (const std::exception& e) {
        std::cerr << "    QoS 2 测试异常: " << e.what() << std::endl;
        results.qos2 = false;
    }
    
    std::cout << "\n  TLS测试结果:" << std::endl;
    std::cout << "    QoS 0: " << (results.qos0 ? "✓ 通过" : "✗ 失败") << std::endl;
    std::cout << "    QoS 1: " << (results.qos1 ? "✓ 通过" : "✗ 失败") << std::endl;
    std::cout << "    QoS 2: " << (results.qos2 ? "✓ 通过" : "✗ 失败") << std::endl;
    
    return results;
}

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "MQTT 3.1.1 TCP和TLS协议收发消息测试" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "服务器: " << TEST_SERVER_HOST << std::endl;
    std::cout << "TCP端口: " << TEST_TCP_PORT << std::endl;
    std::cout << "TLS端口: " << TEST_TLS_PORT << std::endl;
    std::cout << "测试主题: " << TEST_TOPIC << std::endl;
    std::cout << "协议版本: MQTT 3.1.1" << std::endl;
    std::cout << "QoS级别: 0, 1, 2" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 测试TCP连接
    TcpTestResults tcpResults;
    try {
        tcpResults = testTcpConnection();
    } catch (const std::exception& e) {
        std::cerr << "TCP测试异常: " << e.what() << std::endl;
        tcpResults = TcpTestResults{false, false, false};
    }
    
    // 测试TLS连接
    TlsTestResults tlsResults;
    if (TEST_TLS_ENABLED) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        try {
            tlsResults = testTlsConnection();
        } catch (const std::exception& e) {
            std::cerr << "TLS测试异常: " << e.what() << std::endl;
            tlsResults = TlsTestResults{false, false, false};
        }
    }
    
    // 输出测试结果总结
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试结果总结" << std::endl;
    std::cout << "========================================" << std::endl;
    bool tcpAllPass = tcpResults.qos0 && tcpResults.qos1 && tcpResults.qos2;
    const bool tlsAllPass = !TEST_TLS_ENABLED ||
        (tlsResults.qos0 && tlsResults.qos1 && tlsResults.qos2);
    
    std::cout << "MQTT 3.1.1 TCP 测试: " << (tcpAllPass ? "✓ 通过" : "✗ 失败") << std::endl;
    std::cout << "  - QoS 0: " << (tcpResults.qos0 ? "✓ 通过" : "✗ 失败") << std::endl;
    std::cout << "  - QoS 1: " << (tcpResults.qos1 ? "✓ 通过" : "✗ 失败") << std::endl;
    std::cout << "  - QoS 2: " << (tcpResults.qos2 ? "✓ 通过" : "✗ 失败") << std::endl;
    if (TEST_TLS_ENABLED) {
        std::cout << "MQTT 3.1.1 TLS 测试: " << (tlsAllPass ? "✓ 通过" : "✗ 失败") << std::endl;
        std::cout << "  - QoS 0: " << (tlsResults.qos0 ? "✓ 通过" : "✗ 失败") << std::endl;
        std::cout << "  - QoS 1: " << (tlsResults.qos1 ? "✓ 通过" : "✗ 失败") << std::endl;
        std::cout << "  - QoS 2: " << (tlsResults.qos2 ? "✓ 通过" : "✗ 失败") << std::endl;
    } else {
        std::cout << "MQTT 3.1.1 TLS 测试: - 已跳过" << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    if (tcpAllPass && tlsAllPass) {
        std::cout << "所有测试通过！" << std::endl;
        return 0;
    } else {
        std::cout << "部分测试失败" << std::endl;
        return 1;
    }
}
