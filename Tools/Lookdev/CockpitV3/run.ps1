param([switch]$Validate,[switch]$ReadbackRender)
$ErrorActionPreference='Stop'
$taskDir=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$artDir=Join-Path $taskDir 'Art/Explorer/CockpitV3'
$kind=if($Validate){'validate'}elseif($ReadbackRender){'render_glb'}else{'build'}
$scriptPath=Join-Path $PSScriptRoot ($kind+'.py')
$p=Start-Process -FilePath 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' -ArgumentList @('--background','--factory-startup','--threads','4','--python',$scriptPath) -WorkingDirectory $taskDir -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $artDir ($kind+'.stdout.log')) -RedirectStandardError (Join-Path $artDir ($kind+'.stderr.log'))
$p.Refresh()
$receipt=[ordered]@{owner='/root/astra_cockpit_v03';pid=$p.Id;created_utc=$p.StartTime.ToUniversalTime().ToString('o');cwd=$taskDir;purpose=('CockpitV3 CPU separate headless Blender '+$kind);stop_method='natural completion';expires_utc=[DateTime]::UtcNow.AddHours(2).ToString('o')}
$receipt | ConvertTo-Json | Set-Content (Join-Path $artDir ($kind+'-process.json')) -Encoding utf8
$p.WaitForExit()
$receipt.exit_code=$p.ExitCode
$receipt.completed_utc=[DateTime]::UtcNow.ToString('o')
$receipt | ConvertTo-Json | Set-Content (Join-Path $artDir ($kind+'-process.json')) -Encoding utf8
