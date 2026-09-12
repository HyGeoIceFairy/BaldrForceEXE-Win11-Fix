[CmdletBinding()]
param(
    [string]$Compiler = 'gcc.exe'
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $RepositoryRoot 'build'
$TestExecutablePath = Join-Path $BuildDirectory 'config-tests.exe'

New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null

$CompilerCommand = Get-Command $Compiler -ErrorAction Stop
$CompilerPath = $CompilerCommand.Source
$ToolchainDirectory = Split-Path -Parent $CompilerPath
$env:Path = "$ToolchainDirectory;$env:Path"

& $CompilerPath `
    '-std=c11' `
    '-Wall' `
    '-Wextra' `
    '-Wpedantic' `
    '-Werror' `
    '-Wl,--no-insert-timestamp' `
    '-o' $TestExecutablePath `
    (Join-Path $RepositoryRoot 'tests\config_tests.c') `
    (Join-Path $RepositoryRoot 'src\launcher_config.c')

if ($LASTEXITCODE -ne 0) {
    throw "Test build failed with exit code $LASTEXITCODE."
}

& $TestExecutablePath
if ($LASTEXITCODE -ne 0) {
    throw "Configuration tests failed with exit code $LASTEXITCODE."
}
