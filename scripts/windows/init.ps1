# MQTT客户端库 - 初始化脚本 (Windows PowerShell)
# 用于初始化项目依赖（Git Submodules、第三方库构建等）
# 在首次克隆项目或构建前运行此脚本
# 使用方法: .\init.ps1

$ErrorActionPreference = "Stop"

# 颜色函数
function Write-ColorOutput($ForegroundColor) {
    $fc = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $ForegroundColor
    if ($args) {
        Write-Output $args
    }
    $host.UI.RawUI.ForegroundColor = $fc
}

# 获取脚本和项目目录
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$WolfMqttDir = Join-Path $ProjectRoot "third_party\wolfmqtt"
$WolfMqttInstallDir = Join-Path $WolfMqttDir "install"

# 打印标题
function Print-Header {
    Write-ColorOutput Cyan "========================================"
    Write-ColorOutput Cyan "  MQTT 客户端库 - 初始化脚本"
    Write-ColorOutput Cyan "========================================"
    Write-Host ""
}

# 检查基本依赖
function Check-BasicDependencies {
    Write-ColorOutput Blue "========== 检查基本依赖 =========="
    
    $DependenciesOk = $true
    $MissingDeps = @()
    
    # 检查Git
    Write-Host "检查 Git... " -NoNewline
    try {
        $gitVersion = git --version 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-ColorOutput Green "✓ $gitVersion"
        } else {
            throw
        }
    } catch {
        Write-ColorOutput Red "✗ 未找到"
        $DependenciesOk = $false
        $MissingDeps += "Git"
    }
    
    # 检查CMake
    Write-Host "检查 CMake... " -NoNewline
    try {
        $cmakeVersion = cmake --version 2>$null | Select-Object -First 1
        if ($LASTEXITCODE -eq 0) {
            Write-ColorOutput Green "✓ $cmakeVersion"
        } else {
            throw
        }
    } catch {
        Write-ColorOutput Red "✗ 未找到"
        $DependenciesOk = $false
        $MissingDeps += "CMake >= 3.15"
    }
    
    # 检查C++编译器
    Write-Host "检查 C++编译器... " -NoNewline
    $compilerFound = $false
    
    # 检查 MSVC (cl.exe)
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        try {
            $clVersion = cl 2>&1 | Select-Object -First 1
            Write-ColorOutput Green "✓ MSVC (cl.exe)"
            $compilerFound = $true
        } catch {
            # 继续检查其他编译器
        }
    }
    
    # 检查 MinGW (g++)
    if (-not $compilerFound -and (Get-Command g++ -ErrorAction SilentlyContinue)) {
        try {
            $gccVersion = g++ --version 2>$null | Select-Object -First 1
            Write-ColorOutput Green "✓ $gccVersion"
            $compilerFound = $true
        } catch {
            # 继续检查其他编译器
        }
    }
    
    # 检查 Clang
    if (-not $compilerFound -and (Get-Command clang++ -ErrorAction SilentlyContinue)) {
        try {
            $clangVersion = clang++ --version 2>$null | Select-Object -First 1
            Write-ColorOutput Green "✓ $clangVersion"
            $compilerFound = $true
        } catch {
            # 继续检查其他编译器
        }
    }
    
    if (-not $compilerFound) {
        Write-ColorOutput Red "✗ 未找到"
        $DependenciesOk = $false
        $MissingDeps += "C++ 编译器 (MSVC, MinGW 或 Clang)"
    }
    
    # 检查构建工具
    Write-Host "检查 构建工具... " -NoNewline
    $buildToolFound = $false
    
    # 检查 MSBuild (Visual Studio)
    if (Get-Command msbuild.exe -ErrorAction SilentlyContinue) {
        Write-ColorOutput Green "✓ MSBuild 已安装"
        $buildToolFound = $true
    }
    
    # 检查 Ninja
    if (-not $buildToolFound -and (Get-Command ninja -ErrorAction SilentlyContinue)) {
        Write-ColorOutput Green "✓ Ninja 已安装"
        $buildToolFound = $true
    }
    
    # 检查 Make (MinGW/MSYS)
    if (-not $buildToolFound -and (Get-Command make -ErrorAction SilentlyContinue)) {
        Write-ColorOutput Green "✓ Make 已安装"
        $buildToolFound = $true
    }
    
    if (-not $buildToolFound) {
        Write-ColorOutput Red "✗ 未找到"
        $DependenciesOk = $false
        $MissingDeps += "构建工具 (MSBuild, Ninja 或 Make)"
    }
    
    Write-ColorOutput Blue "============================="
    Write-Host ""
    
    if (-not $DependenciesOk) {
        Write-ColorOutput Red "========================================"
        Write-ColorOutput Red "  依赖检查失败！"
        Write-ColorOutput Red "========================================"
        Write-Host ""
        Write-ColorOutput Yellow "缺少的依赖:"
        foreach ($dep in $MissingDeps) {
            Write-ColorOutput Red "  - $dep"
        }
        Write-Host ""
        Write-ColorOutput Yellow "安装方法:"
        Write-ColorOutput Cyan "  1. Git: https://git-scm.com/download/win"
        Write-ColorOutput Cyan "  2. CMake: https://cmake.org/download/"
        Write-ColorOutput Cyan "  3. Visual Studio: https://visualstudio.microsoft.com/ (包含 MSVC 和 MSBuild)"
        Write-ColorOutput Cyan "  4. 或使用 MinGW-w64: https://www.mingw-w64.org/"
        Write-Host ""
        exit 1
    }
    
    Write-ColorOutput Green "所有基本依赖检查通过！✓"
    Write-Host ""
}

# 初始化 Git Submodules
function Init-Submodules {
    Write-ColorOutput Blue "========== 初始化 Git Submodules =========="
    
    # 检查是否在Git仓库中
    if (-not (Test-Path (Join-Path $ProjectRoot ".git"))) {
        Write-ColorOutput Yellow "警告: 当前目录不是Git仓库"
        Write-ColorOutput Yellow "跳过 Git Submodule 初始化"
        Write-Host ""
        return
    }
    
    # 检查 .gitmodules 文件是否存在
    $gitmodulesPath = Join-Path $ProjectRoot ".gitmodules"
    if (-not (Test-Path $gitmodulesPath)) {
        Write-ColorOutput Yellow "未找到 .gitmodules 文件，跳过 Submodule 初始化"
        Write-Host ""
        return
    }
    
    # 检查 wolfMQTT submodule 是否已初始化
    $wolfMqttGitPath = Join-Path $WolfMqttDir ".git"
    if (-not (Test-Path $WolfMqttDir) -or -not (Test-Path $wolfMqttGitPath)) {
        Write-ColorOutput Yellow "检测到 wolfMQTT submodule 未初始化"
        Write-ColorOutput Cyan "正在初始化 Git Submodules..."
        
        Push-Location $ProjectRoot
        try {
            git submodule update --init --recursive
            if ($LASTEXITCODE -ne 0) {
                throw "Git submodule 初始化失败"
            }
            
            if ((Test-Path $WolfMqttDir) -and (Test-Path $wolfMqttGitPath)) {
                Write-ColorOutput Green "✓ Git Submodules 初始化完成"
            } else {
                Write-ColorOutput Red "✗ Git Submodules 初始化失败"
                Write-ColorOutput Yellow "请手动运行: git submodule update --init --recursive"
                exit 1
            }
        } finally {
            Pop-Location
        }
    } else {
        Write-ColorOutput Green "✓ Git Submodules 已初始化"
        
        # 检查是否需要更新
        Push-Location $WolfMqttDir
        try {
            git fetch origin 2>$null | Out-Null
            $currentCommit = git rev-parse HEAD
            $remoteCommit = git rev-parse origin/master 2>$null
            if ($LASTEXITCODE -eq 0 -and $currentCommit -ne $remoteCommit) {
                Write-ColorOutput Yellow "检测到 wolfMQTT 有新版本可用"
                Write-ColorOutput Yellow "如需更新，请运行: cd third_party\wolfmqtt && git pull origin master"
            }
        } catch {
            # 忽略错误
        } finally {
            Pop-Location
        }
    }
    
    Write-Host ""
}

# 检查 wolfMQTT 是否已构建
function Test-WolfMqttBuilt {
    Write-ColorOutput Blue "========== 检查 wolfMQTT 构建状态 =========="
    
    if (-not (Test-Path $WolfMqttDir)) {
        Write-ColorOutput Red "错误: wolfMQTT 目录不存在: $WolfMqttDir"
        Write-ColorOutput Yellow "请先运行 Git Submodule 初始化"
        exit 1
    }
    
    # 检查是否已安装（有 install 目录和库文件）
    $installLibDir = Join-Path $WolfMqttInstallDir "lib"
    if ((Test-Path $installLibDir) -and (Get-ChildItem $installLibDir -Filter "wolfmqtt.*" -ErrorAction SilentlyContinue)) {
        Write-ColorOutput Green "✓ wolfMQTT 已构建并安装"
        Write-ColorOutput Cyan "  安装路径: $WolfMqttInstallDir"
        Write-Host ""
        return $true
    } else {
        Write-ColorOutput Yellow "⚠ wolfMQTT 尚未构建"
        Write-Host ""
        return $false
    }
}

# 构建 wolfMQTT
function Build-WolfMqtt {
    Write-ColorOutput Blue "========== 构建 wolfMQTT =========="
    
    if (-not (Test-Path $WolfMqttDir)) {
        Write-ColorOutput Red "错误: wolfMQTT 目录不存在: $WolfMqttDir"
        exit 1
    }
    
    Push-Location $WolfMqttDir
    
    try {
        # 检测 wolfSSL 路径（Windows 通常需要手动指定）
        $WolfSslPath = $null
        $possiblePaths = @(
            "C:\vcpkg\installed\x64-windows",
            "C:\Program Files\wolfSSL",
            "C:\wolfssl"
        )
        
        foreach ($path in $possiblePaths) {
            if (Test-Path $path) {
                $WolfSslPath = $path
                break
            }
        }
        
        # 创建构建目录
        $WolfMqttBuildDir = Join-Path $WolfMqttDir "build_cmake"
        if (-not (Test-Path $WolfMqttBuildDir)) {
            New-Item -ItemType Directory -Path $WolfMqttBuildDir | Out-Null
        }
        
        Push-Location $WolfMqttBuildDir
        
        try {
            # 配置 CMake
            Write-ColorOutput Cyan "配置 wolfMQTT (启用 TLS 和 MQTT 5.0)..."
            $cmakeArgs = @(
                "..",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_INSTALL_PREFIX=$WolfMqttInstallDir",
                "-DWOLFMQTT_TLS=yes",
                "-DWOLFMQTT_V5=yes"
            )
            
            if ($WolfSslPath) {
                $cmakeArgs += "-DWITH_WOLFSSL=$WolfSslPath"
                Write-ColorOutput Green "使用 wolfSSL: $WolfSslPath"
            } else {
                Write-ColorOutput Yellow "警告: 未找到 wolfSSL，TLS 功能可能不可用"
                Write-ColorOutput Yellow "提示: 可以使用 vcpkg 安装: vcpkg install wolfssl"
            }
            
            cmake @cmakeArgs
            if ($LASTEXITCODE -ne 0) {
                throw "CMake 配置失败"
            }
            
            # 构建
            Write-ColorOutput Cyan "构建 wolfMQTT..."
            $jobs = $env:NUMBER_OF_PROCESSORS
            if (-not $jobs) {
                $jobs = 4
            }
            cmake --build . --config Release --parallel $jobs
            if ($LASTEXITCODE -ne 0) {
                throw "构建失败"
            }
            
            # 安装
            Write-ColorOutput Cyan "安装 wolfMQTT..."
            cmake --install . --config Release
            if ($LASTEXITCODE -ne 0) {
                throw "安装失败"
            }
        } finally {
            Pop-Location
        }
    } finally {
        Pop-Location
    }
    
    # 验证安装
    $installLibDir = Join-Path $WolfMqttInstallDir "lib"
    if ((Test-Path $installLibDir) -and (Get-ChildItem $installLibDir -Filter "wolfmqtt.*" -ErrorAction SilentlyContinue)) {
        Write-ColorOutput Green "✓ wolfMQTT 构建并安装成功"
        Write-ColorOutput Cyan "  安装路径: $WolfMqttInstallDir"
    } else {
        Write-ColorOutput Red "✗ wolfMQTT 构建失败"
        exit 1
    }
    
    Write-Host ""
}

# 主函数
function Main {
    Print-Header
    
    Write-ColorOutput Cyan "平台: Windows"
    Write-Host ""
    
    # 1. 检查基本依赖
    Check-BasicDependencies
    
    # 2. 初始化 Git Submodules
    Init-Submodules
    
    # 3. 检查并构建 wolfMQTT
    if (-not (Test-WolfMqttBuilt)) {
        Write-ColorOutput Yellow "需要构建 wolfMQTT，是否继续? (y/n)"
        $response = Read-Host
        if ($response -match "^[Yy]$" -or [string]::IsNullOrEmpty($response)) {
            Build-WolfMqtt
        } else {
            Write-ColorOutput Yellow "跳过 wolfMQTT 构建"
            Write-ColorOutput Yellow "后续可以运行: .\scripts\windows\build.ps1 --menu (选择选项2)"
            Write-Host ""
        }
    }
    
    # 完成
    Write-ColorOutput Green "========================================"
    Write-ColorOutput Green "  初始化完成！"
    Write-ColorOutput Green "========================================"
    Write-Host ""
    Write-ColorOutput Cyan "下一步:"
    Write-ColorOutput Green "  1. 构建项目: .\scripts\windows\build.ps1"
    Write-ColorOutput Green "  2. 或使用菜单: .\scripts\windows\build.ps1 --menu"
    Write-Host ""
}

# 运行主函数
Main
