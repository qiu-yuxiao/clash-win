# ui_src_gate.ps1 — UI 层「无源码出处常量」门禁（G4 加固版 / 2026-11-07）
# 用法：powershell -ExecutionPolicy Bypass -File scripts\ui_src_gate.ps1 [src根目录]
#   -VerifySources      额外输出出处引用解析明细（RESOLVED 实文 / SHORTHAND）
#   -Strict             把四类存量弱项一并计入违规（供冻结后 / CI 启用）
#   -MiniRoot <path>    指定 Clash Mini 工作区（缺省为 ..\..\ClashVerge）
#   构建前运行；有违规输出清单并 exit 1（可接 CI/预构建步骤）。
# 规则（沿用）：src/ui/** 代码行含 数值/颜色字面量 且（本行 / 上 4-8 行 / 所属块起始
#   `src:`（源码出处，规范）/ `←`（历史移植标记）/ `豁免:`（仅限 Mini 无对应物））
#   没有来源即视为违规。
# 硬性禁止（2026-11-05 起）：不得以「实测 / 截图 / 目测 / 近似 / 取色」作为数值依据。
# 豁免写法：`// 豁免: Mini 无对应物 — <理由>（出处: <Mini 路径或“无”>）`；
#   仅写「豁免」或「有源但取不到」均视为违规。
#
# G4 加固（4 类绕过；当前树存量弱项不默认一刀切，见 -Strict）：
#   G4-a 块级传染：原 $block_src 单布尔只在顶格 } 失效，子块内标 src: 可把后续所有
#        兄弟块的数值行全部放行。新增 {} 深度栈，标注只覆盖本块及子块；仅靠跨块
#        传染放行的数值行列入 WEAK_SRC_CONTAGION 清单，-Strict 下阻断。
#   G4-b 禁词窗口：原 $banned 只查「数值行 ±4/8 行」。新增 N2（禁词与数值字面量
#        同行且无任何出处/豁免标注，默认即拦截）+ BANNED_UNCAUGHT（块内远处禁词
#        清单，-Strict 下阻断）。
#   G4-d 豁免收紧：新增 N3（裸「豁免」无实质理由，默认即拦截）；无「Mini 无对应物」
#        的简化豁免列 EXEMPT_WEAK 清单，「出处: 无」单列 INFO 统计。
#   G4-c 出处验真：抽取 src:/← 注释中的 路径:行号，按 MiniRoot 解析（node_modules
#        被 .gitignore 忽略，必须用 Test-Path 直判磁盘，不能走 git ls-files）。
#        报告 UNRESOLVED_FILE / UNRESOLVED_LINE / SHORTHAND / RESOLVED；
#        N1（伪造出处：文件不存在或行号越界）默认即拦截。
# 默认模式：主判定语义与改前一致，新增通道以 INFO 清单输出，保证当前树仍 PASS。
param(
  [string]$Root      = "$PSScriptRoot\..\src\ui",
  [switch]$VerifySources,
  [switch]$Strict,
  [string]$MiniRoot  = "$PSScriptRoot\..\..\ClashVerge"
)

$ErrorActionPreference = 'Stop'
$srcRoot  = (Resolve-Path $Root).Path
$repoLike = (Resolve-Path "$PSScriptRoot\..").Path

$forbidden = '实测|截图|目测|近似|取色'
$exemptPat = '豁免[^\r\n]*Mini\s*无对应物'
# G4-d 收紧：合法豁免须同时具备「— <理由>」与「（出处: …）」；仅写「Mini 无对应物」不算。
# 注意：$exemptPat 仍用于主判定的 $annotated（保持改前语义，避免当前树新增假红），
#       本正则只用于 G4-d 的豁免「质量」清单，默认只列清单不阻断，-Strict 下阻断。
$strictExempt = '豁免[^\r\n]*Mini\s*无对应物[^\r\n]*[—–-][^\r\n]*（\s*出处\s*:'
$srcPat    = 'src:|←|//\s*:\d'
# G4-a 严格锚点：src: 后必须跟路径字符，防止说明性文字里的 "src:" 字样伪造出处锚点
$anchorPat = 'src:\s*[A-Za-z0-9_.@/\\-]|←|//\s*:\d'
$numLit    = '0x[0-9A-Fa-f]{6,8}|\b\d+\.\d+f?\b'
# 出处引用：带扩展名的相对/绝对路径（可含 @ 与 /），后接可选 :行号 或 :行号-行号
# 扩展名 alternation 必须「长后缀优先」：json 必须排在 jsx? 之前。
# 否则 .json 会被 jsx? 分支只吃掉 "js" 两字符（"on" 挡住行号组），
# token 被截成不存在的 .js 且 :行号 一并丢失 → -VerifySources 整批 i18n 假红。
# tsx? / jsx? 为单分支内可选 x，对 .tsx/.ts、.jsx/.js 无前缀截断，无需拆分。
$refPat    = '([A-Za-z0-9_.@/-]+\.(?:scss|css|tsx?|json|jsx?|md|h|cpp))(?::(\d+)(?:-(\d+))?)?'
# 简写行号（前面无文件名），如 `src: :674-676`
$shortPat  = '(?<![A-Za-z0-9_.@/-]):(\d+)(?:-(\d+))?'

# N1：伪造出处（文件不存在 / 行号越界）。实测当前树存在大量「按 Mini 工作区相对
# src/ 书写」的历史标注与 Win 侧自引用，默认硬拦截会造成 107 处假红并打断冻结重跑；
# 故默认只列入 UNRESOLVED 清单完整披露，由 -Strict 阻断。
# 默认即拦截的零误伤规则为 N2（禁词与数值同行且无标注）/ N3（裸豁免）。
$HardUnresolved = $false

# 字面量检测模式（只盯 UI 常量语义，避免循环变量等噪音）——与改前逐字一致
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

# ---------- MiniRoot 与「纯文件名 -> 全路径」索引（G4-c） ----------
$miniRootResolved = $null
if (Test-Path $MiniRoot) { $miniRootResolved = (Resolve-Path $MiniRoot).Path }
$fileNameIndex = @{}
if ($miniRootResolved) {
  # 只索引 src：node_modules/@mui 多为 pnpm 符号链接，PS5.1 -Recurse 不跟随
  # （实测返回 0 文件），且带完整路径的 node_modules/... 引用走 Test-Path 直查即可。
  $indexRoots = @((Join-Path $miniRootResolved 'src'))
  foreach ($ir in $indexRoots) {
    if (-not (Test-Path $ir)) { continue }
    Get-ChildItem -Path $ir -Recurse -File -Include *.scss,*.css,*.tsx,*.ts,*.jsx,*.js,*.json,*.md,*.h,*.cpp -ErrorAction SilentlyContinue |
      ForEach-Object { if (-not $fileNameIndex.ContainsKey($_.Name)) { $fileNameIndex[$_.Name] = $_.FullName } }
  }
}
# Win 侧自身文件名集合：注释中提及 Win 侧实现文件（tooltip_w.h 等）不是 Mini 出处
$winFileNames = @{}
Get-ChildItem -Path $srcRoot -Recurse -File -Include *.cpp,*.h -ErrorAction SilentlyContinue |
  ForEach-Object { $winFileNames[$_.Name] = $true }
$lineCache = @{}
function Test-SourceRef([string]$tok, [int]$ln, [int]$end) {
  if (-not $miniRootResolved) { return 'NO_MINIROOT|' }
  $norm = $tok -replace '/', '\'
  $base = Split-Path -Leaf $norm
  # 多级回退：MiniRoot/<path> → MiniRoot/src/<path>（历史标注惯例）→ src 内纯文件名索引
  $cands = @()
  $cands += (Join-Path $miniRootResolved $norm)
  $cands += (Join-Path $miniRootResolved ('src\' + $norm))
  if ($fileNameIndex.ContainsKey($base)) { $cands += $fileNameIndex[$base] }
  $full = $null
  foreach ($c in $cands) {
    if ($c -and (Test-Path -LiteralPath $c)) { $full = (Resolve-Path -LiteralPath $c).Path; break }
  }
  if (-not $full) {
    if ($winFileNames.ContainsKey($base)) { return 'NON_MINI_REF|Win 侧自引用，非 Mini 出处' }
    return 'UNRESOLVED_FILE|'
  }
  if ($ln -le 0) { return 'RESOLVED_NOLINE|' }
  if (-not $lineCache.ContainsKey($full)) { $lineCache[$full] = @(Get-Content -LiteralPath $full -ErrorAction SilentlyContinue) }
  $arr = $lineCache[$full]
  if ($end -gt $arr.Count) { return ('UNRESOLVED_LINE|max={0}' -f $arr.Count) }
  return ('RESOLVED|L{0}: {1}' -f $ln, $arr[$ln - 1].Trim())
}

# ---------- 扫描 ----------
$violations      = @()   # 主判定 + N1/N2/N3（默认硬拦截）
$weakSrc         = @()   # G4-a 仅靠跨块传染
$bannedUncaught  = @()   # G4-b 块内远处禁词
$exemptWeak      = @()   # G4-d 简化豁免
$exemptBare      = @()   # G4-d 裸豁免（并入 violations）
$bannedLit       = @()   # 禁词与数值字面量同行（INFO）
$refReports      = @()   # G4-c 引用解析结果
$shorthand       = @()   # G4-c 简写行号
$exemptNoSrc     = 0
$bannedAny       = 0
$numeric         = 0

# 与上方索引构建一致加 -File：PS5.1 的 -Recurse -Include 会一并返回容器，
# 否则下方 Get-Content -LiteralPath 对目录抛 UnauthorizedAccess 并中断 Stop 模式扫描。
Get-ChildItem -Path $srcRoot -Recurse -File -Include *.cpp,*.h | ForEach-Object {
  $full = $_.FullName
  $file = if ($full.StartsWith($repoLike, [StringComparison]::OrdinalIgnoreCase)) {
            $full.Substring($repoLike.Length + 1)
          } else { $_.Name }
  $lines = Get-Content -LiteralPath $full
  $ctx = @('', '', '', '', '', '', '', '')
  $block_src = $false                                   # 改前单布尔（主判定保留）
  $depth = 0                                            # G4-a 深度栈
  $anchors = New-Object 'System.Collections.Generic.List[int]'
  $blockId = 0
  $numericWindows = @{}                                 # "$file#$lineNo" 已被原窗口覆盖
  $numericBlocks  = @{}                                 # "$file#$blockId" 块内有数值行
  $fileBanned     = @()

  for ($i = 0; $i -lt $lines.Count; $i++) {
    $line   = $lines[$i]
    $lineNo = $i + 1
    $codeOnly = $line -replace '//.*$', ''

    # --- G4-a 深度栈：本行 } 收缩并弹出越界锚点 ---
    $closes = ([regex]::Matches($codeOnly, '\}')).Count
    if ($closes -gt 0) {
      $depth = [Math]::Max(0, $depth - $closes)
      for ($k = $anchors.Count - 1; $k -ge 0; $k--) {
        if ($anchors[$k] -gt $depth) { $anchors.RemoveAt($k) }
      }
      if ($depth -eq 0) { $blockId++ }
    }

    # --- 主判定（改前语义，逐字保留） ---
    if ($line -match $srcPat -or ($line -match '豁免' -and $line -match $exemptPat)) { $block_src = $true }
    if (($line -match $anchorPat) -or ($line -match '豁免' -and $line -match $exemptPat)) {
      if (-not $anchors.Contains($depth)) { $anchors.Add($depth) }
    }
    if ($line -match '^\}') { $block_src = $false }
    $isCode = $line.TrimStart() -notmatch '^//'
    if ($isCode -and $line -match $join) {
      $numeric++
      $isCont = ($line -match '^\s*(stops|stop|s|st|cs|pstops)\s*\[\s*\d+\s*\]\s*=') -or
                ($line -match '^\s*[?:]\s')
      $look = if ($isCont) { 8 } else { 4 }
      $ctxText = (@($line) + $ctx[0..($look - 1)]) -join "`n"
      $annotated = $block_src -or ($line -match $srcPat) -or
                   ($ctxText -match $srcPat) -or ($ctxText -match $exemptPat)
      $banned = $ctxText -match $forbidden
      if ($banned -or -not $annotated) {
        $why = if ($banned) { '禁止依据(实测/截图/目测/近似/取色)' } else { '无出处' }
        $violations += ('{0}:{1}: {2}  [{3}]' -f $file, $lineNo, $line.Trim(), $why)
      }
      # G4-a：自携出处 vs 深度栈覆盖 vs 仅靠跨块传染
      $selfSrc  = ($line -match $srcPat) -or ($ctxText -match $srcPat) -or ($ctxText -match $exemptPat)
      $stackSrc = $false
      foreach ($a in $anchors) { if ($a -le $depth) { $stackSrc = $true; break } }
      if (-not $selfSrc -and $block_src -and -not $stackSrc) {
        $weakSrc += ('{0}:{1}: {2}  [G4-a 仅靠跨块传染]' -f $file, $lineNo, $line.Trim())
      }
      for ($w = 0; $w -le $look; $w++) { $numericWindows[('{0}#{1}' -f $file, ($lineNo - $w))] = $true }
      $numericBlocks[('{0}#{1}' -f $file, $blockId)] = $true
    }

    # --- G4-b：禁词全局收集 + N2 默认硬拦截 ---
    if ($line -match $forbidden) {
      $bannedAny++
      if ($line -match $numLit) { $bannedLit += ('{0}:{1}: {2}' -f $file, $lineNo, $line.Trim()) }
      $fileBanned += [pscustomobject]@{ File = $file; No = $lineNo; Text = $line.Trim(); Block = $blockId }
      # N2：禁词与数值/颜色字面量同行、是代码行、且既无出处标注也无豁免标注
      if ($isCode -and ($line -match $numLit) -and
          -not ($line -match $srcPat) -and -not ($line -match '豁免')) {
        $violations += ('{0}:{1}: {2}  [G4-b2 禁止依据与数值同行且无出处/豁免]' -f $file, $lineNo, $line.Trim())
      }
    }

    # --- G4-d：豁免质量 + N3 默认硬拦截 ---
    if ($line -match '豁免') {
      if ($line -match '出处:\s*无') { $exemptNoSrc++ }
      if ($line -match '豁免\s*[:：]?\s*$') {
        $exemptBare += ('{0}:{1}: {2}' -f $file, $lineNo, $line.Trim())
        $violations += ('{0}:{1}: {2}  [G4-d 裸豁免无实质理由]' -f $file, $lineNo, $line.Trim())
      } elseif ($line -notmatch $strictExempt) {
        $exemptWeak += ('{0}:{1}: {2}  [G4-d 简化豁免（缺 — 理由 /（出处: …））]' -f $file, $lineNo, $line.Trim())
      }
    }

    # --- G4-c：出处引用抽取 ---
    if ($line -match 'src:|←') {
      $matchesRef = [regex]::Matches($line, $refPat)
      foreach ($m in $matchesRef) {
        $tok = $m.Groups[1].Value
        $ln  = if ($m.Groups[2].Success) { [int]$m.Groups[2].Value } else { 0 }
        $end = if ($m.Groups[3].Success) { [int]$m.Groups[3].Value } else { $ln }
        $r = Test-SourceRef $tok $ln $end
        $bits = $r.Split('|')
        $rangeSuffix = if ($m.Groups[3].Success) { '-' + $m.Groups[3].Value } else { '' }
        $refReports += [pscustomobject]@{
          Where  = ('{0}:{1}' -f $file, $lineNo)
          Token  = ('{0}:{1}{2}' -f $tok, $(if ($ln -gt 0) { $ln } else { '-' }), $rangeSuffix)
          Status = $bits[0]
          Detail = $bits[1]
        }
      }
      if ($matchesRef.Count -eq 0 -and $line -match $shortPat) {
        $shorthand += ('{0}:{1}: {2}' -f $file, $lineNo, $line.Trim())
      }
    }

    # 本行 { 增加深度（放最后，使下一行处于新块内）
    $opens = ([regex]::Matches($codeOnly, '\{')).Count
    if ($opens -gt 0) { $depth += $opens }

    $ctx = @($line) + $ctx[0..6]
  }

  # G4-b：块内禁词但未被原回看窗口捕获（该块内确实存在数值字面量行）
  foreach ($b in $fileBanned) {
    if ($numericWindows.ContainsKey(('{0}#{1}' -f $b.File, $b.No))) { continue }
    if (-not $numericBlocks.ContainsKey(('{0}#{1}' -f $b.File, $b.Block))) { continue }
    $bannedUncaught += ('{0}:{1}: {2}  [G4-b 块内远处禁词未被原窗口捕获]' -f $b.File, $b.No, $b.Text)
  }
}

# ---------- G4-c 汇总 ----------
$unresolvedFile = @($refReports | Where-Object { $_.Status -eq 'UNRESOLVED_FILE' })
$unresolvedLine = @($refReports | Where-Object { $_.Status -eq 'UNRESOLVED_LINE' })
$resolved       = @($refReports | Where-Object { $_.Status -eq 'RESOLVED' })
$resolvedNoLine = @($refReports | Where-Object { $_.Status -eq 'RESOLVED_NOLINE' })
$nonMiniRef     = @($refReports | Where-Object { $_.Status -eq 'NON_MINI_REF' })
$unresolvedAll  = @($unresolvedFile + $unresolvedLine)

# N1：伪造出处（文件不存在 / 行号越界）——默认列入清单披露，-Strict 下阻断
foreach ($r in $unresolvedAll) {
  if ($HardUnresolved) { $violations += ('{0}: {1}  [G4-c {2}]' -f $r.Where, $r.Token, $r.Status) }
}

# ---------- 输出 ----------
# -Strict：四类存量弱项并入主违规清单统一判定（避免主判定先 exit 导致弱项段不执行）
if ($Strict) {
  foreach ($it in $weakSrc)        { $violations += ('{0}  [STRICT]' -f $it) }
  foreach ($it in $bannedUncaught) { $violations += ('{0}  [STRICT]' -f $it) }
  foreach ($it in $exemptWeak)     { $violations += ('{0}  [STRICT]' -f $it) }
  foreach ($r  in $unresolvedAll)  { $violations += ('{0}: {1}  [STRICT G4-c {2}]' -f $r.Where, $r.Token, $r.Status) }
}
if ($violations.Count -gt 0) {
  Write-Host ("UI_SRC_GATE: {0} 处违规{1}" -f $violations.Count, $(if ($Strict) { '（含 -Strict 弱项）' } else { '' }))
  $violations | ForEach-Object { Write-Host $_ }
  exit 1
}
Write-Host 'UI_SRC_GATE: PASS（src/ui 全部数值/颜色字面量均有 src:/←/豁免 标注）'

$mode = if ($Strict) { 'STRICT' } else { 'DEFAULT' }
Write-Host ('UI_SRC_GATE/G4: MODE={0} NUMERIC={1} WEAK_SRC_CONTAGION={2} BANNED_ANY={3} BANNED_UNCAUGHT={4} EXEMPT_WEAK={5} EXEMPT_NO_SRC={6} EXEMPT_BARE={7}' -f `
  $mode, $numeric, $weakSrc.Count, $bannedAny, $bannedUncaught.Count, $exemptWeak.Count, $exemptNoSrc, $exemptBare.Count)
Write-Host ('UI_SRC_GATE/G4: REFS RESOLVED={0} RESOLVED_NOLINE={1} UNRESOLVED_FILE={2} UNRESOLVED_LINE={3} NON_MINI_REF={4} SHORTHAND={5} MiniRoot={6}' -f `
  $resolved.Count, $resolvedNoLine.Count, $unresolvedFile.Count, $unresolvedLine.Count, $nonMiniRef.Count, $shorthand.Count, $(if ($miniRootResolved) { $miniRootResolved } else { '<未找到>' }))
Write-Host 'UI_SRC_GATE/G4: 默认硬拦截 = N2 禁词与数值同行且无标注 / N3 裸豁免；N1 伪造出处与存量弱项列清单，-Strict 下阻断'

function Show-List([string]$title, [array]$items, [int]$take) {
  if ($items.Count -eq 0) { return }
  Write-Host ('--- {0}（前 {1} / 共 {2}）---' -f $title, [Math]::Min($take, $items.Count), $items.Count)
  $items | Select-Object -First $take | ForEach-Object { Write-Host $_ }
}
Show-List '[G4-a] WEAK_SRC_CONTAGION 清单' $weakSrc 15
Show-List '[G4-b] BANNED_UNCAUGHT 清单' $bannedUncaught 15
Show-List '[G4-d] EXEMPT_WEAK 清单' $exemptWeak 20
Show-List '[G4-b] 禁词与数值字面量同行（INFO）' $bannedLit 10

# 未解析引用清单（主理人要求的交付项，完整输出）
Write-Host ('--- [G4-c] UNRESOLVED 引用清单（共 {0}；另 NON_MINI_REF={1} 不属 Mini 出处）---' -f $unresolvedAll.Count, $nonMiniRef.Count)
$unresolvedAll | ForEach-Object {
  Write-Host ('{0}: {1}  [{2} {3}]' -f $_.Where, $_.Token, $_.Status, $_.Detail)
}

if ($VerifySources) {
  Write-Host ('--- [G4-c] RESOLVED 样本（前 12 / 共 {0}，含引用行实文）---' -f $resolved.Count)
  $resolved | Select-Object -First 12 | ForEach-Object {
    Write-Host ('{0}: {1}  [{2}]' -f $_.Where, $_.Token, $_.Detail)
  }
  Show-List '[G4-c] SHORTHAND 简写行号清单' $shorthand 10
}

if ($Strict) { Write-Host 'UI_SRC_GATE/STRICT: 0 项弱项 -> PASS' }
exit 0
