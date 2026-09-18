/**
 * @file test_subscription_manager.cpp
 * @brief 订阅管理单元测试
 */

#include <gtest/gtest.h>
#include "internal/subscription/subscription_manager.h"
#include "internal/connection/connection_manager.h"
#include "mqtt_client/config/config.h"
#include "mqtt_client/config/config_manager.h"
#include "internal/adapter/wolfmqtt_adapter.h"
#include <memory>

using namespace mqtt_client;

class SubscriptionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = MqttConfigManager::getInstance().getDefaultConfig();
        config_.server.host = "test.mqtt.com";
        config_.server.port = 1883;
        config_.basic.clientId = "test_client";
        
        // 创建适配器和连接管理器
        adapter_ = std::make_unique<WolfMqttAdapter>(config_);
        connectionManager_ = std::make_unique<MqttConnectionManager>(config_, adapter_.get());
        
        // 创建订阅管理器
        subscriptionManager_ = std::make_unique<MqttSubscriptionManager>(*connectionManager_, config_);
    }
    
    void TearDown() override {
        subscriptionManager_.reset();
        connectionManager_.reset();
        adapter_.reset();
    }
    
    MqttConfig config_;
    std::unique_ptr<WolfMqttAdapter> adapter_;
    std::unique_ptr<MqttConnectionManager> connectionManager_;
    std::unique_ptr<MqttSubscriptionManager> subscriptionManager_;
};

// 测试构造函数
TEST_F(SubscriptionManagerTest, Constructor) {
    EXPECT_EQ(subscriptionManager_->getSubscriptionCount(), 0);
    EXPECT_TRUE(subscriptionManager_->getSubscribedTopics().empty());
}

// 测试订阅主题
TEST_F(SubscriptionManagerTest, Subscribe) {
    bool callbackCalled = false;
    std::string receivedTopic;
    std::string receivedPayload;
    
    MessageCallback callback = [&](std::string_view topic,
                                   std::string_view payload,
                                   const MqttProperties&) {
        callbackCalled = true;
        receivedTopic = std::string(topic);
        receivedPayload = std::string(payload);
    };
    
    auto result = subscriptionManager_->subscribe(
        "test/topic",
        callback,
        QoS::QOS_1
    );
    
    ASSERT_TRUE(result.success);
    EXPECT_TRUE(subscriptionManager_->isSubscribed("test/topic"));
}

// 测试取消订阅
TEST_F(SubscriptionManagerTest, Unsubscribe) {
    // 先订阅
    MessageCallback callback = [](std::string_view, std::string_view, const MqttProperties&) {};
    ASSERT_TRUE(subscriptionManager_->subscribe("test/topic", callback, QoS::QOS_1));
    
    // 取消订阅
    auto result = subscriptionManager_->unsubscribe("test/topic");
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(subscriptionManager_->isSubscribed("test/topic"));
}

// 测试取消所有订阅
TEST_F(SubscriptionManagerTest, UnsubscribeAll) {
    // 添加多个订阅
    MessageCallback callback = [](std::string_view, std::string_view, const MqttProperties&) {};
    ASSERT_TRUE(subscriptionManager_->subscribe("test/topic1", callback, QoS::QOS_1));
    ASSERT_TRUE(subscriptionManager_->subscribe("test/topic2", callback, QoS::QOS_1));
    
    // 取消所有订阅
    subscriptionManager_->unsubscribeAll();
    
    // 订阅数量应该为0
    EXPECT_EQ(subscriptionManager_->getSubscriptionCount(), 0);
}

// 测试检查是否已订阅
TEST_F(SubscriptionManagerTest, IsSubscribed) {
    MessageCallback callback = [](std::string_view, std::string_view, const MqttProperties&) {};
    
    // 订阅前应该未订阅
    EXPECT_FALSE(subscriptionManager_->isSubscribed("test/topic"));
    
    // 订阅后应该已订阅（如果连接成功）
    ASSERT_TRUE(subscriptionManager_->subscribe("test/topic", callback, QoS::QOS_1));
    EXPECT_TRUE(subscriptionManager_->isSubscribed("test/topic"));
}

// 测试获取已订阅主题列表
TEST_F(SubscriptionManagerTest, GetSubscribedTopics) {
    MessageCallback callback = [](std::string_view, std::string_view, const MqttProperties&) {};
    
    // 初始应该为空
    auto topics = subscriptionManager_->getSubscribedTopics();
    EXPECT_TRUE(topics.empty());
    
    // 添加订阅
    ASSERT_TRUE(subscriptionManager_->subscribe("test/topic1", callback, QoS::QOS_1));
    ASSERT_TRUE(subscriptionManager_->subscribe("test/topic2", callback, QoS::QOS_1));
    
    // 获取主题列表
    topics = subscriptionManager_->getSubscribedTopics();
    EXPECT_EQ(topics.size(), 2U);
}

// 测试恢复订阅
TEST_F(SubscriptionManagerTest, ResubscribeAll) {
    MessageCallback callback = [](std::string_view, std::string_view, const MqttProperties&) {};
    
    // 添加持久化订阅
    ASSERT_TRUE(subscriptionManager_->subscribe("test/persistent", callback, QoS::QOS_1));
    
    // 恢复订阅
    auto result = subscriptionManager_->resubscribeAll();
    EXPECT_FALSE(result.success);
}

// 测试消息分发
TEST_F(SubscriptionManagerTest, DispatchMessage) {
    bool callbackCalled = false;
    std::string receivedTopic;
    std::string receivedPayload;
    
    MessageCallback callback = [&](std::string_view topic,
                                   std::string_view payload,
                                   const MqttProperties&) {
        callbackCalled = true;
        receivedTopic = std::string(topic);
        receivedPayload = std::string(payload);
    };
    
    // 订阅主题
    ASSERT_TRUE(subscriptionManager_->subscribe("test/dispatch", callback, QoS::QOS_1));
    
    // 分发消息
    MqttProperties properties;
    subscriptionManager_->dispatchMessage("test/dispatch", "test payload", properties);

    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(receivedTopic, "test/dispatch");
    EXPECT_EQ(receivedPayload, "test payload");
}

// 测试通配符订阅
TEST_F(SubscriptionManagerTest, WildcardSubscription) {
    int singleLevelCalls = 0;
    int multiLevelCalls = 0;
    
    // 单级通配符
    auto result1 = subscriptionManager_->subscribe(
        "test/+/status",
        [&](std::string_view, std::string_view, const MqttProperties&) {
            ++singleLevelCalls;
        },
        QoS::QOS_1);
    ASSERT_TRUE(result1.success);
    
    // 多级通配符
    auto result2 = subscriptionManager_->subscribe(
        "test/#",
        [&](std::string_view, std::string_view, const MqttProperties&) {
            ++multiLevelCalls;
        },
        QoS::QOS_1);
    ASSERT_TRUE(result2.success);

    subscriptionManager_->dispatchMessage("test/device/status", "online");
    EXPECT_EQ(singleLevelCalls, 1);
    EXPECT_EQ(multiLevelCalls, 1);
}

// 测试不同QoS级别的订阅
TEST_F(SubscriptionManagerTest, DifferentQoSLevels) {
    MessageCallback callback = [](std::string_view, std::string_view, const MqttProperties&) {};
    
    // QoS 0
    auto result0 = subscriptionManager_->subscribe("test/qos0", callback, QoS::QOS_0);
    EXPECT_TRUE(result0.success);
    
    // QoS 1
    auto result1 = subscriptionManager_->subscribe("test/qos1", callback, QoS::QOS_1);
    EXPECT_TRUE(result1.success);
    
    // QoS 2
    auto result2 = subscriptionManager_->subscribe("test/qos2", callback, QoS::QOS_2);
    EXPECT_TRUE(result2.success);
}
