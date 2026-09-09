[CmdletBinding()]
param([ValidateSet('sunrise','sunset')][string]$Scene='sunrise',[double]$ViewSeconds=0,[string]$Label='sunrise-0')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=Join-Path $root 'outputs/Star-Win64-sun-polish/Windows/Star/Binaries/Win64/Star.exe'
if($Label -notmatch '^[a-z0-9-]+$'){throw 'Use a simple local capture label'}
$destination=Join-Path $root "work/sun-polish/$Label"
New-Item -ItemType Directory -Force $destination | Out-Null
$env:__COMPAT_LAYER='HIGHDPIAWARE'
$flags=@('-windowed','-RenderOffscreen','-ResX=3840','-ResY=2160','-ForceRes','-unattended','-nosplash','-NoSound',
    '-StarBenchmark','-StarBenchmarkStart=0','-StarBenchmarkCount=1','-StarBenchmarkSeconds=18','-StarBenchmarkShot=12',
    "-StarBenchmark$Scene","-StarEarthViewSeconds=$ViewSeconds","-StarBenchmarkPath=$destination","-abslog=$destination/game.log")
$process=Start-Process -FilePath $exe -ArgumentList $flags -WorkingDirectory $root -WindowStyle Hidden -PassThru
$receipt=@{pid=$process.Id;created=$process.StartTime.ToUniversalTime().ToString('o');owner='sun-polish';scene=$Scene;viewSeconds=$ViewSeconds;exe=$exe;stop='benchmark natural exit';physicalInput=$false}
$receipt | ConvertTo-Json | Set-Content "$destination/process.json" -Encoding utf8
if(!$process.WaitForExit(180000)){throw "Capture still running: PID $($process.Id). See process.json before scoped cleanup."}
$receipt.exitCode=$process.ExitCode
$receipt.screenshotExists=Test-Path "$destination/scene-0.png"
$receipt | ConvertTo-Json | Set-Content "$destination/process.json" -Encoding utf8
if($process.ExitCode -ne 0 -or !$receipt.screenshotExists){throw 'Game capture did not complete'}
