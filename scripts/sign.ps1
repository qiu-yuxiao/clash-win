# ClashWin 代码签名脚本（Windows Authenticode / signtool）
#
# 作用：
#   1) 幂等维护一张本机自签代码签名证书（CN=ClashWin，EKU=Code Signing）：
#      已存在且带私钥则复用，否则用 New-SelfSignedCertificate 创建；
#      并把公钥证书安装到 LocalMachine\Root 与 LocalMachine\TrustedPublisher，
#      使本机 UAC / SmartScreen 能识别发布者。
#   2) 对指定产物执行 signtool sign（sha256 + RFC3161 时间戳）。
#   3) 用 Get-AuthenticodeSignature 逐项验证并打印结果。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File scripts\sign.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\sign.ps1 -Paths dist\ClashWin_0.1.0_x64-setup.exe
#
# 说明：
#   - 首次创建/安装证书需要管理员，脚本会自动请求提升（UAC 一次）；之后复用同一张证书。
#   - 自签证书只在本机受信任；对外发行仍需公网可信证书（OV/EV 或 Azure Trusted Signing）。
[CmdletBinding()]
param(
    [string[]]$Paths,
    [string]$Subject = 'CN=ClashWin',
    [string]$TimestampUrl = 'http://timestamp.digicert.com',
    [switch]$NoTimestamp,
    [string]$ResultFile = (Join-Path $env:TEMP 'clashwin-sign-result.txt')
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# ---------- 0. 管理员自提升（装证书到 LocalMachine 需要） ----------
$IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $IsAdmin) {
    Write-Host "需要管理员权限安装/读取本机证书，正在请求提升..." -ForegroundColor Yellow
    $q = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $PSCommandPath + '"'))
    if ($Paths) { $q += @('-Paths', ('"' + ($Paths -join ';') + '"')) }
    if ($Subject) { $q += @('-Subject', ('"' + $Subject + '"')) }
    if ($TimestampUrl) { $q += @('-TimestampUrl', ('"' + $TimestampUrl + '"')) }
    if ($NoTimestamp) { $q += '-NoTimestamp' }
    if ($ResultFile) { $q += @('-ResultFile', ('"' + $ResultFile + '"')) }
    Remove-Item -LiteralPath $ResultFile -Force -ErrorAction SilentlyContinue
    try {
        $p = Start-Process powershell -ArgumentList $q -Verb RunAs -PassThru -Wait -ErrorAction Stop
    } catch {
        Write-Host ("UAC 提升被拒绝或不可用：{0}" -f $_.Exception.Message) -ForegroundColor Red
        Write-Host '未签名任何产物；请在管理员 PowerShell 窗口中重新执行。' -ForegroundColor Yellow
        exit 2
    }
    if ($null -eq $p) { Write-Host 'UAC 提升已取消；未签名任何产物。' -ForegroundColor Red; exit 2 }
    if (Test-Path -LiteralPath $ResultFile) { Get-Content -LiteralPath $ResultFile -Raw | Out-Host }
    exit $p.ExitCode
}

# ---------- 1. 定位 signtool.exe ----------
function Find-SignTool {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kits) {
        $c = Get-ChildItem $kits -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
             Where-Object { $_.FullName -like '*\x64\signtool.exe' } |
             Sort-Object FullName -Descending | Select-Object -First 1
        if ($c) { return $c.FullName }
    }
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

$signTool = Find-SignTool
if (-not $signTool) { throw '未找到 signtool.exe，请安装 Windows SDK（Windows Kits\10\bin\x64\signtool.exe）。' }

# ---------- 2. 幂等证书：存在则复用，否则创建 ----------
function Get-OrCreate-CodeSigningCert([string]$subj) {
    $existing = Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert -ErrorAction SilentlyContinue |
                Where-Object { $_.Subject -eq $subj -and $_.HasPrivateKey } |
                Sort-Object NotAfter -Descending | Select-Object -First 1
    if ($existing) {
        Write-Host "复用已有签名证书: $($existing.Thumbprint) ($($existing.NotAfter.ToString('yyyy-MM-dd')))" -ForegroundColor Cyan
        return $existing
    }
    Write-Host "创建自签代码签名证书: $subj" -ForegroundColor Cyan
    $c = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subj `
        -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 4096 `
        -KeyExportPolicy Exportable -CertStoreLocation Cert:\LocalMachine\My `
        -NotAfter (Get-Date).AddYears(5) `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
    return $c
}

function Add-ToStoreIfMissing($cert, [string]$storePath) {
    $present = Get-ChildItem $storePath -ErrorAction SilentlyContinue |
               Where-Object { $_.Thumbprint -eq $cert.Thumbprint } | Select-Object -First 1
    if ($present) {
        Write-Host "已在 ${storePath}: $($cert.Thumbprint)" -ForegroundColor DarkGray
        return
    }
    $tmp = Join-Path $env:TEMP ("clashwin-sign-" + [guid]::NewGuid().ToString('N') + '.cer')
    Export-Certificate -Cert $cert -FilePath $tmp -Type CERT | Out-Null
    Import-Certificate -FilePath $tmp -CertStoreLocation $storePath | Out-Null
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    Write-Host "已安装到 ${storePath}: $($cert.Thumbprint)" -ForegroundColor Green
}

$cert = Get-OrCreate-CodeSigningCert $Subject
Add-ToStoreIfMissing $cert 'Cert:\LocalMachine\Root'
Add-ToStoreIfMissing $cert 'Cert:\LocalMachine\TrustedPublisher'

# ---------- 3. 待签名产物 ----------
function Get-DefaultPaths {
    $bin = Join-Path $RepoRoot 'build\bin\Release'
    $list = @()
    if (Test-Path $bin) {
        # 注意：Get-ChildItem -Include 只在 -Path 含通配符或配合 -Recurse 时生效，
        # 这里用 -Filter 明确枚举 *.exe/*.dll。
        $list += Get-ChildItem $bin -File -Filter '*.exe' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
        $sidecar = Join-Path $bin 'sidecar'
        if (Test-Path $sidecar) {
            $list += Get-ChildItem $sidecar -File -ErrorAction SilentlyContinue |
                     Where-Object { $_.Extension -in '.exe', '.dll' } |
                     ForEach-Object { $_.FullName }
        }
    }
    $dist = Join-Path $RepoRoot 'dist'
    if (Test-Path $dist) {
        $list += Get-ChildItem $dist -File -Filter '*setup.exe' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
    }
    return $list
}

if (-not $Paths -or $Paths.Count -eq 0) {
    $targets = Get-DefaultPaths
} else {
    $targets = @()
    foreach ($p in $Paths) {
        foreach ($one in ($p -split ';')) {
            if (-not $one) { continue }
            $full = if (Test-Path -LiteralPath $one) { (Resolve-Path -LiteralPath $one).Path } else { Join-Path $RepoRoot $one }
            if (Test-Path -LiteralPath $full) { $targets += $full } else { Write-Warning "跳过不存在的产物: $one" }
        }
    }
}

if ($targets.Count -eq 0) { throw '没有可签名的产物（检查 build\bin\Release 或传入 -Paths）。' }

# ---------- 4. 签名 ----------
$signed = @()
$failed = @()
foreach ($f in ($targets | Select-Object -Unique)) {
    Write-Host "签名: $f" -ForegroundColor White
    if (Test-Path -LiteralPath $f) {
        # 已被进程占用时 signtool 会失败 → 记录并继续，不让整条构建链卡死
        $attemptArgs = @('sign', '/sm', '/sha1', $cert.Thumbprint, '/fd', 'sha256')
        if (-not $NoTimestamp) { $attemptArgs += @('/tr', $TimestampUrl, '/td', 'sha256') }
        $attemptArgs += $f
        & $signTool @attemptArgs | Out-Host
        $rc = $LASTEXITCODE
        if ($rc -ne 0 -and -not $NoTimestamp) {
            Write-Warning "带时间戳签名失败(rc=$rc)，重试不带时间戳..."
            & $signTool sign /sm /sha1 $cert.Thumbprint /fd sha256 $f | Out-Host
            $rc = $LASTEXITCODE
        }
        if ($rc -eq 0) { $signed += $f } else { $failed += "$f (signtool rc=$rc)" }
    }
}

# ---------- 5. 验证 ----------
Write-Host ""
Write-Host "=== 验证 Authenticode 签名 ===" -ForegroundColor Cyan
$results = foreach ($f in ($signed | Select-Object -Unique)) {
    $sig = Get-AuthenticodeSignature -LiteralPath $f
    [pscustomobject]@{
        File     = $f
        Status   = $sig.Status
        Subject  = $sig.SignerCertificate.Subject
        Thumb    = $sig.SignerCertificate.Thumbprint
        Timestamp= if ($sig.TimeStamperCertificate) { 'yes' } else { 'no' }
    }
}
$results | Format-Table -AutoSize | Out-Host

$bad = $results | Where-Object { $_.Status -ne 'Valid' }

# 结果落盘（提权的子进程用它向父进程回传证据）
$summary = @(
    "CERT_SUBJECT=$($cert.Subject)",
    "CERT_THUMBPRINT=$($cert.Thumbprint)",
    "CERT_NOTAFTER=$($cert.NotAfter.ToString('yyyy-MM-dd'))"
)
$results | ForEach-Object {
    $summary += ("FILE={0} STATUS={1} SUBJECT={2} TIMESTAMP={3}" -f $_.File, $_.Status, $_.Subject, $_.Timestamp)
}
$failed | ForEach-Object { $summary += "FAILED=$_" }
$summary | Set-Content -LiteralPath $ResultFile -Encoding utf8
Write-Host ""
Write-Host ("证书指纹: {0}" -f $cert.Thumbprint) -ForegroundColor Green
Write-Host ("证书主体: {0}（有效期至 {1:yyyy-MM-dd}）" -f $cert.Subject, $cert.NotAfter) -ForegroundColor Green
if ($failed.Count -gt 0) {
    Write-Host ("签名失败 {0} 项:" -f $failed.Count) -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
}
if ($bad.Count -gt 0) {
    Write-Host ("验证未通过 {0} 项:" -f ($bad.Count)) -ForegroundColor Red
    $bad | ForEach-Object { Write-Host "  $($_.File) => $($_.Status)" -ForegroundColor Red }
    exit 2
}
if ($failed.Count -gt 0) { exit 3 }
Write-Host "全部产物签名有效（Status=Valid）。" -ForegroundColor Green
exit 0
