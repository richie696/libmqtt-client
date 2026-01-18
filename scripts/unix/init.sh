#!/bin/bash
# MQTT客户端库 - 初始化脚本
# 用于初始化项目依赖（Git Submodules、第三方库构建等）
# 在首次克隆项目或构建前运行此脚本

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
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WOLFMQTT_DIR="${PROJECT_ROOT}/third_party/wolfmqtt"
WOLFMQTT_INSTALL_DIR="${WOLFMQTT_DIR}/install"

# 检测平台
detect_platform() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        PLATFORM="macOS"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        PLATFORM="Linux"
    else
        PLATFORM="Unknown"
    fi
}

# 打印标题
print_header() {
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}  MQTT 客户端库 - 初始化脚本${NC}"
    echo -e "${CYAN}========================================${NC}"
    echo ""
}

# 检查基本依赖
check_basic_dependencies() {
    echo -e "${BLUE}========== 检查基本依赖 ==========${NC}"
    
    DEPENDENCIES_OK=true
    MISSING_DEPS=()
    
    # 检查Git
    echo -n "检查 Git... "
    if ! command -v git &> /dev/null; then
        echo -e "${RED}✗ 未找到${NC}"
        DEPENDENCIES_OK=false
        MISSING_DEPS+=("Git")
    else
        GIT_VERSION=$(git --version | head -n 1)
        echo -e "${GREEN}✓ $GIT_VERSION${NC}"
    fi
    
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
            MISSING_DEPS+=("Clang (Xcode Command Line Tools)")
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
        echo -e "${YELLOW}安装方法:${NC}"
        if [[ "$PLATFORM" == "macOS" ]]; then
            echo -e "  ${CYAN}brew install cmake${NC}"
            echo -e "  ${CYAN}xcode-select --install  # 安装Xcode Command Line Tools${NC}"
        elif [[ "$PLATFORM" == "Linux" ]]; then
            echo -e "  ${CYAN}sudo apt-get update${NC}"
            echo -e "  ${CYAN}sudo apt-get install build-essential cmake${NC}"
        fi
        echo ""
        exit 1
    fi
    
    echo -e "${GREEN}所有基本依赖检查通过！✓${NC}"
    echo ""
}

# 初始化 Git Submodules
init_submodules() {
    echo -e "${BLUE}========== 初始化 Git Submodules ==========${NC}"
    
    # 检查是否在Git仓库中
    if [ ! -d "${PROJECT_ROOT}/.git" ]; then
        echo -e "${YELLOW}警告: 当前目录不是Git仓库${NC}"
        echo -e "${YELLOW}跳过 Git Submodule 初始化${NC}"
        echo ""
        return 0
    fi
    
    # 检查 .gitmodules 文件是否存在
    if [ ! -f "${PROJECT_ROOT}/.gitmodules" ]; then
        echo -e "${YELLOW}未找到 .gitmodules 文件，跳过 Submodule 初始化${NC}"
        echo ""
        return 0
    fi
    
    # 检查 wolfMQTT submodule 是否已初始化
    if [ ! -d "$WOLFMQTT_DIR" ] || [ ! -f "$WOLFMQTT_DIR/.git" ]; then
        echo -e "${YELLOW}检测到 wolfMQTT submodule 未初始化${NC}"
        echo -e "${CYAN}正在初始化 Git Submodules...${NC}"
        
        cd "$PROJECT_ROOT"
        git submodule update --init --recursive
        
        if [ -d "$WOLFMQTT_DIR" ] && [ -f "$WOLFMQTT_DIR/.git" ]; then
            echo -e "${GREEN}✓ Git Submodules 初始化完成${NC}"
        else
            echo -e "${RED}✗ Git Submodules 初始化失败${NC}"
            echo -e "${YELLOW}请手动运行: git submodule update --init --recursive${NC}"
            exit 1
        fi
    else
        echo -e "${GREEN}✓ Git Submodules 已初始化${NC}"
        
        # 检查是否需要更新
        cd "$WOLFMQTT_DIR"
        if git fetch origin > /dev/null 2>&1 && [ "$(git rev-parse HEAD)" != "$(git rev-parse origin/master 2>/dev/null || echo '')" ]; then
            echo -e "${YELLOW}检测到 wolfMQTT 有新版本可用${NC}"
            echo -e "${YELLOW}如需更新，请运行: cd third_party/wolfmqtt && git pull origin master${NC}"
        fi
        cd "$PROJECT_ROOT"
    fi
    
    echo ""
}

# 检查 wolfMQTT 是否已构建
check_wolfmqtt_built() {
    echo -e "${BLUE}========== 检查 wolfMQTT 构建状态 ==========${NC}"
    
    if [ ! -d "$WOLFMQTT_DIR" ]; then
        echo -e "${RED}错误: wolfMQTT 目录不存在: $WOLFMQTT_DIR${NC}"
        echo -e "${YELLOW}请先运行 Git Submodule 初始化${NC}"
        exit 1
    fi
    
    # 检查是否已安装（有 install 目录和库文件）
    if [ -d "$WOLFMQTT_INSTALL_DIR/lib" ] && [ -n "$(find "$WOLFMQTT_INSTALL_DIR/lib" -name "libwolfmqtt.*" 2>/dev/null)" ]; then
        echo -e "${GREEN}✓ wolfMQTT 已构建并安装${NC}"
        echo -e "${CYAN}  安装路径: $WOLFMQTT_INSTALL_DIR${NC}"
        echo ""
        return 0
    else
        echo -e "${YELLOW}⚠ wolfMQTT 尚未构建${NC}"
        echo ""
        return 1
    fi
}

# 构建 wolfMQTT
build_wolfmqtt() {
    echo -e "${BLUE}========== 构建 wolfMQTT ==========${NC}"
    
    if [ ! -d "$WOLFMQTT_DIR" ]; then
        echo -e "${RED}错误: wolfMQTT 目录不存在: $WOLFMQTT_DIR${NC}"
        exit 1
    fi
    
    cd "$WOLFMQTT_DIR"
    
    # 检测 wolfSSL 路径
    WOLFSSL_PATH=""
    if [[ "$PLATFORM" == "macOS" ]]; then
        if [ -d "/opt/homebrew/opt/wolfssl" ]; then
            WOLFSSL_PATH="/opt/homebrew/opt/wolfssl"
        elif [ -d "/usr/local/opt/wolfssl" ]; then
            WOLFSSL_PATH="/usr/local/opt/wolfssl"
        fi
    fi
    
    # 创建构建目录
    WOLFMQTT_BUILD_DIR="${WOLFMQTT_DIR}/build_cmake"
    mkdir -p "$WOLFMQTT_BUILD_DIR"
    cd "$WOLFMQTT_BUILD_DIR"
    
    # 配置 CMake
    echo -e "${CYAN}配置 wolfMQTT (启用 TLS 和 MQTT 5.0)...${NC}"
    CMAKE_ARGS=(
        ".."
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_INSTALL_PREFIX=$WOLFMQTT_INSTALL_DIR"
        "-DWOLFMQTT_TLS=yes"
        "-DWOLFMQTT_V5=yes"
    )
    
    if [ -n "$WOLFSSL_PATH" ]; then
        CMAKE_ARGS+=("-DWITH_WOLFSSL=$WOLFSSL_PATH")
        echo -e "${GREEN}使用 wolfSSL: $WOLFSSL_PATH${NC}"
    else
        echo -e "${YELLOW}警告: 未找到 wolfSSL，TLS 功能可能不可用${NC}"
    fi
    
    cmake "${CMAKE_ARGS[@]}"
    
    # 构建
    echo -e "${CYAN}构建 wolfMQTT...${NC}"
    JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    cmake --build . --config Release -j "$JOBS"
    
    # 安装
    echo -e "${CYAN}安装 wolfMQTT...${NC}"
    cmake --install . --config Release
    
    cd "$PROJECT_ROOT"
    
    # 验证安装
    if [ -d "$WOLFMQTT_INSTALL_DIR/lib" ] && [ -n "$(find "$WOLFMQTT_INSTALL_DIR/lib" -name "libwolfmqtt.*" 2>/dev/null)" ]; then
        echo -e "${GREEN}✓ wolfMQTT 构建并安装成功${NC}"
        echo -e "${CYAN}  安装路径: $WOLFMQTT_INSTALL_DIR${NC}"
    else
        echo -e "${RED}✗ wolfMQTT 构建失败${NC}"
        exit 1
    fi
    
    echo ""
}

# 主函数
main() {
    print_header
    detect_platform
    
    echo -e "${CYAN}平台: $PLATFORM${NC}"
    echo ""
    
    # 1. 检查基本依赖
    check_basic_dependencies
    
    # 2. 初始化 Git Submodules
    init_submodules
    
    # 3. 检查并构建 wolfMQTT
    if ! check_wolfmqtt_built; then
        echo -e "${YELLOW}需要构建 wolfMQTT，是否继续? (y/n)${NC}"
        read -r response
        if [[ "$response" =~ ^[Yy]$ ]] || [ -z "$response" ]; then
            build_wolfmqtt
        else
            echo -e "${YELLOW}跳过 wolfMQTT 构建${NC}"
            echo -e "${YELLOW}后续可以运行: ./scripts/build.sh --menu (选择选项2)${NC}"
            echo ""
        fi
    fi
    
    # 完成
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  初始化完成！${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo -e "${CYAN}下一步:${NC}"
    echo -e "  1. 构建项目: ${GREEN}./scripts/build.sh${NC}"
    echo -e "  2. 或使用菜单: ${GREEN}./scripts/build.sh --menu${NC}"
    echo ""
}

# 运行主函数
main "$@"
