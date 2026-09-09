[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$env:PYTHONUTF8='1'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'work/audio-export'
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(!$vs){throw 'Visual Studio C++ Build Tools are required.'}
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Push-Location $out
try{
 & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$root/Source/Star/Presentation" "$root/Source/Star/Presentation/StarAudioRouting.cpp" "$PSScriptRoot/Export-Sounds.cpp" /Fe:ExportSounds.exe
 if($LASTEXITCODE -ne 0){throw 'Sound exporter build failed'}
 & ./ExportSounds.exe "$root/Content/Star/Audio/Source"
 if($LASTEXITCODE -ne 0){throw 'Sound export failed'}
 & python "$PSScriptRoot/Generate-Music.py"
 if($LASTEXITCODE -ne 0){throw 'Music generation failed'}
}finally{Pop-Location}
