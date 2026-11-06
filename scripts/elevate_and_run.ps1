<#
  elevate_and_run.ps1 — 用一次 UAC 提升启动 acceptance_uac.ps1。

  存在原因：CI / Agent 的 PowerShell 进程通常非管理员且无交互桌面，
  Start-Process -Verb RunAs 会被直接取消。本脚本由用户双击
  scripts\run_uac_acceptance.cmd 触发，在可交互桌面上弹出一次 UAC，
  提升后 acceptance_uac.ps1 检测到已具备管理员上下文，便不再二次提权。

  纪律：
    - 提权失败/被取消必须返回非 0，绝不能吞掉异常后假装成功。
    - 本脚本自身不执行任何安装、卸载或打包动作。
    - 额外参数（如 -SkipRebuild / -SkipDestructive / -DryRun）原样转发。

  退出码：子脚本退出码；1 = 找不到目标脚本；2 = 提权失败或取消
#>
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Extra = @()
)

$target = Join-Path $PSScriptRoot 'acceptance_uac.ps1'
if (-not (Test-Path -LiteralPath $target)) {
    Write-Host "[ERROR] target script not found: $target" -ForegroundColor Red
    exit 1
}

$argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $target + '"'))
foreach ($e in $Extra) { $argList += $e }

Write-Host "Elevating: $target"
Write-Host ("Extra args: {0}" -f ($(if ($Extra.Count -gt 0) { $Extra -join ' ' } else { '(none)' })))

try {
    $p = Start-Process powershell.exe -ArgumentList $argList -Verb RunAs -Wait -PassThru -ErrorAction Stop
} catch {
    Write-Host ("[ERROR] UAC elevation failed: {0}" -f $_.Exception.Message) -ForegroundColor Red
    Write-Host 'No acceptance step was executed. Alternatively right-click run_uac_acceptance.cmd and choose "Run as administrator".' -ForegroundColor Yellow
    exit 2
}
if ($null -eq $p) {
    Write-Host '[ERROR] UAC elevation was cancelled; no acceptance step was executed.' -ForegroundColor Red
    exit 2
}
exit $p.ExitCode
