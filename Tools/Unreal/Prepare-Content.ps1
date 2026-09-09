[CmdletBinding()]
param([string]$EngineRoot='')
$ErrorActionPreference='Stop'
$env:__COMPAT_LAYER='HIGHDPIAWARE'
$env:STAR_AUTHOR_RUN_ID=[guid]::NewGuid().ToString()
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$engine=& (Join-Path $PSScriptRoot 'Find-Engine.ps1') -EngineRoot $EngineRoot
New-Item -ItemType Directory -Force (Join-Path $root 'work/earth-time') | Out-Null
$editor=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $editor "$root/Star.uproject" -run=pythonscript "-script=$PSScriptRoot/import_public_audio.py" -unattended -NullRHI -nosplash -nop4 "-abslog=$root/work/public-audio.log"
if($LASTEXITCODE -ne 0){throw 'Audio import failed'}
$receipt=Get-Content "$root/work/public-audio-receipt.json" -Raw -Encoding utf8 | ConvertFrom-Json
if($receipt.runId -ne $env:STAR_AUTHOR_RUN_ID -or $receipt.imported -ne 45){throw 'Missing or stale audio import receipt'}
& $editor "$root/Star.uproject" -run=pythonscript "-script=$PSScriptRoot/apply_vr_panel.py" -unattended -NullRHI -nosplash -nop4 "-abslog=$root/work/public-vr-material.log"
if($LASTEXITCODE -ne 0){throw 'VR material preparation failed'}
if(!(Test-Path "$root/Content/Star/Materials/M_VRPanel.uasset")){throw 'VR material was not generated'}
