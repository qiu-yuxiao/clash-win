# 滑块有效性端到端验证（开发期对照用，不属于交付物）
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File scripts\shot_factor_effect.ps1
#
# 目的：验证「每套皮肤的两个滑块拖动后画面真的变化」——
#       val1/val2 的语义逐皮肤不同（Depth/Radius/Roundness/Opacity/Glow/
#       Border × Vibrancy/Accent/Shadow/Blur/Speed），
#       任何一套皮肤若"拖了没反应"就是移植缺陷。
#
# 做法：对每套皮肤，分别以 (v1,v2) = (min,def) / (max,def) / (def,min) / (def,max)
#       四组取值启动截图，再用像素 diff 判定"是否产生变化"。
#       输出 ui_diag/fx_<skin>_<tag>.png
$ErrorActionPreference = 'Continue'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exe  = Join-Path $root 'build\bin\Release\clashwin.exe'
$homeDir = Join-Path $root 'build\bin\Release\.config\io.github.clash-win.clash-win'
$outDir  = Join-Path $root 'ui_diag'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

function Stop-Cw {
  Get-Process -Name clashwin -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
  Get-Process -Name win-mihomo -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like '*ClashWin*' } |
    ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
  try {
    Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -Name ProxyEnable -Value 0 -Type DWord
  } catch {}
  Start-Sleep -Seconds 2
}

function Shot([string]$skin, [double]$v1, [double]$v2, [string]$tag) {
  if (-not (Test-Path $homeDir)) { New-Item -ItemType Directory -Path $homeDir -Force | Out-Null }
  $factors = [ordered]@{}
  $factors[$skin] = [ordered]@{ v1 = $v1; v2 = $v2 }
  $json = [ordered]@{
    skin       = $skin
    theme_mode = 'light'
    depth      = $v1
    vibrancy   = $v2
    factors    = $factors
  } | ConvertTo-Json -Depth 5
  Set-Content -LiteralPath (Join-Path $homeDir 'clashwin-skin.json') -Value $json -Encoding utf8
  Set-Content -LiteralPath (Join-Path $homeDir 'clashwin-guide.json') `
      -Value '{"clash-mini-first-run-completed": true}' -Encoding utf8
  $out = Join-Path $outDir ("fx_{0}_{1}.png" -f $skin, $tag)
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'smoke_shot.ps1') `
      -Exe $exe -Out $out -WaitSec 8 | Out-Null
  Write-Output ("{0,-14} {1,-10} v1={2,-4} v2={3,-4} -> {4}" -f $skin, $tag, $v1, $v2, (Split-Path $out -Leaf))
  Stop-Cw
}

Stop-Cw

# 逐皮肤：val1/val2 的取值范围（← Mini use-skin-controls.ts 的 slider min/max）
#   retro-3d : val1 0..1,  val2 0..5
#   original  : val1 0..3,  val2 0..3
#   modern-flat: val1 0..3, val2 0..5
#   frosted   : val1 0..5,  val2 0..5
#   cyberpunk : val1 0..5,  val2 0..5
#   monochrome: val1 0..3,  val2 0..5
# 缺省：retro-3d 0.3/1.0；其余 1.0/1.0
$spec = @(
  @{ skin='retro-3d';      def1=0.3; def2=1.0; max1=1.0; max2=5.0 },
  @{ skin='original';       def1=1.0; def2=1.0; max1=3.0; max2=3.0 },
  @{ skin='modern-flat';    def1=1.0; def2=1.0; max1=3.0; max2=5.0 },
  @{ skin='frosted-glass';  def1=1.0; def2=1.0; max1=5.0; max2=5.0 },
  @{ skin='cyberpunk';      def1=1.0; def2=1.0; max1=5.0; max2=5.0 },
  @{ skin='monochrome';     def1=1.0; def2=1.0; max1=3.0; max2=5.0 }
)

foreach ($sp in $spec) {
  $s = $sp.skin
  Shot $s $sp.def1 $sp.def2 'def'
  Shot $s 0.0        $sp.def2 'v1min'
  Shot $s $sp.max1   $sp.def2 'v1max'
  Shot $s $sp.def1   0.0     'v2min'
  Shot $s $sp.def1   $sp.max2 'v2max'
}
Write-Output "done"
