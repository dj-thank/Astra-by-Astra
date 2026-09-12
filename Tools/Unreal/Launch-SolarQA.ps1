param([switch]$Editor,[switch]$Legacy,[switch]$Movie,[switch]$Lifecycle,[switch]$NormalStart,[double]$Seconds=64,[double]$StartR=120,[double]$SpeedR=4,[string]$Name='solar-current',[int]$Width=3840,[int]$Height=2160)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=Join-Path $root ('work/solar-qa/'+$Name)
if(Test-Path -LiteralPath $output){throw 'Choose a fresh QA output name'}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$engine=& (Join-Path $PSScriptRoot 'Find-Engine.ps1')
$launchArgs=@()
if($Editor){$exe=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe';$launchArgs+=@((Join-Path $root 'Star.uproject'),'-game')}
else {$exe=Join-Path $root 'Saved/StagedBuilds/Windows/Star/Binaries/Win64/Star.exe'}
$launchArgs+=@('-StarBenchmark','-StarBenchmarkCount=1',"-StarBenchmarkSeconds=$Seconds","-StarBenchmarkShot=$($Seconds-2)","-StarBenchmarkPath=$output",'-StarSunQA',"-StarSunStartR=$StartR","-StarSunSpeedR=$SpeedR",'-StarUtc=2026-09-11T14:04:00Z','-StarClockRate=1','-dx12','-windowed',"-ResX=$Width","-ResY=$Height",'-ForceRes','-unattended','-nosplash','-nosound',"-UserDir=$output/user", "-abslog=$output/game.log",'-ExecCmds="r.ScreenPercentage 100,r.VSync 0,t.MaxFPS 30,csv.GpuStatsEnabled 1"')
if($Legacy){$launchArgs+='-StarLegacySun'}
if($Movie){$launchArgs+='-StarSunMovie'}
if($Lifecycle){$launchArgs+='-StarSunLifecycleQA'}
$launchArgs+=@('-UseFixedTimeStep','-FPS=30')
if($NormalStart){
    $launchArgs=@($launchArgs|Where-Object {$_ -ne '-StarSunQA' -and $_ -ne '-StarClockRate=1'})
    $launchArgs+=@('-StarUnifiedWorldQA','-StarEarthFlightQA','-StarClockRate=600')
}
$receipt=& 'C:/Users/rambo/.codex/scripts/Start-CodexTrackedProcess.ps1' -TaskId "star-$Name" -Owner '01a094b3-d88a-7df1-b47f-ce75d5e6b83b' -WorkingDirectory $root -FilePath $exe -ArgumentList $launchArgs -TtlHours 1
$r=$receipt|ConvertFrom-Json
$process=Get-Process -Id $r.pid
[ordered]@{pid=$r.pid;createdAt=$process.StartTime.ToUniversalTime().ToString('o');executable=$exe;packaged=(!$Editor);legacy=[bool]$Legacy;movie=[bool]$Movie;requestedResolution=@($Width,$Height);binarySha256=(Get-FileHash -LiteralPath $exe).Hash}|ConvertTo-Json|Set-Content -Encoding utf8 (Join-Path $output 'process.json')
$receipt
