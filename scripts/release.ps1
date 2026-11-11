<#
.SYNOPSIS
  Clash Win 一站式确定性发版编排器（准备 → 预检 → 构建与回归 → 本地打包校验 → 发布与盯梢核验）。

.DESCRIPTION
  用法一：本地完整预演（默认模式，零远程副作用，推荐每次发版先跑这一条）
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version 0.2.7

    自动完成：
      1) 安装/确认 git pre-push 钩子就位；
      2) 将版本号原子同步至 constants.h / src\clashwin.rc / installer\clashwin.nsi（保持 UTF-8 无 BOM）；
      3) 检查 CHANGELOG.md 是否含 ## v0.2.7 章节（缺失则基于 git log 自动生成草稿）；
      4) 运行 scripts\release_preflight.ps1（GATE 1~7 全量静态与基线门禁）；
      5) 顺序编译 Release 版 clashwin + clashwin_test，运行 scripts\run_tests.ps1 全量隔离回归；
      6) 本地打包便携版 ZIP 与 NSIS 安装包（-SkipSign），校验三方 clashwin.exe 与内核 SHA256 一致。

  用法二：正式推送发布并自动盯梢 CI + 回拉核验 + 分支同步收尾
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version 0.2.7 -Push -Yes

    若刚跑完用法一且二进制未变，可加 -SkipBuild 免重复编译：
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version 0.2.7 -SkipBuild -Push -Yes
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$Version,
  [switch]$SkipBuild,
  [switch]$NoTest,
  [switch]$Push,
  [switch]$NoWatch,
  [switch]$WatchOnly,
  [switch]$Retag,
  [switch]$Yes,
  [switch]$SkipHookInstall
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$GhRepo   = 'qiu-yuxiao/clash-win'
Set-Location $RepoRoot

# 剥离污染代理环境变量（防止 MSBuild 字典键冲突）
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','http_proxy','https_proxy',
                 'ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy',
                 'NO_PROXY','no_proxy')) {
  [Environment]::SetEnvironmentVariable($n, $null, 'Process')
}

function Read-Utf8NoBom([string]$path) {
  $enc = New-Object System.Text.UTF8Encoding($false, $true)
  return [System.IO.File]::ReadAllText($path, $enc)
}

function Write-Utf8NoBom([string]$path, [string]$content) {
  $enc = New-Object System.Text.UTF8Encoding($false)
  [System.IO.File]::WriteAllText($path, $content, $enc)
}

function Replace-RegexStrict([string]$path, [string]$pattern, [string]$replacement) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "缺失文件: $path"
  }
  $rawBytes = [System.IO.File]::ReadAllBytes($path)
  $hadBom = ($rawBytes.Length -ge 3 -and $rawBytes[0] -eq 0xEF -and $rawBytes[1] -eq 0xBB -and $rawBytes[2] -eq 0xBF)
  $mustBom = $hadBom -or ($path -match '\.(nsi|nsh|ps1)$')
  $old = Read-Utf8NoBom $path
  if ($old -notmatch $pattern) {
    throw "在 $path 中未匹配到预期模式: $pattern"
  }
  $new = [regex]::Replace($old, $pattern, $replacement)
  if ($new -ne $old -or ($mustBom -and -not $hadBom)) {
    $enc = New-Object System.Text.UTF8Encoding($mustBom)
    [System.IO.File]::WriteAllText($path, $new, $enc)
    Write-Host ("  [已更新] " + $path.Substring($RepoRoot.Length + 1)) -ForegroundColor DarkGray
  } else {
    Write-Host ("  [已一致] " + $path.Substring($RepoRoot.Length + 1)) -ForegroundColor DarkGray
  }
}

function Watch-And-VerifyRelease([string]$ver, [string]$expectedSha = '') {
  $tag = "v$ver"
  Write-Host "`n[盯梢] 正在定位标签 $tag 触发的 GitHub Actions 运行记录..." -ForegroundColor Yellow
  $runId = $null
  for ($attempt = 1; $attempt -le 24; $attempt++) {
    $jsonTxt = & gh run list --repo $GhRepo --workflow release.yml --limit 8 --json databaseId,headBranch,headSha,status,conclusion,createdAt 2>$null
    if ($LASTEXITCODE -eq 0 -and $jsonTxt) {
      $runs = $jsonTxt | ConvertFrom-Json
      $hit = $runs | Where-Object {
        $_.headBranch -eq $tag -and ([string]::IsNullOrEmpty($expectedSha) -or $_.headSha -eq $expectedSha)
      } | Select-Object -First 1
      if ($hit) {
        $runId = $hit.databaseId
        break
      }
    }
    Start-Sleep -Seconds 5
  }

  if (-not $runId) {
    throw "等待 120s 仍未查询到 $tag 的 release.yml 运行记录，请检查: gh run list --repo $GhRepo"
  }

  Write-Host ("  已锁定 Run ID = $runId (https://github.com/$GhRepo/actions/runs/$runId)") -ForegroundColor Cyan
  & gh run watch $runId --repo $GhRepo --interval 15 --exit-status
  if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] CI 运行失败，拉取失败步骤日志：" -ForegroundColor Red
    & gh run view $runId --repo $GhRepo --log-failed
    exit 1
  }

  Write-Host "`n[核验] 正在下载 GitHub Release $tag 全套产物到 dist/ ..." -ForegroundColor Yellow
  $distDir = Join-Path $RepoRoot 'dist'
  New-Item -ItemType Directory -Force -Path $distDir | Out-Null
  & gh release download $tag --repo $GhRepo --dir $distDir --clobber
  if ($LASTEXITCODE -ne 0) { throw "gh release download $tag 失败" }

  $sumsPath = Join-Path $distDir 'SHA256SUMS.txt'
  if (-not (Test-Path -LiteralPath $sumsPath)) {
    throw "下载产物缺少 dist\SHA256SUMS.txt"
  }
  $sumLines = @(Read-Utf8NoBom $sumsPath -split "`r?`n" | Where-Object { $_.Trim().Length -gt 0 })
  foreach ($line in $sumLines) {
    if ($line -match '^([0-9a-fA-F]{64})\s+(.+)$') {
      $expectedHash = $Matches[1].ToLowerInvariant()
      $fileName     = $Matches[2].Trim()
      $assetPath    = Join-Path $distDir $fileName
      if (-not (Test-Path -LiteralPath $assetPath)) {
        throw "SHA256SUMS.txt 列出的产物不存在: $assetPath"
      }
      $actualHash = (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant()
      if ($actualHash -ne $expectedHash) {
        throw "产物哈希不匹配 ($fileName): expected=$expectedHash actual=$actualHash"
      }
      Write-Host ("  [HASH OK] $fileName  $actualHash") -ForegroundColor Green
    }
  }

  $dlManifest = Join-Path $distDir 'app-update.json'
  if (Test-Path -LiteralPath $dlManifest) {
    $mo = (Read-Utf8NoBom $dlManifest) | ConvertFrom-Json
    if ($mo.version -ne $ver) {
      throw "Release 资产 app-update.json 版本异常: $($mo.version) != $ver"
    }
    $sigLen = ([string]$mo.platforms.'windows-x86_64'.signature).Length
    Write-Host ("  [MANIFEST OK] app-update.json version=$($mo.version) signature_len=$sigLen") -ForegroundColor Green
  }

  # 将 CI 回写到远端的 app-update.json 同步回本地分支，保持本地与远端 100% 对齐
  Write-Host "`n[收尾] 同步远端最新提交（含更新清单回写）到本地分支..." -ForegroundColor Yellow
  & git fetch origin --prune
  $curBranch = (& git rev-parse --abbrev-ref HEAD).Trim()
  if ($curBranch -eq 'dev') {
    & git merge --ff-only origin/main
    if ($LASTEXITCODE -eq 0) {
      & git branch -f main HEAD
      & git push origin dev
      Write-Host '  [SYNC OK] 本地与远端 dev / main 已完全对齐。' -ForegroundColor Green
    }
  }

  Write-Host "`n=== Clash Win v$ver 发布与核验圆满完成！ ===" -ForegroundColor Green
}

# ---------- 0. 参数格式校验与纯盯梢捷径 ----------
if ($Version -notmatch '^([0-9]+\.[0-9]+\.[0-9]+)([-.][0-9A-Za-z.]+)?$') {
  throw "版本号格式非法: '$Version'（期望如 0.2.7 的 SemVer 格式）"
}
$BaseSemVer = $Matches[1]
$CommaVer   = ($BaseSemVer -replace '\.', ',') + ',0'

if ($WatchOnly) {
  Watch-And-VerifyRelease $Version
  exit 0
}

# ---------- 1. 确保 git pre-push 钩子就位 ----------
if (-not $SkipHookInstall) {
  & "$PSScriptRoot\install_hooks.ps1"
}

# ---------- 2. 同步三处版本元数据（不触碰 updater/app-update.json） ----------
Write-Host "`n[步骤 1/5] 同步版本号 ($Version) 至源码、资源与安装脚本" -ForegroundColor Yellow
Replace-RegexStrict (Join-Path $RepoRoot 'include\clashwin\config\constants.h') `
  '(kAppVersion\s*=\s*")([^"]*)(")' ('${1}' + $Version + '${3}')
Replace-RegexStrict (Join-Path $RepoRoot 'installer\clashwin.nsi') `
  '(!define\s+VERSION\s+")([^"]*)(")' ('${1}' + $Version + '${3}')
Replace-RegexStrict (Join-Path $RepoRoot 'src\clashwin.rc') `
  '(FILEVERSION\s+)[0-9]+,[0-9]+,[0-9]+,[0-9]+' ('${1}' + $CommaVer)
Replace-RegexStrict (Join-Path $RepoRoot 'src\clashwin.rc') `
  '(PRODUCTVERSION\s+)[0-9]+,[0-9]+,[0-9]+,[0-9]+' ('${1}' + $CommaVer)
Replace-RegexStrict (Join-Path $RepoRoot 'src\clashwin.rc') `
  '("FileVersion",\s*")([^"]*)(")' ('${1}' + $Version + '${3}')
Replace-RegexStrict (Join-Path $RepoRoot 'src\clashwin.rc') `
  '("ProductVersion",\s*")([^"]*)(")' ('${1}' + $Version + '${3}')

# ---------- 3. 检查 / 自动生成 CHANGELOG.md 对应版本章节草稿 ----------
Write-Host "`n[步骤 2/5] 检查 CHANGELOG.md 发布说明章节" -ForegroundColor Yellow
$changelogPath = Join-Path $RepoRoot 'CHANGELOG.md'
$clText = Read-Utf8NoBom $changelogPath
$escapedVer = [regex]::Escape($Version)
if ($clText -notmatch "(?m)^##\s+v$escapedVer\b") {
  $today = Get-Date -Format 'yyyy-MM-dd'
  $lastTag = (& git describe --tags --abbrev=0 --match 'v[0-9]*' 2>$null)
  $bullets = @()
  if ($LASTEXITCODE -eq 0 -and $lastTag) {
    $rawLogs = @(& git log "$lastTag..HEAD" --no-merges --pretty=format:'%s' 2>$null)
    foreach ($s in $rawLogs) {
      if ($s -and ($s -notmatch '^chore\(updater\):|^chore\(release\):')) {
        $bullets += "- $s"
      }
    }
  }
  if ($bullets.Count -eq 0) {
    $bullets = @('- TODO: 请在此填写本版本用户可见更新内容')
  } else {
    $bullets = @('- TODO: 请审阅并润色以下自动汇总的变更项（确认后删去本行 TODO）') + $bullets
  }
  $draftSection = "## v$Version ($today)`n`n### 更新内容`n" + ($bullets -join "`n") + "`n`n"
  $clNew = [regex]::Replace($clText, '(?s)(# Changelog.*?\r?\n---\r?\n\r?\n?)', ('${1}' + $draftSection), 1)
  Write-Utf8NoBom $changelogPath $clNew
  Write-Host "  已在 CHANGELOG.md 顶部生成 ## v$Version 草稿，请审阅并移除 TODO 占位行后再执行发布。" -ForegroundColor Cyan
} else {
  Write-Host "  CHANGELOG.md 已包含 ## v$Version 章节。" -ForegroundColor Green
}

# ---------- 4. 运行全量静态与基线预检（GATE 1~7） ----------
Write-Host "`n[步骤 3/5] 运行发版预检 (release_preflight.ps1)" -ForegroundColor Yellow
& "$PSScriptRoot\release_preflight.ps1" -Version $Version
if ($LASTEXITCODE -ne 0) {
  Write-Host "`n[阻断] 预检未通过，已终止后续步骤。" -ForegroundColor Red
  exit 1
}

# ---------- 5. 本地编译 + 全量隔离回归 + 本地打包校验 ----------
if (-not $SkipBuild) {
  Write-Host "`n[步骤 4/5] 本地 Release 构建 + 全量回归 + 打包预演" -ForegroundColor Yellow
  & "$PSScriptRoot\build.ps1" -Configuration Release -Target clashwin
  if ($LASTEXITCODE -ne 0) { throw 'clashwin Release 编译失败' }

  & "$PSScriptRoot\build.ps1" -Configuration Release -Target clashwin_test
  if ($LASTEXITCODE -ne 0) { throw 'clashwin_test Release 编译失败' }

  if (-not $NoTest) {
    & "$PSScriptRoot\run_tests.ps1" -Configuration Release
    if ($LASTEXITCODE -ne 0) {
      Write-Host "`n[阻断] 本地全量回归未通过，已终止发版。" -ForegroundColor Red
      exit 1
    }
  }

  Write-Host "`n  正在本地预演便携包 (portable.ps1)..." -ForegroundColor Cyan
  & "$PSScriptRoot\portable.ps1" -Version $Version -Configuration Release
  if ($LASTEXITCODE -ne 0) { throw '本地 portable.ps1 打包失败' }

  $buildExeSha = (Get-FileHash -LiteralPath (Join-Path $RepoRoot 'build\bin\Release\clashwin.exe') -Algorithm SHA256).Hash

  # 若本机装有 NSIS makensis.exe，则一并预演 NSIS 安装包构建与 stage 内核完整性校验
  $installerScript = Join-Path $RepoRoot 'installer\build_installer.ps1'
  $hasMakensis = [bool](Get-Command makensis.exe -ErrorAction SilentlyContinue) -or
                 (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'tauri\NSIS\makensis.exe')) -or
                 (Test-Path -LiteralPath (Join-Path ${env:ProgramFiles} 'NSIS\makensis.exe')) -or
                 (Test-Path -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'))
  if ($hasMakensis) {
    Write-Host "`n  正在本地预演 NSIS 安装包 (build_installer.ps1 -SkipSign)..." -ForegroundColor Cyan
    & $installerScript -Version $Version -Configuration Release -SkipSign
    if ($LASTEXITCODE -ne 0) { throw '本地 build_installer.ps1 打包失败' }

    $stageExe = Join-Path $RepoRoot 'installer\stage\clashwin.exe'
    $stageSha = (Get-FileHash -LiteralPath $stageExe -Algorithm SHA256).Hash
    if ($stageSha -ne $buildExeSha) {
      throw "stage\clashwin.exe 哈希 ($stageSha) 与 build\bin\Release\clashwin.exe ($buildExeSha) 不一致"
    }
    $stageCore = Join-Path $RepoRoot 'installer\stage\sidecar\win-mihomo.exe'
    $coreHash = (Get-FileHash -LiteralPath $stageCore -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($coreHash -ne '1CDE41CFFFD3DE0AEDAD63C0A8515E1CE5EEA507E8133AED0F637D316C93800A') {
      throw "stage\sidecar\win-mihomo.exe SHA256 ($coreHash) 与上游基线不一致"
    }
    $stageSidecarItems = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'installer\stage\sidecar') -Recurse -Force)
    if ($stageSidecarItems.Count -ne 5) {
      throw "stage\sidecar 内包含多余残留文件（期望恰好 5 个，实际 $($stageSidecarItems.Count) 个）"
    }
    Write-Host "  [PACKAGE OK] 本地便携包 + NSIS 安装包预演全部通过（exe SHA256=$buildExeSha）。" -ForegroundColor Green
  } else {
    Write-Host '  [INFO] 本机未安装 NSIS，跳过本地 setup.exe 预演（将在 CI 构建）。' -ForegroundColor DarkGray
  }
} else {
  Write-Host "`n[步骤 4/5] 已跳过本地构建 (-SkipBuild)。" -ForegroundColor DarkGray
}

# ---------- 6. 推送与打标签触发 CI ----------
if (-not $Push) {
  Write-Host "`n=== 本地发版预演全部通过（未触碰远程仓库） ===" -ForegroundColor Cyan
  Write-Host "确认代码已提交后，执行以下命令即可一步完成推送、发版与自动核验：" -ForegroundColor White
  Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release.ps1 -Version $Version -SkipBuild -Push -Yes" -ForegroundColor Green
  exit 0
}

Write-Host "`n[步骤 5/5] 同步远端分支、推送提交并打标签 v$Version" -ForegroundColor Yellow
$curBranch = (& git rev-parse --abbrev-ref HEAD).Trim()
if ($curBranch -ne 'dev') {
  throw "当前分支为 '$curBranch'，发版必须在 'dev' 分支上执行。"
}

$tagName = "v$Version"
$localTagExists = [bool](& git tag -l $tagName)
if ($localTagExists -and -not $Retag) {
  throw "本地已存在标签 $tagName。若确需覆盖重打同一版本标签，请显式追加 -Retag 参数。"
}

if (-not $Yes) {
  $ans = Read-Host "即将提交版本准备改动（如有）、同步推送 dev/main 并推送标签 $tagName 触发正式发布，是否继续？[y/N]"
  if ($ans -notmatch '^[yY]') {
    Write-Host '已取消推送。' -ForegroundColor Yellow
    exit 0
  }
}

# 若工作区存在未提交改动，先提交到 dev
$porcelain = (& git status --porcelain)
if ($porcelain) {
  Write-Host '  检测到工作区未提交改动，正在创建发版提交...' -ForegroundColor Cyan
  & git add -A
  & git commit -m "chore(release): prepare v$Version"
  if ($LASTEXITCODE -ne 0) { throw 'git commit 失败' }
}

# 拉取远端引用并自动吸收上一版 CI 回写到 origin/main 的 app-update.json 提交，杜绝 non-fast-forward
& git fetch origin --prune
if ($LASTEXITCODE -ne 0) { throw 'git fetch origin 失败' }

& git merge-base --is-ancestor origin/dev HEAD 2>$null
if ($LASTEXITCODE -ne 0) {
  Write-Host '  合并 origin/dev 远端更新...' -ForegroundColor Cyan
  & git merge --no-edit origin/dev
  if ($LASTEXITCODE -ne 0) { throw '合并 origin/dev 存在冲突，请先解决冲突后再试' }
}

& git merge-base --is-ancestor origin/main HEAD 2>$null
if ($LASTEXITCODE -ne 0) {
  Write-Host '  自动合并 origin/main 上的历史更新清单提交，消除分支分叉...' -ForegroundColor Cyan
  & git merge --no-edit origin/main -m "chore(release): sync origin/main into dev before v$Version"
  if ($LASTEXITCODE -ne 0) { throw '合并 origin/main 失败，请手工检查分支历史' }
}

# 合并后重跑一次秒级预检，确保最终待打 tag 的树 100% 合规
& "$PSScriptRoot\release_preflight.ps1" -Version $Version
if ($LASTEXITCODE -ne 0) { throw '分支同步后预检未通过，已中止推送' }

Write-Host '  推送 dev -> origin/dev ...' -ForegroundColor Cyan
& git push origin dev
if ($LASTEXITCODE -ne 0) { throw 'git push origin dev 失败' }

Write-Host '  快进同步 main -> origin/main（无需切换工作树）...' -ForegroundColor Cyan
& git push origin dev:main
if ($LASTEXITCODE -ne 0) { throw 'git push origin dev:main 失败（非快进或分支受保护）' }
& git branch -f main HEAD

$targetHeadSha = (& git rev-parse HEAD).Trim()
Write-Host "  创建附注标签 $tagName ($targetHeadSha) 并推送..." -ForegroundColor Cyan
if ($Retag) {
  $remoteTag = (& git ls-remote --tags origin "refs/tags/$tagName")
  if ($remoteTag) {
    & git push origin ":refs/tags/$tagName"
  }
  & git tag -f -a $tagName -m "Clash Win v$Version"
  if ($LASTEXITCODE -ne 0) { throw "git tag -f $tagName 失败" }
  & git push origin "refs/tags/$tagName"
} else {
  & git tag -a $tagName -m "Clash Win v$Version"
  if ($LASTEXITCODE -ne 0) { throw "git tag $tagName 失败" }
  & git push origin "refs/tags/$tagName"
}
if ($LASTEXITCODE -ne 0) { throw "推送标签 $tagName 失败" }

Write-Host "[OK] 已成功推送 dev / main 及标签 $tagName！" -ForegroundColor Green

if (-not $NoWatch) {
  Watch-And-VerifyRelease $Version $targetHeadSha
}
exit 0
