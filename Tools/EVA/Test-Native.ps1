[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'work\eva-native-tests' }
$testOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$dataRoot = Join-Path $projectRoot 'Content\Star\Data'
$globalMeta = Get-Content -LiteralPath (Join-Path $dataRoot 'moon_ldem_16.json') -Raw | ConvertFrom-Json
$regionalMeta = Get-Content -LiteralPath (Join-Path $dataRoot 'apollo17.json') -Raw | ConvertFrom-Json
$numbers = @($globalMeta.width,$globalMeta.height,$globalMeta.scaleMeters,$globalMeta.referenceRadiusMeters,
    $regionalMeta.width,$regionalMeta.height,$regionalMeta.westLongitudeDegrees,$regionalMeta.eastLongitudeDegrees,
    $regionalMeta.northLatitudeDegrees,$regionalMeta.southLatitudeDegrees)
$metadata = ($numbers | ForEach-Object { ([double]$_).ToString('R',[System.Globalization.CultureInfo]::InvariantCulture) }) -join "`n"
$metadataPath = Join-Path $testOutput 'real-data-metadata.txt'
[System.IO.File]::WriteAllText($metadataPath,$metadata,[System.Text.UTF8Encoding]::new($false))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installation) { throw 'Visual Studio C++ Build Tools were not found.' }
Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$sourceRoot = Join-Path $projectRoot 'Source\Star'
$binary = Join-Path $testOutput 'StarEVATests.exe'
Push-Location $testOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$sourceRoot" (Join-Path $sourceRoot 'Simulation\FlightSimulation.cpp') (Join-Path $sourceRoot 'EVA\LunarWalkModel.cpp') (Join-Path $projectRoot 'Tests\EVA\LunarWalkTests.cpp') "/Fe:$binary" 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'build.log')
    if ($LASTEXITCODE -ne 0) { throw "Native EVA build failed ($LASTEXITCODE)." }
    & $binary $dataRoot $metadataPath 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'tests.log')
    if ($LASTEXITCODE -ne 0) { throw "Native EVA tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Output "Verified portable lunar walking against real DEM bytes: $testOutput"
