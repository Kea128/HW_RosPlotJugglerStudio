# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
    [string]$BuildDirectory
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $ProjectRoot "build\windows-ucrt64"
}
$TestRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot "artifacts\test"))
$ExpectedRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot "artifacts")) +
    [IO.Path]::DirectorySeparatorChar + "test"
if (-not $TestRoot.Equals($ExpectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use unexpected updater E2E root: $TestRoot"
}

$BuiltUpdater = Join-Path $BuildDirectory "bin\RosPlotJugglerUpdater.exe"
$BuiltHelper = Join-Path $BuildDirectory "bin\updater_test_health_helper.exe"
$SourceExtractionScript = Join-Path $ProjectRoot "tools\updater\extract-update.ps1"
$UcrtBin = Join-Path $ProjectRoot ".toolchain\msys64\ucrt64\bin"
foreach ($Required in @($BuiltUpdater, $BuiltHelper, $SourceExtractionScript)) {
    if (-not (Test-Path -LiteralPath $Required -PathType Leaf)) {
        throw "Updater E2E prerequisite is missing: $Required"
    }
}
if (-not (Test-Path -LiteralPath $UcrtBin -PathType Container)) {
    throw "Updater E2E runtime directory is missing: $UcrtBin"
}

if (Test-Path -LiteralPath $TestRoot) {
    Remove-Item -LiteralPath $TestRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $TestRoot -Force | Out-Null
$Tools = Join-Path $TestRoot "tools"
New-Item -ItemType Directory -Path $Tools -Force | Out-Null
$Updater = Join-Path $Tools "RosPlotJugglerUpdater.exe"
$ExtractionScript = Join-Path $Tools "extract-update.ps1"
Copy-Item -LiteralPath $BuiltUpdater -Destination $Updater
Copy-Item -LiteralPath $SourceExtractionScript -Destination $ExtractionScript

$TestLocalAppData = Join-Path $TestRoot "localappdata"
New-Item -ItemType Directory -Path $TestLocalAppData -Force | Out-Null
$OriginalLocalAppData = $env:LOCALAPPDATA
$OriginalLaunchMarker = $env:RSPJ_TEST_LAUNCH_MARKER
$OriginalPath = $env:PATH
$env:LOCALAPPDATA = $TestLocalAppData
$env:PATH = "$(Join-Path $BuildDirectory 'bin');$UcrtBin;$env:PATH"
$Utf8NoBom = New-Object Text.UTF8Encoding($false)

function Write-Utf8 {
    param([string]$Path, [string]$Text)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, $Utf8NoBom)
}

function New-InstallTree {
    param(
        [string]$Root,
        [string]$Version,
        [ValidateSet("pass", "fail")][string]$HealthMode
    )
    $Bin = Join-Path $Root "bin"
    $UpdaterDirectory = Join-Path $Bin "updater"
    New-Item -ItemType Directory -Path $UpdaterDirectory -Force | Out-Null
    Copy-Item -LiteralPath $BuiltHelper -Destination (Join-Path $Bin "RosPlotJugglerStudio.exe")
    Copy-Item -LiteralPath $BuiltUpdater -Destination (Join-Path $UpdaterDirectory "RosPlotJugglerUpdater.exe")
    Copy-Item -LiteralPath $SourceExtractionScript -Destination (Join-Path $UpdaterDirectory "extract-update.ps1")
    Write-Utf8 (Join-Path $Bin "health-mode.txt") $HealthMode
    Write-Utf8 (Join-Path $Root "version.txt") $Version
    $Marker = @{
        schemaVersion = 1
        product = "RosPlotJugglerStudio"
        version = $Version
        platform = "windows-x86_64"
        entrypoint = "bin/RosPlotJugglerStudio.exe"
        files = @()
    } | ConvertTo-Json -Depth 3 -Compress
    Write-Utf8 (Join-Path $Root "manifest.json") $Marker
}

function New-UpdateArchive {
    param(
        [string]$CaseRoot,
        [string]$Version,
        [ValidateSet("pass", "fail")][string]$HealthMode
    )
    $Container = Join-Path $CaseRoot "package"
    $PackageRoot = Join-Path $Container "RosPlotJugglerStudio"
    New-InstallTree $PackageRoot $Version $HealthMode
    $Archive = Join-Path $CaseRoot "update.zip"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($Container, $Archive,
        [IO.Compression.CompressionLevel]::Optimal, $false)
    return $Archive
}

function Invoke-UpdaterCase {
    param(
        [string]$Name,
        [ValidateSet("pass", "fail")][string]$HealthMode,
        [int]$ExpectedExitCode,
        [string]$ExpectedInstalledVersion,
        [string]$ExpectedLaunchVersion
    )
    $CaseRoot = Join-Path $TestRoot $Name
    New-Item -ItemType Directory -Path $CaseRoot -Force | Out-Null
    $InstallRoot = Join-Path $CaseRoot "install"
    New-InstallTree $InstallRoot "1.0.0" "pass"
    $Archive = New-UpdateArchive $CaseRoot "2.0.0" $HealthMode
    $Hash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
    $Size = (Get-Item -LiteralPath $Archive).Length

    $ExternalConfig = Join-Path $CaseRoot "external-config\settings.ini"
    Write-Utf8 $ExternalConfig "theme=dark`nautosave=true`n"
    $ConfigHash = (Get-FileHash -LiteralPath $ExternalConfig -Algorithm SHA256).Hash
    $LaunchMarker = Join-Path $CaseRoot "launched-version.txt"
    $env:RSPJ_TEST_LAUNCH_MARKER = $LaunchMarker

    $Parent = Start-Process -FilePath "powershell.exe" -ArgumentList @(
        "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Milliseconds 750"
    ) -PassThru
    $Arguments = @(
        "--protocol", "1",
        "--package", $Archive,
        "--install-root", $InstallRoot,
        "--extract-script", $ExtractionScript,
        "--version", "2.0.0",
        "--sha256", $Hash,
        "--size", [string]$Size,
        "--parent-pid", [string]$Parent.Id,
        "--health-timeout", "5"
    )
    $Process = Start-Process -FilePath $Updater -ArgumentList $Arguments -WorkingDirectory $Tools `
        -PassThru -Wait
    if ($Process.ExitCode -ne $ExpectedExitCode) {
        $Log = Join-Path $TestLocalAppData "RosPlotJugglerStudio\Updater\updater.log"
        $LogText = if (Test-Path -LiteralPath $Log) { Get-Content -LiteralPath $Log -Raw } else { "<missing>" }
        throw "$Name updater exit code was $($Process.ExitCode), expected $ExpectedExitCode.`n$LogText"
    }

    $Deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path -LiteralPath $LaunchMarker) -and [DateTime]::UtcNow -lt $Deadline) {
        Start-Sleep -Milliseconds 100
    }
    if (-not (Test-Path -LiteralPath $LaunchMarker)) {
        throw "$Name did not restart the expected application."
    }
    $InstalledVersion = (Get-Content -LiteralPath (Join-Path $InstallRoot "version.txt") -Raw).Trim()
    $LaunchedVersion = (Get-Content -LiteralPath $LaunchMarker -Raw).Trim()
    if ($InstalledVersion -ne $ExpectedInstalledVersion -or $LaunchedVersion -ne $ExpectedLaunchVersion) {
        throw "$Name version mismatch: installed=$InstalledVersion launched=$LaunchedVersion"
    }
    if ((Get-FileHash -LiteralPath $ExternalConfig -Algorithm SHA256).Hash -ne $ConfigHash) {
        throw "$Name modified external configuration."
    }
    $Leftovers = Get-ChildItem -LiteralPath $CaseRoot -Directory |
        Where-Object { $_.Name -match '^\..+-(staging|backup|failed)-' }
    if ($Leftovers) {
        throw "$Name left staging/backup directories: $($Leftovers.Name -join ', ')"
    }
}

function Assert-UnsafeArchiveRejected {
    param(
        [string]$Name,
        [string]$EntryName,
        [switch]$SymbolicLink
    )
    Add-Type -AssemblyName System.IO.Compression
    $CaseRoot = Join-Path $TestRoot "archive-$Name"
    New-Item -ItemType Directory -Path $CaseRoot -Force | Out-Null
    $Archive = Join-Path $CaseRoot "unsafe.zip"
    $Stream = [IO.File]::Open($Archive, [IO.FileMode]::CreateNew)
    try {
        $Zip = New-Object IO.Compression.ZipArchive($Stream, [IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            $Entry = $Zip.CreateEntry($EntryName)
            if ($SymbolicLink) {
                $Entry.ExternalAttributes = -1610612736 # Unix mode 0120000 in high 16 bits.
            }
            $Writer = New-Object IO.StreamWriter($Entry.Open())
            try { $Writer.Write("payload") } finally { $Writer.Dispose() }
        }
        finally { $Zip.Dispose() }
    }
    finally { $Stream.Dispose() }
    $Destination = Join-Path $CaseRoot "destination"
    $PreviousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File `
        $ExtractionScript -Archive $Archive -Destination $Destination *> $null
    $ExtractionExitCode = $LASTEXITCODE
    $ErrorActionPreference = $PreviousErrorAction
    if ($ExtractionExitCode -eq 0) {
        throw "Unsafe $Name archive was accepted."
    }
}

try {
    Assert-UnsafeArchiveRejected "traversal" "../escaped.txt"
    Assert-UnsafeArchiveRejected "absolute" "/absolute.txt"
    Assert-UnsafeArchiveRejected "symlink" "link" -SymbolicLink
    Invoke-UpdaterCase "success" "pass" 0 "2.0.0" "2.0.0"
    Invoke-UpdaterCase "rollback" "fail" 14 "1.0.0" "1.0.0"
    Write-Host "Updater E2E passed: archive defenses, successful swap, external config, and rollback."
}
finally {
    $env:LOCALAPPDATA = $OriginalLocalAppData
    $env:RSPJ_TEST_LAUNCH_MARKER = $OriginalLaunchMarker
    $env:PATH = $OriginalPath
}
$global:LASTEXITCODE = 0
