/**
 * @file config_manager.h
 * @brief MQTT配置管理器
 * 
 * 统一管理所有配置，支持加载、验证、更新、保存等功能。
 */

#ifndef MQTT_CLIENT_CONFIG_CONFIG_MANAGER_H
#define MQTT_CLIENT_CONFIG_CONFIG_MANAGER_H

#include "mqtt_client/config/config.h"
#include "mqtt_client/core/result.h"
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <mutex>

namespace mqtt_client {

/**
 * @brief 配置管理器
 * 
 * 统一管理所有配置，支持加载、验证、更新。
 * 使用单例模式，确保全局唯一配置实例。
 */
class MqttConfigManager {
public:
    /**
     * @brief 获取单例实例
     * 
     * @return MqttConfigManager& 单例引用
     */
    static MqttConfigManager& getInstance();
    
    /**
     * @brief 从文件加载配置
     * 
     * 自动检测文件格式（JSON/YAML），并加载配置。
     * 
     * @param filePath 配置文件路径
     * @return Result<MqttConfig> 加载结果
     */
    [[nodiscard]] Result<MqttConfig> loadFromFile(const std::string& filePath);
    
    /**
     * @brief 从JSON字符串加载配置
     * 
     * @param json JSON字符串
     * @return Result<MqttConfig> 加载结果
     */
    [[nodiscard]] Result<MqttConfig> loadFromJson(const std::string& json);
    
    /**
     * @brief 从YAML文件加载配置
     * 
     * @param filePath YAML文件路径
     * @return Result<MqttConfig> 加载结果
     */
    [[nodiscard]] static Result<MqttConfig> loadFromYaml(const std::string& filePath);
    
    /**
     * @brief 使用默认配置
     * 
     * @return MqttConfig 默认配置
     */
    static MqttConfig getDefaultConfig();
    
    /**
     * @brief 验证配置
     * 
     * @param config 待验证的配置
     * @return Result<bool> 验证结果，true表示有效
     */
    [[nodiscard]] static Result<bool> validate(const MqttConfig& config);
    
    /**
     * @brief 获取验证错误列表
     * 
     * @param config 待验证的配置
     * @return std::vector<std::string> 错误列表（空表示无错误）
     */
    static std::vector<std::string> getValidationErrors(const MqttConfig& config);
    
    /**
     * @brief 保存配置到文件
     * 
     * @param config 配置对象
     * @param filePath 文件路径
     * @return Result<bool> 保存结果
     */
    [[nodiscard]] Result<bool> saveToFile(const MqttConfig& config, const std::string& filePath);
    
    /**
     * @brief 保存配置到JSON字符串
     * 
     * @param config 配置对象
     * @return Result<std::string> JSON字符串
     */
    [[nodiscard]] static Result<std::string> saveToJson(const MqttConfig& config);
    
    /**
     * @brief 合并配置（用新配置覆盖旧配置）
     * 
     * 将override配置中的非空值覆盖到base配置中。
     * 
     * @param base 基础配置
     * @param override 覆盖配置
     * @return MqttConfig 合并后的配置
     */
    static MqttConfig merge(const MqttConfig& base, const MqttConfig& override);
    
    /**
     * @brief 热更新配置（部分配置支持运行时更新）
     * 
     * 仅更新支持热更新的配置项，其他配置项需要重新初始化。
     * 
     * @param newConfig 新配置
     * @return Result<bool> 更新结果
     */
    [[nodiscard]] Result<bool> hotUpdate(const MqttConfig& newConfig);
    
    /**
     * @brief 注册配置更新回调
     * 
     * 当配置更新时，会调用所有注册的回调函数。
     * 
     * @param callback 回调函数
     */
    void registerUpdateCallback(const std::function<void(const MqttConfig&)>& callback);
    
    /**
     * @brief 获取当前配置
     * 
     * @return const MqttConfig& 当前配置的引用
     */
    const MqttConfig& getCurrentConfig() const;
    
    /**
     * @brief 设置当前配置
     * 
     * @param config 新配置
     */
    void setCurrentConfig(const MqttConfig& config);
    
    /**
     * @brief 检查配置是否已更改
     * 
     * @param newConfig 新配置
     * @return true 配置已更改
     * @return false 配置未更改
     */
    bool hasConfigChanged(const MqttConfig& newConfig) const;
    
    /**
     * @brief 从服务器获取配置（辅助方法）
     * 
     * 提供从HTTP/HTTPS服务器获取配置的辅助方法。
     * 用户也可以自己实现配置获取逻辑。
     * 
     * @param serverUrl 服务器URL
     * @param deviceId 设备ID
     * @param authToken 认证Token（可选）
     * @return Result<std::string> 配置JSON字符串
     */
    [[nodiscard]] static Result<std::string> fetchConfigFromServer(
        const std::string& serverUrl,
        const std::string& deviceId,
        const std::string& authToken = "");
    
private:
    MqttConfigManager() = default;
    ~MqttConfigManager() = default;
    MqttConfigManager(const MqttConfigManager&) = delete;
    MqttConfigManager& operator=(const MqttConfigManager&) = delete;
    
    /**
     * @brief 解析JSON配置
     * 
     * @param json JSON字符串
     * @return Result<MqttConfig> 解析结果
     */
    // 内部方法：解析JSON对象（需要ENABLE_JSON，不加锁）
    [[nodiscard]] static Result<MqttConfig> parseJsonConfigImpl(const std::string& jsonStr);
    
    // 内部方法：加载JSON配置（不加锁版本，供loadFromFile使用）
    [[nodiscard]] static Result<MqttConfig> loadFromJsonImpl(const std::string& jsonStr);
    
    /**
     * @brief 验证基础配置
     * 
     * @param config 基础配置
     * @param errors 错误列表（输出参数）
     * @return true 验证通过
     * @return false 验证失败
     */
    static bool validateBasicConfig(const MqttConfig::BasicConfig& config, 
                                    std::vector<std::string>& errors);
    
    /**
     * @brief 验证服务器配置
     * 
     * @param config 服务器配置
     * @param errors 错误列表（输出参数）
     * @return true 验证通过
     * @return false 验证失败
     */
    static bool validateServerConfig(const MqttConfig::ServerConfig& config,
                                     std::vector<std::string>& errors);
    
    /**
     * @brief 验证安全配置
     * 
     * @param config 安全配置
     * @param errors 错误列表（输出参数）
     * @return true 验证通过
     * @return false 验证失败
     */
    static bool validateSecurityConfig(const MqttConfig::SecurityConfig& config,
                                       std::vector<std::string>& errors);
    
    /**
     * @brief 检测文件格式
     * 
     * @param filePath 文件路径
     * @return std::string 格式名称（"json"或"yaml"）
     */
    static std::string detectFileFormat(const std::string& filePath);
    
    MqttConfig currentConfig_;  ///< 当前配置
    std::vector<std::function<void(const MqttConfig&)>> updateCallbacks_;  ///< 更新回调列表
    mutable std::mutex mutex_;  ///< 互斥锁（保证线程安全）
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_CONFIG_CONFIG_MANAGER_H
