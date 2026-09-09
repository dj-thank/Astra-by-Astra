[CmdletBinding()]
param([string]$DataRoot,[string]$OutputDirectory,[string]$DependencyRoot,[ValidateSet(15,30,60)][int]$Fps=30)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$DependencyRoot) { $DependencyRoot=$projectRoot }
if (!$DataRoot) { $DataRoot=Join-Path $projectRoot 'Content/Star/Data' }
if (!$OutputDirectory) { $OutputDirectory=Join-Path $projectRoot 'work/guided-tour-native-tests' }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$runner=Get-Process -Id $PID
@{owner='guided-tour-native-verification';pid=$PID;creationTimeUtc=$runner.StartTime.ToUniversalTime().ToString('o');cwd=$projectRoot;outputDirectory=$OutputDirectory;deadlineUtc=[DateTime]::UtcNow.AddMinutes(10).ToString('o');stopMethod='foreground command completion; exact runner PID and creation time only'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'process-provenance.json') -Encoding utf8
$g=Get-Content (Join-Path $DataRoot 'moon_ldem_16.json') -Raw | ConvertFrom-Json
$r=Get-Content (Join-Path $DataRoot 'apollo17.json') -Raw | ConvertFrom-Json
$values=@($g.width,$g.height,$g.scaleMeters,$g.referenceRadiusMeters,$r.width,$r.height,$r.westLongitudeDegrees,$r.eastLongitudeDegrees,$r.northLatitudeDegrees,$r.southLatitudeDegrees)
$invariant=[Globalization.CultureInfo]::InvariantCulture
$meta=Join-Path $OutputDirectory 'dem.txt'
[IO.File]::WriteAllText($meta,(($values|ForEach-Object {([double]$_).ToString('R',$invariant)}) -join "`n"),[Text.UTF8Encoding]::new($false))
$catalog=Get-Content (Join-Path $DataRoot 'bodies.json') -Raw | ConvertFrom-Json
$hashes=@{}
foreach($name in @('bodies.json','moon_ldem_16.json','apollo17.json','moon_ldem_16_i16.bin','apollo17_height_f32.bin','apollo17_valid_u8.bin')) {
    $hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $DataRoot $name) -Algorithm SHA256).Hash
}
foreach($path in @('Source/Star/Simulation/FlightSimulation.cpp','Source/Star/Simulation/FlightSimulation.h','Tests/Validation/RealDem.h','Source/Star/Exploration/StarExplorationCore.h')) {
    $hashes[$path]=(Get-FileHash -LiteralPath (Join-Path $DependencyRoot $path) -Algorithm SHA256).Hash
}
foreach($path in @('Source/Star/GuidedTour/GuidedTour.cpp','Source/Star/GuidedTour/GuidedTour.h','Tests/GuidedTour/GuidedTourTests.cpp')) {
    $hashes[$path]=(Get-FileHash -LiteralPath (Join-Path $projectRoot $path) -Algorithm SHA256).Hash
}
@{epoch=$catalog.epoch;basis=$catalog.basis;sourceSha256=$hashes;validation='portable actual flight and DEM; not packaged game or physical input'} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'source-provenance.json') -Encoding utf8
$lines=@([string]$catalog.bodies.Count)
foreach($b in $catalog.bodies) {
    $numbers=@($b.radiusMeters)+@($b.positionMeters)
    foreach($row in $b.bodyFixedToEclipticJ2000) {$numbers+=@($row)}
    $lines+= $b.id+' '+(($numbers|ForEach-Object {([double]$_).ToString('R',$invariant)}) -join ' ')
}
$fixture=Join-Path $OutputDirectory 'bodies.txt'
[IO.File]::WriteAllLines($fixture,$lines,[Text.UTF8Encoding]::new($false))
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$source=Join-Path $projectRoot 'Source/Star'
$dependencySource=Join-Path $DependencyRoot 'Source/Star'
$dependencyTests=Join-Path $DependencyRoot 'Tests'
$binary=Join-Path $OutputDirectory 'GuidedTourTests.exe'
Push-Location $OutputDirectory
try {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$source" "/I$dependencySource" "/I$dependencyTests" (Join-Path $dependencySource 'Simulation/FlightSimulation.cpp') (Join-Path $source 'GuidedTour/GuidedTour.cpp') (Join-Path $projectRoot 'Tests/GuidedTour/GuidedTourTests.cpp') "/Fe:$binary" 2>&1 | Tee-Object (Join-Path $OutputDirectory 'build.log')
    if($LASTEXITCODE) {throw 'Route build failed'}
    & $binary $DataRoot $meta $fixture $Fps 2>&1 | Tee-Object (Join-Path $OutputDirectory 'tests.log')
    if($LASTEXITCODE) {throw 'Route failed'}
} finally {Pop-Location}
