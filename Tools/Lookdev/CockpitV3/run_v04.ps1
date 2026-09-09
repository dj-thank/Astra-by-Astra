param(
 [ValidateSet('build','validate','preview')][string]$Stage='build',
 [string]$ContextRoot='',
 [ValidateNotNullOrEmpty()][string]$Owner='/root',
 [ValidateSet('all','baseline','candidate')][string]$PreviewState='all',
 [string]$Blender='C:/Program Files/Blender Foundation/Blender 5.1/blender.exe'
)
$ErrorActionPreference='Stop'
$taskDir=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$artDir=Join-Path $taskDir 'work/cockpit-v04'
New-Item -ItemType Directory -Force -Path $artDir | Out-Null
if($ContextRoot){$env:STAR_CONTEXT_ROOT=(Resolve-Path -LiteralPath $ContextRoot).Path}
$scriptPath=Join-Path $PSScriptRoot ($Stage+'_v04.py')
$argsList=@('--background','--factory-startup','--python-exit-code','1','--threads','4','--python',$scriptPath)
if($Stage -eq 'build'){$argsList+=@('--','--no-render')}
if($Stage -eq 'preview'){$argsList+=@('--','--state',$PreviewState)}
$p=Start-Process -FilePath $Blender -ArgumentList $argsList -WorkingDirectory $taskDir -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $artDir ($Stage+'.stdout.log')) -RedirectStandardError (Join-Path $artDir ($Stage+'.stderr.log'))
$receipt=[ordered]@{owner=$Owner;pid=$p.Id;created_utc=$p.StartTime.ToUniversalTime().ToString('o');cwd=$taskDir;purpose=('CockpitV04 separate CPU4 Blender '+$Stage);stop_method='natural completion';expires_utc=[DateTime]::UtcNow.AddHours(1).ToString('o')}
$receipt | ConvertTo-Json | Set-Content (Join-Path $artDir ($Stage+'-process.json')) -Encoding utf8
$p.WaitForExit()
$receipt.exit_code=$p.ExitCode;$receipt.completed_utc=[DateTime]::UtcNow.ToString('o')
$receipt | ConvertTo-Json | Set-Content (Join-Path $artDir ($Stage+'-process.json')) -Encoding utf8
if($p.ExitCode -ne 0){throw "Blender $Stage failed with exit $($p.ExitCode). Read scoped logs in $artDir"}
