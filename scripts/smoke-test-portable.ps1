# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [string]$PackageRoot,
    [ValidateRange(1, 120)]
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-Native {
    param([Parameter(Mandatory)][string]$FilePath, [Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
    # Windows PowerShell turns redirected native stderr (the worker's METRIC lines) into
    # terminating errors under "Stop"; callers check $LASTEXITCODE instead.
    $PreviousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { & $FilePath @Arguments }
    finally { $ErrorActionPreference = $PreviousPreference }
}

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
foreach ($PluginName in @("*DataLoadMCAP.dll", "*DataLoadROSBag.dll")) {
    if (-not (Get-ChildItem -LiteralPath $PluginDirectory -Filter $PluginName -File)) {
        throw "Required loader plugin was not installed: $PluginName"
    }
}
$UpdaterRuntimeList = Join-Path $BinDirectory "updater\runtime-dlls.txt"
if (-not (Test-Path -LiteralPath $UpdaterRuntimeList -PathType Leaf)) {
    throw "Updater runtime list is missing: $UpdaterRuntimeList"
}
$UpdaterRuntime = @(Get-Content -LiteralPath $UpdaterRuntimeList | Where-Object { $_ })
if ($UpdaterRuntime -notcontains "Qt5Core.dll") {
    throw "Updater runtime list does not contain Qt5Core.dll: $($UpdaterRuntime -join ', ')"
}
foreach ($UpdaterDll in $UpdaterRuntime) {
    if (-not (Test-Path -LiteralPath (Join-Path $BinDirectory $UpdaterDll) -PathType Leaf)) {
        throw "DLL listed for the updater is not packaged: $UpdaterDll"
    }
}
# Qt5Network loads OpenSSL by name at runtime, so the HTTPS update checker only works
# when the package contains the exact OpenSSL major version Qt was built for.
$QtNetworkText = [Text.Encoding]::ASCII.GetString(
    [IO.File]::ReadAllBytes((Join-Path $BinDirectory "Qt5Network.dll")))
$OpenSslNames = @([regex]::Matches($QtNetworkText, 'lib(?:ssl|crypto)-[0-9a-z_-]+') |
    ForEach-Object { $_.Value } | Sort-Object -Unique)
foreach ($Kind in @("ssl", "crypto")) {
    if (-not ($OpenSslNames | Where-Object {
                $_ -like "lib$Kind-*" -and (Test-Path -LiteralPath (Join-Path $BinDirectory "$_.dll"))
            })) {
        throw "OpenSSL lib$Kind loaded by Qt5Network is not packaged (expected one of: $($OpenSslNames -join ', '))."
    }
}

$env:PATH = "$BinDirectory;$env:PATH"
$env:RSPJ_PYTHON = $Python
$env:PYTHONPATH = Join-Path $BinDirectory "runtime\rosbag_python"
Invoke-Native $Python -c "from rosbags.highlevel import AnyReader; import lz4, numpy, zstandard"
if ($LASTEXITCODE -ne 0) { throw "Bundled ROS bag Python runtime import test failed." }

$Generator = Join-Path $ProjectRoot "rosbag\generate_benchmark_bags.py"
$WorkerSmokeRoot = Join-Path ([IO.Path]::GetTempPath()) "rspj-worker-smoke-$PID"
try {
    if (-not (Test-Path -LiteralPath $Generator -PathType Leaf)) {
        throw "ROS bag smoke generator is missing: $Generator"
    }
    [IO.Directory]::CreateDirectory($WorkerSmokeRoot) | Out-Null
    foreach ($Format in @("ros1", "ros2-sqlite", "ros2-mcap")) {
        $SmokeBag = Join-Path $WorkerSmokeRoot "sample-$Format"
        if ($Format -eq "ros1") { $SmokeBag += ".bag" }
        Invoke-Native $Python $Generator --format $Format --output $SmokeBag --messages 10 `
            --topics 2 --array-width 4 --empty-string-ratio 0.2 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Bundled Python could not generate the $Format smoke fixture."
        }
        $InspectJson = (Invoke-Native $Python -u $Worker $SmokeBag --inspect) -join ""
        if ($LASTEXITCODE -ne 0) { throw "Packaged worker inspect failed for $Format." }
        $Index = $InspectJson | ConvertFrom-Json
        if ([int]$Index.version -ne 1 -or [int]$Index.totalMessages -ne 30 -or
            @($Index.topics).Count -ne 3) {
            throw "Packaged worker returned an invalid $Format topic index."
        }

        # The loader streams raw messages for every format and uses binary-v1 as fallback.
        foreach ($Stream in @(@("binary-v1", "52-53-50-4A-42-41-47-00"),
                              @("raw-v1", "52-53-50-4A-52-41-57-00"))) {
            $Protocol = $Stream[0]
            $StreamOutput = Join-Path $WorkerSmokeRoot "sample-$Format-$Protocol.bin"
            $StartInfo = [Diagnostics.ProcessStartInfo]::new()
            $StartInfo.FileName = $Python
            $StartInfo.UseShellExecute = $false
            $StartInfo.RedirectStandardOutput = $true
            $StartInfo.RedirectStandardError = $true
            $StartInfo.Arguments = (@("-u", $Worker, $SmokeBag, "--protocol", $Protocol) |
                    ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join " "
            $WorkerProcess = [Diagnostics.Process]::new()
            $WorkerProcess.StartInfo = $StartInfo
            [void]$WorkerProcess.Start()
            $OutputStream = [IO.File]::Create($StreamOutput)
            try {
                $CopyTask = $WorkerProcess.StandardOutput.BaseStream.CopyToAsync($OutputStream)
                $ErrorTask = $WorkerProcess.StandardError.ReadToEndAsync()
                if (-not $WorkerProcess.WaitForExit($TimeoutSeconds * 1000)) {
                    $WorkerProcess.Kill()
                    throw "Packaged worker $Protocol protocol timed out for $Format."
                }
                [void]$CopyTask.GetAwaiter().GetResult()
                $Diagnostics = $ErrorTask.GetAwaiter().GetResult()
                if ($WorkerProcess.ExitCode -ne 0) {
                    throw "Packaged worker $Protocol protocol failed for ${Format}: $Diagnostics"
                }
            }
            finally {
                $OutputStream.Dispose()
                $WorkerProcess.Dispose()
            }
            $Magic = [IO.File]::ReadAllBytes($StreamOutput)
            if ($Magic.Length -lt 12 -or [BitConverter]::ToString($Magic, 0, 8) -ne $Stream[1]) {
                throw "Packaged worker returned an invalid $Protocol header for $Format."
            }
        }
    }
}
finally {
    if (Test-Path -LiteralPath $WorkerSmokeRoot) {
        Remove-Item -LiteralPath $WorkerSmokeRoot -Recurse -Force
    }
}

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
