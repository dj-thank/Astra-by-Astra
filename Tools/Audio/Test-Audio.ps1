[CmdletBinding()]
param([string]$OutputDirectory, [string]$PreviewDirectory)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'work\audio-native-tests' }
if (-not $PreviewDirectory) { $PreviewDirectory = Join-Path $projectRoot 'outputs\audio' }
$testOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
$previewOutput = [System.IO.Path]::GetFullPath($PreviewDirectory)
New-Item -ItemType Directory -Force -Path $testOutput, $previewOutput | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installation) { throw 'Visual Studio C++ Build Tools were not found.' }
Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$source = Join-Path $projectRoot 'Source\Star\Presentation'
$dsp = Join-Path $source 'StarShipAudioDSP.cpp'
$testSource = Join-Path $projectRoot 'Tests\Audio\ShipAudioDSPTests.cpp'
$previewSource = Join-Path $PSScriptRoot 'RenderAudioPreview.cpp'
$binary = Join-Path $testOutput 'StarShipAudioTests.exe'
$previewBinary = Join-Path $testOutput 'RenderAudioPreview.exe'
Push-Location $testOutput
try {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$source" $dsp $testSource "/Fe:$binary" 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'build-tests.log')
    if ($LASTEXITCODE -ne 0) { throw "Native audio test build failed ($LASTEXITCODE)" }
    & $binary 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'tests.log')
    if ($LASTEXITCODE -ne 0) { throw "Native audio tests failed ($LASTEXITCODE)" }
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$source" $dsp $previewSource "/Fe:$previewBinary" 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'build-preview.log')
    if ($LASTEXITCODE -ne 0) { throw "Audio preview build failed ($LASTEXITCODE)" }
    & $previewBinary $previewOutput 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'preview.log')
    if ($LASTEXITCODE -ne 0) { throw "Audio preview render failed ($LASTEXITCODE)" }
} finally { Pop-Location }
Write-Output "Native DSP verified; preview generated without playback: $previewOutput"
