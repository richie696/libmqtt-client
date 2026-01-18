/**
 * @file embedded_mqtt_client.cpp
 * @brief 嵌入式MQTT客户端主类实现
 */

#include "mqtt_client/embedded_mqtt_client.h"
#include "mqtt_client/adapter/wolfmqtt_adapter.h"
#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/message/message_manager.h"
#include "mqtt_client/subscription/subscription_manager.h"
#include "mqtt_client/monitor/network_monitor.h"
#include "mqtt_client/monitor/connection_monitor.h"
#include "mqtt_client/monitor/heartbeat_manager.h"
#include "mqtt_client/reconnect/reconnect_manager.h"
#include "mqtt_client/config/config_manager.h"
#include "mqtt_client/persistence/persistence_manager.h"
#include "mqtt_client/persistence/idempotency_manager.h"
#include "mqtt_client/logger/logger_interface.h"
// #include "mqtt_client/platform.h"  // 已移除，不再需要
#include <algorithm>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <mutex>
#ifdef WOLFMQTT_ENABLED
#include <wolfmqtt/mqtt_types.h>
#endif

namespace mqtt_client {

namespace {
    /**
     * @brief 输出库初始化信息（用于运维排查）
     * 
     * 在库加载时输出平台、架构、版本等关键信息，便于远程运维和问题定位。
     */
    void logLibraryInfo() {
        std::ostringstream oss;
        
        // ========== 库基本信息 ==========
        oss << "========== MQTT客户端库初始化信息 ==========\n";
        
        // 库版本信息
        oss << "库版本: 1.0.0\n";
        
        // 编译时间
        oss << "编译时间: " << __DATE__ << " " << __TIME__ << "\n";
        
        // ========== 平台信息 ==========
        oss << "平台信息:\n";
        #ifdef PLATFORM_LINUX
            oss << "  平台: Linux\n";
        #elif defined(PLATFORM_WINDOWS)
            oss << "  平台: Windows\n";
        #elif defined(PLATFORM_MACOS)
            oss << "  平台: macOS\n";
        #else
            oss << "  平台: 未知\n";
        #endif
        
        // ========== 架构信息 ==========
        oss << "架构信息:\n";
        #ifdef ARCH_X86_64
            oss << "  架构: x86_64 (64位)\n";
        #elif defined(ARCH_X86)
            oss << "  架构: x86 (32位)\n";
        #elif defined(ARCH_ARM64)
            oss << "  架构: ARM64 (64位)\n";
        #elif defined(ARCH_ARM32)
            oss << "  架构: ARM32 (32位)\n";
        #elif defined(ARCH_MIPS32)
            oss << "  架构: MIPS32 (32位)\n";
        #else
            oss << "  架构: 未知\n";
        #endif
        
        #ifdef ARCH_64BIT
            oss << "  位宽: 64位\n";
        #else
            oss << "  位宽: 32位\n";
        #endif
        
        // ========== 编译器信息 ==========
        oss << "编译器信息:\n";
        #ifdef __GNUC__
            oss << "  编译器: GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << "\n";
        #elif defined(_MSC_VER)
            oss << "  编译器: MSVC " << _MSC_VER << "\n";
        #elif defined(__clang__)
            oss << "  编译器: Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__ << "\n";
        #else
            oss << "  编译器: 未知\n";
        #endif
        
        // C++标准版本格式化
        #if __cplusplus >= 202002L
            oss << "  C++标准: C++20\n";
        #elif __cplusplus >= 201703L
            oss << "  C++标准: C++17\n";
        #elif __cplusplus >= 201402L
            oss << "  C++标准: C++14\n";
        #elif __cplusplus >= 201103L
            oss << "  C++标准: C++11\n";
        #else
            oss << "  C++标准: C++98/03\n";
        #endif
        
        // ========== 功能特性 ==========
        oss << "功能特性:\n";
        #ifdef WOLFMQTT_ENABLED
            oss << "  ✓ wolfMQTT支持: 已启用\n";
            #ifdef WOLFMQTT_V5
                oss << "  ✓ MQTT 5.0支持: 已启用\n";
            #else
                oss << "  - MQTT 5.0支持: 未启用\n";
            #endif
        #else
            oss << "  ✗ wolfMQTT支持: 未启用\n";
        #endif
        
        #ifdef ENABLE_JSON
            oss << "  ✓ JSON支持: 已启用\n";
        #else
            oss << "  - JSON支持: 未启用\n";
        #endif
        
        // ========== 依赖版本信息 ==========
        oss << "依赖版本:\n";
        #ifdef WOLFMQTT_ENABLED
            // wolfMQTT版本信息（如果可用）
            #ifdef WOLFMQTT_VERSION_MAJOR
                oss << "  wolfMQTT: " << WOLFMQTT_VERSION_MAJOR << "." 
                    << WOLFMQTT_VERSION_MINOR << "." << WOLFMQTT_VERSION_PATCH << "\n";
            #else
                oss << "  wolfMQTT: 已启用（版本未知）\n";
            #endif
        #endif
        
        // ========== 运行时信息 ==========
        oss << "运行时信息:\n";
        std::time_t now = std::time(nullptr);
        std::tm* timeinfo = std::localtime(&now);
        if (timeinfo) {
            oss << "  当前时间: " << std::put_time(timeinfo, "%Y-%m-%d %H:%M:%S") << "\n";
        }
        
        // ========== 内存信息（如果可用）==========
        #ifdef PLATFORM_LINUX
            // Linux下可以获取内存信息
            oss << "  平台: Linux（可通过/proc/meminfo获取详细内存信息）\n";
        #elif defined(PLATFORM_MACOS)
            // macOS下可以获取内存信息
            oss << "  平台: macOS（可通过sysctl获取详细内存信息）\n";
        #endif
        
        // ========== 线程信息 ==========
        oss << "线程支持:\n";
        #ifdef __cpp_lib_thread
            oss << "  ✓ C++标准线程库: 支持\n";
        #else
            oss << "  - C++标准线程库: 不支持\n";
        #endif
        
        // ========== 结束标记 ==========
        oss << "==========================================";
        
        // 使用日志系统输出（INFO级别，便于运维查看）
        LOG_INFO(oss.str());
    }
}

EmbeddedMqttClient::EmbeddedMqttClient()
    : initialized_(false)
    , connected_(false)
{
    // 首次创建客户端时输出库初始化信息（仅输出一次）
    static std::once_flag log_once_flag;
    std::call_once(log_once_flag, logLibraryInfo);
}

EmbeddedMqttClient::EmbeddedMqttClient(const MqttConfig& config)
    : config_(config)
    , initialized_(false)
    , connected_(false)
{
    // 立即初始化
    auto result = initialize(config);
    if (!result) {
        LOG_ERROR("初始化失败: " + result.error.message);
    }
}

EmbeddedMqttClient::~EmbeddedMqttClient() {
    cleanup();
}

Result<bool> EmbeddedMqttClient::initialize(const MqttConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查是否已初始化
    if (initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INVALID_STATE,
                     "客户端已初始化，请先调用cleanup()"));
    }
    
    // 验证配置
    auto& configManager = MqttConfigManager::getInstance();
    auto validation = configManager.validate(config);
    if (!validation) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INVALID_CONFIG,
                     "配置验证失败: " + validation.error.message));
    }
    
    // 保存配置
    config_ = config;
    
    // 初始化子组件
    auto initResult = initializeComponents();
    if (!initResult) {
        cleanupComponents();
        return initResult;
    }
    
    // 设置回调
    setupCallbacks();
    
    initialized_.store(true);
    
    LOG_INFO("客户端初始化成功");
    
    return Result<bool>::Success(true);
}

bool EmbeddedMqttClient::isInitialized() const noexcept {
    return initialized_.load();
}

void EmbeddedMqttClient::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        return;
    }
    
    // 断开连接
    if (connected_.load()) {
        disconnect();
    }
    
    // 清理子组件
    cleanupComponents();
    
    initialized_.store(false);
    
    LOG_INFO("客户端清理完成");
}

Result<bool> EmbeddedMqttClient::updateConfig(const MqttConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "客户端未初始化"));
    }
    
    // 验证配置
    auto& configManager = MqttConfigManager::getInstance();
    auto validation = configManager.validate(config);
    if (!validation) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INVALID_CONFIG,
                     "配置验证失败: " + validation.error.message));
    }
    
    // 使用配置管理器进行热更新
    auto hotUpdateResult = configManager.hotUpdate(config);
    if (!hotUpdateResult) {
        return hotUpdateResult;
    }
    
    // 更新本地配置
    config_ = config;
    
    LOG_INFO("配置更新成功");
    
    return Result<bool>::Success(true);
}

const MqttConfig& EmbeddedMqttClient::getConfig() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

Result<bool> EmbeddedMqttClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "客户端未初始化"));
    }
    
    if (connected_.load()) {
        return Result<bool>::Success(true);
    }
    
    // 通过连接管理器连接
    if (!connectionManager_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "连接管理器未初始化"));
    }
    
    auto result = connectionManager_->connect();
    if (result) {
        connected_.store(true);
        
        // 启动监控组件
        if (networkMonitor_ && config_.monitoring.enableNetworkMonitor) {
            networkMonitor_->start();
        }
        if (connectionMonitor_ && config_.monitoring.enableConnectionMonitor) {
            connectionMonitor_->start();
        }
        if (heartbeatManager_ && config_.monitoring.enableHeartbeat) {
            heartbeatManager_->start();
        }
        
        // 恢复订阅
        if (subscriptionManager_) {
            subscriptionManager_->resubscribeAll();
        }
        
        // 消息管理器会在需要时自动处理队列
    }
    
    return result;
}

Result<bool> EmbeddedMqttClient::disconnect(bool /* force */) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!connected_.load()) {
        return Result<bool>::Success(true);
    }
    
    // 停止监控组件
    if (networkMonitor_) {
        networkMonitor_->stop();
    }
    if (connectionMonitor_) {
        connectionMonitor_->stop();
    }
    if (heartbeatManager_) {
        heartbeatManager_->stop();
    }
    
    // 停止重连
    if (reconnectManager_) {
        reconnectManager_->stopReconnect();
    }
    
    // 通过连接管理器断开
    if (connectionManager_) {
        auto result = connectionManager_->disconnect();
        connected_.store(false);
        return result;
    }
    
    connected_.store(false);
    return Result<bool>::Success(true);
}

Result<bool> EmbeddedMqttClient::reconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "客户端未初始化"));
    }
    
    if (connectionManager_) {
        return connectionManager_->reconnect();
    }
    
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::NOT_INITIALIZED,
                 "连接管理器未初始化"));
}

bool EmbeddedMqttClient::isConnected() const noexcept {
    return connected_.load();
}

ConnectionState EmbeddedMqttClient::getState() const {
    if (connectionManager_) {
        return connectionManager_->getState();
    }
    return ConnectionState::DISCONNECTED;
}

NetworkQuality EmbeddedMqttClient::getNetworkQuality() const {
    if (networkMonitor_) {
        return networkMonitor_->getQuality();
    }
    return NetworkQuality::FAIR;
}

Result<bool> EmbeddedMqttClient::publish(const std::string& topic,
                                        const std::string& payload,
                                        QoS qos,
                                        bool retain) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "客户端未初始化"));
    }
    
    if (!messageManager_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "消息管理器未初始化"));
    }
    
    return messageManager_->publish(topic, payload, qos, retain);
}

Result<bool> EmbeddedMqttClient::publish(const std::string& topic,
                                        const std::string& payload,
                                        const MqttProperties& properties,
                                        QoS qos,
                                        bool retain) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "客户端未初始化"));
    }
    
    if (!messageManager_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "消息管理器未初始化"));
    }
    
    // ========== MQTT 5.0 属性支持 ==========
    // 注意：MQTT 5.0 属性仅在 basic.version="5.0" 时生效
    // 对于 MQTT 3.1.1，属性将被忽略
    
    // 检查协议版本
    if (config_.basic.version != "5.0") {
        // MQTT 3.1.1 不支持属性，忽略 properties 参数
        LOG_DEBUG("MQTT 3.1.1 不支持属性，忽略 properties 参数");
        return messageManager_->publish(topic, payload, qos, retain);
    }
    
    // MQTT 5.0：如果属性为空，使用普通发布方法（通过 MessageManager）
    if (properties.isEmpty()) {
        return messageManager_->publish(topic, payload, qos, retain);
    }
    
    // MQTT 5.0：有属性时，直接通过 adapter 发布（绕过 MessageManager）
    // 因为 MessageManager 目前不支持属性，而 adapter 已实现属性支持
    if (!wolfAdapter_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "适配器未初始化"));
    }
    
    // 检查连接状态
    if (!connected_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法发布消息"));
    }
    
    // 通过 adapter 发布（带属性）
    LOG_DEBUG("使用 MQTT 5.0 属性发布消息");
    return wolfAdapter_->publish(topic, payload, properties, qos, retain);
}

Result<bool> EmbeddedMqttClient::subscribe(const std::string& topic,
                                          MessageCallback callback,
                                          QoS qos) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "客户端未初始化"));
    }
    
    if (!subscriptionManager_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "订阅管理器未初始化"));
    }
    
    return subscriptionManager_->subscribe(topic, callback, qos);
}

Result<bool> EmbeddedMqttClient::unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "客户端未初始化"));
    }
    
    if (!subscriptionManager_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "订阅管理器未初始化"));
    }
    
    return subscriptionManager_->unsubscribe(topic);
}

std::vector<std::string> EmbeddedMqttClient::getSubscribedTopics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!subscriptionManager_) {
        return std::vector<std::string>();
    }
    
    return subscriptionManager_->getSubscribedTopics();
}

void EmbeddedMqttClient::setConnectionCallback(ConnectionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    connectionCallback_ = callback;
}

void EmbeddedMqttClient::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    errorCallback_ = callback;
}

Result<bool> EmbeddedMqttClient::initializeComponents() {
    // 1. 创建wolfMQTT适配器
    wolfAdapter_ = std::make_unique<WolfMqttAdapter>(config_);
    auto adapterInit = wolfAdapter_->initialize();
    if (!adapterInit) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                     "适配器初始化失败: " + adapterInit.error.message));
    }
    
    // 2. 创建连接管理器
    connectionManager_ = std::make_unique<MqttConnectionManager>(config_, wolfAdapter_.get());
    
    // 3. 创建消息管理器
    messageManager_ = std::make_unique<MqttMessageManager>(*connectionManager_, config_);
    
    // 4. 创建订阅管理器
    subscriptionManager_ = std::make_unique<MqttSubscriptionManager>(*connectionManager_, config_);
    
    // 5. 创建网络监控器
    if (config_.monitoring.enableNetworkMonitor) {
        networkMonitor_ = std::make_unique<NetworkMonitor>(
            config_.server.host,
            config_.server.port,
            config_.monitoring.networkCheckInterval
        );
    }
    
    // 6. 创建连接监控器
    if (config_.monitoring.enableConnectionMonitor) {
        connectionMonitor_ = std::make_unique<ConnectionMonitor>(
            *connectionManager_,
            config_.monitoring.connectionCheckInterval
        );
    }
    
    // 7. 创建心跳管理器
    if (config_.monitoring.enableHeartbeat) {
        heartbeatManager_ = std::make_unique<HeartbeatManager>(
            *messageManager_,
            config_.monitoring.heartbeatInterval,
            config_.monitoring.heartbeatTopic
        );
    }
    
    // 8. 创建重连管理器
    reconnectManager_ = std::make_unique<ReconnectManager>(config_.reconnect);
    
    // 9. 创建持久化管理器（如果启用持久化）
    if (config_.persistence.storage.storagePath != "") {
        persistenceManager_ = std::make_shared<PersistenceManager>(
            config_.persistence.storage.storagePath
        );
        
        // 10. 创建幂等去重管理器（如果启用幂等去重）
        if (config_.persistence.idempotency.enableIdempotency) {
            idempotencyManager_ = std::make_shared<IdempotencyManager>(
                persistenceManager_,
                config_.persistence.idempotency.retentionTime,
                config_.persistence.idempotency.cleanupInterval
            );
        }
    }
    
    return Result<bool>::Success(true);
}

void EmbeddedMqttClient::cleanupComponents() {
    // 停止所有监控组件
    if (networkMonitor_) {
        networkMonitor_->stop();
        networkMonitor_.reset();
    }
    
    if (connectionMonitor_) {
        connectionMonitor_->stop();
        connectionMonitor_.reset();
    }
    
    if (heartbeatManager_) {
        heartbeatManager_->stop();
        heartbeatManager_.reset();
    }
    
    if (reconnectManager_) {
        reconnectManager_->stopReconnect();
        reconnectManager_.reset();
    }
    
    // 消息管理器会在析构时自动停止
    
    // 清理管理器
    subscriptionManager_.reset();
    messageManager_.reset();
    connectionManager_.reset();
    
    // 清理适配器
    if (wolfAdapter_) {
        wolfAdapter_->cleanup();
        wolfAdapter_.reset();
    }
    
    // 清理持久化和幂等去重组件
    idempotencyManager_.reset();
    persistenceManager_.reset();
}

void EmbeddedMqttClient::setupCallbacks() {
    // 设置连接管理器回调
    if (connectionManager_) {
        connectionManager_->setOnConnected([this]() {
            connected_.store(true);
            if (connectionCallback_) {
                try {
                    connectionCallback_(ConnectionState::CONNECTED, "连接成功");
                } catch (const std::exception& e) {
                    LOG_ERROR("连接回调执行失败: " + std::string(e.what()));
                }
            }
        });
        
        connectionManager_->setOnConnectionLost([this](const std::string& reason) {
            connected_.store(false);
            if (connectionCallback_) {
                try {
                    connectionCallback_(ConnectionState::DISCONNECTED, reason);
                } catch (const std::exception& e) {
                    LOG_ERROR("连接丢失回调执行失败: " + std::string(e.what()));
                }
            }
            
            // 触发重连
            if (reconnectManager_ && connectionManager_ && config_.reconnect.maxAttempts != 0) {
                auto* connMgr = connectionManager_.get();
                reconnectManager_->startReconnect([connMgr]() {
                    if (connMgr) {
                        auto result = connMgr->connect();
                        return static_cast<bool>(result);
                    }
                    return false;
                });
            }
        });
        
        connectionManager_->setOnConnectFailure([this](const std::string& reason) {
            if (errorCallback_) {
                try {
                    MqttError error(MqttErrorCode::CONNECTION_REFUSED, reason);
                    errorCallback_(error);
                } catch (const std::exception& e) {
                    LOG_ERROR("错误回调执行失败: " + std::string(e.what()));
                }
            }
        });
    }
    
    // 设置适配器消息回调（集成幂等去重）
    if (wolfAdapter_ && subscriptionManager_) {
        wolfAdapter_->setMessageCallback([this](const std::string& topic,
                                                const std::string& payload,
                                                QoS qos) {
            if (subscriptionManager_) {
                // 如果启用了幂等去重，先检查是否重复
                if (idempotencyManager_ && config_.persistence.idempotency.enableIdempotency) {
                    // 计算消息hash
                    std::string messageHash = IdempotencyManager::calculateMessageHash(
                        topic, payload, qos);
                    
                    // 检查是否重复
                    if (idempotencyManager_->isDuplicate(messageHash)) {
                        LOG_WARN("收到重复消息，已忽略: topic=" + topic + 
                                ", hash=" + messageHash);
                        return;  // 忽略重复消息
                    }
                    
                    // 标记消息已处理
                    idempotencyManager_->markProcessed(messageHash);
                }
                
                // 分发消息
                MqttProperties properties;  // 简化实现，暂时为空
                (void)qos;  // 避免未使用参数警告
                subscriptionManager_->dispatchMessage(topic, payload, properties);
            }
        });
    }
    
    // 设置网络监控器回调
    if (networkMonitor_) {
        networkMonitor_->setOnNetworkRecovered([this]() {
            // 网络恢复，触发快速重连
            if (reconnectManager_ && connectionManager_ && !connected_.load()) {
                reconnectManager_->resetAttempts();
                auto* connMgr = connectionManager_.get();
                reconnectManager_->startReconnect([connMgr]() {
                    if (connMgr) {
                        auto result = connMgr->connect();
                        return static_cast<bool>(result);
                    }
                    return false;
                });
            }
        });
    }
    
    // 设置连接监控器回调
    if (connectionMonitor_) {
        connectionMonitor_->setOnDisconnected([this]() {
            // 连接断开，触发重连
            if (reconnectManager_ && connectionManager_ && config_.reconnect.maxAttempts != 0) {
                auto* connMgr = connectionManager_.get();
                reconnectManager_->startReconnect([connMgr]() {
                    if (connMgr) {
                        auto result = connMgr->connect();
                        return static_cast<bool>(result);
                    }
                    return false;
                });
            }
        });
    }
}

} // namespace mqtt_client
