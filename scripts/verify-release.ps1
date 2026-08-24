# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$ReleaseDirectory,
    [string]$ManifestPath,
    [string]$ArchivePath,
    [string]$PackageRoot,
    [string]$VersionFile,
    [string]$Tag,
    [string]$RepositoryUrl = "https://github.com/Kea128/HW_RosPlotJugglerStudio"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ReleaseDirectory) { $ReleaseDirectory = Join-Path $ProjectRoot "release" }
if (-not $VersionFile) { $VersionFile = Join-Path $ProjectRoot "src\PlotJuggler\STUDIO_VERSION" }
if (-not $ManifestPath) {
    $CandidateManifests = @(Get-ChildItem -LiteralPath $ReleaseDirectory `
        -Filter "update-manifest-*.json" -File)
    if ($CandidateManifests.Count -ne 1) {
        throw "Expected exactly one update manifest in $ReleaseDirectory; found $($CandidateManifests.Count)."
    }
    $ManifestPath = $CandidateManifests[0].FullName
}

foreach ($RequiredPath in @($VersionFile, $ManifestPath)) {
    if (-not (Test-Path -LiteralPath $RequiredPath -PathType Leaf)) {
        throw "Release verification prerequisite is missing: $RequiredPath"
    }
}

$Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if (-not $Tag) { $Tag = $env:GITHUB_REF_NAME }
if (-not $Tag) { $Tag = "v$Version" }
$ExpectedTag = "v$Version"
if ($Tag -cne $ExpectedTag) {
    throw "Release tag '$Tag' must exactly match STUDIO_VERSION as '$ExpectedTag'."
}

$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$RequiredManifestProperties = @(
    "schemaVersion", "version", "platform", "arch", "channel", "publishedAt",
    "minimumSupportedVersion", "assetUrl", "releaseNotesUrl", "size", "sha256"
)
foreach ($Property in $RequiredManifestProperties) {
    if ($null -eq $Manifest.PSObject.Properties[$Property]) {
        throw "Update manifest is missing required property '$Property'."
    }
}
if ([int]$Manifest.schemaVersion -ne 1) { throw "Unsupported update manifest schema." }
if ([string]$Manifest.version -cne $Version) {
    throw "Manifest version '$($Manifest.version)' does not match STUDIO_VERSION '$Version'."
}
if ([string]$Manifest.platform -cne "windows" -or [string]$Manifest.arch -cne "x86_64") {
    throw "Manifest platform/arch must be windows/x86_64."
}
if ([string]$Manifest.channel -notin @("stable", "beta")) {
    throw "Manifest channel must be stable or beta."
}
if ([string]$Manifest.sha256 -cnotmatch '^[0-9a-f]{64}$') {
    throw "Manifest SHA-256 must contain 64 lowercase hexadecimal characters."
}

$AssetName = [IO.Path]::GetFileName(([Uri][string]$Manifest.assetUrl).AbsolutePath)
if (-not $ArchivePath) { $ArchivePath = Join-Path $ReleaseDirectory $AssetName }
if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    throw "Release archive is missing: $ArchivePath"
}
$Archive = Get-Item -LiteralPath $ArchivePath
if ($Archive.Name -cne $AssetName) {
    throw "Manifest asset name '$AssetName' does not match archive '$($Archive.Name)'."
}

$ExpectedAssetUrl = "$($RepositoryUrl.TrimEnd('/'))/releases/download/$ExpectedTag/$($Archive.Name)"
$ExpectedNotesUrl = "$($RepositoryUrl.TrimEnd('/'))/releases/tag/$ExpectedTag"
if ([string]$Manifest.assetUrl -cne $ExpectedAssetUrl) {
    throw "Manifest assetUrl must be '$ExpectedAssetUrl'."
}
if ([string]$Manifest.releaseNotesUrl -cne $ExpectedNotesUrl) {
    throw "Manifest releaseNotesUrl must be '$ExpectedNotesUrl'."
}
if ([long]$Manifest.size -ne $Archive.Length) {
    throw "Archive size $($Archive.Length) does not match manifest size $($Manifest.size)."
}
$ArchiveHash = (Get-FileHash -LiteralPath $Archive.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ArchiveHash -cne [string]$Manifest.sha256) {
    throw "Archive SHA-256 '$ArchiveHash' does not match manifest '$($Manifest.sha256)'."
}

foreach ($ChecksumPath in @("$($Archive.FullName).sha256", (Join-Path $ReleaseDirectory "SHA256SUMS"))) {
    if (Test-Path -LiteralPath $ChecksumPath -PathType Leaf) {
        $ExpectedChecksumLine = "$ArchiveHash  $($Archive.Name)"
        $ActualChecksumLine = (Get-Content -LiteralPath $ChecksumPath -Raw).Trim()
        if ($ActualChecksumLine -cne $ExpectedChecksumLine) {
            throw "Checksum file '$ChecksumPath' does not match the archive."
        }
    }
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Zip = [IO.Compression.ZipFile]::OpenRead($Archive.FullName)
try {
    $MarkerEntries = @($Zip.Entries | Where-Object {
        $_.FullName.Replace("\", "/") -ceq "RosPlotJugglerStudio/manifest.json"
    })
    if ($MarkerEntries.Count -ne 1) {
        throw "Archive must contain exactly one RosPlotJugglerStudio/manifest.json."
    }
    $MarkerEntry = $MarkerEntries[0]
    $Reader = New-Object IO.StreamReader($MarkerEntry.Open())
    try { $Marker = $Reader.ReadToEnd() | ConvertFrom-Json } finally { $Reader.Dispose() }

    if ([int]$Marker.schemaVersion -ne 1 -or
        [string]$Marker.product -cne "RosPlotJugglerStudio" -or
        [string]$Marker.version -cne $Version -or
        [string]$Marker.platform -cne "windows-x86_64" -or
        [string]$Marker.entrypoint -cne "bin/RosPlotJugglerStudio.exe") {
        throw "Package marker identity, version, platform, or entrypoint is invalid."
    }
    $EntrypointPath = "RosPlotJugglerStudio/$($Marker.entrypoint)"
    $EntrypointEntries = @($Zip.Entries | Where-Object {
        $_.FullName.Replace("\", "/") -ceq $EntrypointPath
    })
    if ($EntrypointEntries.Count -ne 1) {
        throw "Archive must contain exactly one marker entrypoint: $EntrypointPath"
    }
    $EntrypointEntry = $EntrypointEntries[0]
    $MarkerFiles = @($Marker.files | Where-Object { $_.path -ceq $Marker.entrypoint })
    if ($MarkerFiles.Count -ne 1 -or [long]$MarkerFiles[0].size -ne $EntrypointEntry.Length) {
        throw "Package marker does not describe the archived entrypoint correctly."
    }
    $EntrypointStream = $EntrypointEntry.Open()
    $Hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $EntrypointHash = ([BitConverter]::ToString(
            $Hasher.ComputeHash($EntrypointStream))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $Hasher.Dispose()
        $EntrypointStream.Dispose()
    }
    if ($EntrypointHash -cne [string]$MarkerFiles[0].sha256) {
        throw "Package marker entrypoint SHA-256 does not match the archived file."
    }
}
finally {
    $Zip.Dispose()
}

if ($PackageRoot) {
    $PackageMarkerPath = Join-Path $PackageRoot "manifest.json"
    if (-not (Test-Path -LiteralPath $PackageMarkerPath -PathType Leaf)) {
        throw "Portable tree marker is missing: $PackageMarkerPath"
    }
    $PackageMarker = Get-Content -LiteralPath $PackageMarkerPath -Raw | ConvertFrom-Json
    if ([string]$PackageMarker.version -cne $Version) {
        throw "Portable tree marker version does not match STUDIO_VERSION."
    }
}

Write-Host "Release verification passed: $ExpectedTag, $($Archive.Name), $ArchiveHash"
