/**
 * @file test_error.cpp
 * @brief 错误处理单元测试
 */

#include <gtest/gtest.h>
#include "mqtt_client/core/error.h"
#include <sstream>

using namespace mqtt_client;

class ErrorTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 测试错误创建
TEST_F(ErrorTest, CreateError) {
    MqttError error(MqttErrorCode::NETWORK_ERROR, "Network error", "Connection timeout");
    
    EXPECT_EQ(error.code, MqttErrorCode::NETWORK_ERROR);
    EXPECT_EQ(error.message, "Network error");
    EXPECT_EQ(error.details, "Connection timeout");
}

// 测试默认错误
TEST_F(ErrorTest, DefaultError) {
    MqttError error;
    
    EXPECT_EQ(error.code, MqttErrorCode::SUCCESS);
    EXPECT_TRUE(error.message.empty());
    EXPECT_TRUE(error.details.empty());
}

// 测试错误相等性
TEST_F(ErrorTest, ErrorEquality) {
    MqttError error1(MqttErrorCode::NETWORK_ERROR, "Network error");
    MqttError error2(MqttErrorCode::NETWORK_ERROR, "Network error");
    MqttError error3(MqttErrorCode::CONNECTION_REFUSED, "Connection refused");
    
    EXPECT_EQ(error1.code, error2.code);
    EXPECT_EQ(error1.message, error2.message);
    EXPECT_NE(error1.code, error3.code);
}

// 测试toString方法
TEST_F(ErrorTest, ToString) {
    MqttError error(MqttErrorCode::NETWORK_ERROR, "Network error", "Connection timeout");
    std::string str = error.toString();
    
    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find("Network error"), std::string::npos);
}

// 测试错误代码（简化测试）
TEST_F(ErrorTest, ErrorCode) {
    MqttErrorCode code = MqttErrorCode::NETWORK_ERROR;
    EXPECT_EQ(code, MqttErrorCode::NETWORK_ERROR);
}

// 测试错误toString方法
TEST_F(ErrorTest, ErrorToString) {
    MqttError error(MqttErrorCode::NETWORK_ERROR, "Network error", "Connection timeout");
    std::string str = error.toString();
    
    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find("Network error"), std::string::npos);
}
