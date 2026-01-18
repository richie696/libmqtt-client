/**
 * @file config.h
 * @brief MQTT客户端配置结构定义
 * 
 * 定义完整的MQTT客户端配置结构，包括所有功能模块的配置项。
 */

#ifndef MQTT_CLIENT_CONFIG_CONFIG_H
#define MQTT_CLIENT_CONFIG_CONFIG_H

#include "mqtt_client/core/types.h"
#include "mqtt_client/core/error.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <ctime>

namespace mqtt_client {

/**
 * @brief MQTT客户端完整配置
 * 
 * 包含所有功能模块的配置项，支持MQTT 3.1.1和5.0协议。
 */
struct MqttConfig {
    // ========== 基础MQTT配置 ==========
    struct BasicConfig {
        std::string version = "5.0";              ///< MQTT版本："3.1.1" 或 "5.0"（默认5.0）
        std::string clientIdPrefix = "mqtt_";     ///< 客户端ID前缀
        bool cleanStart = false;                  ///< 是否清理会话
        std::string clientId;                     ///< 客户端ID（可选，不提供则自动生成）
    } basic;
    
    // ========== 服务器配置 ==========
    struct ServerConfig {
        std::string host;                         ///< 服务器地址
        int port = 1883;                          ///< 端口
        bool useSSL = false;                      ///< 是否使用SSL
        int connectTimeout = 30;                  ///< 连接超时（秒）
        int keepAlive = 60;                       ///< 保活时间（秒）
        
        // 备用服务器
        struct BackupServer {
            std::string host;
            int port;
            bool useSSL;
        };
        std::vector<BackupServer> backupServers;  ///< 备用服务器列表
    } server;
    
    // ========== 认证配置 ==========
    struct AuthConfig {
        std::string username;                    ///< 用户名
        std::string password;                     ///< 密码
        std::string token;                        ///< Token（可选）
        bool useTokenAuth = false;                ///< 是否使用Token认证
        int tokenRefreshInterval = 3600;          ///< Token刷新间隔（秒）
    } auth;
    
    // ========== MQTT 5.0配置 ==========
    // 注意：仅当 basic.version="5.0" 时，此配置才生效
    // 当 basic.version="3.1.1" 时，此配置中的所有项将被忽略
    // 这些配置项通过 MQTT 5.0 的 CONNECT 包属性（Properties）发送给服务器
    struct Mqtt5Config {
        int sessionExpiryInterval = 7200;         ///< 会话过期间隔（秒）
        int messageExpiryInterval = 1800;         ///< 消息过期间隔（秒）
        int receiveMaximum = 100;                 ///< 接收最大数量
        int maximumPacketSize = 1048576;          ///< 最大包大小（1MB）
        bool requestResponseInformation = false;  ///< 请求响应信息
        bool requestProblemInformation = true;    ///< 请求问题信息
        
        // 遗嘱消息配置
        struct WillMessage {
            bool enabled = false;                 ///< 是否启用
            std::string topic;                    ///< 主题
            QoS qos = QoS::QOS_1;                 ///< QoS级别
            bool retained = false;                 ///< 是否保留
            std::string payload;                  ///< 负载
            int delayInterval = 0;                ///< 延迟间隔
        } willMessage;
        
        // 用户属性
        std::map<std::string, std::string> userProperties;
    } mqtt5;
    
    // ========== 重连配置 ==========
    struct ReconnectConfig {
        long baseInterval = 1000;                ///< 基础间隔（毫秒）
        long maxInterval = 300000;               ///< 最大间隔（毫秒，5分钟）
        int maxAttempts = 0;                     ///< 最大尝试次数（0=无限）
        double minJitter = 0.2;                  ///< 最小抖动系数
        double maxJitter = 1.0;                  ///< 最大抖动系数
        bool enableExponentialBackoff = true;    ///< 启用指数退避
        bool enableJitter = true;                ///< 启用随机抖动
    } reconnect;
    
    // ========== 监控配置 ==========
    struct MonitoringConfig {
        // 网络监控
        int networkCheckInterval = 5;             ///< 网络检查间隔（秒）
        int networkTimeout = 3;                   ///< 网络超时（秒）
        bool enableNetworkMonitor = true;         ///< 启用网络监控
        
        // 连接监控
        int connectionCheckInterval = 10;         ///< 连接检查间隔（秒）
        bool enableConnectionMonitor = true;      ///< 启用连接监控
        
        // 心跳配置
        int heartbeatInterval = 30;               ///< 心跳间隔（秒）
        QoS heartbeatQoS = QoS::QOS_0;           ///< 心跳QoS
        bool enableHeartbeat = true;              ///< 启用心跳
        std::string heartbeatTopic;              ///< 心跳主题（可选）
    } monitoring;
    
    // ========== 安全配置 ==========
    struct SecurityConfig {
        // TLS/SSL配置
        bool enableTLS = false;                   ///< 启用TLS
        std::string tlsVersion = "1.2";           ///< TLS版本（1.2, 1.3）
        std::vector<std::string> cipherSuites;    ///< 加密套件
        bool verifyCertificate = true;            ///< 验证证书
        int verifyDepth = 9;                       ///< 验证深度
        
        // 证书配置
        std::string caCertificatePath;            ///< CA证书路径
        std::string clientCertificatePath;         ///< 客户端证书路径
        std::string clientPrivateKeyPath;          ///< 客户端私钥路径
        
        // 证书存储配置
        struct CertificateStorageConfig {
            std::string storagePath;               ///< 存储路径
            std::string encryptionAlgorithm = "AES-256-GCM";  ///< 加密算法
            bool useDeviceKey = true;              ///< 使用设备密钥
            bool useHSM = false;                   ///< 使用硬件安全模块
        } certificateStorage;
        
        // 访问控制配置
        struct AccessControlConfig {
            bool enableACL = true;                 ///< 启用访问控制列表
            bool verifyProcessIdentity = true;      ///< 验证进程身份
        } accessControl;
    } security;
    
    // ========== 日志配置 ==========
    struct LoggingConfig {
        LogLevel level = LogLevel::INFO;           ///< 日志级别
        std::string logPath = "/var/log/mqtt_client.log";  ///< 日志路径
        size_t maxFileSize = 10 * 1024 * 1024;    ///< 最大文件大小（10MB）
        int maxFiles = 5;                          ///< 最大文件数
        bool async = true;                         ///< 异步日志
        bool rotateOnStartup = false;              ///< 启动时轮转
        std::string loggerType = "file";           ///< 日志类型（file, console, custom）
    } logging;
    
    // ========== 线程配置 ==========
    struct ThreadConfig {
        int networkMonitorThreads = 1;            ///< 网络监控线程数
        int connectionMonitorThreads = 1;          ///< 连接监控线程数
        int heartbeatThreads = 1;                 ///< 心跳线程数
        int messageProcessorThreads = 1;           ///< 消息处理线程数
        int reconnectThreads = 1;                 ///< 重连线程数
        int totalThreads = 5;                      ///< 总线程数（固定）
    } thread;
    
    // ========== 资源管理配置 ==========
    struct ResourceConfig {
        // 内存限制
        size_t memoryLimit = 10 * 1024 * 1024;    ///< 内存限制（10MB）
        double memoryWarningThreshold = 0.8;      ///< 内存预警阈值（80%）
        
        // 文件描述符限制
        int maxFileDescriptors = 100;              ///< 最大文件描述符数
        
        // 线程限制
        int maxThreads = 10;                       ///< 最大线程数
        
        // 连接限制
        int maxConnections = 5;                    ///< 最大连接数
    } resource;
    
    // ========== 消息队列配置 ==========
    struct MessageQueueConfig {
        size_t maxSendQueueSize = 1000;            ///< 发送队列最大大小
        size_t maxReceiveQueueSize = 1000;         ///< 接收队列最大大小
        int maxRetry = 3;                         ///< 最大重试次数
        int retryInterval = 5000;                  ///< 重试间隔（毫秒）
        bool enablePriority = false;               ///< 启用优先级队列
    } messageQueue;
    
    // ========== 数据持久化配置 ==========
    struct PersistenceConfig {
        // 缓冲区配置
        struct BufferConfig {
            size_t maxSendBufferSize = 1000;        ///< 发送缓冲区最大大小
            size_t maxReceiveBufferSize = 1000;    ///< 接收缓冲区最大大小
            bool enableSendPersistence = true;     ///< 启用发送持久化
            bool enableReceivePersistence = true;  ///< 启用接收持久化
        } buffer;
        
        // 存储配置
        struct StorageConfig {
            std::string storagePath = "./data";    ///< 存储路径
            std::string storageType = "file";      ///< 存储类型（file, sqlite等）
            bool enableCompression = true;         ///< 启用压缩
            int flushInterval = 5;                 ///< 刷新间隔（秒）
        } storage;
        
        // 幂等去重配置
        struct IdempotencyConfig {
            bool enableIdempotency = true;         ///< 启用幂等去重
            time_t retentionTime = 5 * 60;        ///< 保存时间（秒，默认5分钟）
            time_t cleanupInterval = 3600;         ///< 清理间隔（秒，默认1小时）
            bool enablePersistence = true;         ///< 启用持久化
        } idempotency;
        
        // 恢复配置
        struct RecoveryConfig {
            bool enableFastRecovery = true;        ///< 启用快速恢复
            int maxRecoveryTime = 5000;            ///< 最大恢复时间（毫秒）
            bool parallelRecovery = true;          ///< 并行恢复
        } recovery;
    } persistence;
    
    // ========== 性能优化配置 ==========
    struct PerformanceConfig {
        // 批量处理配置
        struct BatchConfig {
            bool enableBatchSend = true;           ///< 启用批量发送
            size_t maxBatchSize = 10;              ///< 最大批量大小
            int maxBatchDelay = 100;               ///< 最大批量延迟（毫秒）
            bool enableBatchReceive = true;        ///< 启用批量接收
        } batch;
        
        // 零拷贝配置
        struct ZeroCopyConfig {
            bool enableZeroCopy = true;            ///< 启用零拷贝
            bool useSharedPtr = true;               ///< 使用shared_ptr共享
        } zeroCopy;
        
        // 对象池配置
        struct ObjectPoolConfig {
            bool enableObjectPool = true;          ///< 启用对象池
            size_t messagePoolSize = 100;          ///< 消息对象池大小
            size_t bufferPoolSize = 10;            ///< 缓冲区池大小
        } objectPool;
        
        // 缓存配置
        struct CacheConfig {
            bool enableDNSCache = true;             ///< 启用DNS缓存
            time_t dnsCacheTTL = 3600;             ///< DNS缓存TTL（秒）
            bool enableSubscriptionCache = true;   ///< 启用订阅缓存
        } cache;
        
        // 消息压缩配置
        struct CompressionConfig {
            bool enableCompression = false;        ///< 启用压缩（默认关闭）
            size_t compressionThreshold = 1024;    ///< 压缩阈值（字节）
            std::string compressionAlgorithm = "gzip";  ///< 压缩算法
        } compression;
        
        // 延迟发送配置
        struct DelayedSendConfig {
            bool enableDelayedSend = false;        ///< 启用延迟发送（默认关闭）
            int delayWindow = 100;                  ///< 延迟窗口（毫秒）
            size_t mergeThreshold = 512;            ///< 合并阈值（字节）
        } delayedSend;
    } performance;
    
    // ========== 监控指标配置 ==========
    struct MetricsConfig {
        bool enableMetrics = true;                 ///< 启用监控指标
        int reportInterval = 60;                   ///< 上报间隔（秒）
        bool enableAutoReport = false;             ///< 启用自动上报
        std::string reportEndpoint;                ///< 上报端点（可选）
        
        // 指标收集配置
        bool collectConnectionMetrics = true;      ///< 收集连接指标
        bool collectMessageMetrics = true;         ///< 收集消息指标
        bool collectNetworkMetrics = true;         ///< 收集网络指标
        bool collectPerformanceMetrics = true;     ///< 收集性能指标
    } metrics;
    
    // ========== 错误处理配置 ==========
    struct ErrorHandlingConfig {
        bool enableGlobalExceptionHandler = true; ///< 启用全局异常处理
        bool enableSignalHandler = true;           ///< 启用信号处理
        bool enableThreadExceptionHandler = true;  ///< 启用线程异常处理
        bool enableAutoRecover = false;            ///< 启用自动恢复
        bool logErrors = true;                     ///< 记录错误日志
    } errorHandling;
    
    // ========== QoS策略配置 ==========
    struct QoSPolicyConfig {
        QoS heartbeat = QoS::QOS_0;                ///< 心跳QoS
        QoS statusReport = QoS::QOS_1;            ///< 状态上报QoS
        QoS printTask = QoS::QOS_1;                ///< 打印任务QoS
        QoS controlCommand = QoS::QOS_1;           ///< 控制指令QoS
        QoS financialData = QoS::QOS_2;           ///< 财务数据QoS
        
        // 自定义QoS映射
        std::map<std::string, QoS> topicQoSMap;   ///< 主题到QoS的映射
    } qosPolicy;
    
    // ========== 主题配置 ==========
    struct TopicConfig {
        std::string heartbeat = "device/{clientId}/heartbeat";
        std::string status = "device/{clientId}/status";
        std::string printTask = "device/{clientId}/print/task";
        std::string printResult = "device/{clientId}/print/result";
        std::string command = "device/{clientId}/command";
        
        // 自定义主题
        std::map<std::string, std::string> customTopics;
    } topics;
    
    /**
     * @brief 获取协议版本枚举
     * 
     * @return MqttProtocolVersion 协议版本枚举值
     */
    MqttProtocolVersion getProtocolVersion() const {
        if (basic.version == "3.1.1") {
            return MqttProtocolVersion::V3_1_1;
        }
        return MqttProtocolVersion::V5_0;
    }
    
    /**
     * @brief 检查配置是否有效
     * 
     * @return true 配置有效
     * @return false 配置无效
     */
    bool isValid() const {
        // 基本验证
        if (basic.version != "3.1.1" && basic.version != "5.0") {
            return false;
        }
        if (server.host.empty()) {
            return false;
        }
        if (server.port <= 0 || server.port > 65535) {
            return false;
        }
        return true;
    }
    
    /**
     * @brief 重置为默认配置
     */
    void reset() {
        *this = MqttConfig();
    }
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_CONFIG_CONFIG_H
