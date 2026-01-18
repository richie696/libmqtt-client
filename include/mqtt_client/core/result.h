/**
 * @file result.h
 * @brief Result类型定义
 * 
 * 定义统一的返回类型，包含成功/失败状态和错误信息。
 * 遵循嵌入式系统不使用异常的最佳实践。
 */

#ifndef MQTT_CLIENT_CORE_RESULT_H
#define MQTT_CLIENT_CORE_RESULT_H

#include "mqtt_client/core/error.h"
#include <type_traits>

namespace mqtt_client {

/**
 * @brief 统一的返回类型
 * 
 * 用于所有可能失败的操作，避免使用C++异常。
 * 
 * @tparam T 返回值类型
 */
template<typename T>
struct Result {
    bool success;           ///< 是否成功
    T value;                ///< 返回值（成功时有效）
    MqttError error;        ///< 错误信息（失败时有效）
    
    /**
     * @brief 默认构造函数（失败状态）
     */
    Result() : success(false), value(), error(MqttErrorCode::UNKNOWN_ERROR, "Unknown error") {}
    
    /**
     * @brief 成功构造函数
     */
    static Result Success(const T& val) {
        Result result;
        result.success = true;
        result.value = val;
        result.error = MqttError(MqttErrorCode::SUCCESS, "");
        return result;
    }
    
    /**
     * @brief 成功构造函数（移动语义）
     */
    static Result Success(T&& val) {
        Result result;
        result.success = true;
        result.value = std::move(val);
        result.error = MqttError(MqttErrorCode::SUCCESS, "");
        return result;
    }
    
    /**
     * @brief 失败构造函数
     */
    static Result Failure(MqttErrorCode code, const std::string& message, const std::string& details = "") {
        Result result;
        result.success = false;
        result.error = MqttError(code, message, details);
        return result;
    }
    
    /**
     * @brief 失败构造函数（使用MqttError）
     */
    static Result Failure(const MqttError& err) {
        Result result;
        result.success = false;
        result.error = err;
        return result;
    }
    
    /**
     * @brief 失败构造函数（使用错误码和消息）
     */
    static Result Failure(MqttErrorCode code, const std::string& message) {
        return Failure(code, message, "");
    }
    
    /**
     * @brief bool转换运算符
     */
    explicit operator bool() const noexcept {
        return success;
    }
    
    /**
     * @brief 解引用运算符（const）
     */
    const T& operator*() const {
        return value;
    }
    
    /**
     * @brief 解引用运算符
     */
    T& operator*() {
        return value;
    }
    
    /**
     * @brief 成员访问运算符（const）
     */
    const T* operator->() const {
        return &value;
    }
    
    /**
     * @brief 成员访问运算符
     */
    T* operator->() {
        return &value;
    }
};

/**
 * @brief Result<void>特化（用于无返回值的情况）
 */
template<>
struct Result<void> {
    bool success;           ///< 是否成功
    MqttError error;        ///< 错误信息（失败时有效）
    
    /**
     * @brief 默认构造函数（失败状态）
     */
    Result() : success(false), error(MqttErrorCode::UNKNOWN_ERROR, "Unknown error") {}
    
    /**
     * @brief 成功构造函数
     */
    static Result Success() {
        Result result;
        result.success = true;
        result.error = MqttError(MqttErrorCode::SUCCESS, "");
        return result;
    }
    
    /**
     * @brief 失败构造函数
     */
    static Result Failure(MqttErrorCode code, const std::string& message, const std::string& details = "") {
        Result result;
        result.success = false;
        result.error = MqttError(code, message, details);
        return result;
    }
    
    /**
     * @brief 失败构造函数（使用MqttError）
     */
    static Result Failure(const MqttError& err) {
        Result result;
        result.success = false;
        result.error = err;
        return result;
    }
    
    /**
     * @brief bool转换运算符
     */
    explicit operator bool() const noexcept {
        return success;
    }
};

// 类型别名，方便使用
using ResultVoid = Result<void>;
using ResultBool = Result<bool>;
using ResultInt = Result<int>;
using ResultString = Result<std::string>;

} // namespace mqtt_client

#endif // MQTT_CLIENT_CORE_RESULT_H
