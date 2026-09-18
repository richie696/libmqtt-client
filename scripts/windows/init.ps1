# 初始化 libmqtt-client 的构建环境（Windows PowerShell）。

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "未找到 $Name"
    }
}

Write-Host "========== 初始化 libmqtt-client ==========" -ForegroundColor Cyan
Require-Command "git"
Require-Command "cmake"

if (-not (Get-Command "cl.exe" -ErrorAction SilentlyContinue) -and
    -not (Get-Command "g++" -ErrorAction SilentlyContinue) -and
    -not (Get-Command "clang++" -ErrorAction SilentlyContinue)) {
    throw "未找到支持 C++17 的编译器"
}

if (-not (Test-Path (Join-Path $ProjectRoot ".git"))) {
    throw "$ProjectRoot 不是 Git 工作区"
}

Write-Host (git --version)
Write-Host ((cmake --version | Select-Object -First 1))

Push-Location $ProjectRoot
try {
    git submodule sync --recursive
    if ($LASTEXITCODE -ne 0) { throw "git submodule sync 失败" }
    git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "git submodule update 失败" }
} finally {
    Pop-Location
}

if (-not (Test-Path (Join-Path $ProjectRoot "third_party\wolfmqtt\wolfmqtt\mqtt_client.h"))) {
    throw "wolfMQTT submodule 不完整"
}
if (-not (Test-Path (Join-Path $ProjectRoot "third_party\wolfssl\wolfssl\ssl.h"))) {
    throw "wolfSSL submodule 不完整"
}

Write-Host "初始化完成；wolfMQTT 与 wolfSSL 将由主工程 CMake 统一构建。" -ForegroundColor Green
Write-Host "下一步: .\scripts\windows\build.ps1"
