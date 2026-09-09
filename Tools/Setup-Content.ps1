[CmdletBinding()]
param([string]$ArchivePath='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifest=Get-Content (Join-Path $root 'Data/content-release.json') -Raw -Encoding utf8 | ConvertFrom-Json
$cache=Join-Path $root 'work/downloads'
New-Item -ItemType Directory -Force $cache | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$specs=if($manifest.PSObject.Properties['archives']){@($manifest.archives)}else{@($manifest)}
if($ArchivePath -and $specs.Count -ne 1){throw '-ArchivePath is for a single-archive manifest; place both release ZIPs in work/downloads.'}
$opened=@()
try{
 $entries=@()
 foreach($spec in $specs){
  $archive=if($ArchivePath){$ArchivePath}else{Join-Path $cache $spec.name}
  if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $spec.url -OutFile $archive}
  if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $spec.sha256){throw 'Content checksum mismatch; nothing extracted.'}
  $zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($archive));$opened+=,$zip
 foreach($entry in $zip.Entries){
  if(!$entry.Name){continue}
  if($entry.FullName -notmatch '^(Content|Art)/' -or $entry.FullName -match '(^|/)\.\.(/|$)' -or $entry.FullName.Contains('\')){throw 'Unexpected archive path'}
  $target=[IO.Path]::GetFullPath((Join-Path $root $entry.FullName))
  if(!$target.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Archive path leaves the project'}
  if(Test-Path -LiteralPath $target){
   $stream=$entry.Open();try{$digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()}
   if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $digest){throw "Edited file preserved: $($entry.FullName). Use a fresh clone."}
  }else{$entries+=@{entry=$entry;target=$target}}
 }
 }
 foreach($item in $entries){
  New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($item.target)) | Out-Null
  [IO.Compression.ZipFileExtensions]::ExtractToFile($item.entry,$item.target,$false)
 }
}finally{foreach($zip in $opened){$zip.Dispose()}}
Write-Output 'Content verified and installed.'
