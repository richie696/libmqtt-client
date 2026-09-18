/**
 * @file connection_manager.cpp
 * @brief MQTT连接管理器实现
 */

#include "internal/connection/connection_manager.h"
#include "internal/adapter/wolfmqtt_adapter.h"
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
    
    adapter_->setConnectionCallback([this](const bool connected) {
        if (!connected && state_.load() != ConnectionState::DISCONNECTING) {
            handleConnectionLost("底层网络连接已断开");
        }
    });
}

MqttConnectionManager::~MqttConnectionManager() {
    if (adapter_) {
        adapter_->setConnectionCallback({});
    }
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
    WolfMqttAdapter* adapter = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (connected_.load()) {
            return Result<bool>::Success(true);
        }
        if (const ConnectionState currentState = state_.load();
            currentState == ConnectionState::CONNECTING ||
            currentState == ConnectionState::DISCONNECTING) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::INVALID_STATE,
                         "连接状态正在切换"));
        }
        adapter = adapter_.get();
        if (!adapter) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::NOT_INITIALIZED,
                         "适配器未初始化"));
        }
        updateState(ConnectionState::CONNECTING);
    }

    // 初始化适配器（如果尚未初始化）
    if (auto initResult = adapter->initialize(); !initResult) {
        updateState(ConnectionState::DISCONNECTED);
        handleConnectFailure("适配器初始化失败: " + initResult.error.message);
        return initResult;
    }
    
    // 执行连接
    if (auto connectResult = adapter->connect(); !connectResult) {
        updateState(ConnectionState::DISCONNECTED);
        handleConnectFailure("连接失败: " + connectResult.error.message);
        return connectResult;
    }
    
    // 如果接收线程在CONNECT返回后立刻检测到断线，它会把状态改为
    // DISCONNECTED。这里用CAS避免再把失效连接覆盖成CONNECTED。
    connected_.store(true);
    ConnectionState expected = ConnectionState::CONNECTING;
    if (!adapter->isConnected() ||
        !state_.compare_exchange_strong(expected, ConnectionState::CONNECTED)) {
        connected_.store(false);
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NETWORK_ERROR,
                      "连接建立后立即中断"));
    }
    lastConnectTime_.store(std::time(nullptr));
    
    handleConnectSuccess();
    
    return Result<bool>::Success(true);
}

Result<bool> MqttConnectionManager::disconnect(const bool force) {
    WolfMqttAdapter* adapter = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (!connected_.load() && state_.load() == ConnectionState::DISCONNECTED) {
            return Result<bool>::Success(true);
        }
        updateState(ConnectionState::DISCONNECTING);
        adapter = adapter_.get();
    }

    Result<bool> result = Result<bool>::Success(true);
    if (adapter) {
        result = adapter->disconnect(force);
        if (!result) {
            LOG_WARN("断开连接时出错: " + result.error.message);
        }
    }

    updateState(ConnectionState::DISCONNECTED);
    connected_.store(false);
    return result;
}

Result<bool> MqttConnectionManager::reconnect() {
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

    std::function<void()> callback;
    {
        std::lock_guard lock(mutex_);
        callback = onConnected_;
    }
    if (callback) {
        try {
            callback();
        } catch (const std::exception& e) {
            LOG_ERROR("连接成功回调执行失败: " + std::string(e.what()));
        }
    }
}

void MqttConnectionManager::handleConnectFailure(const std::string& reason) {
    LOG_ERROR("MQTT连接失败: " + reason);

    std::function<void(const std::string&)> callback;
    {
        std::lock_guard lock(mutex_);
        callback = onConnectFailure_;
    }
    if (callback) {
        try {
            callback(reason);
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
    
    std::function<void(const std::string&)> callback;
    {
        std::lock_guard lock(mutex_);
        callback = onConnectionLost_;
    }
    if (callback) {
        try {
            callback(cause);
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
