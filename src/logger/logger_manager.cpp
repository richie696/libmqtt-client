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

void LoggerManager::setLogger(std::shared_ptr<ILogger> logger) {
    std::lock_guard<std::mutex> lock(mutex_);
    logger_ = logger;
}

std::shared_ptr<ILogger> LoggerManager::getLogger() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logger_;
}

void LoggerManager::log(LogLevel level,
                        const std::string& file,
                        int line,
                        const std::string& function,
                        const std::string& message) {
    // 使用try_to_lock避免在静态销毁时阻塞
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() && logger_) {
        logger_->log(level, file, line, function, message);
    } else {
        // 如果没有设置日志器，或者在析构时无法获取锁，输出到标准错误流
        std::cerr << "[" << static_cast<int>(level) << "] " 
                  << file << ":" << line << " " << function 
                  << " - " << message << std::endl;
    }
}

void LoggerManager::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logger_) {
        logger_->flush();
    }
}

bool LoggerManager::hasLogger() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logger_ != nullptr;
}

} // namespace mqtt_client
