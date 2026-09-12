#include "Runtime/StarSolarVisualComponent.h"
#include "Simulation/SolarVisual.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
bool UStarSolarVisualComponent::Initialize(UProceduralMeshComponent* Photosphere,UMaterialInstanceDynamic* Material) {
    SurfaceMaterial=Material;
    for(const TCHAR* Path:{TEXT("/Game/Star/SolarMotion/AiaTex.AiaTex"),TEXT("/Game/Star/SolarMotion/HmiTex.HmiTex"),TEXT("/Game/Star/SolarMotion/QualityTex.QualityTex")})
        if(!LoadObject<UTexture2D>(nullptr,Path))return false;
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Star/SolarMotion/M_SolarPlasma.M_SolarPlasma"));
    FString Text;TSharedPtr<FJsonObject> Root;
    if(!Base||!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Star/Data/solar-motion-curves.json")))||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())return false;
    const TArray<TSharedPtr<FJsonValue>>* Curves=nullptr;
    if(!Root->TryGetArrayField(TEXT("curves"),Curves)||Curves->IsEmpty())return false;
    TArray<FVector> V,N;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<int32> T;TArray<FProcMeshTangent> Tangents;
    int32 CurveIndex=0;
    for(const auto& Entry:*Curves) {
        const auto& Points=Entry->AsArray();if(Points.Num()<3)continue;
        const float Seed=FMath::Frac((++CurveIndex)*0.61803398875f);
        const bool Spicule=CurveIndex>96;const int32 Sides=Spicule?4:6;
        for(int32 Layer=0;Layer<2;++Layer) {
        const int32 Start=V.Num();
        for(int32 i=0;i<Points.Num();++i) {
            auto Point=[&](int32 j){const auto& A=Points[j]->AsArray();return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());};
            const FVector P=Point(i);const FVector Along=(Point(FMath::Min(i+1,Points.Num()-1))-Point(FMath::Max(i-1,0))).GetSafeNormal();
            const FVector U=FVector::CrossProduct(Along,P.GetSafeNormal()).GetSafeNormal();const FVector W=FVector::CrossProduct(Along,U).GetSafeNormal();
            const float S=static_cast<float>(i)/(Points.Num()-1);const float Width=(Spicule?0.001f:0.006f)*(Layer?3.0f:1.0f)*(0.30f+0.70f*FMath::Sin(PI*S));
            for(int32 k=0;k<Sides;++k) {
                const float A=2*PI*k/Sides;V.Add(P+(U*FMath::Cos(A)+W*FMath::Sin(A))*Width);
                N.Add(P.GetSafeNormal());UV.Add(FVector2D(S,FMath::Max(0.0,P.Length()-1.0)));
                Colors.Add(FLinearColor(Seed,Spicule?0.5f:1.0f,Layer?0.065f:1.0f,1));
                Tangents.Emplace(Along,false);
                if(i+1<Points.Num()){const int32 a=Start+i*Sides+k,b=Start+i*Sides+(k+1)%Sides;T.Append({a,a+Sides,b,b,a+Sides,b+Sides});}
            }
        }
        }
    }
    Plasma=NewObject<UProceduralMeshComponent>(GetOwner(),TEXT("SolarPlasma"));GetOwner()->AddInstanceComponent(Plasma);
    Plasma->SetupAttachment(Photosphere);Plasma->SetCollisionEnabled(ECollisionEnabled::NoCollision);Plasma->SetCastShadow(false);
    Plasma->SetTranslucentSortPriority(7);Plasma->SetBoundsScale(1.2f);Plasma->RegisterComponent();
    Plasma->CreateMeshSection_LinearColor(0,V,T,N,UV,Colors,Tangents,false,false);
    PlasmaMaterial=UMaterialInstanceDynamic::Create(Base,this);Plasma->SetMaterial(0,PlasmaMaterial);
    bAvailable=true;UE_LOG(LogTemp,Display,TEXT("STAR solar motion: %d curves, %d vertices; UTC deterministic; modeled emission"),CurveIndex,V.Num());return true;
}
void UStarSolarVisualComponent::Update(double Utc,double DiameterDegrees,double RadiusCm,bool Visible) {
    if(!bAvailable)return;
    const auto S=star::solarvisual::Evaluate(DiameterDegrees,Utc,!FParse::Param(FCommandLine::Get(),TEXT("StarLegacySun")));
    SurfaceMaterial->SetScalarParameterValue(TEXT("SolarDetail"),S.surface);
    SurfaceMaterial->SetScalarParameterValue(TEXT("SolarTime"),S.seconds);
    Plasma->SetVisibility(Visible&&S.plasma>0.0001);
    PlasmaMaterial->SetScalarParameterValue(TEXT("SolarDetail"),S.plasma);
    PlasmaMaterial->SetScalarParameterValue(TEXT("SolarTime"),S.seconds);
    PlasmaMaterial->SetScalarParameterValue(TEXT("SolarScaleCm"),RadiusCm);
    PlasmaMaterial->SetScalarParameterValue(TEXT("SunRadiance"),SurfaceMaterial->K2_GetScalarParameterValue(TEXT("SunRadiance")));
}
