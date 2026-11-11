<#
.SYNOPSIS
  Clash Win 唯一切口发版编排器 —— 把"遵守 release process"变成"不按流程就走不下去"。

.DESCRIPTION
  这是项目唯一受支持、且被强制的发版路径。它把原先散落在 RELEASE_PROCESS.md 里
  "建议你手动做"的步骤变成代码门禁：

    - 版本号只输入一次，脚本自动同步 constants.h / clashwin.nsi / app-update.json / t35 测试
      四处（彻底消灭"版本不一致"这一类错；app-update.json 同时被清空为占位符，
      因为真实清单由 publish 步骤运行时生成并推回 main）。
    - 自动脚手架发布材料（RELEASE_NOTES / KNOWN_LIMITATIONS / CHANGELOG / Issue 模板链接）。
    - 强制跑 release_preflight.ps1（GATE 1~7 全过才放行）。
    - 【关键门禁】-Build 会在本地编译并跑全量回归；v0.2.6 正是漏了本地回归，
      被 CI 在 18 分钟后才拦下 2 个测试失败。本地回归几秒~十几分钟就能拦下。
    - -Push 才会真正提交/推 dev/快进 main/打 tag/推 tag；此步需显式开关，
      不传 -Push 则只做本地准备，绝不碰远程。

  git pre-push 钩子（scripts/git-hooks/pre-push）会在你推 v* tag 时再拦一道：
  预检不过直接拒绝推送。CI build job 还原 sidecar 后也会重跑全部预检 gate，
  任一失败整条 run 失败、绝不发布。三层兜底，绕不过去。

.PARAMETER Version
  本次版本号（如 0.2.7）。语义化版本，三处代码 + 测试断言将同步到它。

.PARAMETER Build
  本地编译 clashwin + clashwin_test 并跑全量回归（强烈建议，CI 之外的第一道闸）。

.PARAMETER Push
  预检（与可选回归）通过后，执行 提交→推 dev→快进 main→推 main→打 tag→推 tag。
  需要显式开启；不开启则只做本地准备并停在推送前。

.PARAMETER Watch
  推送后 gh run watch 等 CI，回拉产物到 dist/ 并核对 SHA256SUMS.txt。

.PARAMETER NoTest
  与 -Build 同用时跳过全量回归（仅编译）。绝大多数情况不应使用。

.PARAMETER Yes
  跳过 -Push 前的人肉确认（仅在已获用户授权复用本脚本时使用）。

.EXAMPLE
  # 本地准备 + 预检（不碰远程），先把坑在本地暴露完
  powershell -File scripts/release.ps1 -Version 0.2.7

  # 本地编译 + 全量回归 + 预检，全过后再提交并打 tag 发布
  powershell -File scripts/release.ps1 -Version 0.2.7 -Build -Push

  # 发布后盯 CI 并回拉核验
  powershell -File scripts/release.ps1 -Version 0.2.7 -Watch
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Version,
  [switch]$Build,
  [switch]$Push,
  [switch]$Watch,
  [switch]$NoTest,
  [switch]$Yes,
  [switch]$SkipHookInstall
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $RepoRoot

# ---------- 0. 剥离 WorkBuddy 注入的污染代理变量（与 build.ps1 同源治理）----------
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','http_proxy','https_proxy',
                 'ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy',
                 'NO_PROXY','no_proxy')) {
  [Environment]::SetEnvironmentVariable($n, $null, 'Process')
}

# ---------- 1. 确保 pre-push 钩子就位（一次性，幂等）----------
if (-not $SkipHookInstall) {
  & "$PSScriptRoot/install_hooks.ps1"
}

# ---------- 2. 版本格式校验 ----------
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.]+)?$') {
  Write-Error "版本号格式非法: $Version （应为 0.1.0 风格 semver）"; exit 2
}

function Edit-File($path, $pattern, $replacement) {
  if (-not (Test-Path $path)) { Write-Error "缺失文件: $path"; exit 3 }
  $c = Get-Content $path -Raw
  if ($c -notmatch $pattern) { Write-Error "在 $path 中未匹配到: $pattern"; exit 3 }
  $new = [regex]::Replace($c, $pattern, $replacement)
  Set-Content -Path $path -Value $new -NoNewline
  Write-Host ("    已写 $path") -ForegroundColor DarkGray
}

# ---------- 3. 版本号单一事实源：只输入一次，四处同步 ----------
Write-Host "`n[1] 同步版本号到四处（单一输入：$Version）" -ForegroundColor Yellow
Edit-File "$RepoRoot/include/clashwin/config/constants.h" `
  '(kAppVersion\s*=\s*")([^"]*)(")' "(`$1)$Version(`$3)"
Edit-File "$RepoRoot/installer/clashwin.nsi" `
  '(!define VERSION\s+")([^"]*)(")' "(`$1)$Version(`$3)"
# app-update.json：版本号同步，且清空 url/sha256/signature 为占位符
# （真实清单由 publish 步骤运行时生成并推回 main；仓库文件必须是占位符，
#  否则 test_t35_update_manifest_is_clash_win_not_mini 会判 download_url/signature 非空而失败）
Edit-File "$RepoRoot/updater/app-update.json" `
  '("version"\s*:\s*")([^"]*)(")' "(`$1)$Version(`$3)"
Edit-File "$RepoRoot/updater/app-update.json" '("url"\s*:\s*")[^"]*(")' '${1}${2}'
Edit-File "$RepoRoot/updater/app-update.json" '("sha256"\s*:\s*")[^"]*(")' '${1}${2}'
Edit-File "$RepoRoot/updater/app-update.json" '("signature"\s*:\s*")[^"]*(")' '${1}${2}'
Edit-File "$RepoRoot/tests/test_t35_release_readiness.cpp" `
  '(kAppVersion\)\s*==\s*")([^"]*)(")' "(`$1)$Version(`$3)"

# ---------- 4. 脚手架发布材料 ----------
Write-Host "`n[2] 脚手架发布材料（缺失则生成，存在则跳过）" -ForegroundColor Yellow
$relNotes = "$RepoRoot/docs/RELEASE_NOTES_$Version.md"
$knoLim    = "$RepoRoot/docs/KNOWN_LIMITATIONS_$Version.md"
if (-not (Test-Path $relNotes)) {
  # 复用上一版作为模板（替换其中版本号），没有则建空壳
  $prev = Get-ChildItem "$RepoRoot/docs/RELEASE_NOTES_*.md" |
          Where-Object { $_.Name -match 'RELEASE_NOTES_([0-9.]+)\.md' } |
          Sort-Object { [version]($Matches[1]) } | Select-Object -Last 1
  if ($prev) {
    $body = (Get-Content $prev.FullName -Raw) -replace [regex]::Escape($Matches[1]), $Version
    Set-Content -Path $relNotes -Value $body.TrimEnd()
  } else {
    Set-Content -Path $relNotes -Value "# Clash Win v$Version 发布说明`n`n（用用户能懂的语言填写本次改了什么）`n"
  }
  Write-Host ("    生成 $relNotes （请补充内容）") -ForegroundColor Cyan
}
if (-not (Test-Path $knoLim)) {
  $prev = Get-ChildItem "$RepoRoot/docs/KNOWN_LIMITATIONS_*.md" |
          Where-Object { $_.Name -match 'KNOWN_LIMITATIONS_([0-9.]+)\.md' } |
          Sort-Object { [version]($Matches[1]) } | Select-Object -Last 1
  if ($prev) {
    $body = (Get-Content $prev.FullName -Raw) -replace [regex]::Escape($Matches[1]), $Version
    Set-Content -Path $knoLim -Value $body.TrimEnd()
  } else {
    Set-Content -Path $knoLim -Value "# Clash Win v$Version 已知限制（Known Limitations）`n`n诚实披露本版本仍未闭环的事项。`n"
  }
  Write-Host ("    生成 $knoLim （请补充内容）") -ForegroundColor Cyan
}
# CHANGELOG 顶部补 v<ver> 条目
$changelog = "$RepoRoot/CHANGELOG.md"
$c = Get-Content $changelog -Raw
if ($c -notmatch "## v$Version") {
  $today = (Get-Date -Format 'yyyy-MM-dd')
  $entry = "## v$Version ($today)`n`n### 用户可见修复`n- TODO`n`n### 工程 / 发版流程`n- TODO`n`n"
  $c = $c -replace '(?s)(# Changelog(\n.*?)?\n---\n)', "`$1`n$entry"
  Set-Content -Path $changelog -Value $c.TrimEnd()
  Write-Host ("    CHANGELOG.md 补充 ## v$Version 条目") -ForegroundColor Cyan
}
# Issue 模板里的旧版本链接同步
Get-ChildItem "$RepoRoot/.github/ISSUE_TEMPLATE/*.yml" | ForEach-Object {
  $t = Get-Content $_.FullName -Raw
  $t2 = $t -replace 'KNOWN_LIMITATIONS_[\d.]+', "KNOWN_LIMITATIONS_$Version" `
           -replace 'RELEASE_NOTES_[\d.]+', "RELEASE_NOTES_$Version"
  if ($t2 -ne $t) { Set-Content -Path $_.FullName -Value $t2 -NoNewline }
}

# ---------- 5. 强制预检（GATE 1~7）----------
Write-Host "`n[3] 运行 release_preflight（GATE 1~7，不过即退出）" -ForegroundColor Yellow
& "$PSScriptRoot/release_preflight.ps1" -Version $Version
if ($LASTEXITCODE -ne 0) {
  Write-Host "预检未通过，停止。先本地修完再继续（不要等 CI 跑 18 分钟）。" -ForegroundColor Red
  exit 1
}

# ---------- 6. 本地编译 + 全量回归（关键门禁）----------
if ($Build) {
  Write-Host "`n[4] 本地编译 + 全量回归（v0.2.6 正是漏了这步才在 CI 翻车）" -ForegroundColor Yellow
  & "$PSScriptRoot/build.ps1" -Configuration Release -Target clashwin
  if ($LASTEXITCODE -ne 0) { Write-Error "clashwin 编译失败"; exit 1 }
  & "$PSScriptRoot/build.ps1" -Configuration Release -Target clashwin_test
  if ($LASTEXITCODE -ne 0) { Write-Error "clashwin_test 编译失败"; exit 1 }
  if (-not $NoTest) {
    python3 "$PSScriptRoot/run_tests_py.py"
    if ($LASTEXITCODE -ne 0) {
      Write-Host "本地回归未通过（EXIT=$LASTEXITCODE）。这就是 CI 会拦的同一类失败，先修完再推。" -ForegroundColor Red
      exit 1
    }
    Write-Host "本地回归通过。" -ForegroundColor Green
  }
} else {
  Write-Host "`n[!] 未传 -Build：本次未跑本地回归。v0.2.6 的教训就是漏了这步。" -ForegroundColor Magenta
  Write-Host "    CI 仍会在 build job 跑隔离回归兜底，但那要 18 分钟。建议加 -Build 本地先拦。" -ForegroundColor Magenta
}

# ---------- 7. 推送 / 打 tag（需显式 -Push）----------
if (-not $Push) {
  Write-Host "`n=== 本地准备完成（预检已通过）。下一步：===" -ForegroundColor Cyan
  Write-Host "  复核 docs/RELEASE_NOTES_$Version.md 与 docs/KNOWN_LIMITATIONS_$Version.md 内容，" -ForegroundColor White
  Write-Host "  然后加 -Push 提交并打 tag（如需本地回归再加 -Build）。" -ForegroundColor White
  exit 0
}

if (-not $Yes) {
  $ans = Read-Host "确认推送并打 tag v$Version ？（将 提交→推 dev→快进 main→推 main→打 tag→推 tag）[y/N]"
  if ($ans -notmatch '^[yY]') { Write-Host "已取消推送。"; exit 0 }
}

Write-Host "`n[5] 提交 → 推 dev → 快进 main → 推 main → 打 tag → 推 tag" -ForegroundColor Yellow
git add -A
$st = git status --porcelain
if ($st) { git commit -m "chore(release): v$Version 版本准备 + 发布材料" }
else     { Write-Host "    无本地改动，跳过提交。" -ForegroundColor DarkGray }
git push origin dev
git checkout main
git merge --ff-only dev
git checkout dev
git push origin main
git tag -a "v$Version" -m "Clash Win v$Version"
git push origin "v$Version"
Write-Host "已推送 tag v$Version，CI 触发中。" -ForegroundColor Green

# ---------- 8. 盯 CI + 回拉核验 ----------
if ($Watch) {
  Write-Host "`n[6] 等待 CI 并回拉产物到 dist/ 核验" -ForegroundColor Yellow
  gh run watch --repo qiu-yuxiao/clash-win --exit-status
  gh release download "v$Version" --repo qiu-yuxiao/clash-win --dir dist --clobber
  if (Test-Path "dist/SHA256SUMS.txt") {
    Get-Content "dist/SHA256SUMS.txt"
    Write-Host "请人工用 Get-FileHash 复核 setup.exe / portable.zip 与 SHA256SUMS.txt 一致。" -ForegroundColor Cyan
  } else {
    Write-Host "未找到 dist/SHA256SUMS.txt，回拉可能失败。" -ForegroundColor Red
  }
}
