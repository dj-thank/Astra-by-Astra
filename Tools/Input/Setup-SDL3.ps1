[CmdletBinding()]
param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$version = '3.4.16'
$sha256 = '1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de'
$url = "https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL3-devel-$version-VC.zip"
$cache = Join-Path $ProjectRoot 'work/input-downloads'
$archive = Join-Path $cache "SDL3-devel-$version-VC.zip"
$unpacked = Join-Path $cache "SDL3-$version"
$destination = Join-Path $ProjectRoot "Plugins/StarFlightInput/ThirdParty/SDL3"
New-Item -ItemType Directory -Path $cache -Force | Out-Null
if (!(Test-Path -LiteralPath $archive)) { Invoke-WebRequest $url -OutFile $archive }
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha256) {
    throw "SDL archive hash mismatch: $archive. No files installed."
}
Expand-Archive -LiteralPath $archive -DestinationPath $unpacked -Force
$source = Join-Path $unpacked "SDL3-$version"
New-Item -ItemType Directory -Path (Join-Path $destination 'include'),(Join-Path $destination 'lib/Win64'),(Join-Path $destination 'bin/Win64') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'include/SDL3') -Destination (Join-Path $destination 'include') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $source 'lib/x64/SDL3.lib') -Destination (Join-Path $destination 'lib/Win64/SDL3.lib') -Force
Copy-Item -LiteralPath (Join-Path $source 'lib/x64/SDL3.dll') -Destination (Join-Path $destination 'bin/Win64/SDL3.dll') -Force
Copy-Item -LiteralPath (Join-Path $source 'LICENSE.txt') -Destination (Join-Path $destination 'LICENSE.txt') -Force
foreach ($relative in @('lib/x64/SDL3.lib','lib/x64/SDL3.dll')) {
    $target = if ($relative.EndsWith('.lib')) { 'lib/Win64/SDL3.lib' } else { 'bin/Win64/SDL3.dll' }
    if ((Get-FileHash (Join-Path $source $relative)).Hash -ne (Get-FileHash (Join-Path $destination $target)).Hash) { throw 'SDL copy verification failed' }
}
[ordered]@{version=$version;url=$url;archiveSha256=$sha256;architecture='x64';sourceLicense='zlib';librarySha256=(Get-FileHash (Join-Path $destination 'lib/Win64/SDL3.lib')).Hash.ToLowerInvariant();dllSha256=(Get-FileHash (Join-Path $destination 'bin/Win64/SDL3.dll')).Hash.ToLowerInvariant()} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'provenance.json') -Encoding utf8NoBOM
Write-Output "SDL $version headers, Win64 import library and DLL verified at $destination"
