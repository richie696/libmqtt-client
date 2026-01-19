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
# 脚本在 scripts/windows/ 目录下，需要向上两级才能到达项目根目录
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
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

# 从 .gitmodules 读取 submodule 路径的辅助函数
function Get-SubmodulePaths {
    param(
        [string]$GitmodulesFile
    )
    if (-not (Test-Path $GitmodulesFile)) {
        return @()
    }
    $paths = @()
    $pathLines = Select-String -Path $GitmodulesFile -Pattern "^\s*path\s*="
    foreach ($line in $pathLines) {
        $path = ($line.Line -split "=")[1].Trim()
        $paths += $path
    }
    return $paths
}

# 从 .gitmodules 读取 submodule URL 的辅助函数
function Get-SubmoduleUrl {
    param(
        [string]$GitmodulesFile,
        [string]$SubmodulePath
    )
    if (-not (Test-Path $GitmodulesFile) -or [string]::IsNullOrEmpty($SubmodulePath)) {
        return $null
    }
    $content = Get-Content $GitmodulesFile
    $inBlock = $false
    $foundPath = $false
    foreach ($line in $content) {
        if ($line -match "^\[submodule") {
            $inBlock = $false
            $foundPath = $false
        }
        if ($line -match "^\s*path\s*=") {
            $path = ($line -split "=")[1].Trim()
            if ($path -eq $SubmodulePath) {
                $inBlock = $true
                $foundPath = $true
            }
        }
        if ($inBlock -and $foundPath -and $line -match "^\s*url\s*=") {
            $url = ($line -split "=")[1].Trim()
            return $url
        }
    }
    return $null
}

# 检查 submodule 是否在 git 索引中注册
function Test-SubmoduleRegistered {
    param(
        [string]$SubmodulePath
    )
    if ([string]::IsNullOrEmpty($SubmodulePath)) {
        return $false
    }
    Push-Location $ProjectRoot
    try {
        $result = git ls-tree HEAD $SubmodulePath 2>$null
        if ($LASTEXITCODE -eq 0 -and $result -match "^160000") {
            return $true
        }
        return $false
    } finally {
        Pop-Location
    }
}

# 初始化 Git Submodules
function Init-Submodules {
    Write-ColorOutput Blue "========== 初始化 Git Submodules =========="
    
    # 检查是否在Git仓库中（支持 .git 目录或 .git 文件）
    $gitPath = Join-Path $ProjectRoot ".git"
    $isGitRepo = (Test-Path $gitPath) -or (Test-Path (Join-Path $ProjectRoot ".gitmodules"))
    
    if (-not $isGitRepo) {
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
    
    # 获取所有 submodule 路径
    $submodulePaths = Get-SubmodulePaths -GitmodulesFile $gitmodulesPath
    if ($submodulePaths.Count -eq 0) {
        Write-ColorOutput Yellow "未找到任何 submodule 配置"
        Write-Host ""
        return
    }
    
    # 确保在项目根目录执行
    Push-Location $ProjectRoot
    try {
        $submodulesNeedAdd = $false
        $submodulesNeedInit = $false
        
        # 检查每个 submodule
        foreach ($submodulePath in $submodulePaths) {
            $submoduleDir = Join-Path $ProjectRoot $submodulePath
            $submoduleGitPath = Join-Path $submoduleDir ".git"
            $isSubmoduleInitialized = (Test-Path $submoduleDir) -and ((Test-Path $submoduleGitPath) -or (Test-Path (Join-Path $submoduleDir ".git")))
            
            if (-not $isSubmoduleInitialized) {
                # 检查是否在 git 索引中注册
                if (-not (Test-SubmoduleRegistered -SubmodulePath $submodulePath)) {
                    Write-ColorOutput Yellow "检测到 submodule 未在 Git 索引中注册: $submodulePath"
                    $submodulesNeedAdd = $true
                    
                    # 获取 submodule URL
                    $submoduleUrl = Get-SubmoduleUrl -GitmodulesFile $gitmodulesPath -SubmodulePath $submodulePath
                    if ([string]::IsNullOrEmpty($submoduleUrl)) {
                        Write-ColorOutput Red "✗ 无法从 .gitmodules 获取 $submodulePath 的 URL"
                        Write-ColorOutput Yellow "请检查 .gitmodules 文件配置"
                        exit 1
                    }
                    
                    # 检查目录是否存在但不是正确的 submodule
                    if (Test-Path $submoduleDir) {
                        $submoduleGitPath = Join-Path $submoduleDir ".git"
                        # 检查是否是独立的 git 仓库（有 .git 目录或文件）
                        if ((Test-Path $submoduleGitPath) -or (Test-Path (Join-Path $submoduleDir ".git"))) {
                            Write-ColorOutput Yellow "检测到目录已存在但不是正确的 submodule: $submodulePath"
                            Write-ColorOutput Cyan "正在清理不完整的 submodule 目录..."
                            
                            # 询问用户是否要删除（在非交互模式下自动删除）
                            if ([Environment]::UserInteractive) {
                                $response = Read-Host "是否删除现有目录并重新添加? (y/n) [y]"
                                if ($response -and $response -notmatch "^[Yy]$") {
                                    Write-ColorOutput Yellow "跳过 submodule 添加"
                                    continue
                                }
                            }
                            
                            # 删除现有目录
                            Write-ColorOutput Cyan "删除目录: $submoduleDir"
                            try {
                                Remove-Item -Path $submoduleDir -Recurse -Force -ErrorAction Stop
                                Write-ColorOutput Green "✓ 目录已删除"
                            } catch {
                                Write-ColorOutput Red "✗ 无法删除目录: $submoduleDir - $_"
                                Write-ColorOutput Yellow "请手动删除后重试"
                                exit 1
                            }
                        } else {
                            # 目录存在但没有 .git，可能是空目录或其他文件
                            Write-ColorOutput Yellow "检测到目录存在但不是 git 仓库: $submodulePath"
                            Write-ColorOutput Cyan "正在清理目录..."
                            try {
                                Remove-Item -Path $submoduleDir -Recurse -Force -ErrorAction Stop
                            } catch {
                                Write-ColorOutput Red "✗ 无法删除目录: $submoduleDir - $_"
                                exit 1
                            }
                        }
                    }
                    
                    # 创建父目录
                    $parentDir = Split-Path -Parent $submoduleDir
                    if ($parentDir -ne $ProjectRoot -and -not (Test-Path $parentDir)) {
                        Write-ColorOutput Cyan "创建父目录: $parentDir"
                        try {
                            New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
                        } catch {
                            Write-ColorOutput Red "✗ 无法创建目录: $parentDir - $_"
                            exit 1
                        }
                    }
                    
                    # 使用 git submodule add 添加 submodule
                    Write-ColorOutput Cyan "正在添加 submodule: $submodulePath"
                    Write-ColorOutput Cyan "  URL: $submoduleUrl"
                    
                    # 尝试添加 submodule
                    $addOutput = git submodule add $submoduleUrl $submodulePath 2>&1
                    $addExitCode = $LASTEXITCODE
                    
                    if ($addOutput) {
                        Write-Host $addOutput
                    }
                    
                    if ($addExitCode -eq 0) {
                        Write-ColorOutput Green "✓ Submodule 添加成功: $submodulePath"
                    } else {
                        # 如果失败，尝试使用 --force 选项
                        Write-ColorOutput Yellow "常规添加失败，尝试使用 --force 选项..."
                        $addOutput = git submodule add --force $submoduleUrl $submodulePath 2>&1
                        $addExitCode = $LASTEXITCODE
                        
                        if ($addOutput) {
                            Write-Host $addOutput
                        }
                        
                        if ($addExitCode -eq 0) {
                            Write-ColorOutput Green "✓ Submodule 添加成功 (使用 --force): $submodulePath"
                        } else {
                            Write-ColorOutput Red "✗ Submodule 添加失败: $submodulePath"
                            Write-ColorOutput Yellow "错误信息:"
                            git submodule add $submoduleUrl $submodulePath 2>&1 | Select-Object -First 5
                            Write-ColorOutput Yellow "请检查:"
                            Write-ColorOutput Yellow "  1. 网络连接是否正常"
                            Write-ColorOutput Yellow "  2. Git 配置是否正确"
                            Write-ColorOutput Yellow "  3. 目录权限是否正确"
                            Write-ColorOutput Yellow "  4. 如果目录已存在，请手动删除后重试"
                            exit 1
                        }
                    }
                } else {
                    $submodulesNeedInit = $true
                }
            }
        }
        
        # 如果有 submodule 需要初始化（已注册但未下载）
        if ($submodulesNeedInit) {
            Write-ColorOutput Yellow "检测到 submodule 未初始化"
            Write-ColorOutput Cyan "正在初始化 Git Submodules..."
            
            # 显示调试信息
            Write-ColorOutput Cyan "项目根目录: $ProjectRoot"
            
            # 从 .gitmodules 文件中读取所有 submodule 路径，并创建对应的父目录
            $GitmodulesFile = Join-Path $ProjectRoot ".gitmodules"
            if (Test-Path $GitmodulesFile) {
                Write-ColorOutput Cyan "检查并创建 submodule 父目录..."
                # 提取所有 path = 行
                $pathLines = Select-String -Path $GitmodulesFile -Pattern "^\s*path\s*="
                foreach ($line in $pathLines) {
                    # 提取路径（去除 path = 和空格）
                    $path = ($line.Line -split "=")[1].Trim()
                    # 获取父目录
                    $parentDir = Join-Path $ProjectRoot (Split-Path -Parent $path)
                    
                    # 如果父目录不是项目根目录，且不存在，则创建
                    if ($parentDir -ne $ProjectRoot -and -not (Test-Path $parentDir)) {
                        Write-ColorOutput Yellow "创建目录: $parentDir"
                        try {
                            New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
                            Write-ColorOutput Green "✓ 目录已创建: $parentDir"
                        } catch {
                            Write-ColorOutput Red "✗ 无法创建目录: $parentDir - $_"
                            exit 1
                        }
                    }
                }
            }
            
            Write-Host "执行: git submodule update --init --recursive" -ForegroundColor Cyan
            
            # 执行 git submodule 初始化，捕获输出
            $gitOutput = git submodule update --init --recursive 2>&1
            $gitExitCode = $LASTEXITCODE
            
            # 显示 git 命令的输出（如果有）
            if ($gitOutput) {
                Write-Host $gitOutput
            }
            
            # 检查命令执行结果
            if ($gitExitCode -ne 0) {
                Write-ColorOutput Red "✗ Git Submodules 初始化失败 (退出码: $gitExitCode)"
                Write-ColorOutput Yellow "请检查:"
                Write-ColorOutput Yellow "  1. 是否在正确的 Git 仓库中"
                Write-ColorOutput Yellow "  2. .gitmodules 文件是否正确配置"
                Write-ColorOutput Yellow "  3. 网络连接是否正常（需要从远程仓库克隆）"
                Write-ColorOutput Yellow "  4. Git 配置是否正确（user.name, user.email）"
                Write-ColorOutput Yellow "手动运行: cd $ProjectRoot; git submodule update --init --recursive"
                exit 1
            }
            
            # 验证父目录是否存在
            $ThirdPartyDir = Join-Path $ProjectRoot "third_party"
            if (-not (Test-Path $ThirdPartyDir)) {
                Write-ColorOutput Red "✗ 错误: third_party 目录不存在，即使已尝试创建"
                Write-ColorOutput Yellow "请检查目录权限"
                exit 1
            }
            
            # 验证所有 submodule 是否已正确初始化
            $initFailed = $false
            $failedPaths = @()
            
            foreach ($submodulePath in $submodulePaths) {
                $submoduleDir = Join-Path $ProjectRoot $submodulePath
                $submoduleGitPath = Join-Path $submoduleDir ".git"
                $parentDir = Split-Path -Parent $submoduleDir
                
                # 检查父目录是否存在
                if ($parentDir -ne $ProjectRoot -and -not (Test-Path $parentDir)) {
                    Write-ColorOutput Red "✗ 错误: 父目录不存在: $parentDir"
                    Write-ColorOutput Yellow "请检查目录权限"
                    $initFailed = $true
                    $failedPaths += $submodulePath
                    continue
                }
                
                # 检查 submodule 目录和 .git 文件/目录
                $gitExists = (Test-Path $submoduleGitPath) -or (Test-Path (Join-Path $submoduleDir ".git"))
                if ((Test-Path $submoduleDir) -and $gitExists) {
                    Write-ColorOutput Green "✓ Submodule 已初始化: $submodulePath"
                } else {
                    $initFailed = $true
                    $failedPaths += $submodulePath
                    Write-ColorOutput Red "✗ Submodule 初始化失败: $submodulePath"
                    Write-ColorOutput Yellow "预期路径: $submoduleDir"
                    
                    if (Test-Path $submoduleDir) {
                        Write-ColorOutput Yellow "目录存在，但 .git 文件/目录缺失"
                        Write-ColorOutput Cyan "目录内容:"
                        Get-ChildItem $submoduleDir -ErrorAction SilentlyContinue | Select-Object -First 10 | ForEach-Object {
                            Write-Host "  $($_.Name)" -ForegroundColor Cyan
                        }
                    } else {
                        Write-ColorOutput Yellow "目录不存在"
                    }
                }
            }
            
            if ($initFailed) {
                Write-ColorOutput Yellow "当前工作目录: $(Get-Location)"
                Write-ColorOutput Yellow "项目根目录: $ProjectRoot"
                Write-ColorOutput Yellow "请尝试手动运行:"
                Write-ColorOutput Cyan "  cd $ProjectRoot"
                Write-ColorOutput Cyan "  git submodule update --init --recursive"
                Write-ColorOutput Yellow "如果仍然失败，请检查:"
                Write-ColorOutput Yellow "  1. Git 仓库状态: git status"
                Write-ColorOutput Yellow "  2. Submodule 状态: git submodule status"
                Write-ColorOutput Yellow "  3. .gitmodules 内容是否正确"
                exit 1
            } else {
                Write-ColorOutput Green "✓ 所有 Git Submodules 初始化完成"
            }
        }
    } catch {
        Write-ColorOutput Red "✗ Git Submodules 初始化失败: $_"
        Write-ColorOutput Yellow "请手动运行: cd $ProjectRoot; git submodule update --init --recursive"
        exit 1
    } finally {
        Pop-Location
    }
    
    # 如果所有 submodule 都已初始化，检查是否需要更新
    $allInitialized = $true
    foreach ($submodulePath in $submodulePaths) {
        $submoduleDir = Join-Path $ProjectRoot $submodulePath
        $submoduleGitPath = Join-Path $submoduleDir ".git"
        $isSubmoduleInitialized = (Test-Path $submoduleDir) -and ((Test-Path $submoduleGitPath) -or (Test-Path (Join-Path $submoduleDir ".git")))
        if (-not $isSubmoduleInitialized) {
            $allInitialized = $false
            break
        }
    }
    
    if ($allInitialized) {
        Write-ColorOutput Green "✓ Git Submodules 已初始化"
        
        # 检查是否需要更新
        Push-Location $WolfMqttDir
        try {
            git fetch origin 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) {
                $currentCommit = git rev-parse HEAD
                $remoteCommit = git rev-parse origin/master 2>$null
                if ($LASTEXITCODE -eq 0 -and $currentCommit -ne $remoteCommit) {
                    Write-ColorOutput Yellow "检测到 wolfMQTT 有新版本可用"
                    Write-ColorOutput Yellow "如需更新，请运行: cd third_party\wolfmqtt && git pull origin master"
                }
            }
        } catch {
            # 忽略错误
        } finally {
            Pop-Location
        }
    }
    
    Write-Host ""
}

# 检查 submodule 是否已构建
function Test-SubmoduleBuilt {
    param(
        [string]$SubmodulePath
    )
    
    if ([string]::IsNullOrEmpty($SubmodulePath)) {
        return $false
    }
    
    $submoduleDir = Join-Path $ProjectRoot $SubmodulePath
    if (-not (Test-Path $submoduleDir)) {
        return $false
    }
    
    $installLibDir = Join-Path $submoduleDir "install\lib"
    if ((Test-Path $installLibDir) -and (Get-ChildItem $installLibDir -Filter "*.lib" -ErrorAction SilentlyContinue)) {
        return $true
    }
    
    return $false
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
        # 检测系统安装的 wolfSSL（可选，用于 TLS 支持）
        $WolfSslPath = $null
        $possiblePaths = @(
            "C:\vcpkg\installed\x64-windows",
            "C:\Program Files\wolfSSL",
            "C:\wolfssl"
        )
        
        foreach ($path in $possiblePaths) {
            if (Test-Path $path) {
                $WolfSslPath = $path
                Write-ColorOutput Green "使用系统安装的 wolfSSL: $WolfSslPath"
                break
            }
        }
        
        if (-not $WolfSslPath) {
            Write-ColorOutput Yellow "警告: 未找到 wolfSSL，TLS 功能将不可用"
            Write-ColorOutput Yellow "提示: 可以使用 vcpkg 安装: vcpkg install wolfssl"
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
