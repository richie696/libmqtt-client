# MQTT 客户端库

一个轻量级、高性能的跨平台 MQTT 客户端库，采用 C++17 标准开发，提供简洁易用的 API 接口。支持 MQTT 3.1.1 和 5.0 协议，适用于嵌入式设备、IoT 应用和工业自动化场景。

## ✨ 特性

- 🚀 **跨平台支持**: Linux、Windows、macOS
- 🎯 **多架构支持**: x86、x86_64、ARM32、ARM64、MIPS32
- 📦 **轻量级设计**: 资源占用低，适合嵌入式环境
- 🔄 **智能重连**: 指数退避算法，自动恢复连接
- 📡 **MQTT 5.0**: 完整支持 MQTT 5.0 协议标准
- 🛠️ **易于集成**: 提供 CMake 构建系统，支持交叉编译
- 📚 **完整文档**: 详细的架构设计、API 文档和使用指南
- 🔧 **自动化构建**: 提供多平台构建脚本，一键编译
- 🧪 **完整测试**: 单元测试和集成测试，支持代码覆盖率
- ⚡ **性能优化**: 使用 C++17 现代特性，极致性能优化

## 📋 目录

- [快速开始](#快速开始)
- [依赖要求](#依赖要求)
- [构建方法](#构建方法)
- [使用示例](#使用示例)
- [测试](#测试)
- [项目结构](#项目结构)
- [更多文档](#更多文档)

## 🚀 快速开始

### 1. 克隆项目

```bash
git clone <repository-url>
cd libmqtt-client
```

### 2. 初始化项目（首次使用）

**初始化脚本会自动：**
- 检查基本依赖（Git、CMake、编译器）
- 初始化 Git Submodules（下载 wolfMQTT）
- 构建 wolfMQTT 依赖库

```bash
# Linux/macOS
./scripts/unix/init.sh

# Windows (PowerShell)
.\scripts\windows\init.ps1
```

### 3. 构建项目

```bash
# Linux/macOS - 默认 Release 构建
./scripts/unix/build.sh

# Windows (PowerShell)
.\scripts\windows\build.ps1
```

构建产物位于 `build/` 目录：
- 库文件：`build/lib/libmqtt_client-<arch>-<os>-1.0.0.{so|dylib|dll}`
- 头文件：`build/include/mqtt_client/`

## 📦 依赖要求

### 必需依赖

- **Git** - 用于管理 Git Submodules
- **CMake** >= 3.15
- **C++编译器** 支持 C++17 (GCC 7+, Clang 5+, MSVC 2017+)
- **构建工具** (make 或 ninja)

### 自动管理的依赖（通过 FetchContent）

以下依赖会在构建时自动下载和编译，无需手动安装：

- **fmtlib** - 字符串格式化库（必需）
- **nlohmann/json** - JSON 解析库（可选，用于配置文件）
- **yaml-cpp** - YAML 解析库（可选，用于配置文件）
- **Google Test** - 测试框架（仅测试时）

### Git Submodule 依赖

- **wolfMQTT** - MQTT 协议实现库（通过 git submodule 管理）

### 可选系统依赖

- **wolfSSL** - 用于 TLS/SSL 支持（可选，但推荐）
  - macOS: `brew install wolfssl`
  - Linux: `sudo apt-get install libwolfssl-dev`
  - Windows: 从 [wolfSSL 官网](https://www.wolfssl.com/download/) 下载

### 快速安装依赖

**macOS:**
```bash
xcode-select --install  # 安装 Xcode Command Line Tools
brew install cmake wolfssl
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libwolfssl-dev
```

**Windows:**
- 安装 [Visual Studio](https://visualstudio.microsoft.com/) 或 [MinGW-w64](https://www.mingw-w64.org/)
- 安装 [CMake](https://cmake.org/download/)

初始化脚本会自动检查依赖，缺少时会提示安装方法。

## 🔨 构建方法

### 基本构建

```bash
# Release 构建（默认）
./scripts/unix/build.sh

# Debug 构建
./scripts/unix/build.sh -t Debug

# 清理后构建
./scripts/unix/build.sh -c

# 指定并行任务数
./scripts/unix/build.sh -j 8

# 不构建示例程序
./scripts/unix/build.sh --no-demo
```

### 交互式菜单

```bash
./scripts/unix/build.sh --menu
```

菜单选项：
1. 构建所有依赖和项目
2. 仅构建 wolfMQTT
3. 仅构建项目
4. 运行所有测试 (含覆盖率报告)
5. 清理所有构建文件
6. 退出

### macOS Universal Binary

```bash
# 构建 Universal Binary（同时支持 Intel 和 M 系列芯片）
./scripts/unix/build.sh --macos-arch universal

# 仅构建 arm64（M 系列芯片）
./scripts/unix/build.sh --macos-arch arm64

# 仅构建 x86_64（Intel 芯片）
./scripts/unix/build.sh --macos-arch x86_64
```

### 查看帮助

```bash
./scripts/unix/build.sh --help
```

### 手动构建（不使用脚本）

```bash
# 1. 初始化 submodules（如果未运行 init.sh）
git submodule update --init --recursive

# 2. 创建构建目录
mkdir -p build && cd build

# 3. 配置 CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# 4. 编译
cmake --build . -j$(nproc)

# 5. 安装（可选）
cmake --install .
```

## 💻 使用示例

### 基本使用

```cpp
#include "mqtt_client/embedded_mqtt_client.h"
#include <iostream>

int main() {
    using namespace mqtt_client;
    
    // 创建配置
    MqttConfig config;
    config.basic.host = "mqtt.example.com";
    config.basic.port = 1883;
    config.basic.clientId = "my_client";
    config.basic.version = MqttProtocolVersion::V5_0;
    
    // 创建客户端（立即初始化）
    EmbeddedMqttClient client(config);
    
    // 连接
    auto result = client.connect();
    if (!result) {
        std::cerr << "连接失败: " << result.error().message << std::endl;
        return 1;
    }
    
    // 订阅主题
    client.subscribe("device/+/status", 
        [](std::string_view topic, std::string_view payload, const MqttProperties& props) {
            std::cout << "收到消息: " << topic << " -> " << payload << std::endl;
        },
        QoS::QOS_1
    );
    
    // 发布消息
    client.publish("device/001/status", "online", QoS::QOS_1);
    
    // 保持运行
    std::this_thread::sleep_for(std::chrono::seconds(60));
    
    // 断开连接
    client.disconnect();
    
    return 0;
}
```

### 延迟初始化模式

```cpp
#include "mqtt_client/embedded_mqtt_client.h"

int main() {
    using namespace mqtt_client;
    
    // 创建客户端（延迟初始化）
    EmbeddedMqttClient client;
    
    // 从配置文件或服务器获取配置
    MqttConfig config = loadConfigFromFile("config.json");
    
    // 初始化
    if (auto result = client.initialize(config); !result) {
        std::cerr << "初始化失败: " << result.error().message << std::endl;
        return 1;
    }
    
    // 连接和使用...
    client.connect();
    
    return 0;
}
```

### 使用配置文件

```cpp
#include "mqtt_client/embedded_mqtt_client.h"
#include "mqtt_client/config/config_manager.h"

int main() {
    using namespace mqtt_client;
    
    // 从 JSON 或 YAML 文件加载配置
    auto configResult = MqttConfigManager::loadFromJson("config.json");
    // 或
    // auto configResult = MqttConfigManager::loadFromYaml("config.yaml");
    
    if (!configResult) {
        std::cerr << "加载配置失败: " << configResult.error().message << std::endl;
        return 1;
    }
    
    // 创建客户端
    EmbeddedMqttClient client(configResult.value());
    
    // 连接和使用...
    client.connect();
    
    return 0;
}
```

### 设置自定义日志器

```cpp
#include "mqtt_client/embedded_mqtt_client.h"
#include "mqtt_client/logger/logger_interface.h"

class MyLogger : public mqtt_client::ILogger {
public:
    void log(mqtt_client::LogLevel level,
             std::string_view file, int line,
             std::string_view function,
             std::string_view message) override {
        // 实现自定义日志逻辑
        std::cout << "[" << static_cast<int>(level) << "] "
                  << file << ":" << line << " "
                  << function << " - " << message << std::endl;
    }
    
    void flush() override {}
    void setLevel(mqtt_client::LogLevel level) override {}
    mqtt_client::LogLevel getLevel() const override { return mqtt_client::LogLevel::INFO; }
    void close() override {}
};

int main() {
    // 设置自定义日志器
    auto logger = std::make_shared<MyLogger>();
    mqtt_client::LoggerManager::getInstance().setLogger(logger);
    
    // 使用客户端...
    mqtt_client::EmbeddedMqttClient client(config);
    
    return 0;
}
```

更多使用示例请参考 [docs/API_DESIGN.md](docs/API_DESIGN.md)。

## 🧪 测试

### 运行测试

```bash
# 运行所有测试（自动启用覆盖率并生成报告）
./scripts/unix/build.sh --test all

# 仅运行单元测试
./scripts/unix/build.sh --test unit

# 仅运行集成测试
./scripts/unix/build.sh --test integration
```

### 手动运行测试

```bash
# 配置构建（启用测试）
cmake -S . -B build/test -DENABLE_TESTING=ON

# 编译
cmake --build build/test

# 运行所有测试
cd build/test
ctest

# 运行特定测试
./tests/test_core/test_core
./tests/test_config/test_config_manager
```

### 代码覆盖率

运行测试时会自动生成代码覆盖率报告：

```bash
# 运行测试（自动生成覆盖率报告）
./scripts/unix/build.sh --test all

# 查看覆盖率报告
open build/coverage_html/index.html  # macOS
xdg-open build/coverage_html/index.html  # Linux
```

更多测试信息请参考 [tests/README.md](tests/README.md)。

## 📁 项目结构

```
libmqtt-client/
├── include/                      # 公共头文件
│   └── mqtt_client/              # 库头文件（用户需要包含的头文件）
│       ├── embedded_mqtt_client.h    # 主客户端类
│       ├── config/                   # 配置管理
│       ├── connection/               # 连接管理
│       ├── message/                  # 消息管理
│       ├── subscription/             # 订阅管理
│       ├── monitor/                  # 监控模块
│       ├── reconnect/                # 重连管理
│       ├── persistence/              # 持久化
│       ├── logger/                   # 日志接口
│       └── core/                     # 核心类型和错误处理
├── src/                          # 源文件
│   ├── embedded_mqtt_client.cpp
│   ├── adapter/                  # wolfMQTT 适配层
│   ├── config/
│   ├── connection/
│   ├── message/
│   ├── subscription/
│   ├── monitor/
│   ├── reconnect/
│   ├── persistence/
│   └── logger/
├── scripts/                      # 构建脚本
│   ├── unix/                     # Linux/macOS 脚本
│   │   ├── init.sh               # 初始化脚本
│   │   └── build.sh              # 构建脚本
│   └── windows/                  # Windows 脚本
│       ├── init.ps1
│       └── build.ps1
├── third_party/                  # 第三方依赖（Git Submodule）
│   └── wolfmqtt/                 # wolfMQTT 库
├── toolchains/                   # 交叉编译工具链文件
│   ├── arm-linux-gnueabihf.cmake
│   ├── aarch64-linux-gnu.cmake
│   └── ...
├── tests/                        # 测试代码
│   ├── test_core/                # 核心模块测试
│   ├── test_config/              # 配置管理测试
│   ├── test_connection/          # 连接管理测试
│   ├── test_message/             # 消息管理测试
│   ├── test_subscription/        # 订阅管理测试
│   ├── test_monitor/             # 监控模块测试
│   ├── test_reconnect/           # 重连管理测试
│   ├── test_client/              # 主客户端集成测试
│   └── integration/              # 集成测试
├── docs/                         # 文档
│   ├── README.md                 # 文档索引
│   ├── ARCHITECTURE.md           # 架构设计
│   ├── API_DESIGN.md             # API 接口设计
│   ├── BUILD.md                  # 构建说明
│   └── ...
├── CMakeLists.txt                # CMake 配置
└── build/                        # 构建输出目录（不提交）
```

## 🔧 交叉编译

使用工具链文件进行交叉编译：

```bash
# Linux ARM32
cmake -S . -B build/arm32 \
    -DCMAKE_TOOLCHAIN_FILE=../toolchains/arm-linux-gnueabihf.cmake

# Linux ARM64
cmake -S . -B build/arm64 \
    -DCMAKE_TOOLCHAIN_FILE=../toolchains/aarch64-linux-gnu.cmake

# Windows (从 Linux/macOS)
cmake -S . -B build/windows \
    -DCMAKE_TOOLCHAIN_FILE=../toolchains/x86_64-w64-mingw32.cmake

# 编译
cmake --build build/arm32
```

## 📚 更多文档

- **[docs/README.md](docs/README.md)** - 完整的文档目录和阅读指南
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** - 架构设计文档（包含构建、依赖、配置等完整说明）
- **[docs/API_DESIGN.md](docs/API_DESIGN.md)** - 完整的 API 接口清单和使用示例
- **[docs/BUILD.md](docs/BUILD.md)** - 详细的构建说明
- **[tests/README.md](tests/README.md)** - 测试说明和指南

## 🎯 支持的平台和架构

### 操作系统
- ✅ Linux
- ✅ Windows
- ✅ macOS

### 芯片架构
- ✅ x86 (32位)
- ✅ x86_64 (64位)
- ✅ ARM32 (32位)
- ✅ ARM64 (64位)
- ✅ MIPS32 (32位)

### macOS Universal Binary
- ✅ 支持同时构建 Intel (x86_64) 和 M 系列 (arm64) 芯片版本

## 📝 文件命名规范

编译后的文件会自动添加后缀：**架构-系统-版本号**

### 示例

- `libmqtt_client-arm64-macos-1.0.0.dylib` - macOS ARM64
- `libmqtt_client-x86_64-linux-1.0.0.so` - Linux x86_64
- `mqtt_client-x86_64-windows-1.0.0.dll` - Windows x86_64
- `libmqtt_client-universal-macos-1.0.0.dylib` - macOS Universal Binary

支持的架构：`x86`, `x86_64`, `arm32`, `arm64`, `mips32`, `universal` (macOS)

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

## 📄 许可证

查看 [LICENSE](LICENSE) 文件了解详情。
