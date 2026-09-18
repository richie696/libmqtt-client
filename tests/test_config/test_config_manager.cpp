/**
 * @file test_config_manager.cpp
 * @brief 配置管理单元测试
 */

#include <gtest/gtest.h>
#include "mqtt_client/config/config_manager.h"
#include "mqtt_client/config/config.h"
#include <fstream>
#include <memory>
#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "No filesystem support"
#endif

using namespace mqtt_client;

class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 清理测试文件
        testConfigFile_ = "/tmp/test_mqtt_config.json";
        testYamlConfigFile_ = "/tmp/test_mqtt_config.yaml";
        if (fs::exists(testConfigFile_)) {
            fs::remove(testConfigFile_);
        }
        if (fs::exists(testYamlConfigFile_)) {
            fs::remove(testYamlConfigFile_);
        }
    }
    
    void TearDown() override {
        // 清理测试文件
        if (fs::exists(testConfigFile_)) {
            fs::remove(testConfigFile_);
        }
        if (fs::exists(testYamlConfigFile_)) {
            fs::remove(testYamlConfigFile_);
        }
    }
    
    std::string testConfigFile_;
    std::string testYamlConfigFile_;
};

// 测试获取默认配置
TEST_F(ConfigManagerTest, GetDefaultConfig) {
    auto& manager = MqttConfigManager::getInstance();
    auto config = manager.getDefaultConfig();
    
    EXPECT_FALSE(config.basic.clientId.empty());
    EXPECT_GT(config.server.port, 0);
}

// 测试配置验证
TEST_F(ConfigManagerTest, ValidateConfig) {
    auto& manager = MqttConfigManager::getInstance();
    
    // 有效配置
    auto validConfig = manager.getDefaultConfig();
    validConfig.server.host = "mqtt.example.com";
    validConfig.server.port = 1883;
    
    auto result = manager.validate(validConfig);
    EXPECT_TRUE(result);
    
    // 无效配置（空主机）
    MqttConfig invalidConfig = validConfig;
    invalidConfig.server.host = "";
    
    auto invalidResult = manager.validate(invalidConfig);
    EXPECT_FALSE(invalidResult);
}

TEST_F(ConfigManagerTest, TlsValidationAlsoAppliesToUseSsl) {
    auto config = MqttConfigManager::getDefaultConfig();
    config.server.useSSL = true;
    config.security.enableTLS = false;
    config.security.verifyCertificate = true;

    EXPECT_FALSE(MqttConfigManager::validate(config));

    config.security.caCertificatePath = "/path/to/ca.pem";
    EXPECT_TRUE(MqttConfigManager::validate(config));

    config.security.clientCertificatePath = "/path/to/client.pem";
    EXPECT_FALSE(MqttConfigManager::validate(config));
}

TEST_F(ConfigManagerTest, IsValidMatchesManagerValidationForCoreRules) {
    auto config = MqttConfigManager::getDefaultConfig();
    config.server.host = "mqtt.example.com";
    config.server.keepAlive = 0;

    EXPECT_FALSE(config.isValid());
    EXPECT_FALSE(MqttConfigManager::validate(config));
}

// 测试从JSON字符串加载配置
TEST_F(ConfigManagerTest, LoadFromJson) {
    auto& manager = MqttConfigManager::getInstance();
    
    std::string jsonStr = R"({
        "basic": {
            "clientId": "test_client",
            "version": "5.0"
        },
        "server": {
            "host": "test.mqtt.com",
            "port": 1883
        }
    })";
    
    auto result = manager.loadFromJson(jsonStr);
    EXPECT_TRUE(result);
    
    auto config = manager.getCurrentConfig();
    EXPECT_EQ(config.basic.clientId, "test_client");
    EXPECT_EQ(config.server.host, "test.mqtt.com");
    EXPECT_EQ(config.server.port, 1883);
}

// 测试保存配置到文件
TEST_F(ConfigManagerTest, SaveToFile) {
    auto& manager = MqttConfigManager::getInstance();
    
    auto config = manager.getDefaultConfig();
    config.basic.clientId = "test_save_client";
    config.server.host = "save.test.com";
    
    auto result = manager.saveToFile(config, testConfigFile_);
    EXPECT_TRUE(result);
    
    // 验证文件存在
    EXPECT_TRUE(fs::exists(testConfigFile_));
}

// 测试从文件加载配置
TEST_F(ConfigManagerTest, LoadFromFile) {
    auto& manager = MqttConfigManager::getInstance();
    
    // 先保存一个配置
    auto config = manager.getDefaultConfig();
    config.basic.clientId = "test_load_client";
    config.server.host = "load.test.com";
    ASSERT_TRUE(manager.saveToFile(config, testConfigFile_));
    
    // 然后加载
    auto result = manager.loadFromFile(testConfigFile_);
    EXPECT_TRUE(result);
    
    auto loadedConfig = manager.getCurrentConfig();
    EXPECT_EQ(loadedConfig.basic.clientId, "test_load_client");
    EXPECT_EQ(loadedConfig.server.host, "load.test.com");
}

TEST_F(ConfigManagerTest, JsonRoundTripPreservesConfigurationGroups) {
    auto config = MqttConfigManager::getDefaultConfig();
    config.server.host = "roundtrip.example.com";
    config.basic.cleanStart = true;
    config.auth.token = "token-value";
    config.auth.useTokenAuth = true;
    config.mqtt5.receiveMaximum = 42;
    config.mqtt5.willMessage.enabled = true;
    config.mqtt5.willMessage.topic = "device/will";
    config.reconnect.maxAttempts = 7;
    config.monitoring.networkTimeout = 9;
    config.security.enableTLS = true;
    config.security.verifyCertificate = false;
    config.security.tlsVersion = "1.3";
    config.logging.maxFiles = 9;
    config.thread.totalThreads = 12;
    config.resource.memoryLimit = 123456;
    config.messageQueue.maxRetry = 8;
    config.persistence.storage.storageType = "sqlite";
    config.performance.batch.maxBatchSize = 33;
    config.metrics.reportEndpoint = "https://metrics.example.com";
    config.errorHandling.enableAutoRecover = true;
    config.qosPolicy.topicQoSMap["device/status"] = QoS::QOS_2;
    config.topics.customTopics["status"] = "device/status";

    const auto serialized = MqttConfigManager::saveToJson(config);
    ASSERT_TRUE(serialized);
    const auto parsed = MqttConfigManager::getInstance().loadFromJson(*serialized);
    ASSERT_TRUE(parsed);

    const auto& loaded = *parsed;
    EXPECT_EQ(loaded.server.host, config.server.host);
    EXPECT_TRUE(loaded.basic.cleanStart);
    EXPECT_EQ(loaded.auth.token, config.auth.token);
    EXPECT_TRUE(loaded.auth.useTokenAuth);
    EXPECT_EQ(loaded.mqtt5.receiveMaximum, 42);
    EXPECT_TRUE(loaded.mqtt5.willMessage.enabled);
    EXPECT_EQ(loaded.reconnect.maxAttempts, 7);
    EXPECT_EQ(loaded.monitoring.networkTimeout, 9);
    EXPECT_EQ(loaded.security.tlsVersion, "1.3");
    EXPECT_EQ(loaded.logging.maxFiles, 9);
    EXPECT_EQ(loaded.thread.totalThreads, 12);
    EXPECT_EQ(loaded.resource.memoryLimit, 123456U);
    EXPECT_EQ(loaded.messageQueue.maxRetry, 8);
    EXPECT_EQ(loaded.persistence.storage.storageType, "sqlite");
    EXPECT_EQ(loaded.performance.batch.maxBatchSize, 33U);
    EXPECT_EQ(loaded.metrics.reportEndpoint, "https://metrics.example.com");
    EXPECT_TRUE(loaded.errorHandling.enableAutoRecover);
    EXPECT_EQ(loaded.qosPolicy.topicQoSMap.at("device/status"), QoS::QOS_2);
    EXPECT_EQ(loaded.topics.customTopics.at("status"), "device/status");
}

TEST_F(ConfigManagerTest, YamlFileRoundTripUpdatesCurrentConfig) {
    auto config = MqttConfigManager::getDefaultConfig();
    config.server.host = "yaml.example.com";
    config.monitoring.networkTimeout = 11;
    config.mqtt5.receiveMaximum = 42;
    config.auth.token = "yaml-token";
    config.metrics.reportEndpoint = "https://metrics.example.com";

    ASSERT_TRUE(MqttConfigManager::getInstance().saveToFile(config, testYamlConfigFile_));
    const auto result = MqttConfigManager::getInstance().loadFromFile(testYamlConfigFile_);
    ASSERT_TRUE(result) << result.error.message;
    EXPECT_EQ(result->server.host, "yaml.example.com");
    EXPECT_EQ(result->monitoring.networkTimeout, 11);
    EXPECT_EQ(result->mqtt5.receiveMaximum, 42);
    EXPECT_EQ(result->auth.token, "yaml-token");
    EXPECT_EQ(result->metrics.reportEndpoint, "https://metrics.example.com");
    EXPECT_EQ(MqttConfigManager::getInstance().getCurrentConfig().server.host,
              "yaml.example.com");
}

// 测试配置合并
TEST_F(ConfigManagerTest, MergeConfig) {
    auto& manager = MqttConfigManager::getInstance();
    
    auto baseConfig = manager.getDefaultConfig();
    baseConfig.basic.clientId = "base_client";
    baseConfig.server.host = "base.com";
    baseConfig.basic.cleanStart = true;
    baseConfig.server.useSSL = true;
    baseConfig.logging.level = LogLevel::DEBUG;
    
    MqttConfig overrideConfig;
    overrideConfig.basic.clientId = "override_client";
    // server.host 不设置，应该保持base的值
    
    auto merged = manager.merge(baseConfig, overrideConfig);
    
    EXPECT_EQ(merged.basic.clientId, "override_client");
    EXPECT_EQ(merged.server.host, "base.com");  // 应该保持base的值
    EXPECT_TRUE(merged.basic.cleanStart);
    EXPECT_TRUE(merged.server.useSSL);
    EXPECT_EQ(merged.logging.level, LogLevel::DEBUG);
}

// 测试配置更新回调
TEST_F(ConfigManagerTest, UpdateCallback) {
    auto& manager = MqttConfigManager::getInstance();
    
    struct CallbackState {
        bool called = false;
        MqttConfig receivedConfig;
    };
    auto callbackState = std::make_shared<CallbackState>();

    auto config = manager.getDefaultConfig();
    config.logging.level = LogLevel::INFO;
    manager.setCurrentConfig(config);
    
    manager.registerUpdateCallback([callbackState](const MqttConfig& updatedConfig) {
        callbackState->called = true;
        callbackState->receivedConfig = updatedConfig;
    });
    
    // 执行热更新（只更新支持热更新的配置项）
    config.logging.level = LogLevel::DEBUG;  // 日志级别支持热更新
    auto result = manager.hotUpdate(config);
    
    // 验证热更新成功
    EXPECT_TRUE(result);
    
    EXPECT_TRUE(callbackState->called);
    EXPECT_EQ(callbackState->receivedConfig.logging.level, LogLevel::DEBUG);
}
