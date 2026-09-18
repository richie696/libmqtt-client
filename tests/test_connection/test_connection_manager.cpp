/**
 * @file test_connection_manager.cpp
 * @brief 连接管理单元测试
 */

#include <gtest/gtest.h>
#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/config/config.h"
#include "mqtt_client/config/config_manager.h"
#include "mqtt_client/adapter/wolfmqtt_adapter.h"
#include <memory>

using namespace mqtt_client;

class ConnectionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = MqttConfigManager::getInstance().getDefaultConfig();
        config_.server.host = "test.mqtt.com";
        config_.server.port = 1883;
        config_.basic.clientId = "test_client";
        
        // 创建适配器（mock或实际）
        adapter_ = std::make_unique<WolfMqttAdapter>(config_);
        
        // 创建连接管理器
        manager_ = std::make_unique<MqttConnectionManager>(config_, adapter_.get());
    }
    
    void TearDown() override {
        // 先重置manager，避免在析构时调用disconnect导致mutex问题
        manager_.reset();
        adapter_.reset();
    }
    
    MqttConfig config_;
    std::unique_ptr<WolfMqttAdapter> adapter_;
    std::unique_ptr<MqttConnectionManager> manager_;
};

// 测试构造函数
TEST_F(ConnectionManagerTest, Constructor) {
    EXPECT_FALSE(manager_->isConnected());
    EXPECT_EQ(manager_->getState(), ConnectionState::DISCONNECTED);
}

// 测试连接状态查询
TEST_F(ConnectionManagerTest, IsConnected) {
    EXPECT_FALSE(manager_->isConnected());
}

// 测试获取连接状态
TEST_F(ConnectionManagerTest, GetState) {
    ConnectionState state = manager_->getState();
    EXPECT_EQ(state, ConnectionState::DISCONNECTED);
}

// 测试连接回调设置
TEST_F(ConnectionManagerTest, SetCallbacks) {
    bool onConnectedCalled = false;
    bool onConnectionLostCalled = false;
    bool onConnectFailureCalled = false;
    
    manager_->setOnConnected([&]() {
        onConnectedCalled = true;
    });
    
    manager_->setOnConnectionLost([&](const std::string&) {
        onConnectionLostCalled = true;
    });
    
    manager_->setOnConnectFailure([&](const std::string&) {
        onConnectFailureCalled = true;
    });
    
    EXPECT_FALSE(onConnectedCalled);
    EXPECT_FALSE(onConnectionLostCalled);
    EXPECT_FALSE(onConnectFailureCalled);
}

// 测试重连尝试次数
TEST_F(ConnectionManagerTest, ReconnectCount) {
    EXPECT_EQ(manager_->getReconnectCount(), 0);
}

// 测试断开连接（未连接状态）
TEST_F(ConnectionManagerTest, DisconnectWhenNotConnected) {
    auto result = manager_->disconnect();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(manager_->getState(), ConnectionState::DISCONNECTED);
}

// 测试状态转换
TEST_F(ConnectionManagerTest, StateTransitions) {
    // 初始状态应该是DISCONNECTED
    EXPECT_EQ(manager_->getState(), ConnectionState::DISCONNECTED);
}

// 测试连接管理器生命周期
TEST_F(ConnectionManagerTest, Lifecycle) {
    // 创建新的管理器
    auto newManager = std::make_unique<MqttConnectionManager>(config_, adapter_.get());
    EXPECT_FALSE(newManager->isConnected());
    
    // 析构应该正常
    newManager.reset();
    EXPECT_EQ(newManager, nullptr);
}
