# 测试说明

本文档说明如何构建和运行MQTT客户端库的测试。

## 前置要求

1. **CMake 3.15+**
2. **C++17 编译器**（GCC 7+, Clang 5+, MSVC 2017+）
3. **Google Test**（可选，可通过FetchContent自动下载）

## 构建测试

### 启用测试

```bash
# 配置构建（启用测试）
cmake -S . -B build/test -DENABLE_TESTING=ON

# 编译
cmake --build build/test
```

### 使用系统已安装的Google Test

如果系统已安装Google Test，CMake会自动检测并使用：

```bash
# Ubuntu/Debian
sudo apt-get install libgtest-dev

# macOS (Homebrew)
brew install googletest

# 然后配置构建
cmake -S . -B build/test -DENABLE_TESTING=ON
```

## 运行测试

### 运行所有测试

```bash
cd build/test
ctest
```

### 运行特定测试

```bash
cd build/test

# 运行核心模块测试
./tests/test_core/test_core

# 运行配置管理测试
./tests/test_config/test_config

# 运行连接管理测试
./tests/test_connection/test_connection

# 运行消息管理测试
./tests/test_message/test_message

# 运行订阅管理测试
./tests/test_subscription/test_subscription

# 运行监控模块测试
./tests/test_monitor/test_monitor

# 运行重连管理测试
./tests/test_reconnect/test_reconnect

# 运行主客户端集成测试
./tests/test_client/test_client
```

### 使用Google Test的详细输出

```bash
cd build/test
./tests/test_core/test_core --gtest_color=yes
```

## 测试覆盖率

### 生成覆盖率报告（需要GCC/Clang）

```bash
# 配置构建（启用覆盖率）
cmake -S . -B build/coverage \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTING=ON \
    -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage"

# 编译
cmake --build build/coverage

# 运行测试
cd build/coverage
ctest

# 生成覆盖率报告（需要lcov）
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html

# 查看报告
open coverage_html/index.html  # macOS
xdg-open coverage_html/index.html  # Linux
```

## 测试结构

```
tests/
├── CMakeLists.txt              # 测试配置
├── README.md                   # 本文档
├── test_core/                  # 核心模块测试
│   ├── test_result.cpp
│   ├── test_error.cpp
│   └── test_types.cpp
├── test_config/                # 配置管理测试
│   └── test_config_manager.cpp
├── test_connection/            # 连接管理测试
│   └── test_connection_manager.cpp
├── test_message/               # 消息管理测试
│   └── test_message_manager.cpp
├── test_subscription/          # 订阅管理测试
│   └── test_subscription_manager.cpp
├── test_monitor/               # 监控模块测试
│   ├── test_network_monitor.cpp
│   ├── test_connection_monitor.cpp
│   └── test_heartbeat_manager.cpp
├── test_reconnect/             # 重连管理测试
│   └── test_reconnect_manager.cpp
└── test_client/                # 主客户端集成测试
    └── test_embedded_mqtt_client.cpp
```

## 测试覆盖率目标

| 组件 | 行覆盖率 | 分支覆盖率 | 函数覆盖率 |
|------|---------|----------|----------|
| **整体** | ≥ 80% | ≥ 75% | ≥ 85% |
| **核心组件** | ≥ 90% | ≥ 85% | ≥ 95% |
| **工具类** | ≥ 85% | ≥ 80% | ≥ 90% |
| **适配层** | ≥ 80% | ≥ 75% | ≥ 85% |

## 故障排除

### Google Test下载失败

如果FetchContent无法下载Google Test（网络问题），可以：

1. **使用系统已安装的版本**：
   ```bash
   # 安装Google Test
   sudo apt-get install libgtest-dev  # Ubuntu/Debian
   brew install googletest            # macOS
   ```

2. **手动下载并配置**：
   ```bash
   git clone https://github.com/google/googletest.git third_party/googletest
   # 然后在CMakeLists.txt中配置路径
   ```

### 编译错误

如果遇到编译错误，请检查：

1. **C++标准**：确保使用C++17或更高版本
2. **依赖项**：确保所有依赖项已正确安装
3. **编译器**：确保编译器支持C++17特性

### 测试失败

如果测试失败，请检查：

1. **日志输出**：查看详细的错误信息
2. **环境变量**：确保测试环境配置正确
3. **网络连接**：某些测试可能需要网络连接（如网络监控测试）

## CI/CD集成

### GitHub Actions示例

```yaml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libgtest-dev
      - name: Configure
        run: cmake -S . -B build -DENABLE_TESTING=ON
      - name: Build
        run: cmake --build build
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

## 更多信息

详细测试设计请参考：`docs/TESTING_DESIGN.md`
