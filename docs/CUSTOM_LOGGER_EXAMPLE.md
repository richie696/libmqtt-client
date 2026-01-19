# 自定义日志实现示例

本文档展示如何实现自定义日志器（以腾讯云 CLS 为例）并挂载到 MQTT 客户端库中。

## 实现步骤

### 1. 实现 ILogger 接口

创建一个继承自 `mqtt_client::ILogger` 的类，实现所有纯虚函数：

```cpp
#include "mqtt_client/logger/logger_interface.h"
#include "mqtt_client/core/types.h"
#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>

// 腾讯云 CLS 日志器实现示例
class TencentCloudCLSLogger : public mqtt_client::ILogger {
public:
    TencentCloudCLSLogger(const std::string& secretId, 
                         const std::string& secretKey,
                         const std::string& region,
                         const std::string& logsetId,
                         const std::string& topicId)
        : secretId_(secretId)
        , secretKey_(secretKey)
        , region_(region)
        , logsetId_(logsetId)
        , topicId_(topicId)
        , level_(mqtt_client::LogLevel::INFO)
        , running_(true)
    {
        // 初始化腾讯云 CLS SDK
        // 这里需要根据实际的 CLS SDK 进行初始化
        
        // 启动后台线程用于异步发送日志
        logThread_ = std::thread(&TencentCloudCLSLogger::logWorker, this);
    }
    
    ~TencentCloudCLSLogger() override {
        close();
    }
    
    void log(mqtt_client::LogLevel level,
             std::string_view file,
             int line,
             std::string_view function,
             std::string_view message) override {
        // 过滤低于当前级别的日志
        if (level < level_) {
            return;
        }
        
        // 格式化日志消息
        std::string logEntry = formatLog(level, file, line, function, message);
        
        // 将日志添加到队列（异步发送）
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            logQueue_.push(logEntry);
        }
        queueCondition_.notify_one();
    }
    
    void flush() override {
        // 等待队列中的所有日志发送完成
        std::unique_lock<std::mutex> lock(queueMutex_);
        queueCondition_.wait(lock, [this] { return logQueue_.empty(); });
    }
    
    void setLevel(mqtt_client::LogLevel level) override {
        level_ = level;
    }
    
    mqtt_client::LogLevel getLevel() const override {
        return level_;
    }
    
    void close() override {
        if (!running_.load()) {
            return;
        }
        
        running_.store(false);
        queueCondition_.notify_all();
        
        if (logThread_.joinable()) {
            logThread_.join();
        }
        
        // 清理腾讯云 CLS SDK 资源
    }

private:
    std::string formatLog(mqtt_client::LogLevel level,
                         std::string_view file,
                         int line,
                         std::string_view function,
                         std::string_view message) {
        // 格式化日志为 JSON 格式（CLS 推荐格式）
        // 示例格式：
        // {
        //   "timestamp": "2024-01-01T12:00:00Z",
        //   "level": "INFO",
        //   "file": "wolfmqtt_adapter.cpp",
        //   "line": 123,
        //   "function": "connect",
        //   "message": "连接成功"
        // }
        
        // 这里可以使用你喜欢的 JSON 库（如 nlohmann/json）
        // 或者使用 fmt::format 格式化字符串
        
        return fmt::format(
            R"({{"timestamp":"{}","level":"{}","file":"{}","line":{},"function":"{}","message":"{}"}})",
            getCurrentTimestamp(),
            levelToString(level),
            file,
            line,
            function,
            escapeJsonString(message)
        );
    }
    
    void logWorker() {
        while (running_.load() || !logQueue_.empty()) {
            std::unique_lock<std::mutex> lock(queueMutex_);
            
            queueCondition_.wait_for(lock, std::chrono::milliseconds(100),
                [this] { return !logQueue_.empty() || !running_.load(); });
            
            // 批量获取日志（提高效率）
            std::vector<std::string> logs;
            while (!logQueue_.empty() && logs.size() < 100) {
                logs.push_back(logQueue_.front());
                logQueue_.pop();
            }
            
            lock.unlock();
            
            // 批量发送到腾讯云 CLS
            if (!logs.empty()) {
                sendToCLS(logs);
            }
        }
    }
    
    void sendToCLS(const std::vector<std::string>& logs) {
        // 调用腾讯云 CLS SDK 发送日志
        // 这里需要根据实际的 CLS SDK API 进行实现
        
        // 示例伪代码：
        // CLSClient client(secretId_, secretKey_, region_);
        // client.UploadLog(logsetId_, topicId_, logs);
    }
    
    std::string getCurrentTimestamp() {
        // 获取当前时间戳（ISO 8601 格式）
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::gmtime(&time);
        
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", tm);
        return std::string(buffer);
    }
    
    std::string levelToString(mqtt_client::LogLevel level) {
        switch (level) {
            case mqtt_client::LogLevel::TRACE: return "TRACE";
            case mqtt_client::LogLevel::DEBUG: return "DEBUG";
            case mqtt_client::LogLevel::INFO: return "INFO";
            case mqtt_client::LogLevel::WARN: return "WARN";
            case mqtt_client::LogLevel::ERROR: return "ERROR";
            case mqtt_client::LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
    
    std::string escapeJsonString(std::string_view str) {
        // 转义 JSON 字符串中的特殊字符
        std::string result;
        result.reserve(str.length());
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }
    
    // 配置信息
    std::string secretId_;
    std::string secretKey_;
    std::string region_;
    std::string logsetId_;
    std::string topicId_;
    
    // 日志级别
    mqtt_client::LogLevel level_;
    
    // 异步日志队列
    std::queue<std::string> logQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // 后台线程
    std::thread logThread_;
    std::atomic<bool> running_;
};
```

### 2. 在程序启动时注册自定义日志器

在创建 MQTT 客户端之前，调用 `LoggerManager::setLogger()` 注册你的自定义日志器：

```cpp
#include "mqtt_client/logger/logger_interface.h"
#include "mqtt_client/embedded_mqtt_client.h"
#include <memory>

int main() {
    // 1. 创建自定义日志器实例
    auto clsLogger = std::make_shared<TencentCloudCLSLogger>(
        "your-secret-id",      // 腾讯云 SecretId
        "your-secret-key",     // 腾讯云 SecretKey
        "ap-beijing",          // 地域
        "your-logset-id",      // 日志集 ID
        "your-topic-id"        // 日志主题 ID
    );
    
    // 2. 设置日志级别（可选）
    clsLogger->setLevel(mqtt_client::LogLevel::INFO);
    
    // 3. 注册到 LoggerManager
    mqtt_client::LoggerManager::getInstance().setLogger(clsLogger);
    
    // 4. 现在所有 LOG_* 宏都会使用你的自定义日志器
    LOG_INFO("程序启动");
    
    // 5. 创建和使用 MQTT 客户端
    mqtt_client::EmbeddedMqttClient client;
    // ... 使用客户端
    
    // 6. 程序退出前，确保日志已刷新
    mqtt_client::LoggerManager::getInstance().flush();
    
    // 7. 关闭日志器（可选，析构函数会自动调用）
    clsLogger->close();
    
    return 0;
}
```

### 3. 简化版示例（同步发送）

如果你不需要异步发送，可以使用更简单的实现：

```cpp
class SimpleCLSLogger : public mqtt_client::ILogger {
public:
    SimpleCLSLogger(const std::string& secretId, 
                   const std::string& secretKey,
                   const std::string& logsetId,
                   const std::string& topicId)
        : secretId_(secretId)
        , secretKey_(secretKey)
        , logsetId_(logsetId)
        , topicId_(topicId)
        , level_(mqtt_client::LogLevel::INFO)
    {
        // 初始化 CLS SDK
    }
    
    void log(mqtt_client::LogLevel level,
             std::string_view file,
             int line,
             std::string_view function,
             std::string_view message) override {
        if (level < level_) {
            return;
        }
        
        // 直接同步发送（简单但可能阻塞）
        std::string logEntry = formatLog(level, file, line, function, message);
        sendToCLS(logEntry);
    }
    
    void flush() override {
        // 同步版本无需实现
    }
    
    void setLevel(mqtt_client::LogLevel level) override {
        level_ = level;
    }
    
    mqtt_client::LogLevel getLevel() const override {
        return level_;
    }
    
    void close() override {
        // 清理资源
    }

private:
    std::string formatLog(mqtt_client::LogLevel level,
                         std::string_view file,
                         int line,
                         std::string_view function,
                         std::string_view message) {
        // 格式化日志
        // ...
    }
    
    void sendToCLS(const std::string& log) {
        // 发送到 CLS
        // ...
    }
    
    std::string secretId_;
    std::string secretKey_;
    std::string logsetId_;
    std::string topicId_;
    mqtt_client::LogLevel level_;
};
```

## 注意事项

1. **线程安全**：`log()` 方法可能被多个线程同时调用，确保实现是线程安全的。

2. **性能考虑**：
   - 如果日志发送是同步的，可能会阻塞调用线程
   - 建议使用异步队列 + 后台线程的方式
   - 批量发送可以提高效率

3. **错误处理**：
   - 网络错误时，考虑本地缓存日志
   - 避免日志发送失败导致程序崩溃

4. **资源清理**：
   - 在程序退出前调用 `flush()` 确保所有日志已发送
   - 在析构函数中正确清理资源

5. **日志格式**：
   - 根据腾讯云 CLS 的要求格式化日志
   - 通常推荐使用 JSON 格式

## 验证

注册日志器后，所有使用 `LOG_*` 宏的地方都会自动使用你的自定义日志器：

```cpp
// 这些日志都会发送到腾讯云 CLS
LOG_DEBUG("调试信息");
LOG_INFO("连接成功");
LOG_WARN("警告信息");
LOG_ERROR("错误信息");
```

## 参考

- [腾讯云 CLS 文档](https://cloud.tencent.com/document/product/614)
- [ILogger 接口定义](../include/mqtt_client/logger/logger_interface.h)
- [LoggerManager 实现](../src/logger/logger_manager.cpp)
