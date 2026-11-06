# 在 NSIS 编译期签名卸载器（!uninstfinalize 钩子专用）
#
# 背景：NSIS 3.11 的 !uninstfinalize 不注入 UNINSTALLER_OUT_FILE、也不追加路径参数，
#       但卸载器 blob 会以 %TEMP%\nstXXXX.tmp 的形式在编译期落盘（与编译输出的
#       “Uninstall code+data” 尺寸一致）。在此文件生成后原地签名，NSIS 随后把它
#       嵌入安装包，安装后的 uninstall.exe 即带有效 Authenticode 签名。
#
# 约束：
#   - 必须由管理员运行（证书私钥在 LocalMachine\My，signtool 需要 /sm）。
#   - 识别规则：%TEMP% 下最近 10 分钟内生成、且大于 -ExpectedMinBytes 的 nst*.tmp。
#     同机并发编译多个 NSIS 工程时可能拿错文件，构建脚本已串行化 makensis 调用。
#
# 用法（由 clashwin.nsi 的 !uninstfinalize 调用）：
#   powershell -File scripts\sign_nsis_uninstaller.ps1 -LogFile %TEMP%\clashwin-uninst-sign.log
[CmdletBinding()]
param(
    [string]$Subject = 'CN=ClashWin',
    [string]$TimestampUrl = 'http://timestamp.digicert.com',
    [int]$ExpectedMinBytes = 20000,
    [int]$MaxAgeMinutes = 10,
    [string]$LogFile = (Join-Path $env:TEMP 'clashwin-uninst-sign.log')
)

$ErrorActionPreference = 'Continue'

function Write-Log([string]$line) {
    $line | Out-File -FilePath $LogFile -Append -Encoding utf8
}

"UNINST_SIGN_START=$(Get-Date -Format o)" | Out-File -FilePath $LogFile -Encoding utf8

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
if (-not $signTool) {
    Write-Log 'UNINST_SIGN_ERROR=signtool-not-found'
    exit 1
}

$cert = Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
if (-not $cert) {
    Write-Log "UNINST_SIGN_ERROR=cert-not-found($Subject)"
    exit 1
}

$since = (Get-Date).AddMinutes(-$MaxAgeMinutes)
$blobs = Get-ChildItem $env:TEMP -File -Filter 'nst*.tmp' -ErrorAction SilentlyContinue |
         Where-Object { $_.LastWriteTime -gt $since -and $_.Length -ge $ExpectedMinBytes } |
         Sort-Object LastWriteTime -Descending
if (-not $blobs -or $blobs.Count -eq 0) {
    Write-Log 'UNINST_SIGN_ERROR=uninstaller-blob-not-found'
    exit 1
}

$blob = $blobs | Select-Object -First 1
Write-Log "UNINST_SIGN_BLOB=$($blob.FullName) SIZE=$($blob.Length)"

& $signTool sign /sm /sha1 $cert.Thumbprint /fd sha256 /tr $TimestampUrl /td sha256 $blob.FullName *>> $LogFile
$rc = $LASTEXITCODE
if ($rc -ne 0) {
    # 时间戳服务不可达时退回无时间戳签名（仍可被本机信任）
    & $signTool sign /sm /sha1 $cert.Thumbprint /fd sha256 $blob.FullName *>> $LogFile
    $rc = $LASTEXITCODE
}
$status = (Get-AuthenticodeSignature -LiteralPath $blob.FullName).Status
Write-Log "UNINST_SIGN_RC=$rc"
Write-Log "UNINST_SIGN_VERIFY=$status"
Write-Log "UNINST_SIGN_SIZE_AFTER=$((Get-Item -LiteralPath $blob.FullName).Length)"
Write-Log "UNINST_SIGN_END=$(Get-Date -Format o)"

if ($rc -eq 0 -and $status -eq 'Valid') { exit 0 }
exit 1
