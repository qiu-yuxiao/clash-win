# 端到端验证：大尺寸模式 → 打开设置抽屉 → 皮肤条可见 → 点击皮肤格切换皮肤
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File scripts\probe_big_skin.ps1
param(
  [int]$ClickX = -1,      # >=0 时点击皮肤格（客户区物理坐标）
  [int]$ClickY = -1,
  [string]$Tag = 'big'
)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exe  = Join-Path $root 'build\bin\Release\clashwin.exe'
$homeDir = Join-Path $root 'build\bin\Release\.config\io.github.clash-win.clash-win'
$outDir  = Join-Path $root 'ui_diag'
$skinJson = Join-Path $homeDir 'clashwin-skin.json'

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;using System.Collections.Generic;
public class CwBig {
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
     IntPtr hdc = g.GetHdc(); bool ok = PrintWindow(h, hdc, 2); g.ReleaseHdc(hdc);
     bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png); return ok; } }
 public static void Click(IntPtr h, int x, int y) {
   long lp = (long)((y << 16) | (x & 0xFFFF));
   SendMessage(h, 0x0201, (IntPtr)1, (IntPtr)lp);
   System.Threading.Thread.Sleep(90);
   SendMessage(h, 0x0202, (IntPtr)0, (IntPtr)lp); }
}
'@ -ReferencedAssemblies System.Drawing
[CwBig]::SetProcessDpiAwarenessContext([IntPtr]-4) | Out-Null

Get-Process -Name clashwin -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# 固定皮肤 = retro-3d，便于观察切换
$factors = [ordered]@{ 'retro-3d' = [ordered]@{ v1 = 1.0; v2 = 1.0 } }
$json = [ordered]@{ skin='retro-3d'; theme_mode='light'; depth=1.0; vibrancy=1.0; factors=$factors } | ConvertTo-Json -Depth 5
Set-Content -LiteralPath $skinJson -Value $json -Encoding utf8
Set-Content -LiteralPath (Join-Path $homeDir 'clashwin-guide.json') -Value '{"clash-mini-first-run-completed": true}' -Encoding utf8
Set-Content -LiteralPath (Join-Path $homeDir 'verge.yaml') -Value "config_version: 3`ntheme_mode: light`n" -Encoding utf8

$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -PassThru
Start-Sleep -Seconds 7
$ws = [CwBig]::Wins([uint32]$p.Id)
$target = $ws[0]
foreach ($w in $ws) {
  $rc = New-Object CwBig+RECT; [CwBig]::GetWindowRect($w, [ref]$rc) | Out-Null
  if (($rc.R-$rc.L) -gt 200) { $target = $w }
}
function RectOf($h) { $rc = New-Object CwBig+RECT; [CwBig]::GetWindowRect($h, [ref]$rc) | Out-Null; return $rc }

$rc = RectOf $target
Write-Output ("small window = {0}x{1}" -f ($rc.R-$rc.L), ($rc.B-$rc.T))

# 1) 点标题栏「最大化」→ 大尺寸 640x860 逻辑（physical 960x1290）
[CwBig]::Click($target, 328, 22)
Start-Sleep -Seconds 2
$rc = RectOf $target
Write-Output ("big window   = {0}x{1}" -f ($rc.R-$rc.L), ($rc.B-$rc.T))

# 2) 点齿轮 → 打开设置抽屉（大尺寸下齿轮仍在右上角快捷条）
[CwBig]::Click($target, ($rc.R-$rc.L) - 33, 67)
Start-Sleep -Seconds 2
$out = Join-Path $outDir ("bigskin_{0}.png" -f $Tag)
[CwBig]::Shot($target, $out) | Out-Null
Write-Output ("shot -> {0}" -f (Split-Path $out -Leaf))

# 3) 可选：点击皮肤格
if ($ClickX -ge 0) {
  [CwBig]::Click($target, $ClickX, $ClickY)
  Start-Sleep -Seconds 2
  [CwBig]::Shot($target, (Join-Path $outDir ("bigskin_{0}_after.png" -f $Tag))) | Out-Null
  Write-Output "---- clashwin-skin.json ----"
  Get-Content $skinJson
}
Get-Process -Name clashwin -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
