[CmdletBinding()]
param([Parameter(Mandatory)][string]$DependencyRoot,
      [string]$OutputDirectory='work/evidence/coverage', [string]$BaselineRevision='407653d')
$ErrorActionPreference='Stop'
$env:PYTHONUTF8='1'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$depRoot=[IO.Path]::GetFullPath($DependencyRoot)
$outRoot=[IO.Path]::GetFullPath((Join-Path $taskRoot $OutputDirectory))
if(-not $outRoot.StartsWith($taskRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must remain inside this worktree.'}
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
& (Join-Path $PSScriptRoot 'Test-Native.ps1') -DataDirectory (Join-Path $depRoot 'Content/Star/Data') -DependencySourceDirectory (Join-Path $depRoot 'Source/Star') -OutputDirectory (Join-Path $outRoot 'native')
$baseline=Join-Path $outRoot 'baseline-LunarTerrainGeometry.cpp'
Push-Location $taskRoot
try {
 $baselineCode=& git show "${BaselineRevision}:Source/Star/Terrain/LunarTerrainGeometry.cpp"
 if($LASTEXITCODE -ne 0){throw 'Baseline revision is unavailable.'}
 [IO.File]::WriteAllText($baseline,($baselineCode -join "`n"),[Text.UTF8Encoding]::new($false))
 $baselineHash=& git rev-parse $BaselineRevision
 $headHash=& git rev-parse HEAD
} finally {Pop-Location}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
Import-Module (Join-Path $vsRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Push-Location $outRoot
try {
 foreach($mode in @('baseline','candidate')) {
  $geometry=if($mode -eq 'baseline'){$baseline}else{Join-Path $taskRoot 'Source/Star/Terrain/LunarTerrainGeometry.cpp'}
  $binary=Join-Path $outRoot "export-$mode.exe"
  & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$taskRoot/Source/Star" "/I$depRoot/Source/Star" (Join-Path $depRoot 'Source/Star/Simulation/FlightSimulation.cpp') $geometry (Join-Path $PSScriptRoot 'Export-Coverage.cpp') "/Fe:$binary" 2>&1 | Tee-Object -FilePath (Join-Path $outRoot "$mode-build.log")
  if($LASTEXITCODE -ne 0){throw 'Coverage export build failed.'}
  & $binary (Join-Path $depRoot 'Content/Star/Data') (Join-Path $outRoot 'native/real-data-metadata.txt') (Join-Path $outRoot $mode) 2>&1 | Tee-Object -FilePath (Join-Path $outRoot "$mode-export.log")
  if($LASTEXITCODE -ne 0){throw 'Coverage export failed.'}
 }
} finally {Pop-Location}
$sourceFiles=@('moon_ldem_16_i16.bin','apollo17_height_f32.bin','apollo17_valid_u8.bin','apollo17_far_height_i16.bin','apollo17_far_confidence_u8.bin')
$sources=@($sourceFiles | ForEach-Object { $path=Join-Path $depRoot "Content/Star/Data/$_"; @{path=$_;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()} })
$receipt=@{status='CPU_GEOMETRY_NOT_GAME';baselineRevision=$baselineHash;candidateBaseRevision=$headHash;candidateSourceSha256=(Get-FileHash -LiteralPath (Join-Path $taskRoot 'Source/Star/Terrain/LunarTerrainGeometry.cpp') -Algorithm SHA256).Hash.ToLowerInvariant();sources=$sources}
[IO.File]::WriteAllText((Join-Path $outRoot 'source-receipt.json'),($receipt|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
& python (Join-Path $PSScriptRoot 'Report-Coverage.py') $outRoot
if($LASTEXITCODE -ne 0){throw 'Coverage figure generation failed.'}
