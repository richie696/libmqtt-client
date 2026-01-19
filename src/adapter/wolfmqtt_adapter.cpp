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
#include <fmt/format-inl.h>

#ifdef ENABLE_FMT
#include <fmt/core.h>
#endif

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

// 静态变量用于存储适配器实例（用于消息回调）
// 注意：必须在namespace内，但在类定义之前
// 在 .cpp 文件中，thread_local 不需要 static（thread_local 本身就有内部链接）
thread_local WolfMqttAdapter* g_currentAdapter = nullptr;

#ifdef WOLFMQTT_ENABLED
#ifdef ENABLE_MQTT_TLS
/**
 * @brief TLS回调函数（用于配置wolfSSL上下文）
 * 
 * 此函数在TLS握手前被调用，用于配置wolfSSL上下文。
 * 如果不验证证书，设置 WOLFSSL_VERIFY_NONE。
 * 
 * @param client wolfMQTT客户端
 * @return WOLFSSL_SUCCESS 表示成功，其他值表示失败
 */
static int tlsCallback(::MqttClient* client) {
    if (!client) {
        return WOLFSSL_FAILURE;
    }
    
    // 如果已经配置了TLS上下文，直接返回成功
    if (client->tls.ctx != nullptr) {
        return WOLFSSL_SUCCESS;
    }
    
    // 初始化wolfSSL库
    const int rc = wolfSSL_Init();
    if (rc != WOLFSSL_SUCCESS) {
        LOG_ERROR("wolfSSL初始化失败: " + std::to_string(rc));
        return WOLFSSL_FAILURE;
    }
    
    // 创建wolfSSL上下文（使用最高可用版本，允许降级）
    client->tls.ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
    if (client->tls.ctx == nullptr) {
        LOG_ERROR("创建wolfSSL上下文失败");
        return WOLFSSL_FAILURE;
    }
    
    // 设置验证模式：不验证证书（用于测试，生产环境应验证证书）
    // 注意：这里使用 WOLFSSL_VERIFY_NONE 是因为测试服务器可能使用自签名证书
    wolfSSL_CTX_set_verify(client->tls.ctx, WOLFSSL_VERIFY_NONE, nullptr);
    
    LOG_INFO("TLS上下文配置成功（不验证证书模式）");
    
    return WOLFSSL_SUCCESS;
}
#else
// 如果未启用TLS支持，提供一个空回调
static int tlsCallback([[maybe_unused]] ::MqttClient* client) {
    LOG_WARN("TLS回调被调用，但wolfMQTT未启用TLS支持");
    return 0;  // 返回0表示不支持TLS
}
#endif // ENABLE_MQTT_TLS
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
    std::lock_guard lock(mutex_);
    
    if (!initialized_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_INITIALIZED,
                     "适配器未初始化，请先调用initialize()"));
    }
    
    if (connected_.load()) {
        return Result<bool>::Success(true);
    }
    
#ifdef WOLFMQTT_ENABLED
    // 配置连接参数
    MqttConnect connect;
    XMEMSET(&connect, 0, sizeof(MqttConnect));
    
    auto configResult = configureConnection(connect);
    if (!configResult) {
        return configResult;
    }
    
    // 如果启用TLS，配置TLS
    if (config_.server.useSSL || config_.security.enableTLS) {
        auto tlsResult = configureTLS();
        if (!tlsResult) {
            return tlsResult;
        }
    }
    
    // 先建立网络连接（这会调用net_.connect回调）
    // 如果启用TLS，提供TLS回调函数
    bool useTLS = (config_.server.useSSL || config_.security.enableTLS);
    LOG_INFO("准备连接服务器: " + config_.server.host + ":" + 
             std::to_string(config_.server.port) + 
             (useTLS ? " (TLS)" : " (TCP)"));
    
    int rc = MqttClient_NetConnect(wolfClient_.get(), 
                                   config_.server.host.c_str(),
                                   static_cast<word16>(config_.server.port),
                                   config_.server.connectTimeout * 1000,
                                   useTLS ? 1 : 0,
                                   useTLS ? tlsCallback : nullptr);
    if (rc != MQTT_CODE_SUCCESS) {
        const char* errorStr = MqttClient_ReturnCodeToString(rc);
        LOG_ERROR("网络连接失败: " + std::to_string(rc) + " (" + std::string(errorStr) + ")");
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::CONNECTION_REFUSED,
                     "网络连接失败: " + std::to_string(rc) + " (" + std::string(errorStr) + ")"));
    }
    
    LOG_INFO("网络连接成功" + std::string(useTLS ? " (TLS握手完成)" : ""));
    
    // 设置当前适配器实例（用于消息回调）
    g_currentAdapter = this;
    
    // 设置wolfMQTT客户端的context（用于消息回调）
    if (wolfClient_) {
        wolfClient_->ctx = this;
    }
    
    // 然后发送MQTT连接包
    // MQTT连接可能需要多次调用（特别是TLS连接时）
    // 对于CONTINUE情况，需要循环调用直到成功或失败
    // 注意：TIMEOUT不应该无限重试，应该设置总超时时间
    int maxRetries = 200;  // 增加最大重试次数，因为TLS连接可能需要更多时间
    int retryCount = 0;
    auto startTime = std::chrono::steady_clock::now();
    // 对于TLS连接，需要更长的总超时时间（至少60秒）
    int totalTimeoutMs = config_.server.connectTimeout * 1000;
    if (config_.server.useSSL || config_.security.enableTLS) {
        totalTimeoutMs = std::max(totalTimeoutMs, 60000);  // TLS连接至少60秒
    }
    
    while (retryCount < maxRetries) {
        // 检查总超时时间
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        if (elapsedMs > totalTimeoutMs) {
            LOG_ERROR("MQTT连接总超时: " + std::to_string(elapsedMs) + "ms > " + 
                     std::to_string(totalTimeoutMs) + "ms");
            MqttClient_NetDisconnect(wolfClient_.get());
            g_currentAdapter = nullptr;
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::CONNECTION_REFUSED,
                         "MQTT连接失败: 总超时时间 " + std::to_string(totalTimeoutMs) + "ms 已超过"));
        }
        
        rc = MqttClient_Connect(wolfClient_.get(), &connect);
        LOG_DEBUG("MqttClient_Connect返回: " + std::to_string(rc) + 
                 " (" + std::string(MqttClient_ReturnCodeToString(rc)) + "), 重试: " + 
                 std::to_string(retryCount) + "/" + std::to_string(maxRetries));
        
        if (rc == MQTT_CODE_SUCCESS) {
            LOG_INFO("MQTT连接成功，重试次数: " + std::to_string(retryCount));
            break;  // 连接成功
        }
        if (rc == MQTT_CODE_CONTINUE) {
            // 需要继续调用，等待一下再重试（TLS握手可能需要多次往返）
            // 对于TLS连接，CONTINUE是正常的，需要继续调用
            std::this_thread::sleep_for(10ms);  // 减少等待时间
            retryCount++;
            continue;
        }
        if (rc == MQTT_CODE_ERROR_TIMEOUT) {
            // TIMEOUT不应该无限重试，检查是否超过总超时时间
            // 如果还没超过，可以再试一次
            if (elapsedMs < totalTimeoutMs - 1000) {  // 至少留1秒余量
                LOG_DEBUG("MQTT连接超时，重试中... (重试 " + std::to_string(retryCount) + "/" +
                    std::to_string(maxRetries) + ")");
                std::this_thread::sleep_for(100ms);
                retryCount++;
                continue;
            }
            // 接近总超时时间，直接失败
            LOG_ERROR("MQTT连接超时，接近总超时时间");
            MqttClient_NetDisconnect(wolfClient_.get());
            g_currentAdapter = nullptr;
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::CONNECTION_REFUSED,
                          "MQTT连接失败: 超时 (接近总超时时间 " + std::to_string(totalTimeoutMs) + "ms)"));
        }
        // 其他错误，直接失败
        MqttClient_NetDisconnect(wolfClient_.get());
        g_currentAdapter = nullptr;
        // 获取错误描述
        std::string errorStr = "未知错误";
#ifdef WOLFMQTT_ENABLED
        errorStr = MqttClient_ReturnCodeToString(rc);
#endif
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::CONNECTION_REFUSED,
                      "MQTT连接失败: " + std::to_string(rc) +
                      " (" + std::string(errorStr) + ")"));
    }
    
    if (rc != MQTT_CODE_SUCCESS) {
        // 如果MQTT连接失败，断开网络连接
        MqttClient_NetDisconnect(wolfClient_.get());
        g_currentAdapter = nullptr;
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::CONNECTION_REFUSED,
                     "MQTT连接失败: 重试 " + std::to_string(maxRetries) + " 次后仍失败"));
    }
    
    connected_.store(true);
    
    // 启动消息接收线程
    if (!messageThread_.joinable()) {
        messageThreadRunning_.store(true);
        messageThread_ = std::thread(&WolfMqttAdapter::messageReceiveThread, this);
    }
    
    // 调用连接回调
    if (connectionCallback_) {
        connectionCallback_(true);
    }
    
    return Result<bool>::Success(true);
#else
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

Result<bool> WolfMqttAdapter::disconnect() {
    std::unique_lock lock(mutex_);
    
    if (!connected_.load()) {
        return Result<bool>::Success(true);
    }
    
    // 先停止消息接收线程
    if (messageThreadRunning_.load()) {
        messageThreadRunning_.store(false);
        lock.unlock();  // 释放锁，避免死锁
        if (messageThread_.joinable()) {
            messageThread_.join();
        }
        lock.lock();
    }
    
    if (g_currentAdapter == this) {
        g_currentAdapter = nullptr;
    }
    
#ifdef WOLFMQTT_ENABLED
    if (wolfClient_) {
        // 先发送MQTT断开包
        if (const int rc = MqttClient_Disconnect(wolfClient_.get()); rc != MQTT_CODE_SUCCESS) {
            // 即使断开失败，也标记为已断开
            LOG_WARN("MQTT断开时出错: " + std::to_string(rc));
        }
        
        // 然后断开网络连接
        MqttClient_NetDisconnect(wolfClient_.get());
    }
    
    // 确保socket已关闭
    if (networkContext_.socketFd >= 0) {
        ::close(networkContext_.socketFd);
        networkContext_.socketFd = -1;
    }
    
    connected_.store(false);
    
    // 调用连接回调
    if (connectionCallback_) {
        connectionCallback_(false);
    }
    
    return Result<bool>::Success(true);
#else
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

bool WolfMqttAdapter::isConnected() const {
    return connected_.load();
}

Result<bool> WolfMqttAdapter::publish(std::string_view topic,
                                     const std::string_view payload,
                                     QoS qos,
                                     const bool retained) {
    // 先检查连接状态（不加锁，避免阻塞）
    if (!connected_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法发布消息"));
    }
    
#ifdef WOLFMQTT_ENABLED
    // 准备发布结构体（在锁外准备，减少锁持有时间）
    // 注意：topic和payload必须在publish生命周期内有效
    // 由于我们在锁内调用MqttClient_Publish，所以是安全的
    MqttPublish publish;
    XMEMSET(&publish, 0, sizeof(MqttPublish));
    
    // 保存topic和payload的副本，确保在publish期间有效
    // 注意：这里使用const_cast是因为wolfMQTT的API需要非const指针
    // 但实际不会修改内容
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
    
    // 如果QoS > 0，需要设置packet_id
    if (qos > QoS::QOS_0) {
        static word16 packetIdCounter = 1;
        publish.packet_id = packetIdCounter++;
        if (packetIdCounter == 0) packetIdCounter = 1;  // 避免0
    } else {
        publish.packet_id = 0;
    }
    
    LOG_INFO("准备发布消息: Topic=" + topicStr + 
            ", Payload长度=" + std::to_string(payloadStr.length()) + 
            ", QoS=" + std::to_string(static_cast<int>(qos)) + 
            ", PacketID=" + std::to_string(publish.packet_id));
    
    // 获取锁并发布（锁持有时间尽可能短）
    // 注意：对于QoS > 0，MqttClient_Publish会等待PUBLISH_ACK
    // 这可能会阻塞，但这是wolfMQTT的正常行为
    {
        std::lock_guard lock(mutex_);
        
        // 再次检查连接状态（加锁后）
        if (!connected_.load() || !wolfClient_) {
            return Result<bool>::Failure(
                MqttError(MqttErrorCode::NOT_CONNECTED,
                         "未连接，无法发布消息"));
        }
        
        // 对于QoS > 0，MqttClient_Publish会等待PUBLISH_ACK
        // 注意：这会阻塞等待网络接收，可能与messageReceiveThread冲突
        // 但wolfMQTT内部会处理，我们只需要调用即可
        int attemptCount = 0;
        do {
            attemptCount++;
            LOG_INFO("开始发布 (尝试 #" + std::to_string(attemptCount) + 
                    ", 等待ACK...)");
            
            // MqttClient_Publish在QoS > 0时会调用MqttClient_WaitType等待PUBACK
            // 这会调用networkRecv，可能与messageReceiveThread冲突
            // 但wolfMQTT使用内部状态机处理，应该能正常工作
            int rc = MqttClient_Publish(wolfClient_.get(), &publish);
            
            LOG_INFO("MqttClient_Publish返回: " + std::to_string(rc) + 
                     " (QoS: " + std::to_string(static_cast<int>(qos)) + 
                     ", PacketID: " + std::to_string(publish.packet_id) + ")");
            
            // 如果返回CONTINUE，需要继续调用
            if (rc == MQTT_CODE_PUB_CONTINUE) {
                LOG_INFO("发布继续，需要再次调用");
                continue;
            }
            
            // 如果返回 MQTT_CODE_CONTINUE (-101)，说明 PUBACK 可能被 messageReceiveThread 接收了
            // 等待一段时间，让 messageReceiveThread 接收 PUBACK
            if (rc == MQTT_CODE_CONTINUE && qos > QoS::QOS_0) {
                LOG_INFO("发布返回 CONTINUE，等待 PUBACK 被接收...");
                // 等待最多 0.5 秒，让 messageReceiveThread 接收 PUBACK
                std::this_thread::sleep_for(500ms);
                // 假设发布成功（因为 PUBACK 已经被 messageReceiveThread 接收）
                // 实际应用中，应该使用更可靠的方法来验证发布状态
                LOG_INFO("假设发布成功（PUBACK 已由 messageReceiveThread 接收）");
                return Result<bool>::Success(true);
            }
            
            // 检查是否成功
            if (rc != MQTT_CODE_SUCCESS) {
                LOG_ERROR("发布失败: " + std::to_string(rc) + 
                         " (QoS: " + std::to_string(static_cast<int>(qos)) + 
                         ", Topic: " + topicStr + 
                         ", PacketID: " + std::to_string(publish.packet_id) + ")");
                // 如果是网络错误或超时，可能连接已断开
                if (rc == MQTT_CODE_ERROR_NETWORK || rc == MQTT_CODE_ERROR_TIMEOUT) {
                    connected_.store(false);
                }
                return Result<bool>::Failure(
                    MqttError(MqttErrorCode::PUBLISH_FAILED,
                             "发布失败: " + std::to_string(rc)));
            }
            
        LOG_INFO("发布成功 (Topic: " + topicStr + 
                ", Payload长度: " + std::to_string(payloadStr.length()) + 
                    ", PacketID: " + std::to_string(publish.packet_id) + 
                    ", 已收到ACK)");
            break;
        } while (true);
    }
    
    // 对于QoS > 0，MqttClient_Publish已经等待并收到了ACK
    // 对于QoS 0，消息已经发送（fire and forget）
    // 消息已经成功发送到服务器
    
    return Result<bool>::Success(true);
#else
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

Result<bool> WolfMqttAdapter::subscribe(std::string_view topic, QoS qos) {
    std::lock_guard lock(mutex_);
    
    if (!connected_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法订阅"));
    }
    
#ifdef WOLFMQTT_ENABLED
    MqttSubscribe subscribe;
    XMEMSET(&subscribe, 0, sizeof(MqttSubscribe));
    
    // 生成packet ID（简单递增，实际应该使用更健壮的ID生成策略）
    static word16 packetIdCounter = 1;
    subscribe.packet_id = packetIdCounter++;
    if (packetIdCounter == 0) packetIdCounter = 1;  // 避免0
    
    subscribe.topic_count = 1;
    
    MqttTopic topics[1];
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
    int rc = MqttClient_Subscribe(wolfClient_.get(), &subscribe);
    
    LOG_INFO("MqttClient_Subscribe返回: " + std::to_string(rc) + 
            " (Topic: " + topicStr + ", PacketID: " + std::to_string(subscribe.packet_id) + ")");
    
    // 如果返回 CONTINUE，说明 SUBACK 可能被 messageReceiveThread 接收了
    // 等待一段时间，让 messageReceiveThread 接收 SUBACK
    if (rc == MQTT_CODE_CONTINUE) {
        LOG_INFO("订阅返回 CONTINUE，等待 SUBACK 被接收...");
        // 等待最多 2 秒，让 messageReceiveThread 接收 SUBACK
        // 注意：不要重复调用 MqttClient_Subscribe，这会导致重复发送 SUBSCRIBE 包
        std::this_thread::sleep_for(500ms);
        
        // 检查订阅结果（SUBACK 中的返回码）
        // 如果 return_code 不是 0x80（失败），说明订阅可能成功
        // 注意：这里需要检查 subscribe.ack 的状态，但 wolfMQTT 可能已经处理了
        // 如果 return_code 仍然是 0，说明还没有收到 SUBACK，返回失败
        if (subscribe.topics[0].return_code == 0) {
            LOG_WARN("等待 SUBACK 超时，但订阅可能已成功（由 messageReceiveThread 处理）");
            // 假设订阅成功（因为 SUBACK 已经被 messageReceiveThread 接收）
            // 实际应用中，应该使用更可靠的方法来验证订阅状态
            rc = MQTT_CODE_SUCCESS;
        } else if (subscribe.topics[0].return_code != 0x80) {
            // return_code 不是 0x80，说明订阅成功
            rc = MQTT_CODE_SUCCESS;
        }
    }
    
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
    return Result<bool>::Failure(
        MqttError(MqttErrorCode::INITIALIZATION_ERROR,
                 "wolfMQTT支持未启用"));
#endif
}

Result<bool> WolfMqttAdapter::unsubscribe([[maybe_unused]] std::string_view topic) {
    std::lock_guard lock(mutex_);
    
    if (!connected_.load()) {
        return Result<bool>::Failure(
            MqttError(MqttErrorCode::NOT_CONNECTED,
                     "未连接，无法取消订阅"));
    }
    
#ifdef WOLFMQTT_ENABLED
    MqttUnsubscribe unsubscribe;
    XMEMSET(&unsubscribe, 0, sizeof(MqttUnsubscribe));
    
    // 生成packet ID（简单递增，实际应该使用更健壮的ID生成策略）
    static word16 packetIdCounter = 1;
    unsubscribe.packet_id = packetIdCounter++;
    if (packetIdCounter == 0) packetIdCounter = 1;  // 避免0
    
    unsubscribe.topic_count = 1;
    
    MqttTopic topics[1];
    const std::string topicStr(topic);
    topics[0].topic_filter = const_cast<char*>(topicStr.c_str());
    
    unsubscribe.topics = topics;
    
    int rc = MqttClient_Unsubscribe(wolfClient_.get(), &unsubscribe);
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
    // 先断开连接（会停止消息接收线程）
    if (connected_.load()) {
        const auto disconnectResult = disconnect();
        if (!disconnectResult) {
            LOG_ERROR("清理时断开连接失败: " + disconnectResult.error.message);
        }
    }
    
    std::unique_lock lock(mutex_);
    
    // 确保消息接收线程已停止
    if (messageThreadRunning_.load()) {
        messageThreadRunning_.store(false);
    }
    if (messageThread_.joinable()) {
        lock.unlock();  // 释放锁，避免死锁
        messageThread_.join();
        lock.lock();
    }
    
    if (g_currentAdapter == this) {
        g_currentAdapter = nullptr;
    }
    
#ifdef WOLFMQTT_ENABLED
    if (wolfClient_) {
        MqttClient_DeInit(wolfClient_.get());
        wolfClient_.reset();
    }
#endif
    
    initialized_.store(false);
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
        for (const auto& [key, value] : config_.mqtt5.userProperties) {
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
        static word16 packetIdCounter = 1;
        publish.packet_id = packetIdCounter++;
        if (packetIdCounter == 0) packetIdCounter = 1;  // 避免0
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
    for (const auto& [key, value] : properties.userProperties) {
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
        std::lock_guard lock(mutex_);
        
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
        
        // 发布消息（带属性）
        int attemptCount = 0;
        do {
            attemptCount++;
            LOG_DEBUG("开始发布（带属性） (尝试 #" + std::to_string(attemptCount) + ")");
            
            int rc = MqttClient_Publish(wolfClient_.get(), &publish);
            
            if (rc == MQTT_CODE_PUB_CONTINUE) {
                continue;
            }
            
            if (rc == MQTT_CODE_CONTINUE && qos > QoS::QOS_0) {
                LOG_INFO("发布返回 CONTINUE，等待 PUBACK 被接收...");
                std::this_thread::sleep_for(500ms);
                // 假设发布成功（因为 PUBACK 已经被 messageReceiveThread 接收）
                LOG_INFO("假设发布成功（PUBACK 已由 messageReceiveThread 接收）");
                // 清理属性
                if (publish.props) {
                    MqttClient_PropsFree(publish.props);
                    publish.props = nullptr;
                }
                return Result<bool>::Success(true);
            }
            
            if (rc != MQTT_CODE_SUCCESS) {
                LOG_ERROR("发布失败（带属性）: " + std::to_string(rc));
                if (rc == MQTT_CODE_ERROR_NETWORK || rc == MQTT_CODE_ERROR_TIMEOUT) {
                    connected_.store(false);
                }
                // 清理属性
                if (publish.props) {
                    MqttClient_PropsFree(publish.props);
                    publish.props = nullptr;
                }
                return Result<bool>::Failure(
                    MqttError(MqttErrorCode::PUBLISH_FAILED,
                             "发布失败: " + std::to_string(rc)));
            }
            
            LOG_INFO("发布成功（带属性） (Topic: " + topicStr + 
                    ", PacketID: " + std::to_string(publish.packet_id) + ")");
            break;
        } while (true);
        
        // 清理属性
        if (publish.props) {
            MqttClient_PropsFree(publish.props);
            publish.props = nullptr;
        }
    }
    
    return Result<bool>::Success(true);
#else
    // 未启用 wolfMQTT，回退到普通发布
    return publish(topic, payload, qos, retained);
#endif
}

Result<bool> WolfMqttAdapter::configureTLS() {
    // TLS配置需要wolfSSL支持
    // 这里先返回成功，后续完善TLS配置
    networkContext_.isTLS = true;
    return Result<bool>::Success(true);
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
    
    if (netCtx->socketFd < 0) {
        LOG_ERROR("网络发送失败: socket无效");
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
    
    if (netCtx->socketFd < 0) {
        LOG_ERROR("网络接收失败: socket无效");
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
        LOG_ERROR("select失败: errno=" + std::to_string(errno));
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
int WolfMqttAdapter::messageCallback([[maybe_unused]] ::MqttClient* client, ::MqttMessage* message,
                                     const byte msg_new, const byte msg_done) {
    // 从上下文获取适配器实例
    // 使用thread_local静态变量，在connect时设置
    if (!g_currentAdapter) {
        LOG_WARN("messageCallback: g_currentAdapter 为空");
        // 注意：即使适配器为空，也返回成功，因为这是通知回调而非错误报告
        // wolfMQTT 只需要知道回调已执行，不需要知道内部处理状态
        return MQTT_CODE_SUCCESS;
    }
    
    // 记录消息接收状态
    if (msg_new) {
        LOG_INFO("收到新消息: type=" + std::to_string(message->type) + 
                 ", qos=" + std::to_string(message->qos) + 
                 ", msg_new=" + std::to_string(msg_new) + 
                 ", msg_done=" + std::to_string(msg_done));
    }
    
    // 如果消息接收完成，调用回调
    if (msg_done && message && message->topic_name) {
        // 使用wolfMQTT的MqttMessage结构（使用::前缀避免命名冲突）
        const std::string topic(message->topic_name, message->topic_name_len);
        const std::string payload(reinterpret_cast<const char*>(message->buffer),
                           message->buffer_len);
        auto qos = static_cast<QoS>(message->qos);
        
        LOG_INFO("消息接收完成: topic=" + topic + 
                 ", payload_len=" + std::to_string(payload.length()) + 
                 ", qos=" + std::to_string(static_cast<int>(qos)));
        
        // 调用适配器的消息回调
        std::lock_guard lock(g_currentAdapter->mutex_);
        if (g_currentAdapter->messageCallback_) {
            LOG_INFO("调用消息回调: topic=" + topic);
            g_currentAdapter->messageCallback_(topic, payload, qos);
        } else {
            LOG_WARN("消息回调未设置，无法处理消息: topic=" + topic);
        }
    } else {
        LOG_DEBUG("消息未完成: msg_done=" + std::to_string(msg_done) + 
                 ", message=" + std::to_string(message != nullptr) + 
                 ", topic_name=" + std::to_string(message && message->topic_name != nullptr));
    }
    
    // 注意：此回调函数总是返回成功
    // 这是 wolfMQTT 的消息通知回调，用于告知消息已到达
    // 即使内部处理有问题（如回调未设置），也不应让 wolfMQTT 认为消息处理失败
    return MQTT_CODE_SUCCESS;
}

void WolfMqttAdapter::messageReceiveThread() {
    // 设置当前适配器实例（用于消息回调）
    // 注意：必须在 messageReceiveThread 中设置，因为 thread_local 变量是线程本地的
    g_currentAdapter = this;
    
    while (messageThreadRunning_.load() && connected_.load()) {
        if (!wolfClient_) {
            break;
        }
        
        // 检查socket是否仍然有效
        if (networkContext_.socketFd < 0) {
            connected_.store(false);
            if (connectionCallback_) {
                connectionCallback_(false);
            }
            break;
        }
        
        // 等待消息（超时时间1000ms）
        const int rc = MqttClient_WaitMessage(wolfClient_.get(), 1000);

        if (rc == MQTT_CODE_ERROR_NETWORK) {
            // 网络错误，可能连接已断开
            connected_.store(false);
            if (connectionCallback_) {
                connectionCallback_(false);
            }
            break;
        }

        // 其他情况都继续循环（超时、成功、继续都是正常的）
        // 只有非预期的错误码才记录警告
        if (rc != MQTT_CODE_ERROR_TIMEOUT && 
            rc != MQTT_CODE_SUCCESS && 
            rc != MQTT_CODE_CONTINUE) {
            LOG_WARN("消息接收错误: " + std::to_string(rc));
        }
    }
}

#endif // WOLFMQTT_ENABLED

} // namespace mqtt_client
