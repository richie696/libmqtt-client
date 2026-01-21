# MQTT客户端库 - Debug模式测试构建脚本 (Windows PowerShell)
# 用于快速构建 Debug 版本的集成测试程序

param(
    [string]$Target = "",
    [string]$File = "",
    [string]$Dir = "build",
    [int]$Jobs = $env:NUMBER_OF_PROCESSORS,
    [switch]$List,
    [switch]$Help
)

# 已知的测试目标列表
$KnownTargets = @(
    "test_core",
    "test_config",
    "test_connection",
    "test_message",
    "test_subscription",
    "test_monitor",
    "test_reconnect",
    "test_client",
    "test_mqtt311_tcp_tls",
    "test_mqtt5_tcp_tls"
)

# 列出所有可用的测试目标
function Show-Targets {
    Write-Host "可用的测试目标:" -ForegroundColor Cyan
    for ($i = 0; $i -lt $KnownTargets.Length; $i++) {
        Write-Host "  $($i + 1). $($KnownTargets[$i])" -ForegroundColor Green
    }
    Write-Host ""
}

# 从源文件路径推导目标名称
function Get-TargetFromSource {
    param([string]$SourcePath)
    $basename = [System.IO.Path]::GetFileNameWithoutExtension($SourcePath)
    if ($basename -like "test_*") {
        return $basename
    } else {
        return "test_$basename"
    }
}

# 帮助信息
function Show-Help {
    Write-Host "MQTT客户端库 - Debug模式测试构建脚本 (Windows PowerShell)" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "用法: .\build_test_debug.ps1 [选项] [测试目标或源文件]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "选项:" -ForegroundColor Cyan
    Write-Host "  -Target TARGET     测试目标名称（CMake target）" -ForegroundColor White
    Write-Host "  -File FILE         测试源文件路径（会自动推导目标名称）" -ForegroundColor White
    Write-Host "  -List              列出所有可用的测试目标" -ForegroundColor White
    Write-Host "  -Dir DIR           构建目录" -ForegroundColor White
    Write-Host "                      默认: build"
    Write-Host "  -Jobs N            并行编译任务数" -ForegroundColor White
    Write-Host "                      默认: 自动检测CPU核心数"
    Write-Host "  -Help              显示此帮助信息" -ForegroundColor White
    Write-Host ""
    Write-Host "示例:" -ForegroundColor Cyan
    Write-Host "  .\build_test_debug.ps1                                    # 交互式选择测试目标" -ForegroundColor Green
    Write-Host "  .\build_test_debug.ps1 -Target test_mqtt311_tcp_tls      # 指定目标名称" -ForegroundColor Green
    Write-Host "  .\build_test_debug.ps1 -File tests\integration\test_mqtt311_tcp_tls.cpp  # 指定源文件" -ForegroundColor Green
    Write-Host "  .\build_test_debug.ps1 test_mqtt5_tcp_tls                # 直接指定目标（位置参数）" -ForegroundColor Green
    Write-Host "  .\build_test_debug.ps1 -List                              # 列出所有可用目标" -ForegroundColor Green
    Write-Host "  .\build_test_debug.ps1 -Jobs 8                           # 使用8个并行任务" -ForegroundColor Green
}

if ($Help) {
    Show-Help
    exit 0
}

if ($List) {
    Show-Targets
    exit 0
}

# 如果指定了源文件，推导目标名称
if ($File) {
    $Target = Get-TargetFromSource $File
    Write-Host "从源文件推导目标: $File -> $Target" -ForegroundColor Blue
}

# 如果没有指定目标，交互式选择
if (-not $Target) {
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  MQTT 客户端库 - Debug 测试构建" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    Show-Targets
    Write-Host "请选择要构建的测试目标（输入数字或名称）:" -ForegroundColor Yellow
    $userInput = Read-Host "> "
    
    # 检查是否是数字
    if ($userInput -match '^\d+$') {
        $index = [int]$userInput - 1
        if ($index -ge 0 -and $index -lt $KnownTargets.Length) {
            $Target = $KnownTargets[$index]
        } else {
            Write-Host "错误: 无效的选择" -ForegroundColor Red
            exit 1
        }
    } else {
        $Target = $userInput
    }
}

# 验证测试目标（给出警告但不阻止）
if ($KnownTargets -notcontains $Target) {
    Write-Host "警告: '$Target' 不在已知目标列表中" -ForegroundColor Yellow
    Write-Host "将尝试构建该目标（如果 CMake 中存在）..." -ForegroundColor Yellow
}

# 打印标题
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  MQTT 客户端库 - Debug 测试构建" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 获取项目根目录（脚本在 scripts/windows/ 目录下）
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$BuildDir = Join-Path $ProjectRoot $Dir

Write-Host "项目根目录: $ProjectRoot" -ForegroundColor Blue
Write-Host "构建目录: $BuildDir" -ForegroundColor Blue
Write-Host "构建类型: Debug" -ForegroundColor Blue
Write-Host "测试目标: $Target" -ForegroundColor Blue
Write-Host "并行任务: $Jobs" -ForegroundColor Blue
Write-Host ""

# 切换到项目根目录
Set-Location $ProjectRoot

# 配置 CMake
Write-Host "========== 配置 CMake ==========" -ForegroundColor Cyan
cmake -S . -B $BuildDir `
    -DCMAKE_BUILD_TYPE=Debug `
    -DENABLE_TESTING=ON `
    -DENABLE_INTEGRATION_TESTS=ON

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ CMake 配置失败" -ForegroundColor Red
    exit 1
}

Write-Host "✓ CMake 配置成功" -ForegroundColor Green
Write-Host ""

# 构建测试目标
Write-Host "========== 构建测试目标 ==========" -ForegroundColor Cyan
cmake --build $BuildDir --target $Target -j $Jobs

if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ 构建失败" -ForegroundColor Red
    exit 1
}

Write-Host "✓ 构建成功" -ForegroundColor Green
Write-Host ""

# 显示可执行文件路径（Windows 下可能没有 .exe 后缀）
$Executable = Join-Path $BuildDir "bin\$Target.exe"
$ExecutableNoExt = Join-Path $BuildDir "bin\$Target"
if (Test-Path $Executable) {
    Write-Host "可执行文件: $Executable" -ForegroundColor Green
    Write-Host ""
    Write-Host "运行测试:" -ForegroundColor Cyan
    Write-Host "  $Executable" -ForegroundColor White
    Write-Host ""
} elseif (Test-Path $ExecutableNoExt) {
    Write-Host "可执行文件: $ExecutableNoExt" -ForegroundColor Green
    Write-Host ""
    Write-Host "运行测试:" -ForegroundColor Cyan
    Write-Host "  $ExecutableNoExt" -ForegroundColor White
    Write-Host ""
} else {
    Write-Host "警告: 未找到可执行文件: $Executable 或 $ExecutableNoExt" -ForegroundColor Yellow
    Write-Host "请检查构建输出" -ForegroundColor Yellow
}
