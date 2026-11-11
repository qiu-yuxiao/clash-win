<#
.SYNOPSIS
  Clash Win NSIS 安装包构建脚本（暂存 Release 产物并调用 makensis 生成 dist\ClashWin_<version>_x64-setup.exe）。

.DESCRIPTION
  严格遵守上游二进制不可变原则：
  - 仅拷贝 5 个经过基线校验的 sidecar 二进制，绝不把构建目录中的历史残留混入安装包；
  - 绝不对 sidecar/*.exe 执行本地自签证书改写（无需 UAC 自提升，支持本地与 CI 全自动无交互构建）；
  - 显式向 makensis 传入 /INPUTCHARSET UTF8，防止跨区域系统 ACP 编码误判。
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$Makensis = '',
    [string]$Version = '',
    [switch]$SkipStage,
    [switch]$SkipSign
)

$ErrorActionPreference = 'Stop'

# 剥离污染代理环境变量（防止子进程环境字典大小写键冲突）
foreach ($_n in @('HTTP_PROXY','HTTPS_PROXY','http_proxy','https_proxy',
                  'ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy',
                  'NO_PROXY','no_proxy')) {
  [Environment]::SetEnvironmentVariable($_n, $null, 'Process')
}

$RepoRoot  = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BinDir    = Join-Path $RepoRoot "build\bin\$Configuration"
$StageDir  = Join-Path $PSScriptRoot 'stage'
$DistDir   = Join-Path $RepoRoot 'dist'
$ScriptNsi = Join-Path $PSScriptRoot 'clashwin.nsi'
$IconFile  = Join-Path $RepoRoot 'resources\icons\icon.ico'

if (-not $Version) {
    $consts = Join-Path $RepoRoot 'include\clashwin\config\constants.h'
    if (Test-Path -LiteralPath $consts) {
        $hit = Select-String -LiteralPath $consts -Pattern 'kAppVersion\s*=\s*"([^"]+)"' | Select-Object -First 1
        if ($hit) { $Version = $hit.Matches[0].Groups[1].Value }
    }
    if (-not $Version) { throw "未能解析版本号，请传入 -Version <X.Y.Z> 或检查 constants.h" }
}

$OutFile = Join-Path $DistDir "ClashWin_${Version}_x64-setup.exe"

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Find-Makensis([string]$Explicit) {
    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit)) { return $null }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    if ($env:NSIS_MAKENSIS -and (Test-Path -LiteralPath $env:NSIS_MAKENSIS)) {
        return (Resolve-Path -LiteralPath $env:NSIS_MAKENSIS).Path
    }
    $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'tauri\NSIS\makensis.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),
        (Join-Path $env:ProgramFiles 'NSIS\makensis.exe')
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return $c }
    }

    $regKeys = @(
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\NSIS',
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\NSIS',
        'HKCU:\Software\NSIS'
    )
    foreach ($rk in $regKeys) {
        if (Test-Path $rk) {
            $loc = (Get-ItemProperty $rk -ErrorAction SilentlyContinue).InstallLocation
            if (-not $loc) { $loc = (Get-ItemProperty $rk -ErrorAction SilentlyContinue).'(default)' }
            if ($loc) {
                $exe = Join-Path $loc 'makensis.exe'
                if (Test-Path -LiteralPath $exe) { return $exe }
            }
        }
    }
    return $null
}

# ---------- 1. 校验构建产物齐全 ----------
$SidecarNames = @(
    'win-mihomo.exe',
    'clash-verge-service.exe',
    'clash-verge-service-install.exe',
    'clash-verge-service-uninstall.exe',
    'enableLoopback.exe'
)

$mainExe    = Join-Path $BinDir 'clashwin.exe'
$localesDir = Join-Path $BinDir 'locales'
$resDir     = Join-Path $BinDir 'resources'

$missing = @()
if (-not (Test-Path -LiteralPath $mainExe)) { $missing += $mainExe }
foreach ($sc in $SidecarNames) {
    $p = Join-Path $BinDir "sidecar\$sc"
    if (-not (Test-Path -LiteralPath $p)) { $missing += $p }
}
foreach ($rf in @('Country.mmdb', 'geosite.dat', 'gfwlist.yaml', 'icons\icon.ico')) {
    $p = Join-Path $resDir $rf
    if (-not (Test-Path -LiteralPath $p)) { $missing += $p }
}
if (-not (Test-Path -LiteralPath $localesDir)) {
    $missing += $localesDir
} elseif (-not (Get-ChildItem -LiteralPath $localesDir -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    $missing += "$localesDir (empty)"
}
foreach ($p in @($IconFile, $ScriptNsi)) {
    if (-not (Test-Path -LiteralPath $p)) { $missing += $p }
}
if ($missing.Count -gt 0) {
    Fail ("Missing required artifact(s):`n  " + ($missing -join "`n  "))
}

# ---------- 2. 干净暂存（仅拷贝白名单 5 个 sidecar，杜绝历史残留混入安装包） ----------
if (-not $SkipStage) {
    if (Test-Path -LiteralPath $StageDir) {
        Remove-Item -LiteralPath $StageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $StageDir | Out-Null
    Copy-Item -LiteralPath $mainExe -Destination (Join-Path $StageDir 'clashwin.exe') -Force

    $stageSidecar = Join-Path $StageDir 'sidecar'
    New-Item -ItemType Directory -Path $stageSidecar | Out-Null
    foreach ($sc in $SidecarNames) {
        Copy-Item -LiteralPath (Join-Path $BinDir "sidecar\$sc") -Destination (Join-Path $stageSidecar $sc) -Force
    }

    Copy-Item -LiteralPath $resDir -Destination (Join-Path $StageDir 'resources') -Recurse -Force
    Copy-Item -LiteralPath $localesDir -Destination (Join-Path $StageDir 'locales') -Recurse -Force

    # 服务三件套同步放入 resources\（dirs::service_path() 与安装/卸载器均从 <exe>\resources\ 查找）
    foreach ($name in @('clash-verge-service.exe', 'clash-verge-service-install.exe', 'clash-verge-service-uninstall.exe')) {
        Copy-Item -LiteralPath (Join-Path $BinDir "sidecar\$name") `
                  -Destination (Join-Path (Join-Path $StageDir 'resources') $name) -Force
    }
    Write-Host "Staged clean artifacts into $StageDir"
}

# ---------- 3. 定位 makensis ----------
$makensisExe = Find-Makensis $Makensis
if (-not $makensisExe) {
    Fail @"
makensis.exe not found. Install NSIS 3.x (https://nsis.sourceforge.io/) or pass -Makensis <path>.
Searched: -Makensis, `$env:NSIS_MAKENSIS, PATH, %LOCALAPPDATA%\tauri\NSIS, Program Files\NSIS, Program Files (x86)\NSIS, and the NSIS uninstall registry key.
"@
}
Write-Host "Using makensis: $makensisExe"
& $makensisExe /VERSION

# ---------- 4. 编译 NSIS 安装包 ----------
if (-not (Test-Path -LiteralPath $DistDir)) {
    New-Item -ItemType Directory -Path $DistDir | Out-Null
}
if (Test-Path -LiteralPath $OutFile) {
    Remove-Item -LiteralPath $OutFile -Force
}

$defines = @(
    "/DVERSION=$Version",
    "/DSTAGEDIR=$StageDir",
    "/DOUTFILE=$OutFile",
    "/DICONFILE=$IconFile"
)

$nsisRoot = Split-Path -Parent $makensisExe
$tauriPlugin = Join-Path $nsisRoot 'Plugins\x86-unicode\additional\nsis_tauri_utils.dll'
if (Test-Path -LiteralPath $tauriPlugin) {
    $defines += "/DTAURI_UTILS_PLUGIN_DIR=$(Join-Path $nsisRoot 'Plugins\x86-unicode\additional')"
}

Write-Host "Compiling $ScriptNsi ..."
& $makensisExe /INPUTCHARSET UTF8 @defines $ScriptNsi
if ($LASTEXITCODE -ne 0) {
    Fail "makensis failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $OutFile)) {
    Fail "Expected installer was not produced: $OutFile"
}

$item = Get-Item -LiteralPath $OutFile
$sizeMb = [math]::Round($item.Length / 1MB, 2)

Write-Host ""
Write-Host "Installer ready: $($item.FullName)" -ForegroundColor Green
Write-Host ("Size: {0:N0} bytes ({1} MB)" -f $item.Length, $sizeMb) -ForegroundColor Green
exit 0
