# ui_src_gate.ps1 — UI 层「无源码出处常量」门禁
# 用法：powershell -ExecutionPolicy Bypass -File scripts\ui_src_gate.ps1 [src根目录]
#   构建前运行；有违规输出清单并 exit 1（可接 CI/预构建步骤）。
# 规则：src/ui/** 代码行含 数值/颜色字面量 且（本行 / 上 4-8 行 / 所属块起始
#   `src:`（源码出处，规范）/ `←`（历史移植标记）/ `豁免:`（仅限 Mini 无对应物））
#   没有来源即视为违规。
# 硬性禁止（2026-11-05 起）：不得以「实测 / 截图 / 目测 / 近似 / 取色」作为数值依据，
#   注释中出现即违规（exit 1）。
# 豁免写法：`// 豁免: Mini 无对应物 — <理由>（出处: <Mini 路径或“无”>）`；
#   仅写「豁免」或「有源但取不到」均视为违规。
param([string]$Root = "$PSScriptRoot\..\src\ui")

$ErrorActionPreference = 'Stop'
$srcRoot = (Resolve-Path $Root).Path
$forbidden = '实测|截图|目测|近似|取色'
$exemptPat = '豁免[^\r\n]*Mini\s*无对应物'
$srcPat    = 'src:|←|//\s*:\d'

# 字面量检测模式（只盯 UI 常量语义，避免循环变量等噪音）
$patterns = @(
  'Color4f::hex\(\s*0x[0-9A-Fa-f]{6,8}'          # 颜色
  'Color4f\(\s*[\d.]+\s*,\s*[\d.]+'              # 颜色分量
  'style\.(size|weight|line_height|letter_spacing)\s*=\s*[\d.]+f?\b'
  'set_padding\([^)]*\d'
  'constexpr\s+(float|int|double)\s+k\w+\s*=\s*[\d.]+f?\b'
  '\b(radius|gap|height|width|margin|padding|alpha|line_width|duration|offset|thickness)\s*=\s*\d+\.\d+f?\b'
  '\.(w|h|x|y)\s*=\s*\d+\.\d+f?\b'
  'fill_round_rect\w*\([^)]*\b\d+\.\d'
  'draw_line\([^)]*\b\d+\.\d'
)
$join = ($patterns | ForEach-Object { "(?:$_)" }) -join '|'

$violations = @()
Get-ChildItem -Path $srcRoot -Recurse -Include *.cpp,*.h | ForEach-Object {
  $file = $_.FullName.Substring((Resolve-Path "$PSScriptRoot\..").Path.Length + 1)
  $lines = Get-Content $_.FullName
  $ctx = @('', '', '', '', '', '', '', '')
  $block_src = $false   # 块级出处标记：块起始注释标记，遇到闭合 } 失效
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    if ($line -match $srcPat -or ($line -match '豁免' -and $line -match $exemptPat)) { $block_src = $true }
    if ($line -match '^\}') { $block_src = $false }  # 仅顶层函数/命名空间结束才失效
    $isCode = $line.TrimStart() -notmatch '^//'
    if ($isCode -and $line -match $join) {
      # 多行数据块（渐变停靠数组 / 三元续行）回看 8 行，其余回看 4 行
      $isCont = ($line -match '^\s*(stops|stop|s|st|cs|pstops)\s*\[\s*\d+\s*\]\s*=') -or
                ($line -match '^\s*[?:]\s')
      $look = if ($isCont) { 8 } else { 4 }
      # 出处标记：src: / ← / 豁免(Mini 无对应物) / 简写行号 `// :123`；或处于已标注块内
      $ctxText = (@($line) + $ctx[0..($look - 1)]) -join "`n"
      $annotated = $block_src -or ($line -match $srcPat) -or
                   ($ctxText -match $srcPat) -or ($ctxText -match $exemptPat)
      # 硬性禁止：实测/截图/目测/近似/取色 不得作为依据
      $banned = $ctxText -match $forbidden
      if ($banned -or -not $annotated) {
        $why = if ($banned) { '禁止依据(实测/截图/目测/近似/取色)' } else { '无出处' }
        $violations += ('{0}:{1}: {2}  [{3}]' -f $file, ($i + 1), $line.Trim(), $why)
      }
    }
    $ctx = @($line) + $ctx[0..6]
  }
}

if ($violations.Count -gt 0) {
  Write-Host ("UI_SRC_GATE: {0} 处无出处常量" -f $violations.Count)
  $violations | ForEach-Object { Write-Host $_ }
  exit 1
} else {
  Write-Host 'UI_SRC_GATE: PASS（src/ui 全部数值/颜色字面量均有 src:/←/豁免 标注）'
  exit 0
}
