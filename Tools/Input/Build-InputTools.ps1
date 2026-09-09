[CmdletBinding()]
param([switch]$RunTests)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$sdl = Join-Path $root 'Plugins/StarFlightInput/ThirdParty/SDL3'
if (!(Test-Path (Join-Path $sdl 'lib/Win64/SDL3.lib'))) { & (Join-Path $PSScriptRoot 'Setup-SDL3.ps1') -ProjectRoot $root }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ x64 build tools are required' }
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$out = Join-Path $root 'work/input-build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$module = Join-Path $root 'Plugins/StarFlightInput/Source/StarFlightInput'
$common = @('/nologo','/std:c++17','/EHsc','/W4','/WX','/permissive-','/utf-8','/D_CRT_SECURE_NO_WARNINGS',"/I$(Join-Path $module 'Public')","/I$(Join-Path $sdl 'include')",(Join-Path $module 'Private/Core/StarInputCore.cpp'),(Join-Path $module 'Private/Core/StarSdlJoystick.cpp'))
Push-Location $out
try {
    & cl @common (Join-Path $root 'Tools/Input/JoystickProbe.cpp') "/Fe:$out/StarJoystickProbe.exe" /link (Join-Path $sdl 'lib/Win64/SDL3.lib')
    if ($LASTEXITCODE -ne 0) { throw 'Joystick probe compilation failed' }
    & cl @common (Join-Path $root 'Tests/Input/StarInputTests.cpp') "/Fe:$out/StarInputTests.exe" /link (Join-Path $sdl 'lib/Win64/SDL3.lib')
    if ($LASTEXITCODE -ne 0) { throw 'Input test compilation failed' }
    Copy-Item (Join-Path $sdl 'bin/Win64/SDL3.dll') (Join-Path $out 'SDL3.dll') -Force
    if ($RunTests) {
        & (Join-Path $out 'StarInputTests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Input regression tests failed' }
    }
} finally { Pop-Location }
Write-Output "Built input tools: $out"
