param([string]$CompilerEnvironment = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDirectory = Join-Path $project 'work/ui-input-tests'
New-Item -ItemType Directory -Force $outputDirectory | Out-Null
if (-not (Test-Path -LiteralPath $CompilerEnvironment)) { throw 'MSVC vcvars64.bat was not found.' }
$testSource = Join-Path $PSScriptRoot 'input_calibration_tests.cpp'
$starInclude = Join-Path $project 'Source/Star'
$pluginInclude = Join-Path $project 'Plugins/StarFlightInput/Source/StarFlightInput/Public'
$pluginCore = Join-Path $project 'Plugins/StarFlightInput/Source/StarFlightInput/Private/Core/StarInputCore.cpp'
$executable = Join-Path $outputDirectory 'input_calibration_tests.exe'
$objects = $outputDirectory.Replace('\', '/') + '/'
$compileCommand = 'call "{0}" >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /I"{1}" /I"{2}" "{3}" "{4}" /Fo"{5}" /Fe"{6}"' -f $CompilerEnvironment,$starInclude,$pluginInclude,$testSource,$pluginCore,$objects,$executable
& $env:ComSpec /d /s /c $compileCommand
if ($LASTEXITCODE -ne 0) { throw "MSVC build failed: $LASTEXITCODE" }
& $executable
if ($LASTEXITCODE -ne 0) { throw "Input calibration checks failed: $LASTEXITCODE" }
