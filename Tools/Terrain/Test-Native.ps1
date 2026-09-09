[CmdletBinding()]
param([string]$OutputDirectory,[string]$DataDirectory,[string]$DependencySourceDirectory)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'work\terrain-native-tests' }
$testOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$dataRoot = if ($DataDirectory) { [System.IO.Path]::GetFullPath($DataDirectory) } else { Join-Path $projectRoot 'Content\Star\Data' }
$globalMeta = Get-Content -LiteralPath (Join-Path $dataRoot 'moon_ldem_16.json') -Raw | ConvertFrom-Json
$regionalMeta = Get-Content -LiteralPath (Join-Path $dataRoot 'apollo17.json') -Raw | ConvertFrom-Json
$numbers = @($globalMeta.width,$globalMeta.height,$globalMeta.scaleMeters,$globalMeta.referenceRadiusMeters,
    $regionalMeta.width,$regionalMeta.height,$regionalMeta.westLongitudeDegrees,$regionalMeta.eastLongitudeDegrees,
    $regionalMeta.northLatitudeDegrees,$regionalMeta.southLatitudeDegrees)
$farPath = Join-Path $dataRoot 'apollo17_far.json'
if (Test-Path -LiteralPath $farPath) {
    $farMeta = Get-Content -LiteralPath $farPath -Raw | ConvertFrom-Json
    $numbers += @($farMeta.width,$farMeta.height,$farMeta.westLongitudeDegrees,$farMeta.eastLongitudeDegrees,
        $farMeta.northLatitudeDegrees,$farMeta.southLatitudeDegrees,$farMeta.heightScaleMeters,
        $farMeta.blendWidthMeters,$farMeta.blendInsetMeters)
}
$metadata = ($numbers | ForEach-Object { ([double]$_).ToString('R',[System.Globalization.CultureInfo]::InvariantCulture) }) -join "`n"
$metadataPath = Join-Path $testOutput 'real-data-metadata.txt'
[System.IO.File]::WriteAllText($metadataPath,$metadata,[System.Text.UTF8Encoding]::new($false))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installation) { throw 'Visual Studio C++ Build Tools were not found.' }
Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$sourceRoot = Join-Path $projectRoot 'Source\Star'
$dependencyRoot = if ($DependencySourceDirectory) { [System.IO.Path]::GetFullPath($DependencySourceDirectory) } else { $sourceRoot }
$binary = Join-Path $testOutput 'StarTerrainTests.exe'
Push-Location $testOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$sourceRoot" "/I$dependencyRoot" (Join-Path $dependencyRoot 'Simulation\FlightSimulation.cpp') (Join-Path $sourceRoot 'Terrain\LunarTerrainGeometry.cpp') (Join-Path $projectRoot 'Tests\Terrain\LunarTerrainTests.cpp') "/Fe:$binary" 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'build.log')
    if ($LASTEXITCODE -ne 0) { throw "Native terrain build failed ($LASTEXITCODE)." }
    & $binary $dataRoot $metadataPath 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'tests.log')
    if ($LASTEXITCODE -ne 0) { throw "Native terrain tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Output "Verified portable terrain geometry against real DEM bytes: $testOutput"
