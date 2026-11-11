# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$ReleaseDirectory,
    [string]$VersionFile,
    [string]$RepositoryUrl = "https://github.com/Kea128/HW_RosPlotJugglerStudio",
    [switch]$PruneStaleArchives,
    [switch]$Run
)

$ErrorActionPreference = "Stop"
# Windows PowerShell redraws its progress bar for every downloaded block, which makes a
# 300+ MiB release download many times slower.
$ProgressPreference = "SilentlyContinue"
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
    "User-Agent" = "RosPlotJugglerStudio-release-sync"
}
# Public asset URLs avoid the GitHub API, whose anonymous rate limit is shared by everyone
# behind the same proxy; draft releases have no public assets, so they still fail here.
$DownloadBaseUrl = "https://github.com/$RepositoryPath/releases/download/$Tag"
Write-Host "Resolving $Tag from $RepositoryUrl"

$ArchiveName = "RosPlotJugglerStudio-$Version-windows-x86_64.zip"
$RequiredAssetNames = @(
    $ArchiveName,
    "$ArchiveName.sha256",
    "SHA256SUMS",
    $ManifestName
)

New-Item -ItemType Directory -Path $ReleaseDirectory -Force | Out-Null
foreach ($AssetName in $RequiredAssetNames) {
    $Destination = Join-Path $ReleaseDirectory $AssetName
    Write-Host "Downloading $AssetName"
    try {
        Invoke-WebRequest -Uri "$DownloadBaseUrl/$AssetName" `
            -Headers $Headers -UseBasicParsing -OutFile $Destination
    }
    catch {
        throw "Public GitHub Release '$Tag' does not provide '$AssetName': $($_.Exception.Message)"
    }
}

$ArchivePath = Join-Path $ReleaseDirectory $ArchiveName
$ManifestPath = Join-Path $ReleaseDirectory $ManifestName
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ([string]$Manifest.version -cne $Version) {
    throw "Downloaded manifest version '$($Manifest.version)' does not match '$Version'."
}
if ([long]$Manifest.size -ne (Get-Item -LiteralPath $ArchivePath).Length) {
    throw "Archive size does not match the manifest size $($Manifest.size)."
}
$ExpectedHash = ([string]$Manifest.sha256).ToLowerInvariant()
$ActualHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualHash -cne $ExpectedHash) {
    throw "Archive SHA-256 mismatch. Expected $ExpectedHash, got $ActualHash."
}

function Invoke-WithRetry([scriptblock]$Action) {
    # Security scanners briefly lock freshly extracted executables, so moving their
    # directory can fail with "access denied" for a few seconds.
    for ($Attempt = 1; ; $Attempt++) {
        try {
            & $Action
            return
        }
        catch {
            if ($Attempt -ge 20) { throw }
            Start-Sleep -Seconds 3
        }
    }
}

$PortableDirectory = Join-Path $ReleaseDirectory "portable"
$PackageRoot = Join-Path $PortableDirectory "RosPlotJugglerStudio"
$StagingDirectory = Join-Path $ReleaseDirectory ".sync-$Version-$([Guid]::NewGuid().ToString('N'))"
$StagedPackageRoot = Join-Path $StagingDirectory "RosPlotJugglerStudio"
$BackupRoot = "$PackageRoot.previous"

try {
    Write-Host "Extracting verified portable package"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($ArchivePath, $StagingDirectory)
    $StagedExecutable = Join-Path $StagedPackageRoot "bin\RosPlotJugglerStudio.exe"
    if (-not (Test-Path -LiteralPath $StagedExecutable -PathType Leaf)) {
        throw "Portable archive does not contain RosPlotJugglerStudio/bin/RosPlotJugglerStudio.exe."
    }

    New-Item -ItemType Directory -Path $PortableDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $BackupRoot) {
        Remove-Item -LiteralPath $BackupRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $PackageRoot) {
        Invoke-WithRetry { Move-Item -LiteralPath $PackageRoot -Destination $BackupRoot }
    }
    try {
        Invoke-WithRetry { Move-Item -LiteralPath $StagedPackageRoot -Destination $PackageRoot }
    }
    catch {
        if ((Test-Path -LiteralPath $BackupRoot) -and -not (Test-Path -LiteralPath $PackageRoot)) {
            Invoke-WithRetry { Move-Item -LiteralPath $BackupRoot -Destination $PackageRoot }
        }
        throw
    }
    if (Test-Path -LiteralPath $BackupRoot) {
        Invoke-WithRetry { Remove-Item -LiteralPath $BackupRoot -Recurse -Force }
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

$StaleArtifacts = @(Get-ChildItem -LiteralPath $ReleaseDirectory -File | Where-Object {
        ($_.Name -like "RosPlotJugglerStudio-*-windows-x86_64.zip*" -and
         $_.Name -cne $ArchiveName -and $_.Name -cne "$ArchiveName.sha256") -or
        ($_.Name -like "update-manifest-*.json" -and $_.Name -cne $ManifestName)
    })
if ($StaleArtifacts.Count -gt 0) {
    $StaleBytes = ($StaleArtifacts | Measure-Object -Property Length -Sum).Sum
    if ($PruneStaleArchives) {
        $StaleArtifacts | Remove-Item -Force
        Write-Host ("Removed {0} stale release file(s), {1:N0} MiB." -f $StaleArtifacts.Count, ($StaleBytes / 1MB))
    }
    else {
        Write-Host ("{0} stale release file(s) from other versions use {1:N0} MiB; rerun with -PruneStaleArchives to remove them." -f
            $StaleArtifacts.Count, ($StaleBytes / 1MB))
    }
}

Write-Host "Local portable release is ready:"
Write-Host "  $Executable"
Write-Host "  SHA-256: $ActualHash"
if ($Run) {
    Start-Process -FilePath $Executable -WorkingDirectory (Split-Path -Parent $Executable)
}
