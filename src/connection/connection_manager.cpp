/**
 * @file connection_manager.cpp
 * @brief MQTT连接管理器实现
 */

#include "mqtt_client/connection/connection_manager.h"
#include "mqtt_client/adapter/wolfmqtt_adapter.h"
#include "mqtt_client/logger/logger_interface.h"
#include <chrono>

namespace mqtt_client {

MqttConnectionManager::MqttConnectionManager(const MqttConfig& config,
                                             WolfMqttAdapter* adapter)
    : config_(config)
    , adapter_(nullptr, [](WolfMqttAdapter*){})  // 先初始化为空，使用no-op删除器
    , ownsAdapter_(adapter == nullptr)
    , state_(ConnectionState::DISCONNECTED)
    , connected_(false)
    , lastConnectTime_(0)
    , reconnectCount_(0)
{
    // 如果外部传入了适配器，使用外部适配器（不拥有所有权）
    if (adapter) {
        // 使用自定义删除器，不实际删除对象
        adapter_ = std::unique_ptr<WolfMqttAdapter, std::function<void(WolfMqttAdapter*)>>(
            adapter, [](WolfMqttAdapter*){});
        ownsAdapter_ = false;
    } else {
        // 创建新的适配器，使用默认删除器
        adapter_ = std::unique_ptr<WolfMqttAdapter, std::function<void(WolfMqttAdapter*)>>(
            std::make_unique<WolfMqttAdapter>(config).release(),
            [](WolfMqttAdapter* ptr) { delete ptr; });
        ownsAdapter_ = true;
    }
    
    // 延迟设置回调，避免在构造时立即触发mutex问题
    // 回调将在第一次调用 connect() 时设置
}

MqttConnectionManager::~MqttConnectionManager() {
    // 直接更新原子变量，不尝试获取mutex
    // 在析构函数中获取mutex可能导致问题，因为对象可能正在被销毁
    connected_.store(false);
    state_.store(ConnectionState::DISCONNECTED);
    
    // 如果拥有适配器，尝试清理（可能失败，但不影响析构）
    if (ownsAdapter_ && adapter_) {
        try {
            adapter_->cleanup();
        } catch (...) {
            // 忽略清理异常，确保析构能完成
        }
    }
}

Result<bool> MqttConnectionManager::connect() {
    std::lock_guard lock(mutex_);
    
    // 检查是否已连接
    if (connected_.load()) {
        return Result<bool>::Success(true);
    }
    
    // 检查状态
    if (const ConnectionState currentState = state_.load(); currentState == ConnectionState::CONNECTING) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INVALID_STATE,
                     "连接正在进行中"));
    }
    
    // 确保适配器已初始化
    if (!adapter_) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "适配器未初始化"));
    }
    
    // 更新状态为连接中
    updateState(ConnectionState::CONNECTING);
    
    // 初始化适配器（如果尚未初始化）
    if (auto initResult = adapter_->initialize(); !initResult) {
        updateState(ConnectionState::DISCONNECTED);
        handleConnectFailure("适配器初始化失败: " + initResult.error.message);
        return initResult;
    }
    
    // 执行连接
    if (auto connectResult = adapter_->connect(); !connectResult) {
        updateState(ConnectionState::DISCONNECTED);
        handleConnectFailure("连接失败: " + connectResult.error.message);
        return connectResult;
    }
    
    // 连接成功
    updateState(ConnectionState::CONNECTED);
    connected_.store(true);
    lastConnectTime_.store(std::time(nullptr));
    
    handleConnectSuccess();
    
    return Result<bool>::Success(true);
}

Result<bool> MqttConnectionManager::disconnect() {
    // 使用try_lock避免在析构时死锁
    const std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        // 无法获取锁，可能正在析构，直接返回成功
        connected_.store(false);
        state_.store(ConnectionState::DISCONNECTED);
        return Result<bool>::Success(true);
    }
    
    // 检查是否已断开
    if (!connected_.load()) {
        return Result<bool>::Success(true);
    }
    
    // 更新状态为断开中
    updateState(ConnectionState::DISCONNECTING);
    
    // 执行断开
    if (adapter_) {
        if (const auto disconnectResult = adapter_->disconnect(); !disconnectResult) {
            // 即使断开失败，也标记为已断开
            LOG_WARN("断开连接时出错: " + disconnectResult.error.message);
        }
    }
    
    // 更新状态
    updateState(ConnectionState::DISCONNECTED);
    connected_.store(false);
    
    return Result<bool>::Success(true);
}

Result<bool> MqttConnectionManager::reconnect() {
    std::lock_guard lock(mutex_);
    
    // 先断开当前连接
    if (connected_.load()) {
        if (const auto result = disconnect(); !result) {
            LOG_ERROR("断开当前连接失败: " + result.error.message);
            return Result<bool>::Failure(result.error);
        }
    }
    
    // 增加重连计数
    reconnectCount_.fetch_add(1);
    
    // 更新状态为重连中
    updateState(ConnectionState::RECONNECTING);
    
    // 执行连接
    auto result = connect();
    
    if (!result) {
        // 重连失败，恢复为断开状态
        updateState(ConnectionState::DISCONNECTED);
    }
    
    return result;
}

bool MqttConnectionManager::isConnected() const {
    // 不需要获取mutex，直接读取原子变量
    return connected_.load();
}

ConnectionState MqttConnectionManager::getState() const {
    return state_.load();
}

time_t MqttConnectionManager::getLastConnectTime() const {
    return lastConnectTime_.load();
}

int MqttConnectionManager::getReconnectCount() const {
    return reconnectCount_.load();
}

void MqttConnectionManager::resetReconnectCount() {
    reconnectCount_.store(0);
}

void MqttConnectionManager::setOnConnected(const std::function<void()> &callback) {
    std::lock_guard lock(mutex_);
    onConnected_ = callback;
}

void MqttConnectionManager::setOnConnectionLost(const std::function<void(const std::string&)> &callback) {
    std::lock_guard lock(mutex_);
    onConnectionLost_ = callback;
}

void MqttConnectionManager::setOnConnectFailure(const std::function<void(const std::string&)> &callback) {
    std::lock_guard lock(mutex_);
    onConnectFailure_ = callback;
}

WolfMqttAdapter* MqttConnectionManager::getAdapter() const {
    return adapter_.get();
}

void MqttConnectionManager::handleConnectSuccess() {
    LOG_INFO("MQTT连接成功");
    
    // 调用连接成功回调
    if (onConnected_) {
        try {
            onConnected_();
        } catch (const std::exception& e) {
            LOG_ERROR("连接成功回调执行失败: " + std::string(e.what()));
        }
    }
}

void MqttConnectionManager::handleConnectFailure(const std::string& reason) {
    LOG_ERROR("MQTT连接失败: " + reason);
    
    // 调用连接失败回调
    if (onConnectFailure_) {
        try {
            onConnectFailure_(reason);
        } catch (const std::exception& e) {
            LOG_ERROR("连接失败回调执行失败: " + std::string(e.what()));
        }
    }
}

void MqttConnectionManager::handleConnectionLost(const std::string& cause) {
    LOG_WARN("MQTT连接丢失: " + cause);
    
    // 更新状态
    updateState(ConnectionState::DISCONNECTED);
    connected_.store(false);
    
    // 调用连接丢失回调
    if (onConnectionLost_) {
        try {
            onConnectionLost_(cause);
        } catch (const std::exception& e) {
            LOG_ERROR("连接丢失回调执行失败: " + std::string(e.what()));
        }
    }
}

void MqttConnectionManager::updateState(ConnectionState newState) {
    // 直接更新状态，不记录日志（避免在析构时调用LoggerManager导致mutex问题）
    state_.exchange(newState);
}

} // namespace mqtt_client
