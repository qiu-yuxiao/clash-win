# smoke_shot.ps1 — 仅启动 + 截图（不做网络体检，用于快速目视核对）
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File scripts\smoke_shot.ps1 [-Out <目录>\shot.png]
# 默认输出 %TEMP%\cwrun\shots\shot.png（目录不存在会自动创建）
param(
  [string]$Exe = "",
  [string]$Out = "",
  [int]$WaitSec = 9,
  [string]$ProcName = "clashwin",   # 像素对照时传 clash-mini 抓上游窗口（只读，不干扰）
  [switch]$NoLaunch
)
$ErrorActionPreference = 'Continue'

$Repo = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $Repo 'build\bin\Release\clashwin.exe' }
if (-not $Out) { $Out = Join-Path $env:TEMP 'cwrun\shots\shot.png' }
$OutDir = Split-Path -Parent $Out
if ($OutDir) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;using System.Collections.Generic;
public class CwShot {
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
 public static List<IntPtr> Wins(uint target) {
   var o = new List<IntPtr>();
   EnumWindows((h,l)=>{ uint pid; GetWindowThreadProcessId(h, out pid);
     if (pid==target && IsWindowVisible(h)) o.Add(h); return true; }, IntPtr.Zero);
   return o; }
 public static string Info(IntPtr h) {
   var t=new StringBuilder(256); var c=new StringBuilder(256);
   GetWindowTextW(h,t,256); GetClassNameW(h,c,256);
   RECT r; GetWindowRect(h,out r);
   return "hwnd="+h.ToInt64()+" cls="+c+" rect="+r.L+","+r.T+","+(r.R-r.L)+"x"+(r.B-r.T)+" title="+t; }
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
     return ok;
   } }
}
'@ -ReferencedAssemblies System.Drawing
[CwShot]::SetProcessDpiAwarenessContext([IntPtr]-4) | Out-Null

if (-not $NoLaunch) {
  $cwd = Split-Path -Parent $Exe
  $wc = [wmiclass]'Win32_Process'
  $r = $wc.Create($Exe, $cwd, $null)
  Write-Output ("LAUNCH rv=" + $r.ReturnValue + " pid=" + $r.ProcessId)
  Start-Sleep -Seconds $WaitSec
}

$p = Get-Process $ProcName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Output "NOT_RUNNING"; exit 1 }
Write-Output ("PID=" + $p.Id)
$ws = [CwShot]::Wins([uint32]$p.Id)
if ($ws.Count -eq 0) { Write-Output "NO_VISIBLE_WINDOW"; exit 1 }
$i = 0
foreach ($h in $ws) { Write-Output ([CwShot]::Info($h)) }
$target = $ws[0]
for ($k=1; $k -lt $ws.Count; $k++) {
  $s = [CwShot]::Info($ws[$k])
  if ($s -match 'title=(Clash|Mini)') { $target = $ws[$k] }
}
$ok = [CwShot]::Shot($target, $Out)
Write-Output ("SHOT ok=" + $ok + " -> " + $Out)
