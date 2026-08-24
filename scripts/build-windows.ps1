# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "RelWithDebInfo",
    [string]$BuildDirectory,
    [string]$InstallDirectory,
    [string]$ToolchainRoot,
    [string]$DependencyCacheDirectory,
    [ValidateRange(1, 256)]
    [int]$Jobs = 1,
    [switch]$EnableMosaico,
    [switch]$SkipTests,
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $ProjectRoot "build\windows-ucrt64" }
if (-not $InstallDirectory) { $InstallDirectory = Join-Path $ProjectRoot "install\windows-ucrt64" }
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $ProjectRoot ".toolchain\msys64" }
if (-not $DependencyCacheDirectory) {
    $DependencyCacheDirectory = Join-Path $ProjectRoot "build\PlotJuggler\_deps"
}

$SourceDirectory = Join-Path $ProjectRoot "src\PlotJuggler"
$UcrtBin = Join-Path $ToolchainRoot "ucrt64\bin"
$CMake = Join-Path $UcrtBin "cmake.exe"
$CTest = Join-Path $UcrtBin "ctest.exe"

foreach ($RequiredPath in @($SourceDirectory, $CMake, $CTest, (Join-Path $UcrtBin "ninja.exe"))) {
    if (-not (Test-Path -LiteralPath $RequiredPath)) {
        throw "Required build component not found: $RequiredPath"
    }
}

function Invoke-Checked {
    param([Parameter(Mandatory)][string]$FilePath, [Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

$env:PATH = "$UcrtBin;$env:PATH"
$env:CMAKE_PREFIX_PATH = Join-Path $ToolchainRoot "ucrt64"
$env:MSYSTEM = "UCRT64"

if (-not $NoClean) {
    foreach ($OutputDirectory in @($BuildDirectory, $InstallDirectory)) {
        if (Test-Path -LiteralPath $OutputDirectory) {
            Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
        }
    }
}
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null

$ConfigureArguments = @(
    "-S", $SourceDirectory,
    "-B", $BuildDirectory,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_INSTALL_PREFIX=$InstallDirectory",
    "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH",
    "-DBUILD_TESTING=ON",
    "-DPJ_BUILD_MOSAICO_PLUGIN=$(if ($EnableMosaico) { 'ON' } else { 'OFF' })"
)
foreach ($Dependency in @("data_tamer", "lz4", "wasmer")) {
    $CachedSource = Join-Path $DependencyCacheDirectory "$Dependency-src"
    if (Test-Path -LiteralPath $CachedSource) {
        $CMakeCachedSource = ([IO.Path]::GetFullPath($CachedSource)).Replace("\", "/")
        $ConfigureArguments += "-DCPM_${Dependency}_SOURCE=$CMakeCachedSource"
    }
}
Invoke-Checked $CMake @ConfigureArguments
Invoke-Checked $CMake --build $BuildDirectory --config $Configuration --parallel $Jobs

if (-not $SkipTests) {
    Invoke-Checked $CTest --test-dir $BuildDirectory -C $Configuration --output-on-failure
}

Invoke-Checked $CMake --install $BuildDirectory --config $Configuration
Write-Host "Windows build and install completed: $InstallDirectory"
