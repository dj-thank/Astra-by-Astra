[CmdletBinding()]
param([Parameter(Mandatory)][string]$DependencyRoot,[string]$OutputDirectory='work/v04-clasts', [string]$BaselineDirectory)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$depRoot=[IO.Path]::GetFullPath($DependencyRoot)
$outRoot=[IO.Path]::GetFullPath((Join-Path $taskRoot $OutputDirectory))
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
Import-Module (Join-Path $vsRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$modes=@('v04'); if($BaselineDirectory){$modes+= 'v03'}
Push-Location $outRoot
try {
 foreach($mode in $modes){
  $terrainRoot=if($mode -eq 'v03'){[IO.Path]::GetFullPath((Join-Path $taskRoot $BaselineDirectory))}else{Join-Path $taskRoot 'Source/Star'}
  $definitions=@(if($mode -eq 'v04'){'/DSTAR_CLAST_V4'})
  $binary=Join-Path $outRoot "export-$mode.exe"
  & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 @definitions "/I$terrainRoot" "/I$depRoot/Source/Star" "/I$depRoot" (Join-Path $depRoot 'Source/Star/Simulation/FlightSimulation.cpp') (Join-Path $terrainRoot 'Terrain/LunarTerrainGeometry.cpp') (Join-Path $PSScriptRoot 'Export-ClastPreview.cpp') "/Fe:$binary"
  if($LASTEXITCODE -ne 0){throw 'Export build failed'}
  & $binary (Join-Path $depRoot 'Content/Star/Data') (Join-Path $outRoot 'tests/real-data-metadata.txt') (Join-Path $outRoot "$mode.json")
  if($LASTEXITCODE -ne 0){throw 'Export failed'}
 }
}finally{Pop-Location}
