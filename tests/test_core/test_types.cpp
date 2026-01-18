/**
 * @file test_types.cpp
 * @brief 基础类型单元测试
 */

#include <gtest/gtest.h>
#include "mqtt_client/core/types.h"

using namespace mqtt_client;

class TypesTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 测试QoS枚举
TEST_F(TypesTest, QoSEnum) {
    EXPECT_EQ(static_cast<uint8_t>(QoS::QOS_0), 0);
    EXPECT_EQ(static_cast<uint8_t>(QoS::QOS_1), 1);
    EXPECT_EQ(static_cast<uint8_t>(QoS::QOS_2), 2);
}

// 测试连接状态枚举
TEST_F(TypesTest, ConnectionStateEnum) {
    ConnectionState state = ConnectionState::DISCONNECTED;
    EXPECT_EQ(state, ConnectionState::DISCONNECTED);
    
    state = ConnectionState::CONNECTED;
    EXPECT_EQ(state, ConnectionState::CONNECTED);
}

// 测试网络质量枚举
TEST_F(TypesTest, NetworkQualityEnum) {
    NetworkQuality quality = NetworkQuality::EXCELLENT;
    EXPECT_EQ(quality, NetworkQuality::EXCELLENT);
    
    quality = NetworkQuality::POOR;
    EXPECT_EQ(quality, NetworkQuality::POOR);
}

// 测试MqttProperties
TEST_F(TypesTest, MqttProperties) {
    MqttProperties props;
    
    // 测试设置和获取用户属性
    props.setUserProperty("key1", "value1");
    auto value = props.getUserProperty("key1");
    
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "value1");
    
    // 测试不存在的属性
    auto notFound = props.getUserProperty("nonexistent");
    EXPECT_FALSE(notFound.has_value());
    
    // 测试isEmpty
    MqttProperties emptyProps;
    EXPECT_TRUE(emptyProps.isEmpty());
    
    emptyProps.setUserProperty("key", "value");
    EXPECT_FALSE(emptyProps.isEmpty());
}

// 测试MqttMessage
TEST_F(TypesTest, MqttMessage) {
    MqttMessage msg;
    msg.topic = "test/topic";
    msg.payload = "test payload";
    msg.qos = QoS::QOS_1;
    msg.retain = true;
    
    EXPECT_EQ(msg.topic, "test/topic");
    EXPECT_EQ(msg.payload, "test payload");
    EXPECT_EQ(msg.qos, QoS::QOS_1);
    EXPECT_TRUE(msg.retain);
}

// 测试MqttProperties的messageExpiryInterval
TEST_F(TypesTest, MqttPropertiesExpiryInterval) {
    MqttProperties props;
    props.messageExpiryInterval = 3600;
    
    EXPECT_TRUE(props.messageExpiryInterval.has_value());
    EXPECT_EQ(props.messageExpiryInterval.value(), 3600);
}

// 测试MqttProperties的contentType
TEST_F(TypesTest, MqttPropertiesContentType) {
    MqttProperties props;
    props.contentType = "application/json";
    
    EXPECT_TRUE(props.contentType.has_value());
    EXPECT_EQ(props.contentType.value(), "application/json");
}
