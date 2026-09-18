/**
 * @file config_manager.cpp
 * @brief MQTT配置管理器实现
 */

#include "mqtt_client/config/config_manager.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/logger/logger_interface.h"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <utility>
#include <fmt/core.h>

namespace fs = std::filesystem;

// 如果启用了nlohmann/json，则使用它进行JSON解析
#ifdef ENABLE_JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#endif

// 如果启用了yaml-cpp，则使用它进行YAML解析
#ifdef ENABLE_YAML
#include <yaml-cpp/yaml.h>
#endif

namespace mqtt_client {

#if defined(ENABLE_JSON) && defined(ENABLE_YAML)
namespace {

YAML::Node jsonToYaml(const json& value) {
    if (value.is_object()) {
        YAML::Node node(YAML::NodeType::Map);
        for (const auto& [key, child] : value.items()) {
            node[key] = jsonToYaml(child);
        }
        return node;
    }
    if (value.is_array()) {
        YAML::Node node(YAML::NodeType::Sequence);
        for (const auto& child : value) {
            node.push_back(jsonToYaml(child));
        }
        return node;
    }
    if (value.is_boolean()) {
        YAML::Node node(value.get<bool>());
        node.SetTag("tag:yaml.org,2002:bool");
        return node;
    }
    if (value.is_number_integer()) {
        YAML::Node node(value.get<long long>());
        node.SetTag("tag:yaml.org,2002:int");
        return node;
    }
    if (value.is_number_unsigned()) {
        YAML::Node node(value.get<unsigned long long>());
        node.SetTag("tag:yaml.org,2002:int");
        return node;
    }
    if (value.is_number_float()) {
        YAML::Node node(value.get<double>());
        node.SetTag("tag:yaml.org,2002:float");
        return node;
    }
    if (value.is_null()) return YAML::Node();
    YAML::Node node(value.get<std::string>());
    node.SetTag("tag:yaml.org,2002:str");
    return node;
}

json yamlToJson(const YAML::Node& node) {
    if (node.IsMap()) {
        json result = json::object();
        for (const auto& item : node) {
            result[item.first.as<std::string>()] = yamlToJson(item.second);
        }
        return result;
    }
    if (node.IsSequence()) {
        json result = json::array();
        for (const auto& item : node) {
            result.push_back(yamlToJson(item));
        }
        return result;
    }
    if (!node.IsScalar()) return nullptr;

    const std::string scalar = node.as<std::string>();
    const std::string tag = node.Tag();
    if (tag == "tag:yaml.org,2002:str") return scalar;
    if (tag == "tag:yaml.org,2002:bool") return node.as<bool>();
    if (tag == "tag:yaml.org,2002:int") return node.as<long long>();
    if (tag == "tag:yaml.org,2002:float") return node.as<double>();
    if (scalar == "true") return true;
    if (scalar == "false") return false;
    return scalar;
}

} // namespace
#endif

MqttConfigManager& MqttConfigManager::getInstance() {
    static MqttConfigManager instance;
    return instance;
}

Result<MqttConfig> MqttConfigManager::loadFromFile(const std::string& filePath) {
    std::lock_guard lock(mutex_);
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return Result<MqttConfig>::Failure(
            MqttError(MqttErrorCode::CONFIG_NOT_FOUND,
                     "配置文件未找到: " + filePath));
    }

    const std::string format = detectFileFormat(filePath);
    const std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    if (format == "json") {
        // 使用不加锁的内部方法，因为当前已经持有锁
        auto result = loadFromJsonImpl(content);
        if (result) {
            currentConfig_ = *result;
        }
        return result;
    }
    if (format == "yaml") {
        auto result = loadFromYaml(filePath);
        if (result) {
            currentConfig_ = *result;
        }
        return result;
    }
    return Result<MqttConfig>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                  "不支持的配置文件格式: " + format));
}

Result<MqttConfig> MqttConfigManager::loadFromJson(const std::string& jsonStr) {
    std::lock_guard lock(mutex_);
    auto result = loadFromJsonImpl(jsonStr);
    if (result) {
        currentConfig_ = *result;
    }
    return result;
}

Result<MqttConfig> MqttConfigManager::loadFromJsonImpl(
    [[maybe_unused]] const std::string& jsonStr) {
    // 注意：此方法不加锁，调用者必须已经持有锁
#ifdef ENABLE_JSON
    try {
        auto result = parseJsonConfigImpl(jsonStr);
        return result;
    } catch (const std::exception& e) {
        return Result<MqttConfig>::Failure(
            MqttError(MqttErrorCode::CONFIG_ERROR,
                     "JSON解析失败: " + std::string(e.what())));
    }
#else
    // 如果没有启用JSON支持，返回错误
    return Result<MqttConfig>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                 "JSON支持未启用，请编译时启用ENABLE_JSON选项"));
#endif
}

Result<MqttConfig> MqttConfigManager::loadFromYaml(const std::string& filePath) {
#if defined(ENABLE_YAML) && defined(ENABLE_JSON)
    try {
        const YAML::Node yaml = YAML::LoadFile(filePath);
        return parseJsonConfigImpl(yamlToJson(yaml).dump());
    } catch (const YAML::Exception& e) {
        return Result<MqttConfig>::Failure(
            MqttError(MqttErrorCode::CONFIG_ERROR,
                      "YAML解析失败: " + std::string(e.what())));
    } catch (const std::exception& e) {
        return Result<MqttConfig>::Failure(
            MqttError(MqttErrorCode::CONFIG_ERROR,
                      "YAML文件读取失败: " + std::string(e.what())));
    }
#else
    // 如果没有启用YAML支持，返回错误
    return Result<MqttConfig>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                 fmt::format("YAML支持未启用，请编译时启用ENABLE_YAML选项，否则将无法读取 {} 文件。", filePath)));
#endif
}

MqttConfig MqttConfigManager::getDefaultConfig() {
    MqttConfig config;
    
    // 设置默认值
    config.basic.version = "5.0";
    config.basic.clientIdPrefix = "mqtt_client_";
    config.basic.cleanStart = false;
    // 生成默认clientId（使用前缀+时间戳）
    config.basic.clientId = config.basic.clientIdPrefix + std::to_string(std::time(nullptr));
    
    config.server.host = "localhost";
    config.server.port = 1883;
    config.server.useSSL = false;
    config.server.connectTimeout = 30;
    config.server.keepAlive = 60;
    
    // 其他配置使用结构体默认值即可
    return config;
}

Result<bool> MqttConfigManager::validate(const MqttConfig& config) {
    const std::vector<std::string> errors = getValidationErrors(config);
    if (errors.empty()) {
        return Result<bool>::Success(true);
    }
    
    std::string errorMsg = "配置验证失败: ";
    for (size_t i = 0; i < std::size(errors); ++i) {
        errorMsg += errors[i];
        if (i < std::size(errors) - 1) {
            errorMsg += "; ";
        }
    }
    
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::CONFIG_VALIDATION_FAILED, errorMsg));
}

std::vector<std::string> MqttConfigManager::getValidationErrors(const MqttConfig& config) {
    std::vector<std::string> errors;
    
    // 验证基础配置
    validateBasicConfig(config.basic, errors);
    
    // 验证服务器配置
    validateServerConfig(config.server, errors);
    
    // 验证安全配置
    if (config.server.useSSL || config.security.enableTLS) {
        validateSecurityConfig(config.security, errors);
    }
    
    return errors;
}

bool MqttConfigManager::validateBasicConfig(const MqttConfig::BasicConfig& config,
                                            std::vector<std::string>& errors) {
    bool valid = true;
    
    if (config.version != "3.1.1" && config.version != "5.0") {
        errors.emplace_back("MQTT版本必须是'3.1.1'或'5.0'");
        valid = false;
    }
    
    if (config.clientIdPrefix.empty()) {
        errors.emplace_back("客户端ID前缀不能为空");
        valid = false;
    }
    
    return valid;
}

bool MqttConfigManager::validateServerConfig(const MqttConfig::ServerConfig& config,
                                            std::vector<std::string>& errors) {
    bool valid = true;
    
    if (config.host.empty()) {
        errors.emplace_back("服务器地址不能为空");
        valid = false;
    }
    
    if (config.port <= 0 || config.port > 65535) {
        errors.emplace_back("服务器端口必须在1-65535之间");
        valid = false;
    }
    
    if (config.connectTimeout <= 0) {
        errors.emplace_back("连接超时必须大于0");
        valid = false;
    }
    
    if (config.keepAlive <= 0) {
        errors.emplace_back("保活时间必须大于0");
        valid = false;
    }
    
    // 验证备用服务器
    for (size_t i = 0; i < std::size(config.backupServers); ++i) {
        const auto& backup = config.backupServers[i];
        if (backup.host.empty()) {
            errors.emplace_back("备用服务器" + std::to_string(i) + "地址不能为空");
            valid = false;
        }
        if (backup.port <= 0 || backup.port > 65535) {
            errors.emplace_back("备用服务器" + std::to_string(i) + "端口无效");
            valid = false;
        }
    }
    
    return valid;
}

bool MqttConfigManager::validateSecurityConfig(const MqttConfig::SecurityConfig& config,
                                               std::vector<std::string>& errors) {
    bool valid = true;

    if (config.tlsVersion != "1.2" && config.tlsVersion != "1.3") {
        errors.emplace_back("TLS版本必须是'1.2'或'1.3'");
        valid = false;
    }

    if (config.verifyCertificate && config.caCertificatePath.empty()) {
        errors.emplace_back("启用证书验证时，CA证书路径不能为空");
        valid = false;
    }

    if (config.verifyDepth <= 0) {
        errors.emplace_back("证书验证深度必须大于0");
        valid = false;
    }

    // 双向TLS必须同时提供客户端证书和私钥。
    if (config.clientCertificatePath.empty() != config.clientPrivateKeyPath.empty()) {
        errors.emplace_back("客户端证书与私钥必须同时提供");
        valid = false;
    }

    return valid;
}

Result<bool> MqttConfigManager::saveToFile(const MqttConfig& config, const std::string& filePath) {
    std::lock_guard lock(mutex_);
    
    // 先验证配置（使用 if 初始化语句，限制变量作用域）
    if (auto validation = validate(config); !validation) {
        return Result<bool>::Failure(validation.error);
    }
    
    const std::string format = detectFileFormat(filePath);
    Result<std::string> serialized = format == "yaml"
        ? saveToYaml(config)
        : saveToJson(config);
    if (!serialized) {
        return Result<bool>::Failure(serialized.error);
    }
    
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::FILE_ERROR,
                     "无法打开文件进行写入: " + filePath));
    }
    
    file << *serialized;
    file.close();
    
    return Result<bool>::Success(true);
}

Result<std::string> MqttConfigManager::saveToYaml(const MqttConfig& config) {
#ifdef ENABLE_YAML
#ifdef ENABLE_JSON
    try {
        const auto jsonResult = saveToJson(config);
        if (!jsonResult) return Result<std::string>::Failure(jsonResult.error);
        YAML::Emitter emitter;
        emitter << jsonToYaml(json::parse(*jsonResult));
        if (!emitter.good()) {
            return Result<std::string>::Failure(
                MqttError(MqttErrorCode::CONFIG_ERROR, "YAML序列化失败"));
        }
        return Result<std::string>::Success(emitter.c_str());
    } catch (const std::exception& e) {
        return Result<std::string>::Failure(
            MqttError(MqttErrorCode::CONFIG_ERROR,
                      "YAML序列化失败: " + std::string(e.what())));
    }
#else
    (void)config;
    return Result<std::string>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                  "YAML序列化需要同时启用JSON支持"));
#endif
#else
    (void)config;
    return Result<std::string>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                  "YAML支持未启用，请编译时启用ENABLE_YAML选项"));
#endif
}

Result<std::string> MqttConfigManager::saveToJson([[maybe_unused]] const MqttConfig& config) {
#ifdef ENABLE_JSON
    try {
        json j;

        auto& mqtt = j["mqtt"];
        mqtt["basic"] = {
            {"version", config.basic.version},
            {"client_id_prefix", config.basic.clientIdPrefix},
            {"clean_start", config.basic.cleanStart},
            {"client_id", config.basic.clientId}
        };
        mqtt["server"] = {
            {"host", config.server.host},
            {"port", config.server.port},
            {"use_ssl", config.server.useSSL},
            {"connect_timeout", config.server.connectTimeout},
            {"keep_alive", config.server.keepAlive}
        };
        mqtt["server"]["backup_servers"] = json::array();
        for (const auto& backup : config.server.backupServers) {
            mqtt["server"]["backup_servers"].push_back({
                {"host", backup.host}, {"port", backup.port}, {"use_ssl", backup.useSSL}
            });
        }

        mqtt["auth"] = {
            {"username", config.auth.username},
            {"password", config.auth.password},
            {"token", config.auth.token},
            {"use_token_auth", config.auth.useTokenAuth},
            {"token_refresh_interval", config.auth.tokenRefreshInterval}
        };

        mqtt["mqtt5"] = {
            {"session_expiry_interval", config.mqtt5.sessionExpiryInterval},
            {"message_expiry_interval", config.mqtt5.messageExpiryInterval},
            {"receive_maximum", config.mqtt5.receiveMaximum},
            {"maximum_packet_size", config.mqtt5.maximumPacketSize},
            {"request_response_information", config.mqtt5.requestResponseInformation},
            {"request_problem_information", config.mqtt5.requestProblemInformation},
            {"user_properties", config.mqtt5.userProperties}
        };
        mqtt["mqtt5"]["will_message"] = {
            {"enabled", config.mqtt5.willMessage.enabled},
            {"topic", config.mqtt5.willMessage.topic},
            {"qos", static_cast<int>(config.mqtt5.willMessage.qos)},
            {"retained", config.mqtt5.willMessage.retained},
            {"payload", config.mqtt5.willMessage.payload},
            {"delay_interval", config.mqtt5.willMessage.delayInterval}
        };

        mqtt["reconnect"] = {
            {"base_interval", config.reconnect.baseInterval},
            {"max_interval", config.reconnect.maxInterval},
            {"max_attempts", config.reconnect.maxAttempts},
            {"min_jitter", config.reconnect.minJitter},
            {"max_jitter", config.reconnect.maxJitter},
            {"enable_exponential_backoff", config.reconnect.enableExponentialBackoff},
            {"enable_jitter", config.reconnect.enableJitter}
        };

        mqtt["monitoring"] = {
            {"network_check_interval", config.monitoring.networkCheckInterval},
            {"network_timeout", config.monitoring.networkTimeout},
            {"enable_network_monitor", config.monitoring.enableNetworkMonitor},
            {"connection_check_interval", config.monitoring.connectionCheckInterval},
            {"enable_connection_monitor", config.monitoring.enableConnectionMonitor},
            {"heartbeat_interval", config.monitoring.heartbeatInterval},
            {"heartbeat_qos", static_cast<int>(config.monitoring.heartbeatQoS)},
            {"enable_heartbeat", config.monitoring.enableHeartbeat},
            {"heartbeat_topic", config.monitoring.heartbeatTopic}
        };

        mqtt["security"] = {
            {"enable_tls", config.security.enableTLS},
            {"tls_version", config.security.tlsVersion},
            {"cipher_suites", config.security.cipherSuites},
            {"verify_certificate", config.security.verifyCertificate},
            {"verify_depth", config.security.verifyDepth},
            {"ca_certificate_path", config.security.caCertificatePath},
            {"client_certificate_path", config.security.clientCertificatePath},
            {"client_private_key_path", config.security.clientPrivateKeyPath}
        };
        mqtt["security"]["certificate_storage"] = {
            {"storage_path", config.security.certificateStorage.storagePath},
            {"encryption_algorithm", config.security.certificateStorage.encryptionAlgorithm},
            {"use_device_key", config.security.certificateStorage.useDeviceKey},
            {"use_hsm", config.security.certificateStorage.useHSM}
        };
        mqtt["security"]["access_control"] = {
            {"enable_acl", config.security.accessControl.enableACL},
            {"verify_process_identity", config.security.accessControl.verifyProcessIdentity}
        };

        mqtt["logging"] = {
            {"level", static_cast<int>(config.logging.level)},
            {"log_path", config.logging.logPath},
            {"max_file_size", config.logging.maxFileSize},
            {"max_files", config.logging.maxFiles},
            {"async", config.logging.async},
            {"rotate_on_startup", config.logging.rotateOnStartup},
            {"logger_type", config.logging.loggerType}
        };
        mqtt["thread"] = {
            {"network_monitor_threads", config.thread.networkMonitorThreads},
            {"connection_monitor_threads", config.thread.connectionMonitorThreads},
            {"heartbeat_threads", config.thread.heartbeatThreads},
            {"message_processor_threads", config.thread.messageProcessorThreads},
            {"reconnect_threads", config.thread.reconnectThreads},
            {"total_threads", config.thread.totalThreads}
        };
        mqtt["resource"] = {
            {"memory_limit", config.resource.memoryLimit},
            {"memory_warning_threshold", config.resource.memoryWarningThreshold},
            {"max_file_descriptors", config.resource.maxFileDescriptors},
            {"max_threads", config.resource.maxThreads},
            {"max_connections", config.resource.maxConnections}
        };
        mqtt["message_queue"] = {
            {"max_send_queue_size", config.messageQueue.maxSendQueueSize},
            {"max_receive_queue_size", config.messageQueue.maxReceiveQueueSize},
            {"max_retry", config.messageQueue.maxRetry},
            {"retry_interval", config.messageQueue.retryInterval},
            {"enable_priority", config.messageQueue.enablePriority}
        };

        mqtt["persistence"]["buffer"] = {
            {"max_send_buffer_size", config.persistence.buffer.maxSendBufferSize},
            {"max_receive_buffer_size", config.persistence.buffer.maxReceiveBufferSize},
            {"enable_send_persistence", config.persistence.buffer.enableSendPersistence},
            {"enable_receive_persistence", config.persistence.buffer.enableReceivePersistence}
        };
        mqtt["persistence"]["storage"] = {
            {"storage_path", config.persistence.storage.storagePath},
            {"storage_type", config.persistence.storage.storageType},
            {"enable_compression", config.persistence.storage.enableCompression},
            {"flush_interval", config.persistence.storage.flushInterval}
        };
        mqtt["persistence"]["idempotency"] = {
            {"enable_idempotency", config.persistence.idempotency.enableIdempotency},
            {"retention_time", config.persistence.idempotency.retentionTime},
            {"cleanup_interval", config.persistence.idempotency.cleanupInterval},
            {"enable_persistence", config.persistence.idempotency.enablePersistence}
        };
        mqtt["persistence"]["recovery"] = {
            {"enable_fast_recovery", config.persistence.recovery.enableFastRecovery},
            {"max_recovery_time", config.persistence.recovery.maxRecoveryTime},
            {"parallel_recovery", config.persistence.recovery.parallelRecovery}
        };

        mqtt["performance"]["batch"] = {
            {"enable_batch_send", config.performance.batch.enableBatchSend},
            {"max_batch_size", config.performance.batch.maxBatchSize},
            {"max_batch_delay", config.performance.batch.maxBatchDelay},
            {"enable_batch_receive", config.performance.batch.enableBatchReceive}
        };
        mqtt["performance"]["zero_copy"] = {
            {"enable_zero_copy", config.performance.zeroCopy.enableZeroCopy},
            {"use_shared_ptr", config.performance.zeroCopy.useSharedPtr}
        };
        mqtt["performance"]["object_pool"] = {
            {"enable_object_pool", config.performance.objectPool.enableObjectPool},
            {"message_pool_size", config.performance.objectPool.messagePoolSize},
            {"buffer_pool_size", config.performance.objectPool.bufferPoolSize}
        };
        mqtt["performance"]["cache"] = {
            {"enable_dns_cache", config.performance.cache.enableDNSCache},
            {"dns_cache_ttl", config.performance.cache.dnsCacheTTL},
            {"enable_subscription_cache", config.performance.cache.enableSubscriptionCache}
        };
        mqtt["performance"]["compression"] = {
            {"enable_compression", config.performance.compression.enableCompression},
            {"compression_threshold", config.performance.compression.compressionThreshold},
            {"compression_algorithm", config.performance.compression.compressionAlgorithm}
        };
        mqtt["performance"]["delayed_send"] = {
            {"enable_delayed_send", config.performance.delayedSend.enableDelayedSend},
            {"delay_window", config.performance.delayedSend.delayWindow},
            {"merge_threshold", config.performance.delayedSend.mergeThreshold}
        };
        mqtt["metrics"] = {
            {"enable_metrics", config.metrics.enableMetrics},
            {"report_interval", config.metrics.reportInterval},
            {"enable_auto_report", config.metrics.enableAutoReport},
            {"report_endpoint", config.metrics.reportEndpoint},
            {"collect_connection_metrics", config.metrics.collectConnectionMetrics},
            {"collect_message_metrics", config.metrics.collectMessageMetrics},
            {"collect_network_metrics", config.metrics.collectNetworkMetrics},
            {"collect_performance_metrics", config.metrics.collectPerformanceMetrics}
        };
        mqtt["error_handling"] = {
            {"enable_global_exception_handler", config.errorHandling.enableGlobalExceptionHandler},
            {"enable_signal_handler", config.errorHandling.enableSignalHandler},
            {"enable_thread_exception_handler", config.errorHandling.enableThreadExceptionHandler},
            {"enable_auto_recover", config.errorHandling.enableAutoRecover},
            {"log_errors", config.errorHandling.logErrors}
        };
        mqtt["qos_policy"] = {
            {"heartbeat", static_cast<int>(config.qosPolicy.heartbeat)},
            {"status_report", static_cast<int>(config.qosPolicy.statusReport)},
            {"print_task", static_cast<int>(config.qosPolicy.printTask)},
            {"control_command", static_cast<int>(config.qosPolicy.controlCommand)},
            {"financial_data", static_cast<int>(config.qosPolicy.financialData)},
            {"topic_qos_map", json::object()}
        };
        for (const auto& [topic, qos] : config.qosPolicy.topicQoSMap) {
            mqtt["qos_policy"]["topic_qos_map"][topic] = static_cast<int>(qos);
        }
        mqtt["topics"] = {
            {"heartbeat", config.topics.heartbeat},
            {"status", config.topics.status},
            {"print_task", config.topics.printTask},
            {"print_result", config.topics.printResult},
            {"command", config.topics.command},
            {"custom_topics", config.topics.customTopics}
        };
        
        // 返回格式化的JSON字符串
        return Result<std::string>::Success(j.dump(4));
    } catch (const std::exception& e) {
        return Result<std::string>::Failure(
            MqttError(MqttErrorCode::CONFIG_ERROR,
                     "JSON序列化失败: " + std::string(e.what())));
    }
#else
    // 如果没有启用JSON支持，返回错误
    return Result<std::string>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                 "JSON支持未启用，请编译时启用ENABLE_JSON选项"));
#endif
}

MqttConfig MqttConfigManager::merge(const MqttConfig& base, const MqttConfig& override) {
    MqttConfig merged = base;
    const MqttConfig defaults;
    
    // 合并基础配置
    if (!override.basic.version.empty()) {
        merged.basic.version = override.basic.version;
    }
    if (!override.basic.clientIdPrefix.empty()) {
        merged.basic.clientIdPrefix = override.basic.clientIdPrefix;
    }
    if (override.basic.cleanStart != defaults.basic.cleanStart) {
        merged.basic.cleanStart = override.basic.cleanStart;
    }
    if (!override.basic.clientId.empty()) {
        merged.basic.clientId = override.basic.clientId;
    }
    
    // 合并服务器配置
    if (!override.server.host.empty()) {
        merged.server.host = override.server.host;
    }
    if (override.server.port > 0) {
        merged.server.port = override.server.port;
    }
    if (override.server.useSSL != defaults.server.useSSL) {
        merged.server.useSSL = override.server.useSSL;
    }
    if (override.server.connectTimeout > 0) {
        merged.server.connectTimeout = override.server.connectTimeout;
    }
    if (override.server.keepAlive > 0) {
        merged.server.keepAlive = override.server.keepAlive;
    }
    if (!override.server.backupServers.empty()) {
        merged.server.backupServers = override.server.backupServers;
    }
    
    // 合并认证配置
    if (!override.auth.username.empty()) {
        merged.auth.username = override.auth.username;
    }
    if (!override.auth.password.empty()) {
        merged.auth.password = override.auth.password;
    }
    
    // 合并日志配置。默认值代表“未提供”，避免部分覆盖对象重置base配置。
    if (override.logging.level != defaults.logging.level) {
        merged.logging.level = override.logging.level;
    }
    if (!override.logging.logPath.empty()) {
        merged.logging.logPath = override.logging.logPath;
    }
    
    // 对于复杂配置，只有整个分组明确偏离默认值时才覆盖，避免默认构造的
    // partial override 把已有配置静默重置。需要逐字段表达“清空/关闭”的
    // 调用方应使用完整配置对象，而不是依赖这个部分合并函数。
    if (override.mqtt5.sessionExpiryInterval != defaults.mqtt5.sessionExpiryInterval ||
        override.mqtt5.messageExpiryInterval != defaults.mqtt5.messageExpiryInterval ||
        override.mqtt5.receiveMaximum != defaults.mqtt5.receiveMaximum ||
        override.mqtt5.maximumPacketSize != defaults.mqtt5.maximumPacketSize ||
        override.mqtt5.requestResponseInformation != defaults.mqtt5.requestResponseInformation ||
        override.mqtt5.requestProblemInformation != defaults.mqtt5.requestProblemInformation ||
        override.mqtt5.willMessage.enabled != defaults.mqtt5.willMessage.enabled ||
        !override.mqtt5.willMessage.topic.empty() ||
        !override.mqtt5.willMessage.payload.empty() ||
        !override.mqtt5.userProperties.empty()) {
        merged.mqtt5 = override.mqtt5;
    }
    if (override.reconnect.baseInterval != defaults.reconnect.baseInterval ||
        override.reconnect.maxInterval != defaults.reconnect.maxInterval ||
        override.reconnect.maxAttempts != defaults.reconnect.maxAttempts ||
        override.reconnect.minJitter != defaults.reconnect.minJitter ||
        override.reconnect.maxJitter != defaults.reconnect.maxJitter ||
        override.reconnect.enableExponentialBackoff != defaults.reconnect.enableExponentialBackoff ||
        override.reconnect.enableJitter != defaults.reconnect.enableJitter) {
        merged.reconnect = override.reconnect;
    }
    if (override.monitoring.networkCheckInterval != defaults.monitoring.networkCheckInterval ||
        override.monitoring.networkTimeout != defaults.monitoring.networkTimeout ||
        override.monitoring.enableNetworkMonitor != defaults.monitoring.enableNetworkMonitor ||
        override.monitoring.connectionCheckInterval != defaults.monitoring.connectionCheckInterval ||
        override.monitoring.enableConnectionMonitor != defaults.monitoring.enableConnectionMonitor ||
        override.monitoring.heartbeatInterval != defaults.monitoring.heartbeatInterval ||
        override.monitoring.heartbeatQoS != defaults.monitoring.heartbeatQoS ||
        override.monitoring.enableHeartbeat != defaults.monitoring.enableHeartbeat ||
        !override.monitoring.heartbeatTopic.empty()) {
        merged.monitoring = override.monitoring;
    }
    if (override.security.enableTLS != defaults.security.enableTLS ||
        override.security.tlsVersion != defaults.security.tlsVersion ||
        !override.security.cipherSuites.empty() ||
        override.security.verifyCertificate != defaults.security.verifyCertificate ||
        override.security.verifyDepth != defaults.security.verifyDepth ||
        !override.security.caCertificatePath.empty() ||
        !override.security.clientCertificatePath.empty() ||
        !override.security.clientPrivateKeyPath.empty()) {
        merged.security = override.security;
    }
    
    return merged;
}

Result<bool> MqttConfigManager::hotUpdate(const MqttConfig& newConfig) {
    // 先验证配置（不需要锁，使用 if 初始化语句）
    if (auto validation = validate(newConfig); !validation) {
        return validation;
    }
    
    // 复制回调列表，避免在持有锁时执行回调
    std::vector<std::function<void(const MqttConfig&)>> callbacks;
    MqttConfig updatedConfig;
    bool configChanged = false;
    
    {
        std::lock_guard lock(mutex_);
        
        // 直接检查配置是否已更改（避免调用hasConfigChanged导致死锁）
        // 只检查支持热更新的配置项
        if (currentConfig_.logging.level != newConfig.logging.level ||
            currentConfig_.monitoring.enableNetworkMonitor != newConfig.monitoring.enableNetworkMonitor ||
            currentConfig_.monitoring.networkCheckInterval != newConfig.monitoring.networkCheckInterval) {
            configChanged = true;
        }
        
        if (!configChanged) {
            return Result<bool>::Success(true);
        }
        
        // 执行热更新（仅更新支持热更新的配置项）
        // 支持热更新的配置项：
        // - 日志级别
        // - 监控配置
        // - 性能优化配置（部分）
        
        // 更新日志级别
        currentConfig_.logging.level = newConfig.logging.level;
        
        // 更新监控配置
        currentConfig_.monitoring = newConfig.monitoring;
        
        // 更新性能配置
        currentConfig_.performance = newConfig.performance;
        
        // 复制当前配置和回调列表（在锁内）
        updatedConfig = currentConfig_;
        callbacks = updateCallbacks_;
    }
    
    // 在锁外执行回调，避免死锁和长时间持有锁
    for (const auto& callback : callbacks) {
        try {
            callback(updatedConfig);
        } catch (const std::exception& e) {
            LOG_ERROR("配置更新回调执行失败: " + std::string(e.what()));
        }
    }
    
    return Result<bool>::Success(true);
}

void MqttConfigManager::registerUpdateCallback(const std::function<void(const MqttConfig&)>& callback) {
    std::lock_guard lock(mutex_);
    updateCallbacks_.push_back(callback);
}

MqttConfig MqttConfigManager::getCurrentConfig() const {
    std::lock_guard lock(mutex_);
    return currentConfig_;
}

void MqttConfigManager::setCurrentConfig(const MqttConfig& config) {
    std::lock_guard lock(mutex_);
    currentConfig_ = config;
}

bool MqttConfigManager::hasConfigChanged(const MqttConfig& newConfig) const {
    std::lock_guard lock(mutex_);
    
    // 简单比较：比较关键配置项
    if (currentConfig_.basic.version != newConfig.basic.version ||
        currentConfig_.server.host != newConfig.server.host ||
        currentConfig_.server.port != newConfig.server.port ||
        currentConfig_.logging.level != newConfig.logging.level) {
        return true;
    }
    
    return false;
}

Result<std::string> MqttConfigManager::fetchConfigFromServer(
    [[maybe_unused]] const std::string& serverUrl,
    const std::string& /* deviceId */,
    const std::string& /* authToken */) {
    
    // 这里需要HTTP客户端库，暂时返回不支持
    // 用户可以实现自己的配置获取逻辑
    return Result<std::string>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                 "从服务器获取配置功能暂未实现，请使用本地配置文件"));
}

Result<MqttConfig> MqttConfigManager::parseJsonConfigImpl(
    [[maybe_unused]] const std::string& jsonStr) {
#ifdef ENABLE_JSON
    try {
        json j = json::parse(jsonStr);
        MqttConfig config;

        const json& root = j.contains("mqtt") && j["mqtt"].is_object() ? j["mqtt"] : j;
        auto object = [](const json& parent, const char* key) -> const json* {
            if (parent.contains(key) && parent.at(key).is_object()) {
                return &parent.at(key);
            }
            return nullptr;
        };
        auto readString = [](const json& obj, const char* key, std::string& value) {
            if (obj.contains(key)) value = obj.at(key).get<std::string>();
        };
        auto readBool = [](const json& obj, const char* key, bool& value) {
            if (obj.contains(key)) value = obj.at(key).get<bool>();
        };
        auto readInt = [](const json& obj, const char* key, int& value) {
            if (obj.contains(key)) value = obj.at(key).get<int>();
        };
        auto readLong = [](const json& obj, const char* key, long& value) {
            if (obj.contains(key)) value = obj.at(key).get<long>();
        };
        auto readSize = [](const json& obj, const char* key, size_t& value) {
            if (obj.contains(key)) value = obj.at(key).get<size_t>();
        };
        auto readDouble = [](const json& obj, const char* key, double& value) {
            if (obj.contains(key)) value = obj.at(key).get<double>();
        };
        auto readQoS = [&](const json& obj, const char* key, QoS& value) {
            if (obj.contains(key)) value = static_cast<QoS>(obj.at(key).get<int>());
        };

        if (const auto* basic = object(root, "basic")) {
            readString(*basic, "version", config.basic.version);
            if (basic->contains("client_id_prefix")) {
                readString(*basic, "client_id_prefix", config.basic.clientIdPrefix);
            } else {
                readString(*basic, "clientIdPrefix", config.basic.clientIdPrefix);
            }
            if (basic->contains("clean_start")) {
                readBool(*basic, "clean_start", config.basic.cleanStart);
            } else {
                readBool(*basic, "cleanStart", config.basic.cleanStart);
            }
            if (basic->contains("client_id")) {
                readString(*basic, "client_id", config.basic.clientId);
            } else {
                readString(*basic, "clientId", config.basic.clientId);
            }
        }

        if (const auto* server = object(root, "server")) {
            readString(*server, "host", config.server.host);
            readInt(*server, "port", config.server.port);
            if (server->contains("use_ssl")) readBool(*server, "use_ssl", config.server.useSSL);
            else readBool(*server, "useSSL", config.server.useSSL);
            if (server->contains("connect_timeout")) readInt(*server, "connect_timeout", config.server.connectTimeout);
            else readInt(*server, "connectTimeout", config.server.connectTimeout);
            if (server->contains("keep_alive")) readInt(*server, "keep_alive", config.server.keepAlive);
            else readInt(*server, "keepAlive", config.server.keepAlive);
            if (server->contains("backup_servers") && server->at("backup_servers").is_array()) {
                for (const auto& item : server->at("backup_servers")) {
                    MqttConfig::ServerConfig::BackupServer backup;
                    readString(item, "host", backup.host);
                    readInt(item, "port", backup.port);
                    if (item.contains("use_ssl")) readBool(item, "use_ssl", backup.useSSL);
                    else readBool(item, "useSSL", backup.useSSL);
                    config.server.backupServers.push_back(std::move(backup));
                }
            }
        }

        if (const auto* auth = object(root, "auth")) {
            readString(*auth, "username", config.auth.username);
            readString(*auth, "password", config.auth.password);
            readString(*auth, "token", config.auth.token);
            if (auth->contains("use_token_auth")) readBool(*auth, "use_token_auth", config.auth.useTokenAuth);
            else readBool(*auth, "useTokenAuth", config.auth.useTokenAuth);
            if (auth->contains("token_refresh_interval")) readInt(*auth, "token_refresh_interval", config.auth.tokenRefreshInterval);
            else readInt(*auth, "tokenRefreshInterval", config.auth.tokenRefreshInterval);
        }

        if (const auto* mqtt5 = object(root, "mqtt5")) {
            readInt(*mqtt5, "session_expiry_interval", config.mqtt5.sessionExpiryInterval);
            readInt(*mqtt5, "message_expiry_interval", config.mqtt5.messageExpiryInterval);
            readInt(*mqtt5, "receive_maximum", config.mqtt5.receiveMaximum);
            readInt(*mqtt5, "maximum_packet_size", config.mqtt5.maximumPacketSize);
            readBool(*mqtt5, "request_response_information", config.mqtt5.requestResponseInformation);
            readBool(*mqtt5, "request_problem_information", config.mqtt5.requestProblemInformation);
            if (mqtt5->contains("user_properties")) config.mqtt5.userProperties = mqtt5->at("user_properties").get<std::map<std::string, std::string>>();
            if (const auto* will = object(*mqtt5, "will_message")) {
                readBool(*will, "enabled", config.mqtt5.willMessage.enabled);
                readString(*will, "topic", config.mqtt5.willMessage.topic);
                readQoS(*will, "qos", config.mqtt5.willMessage.qos);
                readBool(*will, "retained", config.mqtt5.willMessage.retained);
                readString(*will, "payload", config.mqtt5.willMessage.payload);
                readInt(*will, "delay_interval", config.mqtt5.willMessage.delayInterval);
            }
        }

        if (const auto* reconnect = object(root, "reconnect")) {
            readLong(*reconnect, "base_interval", config.reconnect.baseInterval);
            readLong(*reconnect, "max_interval", config.reconnect.maxInterval);
            readInt(*reconnect, "max_attempts", config.reconnect.maxAttempts);
            readDouble(*reconnect, "min_jitter", config.reconnect.minJitter);
            readDouble(*reconnect, "max_jitter", config.reconnect.maxJitter);
            readBool(*reconnect, "enable_exponential_backoff", config.reconnect.enableExponentialBackoff);
            readBool(*reconnect, "enable_jitter", config.reconnect.enableJitter);
        }

        if (const auto* monitoring = object(root, "monitoring")) {
            readInt(*monitoring, "network_check_interval", config.monitoring.networkCheckInterval);
            readInt(*monitoring, "network_timeout", config.monitoring.networkTimeout);
            readBool(*monitoring, "enable_network_monitor", config.monitoring.enableNetworkMonitor);
            readInt(*monitoring, "connection_check_interval", config.monitoring.connectionCheckInterval);
            readBool(*monitoring, "enable_connection_monitor", config.monitoring.enableConnectionMonitor);
            readInt(*monitoring, "heartbeat_interval", config.monitoring.heartbeatInterval);
            readQoS(*monitoring, "heartbeat_qos", config.monitoring.heartbeatQoS);
            readBool(*monitoring, "enable_heartbeat", config.monitoring.enableHeartbeat);
            readString(*monitoring, "heartbeat_topic", config.monitoring.heartbeatTopic);
        }

        if (const auto* security = object(root, "security")) {
            readBool(*security, "enable_tls", config.security.enableTLS);
            readString(*security, "tls_version", config.security.tlsVersion);
            if (security->contains("cipher_suites")) config.security.cipherSuites = security->at("cipher_suites").get<std::vector<std::string>>();
            readBool(*security, "verify_certificate", config.security.verifyCertificate);
            readInt(*security, "verify_depth", config.security.verifyDepth);
            readString(*security, "ca_certificate_path", config.security.caCertificatePath);
            readString(*security, "client_certificate_path", config.security.clientCertificatePath);
            readString(*security, "client_private_key_path", config.security.clientPrivateKeyPath);
            if (const auto* storage = object(*security, "certificate_storage")) {
                readString(*storage, "storage_path", config.security.certificateStorage.storagePath);
                readString(*storage, "encryption_algorithm", config.security.certificateStorage.encryptionAlgorithm);
                readBool(*storage, "use_device_key", config.security.certificateStorage.useDeviceKey);
                readBool(*storage, "use_hsm", config.security.certificateStorage.useHSM);
            }
            if (const auto* access = object(*security, "access_control")) {
                readBool(*access, "enable_acl", config.security.accessControl.enableACL);
                readBool(*access, "verify_process_identity", config.security.accessControl.verifyProcessIdentity);
            }
        }

        if (const auto* logging = object(root, "logging")) {
            if (logging->contains("level")) config.logging.level = static_cast<LogLevel>(logging->at("level").get<int>());
            readString(*logging, "log_path", config.logging.logPath);
            readSize(*logging, "max_file_size", config.logging.maxFileSize);
            readInt(*logging, "max_files", config.logging.maxFiles);
            readBool(*logging, "async", config.logging.async);
            readBool(*logging, "rotate_on_startup", config.logging.rotateOnStartup);
            readString(*logging, "logger_type", config.logging.loggerType);
        }

        if (const auto* thread = object(root, "thread")) {
            readInt(*thread, "network_monitor_threads", config.thread.networkMonitorThreads);
            readInt(*thread, "connection_monitor_threads", config.thread.connectionMonitorThreads);
            readInt(*thread, "heartbeat_threads", config.thread.heartbeatThreads);
            readInt(*thread, "message_processor_threads", config.thread.messageProcessorThreads);
            readInt(*thread, "reconnect_threads", config.thread.reconnectThreads);
            readInt(*thread, "total_threads", config.thread.totalThreads);
        }
        if (const auto* resource = object(root, "resource")) {
            readSize(*resource, "memory_limit", config.resource.memoryLimit);
            readDouble(*resource, "memory_warning_threshold", config.resource.memoryWarningThreshold);
            readInt(*resource, "max_file_descriptors", config.resource.maxFileDescriptors);
            readInt(*resource, "max_threads", config.resource.maxThreads);
            readInt(*resource, "max_connections", config.resource.maxConnections);
        }
        if (const auto* queue = object(root, "message_queue")) {
            readSize(*queue, "max_send_queue_size", config.messageQueue.maxSendQueueSize);
            readSize(*queue, "max_receive_queue_size", config.messageQueue.maxReceiveQueueSize);
            readInt(*queue, "max_retry", config.messageQueue.maxRetry);
            readInt(*queue, "retry_interval", config.messageQueue.retryInterval);
            readBool(*queue, "enable_priority", config.messageQueue.enablePriority);
        }
        if (const auto* persistence = object(root, "persistence")) {
            if (const auto* buffer = object(*persistence, "buffer")) {
                readSize(*buffer, "max_send_buffer_size", config.persistence.buffer.maxSendBufferSize);
                readSize(*buffer, "max_receive_buffer_size", config.persistence.buffer.maxReceiveBufferSize);
                readBool(*buffer, "enable_send_persistence", config.persistence.buffer.enableSendPersistence);
                readBool(*buffer, "enable_receive_persistence", config.persistence.buffer.enableReceivePersistence);
            }
            if (const auto* storage = object(*persistence, "storage")) {
                readString(*storage, "storage_path", config.persistence.storage.storagePath);
                readString(*storage, "storage_type", config.persistence.storage.storageType);
                readBool(*storage, "enable_compression", config.persistence.storage.enableCompression);
                readInt(*storage, "flush_interval", config.persistence.storage.flushInterval);
            }
            if (const auto* idempotency = object(*persistence, "idempotency")) {
                readBool(*idempotency, "enable_idempotency", config.persistence.idempotency.enableIdempotency);
                if (idempotency->contains("retention_time")) config.persistence.idempotency.retentionTime = idempotency->at("retention_time").get<time_t>();
                if (idempotency->contains("cleanup_interval")) config.persistence.idempotency.cleanupInterval = idempotency->at("cleanup_interval").get<time_t>();
                readBool(*idempotency, "enable_persistence", config.persistence.idempotency.enablePersistence);
            }
            if (const auto* recovery = object(*persistence, "recovery")) {
                readBool(*recovery, "enable_fast_recovery", config.persistence.recovery.enableFastRecovery);
                readInt(*recovery, "max_recovery_time", config.persistence.recovery.maxRecoveryTime);
                readBool(*recovery, "parallel_recovery", config.persistence.recovery.parallelRecovery);
            }
        }
        if (const auto* performance = object(root, "performance")) {
            if (const auto* batch = object(*performance, "batch")) {
                readBool(*batch, "enable_batch_send", config.performance.batch.enableBatchSend);
                readSize(*batch, "max_batch_size", config.performance.batch.maxBatchSize);
                readInt(*batch, "max_batch_delay", config.performance.batch.maxBatchDelay);
                readBool(*batch, "enable_batch_receive", config.performance.batch.enableBatchReceive);
            }
            if (const auto* zeroCopy = object(*performance, "zero_copy")) {
                readBool(*zeroCopy, "enable_zero_copy", config.performance.zeroCopy.enableZeroCopy);
                readBool(*zeroCopy, "use_shared_ptr", config.performance.zeroCopy.useSharedPtr);
            }
            if (const auto* objectPool = object(*performance, "object_pool")) {
                readBool(*objectPool, "enable_object_pool", config.performance.objectPool.enableObjectPool);
                readSize(*objectPool, "message_pool_size", config.performance.objectPool.messagePoolSize);
                readSize(*objectPool, "buffer_pool_size", config.performance.objectPool.bufferPoolSize);
            }
            if (const auto* cache = object(*performance, "cache")) {
                readBool(*cache, "enable_dns_cache", config.performance.cache.enableDNSCache);
                if (cache->contains("dns_cache_ttl")) config.performance.cache.dnsCacheTTL = cache->at("dns_cache_ttl").get<time_t>();
                readBool(*cache, "enable_subscription_cache", config.performance.cache.enableSubscriptionCache);
            }
            if (const auto* compression = object(*performance, "compression")) {
                readBool(*compression, "enable_compression", config.performance.compression.enableCompression);
                readSize(*compression, "compression_threshold", config.performance.compression.compressionThreshold);
                readString(*compression, "compression_algorithm", config.performance.compression.compressionAlgorithm);
            }
            if (const auto* delayed = object(*performance, "delayed_send")) {
                readBool(*delayed, "enable_delayed_send", config.performance.delayedSend.enableDelayedSend);
                readInt(*delayed, "delay_window", config.performance.delayedSend.delayWindow);
                readSize(*delayed, "merge_threshold", config.performance.delayedSend.mergeThreshold);
            }
        }
        if (const auto* metrics = object(root, "metrics")) {
            readBool(*metrics, "enable_metrics", config.metrics.enableMetrics);
            readInt(*metrics, "report_interval", config.metrics.reportInterval);
            readBool(*metrics, "enable_auto_report", config.metrics.enableAutoReport);
            readString(*metrics, "report_endpoint", config.metrics.reportEndpoint);
            readBool(*metrics, "collect_connection_metrics", config.metrics.collectConnectionMetrics);
            readBool(*metrics, "collect_message_metrics", config.metrics.collectMessageMetrics);
            readBool(*metrics, "collect_network_metrics", config.metrics.collectNetworkMetrics);
            readBool(*metrics, "collect_performance_metrics", config.metrics.collectPerformanceMetrics);
        }
        if (const auto* errors = object(root, "error_handling")) {
            readBool(*errors, "enable_global_exception_handler", config.errorHandling.enableGlobalExceptionHandler);
            readBool(*errors, "enable_signal_handler", config.errorHandling.enableSignalHandler);
            readBool(*errors, "enable_thread_exception_handler", config.errorHandling.enableThreadExceptionHandler);
            readBool(*errors, "enable_auto_recover", config.errorHandling.enableAutoRecover);
            readBool(*errors, "log_errors", config.errorHandling.logErrors);
        }
        if (const auto* qos = object(root, "qos_policy")) {
            readQoS(*qos, "heartbeat", config.qosPolicy.heartbeat);
            readQoS(*qos, "status_report", config.qosPolicy.statusReport);
            readQoS(*qos, "print_task", config.qosPolicy.printTask);
            readQoS(*qos, "control_command", config.qosPolicy.controlCommand);
            readQoS(*qos, "financial_data", config.qosPolicy.financialData);
            if (qos->contains("topic_qos_map")) {
                for (const auto& [topic, value] : qos->at("topic_qos_map").items()) {
                    config.qosPolicy.topicQoSMap[topic] = static_cast<QoS>(value.get<int>());
                }
            }
        }
        if (const auto* topics = object(root, "topics")) {
            readString(*topics, "heartbeat", config.topics.heartbeat);
            readString(*topics, "status", config.topics.status);
            readString(*topics, "print_task", config.topics.printTask);
            readString(*topics, "print_result", config.topics.printResult);
            readString(*topics, "command", config.topics.command);
            if (topics->contains("custom_topics")) config.topics.customTopics = topics->at("custom_topics").get<std::map<std::string, std::string>>();
        }
        
        return Result<MqttConfig>::Success(config);
    } catch (const std::exception& e) {
        return Result<MqttConfig>::Failure(
            MqttError(MqttErrorCode::CONFIG_ERROR,
                     "JSON解析失败: " + std::string(e.what())));
    }
#else
    return Result<MqttConfig>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                 "JSON支持未启用"));
#endif
}

std::string MqttConfigManager::detectFileFormat(const std::string& filePath) {
    // 使用 C++17 std::filesystem 处理路径，自动处理边界情况
    const fs::path path(filePath);
    std::string ext = path.extension().string();
    
    // 去掉前导点号（extension() 返回的格式是 ".ext"）
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    
    // 转换为小写
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "json") {
        return "json";
    }
    if (ext == "yaml" || ext == "yml") {
        return "yaml";
    }

    // 默认尝试JSON
    return "json";
}

} // namespace mqtt_client
