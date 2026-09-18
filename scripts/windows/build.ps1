# libmqtt-client 统一构建脚本（Windows PowerShell）。

param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Type = "Release",
    [string]$Dir = "build",
    [switch]$Clean,
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Verbose,
    [switch]$Install,
    [string]$Prefix = "",
    [ValidateSet("", "unit", "integration", "all")]
    [string]$Test = "",
    [switch]$Help
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

if ($Help) {
    Write-Host @"
用法: .\scripts\windows\build.ps1 [选项]
  -Type TYPE       Debug|Release|RelWithDebInfo|MinSizeRel
  -Dir DIR         构建目录，默认 build
  -Clean           清理后构建
  -Jobs N          并行任务数
  -Verbose         显示详细编译命令
  -Install         安装到构建目录下的 install
  -Prefix DIR      指定安装前缀并自动安装
  -Test TYPE       unit|integration|all
"@
    exit 0
}

if ($Jobs -lt 1) { throw "Jobs 必须是正整数" }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw "未找到 CMake 3.15 或更高版本" }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "未找到 Git" }

$WolfMqttHeader = Join-Path $ProjectRoot "third_party\wolfmqtt\wolfmqtt\mqtt_client.h"
$WolfSslHeader = Join-Path $ProjectRoot "third_party\wolfssl\wolfssl\ssl.h"
if (-not (Test-Path $WolfMqttHeader) -or -not (Test-Path $WolfSslHeader)) {
    Push-Location $ProjectRoot
    try {
        git submodule update --init --recursive
        if ($LASTEXITCODE -ne 0) { throw "Submodule 初始化失败" }
    } finally {
        Pop-Location
    }
}

if ([System.IO.Path]::IsPathRooted($Dir)) {
    $BuildDir = [System.IO.Path]::GetFullPath($Dir)
} else {
    $BuildDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $Dir))
}

if ($Clean -and (Test-Path $BuildDir)) {
    if ($BuildDir -eq $ProjectRoot -or $BuildDir -eq [System.IO.Path]::GetPathRoot($BuildDir)) {
        throw "拒绝清理不安全的目录: $BuildDir"
    }
    Remove-Item -Recurse -Force $BuildDir
}

$EnableTesting = if ($Test) { "ON" } else { "OFF" }
$EnableIntegration = if ($Test -eq "integration" -or $Test -eq "all") { "ON" } else { "OFF" }

& cmake -S $ProjectRoot -B $BuildDir `
    "-DCMAKE_BUILD_TYPE=$Type" `
    "-DENABLE_TESTING=$EnableTesting" `
    "-DENABLE_INTEGRATION_TESTS=$EnableIntegration"
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败" }

$BuildArgs = @("--build", $BuildDir, "--config", $Type, "--parallel", $Jobs)
if ($Verbose) { $BuildArgs += "--verbose" }
& cmake $BuildArgs
if ($LASTEXITCODE -ne 0) { throw "构建失败" }

if ($Test -eq "unit") {
    & ctest --test-dir $BuildDir -C $Type -LE integration --output-on-failure
} elseif ($Test -eq "integration") {
    & ctest --test-dir $BuildDir -C $Type -L integration --output-on-failure
} elseif ($Test -eq "all") {
    & ctest --test-dir $BuildDir -C $Type --output-on-failure
}
if ($Test -and $LASTEXITCODE -ne 0) { throw "测试失败" }

if ($Prefix) { $Install = $true }
if ($Install) {
    if (-not $Prefix) { $Prefix = Join-Path $BuildDir "install" }
    & cmake --install $BuildDir --config $Type --prefix $Prefix
    if ($LASTEXITCODE -ne 0) { throw "安装失败" }
    Write-Host "已安装到: $Prefix" -ForegroundColor Green
}

Write-Host "构建完成: $BuildDir" -ForegroundColor Green
