/**
 * @file storage_engine.h
 * @brief 存储引擎接口
 * 
 * 提供统一的存储接口，支持文件存储、SQLite等不同存储后端。
 */

#ifndef MQTT_CLIENT_PERSISTENCE_STORAGE_ENGINE_H
#define MQTT_CLIENT_PERSISTENCE_STORAGE_ENGINE_H

#include "mqtt_client/core/result.h"
#include <string>
#include <memory>

namespace mqtt_client {

/**
 * @brief 存储引擎接口
 * 
 * 提供统一的存储接口，支持不同的存储后端实现
 */
class StorageEngine {
public:
    virtual ~StorageEngine() = default;
    
    /**
     * @brief 写入数据
     * 
     * @param key 数据键（通常是文件路径）
     * @param data 数据内容
     * @return Result<bool> 写入结果
     */
    virtual Result<bool> write(const std::string& key, const std::string& data) = 0;
    
    /**
     * @brief 读取数据
     * 
     * @param key 数据键（通常是文件路径）
     * @return Result<std::string> 读取的数据
     */
    virtual Result<std::string> read(const std::string& key) = 0;
    
    /**
     * @brief 删除数据
     * 
     * @param key 数据键（通常是文件路径）
     * @return Result<bool> 删除结果
     */
    virtual Result<bool> remove(const std::string& key) = 0;
    
    /**
     * @brief 检查数据是否存在
     * 
     * @param key 数据键（通常是文件路径）
     * @return bool 是否存在
     */
    virtual bool exists(const std::string& key) = 0;
    
    /**
     * @brief 清空所有数据
     * 
     * @return Result<bool> 清空结果
     */
    virtual Result<bool> clear() = 0;
};

/**
 * @brief 文件存储引擎
 * 
 * 基于文件系统的存储实现，使用JSON格式存储数据
 */
class FileStorageEngine : public StorageEngine {
public:
    /**
     * @brief 构造函数
     * 
     * @param basePath 基础存储路径
     */
    explicit FileStorageEngine(const std::string& basePath);
    
    /**
     * @brief 析构函数
     */
    ~FileStorageEngine() override = default;
    
    /**
     * @brief 写入数据
     */
    Result<bool> write(const std::string& key, const std::string& data) override;
    
    /**
     * @brief 读取数据
     */
    Result<std::string> read(const std::string& key) override;
    
    /**
     * @brief 删除数据
     */
    Result<bool> remove(const std::string& key) override;
    
    /**
     * @brief 检查数据是否存在
     */
    bool exists(const std::string& key) override;
    
    /**
     * @brief 清空所有数据
     */
    Result<bool> clear() override;
    
    /**
     * @brief 获取完整路径
     */
    std::string getFullPath(const std::string& key) const;
    
private:
    std::string basePath_;
    
    /**
     * @brief 确保目录存在
     */
    Result<bool> ensureDirectory(const std::string& path);
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_PERSISTENCE_STORAGE_ENGINE_H
