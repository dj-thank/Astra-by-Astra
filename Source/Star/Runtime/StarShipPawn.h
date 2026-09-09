#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Simulation/FlightSimulation.h"
#include "StarViewTypes.h"
#include "StarShipPawn.generated.h"

class UCameraComponent;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UStarShipAudioComponent;
class AStarWorldDirector;

// Runtime-owned references to the photo-derived exhaust sections. The meshes
// are registered as actor instance components, so raw pointers here do not
// become an additional ownership surface for the UObject graph.
struct FStarPhotoPlumeRuntime
{
    UProceduralMeshComponent* PlaneMesh = nullptr;
    UProceduralMeshComponent* PlaneLodMesh = nullptr;
    UProceduralMeshComponent* AxialMesh = nullptr;
    UMaterialInstanceDynamic* PlaneMaterial = nullptr;
    UMaterialInstanceDynamic* AxialMaterial = nullptr;
    FVector LocalPositionCm = FVector::ZeroVector;
    FVector ExhaustAxis = FVector(-1.0f, 0.0f, 0.0f);
    float DiameterCm = 0.0f;
    float LengthScale = 1.0f;
    float Radiance = 40.0f;
    bool bMain = false;
};

UCLASS()
class STAR_API AStarShipPawn : public APawn
{
    GENERATED_BODY()
public:
    AStarShipPawn();
    bool InitializeFlight();
    bool IsReady() const;
    FString Error() const { return LoadError; }
    void AdvanceFlight(double Dt,const star::FlightInput& Input);
    void SetLook(float YawDelta,float PitchDelta,bool bPhotoMode);
    void LookAtAbsolute(const star::Vec3d& Target,double Dt);
    void SetGuidedCameraOffset(const star::Vec3d& OffsetMeters,double Dt);
    void ClearGuidedCamera();
    void RecenterLook();
    void ToggleView();
    bool ToggleLandingGear();
    bool IsGearAnimating() const { return GearAlpha>0.001f&&GearAlpha<0.999f; }
    bool GearDesired() const { return bDesiredGearDeployed; }
    void SetPhotoOffset(const FVector& OffsetDelta);
    void ClearPhotoOffset();
    void UpdatePresentation(const FStarHUDSnapshot& Snapshot,bool bPaused,float MasterVolume);
    void PlaySoundEvent(FName Name);
    void SetEVAAudio(bool Enabled);
    void SetInteriorMonitor(bool Enabled);
    bool IsCockpitView() const { return bCockpitView; }
    star::FlightSimulation* Simulation() const { return Flight.Get(); }
    AStarWorldDirector* Director() const { return WorldDirector; }
    star::Vec3d CameraAbsoluteMeters() const;
    star::Vec3d CameraForwardSimulation() const;
    star::Vec3d CameraRightSimulation() const;
    star::Vec3d CameraUpSimulation() const;
    float CameraHorizontalFOVDegrees() const;
    const star::Vec3d& OriginMeters() const { return RenderOrigin; }
    bool RestoreFlight(const star::FlightState& State);
    bool SetWorldUtc(double UnixSeconds);
    void AdvanceWorldClock(double DeltaSeconds);
    bool IsVRRequested() const { return bVRRequested; }
    USceneComponent* VRTrackingOrigin() const { return TrackingOrigin; }
    virtual void CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool LoadShip();
    bool LoadPhotoPlumes();
    void UpdatePhotoPlumes(double Dt,bool bPaused);
    void RefreshTransform(double Dt);
    void CreateInstrument(const FString& Name,const FVector& PositionCm,const FVector2D& SizeM,FName Mode);
    UPROPERTY() TObjectPtr<USceneComponent> ShipRoot;
    UPROPERTY() TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY() TObjectPtr<UCameraComponent> FlightCamera;
    UPROPERTY() TObjectPtr<USceneComponent> TrackingOrigin;
    bool bVRRequested=false;
    UPROPERTY() TObjectPtr<UStarShipAudioComponent> ShipAudio;
    UPROPERTY() TObjectPtr<AStarWorldDirector> WorldDirector;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Meshes;
    UPROPERTY() TMap<FString,TObjectPtr<USceneComponent>> GearPivots;
    UPROPERTY() TArray<TObjectPtr<UWidgetComponent>> Instruments;
    TArray<FStarPhotoPlumeRuntime> PhotoPlumes;
    TUniquePtr<star::FlightSimulation> Flight;
    star::Vec3d RenderOrigin;
    FVector CockpitSocketCm = FVector(600,0,130);
    FVector ChaseSocketCm = FVector(-2400,0,900);
    FVector PhotoOffsetCm = FVector::ZeroVector;
    FString LoadError;
    bool bCockpitView = true;
    bool bAssetsReady = false;
    float LookYaw = 0,LookPitch = 0;
    float GearAlpha = 0;
    bool bDesiredGearDeployed = false;
    FRotator GuideLookRotation = FRotator::ZeroRotator;
    FQuat GuideWorldRotation=FQuat::Identity;
    FVector RcsInputCommand = FVector::ZeroVector;
    FVector RcsTranslationCommand = FVector::ZeroVector;
    bool bPendingRcsSound=false;
    double RcsSoundRequestTime=0.0;
    float PhotoPlumePhase = 0.0f;
    float RcsPulseRemaining = 0.0f;
    float RcsPulseDemand = 0.0f;
    bool bGuideLookActive = false;
    bool bGuideCameraOffsetActive = false;
    bool bGuidedCameraUserOverride = false;
    star::Vec3d GuideCameraOffsetMeters;
};
