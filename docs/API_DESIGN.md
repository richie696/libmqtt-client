# API接口设计文档

[TOC]

------

## 📋 概述

本文档定义MQTT客户端库的完整API接口清单，包括所有公共接口、参数、返回值和使用示例。

**设计原则**：
- ✅ 简洁易用：API设计直观，易于理解和使用
- ✅ 类型安全：使用强类型，避免错误
- ✅ 错误处理：统一的错误处理机制
- ✅ 异步支持：支持同步和异步操作
- ✅ 线程安全：所有公共接口线程安全

---

## 🎯 API分类

### 1. 核心客户端API
- `EmbeddedMqttClient` - 主客户端类

### 2. 配置管理API
- `MqttConfig` - 配置结构
- `MqttConfigManager` - 配置管理器

### 3. 连接管理API
- `MqttConnectionManager` - 连接管理器（内部）

### 4. 消息管理API

- 发布消息
- 订阅主题
- 取消订阅
- 消息回调

### 5. 监控和指标API
- `MqttMetrics` - 监控指标
- 指标查询接口

### 6. 日志API
- `ILogger` - 日志接口
- `LoggerManager` - 日志管理器

### 7. 错误处理API
- `MqttError` - 错误信息
- `Result<T>` - 结果类型
- 错误回调

---

## 📚 核心客户端API

### EmbeddedMqttClient

主客户端类，提供MQTT客户端的所有功能。

类定义

```cpp
namespace mqtt_client {

class EmbeddedMqttClient {
public:
    // ========== 构造和析构 ==========
    
    /**
     * @brief 默认构造函数（延迟初始化模式）
     * 
     * 创建客户端实例，但不初始化。
     * 适用于配置从服务器下发的场景。
     * 
     * @note 使用此构造函数后，必须调用 initialize() 进行初始化
     */
    EmbeddedMqttClient();
    
    /**
     * @brief 带配置的构造函数（立即初始化模式）
     * 
     * 使用配置立即初始化客户端。
     * 适用于配置文件已存在的场景。
     * 
     * @param config MQTT配置对象
     * @throw std::invalid_argument 配置无效时抛出
     */
    explicit EmbeddedMqttClient(const MqttConfig& config);
    
    /**
     * @brief 析构函数
     * 
     * 自动断开连接并清理资源。
     */
    ~EmbeddedMqttClient();
    
    // 禁止拷贝
    EmbeddedMqttClient(const EmbeddedMqttClient&) = delete;
    EmbeddedMqttClient& operator=(const EmbeddedMqttClient&) = delete;
    
    // 允许移动
    EmbeddedMqttClient(EmbeddedMqttClient&&) noexcept = default;
    EmbeddedMqttClient& operator=(EmbeddedMqttClient&&) noexcept = default;
    
    // ========== 初始化和配置 ==========
    
    /**
     * @brief 初始化客户端（延迟初始化）
     * 
     * 使用配置初始化客户端。
     * 必须在调用 connect() 之前调用。
     * 
     * @param config MQTT配置对象
     * @return Result<bool> 初始化结果
     * 
     * @example
     * ```cpp
     * EmbeddedMqttClient client;
     * MqttConfig config;
     * config.server.host = "mqtt.example.com";
     * config.server.port = 1883;
     * 
     * auto result = client.initialize(config);
     * if (!result.success) {
     *     std::cerr << "初始化失败: " << result.error.message << std::endl;
     * }
     * ```
     */
    Result<bool> initialize(const MqttConfig& config);
    
    /**
     * @brief 检查是否已初始化
     * 
     * @return true 已初始化
     * @return false 未初始化
     */
    bool isInitialized() const noexcept;
    
    /**
     * @brief 更新配置（热更新）
     * 
     * 更新客户端配置，部分配置需要重新连接才能生效。
     * 
     * @param config 新的配置对象
     * @return Result<bool> 更新结果
     * 
     * @note 连接相关配置（host、port等）需要重新连接才能生效
     */
    Result<bool> updateConfig(const MqttConfig& config);
    
    /**
     * @brief 获取当前配置
     * 
     * @return const MqttConfig& 当前配置的引用
     */
    const MqttConfig& getConfig() const noexcept;
    
    // ========== 连接管理 ==========
    
    /**
     * @brief 连接到MQTT服务器
     * 
     * 建立与MQTT服务器的连接。
     * 如果已连接，则返回成功。
     * 
     * @return Result<bool> 连接结果
     * 
     * @example
     * ```cpp
     * auto result = client.connect();
     * if (result.success) {
     *     std::cout << "连接成功" << std::endl;
     * } else {
     *     std::cerr << "连接失败: " << result.error.message << std::endl;
     * }
     * ```
     */
    Result<bool> connect();
    
    /**
     * @brief 断开连接
     * 
     * 断开与MQTT服务器的连接。
     * 
     * @param force 是否强制断开（不发送DISCONNECT包）
     * @return Result<bool> 断开结果
     */
    Result<bool> disconnect(bool force = false);
    
    /**
     * @brief 检查是否已连接
     * 
     * @return true 已连接
     * @return false 未连接
     */
    bool isConnected() const noexcept;
    
    /**
     * @brief 重新连接
     * 
     * 断开当前连接并重新连接。
     * 
     * @return Result<bool> 重连结果
     */
    Result<bool> reconnect();
    
    // ========== 消息发布 ==========
    
    /**
     * @brief 发布消息
     * 
     * 向指定主题发布消息。
     * 
     * @param topic 主题名称
     * @param payload 消息内容
     * @param qos QoS等级（0、1、2）
     * @param retain 是否保留消息
     * @return Result<bool> 发布结果
     * 
     * @example
     * ```cpp
     * auto result = client.publish("device/status", "online", QoS::QOS_1);
     * if (result.success) {
     *     std::cout << "消息发布成功" << std::endl;
     * }
     * ```
     */
    Result<bool> publish(const std::string& topic,
                         const std::string& payload,
                         QoS qos = QoS::QOS_0,
                         bool retain = false);
    
    /**
     * @brief 发布消息（带属性，MQTT 5.0）
     * 
     * 向指定主题发布消息，支持MQTT 5.0属性。
     * 
     * @param topic 主题名称
     * @param payload 消息内容
     * @param properties MQTT 5.0属性
     * @param qos QoS等级
     * @param retain 是否保留消息
     * @return Result<bool> 发布结果
     */
    Result<bool> publish(const std::string& topic,
                         const std::string& payload,
                         const MqttProperties& properties,
                         QoS qos = QoS::QOS_0,
                         bool retain = false);
    
    /**
     * @brief 批量发布消息
     * 
     * 批量发布多条消息，提高效率。
     * 
     * @param messages 消息列表
     * @return Result<int> 成功发布的消息数量
     */
    Result<int> publishBatch(const std::vector<MqttMessage>& messages);
    
    // ========== 消息订阅 ==========
    
    /**
     * @brief 订阅主题
     * 
     * 订阅指定主题，接收该主题的消息。
     * 
     * @param topic 主题名称（支持通配符）
     * @param callback 消息回调函数
     * @param qos QoS等级
     * @return Result<bool> 订阅结果
     * 
     * @example
     * ```cpp
     * auto result = client.subscribe("device/+/status", 
     *     [](const std::string& topic, const std::string& payload) {
     *         std::cout << "收到消息: " << topic << " -> " << payload << std::endl;
     *     },
     *     QoS::QOS_1
     * );
     * ```
     */
    Result<bool> subscribe(const std::string& topic,
                          MessageCallback callback,
                          QoS qos = QoS::QOS_0);
    
    /**
     * @brief 订阅多个主题
     * 
     * 同时订阅多个主题。
     * 
     * @param subscriptions 订阅列表（主题和QoS的映射）
     * @param callback 消息回调函数
     * @return Result<int> 成功订阅的主题数量
     */
    Result<int> subscribe(const std::map<std::string, QoS>& subscriptions,
                         MessageCallback callback);
    
    /**
     * @brief 取消订阅
     * 
     * 取消对指定主题的订阅。
     * 
     * @param topic 主题名称
     * @return Result<bool> 取消订阅结果
     */
    Result<bool> unsubscribe(const std::string& topic);
    
    /**
     * @brief 取消订阅多个主题
     * 
     * 同时取消多个主题的订阅。
     * 
     * @param topics 主题列表
     * @return Result<int> 成功取消订阅的主题数量
     */
    Result<int> unsubscribe(const std::vector<std::string>& topics);
    
    /**
     * @brief 获取已订阅的主题列表
     * 
     * @return std::vector<std::string> 已订阅的主题列表
     */
    std::vector<std::string> getSubscribedTopics() const;
    
    // ========== 监控和指标 ==========
    
    /**
     * @brief 获取监控指标
     * 
     * 获取客户端的监控指标。
     * 
     * @return const MqttMetrics& 监控指标对象
     */
    const MqttMetrics& getMetrics() const noexcept;
    
    /**
     * @brief 重置监控指标
     * 
     * 重置所有监控指标为初始值。
     */
    void resetMetrics();
    
    // ========== 事件回调 ==========
    
    /**
     * @brief 设置连接状态回调
     * 
     * 设置连接状态变化时的回调函数。
     * 
     * @param callback 连接状态回调函数
     */
    void setConnectionCallback(ConnectionCallback callback);
    
    /**
     * @brief 设置错误回调
     * 
     * 设置发生错误时的回调函数。
     * 
     * @param callback 错误回调函数
     */
    void setErrorCallback(ErrorCallback callback);
    
    /**
     * @brief 设置重连回调
     * 
     * 设置重连时的回调函数。
     * 
     * @param callback 重连回调函数
     */
    void setReconnectCallback(ReconnectCallback callback);
    
    // ========== 工具方法 ==========
    
    /**
     * @brief 获取客户端ID
     * 
     * @return const std::string& 客户端ID
     */
    const std::string& getClientId() const noexcept;
    
    /**
     * @brief 获取服务器地址
     * 
     * @return std::string 服务器地址（host:port）
     */
    std::string getServerAddress() const;
    
    /**
     * @brief 获取协议版本
     * 
     * @return MqttProtocolVersion 协议版本（3.1.1 或 5.0）
     */
    MqttProtocolVersion getProtocolVersion() const noexcept;
    
    /**
     * @brief 获取连接状态信息
     * 
     * @return ConnectionState 连接状态信息
     */
    ConnectionState getConnectionState() const;
};

} // namespace mqtt_client
```

---

## 📝 类型定义

### Result<T>

统一的返回类型，包含成功/失败状态和错误信息。

```cpp
template<typename T>
struct Result {
    bool success;           // 是否成功
    T value;                // 返回值（成功时有效）
    MqttError error;        // 错误信息（失败时有效）
    
    // 便捷方法
    explicit operator bool() const noexcept { return success; }
    const T& operator*() const { return value; }
    T& operator*() { return value; }
    const T* operator->() const { return value; }
    T* operator->() { return value; }
};
```

### MqttError

错误信息结构。

```cpp
struct MqttError {
    MqttErrorCode code;     // 错误码
    std::string message;     // 错误消息
    std::string details;     // 详细信息（可选）
    
    // 便捷方法
    std::string toString() const;
};
```

### QoS

QoS等级枚举。

```cpp
enum class QoS : uint8_t {
    QOS_0 = 0,  // 最多一次
    QOS_1 = 1,  // 至少一次
    QOS_2 = 2   // 恰好一次
};
```

### MqttProtocolVersion

MQTT协议版本枚举。

```cpp
enum class MqttProtocolVersion {
    V3_1_1,  // MQTT 3.1.1
    V5_0     // MQTT 5.0
};
```

### MessageCallback

消息回调函数类型。

```cpp
using MessageCallback = std::function<void(
    const std::string& topic,      // 主题
    const std::string& payload,    // 消息内容
    const MqttProperties& properties = {}  // MQTT 5.0属性（可选）
)>;
```

### ConnectionCallback

连接状态回调函数类型。

```cpp
using ConnectionCallback = std::function<void(
    bool connected,         // 是否已连接
    const std::string& reason = ""  // 原因（断开时）
)>;
```

### ErrorCallback

错误回调函数类型。

```cpp
using ErrorCallback = std::function<void(
    const MqttError& error  // 错误信息
)>;
```

### ReconnectCallback

重连回调函数类型。

```cpp
using ReconnectCallback = std::function<void(
    int attempt,            // 当前尝试次数
    int maxAttempts,        // 最大尝试次数
    long nextInterval       // 下次重连间隔（毫秒）
)>;
```

### MqttMessage

消息结构。

```cpp
struct MqttMessage {
    std::string topic;              // 主题
    std::string payload;            // 消息内容
    QoS qos = QoS::QOS_0;          // QoS等级
    bool retain = false;            // 是否保留
    MqttProperties properties;      // MQTT 5.0属性（可选）
};
```

### MqttProperties

MQTT 5.0属性结构。

```cpp
struct MqttProperties {
    std::optional<int> messageExpiryInterval;      // 消息过期时间（秒）
    std::optional<std::string> contentType;        // 内容类型
    std::optional<std::string> responseTopic;       // 响应主题
    std::optional<std::string> correlationData;    // 关联数据
    std::map<std::string, std::string> userProperties;  // 用户属性
    
    // 便捷方法
    void setUserProperty(const std::string& key, const std::string& value);
    std::optional<std::string> getUserProperty(const std::string& key) const;
};
```

### ConnectionState

连接状态信息。

```cpp
struct ConnectionState {
    bool connected;                 // 是否已连接
    std::string serverAddress;      // 服务器地址
    MqttProtocolVersion protocolVersion;  // 协议版本
    std::chrono::system_clock::time_point connectedAt;  // 连接时间
    std::chrono::milliseconds lastPingTime;  // 上次心跳时间
    int reconnectAttempts;          // 重连尝试次数
};
```

---

## ⚙️ 配置管理API

### MqttConfig

配置结构，包含所有可配置项。

```cpp
struct MqttConfig {
    // 基础配置
    struct {
        std::string host;                    // 服务器地址
        int port;                           // 服务器端口
        std::string clientId;               // 客户端ID
        MqttProtocolVersion version;         // 协议版本（"3.1.1" 或 "5.0"）
        int keepAlive;                      // 保活时间（秒）
        bool cleanSession;                  // 是否清理会话（3.1.1）
        bool cleanStart;                    // 是否清理启动（5.0）
        std::optional<uint32_t> sessionExpiryInterval;  // 会话过期时间（5.0）
    } basic;
    
    // 认证配置
    struct {
        std::string username;               // 用户名
        std::string password;               // 密码
        std::string token;                  // Token（可选）
    } auth;
    
    // TLS/SSL配置
    struct {
        bool enable;                        // 是否启用TLS
        std::string caCertPath;             // CA证书路径
        std::string clientCertPath;         // 客户端证书路径
        std::string clientKeyPath;          // 客户端密钥路径
        bool verifyServerCert;              // 是否验证服务器证书
    } tls;
    
    // 重连配置
    struct {
        bool enable;                        // 是否启用自动重连
        int baseInterval;                   // 基础重连间隔（毫秒）
        int maxInterval;                    // 最大重连间隔（毫秒）
        double minJitter;                   // 最小抖动系数
        double maxJitter;                   // 最大抖动系数
        int maxAttempts;                    // 最大重连次数（-1表示无限）
        bool enableExponentialBackoff;      // 是否启用指数退避
        bool enableJitter;                 // 是否启用随机抖动
    } reconnect;
    
    // 监控配置
    struct {
        int networkCheckInterval;           // 网络检查间隔（秒）
        int connectionCheckInterval;       // 连接检查间隔（秒）
        int heartbeatInterval;             // 心跳间隔（秒）
    } monitor;
    
    // 线程池配置
    struct {
        int networkThreads;                 // 网络线程数
        int messageThreads;                 // 消息处理线程数
        int maxQueueSize;                   // 最大队列大小
    } threadPool;
    
    // 日志配置
    struct {
        LogLevel level;                     // 日志级别
        std::string filePath;               // 日志文件路径
        size_t maxFileSize;                 // 最大文件大小（字节）
        int maxFiles;                       // 最大文件数
    } logger;
    
    // 持久化配置
    struct {
        bool enable;                        // 是否启用持久化
        std::string dataDir;                // 数据目录
        size_t sendBufferSize;             // 发送缓冲区大小
        size_t receiveBufferSize;           // 接收缓冲区大小
        std::chrono::minutes idempotencyRetention;  // 幂等去重保留时间
    } persistence;
    
    // 性能优化配置
    struct {
        bool enableBatching;                // 是否启用消息批量处理
        int batchSize;                     // 批量大小
        std::chrono::milliseconds batchTimeout;  // 批量超时时间
        bool enableZeroCopy;               // 是否启用零拷贝
        bool enableCompression;            // 是否启用消息压缩
    } performance;
    
    // 便捷方法
    static MqttConfig createDefault();
    static Result<MqttConfig> loadFromFile(const std::string& filePath);
    static Result<MqttConfig> loadFromJson(const std::string& json);
    Result<bool> validate() const;
    std::string toJson() const;
};
```

### MqttConfigManager

配置管理器，提供配置加载、验证、热更新等功能。

```cpp
class MqttConfigManager {
public:
    /**
     * @brief 从文件加载配置
     * 
     * @param filePath 配置文件路径（JSON或YAML）
     * @return Result<MqttConfig> 配置对象
     */
    static Result<MqttConfig> loadFromFile(const std::string& filePath);
    
    /**
     * @brief 从JSON字符串加载配置
     * 
     * @param json JSON字符串
     * @return Result<MqttConfig> 配置对象
     */
    static Result<MqttConfig> loadFromJson(const std::string& json);
    
    /**
     * @brief 从YAML字符串加载配置
     * 
     * @param yaml YAML字符串
     * @return Result<MqttConfig> 配置对象
     */
    static Result<MqttConfig> loadFromYaml(const std::string& yaml);
    
    /**
     * @brief 验证配置
     * 
     * @param config 配置对象
     * @return Result<bool> 验证结果
     */
    static Result<bool> validate(const MqttConfig& config);
    
    /**
     * @brief 合并配置
     * 
     * 将新配置合并到现有配置中。
     * 
     * @param base 基础配置
     * @param override 覆盖配置
     * @return MqttConfig 合并后的配置
     */
    static MqttConfig merge(const MqttConfig& base, const MqttConfig& override);
    
    /**
     * @brief 保存配置到文件
     * 
     * @param config 配置对象
     * @param filePath 文件路径
     * @return Result<bool> 保存结果
     */
    static Result<bool> saveToFile(const MqttConfig& config, const std::string& filePath);
    
    /**
     * @brief 转换为JSON字符串
     * 
     * @param config 配置对象
     * @return std::string JSON字符串
     */
    static std::string toJson(const MqttConfig& config);
    
    /**
     * @brief 转换为YAML字符串
     * 
     * @param config 配置对象
     * @return std::string YAML字符串
     */
    static std::string toYaml(const MqttConfig& config);
};
```

---

## 📊 监控指标API

### MqttMetrics

监控指标结构。

```cpp
struct MqttMetrics {
    // 连接指标
    struct {
        int totalConnections;           // 总连接次数
        int successfulConnections;      // 成功连接次数
        int failedConnections;          // 失败连接次数
        int disconnections;             // 断开连接次数
        int reconnections;              // 重连次数
        std::chrono::milliseconds avgConnectionTime;  // 平均连接时间
    } connection;
    
    // 消息指标
    struct {
        int messagesSent;                // 发送消息数
        int messagesReceived;           // 接收消息数
        int messagesFailed;             // 失败消息数
        int bytesSent;                  // 发送字节数
        int bytesReceived;              // 接收字节数
        std::chrono::milliseconds avgLatency;  // 平均延迟
    } message;
    
    // 网络指标
    struct {
        int networkErrors;              // 网络错误数
        int timeouts;                   // 超时次数
        double packetLossRate;          // 丢包率
        std::chrono::milliseconds avgLatency;  // 平均延迟
    } network;
    
    // 订阅指标
    struct {
        int subscriptions;              // 订阅数
        int unsubscriptions;            // 取消订阅数
        int subscriptionFailures;        // 订阅失败数
    } subscription;
    
    // 心跳指标
    struct {
        int heartbeatsSent;             // 发送心跳数
        int heartbeatsReceived;         // 接收心跳数
        int heartbeatTimeouts;          // 心跳超时数
    } heartbeat;
    
    // 性能指标
    struct {
        size_t memoryUsage;             // 内存使用（字节）
        double cpuUsage;                 // CPU使用率（%）
        int activeThreads;               // 活跃线程数
        int queueSize;                  // 队列大小
    } performance;
    
    // 错误指标
    struct {
        int totalErrors;                 // 总错误数
        std::map<MqttErrorCode, int> errorCounts;  // 各错误码的计数
    } error;
    
    // 业务指标（用户自定义）
    std::map<std::string, int> customMetrics;
    
    // 便捷方法
    void reset();
    std::string toString() const;
    std::string toJson() const;
};
```

---

## 📝 日志API

### ILogger

日志接口。

```cpp
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
                     const std::string& file,
                     int line,
                     const std::string& function,
                     const std::string& message) = 0;
    
    /**
     * @brief 刷新日志
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
     * @return LogLevel 日志级别
     */
    virtual LogLevel getLevel() const = 0;
};
```

### LoggerManager

日志管理器（单例）。

```cpp
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
     * @param logger 日志器实例
     */
    void setLogger(std::shared_ptr<ILogger> logger);
    
    /**
     * @brief 获取当前日志器
     * 
     * @return std::shared_ptr<ILogger> 日志器实例
     */
    std::shared_ptr<ILogger> getLogger() const;
    
    /**
     * @brief 记录日志
     */
    void log(LogLevel level,
             const std::string& file,
             int line,
             const std::string& function,
             const std::string& message);
    
    /**
     * @brief 刷新日志
     */
    void flush();
};
```

### 日志宏

```cpp
#define LOG_TRACE(msg) \
    LoggerManager::getInstance().log(LogLevel::TRACE, __FILE__, __LINE__, __FUNCTION__, msg)

#define LOG_DEBUG(msg) \
    LoggerManager::getInstance().log(LogLevel::DEBUG, __FILE__, __LINE__, __FUNCTION__, msg)

#define LOG_INFO(msg) \
    LoggerManager::getInstance().log(LogLevel::INFO, __FILE__, __LINE__, __FUNCTION__, msg)

#define LOG_WARN(msg) \
    LoggerManager::getInstance().log(LogLevel::WARN, __FILE__, __LINE__, __FUNCTION__, msg)

#define LOG_ERROR(msg) \
    LoggerManager::getInstance().log(LogLevel::ERROR, __FILE__, __LINE__, __FUNCTION__, msg)

#define LOG_FATAL(msg) \
    LoggerManager::getInstance().log(LogLevel::FATAL, __FILE__, __LINE__, __FUNCTION__, msg)
```

---

## 🔧 错误处理API

### MqttErrorCode

错误码枚举。

```cpp
enum class MqttErrorCode {
    // 成功
    SUCCESS = 0,
    
    // 通用错误
    UNKNOWN_ERROR = 1000,
    INVALID_ARGUMENT = 1001,
    INVALID_STATE = 1002,
    NOT_INITIALIZED = 1003,
    NOT_CONNECTED = 1004,
    
    // 网络错误
    NETWORK_ERROR = 2000,
    CONNECTION_REFUSED = 2001,
    CONNECTION_TIMEOUT = 2002,
    NETWORK_UNREACHABLE = 2003,
    HOST_NOT_FOUND = 2004,
    
    // MQTT协议错误
    PROTOCOL_ERROR = 3000,
    INVALID_PACKET = 3001,
    UNSUPPORTED_VERSION = 3002,
    IDENTIFIER_REJECTED = 3003,
    SERVER_UNAVAILABLE = 3004,
    BAD_USERNAME_OR_PASSWORD = 3005,
    NOT_AUTHORIZED = 3006,
    
    // TLS/SSL错误
    TLS_ERROR = 4000,
    CERTIFICATE_ERROR = 4001,
    CERTIFICATE_EXPIRED = 4002,
    CERTIFICATE_INVALID = 4003,
    
    // 资源错误
    RESOURCE_ERROR = 5000,
    OUT_OF_MEMORY = 5001,
    FILE_ERROR = 5002,
    THREAD_ERROR = 5003,
    
    // 配置错误
    CONFIG_ERROR = 6000,
    INVALID_CONFIG = 6001,
    CONFIG_NOT_FOUND = 6002,
    
    // 消息错误
    MESSAGE_ERROR = 7000,
    MESSAGE_TOO_LARGE = 7001,
    INVALID_TOPIC = 7002,
    PUBLISH_FAILED = 7003,
    SUBSCRIBE_FAILED = 7004,
    UNSUBSCRIBE_FAILED = 7005
};
```

---

## 📖 使用示例

### 示例1：基本使用

```cpp
#include "mqtt_client/embedded_mqtt_client.h"

int main() {
    // 创建配置
    MqttConfig config;
    config.basic.host = "mqtt.example.com";
    config.basic.port = 1883;
    config.basic.clientId = "my_client";
    config.basic.version = MqttProtocolVersion::V5_0;
    
    // 创建客户端
    mqtt_client::EmbeddedMqttClient client(config);
    
    // 连接
    auto result = client.connect();
    if (!result.success) {
        std::cerr << "连接失败: " << result.error.message << std::endl;
        return 1;
    }
    
    // 订阅
    client.subscribe("device/+/status", 
        [](const std::string& topic, const std::string& payload) {
            std::cout << "收到消息: " << topic << " -> " << payload << std::endl;
        },
        QoS::QOS_1
    );
    
    // 发布
    client.publish("device/001/status", "online", QoS::QOS_1);
    
    // 保持运行
    std::this_thread::sleep_for(std::chrono::seconds(60));
    
    // 断开连接
    client.disconnect();
    
    return 0;
}
```

### 示例2：延迟初始化

```cpp
#include "mqtt_client/embedded_mqtt_client.h"

int main() {
    // 创建客户端（延迟初始化）
    mqtt_client::EmbeddedMqttClient client;
    
    // 从服务器获取配置（模拟）
    MqttConfig config = fetchConfigFromServer();
    
    // 初始化
    auto result = client.initialize(config);
    if (!result.success) {
        std::cerr << "初始化失败: " << result.error.message << std::endl;
        return 1;
    }
    
    // 连接
    client.connect();
    
    // ... 使用客户端
    
    return 0;
}
```

### 示例3：错误处理

```cpp
#include "mqtt_client/embedded_mqtt_client.h"

int main() {
    mqtt_client::EmbeddedMqttClient client(config);
    
    // 设置错误回调
    client.setErrorCallback([](const MqttError& error) {
        std::cerr << "错误: " << error.code << " - " << error.message << std::endl;
    });
    
    // 设置连接回调
    client.setConnectionCallback([](bool connected, const std::string& reason) {
        if (connected) {
            std::cout << "已连接" << std::endl;
        } else {
            std::cout << "已断开: " << reason << std::endl;
        }
    });
    
    // 连接
    auto result = client.connect();
    if (!result) {
        // 使用Result的bool转换
        handleError(result.error);
    }
    
    return 0;
}
```

### 示例4：监控指标

```cpp
#include "mqtt_client/embedded_mqtt_client.h"

int main() {
    mqtt_client::EmbeddedMqttClient client(config);
    client.connect();
    
    // 定期获取指标
    while (true) {
        const auto& metrics = client.getMetrics();
        
        std::cout << "连接数: " << metrics.connection.totalConnections << std::endl;
        std::cout << "发送消息: " << metrics.message.messagesSent << std::endl;
        std::cout << "接收消息: " << metrics.message.messagesReceived << std::endl;
        std::cout << "内存使用: " << metrics.performance.memoryUsage << " bytes" << std::endl;
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    return 0;
}
```



