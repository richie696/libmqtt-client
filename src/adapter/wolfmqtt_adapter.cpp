/**
 * @file wolfmqtt_adapter.cpp
 * @brief wolfMQTT适配器实现
 */

#include "mqtt_client/adapter/wolfmqtt_adapter.h"
#include "mqtt_client/logger/logger_interface.h"
#include "mqtt_client/core/error.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <fmt/core.h>

using namespace std::chrono_literals;

#ifdef WOLFMQTT_ENABLED
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/select.h>
    #include <unistd.h>
    #include <cerrno>
#endif

// wolfSSL头文件（如果启用TLS）
#ifdef ENABLE_MQTT_TLS
    #include <wolfssl/ssl.h>
    #include <wolfssl/options.h>
#endif
#endif

namespace mqtt_client {

namespace {

class PendingOperationGuard {
public:
    explicit PendingOperationGuard(std::atomic<unsigned int>& counter)
        : counter_(counter) {
        counter_.fetch_add(1, std::memory_order_acq_rel);
    }

    ~PendingOperationGuard() {
        counter_.fetch_sub(1, std::memory_order_acq_rel);
    }

    PendingOperationGuard(const PendingOperationGuard&) = delete;
    PendingOperationGuard& operator=(const PendingOperationGuard&) = delete;

private:
    std::atomic<unsigned int>& counter_;
};

} // namespace

#ifdef WOLFMQTT_ENABLED
#ifdef ENABLE_MQTT_TLS
int WolfMqttAdapter::tlsCallback(::MqttClient* client) {
    if (!client || !client->ctx) {
        return WOLFSSL_FAILURE;
    }

    if (client->tls.ctx != nullptr) {
        return WOLFSSL_SUCCESS;
    }

    const auto* adapter = static_cast<const WolfMqttAdapter*>(client->ctx);
    const auto& security = adapter->config_.security;

    const int rc = wolfSSL_Init();
    if (rc != WOLFSSL_SUCCESS) {
        LOG_ERROR("wolfSSL初始化失败: " + std::to_string(rc));
        return WOLFSSL_FAILURE;
    }

    client->tls.ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
    if (client->tls.ctx == nullptr) {
        LOG_ERROR("创建wolfSSL上下文失败");
        return WOLFSSL_FAILURE;
    }

    const auto fail = [client](const std::string& message) {
        LOG_ERROR(message);
        if (client->tls.ssl != nullptr) {
            wolfSSL_free(client->tls.ssl);
            client->tls.ssl = nullptr;
        }
        wolfSSL_CTX_free(client->tls.ctx);
        client->tls.ctx = nullptr;
        return WOLFSSL_FAILURE;
    };

    const int minimumVersion = security.tlsVersion == "1.3"
        ? WOLFSSL_TLSV1_3 : WOLFSSL_TLSV1_2;
    if (wolfSSL_CTX_SetMinVersion(client->tls.ctx, minimumVersion)
        != WOLFSSL_SUCCESS) {
        return fail("当前wolfSSL构建不支持配置的TLS版本: " + security.tlsVersion);
    }

    if (security.verifyCertificate) {
        wolfSSL_CTX_set_verify(client->tls.ctx, WOLFSSL_VERIFY_PEER, nullptr);
        wolfSSL_CTX_set_verify_depth(client->tls.ctx, security.verifyDepth);
        if (security.caCertificatePath.empty()) {
            return fail("已启用证书校验，但未配置CA证书路径");
        }
        if (wolfSSL_CTX_load_verify_locations(
                client->tls.ctx, security.caCertificatePath.c_str(), nullptr)
            != WOLFSSL_SUCCESS) {
            return fail("加载CA证书失败: " + security.caCertificatePath);
        }
    } else {
        wolfSSL_CTX_set_verify(client->tls.ctx, WOLFSSL_VERIFY_NONE, nullptr);
        LOG_WARN("TLS证书校验已被显式关闭");
    }

    if (!security.clientCertificatePath.empty()) {
        if (wolfSSL_CTX_use_certificate_chain_file(
                client->tls.ctx, security.clientCertificatePath.c_str())
            != WOLFSSL_SUCCESS) {
            return fail("加载客户端证书失败: " + security.clientCertificatePath);
        }
    }
    if (!security.clientPrivateKeyPath.empty()) {
        if (wolfSSL_CTX_use_PrivateKey_file(
                client->tls.ctx, security.clientPrivateKeyPath.c_str(),
                WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
            return fail("加载客户端私钥失败: " + security.clientPrivateKeyPath);
        }
#if !defined(NO_CHECK_PRIVATE_KEY)
        if (wolfSSL_CTX_check_private_key(client->tls.ctx) != WOLFSSL_SUCCESS) {
            return fail("客户端证书与私钥不匹配");
        }
#endif
    }

    if (!security.cipherSuites.empty()) {
        std::string cipherList;
        for (const auto& cipher : security.cipherSuites) {
            if (!cipherList.empty()) {
                cipherList.push_back(':');
            }
            cipherList += cipher;
        }
        if (wolfSSL_CTX_set_cipher_list(client->tls.ctx, cipherList.c_str())
            != WOLFSSL_SUCCESS) {
            return fail("配置TLS加密套件失败");
        }
    }

#ifdef HAVE_SNI
    if (adapter->config_.server.host.size() > UINT16_MAX ||
        wolfSSL_CTX_UseSNI(
            client->tls.ctx, WOLFSSL_SNI_HOST_NAME,
            adapter->config_.server.host.data(),
            static_cast<unsigned short>(adapter->config_.server.host.size()))
            != WOLFSSL_SUCCESS) {
        return fail("配置TLS SNI失败");
    }
#endif

    client->tls.ssl = wolfSSL_new(client->tls.ctx);
    if (client->tls.ssl == nullptr) {
        return fail("创建wolfSSL会话失败");
    }
    if (security.verifyCertificate) {
        const auto& host = adapter->config_.server.host;
        const bool isIpAddress = host.find(':') != std::string::npos ||
            (!host.empty() && std::all_of(host.begin(), host.end(), [](const char ch) {
                return (ch >= '0' && ch <= '9') || ch == '.';
            }));
        const int hostCheck = isIpAddress
            ? wolfSSL_check_ip_address(client->tls.ssl, host.c_str())
            : wolfSSL_check_domain_name(client->tls.ssl, host.c_str());
        if (hostCheck != WOLFSSL_SUCCESS) {
            return fail("配置TLS服务端名称校验失败");
        }
    }

    LOG_INFO(std::string("TLS上下文配置成功，服务端证书校验")
             + (security.verifyCertificate ? "已启用" : "已关闭"));
    return WOLFSSL_SUCCESS;
}
#else
int WolfMqttAdapter::tlsCallback([[maybe_unused]] ::MqttClient* client) {
    LOG_WARN("TLS回调被调用，但wolfMQTT未启用TLS支持");
    return MQTT_CODE_ERROR_TLS_CONNECT;
}
#endif // ENABLE_MQTT_TLS

template <typename Operation>
int completeWolfOperation(Operation&& operation, const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const int rc = operation();
        if (rc == MQTT_CODE_SUCCESS) {
            return rc;
        }
        if (rc != MQTT_CODE_CONTINUE && rc != MQTT_CODE_PUB_CONTINUE) {
            return rc;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return MQTT_CODE_ERROR_TIMEOUT;
        }
        std::this_thread::sleep_for(2ms);
    }
}
#endif // WOLFMQTT_ENABLED

WolfMqttAdapter::WolfMqttAdapter(const MqttConfig& config)
    : config_(config)
    , messageThreadRunning_(false)
    , connected_(false)
    , initialized_(false)
{
#ifdef WOLFMQTT_ENABLED
    // 初始化网络上下文
    networkContext_.socketFd = -1;
    networkContext_.isTLS = false;
    
    // 分配缓冲区（默认大小：发送8KB，接收8KB）
    txBuffer_.resize(8 * 1024);
    rxBuffer_.resize(8 * 1024);
    
    // 初始化网络抽象层
    XMEMSET(&net_, 0, sizeof(MqttNet));
    net_.context = this;
    net_.connect = networkConnect;
    net_.read = networkRecv;
    net_.write = networkSend;
    net_.disconnect = networkDisconnect;
#endif
}

WolfMqttAdapter::~WolfMqttAdapter() {
    cleanup();
}

Result<bool> WolfMqttAdapter::initialize() {
    std::lock_guard lock(mutex_);
    
    if (initialized_.load()) {
        return Result<bool>::Success(true);
    }
    
#ifdef WOLFMQTT_ENABLED
    // 创建wolfMQTT客户端
    auto createResult = createClient();
    if (!createResult) {
        return createResult;
    }
    
    initialized_.store(true);
    return Result<bool>::Success(true);
#else
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用，请编译时启用WOLFMQTT_ENABLED"));
#endif
}

Result<bool> WolfMqttAdapter::connect() {
    {
        std::lock_guard lock(mutex_);
        if (!initialized_.load()) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::NOT_INITIALIZED,
                          "适配器未初始化，请先调用initialize()"));
        }
        if (connected_.load()) {
            return Result<bool>::Success(true);
        }
    }

#ifdef WOLFMQTT_ENABLED
    MqttConnect connect;
    XMEMSET(&connect, 0, sizeof(MqttConnect));

    auto configResult = configureConnection(connect);
    if (!configResult) {
        return configResult;
    }

    if (config_.server.useSSL || config_.security.enableTLS) {
        auto tlsResult = configureTLS();
        if (!tlsResult) {
            return tlsResult;
        }
    }

    const bool useTLS = config_.server.useSSL || config_.security.enableTLS;
    LOG_INFO("准备连接服务器: " + config_.server.host + ":" + 
             std::to_string(config_.server.port) + 
             (useTLS ? " (TLS)" : " (TCP)"));

    int rc = MQTT_CODE_ERROR_NETWORK;
#ifdef ENABLE_MQTT_TLS
    int tlsError = 0;
#endif
    {
        std::lock_guard clientLock(clientMutex_);
        if (!wolfClient_) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::NOT_INITIALIZED, "wolfMQTT客户端未初始化"));
        }

        // wolfMQTT在NetConnect阶段就会调用TLS回调，所以必须提前设置ctx。
        wolfClient_->ctx = this;
        rc = MqttClient_NetConnect(
            wolfClient_.get(), config_.server.host.c_str(),
            static_cast<word16>(config_.server.port),
            config_.server.connectTimeout * 1000, useTLS ? 1 : 0,
            useTLS ? WolfMqttAdapter::tlsCallback : nullptr);

        if (rc == MQTT_CODE_SUCCESS) {
            const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(config_.server.connectTimeout);
            do {
                rc = MqttClient_Connect(wolfClient_.get(), &connect);
                if (rc == MQTT_CODE_SUCCESS) {
                    break;
                }
                if (rc != MQTT_CODE_CONTINUE && rc != MQTT_CODE_ERROR_TIMEOUT) {
                    break;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    rc = MQTT_CODE_ERROR_TIMEOUT;
                    break;
                }
                std::this_thread::sleep_for(5ms);
            } while (true);
        }

        if (rc != MQTT_CODE_SUCCESS) {
#ifdef ENABLE_MQTT_TLS
            if (useTLS) {
                tlsError = wolfClient_->tls.lastError;
            }
#endif
            MqttClient_NetDisconnect(wolfClient_.get());
        }
    }

    if (connect.props) {
        MqttClient_PropsFree(connect.props);
        connect.props = nullptr;
    }
    if (connect.lwt_msg && connect.lwt_msg->props) {
        MqttClient_PropsFree(connect.lwt_msg->props);
        connect.lwt_msg->props = nullptr;
    }

    if (rc != MQTT_CODE_SUCCESS) {
        std::string error = MqttClient_ReturnCodeToString(rc);
#ifdef ENABLE_MQTT_TLS
        if (useTLS && tlsError != 0) {
            const char* tlsReason = wolfSSL_ERR_reason_error_string(tlsError);
            error += ", wolfSSL=" + std::to_string(tlsError);
            if (tlsReason != nullptr) {
                error += " (" + std::string(tlsReason) + ")";
            }
        }
#endif
        LOG_ERROR("MQTT连接失败: " + std::to_string(rc) + " (" + error + ")");
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::CONNECTION_REFUSED,
                      "MQTT连接失败: " + std::to_string(rc) + " (" + error + ")"));
    }

    connected_.store(true);
    if (messageThread_.joinable()) {
        if (messageThread_.get_id() == std::this_thread::get_id()) {
            connected_.store(false);
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::THREAD_ERROR,
                          "不能在消息接收线程内直接重建连接"));
        }
        messageThread_.join();
    }
    messageThreadRunning_.store(true);
    messageThread_ = std::thread(&WolfMqttAdapter::messageReceiveThread, this);

    std::function<void(bool)> callback;
    {
        std::lock_guard lock(mutex_);
        callback = connectionCallback_;
    }
    if (callback) {
        callback(true);
    }

    return Result<bool>::Success(true);
#else
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

Result<bool> WolfMqttAdapter::disconnect() {
    const bool wasConnected = connected_.exchange(false);
    messageThreadRunning_.store(false);

    if (messageThread_.joinable()
        && messageThread_.get_id() != std::this_thread::get_id()) {
        messageThread_.join();
    }

    bool protocolDisconnectSucceeded = true;
#ifdef WOLFMQTT_ENABLED
    {
        std::lock_guard clientLock(clientMutex_);
        if (wolfClient_) {
            if (wasConnected && networkContext_.socketFd >= 0) {
                const int rc = MqttClient_Disconnect(wolfClient_.get());
                protocolDisconnectSucceeded = rc == MQTT_CODE_SUCCESS;
                if (!protocolDisconnectSucceeded) {
                    LOG_WARN("发送MQTT DISCONNECT失败: " + std::to_string(rc));
                }
            }
            MqttClient_NetDisconnect(wolfClient_.get());
        }
    }
#endif

    if (wasConnected) {
        std::function<void(bool)> callback;
        {
            std::lock_guard lock(mutex_);
            callback = connectionCallback_;
        }
        if (callback) {
            callback(false);
        }
    }

    if (!protocolDisconnectSucceeded) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NETWORK_ERROR,
                      "MQTT断开包发送失败，网络连接已关闭"));
    }
    return Result<bool>::Success(true);
}

bool WolfMqttAdapter::isConnected() const {
    return connected_.load();
}

Result<bool> WolfMqttAdapter::publish(std::string_view topic,
                                     const std::string_view payload,
                                     QoS qos,
                                     const bool retained) {
    if (!connected_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法发布消息"));
    }

#ifdef WOLFMQTT_ENABLED
    const PendingOperationGuard operationGuard(pendingOperations_);
    if (topic.size() > UINT16_MAX) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INVALID_ARGUMENT, "主题长度超过MQTT协议限制"));
    }

    MqttPublish publish;
    XMEMSET(&publish, 0, sizeof(MqttPublish));

    std::string topicStr(topic);
    std::string payloadStr(payload);
    publish.topic_name = const_cast<char*>(topicStr.c_str());
    publish.topic_name_len = static_cast<word16>(topicStr.length());
    publish.buffer = reinterpret_cast<byte*>(const_cast<char*>(payloadStr.c_str()));
    publish.buffer_len = static_cast<word32>(payloadStr.length());
    publish.total_len = static_cast<word32>(payloadStr.length());
    publish.qos = static_cast<MqttQoS>(qos);
    publish.retain = retained ? 1 : 0;
    publish.duplicate = 0;
    publish.packet_id = qos > QoS::QOS_0
        ? static_cast<word16>(nextPacketId()) : 0;

    int rc = MQTT_CODE_ERROR_NETWORK;
    {
        std::lock_guard clientLock(clientMutex_);
        if (!connected_.load() || !wolfClient_) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::NOT_CONNECTED,
                         "未连接，无法发布消息"));
        }
        rc = completeWolfOperation(
            [this, &publish] {
                return MqttClient_Publish(wolfClient_.get(), &publish);
            },
            std::chrono::seconds(config_.server.connectTimeout));
    }

    dispatchPendingMessages();
    if (rc != MQTT_CODE_SUCCESS) {
        if ((rc == MQTT_CODE_ERROR_NETWORK || rc == MQTT_CODE_ERROR_TIMEOUT)
            && connected_.exchange(false)) {
            messageThreadRunning_.store(false);
            std::function<void(bool)> callback;
            {
                std::lock_guard lock(mutex_);
                callback = connectionCallback_;
            }
            if (callback) {
                callback(false);
            }
        }
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::PUBLISH_FAILED,
                      "发布失败: " + std::to_string(rc) + " ("
                          + MqttClient_ReturnCodeToString(rc) + ")"));
    }

    return Result<bool>::Success(true);
#else
    (void)topic;
    (void)payload;
    (void)qos;
    (void)retained;
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

Result<bool> WolfMqttAdapter::subscribe(std::string_view topic, QoS qos) {
    const PendingOperationGuard operationGuard(pendingOperations_);
    std::unique_lock clientLock(clientMutex_);
    
    if (!connected_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法订阅"));
    }
    
#ifdef WOLFMQTT_ENABLED
    MqttSubscribe subscribe;
    XMEMSET(&subscribe, 0, sizeof(MqttSubscribe));
    
    subscribe.packet_id = static_cast<word16>(nextPacketId());
    
    subscribe.topic_count = 1;
    
    MqttTopic topics[1]{};
    const std::string topicStr(topic);
    topics[0].topic_filter = const_cast<char*>(topicStr.c_str());
    topics[0].qos = static_cast<MqttQoS>(qos);
    
    subscribe.topics = topics;
    
    LOG_INFO("准备订阅主题: " + topicStr + ", QoS=" + std::to_string(static_cast<int>(qos)) + 
            ", PacketID=" + std::to_string(subscribe.packet_id));
    
    // MqttClient_Subscribe 会等待 SUBACK
    // 注意：这可能会与 messageReceiveThread 冲突
    // 如果返回 MQTT_CODE_CONTINUE (-101)，说明 SUBACK 可能被 messageReceiveThread 接收了
    // 我们等待一段时间，让 messageReceiveThread 接收 SUBACK，然后检查订阅结果
    int rc = completeWolfOperation(
        [this, &subscribe] {
            return MqttClient_Subscribe(wolfClient_.get(), &subscribe);
        },
        std::chrono::seconds(config_.server.connectTimeout));
    
    LOG_INFO("MqttClient_Subscribe返回: " + std::to_string(rc) + 
            " (Topic: " + topicStr + ", PacketID: " + std::to_string(subscribe.packet_id) + ")");

    clientLock.unlock();
    dispatchPendingMessages();

    if (rc != MQTT_CODE_SUCCESS) {
        LOG_ERROR("订阅失败: " + std::to_string(rc) + " (Topic: " + topicStr + ")");
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::SUBSCRIBE_FAILED,
                     "订阅失败: " + std::to_string(rc)));
    }
    
    // 检查订阅结果（SUBACK 中的返回码）
    if (subscribe.topics[0].return_code == 0x80) {
        // 0x80 表示订阅失败（服务器拒绝）
        LOG_ERROR("服务器拒绝订阅: " + topicStr);
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::SUBSCRIBE_FAILED,
                     "服务器拒绝订阅: " + topicStr));
    }
    
    LOG_INFO("订阅成功: " + topicStr + ", QoS=" + 
            std::to_string(static_cast<int>(subscribe.topics[0].return_code)));
    
    return Result<bool>::Success(true);
#else
    (void)topic;
    (void)qos;
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

Result<bool> WolfMqttAdapter::unsubscribe([[maybe_unused]] std::string_view topic) {
    const PendingOperationGuard operationGuard(pendingOperations_);
    std::unique_lock clientLock(clientMutex_);
    
    if (!connected_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法取消订阅"));
    }
    
#ifdef WOLFMQTT_ENABLED
    MqttUnsubscribe unsubscribe;
    XMEMSET(&unsubscribe, 0, sizeof(MqttUnsubscribe));
    
    unsubscribe.packet_id = static_cast<word16>(nextPacketId());
    
    unsubscribe.topic_count = 1;
    
    MqttTopic topics[1]{};
    const std::string topicStr(topic);
    topics[0].topic_filter = const_cast<char*>(topicStr.c_str());
    
    unsubscribe.topics = topics;
    
    int rc = completeWolfOperation(
        [this, &unsubscribe] {
            return MqttClient_Unsubscribe(wolfClient_.get(), &unsubscribe);
        },
        std::chrono::seconds(config_.server.connectTimeout));
    clientLock.unlock();
    dispatchPendingMessages();
    if (rc != MQTT_CODE_SUCCESS) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::UNSUBSCRIBE_FAILED,
                     "取消订阅失败: " + std::to_string(rc)));
    }
    
    return Result<bool>::Success(true);
#else
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

void WolfMqttAdapter::setMessageCallback(
    const std::function<void(std::string_view, std::string_view, QoS)>& callback) {
    std::lock_guard lock(mutex_);
    messageCallback_ = callback;
}

void WolfMqttAdapter::setConnectionCallback(
    const std::function<void(bool)>& callback) {
    std::lock_guard lock(mutex_);
    connectionCallback_ = callback;
}

Result<bool> WolfMqttAdapter::processNetwork() const {
    std::lock_guard lock(mutex_);
    
    if (!connected_.load()) {
        return Result<bool>::Success(true);
    }
    
#ifdef WOLFMQTT_ENABLED
    // 处理网络I/O（非阻塞模式）
    // 注意：这里需要根据wolfMQTT的实际API调整
    // 如果使用非阻塞模式，需要调用相应的处理函数
    // 目前先返回成功，后续完善
    return Result<bool>::Success(true);
#else
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

void WolfMqttAdapter::cleanup() {
    const auto disconnectResult = disconnect();
    if (!disconnectResult) {
        LOG_WARN("清理时发送MQTT断开包失败: " + disconnectResult.error.message);
    }

#ifdef WOLFMQTT_ENABLED
    {
        std::lock_guard clientLock(clientMutex_);
        if (wolfClient_) {
            MqttClient_DeInit(wolfClient_.get());
            wolfClient_.reset();
        }
    }
#endif

    {
        std::lock_guard pendingLock(pendingMessagesMutex_);
        std::queue<PendingMessage> empty;
        pendingMessages_.swap(empty);
    }
    initialized_.store(false);
}

void WolfMqttAdapter::dispatchPendingMessages() {
    while (true) {
        PendingMessage message;
        {
            std::lock_guard pendingLock(pendingMessagesMutex_);
            if (pendingMessages_.empty()) {
                return;
            }
            message = std::move(pendingMessages_.front());
            pendingMessages_.pop();
        }

        std::function<void(std::string_view, std::string_view, QoS)> callback;
        {
            std::lock_guard lock(mutex_);
            callback = messageCallback_;
        }
        if (!callback) {
            continue;
        }

        try {
            callback(message.topic, message.payload, message.qos);
        } catch (const std::exception& e) {
            LOG_ERROR("消息回调抛出异常: " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("消息回调抛出未知异常");
        }
    }
}

std::uint16_t WolfMqttAdapter::nextPacketId() noexcept {
    auto current = packetIdCounter_.load(std::memory_order_relaxed);
    while (true) {
        const std::uint16_t packetId = current == 0 ? 1 : current;
        const std::uint16_t next = packetId == UINT16_MAX
            ? 1 : static_cast<std::uint16_t>(packetId + 1);
        if (packetIdCounter_.compare_exchange_weak(
                current, next, std::memory_order_relaxed)) {
            return packetId;
        }
    }
}

#ifdef WOLFMQTT_ENABLED
Result<bool> WolfMqttAdapter::createClient() {
    // 创建wolfMQTT客户端
    wolfClient_ = std::make_unique<MqttClient>();
    XMEMSET(wolfClient_.get(), 0, sizeof(MqttClient));
    
    // 初始化客户端
    // 使用配置中的连接超时时间作为命令超时时间
    // 对于TLS连接，需要更长的超时时间
    int cmdTimeoutMs = config_.server.connectTimeout * 1000;
    if (config_.server.useSSL || config_.security.enableTLS) {
        // TLS连接需要更长的超时时间（至少30秒）
        cmdTimeoutMs = std::max(cmdTimeoutMs, 30000);
    }
    
    LOG_INFO("初始化wolfMQTT客户端: cmd_timeout_ms=" + std::to_string(cmdTimeoutMs));

    const int rc = MqttClient_Init(
        wolfClient_.get(),
        &net_,
        messageCallback,
        txBuffer_.data(),
        static_cast<int>(txBuffer_.size()),
        rxBuffer_.data(),
        static_cast<int>(rxBuffer_.size()),
        cmdTimeoutMs
    );
    
    if (rc != MQTT_CODE_SUCCESS) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                     "初始化wolfMQTT客户端失败: " + std::to_string(rc)));
    }

    wolfClient_->ctx = this;
    
    return Result<bool>::Success(true);
}

Result<bool> WolfMqttAdapter::configureConnection(MqttConnect& connect) {
    // 设置客户端ID
    std::string clientId;
    if (config_.basic.clientId.empty()) {
        // 自动生成客户端ID（简化版，实际应该使用UUID等）
        clientId = config_.basic.clientIdPrefix + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
    } else {
        clientId = config_.basic.clientId;
    }
    
    // 注意：这里需要确保clientId在connect生命周期内有效
    // 实际实现中应该使用成员变量存储
    // 使用 thread_local 变量临时存储（后续改进为成员变量）
    // 在 .cpp 文件中，thread_local 不需要 static（thread_local 本身就有内部链接）
    thread_local std::string storedClientId;
    storedClientId = clientId;
    connect.client_id = const_cast<char*>(storedClientId.c_str());
    
    // 设置清理会话
    connect.clean_session = config_.basic.cleanStart ? 1 : 0;
    
    // 设置保活时间
    connect.keep_alive_sec = config_.server.keepAlive;
    
    // 设置用户名和密码
    if (!config_.auth.username.empty()) {
        thread_local std::string storedUsername = config_.auth.username;
        thread_local std::string storedPassword = config_.auth.password;
        storedUsername = config_.auth.username;
        storedPassword = config_.auth.password;
        connect.username = const_cast<char*>(storedUsername.c_str());
        connect.password = const_cast<char*>(storedPassword.c_str());
    }
    
    // 根据协议版本配置
    if (config_.basic.version == "5.0") {
        connect.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;
        
        // ========== MQTT 5.0 特定配置 ==========
        // 注意：以下配置仅在 basic.version="5.0" 时生效
        // 对于 MQTT 3.1.1，这些配置将被忽略
        
        // 会话过期间隔（秒）
        // 如果 cleanStart=false，设置会话过期时间
        // 如果 cleanStart=true，会话将在断开连接时立即过期
        if (!config_.basic.cleanStart && config_.mqtt5.sessionExpiryInterval > 0) {
            if (MqttProp* prop = MqttClient_PropsAdd(&connect.props)) {
                prop->type = MQTT_PROP_SESSION_EXPIRY_INTERVAL;
                prop->data_int = static_cast<word32>(config_.mqtt5.sessionExpiryInterval);
                LOG_DEBUG("设置会话过期间隔: " + std::to_string(config_.mqtt5.sessionExpiryInterval) + " 秒");
            }
        }
        
        // 接收最大数量（QoS > 0 的未确认消息数量）
        if (config_.mqtt5.receiveMaximum > 0) {
            if (MqttProp* prop = MqttClient_PropsAdd(&connect.props)) {
                prop->type = MQTT_PROP_RECEIVE_MAX;
                prop->data_short = static_cast<word16>(config_.mqtt5.receiveMaximum);
                LOG_DEBUG("设置接收最大数量: " + std::to_string(config_.mqtt5.receiveMaximum));
            }
        }
        
        // 最大包大小（字节）
        if (config_.mqtt5.maximumPacketSize > 0) {
            if (MqttProp* prop = MqttClient_PropsAdd(&connect.props)) {
                prop->type = MQTT_PROP_MAX_PACKET_SZ;
                prop->data_int = static_cast<word32>(config_.mqtt5.maximumPacketSize);
                LOG_DEBUG("设置最大包大小: " + std::to_string(config_.mqtt5.maximumPacketSize) + " 字节");
            }
        }
        
        // 请求响应信息
        // 如果为 true，服务器将在 CONNACK 中返回响应信息
        {
            if (MqttProp* prop = MqttClient_PropsAdd(&connect.props)) {
                prop->type = MQTT_PROP_REQ_RESP_INFO;
                prop->data_byte = config_.mqtt5.requestResponseInformation ? 1 : 0;
                LOG_DEBUG("请求响应信息: " + std::string(config_.mqtt5.requestResponseInformation ? "是" : "否"));
            }
        }
        
        // 请求问题信息
        // 如果为 true，服务器将在错误响应中包含原因字符串和用户属性
        {
            if (MqttProp* prop = MqttClient_PropsAdd(&connect.props)) {
                prop->type = MQTT_PROP_REQ_PROB_INFO;
                prop->data_byte = config_.mqtt5.requestProblemInformation ? 1 : 0;
                LOG_DEBUG("请求问题信息: " + std::string(config_.mqtt5.requestProblemInformation ? "是" : "否"));
            }
        }
        
        // 用户属性
        // MQTT 5.0 允许在 CONNECT 包中添加用户属性
        for (const auto& userProperty : config_.mqtt5.userProperties) {
            const auto& key = userProperty.first;
            const auto& value = userProperty.second;
            if (MqttProp* prop = MqttClient_PropsAdd(&connect.props)) {
                prop->type = MQTT_PROP_USER_PROP;
                // 用户属性需要两个字符串：key 和 value
                // 注意：这里需要确保字符串在连接期间保持有效
                thread_local std::map<std::string, std::pair<std::string, std::string>> storedUserProps;
                storedUserProps[key] = {key, value};
                prop->data_str.str = const_cast<char*>(storedUserProps[key].first.c_str());
                prop->data_str.len = static_cast<word16>(storedUserProps[key].first.length());
                prop->data_str2.str = const_cast<char*>(storedUserProps[key].second.c_str());
                prop->data_str2.len = static_cast<word16>(storedUserProps[key].second.length());

                LOG_DEBUG(fmt::format("添加用户属性: {} = {}", key, value));
            }
        }
        
        // 遗嘱消息配置（MQTT 5.0）
        // 注意：wolfMQTT 的 lwt_msg 是指向 ::MqttMessage 的指针
        // 需要创建一个 ::MqttMessage 对象并设置指针
        if (config_.mqtt5.willMessage.enabled) {
            // 使用静态变量存储遗嘱消息（确保在连接期间有效）
            thread_local ::MqttMessage lwtMessage;
            XMEMSET(&lwtMessage, 0, sizeof(::MqttMessage));
            
            // 设置遗嘱消息基本属性
            thread_local std::string storedWillTopic = config_.mqtt5.willMessage.topic;
            thread_local std::string storedWillPayload = config_.mqtt5.willMessage.payload;
            storedWillTopic = config_.mqtt5.willMessage.topic;
            storedWillPayload = config_.mqtt5.willMessage.payload;
            
            lwtMessage.topic_name = const_cast<char*>(storedWillTopic.c_str());
            lwtMessage.topic_name_len = static_cast<word16>(storedWillTopic.length());
            lwtMessage.buffer = reinterpret_cast<byte*>(const_cast<char*>(storedWillPayload.c_str()));
            lwtMessage.buffer_len = static_cast<word32>(storedWillPayload.length());
            lwtMessage.total_len = static_cast<word32>(storedWillPayload.length());
            lwtMessage.qos = static_cast<MqttQoS>(config_.mqtt5.willMessage.qos);
            lwtMessage.retain = config_.mqtt5.willMessage.retained ? 1 : 0;
            lwtMessage.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;  // MQTT 5.0
            
            // 设置遗嘱延迟间隔（秒）
            if (config_.mqtt5.willMessage.delayInterval > 0) {
                if (MqttProp* prop = MqttClient_PropsAdd(&lwtMessage.props)) {
                    prop->type = MQTT_PROP_WILL_DELAY_INTERVAL;
                    prop->data_int = static_cast<word32>(config_.mqtt5.willMessage.delayInterval);
                    LOG_DEBUG("设置遗嘱延迟间隔: " + std::to_string(config_.mqtt5.willMessage.delayInterval) + " 秒");
                }
            }
            
            // 设置连接结构体的遗嘱消息指针
            connect.enable_lwt = 1;
            connect.lwt_msg = &lwtMessage;
            
            LOG_DEBUG("配置遗嘱消息: topic=" + config_.mqtt5.willMessage.topic + 
                     ", qos=" + std::to_string(static_cast<int>(config_.mqtt5.willMessage.qos)) +
                     ", retained=" + std::string(config_.mqtt5.willMessage.retained ? "true" : "false"));
        }
    } else {
        // ========== MQTT 3.1.1 配置 ==========
        // 注意：MQTT 3.1.1 不支持 MQTT 5.0 的属性
        // config_.mqtt5 中的配置在 MQTT 3.1.1 下将被忽略
        connect.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_4;
        
        // MQTT 3.1.1 的遗嘱消息配置（如果提供了基本遗嘱消息配置）
        // 注意：这里假设如果使用 MQTT 3.1.1，遗嘱消息应该通过其他方式配置
        // 或者可以通过 basic.willMessage 配置（如果存在）
    }
    
    return Result<bool>::Success(true);
}

Result<bool> WolfMqttAdapter::publish(std::string_view topic,
                                     std::string_view payload,
                                     const MqttProperties& properties,
                                     QoS qos,
                                     bool retained) {
    // ========== MQTT 5.0 属性支持 ==========
    // 注意：此方法仅在 MQTT 5.0 协议下有效
    // 对于 MQTT 3.1.1，属性将被忽略
    
    // 检查协议版本
    if (config_.basic.version != "5.0") {
        // MQTT 3.1.1 不支持属性，回退到普通发布
        LOG_DEBUG("MQTT 3.1.1 不支持属性，使用普通发布方法");
        return publish(topic, payload, qos, retained);
    }
    
    // 先检查连接状态
    if (!connected_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法发布消息"));
    }
    
#ifdef WOLFMQTT_ENABLED
    // 准备发布结构体
    MqttPublish publish;
    XMEMSET(&publish, 0, sizeof(MqttPublish));
    
    // 设置基本属性
    const std::string topicStr(topic);
    const std::string payloadStr(payload);
    publish.topic_name = const_cast<char*>(topicStr.c_str());
    publish.topic_name_len = static_cast<word16>(topicStr.length());
    publish.buffer = reinterpret_cast<byte*>(const_cast<char*>(payloadStr.c_str()));
    publish.buffer_len = static_cast<word32>(payloadStr.length());
    publish.total_len = static_cast<word32>(payloadStr.length());
    publish.qos = static_cast<MqttQoS>(qos);
    publish.retain = retained ? 1 : 0;
    publish.duplicate = 0;
    publish.protocol_level = MQTT_CONNECT_PROTOCOL_LEVEL_5;  // MQTT 5.0
    
    // 如果QoS > 0，需要设置packet_id
    if (qos > QoS::QOS_0) {
        publish.packet_id = static_cast<word16>(nextPacketId());
    } else {
        publish.packet_id = 0;
    }
    
    // ========== 设置 MQTT 5.0 属性 ==========
    // 消息过期时间（秒）
    if (properties.messageExpiryInterval.has_value()) {
        if (MqttProp* prop = MqttClient_PropsAdd(&publish.props)) {
            prop->type = MQTT_PROP_MSG_EXPIRY_INTERVAL;
            prop->data_int = static_cast<word32>(*properties.messageExpiryInterval);
            LOG_DEBUG("设置消息过期时间: " + std::to_string(*properties.messageExpiryInterval) + " 秒");
        }
    }
    
    // 内容类型
    if (properties.contentType.has_value() && !properties.contentType->empty()) {
        if (MqttProp* prop = MqttClient_PropsAdd(&publish.props)) {
            prop->type = MQTT_PROP_CONTENT_TYPE;
            // 需要确保字符串在发布期间有效
            thread_local std::string storedContentType = *properties.contentType;
            storedContentType = *properties.contentType;
            prop->data_str.str = const_cast<char*>(storedContentType.c_str());
            prop->data_str.len = static_cast<word16>(storedContentType.length());
            LOG_DEBUG("设置内容类型: " + storedContentType);
        }
    }
    
    // 响应主题
    if (properties.responseTopic.has_value() && !properties.responseTopic->empty()) {
        if (MqttProp* prop = MqttClient_PropsAdd(&publish.props)) {
            prop->type = MQTT_PROP_RESP_TOPIC;
            thread_local std::string storedResponseTopic = *properties.responseTopic;
            storedResponseTopic = *properties.responseTopic;
            prop->data_str.str = const_cast<char*>(storedResponseTopic.c_str());
            prop->data_str.len = static_cast<word16>(storedResponseTopic.length());
            LOG_DEBUG("设置响应主题: " + storedResponseTopic);
        }
    }
    
    // 关联数据
    if (properties.correlationData.has_value() && !properties.correlationData->empty()) {
        if (MqttProp* prop = MqttClient_PropsAdd(&publish.props)) {
            prop->type = MQTT_PROP_CORRELATION_DATA;
            thread_local std::string storedCorrelationData = *properties.correlationData;
            storedCorrelationData = *properties.correlationData;
            prop->data_bin.data = reinterpret_cast<byte*>(const_cast<char*>(storedCorrelationData.c_str()));
            prop->data_bin.len = static_cast<word16>(storedCorrelationData.length());
            LOG_DEBUG("设置关联数据: 长度=" + std::to_string(storedCorrelationData.length()));
        }
    }
    
    // 用户属性
    for (const auto& userProperty : properties.userProperties) {
        const auto& key = userProperty.first;
        const auto& value = userProperty.second;
        if (MqttProp* prop = MqttClient_PropsAdd(&publish.props)) {
            prop->type = MQTT_PROP_USER_PROP;
            // 用户属性需要两个字符串：key 和 value
            thread_local std::map<std::string, std::pair<std::string, std::string>> storedUserProps;
            storedUserProps[key] = {key, value};
            prop->data_str.str = const_cast<char*>(storedUserProps[key].first.c_str());
            prop->data_str.len = static_cast<word16>(storedUserProps[key].first.length());
            prop->data_str2.str = const_cast<char*>(storedUserProps[key].second.c_str());
            prop->data_str2.len = static_cast<word16>(storedUserProps[key].second.length());
            LOG_DEBUG(fmt::format("添加用户属性: {} = {}", key, value));
        }
    }
    
    LOG_INFO("准备发布消息（带属性）: Topic=" + topicStr + 
            ", Payload长度=" + std::to_string(payloadStr.length()) + 
            ", QoS=" + std::to_string(static_cast<int>(qos)) + 
            ", PacketID=" + std::to_string(publish.packet_id) +
            ", 属性数量=" + std::to_string(properties.userProperties.size()));
    
    // 获取锁并发布
    {
        const PendingOperationGuard operationGuard(pendingOperations_);
        std::unique_lock clientLock(clientMutex_);
        
        // 再次检查连接状态
        if (!connected_.load() || !wolfClient_) {
            // 清理属性
            if (publish.props) {
                MqttClient_PropsFree(publish.props);
                publish.props = nullptr;
            }
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::NOT_CONNECTED,
                         "未连接，无法发布消息"));
        }
        
        const int rc = completeWolfOperation(
            [this, &publish] {
                return MqttClient_Publish(wolfClient_.get(), &publish);
            },
            std::chrono::seconds(config_.server.connectTimeout));

        // 清理属性
        if (publish.props) {
            MqttClient_PropsFree(publish.props);
            publish.props = nullptr;
        }

        clientLock.unlock();
        dispatchPendingMessages();

        if (rc != MQTT_CODE_SUCCESS) {
            if ((rc == MQTT_CODE_ERROR_NETWORK || rc == MQTT_CODE_ERROR_TIMEOUT)
                && connected_.exchange(false)) {
                messageThreadRunning_.store(false);
                std::function<void(bool)> callback;
                {
                    std::lock_guard lock(mutex_);
                    callback = connectionCallback_;
                }
                if (callback) {
                    callback(false);
                }
            }
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::PUBLISH_FAILED,
                          "发布失败: " + std::to_string(rc) + " ("
                              + MqttClient_ReturnCodeToString(rc) + ")"));
        }
    }
    
    return Result<bool>::Success(true);
#else
    // 未启用 wolfMQTT，回退到普通发布
    return publish(topic, payload, qos, retained);
#endif
}

Result<bool> WolfMqttAdapter::configureTLS() {
#ifdef ENABLE_MQTT_TLS
    networkContext_.isTLS = true;
    return Result<bool>::Success(true);
#else
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                  "配置要求TLS，但当前wolfMQTT构建未启用wolfSSL支持"));
#endif
}

int WolfMqttAdapter::networkConnect(void* context, const char* host, word16 port, const int timeout_ms) {
    auto* adapter = static_cast<WolfMqttAdapter*>(context);
    NetworkContext* netCtx = &adapter->networkContext_;
    
    LOG_INFO("networkConnect: 连接到 " + std::string(host) + ":" + std::to_string(port));
    
    // 如果已经连接，先断开
    if (netCtx->socketFd >= 0) {
        ::close(netCtx->socketFd);
        netCtx->socketFd = -1;
    }
    
    // 创建socket
    netCtx->socketFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (netCtx->socketFd < 0) {
        LOG_ERROR("networkConnect: 创建socket失败: " + std::string(strerror(errno)));
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    // 设置socket选项
    const int opt = 1;
    ::setsockopt(netCtx->socketFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 设置超时
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(netCtx->socketFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(netCtx->socketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // 解析主机地址
    sockaddr_in addr{};
    ::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    // 尝试将host解析为IP地址
    if (::inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        // 如果不是IP地址，使用getaddrinfo进行DNS解析
        addrinfo hints{}, *result = nullptr;
        ::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (const int rc = ::getaddrinfo(host, nullptr, &hints, &result); rc != 0 || result == nullptr) {
            LOG_ERROR("networkConnect: DNS解析失败: " + std::string(gai_strerror(rc)));
            ::close(netCtx->socketFd);
            netCtx->socketFd = -1;
            return MQTT_CODE_ERROR_NETWORK;
        }
        
        // 使用第一个结果
        const auto* addr_in = reinterpret_cast<struct sockaddr_in *>(result->ai_addr);
        addr.sin_addr = addr_in->sin_addr;
        
        ::freeaddrinfo(result);
    }
    
    // 连接
    if (const int rc = ::connect(netCtx->socketFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)); rc < 0) {
        LOG_ERROR("networkConnect: TCP连接失败: " + std::string(strerror(errno)) + " (errno=" + std::to_string(errno) + ")");
        ::close(netCtx->socketFd);
        netCtx->socketFd = -1;
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    LOG_INFO("networkConnect: TCP连接成功");
    return MQTT_CODE_SUCCESS;
}

int WolfMqttAdapter::networkDisconnect(void* context) {
    auto* adapter = static_cast<WolfMqttAdapter*>(context);

    if (NetworkContext* netCtx = &adapter->networkContext_; netCtx->socketFd >= 0) {
        ::close(netCtx->socketFd);
        netCtx->socketFd = -1;
    }
    
    return MQTT_CODE_SUCCESS;
}

int WolfMqttAdapter::networkSend(void* context, const byte* buf, int buf_len, [[maybe_unused]] int timeout_ms) {
    const auto* adapter = static_cast<WolfMqttAdapter*>(context);
    const NetworkContext* netCtx = &adapter->networkContext_;
    
    // 检查socket是否有效（可能在disconnect时被关闭）
    if (netCtx->socketFd < 0) {
        // socket已关闭，返回网络错误（不记录错误日志，这是正常情况）
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    // 发送数据
    const ssize_t sent = ::send(netCtx->socketFd, buf, buf_len, 0);
    if (sent < 0) {
        const int err = errno;
        if (err == EAGAIN
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
            || err == EWOULDBLOCK
#endif
        ) {
            LOG_DEBUG("网络发送: EAGAIN/EWOULDBLOCK，返回CONTINUE");
            return MQTT_CODE_CONTINUE;
        }
        LOG_ERROR("网络发送失败: errno=" + std::to_string(err) + 
                 ", 发送长度=" + std::to_string(buf_len));
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    if (sent != static_cast<ssize_t>(buf_len)) {
        LOG_WARN("网络发送部分数据: 期望=" + std::to_string(buf_len) + 
                ", 实际=" + std::to_string(sent));
    } else {
        LOG_DEBUG("网络发送成功: " + std::to_string(sent) + " 字节");
    }
    
    return static_cast<int>(sent);
}

int WolfMqttAdapter::networkRecv(void* context, byte* buf, const int buf_len, const int timeout_ms) {
    const auto* adapter = static_cast<WolfMqttAdapter*>(context);
    const NetworkContext* netCtx = &adapter->networkContext_;
    
    // 检查socket是否有效（可能在disconnect时被关闭）
    if (netCtx->socketFd < 0) {
        // socket已关闭，返回网络错误，让调用者知道连接已断开
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    // 注意：如果启用TLS，这里接收的是加密的TLS数据
    // wolfSSL会在MqttSocket_ReadDo中解密这些数据

    // 使用select等待数据就绪（类似wolfMQTT示例代码）
    fd_set readfds, errfds;
    timeval tv{};
    
    // 设置超时
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    // 确保最小超时值（至少100ms）
    if (tv.tv_sec < 0 || (tv.tv_sec == 0 && tv.tv_usec <= 0)) {
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms
    }
    // 不限制最大超时时间，让wolfMQTT自己控制超时
    // 之前的5秒限制可能导致TLS连接时超时
    
    FD_ZERO(&readfds);
    FD_ZERO(&errfds);
    FD_SET(netCtx->socketFd, &readfds);
    FD_SET(netCtx->socketFd, &errfds);
    
    // 等待数据就绪
    const int selectResult = ::select(netCtx->socketFd + 1, &readfds, nullptr, &errfds, &tv);
    if (selectResult < 0) {
        const int err = errno;
        // 如果socket在select期间被关闭，errno可能是EBADF(9)
        // 这是正常情况（disconnect时关闭socket），不需要记录错误日志
        if (err == EBADF) {
            // socket已关闭，返回网络错误
            return MQTT_CODE_ERROR_NETWORK;
        }
        // 其他错误才记录日志
        LOG_ERROR("select失败: errno=" + std::to_string(err));
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    if (selectResult == 0) {
        // 超时
        // 对于TLS连接，超时是正常的，wolfSSL可能需要多次读取
        // 返回CONTINUE而不是TIMEOUT，让wolfSSL继续尝试
        // 对于TCP连接，在连接建立阶段（等待CONNACK），也返回CONTINUE，允许重试
        // 注意：超时是正常情况（特别是在消息接收线程中轮询时），不需要频繁打印日志
        // 使用 thread_local 变量跟踪超时次数，只在连接建立阶段的前几次超时时打印日志

        // 如果这是连接建立后的第一次超时，标记连接已建立
        // 之后就不再打印超时日志（因为这是正常的轮询行为）
        if (thread_local bool connection_established = false; !connection_established) {
            thread_local int timeout_count = 0;
            timeout_count++;
            // 只在连接建立阶段的前几次超时时打印日志（最多3次）
            if (timeout_count <= 3) {
                if (netCtx->isTLS) {
                    LOG_DEBUG("网络接收超时 (TLS模式，返回CONTINUE)");
                } else {
                    LOG_DEBUG("网络接收超时 (TCP模式，返回CONTINUE以允许重试)");
                }
            } else {
                // 超过3次后，认为连接已建立，不再打印超时日志
                connection_established = true;
            }
        }
        // 连接建立后，超时是正常的轮询行为，不打印日志
        
        return MQTT_CODE_CONTINUE;
    }
    
    // 检查错误
    if (FD_ISSET(netCtx->socketFd, &errfds)) {
        LOG_ERROR("socket错误");
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    // 检查是否有数据可读
    if (!FD_ISSET(netCtx->socketFd, &readfds)) {
        LOG_DEBUG("socket未就绪");
        return MQTT_CODE_CONTINUE;
    }
    
    // 接收数据
    const ssize_t received = ::recv(netCtx->socketFd, buf, buf_len, 0);
    if (received < 0) {
        const int err = errno;
        if (err == EAGAIN
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
            || err == EWOULDBLOCK
#endif
        ) {
            LOG_DEBUG("网络接收: EAGAIN/EWOULDBLOCK，返回CONTINUE");
            return MQTT_CODE_CONTINUE;
        }
        LOG_ERROR("网络接收失败: errno=" + std::to_string(err) + 
                 ", 期望长度=" + std::to_string(buf_len));
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    if (received == 0) {
        // 连接关闭
        LOG_WARN("连接已关闭");
        return MQTT_CODE_ERROR_NETWORK;
    }

    // 如果启用TLS，这里接收的是加密的TLS数据，不应该尝试解析MQTT包类型
    // 只有在非TLS模式下才解析MQTT包类型
    if (!netCtx->isTLS) {
        LOG_INFO("网络接收成功: " + std::to_string(received) + " 字节");
        // 打印MQTT包类型用于调试（仅非TLS模式）
        const byte packetType = buf[0] >> 4 & 0x0F;
        std::string packetTypeName;
        switch (packetType) {
            case 1: packetTypeName = "CONNECT"; break;
            case 2: packetTypeName = "CONNACK"; break;
            case 3: packetTypeName = "PUBLISH"; break;
            case 4: packetTypeName = "PUBACK"; break;
            case 5: packetTypeName = "PUBREC"; break;
            case 6: packetTypeName = "PUBREL"; break;
            case 7: packetTypeName = "PUBCOMP"; break;
            case 8: packetTypeName = "SUBSCRIBE"; break;
            case 9: packetTypeName = "SUBACK"; break;
            case 10: packetTypeName = "UNSUBSCRIBE"; break;
            case 11: packetTypeName = "UNSUBACK"; break;
            case 12: packetTypeName = "PINGREQ"; break;
            case 13: packetTypeName = "PINGRESP"; break;
            case 14: packetTypeName = "DISCONNECT"; break;
            default: packetTypeName = "UNKNOWN"; break;
        }
        LOG_DEBUG("收到MQTT包: 类型=" + packetTypeName +
                " (" + std::to_string(packetType) + ")");
    } else {
        // TLS模式：只记录接收的加密数据长度
        LOG_DEBUG("网络接收成功 (TLS加密数据): " + std::to_string(received) + " 字节");
    }
    
    return static_cast<int>(received);
}

// ReSharper disable once CppDFAConstantFunctionResult
int WolfMqttAdapter::messageCallback(::MqttClient* client, ::MqttMessage* message,
                                     const byte msg_new, const byte msg_done) {
    if (!client || !client->ctx || !message) {
        return MQTT_CODE_SUCCESS;
    }

    auto* adapter = static_cast<WolfMqttAdapter*>(client->ctx);
    std::lock_guard pendingLock(adapter->pendingMessagesMutex_);

    if (msg_new) {
        adapter->incomingMessage_ = PendingMessage{};
        if (message->topic_name) {
            adapter->incomingMessage_.topic.assign(
                message->topic_name, message->topic_name_len);
        }
        adapter->incomingMessage_.qos = static_cast<QoS>(message->qos);
        adapter->receivingMessage_ = true;
    }

    if (!adapter->receivingMessage_) {
        return MQTT_CODE_SUCCESS;
    }

    if (message->buffer && message->buffer_len > 0) {
        adapter->incomingMessage_.payload.append(
            reinterpret_cast<const char*>(message->buffer),
            message->buffer_len);
    }

    if (msg_done) {
        adapter->pendingMessages_.push(std::move(adapter->incomingMessage_));
        adapter->incomingMessage_ = PendingMessage{};
        adapter->receivingMessage_ = false;
    }

    return MQTT_CODE_SUCCESS;
}

void WolfMqttAdapter::messageReceiveThread() {
    bool connectionLost = false;
    while (messageThreadRunning_.load() && connected_.load()) {
        if (pendingOperations_.load(std::memory_order_acquire) > 0) {
            std::this_thread::sleep_for(1ms);
            continue;
        }

        int rc = MQTT_CODE_ERROR_NETWORK;
        {
            std::lock_guard clientLock(clientMutex_);
            if (!wolfClient_ || networkContext_.socketFd < 0) {
                connectionLost = true;
                break;
            }
            rc = MqttClient_WaitMessage(wolfClient_.get(), 1000);
        }

        dispatchPendingMessages();

        if (rc == MQTT_CODE_ERROR_NETWORK || rc == MQTT_CODE_ERROR_TLS_CONNECT) {
            connectionLost = true;
            break;
        }

        if (rc != MQTT_CODE_ERROR_TIMEOUT && 
            rc != MQTT_CODE_SUCCESS && 
            rc != MQTT_CODE_CONTINUE) {
            LOG_WARN("消息接收错误: " + std::to_string(rc));
        }
    }

    messageThreadRunning_.store(false);
    if (connectionLost && connected_.exchange(false)) {
        std::function<void(bool)> callback;
        {
            std::lock_guard lock(mutex_);
            callback = connectionCallback_;
        }
        if (callback) {
            callback(false);
        }
    }
}

#endif // WOLFMQTT_ENABLED

#ifndef WOLFMQTT_ENABLED
Result<bool> WolfMqttAdapter::publish(
    const std::string_view topic,
    const std::string_view payload,
    const MqttProperties&,
    const QoS qos,
    const bool retained) {
    return publish(topic, payload, qos, retained);
}
#endif

} // namespace mqtt_client
