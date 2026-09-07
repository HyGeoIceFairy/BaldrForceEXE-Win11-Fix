[CmdletBinding()]
param(
    [string]$Compiler = 'gcc.exe'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot 'src\Start-BaldrForce.c'
$buildDirectory = Join-Path $repoRoot 'build'
$output = Join-Path $buildDirectory 'Start-BaldrForce.exe'

New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null

& $Compiler `
    '-std=c11' `
    '-Os' `
    '-Wall' `
    '-Wextra' `
    '-municode' `
    '-mwindows' `
    '-Wl,--no-insert-timestamp' `
    '-s' `
    '-o' $output `
    $source

if ($LASTEXITCODE -ne 0) {
    throw "GCC build failed with exit code $LASTEXITCODE."
}

$bytes = [System.IO.File]::ReadAllBytes($output)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
if ($machine -ne 0x8664) {
    throw ('Unexpected PE machine type: 0x{0:X4}' -f $machine)
}

$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
Write-Host "Built: $output"
Write-Host "SHA-256: $hash"
