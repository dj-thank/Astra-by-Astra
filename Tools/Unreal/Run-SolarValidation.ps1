param([switch]$FinalArt,[switch]$Delivery)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$cases=@(
 @{Name='motion-current';Movie=$true;Seconds=64},
 @{Name='motion-legacy';Movie=$true;Legacy=$true;Seconds=64},
 @{Name='lifecycle';Lifecycle=$true;StartR=8;SpeedR=0;Seconds=16},
 @{Name='gpu-current';StartR=0;SpeedR=0;Seconds=30},
 @{Name='gpu-legacy';Legacy=$true;StartR=0;SpeedR=0;Seconds=30},
 @{Name='normal-earth';NormalStart=$true;Seconds=64}
)
$results=@()
if($FinalArt){$cases=@(
 @{Name='motion-final';Movie=$true;Seconds=64},
 @{Name='close-motion';Movie=$true;StartR=0;SpeedR=0;Seconds=40},
 @{Name='gpu-final';StartR=0;SpeedR=0;Seconds=30},
 @{Name='normal-earth-full';NormalStart=$true;Seconds=160}
)}
$resultName=if($FinalArt){'results-final.json'}else{'results.json'}
if($Delivery){
 $cases=@(@{Name='motion-delivery';Movie=$true;Seconds=64},@{Name='close-delivery';Movie=$true;StartR=0;SpeedR=0;Seconds=40},@{Name='gpu-delivery';StartR=0;SpeedR=0;Seconds=30})
 $resultName='results-delivery.json'
}
foreach($case in $cases){
    $output=Join-Path $root ('work/solar-qa/'+$case.Name)
    & (Join-Path $PSScriptRoot 'Launch-SolarQA.ps1') @case
    $receipt=Get-Content -LiteralPath (Join-Path $output 'process.json') -Raw | ConvertFrom-Json
    try {Wait-Process -Id $receipt.pid -Timeout 240 -ErrorAction Stop}
    catch {
        $process=Get-Process -Id $receipt.pid -ErrorAction SilentlyContinue
        if($process -and $process.StartTime.ToUniversalTime().ToString('o') -eq $receipt.createdAt){Stop-Process -Id $receipt.pid}
        throw "Runtime timeout in $($case.Name)"
    }
    $log=Get-Content -LiteralPath (Join-Path $output 'game.log') -Raw
    $pass=$log -match 'LogExit: Exiting' -and $log -notmatch 'Fatal error:|Failed to compile Material'
    if(!$case.NormalStart){
        $rows=Get-Content -LiteralPath (Join-Path $output 'solar-flight.jsonl') | ForEach-Object {$_|ConvertFrom-Json}
        $pass=$pass -and $rows.Count -gt 10 -and ($rows|Where-Object {$_.recoveries -ne 0 -or $_.shipHidden}).Count -eq 0
    }
    if($case.Lifecycle){
        $events=Get-Content -LiteralPath (Join-Path $output 'solar-lifecycle.jsonl') | ForEach-Object {$_|ConvertFrom-Json}
        $pass=$pass -and $events.Count -eq 5 -and ($events|Where-Object {!$_.pass}).Count -eq 0
    }
    $results+=@{name=$case.Name;passed=$pass;pid=$receipt.pid}
    $results|ConvertTo-Json|Set-Content -Encoding utf8 (Join-Path $root ('work/solar-qa/'+$resultName))
    if(!$pass){throw "Runtime validation failed: $($case.Name)"}
}
