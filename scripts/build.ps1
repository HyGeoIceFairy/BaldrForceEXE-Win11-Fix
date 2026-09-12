[CmdletBinding()]
param(
    [string]$Compiler = 'gcc.exe',
    [switch]$KeepDebugSymbols
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $RepositoryRoot 'build'
$OutputPath = Join-Path $BuildDirectory 'Start-BaldrForce.exe'
$ResourceObjectPath = Join-Path $BuildDirectory 'launcher-resources.o'

New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null

$CompilerCommand = Get-Command $Compiler -ErrorAction Stop
$CompilerPath = $CompilerCommand.Source
$ToolchainDirectory = Split-Path -Parent $CompilerPath
$WindresPath = Join-Path $ToolchainDirectory 'windres.exe'
if (!(Test-Path -LiteralPath $WindresPath -PathType Leaf)) {
    throw "windres.exe was not found next to the compiler: $WindresPath"
}

# GCC invokes companion tools such as as.exe and ld.exe by name.
$env:Path = "$ToolchainDirectory;$env:Path"

& $WindresPath `
    '--codepage=65001' `
    '-I' (Join-Path $RepositoryRoot 'src') `
    '-i' (Join-Path $RepositoryRoot 'src\launcher.rc') `
    '-o' $ResourceObjectPath

if ($LASTEXITCODE -ne 0) {
    throw "Resource build failed with exit code $LASTEXITCODE."
}

$LinkerOptions = @('-Wl,--no-insert-timestamp')
if (!$KeepDebugSymbols) {
    $LinkerOptions += '-s'
}

& $CompilerPath `
    '-std=c11' `
    '-Os' `
    '-Wall' `
    '-Wextra' `
    '-Wpedantic' `
    '-Werror' `
    '-municode' `
    '-mwindows' `
    @LinkerOptions `
    '-o' $OutputPath `
    (Join-Path $RepositoryRoot 'src\Start-BaldrForce.c') `
    (Join-Path $RepositoryRoot 'src\launcher_config.c') `
    (Join-Path $RepositoryRoot 'src\launcher_ui.c') `
    $ResourceObjectPath `
    '-lshell32' `
    '-lcomctl32' `
    '-lversion'

if ($LASTEXITCODE -ne 0) {
    throw "GCC build failed with exit code $LASTEXITCODE."
}

$ExecutableBytes = [System.IO.File]::ReadAllBytes($OutputPath)
$PeOffset = [BitConverter]::ToInt32($ExecutableBytes, 0x3c)
$MachineType = [BitConverter]::ToUInt16($ExecutableBytes, $PeOffset + 4)
if ($MachineType -ne 0x8664) {
    throw ('Unexpected PE machine type: 0x{0:X4}' -f $MachineType)
}

$ExecutableHash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash
Write-Host "Built: $OutputPath"
Write-Host "SHA-256: $ExecutableHash"
