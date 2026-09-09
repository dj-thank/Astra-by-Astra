[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'work/earth-time/native-tests'
New-Item -ItemType Directory -Force $out|Out-Null
$checks=Get-Content (Join-Path $root 'Data/ephemeris/ephemeris-checks.json') -Raw -Encoding utf8|ConvertFrom-Json
$culture=[Globalization.CultureInfo]::InvariantCulture
$lines=for($i=0;$i -lt $checks.Count;$i++){foreach($s in $checks[$i].samples){
    $values=@($s.unix,$i)+@($s.position)+@($s.orientation)
    ($values|ForEach-Object{([double]$_).ToString('R',$culture)}) -join ' '
}}
$fixture=Join-Path $out 'independent-checks.txt';[IO.File]::WriteAllLines($fixture,$lines,[Text.UTF8Encoding]::new($false))
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath|Select-Object -First 1)
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'|Out-Null
Push-Location $out
try {
 & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 "/I$root/Source/Star/Simulation" "$root/Source/Star/Simulation/FlightSimulation.cpp" "$root/Tests/Simulation/AstronomyTests.cpp" /Fe:StarAstronomyTests.exe
 if($LASTEXITCODE -ne 0){throw 'Astronomy test build failed'}
 & ./StarAstronomyTests.exe "$root/Content/Star/Data/ephemeris-2026.bin" $fixture | Tee-Object tests.log
 if($LASTEXITCODE -ne 0){throw 'Astronomy verification failed'}
}finally{Pop-Location}
