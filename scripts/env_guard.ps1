# env_guard.ps1 — 实测环境守卫（Pre/Post），前后一律清空自家进程
#
# 规则（2026-11-09 起：Mini 与 Win 互斥，端口 1:1；旧「Mini 占 10801 属正常」规则作废）：
#   - 端口口径以 include\clashwin\config\constants.h 为准：
#     10801 mixed / 10802 socks / 10803 http / 9098 controller / 33335 单实例。
#     7899 只是 http 兜底端口，允许被无关程序占用，不进断言。
#   - 清理对象：clashwin.exe、clashwin_test.exe（全部实例）；mini-mihomo.exe
#     （仓库 build* / %TEMP%\cw* / C:\Program Files\Clash Mini 下）；clash-mini.exe
#   - 服务 clash_verge_service：不再要求保留或 RUNNING。测试中可自由 stop / delete /
#     用我方 sidecar\clash-verge-service-install.exe 安装，也可用 Mini 安装器恢复。
#     批次收尾只要求「任一方都能自行恢复」：服务在且 ImagePath 指向某一边，或服务不存在
#     但两边安装器都在原位可执行（收尾时必须明确写出当前属于哪种）。
#   - 每条清理可审计：PID | 进程名 | 完整路径 | 启动时间 | 结果
#   - 清场后网络体检：默认路由必须回到 192.168.1.1；若残留 198.18.x 默认路由 /
#     Mihomo·Wintun 适配器被拉起，一并清理（需管理员时打印提权命令）
#   - 断言：自家存活进程数 = 0；10801、10802、10803、9098、33335 全部空闲；
#     占用方区分为「自家 / Clash Mini / 其他程序」，任何一方占用都判 NOT READY / NOT CLEAN（退出码 1）
#   - 僵尸 PID 条目（0 线程，进程已退出、句柄被启动器持有）无法 Kill，单列 WARN，不计入存活数
#
# 用法：
#   powershell -NoProfile -File scripts\env_guard.ps1 -Phase Pre
#   powershell -NoProfile -File scripts\env_guard.ps1 -Phase Post

param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('Pre', 'Post')]
  [string]$Phase
)

$ErrorActionPreference = 'Continue'

$Repo = Split-Path -Parent $PSScriptRoot
$OurNames = @('clashwin', 'clashwin_test', 'mini-mihomo', 'clash-mini')
$ExcludedNamePatterns = @('clash_verge_service', 'clash-verge-service')
$WatchPorts = @(10801, 10802, 10803, 9098, 33335)

function Write-Section([string]$text) {
  Write-Host ''
  Write-Host ('=== ' + $text + ' ===') -ForegroundColor Cyan
}

function Get-ProcPath([int]$procId) {
  try {
    $p = Get-Process -Id $procId -ErrorAction Stop
    if ($p.Path) { return $p.Path }
  } catch { }
  try {
    $c = Get-CimInstance Win32_Process -Filter ("ProcessId=" + $procId) -ErrorAction Stop
    if ($c -and $c.ExecutablePath) { return $c.ExecutablePath }
  } catch { }
  return $null
}

function Get-ThreadCount([int]$procId) {
  try { return (Get-Process -Id $procId -ErrorAction Stop).Threads.Count } catch { return -1 }
}

function Test-OwnedByUs([string]$name, [string]$path) {
  foreach ($pat in $ExcludedNamePatterns) {
    if ($name -like $pat) { return $false }
  }
  if ($name -eq 'clashwin' -or $name -eq 'clashwin_test' -or $name -eq 'clash-mini') { return $true }
  if ($name -eq 'mini-mihomo') {
    if (-not $path) { return $false }
    $lp = $path.ToLower()
    return ($lp.StartsWith($Repo.ToLower()) -or
            $lp.StartsWith(($env:TEMP.ToLower() + '\cw')) -or
            $lp.Contains('program files\clash mini'))
  }
  return $false
}

function Get-PortOwnerKind([string]$path) {
  # 占用方归属：own（本仓库）/ mini（Clash Mini 一侧）/ other（无关程序）
  if (-not $path) { return 'other' }
  $lp = $path.ToLower()
  $repoPrefix = $Repo.ToLower() + '\'
  if ($lp.StartsWith($repoPrefix) -or $lp.Contains('\cwrun\')) { return 'own' }
  if ($lp.Contains('clash mini') -or $lp.Contains('clash-mini')) { return 'mini' }
  return 'other'
}

function Scan-Processes {
  $result = @()
  foreach ($name in $OurNames) {
    foreach ($p in (Get-Process -Name $name -ErrorAction SilentlyContinue)) {
      $path = $null
      try { $path = $p.Path } catch { }
      if (-not $path) { $path = Get-ProcPath $p.Id }
      $threads = -1
      try { $threads = $p.Threads.Count } catch { $threads = -1 }
      $start = $null
      try { $start = $p.StartTime } catch { }
      $hwnd = 0
      try { $hwnd = $p.MainWindowHandle } catch { }
      $result += [pscustomobject]@{
        Id = $p.Id; Name = $name; Path = $path; Start = $start; Threads = $threads; Hwnd = $hwnd
        Owned = (Test-OwnedByUs $name $path)
      }
    }
  }
  return $result
}

function Scan-Excluded {
  $result = @()
  foreach ($pat in $ExcludedNamePatterns) {
    foreach ($p in (Get-Process -Name $pat -ErrorAction SilentlyContinue)) {
      $path = $null
      try { $path = $p.Path } catch { }
      if (-not $path) { $path = Get-ProcPath $p.Id }
      $result += [pscustomobject]@{ Id = $p.Id; Name = $p.ProcessName; Path = $path }
    }
  }
  return $result
}

function Get-PortTable {
  $rows = @()
  foreach ($port in $WatchPorts) {
    $conns = @(Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue)
    if ($conns.Count -eq 0) {
      $rows += [pscustomobject]@{ Port = $port; State = 'FREE'; OwnerPid = $null; OwnerPath = $null; OwnerKind = $null }
    } else {
      foreach ($c in $conns) {
        $op = Get-ProcPath $c.OwningProcess
        $rows += [pscustomobject]@{ Port = $port; State = 'LISTEN'; OwnerPid = $c.OwningProcess; OwnerPath = $op; OwnerKind = (Get-PortOwnerKind $op) }
      }
    }
  }
  return $rows
}

function Format-Audit($proc, [string]$result, [string]$note) {
  $start = if ($proc.Start) { $proc.Start.ToString('yyyy-MM-dd HH:mm:ss') } else { '<unknown>' }
  $path = if ($proc.Path) { $proc.Path } else { '<unknown>' }
  Write-Host ('  ' + $proc.Id + ' | ' + $proc.Name + ' | ' + $path + ' | ' + $start + ' | ' + $result + $(if ($note) { ' : ' + $note } else { '' }))
}

function Remove-OwnProcess($proc) {
  if ($proc.Threads -eq 0) {
    Format-Audit $proc 'SKIP(already exited)' ''
    return 'ZOMBIE'
  }
  try {
    Stop-Process -Id $proc.Id -Force -ErrorAction Stop
    Format-Audit $proc 'SUCCESS' ''
    return 'KILLED'
  } catch {
    $msg = $_.Exception.Message
    Format-Audit $proc 'FAILED' $msg
    Write-Host ('    → 提权清理：Start-Process powershell -Verb RunAs -ArgumentList ''-NoProfile'',''-Command'',"Stop-Process -Id ' + $proc.Id + ' -Force"') -ForegroundColor Yellow
    return 'FAILED'
  }
}

# ---------------------------------------------------------------- 网络快照 / 还原
# 规则：测试不得改动系统网络（TUN / 系统代理 / WinHTTP / DNS / 默认路由）。
# Pre 把基线写入 state json；Post 逐项比对，有差异就自动还原，失败即 NET-DIRTY（Post 退出非 0）。
$NetStateDir = Join-Path $env:TEMP 'cwrun'
$NetStateFile = Join-Path $NetStateDir 'net_state_pre.json'
$ProxyRegKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'

function Get-ProxyState {
  $it = Get-ItemProperty -Path $ProxyRegKey -ErrorAction SilentlyContinue
  $enabled = $null
  if ($it -and $null -ne $it.ProxyEnable) { $enabled = [int]$it.ProxyEnable }
  return [pscustomobject]@{
    ProxyEnable   = $enabled
    ProxyServer   = $(if ($it) { $it.ProxyServer } else { $null })
    ProxyOverride = $(if ($it) { $it.ProxyOverride } else { $null })
    AutoConfigURL = $(if ($it) { $it.AutoConfigURL } else { $null })
    AutoDetect    = $(if ($it) { $it.AutoDetect } else { $null })
  }
}

function Get-WinHttpText {
  try { return (((netsh winhttp show proxy) | Out-String).Trim()) } catch { return '<unknown>' }
}

function Test-DefaultIpv6Dns([string]$s) {
  return ($s -match '^fec0:0:0:ffff::[123]$')
}

function Get-DnsRows {
  $rows = @()
  foreach ($r in (Get-DnsClientServerAddress -ErrorAction SilentlyContinue)) {
    $servers = @($r.ServerAddresses)
    # 过滤系统默认 IPv6 占位值（fec0:0:0:ffff::1/2/3），它们不是"被改过的"配置
    if ([int]$r.AddressFamily -ne 2) {
      $real = @($servers | Where-Object { -not (Test-DefaultIpv6Dns $_) })
      if ($real.Count -eq 0) { $servers = @() } else { $servers = $real }
    }
    if ($servers.Count -eq 0 -and [int]$r.AddressFamily -ne 2) { continue }
    $rows += [pscustomobject]@{
      InterfaceIndex = [int]$r.InterfaceIndex
      InterfaceAlias = [string]$r.InterfaceAlias
      AddressFamily  = [int]$r.AddressFamily
      Servers        = $servers
    }
  }
  return $rows
}

function Get-DefaultRouteRows {
  $rows = @()
  foreach ($r in (Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue)) {
    $rows += [pscustomobject]@{
      InterfaceIndex = [int]$r.ifIndex
      InterfaceAlias = [string]$r.InterfaceAlias
      NextHop        = [string]$r.NextHop
      RouteMetric    = [int]$r.RouteMetric
    }
  }
  return $rows
}

function Get-AdapterRows {
  $rows = @()
  foreach ($a in (Get-NetAdapter -ErrorAction SilentlyContinue)) {
    $rows += [pscustomobject]@{
      Name        = [string]$a.Name
      ifIndex     = [int]$a.ifIndex
      Status      = [string]$a.Status
      Description = [string]$a.InterfaceDescription
    }
  }
  return $rows
}

function Get-NetState {
  return [pscustomobject]@{
    Timestamp     = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
    Proxy         = Get-ProxyState
    WinHttp       = Get-WinHttpText
    Dns           = @(Get-DnsRows)
    DefaultRoutes = @(Get-DefaultRouteRows)
    Adapters      = @(Get-AdapterRows)
  }
}

function Write-NetState($state) {
  $ad = @($state.Adapters | ForEach-Object { $_.Name + '(' + $_.ifIndex + ',' + $_.Status + ')' })
  Write-Host ('  网卡: ' + $(if ($ad.Count) { $ad -join '; ' } else { '<none>' }))
  $rt = @($state.DefaultRoutes | ForEach-Object { 'if' + $_.InterfaceIndex + '/' + $_.InterfaceAlias + ' -> ' + $_.NextHop + ' metric ' + $_.RouteMetric })
  Write-Host ('  默认路由: ' + $(if ($rt.Count) { $rt -join '; ' } else { '<none>' }))
  Write-Host ('  系统代理: ProxyEnable=' + $state.Proxy.ProxyEnable + ' ProxyServer=' + $(if ($state.Proxy.ProxyServer) { $state.Proxy.ProxyServer } else { '<empty>' }) + ' ProxyOverride=' + $(if ($state.Proxy.ProxyOverride) { $state.Proxy.ProxyOverride } else { '<empty>' }) + ' AutoConfigURL=' + $(if ($state.Proxy.AutoConfigURL) { $state.Proxy.AutoConfigURL } else { '<empty>' }) + ' AutoDetect=' + $(if ($state.Proxy.AutoDetect) { $state.Proxy.AutoDetect } else { '<empty>' }))
  Write-Host ('  WinHTTP: ' + (("$($state.WinHttp)" -replace "`r?`n", ' ') -replace '\s+', ' '))
  $dns = @($state.Dns | Where-Object { @($_.Servers).Count -gt 0 } | ForEach-Object { '[' + $_.InterfaceIndex + '/' + $(if ($_.AddressFamily -eq 2) { 'v4' } else { 'v6' }) + '] ' + (@($_.Servers) -join ',') })
  Write-Host ('  DNS: ' + $(if ($dns.Count) { $dns -join '; ' } else { '<无自定义>' }))
}

function Compare-NetState($pre, $cur) {
  $diffs = @()

  foreach ($k in @('ProxyEnable', 'ProxyServer', 'ProxyOverride', 'AutoConfigURL', 'AutoDetect')) {
    $a = $pre.Proxy.$k; $b = $cur.Proxy.$k
    if ("$a" -ne "$b") { $diffs += [pscustomobject]@{ Kind = 'Proxy'; Key = $k; Pre = "$a"; Cur = "$b" } }
  }

  if ("$($pre.WinHttp)" -ne "$($cur.WinHttp)") {
    $diffs += [pscustomobject]@{ Kind = 'WinHttp'; Key = 'settings'; Pre = "$($pre.WinHttp)"; Cur = "$($cur.WinHttp)" }
  }

  $preDns = @{}
  foreach ($d in @($pre.Dns)) { $preDns[('' + $d.InterfaceIndex + '|' + $d.AddressFamily)] = (@($d.Servers) -join ',') }
  $curDns = @{}
  foreach ($d in @($cur.Dns)) { $curDns[('' + $d.InterfaceIndex + '|' + $d.AddressFamily)] = (@($d.Servers) -join ',') }
  foreach ($k in @($preDns.Keys)) {
    $b = if ($curDns.ContainsKey($k)) { $curDns[$k] } else { '<interface-missing>' }
    if ($preDns[$k] -ne $b) { $diffs += [pscustomobject]@{ Kind = 'Dns'; Key = $k; Pre = $preDns[$k]; Cur = $b } }
  }
  foreach ($k in @($curDns.Keys)) {
    if (-not $preDns.ContainsKey($k) -and $curDns[$k]) {
      $diffs += [pscustomobject]@{ Kind = 'Dns'; Key = $k; Pre = '<absent>'; Cur = $curDns[$k] }
    }
  }

  $preR = @($pre.DefaultRoutes | ForEach-Object { '' + $_.InterfaceIndex + '|' + $_.NextHop + '|' + $_.RouteMetric })
  $curR = @($cur.DefaultRoutes | ForEach-Object { '' + $_.InterfaceIndex + '|' + $_.NextHop + '|' + $_.RouteMetric })
  foreach ($r in $preR) { if ($curR -notcontains $r) { $diffs += [pscustomobject]@{ Kind = 'RouteMissing'; Key = $r; Pre = $r; Cur = '<absent>' } } }
  foreach ($r in $curR) { if ($preR -notcontains $r) { $diffs += [pscustomobject]@{ Kind = 'RouteExtra'; Key = $r; Pre = '<absent>'; Cur = $r } } }

  foreach ($a in @($cur.Adapters)) {
    $old = @($pre.Adapters | Where-Object { $_.Name -eq $a.Name }) | Select-Object -First 1
    if (-not $old) {
      $diffs += [pscustomobject]@{ Kind = 'AdapterExtra'; Key = $a.Name; Pre = '<absent>'; Cur = ($a.ifIndex.ToString() + ' ' + $a.Status + ' ' + $a.Description) }
    } elseif ($old.Status -ne $a.Status) {
      $diffs += [pscustomobject]@{ Kind = 'AdapterStatus'; Key = $a.Name; Pre = $old.Status; Cur = $a.Status }
    }
  }
  foreach ($oa in @($pre.Adapters)) {
    if (@($cur.Adapters | Where-Object { $_.Name -eq $oa.Name }).Count -eq 0) {
      $diffs += [pscustomobject]@{ Kind = 'AdapterMissing'; Key = $oa.Name; Pre = $oa.Status; Cur = '<absent>' }
    }
  }

  return $diffs
}

function Restore-ProxyState($pre) {
  $failed = @()
  foreach ($k in @('ProxyEnable', 'ProxyServer', 'ProxyOverride', 'AutoConfigURL', 'AutoDetect')) {
    try {
      $v = $pre.Proxy.$k
      $exists = $null -ne (Get-ItemProperty -Path $ProxyRegKey -Name $k -ErrorAction SilentlyContinue)
      if ($null -eq $v -or "$v" -eq '') {
        if ($exists) { Remove-ItemProperty -Path $ProxyRegKey -Name $k -Force -ErrorAction Stop }
      } elseif ($k -eq 'ProxyEnable' -or $k -eq 'AutoDetect') {
        Set-ItemProperty -Path $ProxyRegKey -Name $k -Value ([int]$v) -Type DWord -Force -ErrorAction Stop
      } else {
        Set-ItemProperty -Path $ProxyRegKey -Name $k -Value ([string]$v) -Type String -Force -ErrorAction Stop
      }
    } catch { $failed += ('proxy ' + $k + ': ' + $_.Exception.Message) }
  }
  return $failed
}

function Restore-DnsDiffs($diffs) {
  $failed = @()
  foreach ($d in @($diffs | Where-Object { $_.Kind -eq 'Dns' })) {
    $parts = $d.Key -split '\|'
    $idx = [int]$parts[0]
    $servers = @()
    if ($d.Pre -and $d.Pre -ne '<absent>' -and $d.Pre -ne '<interface-missing>') { $servers = @($d.Pre -split ',') }
    try {
      if ($servers.Count -eq 0) {
        Set-DnsClientServerAddress -InterfaceIndex $idx -ResetServerAddresses -ErrorAction Stop
      } else {
        Set-DnsClientServerAddress -InterfaceIndex $idx -ServerAddresses $servers -ErrorAction Stop
      }
    } catch { $failed += ('DNS ' + $d.Key + ': ' + $_.Exception.Message) }
  }
  return $failed
}

function Restore-RouteDiffs($diffs) {
  $failed = @()
  foreach ($d in @($diffs | Where-Object { $_.Kind -eq 'RouteExtra' })) {
    $p = $d.Cur -split '\|'
    try {
      Remove-NetRoute -DestinationPrefix '0.0.0.0/0' -InterfaceIndex ([int]$p[0]) -NextHop $p[1] -Confirm:$false -ErrorAction Stop
    } catch { $failed += ('route remove ' + $d.Cur + ': ' + $_.Exception.Message) }
  }
  foreach ($d in @($diffs | Where-Object { $_.Kind -eq 'RouteMissing' })) {
    $p = $d.Pre -split '\|'
    try {
      New-NetRoute -DestinationPrefix '0.0.0.0/0' -InterfaceIndex ([int]$p[0]) -NextHop $p[1] -RouteMetric ([int]$p[2]) -ErrorAction Stop
    } catch { $failed += ('route add ' + $d.Pre + ': ' + $_.Exception.Message) }
  }
  return $failed
}

function Restore-AdapterDiffs($diffs) {
  $failed = @()
  foreach ($d in @($diffs | Where-Object { $_.Kind -eq 'AdapterExtra' })) {
    try { Disable-NetAdapter -Name $d.Key -Confirm:$false -ErrorAction Stop }
    catch { $failed += ('adapter disable ' + $d.Key + ': ' + $_.Exception.Message) }
  }
  foreach ($d in @($diffs | Where-Object { $_.Kind -eq 'AdapterMissing' })) {
    try { Enable-NetAdapter -Name $d.Key -Confirm:$false -ErrorAction Stop }
    catch { $failed += ('adapter enable ' + $d.Key + ': ' + $_.Exception.Message) }
  }
  foreach ($d in @($diffs | Where-Object { $_.Kind -eq 'AdapterStatus' })) {
    try {
      if ($d.Pre -eq 'Up') { Enable-NetAdapter -Name $d.Key -Confirm:$false -ErrorAction Stop }
      else { Disable-NetAdapter -Name $d.Key -Confirm:$false -ErrorAction Stop }
    } catch { $failed += ('adapter status ' + $d.Key + ': ' + $_.Exception.Message) }
  }
  return $failed
}

function Restore-WinHttp($preText) {
  $failed = @()
  if ($preText -match 'Direct access') {
    $out = netsh winhttp reset proxy 2>&1
    if ($LASTEXITCODE -ne 0) { $failed += ('winhttp reset 失败: ' + (@($out) -join ' | ')) }
  } else {
    $failed += ('winhttp 需手工恢复（基线：' + $preText + '）')
  }
  return $failed
}

function Invoke-NetSnapshot {
  Write-Section '6) 网络基线（写入 state，供 Post 还原）'
  $state = Get-NetState
  New-Item -ItemType Directory -Force -Path $NetStateDir | Out-Null
  ($state | ConvertTo-Json -Depth 8) | Set-Content -Path $NetStateFile -Encoding UTF8
  Write-NetState $state
  Write-Host ('  state -> ' + $NetStateFile)
  # 残余登记：ProxyEnable=0 但 ProxyServer/ProxyOverride 仍指向本机内核端口。
  # 无法证明是系统原有值时一律“只记录、不清理”，Post 以基线为准也不会抹掉。
  if ($state.Proxy.ProxyEnable -eq 0 -and
      $state.Proxy.ProxyServer -match '^(127\.0\.0\.1|localhost):(10801|10802|10803|9098|7899)$') {
    Write-Host ('  NET-PENDING-RESIDUE  ProxyServer=' + $state.Proxy.ProxyServer +
                '  ProxyOverride=' + $state.Proxy.ProxyOverride +
                '  （未启用，保留不动；归属待人工确认）') -ForegroundColor Yellow
  }
  Write-Host '  网络策略：测试只读；任何 TUN/系统代理/DNS/路由改动必须由 -AllowNetworkChange 显式授权并在本轮内还原'
}

function Invoke-NetCheck {
  Write-Section '6) 网络对比与还原'
  if (-not (Test-Path $NetStateFile)) {
    Write-Host '  NET-DIRTY: 缺少 Pre 网络基线（未按流程先跑 Pre）' -ForegroundColor Red
    return @('NET-DIRTY: 缺少 Pre 网络基线')
  }
  $pre = Get-Content $NetStateFile -Raw | ConvertFrom-Json
  $diffs = @(Compare-NetState $pre (Get-NetState))
  if ($diffs.Count -eq 0) {
    Write-Host '  NET-RESTORED（无差异）' -ForegroundColor Green
    return @()
  }

  Write-Host ('  检测到 ' + $diffs.Count + ' 处网络差异：') -ForegroundColor Yellow
  foreach ($d in $diffs) { Write-Host ('    NET-DIFF ' + $d.Kind + ' ' + $d.Key + ' : ' + $d.Pre + ' -> ' + $d.Cur) }

  $failed = @()
  $failed += Restore-ProxyState $pre
  $failed += Restore-DnsDiffs $diffs
  $failed += Restore-RouteDiffs $diffs
  $failed += Restore-AdapterDiffs $diffs
  if (@($diffs | Where-Object { $_.Kind -eq 'WinHttp' }).Count -gt 0) { $failed += Restore-WinHttp $pre.WinHttp }
  Start-Sleep -Milliseconds 900

  $remain = @(Compare-NetState $pre (Get-NetState))
  if (@($failed).Count -gt 0 -or $remain.Count -gt 0) {
    Write-Host '  NET-DIRTY：自动还原未完全成功' -ForegroundColor Red
    foreach ($f in $failed) { Write-Host ('    RESTORE-FAILED ' + $f) -ForegroundColor Red }
    foreach ($d in $remain) { Write-Host ('    REMAIN ' + $d.Kind + ' ' + $d.Key + ' : ' + $d.Pre + ' -> ' + $d.Cur) -ForegroundColor Red }
    Write-Host ('    → 提权修复：Start-Process powershell -Verb RunAs -ArgumentList ''-NoProfile'',''-File'',"' + $PSCommandPath + '",''-Phase'',''Post''') -ForegroundColor Yellow
    return @('NET-DIRTY（' + $remain.Count + ' 处未还原）')
  }

  Write-Host '  NET-RESTORED' -ForegroundColor Green
  return @()
}

function Invoke-NetworkResidueRepair {
  Write-Section '4b) 清场后网络体检（断网红线）'
  $fail = @()
  $bad = @(Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
           Where-Object { $_.NextHop -match '^198\.18\.' })
  if ($bad.Count -eq 0) { Write-Host '  198.18.x 默认路由: 无' -ForegroundColor Green }
  foreach ($r in $bad) {
    try {
      Remove-NetRoute -DestinationPrefix '0.0.0.0/0' -InterfaceIndex $r.ifIndex -NextHop $r.NextHop -Confirm:$false -ErrorAction Stop
      Write-Host ('  ROUTE-REMOVED ' + $r.InterfaceAlias + ' -> ' + $r.NextHop)
    } catch {
      $fail += ('route ' + $r.InterfaceAlias)
      Write-Host ('  ROUTE-REMOVE-FAILED ' + $r.InterfaceAlias + ' -> ' + $r.NextHop + ' : ' + $_.Exception.Message) -ForegroundColor Red
      Write-Host ('    → 提权：Start-Process powershell -Verb RunAs -ArgumentList ''-NoProfile'',''-Command'',"Remove-NetRoute -DestinationPrefix ''0.0.0.0/0'' -InterfaceIndex ' + $r.ifIndex + ' -NextHop ' + $r.NextHop + ' -Confirm:`$false"') -ForegroundColor Yellow
    }
  }
  $wns = @(Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.InterfaceDescription -match 'Wintun|Mihomo' -and $_.Status -eq 'Up' })
  if ($wns.Count -eq 0) { Write-Host '  Wintun/Mihomo 适配器: 无 Up 状态残留' -ForegroundColor Green }
  foreach ($a in $wns) {
    try {
      Disable-NetAdapter -Name $a.Name -Confirm:$false -ErrorAction Stop
      Write-Host ('  ADAPTER-DISABLED ' + $a.Name)
    } catch {
      $fail += ('adapter ' + $a.Name)
      Write-Host ('  ADAPTER-DISABLE-FAILED ' + $a.Name + ' : ' + $_.Exception.Message) -ForegroundColor Red
      Write-Host ('    → 提权：Start-Process powershell -Verb RunAs -ArgumentList ''-NoProfile'',''-Command'',"Disable-NetAdapter -Name ''' + $a.Name + ''' -Confirm:`$false"') -ForegroundColor Yellow
    }
  }
  $def = @(Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue)
  $ok = @($def | Where-Object { $_.NextHop -eq '192.168.1.1' })
  if ($ok.Count -gt 0) { Write-Host ('  DEFAULT-ROUTE ' + $ok[0].InterfaceAlias + ' -> 192.168.1.1') -ForegroundColor Green }
  else { $fail += '默认路由未回到 192.168.1.1'; Write-Host '  DEFAULT-ROUTE 异常（未见 192.168.1.1）' -ForegroundColor Red }
  if ($fail.Count -gt 0) { return @('断网红线: ' + ($fail -join '; ')) }
  return @()
}

function Invoke-Cleanup([string]$phase) {
  Write-Section ('env_guard ' + $phase + '  ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
  Write-Host ('repo = ' + $Repo)

  Write-Section '1) 排除对象（绝不触碰）'
  $excluded = @(Scan-Excluded)
  if ($excluded.Count -eq 0) {
    Write-Host '  (当前无 clash_verge_service 进程)'
  } else {
    foreach ($e in $excluded) {
      $p = if ($e.Path) { $e.Path } else { '<unknown>' }
      Write-Host ('  EXCLUDED ' + $e.Id + ' | ' + $e.Name + ' | ' + $p) -ForegroundColor Magenta
    }
  }

  Write-Section '2) 清理前：自家进程基线'
  $before = @(Scan-Processes | Where-Object { $_.Owned })
  if ($before.Count -eq 0) { Write-Host '  (无)' }
  foreach ($p in $before) { Format-Audit $p 'BASELINE' ('threads=' + $p.Threads) }

  Write-Section '3) 端口基线'
  foreach ($r in (Get-PortTable)) {
    if ($r.State -eq 'FREE') { Write-Host ('  ' + $r.Port + ' FREE') }
    else { Write-Host ('  ' + $r.Port + ' LISTEN pid=' + $r.OwnerPid + ' [' + $r.OwnerKind + '] path=' + $(if ($r.OwnerPath) { $r.OwnerPath } else { '<unknown>' })) -ForegroundColor Yellow }
  }

  Write-Section '4) 清理全部自家进程（无豁免）'
  $results = @()
  if ($before.Count -eq 0) { Write-Host '  (无需清理)' }
  foreach ($p in $before) { $results += Remove-OwnProcess $p }
  Start-Sleep -Milliseconds 1000
  $netRepair = @(Invoke-NetworkResidueRepair)

  Write-Section '5) 终态复验'
  $after = @(Scan-Processes | Where-Object { $_.Owned })
  $zombies = @($after | Where-Object { $_.Threads -eq 0 })
  $live = @($after | Where-Object { $_.Threads -ne 0 })
  foreach ($p in $after) { Format-Audit $p $(if ($p.Threads -eq 0) { 'ZOMBIE' } else { 'LIVE' }) ('threads=' + $p.Threads) }
  if ($after.Count -eq 0) { Write-Host '  (无任何自家进程)' }

  $ports = @(Get-PortTable)
  foreach ($r in $ports) {
    if ($r.State -eq 'FREE') { Write-Host ('  ' + $r.Port + ' FREE') }
    else { Write-Host ('  ' + $r.Port + ' LISTEN pid=' + $r.OwnerPid + ' [' + $r.OwnerKind + '] path=' + $(if ($r.OwnerPath) { $r.OwnerPath } else { '<unknown>' })) -ForegroundColor Red }
  }

  $netReasons = @($netRepair)
  if ($phase -eq 'Pre') {
    Invoke-NetSnapshot
  } else {
    $netReasons = @($netRepair) + @(Invoke-NetCheck)
  }

  $reasons = @($netReasons)
  if ($live.Count -gt 0) { $reasons += ('自家存活进程数=' + $live.Count + '（应为 0）') }
  foreach ($r in $ports) {
    if ($r.State -ne 'FREE') {
      $who = switch ($r.OwnerKind) { 'own' { '自家进程' } 'mini' { 'Clash Mini' } default { '其他程序' } }
      $reasons += ('端口 ' + $r.Port + ' 仍被' + $who + '(pid=' + $r.OwnerPid + ')占用；Mini 与 Win 互斥，需先停用占用方')
    }
  }

  $verdict = if ($reasons.Count -eq 0) { if ($phase -eq 'Pre') { 'READY' } else { 'clean' } } else { if ($phase -eq 'Pre') { 'NOT READY' } else { 'NOT CLEAN' } }
  Write-Section ($phase + ' 结论: ' + $verdict)
  Write-Host ('  存活自家进程 = ' + $live.Count + '；僵尸 PID 占位 = ' + $zombies.Count + $(if ($zombies.Count -gt 0) { ' (' + (($zombies | ForEach-Object { $_.Id }) -join ',') + ')' } else { '' })) -ForegroundColor $(if ($live.Count -eq 0) { 'Green' } else { 'Red' })
  foreach ($r in $reasons) { Write-Host ('  - ' + $r) -ForegroundColor Yellow }

  if ($verdict -eq 'READY' -or $verdict -eq 'clean') { return 0 } else { return 1 }
}

$code = Invoke-Cleanup $Phase
exit $code
