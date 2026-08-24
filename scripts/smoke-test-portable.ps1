# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$PackageRoot,
    [ValidateRange(1, 120)]
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $PackageRoot) {
    $PackageRoot = Join-Path $ProjectRoot "release\portable\RosPlotJugglerStudio"
}
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path

$BinDirectory = Join-Path $PackageRoot "bin"
$Executable = Join-Path $BinDirectory "RosPlotJugglerStudio.exe"
$Python = Join-Path $BinDirectory "python.exe"
$Worker = Join-Path $BinDirectory "runtime\rosbag_python\extract_rosbag.py"
$Manifest = Join-Path $PackageRoot "manifest.json"
$PluginDirectory = Join-Path $BinDirectory "plugins"
$Updater = Join-Path $BinDirectory "updater\RosPlotJugglerUpdater.exe"
$ExtractionScript = Join-Path $BinDirectory "updater\extract-update.ps1"

foreach ($RequiredPath in @($Executable, $Python, $Worker, $Manifest, $PluginDirectory,
        $Updater, $ExtractionScript)) {
    if (-not (Test-Path -LiteralPath $RequiredPath)) {
        throw "Portable smoke test prerequisite is missing: $RequiredPath"
    }
}
if (-not (Get-ChildItem -LiteralPath $PluginDirectory -Filter "*.dll" -File)) {
    throw "No plugin DLLs were installed in $PluginDirectory"
}

$env:PATH = "$BinDirectory;$env:PATH"
$env:RSPJ_PYTHON = $Python
$env:PYTHONPATH = Join-Path $BinDirectory "runtime\rosbag_python"
& $Python -c "from rosbags.highlevel import AnyReader; import lz4, numpy, zstandard"
if ($LASTEXITCODE -ne 0) { throw "Bundled ROS bag Python runtime import test failed." }

$VersionOutput = Join-Path ([IO.Path]::GetTempPath()) "rspj-version-$PID.txt"
try {
    $Process = Start-Process -FilePath $Executable -ArgumentList "--version" -WorkingDirectory $BinDirectory `
        -RedirectStandardOutput $VersionOutput -RedirectStandardError "$VersionOutput.err" -PassThru
    if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
        $Process.Kill()
        throw "Portable executable did not finish --version within $TimeoutSeconds seconds."
    }
    $Process.Refresh()
    # Windows PowerShell can leave ExitCode unset for GUI-subsystem processes.
    if ($null -ne $Process.ExitCode -and $Process.ExitCode -ne 0) {
        throw "Portable executable --version failed with exit code $($Process.ExitCode)."
    }
}
finally {
    Remove-Item -LiteralPath $VersionOutput, "$VersionOutput.err" -Force -ErrorAction SilentlyContinue
}

Write-Host "Portable smoke test passed: app, updater, plugins, manifest, and ROS bag runtime."
