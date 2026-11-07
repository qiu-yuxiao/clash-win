# 6 套皮肤逐套截图（开发期对照用，不属于交付物）
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File scripts\shot_skins.ps1
# 说明：通过 <app_home>\clashwin-skin.json 指定 skin + 逐皮肤因子槽位
#       （factors: { "<skin>": { "v1": …, "v2": … } }），逐套启动截图后退出。
#       每套结束都会把系统代理关掉（避免 10811 无监听时断网）。
#
# ★ 2026-11-07：因子持久化已从「全局 depth/vibrancy」改为**逐皮肤槽位**
#   （← Mini use-skin-controls.ts:54-67 localStorage['clash-mini-{skin}-val1/val2']），
#   顶层 depth/vibrancy 仅作 retro-3d 的 legacy 镜像保留。
$ErrorActionPreference = 'Continue'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exe  = Join-Path $root 'build\bin\Release\clashwin.exe'
$homeDir = Join-Path $root 'build\bin\Release\.config\io.github.clash-win.clash-win'
$outDir  = Join-Path $root 'ui_diag'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

function Stop-Cw {
  Get-Process -Name clashwin -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
  Get-Process -Name mini-mihomo -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like '*ClashWin*' -or $_.Path -like '*cw-*' } |
    ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
  try {
    Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -Name ProxyEnable -Value 0 -Type DWord
  } catch {}
  Start-Sleep -Seconds 2
}

function Shot([string]$skin, [string]$mode, [double]$v1, [double]$v2, [string]$tag) {
  # 配置目录可能被 reset_test_env.ps1 移走（干净首装态），必须先确保存在
  if (-not (Test-Path $homeDir)) { New-Item -ItemType Directory -Path $homeDir -Force | Out-Null }
  # 逐皮肤因子槽位（v1 = val1/Depth/Radius/…，v2 = val2/Vibrancy/Accent/…）
  $factors = [ordered]@{}
  $factors[$skin] = [ordered]@{ v1 = $v1; v2 = $v2 }
  $json = [ordered]@{
    skin       = $skin
    theme_mode = $mode
    depth      = $v1      # legacy 镜像（仅 retro-3d 生效）
    vibrancy   = $v2
    factors    = $factors
  } | ConvertTo-Json -Depth 5
  Set-Content -LiteralPath (Join-Path $homeDir 'clashwin-skin.json') -Value $json -Encoding utf8
  # 跳过首启引导（key 见 first_run_guide_dialog.h:34），否则弹窗会遮住节点区
  Set-Content -LiteralPath (Join-Path $homeDir 'clashwin-guide.json') `
      -Value '{"clash-mini-first-run-completed": true}' -Encoding utf8
  $out = Join-Path $outDir ("skin_{0}.png" -f $tag)
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'smoke_shot.ps1') `
      -Exe $exe -Out $out -WaitSec 8 | Out-Null
  Write-Output ("{0,-14} {1,-6} v1={2} v2={3} -> {4}" -f $skin, $mode, $v1, $v2, (Split-Path $out -Leaf))
  Stop-Cw
}

Stop-Cw

# ---- 逐皮肤缺省值（← use-skin-controls.ts:13-24 readFactor）----
#   retro-3d 缺省 0.3/1.0；其余 5 套缺省 1.0/1.0
$defaults = @{
  'retro-3d'      = @(0.3, 1.0)
  'original'      = @(1.0, 1.0)
  'modern-flat'   = @(1.0, 1.0)
  'frosted-glass' = @(1.0, 1.0)
  'cyberpunk'     = @(1.0, 1.0)
  'monochrome'    = @(1.0, 1.0)
}

# 1) 缺省参数下逐套对比（亮暗各一轮）
foreach ($m in @('light','dark')) {
  foreach ($s in @('retro-3d','original','modern-flat','frosted-glass','cyberpunk','monochrome')) {
    $d = $defaults[$s]
    Shot $s $m $d[0] $d[1] ("{0}_{1}" -f $s, $m)
  }
}

# 2) ★ 滑块语义验证：每套皮肤的 val1 / val2 取极值
#    （应能看到各皮肤**不同的**参数被改变：圆角 / 主色相 / 阴影 / 不透明度 /
#      辉光 / 边框宽 / 脉冲速度 —— 见 docs/evidence/2026-11-07-skin-slider-semantics.md）
$extremes = @(
  @('retro-3d',      'light', 0.0, 1.0, 'retro_v1min'),
  @('retro-3d',      'light', 1.0, 1.0, 'retro_v1max'),
  @('retro-3d',      'light', 0.3, 0.0, 'retro_v2min'),
  @('retro-3d',      'light', 0.3, 5.0, 'retro_v2max'),
  @('original',      'light', 1.0, 1.0, 'original_v1min'),
  @('original',      'light', 3.0, 1.0, 'original_v1max'),
  @('original',      'light', 1.0, 1.0, 'original_v2min'),
  @('original',      'light', 1.0, 3.0, 'original_v2max'),
  @('modern-flat',   'light', 1.0, 1.0, 'modern_v1min'),
  @('modern-flat',   'light', 3.0, 1.0, 'modern_v1max'),
  @('modern-flat',   'light', 1.0, 1.0, 'modern_v2min'),
  @('modern-flat',   'light', 1.0, 5.0, 'modern_v2max'),
  @('frosted-glass', 'light', 1.0, 1.0, 'frosted_v1min'),
  @('frosted-glass', 'light', 5.0, 1.0, 'frosted_v1max'),
  @('cyberpunk',     'dark',  1.0, 1.0, 'cyber_v1min'),
  @('cyberpunk',     'dark',  5.0, 1.0, 'cyber_v1max'),
  @('cyberpunk',     'dark',  1.0, 1.0, 'cyber_v2min'),
  @('cyberpunk',     'dark',  1.0, 5.0, 'cyber_v2max'),
  @('monochrome',    'light', 1.0, 1.0, 'mono_v1min'),
  @('monochrome',    'light', 3.0, 1.0, 'mono_v1max'),
  @('monochrome',    'light', 1.0, 1.0, 'mono_v2min'),
  @('monochrome',    'light', 1.0, 5.0, 'mono_v2max')
)
foreach ($e in $extremes) {
  Shot $e[0] $e[1] $e[2] $e[3] $e[4]
}

Write-Output 'DONE'
