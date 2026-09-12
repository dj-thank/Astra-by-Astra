#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "StarSolarVisualComponent.generated.h"
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
struct FStreamableHandle;
UCLASS()
class STAR_API UStarSolarVisualComponent : public USceneComponent {
    GENERATED_BODY()
public:
    void Initialize(UProceduralMeshComponent* InPhotosphere,UMaterialInstanceDynamic* Material);
    void RequestPreload();
    UMaterialInstanceDynamic* GetSurfaceMaterial()const{return SurfaceMaterial;}
    void Update(double Utc,double DiameterDegrees,double RadiusCm,bool Visible);
    bool Available()const{return bAvailable;}
private:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool BuildVisual();
    TSharedPtr<FStreamableHandle> Load;
    bool bRequested=false;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Photosphere;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Plasma;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PlasmaMaterial;
    bool bAvailable=false;
};
