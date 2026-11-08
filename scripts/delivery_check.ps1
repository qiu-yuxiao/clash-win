# delivery_check.ps1 — 交付前自检（单一事实源 + 四方哈希 + 工作区门禁 + 运行态一致性）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\delivery_check.ps1 -Record
#       构建完成后调用：记录当前 exe 的 SHA256 到 .delivery\stamp.json
#   powershell -ExecutionPolicy Bypass -File scripts\delivery_check.ps1
#       交付前调用：静态门禁（工作区干净 / 四方哈希一致 / 接线标记）
#   powershell -ExecutionPolicy Bypass -File scripts\delivery_check.ps1 -Runtime
#       追加运行态验收（唯一实例 / 服务 / 系统代理）——会读取本机状态，
#       但绝不改写系统代理或 TUN（总纲 §七.3：验证不得开系统代理）
#
# 单一事实源：build\bin\Release\clashwin.exe（唯一允许交付给用户的路径）。
#   stage / 便携包 / stamp 必须与之**字节级全等**，否则发布对象不可追溯。
# 工作区门禁（2026-11-07 新增）：源码有未提交改动时 exe 无法定位到任何 commit，
#   -Record 与非 -Record 一律 FAIL。提交后再重建一次才能留证。

param(
  [string]$Exe = 'C:\Users\sun_y\Documents\AntiGravity_Projects\ClashWin\build\bin\Release\clashwin.exe',
  [switch]$Record,
  [switch]$Runtime
)

$ErrorActionPreference = 'Continue'
$ok = $true
function Pass($m) { "PASS  $m" }
function Fail($m) { "FAIL  $m"; $script:ok = $false }

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not (Test-Path $Exe)) {
  Fail "exe 不存在: $Exe"
  exit 1
}

$item = Get-Item $Exe
$sha = (Get-FileHash $Exe -Algorithm SHA256).Hash

# ---------- 0. 工作区干净门禁（最前；-Record 同样受约束） ----------
$porcelain = (& git -C $RepoRoot status --porcelain 2>$null) -join "`n"
if ($LASTEXITCODE -ne 0) {
  Fail "git status 执行失败"
} elseif ($porcelain.Trim().Length -gt 0) {
  Fail "工作区有未提交改动，exe 无法定位 commit：`n$porcelain"
} else {
  $head = (& git -C $RepoRoot rev-parse HEAD 2>$null)
  Pass "工作区干净 HEAD=$head"
}

# ---------- 1. SHA256 基线 ----------
$stampDir = Join-Path (Split-Path $Exe -Parent) '.delivery'
$stampFile = Join-Path $stampDir 'stamp.json'
$stampSha = $null
if ($Record) {
  New-Item -ItemType Directory -Force -Path $stampDir | Out-Null
  @{ path = $Exe; sha256 = $sha; mtime = $item.LastWriteTime.ToString('o') } |
    ConvertTo-Json | Out-File $stampFile -Encoding utf8
  $stampSha = $sha
  Pass "已记录交付基线: $stampFile"
} else {
  if (-not (Test-Path $stampFile)) {
    Fail "缺少交付基线 $stampFile（请先构建后运行 -Record）"
  } else {
    $stamp = Get-Content $stampFile -Raw | ConvertFrom-Json
    $stampSha = $stamp.sha256
    if ($stamp.path -ne $Exe) { Fail "登记路径不一致: $($stamp.path) != $Exe" }
    elseif ($stamp.sha256 -ne $sha) { Fail "SHA256 与刚构建产物不一致（exe 被替换/未重建）" }
    else { Pass "SHA256 与刚构建产物一致" }
  }
}

# ---------- 2. 接线标记串 ----------
$bytes = [IO.File]::ReadAllBytes($Exe)
$txt = [Text.Encoding]::ASCII.GetString($bytes)
$txt16 = [Text.Encoding]::Unicode.GetString($bytes)   # wchar_t 字面量按 UTF-16LE 存放
$markers = @{
  'core start decision'      = 'core start decision'
  'needs_tun'                = 'needs_tun'
  'service_mode'             = 'service_mode'
  'clash-verge-service'      = 'clash-verge-service'
  'clash_verge_service'      = 'clash_verge_service'
  'PUT /proxies (节点切换)'  = '/proxies/'
  'Service started core'     = 'Service started core successfully'
}
foreach ($k in $markers.Keys) {
  if ($txt.Contains($markers[$k]) -or $txt16.Contains($markers[$k])) {
    Pass "标记串: $k"
  } else {
    Fail "缺少接线标记: $k"
  }
}

# ---------- 3. 四方哈希一致（build / stage / 便携包内 / stamp） ----------
$stageExe = Join-Path $RepoRoot 'installer\stage\clashwin.exe'
if (Test-Path $stageExe) {
  $stageSha = (Get-FileHash $stageExe -Algorithm SHA256).Hash
  if ($stageSha -eq $sha) { Pass "stage exe 与 build 一致" }
  else { Fail "stage exe 哈希不一致（$stageSha）——必须重新暂存，禁用 -SkipStage" }
} else {
  Fail "stage exe 不存在：$stageExe（交付链缺一方）"
}

$constHeader = Join-Path $RepoRoot 'include\clashwin\config\constants.h'
$ver = '0.1.8'
if (Test-Path $constHeader) {
  $m = Select-String -Path $constHeader -Pattern 'kAppVersion\s*=\s*"([^"]+)"'
  if ($m) { $ver = $m.Matches[0].Groups[1].Value }
}
$zip = Join-Path $RepoRoot "dist\ClashWin_${ver}_x64_portable.zip"
if (Test-Path $zip) {
  $tmp = Join-Path $env:TEMP ("cwdc_" + [guid]::NewGuid().ToString('N'))
  try {
    Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force -ErrorAction Stop
    $zexe = Get-ChildItem -Path $tmp -Recurse -Filter clashwin.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1
    if ($null -eq $zexe) {
      Fail "便携包内找不到 clashwin.exe"
    } else {
      $zipSha = (Get-FileHash $zexe.FullName -Algorithm SHA256).Hash
      if ($zipSha -eq $sha) { Pass "便携包内 exe 与 build 一致" }
      else { Fail "便携包内 exe 哈希不一致（$zipSha）——必须重打便携包" }
    }
  } catch {
    Fail "便携包解压校验失败: $($_.Exception.Message)"
  } finally {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
  }
} else {
  Fail "便携包不存在：$zip（交付链缺一方）"
}

if ($stampSha -and $stampSha -eq $sha) { Pass "stamp 与 build 一致" }
elseif ($stampSha) { Fail "stamp 与 build 不一致" }

# ---------- 4. 签名状态（只记录，不阻断） ----------
# 依据：Mini v3.2.2 安装包未做 Authenticode 签名（§六.4 照抄现状）；
# 本机自签证书对外无效，公网可信证书需用户单独采购，故不作为发布门禁。
$sig = Get-AuthenticodeSignature -FilePath $Exe -ErrorAction SilentlyContinue
"INFO  Authenticode: $($sig.Status) / $($sig.SignerCertificate.Subject)"

if (-not $Runtime) {
  ""
  "DELIVERY_PATH=$Exe"
  "DELIVERY_SHA256=$sha"
  "DELIVERY_MTIME=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
  "RESULT=" + $(if ($ok) { 'PASS (static gate)' } else { 'FAIL' })
  if (-not $ok) { exit 1 }
  exit 0
}

# ---------- 5. 运行态验收（显式 -Runtime 才执行；只读，不改写系统代理/TUN） ----------
$procs = @(Get-Process clashwin -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) {
  Fail "没有运行中的 clashwin（运行态验收时应起唯一实例）"
} elseif ($procs.Count -gt 1) {
  Fail "存在 $($procs.Count) 个 clashwin 进程（只允许 1 个）"
} else {
  $p = $procs[0]
  if ($p.Path -ne $Exe) { Fail "运行实例路径不可信: $($p.Path)" }
  else { Pass "唯一实例 PID=$($p.Id) 路径=$($p.Path)" }
}

$svc = sc.exe query clash_verge_service 2>&1 | Out-String
if ($svc -match 'RUNNING') { Pass "clash_verge_service RUNNING" } else { Fail "clash_verge_service 非 RUNNING" }

$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
$ip = Get-ItemProperty $key
if ($ip.ProxyEnable -eq 1) { Pass 'ProxyEnable=1' } else { Fail 'ProxyEnable 非 1（运行态验收期望系统代理开启）' }
if ($ip.ProxyServer -eq '127.0.0.1:10801') { Pass 'ProxyServer=127.0.0.1:10801' } else { Fail "ProxyServer=$($ip.ProxyServer)" }

""
"DELIVERY_PATH=$Exe"
"DELIVERY_SHA256=$sha"
"DELIVERY_MTIME=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
"RESULT=" + $(if ($ok) { 'PASS' } else { 'FAIL' })
if (-not $ok) { exit 1 }
