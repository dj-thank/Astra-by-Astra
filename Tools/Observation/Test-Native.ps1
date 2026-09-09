[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'work\observation-native-tests' }
$testOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installation) { throw 'Visual Studio C++ Build Tools were not found.' }
Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$source = Join-Path $projectRoot 'Source\Star\Simulation'
$testSource = Join-Path $projectRoot 'Tests\Observation\ObservationGeometryTests.cpp'
$binary = Join-Path $testOutput 'StarObservationTests.exe'
$buildLog = Join-Path $testOutput 'build.log'
$testLog = Join-Path $testOutput 'tests.log'
Push-Location $testOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$source" (Join-Path $source 'FlightSimulation.cpp') (Join-Path $source 'ObservationGeometry.cpp') $testSource "/Fe:$binary" 2>&1 | Tee-Object -FilePath $buildLog
    if ($LASTEXITCODE -ne 0) { throw "Native observation build failed ($LASTEXITCODE). See $buildLog" }
    & $binary 2>&1 | Tee-Object -FilePath $testLog
    if ($LASTEXITCODE -ne 0) { throw "Native observation tests failed ($LASTEXITCODE). See $testLog" }
} finally { Pop-Location }
Write-Output "Verified native C++17 observation geometry: $testLog"
