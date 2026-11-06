<#
  acceptance_uac.ps1 — 把 docs/README.md:129-138 的 5 项 UAC 验收编排为
  「一次管理员授权、逐项自动留证、产出结构化报告」。

  副作用警告：
    #3 会以 /S /KEEP 静默重装 Clash Win 两次；#4 会静默卸载 Clash Win。
    这两项会改变系统状态。请先用 -DryRun 预演，或用 -SkipDestructive 跳过。

  无法自动化的两项，脚本只列 MANUAL，不代做、不谎报：
    #0 重启清理 21 条僵尸 PID（需重启整机）
    #5 UAC 发布者目视（secure desktop 无法程序化截屏）

  纪律：
    - 不自动重启；不建 Release / 不推 tag / 不上传资产（docs/01-目的与方针.md:56）。
    - -DryRun 只做前置检查并打印将执行的命令：不签名、不安装、不卸载、不写报告文件。
    - 生成的 hash_*.txt 与报告写入 docs/evidence/，补齐 docs/README.md:53 的断链。
    - 依赖 docs/06-验收状态台账.md C 节的状态口径。

  用法：
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance_uac.ps1 -DryRun
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance_uac.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance_uac.ps1 -SkipDestructive

  退出码：0 = 全部执行项 PASS（或 DryRun 预演完成）；1 = 存在 FAIL；2 = 前置条件不满足
#>
[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$SkipDestructive,
    [string]$Version = '0.1.0',
    [string]$ReportPath = ''
)

$ErrorActionPreference = 'Continue'
$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Evidence = Join-Path $Repo 'docs\evidence'
$SetupName = "ClashWin_${Version}_x64-setup.exe"
$Setup = Join-Path $Repo "dist\$SetupName"
$ConfigDir = Join-Path $env:APPDATA 'io.github.clash-win.clash-win'
$InstallDir = Join-Path $env:ProgramFiles 'ClashWin'

$results = New-Object System.Collections.ArrayList

function Add-Result {
    param([string]$Step, [string]$Name, [string]$Status, [string]$Detail)
    [void]$results.Add([pscustomobject]@{
        Step = $Step; Name = $Name; Status = $Status; Detail = $Detail
    })
    Write-Host ("[{0,-11}] #{1} {2} — {3}" -f $Status, $Step, $Name, $Detail)
}

function Get-ConfigSnapshot {
    if (-not (Test-Path -LiteralPath $ConfigDir)) { return @() }
    $rows = @()
    Get-ChildItem -LiteralPath $ConfigDir -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
        $rel = $_.FullName.Substring($ConfigDir.Length).TrimStart('\')
        $h = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        $rows += ("{0}|{1}" -f $rel, $h)
    }
    return @($rows | Sort-Object)
}

function Write-Snapshot([string]$FileName, [object]$Rows) {
    if (-not (Test-Path -LiteralPath $Evidence)) {
        New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
    }
    $p = Join-Path $Evidence $FileName
    @($Rows) | Out-File -LiteralPath $p -Encoding utf8
    return $p
}

Write-Host "Clash Win UAC 验收  |  DryRun=$DryRun  SkipDestructive=$SkipDestructive  Version=$Version"
Write-Host ("Repo  = {0}" -f $Repo)
Write-Host ("Setup = {0}" -f $Setup)
Write-Host ''

# ---------- 前置：一次管理员授权 ----------
if ($DryRun) {
    Write-Host '=== DRYRUN：不签名、不安装、不卸载、不写报告 ==='
} else {
    $IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $IsAdmin) {
        Write-Host '需要管理员权限（读 LocalMachine\My 私钥 / 静默安装），正在请求一次 UAC 提升...' -ForegroundColor Yellow
        $q = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $PSCommandPath + '"'), '-Version', $Version)
        if ($SkipDestructive) { $q += '-SkipDestructive' }
        if ($ReportPath) { $q += @('-ReportPath', ('"' + $ReportPath + '"')) }
        $p = Start-Process powershell -ArgumentList $q -Verb RunAs -Wait -PassThru
        exit $p.ExitCode
    }
    Write-Host '管理员上下文：已具备（后续脚本继承，不再逐项弹 UAC）'
}

# ---------- #0 / #5：人工项 ----------
Add-Result '0' '重启清理僵尸 PID' 'MANUAL' '需人工重启一次；重启后 Get-Process clashwin* 应无 threads=0 残留'
Add-Result '5' 'UAC 发布者目视' 'SKIP-MANUAL' '双击安装包，目视提示框顶部是否显示「ClashWin」；secure desktop 无法程序化截屏'

# ---------- #1 签名 ----------
$signScript = Join-Path $PSScriptRoot 'sign.ps1'
$signCmd = "powershell -NoProfile -ExecutionPolicy Bypass -File `"$signScript`" -Paths `"$Setup`""
if ($DryRun) {
    Add-Result '1' '签名' 'DRYRUN' $signCmd
} else {
    if (-not (Test-Path -LiteralPath $Setup)) {
        Add-Result '1' '签名' 'FAIL' "安装包不存在: $Setup"
    } else {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $signScript -Paths $Setup | Out-Host
        $rc = $LASTEXITCODE
        $sig = Get-AuthenticodeSignature -LiteralPath $Setup
        $subj = if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { '(none)' }
        if ($rc -eq 0 -and $sig.Status -eq 'Valid' -and $subj -match 'CN=ClashWin') {
            Add-Result '1' '签名' 'PASS' ("Status={0} Subject={1} TimeStamper={2}" -f $sig.Status, $subj, ($null -ne $sig.TimeStamperCertificate))
        } else {
            Add-Result '1' '签名' 'FAIL' ("EXIT={0} Status={1} Subject={2}" -f $rc, $sig.Status, $subj)
        }
    }
}

# ---------- #2 完整打包（含卸载器签名钩子） ----------
$buildScript = Join-Path $Repo 'installer\build_installer.ps1'
$buildCmd = "powershell -NoProfile -ExecutionPolicy Bypass -File `"$buildScript`" -Version $Version"
if ($DryRun) {
    Add-Result '2' '完整打包' 'DRYRUN' $buildCmd
} else {
    if (-not (Test-Path -LiteralPath $buildScript)) {
        Add-Result '2' '完整打包' 'FAIL' "缺少 $buildScript"
    } else {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript -Version $Version | Out-Host
        $rc = $LASTEXITCODE
        if (Test-Path -LiteralPath $Setup) {
            $item = Get-Item -LiteralPath $Setup
            $sha = (Get-FileHash -LiteralPath $Setup -Algorithm SHA256).Hash
            $sig = Get-AuthenticodeSignature -LiteralPath $Setup
            if ($rc -eq 0 -and $item.Length -gt 0 -and $sig.Status -eq 'Valid') {
                Add-Result '2' '完整打包' 'PASS' ("{0} B  SHA256={1}  {2}" -f $item.Length, $sha, $sig.Status)
            } else {
                Add-Result '2' '完整打包' 'FAIL' ("EXIT={0} Bytes={1} Sig={2}" -f $rc, $item.Length, $sig.Status)
            }
        } else {
            Add-Result '2' '完整打包' 'FAIL' "EXIT=$rc 且未产出 $Setup"
        }
    }
}

# ---------- #3 /KEEP 静默重装 ×2 + 配置哈希比对 ----------
$keepCmd = "`"$Setup`" /S /KEEP  (×2，逐次比对 $ConfigDir 下全部文件 SHA256)"
if ($SkipDestructive) {
    Add-Result '3' '/KEEP 重装 ×2' 'SKIP' '已指定 -SkipDestructive'
} elseif ($DryRun) {
    Add-Result '3' '/KEEP 重装 ×2' 'DRYRUN' $keepCmd
} elseif (-not (Test-Path -LiteralPath $Setup)) {
    Add-Result '3' '/KEEP 重装 ×2' 'FAIL' "安装包不存在: $Setup"
} else {
    $before = @(Get-ConfigSnapshot)
    $pBefore = Write-Snapshot 'hash_before.txt' $before
    $ok3 = $true
    for ($i = 1; $i -le 2; $i++) {
        $proc = Start-Process -FilePath $Setup -ArgumentList '/S', '/KEEP' -Wait -PassThru
        $after = @(Get-ConfigSnapshot)
        $fname = if ($i -eq 1) { 'hash_after.txt' } else { 'hash_after2.txt' }
        $pAfter = Write-Snapshot $fname $after
        $diff = @(Compare-Object -ReferenceObject $before -DifferenceObject $after)
        $detail = "第{0}次 exit={1} 配置{2}项 差异{3}项  {4} / {5}" -f $i, $proc.ExitCode, $after.Count, $diff.Count, (Split-Path $pBefore -Leaf), (Split-Path $pAfter -Leaf)
        if ($proc.ExitCode -ne 0 -or $diff.Count -ne 0) {
            Add-Result '3' '/KEEP 重装 ×2' 'FAIL' $detail
            $ok3 = $false
            break
        }
        Write-Host ("         {0}" -f $detail)
    }
    if ($ok3) { Add-Result '3' '/KEEP 重装 ×2' 'PASS' '两次安装前后配置目录全部文件 SHA256 完全一致；hash_before/after/after2 已落 docs/evidence/' }
}

# ---------- #4 卸载实测 ----------
$uninstaller = Join-Path $InstallDir 'uninstall.exe'
$unCmd = "`"$uninstaller`" /S"
if ($SkipDestructive) {
    Add-Result '4' '卸载实测' 'SKIP' '已指定 -SkipDestructive'
} elseif ($DryRun) {
    Add-Result '4' '卸载实测' 'DRYRUN' $unCmd
} elseif (-not (Test-Path -LiteralPath $uninstaller)) {
    Add-Result '4' '卸载实测' 'FAIL' "卸载器不存在: $uninstaller"
} else {
    $proc = Start-Process -FilePath $uninstaller -ArgumentList '/S' -Wait -PassThru
    $exeGone = -not (Test-Path -LiteralPath (Join-Path $InstallDir 'clashwin.exe'))
    $cfgKept = Test-Path -LiteralPath $ConfigDir
    if ($proc.ExitCode -eq 0 -and $exeGone -and $cfgKept) {
        Add-Result '4' '卸载实测' 'PASS' "exit=0；程序文件已移除；配置目录 $ConfigDir 保留"
    } else {
        Add-Result '4' '卸载实测' 'FAIL' ("exit={0} exeGone={1} cfgKept={2}" -f $proc.ExitCode, $exeGone, $cfgKept)
    }
}

# ---------- 报告 ----------
Write-Host ''
Write-Host '=== 汇总 ==='
$results | ForEach-Object { Write-Host ("  #{0,-2} {1,-16} {2}" -f $_.Step, $_.Status, $_.Name) }

if (-not $DryRun) {
    if (-not (Test-Path -LiteralPath $Evidence)) {
        New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
    }
    if (-not $ReportPath) {
        $ReportPath = Join-Path $Evidence ("acceptance-uac-{0}.md" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    }
    $lines = New-Object System.Collections.ArrayList
    [void]$lines.Add("# UAC 验收报告 · $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
    [void]$lines.Add('')
    [void]$lines.Add("> 由 scripts\acceptance_uac.ps1 生成。SkipDestructive=$SkipDestructive。人工项 #0/#5 需另行确认。")
    [void]$lines.Add('')
    [void]$lines.Add('| # | 项目 | 状态 | 详情 |')
    [void]$lines.Add('| --- | --- | --- | --- |')
    foreach ($r in $results) {
        [void]$lines.Add(("| {0} | {1} | {2} | {3} |" -f $r.Step, $r.Name, $r.Status, ($r.Detail -replace '\|', '\|')))
    }
    [void]$lines.Add('')
    $failed = @($results | Where-Object { $_.Status -eq 'FAIL' })
    [void]$lines.Add("RESULT=$(if ($failed.Count -gt 0) { 'FAIL' } else { 'PASS' })")
    $lines | Out-File -LiteralPath $ReportPath -Encoding utf8
    Write-Host ("REPORT=$ReportPath")
}

$failed = @($results | Where-Object { $_.Status -eq 'FAIL' })
if ($DryRun) { Write-Host 'RESULT=DRYRUN（未改动系统，未写报告）'; exit 0 }
if ($failed.Count -gt 0) { Write-Host "RESULT=FAIL ($($failed.Count) 项)"; exit 1 }
Write-Host 'RESULT=PASS'
exit 0
