[CmdletBinding()]
param([string]$Label='local-flight',[switch]$Editor,[string]$PackageName='Star-Win64-local-drive',[string]$EngineRoot='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if($Label -notmatch '^[a-z0-9-]+$'){throw 'Use a simple label'}
$out=Join-Path $root "work/local-flight/$Label"
if(Test-Path $out){throw 'Preserve previous run evidence'}
New-Item -ItemType Directory $out -Force | Out-Null
$launchArgs=@('-windowed','-RenderOffscreen','-NoVSync','-NoSound','-ForceRes','-ResX=3840','-ResY=2160','-unattended','-nosplash',
 '-StarLocalFlightQA','-StarUtc=2026-09-19T00:00:00Z',('-StarNavigationPath="'+$out+'/capture"'),
 ('-UserDir="'+$root+'/work/unified-world/editor-unified-r1/user"'),('-abslog="'+$out+'/game.log"'),
 '"-ExecCmds=DisableAllScreenMessages,t.MaxFPS 0"')
if($Editor){$engine=& "$PSScriptRoot/Find-Engine.ps1" -EngineRoot $EngineRoot;$exe="$engine/Engine/Binaries/Win64/UnrealEditor.exe";$launchArgs=@(('"'+$root+'/Star.uproject"'),'-game')+$launchArgs}
else{$exe=Join-Path $root "outputs/$PackageName/Windows/Star/Binaries/Win64/Star.exe"}
$env:__COMPAT_LAYER='HIGHDPIAWARE'
$p=Start-Process -FilePath $exe -ArgumentList $launchArgs -WorkingDirectory $root -WindowStyle Hidden -PassThru
$receipt=@{pid=$p.Id;created=$p.StartTime.ToUniversalTime().ToString('o');exe=$exe;packaged=(!$Editor);scriptedInput=$true;physicalInput=$false}
$receipt|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if(!$p.WaitForExit(600000)){throw "QA still running: PID $($p.Id), inspect receipt before stopping"}
$receipt.exitCode=$p.ExitCode;$receipt|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
$result=Get-Content "$out/capture/result.json" -Raw -Encoding utf8 | ConvertFrom-Json
if($p.ExitCode -ne 0 -or !$result.success){throw "Local drive runtime failed: $($result.reason)"}
Write-Output "PASS local driving runtime: $out"
