$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
& (Join-Path $root 'Tools/Simulation/Test-Native.ps1')
if($LASTEXITCODE -ne 0){throw 'Simulation tests failed'}
$out=Join-Path $root 'work/solar-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $out
try {
 & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$root/Source/Star/Simulation" "$root/Tests/Simulation/SolarVisualTests.cpp" "$root/work/simulation-native-tests/FlightSimulation.obj" /Fe:SolarVisualTests.exe
 if($LASTEXITCODE -ne 0){throw 'Solar tests compilation failed'}
 & ./SolarVisualTests.exe
 if($LASTEXITCODE -ne 0){throw 'Solar tests failed'}
 & python "$root/Tools/Unreal/Materials/verify_sun.py"
 if($LASTEXITCODE -ne 0){throw 'Atmosphere regression failed'}
} finally {Pop-Location}
