#include "Terrain/StarEarthTerrainComponent.h"
#include "Terrain/StarRemoteRaster.h"
#include "Simulation/SolarLighting.h"
#include "Runtime/StarDataCatalog.h"
#include "Runtime/StarDiagnostics.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Math/Float16Color.h"
#include "Async/Async.h"
#include "HttpModule.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"

namespace {
// Coverage is a binary footprint, independent of the HDR observation resolution.
constexpr int AtlasTile=512;
constexpr int ImagerySize=1536;
star::Vec3d Direction(double Lat,double Lon)
{Lat*=UE_DOUBLE_PI/180;Lon*=UE_DOUBLE_PI/180;return {cos(Lat)*cos(Lon),cos(Lat)*sin(Lon),sin(Lat)};}
FVector V(const star::Vec3d& P){return FVector(P.x,P.y,P.z);}
FString GeoName(int Lat,int Lon)
{return FString::Printf(TEXT("%s%02d%s%03d"),Lat<0?TEXT("S"):TEXT("N"),FMath::Abs(Lat),Lon<0?TEXT("W"):TEXT("E"),FMath::Abs(Lon));}
struct FTile
{
    int Lat=0,Lon=0;bool Ready=false,Attempted=false;
    star::Vec3d Anchor;
    FStarRemoteRaster Height,Color;
    TArray<uint8> Mask,WaterMask,LandMask;
    TArray<FFloat16Color> DisplayColors;
    TArray<FVector> Vertices,Normals;
    TArray<FVector2D> UV;
    TArray<int32> Triangles;
    TArray<FLinearColor> VertexColors;
    int LandPixels=0;
};
bool PrepareDisplay(FTile& T,const std::atomic<bool>& Cancel)
{
    StarDiagnostics::FScope Scope(TEXT("terrain_prepare_pixels"));
    const auto& C=T.Color;const auto* RGB=reinterpret_cast<const uint16*>(C.Samples.GetData());
    T.DisplayColors.SetNumUninitialized(C.Width*C.Height);T.Mask.SetNumZeroed(AtlasTile*AtlasTile);
    for(int Y=0;Y<C.Height;++Y){
        if(Cancel.load())return false;
        for(int X=0;X<C.Width;++X){
            const int P=Y*C.Width+X;const bool Valid=RGB[P*4]||RGB[P*4+1]||RGB[P*4+2];
            T.DisplayColors[P]=FFloat16Color(FLinearColor(RGB[P*4]*0.0001f,RGB[P*4+1]*0.0001f,RGB[P*4+2]*0.0001f,Valid&&T.LandMask[P]?1:0));
            T.LandPixels+=Valid&&T.LandMask[P]?1:0;
        }
    }
    for(int Y=0;Y<AtlasTile;++Y){
        if(Cancel.load())return false;
        for(int X=0;X<AtlasTile;++X){
            // Only hide the globe where the entire coarse texel has valid detail.
            // Include a one-pixel border for the imagery's bilinear filter.
            bool Covered=true;
            for(int SY=FMath::Max(0,Y*C.Height/AtlasTile-1);SY<FMath::Min(C.Height,(Y+1)*C.Height/AtlasTile+2)&&Covered;++SY)
                for(int SX=FMath::Max(0,X*C.Width/AtlasTile-1);SX<FMath::Min(C.Width,(X+1)*C.Width/AtlasTile+2);++SX){
                    const int P=SY*C.Width+SX;
                    if(!(T.WaterMask[P]||((RGB[P*4]||RGB[P*4+1]||RGB[P*4+2])&&T.LandMask[P]))){Covered=false;break;}
                }
            T.Mask[Y*AtlasTile+X]=Covered?255:0;
        }
    }
    T.Color.Samples.Empty();T.LandMask.Empty();return true;
}
bool PrepareGeometry(FTile& T,double Radius,int Grid,const std::atomic<bool>& Cancel)
{
    StarDiagnostics::FScope Scope(TEXT("terrain_prepare_geometry"));
    const auto& D=T.Height;
    const float* Heights=reinterpret_cast<const float*>(D.Samples.GetData());
    for(int P=0;P<D.Width*D.Height;++P)if(!FMath::IsFinite(Heights[P])||Heights[P]<=-500||Heights[P]>10000){return false;}

    const auto Height=[&](double U,double VV){
        // The native Copernicus raster is pixel-is-point; overview pixels are
        // centers of aggregated native samples, not the north-west corner.
        double X=FMath::Clamp(U*D.Width-0.5+0.5*D.Width/D.NativeWidth,0.0,double(D.Width-1));
        double Y=FMath::Clamp(VV*D.Height-0.5+0.5*D.Height/D.NativeHeight,0.0,double(D.Height-1));
        int X0=int(X),Y0=int(Y),X1=FMath::Min(X0+1,D.Width-1),Y1=FMath::Min(Y0+1,D.Height-1);
        return FMath::Lerp(FMath::Lerp(double(Heights[Y0*D.Width+X0]),double(Heights[Y0*D.Width+X1]),X-X0),FMath::Lerp(double(Heights[Y1*D.Width+X0]),double(Heights[Y1*D.Width+X1]),X-X0),Y-Y0);
    };
    T.Anchor=Direction(T.Lat+0.5,T.Lon+0.5)*Radius;
    auto& Vertices=T.Vertices;auto& Normals=T.Normals;auto& UV=T.UV;
    auto& Triangles=T.Triangles;auto& VertexColors=T.VertexColors;
    const int Count=(Grid+1)*(Grid+1);
    Vertices.Reserve(Count);Normals.Reserve(Count);UV.Reserve(Count);VertexColors.Reserve(Count);Triangles.Reserve(Grid*Grid*6);
    for(int Y=0;Y<=Grid;++Y)for(int X=0;X<=Grid;++X)
    {
        if(Cancel.load())return false;
        double U=double(X)/Grid,VV=double(Y)/Grid;
        auto Dir=Direction(T.Lat+1-VV,T.Lon+U);
        auto P=Dir*(Radius+Height(U,VV));
        Vertices.Add(V(star::SimulationDirectionToUnreal((P-T.Anchor)*100)));UV.Add(FVector2D(U,VV));Normals.Add(FVector::ZeroVector);
    }
    for(int Y=0;Y<Grid;++Y)for(int X=0;X<Grid;++X)
    {int A=Y*(Grid+1)+X,B=A+1,Cc=A+Grid+1,E=Cc+1;Triangles.Append({A,Cc,B,B,Cc,E});}
    for(int I=0;I<Triangles.Num();I+=3)
    {
        int A=Triangles[I],B=Triangles[I+1],Cc=Triangles[I+2];auto N=FVector::CrossProduct(Vertices[B]-Vertices[A],Vertices[Cc]-Vertices[A]);
        Normals[A]-=N;Normals[B]-=N;Normals[Cc]-=N;
    }
    for(auto& N:Normals)
    {
        N.Normalize();
        // Body-fixed measured slope normal, encoded explicitly rather than
        // depending on an unlit material's world tangent-basis permutations.
        VertexColors.Add(FLinearColor(0.5+N.X*0.5,0.5-N.Y*0.5,0.5+N.Z*0.5,1));
    }
    return !Cancel.load();
}
UTexture2D* Texture(int W,int H,EPixelFormat Format,const void* Bytes,int64 Size)
{
    auto* T=UTexture2D::CreateTransient(W,H,Format);if(!T)return nullptr;
    T->SRGB=false;T->NeverStream=true;T->AddressX=TA_Clamp;T->AddressY=TA_Clamp;
    auto& Bulk=T->GetPlatformData()->Mips[0].BulkData;
    void* Target=Bulk.Lock(LOCK_READ_WRITE);FMemory::Memcpy(Target,Bytes,Size);Bulk.Unlock();T->UpdateResource();return T;
}
}
struct FStarEarthTerrainState
{
    std::atomic<bool> Cancel{false};
    TFuture<TUniquePtr<FTile>> Job;
    TSharedPtr<std::atomic<bool>,ESPMode::ThreadSafe> JobCancel;
    double JobStarted=0;
    TArray<TUniquePtr<FTile>> Tiles;
    TArray<uint8> CoverageIndex;
    int South=0,West=0,JobIndex=-1;bool Located=false,Enabled=true,Near=false,Deferred=false;
    star::BodyDefinition Earth;
};
void FStarEarthTerrainDeleter::operator()(FStarEarthTerrainState* S) const {delete S;}
UStarEarthTerrainComponent::UStarEarthTerrainComponent(){State.Reset(new FStarEarthTerrainState);}
UStarEarthTerrainComponent::~UStarEarthTerrainComponent(){Shutdown();}
void UStarEarthTerrainComponent::EndPlay(const EEndPlayReason::Type R){Shutdown();Super::EndPlay(R);}
void UStarEarthTerrainComponent::Shutdown(){if(State){State->Cancel.store(true);if(State->JobCancel)State->JobCancel->store(true);
    // Workers capture their own cancellation token, never this component/state.
    // Teardown therefore does not wait on HTTP or a stale raster on the game thread.
    State->Job=TFuture<TUniquePtr<FTile>>();}}
void UStarEarthTerrainComponent::Initialize(UMaterialInterface* M)
{
    Material=M;State->Enabled=M&&!FParse::Param(FCommandLine::Get(),TEXT("StarOfflineEarth"));
    if(!FFileHelper::LoadFileToArray(State->CoverageIndex,*(FPaths::ProjectContentDir()/TEXT("Star/Data/earth-rgbnir-coverage.bin")))||State->CoverageIndex.Num()!=180*360)
        State->CoverageIndex.Empty();
    FHttpModule::Get();
}
bool UStarEarthTerrainComponent::FindSurveyDirection(const star::BodyDefinition& Earth,const star::Vec3d& Position,star::Vec3d& Forward) const
{
    if(State->CoverageIndex.Num()!=180*360)return false;
    const auto Local=Earth.bodyFixedToSimulation.Conjugate().Rotate(Position-Earth.centerMeters).Normalized();
    double Best=-2;star::Vec3d Target;
    for(int Lat=-85;Lat<85;++Lat)for(int Lon=-180;Lon<180;++Lon){
        if(!State->CoverageIndex[(Lat+90)*360+Lon+180])continue;
        const auto DirectionToCell=Direction(Lat+.5,Lon+.5);const double Score=star::Vec3d::Dot(Local,DirectionToCell);
        if(Score<=Best)continue;
        int Neighbors=0;
        for(int Y=-2;Y<=2;++Y)for(int X=-2;X<=2;++X)
            Neighbors+=State->CoverageIndex[(Lat+Y+90)*360+(Lon+X+540)%360]?1:0;
        // Prefer a broad covered region rather than a lone remote-island tile.
        if(Neighbors>=20){Best=Score;Target=DirectionToCell;}
    }
    const auto Tangent=(Target-Local*star::Vec3d::Dot(Local,Target)).Normalized();
    if(Best<-1||Tangent.LengthSquared()<.5)return false;
    Forward=Earth.bodyFixedToSimulation.Rotate(Tangent);return true;
}
void UStarEarthTerrainComponent::UpdateTerrain(const star::BodyDefinition& Earth,const star::Vec3d& Camera,const star::Vec3d& Origin,double SpeedMps)
{
    if(!State||!State->Enabled||State->Cancel.load())return;
    State->Earth=Earth;
    const auto Local=Earth.bodyFixedToSimulation.Conjugate().Rotate(Camera-Earth.centerMeters);
    const bool Near=star::EarthDetailInRange(Local.Length()-Earth.radiusMeters,State->Near);
    if(State->Near!=Near)++RenderRevision;
    State->Near=Near;
    State->Deferred=SpeedMps>30000;
    for(auto& M:Meshes)if(M)M->SetVisibility(Near);
    for(int I=0;I<Meshes.Num();++I)if(Meshes[I])
    {
        const auto Absolute=Earth.centerMeters+Earth.bodyFixedToSimulation.Rotate(State->Tiles[I]->Anchor);
        Meshes[I]->SetWorldLocationAndRotation(V(star::ToUnrealCentimeters(Absolute,Origin)),FStarDataCatalog::UERotation(Earth.bodyFixedToSimulation));
    }
    const auto N=Local.Normalized();const double Lat=asin(FMath::Clamp(N.z,-1.0,1.0))*180/UE_DOUBLE_PI,Lon=atan2(N.y,N.x)*180/UE_DOUBLE_PI;
    const int South=FMath::Clamp(FMath::FloorToInt(Lat)-1,-90,87),West=FMath::FloorToInt(Lon)-1;
    if(State->Job.IsValid()&&State->JobCancel&&!State->JobCancel->load()&&State->Tiles.IsValidIndex(State->JobIndex)){
        const auto& Tile=*State->Tiles[State->JobIndex];
        const int LongitudeOffset=(Tile.Lon-West+720)%360;
        if(!Near||State->Deferred||Tile.Lat<South||Tile.Lat>South+2||LongitudeOffset>=3){
            State->JobCancel->store(true);
            StarDiagnostics::Event(TEXT("terrain_cancel"),GeoName(Tile.Lat,Tile.Lon)+(Near?TEXT(": superseded region"):TEXT(": left detail range")));
        }
    }
    if(State->Job.IsValid()&&State->Job.IsReady())
    {
        auto Result=State->Job.Consume();const int Index=State->JobIndex;State->JobIndex=-1;
        const bool Cancelled=State->JobCancel&&State->JobCancel->load();State->JobCancel.Reset();
        if(Cancelled&&State->Tiles.IsValidIndex(Index))State->Tiles[Index]->Attempted=false;
        if(!Cancelled&&Result&&State->Tiles.IsValidIndex(Index))
        {
            State->Tiles[Index]=MoveTemp(Result);
            if(State->Tiles[Index]->Ready)Upload(Index);
            else UE_LOG(LogTemp,Warning,TEXT("STAR Earth tile %s unavailable: %s / %s; global fallback retained"),
                *GeoName(State->Tiles[Index]->Lat,State->Tiles[Index]->Lon),*State->Tiles[Index]->Height.Error,*State->Tiles[Index]->Color.Error);
        }
    }
    if(!Near||State->Deferred)return;
    if(!State->Located||South!=State->South||West!=State->West)
    {
        // Finish the single bounded read before shifting the 3x3 window. No concurrent writers.
        if(State->Job.IsValid())return;
        auto PreviousTiles=MoveTemp(State->Tiles);auto PreviousMeshes=MoveTemp(Meshes);
        auto PreviousImages=MoveTemp(Images);auto PreviousMasks=MoveTemp(WaterMasks);auto PreviousWater=MoveTemp(WaterMaterials);
        State->South=South;State->West=West;State->Located=true;Coverage=nullptr;
        Meshes.SetNum(9);Images.SetNum(9);WaterMasks.SetNum(9);WaterMaterials.SetNum(9);
        for(int I=0;I<9;++I)
        {
            const int NewLat=South+2-I/3,NewLon=(West+I%3+540)%360-180;
            int Match=INDEX_NONE;
            for(int J=0;J<PreviousTiles.Num();++J)if(PreviousTiles[J]&&PreviousTiles[J]->Lat==NewLat&&PreviousTiles[J]->Lon==NewLon){Match=J;break;}
            if(Match!=INDEX_NONE)
            {
                State->Tiles.Add(MoveTemp(PreviousTiles[Match]));Meshes[I]=PreviousMeshes[Match];PreviousMeshes[Match]=nullptr;
                Images[I]=PreviousImages[Match];WaterMasks[I]=PreviousMasks[Match];WaterMaterials[I]=PreviousWater[Match];
            }
            else {auto T=MakeUnique<FTile>();T->Lat=NewLat;T->Lon=NewLon;State->Tiles.Add(MoveTemp(T));}
        }
        for(auto& M:PreviousMeshes)if(M)M->DestroyComponent();
        RefreshCoverage();
    }
    for(int I=0;I<9;++I)if(Meshes[I])
    {
        const auto Absolute=Earth.centerMeters+Earth.bodyFixedToSimulation.Rotate(State->Tiles[I]->Anchor);
        Meshes[I]->SetWorldLocationAndRotation(V(star::ToUnrealCentimeters(Absolute,Origin)),FStarDataCatalog::UERotation(Earth.bodyFixedToSimulation));
    }
    if(!State->Job.IsValid())
    {
        // Central footprint first, then its adjacent tiles. Missing ocean or polar products stay absent.
        for(int I:{4,1,3,5,7,0,2,6,8})if(!State->Tiles[I]->Attempted)
        {
            const auto& Tile=*State->Tiles[I];
            if(State->CoverageIndex.Num()==180*360&&!State->CoverageIndex[(Tile.Lat+90)*360+Tile.Lon+180]){
                State->Tiles[I]->Attempted=true;continue;
            }
            State->Tiles[I]->Attempted=true;State->JobIndex=I;
            const int TileLat=State->Tiles[I]->Lat,TileLon=State->Tiles[I]->Lon;
            const auto JobCancel=MakeShared<std::atomic<bool>,ESPMode::ThreadSafe>(false);State->JobCancel=JobCancel;
            State->JobStarted=FPlatformTime::Seconds();StarDiagnostics::Event(TEXT("terrain_request"),GeoName(TileLat,TileLon));
            const FString Cache=FPaths::ProjectSavedDir()/TEXT("EarthCache/v1");
            const double EarthRadius=Earth.radiusMeters;const int Grid=I==4?256:128;
            State->Job=Async(EAsyncExecution::ThreadPool,[JobCancel,TileLat,TileLon,Cache,EarthRadius,Grid](){
                auto T=MakeUnique<FTile>();T->Lat=TileLat;T->Lon=TileLon;T->Attempted=true;
                const FString Name=GeoName(TileLat,TileLon);
                StarDiagnostics::FScope DiagnosticScope(*(TEXT("terrain_download:")+Name));
                const FString Dem=FString::Printf(TEXT("Copernicus_DSM_COG_10_%s%02d_00_%s%03d_00_DEM"),TileLat<0?TEXT("S"):TEXT("N"),FMath::Abs(TileLat),TileLon<0?TEXT("W"):TEXT("E"),FMath::Abs(TileLon));
                const FString ColorUrl=FString::Printf(TEXT("https://esa-worldcover-s2.s3.eu-central-1.amazonaws.com/rgbnir/2021/%s/ESA_WorldCover_10m_2021_v200_%s_S2RGBNIR.tif"),*Name.Left(3),*Name);
                // Year is the source acquisition year for imagery; the DSM is the 2021 release of 2011-2015 observations.
                T->Ready=T->Color.Load(ColorUrl,ImagerySize,Cache,*JobCancel)&&T->Height.Load(TEXT("https://copernicus-dem-30m.s3.amazonaws.com/")+Dem+TEXT("/")+Dem+TEXT(".tif"),1800,Cache,*JobCancel);
                T->Ready=T->Ready&&T->Color.Bits==16&&T->Color.Channels==4&&T->Height.Floating&&T->Height.Bits==32&&T->Height.Channels==1;
                if(T->Ready)
                {
                    // Categorical water product derived from the same Sentinel-2 source family.
                    const int ClassLat=FMath::FloorToInt(TileLat/3.0)*3,ClassLon=FMath::FloorToInt(TileLon/3.0)*3;
                    const FString ClassUrl=TEXT("https://esa-worldcover.s3.eu-central-1.amazonaws.com/v200/2021/map/ESA_WorldCover_10m_2021_v200_")+GeoName(ClassLat,ClassLon)+TEXT("_Map.tif");
                    FStarRemoteRaster Classes;
                    T->Ready=Classes.Load(ClassUrl,9000,Cache,*JobCancel)&&Classes.Bits==8&&Classes.Channels==1;
                    if(T->Ready)
                    {
                        const int W=T->Color.Width,H=T->Color.Height;T->WaterMask.SetNumZeroed(W*H);T->LandMask.SetNumZeroed(W*H);
                        for(int Y=0;Y<H&&!JobCancel->load();++Y)for(int X=0;X<W;++X)
                        {
                            const double Longitude=TileLon+(X+0.5)/W,Latitude=TileLat+1-(Y+0.5)/H;
                            const int CX=FMath::Clamp(int((Longitude-ClassLon)/3*Classes.Width),0,Classes.Width-1);
                            const int CY=FMath::Clamp(int((ClassLat+3-Latitude)/3*Classes.Height),0,Classes.Height-1);
                            const uint8 Label=Classes.Samples[CY*Classes.Width+CX];
                            T->WaterMask[Y*W+X]=Label==80?255:0;
                            T->LandMask[Y*W+X]=(Label>=10&&Label<=100&&Label!=80)?255:0;
                        }
                    }
                    else T->Color.Error=TEXT("Measured water classification unavailable; keep global fallback");
                }
                T->Ready=T->Ready&&!JobCancel->load();
                if(T->Ready)T->Ready=PrepareDisplay(*T,*JobCancel)&&PrepareGeometry(*T,EarthRadius,Grid,*JobCancel);
                StarDiagnostics::Event(T->Ready?TEXT("terrain_download_ready"):JobCancel->load()?TEXT("terrain_download_cancelled"):TEXT("terrain_download_failed"),Name+TEXT(" ")+T->Color.Error+TEXT(" ")+T->Height.Error);
                return T;
            });break;
        }
    }
}
void UStarEarthTerrainComponent::Upload(int32 Index)
{
    StarDiagnostics::FScope DiagnosticScope(TEXT("terrain_gpu_upload"));
    auto& T=*State->Tiles[Index];const auto& C=T.Color;const auto& D=T.Height;
    const auto& Colors=T.DisplayColors;const int LandPixels=T.LandPixels;
    Images[Index]=Texture(C.Width,C.Height,PF_FloatRGBA,Colors.GetData(),Colors.Num()*sizeof(FFloat16Color));
    if(!Images[Index]){T.Ready=false;return;}
    WaterMasks[Index]=Texture(C.Width,C.Height,PF_G8,T.WaterMask.GetData(),T.WaterMask.Num());
    if(!WaterMasks[Index]){T.Ready=false;return;}
    const auto& Vertices=T.Vertices;const auto& Normals=T.Normals;const auto& UV=T.UV;
    const auto& Triangles=T.Triangles;const auto& VertexColors=T.VertexColors;TArray<FProcMeshTangent> Tangents;
    auto* Mesh=NewObject<UProceduralMeshComponent>(GetOwner());GetOwner()->AddInstanceComponent(Mesh);Mesh->SetupAttachment(this);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(true);Mesh->RegisterComponent();
    Mesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,VertexColors,Tangents,false,false);
    Meshes[Index]=Mesh;
    auto* WaterBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Star/Materials/MI_Earth.MI_Earth"));
    WaterMaterials[Index]=UMaterialInstanceDynamic::Create(WaterBase,this);
    Mesh->SetMaterial(0,WaterMaterials[Index]);
    UE_LOG(LogTemp,Display,TEXT("STAR observed Earth terrain committed: %s source DEM=%dx%d imagery=%dx%d vertices=%d; archived 2021 imagery; EGM2008 DSM; mean-radius render datum"),*GeoName(T.Lat,T.Lon),D.Width,D.Height,C.Width,C.Height,Vertices.Num());
    int WaterPixels=0;for(uint8 Value:T.WaterMask)WaterPixels+=Value?1:0;
    UE_LOG(LogTemp,Display,TEXT("STAR classified water: %s WorldCover v200/2021 class 80, water pixels %d/%d; shared global water BRDF"),*GeoName(T.Lat,T.Lon),WaterPixels,T.WaterMask.Num());
    UE_LOG(LogTemp,Display,TEXT("STAR observed land pixels: %s %d/%d"),*GeoName(T.Lat,T.Lon),LandPixels,C.Width*C.Height);
    T.Height.Samples.Empty();T.DisplayColors.Empty();T.WaterMask.Empty();
    T.Vertices.Empty();T.Normals.Empty();T.UV.Empty();T.Triangles.Empty();T.VertexColors.Empty();RefreshCoverage();
}
void UStarEarthTerrainComponent::RefreshCoverage()
{
    StarDiagnostics::FScope DiagnosticScope(TEXT("terrain_coverage_upload"));
    ++RenderRevision;
    if(!Meshes.ContainsByPredicate([](const auto& M){return M!=nullptr;}))return;
    const int W=AtlasTile*3;TArray<uint8> Pixels;Pixels.SetNumZeroed(W*W);
    for(int I=0;I<9;++I)if(Meshes[I]&&State->Tiles[I]->Mask.Num()==AtlasTile*AtlasTile)
        for(int Y=0;Y<AtlasTile;++Y)FMemory::Memcpy(Pixels.GetData()+((I/3)*AtlasTile+Y)*W+(I%3)*AtlasTile,State->Tiles[I]->Mask.GetData()+Y*AtlasTile,AtlasTile);
    if(!Coverage){Coverage=Texture(W,W,PF_G8,Pixels.GetData(),Pixels.Num());if(Coverage)Coverage->Filter=TF_Nearest;}
    else {
        auto* Region=new FUpdateTextureRegion2D(0,0,0,0,W,W);
        auto* Bytes=new uint8[Pixels.Num()];FMemory::Memcpy(Bytes,Pixels.GetData(),Pixels.Num());
        Coverage->UpdateTextureRegions(0,1,Region,W,1,Bytes,[](uint8* Data,const FUpdateTextureRegion2D* Regions){delete[] Data;delete Regions;});
    }
}
void UStarEarthTerrainComponent::ApplyGlobeCoverage(UMaterialInstanceDynamic* Globe)
{
    if(!Globe)return;
    const bool Active=Coverage&&Meshes.ContainsByPredicate([](const auto& M){return M&&M->IsVisible();});
    Globe->SetScalarParameterValue(TEXT("EarthSurfaceEnabled"),Active?1:0);
    if(Active){Globe->SetTextureParameterValue(TEXT("EarthSurfaceMask"),Coverage);Globe->SetVectorParameterValue(TEXT("EarthSurfaceBounds"),FLinearColor(State->West,State->South,3,3));}
    for(int I=0;I<WaterMaterials.Num();++I)if(WaterMaterials[I])
    {
        auto* W=WaterMaterials[I].Get();
        if(W->K2_GetScalarParameterValue(TEXT("TerrainWaterOnly"))<0.5f)W->CopyMaterialUniformParameters(Globe);
        for(const TCHAR* P:{TEXT("SunDirectionLocal"),TEXT("CameraLocal"),TEXT("OccluderLocal"),TEXT("BodyAxes")})
            W->SetVectorParameterValue(P,Globe->K2_GetVectorParameterValue(P));
        for(const TCHAR* P:{TEXT("SunRadiance"),TEXT("RadiusMeters"),TEXT("OccluderRadius"),TEXT("SunAngularRadius"),TEXT("NightIntensity")})
            W->SetScalarParameterValue(P,Globe->K2_GetScalarParameterValue(P));
        W->SetScalarParameterValue(TEXT("EarthSurfaceEnabled"),0);
        W->SetScalarParameterValue(TEXT("TerrainWaterOnly"),1);
        W->SetVectorParameterValue(TEXT("TerrainPatchBounds"),FLinearColor(State->Tiles[I]->Lon,State->Tiles[I]->Lat,1,1));
        W->SetTextureParameterValue(TEXT("TerrainWaterMask"),WaterMasks[I]);
        W->SetTextureParameterValue(TEXT("ObservedColor"),Images[I]);
        const auto Axis=[&](star::Vec3d A){auto P=star::SimulationDirectionToUnreal(State->Earth.bodyFixedToSimulation.Rotate(A));return FLinearColor(P.x,P.y,P.z,1);};
        W->SetVectorParameterValue(TEXT("PatchAxisXUE"),Axis({1,0,0}));W->SetVectorParameterValue(TEXT("PatchAxisYUE"),Axis({0,1,0}));W->SetVectorParameterValue(TEXT("PatchAxisZUE"),Axis({0,0,1}));
        W->SetScalarParameterValue(TEXT("CloudOpacity"),0);W->SetScalarParameterValue(TEXT("CloudCoverage"),0);
        // The observed patch retains the same dated city-light layer as the globe.
    }
    static TWeakObjectPtr<UTexture2D> Last;
    if(Active&&Last.Get()!=Coverage.Get())
    {
        const auto B=Globe->K2_GetVectorParameterValue(TEXT("EarthSurfaceBounds"));
        const auto S=Globe->K2_GetVectorParameterValue(TEXT("SunDirectionLocal"));
        UE_LOG(LogTemp,Display,TEXT("STAR Earth surface light: radiance=%g sunLocal=%g,%g,%g axes=%g"),Globe->K2_GetScalarParameterValue(TEXT("SunRadiance")),S.R,S.G,S.B,Globe->K2_GetVectorParameterValue(TEXT("BodyAxes")).R);
        UE_LOG(LogTemp,Display,TEXT("STAR Earth coverage: enabled=%g bounds=%g,%g,%g textureBound=%d blend=%d"),Globe->K2_GetScalarParameterValue(TEXT("EarthSurfaceEnabled")),B.R,B.G,B.B,Globe->K2_GetTextureParameterValue(TEXT("EarthSurfaceMask"))==Coverage.Get(),int(Globe->GetBlendMode()));
        bool NightMatches=true;
        for(const auto& Patch:WaterMaterials)if(Patch)NightMatches &= FMath::IsNearlyEqual(Patch->K2_GetScalarParameterValue(TEXT("NightIntensity")),Globe->K2_GetScalarParameterValue(TEXT("NightIntensity")));
        UE_LOG(LogTemp,Display,TEXT("STAR shared Earth night layer: intensity=%g patchesMatch=%d"),Globe->K2_GetScalarParameterValue(TEXT("NightIntensity")),NightMatches);
        Last=Coverage;
    }
}
FString UStarEarthTerrainComponent::StatusText() const
{
    if(!State||!State->Near)return FString();
    if(!State->Enabled)return TEXT("地表：全球地図（オフライン）");
    if(State->Deferred)return TEXT("高速航行中 · 全球地球画像を表示（減速後に詳細を先読み）");
    const bool Detailed=Meshes.ContainsByPredicate([](const auto& M){return M&&M->IsVisible();});
    if(State->Job.IsValid()){
        if(FPlatformTime::Seconds()-State->JobStarted>10)return TEXT("地表の読込に時間がかかっています · 操縦できます");
        return Detailed?TEXT("2021年の詳細地表 · 周辺を読み込み中（操縦できます）"):TEXT("地表を読み込み中 · 全球地図で操縦できます");
    }
    if(Detailed)return TEXT("詳細地表：2021年観測画像・実測地形 ／ 雲なし表示");
    if(State->CoverageIndex.Num()==180*360){
        bool Covered=false;
        for(const auto& T:State->Tiles)Covered|=State->CoverageIndex[(T->Lat+90)*360+T->Lon+180]!=0;
        if(!Covered)return TEXT("全球地球を表示中 · この海域・地域は詳細画像の提供範囲外");
    }
    return TEXT("詳細画像は未取得 · 全球地球を表示中");
}
