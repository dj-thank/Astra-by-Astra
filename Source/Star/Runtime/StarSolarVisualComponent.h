#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "StarSolarVisualComponent.generated.h"
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
UCLASS()
class STAR_API UStarSolarVisualComponent : public USceneComponent {
    GENERATED_BODY()
public:
    bool Initialize(UProceduralMeshComponent* Photosphere,UMaterialInstanceDynamic* Material);
    void Update(double Utc,double DiameterDegrees,double RadiusCm,bool Visible);
    bool Available()const{return bAvailable;}
private:
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Plasma;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PlasmaMaterial;
    bool bAvailable=false;
};
