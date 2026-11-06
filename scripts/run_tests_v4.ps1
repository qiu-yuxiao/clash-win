# 全量测试运行器。
# 默认：复制到 %TEMP%\cwrun 隔离运行（360 对 build\bin\Release 路径下进程的写限制，
#       实测 2026-11-03 即使把仓库目录加入信任区后原地仍报 CreateDirectoryW err=5）。
# -InPlace：在 build\bin\Release 原地运行，用于验证 360 策略是否已对该 exe 生效。
param(
  [switch]$InPlace
)

$repo = 'C:\Users\sun_y\Documents\AntiGravity_Projects\ClashWin'
$rel = Join-Path $repo 'build\bin\Release'
$logRoot = 'C:\Users\sun_y\Documents\Trae-CN\temp_20261102_02'

if ($InPlace) {
  Set-Location $rel
  & '.\clashwin_test.exe' *> (Join-Path $logRoot 'tests_inplace.log')
  Write-Host ('EXIT=' + $LASTEXITCODE)
  exit $LASTEXITCODE
}

$runDir = "$env:USERPROFILE\AppData\Local\Temp\cwrun"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
Copy-Item (Join-Path $rel 'clashwin_test.exe') (Join-Path $runDir 'cwrun.exe') -Force
# t08 golden live probe 需要 test core：sidecar\mini-mihomo.exe 必须跟着复制，
#   否则 3 条用例会以 "SKIP: test core failed to start" 结束（2026-11-05）。
Copy-Item (Join-Path $repo 'sidecar') (Join-Path $runDir 'sidecar') -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $rel 'locales') (Join-Path $runDir 'locales') -Recurse -Force -ErrorAction SilentlyContinue
# t25_i18n 从仓库 assets/locales 读取 layout.json（bin/Release 下为 ../../../assets/locales）
New-Item -ItemType Directory -Force -Path (Join-Path $runDir 'assets') | Out-Null
Copy-Item (Join-Path $repo 'assets\locales') (Join-Path $runDir 'assets\locales') -Recurse -Force -ErrorAction SilentlyContinue
$env:TMP = Join-Path $runDir 'tmp'
$env:TEMP = $env:TMP
New-Item -ItemType Directory -Force -Path $env:TMP | Out-Null
Get-ChildItem $env:TMP -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Set-Location $runDir
& '.\cwrun.exe' *> (Join-Path $logRoot 'tests_full.log')
Write-Host ('EXIT=' + $LASTEXITCODE)
exit $LASTEXITCODE
