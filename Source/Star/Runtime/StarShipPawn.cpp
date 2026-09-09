#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Runtime/StarDataCatalog.h"
#include "Presentation/StarInstrumentWidget.h"
#include "Presentation/StarShipAudioComponent.h"
#include "Camera/CameraComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace
{
struct FPhotoPlumeMeshData
{
    TArray<FVector> Vertices;
    TArray<FVector2D> UVs;
    TArray<int32> Triangles;
    TArray<int32> LowLodTriangles;
};

struct FPhotoNozzleSpec
{
    FString Name;
    FVector PositionCm = FVector::ZeroVector;
    FVector ExhaustAxis = FVector(-1.0f, 0.0f, 0.0f);
    float DiameterCm = 0.0f;
    float LengthScale = 1.0f;
    float Radiance = 40.0f;
    bool bMain = false;
};

FVector JsonVector(const TSharedPtr<FJsonObject>& Json,const TCHAR* Key,FVector Default=FVector::ZeroVector)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Json.IsValid()||!Json->TryGetArrayField(Key,Values)||Values->Num()!=3) return Default;
    return FVector((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber(),(*Values)[2]->AsNumber());
}

bool JsonVectorValue(const TSharedPtr<FJsonValue>& Value,FVector& Out)
{
    if(!Value.IsValid()||Value->Type!=EJson::Array) return false;
    const TArray<TSharedPtr<FJsonValue>>& Values=Value->AsArray();
    if(Values.Num()!=3) return false;
    Out=FVector(Values[0]->AsNumber(),Values[1]->AsNumber(),Values[2]->AsNumber());
    return !Out.ContainsNaN();
}

bool JsonUVValue(const TSharedPtr<FJsonValue>& Value,FVector2D& Out)
{
    if(!Value.IsValid()||Value->Type!=EJson::Array) return false;
    const TArray<TSharedPtr<FJsonValue>>& Values=Value->AsArray();
    if(Values.Num()!=2) return false;
    Out=FVector2D(Values[0]->AsNumber(),Values[1]->AsNumber());
    return FMath::IsFinite(Out.X)&&FMath::IsFinite(Out.Y);
}

bool JsonTriangleValue(const TSharedPtr<FJsonValue>& Value,TArray<int32>& Out)
{
    if(!Value.IsValid()||Value->Type!=EJson::Array) return false;
    const TArray<TSharedPtr<FJsonValue>>& Values=Value->AsArray();
    if(Values.Num()!=3) return false;
    for(const auto& IndexValue:Values)
    {
        const double Number=IndexValue->AsNumber();
        const int32 Index=static_cast<int32>(Number);
        if(!FMath::IsFinite(Number)||Number!=static_cast<double>(Index)) return false;
        Out.Add(Index);
    }
    return true;
}

bool LoadJsonFile(const FString& Filename,TSharedPtr<FJsonObject>& Out)
{
    FString Text;
    if(!FFileHelper::LoadFileToString(Text,*Filename)) return false;
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)&&Out.IsValid();
}

bool LoadPhotoMeshData(const FString& Filename,FPhotoPlumeMeshData& Out,FString& OutError,bool bBuildLowLod)
{
    TSharedPtr<FJsonObject> Json;
    if(!LoadJsonFile(Filename,Json))
    {
        OutError=FString::Printf(TEXT("写真噴流メッシュを読み込めません: %s"),*Filename);
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Vertices=nullptr;
    const TArray<TSharedPtr<FJsonValue>>* UVs=nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Triangles=nullptr;
    if(!Json->TryGetArrayField(TEXT("vertices"),Vertices)||!Json->TryGetArrayField(TEXT("uv0"),UVs)||
       !Json->TryGetArrayField(TEXT("triangles"),Triangles)||Vertices->IsEmpty()||UVs->Num()!=Vertices->Num())
    {
        OutError=FString::Printf(TEXT("写真噴流メッシュの頂点/UV/三角形が不正です: %s"),*Filename);
        return false;
    }
    Out.Vertices.Reserve(Vertices->Num());
    Out.UVs.Reserve(UVs->Num());
    for(const auto& Value:*Vertices)
    {
        FVector Vertex;
        if(!JsonVectorValue(Value,Vertex))
        {
            OutError=TEXT("写真噴流メッシュの頂点が有限値ではありません。");
            return false;
        }
        Out.Vertices.Add(Vertex);
    }
    for(const auto& Value:*UVs)
    {
        FVector2D UV;
        if(!JsonUVValue(Value,UV))
        {
            OutError=TEXT("写真噴流メッシュのUVが不正です。");
            return false;
        }
        Out.UVs.Add(UV);
    }
    Out.Triangles.Reserve(Triangles->Num()*3);
    for(const auto& Value:*Triangles)
    {
        if(!JsonTriangleValue(Value,Out.Triangles))
        {
            OutError=TEXT("写真噴流メッシュの三角形が不正です。");
            return false;
        }
    }
    for(const int32 Index:Out.Triangles)
    {
        if(Index<0||Index>=Out.Vertices.Num())
        {
            OutError=TEXT("写真噴流メッシュのインデックスが範囲外です。");
            return false;
        }
    }
    if(Out.Triangles.IsEmpty()||Out.Triangles.Num()%3!=0)
    {
        OutError=TEXT("写真噴流メッシュに三角形がありません。");
        return false;
    }
    // The checked-in vacuum mesh is six contiguous axial planes. Keep a
    // two-plane section for the <4px LOD so the translucent overdraw remains
    // bounded while preserving the same local +X exhaust frame.
    if(bBuildLowLod&&Out.Triangles.Num()%6==0)
    {
        const int32 IndicesPerPlane=Out.Triangles.Num()/6;
        Out.LowLodTriangles.Append(Out.Triangles.GetData(),IndicesPerPlane);
        Out.LowLodTriangles.Append(Out.Triangles.GetData()+IndicesPerPlane*3,IndicesPerPlane);
    }
    return true;
}

float Smooth01(float Value)
{
    const float X=FMath::Clamp(Value,0.0f,1.0f);
    return X*X*(3.0f-2.0f*X);
}
}
AStarShipPawn::AStarShipPawn()
{
    PrimaryActorTick.bCanEverTick=false;
    ShipRoot=CreateDefaultSubobject<USceneComponent>(TEXT("FlightReference"));
    SetRootComponent(ShipRoot);
    VisualRoot=CreateDefaultSubobject<USceneComponent>(TEXT("ExplorerVisuals"));
    VisualRoot->SetupAttachment(ShipRoot);
    FlightCamera=CreateDefaultSubobject<UCameraComponent>(TEXT("FlightCamera"));
    TrackingOrigin=CreateDefaultSubobject<USceneComponent>(TEXT("SeatedTrackingOrigin"));
    TrackingOrigin->SetupAttachment(ShipRoot);
    bVRRequested=FParse::Param(FCommandLine::Get(),TEXT("StarVR"));
    FlightCamera->SetupAttachment(bVRRequested?TrackingOrigin.Get():ShipRoot.Get());
    FlightCamera->bLockToHmd=bVRRequested;
    FlightCamera->bUsePawnControlRotation=false;
    FlightCamera->FieldOfView=100;
    FlightCamera->bConstrainAspectRatio=false;
    FlightCamera->bOverrideAspectRatioAxisConstraint=true;
    FlightCamera->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV);
    ShipAudio=CreateDefaultSubobject<UStarShipAudioComponent>(TEXT("CabinAudio"));
    ShipAudio->SetupAttachment(ShipRoot);
    AutoPossessPlayer=EAutoReceiveInput::Disabled;
}
void AStarShipPawn::BeginPlay() { Super::BeginPlay(); InitializeFlight(); }
void AStarShipPawn::EndPlay(const EEndPlayReason::Type Reason)
{
    if(ShipAudio) ShipAudio->StopShipAudio();
    Super::EndPlay(Reason);
}
bool AStarShipPawn::InitializeFlight()
{
    if(Flight) return IsReady();
    WorldDirector=Cast<AStarWorldDirector>(UGameplayStatics::GetActorOfClass(GetWorld(),AStarWorldDirector::StaticClass()));
    if(!WorldDirector) WorldDirector=GetWorld()->SpawnActor<AStarWorldDirector>();
    if(!WorldDirector||!WorldDirector->Initialize())
    { LoadError=WorldDirector?WorldDirector->Error():TEXT("宇宙空間を初期化できません。"); return false; }
    auto Terrain=[this](const star::BodyDefinition& Body,const star::Vec3d& Direction,star::TerrainSample& Out)
    { return IsValid(WorldDirector)&&WorldDirector->Catalog().SampleTerrain(Body,Direction,Out); };
    Flight=MakeUnique<star::FlightSimulation>(WorldDirector->Catalog().SimulationBodies(),star::FlightConfig{},Terrain);
    const auto Initial=WorldDirector->InitialFlightState();
    Flight->RestoreState(Initial);
    RenderOrigin=Initial.positionMeters;
    bAssetsReady=LoadShip();
    RefreshTransform(0);
    WorldDirector->UpdateScene(Flight->State(),RenderOrigin,CameraAbsoluteMeters(),0);
    if(ShipAudio)
    {
        ShipAudio->SetMasterVolume(0.5f);
        const auto& Propulsion=Flight->Propulsion();
        ShipAudio->SetFlightParameters(static_cast<float>(Propulsion.engineOutput),Initial.velocityMetersPerSecond.Length(),true,false);
        ShipAudio->SetEngineDynamics(static_cast<float>(Propulsion.cruiseCharge),static_cast<float>(Propulsion.braking));
        ShipAudio->StartShipAudio();
    }
    return IsReady();
}
bool AStarShipPawn::IsReady() const { return Flight.IsValid()&&IsValid(WorldDirector)&&WorldDirector->IsReady()&&bAssetsReady; }
bool AStarShipPawn::LoadShip()
{
    FString Text;
    const FString Filename=FPaths::ProjectContentDir()/TEXT("Star/Data/runtime_assets.json");
    if(!FFileHelper::LoadFileToString(Text,*Filename)) { LoadError=TEXT("機体アセットの取込が必要です。runtime_assets.json がありません。"); return false; }
    TSharedPtr<FJsonObject> Json;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json)||!Json.IsValid())
    { LoadError=TEXT("機体アセット情報が不正です。"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;
    if(!Json->TryGetArrayField(TEXT("shipParts"),Parts)||Parts->IsEmpty())
    { LoadError=TEXT("探査船のメッシュがありません。"); return false; }
    for(const auto& PartValue:*Parts)
    {
        const auto Part=PartValue->AsObject();
        if(!Part.IsValid()) continue;
        FString Name,Asset,PartRole;
        Part->TryGetStringField(TEXT("name"),Name);
        Part->TryGetStringField(TEXT("asset"),Asset);
        Part->TryGetStringField(TEXT("role"),PartRole);
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Asset);
        if(!Mesh) { LoadError=FString::Printf(TEXT("機体部品を読み込めません: %s"),*Name); return false; }
        auto* Component=NewObject<UStaticMeshComponent>(this,FName(*Name));
        AddInstanceComponent(Component);
        USceneComponent* Parent=VisualRoot;
        const FVector Location=JsonVector(Part,TEXT("locationCm"));
        if(PartRole==TEXT("gear"))
        {
            auto* Pivot=NewObject<USceneComponent>(this,FName(*(Name+TEXT("_Pivot"))));
            AddInstanceComponent(Pivot);
            Pivot->SetupAttachment(VisualRoot);
            Pivot->SetRelativeLocation(Location);
            Pivot->RegisterComponent();
            GearPivots.Add(Name,Pivot);
            Parent=Pivot;
        }
        Component->SetupAttachment(Parent);
        Component->SetStaticMesh(Mesh);
        Component->SetRelativeLocation(Parent==VisualRoot?Location:FVector::ZeroVector);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        const bool bCabinPart=Name.Contains(TEXT("Cockpit"))||Name.Contains(TEXT("Instrument"))||Name.Contains(TEXT("ControlStick"))||Name.Contains(TEXT("Throttle"));
        Component->SetLightingChannels(true,bCabinPart,!bCabinPart&&PartRole!=TEXT("glass"));
        Component->RegisterComponent();
        Meshes.Add(Component);
    }
    const TSharedPtr<FJsonObject>* Sockets=nullptr;
    if(Json->TryGetObjectField(TEXT("sockets"),Sockets))
    {
        CockpitSocketCm=JsonVector(*Sockets,TEXT("cockpitCameraCm"),CockpitSocketCm);
        ChaseSocketCm=JsonVector(*Sockets,TEXT("chaseCameraCm"),ChaseSocketCm);
    }
    ChaseSocketCm.X*=1.4; // Give the terrain and destination room around the complete hull.
    const TArray<TSharedPtr<FJsonValue>>* Displays=nullptr;
    if(Json->TryGetArrayField(TEXT("instrumentDisplays"),Displays))
    {
        int32 Index=0;
        for(const auto& DisplayValue:*Displays)
        {
            const auto Display=DisplayValue->AsObject();
            if(!Display.IsValid()) continue;
            double Width=0.619,Height=0.312;
            Display->TryGetNumberField(TEXT("widthM"),Width);
            Display->TryGetNumberField(TEXT("heightM"),Height);
            FString Name;Display->TryGetStringField(TEXT("name"),Name);
            CreateInstrument(Name,JsonVector(Display,TEXT("locationCm")),FVector2D(Width,Height),Index++==0?FName(TEXT("Flight")):FName(TEXT("Navigation")));
        }
    }
    if(Instruments.IsEmpty())
    {
        CreateInstrument(TEXT("DisplayFlight"),FVector(683.8,0,74.7),FVector2D(0.619,0.312),TEXT("Flight"));
        CreateInstrument(TEXT("DisplayNavigation"),FVector(683.8,-88,74.7),FVector2D(0.619,0.312),TEXT("Navigation"));
    }
    auto* CabinLight=NewObject<UPointLightComponent>(this,TEXT("CabinInstrumentLight"));
    AddInstanceComponent(CabinLight);
    CabinLight->SetupAttachment(VisualRoot);
    CabinLight->SetRelativeLocation(FVector(480,0,177));
    CabinLight->SetIntensityUnits(ELightUnits::Lumens);
    // Display-calibrated fictional cabin task lights: retain readable detail
    // across the daylight exposure range, without altering the real Sun.
    float CabinScale=3.0f;
    FParse::Value(FCommandLine::Get(),TEXT("StarCabinLightScale="),CabinScale);
    CabinScale=FMath::Clamp(CabinScale,0.25f,4.0f);
    CabinLight->SetIntensity(40*CabinScale);
    CabinLight->InverseExposureBlend=1.0f;
    CabinLight->SetLightColor(FLinearColor(0.68f,0.85f,1.0f));
    CabinLight->SetAttenuationRadius(500);
    CabinLight->SetCastShadows(false);
    CabinLight->SetLightingChannels(false,true,false);
    CabinLight->RegisterComponent();
    // Distributed cockpit task lights. Channel 1 prevents the simplified
    // cabin lights from leaking through the hull onto exterior surfaces.
    for(int32 Side:{-1,1})
    {
        auto* TaskLight=NewObject<UPointLightComponent>(this,FName(*FString::Printf(TEXT("CabinTaskLight%d"),Side)));
        AddInstanceComponent(TaskLight);TaskLight->SetupAttachment(VisualRoot);
        TaskLight->SetRelativeLocation(FVector(660,Side*80,175));
        TaskLight->SetIntensityUnits(ELightUnits::Lumens);TaskLight->SetIntensity(15*CabinScale);
        TaskLight->InverseExposureBlend=1.0f;
        TaskLight->SetLightColor(FLinearColor(1.0f,0.94f,0.84f));
        TaskLight->SetAttenuationRadius(240);TaskLight->SetSourceRadius(18);TaskLight->SetSourceLength(65);
        TaskLight->SetCastShadows(false);TaskLight->SetLightingChannels(false,true,false);TaskLight->RegisterComponent();
    }
    if(!LoadPhotoPlumes()) return false;
    return Meshes.Num()>0;
}

bool AStarShipPawn::LoadPhotoPlumes()
{
    const FString PhotoDirectory=FPaths::ProjectContentDir()/TEXT("Star/Art/EngineFXV3");
    FPhotoPlumeMeshData PlaneData,AxialData;
    FString MeshError;
    if(!LoadPhotoMeshData(PhotoDirectory/TEXT("engine_plume_mesh.json"),PlaneData,MeshError,true))
    {
        LoadError=TEXT("真空噴流の平面メッシュを読み込めません。");
        return false;
    }
    if(!LoadPhotoMeshData(PhotoDirectory/TEXT("engine_plume_disks.json"),AxialData,MeshError,false))
    {
        LoadError=TEXT("真空噴流の軸断面メッシュを読み込めません。");
        return false;
    }
    UTexture2D* PhotoTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Star/Art/PhotoPlumes/T_VacuumPlumePhoto.T_VacuumPlumePhoto"));
    UTexture2D* AxialTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Star/Art/PhotoPlumes/T_VacuumPlumeAxial.T_VacuumPlumeAxial"));
    UMaterialInterface* PlaneBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Star/Materials/M_Thruster.M_Thruster"));
    UMaterialInterface* AxialBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Star/Materials/M_ThrusterAxial.M_ThrusterAxial"));
    if(!PhotoTexture||!AxialTexture||!PlaneBase)
    {
        LoadError=TEXT("真空噴流の写真または M_Thruster マテリアルを読み込めません。");
        return false;
    }
    // Axial is a separate L2 graph when present. Keep a safe local fallback so
    // a branch containing only the shared plane material remains inspectable.
    if(!AxialBase) AxialBase=PlaneBase;

    auto FindMesh=[this](const FString& Name)->UStaticMeshComponent*
    {
        for(const auto& Candidate:Meshes) if(Candidate&&Candidate->GetName()==Name) return Candidate;
        return nullptr;
    };
    TArray<FPhotoNozzleSpec> Specs;
    Specs.Reserve(12);
    for(const FString& Name:{TEXT("SM_EnginePort"),TEXT("SM_EngineStarboard")})
    {
        UStaticMeshComponent* Engine=FindMesh(Name);
        if(!Engine||!Engine->GetStaticMesh())
        {
            LoadError=FString::Printf(TEXT("主エンジンの実ノズル形状がありません: %s"),*Name);
            return false;
        }
        const FBoxSphereBounds Bounds=Engine->GetStaticMesh()->GetBounds();
        const FTransform Relative=Engine->GetRelativeTransform();
        // The V2 engine asset is exported with its pivot retained at the
        // structural hinge. The aft nozzle is the geometry-derived minimum-X
        // bound; do not place a plume at the actor/component pivot.
        const FVector LocalExit(Bounds.Origin.X-Bounds.BoxExtent.X,Bounds.Origin.Y,Bounds.Origin.Z);
        const FVector Position=Relative.TransformPosition(LocalExit);
        const FVector Axis=Relative.TransformVectorNoScale(FVector(-1,0,0)).GetSafeNormal();
        const float DiameterCm=2.0f*FMath::Min(FMath::Abs(Bounds.BoxExtent.Y),FMath::Abs(Bounds.BoxExtent.Z));
        if(Position.ContainsNaN()||Axis.ContainsNaN()||Axis.IsNearlyZero()||!FMath::IsFinite(DiameterCm)||DiameterCm<1.0f)
        {
            LoadError=FString::Printf(TEXT("主エンジンのノズル bounds が不正です: %s"),*Name);
            return false;
        }
        FPhotoNozzleSpec& Spec=Specs.AddDefaulted_GetRef();
        Spec.Name=Name;
        Spec.PositionCm=Position;
        Spec.ExhaustAxis=Axis;
        Spec.DiameterCm=DiameterCm;
        Spec.LengthScale=0.65f; // Visible 3.9D tail; preserve bright near-nozzle photo signal.
        Spec.Radiance=40.0f;
        Spec.bMain=true;
    }

    // Cross-check the assembled component pivots against the V2 manifest when
    // the source manifest is available. This catches stale V1 locations while
    // leaving packaged cooking free to omit the authoring-only Art directory.
    TSharedPtr<FJsonObject> Manifest;
    if(LoadJsonFile(FPaths::ProjectContentDir()/TEXT("Star/Data/ship_nozzles.json"),Manifest))
    {
        const TArray<TSharedPtr<FJsonValue>>* ModelParts=nullptr;
        if(Manifest->TryGetArrayField(TEXT("model_parts"),ModelParts))
        {
            for(const FString Name:{TEXT("SM_EnginePort"),TEXT("SM_EngineStarboard")})
            {
                UStaticMeshComponent* Engine=FindMesh(Name);
                for(const auto& Value:*ModelParts)
                {
                    const auto Part=Value->AsObject();
                    FString PartName;
                    if(Part.IsValid()&&Part->TryGetStringField(TEXT("name"),PartName)&&PartName==Name)
                    {
                        const FVector Expected=JsonVector(Part,TEXT("location_m"))*100.0f;
                        if(!Engine||!Expected.Equals(Engine->GetRelativeLocation(),2.0f))
                        {
                            LoadError=FString::Printf(TEXT("V2エンジン pivot が manifest と一致しません: %s"),*Name);
                            return false;
                        }
                        break;
                    }
                }
            }
        }

        const TArray<TSharedPtr<FJsonValue>>* Rcs=nullptr;
        if(Manifest->TryGetArrayField(TEXT("rcs"),Rcs))
        {
            // Ten of the sixteen authored RCS nozzles cover all translation
            // signs and torque axes. Together with the two main engines this
            // stays inside the twelve-emitter budget.
            for(const auto& Value:*Rcs)
            {
                if(Specs.Num()>=12) break;
                const auto Entry=Value->AsObject();
                if(!Entry.IsValid()) continue;
                FString Name;
                if(!Entry->TryGetStringField(TEXT("name"),Name)) continue;
                const bool bDiagonal=Name.Contains(TEXT("ForePort"))||Name.Contains(TEXT("AftStarboard"));
                const bool bSelected=Name.EndsWith(TEXT("_0"))||
                    (bDiagonal&&(Name.EndsWith(TEXT("_1"))||Name.EndsWith(TEXT("_3"))))||
                    (!bDiagonal&&Name.EndsWith(TEXT("_2")));
                if(!bSelected) continue;
                const FString ClusterName=Name.LeftChop(2);
                if(!FindMesh(ClusterName)) continue;
                const FVector Position=JsonVector(Entry,TEXT("position_m"))*100.0f;
                const FVector Axis=JsonVector(Entry,TEXT("exhaust_axis")).GetSafeNormal();
                if(Position.ContainsNaN()||Axis.ContainsNaN()||Axis.IsNearlyZero()) continue;
                FPhotoNozzleSpec& Spec=Specs.AddDefaulted_GetRef();
                Spec.Name=Name;
                Spec.PositionCm=Position;
                Spec.ExhaustAxis=Axis;
                // V2 source geometry authors the RCS bell at radius 0.108m.
                Spec.DiameterCm=21.6f;
                Spec.LengthScale=0.5f; // 3D from the checked-in 6D photo mesh.
                Spec.Radiance=8.0f;
                Spec.bMain=false;
            }
        }
    }

    auto ConfigureMaterial=[&](UMaterialInstanceDynamic* Material,UTexture2D* Photo,float Radiance,float LayerEnergy,bool bAxial)
    {
        if(!Material) return;
        Material->SetTextureParameterValue(TEXT("PlumePhoto"),bAxial?AxialTexture:Photo);
        Material->SetTextureParameterValue(TEXT("PlumeAxial"),AxialTexture);
        Material->SetVectorParameterValue(TEXT("ThrusterColor"),FLinearColor(0.25f,0.65f,1.0f,1.0f));
        Material->SetScalarParameterValue(TEXT("ThrusterRadiance"),Radiance);
        Material->SetScalarParameterValue(TEXT("LayerEnergy"),LayerEnergy);
        Material->SetScalarParameterValue(TEXT("Thrust"),0.0f);
        Material->SetScalarParameterValue(TEXT("PulsePhase"),0.0f);
        Material->SetScalarParameterValue(TEXT("ViewAngleWeight"),1.0f);
        Material->SetScalarParameterValue(TEXT("DisplayRadiance"),Radiance>10?6.0f:1.2f);
        Material->SetScalarParameterValue(TEXT("FictionalTint"),0.75f);
    };
    auto CreateSection=[this](const FString& Name,const FPhotoPlumeMeshData& Data,const TArray<int32>& Indices,
                              UMaterialInstanceDynamic* Material,const FVector& Position,const FVector& Axis,
                              float DiameterCm,float LengthScale)->UProceduralMeshComponent*
    {
        if(!Material||Data.Vertices.IsEmpty()||Indices.IsEmpty()) return nullptr;
        auto* Component=NewObject<UProceduralMeshComponent>(this,FName(*Name));
        AddInstanceComponent(Component);
        Component->SetupAttachment(VisualRoot);
        Component->SetRelativeLocation(Position);
        Component->SetRelativeRotation(FRotationMatrix::MakeFromX(Axis).ToQuat());
        Component->SetRelativeScale3D(FVector(DiameterCm*LengthScale,DiameterCm,DiameterCm));
        TArray<FVector> Normals;
        Normals.Init(FVector::UpVector,Data.Vertices.Num());
        TArray<FProcMeshTangent> Tangents;
        Tangents.Reserve(Data.Vertices.Num());
        for(int32 Index=0;Index<Data.Vertices.Num();++Index) Tangents.Add(FProcMeshTangent(FVector::ForwardVector,false));
        Component->CreateMeshSection_LinearColor(0,Data.Vertices,Indices,Normals,Data.UVs,TArray<FLinearColor>(),Tangents,false);
        Component->SetMaterial(0,Material);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCastShadow(false);
        // Nearby exhaust is in front of the planetary atmosphere composite.
        Component->SetTranslucentSortPriority(20);
        Component->SetComponentTickEnabled(false);
        Component->SetVisibility(false);
        Component->RegisterComponent();
        return Component;
    };

    PhotoPlumes.Reset();
    for(const FPhotoNozzleSpec& Spec:Specs)
    {
        FStarPhotoPlumeRuntime& Emitter=PhotoPlumes.AddDefaulted_GetRef();
        Emitter.LocalPositionCm=Spec.PositionCm;
        Emitter.ExhaustAxis=Spec.ExhaustAxis;
        Emitter.DiameterCm=Spec.DiameterCm;
        Emitter.LengthScale=Spec.LengthScale;
        Emitter.Radiance=Spec.Radiance;
        Emitter.bMain=Spec.bMain;
        const FString Prefix=FString::Printf(TEXT("PhotoPlume_%s"),*Spec.Name);
        Emitter.PlaneMaterial=UMaterialInstanceDynamic::Create(PlaneBase,this);
        Emitter.AxialMaterial=UMaterialInstanceDynamic::Create(AxialBase,this);
        ConfigureMaterial(Emitter.PlaneMaterial,PhotoTexture,Spec.Radiance,0.166667f,false);
        ConfigureMaterial(Emitter.AxialMaterial,AxialTexture,Spec.Radiance,0.125f,true);
        Emitter.PlaneMesh=CreateSection(Prefix+TEXT("_Planes"),PlaneData,PlaneData.Triangles,Emitter.PlaneMaterial,
                                         Spec.PositionCm,Spec.ExhaustAxis,Spec.DiameterCm,Spec.LengthScale);
        Emitter.PlaneLodMesh=CreateSection(Prefix+TEXT("_PlanesLOD"),PlaneData,
                                           PlaneData.LowLodTriangles.IsEmpty()?PlaneData.Triangles:PlaneData.LowLodTriangles,
                                           Emitter.PlaneMaterial,Spec.PositionCm,Spec.ExhaustAxis,Spec.DiameterCm,Spec.LengthScale);
        Emitter.AxialMesh=CreateSection(Prefix+TEXT("_AxialDisks"),AxialData,AxialData.Triangles,Emitter.AxialMaterial,
                                        Spec.PositionCm,Spec.ExhaustAxis,Spec.DiameterCm,Spec.LengthScale);
        if(!Emitter.PlaneMesh||!Emitter.PlaneLodMesh||!Emitter.AxialMesh)
        {
            LoadError=FString::Printf(TEXT("写真噴流コンポーネントを生成できません: %s"),*Spec.Name);
            PhotoPlumes.Reset();
            return false;
        }
        if(Spec.bMain)
        {
            // Provide a controlled illumination source for the translucent exhaust.
            // Tie a bounded fictional exhaust light to the same nozzle and thrust.
            auto* Light=NewObject<UPointLightComponent>(this,FName(*(Prefix+TEXT("_HullLight"))));
            AddInstanceComponent(Light);
            Light->SetupAttachment(Emitter.PlaneMesh);
            Light->SetAbsolute(false,false,true);
            Light->SetRelativeLocation(FVector(0.25f/FMath::Max(Spec.LengthScale,0.001f),0,0));
            Light->SetIntensityUnits(ELightUnits::Lumens);
            Light->SetIntensity(0);
            Light->SetLightColor(FLinearColor(0.25f,0.65f,1.0f));
            Light->SetAttenuationRadius(Spec.DiameterCm*6.0f);
            Light->SetSourceRadius(Spec.DiameterCm*0.2f);
            Light->SetCastShadows(false);
            Light->SetLightingChannels(false,false,true);
            Light->RegisterComponent();
        }
    }
    return PhotoPlumes.Num()>=2;
}

void AStarShipPawn::UpdatePhotoPlumes(double Dt,bool bPaused)
{
    (void)Dt;
    if(!Flight||PhotoPlumes.IsEmpty()) return;
    const star::PropulsionTelemetry& Propulsion=Flight->Propulsion();
    const star::FlightState& State=Flight->State();
    float Spool=static_cast<float>(Propulsion.engineOutput);
    if(ShipAudio)
    {
        const float AudioSpool=ShipAudio->GetEngineSpool();
        // GetEngineSpool is the audio de-zippered value. During the first
        // frame before its router has advanced, retain the simulation output
        // rather than introducing a visible startup zero.
        Spool=AudioSpool>0.0001f||Propulsion.engineOutput<=0.0001?AudioSpool:Spool;
    }
    Spool=bPaused?0.0f:FMath::Clamp(Spool,0.0f,1.0f);
    // FlightSimulation also reports RCS/assist effort in engineOutput. Main
    // exhaust is intentionally gated by an actual forward/cruise command so
    // an attitude pulse cannot turn into two permanent main plumes.
    const bool bMainCommanded=!bPaused&&Propulsion.braking<0.02&&(State.throttle>0.02||Propulsion.cruiseCharge>0.02);
    const float MainThrust=bMainCommanded?Spool:0.0f;
    const FVector CameraLocation=FlightCamera?FlightCamera->GetComponentLocation():GetActorLocation();
    const float HalfFovTan=FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FlightCamera?FlightCamera->FieldOfView:100.0f,1.0f,179.0f)*0.5f));
    int32 ViewportWidth=1920;
    if(GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport)
        ViewportWidth=FMath::Max(1,GEngine->GameViewport->Viewport->GetSizeXY().X);
    for(FStarPhotoPlumeRuntime& Emitter:PhotoPlumes)
    {
        if(!Emitter.PlaneMesh||!Emitter.PlaneLodMesh||!Emitter.AxialMesh) continue;
        const FVector EmitterWorld=Emitter.PlaneMesh->GetComponentLocation();
        const FVector WorldAxis=Emitter.PlaneMesh->GetComponentTransform().TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
        const FVector ToCamera=(CameraLocation-EmitterWorld).GetSafeNormal();
        const float AxialAlignment=FMath::Abs(FVector::DotProduct(WorldAxis,ToCamera));
        // At an 80-degree rear cone the crossed planes become edge-on and
        // produce radial streaks. Fade those planes into the axial disks.
        constexpr float SideBlendCos=0.34202014f,RearBlendCos=0.93969262f;
        const float PlaneWeight=1.0f-Smooth01((AxialAlignment-SideBlendCos)/(RearBlendCos-SideBlendCos));
        const float DiskWeight=1.0f-PlaneWeight;
        float Thrust=Emitter.bMain?MainThrust:0.0f;
        if(!Emitter.bMain&&!bPaused&&RcsPulseRemaining>0.0f)
        {
            const FVector Axis=Emitter.ExhaustAxis.GetSafeNormal();
            const FVector Force=-Axis;
            const FVector Torque=FVector::CrossProduct(Emitter.LocalPositionCm,Force).GetSafeNormal();
            // Only nozzles whose force or torque assists the signed command
            // fire. Opposing jets are not illuminated merely by absolute input.
            const float PulseDemand=FMath::Max(0.0f,static_cast<float>(FMath::Max(
                FVector::DotProduct(Force,RcsTranslationCommand),FVector::DotProduct(Torque,RcsInputCommand))));
            const float PulseFade=FMath::Clamp(RcsPulseRemaining/0.20f,0.0f,1.0f);
            Thrust=FMath::Clamp(PulseDemand*PulseFade,0.0f,1.0f);
        }
        const FVector WorldScale=Emitter.PlaneMesh->GetComponentScale();
        const float MaxScale=FMath::Max(FMath::Abs(WorldScale.X),FMath::Max(FMath::Abs(WorldScale.Y),FMath::Abs(WorldScale.Z)));
        // Component scale already contains the nozzle diameter in centimetres.
        const float WorldDiameter=FMath::Max(MaxScale,0.001f);
        const float Distance=FMath::Max((CameraLocation-EmitterWorld).Size(),1.0f);
        const float ProjectedPixels=WorldDiameter/Distance*static_cast<float>(ViewportWidth)/(2.0f*FMath::Max(HalfFovTan,0.001f));
        const bool bSubpixelHidden=ProjectedPixels<1.0f;
        const bool bLowLod=ProjectedPixels<4.0f;
        const bool bSignal=Thrust>0.008f&&!bSubpixelHidden;
        for(USceneComponent* Child:Emitter.PlaneMesh->GetAttachChildren())
            if(auto* Light=Cast<UPointLightComponent>(Child))
            {
                Light->SetIntensity(3000.0f*Thrust);
                Light->SetVisibility(bSignal);
            }
        const float PlaneThrust=Thrust;
        const float AxialThrust=Thrust;
        if(Emitter.PlaneMaterial)
        {
            Emitter.PlaneMaterial->SetScalarParameterValue(TEXT("Thrust"),PlaneThrust);
            Emitter.PlaneMaterial->SetScalarParameterValue(TEXT("LayerEnergy"),bLowLod?0.5f:0.166667f);
            Emitter.PlaneMaterial->SetScalarParameterValue(TEXT("PulsePhase"),PhotoPlumePhase);
            Emitter.PlaneMaterial->SetScalarParameterValue(TEXT("ViewAngleWeight"),PlaneWeight);
        }
        if(Emitter.AxialMaterial)
        {
            Emitter.AxialMaterial->SetScalarParameterValue(TEXT("Thrust"),AxialThrust);
            Emitter.AxialMaterial->SetScalarParameterValue(TEXT("LayerEnergy"),0.125f);
            Emitter.AxialMaterial->SetScalarParameterValue(TEXT("PulsePhase"),PhotoPlumePhase);
            Emitter.AxialMaterial->SetScalarParameterValue(TEXT("ViewAngleWeight"),DiskWeight);
        }
        Emitter.PlaneMesh->SetVisibility(bSignal&&PlaneWeight>0.01f&&!bLowLod);
        Emitter.PlaneLodMesh->SetVisibility(bSignal&&PlaneWeight>0.01f&&bLowLod);
        Emitter.AxialMesh->SetVisibility(bSignal&&DiskWeight>0.01f);
    }
}

void AStarShipPawn::CreateInstrument(const FString& Name,const FVector& Position,const FVector2D& Size,FName Mode)
{
    auto* Display=NewObject<UWidgetComponent>(this,FName(*Name));
    AddInstanceComponent(Display);
    Display->SetupAttachment(VisualRoot);
    Display->SetWidgetSpace(EWidgetSpace::World);
    Display->SetWidgetClass(UStarInstrumentWidget::StaticClass());
    Display->SetDrawSize(FVector2D(1024,512));
    // World widgets otherwise emit unit radiance and disappear against the
    // physically lit exterior. HDR tint gives the displays daylight luminance.
    Display->SetTintColorAndOpacity(FLinearColor(4000,4000,4000,1));
    Display->SetRelativeLocation(Position);
    Display->SetRelativeRotation(FRotator(0,180,0));
    Display->SetRelativeScale3D(FVector(1,Size.X*100/1024,Size.Y*100/512));
    Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Display->SetTwoSided(false);
    Display->SetTickWhenOffscreen(false);
    Display->SetRedrawTime(1.0f/20.0f);
    Display->RegisterComponent();
    Display->InitWidget();
    if(auto* Widget=Cast<UStarInstrumentWidget>(Display->GetUserWidgetObject())) Widget->SetDisplayMode(Mode);
    Instruments.Add(Display);
}
void AStarShipPawn::AdvanceFlight(double Dt,const star::FlightInput& Input)
{
    if(!Flight||!WorldDirector) return;
    if(bEarthView) { UpdateEarthView(Dt); return; }
    const float SafeDt=FMath::Clamp(static_cast<float>(Dt),0.0f,0.25f);
    const double BeforeTime=Flight->State().simulationTimeSeconds;
    if(Input.paused)
    {
        PhotoPlumePhase=0.0f;
        RcsInputCommand=FVector::ZeroVector;
        RcsTranslationCommand=FVector::ZeroVector;
        bPendingRcsSound=false;
        RcsPulseRemaining=0.0f;
        RcsPulseDemand=0.0f;
    }
    else
    {
        PhotoPlumePhase=FMath::Fmod(PhotoPlumePhase+SafeDt*10.0f,2.0f*static_cast<float>(star::Pi));
        RcsPulseRemaining=FMath::Max(0.0f,RcsPulseRemaining-SafeDt);
        // Angular velocity is an axial vector: under the simulation-to-UE
        // reflection its sign conversion differs from a position vector.
        const FVector TorqueCommand(-Input.roll,-Input.pitch,Input.yaw);
        FVector ForceCommand(0,Input.strafeRight,Input.strafeUp);
        if(Input.brake)
        {
            const auto LocalVelocity=Flight->State().orientation.Conjugate().Rotate(Flight->State().velocityMetersPerSecond);
            ForceCommand-=FStarDataCatalog::UEVector(star::SimulationDirectionToUnreal(LocalVelocity.Normalized()));
        }
        const float Demand=static_cast<float>(FMath::Max(TorqueCommand.GetAbsMax(),ForceCommand.GetAbsMax()));
        if(Demand<=0.04f&&bPendingRcsSound&&BeforeTime<=RcsSoundRequestTime)
        {
            // A tap released before any fixed step never applied a command.
            bPendingRcsSound=false;
            RcsPulseRemaining=0.0f;RcsPulseDemand=0.0f;
        }
        if(Demand>0.04f&&Flight->State().mode!=star::FlightMode::Landed)
        {
            if(RcsPulseRemaining<=0.0f)
            {
                bPendingRcsSound=true;
                RcsSoundRequestTime=BeforeTime;
            }
            RcsInputCommand=TorqueCommand;
            RcsTranslationCommand=ForceCommand;
            RcsPulseDemand=Demand;
            RcsPulseRemaining=FMath::Max(RcsPulseRemaining,0.08f+0.12f*Demand);
        }
        else if(RcsPulseRemaining<=0.0f) RcsPulseDemand=0.0f;
    }
    const auto Result=Flight->Advance(Dt,Input);
    if(Flight->State().mode==star::FlightMode::Landed)
    {
        RcsPulseRemaining=0.0f;
        RcsPulseDemand=0.0f;
        bPendingRcsSound=false;
    }
    if(Result.contact.kind==star::ContactKind::Landed) PlaySoundEvent(TEXT("Landing"));
    else if(Result.contact.kind==star::ContactKind::Recovered) PlaySoundEvent(TEXT("Warning"));
    const auto Distance=Flight->State().positionMeters-RenderOrigin;
    if(Distance.Length()>5000.0) RenderOrigin=Flight->State().positionMeters;
    RefreshTransform(Input.paused?0:Dt);
    WorldDirector->UpdateScene(Flight->State(),RenderOrigin,CameraAbsoluteMeters(),Input.paused?0:Dt,GetController()!=nullptr);
}
void AStarShipPawn::RefreshTransform(double Dt)
{
    if(!Flight) return;
    const auto& State=Flight->State();
    SetActorLocationAndRotation(FStarDataCatalog::UEVector(star::ToUnrealCentimeters(State.positionMeters,RenderOrigin)),FStarDataCatalog::UERotation(State.orientation),false,nullptr,ETeleportType::TeleportPhysics);
    const float TargetGear=bDesiredGearDeployed?1.0f:0.0f;
    GearAlpha=FMath::FInterpConstantTo(GearAlpha,TargetGear,static_cast<float>(Dt),0.42f);
    Flight->SetGearDeployed(bDesiredGearDeployed&&GearAlpha>=0.999f);
    for(const auto& Pair:GearPivots)
    {
        const float Direction=Pair.Key.Contains(TEXT("Port"))?-1.0f:1.0f;
        Pair.Value->SetRelativeRotation(FRotator((1-GearAlpha)*75,0,(1-GearAlpha)*Direction*12));
    }
    if(bGuideLookActive) GuideLookRotation=(GetActorQuat().Inverse()*GuideWorldRotation).Rotator();
    const FRotator Look=bGuideLookActive?GuideLookRotation:FRotator(LookPitch,LookYaw,0);
    FVector Socket=bCockpitView?CockpitSocketCm:Look.RotateVector(ChaseSocketCm);
    if(bEarthScenicFlightView&&!bCockpitView&&!bGuideCameraOffsetActive&&WorldDirector&&Flight)
    {
        const auto* Earth=WorldDirector->Catalog().Find(TEXT("earth"));
        if(Earth) Socket*=(Flight->State().positionMeters-Earth->Definition.centerMeters).Length()<Earth->Definition.radiusMeters+500000.0?1.8f:3.0f;
    }
    if(!bCockpitView&&bGuideCameraOffsetActive)
        Socket=GetActorTransform().InverseTransformVectorNoScale(FStarDataCatalog::UEVector(star::SimulationDirectionToUnreal(GuideCameraOffsetMeters)*100.0));
    Socket+=PhotoOffsetCm;
    if((!bCockpitView || !PhotoOffsetCm.IsNearlyZero()) && WorldDirector)
    {
        const auto Desired=star::FromUnrealCentimeters(FStarDataCatalog::SimVector(GetActorTransform().TransformPosition(Socket)),RenderOrigin);
        const auto Safe=WorldDirector->SafeCameraPosition(Desired);
        Socket=GetActorTransform().InverseTransformPosition(FStarDataCatalog::UEVector(star::ToUnrealCentimeters(Safe,RenderOrigin)));
    }
    if(bVRRequested)
    {
        TrackingOrigin->SetRelativeLocation(Socket);
        // Seated head tracking owns the camera transform. Never overwrite its
        // physical translation with a cinematic or mouse-look animation.
        TrackingOrigin->SetRelativeRotation(bCockpitView?FRotator::ZeroRotator:(FVector(200,0,50)-Socket).Rotation());
    }
    else
    {
        FlightCamera->SetRelativeLocation(Socket);
        FlightCamera->SetRelativeRotation(bCockpitView||bGuideLookActive?Look:(FVector(200,0,50)-Socket).Rotation());
    }
    FlightCamera->SetFieldOfView(bCockpitView?100.0f:75.0f);
}
void AStarShipPawn::LookAtAbsolute(const star::Vec3d& Target,double Dt)
{
    if(!Flight||!FlightCamera||!Target.IsFinite()||bGuidedCameraUserOverride) return;
    const star::Vec3d ToTarget=Target-CameraAbsoluteMeters();
    if(!ToTarget.IsFinite()||ToTarget.LengthSquared()<1.0e-12) return;
    const FVector WorldDirection=FStarDataCatalog::UEVector(star::SimulationDirectionToUnreal(ToTarget.Normalized()));
    const FVector LocalDirection=GetActorTransform().InverseTransformVectorNoScale(WorldDirection).GetSafeNormal();
    if(LocalDirection.IsNearlyZero()) return;
    if(!bGuideLookActive)
    {
        GuideLookRotation=bCockpitView?FRotator(LookPitch,LookYaw,0):FlightCamera->GetRelativeRotation();
        GuideWorldRotation=FlightCamera->GetComponentQuat();
        bGuideLookActive=true;
    }
    // Parallel transport alone can carry an inverted roll from Earth to Moon.
    // Near a body the visible horizon defines up; approach it with a bounded
    // camera rotation, independently of the ship attitude and simulation.
    FVector PreferredUp=GuideWorldRotation.GetAxisZ();
    double ClosestRatio=3.0;
    if(WorldDirector)for(const auto& Body:WorldDirector->Catalog().Bodies())
    {
        if(Body.Definition.id=="sun")continue;
        const auto Offset=Flight->State().positionMeters-Body.Definition.centerMeters;
        const double Ratio=Offset.Length()/Body.Definition.radiusMeters;
        if(Ratio<ClosestRatio)
        {
            ClosestRatio=Ratio;
            PreferredUp=FStarDataCatalog::UEVector(star::SimulationDirectionToUnreal(Offset.Normalized()));
        }
    }
    // Looking almost along radial up/down has no unique horizon. Transport
    // the previous up direction rather than allowing MakeFromXZ to flip axes.
    if(FMath::Abs(FVector::DotProduct(WorldDirection,PreferredUp))>0.95f)
    {
        PreferredUp=GuideWorldRotation.GetAxisZ()-WorldDirection*FVector::DotProduct(WorldDirection,GuideWorldRotation.GetAxisZ());
        if(PreferredUp.IsNearlyZero())PreferredUp=GuideWorldRotation.GetAxisY();
    }
    const FQuat DesiredWorld=FRotationMatrix::MakeFromXZ(WorldDirection,PreferredUp.GetSafeNormal()).ToQuat();
    double Blend=1.0-FMath::Exp(-FMath::Clamp(Dt,0.0,0.25)/0.65);
    const double Angle=GuideWorldRotation.AngularDistance(DesiredWorld);
    if(Angle>1e-6)Blend=FMath::Min(Blend,FMath::DegreesToRadians(18.0)*FMath::Clamp(Dt,0.0,0.25)/Angle);
    GuideWorldRotation=FQuat::Slerp(GuideWorldRotation,DesiredWorld,Blend).GetNormalized();
    RefreshTransform(0);
}
void AStarShipPawn::SetGuidedCameraOffset(const star::Vec3d& Offset,double Dt)
{
    if(!Flight||bCockpitView||bGuidedCameraUserOverride||!Offset.IsFinite())return;
    if(!bGuideCameraOffsetActive)
    {GuideCameraOffsetMeters=CameraAbsoluteMeters()-Flight->State().positionMeters;bGuideCameraOffsetActive=true;}
    const double Step=FMath::Clamp(Dt,0.0,0.25);
    auto Delta=(Offset-GuideCameraOffsetMeters)*(1.0-FMath::Exp(-Step/0.9));
    const double Length=Delta.Length();
    if(Length>12.0*Step&&Length>0)Delta=Delta*(12.0*Step/Length);
    GuideCameraOffsetMeters+=Delta;
    RefreshTransform(0);
}
void AStarShipPawn::ClearGuidedCamera()
{bGuideCameraOffsetActive=false;bGuidedCameraUserOverride=false;bGuideLookActive=false;}
void AStarShipPawn::SetLook(float YawDelta,float PitchDelta,bool bPhoto)
{
    if(!bCockpitView&&bGuideCameraOffsetActive)
    {
        if(FMath::Abs(YawDelta)>KINDA_SMALL_NUMBER||FMath::Abs(PitchDelta)>KINDA_SMALL_NUMBER)
        {
            // Keep the current world-space viewpoint, then turn the real
            // camera locally. An orbit/socket reset here would jump on handoff.
            GuideWorldRotation=(FlightCamera->GetComponentQuat()*FRotator(PitchDelta,YawDelta,0).Quaternion()).GetNormalized();
            bGuidedCameraUserOverride=true;bGuideLookActive=true;
        }
        RefreshTransform(0);return;
    }
    // A zero-delta tick is the normal controller heartbeat; it must not
    // cancel a tour's smooth absolute look. Real manual input takes control.
    if(FMath::Abs(YawDelta)>KINDA_SMALL_NUMBER||FMath::Abs(PitchDelta)>KINDA_SMALL_NUMBER)
    {
        bGuidedCameraUserOverride=true;
        if(bGuideLookActive)
        {
            LookYaw=(!bCockpitView||bPhoto)?FRotator::NormalizeAxis(GuideLookRotation.Yaw):FMath::Clamp(FRotator::NormalizeAxis(GuideLookRotation.Yaw),-120.0f,120.0f);
            LookPitch=FMath::Clamp(GuideLookRotation.Pitch,bCockpitView?-70.0f:-89.0f,bCockpitView?75.0f:89.0f);
        }
        bGuideLookActive=false;
    }
    LookYaw=(!bCockpitView||bPhoto)?FRotator::NormalizeAxis(LookYaw+YawDelta):FMath::Clamp(LookYaw+YawDelta,-120.0f,120.0f);
    LookPitch=FMath::Clamp(LookPitch+PitchDelta,bCockpitView?-70.0f:-89.0f,bCockpitView?75.0f:89.0f);
    RefreshTransform(0);
}
void AStarShipPawn::RecenterLook()
{
    bGuidedCameraUserOverride=false;LookYaw=LookPitch=0;
    if(bGuideCameraOffsetActive)
    {
        // Home resumes the bounded automatic interpolation from this pose.
        GuideWorldRotation=FlightCamera->GetComponentQuat();bGuideLookActive=true;
    }
    else {bGuideLookActive=false;GuideLookRotation=FRotator::ZeroRotator;}
    RefreshTransform(0);
}
void AStarShipPawn::ToggleView() { bCockpitView=!bCockpitView;ClearGuidedCamera();RecenterLook();bGuidedCameraUserOverride=true;PlaySoundEvent(TEXT("View")); }
void AStarShipPawn::BeginEarthScenicFlightView()
{
    if(bVRRequested){if(!bCockpitView)ToggleView();return;}
    bEarthScenicFlightView=true;
    if(bCockpitView) ToggleView();
    bool bNight=false;
    if(WorldDirector&&Flight)
    {
        const auto* Earth=WorldDirector->Catalog().Find(TEXT("earth"));
        const auto* Sun=WorldDirector->Catalog().Find(TEXT("sun"));
        if(Earth&&Sun)
            bNight=star::Vec3d::Dot((Flight->State().positionMeters-Earth->Definition.centerMeters).Normalized(),
                (Sun->Definition.centerMeters-Flight->State().positionMeters).Normalized())<-0.16;
    }
    RecenterLook();
    const auto LocalForward=Flight->State().orientation.Conjugate().Rotate(CameraForwardSimulation());
    const float NeutralPitch=static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(LocalForward.z,FMath::Sqrt(LocalForward.x*LocalForward.x+LocalForward.y*LocalForward.y))));
    bool bCloseNight=false;
    if(bNight&&WorldDirector) if(const auto* Earth=WorldDirector->Catalog().Find(TEXT("earth")))
        bCloseNight=(Flight->State().positionMeters-Earth->Definition.centerMeters).Length()<Earth->Definition.radiusMeters+80000.0;
    SetLook(bCloseNight?-10.0f:0.0f,(bNight?(bCloseNight?-78.0f:-60.0f):-34.0f)-NeutralPitch,false);
}
bool AStarShipPawn::ToggleLandingGear()
{
    if(!Flight||Flight->State().mode==star::FlightMode::Landed) return false;
    bDesiredGearDeployed=!bDesiredGearDeployed;
    if(!bDesiredGearDeployed) Flight->SetGearDeployed(false);
    else Flight->SetMode(star::FlightMode::Landing);
    PlaySoundEvent(bDesiredGearDeployed?TEXT("GearDeploy"):TEXT("GearStow"));
    return true;
}
void AStarShipPawn::SetPhotoOffset(const FVector& Delta) { PhotoOffsetCm+=Delta;PhotoOffsetCm=PhotoOffsetCm.GetClampedToMaxSize(20000);RefreshTransform(0); }
void AStarShipPawn::ClearPhotoOffset() { PhotoOffsetCm=FVector::ZeroVector; }
void AStarShipPawn::UpdatePresentation(const FStarHUDSnapshot& Snapshot,bool bPaused,float Volume)
{
    for(const auto& Display:Instruments) if(auto* Widget=Cast<UStarInstrumentWidget>(Display->GetUserWidgetObject())) Widget->ApplySnapshot(Snapshot);
    if(ShipAudio)
    {
        ShipAudio->SetMasterVolume(Volume);
        const auto& Propulsion=Flight->Propulsion();
        ShipAudio->SetFlightParameters(static_cast<float>(Propulsion.engineOutput),Snapshot.SpeedMps,bCockpitView,bPaused);
        ShipAudio->SetEngineDynamics(static_cast<float>(Propulsion.cruiseCharge),static_cast<float>(Propulsion.braking));
        FString Environment=Snapshot.BodyId;
        const auto* Nearest=WorldDirector?WorldDirector->Catalog().Find(Environment):nullptr;
        if(!Nearest||Snapshot.AltitudeM>Nearest->Definition.radiusMeters*3) Environment=TEXT("space");
        ShipAudio->SetEnvironment(FName(*Environment),Snapshot.bCruise,Snapshot.bLanded);
    }
    UpdatePhotoPlumes(0.0,bPaused);
    // Keep a requested pulse across render frames that contain no fixed step.
    // Dispatch only after current pause/audio inputs and real flight state
    // have advanced; an input-only render tick cannot consume the sound.
    if(bPendingRcsSound&&!bPaused&&Flight->State().mode!=star::FlightMode::Landed&&
       Flight->State().simulationTimeSeconds>RcsSoundRequestTime)
    {
        PlaySoundEvent(TEXT("RCS"));
        bPendingRcsSound=false;
    }
}
void AStarShipPawn::PlaySoundEvent(FName Name) { if(ShipAudio) ShipAudio->PlayEvent(Name); }
void AStarShipPawn::SetEVAAudio(bool Enabled) { if(ShipAudio) ShipAudio->SetEVAMode(Enabled); }
void AStarShipPawn::SetInteriorMonitor(bool Enabled) { if(ShipAudio) ShipAudio->SetInteriorMonitorEnabled(Enabled); }
star::Vec3d AStarShipPawn::CameraAbsoluteMeters() const
{
    if(bEarthView) return EarthViewCamera;
    return star::FromUnrealCentimeters(FStarDataCatalog::SimVector(FlightCamera->GetComponentLocation()),RenderOrigin);
}
star::Vec3d AStarShipPawn::CameraForwardSimulation() const
{
    return star::UnrealDirectionToSimulation(FStarDataCatalog::SimVector(FlightCamera->GetForwardVector())).Normalized();
}
star::Vec3d AStarShipPawn::CameraRightSimulation() const
{
    return star::UnrealDirectionToSimulation(FStarDataCatalog::SimVector(FlightCamera->GetRightVector())).Normalized();
}
star::Vec3d AStarShipPawn::CameraUpSimulation() const
{
    return star::UnrealDirectionToSimulation(FStarDataCatalog::SimVector(FlightCamera->GetUpVector())).Normalized();
}
float AStarShipPawn::CameraHorizontalFOVDegrees() const { return FlightCamera->FieldOfView; }
bool AStarShipPawn::SetWorldUtc(double UnixSeconds)
{
    if(!IsReady()||!WorldDirector->Catalog().SupportsUtc(UnixSeconds)) return false;
    const auto* From=star::NearestReferenceBody(Flight->Bodies(),Flight->State());
    if(!From) return false;
    const auto Previous=*From;const double PreviousUtc=WorldDirector->WorldUtc();
    if(!WorldDirector->SetWorldUtc(UnixSeconds)) return false;
    const auto Bodies=WorldDirector->Catalog().SimulationBodies();
    if(!Flight->UpdateCelestialFrames(Bodies)) { WorldDirector->SetWorldUtc(PreviousUtc);return false; }
    const auto* To=Flight->FindBody(Previous.id);
    const auto Rotation=(To->bodyFixedToSimulation*Previous.bodyFixedToSimulation.Conjugate()).Normalized();
    RenderOrigin=To->centerMeters+Rotation.Rotate(RenderOrigin-Previous.centerMeters);
    GuideCameraOffsetMeters=Rotation.Rotate(GuideCameraOffsetMeters);
    GuideWorldRotation=FStarDataCatalog::UERotation(Rotation)*GuideWorldRotation;
    PhotoOffsetCm=FStarDataCatalog::UERotation(Rotation).RotateVector(PhotoOffsetCm);
    RefreshTransform(0);
    return true;
}
void AStarShipPawn::AdvanceWorldClock(double DeltaSeconds)
{
    if(!IsReady()||!FMath::IsFinite(DeltaSeconds)||DeltaSeconds<=0||WorldDirector->ClockRate()==0) return;
    if(!SetWorldUtc(WorldDirector->WorldUtc()+DeltaSeconds*WorldDirector->ClockRate())) WorldDirector->SetClockRate(0);
}
bool AStarShipPawn::RestoreFlight(const star::FlightState& State)
{
    if(!Flight||!Flight->RestoreState(State)) return false;
    bDesiredGearDeployed=State.gearDeployed;
    GearAlpha=State.gearDeployed?1.0f:0.0f;
    RenderOrigin=State.positionMeters;
    ClearGuidedCamera();
    ClearPhotoOffset();RecenterLook();RefreshTransform(0);
    WorldDirector->UpdateScene(State,RenderOrigin,CameraAbsoluteMeters(),0);
    return true;
}
void AStarShipPawn::CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult) { FlightCamera->GetCameraView(DeltaTime,OutResult); }

void AStarShipPawn::StartEarthView(bool bSunset, bool bOrbit)
{
    if(!IsReady()) return;
    bEarthView=true; bEarthSunset=bSunset; bEarthOrbit=bOrbit; EarthViewTime=0;
    WorldDirector->SetTwilightDream(!bOrbit,bSunset);
    VisualRoot->SetVisibility(false,true);
    UpdateEarthView(0);
}
void AStarShipPawn::EndEarthView()
{
    if(!bEarthView) return;
    bEarthView=false;
    WorldDirector->SetTwilightDream(false);
    VisualRoot->SetVisibility(true,true);
    RenderOrigin=Flight->State().positionMeters;
    RefreshTransform(0);
    WorldDirector->UpdateScene(Flight->State(),RenderOrigin,CameraAbsoluteMeters(),0);
}
void AStarShipPawn::UpdateEarthView(double Dt)
{
    const auto* Earth=WorldDirector->Catalog().Find(TEXT("earth"));
    const auto* Sun=WorldDirector->Catalog().Find(TEXT("sun"));
    if(!Earth||!Sun) { EndEarthView(); return; }
    EarthViewTime+=FMath::Clamp(Dt,0.0,0.1);
    double ViewSeconds=EarthViewTime;
    if(FParse::Param(FCommandLine::Get(),TEXT("StarBenchmark")))
        FParse::Value(FCommandLine::Get(),TEXT("StarEarthViewSeconds="),ViewSeconds);
    const double T=FMath::Clamp(ViewSeconds/120.0,0.0,1.0);
    const double Approach=T*T*(3.0-2.0*T);
    const double Altitude=bEarthOrbit?FMath::Exp(FMath::Lerp(FMath::Loge(Earth->Definition.radiusMeters*2.9),FMath::Loge(180000.0),Approach))
                                     :bEarthSunset?3000.0:450000.0;
    const double Dip=FMath::Acos(Earth->Definition.radiusMeters/(Earth->Definition.radiusMeters+Altitude));
    const double Elevation=-Dip+FMath::DegreesToRadians(bEarthSunset?FMath::Lerp(1.8,-0.4,T):FMath::Lerp(0.05,1.8,T));
    const auto Sunward=(Sun->Definition.centerMeters-Earth->Definition.centerMeters).Normalized();
    const auto Pole=Earth->Definition.bodyFixedToSimulation.Rotate({0,0,1});
    const auto Side=star::Vec3d::Cross(Pole,Sunward).Normalized()*((bEarthSunset||bEarthOrbit)?1.0:-1.0);
    const double OrbitPhase=FMath::DegreesToRadians(FMath::Lerp(35.0,33.4,Approach));
    const auto Up=bEarthOrbit?(Sunward*FMath::Cos(OrbitPhase)+Side*FMath::Sin(OrbitPhase)+Pole*0.12).Normalized()
                            :Side*FMath::Cos(Elevation)+Sunward*FMath::Sin(Elevation);
    const auto Tangent=(Sunward-Up*star::Vec3d::Dot(Sunward,Up)).Normalized();
    EarthViewCamera=Earth->Definition.centerMeters+Up*(Earth->Definition.radiusMeters+Altitude);
    RenderOrigin=EarthViewCamera;
    // The observation camera has its own nearby render origin. Flight/save state stays untouched.
    SetActorLocationAndRotation(FVector::ZeroVector,FQuat::Identity,false,nullptr,ETeleportType::TeleportPhysics);
    const double Pitch=-Dip+FMath::DegreesToRadians(bEarthSunset?7.0:3.0);
    const auto Forward=bEarthOrbit?-Up:Tangent*FMath::Cos(Pitch)+Up*FMath::Sin(Pitch);
    FlightCamera->SetWorldLocationAndRotation(FVector::ZeroVector,FStarDataCatalog::UERotation(star::Quatd::FromForwardUp(Forward,bEarthOrbit?Pole:Up)));
    FlightCamera->SetFieldOfView(bEarthSunset?55.0f:62.0f);
    auto ViewState=Flight->State(); ViewState.positionMeters=EarthViewCamera;
    WorldDirector->UpdateScene(ViewState,RenderOrigin,EarthViewCamera,Dt);
}
