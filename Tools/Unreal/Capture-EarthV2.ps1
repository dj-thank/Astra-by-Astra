[CmdletBinding()]
param([ValidateSet('day','limb','night','close','clouds','orbit','sunrise','sunset')][string]$Scene='day',[string]$Label='day',[switch]$Editor,[ValidateRange(0,120)][double]$ViewSeconds=0,[switch]$LegacyAtmosphere,[switch]$LegacyClouds,[switch]$LegacyDetails,[ValidateRange(640,7680)][int]$Width=3840,[ValidateRange(480,4320)][int]$Height=2160)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if($Label -notmatch '^[a-z0-9-]+$'){throw 'Use a simple capture label'}
$destination=Join-Path $root "work/earth-v2/$Label"
New-Item -ItemType Directory -Force $destination | Out-Null
$env:__COMPAT_LAYER='HIGHDPIAWARE'
$flags=@('-windowed','-RenderOffscreen',"-ResX=$Width","-ResY=$Height",'-ForceRes','-unattended','-nosplash','-NoSound',
    '-StarBenchmark','-StarBenchmarkStart=0','-StarBenchmarkCount=1','-StarBenchmarkSeconds=22','-StarBenchmarkShot=16',
    '-StarTextureReadback','-StarWeatherTime=0',"-StarBenchmarkEarth=$Scene","-StarBenchmarkPath=$destination","-abslog=$destination/game.log")
if($Scene -eq 'orbit'){$flags+=@('-StarBenchmarkEarthOrbit',"-StarEarthViewSeconds=$ViewSeconds")}
if($Scene -in @('sunrise','sunset')){
    $flags=@($flags | Where-Object {$_ -notlike '-StarBenchmarkEarth=*'})
    $flags+=@("-StarBenchmark$Scene","-StarEarthViewSeconds=$ViewSeconds")
}
if($LegacyAtmosphere){$flags+='-StarLegacyEarthAtmosphere'}
if($LegacyClouds){$flags+='-StarLegacyClouds'}
if($LegacyDetails){$flags+='-StarLegacyEarthDetails'}
if($Editor){
    $engine=& (Join-Path $PSScriptRoot 'Find-Engine.ps1')
    $exe=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe'
    $flags=@((Join-Path $root 'Star.uproject'),'-game')+$flags
}else{$exe=Join-Path $root 'outputs/Star-Win64-earth-v2/Windows/Star/Binaries/Win64/Star.exe'}
$p=Start-Process $exe -ArgumentList $flags -WorkingDirectory $root -WindowStyle Hidden -PassThru
$receipt=@{pid=$p.Id;created=$p.StartTime.ToUniversalTime().ToString('o');owner='earth-v2';scene=$Scene;viewSeconds=$ViewSeconds;exe=$exe;packaged=(!$Editor);stop='benchmark natural exit';physicalInput=$false;requestedViewport=@($Width,$Height);legacyAtmosphere=[bool]$LegacyAtmosphere;legacyClouds=[bool]$LegacyClouds;legacyDetails=[bool]$LegacyDetails}
$receipt | ConvertTo-Json | Set-Content "$destination/process.json" -Encoding utf8
if(!$p.WaitForExit(240000)){throw "Capture still running: PID $($p.Id). Check its receipt before scoped cleanup."}
$receipt.exitCode=$p.ExitCode
$receipt.screenshotExists=Test-Path "$destination/scene-0.png"
$receipt | ConvertTo-Json | Set-Content "$destination/process.json" -Encoding utf8
if($p.ExitCode -ne 0 -or !$receipt.screenshotExists){throw 'Game capture did not complete'}
