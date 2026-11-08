<#
  acceptance_clean_machine.ps1 — 干净机 P0「独立可用性」自检与证据链留证。

  口径：docs/01-目的与方针.md:27-35
    完整链路 = 检测（未安装 / STOPPED / 未就绪）→ 提权安装（UAC）→ 启动服务
              → 等待 IPC 就绪 → 以 SYSTEM 身份起内核；
    验收 = sc query RUNNING + 内核 Session#=0 + 境外三条 URL 通；
    「仅在 Mini 已装服务的机器上通过，不得宣布可用」。

  本脚本的严格边界（不要放宽）：
    - **绝不自动开启 TUN / 系统代理 / 改注册表 / 改路由**（02-方法与流程.md:34,46-50 断网红线）。
      出网验证只用显式代理 curl -x 127.0.0.1:10801，属于「使用网络」而非「设置网络」。
    - **不自证干净机**：检测到 Clash Mini 安装痕迹或服务 ImagePath 指向 Mini，
      即报 NOT-CLEAN-MACHINE，不宣布 P0 通过。
    - **服务已存在即报 SERVICE-PREEXISTING**：不满足「服务从缺失到就位」口径，
      脚本不宣布 P0 通过（01-目的与方针.md:35）。
    - 脚本不代替人工裁决：末行固定 P0_DECLARATION=DEFERRED，最终结论须由人附完整链路证据后给出。
    - -DryRun 只跑只读探测：不安装服务、不发起出网请求、不写报告文件。
    - -AllowNetworkChange 为预留开关；本脚本当前**不实现任何网络改动**，传入只打印提示。
    - -Urls 为空时该项标 SKIP，不编造 URL。

  用法：
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance_clean_machine.ps1 -DryRun
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance_clean_machine.ps1 -Urls 'https://www.google.com','https://www.cloudflare.com','https://github.com'

  退出码：0 = 证据采集完成（不代表 P0 通过）；1 = 环境不满足干净机前提；2 = 采集过程异常
#>
[CmdletBinding()]
param(
    [switch]$DryRun,
    [string[]]$Urls = @(),
    [string]$ReportPath = '',
    [switch]$AllowNetworkChange,
    [int]$CurlTimeoutSec = 15
)

$ErrorActionPreference = 'Continue'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Evidence = Join-Path $Repo 'docs\evidence'
$lines = New-Object System.Collections.ArrayList
$flags = New-Object System.Collections.ArrayList

function Rec([string]$Section, [string]$Key, [string]$Value) {
    if ([string]::IsNullOrEmpty($Value)) { $Value = '(empty)' }
    [void]$lines.Add(("{0} | {1} | {2}" -f $Section, $Key, $Value))
    Write-Host ("  {0,-14} {1,-26} {2}" -f $Section, $Key, $Value)
}
function Flag([string]$f) { [void]$flags.Add($f); Write-Host ("  FLAG: {0}" -f $f) -ForegroundColor Yellow }

Write-Host 'Clash Win 干净机 P0 自检（只读优先，绝不改网络设置）'
Write-Host ("DryRun={0}  Urls={1}  AllowNetworkChange={2}" -f $DryRun, $Urls.Count, $AllowNetworkChange)
Write-Host ''
if ($AllowNetworkChange) {
    Write-Host '提示：-AllowNetworkChange 为预留开关，本脚本不实现任何网络改动。' -ForegroundColor Yellow
}

# ---------- 1. 干净机自证 ----------
Write-Host '--- 1. 干净机自证 ---'
$miniDirs = @((Join-Path $env:ProgramFiles 'Clash Mini'))
$pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
if ($pf86) { $miniDirs += (Join-Path $pf86 'Clash Mini') }
foreach ($d in $miniDirs) {
    if ($d -and (Test-Path -LiteralPath $d)) { Rec 'CLEAN' "Mini 安装目录存在" $d; Flag 'NOT-CLEAN-MACHINE' }
}
$miniProcs = @(Get-Process -Name 'clash-mini' -ErrorAction SilentlyContinue)
Rec 'CLEAN' 'Mini 进程数' $miniProcs.Count
if ($miniProcs.Count -gt 0) { Flag 'NOT-CLEAN-MACHINE' }

# ---------- 2. 服务链路状态（sc query / qc，只读） ----------
Write-Host '--- 2. 服务链路状态 ---'
$svcOut = & sc.exe query clash_verge_service 2>&1
$svcRc = $LASTEXITCODE
$svcText = ($svcOut | Out-String)
# sc.exe 在受限执行环境可能被策略禁用。此时绝不能把「探测失败」误判成「服务不存在 / 干净机起点正确」。
$svcProbeFailed = ($null -eq $svcRc) -or
                  ($svcText -match 'Access is denied|failed to run|NativeCommandFailed|Program Blacklist|Blocked')
$svcRcText = if ($null -eq $svcRc) { '(none)' } else { [string]$svcRc }
Rec 'SERVICE' 'sc query EXIT' $svcRcText
if ($svcProbeFailed) {
    Rec 'SERVICE' '状态' 'UNKNOWN（sc.exe 不可用，无法判定；不得据此推断服务缺失或干净机）'
    Flag 'SERVICE-QUERY-UNAVAILABLE'
} elseif ($svcRc -eq 0) {
    $state = 'UNKNOWN'
    if ($svcText -match 'STATE\s*:\s*(\d+)\s+(\w+)') { $state = $Matches[2] }
    Rec 'SERVICE' 'STATE' $state
    $cfgOut = & sc.exe qc clash_verge_service 2>&1
    $cfgText = ($cfgOut | Out-String)
    $imagePath = ''
    if ($cfgText -match '(?m)^\s*BINARY_PATH_NAME\s*:\s*(.+)$') { $imagePath = $Matches[1].Trim() }
    Rec 'SERVICE' 'BINARY_PATH_NAME' $imagePath
    $imgFile = $imagePath -replace '^"([^"]+)".*$', '$1'
    Rec 'SERVICE' 'ImagePath 文件存在' (Test-Path -LiteralPath $imgFile)
    if ($imagePath -match 'Clash Mini') { Flag 'NOT-CLEAN-MACHINE' }
    if ($state -eq 'RUNNING') { Flag 'SERVICE-PREEXISTING' }
    if ($state -eq 'STOPPED') { Rec 'SERVICE' '链路提示' '服务已注册但 STOPPED：需 UAC 启动，不满足「从缺失到就位」' }
} else {
    Rec 'SERVICE' 'STATE' 'NOT_INSTALLED (1060)'
    Rec 'SERVICE' '链路提示' '干净机起点正确：下一步需 UAC 安装服务（脚本不自动安装）'
    Rec 'SERVICE' '应执行命令' "sidecar\clash-verge-service-install.exe 或 dist\ClashWin_0.1.0_x64-setup.exe /S（需管理员）"
    if (-not $DryRun) { Flag 'SERVICE-INSTALL-PENDING' }
}

# ---------- 3. IPC 命名管道（只读枚举） ----------
Write-Host '--- 3. IPC 命名管道 ---'
try {
    $pipes = @([System.IO.Directory]::GetFiles('\\.\pipe\'))
    $hit = @($pipes | Where-Object { $_ -match 'mini-mihomo-win|clash-win-mihomo' })
    Rec 'IPC' '控制面管道命中' ($(if ($hit.Count -gt 0) { ($hit -join ', ') } else { '(none)' }))
    Rec 'IPC' '管道总数' $pipes.Count
} catch {
    Rec 'IPC' '管道枚举' "UNKNOWN（$($_.Exception.Message)）"
}

# ---------- 4. 内核进程 SessionId ----------
Write-Host '--- 4. 内核进程 ---'
$cores = @(Get-Process -Name 'mini-mihomo' -ErrorAction SilentlyContinue)
Rec 'CORE' 'mini-mihomo 进程数' $cores.Count
foreach ($c in $cores) {
    $p = '(denied)'
    try { $p = $c.Path } catch { }
    Rec 'CORE' ("PID={0} SessionId={1}" -f $c.Id, $c.SessionId) $p
}
if ($cores.Count -eq 0) { Flag 'CORE-NOT-RUNNING' }

# ---------- 5. 网络基线（只读，绝不修改） ----------
Write-Host '--- 5. 网络基线（只读） ---'
try {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
    $ip = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
    Rec 'NET' 'ProxyEnable' $ip.ProxyEnable
    Rec 'NET' 'ProxyServer' $ip.ProxyServer
    Rec 'NET' 'AutoConfigURL' $ip.AutoConfigURL
} catch {
    Rec 'NET' '代理基线' "UNKNOWN（$($_.Exception.Message)）"
}
try {
    $r = @(Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue)
    foreach ($x in $r) {
        Rec 'NET' '默认路由' ("{0} -> {1} if={2}" -f $x.DestinationPrefix, $x.NextHop, $x.InterfaceIndex)
        if ($x.NextHop -like '198.18.*') { Flag 'TUN-ROUTE-PRESENT（存在 TUN 默认路由；本脚本不改动网络）' }
    }
    if ($r.Count -eq 0) { Rec 'NET' '默认路由' '(none)' }
} catch {
    Rec 'NET' '默认路由' "UNKNOWN（$($_.Exception.Message)）"
}

# ---------- 6. 三条境外 URL（显式代理，不改系统设置） ----------
Write-Host '--- 6. 境外三条 URL ---'
if ($Urls.Count -eq 0) {
    Rec 'URL' '状态' 'SKIP（-Urls 未指定；不编造 URL，请按 01-目的与方针.md:35 指定三条）'
    Flag 'URLS-NOT-PROVIDED'
} else {
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if (-not $curl) {
        Rec 'URL' '状态' 'SKIP（未找到 curl.exe）'
        Flag 'URLS-NOT-TESTED'
    } else {
        foreach ($u in $Urls) {
            if ($DryRun) {
                Rec 'URL' $u "DRYRUN: curl.exe -x http://127.0.0.1:10801 --max-time $CurlTimeoutSec"
            } else {
                try {
                    $code = (& curl.exe -x 'http://127.0.0.1:10801' -s -o NUL -w '%{http_code}' --max-time $CurlTimeoutSec $u 2>&1 | Out-String).Trim()
                    Rec 'URL' $u "HTTP $code（显式代理 127.0.0.1:10801，未改系统设置）"
                    if ($code -notmatch '^2\d\d$|^3\d\d$') { Flag 'URL-FAILED' }
                } catch {
                    Rec 'URL' $u "ERROR（$($_.Exception.Message)）"
                    Flag 'URL-FAILED'
                }
            }
        }
    }
}

# ---------- 7. 结论（不越权宣布 P0 通过） ----------
Write-Host ''
Write-Host '=== 证据链结论 ==='
foreach ($f in $flags) { Write-Host ("  FLAG: {0}" -f $f) -ForegroundColor Yellow }

$declaration = 'DEFERRED'
$note = '证据采集完成；P0 最终结论须由人附「服务从缺失到就位」完整链路证据后给出（01-目的与方针.md:35）'
if ($flags -contains 'NOT-CLEAN-MACHINE') {
    $note = '检测到 Clash Mini 痕迹：本机不是干净机，不得据此宣布 P0 通过'
    $declaration = 'NOT-CLEAN-MACHINE'
} elseif ($flags -contains 'SERVICE-PREEXISTING') {
    $note = '服务已 RUNNING：不满足「服务从缺失到就位」口径，不得宣布 P0 通过'
    $declaration = 'SERVICE-PREEXISTING'
} elseif ($flags -contains 'SERVICE-QUERY-UNAVAILABLE') {
    $note = '服务状态采集不可用，证据链不完整，不得宣布 P0 通过'
    $declaration = 'INCOMPLETE'
}

Write-Host ("EVIDENCE=$declaration")
Write-Host ("NOTE=$note")
Write-Host 'P0_DECLARATION=DEFERRED'

# ---------- 报告（DryRun 不写，避免假证据） ----------
if (-not $DryRun) {
    if (-not (Test-Path -LiteralPath $Evidence)) {
        New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
    }
    if (-not $ReportPath) {
        $ReportPath = Join-Path $Evidence ("acceptance-p0-{0}.md" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    }
    $out = New-Object System.Collections.ArrayList
    [void]$out.Add("# 干净机 P0 证据链 · $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
    [void]$out.Add('')
    [void]$out.Add('> 由 scripts\acceptance_clean_machine.ps1 生成。脚本不宣布 P0 通过，最终结论须人工按 01-目的与方针.md:35 裁决。')
    [void]$out.Add('')
    [void]$out.Add('| 分组 | 项 | 值 |')
    [void]$out.Add('| --- | --- | --- |')
    foreach ($l in $lines) { [void]$out.Add("| $l |") }
    [void]$out.Add('')
    [void]$out.Add("EVIDENCE=$declaration")
    [void]$out.Add("FLAGS=$($flags -join ', ')")
    [void]$out.Add('P0_DECLARATION=DEFERRED')
    $out | Out-File -LiteralPath $ReportPath -Encoding utf8
    Write-Host ("REPORT=$ReportPath")
} else {
    Write-Host '（DryRun：未写报告文件）'
}

if ($declaration -eq 'NOT-CLEAN-MACHINE') { exit 1 }
exit 0
