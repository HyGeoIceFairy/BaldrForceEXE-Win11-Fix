[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$DistDirectory = Join-Path $RepositoryRoot 'dist'
$PackageName = "BaldrForceEXE-Win11-Fix-v$Version"
$ArchivePath = Join-Path $DistDirectory "$PackageName.zip"
$ArchiveChecksumPath = "$ArchivePath.sha256"
$StagingDirectory = Join-Path $DistDirectory ".staging-$PackageName-$([guid]::NewGuid().ToString('N'))"

$AllowList = [ordered]@{
    'Start-BaldrForce.exe'     = 'build\Start-BaldrForce.exe'
    'ddraw.dll'                = 'third_party\dgVoodoo2\ddraw.dll'
    'dgVoodoo.conf'            = 'config\dgVoodoo.conf'
    'USER_GUIDE.zh-CN.txt'     = 'docs\USER_GUIDE.zh-CN.txt'
    'LICENSE.txt'              = 'LICENSE'
    'THIRD_PARTY_NOTICES.txt'  = 'THIRD_PARTY_NOTICES.md'
}

if (!(Test-Path -LiteralPath $DistDirectory)) {
    New-Item -ItemType Directory -Path $DistDirectory | Out-Null
}
$ResolvedDistDirectory = (Resolve-Path -LiteralPath $DistDirectory).Path
$DistAttributes = (Get-Item -LiteralPath $ResolvedDistDirectory -Force).Attributes
if ($ResolvedDistDirectory -eq $RepositoryRoot -or
    [System.IO.Path]::GetPathRoot($ResolvedDistDirectory) -eq $ResolvedDistDirectory -or
    ($DistAttributes -band [System.IO.FileAttributes]::ReparsePoint)) {
    throw "Unsafe distribution directory: $ResolvedDistDirectory"
}
$DistributionPrefix = $ResolvedDistDirectory + [System.IO.Path]::DirectorySeparatorChar
$ResolvedArchivePath = [System.IO.Path]::GetFullPath($ArchivePath)
$ResolvedArchiveChecksumPath = [System.IO.Path]::GetFullPath($ArchiveChecksumPath)
$ResolvedStagingDirectory = [System.IO.Path]::GetFullPath($StagingDirectory)
foreach ($CandidatePath in @($ResolvedArchivePath, $ResolvedArchiveChecksumPath,
                              $ResolvedStagingDirectory)) {
    if (!$CandidatePath.StartsWith($DistributionPrefix,
                                   [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe package output path: $CandidatePath"
    }
}
$ArchivePath = $ResolvedArchivePath
$ArchiveChecksumPath = $ResolvedArchiveChecksumPath
$StagingDirectory = $ResolvedStagingDirectory
if (Test-Path -LiteralPath $ArchivePath) {
    throw "Release archive already exists; refusing to overwrite: $ArchivePath"
}
if (Test-Path -LiteralPath $ArchiveChecksumPath) {
    throw "Release checksum already exists; refusing to overwrite: $ArchiveChecksumPath"
}

New-Item -ItemType Directory -Path $StagingDirectory | Out-Null

try {
    foreach ($Entry in $AllowList.GetEnumerator()) {
        $SourcePath = Join-Path $RepositoryRoot $Entry.Value
        if (!(Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
            throw "Missing release file: $($Entry.Value)"
        }
        Copy-Item -LiteralPath $SourcePath -Destination (Join-Path $StagingDirectory $Entry.Key)
    }

    $LauncherPath = Join-Path $StagingDirectory 'Start-BaldrForce.exe'
    $LauncherBytes = [System.IO.File]::ReadAllBytes($LauncherPath)
    if ($LauncherBytes.Length -lt 256) {
        throw 'Built launcher is unexpectedly small.'
    }
    $PeOffset = [BitConverter]::ToInt32($LauncherBytes, 0x3c)
    if ($PeOffset -lt 0x40 -or $PeOffset -gt ($LauncherBytes.Length - 6) -or
        $LauncherBytes[$PeOffset] -ne 0x50 -or
        $LauncherBytes[$PeOffset + 1] -ne 0x45 -or
        $LauncherBytes[$PeOffset + 2] -ne 0 -or
        $LauncherBytes[$PeOffset + 3] -ne 0) {
        throw 'Built launcher has an invalid PE header.'
    }
    $MachineType = [BitConverter]::ToUInt16($LauncherBytes, $PeOffset + 4)
    if ($MachineType -ne 0x8664) {
        throw ('Unexpected launcher PE machine type: 0x{0:X4}' -f $MachineType)
    }

    $HashLines = foreach ($Name in $AllowList.Keys) {
        $FilePath = Join-Path $StagingDirectory $Name
        $FileHash = (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash
        "$FileHash *$Name"
    }
    $HashLines | Set-Content -LiteralPath (Join-Path $StagingDirectory 'SHA256SUMS.txt') -Encoding ASCII

    $ExpectedNames = @(@($AllowList.Keys) + 'SHA256SUMS.txt') | Sort-Object
    $ActualNames = @(Get-ChildItem -LiteralPath $StagingDirectory -File |
        Select-Object -ExpandProperty Name | Sort-Object)
    $Differences = @(Compare-Object -ReferenceObject $ExpectedNames -DifferenceObject $ActualNames)
    if ($Differences.Count -ne 0) {
        throw 'The staging directory does not match the release allowlist.'
    }
    if ((Get-ChildItem -LiteralPath $StagingDirectory -Directory).Count -ne 0) {
        throw 'The staging directory unexpectedly contains a subdirectory.'
    }

    $StagedFiles = @(Get-ChildItem -LiteralPath $StagingDirectory -File)
    Compress-Archive -LiteralPath $StagedFiles.FullName `
        -DestinationPath $ArchivePath -CompressionLevel Optimal

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $Archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $ArchiveNames = @($Archive.Entries | ForEach-Object FullName | Sort-Object)
        if (@($ArchiveNames | Group-Object | Where-Object Count -ne 1).Count -ne 0) {
            throw 'The ZIP contains duplicate entry names.'
        }
        $ArchiveHashes = @{}
        foreach ($ArchiveEntry in $Archive.Entries) {
            if ($ArchiveEntry.FullName -eq 'SHA256SUMS.txt') {
                continue
            }
            $EntryStream = $ArchiveEntry.Open()
            try {
                $Hasher = [System.Security.Cryptography.SHA256]::Create()
                try {
                    $ArchiveHashes[$ArchiveEntry.FullName] = `
                        ([BitConverter]::ToString($Hasher.ComputeHash($EntryStream)) -replace '-', '')
                }
                finally {
                    $Hasher.Dispose()
                }
            }
            finally {
                $EntryStream.Dispose()
            }
        }
        $ChecksumEntry = $Archive.GetEntry('SHA256SUMS.txt')
        if ($null -eq $ChecksumEntry) {
            throw 'The ZIP is missing SHA256SUMS.txt.'
        }
        $ChecksumReader = [System.IO.StreamReader]::new($ChecksumEntry.Open(),
                                                       [System.Text.Encoding]::ASCII)
        try {
            $ArchivedChecksumLines = @($ChecksumReader.ReadToEnd() -split "\r?\n" |
                Where-Object { $_ -ne '' } | Sort-Object)
        }
        finally {
            $ChecksumReader.Dispose()
        }
    }
    finally {
        $Archive.Dispose()
    }
    $ArchiveDifferences = @(Compare-Object -ReferenceObject $ExpectedNames -DifferenceObject $ArchiveNames)
    if ($ArchiveDifferences.Count -ne 0) {
        throw 'The ZIP entries do not match the release allowlist.'
    }
    $ExpectedChecksumLines = @($AllowList.Keys | ForEach-Object {
        "$($ArchiveHashes[$_]) *$_"
    } | Sort-Object)
    if (@(Compare-Object -ReferenceObject $ExpectedChecksumLines `
            -DifferenceObject $ArchivedChecksumLines).Count -ne 0) {
        throw 'The archived files do not match SHA256SUMS.txt.'
    }

    $ArchiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash
    "$ArchiveHash *$PackageName.zip" |
        Set-Content -LiteralPath $ArchiveChecksumPath -Encoding ASCII
    Write-Host "Packaged: $ArchivePath"
    Write-Host "SHA-256: $ArchiveHash"
}
catch {
    if (Test-Path -LiteralPath $ArchivePath -PathType Leaf) {
        Remove-Item -LiteralPath $ArchivePath -Force
    }
    if (Test-Path -LiteralPath $ArchiveChecksumPath -PathType Leaf) {
        Remove-Item -LiteralPath $ArchiveChecksumPath -Force
    }
    throw
}
finally {
    if (Test-Path -LiteralPath $StagingDirectory -PathType Container) {
        Get-ChildItem -LiteralPath $StagingDirectory -File |
            ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
        Remove-Item -LiteralPath $StagingDirectory
    }
}
