[CmdletBinding()]
param(
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version = '1.0.0'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$distDirectory = Join-Path $repoRoot 'dist'
$packageName = "BaldrForceEXE-Win11-Fix-v$Version"
$stageDirectory = Join-Path $distDirectory $packageName
$archivePath = Join-Path $distDirectory "$packageName.zip"
$archiveChecksumPath = "$archivePath.sha256"

$allowList = [ordered]@{
    'Start-BaldrForce.exe'     = 'Start-BaldrForce.exe'
    'ddraw.dll'                = 'third_party\dgVoodoo2\ddraw.dll'
    'dgVoodoo.conf'            = 'config\dgVoodoo.conf'
    'USER_GUIDE.zh-CN.txt'     = 'docs\USER_GUIDE.zh-CN.txt'
    'LICENSE.txt'              = 'LICENSE'
    'THIRD_PARTY_NOTICES.txt'  = 'THIRD_PARTY_NOTICES.md'
}

if (Test-Path -LiteralPath $stageDirectory) {
    Remove-Item -LiteralPath $stageDirectory -Recurse -Force
}
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}
if (Test-Path -LiteralPath $archiveChecksumPath) {
    Remove-Item -LiteralPath $archiveChecksumPath -Force
}
New-Item -ItemType Directory -Force -Path $stageDirectory | Out-Null

foreach ($entry in $allowList.GetEnumerator()) {
    $source = Join-Path $repoRoot $entry.Value
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing release file: $($entry.Value)"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $stageDirectory $entry.Key)
}

$hashLines = foreach ($name in $allowList.Keys) {
    $path = Join-Path $stageDirectory $name
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    "$hash *$name"
}
$hashLines | Set-Content -LiteralPath (Join-Path $stageDirectory 'SHA256SUMS.txt') -Encoding ASCII

$actualNames = @(Get-ChildItem -LiteralPath $stageDirectory -File | Select-Object -ExpandProperty Name | Sort-Object)
$expectedNames = @(@($allowList.Keys) + 'SHA256SUMS.txt') | Sort-Object
$differences = @(Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualNames)
if ($differences.Count -ne 0) {
    throw 'The staging directory does not match the release allowlist.'
}

Compress-Archive -Path (Join-Path $stageDirectory '*') -DestinationPath $archivePath -CompressionLevel Optimal

$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
"$archiveHash *$packageName.zip" |
    Set-Content -LiteralPath $archiveChecksumPath -Encoding ASCII
Write-Host "Packaged: $archivePath"
Write-Host "SHA-256: $archiveHash"
