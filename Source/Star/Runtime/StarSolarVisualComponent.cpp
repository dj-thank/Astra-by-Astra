#include "Runtime/StarSolarVisualComponent.h"
#include "Simulation/SolarVisual.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Runtime/StarDiagnostics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
void UStarSolarVisualComponent::Initialize(UProceduralMeshComponent* InPhotosphere,UMaterialInstanceDynamic* Material) {
    Photosphere=InPhotosphere;SurfaceMaterial=Material;
}
void UStarSolarVisualComponent::RequestPreload() {
    if(bRequested||!Photosphere||FParse::Param(FCommandLine::Get(),TEXT("StarLegacySun")))return;
    bRequested=true;
    TArray<FSoftObjectPath> Paths;
    for(const TCHAR* Name:{TEXT("AiaTex"),TEXT("HmiTex"),TEXT("QualityTex"),TEXT("M_SolarSurface"),TEXT("M_SolarPlasma")})
        Paths.Emplace(FString::Printf(TEXT("/Game/Star/SolarMotion/%s.%s"),Name,Name));
    UE_LOG(LogTemp,Display,TEXT("STAR solar preload requested: destination/approach; analytic Sun remains available"));
    Load=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,FStreamableDelegate::CreateWeakLambda(this,[this]{
        StarDiagnostics::FScope Scope(TEXT("solar_preload_commit"));
        if(!BuildVisual())UE_LOG(LogTemp,Warning,TEXT("STAR solar motion unavailable: analytic fallback retained"));
        Load.Reset();
    }));
}
void UStarSolarVisualComponent::EndPlay(const EEndPlayReason::Type Reason) {
    if(Load){Load->CancelHandle();Load.Reset();}
    Super::EndPlay(Reason);
}
bool UStarSolarVisualComponent::BuildVisual() {
    for(const TCHAR* Path:{TEXT("/Game/Star/SolarMotion/AiaTex.AiaTex"),TEXT("/Game/Star/SolarMotion/HmiTex.HmiTex"),TEXT("/Game/Star/SolarMotion/QualityTex.QualityTex")})
    {
        auto* Texture=Cast<UTexture2D>(FSoftObjectPath(Path).ResolveObject());if(!Texture)return false;
        // Spherical custom sampling has no ordinary UV-density estimate. Keep
        // only these three observation maps sharp after preloading at a distance.
        Texture->bForceMiplevelsToBeResident=true;
    }
    auto* Base=Cast<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Star/SolarMotion/M_SolarPlasma.M_SolarPlasma")).ResolveObject());
    auto* Surface=Cast<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Star/SolarMotion/M_SolarSurface.M_SolarSurface")).ResolveObject());
    if(!Base||!Surface||!SurfaceMaterial||!Photosphere||!GetOwner())return false;
    const FString CurvesPath=FPaths::ProjectContentDir()/TEXT("Star/Data/solar-motion-curves.json");
    // Bound JSON size before loading to avoid OOM on corrupt/modified content.
    if(IFileManager::Get().FileSize(*CurvesPath)>8*1024*1024)return false;
    FString Text;TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text,*CurvesPath)||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())return false;
    const TArray<TSharedPtr<FJsonValue>>* Curves=nullptr;
    if(!Root->TryGetArrayField(TEXT("curves"),Curves)||Curves->IsEmpty()||Curves->Num()>4096)return false;
    TArray<FVector> V,N;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<int32> T;TArray<FProcMeshTangent> Tangents;
    int32 CurveIndex=0;
    for(const auto& Entry:*Curves) {
        if(!Entry.IsValid()||Entry->Type!=EJson::Array)continue;
        const auto& Points=Entry->AsArray();if(Points.Num()<3||Points.Num()>8192)continue;
        // Validate all points before building geometry; reject non-finite or malformed curves.
        TArray<FVector> Clean;Clean.Reserve(Points.Num());
        bool Valid=true;
        for(const auto& P:Points) {
            if(!P.IsValid()||P->Type!=EJson::Array) {Valid=false;break;}
            const auto& A=P->AsArray();
            if(A.Num()<3) {Valid=false;break;}
            double X=0,Y=0,Z=0;
            if(!A[0]->TryGetNumber(X)||!A[1]->TryGetNumber(Y)||!A[2]->TryGetNumber(Z)||
               !FMath::IsFinite(X)||!FMath::IsFinite(Y)||!FMath::IsFinite(Z)) {Valid=false;break;}
            // Solar loop coordinates are unit-sphere-ish; reject absurd values.
            if(FMath::Abs(X)>10.0||FMath::Abs(Y)>10.0||FMath::Abs(Z)>10.0) {Valid=false;break;}
            Clean.Add(FVector(X,Y,Z));
        }
        if(!Valid||Clean.Num()<3)continue;
        // Bound total vertices to keep GPU upload and index range safe.
        const int32 SidesPreview=(CurveIndex+1>96)?4:6;
        if(V.Num()>2000000-Clean.Num()*SidesPreview*2)break;
        const float Seed=FMath::Frac((++CurveIndex)*0.61803398875f);
        const bool Spicule=CurveIndex>96;const int32 Sides=Spicule?4:6;
        for(int32 Layer=0;Layer<2;++Layer) {
        const int32 Start=V.Num();
        for(int32 i=0;i<Clean.Num();++i) {
            const FVector P=Clean[i];
            const FVector Along=(Clean[FMath::Min(i+1,Clean.Num()-1)]-Clean[FMath::Max(i-1,0)]).GetSafeNormal();
            const FVector U=FVector::CrossProduct(Along,P.GetSafeNormal()).GetSafeNormal();const FVector W=FVector::CrossProduct(Along,U).GetSafeNormal();
            const float S=static_cast<float>(i)/(Clean.Num()-1);const float Width=(Spicule?0.001f:0.006f)*(Layer?3.0f:1.0f)*(0.30f+0.70f*FMath::Sin(PI*S));
            for(int32 k=0;k<Sides;++k) {
                const float A=2*PI*k/Sides;V.Add(P+(U*FMath::Cos(A)+W*FMath::Sin(A))*Width);
                N.Add(P.GetSafeNormal());UV.Add(FVector2D(S,FMath::Max(0.0,P.Length()-1.0)));
                Colors.Add(FLinearColor(Seed,Spicule?0.5f:1.0f,Layer?0.065f:1.0f,1));
                Tangents.Emplace(Along,false);
                if(i+1<Clean.Num()){const int32 a=Start+i*Sides+k,b=Start+i*Sides+(k+1)%Sides;T.Append({a,a+Sides,b,b,a+Sides,b+Sides});}
            }
        }
        }
    }
    if(V.Num()==0||V.Num()!=N.Num()||V.Num()!=UV.Num()||V.Num()!=Colors.Num()||V.Num()!=Tangents.Num()||T.Num()%3!=0)return false;
    Plasma=NewObject<UProceduralMeshComponent>(GetOwner(),TEXT("SolarPlasma"));GetOwner()->AddInstanceComponent(Plasma);
    Plasma->SetupAttachment(Photosphere);Plasma->SetCollisionEnabled(ECollisionEnabled::NoCollision);Plasma->SetCastShadow(false);
    Plasma->SetTranslucentSortPriority(7);Plasma->SetBoundsScale(1.2f);Plasma->RegisterComponent();
    Plasma->CreateMeshSection_LinearColor(0,V,T,N,UV,Colors,Tangents,false,false);
    PlasmaMaterial=UMaterialInstanceDynamic::Create(Base,this);Plasma->SetMaterial(0,PlasmaMaterial);
    Plasma->SetVisibility(false);
    auto* Detailed=UMaterialInstanceDynamic::Create(Surface,this);
    Detailed->CopyMaterialUniformParameters(SurfaceMaterial);
    Detailed->SetScalarParameterValue(TEXT("SolarDetail"),0);
    SurfaceMaterial=Detailed;Photosphere->SetMaterial(0,Detailed);
    bAvailable=true;UE_LOG(LogTemp,Display,TEXT("STAR solar motion: %d curves, %d vertices; UTC deterministic; modeled emission"),CurveIndex,V.Num());return true;
}
void UStarSolarVisualComponent::Update(double Utc,double DiameterDegrees,double RadiusCm,bool Visible) {
    if(DiameterDegrees>=0.8)RequestPreload();
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
