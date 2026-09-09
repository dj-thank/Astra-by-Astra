[CmdletBinding()]
param([string]$Label='unified-world',[string]$PackageName='Star-Win64-unified-world-r1',[switch]$Editor,[int]$Seconds=125,[switch]$NightLighting,[switch]$NoEnvironment,[switch]$LegacyExposure,[switch]$ExposureReadback)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if($Label -notmatch '^[a-z0-9-]+$' -or $PackageName -notmatch '^[A-Za-z0-9-]+$'){throw 'Use simple capture names'}
$out=Join-Path $root "work/unified-world/$Label"
if(Test-Path "$out/process.json"){throw 'Preserve prior capture evidence'}
New-Item -ItemType Directory -Force $out | Out-Null
$env:__COMPAT_LAYER='HIGHDPIAWARE'
$shot=[Math]::Min(170,$Seconds-10)
$launchArgs=@('-windowed','-RenderOffscreen','-NoVSync','-NoSound','-ForceRes','-ResX=3840','-ResY=2160','-unattended','-nosplash',
 '-StarBenchmark','-StarBenchmarkUI','-StarBenchmarkStart=0','-StarBenchmarkCount=1',"-StarBenchmarkSeconds=$Seconds","-StarBenchmarkShot=$shot",
 '-StarUnifiedWorldQA','-StarClockRate=600','-StarUtc=2026-09-19T00:00:00Z','-StarBenchmarkThrottle=0',
 ('-StarBenchmarkPath="'+$out+'"'),('-UserDir="'+$root+'/work/unified-world/editor-unified-r1/user"'),('-abslog="'+$out+'/game.log"'),'"-ExecCmds=DisableAllScreenMessages,t.MaxFPS 0"')
if($NightLighting){$launchArgs+='-StarNightLightingQA'}
if($NoEnvironment){$launchArgs+='-StarNoEnvironmentBounce'}
if($LegacyExposure){$launchArgs+='-StarLegacyNightExposure'}
if($ExposureReadback){$launchArgs+='-StarExposureReadback'}
if($Editor){
 $engine=& "$PSScriptRoot/Find-Engine.ps1"
 $exe=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe'
 $launchArgs=@(('"'+$root+'/Star.uproject"'),'-game')+$launchArgs
}else{$exe=Join-Path $root "outputs/$PackageName/Windows/Star/Binaries/Win64/Star.exe"}
$p=Start-Process -FilePath $exe -ArgumentList $launchArgs -WorkingDirectory $root -WindowStyle Hidden -PassThru
$receipt=@{pid=$p.Id;created=$p.StartTime.ToUniversalTime().ToString('o');exe=$exe;packaged=(!$Editor);normalStart=$true;geographicPreset=$false;physicalInput=$false}
$receipt | ConvertTo-Json | Set-Content "$out/process.json" -Encoding utf8
if(!$p.WaitForExit(($Seconds+360)*1000)){throw "Capture still running: $($p.Id); inspect its receipt before stopping"}
$receipt.exitCode=$p.ExitCode;$receipt|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if($p.ExitCode -ne 0){throw 'Unified-world runtime exited unsuccessfully'}
