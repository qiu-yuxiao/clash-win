# ClashWin 便携版打包（1:1 对齐 Clash Mini scripts/portable.mjs）
#
# Mini 的做法（scripts/portable.mjs:14-55）：
#   1) 在配置目录里写一个空的 PORTABLE 标记文件（:25-27）
#   2) 把 exe / 内核 / resources 打平进 zip，再把配置目录作为 .config 打进去（:41-48）
#   3) 命名 <Name>_<version>_<arch>_portable.zip（:54），上传到同一个 Release（:88-93）
#
# ClashWin 的对应物：
#   - 便携标记 = <exe>\.config\PORTABLE（src/util/dirs.cpp:75-81，← dirs.rs:26）
#   - 便携配置目录 = <exe>\.config\io.github.clash-win.clash-win（首次运行自建）
#   因此本包只放一个空的 .config\PORTABLE，不带任何本机配置残留（干净出厂态，
#   首启引导与语言选择才会按 Mini 的首次运行逻辑走）。
#
# 目录布局不用 Mini 的「内核平铺在根」，而用 ClashWin 自己的安装后布局
# （exe 同级 sidecar\ resources\ locales\）——ClashWin 的内核查找路径就在 sidecar\，
# 平铺会找不到内核。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\portable.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\portable.ps1 -Version 0.1.0 -Configuration Release
#
# 产物：dist\ClashWin_<version>_x64_portable.zip（解压即用，零安装、零 UAC）

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$Version = '',
    [string]$OutDir = '',
    [switch]$KeepStage
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BinDir   = Join-Path $RepoRoot "build\bin\$Configuration"
$DistDir  = if ($OutDir) { $OutDir } else { Join-Path $RepoRoot 'dist' }

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

# ---------- 1. 版本号：-Version > constants.h 的 kAppVersion > 兜底 ----------
if (-not $Version) {
    $consts = Join-Path $RepoRoot 'include\clashwin\config\constants.h'
    if (Test-Path -LiteralPath $consts) {
        $hit = Select-String -LiteralPath $consts -Pattern 'kAppVersion\s*=\s*"([^"]+)"' |
               Select-Object -First 1
        if ($hit) { $Version = $hit.Matches[0].Groups[1].Value }
    }
    if (-not $Version) { $Version = '0.1.0' }
}

$OutFile = Join-Path $DistDir "ClashWin_${Version}_x64_portable.zip"

# ---------- 2. 校验构建产物 ----------
if (-not (Test-Path -LiteralPath $BinDir)) {
    Fail "未找到 $BinDir。先编译：cmake --build build --config $Configuration"
}

$required = @(
    (Join-Path $BinDir 'clashwin.exe'),
    (Join-Path $BinDir 'sidecar\mini-mihomo.exe'),
    (Join-Path $BinDir 'sidecar\clash-verge-service.exe'),
    (Join-Path $BinDir 'sidecar\clash-verge-service-install.exe'),
    (Join-Path $BinDir 'sidecar\clash-verge-service-uninstall.exe'),
    (Join-Path $BinDir 'sidecar\enableLoopback.exe'),
    (Join-Path $BinDir 'resources\Country.mmdb'),
    (Join-Path $BinDir 'resources\geosite.dat'),
    (Join-Path $BinDir 'resources\gfwlist.yaml'),
    (Join-Path $BinDir 'locales')
)
$missing = @()
foreach ($p in $required) {
    if (-not (Test-Path -LiteralPath $p)) { $missing += $p }
}
if ($missing.Count -gt 0) {
    Fail ("缺少必需产物：`n  " + ($missing -join "`n  "))
}

# 进程占用会让复制/替换失败，先明确报出来（比莫名 IO 错误好定位）
$running = Get-Process -Name 'clashwin' -ErrorAction SilentlyContinue
if ($running) {
    Fail ("clashwin.exe 正在运行（PID " + (($running | ForEach-Object { $_.Id }) -join ',') + "），先退出再打包。")
}

# ---------- 3. 暂存（安装后布局 + 便携标记） ----------
$stage = Join-Path $env:TEMP ("clashwin-portable-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $stage | Out-Null

try {
    Copy-Item -LiteralPath (Join-Path $BinDir 'clashwin.exe') -Destination (Join-Path $stage 'clashwin.exe') -Force
    Copy-Item -LiteralPath (Join-Path $BinDir 'sidecar')   -Destination (Join-Path $stage 'sidecar')   -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $BinDir 'resources') -Destination (Join-Path $stage 'resources') -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $BinDir 'locales')   -Destination (Join-Path $stage 'locales')   -Recurse -Force

    # 便携标记：空的 .config\PORTABLE（← portable.mjs:25-27 / dirs.cpp:75-81）
    $cfgDir = Join-Path $stage '.config'
    New-Item -ItemType Directory -Path $cfgDir | Out-Null
    [System.IO.File]::WriteAllBytes((Join-Path $cfgDir 'PORTABLE'), @())

    # 根目录再放一份用户必读（← portable.mjs:48 的同名做法）
    $readme = Join-Path $stage 'resources\用户必读.txt'
    if (Test-Path -LiteralPath $readme) {
        Copy-Item -LiteralPath $readme -Destination (Join-Path $stage '用户必读.txt') -Force
    }

    # ---------- 4. 打包 ----------
    if (-not (Test-Path -LiteralPath $DistDir)) {
        New-Item -ItemType Directory -Path $DistDir | Out-Null
    }
    if (Test-Path -LiteralPath $OutFile) {
        Remove-Item -LiteralPath $OutFile -Force
    }

    Write-Host "Packing $OutFile ..." -ForegroundColor Cyan
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $stage,
        $OutFile,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false)
}
finally {
    if (-not $KeepStage) {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host "Stage kept: $stage" -ForegroundColor Yellow
    }
}

# ---------- 5. 校验 zip 内容（关键条目必须在，防止空包） ----------
$mustHave = @(
    'clashwin.exe',
    '.config/PORTABLE',
    'sidecar/mini-mihomo.exe',
    'resources/Country.mmdb',
    'locales/zh/home.json',
    '用户必读.txt'
)

$zip = [System.IO.Compression.ZipFile]::OpenRead($OutFile)
try {
    $names = $zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') }
} finally {
    $zip.Dispose()
}

$absent = @()
foreach ($n in $mustHave) {
    if (-not ($names -contains $n)) { $absent += $n }
}
if ($absent.Count -gt 0) {
    Fail ("zip 缺少条目：" + ($absent -join ', '))
}

$item = Get-Item -LiteralPath $OutFile
Write-Host ""
Write-Host "Portable package ready: $($item.FullName)" -ForegroundColor Green
Write-Host ("Size: {0:N0} bytes ({1} MB), {2} entries" -f $item.Length, [math]::Round($item.Length / 1MB, 2), $names.Count) -ForegroundColor Green
Write-Host "Layout: clashwin.exe + sidecar\ + resources\ + locales\ + .config\PORTABLE + 用户必读.txt" -ForegroundColor Gray
