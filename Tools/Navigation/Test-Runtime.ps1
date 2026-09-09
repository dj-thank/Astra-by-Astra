[CmdletBinding()]
param([string]$DependencyRoot)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(-not $DependencyRoot) { $DependencyRoot=$projectRoot }
$testOutput=Join-Path $projectRoot 'work/navigation-runtime-tests'
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$sourcePath=Join-Path $projectRoot 'Source/Star/Runtime/StarPlayerController.cpp'
$source=Get-Content -LiteralPath $sourcePath -Raw -Encoding utf8
$names=@('NavigationBypassed','ResetNavigation','SetFlightPaused','ToggleNavigationSafeBrake','ApplyNavigationControls')
$methods=foreach($name in $names) {
    $match=[regex]::Match($source,"(?:bool|void) AStarPlayerController::${name}\([^\r\n]*\)(?: const)?\s*\{")
    if(-not $match.Success) { throw "Missing actual controller method: $name" }
    $depth=1
    $end=$match.Index+$match.Length
    while($end -lt $source.Length -and $depth -gt 0) {
        if($source[$end] -eq '{') { $depth++ }
        if($source[$end] -eq '}') { $depth-- }
        $end++
    }
    if($depth -ne 0) { throw "Unbalanced controller method: $name" }
    $source.Substring($match.Index,$end-$match.Index)
}
[IO.File]::WriteAllText((Join-Path $testOutput 'runtime-methods.inc'),($methods -join "`n"),[Text.UTF8Encoding]::new($false))
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(-not $installation) { throw 'Visual Studio C++ Build Tools were not found.' }
Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$binary=Join-Path $testOutput 'StarNavigationRuntimeTests.exe'
Push-Location $testOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$testOutput" "/I$(Join-Path $DependencyRoot 'Source/Star')" "/I$(Join-Path $projectRoot 'Source/Star')" (Join-Path $DependencyRoot 'Source/Star/Simulation/FlightSimulation.cpp') (Join-Path $projectRoot 'Source/Star/Navigation/NavigationPilot.cpp') (Join-Path $PSScriptRoot 'RuntimePolicyTests.cpp') "/Fe:$binary" 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'build.log')
    if($LASTEXITCODE -ne 0) { throw "Runtime policy build failed ($LASTEXITCODE)." }
    & $binary 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'tests.log')
    if($LASTEXITCODE -ne 0) { throw "Runtime policy tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
[ordered]@{
    scope='Actual extracted controller policy + native simulation; UI/device calls are inert host adapters. Not UE, packaged game, or physical input acceptance.'
    controllerSha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    extractedMethods=$names
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testOutput 'receipt.json') -Encoding utf8
