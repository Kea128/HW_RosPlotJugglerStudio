# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$InstallDirectory,
    [string]$ToolchainRoot,
    [switch]$Build,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ApplicationArguments
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $InstallDirectory) { $InstallDirectory = Join-Path $ProjectRoot "install\windows-ucrt64" }
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $ProjectRoot ".toolchain\msys64" }

if ($Build) {
    & (Join-Path $PSScriptRoot "build-windows.ps1") -InstallDirectory $InstallDirectory -ToolchainRoot $ToolchainRoot
    if ($LASTEXITCODE -ne 0) { throw "Windows build failed with exit code $LASTEXITCODE." }
}

$BinDirectory = Join-Path $InstallDirectory "bin"
$Executable = Join-Path $BinDirectory "RosPlotJugglerStudio.exe"
$UcrtBin = Join-Path $ToolchainRoot "ucrt64\bin"
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Studio executable not found: $Executable. Run scripts/build-windows.ps1 first."
}

$env:PATH = "$BinDirectory;$UcrtBin;$env:PATH"
$BundledPython = Join-Path $BinDirectory "python.exe"
if (Test-Path -LiteralPath $BundledPython) {
    $env:RSPJ_PYTHON = $BundledPython
}

& $Executable @ApplicationArguments
if ($LASTEXITCODE -ne 0) {
    throw "RosPlotJugglerStudio exited with code $LASTEXITCODE."
}
