/**
 * @file test_heartbeat_manager.cpp
 * @brief 心跳管理单元测试
 */

#include <gtest/gtest.h>
#include "internal/monitor/heartbeat_manager.h"
#include "internal/message/message_manager.h"
#include "internal/connection/connection_manager.h"
#include "mqtt_client/config/config.h"
#include "mqtt_client/config/config_manager.h"
#include "internal/adapter/wolfmqtt_adapter.h"
#include <memory>
#include <thread>
#include <chrono>

using namespace mqtt_client;

class HeartbeatManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = MqttConfigManager::getInstance().getDefaultConfig();
        config_.server.host = "test.mqtt.com";
        config_.server.port = 1883;
        
        adapter_ = std::make_unique<WolfMqttAdapter>(config_);
        connectionManager_ = std::make_unique<MqttConnectionManager>(config_, adapter_.get());
        messageManager_ = std::make_unique<MqttMessageManager>(*connectionManager_, config_);
        
        heartbeatManager_ = std::make_unique<HeartbeatManager>(*messageManager_, 1, "test/heartbeat");
    }
    
    void TearDown() override {
        if (heartbeatManager_ && heartbeatManager_->isRunning()) {
            heartbeatManager_->stop();
        }
        heartbeatManager_.reset();
        messageManager_.reset();
        connectionManager_.reset();
        adapter_.reset();
    }
    
    MqttConfig config_;
    std::unique_ptr<WolfMqttAdapter> adapter_;
    std::unique_ptr<MqttConnectionManager> connectionManager_;
    std::unique_ptr<MqttMessageManager> messageManager_;
    std::unique_ptr<HeartbeatManager> heartbeatManager_;
};

// 测试构造函数
TEST_F(HeartbeatManagerTest, Constructor) {
    EXPECT_FALSE(heartbeatManager_->isRunning());
}

// 测试启动和停止
TEST_F(HeartbeatManagerTest, StartStop) {
    heartbeatManager_->start();
    EXPECT_TRUE(heartbeatManager_->isRunning());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    heartbeatManager_->stop();
    EXPECT_FALSE(heartbeatManager_->isRunning());
}

// 测试获取心跳统计信息
TEST_F(HeartbeatManagerTest, GetStats) {
    auto stats = heartbeatManager_->getStats();
    
    // 初始统计信息应该存在
    EXPECT_EQ(stats.totalSent, 0);
    EXPECT_EQ(stats.totalFailed, 0);
}

// 测试心跳线程生命周期
TEST_F(HeartbeatManagerTest, HeartbeatThreadLifecycle) {
    heartbeatManager_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(heartbeatManager_->isRunning());
    
    heartbeatManager_->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_FALSE(heartbeatManager_->isRunning());
}
