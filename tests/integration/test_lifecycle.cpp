/**
 * @file test_lifecycle.cpp
 * @brief 连接生命周期、主动断开和 MQTT 5 属性发布的真实 broker 测试
 */

#include "test_environment.h"
#include "mqtt_client/embedded_mqtt_client.h"
#include "mqtt_client/config/config_manager.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using namespace mqtt_client;

namespace {

struct MessageWaiter {
    std::mutex mutex;
    std::condition_variable cv;
    int received = 0;
};

MqttConfig makeConfig() {
    auto config = MqttConfigManager::getDefaultConfig();
    config.server.host = mqtt_test::envOr("MQTT_TEST_HOST", "127.0.0.1");
    config.server.port = mqtt_test::envPort("MQTT_TEST_TCP_PORT", 1883);
    config.server.useSSL = false;
    config.basic.version = "5.0";
    config.basic.cleanStart = true;
    config.basic.clientId = "libmqtt_lifecycle_" + std::to_string(std::time(nullptr));
    config.monitoring.enableNetworkMonitor = false;
    config.monitoring.enableConnectionMonitor = false;
    config.monitoring.enableHeartbeat = false;
    config.performance.batch.enableBatchSend = false;
    config.persistence.buffer.enableSendPersistence = false;
    config.reconnect.baseInterval = 100;
    config.reconnect.maxInterval = 500;
    config.reconnect.maxAttempts = 5;
    return config;
}

bool waitFor(MessageWaiter& waiter, const int expected, const int timeoutMs = 5000) {
    std::unique_lock lock(waiter.mutex);
    return waiter.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        return waiter.received >= expected;
    });
}

} // namespace

int main() {
    const std::string topic = mqtt_test::envOr("MQTT_TEST_TOPIC", "libmqtt-client/lifecycle");
    auto config = makeConfig();
    EmbeddedMqttClient client(config);
    if (!client.isInitialized()) {
        std::cerr << "client initialization failed" << std::endl;
        return 1;
    }

    MessageWaiter waiter;
    if (!client.connect()) {
        std::cerr << "initial connection failed" << std::endl;
        return 1;
    }

    if (!client.subscribe(topic, [&waiter](std::string_view,
                                           std::string_view,
                                           const MqttProperties&) {
            std::lock_guard lock(waiter.mutex);
            ++waiter.received;
            waiter.cv.notify_all();
        }, QoS::QOS_1)) {
        std::cerr << "subscription failed" << std::endl;
        return 1;
    }

    MqttProperties properties;
    properties.contentType = "application/json";
    properties.setUserProperty("test", "lifecycle");
    if (!client.publish(topic, "{\"phase\":1}", properties, QoS::QOS_1)) {
        std::cerr << "property publish failed" << std::endl;
        return 1;
    }
    if (!waitFor(waiter, 1)) {
        std::cerr << "first message was not received" << std::endl;
        return 1;
    }

    if (!client.disconnect(true)) {
        std::cerr << "forced disconnect failed" << std::endl;
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    if (client.isConnected() || client.getState() != ConnectionState::DISCONNECTED) {
        std::cerr << "forced disconnect unexpectedly triggered reconnect" << std::endl;
        return 1;
    }

    if (!client.reconnect()) {
        std::cerr << "explicit reconnect failed" << std::endl;
        return 1;
    }
    if (!client.publish(topic, "{\"phase\":2}", properties, QoS::QOS_1)) {
        std::cerr << "property publish after reconnect failed" << std::endl;
        return 1;
    }
    if (!waitFor(waiter, 2)) {
        std::cerr << "subscription was not restored after reconnect" << std::endl;
        return 1;
    }

    (void)client.disconnect(true);
    std::cout << "lifecycle test passed" << std::endl;
    return 0;
}
