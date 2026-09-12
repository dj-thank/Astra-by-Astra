#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Simulation/FlightSimulation.h"
#include "StarEarthTerrainComponent.generated.h"
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
struct FStarEarthTerrainState;
struct FStarEarthTerrainDeleter {void operator()(FStarEarthTerrainState*) const;};
UCLASS(ClassGroup=(STAR))
class STAR_API UStarEarthTerrainComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UStarEarthTerrainComponent();
    virtual ~UStarEarthTerrainComponent() override;
    void Initialize(UMaterialInterface* SurfaceMaterial);
    void UpdateTerrain(const star::BodyDefinition& Earth,const star::Vec3d& Camera,const star::Vec3d& Origin,double SpeedMps=0);
    void ApplyGlobeCoverage(UMaterialInstanceDynamic* Globe);
    void Shutdown();
    FString StatusText() const;
    bool FindSurveyDirection(const star::BodyDefinition& Earth,const star::Vec3d& Position,star::Vec3d& Forward) const;
    uint64 RadianceRevision() const { return RenderRevision; }
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    uint64 RenderRevision=0;
    void Upload(int32 Index);
    void RefreshCoverage();
    TUniquePtr<FStarEarthTerrainState,FStarEarthTerrainDeleter> State;
    UPROPERTY() TObjectPtr<UMaterialInterface> Material;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Meshes;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> Images;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> WaterMasks;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> WaterMaterials;
    UPROPERTY() TObjectPtr<UTexture2D> Coverage;
};
