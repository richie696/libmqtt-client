/**
 * @file wolfmqtt_adapter.h
 * @brief wolfMQTT适配器
 * 
 * 封装wolfMQTT C API，提供统一的C++接口。
 */

#ifndef MQTT_CLIENT_ADAPTER_WOLFMQTT_ADAPTER_H
#define MQTT_CLIENT_ADAPTER_WOLFMQTT_ADAPTER_H

#include "mqtt_client/config/config.h"
#include "mqtt_client/core/types.h"
#include "mqtt_client/core/result.h"
#include "mqtt_client/core/error.h"
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include <queue>
#include <vector>

// wolfMQTT头文件
#ifdef WOLFMQTT_ENABLED
#include <wolfmqtt/mqtt_client.h>
#include <wolfmqtt/mqtt_socket.h>
#include <wolfmqtt/mqtt_types.h>
#endif

namespace mqtt_client {

/**
 * @brief wolfMQTT适配器
 * 
 * 封装wolfMQTT C API，提供统一的C++接口。
 * 注意：wolfMQTT使用C API，这里使用MqttClient结构体（不是类）。
 */
class WolfMqttAdapter {
public:
    /**
     * @brief 构造函数
     * 
     * @param config MQTT配置
     */
    explicit WolfMqttAdapter(const MqttConfig& config);
    
    /**
     * @brief 析构函数
     */
    ~WolfMqttAdapter();
    
    // 禁止拷贝和赋值
    WolfMqttAdapter(const WolfMqttAdapter&) = delete;
    WolfMqttAdapter& operator=(const WolfMqttAdapter&) = delete;
    
    /**
     * @brief 初始化适配器
     * 
     * @return Result<bool> 初始化结果
     */
    [[nodiscard]] Result<bool> initialize();
    
    /**
     * @brief 连接服务器
     * 
     * @return Result<bool> 连接结果
     */
    [[nodiscard]] Result<bool> connect();
    
    /**
     * @brief 断开连接
     * 
     * @return Result<bool> 断开结果
     */
    [[nodiscard]] Result<bool> disconnect(bool force = false);
    
    /**
     * @brief 检查是否已连接
     * 
     * @return true 已连接
     * @return false 未连接
     */
    [[nodiscard]] bool isConnected() const;
    
    /**
     * @brief 发布消息
     * 
     * @param topic 主题
     * @param payload 消息内容
     * @param qos QoS等级
     * @param retained 是否保留
     * @return Result<bool> 发布结果
     */
    [[nodiscard]] Result<bool> publish(std::string_view topic,
                                       std::string_view payload,
                                       QoS qos,
                                       bool retained = false);
    
    /**
     * @brief 发布消息（带MQTT 5.0属性）
     * 
     * 注意：此方法仅在 MQTT 5.0 协议下有效
     * 对于 MQTT 3.1.1，属性将被忽略
     * 
     * @param topic 主题
     * @param payload 消息内容
     * @param properties MQTT 5.0属性
     * @param qos QoS等级
     * @param retained 是否保留
     * @return Result<bool> 发布结果
     */
    [[nodiscard]] Result<bool> publish(std::string_view topic,
                                       std::string_view payload,
                                       const MqttProperties& properties,
                                       QoS qos,
                                       bool retained = false);
    
    /**
     * @brief 订阅主题
     * 
     * @param topic 主题
     * @param qos QoS等级
     * @return Result<bool> 订阅结果
     */
    [[nodiscard]] Result<bool> subscribe(std::string_view topic, QoS qos);
    
    /**
     * @brief 取消订阅
     * 
     * @param topic 主题
     * @return Result<bool> 取消订阅结果
     */
    [[nodiscard]] Result<bool> unsubscribe(std::string_view topic);
    
    /**
     * @brief 设置消息接收回调
     * 
     * @param callback 回调函数
     */
    void setMessageCallback(const std::function<void(std::string_view topic,
                                                     std::string_view payload,
                                                     QoS qos)>& callback);
    
    /**
     * @brief 设置连接状态回调
     * 
     * @param callback 回调函数
     */
    void setConnectionCallback(const std::function<void(bool connected)>& callback);
    
    /**
     * @brief 处理网络I/O（需要在事件循环中调用）
     * 
     * @return Result<bool> 处理结果
     */
    [[nodiscard]] Result<bool> processNetwork() const;
    
    /**
     * @brief 清理资源
     */
    void cleanup();

private:
#ifdef WOLFMQTT_ENABLED
    /**
     * @brief 创建wolfMQTT客户端
     * 
     * @return Result<bool> 创建结果
     */
    [[nodiscard]] Result<bool> createClient();
    
    /**
     * @brief 配置连接参数
     * 
     * @param connect 连接参数结构体（输出）
     * @return Result<bool> 配置结果
     */
    [[nodiscard]] Result<bool> configureConnection(MqttConnect& connect);
    
    /**
     * @brief 配置TLS
     * 
     * @return Result<bool> 配置结果
     */
    [[nodiscard]] Result<bool> configureTLS();
    
    /**
     * @brief 网络连接回调（wolfMQTT回调）
     * 
     * @param context 上下文（WolfMqttAdapter实例）
     * @param host 主机地址
     * @param port 端口号
     * @param timeout_ms 超时时间（毫秒）
     * @return 0表示成功，负数表示错误
     */
    static int networkConnect(void* context, const char* host, word16 port, int timeout_ms);
    
    /**
     * @brief 网络断开回调（wolfMQTT回调）
     * 
     * @param context 上下文（WolfMqttAdapter实例）
     * @return 0表示成功，负数表示错误
     */
    static int networkDisconnect(void* context);
    
    /**
     * @brief 网络发送回调（wolfMQTT回调）
     * 
     * @param context 上下文（WolfMqttAdapter实例）
     * @param buf 要发送的数据
     * @param bufLen 数据长度
     * @return 实际发送的字节数，或错误码
     */
    static int networkSend(void* context, const byte* buf, int buf_len, int timeout_ms);
    
    /**
     * @brief 网络接收回调（wolfMQTT回调）
     * 
     * @param context 上下文（WolfMqttAdapter实例）
     * @param buf 接收缓冲区
     * @param buf_len 缓冲区大小
     * @param timeout_ms 超时时间（毫秒）
     * @return 实际接收的字节数，或错误码
     */
    static int networkRecv(void* context, byte* buf, int buf_len, int timeout_ms);
    
    /**
     * @brief 消息接收回调（wolfMQTT回调）
     * 
     * @param client wolfMQTT客户端
     * @param message 消息
     * @param msg_new 是否是新消息
     * @param msg_done 消息是否接收完成
     * @return MQTT_CODE_SUCCESS或其他错误码
     */
    static int messageCallback(::MqttClient* client, ::MqttMessage* message,
                              byte msg_new, byte msg_done);

    /**
     * @brief TLS上下文配置回调
     */
    static int tlsCallback(::MqttClient* client);
    
    // wolfMQTT客户端实例（C结构体）
    std::unique_ptr<MqttClient> wolfClient_;
    MqttNet net_{};  // 网络抽象层
    
    // 缓冲区
    std::vector<byte> txBuffer_;
    std::vector<byte> rxBuffer_;
    
    // 网络上下文
    struct NetworkContext {
        int socketFd;
        bool isTLS;
        // 其他网络相关数据
    } networkContext_{};
#endif
    
    // 配置
    MqttConfig config_;
    
    // 回调函数
    std::function<void(std::string_view, std::string_view, QoS)> messageCallback_;
    std::function<void(bool)> connectionCallback_;
    
    // 消息接收线程
    std::thread messageThread_;
    std::atomic<bool> messageThreadRunning_;

    struct PendingMessage {
        std::string topic;
        std::string payload;
        QoS qos;
    };
    std::queue<PendingMessage> pendingMessages_;
    PendingMessage incomingMessage_;
    bool receivingMessage_{false};
    std::mutex pendingMessagesMutex_;
    
    /**
     * @brief 消息接收线程函数
     */
    void messageReceiveThread();
    
    // 状态
    std::atomic<bool> connected_;
    std::atomic<bool> initialized_;
    
    // 线程安全
    mutable std::mutex mutex_;
    mutable std::mutex clientMutex_;
    std::atomic<unsigned int> pendingOperations_{0};
    std::atomic<std::uint16_t> packetIdCounter_{1};

    void dispatchPendingMessages();
    [[nodiscard]] std::uint16_t nextPacketId() noexcept;
};

} // namespace mqtt_client

#endif // MQTT_CLIENT_ADAPTER_WOLFMQTT_ADAPTER_H
