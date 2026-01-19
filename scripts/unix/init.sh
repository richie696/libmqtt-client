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
# 脚本在 scripts/unix/ 目录下，需要向上两级才能到达项目根目录
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# 从 .gitmodules 读取 submodule 路径的辅助函数
get_submodule_paths() {
    local gitmodules_file="${PROJECT_ROOT}/.gitmodules"
    if [ ! -f "$gitmodules_file" ]; then
        return 1
    fi
    grep "^[[:space:]]*path[[:space:]]*=" "$gitmodules_file" | sed 's/^[[:space:]]*path[[:space:]]*=[[:space:]]*//;s/[[:space:]]*$//'
}

# 从 .gitmodules 读取 submodule URL 的辅助函数
get_submodule_url() {
    local gitmodules_file="${PROJECT_ROOT}/.gitmodules"
    local submodule_path="$1"
    if [ ! -f "$gitmodules_file" ] || [ -z "$submodule_path" ]; then
        return 1
    fi
    # 使用 awk 找到包含该 path 的 submodule 块，然后读取 url
    awk -v target_path="$submodule_path" '
        /^\[submodule/ { in_block=0; found_path=0 }
        /^[[:space:]]*path[[:space:]]*=/ {
            path_value = $0
            gsub(/^[[:space:]]*path[[:space:]]*=[[:space:]]*/, "", path_value)
            gsub(/[[:space:]]*$/, "", path_value)
            if (path_value == target_path) {
                in_block=1
                found_path=1
            }
        }
        in_block && found_path && /^[[:space:]]*url[[:space:]]*=/ {
            url_value = $0
            gsub(/^[[:space:]]*url[[:space:]]*=[[:space:]]*/, "", url_value)
            gsub(/[[:space:]]*$/, "", url_value)
            print url_value
            exit 0
        }
    ' "$gitmodules_file"
}

# 检查 submodule 是否在 git 索引中注册
is_submodule_registered() {
    local submodule_path="$1"
    if [ -z "$submodule_path" ]; then
        return 1
    fi
    # 检查 git ls-tree 中是否有该路径的 submodule 条目
    git ls-tree HEAD "$submodule_path" 2>/dev/null | grep -q "^160000" && return 0
    return 1
}

# 获取第一个 submodule 的完整路径（用于兼容性，如果只有一个 submodule）
get_first_submodule_dir() {
    local first_path=$(get_submodule_paths | head -n 1)
    if [ -n "$first_path" ]; then
        echo "${PROJECT_ROOT}/${first_path}"
    fi
}

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
    
    # 检查是否在Git仓库中（支持 .git 目录或 .git 文件，以及通过 .gitmodules 判断）
    if [ ! -d "${PROJECT_ROOT}/.git" ] && [ ! -f "${PROJECT_ROOT}/.git" ] && [ ! -f "${PROJECT_ROOT}/.gitmodules" ]; then
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
    
    # 检查所有 submodule 是否已初始化
    # 注意：submodule 的 .git 可能是一个文件（指向主仓库的 .git/modules）或目录
    local submodules_need_init=false
    local submodule_paths=$(get_submodule_paths)
    
    if [ -z "$submodule_paths" ]; then
        echo -e "${YELLOW}未找到任何 submodule 配置${NC}"
        echo ""
        return 0
    fi
    
    # 确保在项目根目录执行
    cd "$PROJECT_ROOT" || {
        echo -e "${RED}✗ 无法切换到项目根目录: $PROJECT_ROOT${NC}"
        exit 1
    }
    
    # 检查每个 submodule，如果不存在则自动添加
    local submodules_need_add=false
    while IFS= read -r submodule_path; do
        local submodule_dir="${PROJECT_ROOT}/${submodule_path}"
        
        # 检查 submodule 目录是否存在且已初始化
        if [ ! -d "$submodule_dir" ] || ([ -d "$submodule_dir" ] && [ ! -f "$submodule_dir/.git" ] && [ ! -d "$submodule_dir/.git" ]); then
            # 检查是否在 git 索引中注册
            if ! is_submodule_registered "$submodule_path"; then
                echo -e "${YELLOW}检测到 submodule 未在 Git 索引中注册: $submodule_path${NC}"
                submodules_need_add=true
                
                # 获取 submodule URL
                local submodule_url=$(get_submodule_url "$submodule_path")
                if [ -z "$submodule_url" ]; then
                    echo -e "${RED}✗ 无法从 .gitmodules 获取 $submodule_path 的 URL${NC}"
                    echo -e "${YELLOW}请检查 .gitmodules 文件配置${NC}"
                    exit 1
                fi
                
                # 检查目录是否存在但不是正确的 submodule
                if [ -d "$submodule_dir" ]; then
                    # 检查是否是独立的 git 仓库（有 .git 目录或文件）
                    if [ -f "$submodule_dir/.git" ] || [ -d "$submodule_dir/.git" ]; then
                        echo -e "${YELLOW}检测到目录已存在但不是正确的 submodule: $submodule_path${NC}"
                        echo -e "${CYAN}正在清理不完整的 submodule 目录...${NC}"
                        
                        # 询问用户是否要删除（在非交互模式下自动删除）
                        if [ -t 0 ]; then
                            echo -e "${YELLOW}是否删除现有目录并重新添加? (y/n) [y]${NC}"
                            read -r response
                            if [[ ! "$response" =~ ^[Yy]$ ]] && [ -n "$response" ]; then
                                echo -e "${YELLOW}跳过 submodule 添加${NC}"
                                continue
                            fi
                        fi
                        
                        # 删除现有目录
                        echo -e "${CYAN}删除目录: $submodule_dir${NC}"
                        rm -rf "$submodule_dir" || {
                            echo -e "${RED}✗ 无法删除目录: $submodule_dir${NC}"
                            echo -e "${YELLOW}请手动删除后重试${NC}"
                            exit 1
                        }
                        echo -e "${GREEN}✓ 目录已删除${NC}"
                    else
                        # 目录存在但没有 .git，可能是空目录或其他文件
                        echo -e "${YELLOW}检测到目录存在但不是 git 仓库: $submodule_path${NC}"
                        echo -e "${CYAN}正在清理目录...${NC}"
                        rm -rf "$submodule_dir" || {
                            echo -e "${RED}✗ 无法删除目录: $submodule_dir${NC}"
                            exit 1
                        }
                    fi
                fi
                
                # 创建父目录
                local parent_dir="$(dirname "$submodule_dir")"
                if [ "$parent_dir" != "$PROJECT_ROOT" ] && [ ! -d "$parent_dir" ]; then
                    echo -e "${CYAN}创建父目录: $parent_dir${NC}"
                    mkdir -p "$parent_dir" || {
                        echo -e "${RED}✗ 无法创建目录: $parent_dir${NC}"
                        exit 1
                    }
                fi
                
                # 使用 git submodule add 添加 submodule（如果目录存在但不是 submodule，使用 --force）
                echo -e "${CYAN}正在添加 submodule: $submodule_path${NC}"
                echo -e "${CYAN}  URL: $submodule_url${NC}"
                
                # 尝试添加 submodule
                if git submodule add "$submodule_url" "$submodule_path" 2>&1; then
                    echo -e "${GREEN}✓ Submodule 添加成功: $submodule_path${NC}"
                else
                    # 如果失败，尝试使用 --force 选项
                    echo -e "${YELLOW}常规添加失败，尝试使用 --force 选项...${NC}"
                    if git submodule add --force "$submodule_url" "$submodule_path" 2>&1; then
                        echo -e "${GREEN}✓ Submodule 添加成功 (使用 --force): $submodule_path${NC}"
                    else
                        echo -e "${RED}✗ Submodule 添加失败: $submodule_path${NC}"
                        echo -e "${YELLOW}错误信息:${NC}"
                        git submodule add "$submodule_url" "$submodule_path" 2>&1 | head -5
                        echo -e "${YELLOW}请检查:${NC}"
                        echo -e "${YELLOW}  1. 网络连接是否正常${NC}"
                        echo -e "${YELLOW}  2. Git 配置是否正确${NC}"
                        echo -e "${YELLOW}  3. 目录权限是否正确${NC}"
                        echo -e "${YELLOW}  4. 如果目录已存在，请手动删除后重试${NC}"
                        exit 1
                    fi
                fi
            else
                submodules_need_init=true
            fi
        fi
    done <<< "$submodule_paths"
    
    # 如果有 submodule 需要初始化（已注册但未下载）
    if [ "$submodules_need_init" = true ]; then
        echo -e "${YELLOW}检测到 submodule 未初始化${NC}"
        echo -e "${CYAN}正在初始化 Git Submodules...${NC}"
        
        # 显示调试信息
        echo -e "${CYAN}项目根目录: $PROJECT_ROOT${NC}"
        
        # 从 .gitmodules 文件中读取所有 submodule 路径，并创建对应的父目录
        GITMODULES_FILE="${PROJECT_ROOT}/.gitmodules"
        if [ -f "$GITMODULES_FILE" ]; then
            echo -e "${CYAN}检查并创建 submodule 父目录...${NC}"
            # 提取所有 path = 行，获取路径的父目录
            while IFS='=' read -r key path; do
                # 去除空格和引号
                path=$(echo "$path" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
                # 获取父目录
                parent_dir="${PROJECT_ROOT}/$(dirname "$path")"
                
                # 如果父目录不是项目根目录，且不存在，则创建
                if [ "$parent_dir" != "$PROJECT_ROOT" ] && [ ! -d "$parent_dir" ]; then
                    echo -e "${YELLOW}创建目录: $parent_dir${NC}"
                    if ! mkdir -p "$parent_dir"; then
                        echo -e "${RED}✗ 无法创建目录: $parent_dir${NC}"
                        exit 1
                    fi
                    echo -e "${GREEN}✓ 目录已创建: $parent_dir${NC}"
                fi
            done < <(grep "^[[:space:]]*path[[:space:]]*=" "$GITMODULES_FILE")
        fi
        
        echo -e "${CYAN}执行: git submodule update --init --recursive${NC}"
        
        # 执行 git submodule 初始化，捕获输出
        GIT_OUTPUT=$(git submodule update --init --recursive 2>&1)
        GIT_EXIT_CODE=$?
        
        # 显示 git 命令的输出（如果有）
        if [ -n "$GIT_OUTPUT" ]; then
            echo "$GIT_OUTPUT"
        fi
        
        # 检查命令执行结果
        if [ $GIT_EXIT_CODE -ne 0 ]; then
            echo -e "${RED}✗ Git Submodules 初始化失败 (退出码: $GIT_EXIT_CODE)${NC}"
            echo -e "${YELLOW}请检查:${NC}"
            echo -e "${YELLOW}  1. 是否在正确的 Git 仓库中${NC}"
            echo -e "${YELLOW}  2. .gitmodules 文件是否正确配置${NC}"
            echo -e "${YELLOW}  3. 网络连接是否正常（需要从远程仓库克隆）${NC}"
            echo -e "${YELLOW}  4. Git 配置是否正确（user.name, user.email）${NC}"
            echo -e "${YELLOW}手动运行: cd $PROJECT_ROOT && git submodule update --init --recursive${NC}"
            exit 1
        fi
    fi
    
    # 如果添加了新的 submodule，需要重新获取路径列表
    if [ "$submodules_need_add" = true ]; then
        submodule_paths=$(get_submodule_paths)
    fi
    
    # 验证所有 submodule 是否已正确初始化
    if [ "$submodules_need_init" = true ] || [ "$submodules_need_add" = true ]; then
        
        # 验证初始化是否成功
        local init_failed=false
        local failed_paths=()
        
        while IFS= read -r submodule_path; do
            local submodule_dir="${PROJECT_ROOT}/${submodule_path}"
            local parent_dir="$(dirname "$submodule_dir")"
            
            # 检查父目录是否存在
            if [ ! -d "$parent_dir" ] && [ "$parent_dir" != "$PROJECT_ROOT" ]; then
                echo -e "${RED}✗ 错误: 父目录不存在: $parent_dir${NC}"
                echo -e "${YELLOW}请检查目录权限${NC}"
                init_failed=true
                failed_paths+=("$submodule_path")
                continue
            fi
            
            # 检查 submodule 目录和 .git 文件/目录
            if [ -d "$submodule_dir" ] && ([ -f "$submodule_dir/.git" ] || [ -d "$submodule_dir/.git" ]); then
                echo -e "${GREEN}✓ Submodule 已初始化: $submodule_path${NC}"
            else
                init_failed=true
                failed_paths+=("$submodule_path")
                echo -e "${RED}✗ Submodule 初始化失败: $submodule_path${NC}"
                echo -e "${YELLOW}预期路径: $submodule_dir${NC}"
                
                if [ -d "$submodule_dir" ]; then
                    echo -e "${YELLOW}目录存在，但 .git 文件/目录缺失${NC}"
                    echo -e "${CYAN}目录内容:${NC}"
                    ls -la "$submodule_dir" 2>/dev/null | head -10 || echo "  无法列出目录内容"
                else
                    echo -e "${YELLOW}目录不存在${NC}"
                fi
            fi
        done <<< "$submodule_paths"
        
        if [ "$init_failed" = true ]; then
            echo -e "${YELLOW}当前工作目录: $(pwd)${NC}"
            echo -e "${YELLOW}项目根目录: $PROJECT_ROOT${NC}"
            echo -e "${YELLOW}请尝试手动运行:${NC}"
            echo -e "${CYAN}  cd $PROJECT_ROOT${NC}"
            echo -e "${CYAN}  git submodule update --init --recursive${NC}"
            echo -e "${YELLOW}如果仍然失败，请检查:${NC}"
            echo -e "${YELLOW}  1. Git 仓库状态: git status${NC}"
            echo -e "${YELLOW}  2. Submodule 状态: git submodule status${NC}"
            echo -e "${YELLOW}  3. .gitmodules 内容是否正确${NC}"
            exit 1
        else
            echo -e "${GREEN}✓ 所有 Git Submodules 初始化完成${NC}"
        fi
    else
        echo -e "${GREEN}✓ Git Submodules 已初始化${NC}"
        
        # 检查是否需要更新
        while IFS= read -r submodule_path; do
            local submodule_dir="${PROJECT_ROOT}/${submodule_path}"
            if [ -d "$submodule_dir" ]; then
                cd "$submodule_dir" || continue
                if git fetch origin > /dev/null 2>&1 && [ "$(git rev-parse HEAD)" != "$(git rev-parse origin/master 2>/dev/null || echo '')" ]; then
                    echo -e "${YELLOW}检测到 $submodule_path 有新版本可用${NC}"
                    echo -e "${YELLOW}如需更新，请运行: cd $submodule_path && git pull origin master${NC}"
                fi
                cd "$PROJECT_ROOT" || exit 1
            fi
        done <<< "$submodule_paths"
    fi
    
    echo ""
}

# 检查 submodule 是否已构建（检查第一个 submodule，用于兼容性）
# 检查 submodule 是否已构建
check_submodule_built() {
    local submodule_path="$1"
    if [ -z "$submodule_path" ]; then
        return 1
    fi
    
    local submodule_dir="${PROJECT_ROOT}/${submodule_path}"
    local submodule_name=$(basename "$submodule_path")
    
    if [ ! -d "$submodule_dir" ]; then
        return 1
    fi
    
    # 检查是否已安装（有 install 目录和库文件）
    local install_dir="${submodule_dir}/install"
    if [ -d "${install_dir}/lib" ] && [ -n "$(find "${install_dir}/lib" -name "lib*.a" -o -name "lib*.so" -o -name "lib*.dylib" 2>/dev/null | head -1)" ]; then
        return 0
    else
        return 1
    fi
}

check_wolfmqtt_built() {
    local submodule_dir=$(get_first_submodule_dir)
    if [ -z "$submodule_dir" ]; then
        echo -e "${YELLOW}未找到任何 submodule，跳过构建检查${NC}"
        echo ""
        return 1
    fi
    
    local submodule_name=$(basename "$submodule_dir")
    echo -e "${BLUE}========== 检查 $submodule_name 构建状态 ==========${NC}"
    
    if [ ! -d "$submodule_dir" ]; then
        echo -e "${RED}错误: $submodule_name 目录不存在: $submodule_dir${NC}"
        echo -e "${YELLOW}请先运行 Git Submodule 初始化${NC}"
        exit 1
    fi
    
    # 检查是否已安装（有 install 目录和库文件）
    local install_dir="${submodule_dir}/install"
    if [ -d "${install_dir}/lib" ] && [ -n "$(find "${install_dir}/lib" -name "lib*.a" -o -name "lib*.so" -o -name "lib*.dylib" 2>/dev/null | head -1)" ]; then
        echo -e "${GREEN}✓ $submodule_name 已构建并安装${NC}"
        echo -e "${CYAN}  安装路径: $install_dir${NC}"
        echo ""
        return 0
    else
        echo -e "${YELLOW}⚠ $submodule_name 尚未构建${NC}"
        echo ""
        return 1
    fi
}

# 构建 submodule（构建第一个 submodule，用于兼容性）
build_wolfmqtt() {
    local submodule_dir=$(get_first_submodule_dir)
    if [ -z "$submodule_dir" ]; then
        echo -e "${RED}错误: 未找到任何 submodule${NC}"
        exit 1
    fi
    
    local submodule_name=$(basename "$submodule_dir")
    echo -e "${BLUE}========== 构建 $submodule_name ==========${NC}"
    
    if [ ! -d "$submodule_dir" ]; then
        echo -e "${RED}错误: $submodule_name 目录不存在: $submodule_dir${NC}"
        exit 1
    fi
    
    cd "$submodule_dir"
    
    # 检测系统安装的 wolfSSL（可选，用于 TLS 支持）
    local WOLFSSL_PATH=""
    if [[ "$PLATFORM" == "macOS" ]]; then
        if [ -d "/opt/homebrew/opt/wolfssl" ]; then
            WOLFSSL_PATH="/opt/homebrew/opt/wolfssl"
        elif [ -d "/usr/local/opt/wolfssl" ]; then
            WOLFSSL_PATH="/usr/local/opt/wolfssl"
        fi
    fi
    if [ -n "$WOLFSSL_PATH" ]; then
        echo -e "${GREEN}使用系统安装的 wolfSSL: $WOLFSSL_PATH${NC}"
    else
        echo -e "${YELLOW}警告: 未找到 wolfSSL，TLS 功能将不可用${NC}"
    fi
    
    # 创建构建目录
    local build_dir="${submodule_dir}/build_cmake"
    mkdir -p "$build_dir"
    cd "$build_dir"
    
    # 配置 CMake
    echo -e "${CYAN}配置 $submodule_name (启用 TLS 和 MQTT 5.0)...${NC}"
    local install_dir="${submodule_dir}/install"
    CMAKE_ARGS=(
        ".."
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_INSTALL_PREFIX=$install_dir"
        "-DWOLFMQTT_TLS=yes"
        "-DWOLFMQTT_V5=yes"
    )
    
    if [ -n "$WOLFSSL_PATH" ]; then
        CMAKE_ARGS+=("-DWITH_WOLFSSL=$WOLFSSL_PATH")
    fi
    
    cmake "${CMAKE_ARGS[@]}"
    
    # 构建
    echo -e "${CYAN}构建 $submodule_name...${NC}"
    JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    cmake --build . --config Release -j "$JOBS"
    
    # 安装
    echo -e "${CYAN}安装 $submodule_name...${NC}"
    cmake --install . --config Release
    
    cd "$PROJECT_ROOT"
    
    # 验证安装
    if [ -d "${install_dir}/lib" ] && [ -n "$(find "${install_dir}/lib" -name "lib*.a" -o -name "lib*.so" -o -name "lib*.dylib" 2>/dev/null | head -1)" ]; then
        echo -e "${GREEN}✓ $submodule_name 构建并安装成功${NC}"
        echo -e "${CYAN}  安装路径: $install_dir${NC}"
    else
        echo -e "${RED}✗ $submodule_name 构建失败${NC}"
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
        local submodule_dir=$(get_first_submodule_dir)
        local submodule_name="submodule"
        if [ -n "$submodule_dir" ]; then
            submodule_name=$(basename "$submodule_dir")
        fi
        echo -e "${YELLOW}需要构建 $submodule_name，是否继续? (y/n)${NC}"
        read -r response
        if [[ "$response" =~ ^[Yy]$ ]] || [ -z "$response" ]; then
            build_wolfmqtt
        else
            echo -e "${YELLOW}跳过 $submodule_name 构建${NC}"
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
