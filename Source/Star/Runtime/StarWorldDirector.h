#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Runtime/StarDataCatalog.h"
#include "Exploration/StarExplorationSubsystem.h"
#include "RenderCommandFence.h"
#include "StarWorldDirector.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class ADirectionalLight;
class APostProcessVolume;
class ASkyLight;
class UStarLunarTerrainComponent;
class UStarEarthTerrainComponent;
class UStarSolarVisualComponent;

UCLASS()
class STAR_API AStarWorldDirector : public AActor
{
    GENERATED_BODY()
public:
    AStarWorldDirector();
    bool Initialize();
    void PreloadSun();
    const FStarDataCatalog& Catalog() const { return Data; }
    FString Error() const { return LoadError; }
    bool IsReady() const { return bReady; }
    double WorldUtc() const { return AstronomicalUtc; }
    double ClockRate() const { return AstronomicalRate; }
    void SetClockRate(double Rate) { if(Rate==0||Rate==1||Rate==60||Rate==600) AstronomicalRate=Rate; }
    bool SetWorldUtc(double UnixSeconds);
    void UpdateScene(const star::FlightState& State, const star::Vec3d& OriginMeters, const star::Vec3d& CameraMeters, double DeltaSeconds, bool bActiveCameraUpdate=true);
    FStarObservationSample Observe(const star::FlightState& State, const star::Vec3d& CameraMeters, const star::Vec3d& CameraForward, const FString& TargetId, bool bScanning, bool bCockpit, double DeltaSeconds, uint64 Sequence) const;
    star::FlightState InitialFlightState() const;
    void SetExposure(float Value);
    // Toggle the cosmetic photographic sky. The NASA ICRF/J2000 catalogue
    // remains the restrained fallback when this is disabled.
    void SetEnhancedStars(bool Enabled);
    // Return the rendered (possibly angular-size-preserving proxy) component
    // centre in the current Unreal world centimetre frame.
    bool GetRenderedBodyCenter(const FString& BodyId, FVector& OutWorldCm) const;
    star::Vec3d SafeCameraPosition(const star::Vec3d& Desired) const;
    bool TerrainReadyForLanding() const;
    FString EarthSurfaceStatus() const;
    FLinearColor EnvironmentLightIntegral() const;
    float MinimumExposureEV() const;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<UStarEarthTerrainComponent> EarthTerrain;
    bool bEarthLUTEnabled=true, bEarthVolumeClouds=true, bEarthPacificEnabled=true;
    double EarthWeatherTimeOverride=-1.0;
    void CreateBodies();
    void CreateLighting();
    void CaptureEnvironmentDiagnostic(const star::Vec3d& Origin, const star::Vec3d& Camera);
    void UpdateLocalEnvironment(const star::Vec3d& Origin,const star::Vec3d& Camera,double Dt);
    void UpdateTerrain(const star::FlightState& State, const star::Vec3d& OriginMeters);
    UProceduralMeshComponent* NewMesh(const FString& Name);
    static void Sphere(UProceduralMeshComponent* Mesh, int32 Segments, int32 Rings, bool bInside = false, const FStarDataCatalog* MoonTerrain = nullptr);
    static void Ring(UProceduralMeshComponent* Mesh, double Inner, double Outer);
    static bool RaySphere(const star::Vec3d& RayStart, const star::Vec3d& RayDirection, const star::Vec3d& Center, double Radius, double& HitDistance);
    FStarDataCatalog Data;
    FString LoadError;
    bool bReady = false;
    bool bInitializationAttempted = false;
    double SceneTime = 0;
    double AstronomicalUtc=0, AstronomicalRate=1;
    bool bEnvironmentCaptureDiagnostic = false;
    bool bEnvironmentCaptureRequested = false;
    bool bLocalEnvironment = false;
    bool bEnvironmentPending = false, bEnvironmentFenceIssued = false, bEnvironmentActive = false;
    uint64 EnvironmentUpdateFrame = MAX_uint64;
    double EnvironmentClock = 0, EnvironmentCaptureTime = 0;
    double EnvironmentValidityMeters = 500, EnvironmentFade = 0;
    FString EnvironmentBody;
    star::Vec3d EnvironmentCapturePosition, EnvironmentSunLocal, EnvironmentWorldDirection;
    FLinearColor EnvironmentIntegral=FLinearColor::Black;
    double EnvironmentCaptureUtc=0;
    uint64 EnvironmentTerrainRevision=0;
    FRenderCommandFence EnvironmentFence;
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<UStarSolarVisualComponent> SolarVisual;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> BodyMeshes;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Atmospheres;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> AtmosphereMaterials;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> EarthCloudMesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> EarthCloudMaterial;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> RingMesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> StarMesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StarMaterial;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> SunMesh;
    UPROPERTY() TObjectPtr<UStarLunarTerrainComponent> LunarTerrain;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TerrainMaterial;
    UPROPERTY() TObjectPtr<ADirectionalLight> SunLight;
    UPROPERTY() TObjectPtr<APostProcessVolume> PostProcess;
    UPROPERTY() TObjectPtr<ASkyLight> SkyLight;
    bool bEnhancedStars = false;
    float PhotoSkyIntensity = 0.08f;
    bool bFarPhoto = false;
    float FarPhotoGain = 1.0f;
};
