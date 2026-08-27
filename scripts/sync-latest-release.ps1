# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$ReleaseDirectory,
    [string]$VersionFile,
    [string]$RepositoryUrl = "https://github.com/Kea128/HW_RosPlotJugglerStudio",
    [switch]$Run
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ReleaseDirectory) { $ReleaseDirectory = Join-Path $ProjectRoot "release" }
if (-not $VersionFile) { $VersionFile = Join-Path $ProjectRoot "src\PlotJuggler\STUDIO_VERSION" }

$Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
$Tag = "v$Version"
$Channel = if ($Version -match '(^|[.-])(beta|rc)([.-]|$)') { "beta" } else { "stable" }
$ManifestName = "update-manifest-$Channel.json"
$RepositoryPath = ([Uri]$RepositoryUrl).AbsolutePath.Trim("/").TrimEnd("/")
if ($RepositoryPath.EndsWith(".git", [StringComparison]::OrdinalIgnoreCase)) {
    $RepositoryPath = $RepositoryPath.Substring(0, $RepositoryPath.Length - 4)
}
if ($RepositoryPath.Split("/").Count -ne 2) {
    throw "RepositoryUrl must identify a GitHub owner/repository: $RepositoryUrl"
}

$Headers = @{
    Accept = "application/vnd.github+json"
    "User-Agent" = "RosPlotJugglerStudio-release-sync"
}
$ReleaseApiUrl = "https://api.github.com/repos/$RepositoryPath/releases/tags/$Tag"
Write-Host "Resolving $Tag from $RepositoryUrl"
$Release = Invoke-RestMethod -Uri $ReleaseApiUrl -Headers $Headers
if ([string]$Release.tag_name -cne $Tag -or [bool]$Release.draft) {
    throw "GitHub Release '$Tag' is missing or is still a draft."
}

$ArchiveName = "RosPlotJugglerStudio-$Version-windows-x86_64.zip"
$RequiredAssetNames = @(
    $ArchiveName,
    "$ArchiveName.sha256",
    "SHA256SUMS",
    $ManifestName
)
$Assets = @{}
foreach ($Asset in @($Release.assets)) {
    $Assets[[string]$Asset.name] = $Asset
}
foreach ($AssetName in $RequiredAssetNames) {
    if (-not $Assets.ContainsKey($AssetName)) {
        throw "GitHub Release '$Tag' does not contain required asset '$AssetName'."
    }
}

New-Item -ItemType Directory -Path $ReleaseDirectory -Force | Out-Null
foreach ($AssetName in $RequiredAssetNames) {
    $Destination = Join-Path $ReleaseDirectory $AssetName
    Write-Host "Downloading $AssetName"
    Invoke-WebRequest -Uri ([string]$Assets[$AssetName].browser_download_url) `
        -Headers $Headers -UseBasicParsing -OutFile $Destination
}

$ArchivePath = Join-Path $ReleaseDirectory $ArchiveName
$ManifestPath = Join-Path $ReleaseDirectory $ManifestName
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ([string]$Manifest.version -cne $Version) {
    throw "Downloaded manifest version '$($Manifest.version)' does not match '$Version'."
}
$ExpectedHash = ([string]$Manifest.sha256).ToLowerInvariant()
$ActualHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualHash -cne $ExpectedHash) {
    throw "Archive SHA-256 mismatch. Expected $ExpectedHash, got $ActualHash."
}

$PortableDirectory = Join-Path $ReleaseDirectory "portable"
$PackageRoot = Join-Path $PortableDirectory "RosPlotJugglerStudio"
$StagingDirectory = Join-Path $ReleaseDirectory ".sync-$Version-$([Guid]::NewGuid().ToString('N'))"
$StagedPackageRoot = Join-Path $StagingDirectory "RosPlotJugglerStudio"
$BackupRoot = "$PackageRoot.previous"

try {
    Write-Host "Extracting verified portable package"
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $StagingDirectory -Force
    $StagedExecutable = Join-Path $StagedPackageRoot "bin\RosPlotJugglerStudio.exe"
    if (-not (Test-Path -LiteralPath $StagedExecutable -PathType Leaf)) {
        throw "Portable archive does not contain RosPlotJugglerStudio/bin/RosPlotJugglerStudio.exe."
    }

    New-Item -ItemType Directory -Path $PortableDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $BackupRoot) {
        Remove-Item -LiteralPath $BackupRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $PackageRoot) {
        Move-Item -LiteralPath $PackageRoot -Destination $BackupRoot
    }
    try {
        Move-Item -LiteralPath $StagedPackageRoot -Destination $PackageRoot
    }
    catch {
        if ((Test-Path -LiteralPath $BackupRoot) -and -not (Test-Path -LiteralPath $PackageRoot)) {
            Move-Item -LiteralPath $BackupRoot -Destination $PackageRoot
        }
        throw
    }
    if (Test-Path -LiteralPath $BackupRoot) {
        Remove-Item -LiteralPath $BackupRoot -Recurse -Force
    }
}
finally {
    if (Test-Path -LiteralPath $StagingDirectory) {
        Remove-Item -LiteralPath $StagingDirectory -Recurse -Force
    }
}

$Executable = Join-Path $PackageRoot "bin\RosPlotJugglerStudio.exe"
$LauncherPath = Join-Path $ReleaseDirectory "Run-RosPlotJugglerStudio.cmd"
$Launcher = "@echo off`r`nstart `"`" `"%~dp0portable\RosPlotJugglerStudio\bin\RosPlotJugglerStudio.exe`" %*`r`n"
Set-Content -LiteralPath $LauncherPath -Value $Launcher -Encoding ASCII -NoNewline
Set-Content -LiteralPath (Join-Path $ReleaseDirectory "CURRENT_VERSION") `
    -Value $Version -Encoding ASCII -NoNewline

Write-Host "Local portable release is ready:"
Write-Host "  $Executable"
Write-Host "  SHA-256: $ActualHash"
if ($Run) {
    Start-Process -FilePath $Executable -WorkingDirectory (Split-Path -Parent $Executable)
}
