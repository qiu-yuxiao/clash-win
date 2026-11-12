<#
.SYNOPSIS
  Clash Win 发版前全维度静态与基线预检（零副作用，只读校验）。

.DESCRIPTION
  在本地准备、pre-push 钩子拦截、以及 CI 构建前统一执行本脚本：
    GATE 1  版本四方严格一致：constants.h == clashwin.nsi == clashwin.rc == 目标版本号
    GATE 2  更新清单安全契约：updater/app-update.json 合法、无上游污染、版本不倒挂
    GATE 3  发布说明与文档门禁：CHANGELOG.md 含对应版本章节且无 TODO/TBD、常青文档与 Issue 模板链接有效
    GATE 4  测试注册与内核名契约：tests/*.cpp 全量登记 CMakeLists.txt、内核进程名一致
    GATE 5  sidecar 5/5 二进制基线（含 win-mihomo.exe 大小与 SHA256）
    GATE 6  UI 源码归属门禁（ui_src_gate.ps1）
    GATE 7  脚本编码门禁：scripts/*.ps1 与 installer/*.ps1 全含 UTF-8 BOM 且无孤立 CR；git hook 为 LF
#>
[CmdletBinding()]
param(
  [string]$Version = '',
  [switch]$SkipSidecar
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$pass = 0
$fail = 0

function Check([string]$name, [bool]$ok, [string]$detail = '') {
  if ($ok) {
    Write-Host ("  [PASS] " + $name) -ForegroundColor Green
    $script:pass++
  } else {
    Write-Host ("  [FAIL] " + $name + $(if ($detail) { " -> $detail" } else { '' })) -ForegroundColor Red
    $script:fail++
  }
}

function Read-Utf8([string]$path) {
  $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
  return [System.IO.File]::ReadAllText($path, $utf8)
}

function Get-BaseSemVer([string]$ver) {
  if (-not $ver) { return $null }
  if ($ver -match '^([0-9]+\.[0-9]+\.[0-9]+)') { return $Matches[1] }
  return $null
}

Write-Host "=== Clash Win 发版预检 (Preflight Gates) ===" -ForegroundColor Cyan
Write-Host ("仓库根: " + $RepoRoot)

# ---------- GATE 1: 版本号一致性（constants.h / clashwin.nsi / clashwin.rc / -Version） ----------
Write-Host "`n[GATE 1] 版本号单一事实源与跨文件同步" -ForegroundColor Yellow
$constPath = Join-Path $RepoRoot 'include\clashwin\config\constants.h'
$nsiPath   = Join-Path $RepoRoot 'installer\clashwin.nsi'
$rcPath    = Join-Path $RepoRoot 'src\clashwin.rc'

$cVer = $null; $cSubUa = $null; $nsiVer = $null; $rcFileStr = $null; $rcProdStr = $null; $rcFileNum = $null; $rcProdNum = $null
if (Test-Path -LiteralPath $constPath) {
  $txt = Read-Utf8 $constPath
  if ($txt -match 'kAppVersion\s*=\s*"([^"]+)"') { $cVer = $Matches[1] }
  if ($txt -match 'kSubscriptionCompatUserAgent\s*=\s*"([^"]+)"') { $cSubUa = $Matches[1] }
}
if (Test-Path -LiteralPath $nsiPath) {
  $txt = Read-Utf8 $nsiPath
  if ($txt -match '!define\s+VERSION\s+"([^"]+)"') { $nsiVer = $Matches[1] }
}
if (Test-Path -LiteralPath $rcPath) {
  $txt = Read-Utf8 $rcPath
  if ($txt -match 'FILEVERSION\s+([0-9]+,[0-9]+,[0-9]+,[0-9]+)')    { $rcFileNum = $Matches[1] }
  if ($txt -match 'PRODUCTVERSION\s+([0-9]+,[0-9]+,[0-9]+,[0-9]+)') { $rcProdNum = $Matches[1] }
  if ($txt -match '"FileVersion",\s*"([^"]+)"')                     { $rcFileStr = $Matches[1] }
  if ($txt -match '"ProductVersion",\s*"([^"]+)"')                  { $rcProdStr = $Matches[1] }
}

$semVerPattern = '^[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.]+)?$'
$subUaPattern  = '^clash-verge/v([2-9]|[1-9][0-9]+)\.[0-9]+\.[0-9]+$'
Check 'constants.h kAppVersion 存在且符合 SemVer' ([bool]($cVer -and ($cVer -match $semVerPattern))) "got='$cVer'"
Check 'constants.h 订阅默认 UA 锚定上游 v2+ 纯净单段标识' ([bool]($cSubUa -and ($cSubUa -match $subUaPattern))) "got='$cSubUa'"
Check 'clashwin.nsi VERSION == kAppVersion'       ($nsiVer -eq $cVer) "nsi='$nsiVer' expected='$cVer'"

$baseVer = Get-BaseSemVer $cVer
$expectedComma = if ($baseVer) { ($baseVer -replace '\.', ',') + ',0' } else { '' }
Check 'clashwin.rc FILEVERSION/PRODUCTVERSION 同步' ($rcFileNum -eq $expectedComma -and $rcProdNum -eq $expectedComma) `
  "FILEVERSION='$rcFileNum' PRODUCTVERSION='$rcProdNum' expected='$expectedComma'"
Check 'clashwin.rc FileVersion/ProductVersion 同步' ($rcFileStr -eq $cVer -and $rcProdStr -eq $cVer) `
  "FileVersion='$rcFileStr' ProductVersion='$rcProdStr' expected='$cVer'"

$targetVer = if ($Version) { $Version } else { $cVer }
if ($Version) {
  Check '目标版本号 -Version == kAppVersion' ($Version -eq $cVer) "arg='$Version' constants.h='$cVer'"
}

# ---------- GATE 2: updater/app-update.json 安全契约 ----------
Write-Host "`n[GATE 2] 更新清单契约检查" -ForegroundColor Yellow
$manifestPath = Join-Path $RepoRoot 'updater\app-update.json'
if (Test-Path -LiteralPath $manifestPath) {
  try {
    $mRaw = Read-Utf8 $manifestPath
    $mObj = $mRaw | ConvertFrom-Json
    $noMini = ($mRaw -notmatch 'clash-mini|Clash\.Mini|3\.2\.2')
    Check 'app-update.json 不含上游 Clash Mini 残留' $noMini '发现 clash-mini 或 3.2.2 字样'
    $mBase = Get-BaseSemVer ([string]$mObj.version)
    $verNotAhead = ([bool]$mBase -and [bool]$baseVer -and ([version]$mBase -le [version]$baseVer))
    Check 'app-update.json 版本不高于当前 kAppVersion' $verNotAhead "manifest='$($mObj.version)' kAppVersion='$cVer'"
    $winUrl = [string]$mObj.platforms.'windows-x86_64'.url
    $urlOk = ([string]::IsNullOrEmpty($winUrl) -or ($winUrl -match '^https://github\.com/qiu-yuxiao/clash-win/releases/download/'))
    Check 'app-update.json 下载地址归属本仓库' $urlOk "url='$winUrl'"
  } catch {
    Check 'app-update.json JSON 解析合法' $false $_.Exception.Message
  }
} else {
  Check 'updater/app-update.json 存在' $false $manifestPath
}

# ---------- GATE 3: 发布说明与文档门禁 ----------
Write-Host "`n[GATE 3] 发布说明 (CHANGELOG.md) 与常青文档检查" -ForegroundColor Yellow
$changelogPath = Join-Path $RepoRoot 'CHANGELOG.md'
if ((Test-Path -LiteralPath $changelogPath) -and $targetVer) {
  $cl = Read-Utf8 $changelogPath
  $escapedVer = [regex]::Escape($targetVer)
  $secPattern = "(?ms)^##\s+v$escapedVer\b[^\r\n]*\r?\n(.*?)(?=^##\s+v|\z)"
  if ($cl -match $secPattern) {
    $secBody = $Matches[1].Trim()
    Check "CHANGELOG.md 含 ## v$targetVer 章节且内容充实" ($secBody.Length -ge 20) "正文长度=$($secBody.Length)"
    $hasTodo = ($secBody -match '(?i)\b(TODO|TBD|FIXME)\b|待补充|请补充')
    Check "CHANGELOG.md ## v$targetVer 不含未填占位词 (TODO/TBD)" (-not $hasTodo) '请将 CHANGELOG 中 TODO 替换为真实变更说明'
  } else {
    Check "CHANGELOG.md 含 ## v$targetVer 章节" $false "缺少 '## v$targetVer' 标题"
  }
} else {
  Check 'CHANGELOG.md 存在' (Test-Path -LiteralPath $changelogPath) $changelogPath
}

$knoPath = Join-Path $RepoRoot 'docs\KNOWN_LIMITATIONS.md'
Check '常青文档 docs/KNOWN_LIMITATIONS.md 存在' (Test-Path -LiteralPath $knoPath) $knoPath

$tplBad = @()
Get-ChildItem -Path (Join-Path $RepoRoot '.github\ISSUE_TEMPLATE') -Filter *.yml -ErrorAction SilentlyContinue | ForEach-Object {
  $yt = Read-Utf8 $_.FullName
  foreach ($m in [regex]::Matches($yt, 'blob/main/(docs/[^\s\)\]]+)')) {
    $relDoc = $m.Groups[1].Value
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot $relDoc))) {
      $tplBad += "$($_.Name):$relDoc"
    }
  }
}
Check 'Issue 模板文档链接全部有效（无死链）' ($tplBad.Count -eq 0) ($tplBad -join ', ')

# ---------- GATE 4: 单测全量注册与内核名契约 ----------
Write-Host "`n[GATE 4] 单测注册与内核名契约" -ForegroundColor Yellow
$cmakeTxt = Read-Utf8 (Join-Path $RepoRoot 'CMakeLists.txt')
$unregistered = @()
Get-ChildItem -Path (Join-Path $RepoRoot 'tests') -Filter *.cpp | ForEach-Object {
  $tc = Read-Utf8 $_.FullName
  if ($tc -match 'UT_TEST\(') {
    if ($cmakeTxt -notmatch [regex]::Escape("tests/$($_.Name)")) {
      $unregistered += $_.Name
    }
  }
}
Check 'tests/*.cpp 含 UT_TEST 的文件 100% 登记进 CMakeLists.txt' ($unregistered.Count -eq 0) ($unregistered -join ', ')

$coreConstPath = Join-Path $RepoRoot 'include\clashwin\core\constants.h'
$token = $null
if (Test-Path -LiteralPath $coreConstPath) {
  $cc = Read-Utf8 $coreConstPath
  if ($cc -match 'kCoreProcessToken[^=]*=\s*L?"([^"]+)"') { $token = $Matches[1] }
}
Check '读取 kCoreProcessToken' ([bool]$token) $coreConstPath
if ($token) {
  $tplTest = Read-Utf8 (Join-Path $RepoRoot 'tests\test_config_template.cpp')
  $tplMatch = ($tplTest -match "clash_core:\s*$([regex]::Escape($token))\b")
  Check "test_config_template.cpp clash_core == $token" $tplMatch ''
  $killTest = Read-Utf8 (Join-Path $RepoRoot 'tests\test_core_kill.cpp')
  $hasOwnToken = ($killTest -match [regex]::Escape("L`"$token.exe`"")) -and ($killTest -match 'kCoreProcessToken')
  Check "test_core_kill.cpp 对齐当前内核名 ($token.exe)" $hasOwnToken ''
}

# ---------- GATE 5: sidecar 二进制基线 ----------
Write-Host "`n[GATE 5] sidecar 上游二进制基线 (5/5)" -ForegroundColor Yellow
if ($SkipSidecar) {
  Write-Host '  [SKIP] 已指定 -SkipSidecar（仅用于尚未还原 sidecar 的纯静态阶段）' -ForegroundColor DarkGray
} else {
  $vs = Join-Path $RepoRoot 'scripts\verify_sidecar.ps1'
  & $vs | Out-Null
  $vsCode = $LASTEXITCODE
  Check 'verify_sidecar.ps1 5/5 字节数与 SHA256 全匹配' ($vsCode -eq 0) "exit=$vsCode"

  $scanFiles = @(
    (Join-Path $RepoRoot '.github\workflows\release.yml'),
    (Join-Path $RepoRoot 'scripts\portable.ps1'),
    (Join-Path $RepoRoot 'installer\build_installer.ps1')
  )
  $sidecarExes = @((Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'sidecar') -Filter *.exe -ErrorAction SilentlyContinue).Name)
  $missingRefs = @()
  foreach ($sf in $scanFiles) {
    if (-not (Test-Path -LiteralPath $sf)) { continue }
    $sc = Read-Utf8 $sf
    foreach ($m in [regex]::Matches($sc, 'sidecar[/\\]([\w\-\.]+\.exe)')) {
      $exeName = $m.Groups[1].Value
      if ($sidecarExes -notcontains $exeName) { $missingRefs += $exeName }
    }
  }
  $missingRefs = @($missingRefs | Sort-Object -Unique)
  Check '打包脚本与工作流引用的 sidecar/*.exe 全部真实存在' ($missingRefs.Count -eq 0) ($missingRefs -join ', ')
}

# ---------- GATE 6: UI 源码归属门禁 ----------
Write-Host "`n[GATE 6] UI 源码归属门禁" -ForegroundColor Yellow
$uiGate = Join-Path $RepoRoot 'scripts\ui_src_gate.ps1'
& $uiGate | Out-Null
$uiCode = $LASTEXITCODE
Check 'ui_src_gate.ps1 退出码 0' ($uiCode -eq 0) "exit=$uiCode"

# ---------- GATE 7: 脚本编码合规（UTF-8 BOM + 无孤立 CR + Git Hook 纯 LF） ----------
Write-Host "`n[GATE 7] 脚本与钩子编码合规" -ForegroundColor Yellow
$ps1Dirs = @((Join-Path $RepoRoot 'scripts'), (Join-Path $RepoRoot 'installer'))
$allPs1 = @()
foreach ($d in $ps1Dirs) {
  if (Test-Path -LiteralPath $d) {
    $allPs1 += @(Get-ChildItem -LiteralPath $d -Filter *.ps1 -Recurse | Select-Object -ExpandProperty FullName)
  }
}
$encBad = @(); $crBad = @()
foreach ($p in $allPs1) {
  $b = [System.IO.File]::ReadAllBytes($p)
  if (-not ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)) {
    $encBad += $p.Substring($RepoRoot.Length + 1)
  }
  for ($i = 0; $i -lt $b.Length - 1; $i++) {
    if ($b[$i] -eq 0x0D -and $b[$i + 1] -ne 0x0A) {
      $crBad += $p.Substring($RepoRoot.Length + 1)
      break
    }
  }
}
Check ("全部 {0} 个 .ps1 脚本含 UTF-8 BOM（兼容 PS 5.1 与 pwsh 7）" -f $allPs1.Count) ($encBad.Count -eq 0) ($encBad -join ', ')
Check ("全部 {0} 个 .ps1 脚本无孤立 CR" -f $allPs1.Count) ($crBad.Count -eq 0) ($crBad -join ', ')

$nsiBytes = [System.IO.File]::ReadAllBytes($nsiPath)
$nsiHasBom = ($nsiBytes.Length -ge 3 -and $nsiBytes[0] -eq 0xEF -and $nsiBytes[1] -eq 0xBB -and $nsiBytes[2] -eq 0xBF)
Check 'installer/clashwin.nsi 含 UTF-8 BOM（防 makensis ACP 乱码报错）' $nsiHasBom '缺少 UTF-8 BOM'

$hookPath = Join-Path $RepoRoot 'scripts\git-hooks\pre-push'
if (Test-Path -LiteralPath $hookPath) {
  $hb = [System.IO.File]::ReadAllBytes($hookPath)
  $hasCr = ($hb -contains [byte]0x0D)
  Check 'scripts/git-hooks/pre-push 为纯 LF 换行（防 /bin/sh 报错）' (-not $hasCr) '发现 CR 字节，请转为 LF'
} else {
  Check 'scripts/git-hooks/pre-push 存在' $false $hookPath
}

# ---------- 汇总 ----------
Write-Host "`n=== 预检汇总: PASS=$pass  FAIL=$fail ===" -ForegroundColor Cyan
if ($fail -gt 0) {
  Write-Host '预检未通过：请先在本地修复上述 [FAIL] 项后再继续发版。' -ForegroundColor Red
  exit 1
}
Write-Host '预检全部通过！' -ForegroundColor Green
exit 0
