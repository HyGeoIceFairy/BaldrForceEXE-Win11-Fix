[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$TestVersion = "0.0.$PID"
$PackageName = "BaldrForceEXE-Win11-Fix-v$TestVersion"
$ArchivePath = Join-Path $RepositoryRoot "dist\$PackageName.zip"
$ChecksumPath = "$ArchivePath.sha256"
$CreatedArchive = $false
$CreatedChecksum = $false

try {
    & (Join-Path $PSScriptRoot 'package.ps1') -Version $TestVersion
    $CreatedArchive = Test-Path -LiteralPath $ArchivePath -PathType Leaf
    $CreatedChecksum = Test-Path -LiteralPath $ChecksumPath -PathType Leaf
    if (!$CreatedArchive -or !$CreatedChecksum) {
        throw 'The package test did not create both release files.'
    }
    $ExternalChecksumLine = (Get-Content -LiteralPath $ChecksumPath -Raw).Trim()
    if ($ExternalChecksumLine -notmatch '^([0-9A-F]{64}) \*(.+\.zip)$') {
        throw 'The external checksum file has an invalid format.'
    }
    $ExternalExpectedHash = $Matches[1]
    $ExternalExpectedName = $Matches[2]
    $ExternalActualHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash
    if ($ExternalExpectedName -ne (Split-Path -Leaf $ArchivePath) -or
        $ExternalExpectedHash -ne $ExternalActualHash) {
        throw 'The external checksum does not match the release archive.'
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $ExpectedNames = @(
        'LICENSE.txt'
        'SHA256SUMS.txt'
        'Start-BaldrForce.exe'
        'THIRD_PARTY_NOTICES.txt'
        'USER_GUIDE.zh-CN.txt'
        'ddraw.dll'
        'dgVoodoo.conf'
    ) | Sort-Object
    $Archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $ActualNames = @($Archive.Entries | ForEach-Object FullName | Sort-Object)
        if (@($ActualNames | Group-Object | Where-Object Count -ne 1).Count -ne 0) {
            throw 'Package test found duplicate ZIP entries.'
        }
        if (@(Compare-Object -ReferenceObject $ExpectedNames -DifferenceObject $ActualNames).Count -ne 0) {
            throw 'Package test found unexpected ZIP entries.'
        }
        $ChecksumEntry = $Archive.GetEntry('SHA256SUMS.txt')
        if ($null -eq $ChecksumEntry) {
            throw 'Package test did not find SHA256SUMS.txt.'
        }
        $ChecksumReader = [System.IO.StreamReader]::new($ChecksumEntry.Open(),
                                                       [System.Text.Encoding]::ASCII)
        try {
            $ChecksumLines = @($ChecksumReader.ReadToEnd() -split "\r?\n" |
                Where-Object { $_ -ne '' })
        }
        finally {
            $ChecksumReader.Dispose()
        }
        $ExpectedHashes = @{}
        foreach ($ChecksumLine in $ChecksumLines) {
            if ($ChecksumLine -notmatch '^([0-9A-F]{64}) \*(.+)$') {
                throw "Invalid SHA256SUMS.txt line: $ChecksumLine"
            }
            $ExpectedHashes[$Matches[2]] = $Matches[1]
        }
        foreach ($EntryName in $ExpectedNames | Where-Object { $_ -ne 'SHA256SUMS.txt' }) {
            $Entry = $Archive.GetEntry($EntryName)
            $EntryStream = $Entry.Open()
            try {
                $Hasher = [System.Security.Cryptography.SHA256]::Create()
                try {
                    $ActualHash = [BitConverter]::ToString($Hasher.ComputeHash($EntryStream)) `
                        -replace '-', ''
                }
                finally {
                    $Hasher.Dispose()
                }
            }
            finally {
                $EntryStream.Dispose()
            }
            if ($ActualHash -ne $ExpectedHashes[$EntryName]) {
                throw "Package checksum mismatch: $EntryName"
            }
        }
        $PackagedLauncherHash = $ExpectedHashes['Start-BaldrForce.exe']
        $BuiltHash = (Get-FileHash -LiteralPath `
            (Join-Path $RepositoryRoot 'build\Start-BaldrForce.exe') -Algorithm SHA256).Hash
        if ($PackagedLauncherHash -ne $BuiltHash) {
            throw 'Package test found a launcher that differs from the current build.'
        }
    }
    finally {
        $Archive.Dispose()
    }

    $RefusedOverwrite = $false
    try {
        & (Join-Path $PSScriptRoot 'package.ps1') -Version $TestVersion
    }
    catch {
        $RefusedOverwrite = $_.Exception.Message -like '*refusing to overwrite*'
    }
    if (!$RefusedOverwrite) {
        throw 'Package test expected an existing archive to be refused.'
    }
    Write-Host 'All package tests passed.'
}
finally {
    if ($CreatedArchive -and (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
        Remove-Item -LiteralPath $ArchivePath -Force
    }
    if ($CreatedChecksum -and (Test-Path -LiteralPath $ChecksumPath -PathType Leaf)) {
        Remove-Item -LiteralPath $ChecksumPath -Force
    }
}
