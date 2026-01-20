/**
 * @file storage_engine.cpp
 * @brief 存储引擎实现
 */

#include "mqtt_client/persistence/storage_engine.h"
#include "mqtt_client/core/error.h"
#include "mqtt_client/logger/logger_interface.h"
#include <fmt/core.h>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace mqtt_client {

FileStorageEngine::FileStorageEngine(std::string basePath)
    : basePath_(std::move(basePath)) {
    // 确保基础目录存在
    if (const auto result = ensureDirectory(basePath_); !result.success) {
        LOG_ERROR(fmt::format("路径校验失败，错误原因：{}", result.error.message));
    }
}

Result<bool> FileStorageEngine::write(const std::string& key, const std::string& data) {
    try {
        std::string fullPath = getFullPath(key);
        
        // 确保目录存在
        std::filesystem::path path(fullPath);
        std::string dirPath = path.parent_path().string();
        if (auto dirResult = ensureDirectory(dirPath); !dirResult.success) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                         fmt::format("无法创建目录: {}", dirPath)));
        }
        
        // 写入文件（原子写入：先写临时文件，再重命名）
        std::string tempPath = fullPath + ".tmp";
        std::ofstream file(tempPath, std::ios::binary);
        if (!file.is_open()) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                         fmt::format("无法打开文件写入: {}", tempPath)));
        }
        
        // 显式转换 size_t 到 streamsize，避免窄化转换警告
        file.write(data.c_str(), static_cast<std::streamsize>(data.length()));
        file.close();
        
        if (!file.good()) {
            std::filesystem::remove(tempPath);
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                         fmt::format("写入文件失败: {}", tempPath)));
        }
        
        // 原子重命名
        std::filesystem::rename(tempPath, fullPath);
        
        LOG_DEBUG(fmt::format("写入文件成功: {}", fullPath));
        return Result<bool>::Success(true);
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     fmt::format("写入文件异常: {}", e.what())));
    }
}

Result<std::string> FileStorageEngine::read(const std::string& key) {
    try {
        std::string fullPath = getFullPath(key);
        
        if (!exists(key)) {
            return Result<std::string>::Failure(
                MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                         "文件不存在: " + fullPath));
        }
        
        std::ifstream file(fullPath, std::ios::binary);
        if (!file.is_open()) {
            return Result<std::string>::Failure(
                MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                         fmt::format("无法打开文件读取: {}", fullPath)));
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        
        std::string data = buffer.str();
        LOG_DEBUG(fmt::format("读取文件成功: {}, 大小: {}", fullPath, data.length()));
        return Result<std::string>::Success(data);
    } catch (const std::exception& e) {
        return Result<std::string>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     fmt::format("读取文件异常: {}", e.what())));
    }
}

Result<bool> FileStorageEngine::remove(const std::string& key) {
    try {
        const std::string fullPath = getFullPath(key);
        
        if (!exists(key)) {
            // 文件不存在，认为删除成功
            return Result<bool>::Success(true);
        }

        if (std::filesystem::remove(fullPath)) {
            LOG_DEBUG(fmt::format("删除文件成功: {}", fullPath));
            return Result<bool>::Success(true);
        }
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                      fmt::format("删除文件失败: {}", fullPath)));
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     fmt::format("删除文件异常: {}", e.what())));
    }
}

bool FileStorageEngine::exists(const std::string& key) {
    try {
        std::string fullPath = getFullPath(key);
        return std::filesystem::exists(fullPath) && 
               std::filesystem::is_regular_file(fullPath);
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("检查文件存在性异常: {}", e.what()));
        return false;
    }
}

Result<bool> FileStorageEngine::clear() {
    try {
        if (std::filesystem::exists(basePath_)) {
            std::filesystem::remove_all(basePath_);
            LOG_INFO(fmt::format("清空存储目录: {}", basePath_));
        }
        return Result<bool>::Success(true);
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     fmt::format("清空存储目录异常: {}", e.what())));
    }
}

std::string FileStorageEngine::getFullPath(const std::string& key) const {
    if (key.empty()) {
        return basePath_;
    }
    
    // 如果key已经是绝对路径，直接返回
    if (std::filesystem::path(key).is_absolute()) {
        return key;
    }
    
    // 否则拼接基础路径
    const std::filesystem::path base(basePath_);
    const std::filesystem::path file(key);
    return (base / file).string();
}

    Result<bool> FileStorageEngine::ensureDirectory(const std::string& path) {
    try {
        if (path.empty()) {
            return Result<bool>::Success(true);
        }
        
        if (std::filesystem::exists(path)) {
            if (std::filesystem::is_directory(path)) {
                return Result<bool>::Success(true);
            }
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                          fmt::format("路径已存在但不是目录: {}", path)));
        }
        
        // 创建目录（包括父目录）
        bool created = std::filesystem::create_directories(path);
        if (created || std::filesystem::exists(path)) {
            return Result<bool>::Success(true);
        } else {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                         fmt::format("无法创建目录: {}", path)));
        }
    } catch (const std::exception& e) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PERSISTENCE_ERROR,
                     fmt::format("创建目录异常: {}", e.what())));
    }
}

} // namespace mqtt_client
