$ErrorActionPreference='Stop'
$taskDir=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$artDir=Join-Path $taskDir 'Art/Explorer/CockpitV3'
$p=Start-Process -FilePath 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' -ArgumentList @('--background','--factory-startup','--threads','4','--python',(Join-Path $PSScriptRoot 'build_side_details.py')) -WorkingDirectory $taskDir -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $artDir 'side-details.stdout.log') -RedirectStandardError (Join-Path $artDir 'side-details.stderr.log')
$p.Refresh()
$receipt=[ordered]@{owner='/root/astra_cockpit_v03';pid=$p.Id;created_utc=$p.StartTime.ToUniversalTime().ToString('o');cwd=$taskDir;purpose='Independent side details CPU Blender';stop_method='natural completion';expires_utc=[DateTime]::UtcNow.AddHours(1).ToString('o')}
$receipt | ConvertTo-Json | Set-Content (Join-Path $artDir 'side-details-process.json') -Encoding utf8
$p.WaitForExit();$receipt.exit_code=$p.ExitCode;$receipt.completed_utc=[DateTime]::UtcNow.ToString('o')
$receipt | ConvertTo-Json | Set-Content (Join-Path $artDir 'side-details-process.json') -Encoding utf8
