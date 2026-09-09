[CmdletBinding()]
param([string]$Blender='C:/Program Files/Blender Foundation/Blender 5.1/blender.exe',[string]$OutputDirectory='work/v04-clasts')
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$outRoot=[IO.Path]::GetFullPath((Join-Path $taskRoot $OutputDirectory))
$script=Join-Path $PSScriptRoot 'preview_clasts_v4.py'
# Exact dedicated process only. Keep logs and creation identity in this worktree.
$argsText=@('--background','--threads','4','--python-exit-code','1','--python',('"'+$script+'"'),'--',('"'+$outRoot+'"'))
$process=Start-Process -FilePath $Blender -ArgumentList $argsText -WorkingDirectory $taskRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $outRoot 'blender.stdout.log') -RedirectStandardError (Join-Path $outRoot 'blender.stderr.log') -PassThru
$identity=Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
@{taskId='STAR-v04-lunar';owner='/root/astra_lunar_v04';cwd=$taskRoot;pid=$process.Id;creationUtc=$identity.CreationDate.ToUniversalTime().ToString('o');threads=4;stopMethod='exact PID plus creation identity only';ttlMinutes=15}|ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $outRoot 'blender-process.json')
$process.WaitForExit()
Write-Output "Blender PID $($process.Id) exit $($process.ExitCode)"
if($process.ExitCode -ne 0){throw 'CPU Blender preview failed; inspect worktree logs'}
if(-not (Test-Path -LiteralPath (Join-Path $outRoot 'preview-receipt.json'))){throw 'Blender produced no completed preview receipt'}
