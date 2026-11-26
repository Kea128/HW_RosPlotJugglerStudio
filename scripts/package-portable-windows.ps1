# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "RelWithDebInfo",
    [string]$BuildDirectory,
    [string]$InstallDirectory,
    [string]$ReleaseDirectory,
    [string]$ToolchainRoot,
    [ValidateRange(1, 256)]
    [int]$Jobs = 1,
    [ValidateSet("stable", "beta")]
    [string]$Channel = "stable",
    [string]$MinimumSupportedVersion = "3.17.2-studio.1",
    [string]$RepositoryUrl = "https://github.com/Kea128/HW_RosPlotJugglerStudio",
    [switch]$EnableMosaico,
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
# Windows PowerShell's per-entry progress rendering makes Compress-Archive far slower.
$ProgressPreference = "SilentlyContinue"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $ProjectRoot "build\windows-ucrt64" }
if (-not $InstallDirectory) { $InstallDirectory = Join-Path $ProjectRoot "install\windows-ucrt64" }
if (-not $ReleaseDirectory) { $ReleaseDirectory = Join-Path $ProjectRoot "release" }
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $ProjectRoot ".toolchain\msys64" }

$UcrtRoot = Join-Path $ToolchainRoot "ucrt64"
$UcrtBin = Join-Path $UcrtRoot "bin"
$Python = Join-Path $UcrtBin "python.exe"
$WinDeployQt = Join-Path $UcrtBin "windeployqt.exe"
$ObjDump = Join-Path $UcrtBin "objdump.exe"
$VersionFile = Join-Path $ProjectRoot "src\PlotJuggler\STUDIO_VERSION"
$Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$') {
    throw "Invalid STUDIO_VERSION: '$Version'"
}
if ($Version -match '-([^+]+)') {
    foreach ($Identifier in $Matches[1].Split(".")) {
        if ($Identifier -match '^[0-9]+$' -and $Identifier -notmatch '^(0|[1-9][0-9]*)$') {
            throw "Numeric SemVer pre-release identifiers must not have leading zeroes: '$Version'"
        }
    }
}

foreach ($RequiredPath in @($Python, $ObjDump)) {
    if (-not (Test-Path -LiteralPath $RequiredPath)) {
        throw "Required packaging component not found: $RequiredPath"
    }
}

if (-not $SkipBuild) {
    $BuildArguments = @{
        Configuration = $Configuration
        BuildDirectory = $BuildDirectory
        InstallDirectory = $InstallDirectory
        ToolchainRoot = $ToolchainRoot
        Jobs = $Jobs
    }
    if ($EnableMosaico) { $BuildArguments.EnableMosaico = $true }
    if ($SkipTests) { $BuildArguments.SkipTests = $true }
    & (Join-Path $PSScriptRoot "build-windows.ps1") @BuildArguments
}

$InstalledExecutable = Join-Path $InstallDirectory "bin\RosPlotJugglerStudio.exe"
if (-not (Test-Path -LiteralPath $InstalledExecutable)) {
    throw "Installed executable not found: $InstalledExecutable"
}

$PackageRoot = Join-Path $ReleaseDirectory "portable\RosPlotJugglerStudio"
$ArchiveName = "RosPlotJugglerStudio-$Version-windows-x86_64.zip"
$ArchivePath = Join-Path $ReleaseDirectory $ArchiveName
$UpdateManifestPath = Join-Path $ReleaseDirectory "update-manifest-$Channel.json"
New-Item -ItemType Directory -Path $ReleaseDirectory -Force | Out-Null
foreach ($OldOutput in @($PackageRoot, $ArchivePath, "$ArchivePath.sha256",
        (Join-Path $ReleaseDirectory "SHA256SUMS"), $UpdateManifestPath)) {
    if (Test-Path -LiteralPath $OldOutput) {
        Remove-Item -LiteralPath $OldOutput -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $PackageRoot -Force | Out-Null
$InstalledBin = Join-Path $InstallDirectory "bin"
New-Item -ItemType Directory -Path (Join-Path $PackageRoot "bin") -Force | Out-Null
Copy-Item -Path (Join-Path $InstalledBin "*") -Destination (Join-Path $PackageRoot "bin") `
    -Recurse -Force
Get-ChildItem -LiteralPath $PackageRoot -Filter "*.dll.a" -File -Recurse |
    Remove-Item -Force

$BinDirectory = Join-Path $PackageRoot "bin"
if (Test-Path -LiteralPath $WinDeployQt) {
    $DeployMode = if ($Configuration -eq "Debug") { "--debug" } else { "--release" }
    & $WinDeployQt $DeployMode --compiler-runtime --no-translations --dir $BinDirectory `
        (Join-Path $BinDirectory "RosPlotJugglerStudio.exe")
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }
}
else {
    $QtPluginRoot = Join-Path $UcrtRoot "share\qt5\plugins"
    foreach ($PluginCategory in @("platforms", "imageformats", "iconengines", "styles", "tls")) {
        $PluginSource = Join-Path $QtPluginRoot $PluginCategory
        if (Test-Path -LiteralPath $PluginSource) {
            Copy-Item -LiteralPath $PluginSource -Destination $BinDirectory -Recurse -Force
        }
    }
}

# Qt5Network loads OpenSSL by name at runtime instead of importing it, so the dependency
# scan below only finds it when some other library happens to link it.
$QtNetwork = Join-Path $BinDirectory "Qt5Network.dll"
if (Test-Path -LiteralPath $QtNetwork -PathType Leaf) {
    $QtNetworkText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($QtNetwork))
    $BundledOpenSsl = @([regex]::Matches($QtNetworkText, 'lib(?:ssl|crypto)-[0-9a-z_-]+') |
        ForEach-Object { $_.Value } | Sort-Object -Unique |
        Where-Object { Test-Path -LiteralPath (Join-Path $UcrtBin "$_.dll") -PathType Leaf })
    foreach ($Kind in @("ssl", "crypto")) {
        if (-not ($BundledOpenSsl | Where-Object { $_ -like "lib$Kind-*" })) {
            throw "The OpenSSL lib$Kind that Qt5Network loads is not installed in $UcrtBin."
        }
    }
    foreach ($Library in $BundledOpenSsl) {
        Copy-Item -LiteralPath (Join-Path $UcrtBin "$Library.dll") -Destination $BinDirectory -Force
    }
}

# Resolve non-system MinGW dependencies for the executable and every installed plugin.
$AvailableDlls = @{}
Get-ChildItem -LiteralPath $UcrtBin -Filter "*.dll" -File | ForEach-Object {
    $AvailableDlls[$_.Name.ToLowerInvariant()] = $_.FullName
}
$Queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $PackageRoot -Recurse -File |
    Where-Object { $_.Extension -in @(".exe", ".dll") } |
    ForEach-Object { $Queue.Enqueue($_.FullName) }
$Scanned = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while ($Queue.Count -gt 0) {
    $Binary = $Queue.Dequeue()
    if (-not $Scanned.Add($Binary)) { continue }
    $Dependencies = & $ObjDump -p $Binary |
        Select-String -Pattern 'DLL Name:\s*(.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
    if ($LASTEXITCODE -ne 0) { throw "objdump failed while inspecting $Binary" }
    foreach ($Dependency in $Dependencies) {
        $Key = $Dependency.ToLowerInvariant()
        if (-not $AvailableDlls.ContainsKey($Key)) { continue }
        $Destination = Join-Path $BinDirectory $Dependency
        if (-not (Test-Path -LiteralPath $Destination)) {
            Copy-Item -LiteralPath $AvailableDlls[$Key] -Destination $Destination
            $Queue.Enqueue($Destination)
        }
    }
}

# Bundle the UCRT64 Python runtime and the offline ROS bag worker/packages.
$PythonVersion = (& $Python -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')").Trim()
if ($LASTEXITCODE -ne 0) { throw "Unable to determine bundled Python version." }
Copy-Item -LiteralPath $Python -Destination (Join-Path $BinDirectory "python.exe") -Force
Get-ChildItem -LiteralPath $UcrtBin -Filter "python*.dll" -File |
    Copy-Item -Destination $BinDirectory -Force
$PythonLibrarySource = Join-Path $UcrtRoot "lib\python$PythonVersion"
$PythonLibraryDestination = Join-Path $PackageRoot "lib\python$PythonVersion"
if (-not (Test-Path -LiteralPath $PythonLibrarySource)) {
    throw "Python standard library not found: $PythonLibrarySource"
}
New-Item -ItemType Directory -Path (Split-Path -Parent $PythonLibraryDestination) -Force | Out-Null
Copy-Item -LiteralPath $PythonLibrarySource -Destination $PythonLibraryDestination -Recurse -Force

# Neither the ROS bag worker nor the embedded interpreter uses test suites, IDLE/Tk,
# packaging tools or -O/-OO bytecode. They hold the package's deepest paths, which
# in-place updates must extract below a staging directory (checked before archiving).
$SitePackages = Join-Path $PythonLibraryDestination "site-packages"
foreach ($Relative in @("test", "idlelib", "tkinter", "turtledemo", "ensurepip",
        "site-packages\pip", "site-packages\setuptools", "site-packages\_distutils_hack",
        "site-packages\distlib", "site-packages\distutils-precedence.pth")) {
    $UnusedPath = Join-Path $PythonLibraryDestination $Relative
    if (Test-Path -LiteralPath $UnusedPath) { Remove-Item -LiteralPath $UnusedPath -Recurse -Force }
}
Get-ChildItem -LiteralPath $SitePackages -Directory |
    Where-Object { $_.Name -match '^(pip|setuptools|distlib)-[^\\]+\.dist-info$' } |
    Remove-Item -Recurse -Force
Get-ChildItem -LiteralPath (Join-Path $PythonLibraryDestination "lib-dynload") -Filter "_tkinter*" -File |
    Remove-Item -Force
$RemovedTestDirectories = [Collections.Generic.List[string]]::new()
foreach ($TestDirectory in @(Get-ChildItem -LiteralPath $SitePackages -Directory -Recurse -Filter "tests" |
            Sort-Object FullName)) {
    if ($RemovedTestDirectories | Where-Object { $TestDirectory.FullName.StartsWith("$_\", [StringComparison]::OrdinalIgnoreCase) }) {
        continue
    }
    Remove-Item -LiteralPath $TestDirectory.FullName -Recurse -Force
    $RemovedTestDirectories.Add($TestDirectory.FullName)
}
Get-ChildItem -LiteralPath $PythonLibraryDestination -Recurse -File -Filter "*.opt-*.pyc" |
    Remove-Item -Force

$RosbagRuntimeDestination = Join-Path $BinDirectory "runtime\rosbag_python"
New-Item -ItemType Directory -Path $RosbagRuntimeDestination -Force | Out-Null
Copy-Item -Path (Join-Path $ProjectRoot "runtime\rosbag_python\*") `
    -Destination $RosbagRuntimeDestination -Recurse -Force

# Python extension modules carry additional native dependencies (for example
# sqlite3 and compression libraries), so resolve them after Python is staged.
Get-ChildItem -LiteralPath $PackageRoot -Recurse -File |
    Where-Object { $_.Extension -in @(".exe", ".dll", ".pyd") } |
    ForEach-Object {
        if (-not $Scanned.Contains($_.FullName)) { $Queue.Enqueue($_.FullName) }
    }
while ($Queue.Count -gt 0) {
    $Binary = $Queue.Dequeue()
    if (-not $Scanned.Add($Binary)) { continue }
    $Dependencies = & $ObjDump -p $Binary |
        Select-String -Pattern 'DLL Name:\s*(.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
    if ($LASTEXITCODE -ne 0) { throw "objdump failed while inspecting $Binary" }
    foreach ($Dependency in $Dependencies) {
        $Key = $Dependency.ToLowerInvariant()
        if (-not $AvailableDlls.ContainsKey($Key)) { continue }
        $Destination = Join-Path $BinDirectory $Dependency
        if (-not (Test-Path -LiteralPath $Destination)) {
            Copy-Item -LiteralPath $AvailableDlls[$Key] -Destination $Destination
            $Queue.Enqueue($Destination)
        }
    }
}

# The update coordinator copies these next to the updater, so that the updater does not
# run on DLLs of the installation it replaces and can delete all of it afterwards.
$UpdaterDirectory = Join-Path $BinDirectory "updater"
$UpdaterRuntime = [Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$UpdaterQueue = [Collections.Generic.Queue[string]]::new()
$UpdaterQueue.Enqueue((Join-Path $UpdaterDirectory "RosPlotJugglerUpdater.exe"))
while ($UpdaterQueue.Count -gt 0) {
    $Binary = $UpdaterQueue.Dequeue()
    $Dependencies = & $ObjDump -p $Binary |
        Select-String -Pattern 'DLL Name:\s*(.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() }
    if ($LASTEXITCODE -ne 0) { throw "objdump failed while inspecting $Binary" }
    foreach ($Dependency in $Dependencies) {
        $Packaged = Join-Path $BinDirectory $Dependency
        if ((Test-Path -LiteralPath $Packaged -PathType Leaf) -and $UpdaterRuntime.Add($Dependency)) {
            $UpdaterQueue.Enqueue($Packaged)
        }
    }
}
Set-Content -LiteralPath (Join-Path $UpdaterDirectory "runtime-dlls.txt") `
    -Value @($UpdaterRuntime) -Encoding ASCII

@"
RosPlotJuggler Studio $Version portable for Windows x86_64

Run bin\RosPlotJugglerStudio.exe. The package includes Qt/MinGW runtime DLLs,
the plugin directory, and the Python ROS1/ROS2 bag runtime.
"@ | Set-Content -LiteralPath (Join-Path $PackageRoot "README_PORTABLE.txt") -Encoding UTF8

& (Join-Path $PSScriptRoot "generate-portable-manifest.ps1") `
    -PackageRoot $PackageRoot -VersionFile $VersionFile
& (Join-Path $PSScriptRoot "smoke-test-portable.ps1") -PackageRoot $PackageRoot

# Updates extract the archive below "<install parent>\.RosPlotJugglerStudio-new-<8 hex>\" with
# Windows PowerShell, whose file paths must stay below 260 characters and directory paths
# below 248. These limits keep updates working from install parents up to 80 characters.
$PackageBase = (Split-Path -Parent $PackageRoot).Length + 1
foreach ($Limit in @(@{ Kind = "file"; File = $true; Maximum = 143 },
                     @{ Kind = "directory"; File = $false; Maximum = 131 })) {
    $LongestPath = Get-ChildItem -LiteralPath $PackageRoot -Recurse -Force |
        Where-Object { $_.PSIsContainer -ne $Limit.File } |
        ForEach-Object { $_.FullName.Substring($PackageBase) } |
        Sort-Object Length -Descending | Select-Object -First 1
    if ($LongestPath.Length -gt $Limit.Maximum) {
        throw "Package $($Limit.Kind) path is too long for in-place updates ($($LongestPath.Length) > $($Limit.Maximum)): $LongestPath"
    }
}

# Compress-Archive in Windows PowerShell 5.1 stores "\" separators, which the update
# extractor (and the ZIP specification) treat as part of a file name.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ArchiveBase = (Split-Path -Parent $PackageRoot).Length + 1
$ArchiveStream = [IO.File]::Open($ArchivePath, [IO.FileMode]::CreateNew)
try {
    $Archive = [IO.Compression.ZipArchive]::new($ArchiveStream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($Item in Get-ChildItem -LiteralPath $PackageRoot -Recurse -Force) {
            $EntryName = $Item.FullName.Substring($ArchiveBase).Replace("\", "/")
            if ($Item.PSIsContainer) {
                if (-not (Get-ChildItem -LiteralPath $Item.FullName -Force | Select-Object -First 1)) {
                    [void]$Archive.CreateEntry("$EntryName/")
                }
                continue
            }
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $Archive, $Item.FullName, $EntryName, [IO.Compression.CompressionLevel]::Optimal)
        }
    }
    finally { $Archive.Dispose() }
}
finally { $ArchiveStream.Dispose() }
$ArchiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
"$ArchiveHash  $ArchiveName" | Set-Content -LiteralPath (Join-Path $ReleaseDirectory "SHA256SUMS") -Encoding ASCII
"$ArchiveHash  $ArchiveName" | Set-Content -LiteralPath "$ArchivePath.sha256" -Encoding ASCII

$ReleaseTag = "v$Version"
$UpdateManifest = [ordered]@{
    schemaVersion = 1
    version = $Version
    platform = "windows"
    arch = "x86_64"
    channel = $Channel
    publishedAt = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    minimumSupportedVersion = $MinimumSupportedVersion
    assetUrl = "$($RepositoryUrl.TrimEnd('/'))/releases/download/$ReleaseTag/$ArchiveName"
    releaseNotesUrl = "$($RepositoryUrl.TrimEnd('/'))/releases/tag/$ReleaseTag"
    size = (Get-Item -LiteralPath $ArchivePath).Length
    sha256 = $ArchiveHash
}
$UpdateManifest | ConvertTo-Json -Depth 3 |
    Set-Content -LiteralPath $UpdateManifestPath -Encoding UTF8

Write-Host "Portable package: $ArchivePath"
Write-Host "SHA-256: $ArchiveHash"
Write-Host "Update manifest: $UpdateManifestPath"
