/**
 * @file config_manager.cpp
 * @brief MQTT配置管理器实现
 */

#include "mqtt_client/config/config_manager.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/logger/logger_interface.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// 如果启用了nlohmann/json，则使用它进行JSON解析
#ifdef ENABLE_JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#endif

namespace mqtt_client {

MqttConfigManager& MqttConfigManager::getInstance() {
    static MqttConfigManager instance;
    return instance;
}

Result<MqttConfig> MqttConfigManager::loadFromFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return Result<MqttConfig>::Failure(
            MqttError(MqttErrorCode::CONFIG_NOT_FOUND,
                     "配置文件未找到: " + filePath));
    }
    
    std::string format = detectFileFormat(filePath);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    if (format == "json") {
        // 使用不加锁的内部方法，因为当前已经持有锁
        auto result = loadFromJsonImpl(content);
        if (result) {
            currentConfig_ = *result;
        }
        return result;
    } else if (format == "yaml") {
        return loadFromYaml(filePath);
    } else {
        return Result<MqttConfig>::Failure(
            MqttError(MqttErrorCode::CONFIG_ERROR,
                     "不支持的配置文件格式: " + format));
    }
}

Result<MqttConfig> MqttConfigManager::loadFromJson(const std::string& jsonStr) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = loadFromJsonImpl(jsonStr);
    if (result) {
        currentConfig_ = *result;
    }
    return result;
}

Result<MqttConfig> MqttConfigManager::loadFromJsonImpl(const std::string& jsonStr) {
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
    std::lock_guard<std::mutex> lock(mutex_);
    
    // YAML解析需要yaml-cpp库，这里先返回不支持
    return Result<MqttConfig>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                 "YAML格式暂不支持，请使用JSON格式"));
}

MqttConfig MqttConfigManager::getDefaultConfig() const {
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

Result<bool> MqttConfigManager::validate(const MqttConfig& config) const {
    std::vector<std::string> errors = getValidationErrors(config);
    if (errors.empty()) {
        return Result<bool>::Success(true);
    }
    
    std::string errorMsg = "配置验证失败: ";
    for (size_t i = 0; i < errors.size(); ++i) {
        errorMsg += errors[i];
        if (i < errors.size() - 1) {
            errorMsg += "; ";
        }
    }
    
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::CONFIG_VALIDATION_FAILED, errorMsg));
}

std::vector<std::string> MqttConfigManager::getValidationErrors(const MqttConfig& config) const {
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
                                            std::vector<std::string>& errors) const {
    bool valid = true;
    
    if (config.version != "3.1.1" && config.version != "5.0") {
        errors.push_back("MQTT版本必须是'3.1.1'或'5.0'");
        valid = false;
    }
    
    if (config.clientIdPrefix.empty()) {
        errors.push_back("客户端ID前缀不能为空");
        valid = false;
    }
    
    return valid;
}

bool MqttConfigManager::validateServerConfig(const MqttConfig::ServerConfig& config,
                                            std::vector<std::string>& errors) const {
    bool valid = true;
    
    if (config.host.empty()) {
        errors.push_back("服务器地址不能为空");
        valid = false;
    }
    
    if (config.port <= 0 || config.port > 65535) {
        errors.push_back("服务器端口必须在1-65535之间");
        valid = false;
    }
    
    if (config.connectTimeout <= 0) {
        errors.push_back("连接超时必须大于0");
        valid = false;
    }
    
    if (config.keepAlive <= 0) {
        errors.push_back("保活时间必须大于0");
        valid = false;
    }
    
    // 验证备用服务器
    for (size_t i = 0; i < config.backupServers.size(); ++i) {
        const auto& backup = config.backupServers[i];
        if (backup.host.empty()) {
            errors.push_back("备用服务器" + std::to_string(i) + "地址不能为空");
            valid = false;
        }
        if (backup.port <= 0 || backup.port > 65535) {
            errors.push_back("备用服务器" + std::to_string(i) + "端口无效");
            valid = false;
        }
    }
    
    return valid;
}

bool MqttConfigManager::validateSecurityConfig(const MqttConfig::SecurityConfig& config,
                                               std::vector<std::string>& errors) const {
    bool valid = true;
    
    if (config.enableTLS) {
        if (config.tlsVersion != "1.2" && config.tlsVersion != "1.3") {
            errors.push_back("TLS版本必须是'1.2'或'1.3'");
            valid = false;
        }
        
        if (config.verifyCertificate) {
            if (config.caCertificatePath.empty()) {
                errors.push_back("启用证书验证时，CA证书路径不能为空");
                valid = false;
            }
        }
        
        // 如果提供了客户端证书，必须同时提供私钥
        if (!config.clientCertificatePath.empty() && config.clientPrivateKeyPath.empty()) {
            errors.push_back("提供客户端证书时必须同时提供私钥");
            valid = false;
        }
    }
    
    return valid;
}

Result<bool> MqttConfigManager::saveToFile(const MqttConfig& config, const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 先验证配置
    auto validation = validate(config);
    if (!validation) {
        return Result<bool>::Failure(validation.error);
    }
    
    // 保存为JSON格式
    auto jsonResult = saveToJson(config);
    if (!jsonResult) {
        return Result<bool>::Failure(jsonResult.error);
    }
    
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::FILE_ERROR,
                     "无法打开文件进行写入: " + filePath));
    }
    
    file << *jsonResult;
    file.close();
    
    return Result<bool>::Success(true);
}

Result<std::string> MqttConfigManager::saveToJson(const MqttConfig& config) const {
    (void)config;  // 避免未使用参数警告（当JSON未启用时）
#ifdef ENABLE_JSON
    try {
        json j;
        
        // 基础配置
        j["mqtt"]["basic"]["version"] = config.basic.version;
        j["mqtt"]["basic"]["client_id_prefix"] = config.basic.clientIdPrefix;
        j["mqtt"]["basic"]["clean_start"] = config.basic.cleanStart;
        if (!config.basic.clientId.empty()) {
            j["mqtt"]["basic"]["client_id"] = config.basic.clientId;
        }
        
        // 服务器配置
        j["mqtt"]["server"]["host"] = config.server.host;
        j["mqtt"]["server"]["port"] = config.server.port;
        j["mqtt"]["server"]["use_ssl"] = config.server.useSSL;
        j["mqtt"]["server"]["connect_timeout"] = config.server.connectTimeout;
        j["mqtt"]["server"]["keep_alive"] = config.server.keepAlive;
        
        // 认证配置
        if (!config.auth.username.empty()) {
            j["mqtt"]["auth"]["username"] = config.auth.username;
        }
        if (!config.auth.password.empty()) {
            j["mqtt"]["auth"]["password"] = config.auth.password;
        }
        
        // 日志配置
        j["mqtt"]["logging"]["level"] = static_cast<int>(config.logging.level);
        j["mqtt"]["logging"]["log_path"] = config.logging.logPath;
        
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

MqttConfig MqttConfigManager::merge(const MqttConfig& base, const MqttConfig& override) const {
    MqttConfig merged = base;
    
    // 合并基础配置
    if (!override.basic.version.empty()) {
        merged.basic.version = override.basic.version;
    }
    if (!override.basic.clientIdPrefix.empty()) {
        merged.basic.clientIdPrefix = override.basic.clientIdPrefix;
    }
    merged.basic.cleanStart = override.basic.cleanStart;
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
    merged.server.useSSL = override.server.useSSL;
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
    
    // 合并日志配置
    merged.logging.level = override.logging.level;
    if (!override.logging.logPath.empty()) {
        merged.logging.logPath = override.logging.logPath;
    }
    
    // 注意：这里只合并了主要配置项，完整实现需要合并所有配置项
    // 为了简化，其他配置项保持base的值
    
    return merged;
}

Result<bool> MqttConfigManager::hotUpdate(const MqttConfig& newConfig) {
    // 先验证配置（不需要锁）
    auto validation = validate(newConfig);
    if (!validation) {
        return validation;
    }
    
    // 复制回调列表，避免在持有锁时执行回调
    std::vector<std::function<void(const MqttConfig&)>> callbacks;
    MqttConfig updatedConfig;
    bool configChanged = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
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

void MqttConfigManager::registerUpdateCallback(std::function<void(const MqttConfig&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    updateCallbacks_.push_back(callback);
}

const MqttConfig& MqttConfigManager::getCurrentConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentConfig_;
}

void MqttConfigManager::setCurrentConfig(const MqttConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentConfig_ = config;
}

bool MqttConfigManager::hasConfigChanged(const MqttConfig& newConfig) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
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
    const std::string& serverUrl,
    const std::string& /* deviceId */,
    const std::string& /* authToken */) {
    
    // 这里需要HTTP客户端库，暂时返回不支持
    // 用户可以实现自己的配置获取逻辑
    (void)serverUrl;  // 避免未使用参数警告
    return Result<std::string>::Failure(
        MqttError(MqttErrorCode::CONFIG_ERROR,
                 "从服务器获取配置功能暂未实现，请使用本地配置文件"));
}

Result<MqttConfig> MqttConfigManager::parseJsonConfigImpl(const std::string& jsonStr) const {
#ifdef ENABLE_JSON
    try {
        json j = json::parse(jsonStr);
        MqttConfig config;
        
        // 解析基础配置（支持两种格式：带mqtt前缀和不带）
        if (j.contains("mqtt") && j["mqtt"].contains("basic")) {
            const auto& basic = j["mqtt"]["basic"];
            if (basic.contains("version")) {
                config.basic.version = basic["version"].get<std::string>();
            }
            if (basic.contains("client_id_prefix")) {
                config.basic.clientIdPrefix = basic["client_id_prefix"].get<std::string>();
            }
            if (basic.contains("clean_start")) {
                config.basic.cleanStart = basic["clean_start"].get<bool>();
            }
            if (basic.contains("client_id")) {
                config.basic.clientId = basic["client_id"].get<std::string>();
            }
        } else if (j.contains("basic")) {
            // 支持不带mqtt前缀的格式（测试用例使用的格式）
            const auto& basic = j["basic"];
            if (basic.contains("version")) {
                config.basic.version = basic["version"].get<std::string>();
            }
            if (basic.contains("clientId")) {
                config.basic.clientId = basic["clientId"].get<std::string>();
            } else if (basic.contains("client_id")) {
                config.basic.clientId = basic["client_id"].get<std::string>();
            }
        }
        
        // 解析服务器配置（支持两种格式）
        if (j.contains("mqtt") && j["mqtt"].contains("server")) {
            const auto& server = j["mqtt"]["server"];
            if (server.contains("host")) {
                config.server.host = server["host"].get<std::string>();
            }
            if (server.contains("port")) {
                config.server.port = server["port"].get<int>();
            }
            if (server.contains("use_ssl")) {
                config.server.useSSL = server["use_ssl"].get<bool>();
            }
            if (server.contains("connect_timeout")) {
                config.server.connectTimeout = server["connect_timeout"].get<int>();
            }
            if (server.contains("keep_alive")) {
                config.server.keepAlive = server["keep_alive"].get<int>();
            }
        } else if (j.contains("server")) {
            // 支持不带mqtt前缀的格式（测试用例使用的格式）
            const auto& server = j["server"];
            if (server.contains("host")) {
                config.server.host = server["host"].get<std::string>();
            }
            if (server.contains("port")) {
                config.server.port = server["port"].get<int>();
            }
        }
        
        // 解析认证配置
        if (j.contains("mqtt") && j["mqtt"].contains("auth")) {
            const auto& auth = j["mqtt"]["auth"];
            if (auth.contains("username")) {
                config.auth.username = auth["username"].get<std::string>();
            }
            if (auth.contains("password")) {
                config.auth.password = auth["password"].get<std::string>();
            }
        }
        
        // 解析日志配置
        if (j.contains("mqtt") && j["mqtt"].contains("logging")) {
            const auto& logging = j["mqtt"]["logging"];
            if (logging.contains("level")) {
                int level = logging["level"].get<int>();
                config.logging.level = static_cast<LogLevel>(level);
            }
            if (logging.contains("log_path")) {
                config.logging.logPath = logging["log_path"].get<std::string>();
            }
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

std::string MqttConfigManager::detectFileFormat(const std::string& filePath) const {
    std::string ext = filePath.substr(filePath.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "json") {
        return "json";
    } else if (ext == "yaml" || ext == "yml") {
        return "yaml";
    }
    
    // 默认尝试JSON
    return "json";
}

} // namespace mqtt_client
