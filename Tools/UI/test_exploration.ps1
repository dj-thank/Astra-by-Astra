param([string]$CompilerEnvironment = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $project 'work/ui-tests'
New-Item -ItemType Directory -Force $out | Out-Null
if (-not (Test-Path -LiteralPath $CompilerEnvironment)) { throw 'MSVC vcvars64.bat was not found.' }
# Compile within one cmd invocation to retain vcvars environment. No file deletion/move is performed.
$source = Join-Path $PSScriptRoot 'exploration_core_tests.cpp'
$include = Join-Path $project 'Source/Star'
$exe = Join-Path $out 'exploration_core_tests.exe'
$obj = Join-Path $out 'exploration_core_tests.obj'
$compileCommand = 'call "{0}" >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /I"{1}" "{2}" /Fo"{3}" /Fe"{4}"' -f $CompilerEnvironment,$include,$source,$obj,$exe
& $env:ComSpec /d /s /c $compileCommand
if ($LASTEXITCODE -ne 0) { throw "MSVC build failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Exploration checks failed: $LASTEXITCODE" }
