/**
 * @file test_result.cpp
 * @brief Result类型单元测试
 */

#include <gtest/gtest.h>
#include "mqtt_client/core/result.h"
#include "mqtt_client/core/error.h"

using namespace mqtt_client;

class ResultTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 测试成功结果
TEST_F(ResultTest, SuccessResult) {
    auto result = Result<int>::Success(42);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(*result, 42);
    EXPECT_TRUE(static_cast<bool>(result));
}

// 测试失败结果
TEST_F(ResultTest, FailureResult) {
    MqttError error(MqttErrorCode::NETWORK_ERROR, "Network error");
    auto result = Result<int>::Failure(error);
    
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error.code, MqttErrorCode::NETWORK_ERROR);
    EXPECT_EQ(result.error.message, "Network error");
}

// 测试value_or方法（如果实现）
TEST_F(ResultTest, ValueOr) {
    // 成功情况
    auto success = Result<int>::Success(42);
    if (success) {
        EXPECT_EQ(*success, 42);
    }
    
    // 失败情况
    MqttError error(MqttErrorCode::NETWORK_ERROR, "Network error");
    auto failure = Result<int>::Failure(error);
    EXPECT_FALSE(failure);
}

// 测试Result<void>
TEST_F(ResultTest, VoidResult) {
    // 成功
    auto success = Result<void>::Success();
    EXPECT_TRUE(success.success);
    EXPECT_TRUE(static_cast<bool>(success));
    
    // 失败
    MqttError error(MqttErrorCode::NETWORK_ERROR, "Network error");
    auto failure = Result<void>::Failure(error);
    EXPECT_FALSE(failure.success);
    EXPECT_FALSE(static_cast<bool>(failure));
}

// 测试字符串结果
TEST_F(ResultTest, StringResult) {
    auto result = Result<std::string>::Success("hello");
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(*result, "hello");
}

// 测试移动语义
TEST_F(ResultTest, MoveSemantics) {
    auto result1 = Result<std::string>::Success("hello");
    auto result2 = std::move(result1);
    
    EXPECT_TRUE(result2.success);
    EXPECT_EQ(*result2, "hello");
}
