/**
 * @file logger_manager.cpp
 * @brief 日志管理器实现
 */

#include "mqtt_client/logger/logger_interface.h"
#include <iostream>

namespace mqtt_client {

LoggerManager& LoggerManager::getInstance() {
    static LoggerManager instance;
    return instance;
}

void LoggerManager::setLogger(const std::shared_ptr<ILogger> &logger) {
    std::lock_guard lock(mutex_);
    logger_ = logger;
}

std::shared_ptr<ILogger> LoggerManager::getLogger() const {
    std::lock_guard lock(mutex_);
    return logger_;
}

void LoggerManager::log(LogLevel level,
                        std::string_view file,
                        const int line,
                        std::string_view function,
                        std::string_view message) const {
    // 使用try_to_lock避免在静态销毁时阻塞
    const std::unique_lock lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() && logger_) {
        logger_->log(level, std::string(file), line, std::string(function), std::string(message));
    } else {
        // 如果没有设置日志器，或者在析构时无法获取锁，输出到标准错误流
        std::cerr << "[" << static_cast<int>(level) << "] " 
                  << file << ":" << line << " " << function 
                  << " - " << message << std::endl;
    }
}

void LoggerManager::flush() const {
    std::lock_guard lock(mutex_);
    if (logger_) {
        logger_->flush();
    }
}

bool LoggerManager::hasLogger() const {
    std::lock_guard lock(mutex_);
    return logger_ != nullptr;
}

bool LoggerManager::shouldLog(LogLevel level) const {
    // 快速检查，无锁（使用 try_to_lock 避免阻塞）
    const std::unique_lock lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() && logger_) {
        return logger_->getLevel() <= level;
    }
    // 如果没有日志器或无法获取锁，默认返回 true（输出到 stderr）
    return true;
}

} // namespace mqtt_client
