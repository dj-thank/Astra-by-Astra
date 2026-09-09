[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'work\audio-routing-tests' }
$testOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installation) { throw 'Visual Studio C++ Build Tools were not found.' }
Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$source = Join-Path $projectRoot 'Source\Star\Presentation'
$binary = Join-Path $testOutput 'StarAudioRoutingTests.exe'
Push-Location $testOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$source" (Join-Path $source 'StarAudioRouting.cpp') (Join-Path $PSScriptRoot 'AudioRoutingTests.cpp') "/Fe:$binary" 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'build.log')
    if ($LASTEXITCODE -ne 0) { throw "Native routing build failed ($LASTEXITCODE)" }
    & $binary (Join-Path $testOutput 'compiled-routing-contract.json') 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'tests.log')
    if ($LASTEXITCODE -ne 0) { throw "Native routing tests failed ($LASTEXITCODE)" }
} finally { Pop-Location }
Write-Output "Native routing/fallback verified: $testOutput"
