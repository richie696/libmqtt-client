# wolfMQTT 集成说明

## 快速开始

**推荐方式**：使用初始化脚本（自动处理所有步骤）

```bash
# Linux/macOS
./scripts/unix/init.sh

# 初始化脚本会：
# 1. 初始化 Git Submodules（下载 wolfMQTT）
# 2. 构建 wolfMQTT（启用 TLS 和 MQTT 5.0）
```

**手动方式**：

```bash
# 1. 初始化 Git Submodules
git submodule update --init --recursive

# 2. 构建 wolfMQTT
cd third_party/wolfmqtt
mkdir -p build_cmake
cd build_cmake
cmake .. -DWOLFMQTT_TLS=yes -DWOLFMQTT_V5=yes -DCMAKE_INSTALL_PREFIX=../install
cmake --build . -j$(nproc)
cmake --install .
```

## 详细文档

- [架构设计文档](docs/ARCHITECTURE.md) - 包含 wolfMQTT 集成设计
