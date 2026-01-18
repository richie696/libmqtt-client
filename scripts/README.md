# 构建脚本说明

本目录包含跨平台构建脚本，支持 Linux、macOS 和 Windows。

## 📁 目录结构

```
scripts/
├── unix/              # Linux/macOS 脚本
│   ├── init.sh        # 初始化脚本（首次使用必运行）⭐
│   └── build.sh       # Bash 构建脚本
├── windows/           # Windows 脚本
│   ├── init.ps1       # 初始化脚本（首次使用必运行）⭐
│   └── build.ps1       # PowerShell 构建脚本
├── generate_api_doc.py # API 文档生成脚本
└── README.md          # 本文件
```

## 脚本列表

### Unix 脚本 (Linux/macOS)

- **init.sh** - 初始化脚本（首次使用必运行）⭐
- **build.sh** - Bash 构建脚本

### Windows 脚本

- **init.ps1** - 初始化脚本（首次使用必运行）⭐
- **build.ps1** - PowerShell 构建脚本

### 工具脚本

- **generate_api_doc.py** - API 文档生成脚本

## 快速开始

### 首次使用（必运行）

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

### 构建项目

**初始化完成后，使用构建脚本：**

```bash
# Linux/macOS - 使用 Bash 脚本
./scripts/unix/build.sh --menu

# Windows - 使用 PowerShell 脚本
.\scripts\windows\build.ps1
```

## 主要功能

### 1. 初始化脚本 (unix/init.sh) ⭐ 首次使用必运行

**功能**:
- ✅ 检查基本依赖（Git、CMake、编译器）
- ✅ 初始化 Git Submodules（下载 wolfMQTT）
- ✅ 构建 wolfMQTT 依赖库（如果需要）

**使用场景**:
- 首次克隆项目后
- 更新了 Git Submodules 后
- 需要重新构建依赖时

**使用方法**:
```bash
# Linux/macOS
./scripts/unix/init.sh
```

### 2. 依赖构建

构建脚本会自动按顺序构建所有依赖：

1. **wolfMQTT** - 启用 TLS/SSL 和 MQTT 5.0 支持（由 init.sh 或 build.sh 构建）
2. **nlohmann/json** - Header-only 库（自动下载）
3. **项目本身** - MQTT 客户端库

### 3. 交互式菜单

使用 `--menu` 选项显示交互式菜单：

```
========================================
  构建菜单
========================================

1. 构建所有依赖和项目
2. 仅构建 wolfMQTT
3. 仅构建项目
4. 运行所有测试（含覆盖率报告）
5. 清理所有构建文件
6. 退出
```

### 4. 测试支持

运行测试时会**自动启用代码覆盖率并生成报告**，无需额外步骤。

#### 运行测试

```bash
# Linux/macOS - 运行所有测试（自动启用覆盖率并生成报告）
./scripts/unix/build.sh --test all

# Windows
.\scripts\windows\build.ps1 --test all
```

#### 测试报告

测试报告会自动生成在 `build/TEST_REPORT.md`，包含：

- 测试概览（通过的测试、失败的测试）
- 代码覆盖率统计（自动启用）
- 测试总结（通过率等）

**注意**：运行测试时会自动启用代码覆盖率，覆盖率数据使用 `gcov` 生成，报告会包含在测试报告中。

## 命令行选项

### 基本选项

```bash
-h, --help          显示帮助信息
-t, --type TYPE     构建类型 (Debug|Release|RelWithDebInfo|MinSizeRel)
-d, --dir DIR       构建目录（默认: build）
-c, --clean         清理构建目录
-j, --jobs N        并行编译任务数（默认: 自动检测）
-v, --verbose       显示详细输出
-i, --install       安装到系统
--menu              显示交互式菜单
```

### 测试选项

```bash
--test TYPE         运行测试 (unit|integration|all)
                    注意：运行测试时会自动启用代码覆盖率并生成报告
                    推荐使用 --test all 运行所有测试
```

### macOS 特定选项

```bash
--macos-arch ARCH   macOS架构 (x86_64|arm64|universal)
```

## 使用示例

### 示例 1: 默认构建

```bash
# Linux/macOS
./scripts/unix/build.sh

# Windows
.\scripts\windows\build.ps1
```

### 示例 2: Debug 构建

```bash
# Linux/macOS
./scripts/unix/build.sh -t Debug -c

# Windows
.\scripts\windows\build.ps1 -t Debug -c
```

### 示例 3: 运行测试并生成报告

```bash
# Linux/macOS
./scripts/unix/build.sh --test all

# Windows
.\scripts\windows\build.ps1 --test all
```

### 示例 4: 仅构建 wolfMQTT

```bash
# Linux/macOS
./scripts/unix/build.sh --menu
# 然后选择选项 2

# Windows
.\scripts\windows\build.ps1 --menu
# 然后选择选项 2
```

### 示例 5: 清理所有构建文件

```bash
# Linux/macOS
./scripts/unix/build.sh --menu
# 然后选择选项 5

# Windows
.\scripts\windows\build.ps1 --menu
# 然后选择选项 5
```

## 构建流程

### 1. wolfMQTT 构建

构建脚本会：

1. 检查 wolfMQTT 源码是否存在（`third_party/wolfmqtt`）
2. 使用 CMake 配置 wolfMQTT，启用：
   - TLS/SSL 支持 (`-DWOLFMQTT_TLS=yes`)
   - MQTT 5.0 支持 (`-DWOLFMQTT_V5=yes`)
3. 构建并安装到 `third_party/wolfmqtt/install`

### 2. nlohmann/json

nlohmann/json 是 header-only 库，CMake 会自动通过 FetchContent 下载。

### 3. 项目构建

构建脚本会：

1. 配置 CMake，启用：
   - JSON 支持 (`-DENABLE_JSON=ON`)
   - 测试支持 (`-DENABLE_TESTING=ON`)
   - wolfMQTT 支持 (`-DWOLFMQTT_ENABLED=ON`)
2. 构建项目库和测试可执行文件

## 构建产物

构建完成后，所有目标文件位于 `build/` 目录：

```
build/
├── lib/              # 库文件
│   └── libmqtt_client-*.so (Linux)
│   └── libmqtt_client-*.dylib (macOS)
│   └── mqtt_client-*.dll (Windows)
├── bin/              # 可执行文件
│   └── test_*        # 测试可执行文件
├── include/          # 头文件（安装时）
└── TEST_REPORT.md    # 测试报告（如果运行了测试）
```

## 依赖要求

### 必需依赖

- **Git** - 用于管理 Git Submodules
- **CMake** >= 3.15
- **C++ 编译器** (GCC >= 7.0 或 Clang >= 5.0)
- **构建工具** (make 或 ninja)

### 可选依赖

- **wolfSSL** - TLS/SSL 支持（推荐安装）
  - macOS: `brew install wolfssl`
  - Linux: 使用包管理器安装 `libwolfssl-dev`
- **Python 3** - 用于运行 API 文档生成脚本（可选）

## 故障排除

### 问题 1: wolfMQTT 构建失败

**症状**: 构建 wolfMQTT 时出错

**解决方案**:
1. 检查 wolfSSL 是否已安装
2. 手动构建 wolfMQTT:
   ```bash
   cd third_party/wolfmqtt
   mkdir -p build_cmake
   cd build_cmake
   cmake .. -DWOLFMQTT_TLS=yes -DWOLFMQTT_V5=yes
   cmake --build .
   cmake --install . --prefix ../install
   ```

### 问题 2: 测试失败

**症状**: 运行测试时某些测试失败

**解决方案**:
1. 检查 MQTT 服务器配置（集成测试需要）
2. 查看测试日志: `build/TEST_REPORT.md`
3. 使用 `--verbose` 选项查看详细输出

### 问题 3: 代码覆盖率未生成

**症状**: 测试报告中没有覆盖率数据

**解决方案**:
1. 确保使用 `--test` 选项（会自动启用覆盖率）
2. 检查 `gcov` 是否已安装
3. 确保使用 GCC 或支持覆盖率的编译器

## 平台说明

### Linux 和 macOS

- 使用相同的 Bash 脚本（`unix/` 目录）
- 脚本自动检测平台并适配差异
- 主要差异：包管理器路径（/opt/homebrew vs /usr/local）

### Windows

- 使用 PowerShell 脚本（`build.ps1`）
- PowerShell 在 Windows 7+ 默认安装

### 平台选择建议

- **Linux/macOS**: 使用 `unix/build.sh`
- **Windows**: 使用 `windows/build.ps1`
- **CI/CD**: 根据运行平台选择对应的脚本

## 更多信息

- 项目文档: `docs/`
- 架构设计: `docs/ARCHITECTURE.md`
- API 接口: `docs/API_DESIGN.md`
