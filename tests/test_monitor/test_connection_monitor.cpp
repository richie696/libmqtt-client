/**
 * @file test_connection_monitor.cpp
 * @brief 连接监控单元测试
 */

#include <gtest/gtest.h>
#include "internal/monitor/connection_monitor.h"
#include "internal/connection/connection_manager.h"
#include "mqtt_client/config/config.h"
#include "mqtt_client/config/config_manager.h"
#include "internal/adapter/wolfmqtt_adapter.h"
#include <memory>
#include <thread>
#include <chrono>

using namespace mqtt_client;

class ConnectionMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = MqttConfigManager::getInstance().getDefaultConfig();
        config_.server.host = "test.mqtt.com";
        config_.server.port = 1883;
        
        adapter_ = std::make_unique<WolfMqttAdapter>(config_);
        connectionManager_ = std::make_unique<MqttConnectionManager>(config_, adapter_.get());
        
        monitor_ = std::make_unique<ConnectionMonitor>(*connectionManager_, 1);
    }
    
    void TearDown() override {
        // 先停止monitor，然后按顺序析构（避免mutex问题）
        if (monitor_) {
            if (monitor_->isRunning()) {
                monitor_->stop();
            }
            monitor_.reset();
        }
        // 等待一下，确保线程完全退出
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        connectionManager_.reset();
        adapter_.reset();
    }
    
    MqttConfig config_;
    std::unique_ptr<WolfMqttAdapter> adapter_;
    std::unique_ptr<MqttConnectionManager> connectionManager_;
    std::unique_ptr<ConnectionMonitor> monitor_;
};

// 测试构造函数
TEST_F(ConnectionMonitorTest, Constructor) {
    EXPECT_FALSE(monitor_->isRunning());
}

// 测试启动和停止
TEST_F(ConnectionMonitorTest, StartStop) {
    monitor_->start();
    EXPECT_TRUE(monitor_->isRunning());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    monitor_->stop();
    EXPECT_FALSE(monitor_->isRunning());
}

// 测试获取连接统计信息
TEST_F(ConnectionMonitorTest, GetStats) {
    auto stats = monitor_->getStats();
    
    // 初始统计信息应该存在
    EXPECT_EQ(stats.totalDisconnects, 0);
    EXPECT_EQ(stats.abnormalDisconnects, 0);
}

// 测试断开连接回调
TEST_F(ConnectionMonitorTest, DisconnectedCallback) {
    bool callbackCalled = false;
    
    monitor_->setOnDisconnected([&]() {
        callbackCalled = true;
    });
    
    EXPECT_FALSE(callbackCalled);
}

// 测试监控线程生命周期
TEST_F(ConnectionMonitorTest, MonitorThreadLifecycle) {
    monitor_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(monitor_->isRunning());
    
    monitor_->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_FALSE(monitor_->isRunning());
}
