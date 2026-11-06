# run_app_test.ps1 — 一条命令完成「干净环境启动 → 四项自检 → 截图 → 收尾」
#
# 用法（默认：复制到 %TEMP%\cwrun\app 运行，最贴近发布形态）：
#   powershell -NoProfile -File scripts\run_app_test.ps1
#   powershell -NoProfile -File scripts\run_app_test.ps1 -Runs 5 -LogDir C:\...\shots
#
# 对比用（直接在仓库输出目录启动，不复制；用于崩溃率对照）：
#   powershell -NoProfile -File scripts\run_app_test.ps1 -FromRepo -Runs 5
#
# 四项自检（任一失败即判启动失败）：
#   1) PID 存活（线程数 > 0）
#   2) 33337 LISTEN 且 OwnerPid == 该 PID
#   3) 日志有新增（home\logs 下任一文件字节数变大或新增文件）
#   4) EnumWindows 能枚举到该 PID 的顶层窗口，且 PrintWindow 截图非空（像素方差达标）
#
# 退出码：0 = 全部 PASS；1 = 有 FAIL；2 = Pre 未就绪

param(
  [string]$AppDir,
  [switch]$FromRepo,
  [int]$Runs = 1,
  [int]$TimeoutSec = 40,
  [string]$WorkDir = (Join-Path $env:TEMP 'cwrun'),
  [string]$LogDir = (Join-Path $env:TEMP 'cwrun\shots'),
  [switch]$KeepInstance,
  [switch]$AllowNetworkChange,
  [ValidateSet('WMI', 'Explorer')]
  [string]$LaunchVia = 'WMI'
)

$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Drawing

$Repo = Split-Path -Parent $PSScriptRoot
if (-not $AppDir) {
  # 2026-11-06：build_verify\ 是已清理的中间产物目录，默认来源改为唯一交付事实源
  # build\bin\Release（与 scripts\delivery_check.ps1 及 docs\README.md:145 一致）。
  $AppDir = Join-Path $Repo 'build\bin\Release'
}
$RunDir = if ($FromRepo) { $AppDir } else { Join-Path $WorkDir 'app' }
$ExeName = 'clashwin.exe'
$SingletonPort = 33337

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class CwWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public struct RECT { public int L,T,R,B; }
  public static IntPtr FindWindow(uint target, out int w, out int h, out bool visible, out string title) {
    IntPtr found = IntPtr.Zero; int fw=0; int fh=0; bool fv=false; string ft="";
    EnumWindows((hw,l)=>{
      uint pid; GetWindowThreadProcessId(hw, out pid);
      if(pid==target){
        RECT r; GetWindowRect(hw, out r);
        var t=new StringBuilder(256); GetWindowTextW(hw,t,256);
        bool vis=IsWindowVisible(hw);
        int aw=r.R-r.L, ah=r.B-r.T;
        // 优先可见且有标题、面积足够
        if(vis && aw>200 && ah>150 && found==IntPtr.Zero){ found=hw; fw=aw; fh=ah; fv=vis; ft=t.ToString(); }
        if(found==IntPtr.Zero){ found=hw; fw=aw; fh=ah; fv=vis; ft=t.ToString(); }
      }
      return true;
    }, IntPtr.Zero);
    w=fw; h=fh; visible=fv; title=ft; return found;
  }
  public static void DumpWindows(uint target) {
    int n = 0;
    EnumWindows((hw,l)=>{
      uint pid; GetWindowThreadProcessId(hw, out pid);
      if(pid==target){
        n++;
        RECT r; GetWindowRect(hw, out r);
        var t=new StringBuilder(256); GetWindowTextW(hw,t,256);
        var c=new StringBuilder(256); GetClassNameW(hw,c,256);
        Console.WriteLine(string.Format("      hwnd=0x{0:X} class={1} visible={2} rect=({3},{4},{5},{6}) title='{7}'",
          hw.ToInt64(), c, IsWindowVisible(hw), r.L, r.T, r.R, r.B, t));
      }
      return true;
    }, IntPtr.Zero);
    if(n==0) Console.WriteLine("      (该 PID 无顶层窗口)");
  }
}
'@

# DPI 感知：非 96 DPI 下 GetWindowRect 会被虚拟化，PrintWindow 只截到左上角一小块，
# 底部流量条等区域被判"不可见"。显式声明后 rect 与截图均为物理像素、与窗口一致。
[CwWin]::SetProcessDPIAware() | Out-Null

function Write-Section([string]$t) { Write-Host ''; Write-Host ('=== ' + $t + ' ===') -ForegroundColor Cyan }

function Start-Detached([string]$exe, [string]$cwd, [string]$via) {
  if ($via -eq 'Explorer') {
    # ShellExecute 路径：复刻"资源管理器双击"的启动上下文（父进程=explorer，绕过 WMI/工具作业对象）
    $ts = Get-Date
    Start-Process -FilePath 'explorer.exe' -ArgumentList ('"' + $exe + '"') -ErrorAction SilentlyContinue | Out-Null
    for ($i = 0; $i -lt 40; $i++) {
      Start-Sleep -Milliseconds 250
      $name = [IO.Path]::GetFileNameWithoutExtension($exe)
      $cand = @(Get-Process -Name $name -ErrorAction SilentlyContinue | Where-Object {
          try { $_.StartTime -ge $ts.AddSeconds(-2) } catch { $false }
        })
      if ($cand.Count -ge 1) {
        $p = $cand | Sort-Object StartTime | Select-Object -Last 1
        return [pscustomobject]@{ Pid = [int]$p.Id; Method = 'Explorer(ShellExecute)' }
      }
    }
    Write-Host '  Explorer 启动后 10s 内未发现进程' -ForegroundColor Red
    return $null
  }

  # WMI Win32_Process.Create：脱离当前 shell/工具作业对象
  try {
    $wc = [wmiclass]'Win32_Process'
    $r = $wc.Create($exe, $cwd, $null)
    if ($r.ReturnValue -eq 0 -and $r.ProcessId -gt 0) {
      return [pscustomobject]@{ Pid = [int]$r.ProcessId; Method = 'WMI' }
    }
    Write-Host ('  WMI Create 返回 ' + $r.ReturnValue) -ForegroundColor Red
  } catch {
    Write-Host ('  WMI Create 异常：' + $_.Exception.Message) -ForegroundColor Red
  }
  $p = Start-Process -FilePath $exe -WorkingDirectory $cwd -PassThru -ErrorAction SilentlyContinue
  if ($p) { return [pscustomobject]@{ Pid = $p.Id; Method = 'Start-Process(fallback)' } }
  return $null
}

function Get-LogSnapshot([string]$AppHome) {
  $dir = Join-Path $AppHome 'logs'
  $map = @{}
  if (Test-Path $dir) {
    foreach ($f in Get-ChildItem $dir -File -Recurse -ErrorAction SilentlyContinue) {
      $map[$f.FullName] = $f.Length
    }
  }
  return $map
}

function Get-LogSize([string]$path) {
  if (Test-Path $path) { try { return (Get-Item $path).Length } catch { return 0 } }
  return 0
}

function Get-MainLogDelta([long]$beforeLen, [string]$MainLog) {
  $now = Get-LogSize $MainLog
  $delta = $now - $beforeLen
  $lines = 0
  if ($delta -gt 0) {
    try { $lines = @(Get-Content $MainLog -Tail 200 -ErrorAction SilentlyContinue).Count } catch { }
  }
  return [pscustomobject]@{ Changed = ($delta -gt 0); Bytes = $delta; Size = $now; TailLines = $lines }
}

function Get-LogDelta($before, [string]$AppHome) {
  $dir = Join-Path $AppHome 'logs'
  if (-not (Test-Path $dir)) { return @{ Changed = $false; Detail = 'no logs dir' } }
  $added = 0; $files = @()
  foreach ($f in Get-ChildItem $dir -File -Recurse -ErrorAction SilentlyContinue) {
    $old = if ($before.ContainsKey($f.FullName)) { $before[$f.FullName] } else { 0 }
    if ($f.Length -gt $old) { $added += ($f.Length - $old); $files += ($f.Name + '+' + ($f.Length - $old)) }
  }
  return @{ Changed = ($added -gt 0); Detail = ('+' + $added + 'B ' + ($files -join ',')); Bytes = $added }
}

function Get-CrashCount {
  try {
    return @(Get-WinEvent -LogName Application -MaxEvents 60 -ErrorAction Stop | Where-Object { $_.Id -eq 1000 -and $_.Message -match 'clashwin\.exe' }).Count
  } catch { return 0 }
}

function Stop-OurTree([int]$rootPid, [string]$runDir) {
  # 先杀本轮 app 派生的 sidecar（cwrun 下的 mini-mihomo），再杀 app 本体
  $rd = $runDir.ToLower()
  foreach ($m in (Get-Process -Name mini-mihomo -ErrorAction SilentlyContinue)) {
    $mp = $null
    try { $mp = $m.Path } catch { }
    if ($mp -and $mp.ToLower().StartsWith($rd)) {
      try { $m.Kill(); Write-Host ('  stopped sidecar pid=' + $m.Id) } catch { Write-Host ('  sidecar stop failed: ' + $_.Exception.Message) -ForegroundColor Yellow }
    }
  }
  $p = Get-Process -Id $rootPid -ErrorAction SilentlyContinue
  if ($p) {
    try { $p.Kill(); Write-Host ('  stopped app pid=' + $rootPid) } catch { Write-Host ('  app stop failed: ' + $_.Exception.Message) -ForegroundColor Yellow }
  }
}

function Save-WindowShot([IntPtr]$hwnd, [int]$w, [int]$h, [string]$path) {
  if ($hwnd -eq [IntPtr]::Zero -or $w -le 0 -or $h -le 0) { return $null }
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [CwWin]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  # 非空判定：采样网格，统计不同颜色数与主导色占比
  $colors = @{}
  $total = 0; $maxCount = 0
  for ($y = 0; $y -lt $h; $y += 8) {
    for ($x = 0; $x -lt $w; $x += 8) {
      $c = $bmp.GetPixel($x, $y).ToArgb()
      $total++
      if ($colors.ContainsKey($c)) { $colors[$c] = $colors[$c] + 1 } else { $colors[$c] = 1 }
      if ($colors[$c] -gt $maxCount) { $maxCount = $colors[$c] }
    }
  }
  $bmp.Dispose()
  return [pscustomobject]@{
    Ok = ($ok -and $total -gt 0 -and $colors.Count -ge 3 -and ($maxCount / $total) -lt 0.98)
    PrintWindow = $ok
    Colors = $colors.Count
    DominantRatio = [math]::Round(($maxCount / [math]::Max($total,1)), 3)
    File = $path
  }
}

# ---------------------------------------------------------------- Pre
Write-Section ('run_app_test  ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Write-Host ('source = ' + $AppDir)
Write-Host ('rundir = ' + $RunDir + $(if ($FromRepo) { '  (仓库路径，不复制)' } else { '  (复制运行）' }))
if ($AllowNetworkChange) {
  Write-Host '网络策略 = 允许改动（仅限开关 TUN/系统代理的本轮验证；必须在同一命令内开→验→关，Post 会比对并强制还原）' -ForegroundColor Yellow
} else {
  Write-Host '网络策略 = 只读（禁止改系统代理/TUN/DNS；验证出网请用显式代理 127.0.0.1:10811 或内核 API）' -ForegroundColor DarkGray
}
$pre = & (Join-Path $PSScriptRoot 'env_guard.ps1') -Phase Pre
$preExit = $LASTEXITCODE

if ($preExit -ne 0) {
  Write-Host 'Pre 未就绪，终止实验（先处理上面列出的端口/进程问题）' -ForegroundColor Red
  exit 2
}

# ---------------------------------------------------------------- 准备运行目录
Write-Section '准备运行目录'
if (-not $FromRepo) {
  for ($i = 1; $i -le 3; $i++) {
    try {
      if (Test-Path $RunDir) { Remove-Item $RunDir -Recurse -Force -ErrorAction Stop }
      break
    } catch {
      Write-Host ('  清理旧运行目录失败(第' + $i + '次)：' + $_.Exception.Message) -ForegroundColor Yellow
      Start-Sleep -Milliseconds 500
    }
  }
  New-Item -ItemType Directory -Force -Path $RunDir | Out-Null
  $copyOk = $false
  for ($i = 1; $i -le 3; $i++) {
    try {
      Copy-Item (Join-Path $AppDir $ExeName) $RunDir -Force -ErrorAction Stop
      if (Test-Path (Join-Path $RunDir $ExeName)) { $copyOk = $true; break }
    } catch {
      Write-Host ('  复制 exe 失败(第' + $i + '次)：' + $_.Exception.Message) -ForegroundColor Yellow
    }
    Start-Sleep -Milliseconds 500
  }
  if (-not $copyOk) {
    Write-Host '  复制 exe 失败：不要把这当成启动失败，先查文件锁/安全软件' -ForegroundColor Red
    exit 2
  }
  foreach ($item in @('resources', 'locales', 'sidecar')) {
    $src = Join-Path $AppDir $item
    if (Test-Path $src) { Copy-Item $src $RunDir -Recurse -Force }
  }
  Get-ChildItem $AppDir -Filter *.dll -ErrorAction SilentlyContinue | Copy-Item -Destination $RunDir -Force
  Write-Host ('  copied -> ' + $RunDir)
}

# 固定便携 home：<exe>\.config\PORTABLE → home = <exe>\.config\io.github.clash-win.clash-win
$portableDir = Join-Path $RunDir '.config'
New-Item -ItemType Directory -Force -Path $portableDir | Out-Null
$marker = Join-Path $portableDir 'PORTABLE'
if (-not (Test-Path $marker)) { New-Item -ItemType File -Path $marker | Out-Null }
$AppHome = Join-Path $portableDir 'io.github.clash-win.clash-win'
# 首启 home 不存在时 Logger::init 只建 <home>\logs（父目录缺失会导致 CreateDirectoryW 失败，主日志静默缺失）
# 这里预建 home 与 logs，保证 logs\clashwin.log 一定可写
New-Item -ItemType Directory -Force -Path (Join-Path $AppHome 'logs') | Out-Null
$MainLog = Join-Path $AppHome 'logs\clashwin.log'

# 测试专用 verge.yaml：禁止被测程序动系统网络（默认模板 enable_system_proxy=true，会让每次启动都把 ProxyEnable 置 1）
# 以真实 home 的 verge.yaml 为底本逐字段保留，仅强制两项为 false；只写测试 home，不改产品默认值。
$seedVerge = Join-Path $env:APPDATA 'io.github.clash-win.clash-win\verge.yaml'
$dstVerge = Join-Path $AppHome 'verge.yaml'
$vergeText = if (Test-Path $seedVerge) { Get-Content $seedVerge -Raw -Encoding UTF8 } else { "config_version: 3`nverge_mixed_port: 10811`n" }
$vergeText = $vergeText -replace '(?m)^enable_system_proxy:.*$', 'enable_system_proxy: false'
$vergeText = $vergeText -replace '(?m)^enable_tun_mode:.*$', 'enable_tun_mode: false'
if ($vergeText -notmatch '(?m)^enable_system_proxy:') { $vergeText += "`nenable_system_proxy: false`n" }
if ($vergeText -notmatch '(?m)^enable_tun_mode:') { $vergeText += "`nenable_tun_mode: false`n" }
[IO.File]::WriteAllText($dstVerge, $vergeText, (New-Object System.Text.UTF8Encoding($false)))

Write-Host ('  home = ' + $AppHome)
Write-Host ('  main log = ' + $MainLog)

# ---------------------------------------------------------------- 启动 + 四项自检
$exe = Join-Path $RunDir $ExeName
$allResults = @()

for ($run = 1; $run -le $Runs; $run++) {
  Write-Section ('Run ' + $run + '/' + $Runs + '  启动 ' + (Get-Date -Format 'HH:mm:ss'))
  $logBefore = Get-LogSnapshot $AppHome
  $mainBefore = Get-LogSize $MainLog
  $crashBefore = Get-CrashCount
  $launch = Start-Detached $exe $RunDir $LaunchVia
  if (-not $launch) { Write-Host '  启动失败（未能创建进程）' -ForegroundColor Red; $allResults += 'FAIL(no-process)'; continue }
  Write-Host ('  launched pid=' + $launch.Pid + ' via ' + $launch.Method)

  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $status = @{ Alive = $false; Port = $false; Log = $false; Window = $false; Shot = $null; Hwnd = [IntPtr]::Zero }
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 600
    $proc = Get-Process -Id $launch.Pid -ErrorAction SilentlyContinue
    if (-not $proc) {
      $status.Alive = $false
      break
    }
    $status.Alive = ($proc.Threads.Count -gt 0)
    $conn = @(Get-NetTCPConnection -LocalPort $SingletonPort -State Listen -ErrorAction SilentlyContinue | Where-Object { $_.OwningProcess -eq $launch.Pid })
    $status.Port = ($conn.Count -gt 0)
    $delta = Get-LogDelta $logBefore $AppHome
    $mainDelta = Get-MainLogDelta $mainBefore $MainLog
    $status.Log = $mainDelta.Changed
    $w = 0; $h = 0; $vis = $false; $title = ''
    $hwnd = [CwWin]::FindWindow([uint32]$launch.Pid, [ref]$w, [ref]$h, [ref]$vis, [ref]$title)
    $status.Hwnd = $hwnd
    $status.Window = ($hwnd -ne [IntPtr]::Zero)
    if ($status.Alive -and $status.Port -and $status.Log -and $status.Window) { break }
  }

  $proc = Get-Process -Id $launch.Pid -ErrorAction SilentlyContinue
  $w = 0; $h = 0; $vis = $false; $title = ''
  if ($status.Hwnd -ne [IntPtr]::Zero) {
    $null = [CwWin]::FindWindow([uint32]$launch.Pid, [ref]$w, [ref]$h, [ref]$vis, [ref]$title)
  }
  if ($status.Hwnd -ne [IntPtr]::Zero) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
    $shotPath = Join-Path $LogDir ('app_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '_run' + $run + '.png')
    $status.Shot = Save-WindowShot $status.Hwnd $w $h $shotPath
  }
  Write-Host '  顶层窗口枚举:'
  [CwWin]::DumpWindows([uint32]$launch.Pid)
  $crashAfter = Get-CrashCount
  if ($crashAfter -gt $crashBefore) { Write-Host ('  !! 检测到新的 WER 1000 崩溃事件 +' + ($crashAfter - $crashBefore)) -ForegroundColor Red }

  $delta = Get-LogDelta $logBefore $AppHome
  Write-Host ('  [1] PID alive      : ' + $status.Alive + '  pid=' + $launch.Pid + ' threads=' + $(if ($proc) { $proc.Threads.Count } else { 'exited' }))
  Write-Host ('  [2] 33337 LISTEN   : ' + $status.Port + '  owner=' + $launch.Pid)
  Write-Host ('  [3] log new lines  : ' + $status.Log + '  ' + $delta.Detail)
  Write-Host ('  [4] window visible : ' + $status.Window + '  hwnd=' + $status.Hwnd + ' title=' + $(if ($title) { "'" + $title + "'" } else { "''" }))
  if ($status.Shot) {
    Write-Host ('      PrintWindow    : ok=' + $status.Shot.Ok + ' colors=' + $status.Shot.Colors + ' dominant=' + $status.Shot.DominantRatio + ' file=' + $status.Shot.File)
  }

  $pass = ($status.Alive -and $status.Port -and $status.Log -and $status.Window -and $status.Shot -and $status.Shot.Ok)
  if ($pass) { Write-Host '  Run 结果: PASS' -ForegroundColor Green; $allResults += 'PASS' }
  else { Write-Host '  Run 结果: FAIL' -ForegroundColor Red; $allResults += 'FAIL' }

  # 本轮实例立即结束（Post 会清理全部自家进程，但多次运行需要先腾出单实例端口）
  if (-not $KeepInstance) { Stop-OurTree $launch.Pid $RunDir; Start-Sleep -Milliseconds 900 }
}

# ---------------------------------------------------------------- Post
Write-Section 'Post'
$post = & (Join-Path $PSScriptRoot 'env_guard.ps1') -Phase Post
$postExit = $LASTEXITCODE

Write-Section ('汇总: ' + ($allResults -join ' '))
if (($allResults | Where-Object { $_ -ne 'PASS' }).Count -eq 0 -and $postExit -eq 0) {
  Write-Host 'OVERALL: PASS' -ForegroundColor Green
  exit 0
} else {
  Write-Host 'OVERALL: FAIL' -ForegroundColor Red
  exit 1
}

