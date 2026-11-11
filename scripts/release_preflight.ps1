<#
.SYNOPSIS
  发版前本地预检 —— 拦截 v0.2.2 / v0.2.3 反复踩过的 CI 失败。

.DESCRIPTION
  在「提交 → 推 dev → 快进 main → 打 tag → 触发 CI」之前跑一遍本脚本。
  任何一项 FAIL 都意味着这次推送大概率会白跑一轮 CI（18 分钟），
  先本地改完再推。本脚本零副作用，只读取、不修改、不推送。

  覆盖的坑（均来自真实发版失败复盘）：
    GATE 1  版本单一事实源：constants.h == clashwin.nsi == updater/app-update.json == 待打 tag
    GATE 2  便携包预校验条目与实际 sidecar 内核名一致
            （0.2.3 第二次 run 失败根因：release.yml 写死 'sidecar/mini-mihomo.exe'，
             而内核改名后是 win-mihomo.exe）
    GATE 3  t35 发布就绪用例的版本断言 == 版本（CI 回归会卡死版本写死用例）
    GATE 4  测试里写死的内核名（test_config_template / test_core_kill）== kCoreProcessToken
    GATE 5  sidecar 5 个上游二进制基线 5/5（大小 + SHA256）

.PARAMETER Version
  本次要发布的版本号（如 0.2.4）。不传则只校验仓库内三个版本号互相等，
  且要求你打算打的 tag 与它们一致——传了该参数会额外比对 tag。

.EXAMPLE
  powershell -NoProfile -File scripts/release_preflight.ps1 -Version 0.2.4
#>
param(
  [string]$Version = ""
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Get-Item $PSScriptRoot).Parent.FullName
$pass = 0; $fail = 0
function Check($name, $ok, $detail = "") {
  if ($ok) { Write-Host ("  [PASS] " + $name) ; $script:pass++ }
  else     { Write-Host ("  [FAIL] " + $name + "  -> " + $detail); $script:fail++ }
}

Write-Host "=== Clash Win 发版预检 (preflight) ===" -ForegroundColor Cyan
Write-Host ("仓库根: " + $RepoRoot)

# ---------- GATE 1: 版本单一事实源 ----------
Write-Host "`n[1] 版本号一致性" -ForegroundColor Yellow
function Get-Ver($path, $pattern) {
  if (-not (Test-Path $path)) { return $null }
  $c = Get-Content $path -Raw
  if ($c -match $pattern) { return $Matches[1] }
  return $null
}
$cVer   = Get-Ver "$RepoRoot/include/clashwin/config/constants.h"   'kAppVersion\s*=\s*"([^"]+)"'
$nsiVer = Get-Ver "$RepoRoot/installer/clashwin.nsi"               '!define VERSION "([^"]+)"'
$jsonVer= Get-Ver "$RepoRoot/updater/app-update.json"              '"version"\s*:\s*"([^"]+)"'
Check "constants.h kAppVersion"            ($null -ne $cVer)   ("缺失，path=" + "$RepoRoot/include/clashwin/config/constants.h")
Check "clashwin.nsi VERSION"               ($null -ne $nsiVer) ("缺失")
Check "updater/app-update.json version"    ($null -ne $jsonVer)("缺失")
if ($cVer -and $nsiVer -and $jsonVer) {
  Check "三处版本互相等" ($cVer -eq $nsiVer -and $nsiVer -eq $jsonVer) ("constants=$cVer  nsi=$nsiVer  json=$jsonVer")
  if ($Version) {
    Check "待打 tag 版本 == 三处版本" ($Version -eq $cVer) ("tag=$Version  三处=$cVer")
  }
}

# ---------- GATE 2: 便携包预校验条目 vs 实际 sidecar 内核名 ----------
Write-Host "`n[2] 便携包条目与实际 sidecar 内核名一致" -ForegroundColor Yellow
$scanFiles = @(
  "$RepoRoot/.github/workflows/release.yml",
  "$RepoRoot/scripts/portable.ps1",
  "$RepoRoot/installer/build_installer.ps1"
)
$sidecarExes = @()
if (Test-Path "$RepoRoot/sidecar") {
  $sidecarExes = (Get-ChildItem "$RepoRoot/sidecar" -Filter *.exe).Name
}
$refs = @()
foreach ($f in $scanFiles) {
  if (-not (Test-Path $f)) { continue }
  $c = Get-Content $f -Raw
  foreach ($m in [regex]::Matches($c, "sidecar/([\w\-\.]+)")) { $refs += $m.Groups[1].Value }
}
$refs = $refs | Sort-Object -Unique
if ($refs.Count -eq 0) {
  Check "找到 sidecar 条目引用" $false "三个脚本里都没有 sidecar/*.exe 引用"
} else {
  foreach ($r in $refs) {
    $hit = $sidecarExes -contains $r
    Check ("脚本引用 sidecar/$r 在 sidecar/ 真实存在") $hit ("sidecar/ 现有: " + ($sidecarExes -join ', '))
  }
}

# ---------- GATE 3: t35 版本断言 ----------
Write-Host "`n[3] t35 发布就绪用例版本断言" -ForegroundColor Yellow
$t35 = "$RepoRoot/tests/test_t35_release_readiness.cpp"
if (Test-Path $t35) {
  $c = Get-Content $t35 -Raw
  if ($c -match "kAppVersion\)\s*==\s*""([^""]+)""") {
    $t35Ver = $Matches[1]
    Check "t35 断言版本 == 版本号" ($t35Ver -eq $cVer) ("t35=$t35Ver  期望=$cVer")
  } else {
    Check "t35 含版本断言" $false "未匹配到 kAppVersion == ""..."""
  }
} else { Check "t35 文件存在" $false $t35 }

# ---------- GATE 4: 测试写死内核名 == kCoreProcessToken ----------
Write-Host "`n[4] 测试写死内核名 == kCoreProcessToken" -ForegroundColor Yellow
$token = Get-Ver "$RepoRoot/include/clashwin/core/constants.h" 'kCoreProcessToken[^=]*=\s*L?"([^"]+)"'
if ($token) {
  $tkExe = $token + ".exe"
  # test_config_template.cpp: clash_core: <name>
  $tc = Get-Content "$RepoRoot/tests/test_config_template.cpp" -Raw
  if ($tc -match "clash_core:\s*([\w\-]+)") {
    Check "test_config_template clash_core == token" ($Matches[1] -eq $token) ("got=$($Matches[1])  expect=$token")
  } else { Check "test_config_template 含 clash_core" $false "未匹配" }
  # test_core_kill.cpp: 进程名引用
  $kc = Get-Content "$RepoRoot/tests/test_core_kill.cpp" -Raw
  $badKill = [regex]::Matches($kc, 'L"([\w\-]*mini-mihomo[\w\-]*)"') | ForEach-Object { $_.Groups[1].Value }
  Check "test_core_kill 无残留 mini-mihomo 进程名" ($badKill.Count -eq 0) ("残留: " + ($badKill -join ', '))
} else { Check "读取 kCoreProcessToken" $false "constants.h 未找到" }

# ---------- GATE 5: sidecar 5/5 基线 ----------
Write-Host "`n[5] sidecar 上游二进制基线 5/5" -ForegroundColor Yellow
$vs = "$RepoRoot/scripts/verify_sidecar.ps1"
if (Test-Path $vs) {
  & powershell -NoProfile -File $vs | Out-Null
  $code = $LASTEXITCODE
  Check "verify_sidecar.ps1 退出码 0（5/5）" ($code -eq 0) ("exit=$code")
} else { Check "verify_sidecar.ps1 存在" $false $vs }

# ---------- GATE 6: 本地 sidecar 必须含 win-mihomo（setup 内核源头）----------
# v0.2.5 复盘：归档漏内核 + 无步骤拦截 → setup 不含 win-mihomo。
# 本地发版前必须有内核；CI 侧另有「归档缺失时上游补齐 + setup 体积断言」双保险。
Write-Host "`n[6] 本地 sidecar 含 win-mihomo.exe（setup 内核源头）" -ForegroundColor Yellow
$wmLocal = Join-Path $RepoRoot 'sidecar\win-mihomo.exe'
if (Test-Path -LiteralPath $wmLocal) {
  $wmItem = Get-Item -LiteralPath $wmLocal
  Check "sidecar/win-mihomo.exe 存在" $true ("$('{0:N0}' -f $wmItem.Length) bytes")
} else {
  Check "sidecar/win-mihomo.exe 存在" $false "本地 sidecar/ 缺失内核；本地构建 setup 会失败，CI 也会走上游补齐，但发版前应先补上"
}

# ---------- 汇总 ----------
Write-Host "`n=== 结果: PASS=$pass  FAIL=$fail ===" -ForegroundColor Cyan
if ($fail -gt 0) {
  Write-Host "存在 FAIL，先本地改完再推送（不要等 CI 跑 18 分钟才暴露）。" -ForegroundColor Red
  exit 1
}
Write-Host "全部通过，可以走发布流程（提交 → 推 dev → 快进 main → 打 tag → 等 CI）。" -ForegroundColor Green
exit 0
