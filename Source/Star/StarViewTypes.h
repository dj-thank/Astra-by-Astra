#pragma once
#include "CoreMinimal.h"
#include "StarViewTypes.generated.h"

USTRUCT(BlueprintType)
struct FStarObjectiveView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FString Title;
    UPROPERTY(BlueprintReadOnly) FString Detail;
    UPROPERTY(BlueprintReadOnly) float Progress = 0.0f;
    UPROPERTY(BlueprintReadOnly) bool bComplete = false;
};

USTRUCT(BlueprintType)
struct FStarHUDSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString BodyId = TEXT("earth");
    UPROPERTY(BlueprintReadOnly) FString BodyName = TEXT("地球");
    UPROPERTY(BlueprintReadOnly) FString TargetId = TEXT("moon");
    UPROPERTY(BlueprintReadOnly) FString TargetName = TEXT("月");
    UPROPERTY(BlueprintReadOnly) bool bNavigationAvailable = false;
    UPROPERTY(BlueprintReadOnly) bool bAutopilotActive = false;
    UPROPERTY(BlueprintReadOnly) bool bHighSpeedAuthorized = false;
    UPROPERTY(BlueprintReadOnly) bool bNavigationSafetyPause = false;
    UPROPERTY(BlueprintReadOnly) bool bNavigationBrakingRecovery = false;
    UPROPERTY(BlueprintReadOnly) bool bCanConfirmTransfer = false;
    UPROPERTY(BlueprintReadOnly) FString NavigationStatusText;
    UPROPERTY(BlueprintReadOnly) FString TransferActionText;
    UPROPERTY(BlueprintReadOnly) FString AutopilotActionText;
    UPROPERTY(BlueprintReadOnly) FString MissionTitle;
    UPROPERTY(BlueprintReadOnly) FString MissionDetail;
    UPROPERTY(BlueprintReadOnly) FString StatusText;
    UPROPERTY(BlueprintReadOnly) FString WorldUtcText;
    UPROPERTY(BlueprintReadOnly) FString EarthSurfaceStatus;
    UPROPERTY(BlueprintReadOnly) double WorldUtcSeconds = 0;
    UPROPERTY(BlueprintReadOnly) double WorldClockRate = 1;
    UPROPERTY(BlueprintReadOnly) FString ControllerName;
    UPROPERTY(BlueprintReadOnly) double SpeedMps = 0;
    UPROPERTY(BlueprintReadOnly) double AltitudeM = 0;
    UPROPERTY(BlueprintReadOnly) FString AltitudeReferenceName = TEXT("地球");
    UPROPERTY(BlueprintReadOnly) double TargetDistanceM = 0;
    // Runtime projects the actual target relative to the active camera. No UI-side world coordinates.
    UPROPERTY(BlueprintReadOnly) bool bHasTargetDirection = false;
    // Marker center: viewport top-left (0,0), bottom-right (1,1). Onscreen is the raw projection;
    // runtime clamps only offscreen / behind-camera cues to a safe area.
    UPROPERTY(BlueprintReadOnly) FVector2D TargetScreenPositionNormalized = FVector2D(0.5, 0.5);
    UPROPERTY(BlueprintReadOnly) bool bTargetOnScreen = false;
    UPROPERTY(BlueprintReadOnly) bool bTargetBehind = false;
    // Camera-relative degrees: yaw right positive, pitch up positive.
    UPROPERTY(BlueprintReadOnly) float TargetYawDeg = 0;
    UPROPERTY(BlueprintReadOnly) float TargetPitchDeg = 0;
    // Current viewport / backbuffer dimensions and render screen percentage; zero means not yet measured.
    UPROPERTY(BlueprintReadOnly) int32 RenderWidth = 0;
    UPROPERTY(BlueprintReadOnly) int32 RenderHeight = 0;
    UPROPERTY(BlueprintReadOnly) float RenderScalePercent = 0;
    UPROPERTY(BlueprintReadOnly) double VerticalSpeedMps = 0;
    UPROPERTY(BlueprintReadOnly) double LatitudeDeg = 0;
    UPROPERTY(BlueprintReadOnly) double LongitudeDeg = 0;
    UPROPERTY(BlueprintReadOnly) float Throttle = 0;
    UPROPERTY(BlueprintReadOnly) float ScanProgress = 0;
    UPROPERTY(BlueprintReadOnly) float ExposureBias = 0;
    UPROPERTY(BlueprintReadOnly) bool bCockpitView = true;
    UPROPERTY(BlueprintReadOnly) bool bGearDeployed = false;
    UPROPERTY(BlueprintReadOnly) bool bLanded = false;
    UPROPERTY(BlueprintReadOnly) bool bCruise = false;
    UPROPERTY(BlueprintReadOnly) bool bPaused = false;
    UPROPERTY(BlueprintReadOnly) bool bPhotoMode = false;
    UPROPERTY(BlueprintReadOnly) bool bOnFoot = false;
    UPROPERTY(BlueprintReadOnly) bool bInteriorMonitor = true;
    UPROPERTY(BlueprintReadOnly) bool bJoystickConnected = false;
    UPROPERTY(BlueprintReadOnly) bool bCanScan = false;
    // Presentation telemetry stays distinct from the raw throttle command.
    // EngineOutput is the smoothed propulsion load used by audio/exhaust.
    UPROPERTY(BlueprintReadOnly) float EngineOutput = 0.0f;
    UPROPERTY(BlueprintReadOnly) bool bEnhancedStars = true;
    UPROPERTY(BlueprintReadOnly) bool bFlightAssist = true;
    UPROPERTY(BlueprintReadOnly) bool bGuidedTour = false;
    UPROPERTY(BlueprintReadOnly) float GuidedTourProgress = 0.0f;
    UPROPERTY(BlueprintReadOnly) FString GuidedTourTitle;
    UPROPERTY(BlueprintReadOnly) TArray<FStarObjectiveView> Objectives;
};
