# 构建脚本

本目录提供 Linux/macOS 与 Windows 的初始化和构建入口。当前库面向桌面系统及资源较充足的嵌入式 Linux；wolfMQTT 与 wolfSSL 都以固定版本 Submodule 随主工程构建。

## Linux / macOS

首次克隆后初始化依赖：

```bash
./scripts/unix/init.sh
```

默认 Release 构建：

```bash
./scripts/unix/build.sh
```

常用选项：

```bash
# 清理指定构建目录后重新构建
./scripts/unix/build.sh --clean

# Debug、指定并行数
./scripts/unix/build.sh --type Debug --jobs 8

# 单元测试
./scripts/unix/build.sh --test unit

# 安装到指定前缀
./scripts/unix/build.sh --prefix /opt/libmqtt-client

# macOS Universal Binary
./scripts/unix/build.sh --macos-arch universal
```

真实 EMQX 集成测试需要显式提供环境变量：

```bash
MQTT_TEST_HOST=127.0.0.1 \
MQTT_TEST_TCP_PORT=1883 \
MQTT_TEST_TLS_PORT=8883 \
MQTT_TEST_CA_CERT=/path/to/ca.pem \
MQTT_TEST_ENABLE_TLS=true \
./scripts/unix/build.sh --test integration
```

支持的集成测试协议矩阵为 MQTT 3.1.1/5.0、TCP/TLS、QoS 0/1/2。

## Windows

在 PowerShell 中运行：

```powershell
.\scripts\windows\init.ps1
.\scripts\windows\build.ps1
```

## 依赖管理

- `third_party/wolfmqtt`: wolfMQTT 2.1.0
- `third_party/wolfssl`: wolfSSL 5.9.2
- fmt、nlohmann/json、yaml-cpp、GoogleTest: CMake FetchContent

不需要单独安装或编译系统 wolfSSL。若 Submodule 缺失，可手动执行：

```bash
git submodule update --init --recursive
```
