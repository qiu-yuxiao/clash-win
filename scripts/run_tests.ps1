<#
.SYNOPSIS
  Clash Win 统一全量/过滤回归测试运行器（本地 PowerShell 5.1 与 CI pwsh 7 共用）。

.DESCRIPTION
  将 clashwin_test.exe 及其运行时依赖（sidecar / locales / assets\locales / resources）
  同步到隔离目录执行，绕开安全软件对 build\bin\Release 路径的写拦截，同时保证：
    1) 零硬编码开发机绝对路径（通过 $PSScriptRoot 动态定位仓库根并导出 CLASHWIN_REPO_ROOT）；
    2) 每次运行前清理隔离目录内的 locales 与 assets\locales，杜绝旧词条缓存残留；
    3) 统一设置 TZ=UTC-8（对齐 MSVC CRT 东八区 golden 夹具）并剥离污染代理环境变量；
    4) 严格双重校验：进程退出码 == 0 且摘要行 "0 failed, 0 assertion failures"。
#>
[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$Filter = '',
  [string]$RunDir = '',
  [ValidateSet('Release', 'Debug')]
  [string]$Configuration = 'Release',
  [string]$LogPath = '',
  [switch]$InPlace
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BinDir   = Join-Path $RepoRoot "build\bin\$Configuration"
$TestExe  = Join-Path $BinDir 'clashwin_test.exe'

if (-not (Test-Path -LiteralPath $TestExe)) {
  Write-Host "[FAIL] 未找到测试可执行文件: $TestExe（请先编译 clashwin_test）" -ForegroundColor Red
  exit 2
}

# 剥离可能引起冲突的大小写重复代理环境变量
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','http_proxy','https_proxy',
                 'ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy',
                 'NO_PROXY','no_proxy')) {
  [Environment]::SetEnvironmentVariable($n, $null, 'Process')
}
$env:CLASHWIN_REPO_ROOT = $RepoRoot
$env:TZ = 'UTC-8'

if ($InPlace) {
  $ExecDir = $BinDir
  $TargetExe = $TestExe
  if (-not $LogPath) { $LogPath = Join-Path $RepoRoot 'build\cwtest_logs\tests_inplace.log' }
} else {
  if (-not $RunDir) {
    $baseTmp = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'Temp' } else { [IO.Path]::GetTempPath() }
    $RunDir = Join-Path $baseTmp 'cwrun'
  } elseif (-not [IO.Path]::IsPathRooted($RunDir)) {
    $RunDir = Join-Path $RepoRoot $RunDir
  }

  New-Item -ItemType Directory -Force -Path $RunDir | Out-Null
  $TargetExe = Join-Path $RunDir 'cwrun.exe'
  try {
    Copy-Item -LiteralPath $TestExe -Destination $TargetExe -Force -ErrorAction Stop
  } catch {
    $TargetExe = Join-Path $RunDir ("cwrun_" + (Get-Date -Format 'HHmmss') + '.exe')
    Copy-Item -LiteralPath $TestExe -Destination $TargetExe -Force
  }

  # 同步运行时资产：locales / assets\locales / resources 先删再拷，防止嵌套目录或陈旧文件残留
  foreach ($cleanRel in @('locales', 'assets\locales', 'resources')) {
    $cleanTarget = Join-Path $RunDir $cleanRel
    if (Test-Path -LiteralPath $cleanTarget) {
      Remove-Item -LiteralPath $cleanTarget -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
  New-Item -ItemType Directory -Force -Path (Join-Path $RunDir 'assets') | Out-Null

  $sidecarSrc = Join-Path $RepoRoot 'sidecar'
  $sidecarDst = Join-Path $RunDir 'sidecar'
  if (Test-Path -LiteralPath $sidecarSrc) {
    New-Item -ItemType Directory -Force -Path $sidecarDst | Out-Null
    Get-ChildItem -LiteralPath $sidecarSrc -File | ForEach-Object {
      $dstFile = Join-Path $sidecarDst $_.Name
      if (-not (Test-Path -LiteralPath $dstFile) -or ((Get-Item -LiteralPath $dstFile).Length -ne $_.Length)) {
        Copy-Item -LiteralPath $_.FullName -Destination $dstFile -Force
      }
    }
  }

  foreach ($pair in @(
      @((Join-Path $BinDir 'locales'),        (Join-Path $RunDir 'locales')),
      @((Join-Path $BinDir 'resources'),      (Join-Path $RunDir 'resources')),
      @((Join-Path $RepoRoot 'assets\locales'), (Join-Path $RunDir 'assets\locales'))
  )) {
    if (Test-Path -LiteralPath $pair[0]) {
      Copy-Item -LiteralPath $pair[0] -Destination $pair[1] -Recurse -Force
    }
  }

  $runTmp = Join-Path $RunDir 'tmp'
  New-Item -ItemType Directory -Force -Path $runTmp | Out-Null
  Get-ChildItem -LiteralPath $runTmp -Force -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
  $env:TMP  = $runTmp
  $env:TEMP = $runTmp
  $ExecDir  = $RunDir

  if (-not $LogPath) {
    $LogPath = Join-Path $RepoRoot 'build\cwtest_logs\tests_full.log'
  }
}

if (-not [IO.Path]::IsPathRooted($LogPath)) {
  $LogPath = Join-Path $RepoRoot $LogPath
}
$logDir = Split-Path -Parent $LogPath
if ($logDir) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }

Push-Location $ExecDir
try {
  $prevEap = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  if ($Filter) {
    & $TargetExe $Filter > $LogPath 2>&1
  } else {
    & $TargetExe > $LogPath 2>&1
  }
  $code = $LASTEXITCODE
  $ErrorActionPreference = $prevEap
} finally {
  Pop-Location
}

$lines = @()
if (Test-Path -LiteralPath $LogPath) {
  $lines = @(Get-Content -LiteralPath $LogPath -Encoding UTF8)
}
$tailCount = [Math]::Min(12, $lines.Count)
$summaryText = if ($tailCount -gt 0) { ($lines[($lines.Count - $tailCount)..($lines.Count - 1)]) -join "`n" } else { '' }

Write-Host "---- Regression Summary ----" -ForegroundColor Cyan
if ($summaryText) { Write-Host $summaryText }
Write-Host ("EXIT=$code  LOG=$LogPath") -ForegroundColor Cyan

$failedCases = -1
$failedAsserts = -1
if ($summaryText -match '(\d+)\s+cases,\s+(\d+)\s+failed,\s+(\d+)\s+assertion\s+failures') {
  $totalCases    = [int]$Matches[1]
  $failedCases   = [int]$Matches[2]
  $failedAsserts = [int]$Matches[3]
}

if ($code -ne 0 -or $failedCases -ne 0 -or $failedAsserts -ne 0) {
  Write-Host "`n---- Failure Diagnostics ----" -ForegroundColor Red
  $diagHits = $lines | Where-Object { $_ -match '^\s*FAIL\s|^\[FAIL\]' }
  if ($diagHits) {
    $diagHits | Select-Object -First 60 | ForEach-Object { Write-Host $_ -ForegroundColor Red }
  } else {
    $lines | Select-Object -Last 60 | ForEach-Object { Write-Host $_ }
  }
  if ($code -eq 0) { $code = 1 }
  exit $code
}

Write-Host ("[PASS] {0} cases, 0 failed, 0 assertion failures" -f $totalCases) -ForegroundColor Green
exit 0
