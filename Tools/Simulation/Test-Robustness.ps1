[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=Join-Path $projectRoot 'work/robustness-tests'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(!$installation){throw 'Visual Studio C++ Build Tools were not found.'}
Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
& python (Join-Path $projectRoot 'Tools/Testing/extract_method.py') --output $output
if($LASTEXITCODE -ne 0){throw 'Runtime extraction/integration guard failed.'}
$targets=@(
    @{name='input-robustness';sources=@('Plugins/StarFlightInput/Source/StarFlightInput/Private/Core/StarInputCore.cpp','Tests/Input/InputRobustnessTests.cpp')},
    @{name='eva-robustness';sources=@('Source/Star/Simulation/FlightSimulation.cpp','Source/Star/EVA/LunarWalkModel.cpp','Tests/EVA/LunarWalkRobustnessTests.cpp')},
    @{name='terrain-robustness';sources=@('Source/Star/Simulation/FlightSimulation.cpp','Source/Star/Terrain/LunarTerrainGeometry.cpp','Tests/Terrain/TerrainRobustnessTests.cpp')},
    @{name='eva-lifecycle';sources=@('Tests/Runtime/EVALoadLifecycleTests.cpp')},
    @{name='robustness';sources=@('Source/Star/Simulation/FlightSimulation.cpp','Tests/Simulation/RobustnessTests.cpp')},
    @{name='raster-policy';sources=@('Tests/Terrain/RasterReadPolicyTests.cpp')}
)
Push-Location $output
try {
    foreach($target in $targets) {
        $sources=@($target.sources | ForEach-Object { Join-Path $projectRoot $_ })
        $binary=Join-Path $output ($target.name+'.exe')
        & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$(Join-Path $projectRoot 'Source/Star')" "/I$(Join-Path $projectRoot 'Plugins/StarFlightInput/Source/StarFlightInput/Public')" "/I$output" @sources "/Fe:$binary" 2>&1 | Tee-Object -FilePath ($target.name+'-build.log')
        if($LASTEXITCODE -ne 0){throw "Regression build failed: $($target.name)"}
        & $binary 2>&1 | Tee-Object -FilePath ($target.name+'-tests.log')
        if($LASTEXITCODE -ne 0){throw "Regression tests failed: $($target.name)"}
    }
} finally { Pop-Location }
