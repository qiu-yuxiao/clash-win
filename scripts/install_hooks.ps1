<#
.SYNOPSIS
  安装 Clash Win 的 git hooks（幂等，跑一次即可）。
.DESCRIPTION
  把 git 的 core.hooksPath 指向仓库内受跟踪的 scripts/git-hooks 目录，
  使 pre-push 钩子对所有参与者生效（克隆后跑一次，提交即受约束）。
  钩子文件本身在版本库内，随仓库演进，无需每人手动复制。
#>
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$hooksDir = Join-Path $RepoRoot 'scripts/git-hooks'
if (-not (Test-Path $hooksDir)) { New-Item -ItemType Directory -Force -Path $hooksDir | Out-Null }

$current = git -C $RepoRoot config core.hooksPath
if ($current -ne $hooksDir) {
  git -C $RepoRoot config core.hooksPath $hooksDir
  Write-Host "[ok] core.hooksPath = $hooksDir" -ForegroundColor Green
} else {
  Write-Host "[ok] core.hooksPath 已指向 $hooksDir（无需更改）" -ForegroundColor Green
}
Write-Host "[说明] pre-push 钩子会在你 `git push origin vX.Y.Z` 时强制跑 release_preflight，" -ForegroundColor Cyan
Write-Host "       预检不过则拒绝推送。发版请统一走 scripts/release.ps1。" -ForegroundColor Cyan
