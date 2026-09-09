[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'work/unified-world/native'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Push-Location $out
try {
 & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$root/Source/Star/Simulation" "$root/Source/Star/Simulation/FlightSimulation.cpp" "$root/Source/Star/Simulation/ObservationGeometry.cpp" "$root/Tests/Simulation/WorldContinuityTests.cpp" /Fe:WorldContinuityTests.exe
 if($LASTEXITCODE -ne 0){throw 'World continuity test build failed'}
 $ephemeris="$root/Content/Star/Data/ephemeris-2026.bin"
 if(!(Test-Path -LiteralPath $ephemeris)){$ephemeris="$root/Tests/Fixtures/ephemeris-world-test.bin"}
 & ./WorldContinuityTests.exe $ephemeris ([DateTimeOffset]::Parse('2026-09-19T00:00:00Z').ToUnixTimeSeconds()) | Tee-Object tests.log
 if($LASTEXITCODE -ne 0){throw 'World continuity test failed'}
}finally{Pop-Location}
