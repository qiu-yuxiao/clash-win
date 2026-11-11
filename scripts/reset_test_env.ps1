# ============================================================================
# reset_test_env.ps1 —— Clash Win 测试环境重置（用户测试前必跑）
#
# 用途：把本机的 Clash Win 运行态恢复到「干净首装」等价状态，避免开发期残留
#       污染用户测试。会做四件事：
#         1) 停止所有 clashwin.exe / 本应用内核进程（win-mihomo 仅限本产品目录）
#         2) 关闭系统代理并广播刷新（防止退出异常时遗留代理黑洞）
#         3) 备份并移走配置目录 %APPDATA%\io.github.clash-win.clash-win
#         4) 校验：配置目录已不存在、系统代理已关、进程已清空
#
# 不做的事（硬约束）：
#   - 绝不触碰 Clash Mini（io.github.clash-mini.*）的配置与进程
#   - 绝不触碰共享服务 clash_verge_service
#   - 绝不删除备份（每次带时间戳，人工确认后再清理）
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\reset_test_env.ps1
#   加 -KeepBackupDays 7 可自动清理 7 天前的备份（默认不清理）
# ============================================================================
param(
  [int]$KeepBackupDays = 0
)

$ErrorActionPreference = 'Stop'
$configDir = Join-Path $env:APPDATA 'io.github.clash-win.clash-win'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupDir = "$configDir.bak-$stamp"

Write-Host '=== Clash Win 测试环境重置 ===' -ForegroundColor Cyan

# ---------- 1) 停止进程 ----------
$procs = Get-Process clashwin -ErrorAction SilentlyContinue
if ($procs) {
  foreach ($p in $procs) {
    Write-Host "停止 clashwin.exe pid=$($p.Id)"
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  }
  Start-Sleep -Milliseconds 800
} else {
  Write-Host 'clashwin.exe 未运行'
}

# 本产品目录下的内核进程（只认安装目录/构建目录，避免误杀 Clash Mini 的内核）
$instDirs = @('C:\Program Files\ClashWin',
              (Join-Path $PSScriptRoot '..\build\bin\Release'))
$resolved = @()
foreach ($d in $instDirs) {
  try { $resolved += (Resolve-Path $d -ErrorAction Stop).Path } catch {}
}
foreach ($name in @('win-mihomo')) {
  Get-Process $name -ErrorAction SilentlyContinue | ForEach-Object {
    $path = $null
    try { $path = $_.Path } catch {}
    if (-not $path) { return }
    foreach ($d in $resolved) {
      if ($path.StartsWith($d, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "停止 $name pid=$($_.Id) path=$path"
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        break
      }
    }
  }
}

# ---------- 2) 关闭系统代理 ----------
$inetKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
$proxyEnable = (Get-ItemProperty -Path $inetKey -Name ProxyEnable -ErrorAction SilentlyContinue).ProxyEnable
if ($proxyEnable -ne 0) {
  Set-ItemProperty -Path $inetKey -Name ProxyEnable -Value 0 -Type DWord
  Write-Host "系统代理已关闭（原 ProxyEnable=$proxyEnable）"
} else {
  Write-Host '系统代理已是关闭状态'
}
# 广播刷新（浏览器立即感知）
Add-Type -Namespace CWReset -Name WinInet -MemberDefinition @'
[DllImport("wininet.dll", SetLastError = true)]
public static extern bool InternetSetOption(IntPtr h, int opt, IntPtr buf, int len);
'@ -ErrorAction SilentlyContinue
[void][CWReset.WinInet]::InternetSetOption([IntPtr]::Zero, 39, [IntPtr]::Zero, 0)  # SETTINGS_CHANGED
[void][CWReset.WinInet]::InternetSetOption([IntPtr]::Zero, 37, [IntPtr]::Zero, 0)  # REFRESH

# ---------- 3) 备份并移走配置目录 ----------
if (Test-Path $configDir) {
  Move-Item -Path $configDir -Destination $backupDir -Force
  Write-Host "配置目录已备份移走：$backupDir" -ForegroundColor Yellow
} else {
  Write-Host '配置目录不存在（已是干净状态）'
}

# ---------- 3b) 便携模式（.config\PORTABLE 标记）同样要清 ----------
# 【踩坑】开发期跑 build\bin\Release\clashwin.exe 时配置落在
#   <exe目录>\.config\io.github.clash-win.clash-win，里面残留过真实订阅
#   → 首装引导不弹、节点列表非空、「什么都没做就有节点代理」。
#   只清 %APPDATA% 不看便携目录 = 假干净。
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$portableRoots = @(
  (Join-Path $repoRoot 'build\bin\Release'),
  (Join-Path $repoRoot 'dist\ClashWin'),
  (Join-Path $repoRoot 'ClashWin-Portable'),
  (Join-Path $repoRoot 'installer\stage')
)
foreach ($root in $portableRoots) {
  if (-not (Test-Path $root)) { continue }
  $marker = Join-Path $root '.config\PORTABLE'
  $pdir = Join-Path $root '.config\io.github.clash-win.clash-win'
  if ((Test-Path $pdir) -and (Test-Path $marker)) {
    $pbackup = "$pdir.bak-$stamp"
    try {
      Move-Item -Path $pdir -Destination $pbackup -Force -ErrorAction Stop
      Write-Host "便携配置已备份移走：$pbackup" -ForegroundColor Yellow
    } catch {
      # 目录被内核占用（TUN 模式下服务以 SYSTEM 起的 win-mihomo 锁着 cache.db，
      # 普通权限杀不掉）→ 退化为「只搬状态文件、留下资产/被占用文件」，
      # 效果等价干净首装：profiles/verge/window_state/skin 全清。
      Write-Host "整目录被占用，改为逐项搬移状态文件 → $pbackup" -ForegroundColor Yellow
      New-Item -ItemType Directory -Path $pbackup -Force | Out-Null
      $stateNames = @(
        'profiles', 'profiles.yaml', 'verge.yaml', 'clash.yaml', 'clash-win.yaml',
        'clash-win-check.yaml', 'config.yaml', 'window_state.json',
        'clashwin-skin.json', 'last_non_large_size.json', 'logs', 'diagnostics',
        'proxy_head_state.json', 'unlock_cache.json', 'runtime.yaml'
      )
      foreach ($n in $stateNames) {
        $src = Join-Path $pdir $n
        if (Test-Path $src) {
          try {
            Move-Item -Path $src -Destination (Join-Path $pbackup $n) -Force -ErrorAction Stop
            Write-Host "  搬移 $n"
          } catch {
            Write-Host "  跳过（占用）$n" -ForegroundColor DarkYellow
          }
        }
      }
    }
  }
}

# ---------- 4) 校验 ----------
$ok = $true
if (Test-Path $configDir) { Write-Host '校验失败：配置目录仍存在' -ForegroundColor Red; $ok = $false }
if (Get-Process clashwin -ErrorAction SilentlyContinue) { Write-Host '校验失败：clashwin 仍在运行' -ForegroundColor Red; $ok = $false }
$pe = (Get-ItemProperty -Path $inetKey -Name ProxyEnable -ErrorAction SilentlyContinue).ProxyEnable
if ($pe -ne 0) { Write-Host "校验失败：ProxyEnable=$pe" -ForegroundColor Red; $ok = $false }

if ($KeepBackupDays -gt 0) {
  Get-ChildItem "$configDir.bak-*" -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-$KeepBackupDays) } |
    ForEach-Object { Write-Host "清理旧备份：$($_.FullName)"; Remove-Item $_.FullName -Recurse -Force }
}

if ($ok) {
  Write-Host ''
  Write-Host '环境已重置：下次启动即为干净首装状态（引导弹窗会出现、节点列表为空、系统代理关闭）' -ForegroundColor Green
  Write-Host "备份保留在：$backupDir" -ForegroundColor DarkGray
} else {
  exit 1
}
