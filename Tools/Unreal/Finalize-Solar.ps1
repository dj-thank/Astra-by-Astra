param([Parameter(Mandatory)][int]$ImportPid,[Parameter(Mandatory)][string]$ImportStartedUtc,[Parameter(Mandatory)][string]$ImportLog,[switch]$Delivery)
$ErrorActionPreference='Stop'
$p=Get-Process -Id $ImportPid -ErrorAction SilentlyContinue
if($p){
 if($p.StartTime.ToUniversalTime().ToString('o') -ne $ImportStartedUtc){throw 'Import PID was reused'}
 Wait-Process -Id $ImportPid -Timeout 600
}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$log=Get-Content -LiteralPath (Join-Path $root $ImportLog) -Raw
if($log -notmatch 'STAR SolarMotion assets saved' -or $log -match 'Python script executed with errors'){throw 'Solar material import did not complete'}
& (Join-Path $PSScriptRoot 'Build-SolarPackage.ps1') -SkipBuild
if($Delivery){& (Join-Path $PSScriptRoot 'Run-SolarValidation.ps1') -Delivery}
else{& (Join-Path $PSScriptRoot 'Run-SolarValidation.ps1') -FinalArt}
