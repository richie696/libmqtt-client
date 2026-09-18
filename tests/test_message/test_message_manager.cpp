/**
 * @file test_message_manager.cpp
 * @brief 消息管理单元测试
 */

#include <gtest/gtest.h>
#include "mqtt_client/message/message_manager.h"
#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/config/config.h"
#include "mqtt_client/config/config_manager.h"
#include "mqtt_client/adapter/wolfmqtt_adapter.h"
#include <memory>
#include <thread>
#include <chrono>

using namespace mqtt_client;

class MessageManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = MqttConfigManager::getInstance().getDefaultConfig();
        config_.server.host = "test.mqtt.com";
        config_.server.port = 1883;
        config_.basic.clientId = "test_client";
        
        // 创建适配器和连接管理器
        adapter_ = std::make_unique<WolfMqttAdapter>(config_);
        connectionManager_ = std::make_unique<MqttConnectionManager>(config_, adapter_.get());
        
        // 创建消息管理器
        messageManager_ = std::make_unique<MqttMessageManager>(*connectionManager_, config_);
    }
    
    void TearDown() override {
        messageManager_.reset();
        connectionManager_.reset();
        adapter_.reset();
    }
    
    MqttConfig config_;
    std::unique_ptr<WolfMqttAdapter> adapter_;
    std::unique_ptr<MqttConnectionManager> connectionManager_;
    std::unique_ptr<MqttMessageManager> messageManager_;
};

// 测试构造函数
TEST_F(MessageManagerTest, Constructor) {
    EXPECT_EQ(messageManager_->getQueueSize(), 0);
}

// 测试发布消息（异步）
TEST_F(MessageManagerTest, PublishAsync) {
    auto result = messageManager_->publish(
        "test/topic",
        "test payload",
        QoS::QOS_1,
        false
    );
    
    ASSERT_TRUE(result.success);
    EXPECT_EQ(messageManager_->getQueueSize(), 1U);
}

// 测试发布消息（同步）
TEST_F(MessageManagerTest, PublishSync) {
    auto result = messageManager_->publishSync(
        "test/topic",
        "test payload",
        QoS::QOS_1,
        false,
        1000  // 1秒超时
    );
    
    ASSERT_FALSE(result.success);
    EXPECT_EQ(result.error.code, MqttErrorCode::NOT_CONNECTED);
}

// 测试获取队列大小
TEST_F(MessageManagerTest, GetQueueSize) {
    size_t initialSize = messageManager_->getQueueSize();
    
    // 发布一些消息
    ASSERT_TRUE(messageManager_->publish("test/topic1", "payload1", QoS::QOS_1));
    ASSERT_TRUE(messageManager_->publish("test/topic2", "payload2", QoS::QOS_1));
    
    // 队列大小可能增加（取决于实现）
    size_t newSize = messageManager_->getQueueSize();
    EXPECT_EQ(newSize, initialSize + 2);
}

// 测试清空队列
TEST_F(MessageManagerTest, ClearQueue) {
    // 先添加一些消息
    ASSERT_TRUE(messageManager_->publish("test/topic1", "payload1", QoS::QOS_1));
    ASSERT_TRUE(messageManager_->publish("test/topic2", "payload2", QoS::QOS_1));
    
    // 清空队列
    messageManager_->clearQueue();
    
    // 队列应该为空
    EXPECT_EQ(messageManager_->getQueueSize(), 0);
}

// 测试消息统计
TEST_F(MessageManagerTest, GetStats) {
    auto stats = messageManager_->getStats();
    
    // 初始统计应该为0
    EXPECT_EQ(stats.totalQueued, 0);
    EXPECT_EQ(stats.totalPublished, 0);
    EXPECT_EQ(stats.totalFailed, 0);
}

// 测试不同QoS级别
TEST_F(MessageManagerTest, DifferentQoSLevels) {
    // QoS 0
    auto result0 = messageManager_->publish("test/qos0", "payload", QoS::QOS_0);
    EXPECT_TRUE(result0.success);
    
    // QoS 1
    auto result1 = messageManager_->publish("test/qos1", "payload", QoS::QOS_1);
    EXPECT_TRUE(result1.success);
    
    // QoS 2
    auto result2 = messageManager_->publish("test/qos2", "payload", QoS::QOS_2);
    EXPECT_TRUE(result2.success);
    EXPECT_EQ(messageManager_->getQueueSize(), 3U);
}

// 测试保留消息
TEST_F(MessageManagerTest, RetainedMessage) {
    auto result = messageManager_->publish(
        "test/retained",
        "retained payload",
        QoS::QOS_1,
        true  // retained
    );
    
    EXPECT_TRUE(result.success);
}

// 测试消息优先级
TEST_F(MessageManagerTest, MessagePriority) {
    // 低优先级
    auto resultLow = messageManager_->publish(
        "test/low",
        "payload",
        QoS::QOS_1,
        false,
        1  // 低优先级
    );
    
    // 高优先级
    auto resultHigh = messageManager_->publish(
        "test/high",
        "payload",
        QoS::QOS_1,
        false,
        9  // 高优先级
    );
    
    EXPECT_TRUE(resultLow.success);
    EXPECT_TRUE(resultHigh.success);
    EXPECT_EQ(messageManager_->getQueueSize(), 2U);
}
