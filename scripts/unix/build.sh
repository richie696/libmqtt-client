#!/usr/bin/env bash
# libmqtt-client 统一构建脚本（Linux/macOS）。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_TYPE="Release"
BUILD_DIR="build"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
CLEAN=false
VERBOSE=false
INSTALL=false
INSTALL_PREFIX=""
MACOS_ARCH=""
TEST_TYPE=""
MENU=false
CMAKE_BIN="${CMAKE_COMMAND:-}"
CTEST_BIN="${CTEST_COMMAND:-}"

usage() {
    cat <<'EOF'
用法: ./scripts/unix/build.sh [选项]

  -h, --help                 显示帮助
  -t, --type TYPE            Debug|Release|RelWithDebInfo|MinSizeRel
  -d, --dir DIR              构建目录，默认 build
  -c, --clean                配置前清理指定构建目录
  -j, --jobs N               并行任务数
  -v, --verbose              显示详细编译命令
  -i, --install              构建后安装
      --prefix DIR           安装前缀，并自动启用 --install
      --macos-arch ARCH      arm64|x86_64|universal
      --test TYPE            unit|integration|all
      --menu                 显示交互菜单

集成测试通过以下环境变量指定服务器：
  MQTT_TEST_HOST, MQTT_TEST_TCP_PORT, MQTT_TEST_TLS_PORT,
  MQTT_TEST_CA_CERT, MQTT_TEST_ENABLE_TLS
EOF
}

die() {
    printf '错误: %s\n' "$*" >&2
    exit 1
}

resolve_build_dir() {
    if [[ "${BUILD_DIR}" = /* ]]; then
        RESOLVED_BUILD_DIR="${BUILD_DIR}"
    else
        RESOLVED_BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR}"
    fi
}

check_dependencies() {
    if [ -z "${CMAKE_BIN}" ]; then
        if command -v cmake >/dev/null 2>&1; then
            CMAKE_BIN="$(command -v cmake)"
        else
            local candidate
            for candidate in \
                /Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake \
                /Applications/CLion.app/Contents/bin/cmake/mac/x64/bin/cmake; do
                if [ -x "${candidate}" ]; then
                    CMAKE_BIN="${candidate}"
                    break
                fi
            done
        fi
    fi
    [ -n "${CMAKE_BIN}" ] && [ -x "${CMAKE_BIN}" ] || \
        die "未找到 CMake 3.15 或更高版本（可通过 CMAKE_COMMAND 指定）"

    if [ -z "${CTEST_BIN}" ]; then
        CTEST_BIN="$(dirname "${CMAKE_BIN}")/ctest"
        if [ ! -x "${CTEST_BIN}" ] && command -v ctest >/dev/null 2>&1; then
            CTEST_BIN="$(command -v ctest)"
        fi
    fi
    [ -x "${CTEST_BIN}" ] || die "未找到与 CMake 配套的 CTest"
    if ! command -v c++ >/dev/null 2>&1 && \
       ! command -v g++ >/dev/null 2>&1 && \
       ! command -v clang++ >/dev/null 2>&1; then
        die "未找到支持 C++17 的编译器"
    fi
    command -v git >/dev/null 2>&1 || die "未找到 Git"
}

init_submodules() {
    if [ ! -f "${PROJECT_ROOT}/third_party/wolfmqtt/wolfmqtt/mqtt_client.h" ] || \
       [ ! -f "${PROJECT_ROOT}/third_party/wolfssl/wolfssl/ssl.h" ]; then
        printf '初始化 wolfMQTT 与 wolfSSL submodule...\n'
        git -C "${PROJECT_ROOT}" submodule update --init --recursive
    fi
}

configure_and_build() {
    resolve_build_dir
    check_dependencies
    init_submodules

    case "${BUILD_TYPE}" in
        Debug|Release|RelWithDebInfo|MinSizeRel) ;;
        *) die "不支持的构建类型: ${BUILD_TYPE}" ;;
    esac
    [[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "并行任务数必须是正整数"

    if [ "${CLEAN}" = true ] && [ -d "${RESOLVED_BUILD_DIR}" ]; then
        [ "${RESOLVED_BUILD_DIR}" != "${PROJECT_ROOT}" ] || die "拒绝清理项目根目录"
        [ "${RESOLVED_BUILD_DIR}" != "/" ] || die "拒绝清理根目录"
        rm -rf "${RESOLVED_BUILD_DIR}"
    fi

    local enable_testing=OFF
    local enable_integration=OFF
    if [ -n "${TEST_TYPE}" ]; then
        enable_testing=ON
    fi
    if [ "${TEST_TYPE}" = integration ] || [ "${TEST_TYPE}" = all ]; then
        enable_integration=ON
    fi

    local cmake_args=(
        -S "${PROJECT_ROOT}"
        -B "${RESOLVED_BUILD_DIR}"
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DENABLE_TESTING=${enable_testing}"
        "-DENABLE_INTEGRATION_TESTS=${enable_integration}"
    )

    if [ -n "${MACOS_ARCH}" ]; then
        case "${MACOS_ARCH}" in
            arm64|x86_64) cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=${MACOS_ARCH}") ;;
            universal) cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64") ;;
            *) die "不支持的 macOS 架构: ${MACOS_ARCH}" ;;
        esac
    fi

    printf '配置: %s (%s)\n' "${RESOLVED_BUILD_DIR}" "${BUILD_TYPE}"
    "${CMAKE_BIN}" "${cmake_args[@]}"

    local build_args=(--build "${RESOLVED_BUILD_DIR}" --config "${BUILD_TYPE}" --parallel "${JOBS}")
    if [ "${VERBOSE}" = true ]; then
        build_args+=(--verbose)
    fi
    "${CMAKE_BIN}" "${build_args[@]}"

    case "${TEST_TYPE}" in
        "") ;;
        unit)
            "${CTEST_BIN}" --test-dir "${RESOLVED_BUILD_DIR}" -C "${BUILD_TYPE}" -LE integration --output-on-failure
            ;;
        integration)
            "${CTEST_BIN}" --test-dir "${RESOLVED_BUILD_DIR}" -C "${BUILD_TYPE}" -L integration --output-on-failure
            ;;
        all)
            "${CTEST_BIN}" --test-dir "${RESOLVED_BUILD_DIR}" -C "${BUILD_TYPE}" --output-on-failure
            ;;
        *) die "测试类型必须是 unit、integration 或 all" ;;
    esac

    if [ "${INSTALL}" = true ]; then
        if [ -z "${INSTALL_PREFIX}" ]; then
            INSTALL_PREFIX="${RESOLVED_BUILD_DIR}/install"
        fi
        "${CMAKE_BIN}" --install "${RESOLVED_BUILD_DIR}" --config "${BUILD_TYPE}" --prefix "${INSTALL_PREFIX}"
        printf '已安装到: %s\n' "${INSTALL_PREFIX}"
    fi
}

show_menu() {
    printf '1. 构建项目\n2. 构建并运行单元测试\n3. 构建并运行集成测试\n4. 构建并运行全部测试\n5. 清理后构建\n6. 退出\n'
    read -r -p '请选择 [1-6]: ' choice
    case "${choice}" in
        1) ;;
        2) TEST_TYPE=unit ;;
        3) TEST_TYPE=integration ;;
        4) TEST_TYPE=all ;;
        5) CLEAN=true ;;
        6) exit 0 ;;
        *) die "无效选项" ;;
    esac
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        -t|--type) [ "$#" -ge 2 ] || die "$1 缺少参数"; BUILD_TYPE="$2"; shift 2 ;;
        -d|--dir) [ "$#" -ge 2 ] || die "$1 缺少参数"; BUILD_DIR="$2"; shift 2 ;;
        -c|--clean) CLEAN=true; shift ;;
        -j|--jobs) [ "$#" -ge 2 ] || die "$1 缺少参数"; JOBS="$2"; shift 2 ;;
        -v|--verbose) VERBOSE=true; shift ;;
        -i|--install) INSTALL=true; shift ;;
        --prefix) [ "$#" -ge 2 ] || die "$1 缺少参数"; INSTALL_PREFIX="$2"; INSTALL=true; shift 2 ;;
        --macos-arch) [ "$#" -ge 2 ] || die "$1 缺少参数"; MACOS_ARCH="$2"; shift 2 ;;
        --test) [ "$#" -ge 2 ] || die "$1 缺少参数"; TEST_TYPE="$2"; shift 2 ;;
        --menu) MENU=true; shift ;;
        *) die "未知选项: $1" ;;
    esac
done

if [ "${MENU}" = true ]; then
    show_menu
fi

configure_and_build
