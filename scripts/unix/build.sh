#!/bin/bash
# MQTT客户端库 - 跨平台构建脚本 (Linux/macOS)
# 支持依赖构建、测试和代码覆盖率报告

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 默认配置
BUILD_TYPE="Release"
BUILD_DIR="build"
CLEAN=false
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
VERBOSE=false
INSTALL=false
MACOS_ARCH=""  # macOS架构，空表示自动检测
ENABLE_COVERAGE=false
RUN_TESTS=false
TEST_TYPE=""  # unit, integration, all

# 获取脚本和项目目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 脚本在 scripts/unix/ 目录下，需要向上两级才能到达项目根目录
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
WOLFMQTT_DIR="${PROJECT_ROOT}/third_party/wolfmqtt"
WOLFMQTT_BUILD_DIR="${WOLFMQTT_DIR}/build_cmake"
WOLFMQTT_INSTALL_DIR="${WOLFMQTT_DIR}/install"

# 帮助信息
show_help() {
    cat << EOF
MQTT客户端库 - 构建脚本 (Linux/macOS)

用法: $0 [选项] [菜单选项]

选项:
    -h, --help          显示此帮助信息
    -t, --type TYPE     构建类型 (Debug|Release|RelWithDebInfo|MinSizeRel)
                        默认: Release
    -d, --dir DIR       构建目录
                        默认: build
    -c, --clean         清理构建目录
    -j, --jobs N        并行编译任务数
                        默认: 自动检测CPU核心数
    -v, --verbose       显示详细输出
    -i, --install       安装到系统
    --macos-arch ARCH   macOS架构 (x86_64|arm64|universal)
                        默认: 自动检测
    --test TYPE         运行测试 (unit|integration|all)
                        注意: 运行测试时会自动启用代码覆盖率并生成报告
    --menu              显示交互式菜单

菜单选项:
    1. 构建所有依赖和项目
    2. 仅构建 wolfMQTT
    3. 仅构建项目
    4. 运行单元测试
    5. 运行集成测试
    6. 运行所有测试
    7. 生成测试报告（含代码覆盖率）
    8. 清理所有构建文件
    9. 退出

示例:
    $0                          # 默认Release构建
    $0 --menu                   # 显示交互式菜单
    $0 --test all               # 运行所有测试（自动启用覆盖率并生成报告）
    $0 -t Debug -c              # Debug构建，清理后构建

EOF
}

# 打印标题
print_header() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}  MQTT 客户端库 - 构建脚本${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
}

# 检测平台
detect_platform() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        PLATFORM="macOS"
        echo -e "${GREEN}平台: macOS${NC}"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        PLATFORM="Linux"
        echo -e "${GREEN}平台: Linux${NC}"
    else
        echo -e "${YELLOW}警告: 未识别的平台: $OSTYPE${NC}"
        PLATFORM="Unknown"
    fi
}

# 检查依赖
check_dependencies() {
    echo -e "${BLUE}========== 依赖检查 ==========${NC}"
    
    DEPENDENCIES_OK=true
    MISSING_DEPS=()
    
    # 检查CMake
    echo -n "检查 CMake... "
    if ! command -v cmake &> /dev/null; then
        echo -e "${RED}✗ 未找到${NC}"
        DEPENDENCIES_OK=false
        MISSING_DEPS+=("CMake >= 3.15")
    else
        CMAKE_VERSION=$(cmake --version | head -n 1)
        echo -e "${GREEN}✓ $CMAKE_VERSION${NC}"
    fi
    
    # 检查C++编译器
    echo -n "检查 C++编译器... "
    if [[ "$PLATFORM" == "macOS" ]]; then
        if command -v clang++ &> /dev/null; then
            CLANG_VERSION=$(clang++ --version | head -n 1)
            echo -e "${GREEN}✓ $CLANG_VERSION${NC}"
        else
            echo -e "${RED}✗ 未找到${NC}"
            DEPENDENCIES_OK=false
            MISSING_DEPS+=("Clang >= 5.0")
        fi
    elif [[ "$PLATFORM" == "Linux" ]]; then
        if command -v g++ &> /dev/null; then
            GCC_VERSION=$(g++ --version | head -n 1)
            echo -e "${GREEN}✓ $GCC_VERSION${NC}"
        elif command -v clang++ &> /dev/null; then
            CLANG_VERSION=$(clang++ --version | head -n 1)
            echo -e "${GREEN}✓ $CLANG_VERSION${NC}"
        else
            echo -e "${RED}✗ 未找到${NC}"
            DEPENDENCIES_OK=false
            MISSING_DEPS+=("GCC >= 7.0 或 Clang >= 5.0")
        fi
    fi
    
    # 检查构建工具
    echo -n "检查 构建工具... "
    if command -v make &> /dev/null || command -v ninja &> /dev/null; then
        echo -e "${GREEN}✓ 已安装${NC}"
    else
        echo -e "${RED}✗ 未找到${NC}"
        DEPENDENCIES_OK=false
        MISSING_DEPS+=("make 或 ninja")
    fi
    
    # 检查wolfSSL（TLS支持需要）
    echo -n "检查 wolfSSL... "
    if [[ "$PLATFORM" == "macOS" ]]; then
        if [ -d "/opt/homebrew/opt/wolfssl" ] || [ -d "/usr/local/opt/wolfssl" ]; then
            echo -e "${GREEN}✓ 已安装${NC}"
        else
            echo -e "${YELLOW}⚠ 未找到（TLS功能需要）${NC}"
            echo -e "  ${YELLOW}安装方法: brew install wolfssl${NC}"
        fi
    elif [[ "$PLATFORM" == "Linux" ]]; then
        if ldconfig -p 2>/dev/null | grep -q libwolfssl || [ -f /usr/lib/libwolfssl.so ] || [ -f /usr/local/lib/libwolfssl.so ]; then
            echo -e "${GREEN}✓ 已安装${NC}"
        else
            echo -e "${YELLOW}⚠ 未找到（TLS功能需要）${NC}"
        fi
    fi
    
    echo -e "${BLUE}=============================${NC}"
    echo ""
    
    if [ "$DEPENDENCIES_OK" != true ]; then
        echo -e "${RED}========================================${NC}"
        echo -e "${RED}  依赖检查失败！${NC}"
        echo -e "${RED}========================================${NC}"
        echo ""
        echo -e "${YELLOW}缺少的依赖:${NC}"
        for dep in "${MISSING_DEPS[@]}"; do
            echo -e "  - ${RED}$dep${NC}"
        done
        echo ""
        exit 1
    fi
    
    echo -e "${GREEN}所有依赖检查通过！✓${NC}"
    echo ""
}

# 初始化 Git Submodule（如果需要）
init_submodules() {
    echo -e "${BLUE}========== 初始化 Git Submodules ==========${NC}"
    
    if [ ! -d "$WOLFMQTT_DIR" ] || [ ! -f "$WOLFMQTT_DIR/.git" ]; then
        echo -e "${YELLOW}检测到 wolfMQTT submodule 未初始化，正在初始化...${NC}"
        
        # 检查是否在 Git 仓库中
        if [ -d "${PROJECT_ROOT}/.git" ]; then
            cd "$PROJECT_ROOT"
            git submodule update --init --recursive
            echo -e "${GREEN}✓ Submodule 初始化完成${NC}"
        else
            echo -e "${YELLOW}警告: 不在 Git 仓库中，无法自动初始化 submodule${NC}"
            echo -e "${YELLOW}请手动运行: git submodule update --init --recursive${NC}"
        fi
        echo ""
    else
        echo -e "${GREEN}✓ Submodule 已初始化${NC}"
        echo ""
    fi
}

# 构建 wolfMQTT
build_wolfmqtt() {
    echo -e "${BLUE}========== 构建 wolfMQTT ==========${NC}"
    
    # 先尝试初始化 submodule
    init_submodules
    
    if [ ! -d "$WOLFMQTT_DIR" ]; then
        echo -e "${RED}错误: wolfMQTT 目录不存在: $WOLFMQTT_DIR${NC}"
        echo -e "${YELLOW}请先运行: ./scripts/unix/init.sh${NC}"
        exit 1
    fi
    
    cd "$WOLFMQTT_DIR"
    
    # 创建构建目录
    mkdir -p "$WOLFMQTT_BUILD_DIR"
    cd "$WOLFMQTT_BUILD_DIR"
    
    # 检测 wolfSSL 路径
    WOLFSSL_PATH=""
    if [[ "$PLATFORM" == "macOS" ]]; then
        if [ -d "/opt/homebrew/opt/wolfssl" ]; then
            WOLFSSL_PATH="/opt/homebrew/opt/wolfssl"
        elif [ -d "/usr/local/opt/wolfssl" ]; then
            WOLFSSL_PATH="/usr/local/opt/wolfssl"
        fi
    fi
    
    # 配置 CMake
    echo -e "${CYAN}配置 wolfMQTT (启用 TLS 和 MQTT 5.0)...${NC}"
    CMAKE_ARGS=(
        ".."
        "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
        "-DCMAKE_INSTALL_PREFIX=$WOLFMQTT_INSTALL_DIR"
        "-DWOLFMQTT_TLS=yes"
        "-DWOLFMQTT_V5=yes"
    )
    
    if [ -n "$WOLFSSL_PATH" ]; then
        CMAKE_ARGS+=("-DWITH_WOLFSSL=$WOLFSSL_PATH")
        echo -e "${GREEN}使用 wolfSSL: $WOLFSSL_PATH${NC}"
    fi
    
    if [ "$VERBOSE" = true ]; then
        cmake "${CMAKE_ARGS[@]}"
    else
        cmake "${CMAKE_ARGS[@]}" > /dev/null 2>&1
    fi
    
    # 构建
    echo -e "${CYAN}构建 wolfMQTT...${NC}"
    if [ "$VERBOSE" = true ]; then
        cmake --build . --config "$BUILD_TYPE" -j "$JOBS"
    else
        cmake --build . --config "$BUILD_TYPE" -j "$JOBS" > /dev/null 2>&1
    fi
    
    # 安装
    echo -e "${CYAN}安装 wolfMQTT...${NC}"
    if [ "$VERBOSE" = true ]; then
        cmake --install . --config "$BUILD_TYPE"
    else
        cmake --install . --config "$BUILD_TYPE" > /dev/null 2>&1
    fi
    
    echo -e "${GREEN}✓ wolfMQTT 构建完成${NC}"
    echo ""
    
    cd "$PROJECT_ROOT"
}

# 构建项目
build_project() {
    echo -e "${BLUE}========== 构建项目 ==========${NC}"
    
    BUILD_DIR_ABS="$PROJECT_ROOT/$BUILD_DIR"
    
    # 清理构建目录
    if [ "$CLEAN" = true ]; then
        echo -e "${YELLOW}清理构建目录...${NC}"
        rm -rf "$BUILD_DIR_ABS"
    fi
    
    # 创建构建目录
    mkdir -p "$BUILD_DIR_ABS"
    cd "$BUILD_DIR_ABS"
    
    # 准备CMake参数
    CMAKE_ARGS=(
        "$PROJECT_ROOT"
        "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
        "-DENABLE_JSON=ON"
        "-DENABLE_TESTING=ON"
        "-DWOLFMQTT_ENABLED=ON"
    )
    
    if [ "$ENABLE_COVERAGE" = true ]; then
        CMAKE_ARGS+=(
            "-DENABLE_COVERAGE=ON"
            "-DCMAKE_CXX_FLAGS=--coverage -fprofile-arcs -ftest-coverage"
        )
    fi
    
    # macOS特定配置
    if [[ "$PLATFORM" == "macOS" ]]; then
        if [[ -n "$MACOS_ARCH" ]]; then
            CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=$MACOS_ARCH")
        fi
        CMAKE_ARGS+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15")
    fi
    
    # 配置CMake
    echo -e "${CYAN}配置CMake...${NC}"
    if [ "$VERBOSE" = true ]; then
        cmake "${CMAKE_ARGS[@]}"
    else
        cmake "${CMAKE_ARGS[@]}" 2>&1 | grep -E "(平台|架构|配置|错误|警告|CMake|找到|已配置)" || true
    fi
    
    echo ""
    
    # 构建
    echo -e "${CYAN}开始构建...${NC}"
    BUILD_ARGS=(
        "--build" "."
        "--config" "$BUILD_TYPE"
        "-j" "$JOBS"
    )
    
    if [ "$VERBOSE" = true ]; then
        cmake "${BUILD_ARGS[@]}"
    else
        cmake "${BUILD_ARGS[@]}" 2>&1 | grep -E "(\[.*%\]|Building|Linking|Built target|错误|error)" || true
    fi
    
    BUILD_RESULT=$?
    if [ $BUILD_RESULT -ne 0 ]; then
        echo -e "${RED}构建失败！${NC}"
        exit $BUILD_RESULT
    fi
    
    echo ""
    echo -e "${GREEN}构建成功！${NC}"
    echo ""
    
    # 显示构建产物
    show_build_artifacts
    
    cd "$PROJECT_ROOT"
}

# 显示构建产物
show_build_artifacts() {
    echo "构建产物:"
    
    # 显示库文件
    if [[ "$PLATFORM" == "macOS" ]]; then
        DYLIB_FILES=$(find lib -name "libmqtt_client-*.dylib" -type f 2>/dev/null | head -1)
        if [ -n "$DYLIB_FILES" ]; then
            echo "  库文件: $DYLIB_FILES"
            file "$DYLIB_FILES"
        fi
    elif [[ "$PLATFORM" == "Linux" ]]; then
        SO_FILES=$(find lib -name "libmqtt_client-*.so*" -type f 2>/dev/null | head -1)
        if [ -n "$SO_FILES" ]; then
            echo "  库文件: $SO_FILES"
            file "$SO_FILES"
        fi
    fi
    
    # 显示测试可执行文件
    if [ -d "bin" ]; then
        TEST_FILES=$(find bin -name "test_*" -type f 2>/dev/null)
        if [ -n "$TEST_FILES" ]; then
            echo "  测试文件:"
            echo "$TEST_FILES" | sed 's|^|    - |'
        fi
    fi
}

# 运行测试（自动启用覆盖率并生成报告）
run_tests() {
    local test_type=$1
    
    echo -e "${BLUE}========== 运行测试（含覆盖率） ==========${NC}"
    
    # 自动启用覆盖率
    ENABLE_COVERAGE=true
    
    BUILD_DIR_ABS="$PROJECT_ROOT/$BUILD_DIR"
    cd "$BUILD_DIR_ABS"
    
    # 清理旧的覆盖率数据
    echo -e "${CYAN}清理旧的覆盖率数据...${NC}"
    find . -name "*.gcda" -delete 2>/dev/null || true
    
    # 运行测试
    case "$test_type" in
        unit)
            echo -e "${CYAN}运行单元测试...${NC}"
            TEST_TARGETS=$(find bin -name "test_*" -type f 2>/dev/null | grep -v "test_mqtt" | grep -v "test_integration" || true)
            ;;
        integration)
            echo -e "${CYAN}运行集成测试...${NC}"
            TEST_TARGETS=$(find bin -name "test_mqtt*" -type f 2>/dev/null || true)
            ;;
        all)
            echo -e "${CYAN}运行所有测试...${NC}"
            TEST_TARGETS=$(find bin -name "test_*" -type f 2>/dev/null || true)
            ;;
        *)
            echo -e "${RED}错误: 无效的测试类型: $test_type${NC}"
            exit 1
            ;;
    esac
    
    if [ -z "$TEST_TARGETS" ]; then
        echo -e "${YELLOW}警告: 未找到测试文件${NC}"
        return 1
    fi
    
    # 运行每个测试
    FAILED_TESTS=()
    PASSED_TESTS=()
    
    # 创建临时文件存储测试结果
    TEST_RESULTS_FILE="/tmp/test_results_$$.txt"
    > "$TEST_RESULTS_FILE"  # 清空文件
    
    for test_file in $TEST_TARGETS; do
        test_name=$(basename "$test_file")
        echo -e "${CYAN}运行: $test_name${NC}"
        
        if "$test_file" 2>&1 | tee "/tmp/${test_name}.log"; then
            PASSED_TESTS+=("$test_name")
            echo "PASSED:$test_name" >> "$TEST_RESULTS_FILE"
            echo -e "${GREEN}✓ $test_name 通过${NC}"
        else
            FAILED_TESTS+=("$test_name")
            echo "FAILED:$test_name" >> "$TEST_RESULTS_FILE"
            echo -e "${RED}✗ $test_name 失败${NC}"
        fi
        echo ""
    done
    
    # 显示测试结果
    echo -e "${BLUE}========== 测试结果 ==========${NC}"
    echo -e "${GREEN}通过的测试: ${#PASSED_TESTS[@]}${NC}"
    for test in "${PASSED_TESTS[@]}"; do
        echo -e "  ${GREEN}✓ $test${NC}"
    done
    
    if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
        echo -e "${RED}失败的测试: ${#FAILED_TESTS[@]}${NC}"
        for test in "${FAILED_TESTS[@]}"; do
            echo -e "  ${RED}✗ $test${NC}"
        done
    fi
    
    # 自动生成测试报告（含代码覆盖率）
    echo ""
    generate_test_report "$TEST_RESULTS_FILE"
    
    # 清理临时文件
    rm -f "$TEST_RESULTS_FILE"
    
    cd "$PROJECT_ROOT"
}

# 生成测试报告
generate_test_report() {
    local test_results_file="$1"
    
    echo -e "${BLUE}========== 生成测试报告 ==========${NC}"
    
    BUILD_DIR_ABS="$PROJECT_ROOT/$BUILD_DIR"
    cd "$BUILD_DIR_ABS"
    
    REPORT_FILE="$BUILD_DIR_ABS/TEST_REPORT.md"
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
    
    # 读取测试结果
    local passed_tests=()
    local failed_tests=()
    
    if [ -f "$test_results_file" ]; then
        while IFS=: read -r status test_name; do
            if [ "$status" = "PASSED" ]; then
                passed_tests+=("$test_name")
            elif [ "$status" = "FAILED" ]; then
                failed_tests+=("$test_name")
            fi
        done < "$test_results_file"
    fi
    
    # 创建报告文件
    cat > "$REPORT_FILE" << EOF
# 测试报告

生成时间: $TIMESTAMP

## 测试概览

EOF
    
    # 统计信息
    local total_tests=$((${#passed_tests[@]} + ${#failed_tests[@]}))
    local passed_count=${#passed_tests[@]}
    local failed_count=${#failed_tests[@]}
    
    if [ $total_tests -gt 0 ]; then
        echo "| 测试套件 | 状态 | 详情 |" >> "$REPORT_FILE"
        echo "|---------|------|------|" >> "$REPORT_FILE"
        
        # 处理通过的测试
        for test_name in "${passed_tests[@]}"; do
            if [ -n "$test_name" ]; then
                # 解析测试日志，提取测试用例信息
                local test_cases=""
                if [ -f "/tmp/${test_name}.log" ]; then
                    # 尝试提取 Google Test 格式的测试用例
                    local case_count=$(grep -c "\[  PASSED  \]" "/tmp/${test_name}.log" 2>/dev/null | tr -d '\n\r ' || echo "0")
                    # 确保是数字
                    case_count=${case_count:-0}
                    case_count=$((case_count + 0))
                    if [ $case_count -gt 0 ]; then
                        test_cases="通过 $case_count 个测试用例"
                    else
                        test_cases="通过"
                    fi
                else
                    test_cases="通过"
                fi
                echo "| $test_name | ✓ 通过 | $test_cases |" >> "$REPORT_FILE"
            fi
        done
        
        # 处理失败的测试
        for test_name in "${failed_tests[@]}"; do
            if [ -n "$test_name" ]; then
                # 解析测试日志，提取失败信息
                local failure_info="失败"
                if [ -f "/tmp/${test_name}.log" ]; then
                    # 尝试提取失败原因
                    local failure_line=$(grep -E "(FAILED|失败|错误|error)" "/tmp/${test_name}.log" | head -1)
                    if [ -n "$failure_line" ]; then
                        failure_info=$(echo "$failure_line" | cut -c1-50 | sed 's/|/\\|/g')
                    fi
                fi
                echo "| $test_name | ✗ 失败 | $failure_info |" >> "$REPORT_FILE"
            fi
        done
    fi
    
    # 详细的测试结果
    echo "" >> "$REPORT_FILE"
    echo "## 详细测试结果" >> "$REPORT_FILE"
    echo "" >> "$REPORT_FILE"
    
    # 为每个测试添加详细信息
    for test_name in "${passed_tests[@]}" "${failed_tests[@]}"; do
        if [ -n "$test_name" ] && [ -f "/tmp/${test_name}.log" ]; then
            echo "### $test_name" >> "$REPORT_FILE"
            echo "" >> "$REPORT_FILE"
            
            # 提取关键信息
            if grep -q "\[  PASSED  \]" "/tmp/${test_name}.log"; then
                echo "**状态**: ✓ 通过" >> "$REPORT_FILE"
                echo "" >> "$REPORT_FILE"
                echo "**通过的测试用例**:\`\`\`" >> "$REPORT_FILE"
                grep "\[  PASSED  \]" "/tmp/${test_name}.log" | sed 's/\[  PASSED  \]//' | head -20 >> "$REPORT_FILE"
                echo "\`\`\`" >> "$REPORT_FILE"
            elif grep -q "\[  FAILED  \]" "/tmp/${test_name}.log"; then
                echo "**状态**: ✗ 失败" >> "$REPORT_FILE"
                echo "" >> "$REPORT_FILE"
                echo "**失败的测试用例**:\`\`\`" >> "$REPORT_FILE"
                grep -A 2 "\[  FAILED  \]" "/tmp/${test_name}.log" | head -30 >> "$REPORT_FILE"
                echo "\`\`\`" >> "$REPORT_FILE"
            else
                # 非 Google Test 格式，显示最后几行
                echo "**输出**:\`\`\`" >> "$REPORT_FILE"
                tail -20 "/tmp/${test_name}.log" >> "$REPORT_FILE"
                echo "\`\`\`" >> "$REPORT_FILE"
            fi
            echo "" >> "$REPORT_FILE"
        fi
    done
    
    # 代码覆盖率
    echo "" >> "$REPORT_FILE"
    echo "## 代码覆盖率" >> "$REPORT_FILE"
    echo "" >> "$REPORT_FILE"
    
    if [ "$ENABLE_COVERAGE" = true ] && command -v gcov &> /dev/null; then
        # 收集覆盖率数据
        local gcda_files=$(find . -name "*.gcda" 2>/dev/null)
        if [ -n "$gcda_files" ]; then
            echo "使用 gcov 生成覆盖率报告..." >> "$REPORT_FILE"
            echo "" >> "$REPORT_FILE"
            
            # 生成覆盖率统计
            echo "### 覆盖率统计" >> "$REPORT_FILE"
            echo "" >> "$REPORT_FILE"
            echo "| 文件 | 行覆盖率 | 函数覆盖率 | 分支覆盖率 |" >> "$REPORT_FILE"
            echo "|------|---------|-----------|-----------|" >> "$REPORT_FILE"
            
            # 如果没有找到 gcov 文件，尝试生成
            if [ -z "$(find . -name "*.gcov" 2>/dev/null)" ]; then
                echo "正在生成覆盖率文件..." >> "$REPORT_FILE"
                # 查找所有 .gcda 文件并生成对应的 .gcov 文件
                for gcda_file in $(find . -name "*.gcda" 2>/dev/null | head -20); do
                    gcov -b -c "$gcda_file" > /dev/null 2>&1 || true
                done
            fi
            
            # 使用 gcov 分析每个源文件
            local gcov_files=$(find . -name "*.gcov" 2>/dev/null | head -30)
            local coverage_count=0
            
            # 改用直接从 .gcda 文件生成覆盖率统计
            local gcda_files=$(find . -name "*.gcda" -path "*/src/*" 2>/dev/null | head -30)
            
            for gcda_file in $gcda_files; do
                if [ -f "$gcda_file" ]; then
                    local gcda_dir=$(dirname "$gcda_file")
                    
                    # 运行 gcov 获取覆盖率信息
                    local gcov_output=$(gcov -b -c -o "$gcda_dir" "$gcda_file" 2>&1)
                    
                    # 从 gcov 输出中提取覆盖率信息
                    local line_info=$(echo "$gcov_output" | grep "Lines executed:" | head -1)
                    local branch_info=$(echo "$gcov_output" | grep "Branches executed:" | head -1)
                    local file_path=$(echo "$gcov_output" | grep "^File '" | sed "s/^File '\(.*\)'/\1/" | head -1)
                    
                    if [ -n "$line_info" ] && [ -n "$file_path" ]; then
                        # 提取行覆盖率百分比
                        local line_coverage=$(echo "$line_info" | sed -n 's/.*Lines executed:[[:space:]]*\([0-9.]*\)%.*/\1/p')
                        # 提取分支覆盖率百分比
                        local branch_coverage="-"
                        if [ -n "$branch_info" ]; then
                            branch_coverage=$(echo "$branch_info" | sed -n 's/.*Branches executed:[[:space:]]*\([0-9.]*\)%.*/\1/p')
                            branch_coverage="${branch_coverage}%"
                        fi
                        
                        # 提取相对路径（相对于项目根目录）
                        local display_name=$(echo "$file_path" | sed "s|.*/src/|src/|")
                        
                        # 只显示项目源文件
                        if [[ "$display_name" == src/* ]] && [ -n "$line_coverage" ]; then
                            echo "| $display_name | ${line_coverage}% | - | ${branch_coverage} |" >> "$REPORT_FILE"
                            coverage_count=$((coverage_count + 1))
                        fi
                    fi
                    
                    # 限制显示的文件数量
                    if [ $coverage_count -ge 20 ]; then
                        break
                    fi
                fi
            done
            
            if [ $coverage_count -eq 0 ]; then
                echo "| (暂无覆盖率数据) | - | - | - |" >> "$REPORT_FILE"
            fi
        else
            echo "未找到覆盖率数据文件 (.gcda)" >> "$REPORT_FILE"
            echo "请确保使用 \`--coverage\` 选项编译并运行测试。" >> "$REPORT_FILE"
        fi
    else
        echo "代码覆盖率未启用或 gcov 未安装。" >> "$REPORT_FILE"
        if ! command -v gcov &> /dev/null; then
            echo "安装 gcov: \`sudo apt-get install gcov\` (Linux) 或 \`brew install gcc\` (macOS)" >> "$REPORT_FILE"
        fi
    fi
    
    # 总结
    echo "" >> "$REPORT_FILE"
    echo "## 总结" >> "$REPORT_FILE"
    echo "" >> "$REPORT_FILE"
    echo "- 总测试套件数: $total_tests" >> "$REPORT_FILE"
    echo "- 通过的测试: $passed_count" >> "$REPORT_FILE"
    echo "- 失败的测试: $failed_count" >> "$REPORT_FILE"
    if [ $total_tests -gt 0 ]; then
        local pass_rate=$(awk "BEGIN {printf \"%.1f\", ($passed_count/$total_tests)*100}")
        echo "- 通过率: ${pass_rate}%" >> "$REPORT_FILE"
    fi
    
    echo -e "${GREEN}测试报告已生成: $REPORT_FILE${NC}"
    echo ""
    
    cd "$PROJECT_ROOT"
}

# 清理所有构建文件
clean_all() {
    echo -e "${YELLOW}清理所有构建文件...${NC}"
    
    # 清理项目构建目录
    if [ -d "$PROJECT_ROOT/$BUILD_DIR" ]; then
        rm -rf "$PROJECT_ROOT/$BUILD_DIR"
        echo -e "${GREEN}✓ 已清理项目构建目录${NC}"
    fi
    
    # 清理 wolfMQTT 构建目录
    if [ -d "$WOLFMQTT_BUILD_DIR" ]; then
        rm -rf "$WOLFMQTT_BUILD_DIR"
        echo -e "${GREEN}✓ 已清理 wolfMQTT 构建目录${NC}"
    fi
    
    echo -e "${GREEN}清理完成！${NC}"
    echo ""
}

# 显示菜单
show_menu() {
    while true; do
        echo -e "${CYAN}========================================${NC}"
        echo -e "${CYAN}  构建菜单${NC}"
        echo -e "${CYAN}========================================${NC}"
        echo ""
        echo "1. 构建所有依赖和项目"
        echo "2. 仅构建 wolfMQTT"
        echo "3. 仅构建项目"
        echo "4. 运行所有测试（含覆盖率报告）"
        echo "5. 清理所有构建文件"
        echo "6. 退出"
        echo ""
        echo -n "请选择 [1-6]: "
        read -r choice
        
        case $choice in
            1)
                check_dependencies
                build_wolfmqtt
                build_project
                ;;
            2)
                check_dependencies
                build_wolfmqtt
                ;;
            3)
                build_project
                ;;
            4)
                # 运行所有测试（自动启用覆盖率并生成报告）
                run_tests all
                ;;
            5)
                clean_all
                ;;
            6)
                echo -e "${GREEN}退出${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}无效的选择，请重试${NC}"
                ;;
        esac
        
        echo ""
        echo -n "按 Enter 继续..."
        read -r
        echo ""
    done
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -d|--dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -i|--install)
            INSTALL=true
            shift
            ;;
        --macos-arch)
            MACOS_ARCH="$2"
            shift 2
            ;;
        --test)
            RUN_TESTS=true
            TEST_TYPE="$2"
            shift 2
            ;;
        --menu)
            print_header
            detect_platform
            show_menu
            exit 0
            ;;
        *)
            echo -e "${RED}错误: 未知选项 $1${NC}" >&2
            show_help
            exit 1
            ;;
    esac
done

# 验证构建类型
if [[ ! "$BUILD_TYPE" =~ ^(Debug|Release|RelWithDebInfo|MinSizeRel)$ ]]; then
    echo -e "${RED}错误: 无效的构建类型: $BUILD_TYPE${NC}" >&2
    exit 1
fi

# 主流程
print_header
detect_platform
check_dependencies

# 如果指定了测试，先构建
if [ "$RUN_TESTS" = true ]; then
    # 自动启用覆盖率
    ENABLE_COVERAGE=true
    
    # 检查是否需要构建 wolfMQTT
    if [ ! -f "$WOLFMQTT_INSTALL_DIR/lib/libwolfmqtt.a" ] && [ ! -f "$WOLFMQTT_INSTALL_DIR/lib/libwolfmqtt.so" ] && [ ! -f "$WOLFMQTT_INSTALL_DIR/lib/libwolfmqtt.dylib" ]; then
        build_wolfmqtt
    fi
    
    # 构建项目（启用覆盖率）
    build_project
    
    # 运行测试（自动包含覆盖率收集和报告生成）
    run_tests "$TEST_TYPE"
else
    # 正常构建流程
    # 检查是否需要构建 wolfMQTT
    if [ ! -f "$WOLFMQTT_INSTALL_DIR/lib/libwolfmqtt.a" ] && [ ! -f "$WOLFMQTT_INSTALL_DIR/lib/libwolfmqtt.so" ] && [ ! -f "$WOLFMQTT_INSTALL_DIR/lib/libwolfmqtt.dylib" ]; then
        build_wolfmqtt
    fi
    
    # 构建项目
    build_project
    
    # 安装
    if [ "$INSTALL" = true ]; then
        echo ""
        echo -e "${BLUE}安装到系统...${NC}"
        cd "$PROJECT_ROOT/$BUILD_DIR"
        sudo cmake --install . --config "$BUILD_TYPE"
        echo -e "${GREEN}安装完成！${NC}"
    fi
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  完成！✓${NC}"
echo -e "${GREEN}========================================${NC}"
