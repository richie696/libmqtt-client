# MQTT 客户端库

一个轻量级、高性能的跨平台 MQTT 客户端库，采用 C++17 标准开发，提供简洁易用的 API 接口。支持 MQTT 5.0 协议，适用于嵌入式设备、IoT 应用和工业自动化场景。

## ✨ 特性

- 🚀 **跨平台支持**: Linux、Windows、macOS
- 🎯 **多架构支持**: x86、x86_64、ARM32、ARM64、MIPS32
- 📦 **轻量级设计**: 资源占用低，适合嵌入式环境
- 🔄 **智能重连**: 指数退避算法，自动恢复连接
- 📡 **MQTT 5.0**: 支持最新 MQTT 5.0 协议标准
- 🛠️ **易于集成**: 提供 CMake 构建系统，支持交叉编译
- 📚 **完整文档**: 详细的构建说明和使用文档
- 🔧 **自动化构建**: 提供多平台构建脚本，一键编译

## 快速开始

### 首次使用

**克隆项目后，首先运行初始化脚本：**

```bash
# Linux/macOS
./scripts/unix/init.sh

# Windows (PowerShell)
.\scripts\windows\init.ps1

# 初始化脚本会：
# 1. 检查基本依赖（Git、CMake、编译器）
# 2. 初始化 Git Submodules（下载 wolfMQTT）
# 3. 构建 wolfMQTT 依赖库
```

### 构建

**初始化完成后，使用构建脚本：**

```bash
# Linux/macOS
./scripts/unix/build.sh

# Windows (PowerShell)
.\scripts\windows\build.ps1
```

### 运行Demo

```bash
# Linux/macOS
./build/bin/mqtt_demo-arm64-macos-1.0.0

# Windows
build\bin\mqtt_demo-x86_64-windows-1.0.0.exe
```

## 依赖要求

### 必需依赖

- **Git** - 用于管理 Git Submodules
- **CMake** >= 3.15
- **C++编译器** 支持 C++17 (GCC 7+, Clang 5+, MSVC 2017+)
- **构建工具** (make 或 ninja)

### 可选依赖

- **wolfSSL** - 用于 TLS/SSL 支持（可选，但推荐）
  - macOS: `brew install wolfssl`
  - Linux: `sudo apt-get install libwolfssl-dev`

### 快速安装

**macOS:**
```bash
xcode-select --install  # 安装Xcode Command Line Tools
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

**初始化脚本会自动检查依赖，缺少时会提示安装方法。**

## 构建选项

### 基本构建

```bash
./scripts/unix/build.sh                    # Release构建
./scripts/unix/build.sh -t Debug          # Debug构建
./scripts/unix/build.sh -c                # 清理后构建
./scripts/unix/build.sh -j 8               # 指定并行任务数
./scripts/unix/build.sh --no-demo         # 不构建Demo
```

### macOS Universal Binary

```bash
./scripts/unix/build.sh --macos-arch universal  # 同时支持Intel和M系列芯片
```

### 查看帮助

```bash
./scripts/unix/build.sh --help
```

## 文件命名规范

编译后的文件会自动添加后缀：**架构-系统-版本号**

### 示例

- `libmqtt_client-arm64-macos-1.0.0.dylib` - macOS ARM64
- `libmqtt_client-x86_64-linux-1.0.0.so` - Linux x86_64
- `mqtt_client-x86_64-windows-1.0.0.dll` - Windows x86_64
- `mqtt_demo-arm64-macos-1.0.0` - Demo程序

支持的架构：`x86`, `x86_64`, `arm32`, `arm64`, `mips32`, `universal` (macOS)

## 支持的平台和架构

### 操作系统
- Linux
- Windows
- macOS

### 芯片架构
- x86 (32位)
- x86_64 (64位)
- ARM32 (32位)
- ARM64 (64位)
- MIPS32 (32位)

## 交叉编译

使用工具链文件进行交叉编译：

```bash
# Linux ARM32
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/arm-linux-gnueabihf.cmake ..

# Linux ARM64
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/aarch64-linux-gnu.cmake ..

# Windows (从Linux/macOS)
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/x86_64-w64-mingw32.cmake ..
```

## 项目结构

```
libmqtt-client/
├── include/              # 公共头文件
│   └── mqtt_client/      # 库头文件（用户需要包含的头文件）
│       ├── embedded_mqtt_client.h
│       ├── config/
│       ├── connection/
│       ├── message/
│       ├── subscription/
│       └── ...
├── src/                  # 源文件
│   ├── embedded_mqtt_client.cpp
│   ├── adapter/
│   ├── config/
│   ├── connection/
│   ├── message/
│   └── ...
├── scripts/              # 构建脚本
│   ├── unix/            # Linux/macOS 脚本
│   └── windows/          # Windows 脚本
├── third_party/          # 第三方依赖（Git Submodule）
│   └── wolfmqtt/        # wolfMQTT 库（通过 submodule 管理）
├── toolchains/           # 交叉编译工具链文件
├── docs/                 # 文档
│   ├── README.md        # 文档索引
│   ├── ARCHITECTURE.md   # 架构设计
│   └── API_DESIGN.md    # API接口设计
├── tests/                # 测试代码
├── CMakeLists.txt        # CMake配置
└── build/                # 构建输出目录（不提交）
```

### 使用库

在代码中使用库时，包含主头文件：

```cpp
#include "mqtt_client/embedded_mqtt_client.h"
```

## 更多信息

- **文档索引**: 查看 [docs/README.md](docs/README.md) - 完整的文档目录和阅读指南
- **架构设计方案**: 查看 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - 嵌入式MQTT客户端架构设计（包含构建、依赖、配置等完整说明）
- **API接口设计**: 查看 [docs/API_DESIGN.md](docs/API_DESIGN.md) - 完整的API接口清单和接口定义
