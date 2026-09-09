#include "Runtime/StarDataCatalog.h"
#include "Simulation/EarthFlight.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
TSharedPtr<FJsonObject> ReadJson(const FString& Name)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Star/Data") / Name))) return nullptr;
    TSharedPtr<FJsonObject> Json;
    const auto Reader = TJsonReaderFactory<>::Create(Text);
    return FJsonSerializer::Deserialize(Reader, Json) ? Json : nullptr;
}
double Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Default)
{
    double Value = Default;
    if (Object.IsValid()) Object->TryGetNumberField(Name, Value);
    return FMath::IsFinite(Value) ? Value : Default;
}
bool ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, star::Vec3d& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object.IsValid() || !Object->TryGetArrayField(Name, Values) || Values->Num() != 3) return false;
    Out = { (*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber() };
    return Out.IsFinite();
}
star::Quatd ReadOrientation(const TSharedPtr<FJsonObject>& Object)
{
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Object->TryGetArrayField(TEXT("bodyFixedToEclipticJ2000"), Rows)) return {};
    double M[9] = {1,0,0,0,1,0,0,0,1};
    if (Rows->Num() == 3 && (*Rows)[0]->Type == EJson::Array)
    {
        for (int32 Row = 0; Row < 3; ++Row)
        {
            const auto& Values = (*Rows)[Row]->AsArray();
            if (Values.Num() != 3) return {};
            for (int32 Col = 0; Col < 3; ++Col) M[Row * 3 + Col] = Values[Col]->AsNumber();
        }
    }
    else if (Rows->Num() == 9) for (int32 I = 0; I < 9; ++I) M[I] = (*Rows)[I]->AsNumber();
    else return {};
    return star::Quatd::FromForwardUp({M[0],M[3],M[6]}, {M[2],M[5],M[8]});
}
double Smooth01(double X) { X = FMath::Clamp(X, 0.0, 1.0); return X * X * (3.0 - 2.0 * X); }
}

bool FStarDataCatalog::Load(FString& OutError)
{
    bReady = false;
    BodyRecords.Reset();
    const auto Json = ReadJson(TEXT("bodies.json"));
    if (!Json.IsValid()) { OutError = TEXT("天体データ bodies.json を読み込めません。"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* BodiesJson = nullptr;
    if (!Json->TryGetArrayField(TEXT("bodies"), BodiesJson)) { OutError = TEXT("天体データの形式が不正です。"); return false; }
    Json->TryGetStringField(TEXT("epoch"), Epoch);
    TSet<FString> Seen;
    for (const auto& Value : *BodiesJson)
    {
        const auto BodyJson = Value->AsObject();
        if (!BodyJson.IsValid()) continue;
        FString Id;
        if (!BodyJson->TryGetStringField(TEXT("id"), Id) || Seen.Contains(Id)) { OutError = TEXT("天体IDが重複しています。"); return false; }
        Seen.Add(Id);
        FStarBodyRecord Body;
        Body.Definition.id = TCHAR_TO_UTF8(*Id);
        BodyJson->TryGetStringField(TEXT("nameJa"), Body.Name);
        BodyJson->TryGetStringField(TEXT("bodyFixedFrame"), Body.BodyFixedFrame);
        Body.Epoch = Epoch;
        Body.Definition.radiusMeters = Number(BodyJson, TEXT("radiusMeters"), 0);
        if (Body.Definition.radiusMeters <= 0 || !ReadVector(BodyJson, TEXT("positionMeters"), Body.Definition.centerMeters))
        { OutError = TEXT("天体の位置または半径が不正です。"); return false; }
        Body.Definition.bodyFixedToSimulation = ReadOrientation(BodyJson);
        Body.Definition.landable = Id == TEXT("moon");
        // This field is the non-landable collision shell, not the visual atmosphere height.
        Body.Definition.atmosphereHeightMeters = Id == TEXT("earth") ? star::EarthFlightFloorMeters : Id == TEXT("saturn") ? 150000.0 : 0.0;
        BodyRecords.Add(MoveTemp(Body));
    }
    if (!Find(TEXT("sun")) || !Find(TEXT("earth")) || !Find(TEXT("moon")) || !Find(TEXT("saturn")))
    { OutError = TEXT("太陽・地球・月・土星のデータが必要です。"); return false; }
    const auto RingData = ReadJson(TEXT("saturn_rings.json"));
    RingInner = Number(RingData,TEXT("innerRadiusMeters"),0);
    RingOuter = Number(RingData,TEXT("outerRadiusMeters"),0);
    if (RingInner <= Find(TEXT("saturn"))->Definition.radiusMeters || RingOuter <= RingInner)
    { OutError = TEXT("土星の環の半径データが不正です。"); return false; }
    TArray<uint8> Bytes;
    const FString DataDirectory = FPaths::ProjectContentDir() / TEXT("Star/Data");
    const auto GlobalMeta = ReadJson(TEXT("moon_ldem_16.json"));
    GlobalWidth = static_cast<int32>(Number(GlobalMeta, TEXT("width"), 5760));
    GlobalHeight = static_cast<int32>(Number(GlobalMeta, TEXT("height"), 2880));
    const int64 ExpectedBytes = static_cast<int64>(GlobalWidth) * GlobalHeight * sizeof(int16);
    if (GlobalWidth < 2 || GlobalHeight < 2 || ExpectedBytes > 1024ll * 1024 * 1024 ||
        !FFileHelper::LoadFileToArray(Bytes, *(DataDirectory / TEXT("moon_ldem_16_i16.bin"))) || Bytes.Num() != ExpectedBytes)
    { OutError = TEXT("月の標高データが見つからないか、サイズが不正です。"); return false; }
    GlobalHeights.SetNumUninitialized(GlobalWidth * GlobalHeight);
    FMemory::Memcpy(GlobalHeights.GetData(), Bytes.GetData(), Bytes.Num());
    MinimumHeight = 100000;
    MaximumHeight = -100000;
    for (const int16 Height : GlobalHeights)
    {
        MinimumHeight = FMath::Min(MinimumHeight, static_cast<double>(Height) * 0.5);
        MaximumHeight = FMath::Max(MaximumHeight, static_cast<double>(Height) * 0.5);
    }
    RegionalHeights.Reset();
    RegionalValid.Reset();
    const auto Region = ReadJson(TEXT("apollo17.json"));
    if (Region.IsValid())
    {
        RegionalWidth = static_cast<int32>(Number(Region, TEXT("width"), 0));
        RegionalHeightCount = static_cast<int32>(Number(Region, TEXT("height"), 0));
        RegionWest = Number(Region, TEXT("westLongitudeDegrees"), 0);
        RegionEast = Number(Region, TEXT("eastLongitudeDegrees"), 0);
        RegionNorth = Number(Region, TEXT("northLatitudeDegrees"), 0);
        RegionSouth = Number(Region, TEXT("southLatitudeDegrees"), 0);
        const int64 Count = static_cast<int64>(RegionalWidth) * RegionalHeightCount;
        if (Count > 0 && Count < 100000000 && RegionEast > RegionWest && RegionNorth > RegionSouth &&
            FFileHelper::LoadFileToArray(Bytes, *(DataDirectory / TEXT("apollo17_height_f32.bin"))) && Bytes.Num() == Count * sizeof(float))
        {
            RegionalHeights.SetNumUninitialized(static_cast<int32>(Count));
            FMemory::Memcpy(RegionalHeights.GetData(), Bytes.GetData(), Bytes.Num());
            FFileHelper::LoadFileToArray(RegionalValid, *(DataDirectory / TEXT("apollo17_valid_u8.bin")));
            if (RegionalValid.Num() != Count) { RegionalHeights.Reset(); RegionalValid.Reset(); }
        }
    }
    FarHeights.Reset();
    FarConfidence.Reset();
    const auto Far = ReadJson(TEXT("apollo17_far.json"));
    if (Far.IsValid())
    {
        FarWidth = static_cast<int32>(Number(Far, TEXT("width"), 0));
        FarHeightCount = static_cast<int32>(Number(Far, TEXT("height"), 0));
        FarWest = Number(Far, TEXT("westLongitudeDegrees"), 0);
        FarEast = Number(Far, TEXT("eastLongitudeDegrees"), 0);
        FarNorth = Number(Far, TEXT("northLatitudeDegrees"), 0);
        FarSouth = Number(Far, TEXT("southLatitudeDegrees"), 0);
        FarHeightScale = Number(Far, TEXT("heightScaleMeters"), 0);
        FarBlendWidth = Number(Far, TEXT("blendWidthMeters"), 500);
        FarBlendInset = Number(Far, TEXT("blendInsetMeters"), 20);
        const int64 Count = static_cast<int64>(FarWidth) * FarHeightCount;
        if (FarWidth < 2 || FarHeightCount < 2 || Count > 40000000 ||
            FarEast <= FarWest || FarNorth <= FarSouth || FarHeightScale <= 0 ||
            FarBlendInset < 0 || FarBlendWidth <= FarBlendInset ||
            !FFileHelper::LoadFileToArray(Bytes, *(DataDirectory / TEXT("apollo17_far_height_i16.bin"))) ||
            Bytes.Num() != Count * sizeof(int16))
        { OutError = TEXT("月の遠景地形データの形式またはサイズが不正です。"); return false; }
        FarHeights.SetNumUninitialized(static_cast<int32>(Count));
        FMemory::Memcpy(FarHeights.GetData(), Bytes.GetData(), Bytes.Num());
        if (!FFileHelper::LoadFileToArray(FarConfidence, *(DataDirectory / TEXT("apollo17_far_confidence_u8.bin"))) ||
            FarConfidence.Num() != Count)
        { FarHeights.Reset(); FarConfidence.Reset(); OutError = TEXT("月の遠景地形の有効範囲を読み込めません。"); return false; }
    }
    Bytes.Empty();
    for (auto& Body : BodyRecords)
    {
        if (!Body.Definition.landable) continue;
        Body.Definition.terrainMinHeightMeters = MinimumHeight - 10;
        Body.Definition.terrainMaxHeightMeters = MaximumHeight + 10;
    }
    ReferenceDefinitions=SimulationBodies();
    TArray<uint8> EphemerisBytes;
    if(!FFileHelper::LoadFileToArray(EphemerisBytes,*(DataDirectory/TEXT("ephemeris-2026.bin")))||
       !Ephemeris.Load(EphemerisBytes.GetData(),static_cast<std::size_t>(EphemerisBytes.Num())))
    { OutError=TEXT("日時に対応する天体暦を読み込めません。");return false; }
    bReady = true;
    return true;
}

const FStarBodyRecord* FStarDataCatalog::Find(const FString& Id) const
{
    return BodyRecords.FindByPredicate([&](const FStarBodyRecord& Body) { return Body.Definition.id == TCHAR_TO_UTF8(*Id); });
}
const FStarBodyRecord* FStarDataCatalog::Find(const std::string& Id) const
{
    return BodyRecords.FindByPredicate([&](const FStarBodyRecord& Body) { return Body.Definition.id == Id; });
}
std::vector<star::BodyDefinition> FStarDataCatalog::SimulationBodies() const
{
    std::vector<star::BodyDefinition> Result;
    for (const auto& Body : BodyRecords) Result.push_back(Body.Definition);
    return Result;
}
double FStarDataCatalog::GlobeHeight(double Lat, double Lon) const
{
    if (GlobalHeights.IsEmpty()) return MaximumHeight;
    Lon = FMath::Fmod(Lon + 180.0, 360.0);
    if (Lon < 0) Lon += 360.0;
    const double X = Lon / 360.0 * GlobalWidth - 0.5;
    const double Y = FMath::Clamp((90.0 - Lat) / 180.0 * GlobalHeight - 0.5, 0.0, static_cast<double>(GlobalHeight - 1));
    const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
    const double Fx = X - X0, Fy = Y - Y0;
    const auto At = [&](int32 Col, int32 Row) { return GlobalHeights[FMath::Clamp(Row, 0, GlobalHeight - 1) * GlobalWidth + (Col % GlobalWidth + GlobalWidth) % GlobalWidth] * 0.5; };
    return FMath::Lerp(FMath::Lerp(At(X0,Y0),At(X0+1,Y0),Fx),FMath::Lerp(At(X0,Y0+1),At(X0+1,Y0+1),Fx),Fy);
}
bool FStarDataCatalog::RegionalHeight(double Lat, double Lon, double& Height, double& Blend) const
{
    if (RegionalHeights.IsEmpty() || Lon <= RegionWest || Lon >= RegionEast || Lat <= RegionSouth || Lat >= RegionNorth) return false;
    const double X = (Lon - RegionWest) / (RegionEast - RegionWest) * RegionalWidth - 0.5;
    const double Y = (RegionNorth - Lat) / (RegionNorth - RegionSouth) * RegionalHeightCount - 0.5;
    const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
    if (X0 < 0 || Y0 < 0 || X0 + 1 >= RegionalWidth || Y0 + 1 >= RegionalHeightCount) return false;
    const int32 Indices[4] = {Y0*RegionalWidth+X0,Y0*RegionalWidth+X0+1,(Y0+1)*RegionalWidth+X0,(Y0+1)*RegionalWidth+X0+1};
    for (int32 Index : Indices) if (!RegionalValid[Index] || !FMath::IsFinite(RegionalHeights[Index])) return false;
    const double Fx = X-X0, Fy = Y-Y0;
    Height = FMath::Lerp(FMath::Lerp(static_cast<double>(RegionalHeights[Indices[0]]),static_cast<double>(RegionalHeights[Indices[1]]),Fx),FMath::Lerp(static_cast<double>(RegionalHeights[Indices[2]]),static_cast<double>(RegionalHeights[Indices[3]]),Fx),Fy);
    const double EdgePixels = FMath::Min(FMath::Min(X,static_cast<double>(RegionalWidth-1)-X),FMath::Min(Y,static_cast<double>(RegionalHeightCount-1)-Y));
    Blend = Smooth01(EdgePixels / 60.0); // Blend the 5m source into the globe across 300m.
    return true;
}
bool FStarDataCatalog::FarRegionalHeight(double Lat, double Lon, double& Height, double& Blend) const
{
    if (FarHeights.IsEmpty() || Lon <= FarWest || Lon >= FarEast || Lat <= FarSouth || Lat >= FarNorth) return false;
    const double X = (Lon-FarWest)/(FarEast-FarWest)*FarWidth-0.5;
    const double Y = (FarNorth-Lat)/(FarNorth-FarSouth)*FarHeightCount-0.5;
    const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
    if (X0 < 0 || Y0 < 0 || X0+1 >= FarWidth || Y0+1 >= FarHeightCount) return false;
    const int32 I[4] = {Y0*FarWidth+X0,Y0*FarWidth+X0+1,(Y0+1)*FarWidth+X0,(Y0+1)*FarWidth+X0+1};
    for (int32 Index : I) if (!FarConfidence[Index]) return false;
    const double Fx=X-X0, Fy=Y-Y0;
    const auto Bilinear = [&](double A, double B, double C, double D)
    { return FMath::Lerp(FMath::Lerp(A,B,Fx),FMath::Lerp(C,D,Fx),Fy); };
    Height = Bilinear(FarHeights[I[0]],FarHeights[I[1]],FarHeights[I[2]],FarHeights[I[3]])*FarHeightScale;
    const double Confidence = Bilinear(FarConfidence[I[0]],FarConfidence[I[1]],FarConfidence[I[2]],FarConfidence[I[3]])/255.0;
    // Inset makes the blend zero in every cell touching missing data.
    Blend = Smooth01((Confidence*FarBlendWidth-FarBlendInset)/(FarBlendWidth-FarBlendInset));
    return true;
}
double FStarDataCatalog::MoonHeight(double Lat, double Lon, bool* OutRegional) const
{
    const double Global = GlobeHeight(Lat,Lon);
    double Regional = Global, Blend = 0;
    const bool Available = RegionalHeight(Lat,Lon,Regional,Blend);
    // Interior 5m samples remain bit-for-bit on the original path.
    if (Available && Blend >= 1.0)
    {
        if (OutRegional) *OutRegional = true;
        return FMath::Lerp(Global,Regional,Blend);
    }
    double FarHeight=Global, FarBlend=0;
    const bool FarAvailable=FarRegionalHeight(Lat,Lon,FarHeight,FarBlend);
    const double FarBase=FarAvailable ? FMath::Lerp(Global,FarHeight,FarBlend) : Global;
    // Only the pre-existing 300m edge changes its blend target: 5m -> 10m -> LOLA.
    if (OutRegional) *OutRegional = Available || (FarAvailable && FarBlend > 0);
    return Available ? FMath::Lerp(FarBase,Regional,Blend) : FarBase;
}
bool FStarDataCatalog::SampleTerrain(const star::BodyDefinition& Body, const star::Vec3d& Direction, star::TerrainSample& Out) const
{
    if (!bReady || !Body.landable || Body.id != "moon") return false;
    const auto Local = Body.bodyFixedToSimulation.Conjugate().Rotate(Direction).Normalized();
    const double Lat = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Local.z,-1.0,1.0)));
    const double Lon = FMath::Abs(Local.z)>1.0-1e-14?0.0:FMath::RadiansToDegrees(FMath::Atan2(Local.y,Local.x));
    Out.heightMeters = MoonHeight(Lat,Lon);
    const double LatitudeStep = FMath::RadiansToDegrees(5.0/Body.radiusMeters);
    const double CosLat = FMath::Max(0.01,FMath::Cos(FMath::DegreesToRadians(Lat)));
    const double LongitudeStep = LatitudeStep/CosLat;
    const double EastGradient = (MoonHeight(Lat,Lon+LongitudeStep)-MoonHeight(Lat,Lon-LongitudeStep))/10.0;
    const double NorthGradient = (MoonHeight(FMath::Min(90.0,Lat+LatitudeStep),Lon)-MoonHeight(FMath::Max(-90.0,Lat-LatitudeStep),Lon))/10.0;
    const double LonRad = FMath::DegreesToRadians(Lon), LatRad = FMath::DegreesToRadians(Lat);
    const star::Vec3d East{-FMath::Sin(LonRad),FMath::Cos(LonRad),0};
    const star::Vec3d North{-FMath::Sin(LatRad)*FMath::Cos(LonRad),-FMath::Sin(LatRad)*FMath::Sin(LonRad),FMath::Cos(LatRad)};
    Out.normalSimulation = Body.bodyFixedToSimulation.Rotate((Local-East*EastGradient-North*NorthGradient).Normalized());
    return FMath::IsFinite(Out.heightMeters) && Out.normalSimulation.IsFinite();
}
bool FStarDataCatalog::SetAstronomicalUtc(double UnixSeconds)
{
    std::array<star::AstronomicalPose,4> Poses;
    if(!Ephemeris.At(UnixSeconds,Poses)) return false;
    for(auto& Record:BodyRecords) for(std::size_t I=0;I<4;++I)
        if(Record.Definition.id==star::EphemerisTable::Ids[I])
        { Record.Definition.centerMeters=Poses[I].position;Record.Definition.bodyFixedToSimulation=Poses[I].rotation; }
    return true;
}
bool FStarDataCatalog::SimulationBodiesAt(double UnixSeconds,std::vector<star::BodyDefinition>& Out) const
{
    std::array<star::AstronomicalPose,4> Poses;if(!Ephemeris.At(UnixSeconds,Poses))return false;
    Out=SimulationBodies();
    for(auto& Body:Out)for(std::size_t I=0;I<4;++I)if(Body.id==star::EphemerisTable::Ids[I])
    {Body.centerMeters=Poses[I].position;Body.bodyFixedToSimulation=Poses[I].rotation;}
    return true;
}
FVector FStarDataCatalog::UEVector(const star::Vec3d& V) { return FVector(V.x,V.y,V.z); }
star::Vec3d FStarDataCatalog::SimVector(const FVector& V) { return {V.X,V.Y,V.Z}; }
FQuat FStarDataCatalog::UERotation(const star::Quatd& Q)
{
    return FRotationMatrix::MakeFromXZ(UEVector(star::SimulationDirectionToUnreal(star::Forward(Q))),UEVector(star::SimulationDirectionToUnreal(star::Up(Q)))).ToQuat();
}
