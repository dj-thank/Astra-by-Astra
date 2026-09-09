#include "Exploration/StarExplorationSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
using namespace star::exploration;
const TCHAR* Titles[] = {TEXT("青い惑星の昼"), TEXT("夜の地球"), TEXT("月の地形を読む"),
    TEXT("Taurus-Littrow に着陸"), TEXT("土星の環をたどる"), TEXT("環に落ちる土星の影")};
const TCHAR* Details[] = {
    TEXT("地球の昼側を向き、高度100～50,000 km・速度12 km/s以下で12秒スキャン。"),
    TEXT("地球の夜側を向き、高度100～50,000 km・速度12 km/s以下で12秒スキャン。"),
    TEXT("月面を向き、高度20～500 km・速度3 km/s以下で10秒スキャン。"),
    TEXT("北緯20.1908°・東経30.7717°付近に安全に着陸。静止して8秒スキャン。"),
    TEXT("土星の環を向き、高度10,000～150,000 km・速度30 km/s以下で12秒スキャン。"),
    TEXT("土星本体の影に入った環を向き、高度10,000～150,000 km・速度30 km/s以下で12秒スキャン。")};
const TCHAR* References[] = {
    TEXT("参考知識：地球の自転により、太陽に照らされる昼側と夜側が移り変わります。"),
    TEXT("参考資料：NASA Black Marble は、衛星から捉えた夜の地球の光を可視化したデータです。"),
    TEXT("参考知識：月の地形には衝突クレーターなど、長い地質史の痕跡が残っています。"),
    TEXT("参考知識：Taurus-Littrow は Apollo 17 の着陸地域です。本ゲームの着陸記録は実地調査ではありません。"),
    TEXT("参考知識：土星の環は多数の氷や岩石の粒子から成ります。近景の粒子配置は視覚的な再構成です。"),
    TEXT("参考知識：土星は太陽光を遮り、環に影を落とします。ここで記録するのはゲーム内の観測条件です。")};
const TCHAR* Sources[] = {
    TEXT("https://science.nasa.gov/earth/facts/"),
    TEXT("https://science.nasa.gov/earth/earth-observatory/earth-at-night/"),
    TEXT("https://science.nasa.gov/moon/facts/"),
    TEXT("https://science.nasa.gov/resource/apollo-17-landing-site-the-taurus-littrow-valley/"),
    TEXT("https://science.nasa.gov/saturn/facts/"),
    TEXT("https://science.nasa.gov/saturn/facts/")};

Body ParseBody(const FString& Id)
{
    if (Id == TEXT("earth")) return Body::Earth;
    if (Id == TEXT("moon")) return Body::Moon;
    if (Id == TEXT("saturn")) return Body::Saturn;
    return Body::Unknown;
}
FString BodyId(Body Value)
{
    switch (Value) { case Body::Earth: return TEXT("earth"); case Body::Moon: return TEXT("moon");
        case Body::Saturn: return TEXT("saturn"); default: return TEXT("unknown"); }
}
Sample Convert(const FStarObservationSample& In)
{
    Sample S;
    S.body = ParseBody(In.BodyId); S.sequence = In.Sequence; S.deltaSeconds = In.DeltaSeconds;
    S.altitudeM = In.AltitudeM; S.latitudeDeg = In.LatitudeDeg; S.longitudeDeg = In.LongitudeDeg;
    S.speedMps = In.SpeedMps; S.verticalSpeedMps = In.VerticalSpeedMps;
    S.sunIllumination = In.SunIllumination; S.ringRadiusM = In.RingRadiusM;
    S.landed = In.bLanded; S.scanning = In.bScanning; S.viewingBody = In.bViewingBody;
    S.cockpitView = In.bCockpitView; S.viewingRings = In.bViewingRings;
    S.observedPointInPlanetShadow = In.bObservedPointInPlanetShadow;
    return S;
}
TSharedRef<FJsonObject> WriteSample(const Sample& S)
{
    TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("body"), BodyId(S.body));
    O->SetNumberField(TEXT("delta_seconds"), S.deltaSeconds);
    O->SetNumberField(TEXT("altitude_m"), S.altitudeM);
    O->SetNumberField(TEXT("latitude_deg"), S.latitudeDeg);
    O->SetNumberField(TEXT("longitude_deg"), S.longitudeDeg);
    O->SetNumberField(TEXT("speed_mps"), S.speedMps);
    O->SetNumberField(TEXT("vertical_speed_mps"), S.verticalSpeedMps);
    O->SetNumberField(TEXT("sun_illumination"), S.sunIllumination);
    O->SetNumberField(TEXT("ring_radius_m"), S.ringRadiusM);
    O->SetBoolField(TEXT("landed"), S.landed);
    O->SetBoolField(TEXT("scanning"), S.scanning);
    O->SetBoolField(TEXT("viewing_body"), S.viewingBody);
    O->SetBoolField(TEXT("cockpit_view"), S.cockpitView);
    O->SetBoolField(TEXT("viewing_rings"), S.viewingRings);
    O->SetBoolField(TEXT("planet_shadow"), S.observedPointInPlanetShadow);
    return O;
}
bool ReadSample(const TSharedPtr<FJsonObject>& O, Sample& S)
{
    FString Id;
    if (!O.IsValid() || !O->TryGetStringField(TEXT("body"), Id)) return false;
    S.body = ParseBody(Id);
    S.sequence = 1; // replay identity is intentionally not restored
    return O->TryGetNumberField(TEXT("delta_seconds"), S.deltaSeconds) &&
        O->TryGetNumberField(TEXT("altitude_m"), S.altitudeM) &&
        O->TryGetNumberField(TEXT("latitude_deg"), S.latitudeDeg) &&
        O->TryGetNumberField(TEXT("longitude_deg"), S.longitudeDeg) &&
        O->TryGetNumberField(TEXT("speed_mps"), S.speedMps) &&
        O->TryGetNumberField(TEXT("vertical_speed_mps"), S.verticalSpeedMps) &&
        O->TryGetNumberField(TEXT("sun_illumination"), S.sunIllumination) &&
        O->TryGetNumberField(TEXT("ring_radius_m"), S.ringRadiusM) &&
        O->TryGetBoolField(TEXT("landed"), S.landed) &&
        O->TryGetBoolField(TEXT("scanning"), S.scanning) &&
        O->TryGetBoolField(TEXT("viewing_body"), S.viewingBody) &&
        O->TryGetBoolField(TEXT("cockpit_view"), S.cockpitView) &&
        O->TryGetBoolField(TEXT("viewing_rings"), S.viewingRings) &&
        O->TryGetBoolField(TEXT("planet_shadow"), S.observedPointInPlanetShadow) && IsValid(S);
}
}

void UStarExplorationSubsystem::SubmitObservation(const FStarObservationSample& Input)
{
    const uint32 NewRecords = Tracker.Observe(Convert(Input));
    for (std::size_t I = 0; I < ObjectiveCount; ++I)
        if (NewRecords & (1u << I)) OnDiscoveryRecorded.Broadcast(UTF8_TO_TCHAR(Ids[I]));
}

void UStarExplorationSubsystem::NotifyAction(FName Action)
{
    if (Action == TEXT("TargetEarth") || Action == TEXT("TargetMoon") || Action == TEXT("TargetSaturn")) Tracker.TargetSelected();
    if (Action == TEXT("ToggleView")) Tracker.ViewChanged();
}

TArray<FStarObjectiveView> UStarExplorationSubsystem::GetObjectives() const
{
    TArray<FStarObjectiveView> Out;
    for (std::size_t I = 0; I < ObjectiveCount; ++I)
    {
        FStarObjectiveView View;
        View.Id = UTF8_TO_TCHAR(Ids[I]); View.Title = Titles[I]; View.Detail = Details[I];
        View.Progress = static_cast<float>(Tracker.Progress(I)); View.bComplete = Tracker.Complete(I);
        Out.Add(MoveTemp(View));
    }
    return Out;
}

FStarObjectiveView UStarExplorationSubsystem::GetTutorialObjective() const
{
    const TCHAR* TutorialTitles[] = {TEXT("操船を確かめる"), TEXT("目的地を選ぶ"), TEXT("視点を切り替える"), TEXT("観測を始める"), TEXT("月に着陸する")};
    const TCHAR* TutorialDetails[] = {
        TEXT("推力を上げて離陸。軌道から始めた場合は、20 m/s以上で操船を確認します。"),
        TEXT("メニューで地球・月・土星から目的地を選びます。"),
        TEXT("V または視点ボタンでコックピットと船外視点を切り替えます。"),
        TEXT("目的地を向いて減速。目標の観測条件を満たし、スキャンを続けます。"),
        TEXT("月へ接近して脚を展開。姿勢を整え、ゆっくり降下して着陸します。")};
    FStarObjectiveView Out; Out.Id = TEXT("tutorial");
    int32 Done = 0;
    for (int32 I = 0; I < 5; ++I) if (Tracker.State().tutorialMask & (1u << I)) ++Done;
    Out.Progress = Done / 5.0f; Out.bComplete = Done == 5;
    if (Out.bComplete) { Out.Title = TEXT("準備完了 — 自由探査"); Out.Detail = TEXT("6つの観測を記録するか、好きな場所へ飛びましょう。"); return Out; }
    for (int32 I = 0; I < 5; ++I)
        if (!(Tracker.State().tutorialMask & (1u << I))) { Out.Title = TutorialTitles[I]; Out.Detail = TutorialDetails[I]; break; }
    return Out;
}

TArray<FStarJournalEntry> UStarExplorationSubsystem::GetJournalEntries() const
{
    TArray<FStarJournalEntry> Out;
    for (std::size_t I = 0; I < ObjectiveCount; ++I)
    {
        if (!Tracker.Complete(I)) continue;
        const auto& S = Tracker.State().discoveries[I];
        FStarJournalEntry Entry;
        Entry.ObjectiveId = UTF8_TO_TCHAR(Ids[I]); Entry.Title = Titles[I];
        Entry.RuntimeSummary = FString::Printf(TEXT("飛行記録：高度 %.1f km / 速度 %.1f m/s / 北緯 %.4f°・東経 %.4f°\n観測を %.0f 秒継続。ゲーム内シミュレーションの記録。"),
            S.altitudeM / 1000, S.speedMps, S.latitudeDeg, S.longitudeDeg, Durations[I]);
        Entry.ReferenceText = References[I]; Entry.SourceUrl = Sources[I];
        Out.Add(MoveTemp(Entry));
    }
    return Out;
}

float UStarExplorationSubsystem::GetActiveScanProgress() const
{
    double Active = 0;
    for (std::size_t I = 0; I < ObjectiveCount; ++I) if (!Tracker.Complete(I)) Active = FMath::Max(Active, Tracker.Progress(I));
    return static_cast<float>(Active);
}

bool UStarExplorationSubsystem::CanScan(const FStarObservationSample& Input) const
{
    auto S = Convert(Input); S.scanning = true;
    for (std::size_t I = 0; I < ObjectiveCount; ++I) if (!Tracker.Complete(I) && Qualifies(I, S)) return true;
    return false;
}

FString UStarExplorationSubsystem::ExportProgressJson() const
{
    const auto& State = Tracker.State();
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), 1);
    Root->SetNumberField(TEXT("tutorial_mask"), State.tutorialMask);
    TArray<TSharedPtr<FJsonValue>> Objectives;
    for (std::size_t I = 0; I < ObjectiveCount; ++I)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), UTF8_TO_TCHAR(Ids[I]));
        Item->SetNumberField(TEXT("seconds"), State.seconds[I]);
        if (Tracker.Complete(I)) Item->SetObjectField(TEXT("observation"), WriteSample(State.discoveries[I]));
        Objectives.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("objectives"), Objectives);
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

bool UStarExplorationSubsystem::RestoreProgressJson(const FString& Json, FString& OutError)
{
    OutError.Reset();
    const auto Fail = [&OutError]() { OutError = TEXT("探査記録を読み込めません。保存データの形式または内容が不正です。"); return false; };
    if (Json.Len() > 128 * 1024) return Fail();
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return Fail();
    double Version = 0, Mask = 0;
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1 ||
        !Root->TryGetNumberField(TEXT("tutorial_mask"), Mask) || !FMath::IsFinite(Mask) ||
        Mask < 0 || Mask > TutorialAll || FMath::FloorToDouble(Mask) != Mask ||
        !Root->TryGetArrayField(TEXT("objectives"), Items) || Items->Num() != ObjectiveCount) return Fail();
    ProgressState Candidate;
    Candidate.tutorialMask = static_cast<uint32>(Mask);
    TSet<FString> Seen;
    for (const auto& Value : *Items)
    {
        if (!Value.IsValid() || Value->Type != EJson::Object) return Fail();
        const auto Item = Value->AsObject();
        FString Id; double Seconds = 0;
        if (!Item.IsValid() || !Item->TryGetStringField(TEXT("id"), Id) || Seen.Contains(Id) ||
            !Item->TryGetNumberField(TEXT("seconds"), Seconds)) return Fail();
        Seen.Add(Id);
        std::size_t Index = ObjectiveCount;
        for (std::size_t I = 0; I < ObjectiveCount; ++I) if (Id == UTF8_TO_TCHAR(Ids[I])) { Index = I; break; }
        if (Index == ObjectiveCount) return Fail();
        Candidate.seconds[Index] = Seconds;
        if (Seconds == Durations[Index])
        {
            const TSharedPtr<FJsonObject>* Observation = nullptr;
            if (!Item->TryGetObjectField(TEXT("observation"), Observation) || !ReadSample(*Observation, Candidate.discoveries[Index])) return Fail();
        }
        else if (Item->HasField(TEXT("observation"))) return Fail();
    }
    if (!Tracker.Restore(Candidate)) return Fail();
    return true;
}
