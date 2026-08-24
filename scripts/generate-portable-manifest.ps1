# SPDX-License-Identifier: MPL-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageRoot,
    [string]$VersionFile,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $VersionFile) { $VersionFile = Join-Path $ProjectRoot "src\PlotJuggler\STUDIO_VERSION" }
if (-not $OutputPath) { $OutputPath = Join-Path $PackageRoot "manifest.json" }

$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path.TrimEnd("\")
$Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if (-not $Version) { throw "Version file is empty: $VersionFile" }

$Files = Get-ChildItem -LiteralPath $PackageRoot -File -Recurse |
    Where-Object { $_.FullName -ne [IO.Path]::GetFullPath($OutputPath) } |
    ForEach-Object {
        $RelativePath = $_.FullName.Substring($PackageRoot.Length + 1).Replace("\", "/")
        [ordered]@{
            path = $RelativePath
            size = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    } |
    Sort-Object { $_.path }

$Manifest = [ordered]@{
    schemaVersion = 1
    product = "RosPlotJugglerStudio"
    version = $Version
    platform = "windows-x86_64"
    entrypoint = "bin/RosPlotJugglerStudio.exe"
    files = @($Files)
}

$OutputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Portable updater manifest generated: $OutputPath"
