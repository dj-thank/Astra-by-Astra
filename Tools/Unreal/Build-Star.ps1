[CmdletBinding()]
param([ValidateSet('Editor','Game','Package','Stage')][string]$Target='Editor',[string]$EngineRoot='', [int]$ParallelActions=2,[switch]$Incremental,[string]$ArchiveDirectory='',[switch]$NoUba)
$ErrorActionPreference='Stop'
$env:VSLANG='1033'
$env:__COMPAT_LAYER='HIGHDPIAWARE'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$engine=& (Join-Path $PSScriptRoot 'Find-Engine.ps1') -EngineRoot $EngineRoot
$project=Join-Path $projectRoot 'Star.uproject'
$output=if($ArchiveDirectory){[IO.Path]::GetFullPath($ArchiveDirectory)}else{Join-Path $projectRoot 'outputs\Star-Win64'}
$outputsBoundary=[IO.Path]::GetFullPath((Join-Path $projectRoot 'outputs'))+[IO.Path]::DirectorySeparatorChar
if(!$output.StartsWith($outputsBoundary,[StringComparison]::OrdinalIgnoreCase)){throw 'ArchiveDirectory must remain inside this project outputs directory.'}
$logs=Join-Path $projectRoot 'work\logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$log=Join-Path $logs "build-$($Target.ToLowerInvariant())-$stamp.log"
$result=Join-Path $logs "build-$($Target.ToLowerInvariant())-latest.json"
[ordered]@{state='running';target=$Target;engine=$engine;version='5.8.2';log=$log;startedAt=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $result -Encoding utf8
Push-Location $projectRoot
try{
    if($Target -eq 'Package' -and $NoUba){
        # Pass the executor switch at UBT's top level for both targets. UAT's
        # per-target UbtArgs is not a reliable place for an executor selection.
        foreach($name in @('StarEditor','Star')){
            & (Join-Path $engine 'Engine\Build\BatchFiles\Build.bat') $name Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE -NoUBA "-MaxParallelActions=$ParallelActions" 2>&1 | Tee-Object -FilePath $log -Append
            if($LASTEXITCODE -ne 0){throw "Direct $name build failed ($LASTEXITCODE). See $log"}
        }
    }
    if($Target -in @('Package','Stage')){
        if($Target -eq 'Stage'){
            if(!(Test-Path -LiteralPath (Join-Path $projectRoot 'Saved/Cooked/Windows/ue.projectstore'))){ throw 'A successful existing cook is required for Stage.' }
            & (Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$project" -noP4 -unattended -platform=Win64 -clientconfig=Development -skipbuild -skipcook -stage -pak -iostore -archive "-archivedirectory=$output" 2>&1 | Tee-Object -FilePath $log
        } else {
            [string[]]$cookOptions=@()
            if($Incremental){$cookOptions=@('-iterate')}
            [string[]]$buildOptions=if($NoUba){@('-skipbuild')}else{@('-build')}
            & (Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$project" -noP4 -unattended -platform=Win64 -clientconfig=Development @buildOptions -cook @cookOptions -stage -pak -iostore -archive "-archivedirectory=$output" "-UbtArgs=-MaxParallelActions=$ParallelActions" 2>&1 | Tee-Object -FilePath $log -Append
        }
    } else {
        $name=if($Target -eq 'Editor'){'StarEditor'}else{'Star'}
        [string[]]$executorOptions=if($NoUba){@('-NoUBA')}else{@()}
        & (Join-Path $engine 'Engine\Build\BatchFiles\Build.bat') $name Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE @executorOptions "-MaxParallelActions=$ParallelActions" 2>&1 | Tee-Object -FilePath $log
    }
    $exitCode=$LASTEXITCODE
    $artifact=if($Target -eq 'Editor'){Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Star.dll'}elseif($Target -eq 'Game'){Join-Path $projectRoot 'Binaries\Win64\Star.exe'}else{Join-Path $output 'Windows\Star.exe'}
    $exists=Test-Path -LiteralPath $artifact
    $success=$exitCode -eq 0 -and $exists
    [ordered]@{state=$(if($success){'built'}else{'failed'});target=$Target;engine=$engine;version='5.8.2';log=$log;exitCode=$exitCode;artifact=$artifact;artifactExists=$exists;finishedAt=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $result -Encoding utf8
    if(!$success){throw "STAR $Target build failed (exit $exitCode). See $log"}
} catch {
    $current=Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
    if($current.state -eq 'running'){
        [ordered]@{state='failed';target=$Target;engine=$engine;version='5.8.2';log=$log;error=$_.Exception.Message;finishedAt=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $result -Encoding utf8
    }
    throw
} finally {Pop-Location}
