$ErrorActionPreference='Stop'
$root=Resolve-Path (Join-Path $PSScriptRoot '../../..')
$cache=Join-Path $root 'work/lunar-source'
New-Item -ItemType Directory -Force $cache | Out-Null
$m=Invoke-RestMethod 'https://api.polyhaven.com/files/gravelly_sand'
$m | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $cache 'files.json') -Encoding utf8
$sum=0
foreach($kind in @('Diffuse','nor_dx','Rough')) {$sum+=$m.$kind.'2k'.png.size}
if($sum -gt 200MB) {throw 'Download budget exceeded'}
foreach($kind in @('Diffuse','nor_dx','Rough')) {
 $d=$m.$kind.'2k'.png
 $p=Join-Path $cache ([IO.Path]::GetFileName($d.url))
 if(!(Test-Path $p)){Invoke-WebRequest $d.url -OutFile $p}
 if((Get-FileHash $p -Algorithm MD5).Hash.ToLower() -ne $d.md5){throw 'Source hash mismatch'}
}
