# SPDX-License-Identifier: MPL-2.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][string]$Destination
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Archive = [IO.Path]::GetFullPath($Archive)
$Destination = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
[IO.Directory]::CreateDirectory($Destination) | Out-Null
$Prefix = $Destination + [IO.Path]::DirectorySeparatorChar
$Stream = [IO.File]::Open($Archive, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    $Zip = [IO.Compression.ZipArchive]::new($Stream, [IO.Compression.ZipArchiveMode]::Read, $false)
    try {
        foreach ($Entry in $Zip.Entries) {
            $Name = $Entry.FullName
            if ([string]::IsNullOrWhiteSpace($Name) -or
                $Name.StartsWith('/') -or $Name.StartsWith('\') -or
                $Name.Contains(':') -or [IO.Path]::IsPathRooted($Name)) {
                throw "Unsafe absolute archive entry: $Name"
            }

            # Unix file type is stored in the high 16 bits; 0xA000 is a symbolic link.
            $UnixMode = (($Entry.ExternalAttributes -shr 16) -band 0xF000)
            $DosAttributes = ($Entry.ExternalAttributes -band 0xFFFF)
            if ($UnixMode -eq 0xA000 -or
                (($DosAttributes -band [int][IO.FileAttributes]::ReparsePoint) -ne 0)) {
                throw "Links/reparse points are not permitted in update archives: $Name"
            }

            $Relative = $Name.Replace('/', [IO.Path]::DirectorySeparatorChar)
            $Target = [IO.Path]::GetFullPath([IO.Path]::Combine($Destination, $Relative))
            if (-not $Target.StartsWith($Prefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Archive entry escapes destination: $Name"
            }
            if ($Name.EndsWith('/')) {
                [IO.Directory]::CreateDirectory($Target) | Out-Null
                continue
            }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Target)) | Out-Null
            $Input = $Entry.Open()
            try {
                $Output = [IO.File]::Open($Target, [IO.FileMode]::CreateNew,
                                         [IO.FileAccess]::Write, [IO.FileShare]::None)
                try { $Input.CopyTo($Output) } finally { $Output.Dispose() }
            }
            finally { $Input.Dispose() }
        }
    }
    finally { $Zip.Dispose() }
}
finally { $Stream.Dispose() }
