# ============================================================================
# 测试配置
# ============================================================================

# 启用测试
enable_testing()

# 集成测试选项（需要在早期定义）
# 注意：如果从父CMakeLists.txt传递，使用父的值；否则定义新选项
if(DEFINED ENABLE_INTEGRATION_TESTS)
    # 选项已从父CMakeLists.txt传递，不需要重新定义
else()
    option(ENABLE_INTEGRATION_TESTS "Enable integration tests with real MQTT server" OFF)
endif()

# 查找Google Test
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    # 尝试使用FetchContent下载Google Test
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    
    # 尝试下载，如果失败则提示用户手动安装
    FetchContent_MakeAvailable(googletest)
endif()

if(GTest_FOUND OR TARGET gtest)
    # 包含Google Test
    include(GoogleTest)
    
    # ========================================================================
    # 测试可执行文件
    # ========================================================================
    
    # 核心模块测试
    add_executable(test_core
        ${CMAKE_CURRENT_SOURCE_DIR}/test_core/test_result.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_core/test_error.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_core/test_types.cpp
    )
    
    target_link_libraries(test_core
        PRIVATE
            mqtt_client
            GTest::gtest
            GTest::gtest_main
    )
    
    gtest_discover_tests(test_core)
    
    # 配置管理测试
    add_executable(test_config
        ${CMAKE_CURRENT_SOURCE_DIR}/test_config/test_config_manager.cpp
    )
    
    target_link_libraries(test_config
        PRIVATE
            mqtt_client
            GTest::gtest
            GTest::gtest_main
    )
    
    gtest_discover_tests(test_config)
    
    # 连接管理测试
    add_executable(test_connection
        ${CMAKE_CURRENT_SOURCE_DIR}/test_connection/test_connection_manager.cpp
    )
    
    target_link_libraries(test_connection
        PRIVATE
            mqtt_client
            GTest::gtest
            GTest::gtest_main
    )
    
    gtest_discover_tests(test_connection)
    
    # 消息管理测试
    add_executable(test_message
        ${CMAKE_CURRENT_SOURCE_DIR}/test_message/test_message_manager.cpp
    )
    
    target_link_libraries(test_message
        PRIVATE
            mqtt_client
            GTest::gtest
            GTest::gtest_main
    )
    
    gtest_discover_tests(test_message)
    
    # 订阅管理测试
    add_executable(test_subscription
        ${CMAKE_CURRENT_SOURCE_DIR}/test_subscription/test_subscription_manager.cpp
    )
    
    target_link_libraries(test_subscription
        PRIVATE
            mqtt_client
            GTest::gtest
            GTest::gtest_main
    )
    
    gtest_discover_tests(test_subscription)
    
    # 监控模块测试
    add_executable(test_monitor
        ${CMAKE_CURRENT_SOURCE_DIR}/test_monitor/test_network_monitor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_monitor/test_connection_monitor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test_monitor/test_heartbeat_manager.cpp
    )
    
    target_link_libraries(test_monitor
        PRIVATE
            mqtt_client
            GTest::gtest
            GTest::gtest_main
    )
    
    gtest_discover_tests(test_monitor)
    
    # 重连管理测试
    add_executable(test_reconnect
        ${CMAKE_CURRENT_SOURCE_DIR}/test_reconnect/test_reconnect_manager.cpp
    )
    
    target_link_libraries(test_reconnect
        PRIVATE
            mqtt_client
            GTest::gtest
            GTest::gtest_main
    )
    
    gtest_discover_tests(test_reconnect)
    
    # 主客户端集成测试
    add_executable(test_client
        ${CMAKE_CURRENT_SOURCE_DIR}/test_client/test_embedded_mqtt_client.cpp
    )
    
    target_link_libraries(test_client
        PRIVATE
            mqtt_client
            GTest::gtest
            GTest::gtest_main
    )
    
    gtest_discover_tests(test_client)
    
    # 真实服务器集成测试（可选，需要网络连接）
    # test_integration 已删除，功能由 test_mqtt5_tcp_tls 覆盖
    # test_mqtt5_tcp_tls 专门针对 MQTT 5.0 协议，更符合当前需求
    
    # MQTT 5.0 发布测试程序已删除，功能由 test_mqtt5_tcp_tls 覆盖
    
    # MQTT 5.0 TCP和TLS收发消息测试程序
    add_executable(test_mqtt5_tcp_tls
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_mqtt5_tcp_tls.cpp
    )
    
    target_link_libraries(test_mqtt5_tcp_tls
        PRIVATE
            mqtt_client
    )
    
    message(STATUS "MQTT 5.0 测试程序已添加")
    
    # MQTT 3.1.1 TCP和TLS测试程序
    add_executable(test_mqtt311_tcp_tls
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_mqtt311_tcp_tls.cpp
    )
    
    target_link_libraries(test_mqtt311_tcp_tls
        PRIVATE
            mqtt_client
    )
    
    message(STATUS "MQTT 3.1.1 测试程序已添加")
    
    message(STATUS "测试已启用")
else()
    message(STATUS "测试已禁用 (使用 -DENABLE_TESTING=ON 启用)")
endif()
