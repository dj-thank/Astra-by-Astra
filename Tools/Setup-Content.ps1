[CmdletBinding()]
param([string]$ArchivePath='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifest=Get-Content (Join-Path $root 'Data/content-release.json') -Raw -Encoding utf8 | ConvertFrom-Json
$cache=Join-Path $root 'work/downloads'
New-Item -ItemType Directory -Force $cache | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$specs=if($manifest.PSObject.Properties['archives']){@($manifest.archives)}else{@($manifest)}
if(!$specs.Count){throw 'Empty content archive manifest.'}
if($ArchivePath -and $specs.Count -ne 1){throw '-ArchivePath is for a single-archive manifest; place both release ZIPs in work/downloads.'}

function Assert-PortableSegment([string]$Segment) {
    # Windows aliases (ADS, device names and trailing dots/spaces) are not distinct assets.
    if(!$Segment -or $Segment -eq '.' -or $Segment -eq '..' -or
       $Segment -match '[<>:"\\|?*\x00-\x1f]' -or $Segment -match '[. ]$' -or
       $Segment -match '^(CON|PRN|AUX|NUL|COM[1-9\u00b9\u00b2\u00b3]|LPT[1-9\u00b9\u00b2\u00b3])(?:\.|$)') {
        throw "Ambiguous archive path segment: $Segment"
    }
}
function Assert-ExistingPath([string]$Relative,[bool]$Directory) {
    $parts=$Relative.Split('/')
    $current=$root
    for($i=0;$i -lt $parts.Count;$i++) {
        $current=Join-Path $current $parts[$i]
        $item=Get-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue
        if($null -eq $item){continue}
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked content path refused: $current"}
        $needsDirectory=$Directory -or $i -lt $parts.Count-1
        if($needsDirectory -ne [bool]$item.PSIsContainer){throw "File/directory conflict: $current"}
    }
}

$opened=@()
$files=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$directories=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
try {
    $entries=[Collections.Generic.List[object]]::new()
    foreach($spec in $specs) {
        if(!$ArchivePath) {
            Assert-PortableSegment $spec.name
            if($spec.name.Contains('/')){throw 'Archive name must be a filename.'}
        }
        $archive=if($ArchivePath){$ArchivePath}else{Join-Path $cache $spec.name}
        if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $spec.url -OutFile $archive}
        if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $spec.sha256){throw 'Content checksum mismatch; nothing extracted.'}
        $zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($archive));$opened+=,$zip
        foreach($entry in $zip.Entries) {
            if($entry.FullName -cnotmatch '^(Content|Art)/'){throw 'Unexpected archive path'}
            if((($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000){throw 'Archive symlinks are not content files.'}
            $isDirectory=$entry.FullName.EndsWith('/')
            $relative=if($isDirectory){$entry.FullName.Substring(0,$entry.FullName.Length-1)}else{$entry.FullName}
            $parts=$relative.Split('/')
            foreach($part in $parts){Assert-PortableSegment $part}
            $target=[IO.Path]::GetFullPath((Join-Path $root $relative))
            if(!$target.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Archive path leaves the project'}

            # Build a single plan across ALL ZIPs before writing any project assets.
            $parent=''
            for($i=0;$i -lt $parts.Count-1;$i++) {
                $parent=if($parent){$parent+'/'+$parts[$i]}else{$parts[$i]}
                if($files.Contains($parent)){throw "Archive file/directory conflict: $parent"}
                [void]$directories.Add($parent)
            }
            if($isDirectory) {
                if($files.Contains($relative)){throw "Archive file/directory conflict: $relative"}
                [void]$directories.Add($relative)
            } elseif($directories.Contains($relative) -or !$files.Add($relative)) {
                throw "Duplicate or conflicting archive destination: $relative"
            }
            Assert-ExistingPath $relative $isDirectory
            if($isDirectory){continue}
            if(Test-Path -LiteralPath $target) {
                $stream=$entry.Open()
                try{$digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()}
                if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $digest){throw "Edited file preserved: $relative. Use a fresh clone."}
            } else {
                $entries.Add(@{entry=$entry;target=$target;relative=$relative})
            }
        }
    }
    foreach($item in $entries) {
        # Recheck to catch ordinary filesystem changes since preflight; never overwrite.
        Assert-ExistingPath $item.relative $false
        New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($item.target)) | Out-Null
        Assert-ExistingPath $item.relative $false
        [IO.Compression.ZipFileExtensions]::ExtractToFile($item.entry,$item.target,$false)
    }
} finally {foreach($zip in $opened){$zip.Dispose()}}
Write-Output 'Content verified and installed.'
