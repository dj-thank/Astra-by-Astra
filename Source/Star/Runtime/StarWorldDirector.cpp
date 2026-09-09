#include "Runtime/StarWorldDirector.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "GameFramework/WorldSettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Terrain/StarLunarTerrainComponent.h"
#include "Terrain/StarEarthTerrainComponent.h"
#include "Simulation/ObservationGeometry.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Runtime/StarShipPawn.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"

namespace
{
constexpr double AU = 149597870700.0;
constexpr double MaximumProxyDistanceM = 500000000.0;
constexpr double LunarRadiusM = 1737400.0;
// The simulation and collision contract deliberately use Saturn's mean
// radius. Rendering may show the documented IAU/PDS equatorial and polar
// radii, but this is an explicitly labelled visual approximation rather than
// a silent physics rescale.
constexpr bool bSaturnOblateKnownApprox = true;
constexpr double SaturnEquatorialRadiusM = 60268000.0;
constexpr double SaturnPolarRadiusM = 54364000.0;
FLinearColor ColorVector(const star::Vec3d& V) { return FLinearColor(static_cast<float>(V.x),static_cast<float>(V.y),static_cast<float>(V.z),1); }
UMaterialInterface* Material(const TCHAR* Name) { return LoadObject<UMaterialInterface>(nullptr,Name); }
star::Vec3d BodyPoint(double Lat, double Lon)
{
    return {FMath::Cos(Lat)*FMath::Cos(Lon),FMath::Cos(Lat)*FMath::Sin(Lon),FMath::Sin(Lat)};
}
}

AStarWorldDirector::AStarWorldDirector()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CelestialScene"));
    SetRootComponent(SceneRoot);
    EarthTerrain=CreateDefaultSubobject<UStarEarthTerrainComponent>(TEXT("ObservedEarthTerrain"));
    EarthTerrain->SetupAttachment(SceneRoot);
    LunarTerrain=CreateDefaultSubobject<UStarLunarTerrainComponent>(TEXT("LunarTerrain"));
    LunarTerrain->SetupAttachment(SceneRoot);
}
void AStarWorldDirector::BeginPlay() { Super::BeginPlay(); Initialize(); }
void AStarWorldDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    if(LunarTerrain) LunarTerrain->Shutdown();
    if(EarthTerrain) EarthTerrain->Shutdown();
    if(bEnvironmentFenceIssued) EnvironmentFence.Wait();
    Super::EndPlay(Reason);
}
bool AStarWorldDirector::Initialize()
{
    if (bReady) return true;
    if (bInitializationAttempted) return false;
    bInitializationAttempted = true;
    bEarthLUTEnabled=!FParse::Param(FCommandLine::Get(),TEXT("StarLegacyEarthAtmosphere"));
    bEarthVolumeClouds=false; // No invented 3-D cloud density from a 2-D photograph.
    bEarthPacificEnabled=false; // Archived baked cloud/glint RGB is not live terrain.
    FParse::Value(FCommandLine::Get(),TEXT("StarWeatherTime="),EarthWeatherTimeOverride);
    // Missing content must not masquerade as a ready world or trigger a large
    // synchronous file read every frame. Reload the level after repairing assets.
    const TCHAR* RequiredMaterials[] = {
        TEXT("/Game/Star/Materials/MI_Earth.MI_Earth"),
        TEXT("/Game/Star/Materials/MI_Moon.MI_Moon"),
        TEXT("/Game/Star/Materials/MI_Saturn.MI_Saturn"),
        TEXT("/Game/Star/Materials/M_Sun.M_Sun"),
        TEXT("/Game/Star/Materials/M_Atmosphere.M_Atmosphere"),
        TEXT("/Game/Star/Materials/M_Clouds.M_Clouds"),
        TEXT("/Game/Star/Materials/M_CloudsSurface.M_CloudsSurface"),
        TEXT("/Game/Star/Materials/M_Rings.M_Rings"),
        TEXT("/Game/Star/Materials/M_Stars.M_Stars"),
        TEXT("/Game/Star/Materials/M_Surface.M_Surface")
        ,TEXT("/Game/Star/Materials/M_ObservedEarth.M_ObservedEarth")
    };
    for (const TCHAR* Path : RequiredMaterials)
    {
        if (!Material(Path))
        {
            LoadError = TEXT("景観データを読み込めません。ゲームのファイルを確認してください。");
            UE_LOG(LogTemp, Error, TEXT("STAR required material missing: %s"), Path);
            return false;
        }
    }
    for(const TCHAR* Path:{TEXT("/Game/Star/Art/EarthV2/T_EarthOpticalDepth.T_EarthOpticalDepth"),
        TEXT("/Game/Star/Art/EarthV2/T_CloudDensityAtlas.T_CloudDensityAtlas"),
        TEXT("/Game/Star/Art/EarthV2/T_Pacific_Aqua_20250906_8K.T_Pacific_Aqua_20250906_8K")})
    {
        if(!LoadObject<UTexture2D>(nullptr,Path))
        {
            LoadError=TEXT("地球の景観データを読み込めません。");
            UE_LOG(LogTemp,Error,TEXT("STAR Earth texture missing: %s"),Path);
            return false;
        }
    }
    UE_LOG(LogTemp,Display,TEXT("STAR Earth V2: opticalLUT=%d volumeClouds=%d pacificDetail=%d"),
        bEarthLUTEnabled,bEarthVolumeClouds,bEarthPacificEnabled);
    if (!Data.Load(LoadError)) { UE_LOG(LogTemp,Error,TEXT("STAR data: %s"),*LoadError); return false; }
    AstronomicalUtc=static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp());
    FString ClockStart;
    if(FParse::Value(FCommandLine::Get(),TEXT("StarUtc="),ClockStart))
    {
        FDateTime Parsed;
        if(!FDateTime::ParseIso8601(*ClockStart,Parsed)) { LoadError=TEXT("開始日時の形式が不正です。");return false; }
        AstronomicalUtc=static_cast<double>(Parsed.ToUnixTimestamp());
    }
    double RequestedRate=1;FParse::Value(FCommandLine::Get(),TEXT("StarClockRate="),RequestedRate);SetClockRate(RequestedRate);
    if(!Data.SetAstronomicalUtc(AstronomicalUtc))
    { LoadError=TEXT("日時が天体暦の収録範囲外です。2026年の日時を指定してください。");return false; }
    GetWorld()->GetWorldSettings()->bEnableWorldBoundsChecks = false;
    CreateBodies();
    CreateLighting();
    bReady = true;
    return true;
}
UProceduralMeshComponent* AStarWorldDirector::NewMesh(const FString& Name)
{
    auto* Mesh = NewObject<UProceduralMeshComponent>(this,FName(*Name));
    AddInstanceComponent(Mesh);
    Mesh->SetupAttachment(SceneRoot);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    Mesh->bUseAsyncCooking = true;
    Mesh->RegisterComponent();
    return Mesh;
}
void AStarWorldDirector::Sphere(UProceduralMeshComponent* Mesh, int32 Segments, int32 Rings, bool bInside,const FStarDataCatalog* MoonTerrain)
{
    TArray<FVector> Vertices,Normals;
    TArray<FVector2D> UV;
    TArray<int32> Triangles;
    TArray<FProcMeshTangent> Tangents;
    for (int32 Y=0;Y<=Rings;++Y)
    {
        const double V=static_cast<double>(Y)/Rings,Lat=star::Pi*(0.5-V);
        for(int32 X=0;X<=Segments;++X)
        {
            const double U=static_cast<double>(X)/Segments,Lon=star::Pi*(2*U-1);
            const FVector Normal(FMath::Cos(Lat)*FMath::Cos(Lon),-FMath::Cos(Lat)*FMath::Sin(Lon),FMath::Sin(Lat));
            const double Elevation=MoonTerrain?MoonTerrain->MoonHeight(FMath::RadiansToDegrees(Lat),FMath::RadiansToDegrees(Lon)):0.0;
            Vertices.Add(Normal*(1.0+Elevation/LunarRadiusM));
            Normals.Add(bInside?-Normal:Normal);
            UV.Add(FVector2D(U,V));
            Tangents.Emplace(FVector(-FMath::Sin(Lon),-FMath::Cos(Lon),0),false);
        }
    }
    for(int32 Y=0;Y<Rings;++Y) for(int32 X=0;X<Segments;++X)
    {
        const int32 A=Y*(Segments+1)+X,B=A+1,C=A+Segments+1,D=C+1;
        // UE front faces use negative cross(edge1,edge2).normal after the
        // simulation-to-UE Y reflection (same convention as CreateGridMeshWelded).
        if(bInside) { Triangles.Append({A,B,C,B,D,C}); }
        else { Triangles.Append({A,C,B,B,C,D}); }
    }
    Mesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,TArray<FLinearColor>(),Tangents,false);
}
void AStarWorldDirector::Ring(UProceduralMeshComponent* Mesh,double Inner,double Outer)
{
    constexpr int32 Segments=512;
    TArray<FVector> Vertices,Normals;
    TArray<FVector2D> UV;
    TArray<int32> Triangles;
    for(int32 A=0;A<=Segments;++A)
    {
        const double Phase=2*star::Pi*A/Segments;
        for(int32 Side=0;Side<2;++Side)
        {
            const double Radius=Side?Outer:Inner;
            Vertices.Emplace(Radius*FMath::Cos(Phase),-Radius*FMath::Sin(Phase),0);
            Normals.Emplace(0,0,1);
            UV.Emplace(Side,static_cast<double>(A)/Segments);
        }
        if(A<Segments) { const int32 I=A*2; Triangles.Append({I,I+1,I+2,I+1,I+3,I+2}); }
    }
    Mesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,TArray<FLinearColor>(),TArray<FProcMeshTangent>(),false);
}
void AStarWorldDirector::CreateBodies()
{
    for(const auto& Body:Data.Bodies())
    {
        const FString Id=UTF8_TO_TCHAR(Body.Definition.id.c_str());
        auto* Mesh=NewMesh(TEXT("Body_")+Id);
        Sphere(Mesh,(Id==TEXT("moon")||Id==TEXT("earth"))?512:256,(Id==TEXT("moon")||Id==TEXT("earth"))?256:128,false,Id==TEXT("moon")?&Data:nullptr);
        BodyMeshes.Add(Mesh);
        UMaterialInterface* Base=nullptr;
        if(Id==TEXT("earth")) Base=Material(TEXT("/Game/Star/Materials/MI_Earth.MI_Earth"));
        else if(Id==TEXT("moon")) Base=Material(TEXT("/Game/Star/Materials/MI_Moon.MI_Moon"));
        else if(Id==TEXT("saturn")) Base=Material(TEXT("/Game/Star/Materials/MI_Saturn.MI_Saturn"));
        else Base=Material(TEXT("/Game/Star/Materials/M_Sun.M_Sun"));
        auto* Dynamic=Base?UMaterialInstanceDynamic::Create(Base,this):nullptr;
        Mesh->SetMaterial(0,Dynamic);
        BodyMaterials.Add(Dynamic);
        if(Id==TEXT("earth"))
        {
            EarthCloudMesh=NewMesh(TEXT("EarthCloudDeck"));
            Sphere(EarthCloudMesh,512,256);
            EarthCloudMaterial=UMaterialInstanceDynamic::Create(Material(bEarthVolumeClouds?
                TEXT("/Game/Star/Materials/M_Clouds.M_Clouds"):TEXT("/Game/Star/Materials/M_CloudsSurface.M_CloudsSurface")),this);
            EarthCloudMaterial->SetScalarParameterValue(TEXT("EarthLUTEnabled"),bEarthLUTEnabled?1.0f:0.0f);
            EarthCloudMesh->SetMaterial(0,EarthCloudMaterial);
            EarthCloudMesh->SetTranslucentSortPriority(5);
            if(Dynamic && EarthCloudMaterial)
            {
                Dynamic->SetScalarParameterValue(TEXT("CloudLayerEnabled"),1.0f);
                Dynamic->SetScalarParameterValue(TEXT("CloudHeightMeters"),5000.0f);
                Dynamic->SetScalarParameterValue(TEXT("CloudOpacity"),0.98f);
                Dynamic->SetScalarParameterValue(TEXT("WaterRoughness"),0.16f);
                Dynamic->SetScalarParameterValue(TEXT("NightIntensity"),24.0f);
                for(const TCHAR* Parameter:{TEXT("EarthDetailEnabled"),TEXT("EarthDetailWest"),TEXT("EarthDetailEast"),
                    TEXT("EarthDetailNorth"),TEXT("EarthDetailSouth"),TEXT("EarthDetailFeatherDegrees")})
                    EarthCloudMaterial->SetScalarParameterValue(Parameter,Dynamic->K2_GetScalarParameterValue(Parameter));
            }
        }
        UProceduralMeshComponent* Shell=nullptr;
        UMaterialInstanceDynamic* ShellMaterial=nullptr;
        if(Id==TEXT("earth") || Id==TEXT("saturn"))
        {
            Shell=NewMesh(TEXT("Atmosphere_")+Id);
            Sphere(Shell,Id==TEXT("earth")?256:128,Id==TEXT("earth")?128:64);
            if(auto* AtmosphereBase=Material(TEXT("/Game/Star/Materials/M_Atmosphere.M_Atmosphere")))
                ShellMaterial=UMaterialInstanceDynamic::Create(AtmosphereBase,this);
            Shell->SetMaterial(0,ShellMaterial);
            Shell->SetTranslucentSortPriority(10);
            if(ShellMaterial && Id==TEXT("earth"))
                ShellMaterial->SetScalarParameterValue(TEXT("EarthLUTEnabled"),bEarthLUTEnabled?1.0f:0.0f);
            if(ShellMaterial && Id==TEXT("saturn"))
            {
                ShellMaterial->SetScalarParameterValue(TEXT("AtmosphereHeightMeters"),200000);
                ShellMaterial->SetScalarParameterValue(TEXT("RayleighScaleHeightMeters"),60000);
                ShellMaterial->SetScalarParameterValue(TEXT("DensityScale"),0.16f);
                ShellMaterial->SetVectorParameterValue(TEXT("RayleighBeta"),FLinearColor(0.35e-6f,0.55e-6f,1.10e-6f));
            }
        }
        Atmospheres.Add(Shell);
        AtmosphereMaterials.Add(ShellMaterial);
    }
    RingMesh=NewMesh(TEXT("SaturnRings"));
    const double SaturnRadius=Data.Find(TEXT("saturn"))->Definition.radiusMeters;
    Ring(RingMesh,Data.RingInnerRadiusMeters()/SaturnRadius,Data.RingOuterRadiusMeters()/SaturnRadius);
    if(auto* Base=Material(TEXT("/Game/Star/Materials/M_Rings.M_Rings"))) RingMaterial=UMaterialInstanceDynamic::Create(Base,this);
    RingMesh->SetMaterial(0,RingMaterial);
    RingMesh->SetTranslucentSortPriority(2);
    StarMesh=NewMesh(TEXT("CatalogStarfield"));
    Sphere(StarMesh,128,64,true);
    if(auto* Base=Material(TEXT("/Game/Star/Materials/M_Stars.M_Stars")))
    {
        StarMaterial=UMaterialInstanceDynamic::Create(Base,this);
        // Keep the astronomical catalogue as the explicit low-artifice
        // fallback. Enhanced mode combines a compact-star-suppressed diffuse
        // photograph with the separate 16K bright catalogue in Stars.ush.
        StarMaterial->SetScalarParameterValue(TEXT("StarIntensity"),0.02f);
        StarMaterial->SetScalarParameterValue(TEXT("EnhancedStars"),bEnhancedStars?1.0f:0.0f);
        StarMaterial->SetScalarParameterValue(TEXT("PhotoSkyIntensity"),PhotoSkyIntensity);
    }
    StarMesh->SetMaterial(0,StarMaterial);
    if(auto* Base=Material(TEXT("/Game/Star/Materials/M_Surface.M_Surface"))) TerrainMaterial=UMaterialInstanceDynamic::Create(Base,this);
    float ClastStyle=1.0f;
    FParse::Value(FCommandLine::Get(),TEXT("StarClastStyle="),ClastStyle);
    if(TerrainMaterial) TerrainMaterial->SetScalarParameterValue(TEXT("ClastStyleStrength"),FMath::Clamp(ClastStyle,0.0f,1.0f));
    float MesoBlend=0;
    FParse::Value(FCommandLine::Get(),TEXT("StarMesoBlend="),MesoBlend);
    if(TerrainMaterial) TerrainMaterial->SetScalarParameterValue(TEXT("MesoBlendStrength"),FMath::Clamp(MesoBlend,0.0f,1.0f));
    LunarTerrain->Initialize(&Data,TerrainMaterial);
    EarthTerrain->Initialize(Material(TEXT("/Game/Star/Materials/M_ObservedEarth.M_ObservedEarth")));
    bFarPhoto=FParse::Param(FCommandLine::Get(),TEXT("StarFarPhoto"));
    FParse::Value(FCommandLine::Get(),TEXT("StarFarPhotoGain="),FarPhotoGain);
    if(TerrainMaterial) TerrainMaterial->SetScalarParameterValue(TEXT("FarPhotoGain"),FMath::Clamp(FarPhotoGain,0.1f,4.0f));
}
void AStarWorldDirector::CreateLighting()
{
    int32 MeshSDF=-1,ProbeResolution=-1;
    FParse::Value(FCommandLine::Get(),TEXT("StarMeshSDF="),MeshSDF);
    FParse::Value(FCommandLine::Get(),TEXT("StarProbeDownsample="),ProbeResolution);
    if(MeshSDF>=0)
        for(const TCHAR* Name:{TEXT("r.Lumen.TraceMeshSDFs"),TEXT("r.Lumen.TraceMeshSDFs.Allow")})
            if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name)) Variable->Set(MeshSDF?1:0,ECVF_SetByCommandline);
    if(ProbeResolution==16||ProbeResolution==32)
        if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor")))
            Variable->Set(ProbeResolution,ECVF_SetByCommandline);
    int32 LumenView=0;
    if(FParse::Value(FCommandLine::Get(),TEXT("StarLumenVisualize="),LumenView)&&LumenView>=0&&LumenView<=13)
        if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.Visualize")))
            Variable->Set(LumenView,ECVF_SetByCommandline);
    SunLight=GetWorld()->SpawnActor<ADirectionalLight>();
    auto* Component=Cast<UDirectionalLightComponent>(SunLight->GetLightComponent());
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetIntensity(127500);
    Component->SetLightColor(FLinearColor(1.0f,0.985f,0.96f));
    Component->SetCastShadows(true);
    if(!FParse::Param(FCommandLine::Get(),TEXT("StarSingleScatterAtmosphere")))
    {
        // A/B candidate: the engine's physical, multiple-scattering atmosphere.
        // Its planet centre follows the same local origin as the actual Earth.
        auto* Atmosphere=NewObject<USkyAtmosphereComponent>(this,TEXT("EarthPhysicalAtmosphere"));
        Atmosphere->SetupAttachment(SceneRoot);
        Atmosphere->SetMobility(EComponentMobility::Movable);
        Atmosphere->TransformMode=ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
        // UE's per-pixel transmittance treats every receiver inside BottomRadius
        // as shadowed by a solid planet (SkyAtmosphereCommon.ush). A sea-level
        // radius incorrectly shadows negative orthometric elevations and nearly
        // coplanar sea triangles. Enclose the supported DSM from below instead.
        // Shift density coordinates as well: this is NOT an artistic haze gain.
        constexpr float AtmosphereDatumDepthKm=0.5f;
        Atmosphere->BottomRadius=static_cast<float>(Data.Find(TEXT("earth"))->Definition.radiusMeters/1000.0)-AtmosphereDatumDepthKm;
        Atmosphere->AtmosphereHeight=100.0f+AtmosphereDatumDepthKm;
        Atmosphere->RayleighScatteringScale*=FMath::Exp(AtmosphereDatumDepthKm/Atmosphere->RayleighExponentialDistribution);
        const float MieDatumScale=FMath::Exp(AtmosphereDatumDepthKm/Atmosphere->MieExponentialDistribution);
        Atmosphere->MieScatteringScale*=MieDatumScale;
        Atmosphere->MieAbsorptionScale*=MieDatumScale;
        Atmosphere->OtherTentDistribution.TipAltitude+=AtmosphereDatumDepthKm;
        Atmosphere->MultiScatteringFactor=1.0f;
        Atmosphere->TransmittanceMinLightElevationAngle=-90.0f;
        AddInstanceComponent(Atmosphere);Atmosphere->RegisterComponent();
        Component->bPerPixelAtmosphereTransmittance=true;
        Component->SetAtmosphereSunLight(true);Component->SetAtmosphereSunLightIndex(0);
        Component->SetLightColor(FLinearColor::White);
        // In UE 5.8 atmosphere-sun is already true by default, so its setter
        // may do nothing. Explicitly rebuild the proxy after changing this bit.
        Component->MarkRenderStateDirty();
        if(auto* Quality=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkyAtmosphere.MultiScatteringLUT.HighQuality"))) Quality->Set(1,ECVF_SetByCode);
        UE_LOG(LogTemp,Display,TEXT("STAR physical Earth atmosphere: bottom %.6f km (-500m datum), top mean radius +100km; sea-level exponential densities and ozone altitude preserved; per-pixel solar transmittance"),Atmosphere->BottomRadius);
    }
    SkyLight=GetWorld()->SpawnActor<ASkyLight>();
    SkyLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    // Space has no bright ambient sky. A captured terrestrial cubemap would
    // follow the ship to the Moon and overpower the dark side's reflections.
    SkyLight->GetLightComponent()->SetIntensity(0.0f);
    SkyLight->GetLightComponent()->SetLightColor(FLinearColor(0.24f,0.32f,0.48f));
    // Single-pose lookdev only until measured. Never carry this one-shot
    // cubemap into ordinary travel: the production environment changes.
    int32 CapturePoseCount=0;
    FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkCount="),CapturePoseCount);
    FString CaptureEVAPath;
    const bool bValidEVAProbe=FParse::Param(FCommandLine::Get(),TEXT("StarEVAQA"))&&
        FParse::Value(FCommandLine::Get(),TEXT("StarEVAPath="),CaptureEVAPath)&&!CaptureEVAPath.IsEmpty();
    bEnvironmentCaptureDiagnostic=FParse::Param(FCommandLine::Get(),TEXT("StarEnvironmentCapture"))&&
        ((FParse::Param(FCommandLine::Get(),TEXT("StarBenchmark"))&&CapturePoseCount==1)||
         bValidEVAProbe);
    bLocalEnvironment=FParse::Param(FCommandLine::Get(),TEXT("StarLocalEnvironment"))&&!bEnvironmentCaptureDiagnostic;
    PostProcess=GetWorld()->SpawnActor<APostProcessVolume>();
    PostProcess->bUnbound=true;
    auto& Settings=PostProcess->Settings;
    Settings.bOverride_AutoExposureMethod=true;
    Settings.AutoExposureMethod=AEM_Histogram;
    Settings.bOverride_HistogramLogMin=true;
    Settings.bOverride_HistogramLogMax=true;
    Settings.HistogramLogMin=-8;
    Settings.HistogramLogMax=20;
    Settings.bOverride_AutoExposureLowPercent=true;
    Settings.bOverride_AutoExposureHighPercent=true;
    Settings.AutoExposureLowPercent=80;
    Settings.AutoExposureHighPercent=98;
    Settings.bOverride_AutoExposureMinBrightness=true;
    Settings.bOverride_AutoExposureMaxBrightness=true;
    Settings.AutoExposureMinBrightness=-2;
    Settings.AutoExposureMaxBrightness=18;
    Settings.bOverride_AutoExposureSpeedUp=true;
    Settings.bOverride_AutoExposureSpeedDown=true;
    Settings.AutoExposureSpeedUp=3;
    Settings.AutoExposureSpeedDown=1;
    Settings.bOverride_AutoExposureBias=true;
    Settings.AutoExposureBias=0;
    Settings.bOverride_BloomIntensity=true;
    Settings.BloomIntensity=0.18f;
    Settings.bOverride_VignetteIntensity=true;
    Settings.VignetteIntensity=0.15f;
    Settings.bOverride_MotionBlurAmount=true;
    Settings.MotionBlurAmount=0;
    Settings.bOverride_LocalExposureHighlightContrastScale=true;
    Settings.LocalExposureHighlightContrastScale=0.65f;
    Settings.bOverride_LocalExposureShadowContrastScale=true;
    Settings.LocalExposureShadowContrastScale=0.75f;
    Settings.bOverride_LocalExposureDetailStrength=true;
    Settings.LocalExposureDetailStrength=0.7f;
    float Detail=Settings.LocalExposureDetailStrength;
    if(FParse::Value(FCommandLine::Get(),TEXT("StarLocalExposureDetail="),Detail))
        Settings.LocalExposureDetailStrength=FMath::Clamp(Detail,0.5f,1.25f);
    UE_LOG(LogTemp,Display,TEXT("STAR lookdev: Detail=%g MeshSDF=%d Allow=%d ProbeDownsample=%d"),
        Settings.LocalExposureDetailStrength,
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.TraceMeshSDFs"))->GetInt(),
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.TraceMeshSDFs.Allow"))->GetInt(),
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"))->GetInt());
}
star::FlightState AStarWorldDirector::InitialFlightState() const
{
    star::FlightState State;
    const auto* Earth=Data.Find(TEXT("earth"));
    const auto* Moon=Data.Find(TEXT("moon"));
    const auto* Sun=Data.Find(TEXT("sun"));
    if(!Earth||!Moon||!Sun) return State;
    const auto TowardSun=(Sun->Definition.centerMeters-Earth->Definition.centerMeters).Normalized();
    const auto Pole=Earth->Definition.bodyFixedToSimulation.Rotate({0,0,1});
    const auto East=star::Vec3d::Cross(Pole,TowardSun).Normalized();
    const auto Radial=(-East+TowardSun*0.14+Pole*0.20).Normalized();
    State.positionMeters=Earth->Definition.centerMeters+Radial*(Earth->Definition.radiusMeters+450000.0);
    const auto Forward=(star::Vec3d::Cross(Radial,TowardSun).Normalized()-Radial*0.35).Normalized();
    State.orientation=star::Quatd::FromForwardUp(Forward,Radial);
    State.targetBodyId="moon";
    return State;
}
void AStarWorldDirector::UpdateScene(const star::FlightState& State,const star::Vec3d& Origin,const star::Vec3d& Camera,double Dt,bool bActiveCameraUpdate)
{
    // A parked, unpossessed ship must not overwrite the walking observer's
    // terrain window or camera-relative planetary parameters later this frame.
    if(!bReady||!bActiveCameraUpdate) return;
    // Explicit diagnostic flags only; ordinary play keeps the normal layers.
    const bool LayerQA=FParse::Param(FCommandLine::Get(),TEXT("StarBenchmark"))&&FParse::Param(FCommandLine::Get(),TEXT("StarEarthLayerQA"));
    // Clear-sky scenario until dated meteorological fields are available. Do
    // not pass a 2002 cloud composite off as the selected UTC's weather.
    const bool NoClouds=true;
    const bool NoAtmosphere=LayerQA&&FParse::Param(FCommandLine::Get(),TEXT("StarEarthNoAtmosphere"));
    const bool NoSurfacePhoto=LayerQA&&FParse::Param(FCommandLine::Get(),TEXT("StarEarthNoSurfacePhoto"));
    SceneTime+=Dt;
    UpdateTerrain(State,Origin);
    if(TerrainMaterial)
    {
        double Weight=0;
        if(bFarPhoto) if(const auto* Moon=Data.Find(TEXT("moon")))
        {
            constexpr double Lat=20.1908*PI/180.0,Lon=30.7717*PI/180.0;
            const star::Vec3d Radial{std::cos(Lat)*std::cos(Lon),std::cos(Lat)*std::sin(Lon),std::sin(Lat)};
            star::TerrainSample Ground;
            if(Data.SampleTerrain(Moon->Definition,Moon->Definition.bodyFixedToSimulation.Rotate(Radial),Ground))
            {
                const auto Local=Moon->Definition.bodyFixedToSimulation.Conjugate().Rotate(Camera-Moon->Definition.centerMeters);
                const auto Offset=Local-Radial*(Moon->Definition.radiusMeters+Ground.heightMeters);
                const double Height=star::Vec3d::Dot(Offset,Radial);
                const double Walk=(Offset-Radial*Height).Length();
                Weight=FMath::Clamp((100.0-Walk)/70.0,0.0,1.0)*FMath::Clamp((8.0-Height)/5.8,0.0,1.0);
            }
        }
        TerrainMaterial->SetScalarParameterValue(TEXT("FarPhotoStrength"),Weight);
    }
    const auto* Sun=Data.Find(TEXT("sun"));
    if(!Sun) return;
    const auto* LocalEarth=Data.Find(TEXT("earth"));
    auto* PhysicalAtmosphere=FindComponentByClass<USkyAtmosphereComponent>();
    const bool NativeAtmosphere=PhysicalAtmosphere&&LocalEarth&&!NoAtmosphere&&(Camera-LocalEarth->Definition.centerMeters).Length()<MaximumProxyDistanceM;
    if(PhysicalAtmosphere)
    {
        PhysicalAtmosphere->SetVisibility(NativeAtmosphere);
        if(NativeAtmosphere) PhysicalAtmosphere->SetWorldLocation(FStarDataCatalog::UEVector(star::ToUnrealCentimeters(LocalEarth->Definition.centerMeters,Origin)));
    }
    const bool bEarthAtmosphere=LocalEarth && (Camera-LocalEarth->Definition.centerMeters).Length()<LocalEarth->Definition.radiusMeters+80000.0;
    // Bright terrestrial twilight washes out the photographic star background.
    if(StarMesh) StarMesh->SetVisibility(!bEarthAtmosphere || bDreamTwilight);
    const auto ToSun=Sun->Definition.centerMeters-Camera;
    const double SunDistance=FMath::Max(AU*0.05,ToSun.Length());
    if(NativeAtmosphere)
    {
        auto* Light=Cast<UDirectionalLightComponent>(SunLight->GetLightComponent());
        const float Angle=static_cast<float>(FMath::RadiansToDegrees(2.0*FMath::Asin(FMath::Clamp(Sun->Definition.radiusMeters/SunDistance,0.0,1.0))));
        if(!FMath::IsNearlyEqual(Light->LightSourceAngle,Angle,0.0001f)) Light->SetLightSourceAngle(Angle);
    }
    if(PostProcess)
    {
        // Bound auto exposure by the actual incident solar flux. The optional
        // exposure-compensated star photo must not drive the meter to an
        // extreme while a small bright Moon grows into the frame.
        const float SolarEV=14.0f-2.0f*FMath::Log2(static_cast<float>(SunDistance/AU));
        float NightAllowance=0.0f;
        if(LocalEarth)
        {
            const auto Offset=Camera-LocalEarth->Definition.centerMeters;
            const float RadiusRatio=static_cast<float>(Offset.Length()/LocalEarth->Definition.radiusMeters);
            const float SolarCos=static_cast<float>(star::Vec3d::Dot(Offset.Normalized(),ToSun.Normalized()));
            const float Nightside=FMath::SmoothStep(0.35f,0.9f,-SolarCos);
            const float Nearby=1.0f-FMath::SmoothStep(3.0f,4.0f,RadiusRatio);
            // Allow the histogram to reveal actual Black Marble city lights in
            // Earth's shadow. Smooth distance/phase bounds avoid a camera jump.
            NightAllowance=14.0f*Nightside*Nearby;
        }
        PostProcess->Settings.AutoExposureMinBrightness=SolarEV-2.0f-NightAllowance;
        PostProcess->Settings.AutoExposureMaxBrightness=SolarEV+2.0f;
        float FixedEV=13.0f;
        if(LayerQA&&FParse::Value(FCommandLine::Get(),TEXT("StarEarthExposureEV="),FixedEV))
        {
            PostProcess->Settings.AutoExposureMinBrightness=FMath::Clamp(FixedEV,-2.0f,18.0f);
            PostProcess->Settings.AutoExposureMaxBrightness=PostProcess->Settings.AutoExposureMinBrightness;
        }
    }
    const FVector LightDirection=FStarDataCatalog::UEVector(star::SimulationDirectionToUnreal(ToSun.Normalized()));
    SunLight->SetActorRotation((-LightDirection).Rotation());
    double SunVisibility=1.0;
    for(const auto& Record:Data.Bodies())
    {
        const auto& Occluder=Record.Definition;
        if(Occluder.id=="sun") continue;
        if(NativeAtmosphere&&Occluder.id=="earth") continue; // Applied per pixel by the native atmosphere; do not count it twice.
        double ShadowRadius=Occluder.radiusMeters;
        const auto Offset=State.positionMeters-Occluder.centerMeters;
        if(Occluder.landable&&Offset.Length()<Occluder.radiusMeters+30000)
        {
            // A below-datum crater is still open to the sky. A local sampled
            // shell provides the broad horizon; terrain meshes cast local shadows.
            star::TerrainSample Surface;
            if(Data.SampleTerrain(Occluder,Offset.Normalized(),Surface)) ShadowRadius+=Surface.heightMeters-0.25;
        }
        SunVisibility=FMath::Min(SunVisibility,star::SolarDiskVisibleFraction(State.positionMeters,
            Sun->Definition.centerMeters,Sun->Definition.radiusMeters,Occluder.centerMeters,ShadowRadius));
    }
    auto* SolarLight=Cast<UDirectionalLightComponent>(SunLight->GetLightComponent());
    // The atmosphere needs unoccluded solar illuminance even after local sunset.
    // Native per-pixel transmittance includes Earth shadow. Other bodies still
    // use the geometric occultation factor for opaque receivers.
    SolarLight->SetIntensity(static_cast<float>(127500.0*AU*AU/(SunDistance*SunDistance)*(NativeAtmosphere?1.0:SunVisibility)));
    SolarLight->SetDiffuseScale(NativeAtmosphere?static_cast<float>(SunVisibility):1.0f);
    SolarLight->SetSpecularScale(NativeAtmosphere?static_cast<float>(SunVisibility):1.0f);
    for(int32 I=0;I<Data.Bodies().Num();++I)
    {
        const auto& Record=Data.Bodies()[I];
        const auto& Body=Record.Definition;
        const auto Relative=Body.centerMeters-Camera;
        const double Distance=Relative.Length();
        const double ProxyScale=FMath::Min(1.0,MaximumProxyDistanceM/FMath::Max(1.0,Distance));
        const auto ProxyCenter=Camera+Relative*ProxyScale;
        const FVector Center=FStarDataCatalog::UEVector(star::ToUnrealCentimeters(ProxyCenter,Origin));
        const FQuat Rotation=FStarDataCatalog::UERotation(Body.bodyFixedToSimulation);
        const double RenderRadius=Body.radiusMeters*ProxyScale*100;
        auto* Mesh=BodyMeshes[I].Get();
        if(Body.id=="sun") Mesh->SetVisibility(!NativeAtmosphere); // One physical solar disk, not two overlapping ones.
        Mesh->SetWorldLocationAndRotation(Center,Rotation);
        const bool bSaturnVisual=Body.id=="saturn"&&bSaturnOblateKnownApprox;
        const float EquatorialScale=static_cast<float>(bSaturnVisual?SaturnEquatorialRadiusM/Body.radiusMeters:1.0);
        const float PolarScale=static_cast<float>(bSaturnVisual?SaturnPolarRadiusM/Body.radiusMeters:1.0);
        // The sphere's local XY plane is the body equator and local Z is the
        // body north pole. This keeps the centre, orientation and ring
        // coordinates unchanged while making the known approximation visible
        // in the rendered body only. The material receives these same axes
        // and converts the parameter latitude to physical planetocentric UV.
        Mesh->SetWorldScale3D(FVector(RenderRadius*EquatorialScale,RenderRadius*EquatorialScale,RenderRadius*PolarScale));
        const auto BodyToSun=Sun->Definition.centerMeters-Body.centerMeters;
        const double BodySunDistance=FMath::Max(AU*0.05,BodyToSun.Length());
        const auto LocalSun=Body.bodyFixedToSimulation.Conjugate().Rotate(BodyToSun.Normalized());
        const auto LocalCamera=Body.bodyFixedToSimulation.Conjugate().Rotate(Camera-Body.centerMeters)/Body.radiusMeters;
        const float Radiance=static_cast<float>(127500.0*AU*AU/(BodySunDistance*BodySunDistance)/star::Pi);
        const auto SetParameters=[&](UMaterialInstanceDynamic* Mat)
        {
            if(!Mat) return;
            Mat->SetVectorParameterValue(TEXT("SunDirectionLocal"),ColorVector(LocalSun));
            Mat->SetVectorParameterValue(TEXT("CameraLocal"),ColorVector(LocalCamera));
            if(Mat==AtmosphereMaterials[I].Get())
                Mat->SetScalarParameterValue(TEXT("RenderRadiusCm"),static_cast<float>(RenderRadius));
            if(Mat==AtmosphereMaterials[I].Get() || Mat==EarthCloudMaterial.Get())
            {
                const auto ToAxis=[&](const star::Vec3d& Axis)
                {return ColorVector(star::SimulationDirectionToUnreal(Body.bodyFixedToSimulation.Rotate(Axis)));};
                Mat->SetVectorParameterValue(TEXT("AxisXUE"),ToAxis({1,0,0}));
                Mat->SetVectorParameterValue(TEXT("AxisYUE"),ToAxis({0,1,0}));
                Mat->SetVectorParameterValue(TEXT("AxisZUE"),ToAxis({0,0,1}));
                Mat->SetScalarParameterValue(TEXT("DreamStrength"),Body.id=="earth"&&bDreamTwilight?1.0f:0.0f);
                Mat->SetScalarParameterValue(TEXT("DreamSunset"),bDreamSunset?1.0f:0.0f);
            }
            Mat->SetVectorParameterValue(TEXT("BodyAxes"),FLinearColor(EquatorialScale,EquatorialScale,PolarScale,1));
            Mat->SetScalarParameterValue(TEXT("SunRadiance"),Radiance);
            Mat->SetScalarParameterValue(TEXT("RadiusMeters"),static_cast<float>(Body.radiusMeters));
            if(Body.id=="earth")
            {
                if(Mat==BodyMaterials[I].Get())EarthTerrain->ApplyGlobeCoverage(Mat);
                Mat->SetScalarParameterValue(TEXT("EarthDetailEnabled"),0.0f);
                Mat->SetScalarParameterValue(TEXT("NightPhotoEnabled"),0.0f);
                Mat->SetScalarParameterValue(TEXT("NightDetailEnabled"),0.0f);
                const float NearPhoto=1.0f-FMath::SmoothStep(300000.0f,600000.0f,static_cast<float>(FMath::Max(Distance-Body.radiusMeters,0.0)));
                Mat->SetScalarParameterValue(TEXT("PacificEnabled"),bEarthPacificEnabled?NearPhoto:0.0f);
                if(NoSurfacePhoto&&Mat==BodyMaterials[I].Get())
                {
                    Mat->SetScalarParameterValue(TEXT("EarthDetailEnabled"),0.0f);
                    Mat->SetScalarParameterValue(TEXT("PacificEnabled"),0.0f);
                }
                if(NoClouds)
                {
                    Mat->SetScalarParameterValue(TEXT("CloudOpacity"),0.0f);
                    Mat->SetScalarParameterValue(TEXT("CloudLayerEnabled"),0.0f);
                    Mat->SetScalarParameterValue(TEXT("UseCloudTexture"),0.0f);
                    Mat->SetScalarParameterValue(TEXT("CloudCoverage"),0.0f);
                }
            }
            Mat->SetScalarParameterValue(TEXT("SunAngularRadius"),static_cast<float>(695700000.0/BodySunDistance));
            Mat->SetScalarParameterValue(TEXT("CloudPhase"),0.0f);
            Mat->SetScalarParameterValue(TEXT("CloudReliefMeters"),0.0f);
            if(Body.id=="saturn")
            {
                Mat->SetScalarParameterValue(TEXT("RingInnerRadius"),static_cast<float>(Data.RingInnerRadiusMeters()/Body.radiusMeters));
                Mat->SetScalarParameterValue(TEXT("RingOuterRadius"),static_cast<float>(Data.RingOuterRadiusMeters()/Body.radiusMeters));
            }
            if(Body.id=="moon")
            {
                star::Vec3d HoleDirection;double HoleCos=1;bool Enabled=false;
                LunarTerrain->GetGlobeHole(HoleDirection,HoleCos,Enabled);
                Mat->SetVectorParameterValue(TEXT("TerrainHoleDirection"),ColorVector(HoleDirection));
                Mat->SetScalarParameterValue(TEXT("TerrainHoleCos"),static_cast<float>(HoleCos));
                Mat->SetScalarParameterValue(TEXT("TerrainHoleEnabled"),Enabled?1.0f:0.0f);
            }
            const auto* Occluder=Data.Find(Body.id=="earth"?TEXT("moon"):Body.id=="moon"?TEXT("earth"):TEXT(""));
            if(Occluder)
            {
                const auto LocalOccluder=Body.bodyFixedToSimulation.Conjugate().Rotate(Occluder->Definition.centerMeters-Body.centerMeters)/Body.radiusMeters;
                Mat->SetVectorParameterValue(TEXT("OccluderLocal"),ColorVector(LocalOccluder));
                Mat->SetScalarParameterValue(TEXT("OccluderRadius"),static_cast<float>(Occluder->Definition.radiusMeters/Body.radiusMeters));
            }
        };
        SetParameters(BodyMaterials[I]);
        if(Body.id=="sun" && BodyMaterials[I])
        {
            auto* Mat=BodyMaterials[I].Get();
            // Photosphere surface brightness is independent of observer distance.
            Mat->SetScalarParameterValue(TEXT("SunRadiance"),bDreamTwilight?72000000.0f:18000000.0f);
            Mat->SetScalarParameterValue(TEXT("EarthRadiusMeters"),0.0f);
            if(LocalEarth)
            {
                const auto& E=LocalEarth->Definition;
                const auto EarthRelative=E.bodyFixedToSimulation.Conjugate().Rotate(Camera-E.centerMeters)/E.radiusMeters;
                // Outside the local Earth neighborhood, retain ordinary geometry
                // occlusion without feeding astronomical float offsets to this shader.
                if(EarthRelative.Length()<32.0)
                {
                    Mat->SetScalarParameterValue(TEXT("EarthRadiusMeters"),static_cast<float>(E.radiusMeters));
                    Mat->SetVectorParameterValue(TEXT("EarthCameraLocal"),ColorVector(EarthRelative));
                    const auto Axis=[&](const star::Vec3d& V)
                    { return ColorVector(star::SimulationDirectionToUnreal(E.bodyFixedToSimulation.Rotate(V))); };
                    Mat->SetVectorParameterValue(TEXT("EarthAxisX"),Axis({1,0,0}));
                    Mat->SetVectorParameterValue(TEXT("EarthAxisY"),Axis({0,1,0}));
                    Mat->SetVectorParameterValue(TEXT("EarthAxisZ"),Axis({0,0,1}));
                }
            }
        }
        if(Body.id=="earth" && EarthCloudMesh && EarthCloudMaterial)
        {
            EarthCloudMesh->SetWorldLocationAndRotation(Center,Rotation);
            EarthCloudMesh->SetWorldScale3D(FVector(RenderRadius*(1.0+(bEarthVolumeClouds?8000.0:5000.0)/Body.radiusMeters)));
            EarthCloudMesh->SetVisibility(!NoClouds&&Body.radiusMeters/FMath::Max(Distance,1.0)>0.0004);
            SetParameters(EarthCloudMaterial);
        }
        if(Atmospheres[I])
        {
            const double Height=Body.id=="saturn"?200000.0:100000.0;
            Atmospheres[I]->SetWorldLocationAndRotation(Center,Rotation);
            const float HeightScale=static_cast<float>(Height/Body.radiusMeters);
            Atmospheres[I]->SetWorldScale3D(FVector(RenderRadius*(EquatorialScale+HeightScale),
                                                     RenderRadius*(EquatorialScale+HeightScale),
                                                     RenderRadius*(PolarScale+HeightScale)));
            Atmospheres[I]->SetVisibility(!(Body.id=="earth"&&(NoAtmosphere||NativeAtmosphere))&&Body.radiusMeters/FMath::Max(Distance,1.0)>0.0004);
            SetParameters(AtmosphereMaterials[I]);
            if(AtmosphereMaterials[I])
            {
                // Preserve the high-precision orbital sunrise close to Earth,
                // but avoid its 48x16 budget on every distant globe pixel.
                const float Detail=Body.id=="earth"?1.0f-FMath::SmoothStep(1.12f,1.6f,static_cast<float>(LocalCamera.Length())):1.0f;
                AtmosphereMaterials[I]->SetScalarParameterValue(TEXT("ViewSteps"),FMath::RoundToFloat(FMath::Lerp(24.0f,48.0f,Detail)));
                AtmosphereMaterials[I]->SetScalarParameterValue(TEXT("SunSteps"),FMath::RoundToFloat(FMath::Lerp(8.0f,16.0f,Detail)));
            }

        }
        if(Body.id=="saturn")
        {
            RingMesh->SetWorldLocationAndRotation(Center,Rotation);
            RingMesh->SetWorldScale3D(FVector(RenderRadius));
            SetParameters(RingMaterial);
        }
    }
    StarMesh->SetWorldLocation(FStarDataCatalog::UEVector(star::ToUnrealCentimeters(Camera,Origin)));
    StarMesh->SetWorldScale3D(FVector(MaximumProxyDistanceM*8*100));
    if(bActiveCameraUpdate&&EnvironmentUpdateFrame!=GFrameCounter)
    {
        EnvironmentUpdateFrame=GFrameCounter;
        const double RealDt=FMath::Clamp(double(GetWorld()->GetDeltaSeconds()),0.0,0.25);
        EnvironmentClock+=RealDt;
        if(bEnvironmentCaptureDiagnostic&&!bEnvironmentCaptureRequested&&EnvironmentClock>=5.0)
            CaptureEnvironmentDiagnostic(Origin,Camera);
        if(bLocalEnvironment) UpdateLocalEnvironment(Origin,Camera,RealDt);
    }
}
void AStarWorldDirector::CaptureEnvironmentDiagnostic(const star::Vec3d& Origin,const star::Vec3d& Camera)
{
    // Raster capture includes the actual current illuminated procedural planet
    // and terrain, which do not have software-Lumen mesh cards/distance fields.
    // Direct sunlight remains the directional light; exclude its visible disk
    // and the exposure-compensated decorative stars to avoid double energy.
    StarMesh->bVisibleInReflectionCaptures=false;StarMesh->MarkRenderStateDirty();
    for(int32 I=0;I<Data.Bodies().Num();++I)
        if(Data.Bodies()[I].Definition.id=="sun")
        {BodyMeshes[I]->bVisibleInReflectionCaptures=false;BodyMeshes[I]->MarkRenderStateDirty();}
    if(FParse::Param(FCommandLine::Get(),TEXT("StarEnvironmentEmpty")))
    {
        // Negative control: capture black space while retaining the normal
        // visible scene. Unit intensity must not invent any ambient energy.
        TInlineComponentArray<UPrimitiveComponent*> Environment;
        GetComponents(Environment);
        for(auto* Part:Environment){Part->bVisibleInReflectionCaptures=false;Part->MarkRenderStateDirty();}
    }
    for(TActorIterator<AStarShipPawn> It(GetWorld());It;++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Parts;
        It->GetComponents(Parts);
        for(auto* Part:Parts){Part->bVisibleInReflectionCaptures=false;Part->MarkRenderStateDirty();}
    }
    auto* Light=SkyLight->GetLightComponent();
    SkyLight->SetActorLocation(FStarDataCatalog::UEVector(star::ToUnrealCentimeters(Camera,Origin)));
    Light->SourceType=SLS_CapturedScene;
    Light->bRealTimeCapture=false;
    Light->SkyDistanceThreshold=10.0f;
    Light->bCaptureEmissiveOnly=false;
    Light->bLowerHemisphereIsBlack=false;
    Light->SetLightColor(FLinearColor::White);
    Light->SetIntensity(bLocalEnvironment?0.0f:1.0f); // No old cubemap during a managed update.
    Light->RecaptureSky();
    bEnvironmentCaptureRequested=true;
    UE_LOG(LogTemp,Display,TEXT("STAR lookdev: single-pose environment recapture requested at %g seconds"),SceneTime);
}
void AStarWorldDirector::UpdateLocalEnvironment(const star::Vec3d& Origin,const star::Vec3d& Camera,double Dt)
{
    FString BodyId;
    double LargestRatio=0.1,Altitude=0;
    for(const auto& Record:Data.Bodies())
    {
        const auto& Body=Record.Definition;
        if(Body.id=="sun")continue;
        const auto Offset=Camera-Body.centerMeters;
        const double Distance=Offset.Length();
        const double Ratio=Body.radiusMeters/FMath::Max(1.0,Distance);
        if(Ratio<=LargestRatio)continue;
        LargestRatio=Ratio;BodyId=UTF8_TO_TCHAR(Body.id.c_str());
        Altitude=Distance-Body.radiusMeters;
        star::TerrainSample Ground;
        if(Body.landable&&Data.SampleTerrain(Body,Offset.Normalized(),Ground))Altitude-=Ground.heightMeters;
    }
    const double Speed=bHaveEnvironmentCamera&&Dt>0?(Camera-EnvironmentPreviousCamera).Length()/Dt:0;
    EnvironmentPreviousCamera=Camera;bHaveEnvironmentCamera=true;
    const bool bEligible=!BodyId.IsEmpty()&&(BodyId!=TEXT("moon")||Altitude>5000||TerrainReadyForLanding());
    const double Drift=(Camera-EnvironmentCapturePosition).Length();
    const bool bStillValid=bEligible&&BodyId==EnvironmentBody&&Drift<EnvironmentValidityMeters;
    auto* Light=SkyLight->GetLightComponent();
    if(bEnvironmentActive&&!bStillValid)
    {
        Light->SetIntensity(0);bEnvironmentActive=false;EnvironmentFade=0;
        UE_LOG(LogTemp,Display,TEXT("STAR environment cleared: previous=%s current=%s drift=%g"),*EnvironmentBody,*BodyId,Drift);
    }
    if(bEnvironmentPending)
    {
        USkyLightComponent::UpdateSkyCaptureContents(GetWorld());
        if(!bEnvironmentFenceIssued&&!USkyLightComponent::HasSkyCapturesToUpdate()&&Light->GetProcessedSkyTexture())
        {
            GetWorld()->SendAllEndOfFrameUpdates();
            EnvironmentFence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
            bEnvironmentFenceIssued=true;
        }
        if(bEnvironmentFenceIssued&&EnvironmentFence.IsFenceComplete())
        {
            bEnvironmentPending=false;bEnvironmentFenceIssued=false;
            bEnvironmentActive=bStillValid;EnvironmentFade=0;
            UE_LOG(LogTemp,Display,TEXT("STAR environment ready: body=%s accepted=%d seconds=%g"),*EnvironmentBody,bEnvironmentActive,EnvironmentClock-EnvironmentCaptureTime);
        }
    }
    if(bEnvironmentActive)
    {
        EnvironmentFade=FMath::Min(1.0,EnvironmentFade+Dt*2.0);
        const double Weight=EnvironmentFade*FMath::Clamp(1.0-Drift/EnvironmentValidityMeters,0.0,1.0);
        Light->SetIntensity(float(Weight));
    }
    EnvironmentStableSeconds=bEligible&&Speed<5.0?EnvironmentStableSeconds+Dt:0;
    if(!bEnvironmentActive&&!bEnvironmentPending&&EnvironmentStableSeconds>=3.0&&EnvironmentClock>=5.0)
    {
        EnvironmentBody=BodyId;EnvironmentCapturePosition=Camera;
        EnvironmentValidityMeters=FMath::Clamp(FMath::Max(0.0,Altitude)*0.25,500.0,10000.0);
        EnvironmentCaptureTime=EnvironmentClock;
        CaptureEnvironmentDiagnostic(Origin,Camera);
        bEnvironmentPending=true;EnvironmentStableSeconds=0;
        UE_LOG(LogTemp,Display,TEXT("STAR environment request: body=%s validity_m=%g"),*BodyId,EnvironmentValidityMeters);
    }
}
void AStarWorldDirector::UpdateTerrain(const star::FlightState& State,const star::Vec3d& Origin)
{
    if(LunarTerrain) LunarTerrain->UpdateTerrain(State.positionMeters,Origin);
    if(const auto* Earth=Data.Find(TEXT("earth")))EarthTerrain->UpdateTerrain(Earth->Definition,State.positionMeters,Origin);
}
bool AStarWorldDirector::TerrainReadyForLanding() const
{
    return LunarTerrain&&LunarTerrain->IsReadyForLanding();
}
bool AStarWorldDirector::RaySphere(const star::Vec3d& Start,const star::Vec3d& Direction,const star::Vec3d& Center,double Radius,double& T)
{
    const auto Offset=Center-Start;
    const double Along=star::Vec3d::Dot(Offset,Direction);
    const auto Perpendicular=Offset-Direction*Along;
    const double Discriminant=Radius*Radius-Perpendicular.LengthSquared();
    if(Discriminant<0) return false;
    const double Root=FMath::Sqrt(Discriminant);
    T=Along-Root;
    if(T<0) T=Along+Root;
    return T>=0;
}
FStarObservationSample AStarWorldDirector::Observe(const star::FlightState& State,const star::Vec3d& Camera,const star::Vec3d& Forward,const FString& Target,bool bScanning,bool bCockpit,double Dt,uint64 Sequence) const
{
    FStarObservationSample Sample;
    Sample.Sequence=Sequence; Sample.DeltaSeconds=Dt; Sample.BodyId=Target;
    Sample.bScanning=bScanning; Sample.bCockpitView=bCockpit;
    Sample.SpeedMps=State.velocityMetersPerSecond.Length();
    Sample.bLanded=State.mode==star::FlightMode::Landed;
    const auto* Body=Data.Find(Target); const auto* Sun=Data.Find(TEXT("sun"));
    if(!Body||!Sun) return Sample;
    const auto Offset=State.positionMeters-Body->Definition.centerMeters;
    const auto BodyLocal=Body->Definition.bodyFixedToSimulation.Conjugate().Rotate(Offset);
    const auto Radial=Offset.Normalized();
    Sample.AltitudeM=Offset.Length()-Body->Definition.radiusMeters;
    Sample.LatitudeDeg=FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(BodyLocal.Normalized().z,-1.0,1.0)));
    Sample.LongitudeDeg=FMath::RadiansToDegrees(FMath::Atan2(BodyLocal.y,BodyLocal.x));
    Sample.VerticalSpeedMps=star::Vec3d::Dot(State.velocityMetersPerSecond,Radial);
    if(Body->Definition.landable) Sample.AltitudeM-=Data.MoonHeight(Sample.LatitudeDeg,Sample.LongitudeDeg);
    double Hit=0;
    bool bHit=false;
    if(Body->Definition.landable)
    {
        const auto Terrain=[this](const star::BodyDefinition& Definition,const star::Vec3d& Direction,star::TerrainSample& Out)
        { return Data.SampleTerrain(Definition,Direction,Out); };
        const auto SurfaceHit=star::RaycastObservationSurface(Body->Definition,Camera,Forward,Terrain);
        bHit=SurfaceHit.status==star::ObservationRayStatus::Hit;
        if(bHit) Hit=SurfaceHit.distanceMeters;
    }
    else bHit=RaySphere(Camera,Forward,Body->Definition.centerMeters,Body->Definition.radiusMeters,Hit);
    if(bHit)
    {
        Sample.bViewingBody=true;
        const auto Point=Camera+Forward*Hit;
        Sample.SunIllumination=star::Vec3d::Dot((Point-Body->Definition.centerMeters).Normalized(),(Sun->Definition.centerMeters-Point).Normalized());
    }
    if(Target==TEXT("saturn"))
    {
        const auto StartLocal=Body->Definition.bodyFixedToSimulation.Conjugate().Rotate(Camera-Body->Definition.centerMeters);
        const auto DirLocal=Body->Definition.bodyFixedToSimulation.Conjugate().Rotate(Forward);
        if(FMath::Abs(DirLocal.z)>1e-9)
        {
            const double PlaneHit=-StartLocal.z/DirLocal.z;
            const auto RingPoint=StartLocal+DirLocal*PlaneHit;
            const double Radius=FMath::Sqrt(RingPoint.x*RingPoint.x+RingPoint.y*RingPoint.y);
            if(PlaneHit>0&&Radius>=Data.RingInnerRadiusMeters()&&Radius<=Data.RingOuterRadiusMeters()&&(!Sample.bViewingBody||PlaneHit<Hit))
            {
                Sample.bViewingRings=true; Sample.RingRadiusM=Radius;
                const auto Point=Body->Definition.centerMeters+Body->Definition.bodyFixedToSimulation.Rotate(RingPoint);
                const auto SunDirection=(Sun->Definition.centerMeters-Point).Normalized();
                double ShadowHit=0;
                Sample.bObservedPointInPlanetShadow=RaySphere(Point,SunDirection,Body->Definition.centerMeters,Body->Definition.radiusMeters,ShadowHit);
            }
        }
    }
    return Sample;
}
bool AStarWorldDirector::SetWorldUtc(double UnixSeconds)
{
    if(!Data.SetAstronomicalUtc(UnixSeconds)) return false;
    AstronomicalUtc=UnixSeconds;return true;
}
FString AStarWorldDirector::EarthSurfaceStatus() const {return EarthTerrain?EarthTerrain->StatusText():FString();}
void AStarWorldDirector::SetTwilightDream(bool Enabled,bool Sunset)
{
    Enabled=false;Sunset=false;bDreamTwilight=false;bDreamSunset=false;
    if(!PostProcess) return;
    auto& Settings=PostProcess->Settings;
    Settings.BloomIntensity=Enabled?(Sunset?1.15f:0.95f):0.18f;
    Settings.VignetteIntensity=0.0f;
    Settings.bOverride_BloomThreshold=Enabled;
    Settings.BloomThreshold=1.0f;
    Settings.bOverride_LensFlareIntensity=Enabled;
    Settings.LensFlareIntensity=0.0f;
}

void AStarWorldDirector::SetExposure(float Value)
{
    if(PostProcess) PostProcess->Settings.AutoExposureBias=FMath::Clamp(Value,-5.0f,5.0f);
}
void AStarWorldDirector::SetEnhancedStars(bool Enabled)
{
    Enabled=false;bEnhancedStars=false;
    if(!StarMaterial) return;
    StarMaterial->SetScalarParameterValue(TEXT("EnhancedStars"),bEnhancedStars?1.0f:0.0f);
    // Keep the calibrated photo intensity intact while disabled so a later
    // toggle is reversible and does not mutate the original catalogue path.
    StarMaterial->SetScalarParameterValue(TEXT("PhotoSkyIntensity"),PhotoSkyIntensity);
}
bool AStarWorldDirector::GetRenderedBodyCenter(const FString& BodyId, FVector& OutWorldCm) const
{
    OutWorldCm=FVector::ZeroVector;
    if(!bReady) return false;
    for(int32 I=0;I<Data.Bodies().Num();++I)
    {
        const auto& Body=Data.Bodies()[I];
        if(FString(UTF8_TO_TCHAR(Body.Definition.id.c_str()))!=BodyId) continue;
        const auto* Mesh=BodyMeshes.IsValidIndex(I)?BodyMeshes[I].Get():nullptr;
        if(!IsValid(Mesh)) return false;
        OutWorldCm=Mesh->GetComponentLocation();
        return !OutWorldCm.ContainsNaN();
    }
    return false;
}
star::Vec3d AStarWorldDirector::SafeCameraPosition(const star::Vec3d& Desired) const
{
    const auto* Moon=Data.Find(TEXT("moon"));
    if(!Moon) return Desired;
    const auto Offset=Desired-Moon->Definition.centerMeters;
    if(Offset.Length()>LunarRadiusM+20000) return Desired;
    star::TerrainSample Sample;
    if(!Data.SampleTerrain(Moon->Definition,Offset.Normalized(),Sample)) return Desired;
    const double Radius=LunarRadiusM+Sample.heightMeters+1.5;
    return Offset.Length()<Radius?Moon->Definition.centerMeters+Offset.Normalized()*Radius:Desired;
}
