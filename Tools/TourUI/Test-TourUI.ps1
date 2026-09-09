[CmdletBinding()]
param([string]$CaptureDirectory='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if($CaptureDirectory){
 $env:PYTHONUTF8='1'
 & python "$root/Tools/Unreal/Verify-UnifiedWorld.py" $CaptureDirectory
 if($LASTEXITCODE -ne 0){throw 'Captured world/guide checks failed'}
}else{
 & "$root/Tools/Simulation/Test-WorldContinuity.ps1"
 Write-Output 'Native world tests passed. Supply -CaptureDirectory for actual runtime/guide checks.'
}
