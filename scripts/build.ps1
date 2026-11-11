# build.ps1 — ClashWin 标准增量编译入口（开发者日常使用）
#
# 根因治理：WorkBuddy 透明代理(sandbox-cli)会向子进程注入大小写重复的 *_PROXY
# 变量（HTTPS_PROXY 与 https_proxy 同值并存），导致 MSBuild CL 任务 "字典关键字冲突"
# 崩溃。本脚本在调用 cmake 前强制剥离所有 *_PROXY（不区分大小写），保证构建环境干净。
# 以后编译统一走本脚本，即可彻底绕开该问题，不必每次手动 unset。
[CmdletBinding()]
param(
  [ValidateSet('Release', 'Debug')]
  [string]$Configuration = 'Release',
  [string]$Target = 'clashwin'
)
$ErrorActionPreference = 'Stop'

# 剥离 WorkBuddy 注入的污染环境变量（已验证：清除后 Get-ChildItem Env: 不再崩溃）
foreach ($n in @('HTTP_PROXY','HTTPS_PROXY','http_proxy','https_proxy',
                 'ALL_PROXY','all_proxy','FTP_PROXY','ftp_proxy',
                 'NO_PROXY','no_proxy')) {
  [Environment]::SetEnvironmentVariable($n, $null, 'Process')
}

# 定位 cmake（优先 VS 自带的 CMake，其次系统 PATH/默认安装位置）
$cmake = $null
$vsList = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath 2>$null
foreach ($vs in $vsList) {
  $c = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
  if (Test-Path $c) { $cmake = $c; break }
}
if (-not $cmake -and (Test-Path 'C:\Program Files\CMake\bin\cmake.exe')) {
  $cmake = 'C:\Program Files\CMake\bin\cmake.exe'
}
if (-not $cmake) { Write-Error '未找到 cmake.exe'; exit 2 }

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& $cmake --build (Join-Path $RepoRoot 'build') --config $Configuration --target $Target
exit $LASTEXITCODE
