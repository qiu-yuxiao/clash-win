# ClashWin installer build wrapper
# Stages the Release artifacts and invokes NSIS (makensis) to produce
# dist\ClashWin_<version>_x64-setup.exe. Fails loudly when anything is missing.
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$Makensis = '',
    [string]$Version = '0.1.3',
    [switch]$SkipStage,
    [switch]$SkipSign
)

$ErrorActionPreference = 'Stop'

$RepoRoot  = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BinDir    = Join-Path $RepoRoot "build\bin\$Configuration"
$StageDir  = Join-Path $PSScriptRoot 'stage'
$DistDir   = Join-Path $RepoRoot 'dist'
$ScriptNsi = Join-Path $PSScriptRoot 'clashwin.nsi'
$IconFile  = Join-Path $RepoRoot 'resources\icons\icon.ico'
$OutFile   = Join-Path $DistDir "ClashWin_${Version}_x64-setup.exe"

# ---------- 0. 管理员自提升 ----------
# !uninstfinalize 钩子要用 LocalMachine\My 证书私钥（signtool /sm）签卸载器 blob，
# 必须在管理员上下文执行。这里一次性提升，后续签名（sign.ps1）不会再弹 UAC。
$IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $SkipSign -and -not $IsAdmin) {
    Write-Host "需要管理员权限签名卸载器，正在请求提升..." -ForegroundColor Yellow
    $q = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $PSCommandPath + '"'),
           '-Configuration', $Configuration, '-Version', $Version)
    if ($Makensis) { $q += @('-Makensis', ('"' + $Makensis + '"')) }
    if ($SkipStage) { $q += '-SkipStage' }
    try {
        $p = Start-Process powershell -ArgumentList $q -Verb RunAs -Wait -PassThru -ErrorAction Stop
    } catch {
        Write-Host ("UAC 提升被拒绝或不可用：{0}" -f $_.Exception.Message) -ForegroundColor Red
        Write-Host '未生成安装包；请在管理员 PowerShell 窗口中重新执行。' -ForegroundColor Yellow
        $SkipSign = $true
    }
    if ($null -eq $p -and -not $SkipSign) { Write-Host 'UAC 提升已取消；未生成安装包。' -ForegroundColor Red; exit 2 }
    if (-not $SkipSign) { exit $p.ExitCode }
}

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
        (Join-Path ${env:ProgramFiles} 'NSIS\makensis.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe')
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return (Resolve-Path -LiteralPath $c).Path }
    }
    foreach ($key in @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\NSIS',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\NSIS')) {
        $props = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($props -and $props.InstallLocation) {
            $exe = Join-Path $props.InstallLocation 'makensis.exe'
            if (Test-Path -LiteralPath $exe) { return (Resolve-Path -LiteralPath $exe).Path }
        }
    }
    return $null
}

# ---------- 1. 校验 Release 产物 ----------
if (-not (Test-Path -LiteralPath $BinDir)) {
    Fail "Release output directory not found: $BinDir. Build first: cmake --build build --config $Configuration"
}

$missing = @()
$mainExe = Join-Path $BinDir 'clashwin.exe'
if (-not (Test-Path -LiteralPath $mainExe)) { $missing += $mainExe }

$sidecarNames = @(
    'mini-mihomo.exe',
    'clash-verge-service.exe',
    'clash-verge-service-install.exe',
    'clash-verge-service-uninstall.exe',
    'enableLoopback.exe'
)
foreach ($name in $sidecarNames) {
    $p = Join-Path (Join-Path $BinDir 'sidecar') $name
    if (-not (Test-Path -LiteralPath $p)) { $missing += $p }
}

foreach ($name in @('Country.mmdb', 'geosite.dat', 'gfwlist.yaml')) {
    $p = Join-Path (Join-Path $BinDir 'resources') $name
    if (-not (Test-Path -LiteralPath $p)) { $missing += $p }
}

$localesDir = Join-Path $BinDir 'locales'
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

# ---------- 1.5 代码签名：先签 Release 产物，保证安装包内的 exe 也是签过的 ----------
# scripts\sign.ps1 幂等（证书不存在才创建），首次会请求一次管理员提升。
$SignScript = Join-Path $RepoRoot 'scripts\sign.ps1'
if (-not $SkipSign -and (Test-Path -LiteralPath $SignScript)) {
    Write-Host "Signing Release binaries before packaging ..." -ForegroundColor Cyan
    & powershell -NoProfile -ExecutionPolicy Bypass -File $SignScript
    if ($LASTEXITCODE -ne 0) {
        Fail "代码签名失败（scripts\sign.ps1 exit=$LASTEXITCODE）。安装包未生成，避免打出未签名的包。"
    }
} else {
    Write-Warning '跳过代码签名。'
}

# ---------- 2. 暂存（保持安装后的目录布局：exe 同级 sidecar\ resources\ locales\） ----------
if (-not $SkipStage) {
    if (Test-Path -LiteralPath $StageDir) {
        Remove-Item -LiteralPath $StageDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $StageDir | Out-Null
    Copy-Item -LiteralPath $mainExe -Destination (Join-Path $StageDir 'clashwin.exe') -Force
    Copy-Item -LiteralPath (Join-Path $BinDir 'sidecar') -Destination (Join-Path $StageDir 'sidecar') -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $BinDir 'resources') -Destination (Join-Path $StageDir 'resources') -Recurse -Force
    Copy-Item -LiteralPath $localesDir -Destination (Join-Path $StageDir 'locales') -Recurse -Force

    # 应用按 dirs::service_path() 在 <exe>\resources\clash-verge-service.exe 查找服务程序，
    # 因此把服务三件套同时放进 resources\（安装/卸载器也直接从这里调用）。
    foreach ($name in @('clash-verge-service.exe', 'clash-verge-service-install.exe', 'clash-verge-service-uninstall.exe')) {
        Copy-Item -LiteralPath (Join-Path (Join-Path $BinDir 'sidecar') $name) `
                  -Destination (Join-Path (Join-Path $StageDir 'resources') $name) -Force
    }
    Write-Host "Staged artifacts into $StageDir"

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

# ---------- 4. 编译 ----------
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
# License 页已随 1:1 对齐移除（Mini bundle.license 未配置 → installer.nsi:161 不渲染）
# 卸载器签名：clashwin.nsi 的 !uninstfinalize 在编译期对 %TEMP%\nst*.tmp 就地签名，
# 该 blob 随后被 NSIS 嵌入安装包 → 安装后的 uninstall.exe 也带有效签名。
$UninstSignScript = Join-Path $RepoRoot 'scripts\sign_nsis_uninstaller.ps1'
$UninstSignLog    = Join-Path $env:TEMP 'clashwin-uninst-sign.log'
if (-not $SkipSign -and (Test-Path -LiteralPath $UninstSignScript)) {
    Remove-Item -LiteralPath $UninstSignLog -Force -ErrorAction SilentlyContinue
    $defines += "/DUNINST_SIGN_SCRIPT=$UninstSignScript"
    $defines += "/DUNINST_SIGN_LOG=$UninstSignLog"
} else {
    Write-Warning "未找到 scripts\sign_nsis_uninstaller.ps1，卸载器将不带签名。"
}

# Optional Tauri helper plugin: lets the finish page launch the app unelevated.
$nsisRoot = Split-Path -Parent $makensisExe
$tauriPlugin = Join-Path $nsisRoot 'Plugins\x86-unicode\additional\nsis_tauri_utils.dll'
if (Test-Path -LiteralPath $tauriPlugin) {
    $defines += "/DTAURI_UTILS_PLUGIN_DIR=$(Join-Path $nsisRoot 'Plugins\x86-unicode\additional')"
}

Write-Host "Compiling $ScriptNsi ..."
& $makensisExe @defines $ScriptNsi
if ($LASTEXITCODE -ne 0) {
    Fail "makensis failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $OutFile)) {
    Fail "Expected installer was not produced: $OutFile"
}

# 校验卸载器签名：钩子脚本必须明确回报 RC=0 且 VERIFY=Valid，否则不出包，
# 避免留下“主程序已签、卸载器未签”的半成品安装包。
if ($defines -contains "/DUNINST_SIGN_SCRIPT=$UninstSignScript") {
    $logText = if (Test-Path -LiteralPath $UninstSignLog) { Get-Content -LiteralPath $UninstSignLog -Raw } else { '' }
    if ($logText -notmatch 'UNINST_SIGN_RC=0' -or $logText -notmatch 'UNINST_SIGN_VERIFY=Valid') {
        Fail ("卸载器签名失败（未见成功记录：$UninstSignLog）。安装包未产出。" + "`n" + $logText)
    }
    $blobLine = ($logText -split "`n" | Where-Object { $_ -match '^UNINST_SIGN_BLOB=' } | Select-Object -First 1)
    Write-Host "Uninstaller signed: $blobLine" -ForegroundColor Green
}

$item = Get-Item -LiteralPath $OutFile
$sizeMb = [math]::Round($item.Length / 1MB, 2)

# ---------- 5. 代码签名：签最终安装包（NSIS 出包后自动签） ----------
if (-not $SkipSign -and (Test-Path -LiteralPath $SignScript)) {
    Write-Host "Signing installer ..." -ForegroundColor Cyan
    & powershell -NoProfile -ExecutionPolicy Bypass -File $SignScript -Paths $OutFile
    if ($LASTEXITCODE -ne 0) {
        Fail "安装包签名失败（scripts\sign.ps1 exit=$LASTEXITCODE）: $OutFile"
    }
}

Write-Host ""
Write-Host "Installer ready: $($item.FullName)" -ForegroundColor Green
Write-Host ("Size: {0:N0} bytes ({1} MB)" -f $item.Length, $sizeMb) -ForegroundColor Green





