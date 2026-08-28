# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BagPath,
    [string]$PythonPath,
    [string]$WorkerPath,
    [string]$TopicsFile,
    [ValidateSet("text", "binary-v1")]
    [string]$Protocol = "text",
    [ValidateRange(1, 10000)]
    [int]$Iterations = 3,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function ConvertTo-ProcessArgument {
    param([Parameter(Mandatory = $true)][string]$Value)

    # Inputs here are file paths and fixed protocol names, so quote the only
    # character that can terminate a Windows command-line argument.
    return '"' + $Value.Replace('"', '\"') + '"'
}

function ConvertTo-InvariantNumber {
    param([string]$Value)

    $number = 0.0
    if ([double]::TryParse(
            $Value,
            [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$number)) {
        return $number
    }
    return $Value
}

function Get-Median {
    param([double[]]$Values)

    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) {
        return $sorted[$middle]
    }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
}

try {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
    if (-not $PythonPath) {
        $PythonPath = Join-Path $ProjectRoot `
            "release\portable\RosPlotJugglerStudio\bin\python.exe"
    }
    if (-not $WorkerPath) {
        $WorkerPath = Join-Path $ProjectRoot `
            "release\portable\RosPlotJugglerStudio\bin\runtime\rosbag_python\extract_rosbag.py"
    }
    if (-not $OutputPath) {
        $OutputPath = Join-Path $ProjectRoot "artifacts\benchmark\rosbag-worker.json"
    }

    $BagPath = [IO.Path]::GetFullPath($BagPath)
    $PythonPath = [IO.Path]::GetFullPath($PythonPath)
    $WorkerPath = [IO.Path]::GetFullPath($WorkerPath)
    if ($TopicsFile) { $TopicsFile = [IO.Path]::GetFullPath($TopicsFile) }
    $OutputPath = [IO.Path]::GetFullPath($OutputPath)

    if (-not (Test-Path -LiteralPath $BagPath)) {
        throw "Bag path not found: $BagPath"
    }
    foreach ($required in @($PythonPath, $WorkerPath, $TopicsFile)) {
        if (-not $required) { continue }
        if (-not [IO.File]::Exists($required)) {
            throw "Required file not found: $required"
        }
    }
    $BagBytes = if ([IO.Directory]::Exists($BagPath)) {
        [long](Get-ChildItem -LiteralPath $BagPath -File -Recurse |
            Measure-Object -Property Length -Sum).Sum
    }
    else {
        [long]([IO.FileInfo]$BagPath).Length
    }

    $workerSource = [IO.File]::ReadAllText($WorkerPath)
    $supportsProtocolOption = $workerSource.Contains("--protocol")
    if ($Protocol -eq "binary-v1" -and -not $supportsProtocolOption) {
        throw "Worker does not advertise --protocol; binary-v1 cannot be benchmarked."
    }

    $runs = @()
    for ($iteration = 1; $iteration -le $Iterations; $iteration++) {
        $token = [Guid]::NewGuid().ToString("N")
        $stdoutPath = Join-Path ([IO.Path]::GetTempPath()) "rspj-benchmark-$token.stdout"
        $stderrPath = Join-Path ([IO.Path]::GetTempPath()) "rspj-benchmark-$token.stderr"
        $stdoutStream = $null
        $stderrStream = $null
        $process = $null

        try {
            $startInfo = New-Object Diagnostics.ProcessStartInfo
            $startInfo.FileName = $PythonPath
            $arguments = @(
                (ConvertTo-ProcessArgument $WorkerPath),
                (ConvertTo-ProcessArgument $BagPath)
            )
            if ($supportsProtocolOption) {
                $arguments += "--protocol"
                $arguments += (ConvertTo-ProcessArgument $Protocol)
            }
            if ($TopicsFile) {
                $arguments += "--topics-file"
                $arguments += (ConvertTo-ProcessArgument $TopicsFile)
            }
            $startInfo.Arguments = $arguments -join " "
            $startInfo.UseShellExecute = $false
            $startInfo.CreateNoWindow = $true
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            $startInfo.WorkingDirectory = Split-Path -Parent $WorkerPath

            $workerDirectory = Split-Path -Parent $WorkerPath
            $existingPythonPath = $startInfo.EnvironmentVariables["PYTHONPATH"]
            if ($existingPythonPath) {
                $startInfo.EnvironmentVariables["PYTHONPATH"] =
                    "$workerDirectory;$existingPythonPath"
            }
            else {
                $startInfo.EnvironmentVariables["PYTHONPATH"] = $workerDirectory
            }

            $stdoutStream = [IO.File]::Open(
                $stdoutPath,
                [IO.FileMode]::CreateNew,
                [IO.FileAccess]::Write,
                [IO.FileShare]::ReadWrite
            )
            $stderrStream = [IO.File]::Open(
                $stderrPath,
                [IO.FileMode]::CreateNew,
                [IO.FileAccess]::Write,
                [IO.FileShare]::ReadWrite
            )

            $process = New-Object Diagnostics.Process
            $process.StartInfo = $startInfo
            $stopwatch = [Diagnostics.Stopwatch]::StartNew()
            if (-not $process.Start()) {
                throw "Failed to start ROS bag worker."
            }
            $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdoutStream)
            $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderrStream)

            $firstOutputMs = $null
            [long]$peakWorkingSetBytes = 0
            while (-not $process.HasExited) {
                try {
                    $process.Refresh()
                    $peakWorkingSetBytes =
                        [Math]::Max($peakWorkingSetBytes, $process.WorkingSet64)
                }
                catch {
                    # The process may exit between HasExited and Refresh.
                }
                if ($null -eq $firstOutputMs) {
                    if (([IO.FileInfo]$stdoutPath).Length -gt 0 -or
                        ([IO.FileInfo]$stderrPath).Length -gt 0) {
                        $firstOutputMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
                    }
                }
                Start-Sleep -Milliseconds 10
            }

            $process.WaitForExit()
            $stdoutTask.Wait()
            $stderrTask.Wait()
            $stdoutStream.Flush()
            $stderrStream.Flush()
            $stopwatch.Stop()
            $exitCode = $process.ExitCode
            try {
                $process.Refresh()
                $peakWorkingSetBytes =
                    [Math]::Max($peakWorkingSetBytes, $process.PeakWorkingSet64)
            }
            catch {
                # The sampled peak remains valid if the exited process is unavailable.
            }
            if ($null -eq $firstOutputMs -and
                (([IO.FileInfo]$stdoutPath).Length -gt 0 -or
                 ([IO.FileInfo]$stderrPath).Length -gt 0)) {
                $firstOutputMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
            }

            $stdoutStream.Dispose()
            $stdoutStream = $null
            $stderrStream.Dispose()
            $stderrStream = $null

            $info = $null
            $progress = @()
            $metrics = @()
            $done = $null
            $errors = @()
            $stderrLines = [IO.File]::ReadAllLines($stderrPath)
            foreach ($line in $stderrLines) {
                $fields = $line -split "`t"
                switch ($fields[0]) {
                    "INFO" {
                        if ($fields.Count -ge 3) {
                            $info = [pscustomobject][ordered]@{
                                connections = [long]$fields[1]
                                messages = [long]$fields[2]
                            }
                        }
                    }
                    "PROGRESS" {
                        if ($fields.Count -ge 4) {
                            $progress += [pscustomobject][ordered]@{
                                messages = [long]$fields[1]
                                total = [long]$fields[2]
                                emitted = [long]$fields[3]
                            }
                        }
                    }
                    "METRIC" {
                        if ($fields.Count -ge 3) {
                            $metrics += [pscustomobject][ordered]@{
                                name = $fields[1]
                                value = ConvertTo-InvariantNumber $fields[2]
                                unit = if ($fields.Count -ge 4) { $fields[3] } else { $null }
                            }
                        }
                    }
                    "DONE" {
                        if ($fields.Count -ge 3) {
                            $done = [pscustomobject][ordered]@{
                                messages = [long]$fields[1]
                                emitted = [long]$fields[2]
                            }
                        }
                    }
                    "ERROR" { $errors += $line }
                }
            }

            # The legacy text worker writes INFO to stdout. Read only its first
            # line so large benchmark payloads are never loaded into memory.
            if ($null -eq $info -and $Protocol -eq "text") {
                $reader = [IO.File]::OpenText($stdoutPath)
                try {
                    $firstLine = $reader.ReadLine()
                }
                finally {
                    $reader.Dispose()
                }
                $fields = $firstLine -split "`t"
                if ($fields.Count -ge 3 -and $fields[0] -eq "INFO") {
                    $info = [pscustomobject][ordered]@{
                        connections = [long]$fields[1]
                        messages = [long]$fields[2]
                    }
                }
            }

            if ($exitCode -ne 0) {
                throw "Worker iteration $iteration exited with code $exitCode. $($errors -join '; ')"
            }
            if ($null -eq $done) {
                throw "Worker iteration $iteration did not emit a DONE record on stderr."
            }

            $runs += [pscustomobject][ordered]@{
                iteration = $iteration
                wallTimeMs = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds, 3)
                firstOutputMs = $firstOutputMs
                fieldsPerSecond = if ($stopwatch.Elapsed.TotalSeconds -gt 0) {
                    [Math]::Round($done.emitted / $stopwatch.Elapsed.TotalSeconds, 3)
                }
                else { 0.0 }
                peakWorkingSetBytes = $peakWorkingSetBytes
                exitCode = $exitCode
                info = $info
                progress = $progress
                metrics = $metrics
                done = $done
            }
        }
        finally {
            if ($null -ne $stdoutStream) { $stdoutStream.Dispose() }
            if ($null -ne $stderrStream) { $stderrStream.Dispose() }
            if ($null -ne $process) { $process.Dispose() }
            if ([IO.File]::Exists($stdoutPath)) { [IO.File]::Delete($stdoutPath) }
            if ([IO.File]::Exists($stderrPath)) { [IO.File]::Delete($stderrPath) }
        }
    }

    $wallTimes = [double[]]@($runs | ForEach-Object { $_.wallTimeMs })
    $firstOutputTimes = [double[]]@(
        $runs | Where-Object { $null -ne $_.firstOutputMs } |
            ForEach-Object { $_.firstOutputMs }
    )
    $peakWorkingSets = [long[]]@($runs | ForEach-Object { $_.peakWorkingSetBytes })
    $fieldsPerSecond = [double[]]@($runs | ForEach-Object { $_.fieldsPerSecond })
    $result = [pscustomobject][ordered]@{
        schemaVersion = 1
        bagPath = $BagPath
        bagBytes = $BagBytes
        pythonPath = $PythonPath
        workerPath = $WorkerPath
        topicsFile = $TopicsFile
        protocol = $Protocol
        iterations = $Iterations
        summary = [pscustomobject][ordered]@{
            medianWallTimeMs = [Math]::Round((Get-Median $wallTimes), 3)
            medianFirstOutputMs = if ($firstOutputTimes.Count -gt 0) {
                [Math]::Round((Get-Median $firstOutputTimes), 3)
            }
            else { $null }
            maxPeakWorkingSetBytes = ($peakWorkingSets | Measure-Object -Maximum).Maximum
            medianFieldsPerSecond = [Math]::Round((Get-Median $fieldsPerSecond), 3)
        }
        runs = $runs
    }

    $outputDirectory = Split-Path -Parent $OutputPath
    if ($outputDirectory) {
        [IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
    }
    $json = $result | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText(
        $OutputPath,
        $json + [Environment]::NewLine,
        (New-Object Text.UTF8Encoding($false))
    )
    Write-Output $OutputPath
}
catch {
    Write-Error $_
    exit 1
}
