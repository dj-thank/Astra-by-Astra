[CmdletBinding()]
param([string]$EngineRoot='')
$ErrorActionPreference='Stop'
$candidates=@()
if($EngineRoot){$candidates+=$EngineRoot}
if($env:STAR_UE_ROOT){$candidates+=$env:STAR_UE_ROOT}
$candidates+=@('C:\Program Files\Epic Games\UE_5.8')
foreach($candidate in ($candidates | Select-Object -Unique)){
    $versionPath=Join-Path $candidate 'Engine\Build\Build.version'
    $editorPath=Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor.exe'
    if((Test-Path -LiteralPath $versionPath) -and (Test-Path -LiteralPath $editorPath)){
        $version=Get-Content -LiteralPath $versionPath -Raw -Encoding utf8 | ConvertFrom-Json
        if($version.MajorVersion -eq 5 -and $version.MinorVersion -eq 8 -and $version.PatchVersion -eq 2){
            return [IO.Path]::GetFullPath($candidate)
        }
    }
}
throw 'Unreal Engine 5.8.2 is required. Specify -EngineRoot or STAR_UE_ROOT.'
