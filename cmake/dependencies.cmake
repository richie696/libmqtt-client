# ============================================================================
# 依赖管理配置文件
# ============================================================================
# 此文件包含所有依赖的FetchContent配置
# 可以在CMakeLists.txt中include此文件来统一管理依赖

include(FetchContent)

# ============================================================================
# wolfMQTT 配置
# ============================================================================
function(add_wolfmqtt_dependency)
    FetchContent_Declare(
        wolfmqtt
        GIT_REPOSITORY https://github.com/wolfSSL/wolfMQTT.git
        GIT_TAG        master  # 可以改为特定版本，如 v1.15.0
        GIT_SHALLOW    TRUE    # 只下载最新提交，节省时间
    )
    
    FetchContent_GetProperties(wolfmqtt)
    if(NOT wolfmqtt_POPULATED)
        message(STATUS "下载wolfMQTT...")
        FetchContent_Populate(wolfmqtt)
        set(WOLFMQTT_ROOT ${wolfmqtt_SOURCE_DIR} PARENT_SCOPE)
        set(WOLFMQTT_INCLUDE_DIR "${wolfmqtt_SOURCE_DIR}/include" PARENT_SCOPE)
        message(STATUS "wolfMQTT已下载到: ${wolfmqtt_SOURCE_DIR}")
    endif()
endfunction()

# ============================================================================
# nlohmann/json 配置
# ============================================================================
function(add_json_dependency)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.2
        GIT_SHALLOW    TRUE
    )
    
    FetchContent_GetProperties(nlohmann_json)
    if(NOT nlohmann_json_POPULATED)
        message(STATUS "下载nlohmann/json...")
        FetchContent_Populate(nlohmann_json)
        add_subdirectory(${nlohmann_json_SOURCE_DIR} ${nlohmann_json_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endfunction()

# ============================================================================
# Google Test 配置
# ============================================================================
function(add_googletest_dependency)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    
    FetchContent_GetProperties(googletest)
    if(NOT googletest_POPULATED)
        message(STATUS "下载Google Test...")
        FetchContent_Populate(googletest)
        add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endfunction()

# ============================================================================
# fmt 配置
# ============================================================================
function(add_fmt_dependency)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG        10.1.1
        GIT_SHALLOW    TRUE
    )
    
    FetchContent_GetProperties(fmt)
    if(NOT fmt_POPULATED)
        message(STATUS "下载fmt...")
        FetchContent_Populate(fmt)
        add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endfunction()

# ============================================================================
# spdlog 配置（日志库，可选）
# ============================================================================
function(add_spdlog_dependency)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.12.0
        GIT_SHALLOW    TRUE
    )
    
    FetchContent_GetProperties(spdlog)
    if(NOT spdlog_POPULATED)
        message(STATUS "下载spdlog...")
        FetchContent_Populate(spdlog)
        add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endfunction()
