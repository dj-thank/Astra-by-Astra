[CmdletBinding()]
param([ValidateSet('day','limb','night','close','flight','nightflight','clouds','orbit','sunrise','sunset')][string]$Scene='day',[string]$Label='day',[switch]$Editor,[ValidateRange(0,120)][double]$ViewSeconds=0,[switch]$LegacyAtmosphere,[switch]$LegacyClouds,[switch]$LegacyDetails,[ValidateRange(640,7680)][int]$Width=3840,[ValidateRange(480,4320)][int]$Height=2160,[switch]$Chase,[switch]$Cockpit,[switch]$KeepShip,[switch]$Motion,[switch]$ControlsQA,[switch]$Journey,[double]$MotionStart=8,[double]$MotionEnd=20,[ValidateRange(12,360)][int]$DurationSeconds=40,[int]$ShotSeconds=32,[double]$LookPitch=0,[double]$LookYaw=0,[ValidateSet('baseline','no-clouds','no-atmosphere','no-surface-photo','bare')][string]$Layer,[ValidateRange(-2,18)][double]$ExposureEV=13,[ValidatePattern('^[A-Za-z0-9-]+$')][string]$PackageName='Star-Win64-earth-flight',[string]$UserDirectory='',[string]$Utc='2026-09-19T00:00:00Z',[ValidateSet(0,1,60,600)][double]$ClockRate=1,[switch]$TimeChecks,[ValidateSet('Default','Conventional','Disabled')][string]$ShadowMode='Default',[ValidateRange(0,120)][int]$FrameLimit=0,[switch]$VR,[switch]$VRMenuCheck,[switch]$Audio)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Scene=$Scene.ToLowerInvariant()
if($Motion -and ($MotionStart -lt 0 -or $MotionEnd -le $MotionStart -or $MotionEnd -ge $DurationSeconds)){throw 'Motion interval must fit inside the flight duration.'}
if($Label -notmatch '^[a-z0-9-]+$'){throw 'Use a simple capture label'}
$destination=Join-Path $root "work/earth-flight/$Label"
if(Test-Path (Join-Path $destination 'flight-trajectory.jsonl')){throw 'This label already contains a flight trajectory. Use a new label to preserve prior evidence.'}
New-Item -ItemType Directory -Force $destination | Out-Null
$env:__COMPAT_LAYER='HIGHDPIAWARE'
$flightScenes=@('flight','nightflight','sunrise','sunset','orbit')
$pose=if($Scene -in @('sunrise','sunset','orbit')){$Scene+'flight'}else{$Scene}
$flags=@('-windowed','-NoVSync','-RenderOffscreen',"-ResX=$Width","-ResY=$Height",'-ForceRes','-unattended','-nosplash','-NoSound',
    '-StarBenchmark','-StarBenchmarkStart=0','-StarBenchmarkCount=1',"-StarBenchmarkSeconds=$DurationSeconds","-StarBenchmarkShot=$ShotSeconds",
    '-StarTextureReadback','-StarWeatherTime=0',"-StarBenchmarkEarth=$pose","-StarBenchmarkPath=$destination","-abslog=$destination/game.log")
if($Audio){$flags=@($flags | Where-Object {$_ -ne '-NoSound'})}
$flags+=@("-StarUtc=$Utc","-StarClockRate=$ClockRate")
if($TimeChecks){$flags+='-StarTimeQA'}
if($UserDirectory){$userPath=[IO.Path]::GetFullPath($UserDirectory); if(!$userPath.StartsWith((Join-Path $root 'work')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'QA user directory must be inside work'};$flags+=('-UserDir="'+$userPath+'"')}
if($PSBoundParameters.ContainsKey('LookPitch')){$flags+="-StarBenchmarkPitch=$LookPitch"}
if($PSBoundParameters.ContainsKey('LookYaw')){$flags+="-StarBenchmarkYaw=$LookYaw"}
$flags+='-ExecCmds=DisableAllScreenMessages'
if($LegacyAtmosphere){$flags+='-StarLegacyEarthAtmosphere'}
if($LegacyClouds){$flags+='-StarLegacyClouds'}
if($LegacyDetails){$flags+='-StarLegacyEarthDetails'}
if($Scene -in $flightScenes){
    $flags+=@('-StarEarthFlightQA','-StarBenchmarkThrottle=0.8','-StarBenchmarkUI')
    if($Chase){$flags+='-StarEarthFlightChase'}
    if($Cockpit){$flags+='-StarEarthFlightCockpit'}
}
if($Motion){$flags+=@('-StarEarthMotionQA',"-StarMotionStart=$MotionStart","-StarMotionEnd=$MotionEnd")}
if($Journey){$flags+='-StarScenicJourneyQA'}
if($ControlsQA){$flags+='-StarScenicControlsQA'}
if($env:STAR_NATIVE_ATMOSPHERE -eq '1'){$flags+='-StarNativeAtmosphere'}
if($env:STAR_NATIVE_ATMOSPHERE -eq '0'){$flags+='-StarSingleScatterAtmosphere'}
if($Layer){
    if($KeepShip){$flags+='-StarEarthKeepShip'}
    $flags=@($flags | Where-Object {$_ -notlike '-StarBenchmarkSeconds=*' -and $_ -notlike '-StarBenchmarkShot=*' -and $_ -notlike '-StarBenchmarkThrottle=*' -and $_ -ne '-StarBenchmarkUI'})
    $flags+=@('-StarEarthLayerQA','-StarBenchmarkSeconds=12','-StarBenchmarkShot=8','-StarBenchmarkThrottle=0',"-StarEarthExposureEV=$ExposureEV")
    if($Layer -in @('no-clouds','bare')){$flags+='-StarEarthNoClouds'}
    if($Layer -in @('no-atmosphere','bare')){$flags+='-StarEarthNoAtmosphere'}
    if($Layer -in @('no-surface-photo','bare')){$flags+='-StarEarthNoSurfacePhoto'}
}
$flags=@($flags | Where-Object {$_ -notlike '-ExecCmds=*'})
$commands=@('DisableAllScreenMessages',("t.MaxFPS "+$FrameLimit))
if($ShadowMode -eq 'Conventional'){$commands+='r.Shadow.Virtual.Enable 0'}
if($ShadowMode -eq 'Disabled'){$commands+='r.ShadowQuality 0'}
$flags+=('"-ExecCmds='+($commands -join ',')+'"')
if($VR -or $VRMenuCheck){$flags+=@('-vr','-StarVR')}
if($VRMenuCheck){$flags+='-StarVRUIQA'}
if($Editor){
    $engine=& (Join-Path $PSScriptRoot 'Find-Engine.ps1')
    $exe=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe'
    $flags=@((Join-Path $root 'Star.uproject'),'-game')+$flags
}else{$exe=Join-Path $root "outputs/$PackageName/Windows/Star/Binaries/Win64/Star.exe"}
$p=Start-Process $exe -ArgumentList $flags -WorkingDirectory $root -WindowStyle Hidden -PassThru
$receipt=@{pid=$p.Id;created=$p.StartTime.ToUniversalTime().ToString('o');owner='earth-flight';scene=$Scene;viewSeconds=$ViewSeconds;exe=$exe;packaged=(!$Editor);stop='benchmark natural exit';physicalInput=$false;requestedViewport=@($Width,$Height);legacyAtmosphere=[bool]$LegacyAtmosphere;legacyClouds=[bool]$LegacyClouds;legacyDetails=[bool]$LegacyDetails}
$receipt.vrRequested=[bool]($VR -or $VRMenuCheck);$receipt.vrMenuCheck=[bool]$VRMenuCheck;$receipt.requestedFrameLimit=$FrameLimit;$receipt.shadowMode=$ShadowMode
$receipt.nativeAtmosphere=($env:STAR_NATIVE_ATMOSPHERE -ne '0')
$receipt.layerDiagnostic=$Layer;$receipt.fixedExposureEV=$(if($Layer){$ExposureEV}else{$null});
$receipt | ConvertTo-Json | Set-Content "$destination/process.json" -Encoding utf8
if(!$p.WaitForExit([Math]::Max(420000,($DurationSeconds+240)*1000))){throw "Capture still running: PID $($p.Id). Check its receipt before scoped cleanup."}
$receipt.exitCode=$p.ExitCode
$receipt.screenshotExists=Test-Path "$destination/scene-0.png"
$receipt.layerDiagnostic=$Layer;$receipt.fixedExposureEV=$(if($Layer){$ExposureEV}else{$null});
$receipt | ConvertTo-Json | Set-Content "$destination/process.json" -Encoding utf8
$receipt.screenshotFresh=$receipt.screenshotExists -and (Get-Item "$destination/scene-0.png").LastWriteTimeUtc -ge $p.StartTime.ToUniversalTime()
$receipt.layerDiagnostic=$Layer;$receipt.fixedExposureEV=$(if($Layer){$ExposureEV}else{$null});
$receipt | ConvertTo-Json | Set-Content "$destination/process.json" -Encoding utf8
if($p.ExitCode -ne 0 -or !$receipt.screenshotFresh){throw 'Game capture did not complete'}

