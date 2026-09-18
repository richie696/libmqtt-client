#!/usr/bin/env bash
# 初始化 libmqtt-client 的构建环境（Linux/macOS）。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CMAKE_BIN="${CMAKE_COMMAND:-}"

die() {
    printf '错误: %s\n' "$*" >&2
    exit 1
}

printf '========== 初始化 libmqtt-client ==========\n'

command -v git >/dev/null 2>&1 || die "未找到 Git"
if [ -z "${CMAKE_BIN}" ]; then
    if command -v cmake >/dev/null 2>&1; then
        CMAKE_BIN="$(command -v cmake)"
    else
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

if ! command -v c++ >/dev/null 2>&1 && \
   ! command -v g++ >/dev/null 2>&1 && \
   ! command -v clang++ >/dev/null 2>&1; then
    die "未找到支持 C++17 的编译器"
fi

if [ ! -d "${PROJECT_ROOT}/.git" ]; then
    die "${PROJECT_ROOT} 不是 Git 工作区，无法初始化 submodule"
fi

printf 'Git:   %s\n' "$(git --version)"
printf 'CMake: %s\n' "$("${CMAKE_BIN}" --version | head -n 1)"

printf '初始化固定版本的 wolfMQTT 与 wolfSSL...\n'
git -C "${PROJECT_ROOT}" submodule sync --recursive
git -C "${PROJECT_ROOT}" submodule update --init --recursive

test -f "${PROJECT_ROOT}/third_party/wolfmqtt/wolfmqtt/mqtt_client.h" || \
    die "wolfMQTT submodule 不完整"
test -f "${PROJECT_ROOT}/third_party/wolfssl/wolfssl/ssl.h" || \
    die "wolfSSL submodule 不完整"

printf 'wolfMQTT: %s\n' "$(git -C "${PROJECT_ROOT}/third_party/wolfmqtt" describe --tags --always)"
printf 'wolfSSL:  %s\n' "$(git -C "${PROJECT_ROOT}/third_party/wolfssl" describe --tags --always)"
printf '\n初始化完成。依赖会由主工程 CMake 统一构建，无需单独安装 wolfSSL。\n'
printf '下一步: ./scripts/unix/build.sh\n'
