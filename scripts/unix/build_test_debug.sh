#!/bin/bash
# MQTT客户端库 - Debug模式测试构建脚本 (Linux/macOS)
# 用于快速构建 Debug 版本的集成测试程序

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 获取脚本和项目目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 脚本在 scripts/unix/ 目录下，需要向上两级才能到达项目根目录
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# 默认配置
BUILD_TYPE="Debug"
TEST_TARGET=""
TEST_SOURCE=""
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# 已知的测试目标列表
KNOWN_TARGETS=(
    "test_core"
    "test_config"
    "test_connection"
    "test_message"
    "test_subscription"
    "test_monitor"
    "test_reconnect"
    "test_client"
    "test_mqtt311_tcp_tls"
    "test_mqtt5_tcp_tls"
)

# 打印标题
print_header() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}  MQTT 客户端库 - Debug 测试构建${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
}

# 列出所有可用的测试目标
list_targets() {
    echo -e "${CYAN}可用的测试目标:${NC}"
    for target in "${KNOWN_TARGETS[@]}"; do
        echo -e "  - ${GREEN}${target}${NC}"
    done
    echo ""
}

# 从源文件路径推导目标名称
derive_target_from_source() {
    local source_path="$1"
    # 移除路径前缀和后缀
    local basename=$(basename "$source_path" .cpp)
    # 如果以 test_ 开头，直接返回；否则添加 test_ 前缀
    if [[ "$basename" == test_* ]]; then
        echo "$basename"
    else
        echo "test_${basename}"
    fi
}

# 帮助信息
show_help() {
    cat << EOF
MQTT客户端库 - Debug模式测试构建脚本

用法: $0 [选项] [测试目标或源文件]

选项:
    -h, --help          显示此帮助信息
    -t, --target TARGET 测试目标名称（CMake target）
    -f, --file FILE     测试源文件路径（会自动推导目标名称）
    -l, --list          列出所有可用的测试目标
    -j, --jobs N        并行编译任务数
                        默认: 自动检测CPU核心数
    -d, --dir DIR       构建目录
                        默认: build

示例:
    $0                                    # 交互式选择测试目标
    $0 -t test_mqtt311_tcp_tls          # 指定目标名称
    $0 -f tests/integration/test_mqtt311_tcp_tls.cpp  # 指定源文件
    $0 test_mqtt5_tcp_tls                # 直接指定目标（位置参数）
    $0 -l                                 # 列出所有可用目标
    $0 -j 8                              # 使用8个并行任务

EOF
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -t|--target)
            TEST_TARGET="$2"
            shift 2
            ;;
        -f|--file)
            TEST_SOURCE="$2"
            shift 2
            ;;
        -l|--list)
            list_targets
            exit 0
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -d|--dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -*)
            echo -e "${RED}错误: 未知选项: $1${NC}" >&2
            show_help
            exit 1
            ;;
        *)
            # 位置参数：可能是目标名称或源文件路径
            if [[ -z "$TEST_TARGET" && -z "$TEST_SOURCE" ]]; then
                if [[ "$1" == *.cpp ]]; then
                    TEST_SOURCE="$1"
                else
                    TEST_TARGET="$1"
                fi
            else
                echo -e "${RED}错误: 只能指定一个测试目标或源文件${NC}" >&2
                show_help
                exit 1
            fi
            shift
            ;;
    esac
done

# 如果指定了源文件，推导目标名称
if [[ -n "$TEST_SOURCE" ]]; then
    TEST_TARGET=$(derive_target_from_source "$TEST_SOURCE")
    echo -e "${BLUE}从源文件推导目标: ${TEST_SOURCE} -> ${TEST_TARGET}${NC}"
fi

# 如果没有指定目标，交互式选择
if [[ -z "$TEST_TARGET" ]]; then
    print_header
    list_targets
    echo -e "${YELLOW}请选择要构建的测试目标（输入数字或名称）:${NC}"
    echo -n "> "
    read -r user_input
    
    # 检查是否是数字
    if [[ "$user_input" =~ ^[0-9]+$ ]]; then
        index=$((user_input - 1))
        if [[ $index -ge 0 && $index -lt ${#KNOWN_TARGETS[@]} ]]; then
            TEST_TARGET="${KNOWN_TARGETS[$index]}"
        else
            echo -e "${RED}错误: 无效的选择${NC}"
            exit 1
        fi
    else
        TEST_TARGET="$user_input"
    fi
fi

# 验证测试目标（给出警告但不阻止）
if [[ ! " ${KNOWN_TARGETS[@]} " =~ " ${TEST_TARGET} " ]]; then
    echo -e "${YELLOW}警告: '${TEST_TARGET}' 不在已知目标列表中${NC}"
    echo -e "${YELLOW}将尝试构建该目标（如果 CMake 中存在）...${NC}"
fi

print_header

# 切换到项目根目录
cd "$PROJECT_ROOT"

echo -e "${BLUE}项目根目录: ${PROJECT_ROOT}${NC}"
echo -e "${BLUE}构建目录: ${BUILD_DIR}${NC}"
echo -e "${BLUE}构建类型: ${BUILD_TYPE}${NC}"
echo -e "${BLUE}测试目标: ${TEST_TARGET}${NC}"
echo -e "${BLUE}并行任务: ${JOBS}${NC}"
echo ""

# 配置 CMake
echo -e "${CYAN}========== 配置 CMake ==========${NC}"
cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DENABLE_TESTING=ON \
    -DENABLE_INTEGRATION_TESTS=ON

if [ $? -ne 0 ]; then
    echo -e "${RED}✗ CMake 配置失败${NC}"
    exit 1
fi

echo -e "${GREEN}✓ CMake 配置成功${NC}"
echo ""

# 构建测试目标
echo -e "${CYAN}========== 构建测试目标 ==========${NC}"
cmake --build "${BUILD_DIR}" --target "${TEST_TARGET}" -j "${JOBS}"

if [ $? -ne 0 ]; then
    echo -e "${RED}✗ 构建失败${NC}"
    exit 1
fi

echo -e "${GREEN}✓ 构建成功${NC}"
echo ""

# 显示可执行文件路径
EXECUTABLE="${BUILD_DIR}/bin/${TEST_TARGET}"
if [ -f "${EXECUTABLE}" ]; then
    echo -e "${GREEN}可执行文件: ${EXECUTABLE}${NC}"
    echo ""
    echo -e "${CYAN}运行测试:${NC}"
    echo -e "  ${EXECUTABLE}"
    echo ""
else
    echo -e "${YELLOW}警告: 未找到可执行文件: ${EXECUTABLE}${NC}"
    echo -e "${YELLOW}请检查构建输出${NC}"
fi
