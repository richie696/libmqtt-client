/**
 * @file test_network_monitor.cpp
 * @brief 网络监控单元测试
 */

#include <gtest/gtest.h>
#include "internal/monitor/network_monitor.h"
#include <thread>
#include <chrono>

using namespace mqtt_client;

class NetworkMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        monitor_ = std::make_unique<NetworkMonitor>("test.mqtt.com", 1883, 1);
    }
    
    void TearDown() override {
        if (monitor_ && monitor_->isRunning()) {
            monitor_->stop();
        }
        monitor_.reset();
    }
    
    std::unique_ptr<NetworkMonitor> monitor_;
};

// 测试构造函数
TEST_F(NetworkMonitorTest, Constructor) {
    EXPECT_FALSE(monitor_->isRunning());
}

// 测试启动和停止
TEST_F(NetworkMonitorTest, StartStop) {
    monitor_->start();
    EXPECT_TRUE(monitor_->isRunning());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    monitor_->stop();
    EXPECT_FALSE(monitor_->isRunning());
}

// 测试获取网络统计信息
TEST_F(NetworkMonitorTest, GetStats) {
    auto stats = monitor_->getStats();
    
    EXPECT_FALSE(stats.available);
    EXPECT_EQ(stats.latency, -1);
    EXPECT_EQ(stats.lastCheckTime, 0);
}

// 测试获取网络质量
TEST_F(NetworkMonitorTest, GetNetworkQuality) {
    NetworkQuality quality = monitor_->getQuality();
    
    // 质量应该是有效值
    EXPECT_TRUE(quality >= NetworkQuality::EXCELLENT && 
                quality <= NetworkQuality::POOR);
}

// 测试网络恢复回调
TEST_F(NetworkMonitorTest, NetworkRecoveredCallback) {
    bool callbackCalled = false;
    
    monitor_->setOnNetworkRecovered([&]() {
        callbackCalled = true;
    });
    
    EXPECT_FALSE(callbackCalled);
}

// 测试网络丢失回调
TEST_F(NetworkMonitorTest, NetworkLostCallback) {
    bool callbackCalled = false;
    
    monitor_->setOnNetworkLost([&]() {
        callbackCalled = true;
    });
    
    EXPECT_FALSE(callbackCalled);
}

// 测试网络质量变化回调
TEST_F(NetworkMonitorTest, QualityChangedCallback) {
    bool callbackCalled = false;
    NetworkQuality receivedQuality = NetworkQuality::EXCELLENT;
    
    monitor_->setOnQualityChanged([&](NetworkQuality quality) {
        callbackCalled = true;
        receivedQuality = quality;
    });
    
    EXPECT_FALSE(callbackCalled);
    EXPECT_EQ(receivedQuality, NetworkQuality::EXCELLENT);
}

// 测试监控线程生命周期
TEST_F(NetworkMonitorTest, MonitorThreadLifecycle) {
    monitor_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 监控应该正在运行
    EXPECT_TRUE(monitor_->isRunning());
    
    monitor_->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 监控应该已停止
    EXPECT_FALSE(monitor_->isRunning());
}
