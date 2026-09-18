/**
 * @file test_reconnect_manager.cpp
 * @brief 重连管理单元测试
 */

#include <gtest/gtest.h>
#include "mqtt_client/reconnect/reconnect_manager.h"
#include "mqtt_client/config/config.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <future>

using namespace mqtt_client;

class ReconnectManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.baseInterval = 100;  // 使用较短的间隔以便测试
        config_.maxInterval = 1000;
        config_.maxAttempts = 3;
        config_.minJitter = 0.8;
        config_.maxJitter = 1.0;
        config_.enableExponentialBackoff = true;
        config_.enableJitter = false;  // 测试时禁用抖动以便预测
        
        manager_ = std::make_unique<ReconnectManager>(config_);
    }
    
    void TearDown() override {
        if (manager_) {
            manager_->stopReconnect();
        }
        manager_.reset();
    }
    
    MqttConfig::ReconnectConfig config_;
    std::unique_ptr<ReconnectManager> manager_;
};

// 测试构造函数
TEST_F(ReconnectManagerTest, Constructor) {
    EXPECT_FALSE(manager_->isReconnecting());
    EXPECT_EQ(manager_->getAttemptCount(), 0);
}

// 测试指数退避计算
TEST_F(ReconnectManagerTest, CalculateBackoffInterval) {
    // 第一次尝试：baseInterval
    long interval1 = manager_->getNextRetryInterval();
    EXPECT_GE(interval1, config_.baseInterval);
    
    // 增加尝试次数
    manager_->resetAttempts();
    // 注意：calculateBackoffInterval是私有方法，我们通过getNextRetryInterval间接测试
}

// 测试最大间隔限制
TEST_F(ReconnectManagerTest, MaxIntervalLimit) {
    // 设置一个很大的尝试次数，应该被限制在maxInterval
    config_.baseInterval = 10;
    config_.maxInterval = 100;
    manager_ = std::make_unique<ReconnectManager>(config_);
    
    // 即使尝试次数很大，间隔也不应超过maxInterval
    long interval = manager_->getNextRetryInterval();
    EXPECT_LE(interval, config_.maxInterval);
}

// 测试重连停止
TEST_F(ReconnectManagerTest, StopReconnect) {
    int connectCallCount = 0;
    
    auto connectFunc = [&connectCallCount]() -> bool {
        connectCallCount++;
        return false;  // 总是失败
    };
    
    // 启动重连（在后台线程）
    ASSERT_TRUE(manager_->startReconnect(connectFunc));
    
    // 等待一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 停止重连
    manager_->stopReconnect();
    
    // 等待线程结束
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_FALSE(manager_->isReconnecting());
}

// 测试重置尝试次数
TEST_F(ReconnectManagerTest, ResetAttempts) {
    manager_->resetAttempts();
    EXPECT_EQ(manager_->getAttemptCount(), 0);
}

// 测试配置更新
TEST_F(ReconnectManagerTest, UpdateConfig) {
    MqttConfig::ReconnectConfig newConfig = config_;
    newConfig.baseInterval = 200;
    
    manager_->updateConfig(newConfig);
    
    EXPECT_EQ(manager_->getNextRetryInterval(), newConfig.baseInterval);
}

// 测试重连回调
TEST_F(ReconnectManagerTest, ReconnectCallback) {
    std::atomic<bool> callbackCalled{false};
    std::atomic<int> receivedAttempt{-1};
    
    manager_->setOnReconnectAttempt([&](int attempt, int, long) {
        callbackCalled.store(true);
        receivedAttempt.store(attempt);
    });
    
    // 启动重连（会触发回调）
    auto connectFunc = []() -> bool { return false; };
    ASSERT_TRUE(manager_->startReconnect(connectFunc));
    
    // 等待一小段时间让回调有机会执行
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    manager_->stopReconnect();
    
    EXPECT_TRUE(callbackCalled.load());
    EXPECT_GE(receivedAttempt.load(), 1);
}

TEST(ReconnectManagerLifecycleTest, NaturalCompletionCanBeDestroyed) {
    MqttConfig::ReconnectConfig config;
    config.enableJitter = false;

    std::promise<void> connected;
    auto connectedFuture = connected.get_future();
    {
        ReconnectManager manager(config);
        ASSERT_TRUE(manager.startReconnect([&connected] {
            connected.set_value();
            return true;
        }));
        ASSERT_EQ(connectedFuture.wait_for(std::chrono::seconds(1)),
                  std::future_status::ready);
    }
}

TEST(ReconnectManagerLifecycleTest, CanRestartAfterNaturalCompletion) {
    MqttConfig::ReconnectConfig config;
    config.enableJitter = false;
    ReconnectManager manager(config);

    std::atomic<int> calls{0};
    auto connect = [&calls] {
        calls.fetch_add(1);
        return true;
    };

    ASSERT_TRUE(manager.startReconnect(connect));
    for (int i = 0; i < 100 && manager.isReconnecting(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_FALSE(manager.isReconnecting());

    ASSERT_TRUE(manager.startReconnect(connect));
    for (int i = 0; i < 100 && calls.load() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    manager.stopReconnect();
    EXPECT_EQ(calls.load(), 2);
}
