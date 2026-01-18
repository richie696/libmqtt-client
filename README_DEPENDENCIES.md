# 依赖管理说明（混合方案）

## 📦 依赖管理策略

本项目采用**混合方案**管理依赖：

### 核心依赖（Git Submodule）
- **wolfMQTT** - MQTT协议库（autotools构建）
- **wolfSSL** - TLS/SSL库（可选，autotools构建）

**原因**：这些库使用autotools构建系统，需要手动构建，Git Submodule更适合版本控制和团队协作。

### 其他依赖（FetchContent）
- **nlohmann/json** - JSON库（header-only）
- **Google Test** - 测试框架（CMake）
- **fmt** - 格式化库（CMake）

**原因**：这些库使用CMake或header-only，FetchContent可以自动下载和构建。

---

## 🚀 快速开始

### 1. 初始化核心依赖（Git Submodule）

```bash
# 初始化项目依赖（推荐，自动初始化 submodules 和构建 wolfMQTT）
./scripts/unix/init.sh

# 或手动添加
git submodule add https://github.com/wolfSSL/wolfMQTT.git third_party/wolfmqtt
git submodule update --init --recursive
```

### 2. 构建wolfMQTT

```bash
cd third_party/wolfmqtt
./autogen.sh
./configure --enable-mqtt5 --prefix=$(pwd)/install
make -j$(nproc)
make install
cd ../..
```

### 3. 配置CMake（自动处理其他依赖）

```bash
# 基本配置
cmake -B build

# 启用其他依赖
cmake -B build -DENABLE_JSON=ON -DENABLE_TESTING=ON
cmake --build build
```

---

## 📝 依赖列表

| 依赖 | 管理方式 | 构建系统 | 说明 |
|------|---------|---------|------|
| wolfMQTT | Git Submodule | autotools | 核心依赖，需要手动构建 |
| wolfSSL | Git Submodule | autotools | TLS支持（可选） |
| nlohmann/json | FetchContent | header-only | JSON库（可选） |
| Google Test | FetchContent | CMake | 测试框架（可选） |
| fmt | FetchContent | CMake | 格式化库（可选） |

---

## 🔄 更新依赖

### 更新wolfMQTT（Git Submodule）

```bash
cd third_party/wolfmqtt
git fetch origin
git checkout v1.15.0  # 或 master
cd ../..
git add third_party/wolfmqtt
git commit -m "Update wolfMQTT"
```

### 更新其他依赖（FetchContent）

修改CMakeLists.txt中的版本号，重新配置：

```cmake
FetchContent_Declare(
    nlohmann_json
    GIT_TAG v3.12.0  # 更新版本
)
```

---

## ❓ 常见问题

**Q: 为什么wolfMQTT不用FetchContent？**

A: wolfMQTT使用autotools，FetchContent无法自动构建。Git Submodule更适合。

**Q: 团队成员如何获取依赖？**

A: 
```bash
git clone --recursive <repo-url>  # 自动获取submodule
cmake -B build  # FetchContent会自动下载其他依赖
```

**Q: 可以全部用Conan吗？**

A: 可以，但需要为wolfMQTT写recipe。当前混合方案更简单实用。
