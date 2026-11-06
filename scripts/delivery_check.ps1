# delivery_check.ps1 — 交付前自检（单一事实源 + 接线标记 + 运行态一致性）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\delivery_check.ps1 -Record
#       构建完成后调用：记录当前 exe 的 SHA256 到 .delivery\stamp.json
#   powershell -ExecutionPolicy Bypass -File scripts\delivery_check.ps1
#       交付前调用：校验 exe 与 Record 时的产物一致，并检查运行态
#
# 单一事实源：build\bin\Release\clashwin.exe（唯一允许交付给用户的路径）。
# 规则：任何其它目录下的同名进程一律视为不可信，需先杀掉再交付。

param(
  [string]$Exe = 'C:\Users\sun_y\Documents\AntiGravity_Projects\ClashWin\build\bin\Release\clashwin.exe',
  [switch]$Record
)

$ErrorActionPreference = 'Continue'
$ok = $true
function Pass($m) { "PASS  $m" }
function Fail($m) { "FAIL  $m"; $script:ok = $false }

if (-not (Test-Path $Exe)) {
  Fail "exe 不存在: $Exe"
  exit 1
}

$item = Get-Item $Exe
$sha = (Get-FileHash $Exe -Algorithm SHA256).Hash

# ---------- 1. SHA256 与刚构建产物一致 ----------
$stampDir = Join-Path (Split-Path $Exe -Parent) '.delivery'
$stampFile = Join-Path $stampDir 'stamp.json'
if ($Record) {
  New-Item -ItemType Directory -Force -Path $stampDir | Out-Null
  @{ path = $Exe; sha256 = $sha; mtime = $item.LastWriteTime.ToString('o') } |
    ConvertTo-Json | Out-File $stampFile -Encoding utf8
  Pass "已记录交付基线: $stampFile"
} else {
  if (-not (Test-Path $stampFile)) {
    Fail "缺少交付基线 $stampFile（请先构建后运行 -Record）"
  } else {
    $stamp = Get-Content $stampFile -Raw | ConvertFrom-Json
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

if ($Record) {
  "DELIVERY_PATH=$Exe"
  "DELIVERY_SHA256=$sha"
  "DELIVERY_MTIME=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
  "RESULT=PASS (baseline recorded; runtime checks skipped)"
  exit 0
}

# ---------- 3. 唯一实例且路径已登记 ----------
$procs = @(Get-Process clashwin -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) {
  Fail "没有运行中的 clashwin（交付前应起唯一实例）"
} elseif ($procs.Count -gt 1) {
  Fail "存在 $($procs.Count) 个 clashwin 进程（只允许 1 个）"
} else {
  $p = $procs[0]
  if ($p.Path -ne $Exe) { Fail "运行实例路径不可信: $($p.Path)" }
  else { Pass "唯一实例 PID=$($p.Id) 路径=$($p.Path)" }
}

# ---------- 4. 服务 ----------
$svc = sc.exe query clash_verge_service 2>&1 | Out-String
if ($svc -match 'RUNNING') { Pass "clash_verge_service RUNNING" } else { Fail "clash_verge_service 非 RUNNING" }
$cfg = sc.exe qc clash_verge_service 2>&1 | Out-String
if ($cfg -match 'Clash Mini') { Pass "服务 ImagePath 指向 Clash Mini" } else { Fail "服务 ImagePath 异常" }

# ---------- 5. 当前接管模式 ----------
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
$ip = Get-ItemProperty $key
if ($ip.ProxyEnable -eq 1) { Pass 'ProxyEnable=1' } else { Fail 'ProxyEnable 非 1（系统代理应开启）' }
if ($ip.ProxyServer -eq '127.0.0.1:10811') { Pass 'ProxyServer=127.0.0.1:10811' } else { Fail "ProxyServer=$($ip.ProxyServer)" }

$verge = Get-ChildItem "$env:LOCALAPPDATA","$env:APPDATA" -Filter verge.yaml -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($verge) {
  $t = [IO.File]::ReadAllText($verge.FullName)
  if ($t -match 'enable_tun_mode:\s*true') { Fail 'enable_tun_mode=true（交付现场应为 TUN OFF）' }
  else { Pass 'enable_tun_mode=false（TUN OFF）' }
}

""
"DELIVERY_PATH=$Exe"
"DELIVERY_SHA256=$sha"
"DELIVERY_MTIME=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
if ($ok) { "RESULT=PASS" } else { "RESULT=FAIL" }
if (-not $ok) { exit 1 }
