<#
  Read-only integrity check for the 5 upstream sidecar binaries in sidecar/.
  Does NOT download / copy / replace / modify any file.
  Baseline spec: docs/05-sidecar-内核资产.md

  Usage:
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_sidecar.ps1

  Exit code: 0 = 5/5 match baseline; 1 = missing or mismatched (do NOT build/test/release).
#>
param(
  [string]$SidecarDir = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($SidecarDir)) {
  $SidecarDir = Join-Path -Path $PSScriptRoot -ChildPath '..\sidecar'
}

$expected = @(
  [pscustomobject]@{ Name = 'clash-verge-service-install.exe';    Bytes = 175616;   Sha256 = 'EA73082094D12A0782389667B9A608CEED3814DF442EED25BA9B56820D0A1A1D' },
  [pscustomobject]@{ Name = 'clash-verge-service-uninstall.exe';  Bytes = 155648;   Sha256 = '1CDFCFC3CAB2F26ED4E62FBCE128BC58B6D5C2A3D435BFC8042A5051E288350F' },
  [pscustomobject]@{ Name = 'clash-verge-service.exe';            Bytes = 1778176;  Sha256 = '92AD792ABB91BD84B89894E63A8251434A631A5803D9D043C18F7A5A7961E9D2' },
  [pscustomobject]@{ Name = 'enableLoopback.exe';                  Bytes = 84040;    Sha256 = 'F0C328376BD5AE0EF3B0EB19AC1F343C24A9ABFE7BC1008D3B2F6EF3573D04D1' },
  [pscustomobject]@{ Name = 'win-mihomo.exe';                      Bytes = 61247488; Sha256 = '1CDE41CFFFD3DE0AEDAD63C0A8515E1CE5EEA507E8133AED0F637D316C93800A' }
)

if (-not (Test-Path -LiteralPath $SidecarDir -PathType Container)) {
  Write-Host "[FAIL] sidecar directory not found: $SidecarDir"
  exit 1
}

$root = (Resolve-Path -LiteralPath $SidecarDir).Path
Write-Host "sidecar: $root"
Write-Host ''

$fail = 0
foreach ($e in $expected) {
  $path = Join-Path -Path $root -ChildPath $e.Name

  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    Write-Host ("[MISSING] {0}" -f $e.Name)
    $fail++
    continue
  }

  $item = Get-Item -LiteralPath $path
  $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
  $sizeOk = ($item.Length -eq $e.Bytes)
  $hashOk = ($hash -eq $e.Sha256)

  if ($sizeOk -and $hashOk) {
    Write-Host ("[OK]      {0}  {1} B" -f $e.Name, $item.Length)
  }
  else {
    Write-Host ("[FAIL]    {0}" -f $e.Name)
    if (-not $sizeOk) {
      Write-Host ("          size   expected={0} actual={1}" -f $e.Bytes, $item.Length)
    }
    if (-not $hashOk) {
      Write-Host ("          sha256 expected={0}" -f $e.Sha256)
      Write-Host ("                 actual={0}" -f $hash)
    }
    $fail++
  }
}

Write-Host ''
if ($fail -gt 0) {
  Write-Host ("FAIL: {0}/{1} items mismatch baseline, do NOT build or release." -f $fail, $expected.Count)
  exit 1
}

Write-Host ("PASS: {0}/{1} items match baseline, sidecar assets unchanged." -f $expected.Count, $expected.Count)
exit 0
