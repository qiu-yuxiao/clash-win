# ==============================================================================
# ClashWin Packaging Pipeline
# ==============================================================================

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
$BinDir = Join-Path $RootDir "build\bin"
$DistDir = Join-Path $RootDir "build\dist\ClashWin"
$SidecarSource = Join-Path $RootDir "sidecar"
$SetupExe = Join-Path $BinDir "ClashWin-Setup-x64.exe"
$ZipOut = Join-Path $BinDir "ClashWin-Portable-x64.zip"

Write-Host ">>> Starting ClashWin Packaging Pipeline..." -ForegroundColor Cyan

# 1. 确保目标输出目录存在
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $DistDir "sidecar") -Force | Out-Null

# 2. 拷贝主程序
$MainExe = Join-Path $BinDir "ClashWin.exe"
if (-not (Test-Path $MainExe)) {
    throw "Main binary not found: $MainExe"
}
Copy-Item $MainExe $DistDir
Write-Host "  -> Deployed ClashWin.exe" -ForegroundColor Green

# 3. 拷贝所有伴生侧车 (Sidecar) 二进制
$Sidecars = @(
    "mini-mihomo.exe",
    "clash-verge-service.exe",
    "clash-verge-service-install.exe",
    "clash-verge-service-uninstall.exe",
    "enableLoopback.exe"
)

foreach ($sc in $Sidecars) {
    $scPath = Join-Path $SidecarSource $sc
    if (Test-Path $scPath) {
        Copy-Item $scPath (Join-Path $DistDir "sidecar")
        Write-Host "  -> Deployed sidecar: $sc" -ForegroundColor Green
    } else {
        throw "Sidecar binary missing: $scPath"
    }
}

# 4. 生成一键服务安装与卸载脚本 (方便免安装便携版用户一键配置 TUN 模式)
$InstallServiceBat = @"
@echo off
echo ==================================================
echo   ClashWin SCM TUN Service Installer
echo ==================================================
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [ERROR] Please right-click this file and select 'Run as administrator'.
    pause
    exit /b 1
)

echo Registering clash_verge_service...
"%~dp0sidecar\clash-verge-service-install.exe"
echo Done!
pause
"@

$UninstallServiceBat = @"
@echo off
echo ==================================================
echo   ClashWin SCM TUN Service Uninstaller
echo ==================================================
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [ERROR] Please right-click this file and select 'Run as administrator'.
    pause
    exit /b 1
)

echo Removing clash_verge_service...
"%~dp0sidecar\clash-verge-service-uninstall.exe"
echo Done!
pause
"@

Set-Content -Path (Join-Path $DistDir "install-service.bat") -Value $InstallServiceBat -Encoding UTF8
Set-Content -Path (Join-Path $DistDir "uninstall-service.bat") -Value $UninstallServiceBat -Encoding UTF8
Write-Host "  -> Generated service helper scripts" -ForegroundColor Green

# 5. 打包便携版 ZIP
if (Test-Path $ZipOut) {
    Remove-Item -Force $ZipOut
}
Compress-Archive -Path "$DistDir\*" -DestinationPath $ZipOut -Force
Write-Host ">>> Created Portable Zip Package: $ZipOut" -ForegroundColor Yellow

# 6. 检测 makensis 是否可用以生成 Setup.exe
$MakeNsisPaths = @(
    "makensis",
    "C:\Program Files (x86)\NSIS\makensis.exe",
    "C:\Program Files\NSIS\makensis.exe",
    "$env:LOCALAPPDATA\NSIS\makensis.exe"
)

$NsisExe = $null
foreach ($p in $MakeNsisPaths) {
    if (Get-Command $p -ErrorAction SilentlyContinue) {
        $NsisExe = $p
        break
    }
    if (Test-Path $p) {
        $NsisExe = $p
        break
    }
}

if ($NsisExe) {
    Write-Host ">>> Compiling NSIS Installer using $NsisExe..." -ForegroundColor Cyan
    $NsiScript = Join-Path $RootDir "installer\ClashWin-Setup.nsi"
    & $NsisExe $NsiScript
    if ($LASTEXITCODE -eq 0) {
        Write-Host ">>> Official Setup Executable compiled: $SetupExe" -ForegroundColor Green
    }
} else {
    Write-Host ">>> Note: NSIS compiler not detected in standard paths. Portable package and staging directory ready!" -ForegroundColor Gray
}

Write-Host ">>> Packaging pipeline completed successfully!" -ForegroundColor Green
