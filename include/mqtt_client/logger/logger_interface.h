/**
 * @file logger_interface.h
 * @brief 日志接口定义
 * 
 * 定义可插拔的日志接口，遵循依赖倒置原则（DIP）。
 * 用户可以实现自定义日志（如云日志服务）。
 */

#ifndef MQTT_CLIENT_LOGGER_LOGGER_INTERFACE_H
#define MQTT_CLIENT_LOGGER_LOGGER_INTERFACE_H

#include "mqtt_client/core/types.h"
#include <string_view>
#include <memory>
#include <mutex>

namespace mqtt_client {

/**
 * @brief 日志接口（抽象基类）
 * 
 * 遵循接口隔离原则（ISP），只包含日志相关的核心方法。
 * 用户可以实现此接口，提供自定义日志实现（如云日志服务）。
 */
class ILogger {
public:
    virtual ~ILogger() = default;
    
    /**
     * @brief 记录日志
     * 
     * @param level 日志级别
     * @param file 文件名
     * @param line 行号
     * @param function 函数名
     * @param message 日志消息
     */
    virtual void log(LogLevel level,
                     std::string_view file,
                     int line,
                     std::string_view function,
                     std::string_view message) = 0;
    
    /**
     * @brief 刷新日志缓冲区
     * 
     * 对于有缓冲的日志实现，刷新缓冲区确保日志被写入。
     */
    virtual void flush() = 0;
    
    /**
     * @brief 设置日志级别
     * 
     * @param level 日志级别
     */
    virtual void setLevel(LogLevel level) = 0;
    
    /**
     * @brief 获取日志级别
     * 
     * @return LogLevel 当前日志级别
     */
    [[nodiscard]] virtual LogLevel getLevel() const = 0;
    
    /**
     * @brief 关闭日志（清理资源）
     * 
     * 关闭日志，释放资源。
     */
    virtual void close() = 0;
};

/**
 * @brief 日志管理器（单例）
 * 
 * 管理日志实现的注册和分发。
 * 遵循单例模式，确保全局唯一。
 */
class LoggerManager {
public:
    /**
     * @brief 获取单例实例
     * 
     * @return LoggerManager& 单例引用
     */
    static LoggerManager& getInstance();
    
    /**
     * @brief 设置日志器
     * 
     * 设置自定义日志实现。
     * 
     * @param logger 日志器实例（使用shared_ptr管理生命周期）
     */
    void setLogger(const std::shared_ptr<ILogger> &logger);
    
    /**
     * @brief 获取当前日志器
     * 
     * @return std::shared_ptr<ILogger> 日志器实例
     */
    std::shared_ptr<ILogger> getLogger() const;
    
    /**
     * @brief 记录日志
     * 
     * 便捷方法，内部调用当前日志器的log方法。
     * 
     * @param level 日志级别
     * @param file 文件名
     * @param line 行号
     * @param function 函数名
     * @param message 日志消息
     */
    void log(LogLevel level,
             std::string_view file,
             int line,
             std::string_view function,
             std::string_view message) const;
    
    /**
     * @brief 刷新日志
     */
    void flush() const;
    
    /**
     * @brief 检查是否有日志器
     * 
     * @return true 已设置日志器
     * @return false 未设置日志器
     */
    bool hasLogger() const;
    
    /**
     * @brief 快速检查日志级别（无锁，用于性能优化）
     * 
     * 用于日志宏中快速检查是否应该记录日志，避免不必要的字符串构造
     * 
     * @param level 日志级别
     * @return true 应该记录
     * @return false 不需要记录
     */
    [[nodiscard]] bool shouldLog(LogLevel level) const;

private:
    LoggerManager() = default;
    ~LoggerManager() = default;
    LoggerManager(const LoggerManager&) = delete;
    LoggerManager& operator=(const LoggerManager&) = delete;
    
    std::shared_ptr<ILogger> logger_;
    mutable std::mutex mutex_;
};

} // namespace mqtt_client

// 便捷日志宏（性能优化：延迟求值，只在日志级别满足时才构造字符串）
// 使用 shouldLog 快速检查，避免在日志级别关闭时构造字符串参数
#define LOG_TRACE(msg) \
    do { \
        auto& loggerMgr = mqtt_client::LoggerManager::getInstance(); \
        if (loggerMgr.shouldLog(mqtt_client::LogLevel::TRACE)) { \
            loggerMgr.log(mqtt_client::LogLevel::TRACE, __FILE__, __LINE__, __FUNCTION__, \
                [&]() -> std::string { return std::string(msg); }()); \
        } \
    } while(0)

#define LOG_DEBUG(msg) \
    do { \
        auto& loggerMgr = mqtt_client::LoggerManager::getInstance(); \
        if (loggerMgr.shouldLog(mqtt_client::LogLevel::DEBUG)) { \
            loggerMgr.log(mqtt_client::LogLevel::DEBUG, __FILE__, __LINE__, __FUNCTION__, \
                [&]() -> std::string { return std::string(msg); }()); \
        } \
    } while(0)

#define LOG_INFO(msg) \
    do { \
        auto& loggerMgr = mqtt_client::LoggerManager::getInstance(); \
        if (loggerMgr.shouldLog(mqtt_client::LogLevel::INFO)) { \
            loggerMgr.log(mqtt_client::LogLevel::INFO, __FILE__, __LINE__, __FUNCTION__, \
                [&]() -> std::string { return std::string(msg); }()); \
        } \
    } while(0)

#define LOG_WARN(msg) \
    do { \
        auto& loggerMgr = mqtt_client::LoggerManager::getInstance(); \
        if (loggerMgr.shouldLog(mqtt_client::LogLevel::WARN)) { \
            loggerMgr.log(mqtt_client::LogLevel::WARN, __FILE__, __LINE__, __FUNCTION__, \
                [&]() -> std::string { return std::string(msg); }()); \
        } \
    } while(0)

#define LOG_ERROR(msg) \
    do { \
        auto& loggerMgr = mqtt_client::LoggerManager::getInstance(); \
        if (loggerMgr.shouldLog(mqtt_client::LogLevel::ERROR)) { \
            loggerMgr.log(mqtt_client::LogLevel::ERROR, __FILE__, __LINE__, __FUNCTION__, \
                [&]() -> std::string { return std::string(msg); }()); \
        } \
    } while(0)

#define LOG_FATAL(msg) \
    do { \
        auto& loggerMgr = mqtt_client::LoggerManager::getInstance(); \
        if (loggerMgr.shouldLog(mqtt_client::LogLevel::FATAL)) { \
            loggerMgr.log(mqtt_client::LogLevel::FATAL, __FILE__, __LINE__, __FUNCTION__, \
                [&]() -> std::string { return std::string(msg); }()); \
        } \
    } while(0)

#endif // MQTT_CLIENT_LOGGER_LOGGER_INTERFACE_H
