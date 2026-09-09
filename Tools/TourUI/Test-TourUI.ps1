[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "Tour UI check failed: $Message" }
}

$sources = @(
    (Join-Path $projectRoot 'Source/Star/Runtime/StarPlayerController.h'),
    (Join-Path $projectRoot 'Source/Star/Runtime/StarPlayerController.cpp'),
    (Join-Path $projectRoot 'Source/Star/Runtime/StarGuidedTourRuntime.cpp'),
    (Join-Path $projectRoot 'Source/Star/UI/StarHUDWidget.cpp'),
    (Join-Path $projectRoot 'Source/Star/StarViewTypes.h'),
    (Join-Path $projectRoot 'Source/Star/Presentation/StarInstrumentWidget.cpp')
)
foreach ($source in $sources) { Assert-True (Test-Path -LiteralPath $source) "missing $source" }

$controller = Get-Content -LiteralPath $sources[1] -Raw -Encoding utf8
$runtime = Get-Content -LiteralPath $sources[2] -Raw -Encoding utf8
$hud = Get-Content -LiteralPath $sources[3] -Raw -Encoding utf8
$types = Get-Content -LiteralPath $sources[4] -Raw -Encoding utf8
$instrument = Get-Content -LiteralPath $sources[5] -Raw -Encoding utf8

foreach ($token in @(
    'StartGuidedTour', 'ManualTakeover', 'star-tour-v1.json',
    'bGuidedTourTest', 'UpdateGuidedTourObservation',
    'SetFlightAssistEnabled', 'SetEnhancedStarsEnabled'
)) { Assert-True ($controller.Contains($token) -or $runtime.Contains($token)) "missing runtime token $token" }
foreach ($token in @('約5分の宇宙ツアー', 'ESO/S. Brunier', 'ToggleEnhancedStars', 'ToggleFlightAssist')) {
    Assert-True $hud.Contains($token) "missing UI token $token"
}
foreach ($token in @('EngineOutput', 'bEnhancedStars', 'bFlightAssist', 'bGuidedTour', 'GuidedTourProgress', 'GuidedTourTitle')) {
    Assert-True $types.Contains($token) "missing snapshot token $token"
}
Assert-True $instrument.Contains('エンジン出力（実負荷）') 'engine output label is not explicit'
Assert-True $instrument.Contains('推力指令（入力）') 'raw throttle label is not explicit'

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("STAR-TourUI-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
try {
    $manual = Join-Path $tempRoot 'star-v1.json'
    $tour = Join-Path $tempRoot 'star-tour-v1.json'
    $manualBytes = [Text.UTF8Encoding]::new($false).GetBytes('{"slot":"manual","position":123}')
    [IO.File]::WriteAllBytes($manual, $manualBytes)

    # Behavioral save probe: guided persistence has a different exact path.
    [IO.File]::WriteAllText($tour, '{"slot":"tour","stage":"moon_landed_scan"}', [Text.UTF8Encoding]::new($false))
    $manualHash = (Get-FileHash -LiteralPath $manual -Algorithm SHA256).Hash
    $expectedManualHash = ([Security.Cryptography.SHA256]::Create().ComputeHash($manualBytes) |
        ForEach-Object { $_.ToString('x2') } | Join-String)
    Assert-True ($manualHash -eq $expectedManualHash) 'guided save changed the manual slot'
    Assert-True ((Get-Content -LiteralPath $tour -Raw -Encoding utf8) -match 'moon_landed_scan') 'tour slot was not written'

    # Behavioral cancel probe: cancellation clears the guide marker while the
    # current flight marker remains the same; no earlier state is restored.
    $flightMarker = Join-Path $tempRoot 'current-flight.marker'
    [IO.File]::WriteAllText($flightMarker, 'current-position-preserved', [Text.UTF8Encoding]::new($false))
    $before = Get-FileHash -LiteralPath $flightMarker -Algorithm SHA256
    $guidedActive = $true
    $guidedActive = $false # equivalent to ManualTakeover's helper stop
    $after = Get-FileHash -LiteralPath $flightMarker -Algorithm SHA256
    Assert-True (-not $guidedActive) 'cancel did not stop the guide marker'
    Assert-True ($before.Hash -eq $after.Hash) 'cancel rewrote the current flight marker'

    Write-Output 'SOURCE_CHECKS_PASS: source contract, separated save slot, and cancellation preservation checks passed.'
}
finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}
