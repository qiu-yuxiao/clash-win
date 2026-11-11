# win_metrics.ps1 — 打印指定进程主窗口的 DPI / 外框 / 客户区 / 换算逻辑尺寸
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File scripts\win_metrics.ps1 -ProcName clashwin
param([string[]]$ProcName = @("clashwin", "clash-mini"))

Add-Type @'
using System;using System.Runtime.InteropServices;
public class WM {
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int i);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
'@

foreach ($n in $ProcName) {
  $p = Get-Process $n -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { Write-Output "$n : NOT_RUNNING"; continue }
  $h = $p.MainWindowHandle
  if ($h -eq [IntPtr]::Zero) { Write-Output "$n : NO_WINDOW"; continue }
  $r = New-Object WM+RECT; [WM]::GetWindowRect($h, [ref]$r) | Out-Null
  $c = New-Object WM+RECT; [WM]::GetClientRect($h, [ref]$c) | Out-Null
  $dpi = [WM]::GetDpiForWindow($h)
  $sc = $dpi / 96.0
  $ww = $r.R - $r.L; $wh = $r.B - $r.T
  $cw = $c.R - $c.L; $ch = $c.B - $c.T
  $style = [WM]::GetWindowLongW($h, -16)
  Write-Output ("{0} hwnd={1} dpi={2} scale={3} style=0x{4:X8}" -f $n, $h.ToInt64(), $dpi, $sc, $style)
  Write-Output ("   window={0}x{1}  client={2}x{3}  logical(client)={4}x{5}" -f `
      $ww, $wh, $cw, $ch, [math]::Round($cw / $sc, 3), [math]::Round($ch / $sc, 3))
}
