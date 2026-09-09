param([switch]$Render, [switch]$Validate, [switch]$Draft, [int]$Samples = 32)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$logRoot = Join-Path $taskRoot 'work/art'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$env:PYTHONUTF8 = '1'
$scriptName = if ($Validate) { 'validate_explorer.py' } else { 'build_explorer.py' }
$logName = if ($Validate) { 'blender-validation' } else { 'blender-build' }
$scriptPath = Join-Path $PSScriptRoot $scriptName
$blenderArgs = @('--background','--factory-startup','--threads','6','--python',('"' + $scriptPath + '"'))
if (-not $Validate) {
    $blenderArgs += @('--','--samples',$Samples)
    if ($Render) { $blenderArgs += '--render' }
    if ($Draft) { $blenderArgs += '--draft' }
}
$p = Start-Process -FilePath 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' -ArgumentList $blenderArgs -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logRoot ($logName+'.stdout.log')) -RedirectStandardError (Join-Path $logRoot ($logName+'.stderr.log'))
$p.Refresh()
$receipt = [ordered]@{owner='/root/build_explorer_art'; pid=$p.Id; created_utc=$p.StartTime.ToUniversalTime().ToString('o'); cwd=$taskRoot; purpose='STAR separate headless Blender authoring/export/render'; stop_method='natural completion'; expires_utc=[DateTime]::UtcNow.AddHours(2).ToString('o')}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logRoot ($logName+'-process.json')) -Encoding utf8
$p.WaitForExit()
$receipt.exit_code=$p.ExitCode
$receipt.completed_utc=[DateTime]::UtcNow.ToString('o')
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logRoot ($logName+'-process.json')) -Encoding utf8
exit $p.ExitCode
