#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Simulation/FlightSimulation.h"
#include "StarLunarTerrainComponent.generated.h"

class FStarDataCatalog;
class UMaterialInterface;
class UProceduralMeshComponent;
struct FStarLunarTerrainState;
struct FStarLunarTerrainStateDeleter
{
    void operator()(FStarLunarTerrainState* Value) const;
};

/** Curved, asynchronous lunar clipmaps. The owner retains an immutable catalog
 * until Shutdown() returns. Call UpdateTerrain once per render frame on the game
 * thread, BEFORE reading GetGlobeHole and updating the globe material. */
UCLASS(ClassGroup=(STAR))
class STAR_API UStarLunarTerrainComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UStarLunarTerrainComponent();
    virtual ~UStarLunarTerrainComponent() override;
    void Initialize(const FStarDataCatalog* InCatalog, UMaterialInterface* SurfaceMaterial);
    void UpdateTerrain(const star::Vec3d& ShipAbsoluteMeters, const star::Vec3d& RenderOriginMeters);
    void GetGlobeHole(star::Vec3d& OutBodyLocalRHDirection, double& OutCosAngularRadius, bool& OutEnabled) const;
    bool HasCoverage() const;
    /** True only when the CURRENT ship footprint plus 32m is in a committed 5m mesh.
     * This is rendering readiness; physical contact still uses the flight sampler. */
    bool IsReadyForLanding() const;
    /** Cancels and joins the single sampling job; safe to call repeatedly. */
    void Shutdown();
protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnUnregister() override;
private:
    UProceduralMeshComponent* AcquireMesh();
    void ReleaseMeshes();
    void PositionMeshes(const star::Vec3d& RenderOriginMeters);
    TUniquePtr<FStarLunarTerrainState,FStarLunarTerrainStateDeleter> State;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> Material;
    UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> MeshPool;
};
