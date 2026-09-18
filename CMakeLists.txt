cmake_minimum_required(VERSION 3.15)
project(mqtt_client VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_SHARED_LIBS "Build mqtt_client as a shared library" ON)

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ============================================================================
# 平台检测
# ============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_LINUX TRUE)
    message(STATUS "平台: Linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM_WINDOWS TRUE)
    message(STATUS "平台: Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_MACOS TRUE)
    message(STATUS "平台: macOS")
else()
    message(WARNING "未识别的平台: ${CMAKE_SYSTEM_NAME}")
endif()

# ============================================================================
# 架构检测
# ============================================================================
# 获取目标架构
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_64BIT TRUE)
else()
    set(ARCH_32BIT TRUE)
endif()

# 检测具体架构。macOS Universal 构建不能把宿主机架构冒充为全部目标架构。
if(PLATFORM_MACOS AND CMAKE_OSX_ARCHITECTURES AND
   CMAKE_OSX_ARCHITECTURES MATCHES "arm64" AND
   CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
    set(ARCH_UNIVERSAL TRUE)
    set(ARCH_NAME "universal")
    message(STATUS "架构: macOS Universal (arm64 + x86_64)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(ARCH_X86_64 TRUE)
    set(ARCH_NAME "x86_64")
    message(STATUS "架构: x86_64 (64位)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86|X86)$")
    set(ARCH_X86 TRUE)
    set(ARCH_NAME "x86")
    message(STATUS "架构: x86 (32位)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(ARCH_ARM64 TRUE)
    set(ARCH_NAME "arm64")
    message(STATUS "架构: ARM64 (64位)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|ARM|armv7|armv6)$")
    set(ARCH_ARM32 TRUE)
    set(ARCH_NAME "arm32")
    message(STATUS "架构: ARM32 (32位)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(mips|MIPS|mipsel)$")
    set(ARCH_MIPS32 TRUE)
    set(ARCH_NAME "mips32")
    message(STATUS "架构: MIPS32 (32位)")
else()
    message(WARNING "未识别的架构: ${CMAKE_SYSTEM_PROCESSOR}")
    set(ARCH_NAME "unknown")
endif()

# ============================================================================
# 平台特定配置
# ============================================================================

# Linux配置
if(PLATFORM_LINUX)
    # 链接pthread（Linux上通常需要）
    find_package(Threads REQUIRED)
    
    # 设置库文件扩展名
    set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
endif()

# Windows配置
if(PLATFORM_WINDOWS)
    # Windows特定的编译选项
    # 设置库文件扩展名
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
    
    # Windows下需要导出符号
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    
    # 如果是MSVC编译器
    if(MSVC)
        # 设置运行时库
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

# macOS配置
if(PLATFORM_MACOS)
    # 设置库文件扩展名
    set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".dylib")
    
    # macOS版本要求
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        message(STATUS "macOS部署目标: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        # 默认支持 macOS 10.15 (Catalina) 及以上版本
        # 这样可以同时支持Intel和M系列芯片
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "macOS部署目标版本")
    endif()
    
    # macOS架构支持
    # 如果未指定架构，默认构建Universal Binary（同时支持Intel和M系列芯片）
    if(NOT CMAKE_OSX_ARCHITECTURES)
        # 检测当前运行的架构
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE CURRENT_ARCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        
        if(CURRENT_ARCH STREQUAL "arm64")
            # 在M系列芯片上，默认同时构建arm64和x86_64（Universal Binary）
            set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS目标架构")
            message(STATUS "检测到M系列芯片，默认构建Universal Binary (arm64 + x86_64)")
        else()
            # 在Intel芯片上，默认同时构建x86_64和arm64（Universal Binary）
            set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS目标架构")
            message(STATUS "检测到Intel芯片，默认构建Universal Binary (x86_64 + arm64)")
        endif()
    else()
        message(STATUS "macOS架构: ${CMAKE_OSX_ARCHITECTURES}")
        
        # 验证架构是否有效
        set(VALID_ARCHS "x86_64;arm64")
        foreach(ARCH ${CMAKE_OSX_ARCHITECTURES})
            if(NOT ARCH IN_LIST VALID_ARCHS)
                message(WARNING "不支持的macOS架构: ${ARCH}，支持的架构: x86_64, arm64")
            endif()
        endforeach()
    endif()
    
    # 确保Universal Binary构建时使用正确的设置
    if(CMAKE_OSX_ARCHITECTURES MATCHES ".*arm64.*" AND CMAKE_OSX_ARCHITECTURES MATCHES ".*x86_64.*")
        message(STATUS "构建Universal Binary: 同时支持Intel (x86_64) 和 M系列 (arm64) 芯片")
        # Universal Binary需要特殊处理
        set(CMAKE_OSX_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}" CACHE STRING "" FORCE)
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES ".*arm64.*")
        message(STATUS "构建仅支持M系列芯片 (arm64)")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES ".*x86_64.*")
        message(STATUS "构建仅支持Intel芯片 (x86_64)")
    endif()
endif()

# 默认 macOS 架构是在平台配置阶段确定的，因此在生成宏和文件名之前
# 再归一化一次架构信息。
if(PLATFORM_MACOS AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64" AND
   CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
    unset(ARCH_X86_64)
    unset(ARCH_X86)
    unset(ARCH_ARM64)
    unset(ARCH_ARM32)
    unset(ARCH_MIPS32)
    set(ARCH_UNIVERSAL TRUE)
    set(ARCH_NAME "universal")
endif()

# ============================================================================
# 架构特定配置
# ============================================================================

# ARM架构配置（仅Linux，macOS不需要这些选项）
if(PLATFORM_LINUX AND ARCH_ARM32)
    # Linux ARM特定的编译选项
    add_compile_options(-mfloat-abi=hard)  # 如果支持硬件浮点
    message(STATUS "启用Linux ARM架构优化")
elseif(PLATFORM_MACOS AND ARCH_ARM64)
    # macOS ARM64 (M系列芯片) 不需要特殊选项，使用默认设置即可
    message(STATUS "macOS ARM64 (M系列芯片) 配置")
endif()

# MIPS架构配置（仅Linux）
if(PLATFORM_LINUX AND ARCH_MIPS32)
    # MIPS特定的编译选项
    add_compile_options(-mips32)
    message(STATUS "启用MIPS架构优化")
endif()

# ============================================================================
# 预处理器定义
# ============================================================================
add_definitions(
    -DPLATFORM_${CMAKE_SYSTEM_NAME}
)

# 平台特定的定义
if(PLATFORM_LINUX)
    add_definitions(-DPLATFORM_LINUX)
elseif(PLATFORM_WINDOWS)
    add_definitions(-DPLATFORM_WINDOWS)
    if(MSVC)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    endif()
elseif(PLATFORM_MACOS)
    add_definitions(-DPLATFORM_MACOS)
endif()

# 仅定义项目自有宏；编译器保留宏（如 __aarch64__）由每个目标架构自行提供。
if(ARCH_UNIVERSAL)
    add_definitions(-DARCH_UNIVERSAL)
elseif(ARCH_X86_64)
    add_definitions(-DARCH_X86_64)
elseif(ARCH_X86)
    add_definitions(-DARCH_X86)
elseif(ARCH_ARM64)
    add_definitions(-DARCH_ARM64)
elseif(ARCH_ARM32)
    add_definitions(-DARCH_ARM32)
elseif(ARCH_MIPS32)
    add_definitions(-DARCH_MIPS32)
endif()

# 位宽定义
if(ARCH_64BIT)
    add_definitions(-DARCH_64BIT)
else()
    add_definitions(-DARCH_32BIT)
endif()

# ============================================================================
# 生成文件名后缀（架构-系统-版本号）
# ============================================================================

# 确定平台名称
if(PLATFORM_LINUX)
    set(PLATFORM_NAME "linux")
elseif(PLATFORM_WINDOWS)
    set(PLATFORM_NAME "windows")
elseif(PLATFORM_MACOS)
    set(PLATFORM_NAME "macos")
else()
    set(PLATFORM_NAME "unknown")
endif()

# 处理macOS Universal Binary的情况
if(PLATFORM_MACOS AND CMAKE_OSX_ARCHITECTURES)
    # 如果指定了多个架构（Universal Binary）
    list(LENGTH CMAKE_OSX_ARCHITECTURES ARCH_COUNT)
    if(ARCH_COUNT GREATER 1)
        # Universal Binary使用"universal"作为架构标识
        set(FILE_ARCH_NAME "universal")
        message(STATUS "Universal Binary: 使用 'universal' 作为架构标识")
    else()
        # 单个架构，使用实际架构名称
        list(GET CMAKE_OSX_ARCHITECTURES 0 FILE_ARCH_NAME)
    endif()
else()
    # 非macOS或未指定架构，使用检测到的架构
    set(FILE_ARCH_NAME "${ARCH_NAME}")
endif()

# 生成文件名后缀：架构-系统-版本号
# 注意：在macOS上，CMake的VERSION属性会自动在库文件名后添加版本号
# 这会导致文件名包含重复的版本号（如：libname-1.0.0.1.0.0.dylib）
# 解决方案：从FILE_NAME_SUFFIX中移除版本号，让CMake自动管理
set(FILE_NAME_SUFFIX "${FILE_ARCH_NAME}-${PLATFORM_NAME}")
message(STATUS "文件名后缀: ${FILE_NAME_SUFFIX} (版本号由CMake VERSION属性自动添加)")

# ============================================================================
# 源文件
# ============================================================================
set(SOURCES
    src/core/result.cpp
    src/logger/logger_manager.cpp
    src/config/config_manager.cpp
    src/adapter/wolfmqtt_adapter.cpp
    src/connection/connection_manager.cpp
    src/message/message_manager.cpp
    src/subscription/subscription_manager.cpp
    src/monitor/network_monitor.cpp
    src/monitor/connection_monitor.cpp
    src/monitor/heartbeat_manager.cpp
    src/reconnect/reconnect_manager.cpp
    src/persistence/storage_engine.cpp
    src/persistence/persistence_manager.cpp
    src/persistence/idempotency_manager.cpp
    src/metrics/metrics.cpp
    src/embedded_mqtt_client.cpp
)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/mqtt_client/*.h"
)
file(GLOB_RECURSE INTERNAL_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/internal/*.h"
)

# ============================================================================
# 创建库
# ============================================================================
add_library(mqtt_client ${SOURCES} ${HEADERS} ${INTERNAL_HEADERS})
target_compile_features(mqtt_client PUBLIC cxx_std_17)

# 严格警告仅作用于本项目；不能把这些选项传播给 wolfSSL、wolfMQTT、
# fmt 等第三方目标。
if(MSVC)
    target_compile_options(mqtt_client PRIVATE /W4 /wd4251 /wd4275)
else()
    target_compile_options(mqtt_client PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 设置目标属性，包括自定义输出名称
# 注意：在macOS上，如果设置了VERSION属性，CMake会自动在文件名后添加版本号（用点分隔）
# 例如：OUTPUT_NAME="mqtt_client-arm64-macos" + VERSION="1.0.0" 
#      -> 实际文件: libmqtt_client-arm64-macos.1.0.0.dylib
#      -> 符号链接: libmqtt_client-arm64-macos.1.dylib
#      -> 符号链接: libmqtt_client-arm64-macos.dylib
# 
# 如果希望在文件名中使用连字符包含版本号（如：libmqtt_client-arm64-macos-1.0.0.dylib），
# 可以注释掉VERSION和SOVERSION属性，并在OUTPUT_NAME中包含版本号。
# 但这样会失去CMake的版本管理功能（自动创建符号链接等）。
set_target_properties(mqtt_client PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME "mqtt_client-${FILE_NAME_SUFFIX}"
    ARCHIVE_OUTPUT_NAME "mqtt_client-${FILE_NAME_SUFFIX}"
    LIBRARY_OUTPUT_NAME "mqtt_client-${FILE_NAME_SUFFIX}"
    RUNTIME_OUTPUT_NAME "mqtt_client-${FILE_NAME_SUFFIX}"
)

# ============================================================================
# 依赖管理配置（混合方案）
# ============================================================================
# 混合方案：
# - 核心依赖：使用固定版本的 Git Submodule（wolfMQTT, wolfSSL）
# - 其他依赖（CMake/header-only）：使用FetchContent（JSON, 测试框架等）

# 包含FetchContent模块（CMake 3.11+）
include(FetchContent)

# 设置 FetchContent 选项
set(FETCHCONTENT_QUIET OFF)  # 显示下载进度
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)  # 允许离线模式

# 依赖管理选项
option(USE_FETCHCONTENT "Use FetchContent for dependency management" ON)
option(USE_GIT_SUBMODULE "Use Git Submodules for core dependencies (wolfMQTT and wolfSSL)" ON)
option(ENABLE_WOLFMQTT "Enable wolfMQTT support" ON)
option(BUILD_BUNDLED_WOLFMQTT
    "Build the bundled wolfMQTT and wolfSSL submodules from source" ON)
option(ENABLE_JSON "Enable nlohmann/json support" ON)
option(ENABLE_YAML "Enable yaml-cpp support" ON)
option(ENABLE_TESTING "Enable Google Test" OFF)
option(ENABLE_INTEGRATION_TESTS
    "Register real-broker MQTT integration tests with CTest" OFF)

# 持久化格式和指标JSON输出目前都以nlohmann/json实现。
# 明确声明为必需依赖，避免ENABLE_JSON=OFF配置成功后在编译阶段才失败。
if(NOT ENABLE_JSON)
    message(FATAL_ERROR "ENABLE_JSON=OFF is not supported: persistence and metrics require nlohmann/json")
endif()

# 链接系统库
if(PLATFORM_LINUX)
    target_link_libraries(mqtt_client PRIVATE Threads::Threads)
endif()

# Prefer the pinned in-tree sources so a fresh clone does not depend on a
# separately installed wolfMQTT/wolfSSL pair. The legacy prebuilt-library path
# below remains available through BUILD_BUNDLED_WOLFMQTT=OFF.
set(BUNDLED_WOLFMQTT_CONFIGURED FALSE)
set(WOLFMQTT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wolfmqtt")
set(WOLFSSL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wolfssl")

if(ENABLE_WOLFMQTT AND USE_GIT_SUBMODULE AND BUILD_BUNDLED_WOLFMQTT)
    if(NOT EXISTS "${WOLFMQTT_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${WOLFMQTT_SOURCE_DIR}/wolfmqtt/mqtt_client.h" OR
       NOT EXISTS "${WOLFSSL_SOURCE_DIR}/CMakeLists.txt" OR
       NOT EXISTS "${WOLFSSL_SOURCE_DIR}/wolfssl/ssl.h")
        message(FATAL_ERROR
            "Bundled wolfMQTT/wolfSSL sources are missing. Run: "
            "git submodule update --init --recursive")
    endif()

    # wolfMQTT's CMake build consumes WITH_WOLFSSL_TREE and builds wolfSSL as
    # its child project. Disable dependency examples/tests to keep this library
    # build focused and enable every ABI-affecting feature used by our adapter.
    set(WITH_WOLFSSL_TREE "${WOLFSSL_SOURCE_DIR}" CACHE PATH
        "Bundled wolfSSL source tree" FORCE)
    set(WOLFSSL_EXAMPLES "no" CACHE STRING "" FORCE)
    set(WOLFSSL_CRYPT_TESTS "no" CACHE STRING "" FORCE)
    # wolfMQTT exports its own target unconditionally; wolfSSL must therefore
    # remain in an export set as its public link dependency.
    set(WOLFSSL_INSTALL "yes" CACHE STRING "" FORCE)
    set(WOLFSSL_IP_ALT_NAME "yes" CACHE STRING "" FORCE)
    set(WOLFSSL_SNI "yes" CACHE STRING "" FORCE)
    set(WOLFMQTT_EXAMPLES "no" CACHE BOOL "" FORCE)
    set(WOLFMQTT_UNIT_TESTS "no" CACHE STRING "" FORCE)
    set(WOLFMQTT_TLS "yes" CACHE STRING "" FORCE)
    set(WOLFMQTT_V5 "yes" CACHE STRING "" FORCE)
    set(WOLFMQTT_MT "yes" CACHE STRING "" FORCE)
    # wolfMQTT 默认把生成的 options.h 写回 Submodule。强制使用构建目录，
    # 以支持只读源码树、容器挂载和可复现的 out-of-source 构建。
    set(WOLFMQTT_BUILD_OUT_OF_TREE "yes" CACHE STRING "" FORCE)

    add_subdirectory(
        "${WOLFMQTT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/third_party/wolfmqtt"
    )

    target_link_libraries(mqtt_client PRIVATE wolfmqtt)
    # wolfMQTT/wolfSSL are implementation details; public headers no longer
    # expose their types or feature macros.
    target_compile_definitions(mqtt_client PRIVATE
        WOLFMQTT_ENABLED=1
        ENABLE_MQTT_TLS=1
        WOLFMQTT_V5=1
        WOLFMQTT_MULTITHREAD=1
    )
    target_include_directories(mqtt_client SYSTEM PRIVATE
        $<BUILD_INTERFACE:${WOLFMQTT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/third_party/wolfmqtt>
        $<BUILD_INTERFACE:${WOLFSSL_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/third_party/wolfmqtt/wolfssl>
    )
    set(BUNDLED_WOLFMQTT_CONFIGURED TRUE)
    message(STATUS "wolfMQTT/wolfSSL: using pinned bundled sources")
endif()

# ============================================================================
# 使用FetchContent管理依赖
# ============================================================================
if(USE_FETCHCONTENT)
    message(STATUS "========== 使用FetchContent管理依赖 ==========")
    
    # ========================================================================
    # wolfMQTT 依赖（核心依赖，使用Git Submodule）
    # ========================================================================
    if(ENABLE_WOLFMQTT AND NOT BUNDLED_WOLFMQTT_CONFIGURED)
        # 混合方案：wolfMQTT/wolfSSL 使用固定版本的 Git Submodule（推荐）
        if(USE_GIT_SUBMODULE)
            # 使用Git Submodule
            set(WOLFMQTT_ROOT "${CMAKE_SOURCE_DIR}/third_party/wolfmqtt" CACHE PATH
                "wolfMQTT source or installation prefix")
            
            # wolfMQTT的头文件在wolfmqtt/目录下，不是include/wolfmqtt/
            if(EXISTS "${WOLFMQTT_ROOT}/wolfmqtt/mqtt_client.h" OR
               EXISTS "${WOLFMQTT_ROOT}/install/include/wolfmqtt/mqtt_client.h")
                message(STATUS "找到wolfMQTT (Git Submodule): ${WOLFMQTT_ROOT}")
                if(EXISTS "${WOLFMQTT_ROOT}/install/include/wolfmqtt/mqtt_client.h")
                    set(WOLFMQTT_INCLUDE_DIR "${WOLFMQTT_ROOT}/install/include")
                else()
                    set(WOLFMQTT_INCLUDE_DIR "${WOLFMQTT_ROOT}")
                endif()
                
                find_library(WOLFMQTT_LIBRARY
                    NAMES wolfmqtt
                    PATHS
                        ${WOLFMQTT_ROOT}/install/lib
                        ${WOLFMQTT_ROOT}/.libs
                        ${WOLFMQTT_ROOT}/lib
                        /usr/local/lib
                        /usr/lib
                )
                
                if(WOLFMQTT_LIBRARY)
                    message(STATUS "找到wolfMQTT库: ${WOLFMQTT_LIBRARY}")
                    target_include_directories(mqtt_client PRIVATE ${WOLFMQTT_INCLUDE_DIR})
                    target_link_libraries(mqtt_client PRIVATE ${WOLFMQTT_LIBRARY})
                    target_compile_definitions(mqtt_client PRIVATE WOLFMQTT_ENABLED=1)
                    
                    # 检查wolfMQTT是否启用了TLS支持
                    if(EXISTS "${WOLFMQTT_ROOT}/install/include/wolfmqtt/options.h")
                        file(READ "${WOLFMQTT_ROOT}/install/include/wolfmqtt/options.h" WOLFMQTT_OPTIONS)
                        if(WOLFMQTT_OPTIONS MATCHES "#define ENABLE_MQTT_TLS")
                            message(STATUS "检测到wolfMQTT已启用TLS支持，查找wolfSSL库...")
                            
                            # 查找系统安装的 wolfSSL
                            find_library(WOLFSSL_LIBRARY
                                NAMES wolfssl
                                PATHS
                                    /opt/homebrew/opt/wolfssl/lib
                                    /usr/local/lib
                                    /usr/lib
                            )
                            
                            if(WOLFSSL_LIBRARY)
                                find_path(WOLFSSL_INCLUDE_DIR
                                    NAMES wolfssl/openssl/sha.h
                                    PATHS
                                        /opt/homebrew/opt/wolfssl/include
                                        /usr/local/include
                                        /usr/include
                                )
                                
                                if(WOLFSSL_INCLUDE_DIR)
                                    target_link_libraries(mqtt_client PRIVATE ${WOLFSSL_LIBRARY})
                                    target_include_directories(mqtt_client PRIVATE ${WOLFSSL_INCLUDE_DIR})
                                    target_compile_definitions(mqtt_client PRIVATE ENABLE_MQTT_TLS=1)
                                    message(STATUS "  ✓ wolfSSL库已链接: ${WOLFSSL_LIBRARY}")
                                    message(STATUS "  ✓ wolfSSL头文件目录: ${WOLFSSL_INCLUDE_DIR}")
                                    message(STATUS "  ✓ TLS支持已启用")
                                else()
                                    message(FATAL_ERROR "wolfMQTT已启用TLS，但未找到wolfSSL头文件")
                                endif()
                            else()
                                message(FATAL_ERROR "wolfMQTT已启用TLS，但未找到wolfSSL库")
                            endif()
                        endif()
                    endif()
                else()
                    message(WARNING "wolfMQTT库未找到，请先构建")
                endif()
            else()
                message(WARNING "wolfMQTT (Git Submodule) 未找到")
            endif()
        else()
            message(FATAL_ERROR
                "USE_FETCHCONTENT=ON with wolfMQTT requires USE_GIT_SUBMODULE=ON "
                "and BUILD_BUNDLED_WOLFMQTT=ON; the old source-only FetchContent "
                "fallback was removed because it could configure successfully but "
                "never produce a linkable wolfMQTT target.")
        endif()
    endif()
    
    # ========================================================================
    # 其他依赖（使用FetchContent - 混合方案）
    # ========================================================================
    # 这些依赖使用CMake或header-only，适合FetchContent自动管理
    
    # nlohmann/json - JSON库（header-only，适合FetchContent）
    # 配置管理需要JSON支持，默认启用
    option(ENABLE_JSON "Enable nlohmann/json support" ON)
    if(ENABLE_JSON)
        message(STATUS "配置nlohmann/json (FetchContent)...")
        
        # 使用Git方式获取完整仓库（包含CMake支持）
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG        v3.11.3
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE  # 显示下载进度
        )
        
        # 设置策略以兼容旧版本
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)  # 允许使用FetchContent_Populate（header-only库需要）
        endif()
        
        # 对于header-only库，使用Populate方式创建接口目标
        message(STATUS "正在下载 nlohmann/json (这可能需要一些时间，请耐心等待)...")
        FetchContent_GetProperties(nlohmann_json)
        if(NOT nlohmann_json_POPULATED)
            FetchContent_Populate(nlohmann_json)
            message(STATUS "nlohmann/json 下载完成: ${nlohmann_json_SOURCE_DIR}")
            
        endif()

        # nlohmann/json is header-only. Linking a build-local interface target
        # leaks that target into static-library exports and makes installation
        # fail because there is no corresponding exported dependency target.
        target_include_directories(mqtt_client PRIVATE
            ${nlohmann_json_SOURCE_DIR}/include)
        # 定义预处理器宏，使代码可以使用 #ifdef ENABLE_JSON
        target_compile_definitions(mqtt_client PRIVATE ENABLE_JSON=1)
        message(STATUS "nlohmann/json已配置")
    endif()
    
    # Google Test - 测试框架（如果需要）
    option(ENABLE_TESTING "Enable Google Test" OFF)
    if(ENABLE_TESTING)
        message(STATUS "配置Google Test (FetchContent)...")

        # GoogleTest 是构建期依赖，不应进入本库的安装包。
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.14.0
        )
        
        FetchContent_MakeAvailable(googletest)
        
        message(STATUS "Google Test已配置")
    endif()
    
    # fmt - 格式化库（必需依赖，使用FetchContent）
    message(STATUS "配置fmt (FetchContent)...")
    
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG        12.1.0
        GIT_PROGRESS   TRUE  # 显示下载进度
    )

    # fmt 12 在作为子项目时默认关闭安装；静态导出仍会引用 fmt，
    # 因此必须让它随本工程生成可消费的安装包配置。
    set(FMT_INSTALL ON CACHE BOOL "" FORCE)
    
    message(STATUS "正在下载 fmt (这可能需要一些时间，请耐心等待)...")
    FetchContent_MakeAvailable(fmt)
    
    target_link_libraries(mqtt_client PRIVATE fmt::fmt)
    message(STATUS "fmt已配置")
    
    # yaml-cpp - YAML解析库（可选）
    option(ENABLE_YAML "Enable yaml-cpp support" ON)
    if(ENABLE_YAML)
        message(STATUS "配置yaml-cpp (FetchContent)...")
        
        FetchContent_Declare(
            yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG        0.8.0
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE  # 显示下载进度
        )
        
        # 使用 Populate + add_subdirectory 方式，可以在配置前设置策略
        message(STATUS "正在下载 yaml-cpp (这可能需要一些时间，请耐心等待)...")
        FetchContent_GetProperties(yaml-cpp)
        if(NOT yaml-cpp_POPULATED)
            FetchContent_Populate(yaml-cpp)

            set(YAML_CPP_INSTALL ON CACHE BOOL "" FORCE)
            set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
            
            add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR})
        endif()
        
        target_link_libraries(mqtt_client PRIVATE yaml-cpp)
        target_compile_definitions(mqtt_client PRIVATE ENABLE_YAML=1)
        message(STATUS "yaml-cpp已配置")
    endif()
    
    message(STATUS "==========================================")
endif()

# ============================================================================
# Git Submodule配置（核心依赖 - 混合方案的一部分）
# ============================================================================
# 当USE_FETCHCONTENT=OFF时，使用传统Git Submodule方式
if(NOT USE_FETCHCONTENT)
    find_package(fmt CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(mqtt_client PRIVATE fmt::fmt nlohmann_json::nlohmann_json)
    target_compile_definitions(mqtt_client PRIVATE ENABLE_JSON=1)

    if(ENABLE_YAML)
        find_package(yaml-cpp CONFIG REQUIRED)
        target_link_libraries(mqtt_client PRIVATE yaml-cpp)
        target_compile_definitions(mqtt_client PRIVATE ENABLE_YAML=1)
    endif()
    
    if(ENABLE_WOLFMQTT AND NOT BUNDLED_WOLFMQTT_CONFIGURED)
    message(STATUS "========== wolfMQTT 配置 ==========")
    
            # wolfMQTT路径（Git Submodule）
            set(WOLFMQTT_ROOT "${CMAKE_SOURCE_DIR}/third_party/wolfmqtt" CACHE PATH
                "wolfMQTT source or installation prefix")

            # wolfMQTT的头文件在wolfmqtt/目录下，不是include/wolfmqtt/
            if(EXISTS "${WOLFMQTT_ROOT}/wolfmqtt/mqtt_client.h" OR
               EXISTS "${WOLFMQTT_ROOT}/install/include/wolfmqtt/mqtt_client.h")
                message(STATUS "找到wolfMQTT: ${WOLFMQTT_ROOT}")

                # 设置wolfMQTT包含目录（头文件在wolfmqtt/目录下）
                if(EXISTS "${WOLFMQTT_ROOT}/install/include/wolfmqtt/mqtt_client.h")
                    set(WOLFMQTT_INCLUDE_DIR "${WOLFMQTT_ROOT}/install/include")
                else()
                    set(WOLFMQTT_INCLUDE_DIR "${WOLFMQTT_ROOT}")
                endif()
        
        # 查找或构建wolfMQTT库
        # 首先尝试查找预编译的库
        find_library(WOLFMQTT_LIBRARY
            NAMES wolfmqtt
            PATHS
                ${WOLFMQTT_ROOT}/install/lib
                ${WOLFMQTT_ROOT}/.libs
                ${WOLFMQTT_ROOT}/lib
                /usr/local/lib
                /usr/lib
        )
        
        if(WOLFMQTT_LIBRARY)
            message(STATUS "找到wolfMQTT库: ${WOLFMQTT_LIBRARY}")
        else()
            # 如果未找到预编译库，提示用户需要先构建
            message(WARNING "未找到预编译的wolfMQTT库")
            message(STATUS "请先构建wolfMQTT:")
            message(STATUS "  cd ${WOLFMQTT_ROOT}")
            message(STATUS "  ./autogen.sh")
            message(STATUS "  ./configure --enable-mqtt5 --prefix=\$(pwd)/install")
            message(STATUS "  make -j\$(nproc)")
            message(STATUS "  make install")
            message(STATUS "")
            message(STATUS "或者使用系统安装的wolfMQTT库")
            
            # 尝试使用系统库
            find_library(WOLFMQTT_LIBRARY
                NAMES wolfmqtt
                PATHS
                    /usr/local/lib
                    /usr/lib
            )
        endif()
        
        if(WOLFMQTT_LIBRARY)
            target_include_directories(mqtt_client PRIVATE ${WOLFMQTT_INCLUDE_DIR})
            target_link_libraries(mqtt_client PRIVATE ${WOLFMQTT_LIBRARY})
            target_compile_definitions(mqtt_client PRIVATE WOLFMQTT_ENABLED=1)
            
            # 检查wolfMQTT是否启用了TLS支持
            # 如果启用了TLS，需要链接wolfSSL库
            if(EXISTS "${WOLFMQTT_ROOT}/install/include/wolfmqtt/options.h")
                file(READ "${WOLFMQTT_ROOT}/install/include/wolfmqtt/options.h" WOLFMQTT_OPTIONS)
                if(WOLFMQTT_OPTIONS MATCHES "#define ENABLE_MQTT_TLS")
                    message(STATUS "检测到wolfMQTT已启用TLS支持，查找wolfSSL库...")
                    
                    # 查找wolfSSL库（优先使用Homebrew安装的）
                    find_library(WOLFSSL_LIBRARY
                        NAMES wolfssl
                        PATHS
                            /opt/homebrew/opt/wolfssl/lib
                            /usr/local/lib
                            /usr/lib
                    )
                    
                    # 查找系统安装的 wolfSSL
                    find_library(WOLFSSL_LIBRARY_FOUND
                        NAMES wolfssl
                        PATHS
                            /opt/homebrew/opt/wolfssl/lib
                            /usr/local/lib
                            /usr/lib
                    )
                    
                    if(WOLFSSL_LIBRARY_FOUND)
                        find_path(WOLFSSL_INCLUDE_DIR_FOUND
                            NAMES wolfssl/openssl/sha.h
                            PATHS
                                /opt/homebrew/opt/wolfssl/include
                                /usr/local/include
                                /usr/include
                        )
                        
                        if(WOLFSSL_INCLUDE_DIR_FOUND)
                            target_link_libraries(mqtt_client PRIVATE ${WOLFSSL_LIBRARY_FOUND})
                            target_include_directories(mqtt_client PRIVATE ${WOLFSSL_INCLUDE_DIR_FOUND})
                            target_compile_definitions(mqtt_client PRIVATE ENABLE_MQTT_TLS=1)
                            message(STATUS "  ✓ wolfSSL库已链接: ${WOLFSSL_LIBRARY_FOUND}")
                            message(STATUS "  ✓ wolfSSL头文件目录: ${WOLFSSL_INCLUDE_DIR_FOUND}")
                            message(STATUS "  ✓ TLS支持已启用")
                        else()
                            message(FATAL_ERROR "wolfMQTT已启用TLS，但未找到wolfSSL头文件")
                        endif()
                    else()
                        message(FATAL_ERROR "wolfMQTT已启用TLS，但未找到wolfSSL库")
                    endif()
                endif()
            endif()
            
            message(STATUS "wolfMQTT已配置")
        else()
            message(WARNING "wolfMQTT库未找到，某些功能将不可用")
        endif()
    else()
        message(WARNING "wolfMQTT源码未找到")
        message(STATUS "请执行以下命令初始化 wolfMQTT 与 wolfSSL:")
        message(STATUS "  ./scripts/unix/init.sh")
        message(STATUS "  或")
        message(STATUS "  git submodule update --init --recursive")
    endif()
    
        message(STATUS "================================")
    else()
        if(ENABLE_WOLFMQTT AND BUNDLED_WOLFMQTT_CONFIGURED)
            message(STATUS "wolfMQTT已由bundled源码配置")
        else()
            message(STATUS "wolfMQTT支持已禁用")
        endif()
    endif()
endif()

# ============================================================================
# 依赖管理总结
# ============================================================================
message(STATUS "")
message(STATUS "========== 依赖管理总结（混合方案）==========")
message(STATUS "核心依赖（固定版本）: Git Submodule")
if(ENABLE_WOLFMQTT)
    if(BUNDLED_WOLFMQTT_CONFIGURED)
        message(STATUS "  ✓ wolfMQTT + wolfSSL: pinned bundled sources")
    else()
        message(STATUS "  ✓ wolfMQTT: external/prebuilt library")
    endif()
endif()
message(STATUS "")
message(STATUS "其他依赖（CMake/header-only）: FetchContent")
if(ENABLE_JSON)
    message(STATUS "  ✓ nlohmann/json: FetchContent")
endif()
if(ENABLE_YAML)
    message(STATUS "  ✓ yaml-cpp: FetchContent")
endif()
if(ENABLE_TESTING)
    message(STATUS "  ✓ Google Test: FetchContent")
endif()
message(STATUS "  ✓ fmt: FetchContent")
message(STATUS "==========================================")
message(STATUS "")

# 包含目录
target_include_directories(mqtt_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# ============================================================================
# 安装配置
# ============================================================================
install(TARGETS mqtt_client
    EXPORT mqtt_clientTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/mqtt_client
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT mqtt_clientTargets
    FILE mqtt_clientTargets.cmake
    NAMESPACE mqtt_client::
    DESTINATION lib/cmake/mqtt_client
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/mqtt_clientConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/mqtt_clientConfig.cmake"
    INSTALL_DESTINATION lib/cmake/mqtt_client
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mqtt_clientConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/mqtt_clientConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/mqtt_clientConfigVersion.cmake"
    DESTINATION lib/cmake/mqtt_client
)

# ============================================================================
# 复制头文件到构建目录
# ============================================================================
# 创建include目录
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/mqtt_client)

# 复制头文件到构建目录
# 注意：library.h 和 platform.h 已移除，不再需要复制
# add_custom_command(TARGET mqtt_client POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#     COMMENT "复制头文件到构建目录"
# )

# ============================================================================
# 生成API文档
# ============================================================================
option(BUILD_API_DOC "生成API文档" ON)

if(BUILD_API_DOC)
    # 查找Python（用于运行文档生成脚本）
    find_program(PYTHON_EXECUTABLE python3 python)
    
    if(PYTHON_EXECUTABLE)
        # 创建docs输出目录
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/docs)
        
        # 添加自定义目标生成API文档
        add_custom_target(api_doc
            COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_api_doc.py
                --source-dir ${CMAKE_SOURCE_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/docs
                --version ${PROJECT_VERSION}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "生成API文档"
        )
        
        # 在构建后自动生成文档
        add_custom_command(TARGET mqtt_client POST_BUILD
            COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_api_doc.py
                --source-dir ${CMAKE_SOURCE_DIR}
                --output-dir ${CMAKE_BINARY_DIR}/docs
                --version ${PROJECT_VERSION}
            COMMENT "生成API文档"
        )
        
        message(STATUS "API文档生成已启用")
    else()
        message(WARNING "未找到Python，无法生成API文档。请安装Python 3或设置PYTHON_EXECUTABLE")
    endif()
endif()

# ============================================================================
# 测试配置
# ============================================================================
if(ENABLE_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# 打印配置摘要
# ============================================================================
message(STATUS "")
message(STATUS "========== 配置摘要 ==========")
message(STATUS "项目名称: ${PROJECT_NAME}")
message(STATUS "项目版本: ${PROJECT_VERSION}")
message(STATUS "C++标准: C++${CMAKE_CXX_STANDARD}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "编译器: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "平台: ${CMAKE_SYSTEM_NAME}")
message(STATUS "处理器: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "架构: ${ARCH_NAME}")
if(ARCH_64BIT)
    message(STATUS "位宽: 64位")
else()
    message(STATUS "位宽: 32位")
endif()
message(STATUS "输出文件名后缀: ${FILE_NAME_SUFFIX}")
message(STATUS "=============================")
message(STATUS "")
