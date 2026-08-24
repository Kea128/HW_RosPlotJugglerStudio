# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$ToolchainRoot,
    [string]$RuntimeDirectory,
    [string]$RosbagsVersion = "0.10.11",
    [switch]$SkipSystemPackages
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $ProjectRoot ".toolchain\msys64" }
if (-not $RuntimeDirectory) {
    $RuntimeDirectory = Join-Path $ProjectRoot "runtime\rosbag_python"
}
$Bash = Join-Path $ToolchainRoot "usr\bin\bash.exe"
$PythonHome = Join-Path $ToolchainRoot "ucrt64"
$Python = Join-Path $PythonHome "bin\python.exe"

foreach ($required in @($Bash, $Python)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required toolchain component not found: $required"
    }
}

$env:PATH = "$(Join-Path $PythonHome "bin");$env:PATH"
if (-not $SkipSystemPackages) {
    $env:MSYSTEM = "UCRT64"
    $env:CHERE_INVOKING = "1"
    & $Bash -lc @"
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-python-pip \
  mingw-w64-ucrt-x86_64-python-lz4 \
  mingw-w64-ucrt-x86_64-python-numpy \
  mingw-w64-ucrt-x86_64-python-ruamel-yaml \
  mingw-w64-ucrt-x86_64-python-typing_extensions \
  mingw-w64-ucrt-x86_64-python-zstandard
"@
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install ROS bag runtime dependencies."
    }
}

New-Item -ItemType Directory -Path $RuntimeDirectory -Force | Out-Null
$env:PYTHONHOME = $PythonHome
$PythonVersion = (& $Python -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')").Trim()
$env:PYTHONPATH = "$RuntimeDirectory;$(Join-Path $PythonHome "lib\python$PythonVersion")"

& $Python -m pip install --no-deps --upgrade --target $RuntimeDirectory "rosbags==$RosbagsVersion"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install the rosbags Python package."
}

& $Python -c "from rosbags.highlevel import AnyReader; import lz4, numpy, zstandard"
if ($LASTEXITCODE -ne 0) {
    throw "ROS bag runtime validation failed."
}

Write-Host "ROS1/ROS2 offline bag runtime ready: $RuntimeDirectory"
