# MQTT客户端库 - 跨平台构建脚本 (Windows PowerShell)
# 使用方法: .\build.ps1 [选项]

param(
    [string]$Type = "Release",
    [string]$Dir = "build",
    [switch]$Clean,
    [switch]$NoDemo,
    [int]$Jobs = $env:NUMBER_OF_PROCESSORS,
    [switch]$Verbose,
    [switch]$Install,
    [switch]$Help
)

# 帮助信息
function Show-Help {
    Write-Host "MQTT客户端库 - 构建脚本 (Windows PowerShell)" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "用法: .\build.ps1 [选项]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "选项:" -ForegroundColor Cyan
    Write-Host "  -Type TYPE         构建类型 (Debug|Release|RelWithDebInfo|MinSizeRel)" -ForegroundColor White
    Write-Host "                      默认: Release"
    Write-Host "  -Dir DIR            构建目录" -ForegroundColor White
    Write-Host "                      默认: build"
    Write-Host "  -Clean              清理构建目录" -ForegroundColor White
    Write-Host "  -Jobs N             并行编译任务数" -ForegroundColor White
    Write-Host "                      默认: 自动检测CPU核心数"
    Write-Host "  -Verbose            显示详细输出" -ForegroundColor White
    Write-Host "  -NoDemo             不构建Demo程序" -ForegroundColor White
    Write-Host "  -Install            安装到系统" -ForegroundColor White
    Write-Host "  -Help               显示此帮助信息" -ForegroundColor White
    Write-Host ""
    Write-Host "示例:" -ForegroundColor Cyan
    Write-Host "  .\build.ps1                          # 默认Release构建" -ForegroundColor Green
    Write-Host "  .\build.ps1 -Type Debug              # Debug构建" -ForegroundColor Green
    Write-Host "  .\build.ps1 -Clean                  # 清理后构建" -ForegroundColor Green
    Write-Host "  .\build.ps1 -Jobs 8                 # 使用8个并行任务" -ForegroundColor Green
    Write-Host "  .\build.ps1 -Type Debug -Verbose    # Debug构建，显示详细输出" -ForegroundColor Green
}

if ($Help) {
    Show-Help
    exit 0
}

# 验证构建类型
$ValidTypes = @("Debug", "Release", "RelWithDebInfo", "MinSizeRel")
if ($ValidTypes -notcontains $Type) {
    Write-Host "错误: 无效的构建类型: $Type" -ForegroundColor Red
    exit 1
}

# 打印标题
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  MQTT 客户端库 - 构建脚本" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检测平台
Write-Host "平台: Windows" -ForegroundColor Green
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  依赖检查" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$dependenciesOk = $true
$missingDeps = @()

# 检查CMake
Write-Host "检查 CMake... " -NoNewline
try {
    $cmakeOutput = cmake --version 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0 -and $cmakeOutput) {
        $versionLine = ($cmakeOutput -split "`n")[0]
        Write-Host "✓ $versionLine" -ForegroundColor Green
        
        # 检查版本
        if ($versionLine -match "version (\d+)\.(\d+)") {
            $major = [int]$matches[1]
            $minor = [int]$matches[2]
            if ($major -lt 3 -or ($major -eq 3 -and $minor -lt 15)) {
                Write-Host "✗ 版本过低 (当前: $major.$minor)" -ForegroundColor Red
                $dependenciesOk = $false
                $missingDeps += "CMake >= 3.15"
                Write-Host "  需要版本: >= 3.15" -ForegroundColor Yellow
                Write-Host "  安装方法:" -ForegroundColor Yellow
                Write-Host "    从 https://cmake.org/download/ 下载最新版本" -ForegroundColor Green
                Write-Host "    或使用 Chocolatey: choco install cmake" -ForegroundColor Green
                Write-Host "    或使用 Scoop: scoop install cmake" -ForegroundColor Green
            }
        }
    } else {
        throw "CMake not found"
    }
} catch {
    Write-Host "✗ 未找到" -ForegroundColor Red
    $dependenciesOk = $false
    $missingDeps += "CMake >= 3.15"
    Write-Host "  需要版本: >= 3.15" -ForegroundColor Yellow
    Write-Host "  安装方法:" -ForegroundColor Yellow
    Write-Host "    从 https://cmake.org/download/ 下载并安装" -ForegroundColor Green
    Write-Host "    或使用 Chocolatey: choco install cmake" -ForegroundColor Green
    Write-Host "    或使用 Scoop: scoop install cmake" -ForegroundColor Green
}

# 检查C++编译器
Write-Host "检查 C++编译器... " -NoNewline
$compilerFound = $false

# 检查MSVC
if (Get-Command cl -ErrorAction SilentlyContinue) {
    Write-Host "✓ 找到 MSVC 编译器" -ForegroundColor Green
    $compilerFound = $true
}
# 检查MinGW/GCC
elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
    Write-Host "✓ 找到 MinGW/GCC 编译器" -ForegroundColor Green
    $compilerFound = $true
}
# 检查Clang
elseif (Get-Command clang++ -ErrorAction SilentlyContinue) {
    Write-Host "✓ 找到 Clang 编译器" -ForegroundColor Green
    $compilerFound = $true
}

if (-not $compilerFound) {
    Write-Host "✗ 未找到" -ForegroundColor Red
    $dependenciesOk = $false
    $missingDeps += "C++编译器 (Visual Studio 或 MinGW 或 Clang)"
    Write-Host "  需要: Visual Studio 或 MinGW-w64 或 Clang" -ForegroundColor Yellow
    Write-Host "  安装方法:" -ForegroundColor Yellow
    Write-Host "    - Visual Studio: 从 https://visualstudio.microsoft.com/ 下载安装" -ForegroundColor Green
    Write-Host "    - MinGW-w64: 从 https://www.mingw-w64.org/ 下载安装" -ForegroundColor Green
    Write-Host "    - 或使用 Chocolatey: choco install mingw" -ForegroundColor Green
}

# 检查构建工具
Write-Host "检查 构建工具... " -NoNewline
if (Get-Command cmake -ErrorAction SilentlyContinue) {
    Write-Host "✓ CMake 已包含构建工具" -ForegroundColor Green
} elseif (Get-Command nmake -ErrorAction SilentlyContinue) {
    Write-Host "✓ 找到 NMake" -ForegroundColor Green
} elseif (Get-Command ninja -ErrorAction SilentlyContinue) {
    Write-Host "✓ 找到 Ninja" -ForegroundColor Green
} else {
    Write-Host "⚠ 未找到独立构建工具，但CMake会自动处理" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 如果依赖检查失败，退出
if (-not $dependenciesOk) {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  依赖检查失败！" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "缺少的依赖:" -ForegroundColor Yellow
    foreach ($dep in $missingDeps) {
        Write-Host "  - $dep" -ForegroundColor Red
    }
    Write-Host ""
    Write-Host "请先安装上述依赖，然后重新运行构建脚本。" -ForegroundColor Yellow
    exit 1
}

Write-Host "所有依赖检查通过！✓" -ForegroundColor Green
Write-Host ""

Write-Host ""
Write-Host "配置:"
Write-Host "  构建类型: $Type"
Write-Host "  构建目录: $Dir"
Write-Host "  并行任务: $Jobs"
Write-Host "  构建Demo: $(-not $NoDemo)"
Write-Host ""

# 获取脚本所在目录和项目根目录
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

# 构建目录的绝对路径
$buildDirAbs = Join-Path $projectRoot $Dir

# 清理构建目录
if ($Clean) {
    Write-Host "清理构建目录..." -ForegroundColor Yellow
    if (Test-Path $buildDirAbs) {
        Remove-Item -Recurse -Force $buildDirAbs
    }
}

# 创建构建目录
if (-not (Test-Path $buildDirAbs)) {
    New-Item -ItemType Directory -Path $buildDirAbs | Out-Null
}
Set-Location $buildDirAbs

# 配置CMake（源目录是项目根目录）
Write-Host "配置CMake..." -ForegroundColor Cyan
$demoFlag = if ($NoDemo) { "OFF" } else { "ON" }
$cmakeArgs = @(
    $projectRoot,
    "-DCMAKE_BUILD_TYPE=$Type",
    "-DBUILD_DEMO=$demoFlag"
)

if ($Verbose) {
    & cmake $cmakeArgs
} else {
    & cmake $cmakeArgs 2>&1 | Select-String -Pattern "(平台|架构|配置|错误|警告|CMake)" | ForEach-Object { Write-Host $_ }
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "错误: CMake配置失败" -ForegroundColor Red
    exit 1
}

Write-Host ""

# 构建
Write-Host "开始构建..." -ForegroundColor Cyan
$buildArgs = @(
    "--build", ".",
    "--config", $Type,
    "-j", $Jobs
)

if (-not $Verbose) {
    $buildArgs += "--quiet"
}

& cmake $buildArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host "错误: 构建失败" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "构建成功！" -ForegroundColor Green
Write-Host ""

# 显示构建产物
Write-Host "构建产物:"
if (Test-Path "bin\mqtt_client.dll") {
    Write-Host "  库文件: bin\mqtt_client.dll" -ForegroundColor Green
}
if (Test-Path "lib\mqtt_client.lib") {
    Write-Host "  导入库: lib\mqtt_client.lib" -ForegroundColor Green
}
if (Test-Path "bin\mqtt_demo.exe") {
    if (-not $NoDemo) {
        Write-Host "  Demo: bin\mqtt_demo.exe" -ForegroundColor Green
    }
}

# 安装
if ($Install) {
    Write-Host ""
    Write-Host "安装到系统..." -ForegroundColor Cyan
    & cmake --install . --config $Type
    if ($LASTEXITCODE -ne 0) {
        Write-Host "错误: 安装失败" -ForegroundColor Red
        exit 1
    }
    Write-Host "安装完成！" -ForegroundColor Green
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  构建完成！✓" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
