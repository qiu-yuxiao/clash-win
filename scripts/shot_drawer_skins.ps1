# 6 套皮肤「设置抽屉」逐套截图（开发期对照用，不属于交付物）
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File scripts\shot_drawer_skins.ps1 -Mode light
#
# 两个关键点（踩过的坑）：
#  1) 点击用 SendMessage 投递 WM_LBUTTONDOWN/UP 到窗口客户区，而不是
#     SetCursorPos + mouse_event —— 后者需要窗口真正在前台，而 SetForegroundWindow
#     在本机会被系统拒绝（点击落到别的窗口，抽屉根本没开）。SendMessage 直接
#     命中 Win 侧的 route_mouse_event(&root_, e) 路由，100% 可靠。
#     坐标 = 客户区物理像素（main_window.cpp:1629 会再除以 dpi_scale）。
#  2) 主题模式以 verge.yaml 为准（main_window.cpp:1089-1094 明确「启动期以
#     verge 为准回填」），clashwin-skin.json 只是缓存 → 必须同时写 verge.yaml，
#     否则永远是 template 缺省 dark。
param(
  [string]$Mode = 'light',
  [switch]$All
)
$ErrorActionPreference = 'Continue'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exe  = Join-Path $root 'build\bin\Release\clashwin.exe'
$homeDir = Join-Path $root 'build\bin\Release\.config\io.github.clash-win.clash-win'
$outDir  = Join-Path $root 'ui_diag'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;using System.Collections.Generic;
public class CwDrawer {
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
 public static List<IntPtr> Wins(uint target) {
   var o = new List<IntPtr>();
   EnumWindows((h,l)=>{ uint pid; GetWindowThreadProcessId(h, out pid);
     if (pid==target && IsWindowVisible(h)) o.Add(h); return true; }, IntPtr.Zero);
   return o; }
 public static bool Shot(IntPtr h, string path) {
   RECT r; GetWindowRect(h,out r);
   int w=r.R-r.L, hh=r.B-r.T;
   if (w<=0||hh<=0) return false;
   using (var bmp = new System.Drawing.Bitmap(w,hh))
   using (var g = System.Drawing.Graphics.FromImage(bmp)) {
     IntPtr hdc = g.GetHdc();
     bool ok = PrintWindow(h, hdc, 2);
     g.ReleaseHdc(hdc);
     bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
     return ok; } }
 // 客户区坐标（物理像素）点击：WM_LBUTTONDOWN -> WM_LBUTTONUP
 public static void Click(IntPtr h, int x, int y) {
   long lp = (long)((y << 16) | (x & 0xFFFF));
   SendMessage(h, 0x0201, (IntPtr)1, (IntPtr)lp);  // WM_LBUTTONDOWN, MK_LBUTTON
   System.Threading.Thread.Sleep(60);
   SendMessage(h, 0x0202, (IntPtr)0, (IntPtr)lp);  // WM_LBUTTONUP
 }
}
'@ -ReferencedAssemblies System.Drawing
[CwDrawer]::SetProcessDpiAwarenessContext([IntPtr]-4) | Out-Null

function Stop-Cw {
  Get-Process -Name clashwin -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
  Get-Process -Name win-mihomo -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like '*ClashWin*' -or $_.Path -like '*cw-*' } |
    ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
  try { Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -Name ProxyEnable -Value 0 -Type DWord } catch {}
  Start-Sleep -Seconds 2
}

function ShotDrawer([string]$skin, [string]$mode, [string]$tag) {
  if (-not (Test-Path $homeDir)) { New-Item -ItemType Directory -Path $homeDir -Force | Out-Null }
  # --- 皮肤缓存（因子槽位）---
  $factors = [ordered]@{}
  $factors[$skin] = [ordered]@{ v1 = 1.0; v2 = 1.0 }
  $json = [ordered]@{
    skin = $skin; theme_mode = $mode; depth = 1.0; vibrancy = 1.0; factors = $factors
  } | ConvertTo-Json -Depth 5
  Set-Content -LiteralPath (Join-Path $homeDir 'clashwin-skin.json') -Value $json -Encoding utf8
  Set-Content -LiteralPath (Join-Path $homeDir 'clashwin-guide.json') `
      -Value '{"clash-mini-first-run-completed": true}' -Encoding utf8
  # --- 单一事实源：verge.yaml（main_window.cpp:1089-1094 启动期以 verge 回填 theme_mode）---
  Set-Content -LiteralPath (Join-Path $homeDir 'verge.yaml') `
      -Value ("config_version: 3`ntheme_mode: $mode`n") -Encoding utf8

  $cwd = Split-Path -Parent $exe
  $wc = [wmiclass]'Win32_Process'
  $r = $wc.Create($exe, $cwd, $null)
  Start-Sleep -Seconds 6
  $p = Get-Process clashwin -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { Write-Output "NOT_RUNNING"; return }
  $ws = [CwDrawer]::Wins([uint32]$p.Id)
  if ($ws.Count -eq 0) { Write-Output "NO_VISIBLE_WINDOW"; return }
  $target = $ws[0]
  for ($k=1; $k -lt $ws.Count; $k++) {
    $rc = New-Object CwDrawer+RECT
    [CwDrawer]::GetWindowRect($ws[$k], [ref]$rc) | Out-Null
    if (($rc.R-$rc.L) -gt 200) { $target = $ws[$k] }
  }
  $rc = New-Object CwDrawer+RECT
  [CwDrawer]::GetWindowRect($target, [ref]$rc) | Out-Null
  $w = $rc.R - $rc.L
  # 齿轮按钮（quick_bar.cpp:318-321）：逻辑 right-28..right，btn_h=28；
  #   客户区物理坐标 = 逻辑 × 1.5（dpi=144）。窗口物理 407 → 齿轮中心 x≈374, y≈67。
  [CwDrawer]::Click($target, $w - 33, 67)
  Start-Sleep -Seconds 3
  $out = Join-Path $outDir ("drawer_{0}.png" -f $tag)
  $ok = [CwDrawer]::Shot($target, $out)
  Write-Output ("{0,-14} {1,-6} -> {2} ok={3} ({4}x{5})" -f $skin, $mode, (Split-Path $out -Leaf), $ok, $w, ($rc.B-$rc.T))
  Stop-Cw
}

Stop-Cw
$skins = @('retro-3d','original','modern-flat','frosted-glass','cyberpunk','monochrome')
if ($All) {
  foreach ($s in $skins) { ShotDrawer $s 'light' ("{0}_light" -f $s) }
  foreach ($s in $skins) { ShotDrawer $s 'dark'  ("{0}_dark"  -f $s) }
} else {
  foreach ($s in $skins) { ShotDrawer $s $Mode ("{0}_{1}" -f $s, $Mode) }
}
Write-Output 'DONE'
