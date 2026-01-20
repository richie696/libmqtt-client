cmake_minimum_required(VERSION 3.15)
project(mqtt_client VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# 检测具体架构
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
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
    # 添加Linux特定的编译选项
    add_compile_options(-Wall -Wextra -Wpedantic)
    
    # 链接pthread（Linux上通常需要）
    find_package(Threads REQUIRED)
    
    # 设置库文件扩展名
    set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
endif()

# Windows配置
if(PLATFORM_WINDOWS)
    # Windows特定的编译选项
    add_compile_options(/W4)
    
    # 设置库文件扩展名
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
    
    # Windows下需要导出符号
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    
    # 如果是MSVC编译器
    if(MSVC)
        # 禁用一些MSVC警告
        add_compile_options(/wd4251 /wd4275)
        # 设置运行时库
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

# macOS配置
if(PLATFORM_MACOS)
    # macOS特定的编译选项
    add_compile_options(-Wall -Wextra -Wpedantic)
    
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

# ============================================================================
# 架构特定配置
# ============================================================================

# ARM架构配置（仅Linux，macOS不需要这些选项）
if(PLATFORM_LINUX AND (ARCH_ARM32 OR ARCH_ARM64))
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
    -DARCH_${ARCH_NAME}
)

# 平台特定的定义
if(PLATFORM_LINUX)
    add_definitions(-DPLATFORM_LINUX)
elseif(PLATFORM_WINDOWS)
    add_definitions(-DPLATFORM_WINDOWS -DWIN32 -D_WIN32)
    if(MSVC)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    endif()
elseif(PLATFORM_MACOS)
    add_definitions(-DPLATFORM_MACOS -D__APPLE__ -D__MACH__)
endif()

# 架构特定的定义
if(ARCH_X86_64)
    add_definitions(-DARCH_X86_64 -D__x86_64__)
elseif(ARCH_X86)
    add_definitions(-DARCH_X86 -D__i386__)
elseif(ARCH_ARM64)
    add_definitions(-DARCH_ARM64 -D__aarch64__)
elseif(ARCH_ARM32)
    add_definitions(-DARCH_ARM32 -D__arm__)
elseif(ARCH_MIPS32)
    add_definitions(-DARCH_MIPS32 -D__mips__)
endif()

# 位宽定义
if(ARCH_64BIT)
    add_definitions(-DARCH_64BIT -D__LP64__)
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
    src/embedded_mqtt_client.cpp
)

# ============================================================================
# 创建库
# ============================================================================
add_library(mqtt_client SHARED ${SOURCES} ${HEADERS})

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
    PUBLIC_HEADER "${HEADERS}"
    OUTPUT_NAME "mqtt_client-${FILE_NAME_SUFFIX}"
    ARCHIVE_OUTPUT_NAME "mqtt_client-${FILE_NAME_SUFFIX}"
    LIBRARY_OUTPUT_NAME "mqtt_client-${FILE_NAME_SUFFIX}"
    RUNTIME_OUTPUT_NAME "mqtt_client-${FILE_NAME_SUFFIX}"
)

# ============================================================================
# 依赖管理配置（混合方案）
# ============================================================================
# 混合方案：
# - 核心依赖（autotools）：使用Git Submodule（wolfMQTT, wolfSSL）
# - 其他依赖（CMake/header-only）：使用FetchContent（JSON, 测试框架等）

# 包含FetchContent模块（CMake 3.11+）
include(FetchContent)

# 依赖管理选项
option(USE_FETCHCONTENT "Use FetchContent for dependency management" ON)
option(USE_GIT_SUBMODULE "Use Git Submodule for core dependencies (wolfMQTT)" ON)

# 链接系统库
if(PLATFORM_LINUX)
    target_link_libraries(mqtt_client PRIVATE Threads::Threads)
endif()

# ============================================================================
# 使用FetchContent管理依赖
# ============================================================================
if(USE_FETCHCONTENT)
    message(STATUS "========== 使用FetchContent管理依赖 ==========")
    
    # ========================================================================
    # wolfMQTT 依赖（核心依赖，使用Git Submodule）
    # ========================================================================
    option(ENABLE_WOLFMQTT "Enable wolfMQTT support" ON)
    
    if(ENABLE_WOLFMQTT)
        # 混合方案：wolfMQTT使用Git Submodule（推荐）
        # 原因：wolfMQTT使用autotools，FetchContent无法自动构建
        if(USE_GIT_SUBMODULE)
            # 使用Git Submodule
            set(WOLFMQTT_ROOT "${CMAKE_SOURCE_DIR}/third_party/wolfmqtt")
            
            # wolfMQTT的头文件在wolfmqtt/目录下，不是include/wolfmqtt/
            if(EXISTS "${WOLFMQTT_ROOT}/wolfmqtt/mqtt_client.h")
                message(STATUS "找到wolfMQTT (Git Submodule): ${WOLFMQTT_ROOT}")
                set(WOLFMQTT_INCLUDE_DIR "${WOLFMQTT_ROOT}")
                
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
                                    message(WARNING "  ⚠ 找到 wolfSSL 库但未找到头文件，TLS 功能将不可用")
                                endif()
                            else()
                                message(WARNING "  ⚠ 未找到 wolfSSL 库，TLS 功能将不可用")
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
            # 备选方案：使用FetchContent下载（不推荐，仅用于测试）
            message(WARNING "不推荐使用FetchContent管理wolfMQTT")
            message(STATUS "原因: wolfMQTT使用autotools构建系统，无法自动构建")
            message(STATUS "推荐: 使用Git Submodule (USE_GIT_SUBMODULE=ON)")
            message(STATUS "")
            message(STATUS "详细说明: docs/WOLFMQTT_FETCHCONTENT_LIMITATIONS.md")
            
            FetchContent_Declare(
                wolfmqtt
                GIT_REPOSITORY https://github.com/wolfSSL/wolfMQTT.git
                GIT_TAG        master
            )
            
            FetchContent_GetProperties(wolfmqtt)
            if(NOT wolfmqtt_POPULATED)
                message(STATUS "下载wolfMQTT源码...")
                FetchContent_Populate(wolfmqtt)
                set(WOLFMQTT_ROOT ${wolfmqtt_SOURCE_DIR})
                set(WOLFMQTT_INCLUDE_DIR "${WOLFMQTT_ROOT}/include")
                message(WARNING "需要手动构建wolfMQTT（使用autotools）")
            endif()
            # 注意：wolfMQTT使用autotools，无法使用FetchContent_MakeAvailable
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
        )
        
        # 设置策略以兼容旧版本
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)
        endif()
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)  # 允许使用FetchContent_Populate（header-only库需要）
        endif()
        
        # 对于header-only库，使用Populate方式创建接口目标
        FetchContent_GetProperties(nlohmann_json)
        if(NOT nlohmann_json_POPULATED)
            FetchContent_Populate(nlohmann_json)
            # 创建接口库（header-only）
            add_library(nlohmann_json INTERFACE)
            target_include_directories(nlohmann_json INTERFACE 
                ${nlohmann_json_SOURCE_DIR}/include
            )
            add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
        endif()
        
        target_link_libraries(mqtt_client PRIVATE nlohmann_json::nlohmann_json)
        # 定义预处理器宏，使代码可以使用 #ifdef ENABLE_JSON
        target_compile_definitions(mqtt_client PRIVATE ENABLE_JSON=1)
        message(STATUS "nlohmann/json已配置")
    endif()
    
    # Google Test - 测试框架（如果需要）
    option(ENABLE_TESTING "Enable Google Test" OFF)
    if(ENABLE_TESTING)
        message(STATUS "配置Google Test (FetchContent)...")
        
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
        GIT_TAG        10.1.1
    )
    
    FetchContent_MakeAvailable(fmt)
    
    # 使用 PUBLIC 链接，确保依赖 mqtt_client 的目标也能访问 fmt
    target_link_libraries(mqtt_client PUBLIC fmt::fmt)
    message(STATUS "fmt已配置")
    
    # yaml-cpp - YAML解析库（可选）
    option(ENABLE_YAML "Enable yaml-cpp support" ON)
    if(ENABLE_YAML)
        message(STATUS "配置yaml-cpp (FetchContent)...")
        
        FetchContent_Declare(
            yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG        master  # 使用 master 分支，已修复 CMake 兼容性问题
            GIT_SHALLOW    TRUE
        )
        
        # 使用 Populate + add_subdirectory 方式，可以在配置前设置策略
        FetchContent_GetProperties(yaml-cpp)
        if(NOT yaml-cpp_POPULATED)
            FetchContent_Populate(yaml-cpp)
            
            # 在 add_subdirectory 之前，设置策略以兼容旧版本的 cmake_minimum_required
            # 这允许 yaml-cpp 使用较旧的 CMake 版本要求
            if(POLICY CMP0129)
                cmake_policy(SET CMP0129 OLD)
            endif()
            
            add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR} EXCLUDE_FROM_ALL)
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
    option(ENABLE_WOLFMQTT "Enable wolfMQTT support" ON)
    
    if(ENABLE_WOLFMQTT)
    message(STATUS "========== wolfMQTT 配置 ==========")
    
            # wolfMQTT路径（Git Submodule）
            set(WOLFMQTT_ROOT "${CMAKE_SOURCE_DIR}/third_party/wolfmqtt")

            # wolfMQTT的头文件在wolfmqtt/目录下，不是include/wolfmqtt/
            if(EXISTS "${WOLFMQTT_ROOT}/wolfmqtt/mqtt_client.h")
                message(STATUS "找到wolfMQTT: ${WOLFMQTT_ROOT}")

                # 设置wolfMQTT包含目录（头文件在wolfmqtt/目录下）
                set(WOLFMQTT_INCLUDE_DIR "${WOLFMQTT_ROOT}")
        
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
                            message(WARNING "  ⚠ 找到 wolfSSL 库但未找到头文件，TLS 功能将不可用")
                        endif()
                    else()
                        message(WARNING "  ⚠ 未找到 wolfSSL 库，TLS 功能将不可用")
                    endif()
                endif()
            endif()
            
            message(STATUS "wolfMQTT已配置")
        else()
            message(WARNING "wolfMQTT库未找到，某些功能将不可用")
        endif()
    else()
        message(WARNING "wolfMQTT源码未找到")
        message(STATUS "请执行以下命令添加wolfMQTT:")
        message(STATUS "  ./scripts/unix/init.sh")
        message(STATUS "  或")
        message(STATUS "  git submodule add https://github.com/wolfSSL/wolfMQTT.git third_party/wolfmqtt")
        message(STATUS "  git submodule update --init --recursive")
    endif()
    
        message(STATUS "================================")
    else()
        message(STATUS "wolfMQTT支持已禁用")
    endif()
endif()

# ============================================================================
# 依赖管理总结
# ============================================================================
message(STATUS "")
message(STATUS "========== 依赖管理总结（混合方案）==========")
message(STATUS "核心依赖（autotools）: Git Submodule")
if(ENABLE_WOLFMQTT)
    message(STATUS "  ✓ wolfMQTT: Git Submodule")
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
)


# ============================================================================
# 安装配置
# ============================================================================
install(TARGETS mqtt_client
    EXPORT mqtt_clientTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/mqtt_client
)

# 安装头文件
install(FILES 
    # include/mqtt_client/platform.h  # 已移除
    DESTINATION include/mqtt_client
)

install(EXPORT mqtt_clientTargets
    FILE mqtt_clientTargets.cmake
    NAMESPACE mqtt_client::
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
