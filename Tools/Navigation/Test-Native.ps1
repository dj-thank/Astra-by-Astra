[CmdletBinding()]
param([string]$DependencyRoot, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $DependencyRoot) { $DependencyRoot = $projectRoot }
$DependencyRoot = [System.IO.Path]::GetFullPath($DependencyRoot)
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'work/navigation-native-tests' }
$testOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installation) { throw 'Visual Studio C++ Build Tools were not found.' }
Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$dataPath = Join-Path $DependencyRoot 'Content/Star/Data/bodies.json'
if(!(Test-Path -LiteralPath $dataPath)){$dataPath=Join-Path $projectRoot 'Tests/Fixtures/navigation-bodies.json'}
$data = Get-Content -LiteralPath $dataPath -Raw -Encoding utf8 | ConvertFrom-Json
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$fixtureLines = foreach ($body in $data.bodies) {
    if ($body.id -notin @('earth','moon','saturn','sun')) { continue }
    $atmosphere = if ($body.id -eq 'earth') { 100000 } elseif ($body.id -eq 'saturn') { 150000 } else { 0 }
    $landable = if ($body.id -eq 'moon') { 1 } else { 0 }
    # Conservative fixture ceiling, not a claim of measured terrain contact.
    $ceiling = if ($landable) { 12000 } else { 0 }
    $values = @($body.positionMeters[0],$body.positionMeters[1],$body.positionMeters[2],$body.radiusMeters)
    $formatted = ($values | ForEach-Object { ([double]$_).ToString('R',$culture) }) -join ' '
    "$($body.id) $formatted $atmosphere $landable $ceiling"
}
$fixture = Join-Path $testOutput 'dated-bodies.txt'
[System.IO.File]::WriteAllLines($fixture,[string[]]$fixtureLines,[System.Text.UTF8Encoding]::new($false))
$receipt = [ordered]@{
    source = $dataPath
    sourceSha256 = (Get-FileHash -LiteralPath $dataPath -Algorithm SHA256).Hash
    epoch = $data.epoch
    fixtureTerrainCeilingMeters = 12000
    fixtureTerrainIsMeasuredContactEvidence = $false
    simulationSourceSha256 = (Get-FileHash -LiteralPath (Join-Path $DependencyRoot 'Source/Star/Simulation/FlightSimulation.cpp') -Algorithm SHA256).Hash
}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testOutput 'inputs.json') -Encoding utf8
$binary = Join-Path $testOutput 'StarNavigationTests.exe'
Push-Location $testOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$(Join-Path $DependencyRoot 'Source/Star')" "/I$(Join-Path $projectRoot 'Source/Star')" (Join-Path $DependencyRoot 'Source/Star/Simulation/FlightSimulation.cpp') (Join-Path $projectRoot 'Source/Star/Navigation/NavigationPilot.cpp') (Join-Path $projectRoot 'Tests/Navigation/NavigationPilotTests.cpp') "/Fe:$binary" 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'build.log')
    if ($LASTEXITCODE -ne 0) { throw "Navigation native build failed ($LASTEXITCODE)." }
    & $binary $fixture 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'tests.log')
    if ($LASTEXITCODE -ne 0) { throw "Navigation native tests failed ($LASTEXITCODE)." }
} finally { Pop-Location }
Write-Output "Verified native navigation core only: $testOutput"
