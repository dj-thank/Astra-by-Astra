#include "Terrain/StarLunarTerrainComponent.h"
#include "Terrain/LunarTerrainGeometry.h"
#include "Runtime/StarDataCatalog.h"
#include "ProceduralMeshComponent.h"
#include "Async/Async.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderCommandFence.h"
#include <memory>

namespace {
constexpr int32 MaximumMeshSlots = 2*(star::terrain::MaxGroundPatches+star::terrain::MaxClastPatches);
constexpr int32 MaximumUploadsPerFrame = 2;
constexpr int32 MaximumUploadVerticesPerFrame = 10000;
constexpr double UploadBudgetSeconds = 0.002;
FVector Vector(star::Vec3d V) { return {V.x,V.y,V.z}; }
FVector Direction(star::Vec3d V) { return {V.x,-V.y,V.z}; }
const star::terrain::Patch& PlanPatch(const star::terrain::Plan& Plan,int32 Index)
{
    const auto I=static_cast<std::size_t>(Index);
    return I<Plan.patches.size()?Plan.patches[I]:Plan.clastPatches[I-Plan.patches.size()];
}

struct FUpload
{
    int32 PatchIndex = INDEX_NONE;
    star::Vec3d AnchorBodyMeters;
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UV0, UV1, UV2, UV3;
    TArray<int32> Indices;
    TArray<FProcMeshTangent> Tangents;
};
struct FBuildResult
{
    uint64 Generation = 0;
    uint64 ChartRevision = 0;
    star::terrain::Plan Plan;
    std::vector<int32> Slots;
    std::vector<FUpload> Uploads;
    bool bComplete = false;
};
struct FMeshSlot
{
    UProceduralMeshComponent* Component = nullptr; // GC retained by MeshPool.
    star::terrain::Patch Patch;
    star::Vec3d AnchorBodyMeters;
    uint64 ChartRevision = 0;
    uint64 LastUsed = 0;
    bool bHasData = false, bActive = false, bPending = false;
    bool bClast = false;
};
}

struct FStarLunarTerrainState
{
    const FStarDataCatalog* Catalog = nullptr;
    star::BodyDefinition Moon;
    star::terrain::Chart Chart;
    star::terrain::Plan ActivePlan;
    star::Vec3d ShipDirection;
    uint64 ChartRevision = 0, Generation = 0, Frame = 0;
    bool bHasChart = false, bCoverage = false, bReadyForLanding = false, bInactive = true;
    bool bTerrainShadows = true, bClastShadows = true, bFarTerrainShadows = false;
    std::vector<FMeshSlot> Slots;
    TFuture<std::shared_ptr<FBuildResult>> Future;
    std::shared_ptr<std::atomic_bool> Cancellation;
    std::shared_ptr<FBuildResult> Pending;
    std::size_t NextUpload = 0;
    uint64 UploadCompleteFrame = 0;
    FRenderCommandFence UploadFence;
    bool bFenceStarted = false;

    void Cancel()
    {
        ++Generation;
        if (Cancellation) Cancellation->store(true,std::memory_order_relaxed);
        Pending.reset();
        NextUpload = 0;
        UploadCompleteFrame = 0;
        bFenceStarted = false;
        for (auto& Slot : Slots) Slot.bPending = false;
    }
    void Join()
    {
        Cancel();
        if (Future.IsValid()) { Future.Wait(); Future.Get(); }
        UploadFence.Wait();
        Future = TFuture<std::shared_ptr<FBuildResult>>();
        Cancellation.reset();
    }
};

void FStarLunarTerrainStateDeleter::operator()(FStarLunarTerrainState* Value) const { delete Value; }

UStarLunarTerrainComponent::UStarLunarTerrainComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    State.Reset(new FStarLunarTerrainState());
}
UStarLunarTerrainComponent::~UStarLunarTerrainComponent()
{
    // No UObject mutation during destruction, but never allow the raw catalog
    // captured by a sampling job to survive the final native component teardown.
    if (State) State->Join();
}
void UStarLunarTerrainComponent::Initialize(const FStarDataCatalog* InCatalog, UMaterialInterface* SurfaceMaterial)
{
    check(IsInGameThread());
    Shutdown();
    if (!InCatalog || !InCatalog->IsReady() || !SurfaceMaterial) return;
    const auto* Moon = InCatalog->Find(FString(TEXT("moon")));
    if (!Moon) return;
    Material = SurfaceMaterial;
    State->Catalog = InCatalog;
    State->Moon = Moon->Definition;
    // Root-owned QA launch arguments only. Normal launches retain both shadow
    // classes; no permanent global shadow-disable or user-facing settings change.
    int32 TerrainShadowProbe=1,ClastShadowProbe=1;
    FParse::Value(FCommandLine::Get(),TEXT("StarTerrainShadowProbe="),TerrainShadowProbe);
    FParse::Value(FCommandLine::Get(),TEXT("StarClastShadowProbe="),ClastShadowProbe);
    State->bTerrainShadows=TerrainShadowProbe!=0;
    State->bFarTerrainShadows=TerrainShadowProbe>=2;
    State->bClastShadows=ClastShadowProbe!=0;
    UE_LOG(LogTemp,Log,TEXT("STAR terrain shadow probes: ground=%d clasts=%d (default both on)"),
        State->bTerrainShadows?1:0,State->bClastShadows?1:0);
}
void UStarLunarTerrainComponent::Shutdown()
{
    check(IsInGameThread());
    if (!State) return;
    State->Join();
    State->Catalog = nullptr;
    State->bCoverage = State->bReadyForLanding = State->bHasChart = false;
    State->bInactive = true;
    State->ActivePlan = {};
    ReleaseMeshes();
    Material = nullptr;
}
void UStarLunarTerrainComponent::EndPlay(const EEndPlayReason::Type Reason) { Shutdown(); Super::EndPlay(Reason); }
void UStarLunarTerrainComponent::OnUnregister() { Shutdown(); Super::OnUnregister(); }
void UStarLunarTerrainComponent::ReleaseMeshes()
{
    for (auto& Mesh : MeshPool) if (IsValid(Mesh)) Mesh->DestroyComponent();
    MeshPool.Reset();
    State->Slots.clear();
}
UProceduralMeshComponent* UStarLunarTerrainComponent::AcquireMesh()
{
    if (MeshPool.Num() >= MaximumMeshSlots || !GetOwner() || !IsRegistered()) return nullptr;
    auto* Mesh = NewObject<UProceduralMeshComponent>(GetOwner());
    GetOwner()->AddInstanceComponent(Mesh);
    Mesh->SetupAttachment(this);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetAbsolute(true,true,true);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCastShadow(true);
    Mesh->SetVisibility(false);
    Mesh->bUseAsyncCooking = false; // Flight contact samples the same DEM directly.
    Mesh->SetMaterial(0,Material);
    Mesh->RegisterComponent();
    MeshPool.Add(Mesh);
    FMeshSlot Slot;
    Slot.Component = Mesh;
    State->Slots.push_back(Slot);
    return Mesh;
}
void UStarLunarTerrainComponent::PositionMeshes(const star::Vec3d& Origin)
{
    const auto Rotation = FStarDataCatalog::UERotation(State->Moon.bodyFixedToSimulation);
    for (auto& Slot : State->Slots)
    {
        if (!Slot.bHasData || (!Slot.bActive && !Slot.bPending)) continue;
        // Subtract the astronomical origin in DOUBLE METERS before cm conversion.
        // Vertex buffers contain only deltas from each tile's own body-space anchor.
        const auto Absolute = State->Moon.centerMeters+State->Moon.bodyFixedToSimulation.Rotate(Slot.AnchorBodyMeters);
        Slot.Component->SetWorldLocationAndRotation(Vector(star::ToUnrealCentimeters(Absolute,Origin)),Rotation);
    }
}
bool UStarLunarTerrainComponent::HasCoverage() const { return State && State->bCoverage; }
bool UStarLunarTerrainComponent::IsReadyForLanding() const { return State && State->bReadyForLanding; }
void UStarLunarTerrainComponent::GetGlobeHole(star::Vec3d& DirectionOut, double& CosAngle, bool& Enabled) const
{
    Enabled = HasCoverage() && State->ActivePlan.holeAngle > 0;
    DirectionOut = Enabled ? State->ActivePlan.holeDirection : star::Vec3d{1,0,0};
    CosAngle = Enabled ? FMath::Cos(State->ActivePlan.holeAngle) : 1.0;
}

void UStarLunarTerrainComponent::UpdateTerrain(const star::Vec3d& Ship, const star::Vec3d& Origin)
{
    check(IsInGameThread());
    if (!State->Catalog || !IsRegistered() || !Ship.IsFinite() || !Origin.IsFinite()) return;
    ++State->Frame;
    // Only the body frame is mutable. Worker jobs read the immutable DEM arrays
    // through MoonHeight and never access these game-thread celestial records.
    if(const auto* CurrentMoon=State->Catalog->Find(FString(TEXT("moon")))) State->Moon=CurrentMoon->Definition;
    const auto Local = State->Moon.bodyFixedToSimulation.Conjugate().Rotate(Ship-State->Moon.centerMeters);
    State->ShipDirection = Local.Normalized();
    const double DatumAltitude = Local.Length()-State->Moon.radiusMeters;

    if (DatumAltitude > star::terrain::ActiveAltitudeMeters)
    {
        if (!State->bInactive)
        {
            State->Cancel();
            State->bInactive = true;
            State->bCoverage = State->bReadyForLanding = State->bHasChart = false;
            State->ActivePlan = {};
            for (auto& Slot : State->Slots) { Slot.bActive = false; Slot.Component->SetVisibility(false); }
        }
        if (State->Future.IsValid() && State->Future.IsReady()) { State->Future.Get(); State->Future = {}; State->Cancellation.reset(); }
        // Retire at most two GPU buffers per frame. Empty UObjects are retained in
        // the strictly bounded pool so repeated approach/takeoff cannot accumulate.
        int32 Retired = 0;
        for (auto& Slot : State->Slots) if (Slot.bHasData && Retired < MaximumUploadsPerFrame)
        {
            Slot.Component->ClearAllMeshSections(); Slot.bHasData = false; ++Retired;
        }
        return;
    }
    State->bInactive = false;
    const double Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(State->ShipDirection.z,-1.0,1.0)));
    const double Longitude = FMath::RadiansToDegrees(FMath::Atan2(State->ShipDirection.y,State->ShipDirection.x));
    const double Altitude = DatumAltitude-State->Catalog->MoonHeight(Latitude,Longitude);
    if (!State->bHasChart || star::Vec3d::Dot(State->Chart.radial,State->ShipDirection) < FMath::Cos(0.03))
    {
        State->Cancel();
        ++State->ChartRevision;
        State->Chart = star::terrain::Chart::At(State->ShipDirection,State->Moon.radiusMeters);
        State->bHasChart = true;
    }
    const auto Desired = star::terrain::MakePlan(State->Chart,State->ShipDirection,Altitude,
        State->Catalog->TerrainMaximum()-State->Catalog->TerrainMinimum());

    if (State->Future.IsValid() && State->Future.IsReady())
    {
        auto Result = State->Future.Get();
        State->Future = {};
        State->Cancellation.reset();
        if (Result && Result->bComplete && Result->Generation == State->Generation && Result->ChartRevision == State->ChartRevision)
        {
            State->Pending = std::move(Result);
            State->NextUpload = 0;
            State->UploadCompleteFrame = 0;
            State->bFenceStarted = false;
        }
        else for (auto& Slot : State->Slots) Slot.bPending = false;
    }

    if (State->Pending)
    {
        const double Start = FPlatformTime::Seconds();
        int32 Uploads = 0, Vertices = 0;
        while (State->NextUpload < State->Pending->Uploads.size() && Uploads < MaximumUploadsPerFrame)
        {
            auto& Upload = State->Pending->Uploads[State->NextUpload];
            if (Uploads > 0 && (Vertices+Upload.Vertices.Num() > MaximumUploadVerticesPerFrame || FPlatformTime::Seconds()-Start > UploadBudgetSeconds)) break;
            int32 Target = INDEX_NONE;
            uint64 Oldest = MAX_uint64;
            for (int32 I=0; I<static_cast<int32>(State->Slots.size()); ++I)
            {
                const auto& Slot = State->Slots[static_cast<std::size_t>(I)];
                if (!Slot.bActive && !Slot.bPending && (!Slot.bHasData || Slot.LastUsed < Oldest))
                { Target = I; Oldest = Slot.bHasData ? Slot.LastUsed : 0; if (!Slot.bHasData) break; }
            }
            if (Target == INDEX_NONE)
            {
                if (!AcquireMesh()) { State->Cancel(); break; }
                Target = static_cast<int32>(State->Slots.size())-1;
            }
            auto& Slot = State->Slots[static_cast<std::size_t>(Target)];
            Slot.Component->SetVisibility(false);
            Slot.Component->CreateMeshSection_LinearColor(0,Upload.Vertices,Upload.Indices,Upload.Normals,
                Upload.UV0,Upload.UV1,Upload.UV2,Upload.UV3,TArray<FLinearColor>(),Upload.Tangents,false);
            Slot.Patch = PlanPatch(State->Pending->Plan,Upload.PatchIndex);
            Slot.bClast = static_cast<std::size_t>(Upload.PatchIndex)>=State->Pending->Plan.patches.size();
            Slot.AnchorBodyMeters = Upload.AnchorBodyMeters;
            Slot.ChartRevision = State->Pending->ChartRevision;
            Slot.bHasData = Slot.bPending = true;
            Slot.LastUsed = State->Frame;
            // Only the nearest detailed tiles participate in dynamic shadow maps.
            Slot.Component->SetCastShadow(Slot.bClast?(State->bClastShadows&&Slot.Patch.level==0):
                (State->bTerrainShadows && (State->bFarTerrainShadows || (Slot.Patch.level==0 && State->Pending->Plan.fineStep==5))));
            State->Pending->Slots[static_cast<std::size_t>(Upload.PatchIndex)] = Target;
            Vertices += Upload.Vertices.Num(); ++Uploads; ++State->NextUpload;
            // Release CPU upload arrays immediately, not at the end of a long queue.
            Upload = FUpload();
        }
        if (State->Pending && State->NextUpload == State->Pending->Uploads.size())
        {
            // CreateMeshSection schedules deferred render-state work. Let its
            // end-of-frame update run BEFORE fencing, then wait without blocking
            // any frame. A CPU-built section alone is not coverage readiness.
            if (State->UploadCompleteFrame == 0) State->UploadCompleteFrame = State->Frame;
            else if (!State->bFenceStarted)
            {
                State->UploadFence.BeginFence();
                State->bFenceStarted = true;
            }
        }
        if (State->Pending && State->bFenceStarted && State->UploadFence.IsFenceComplete())
        {
            // All sections now exist. Visibility and the cap metadata are committed
            // in this same game-thread update; no partial set can punch the globe.
            for (auto& Slot : State->Slots) Slot.bActive = false;
            for (const int32 Index : State->Pending->Slots)
            {
                check(Index >= 0 && Index < static_cast<int32>(State->Slots.size()));
                auto& Slot = State->Slots[static_cast<std::size_t>(Index)];
                Slot.bActive = true; Slot.LastUsed = State->Frame;
            }
            for (auto& Slot : State->Slots) { Slot.bPending = false; Slot.Component->SetVisibility(Slot.bActive); }
            State->ActivePlan = std::move(State->Pending->Plan);
            State->bCoverage = true;
            State->Pending.reset();
        }
    }

    PositionMeshes(Origin);
    State->bReadyForLanding = State->bCoverage && State->ActivePlan.fineStep <= 5.0 &&
        State->ActivePlan.Contains(State->ShipDirection,true,32.0);
    if (Desired.patches.empty() || State->Future.IsValid() || State->Pending ||
        (State->bCoverage && star::terrain::SamePlan(Desired,State->ActivePlan))) return;

    auto Result = std::make_shared<FBuildResult>();
    Result->Plan = Desired;
    Result->ChartRevision = State->ChartRevision;
    Result->Generation = ++State->Generation;
    const auto PatchCount=Desired.patches.size()+Desired.clastPatches.size();
    Result->Slots.assign(PatchCount,INDEX_NONE);
    std::vector<int32> Missing;
    for (int32 P=0; P<static_cast<int32>(PatchCount); ++P)
    {
        for (int32 I=0; I<static_cast<int32>(State->Slots.size()); ++I)
        {
            auto& Slot = State->Slots[static_cast<std::size_t>(I)];
            if (Slot.bHasData && Slot.ChartRevision == State->ChartRevision &&
                Slot.bClast==(static_cast<std::size_t>(P)>=Desired.patches.size()) && Slot.Patch==PlanPatch(Desired,P))
            { Result->Slots[static_cast<std::size_t>(P)] = I; Slot.bPending = true; break; }
        }
        if (Result->Slots[static_cast<std::size_t>(P)] == INDEX_NONE) Missing.push_back(P);
    }
    State->Cancellation = std::make_shared<std::atomic_bool>(false);
    const auto Cancel = State->Cancellation;
    const FStarDataCatalog* Catalog = State->Catalog;
    // One bounded thread-pool job. It captures immutable data and plain mesh
    // buffers only; no actor, component, material, or other UObject is touched.
    State->Future = Async(EAsyncExecution::ThreadPool,[Result,Missing=std::move(Missing),Cancel,Catalog]() mutable {
        const auto Sample = [Catalog](double Lat, double Lon) { return Catalog->MoonHeight(Lat,Lon); };
        Result->Uploads.reserve(Missing.size());
        for (const int32 P : Missing)
        {
            if (Cancel->load(std::memory_order_relaxed)) return Result;
            const auto& Patch=PlanPatch(Result->Plan,P);
            const bool bClast=static_cast<std::size_t>(P)>=Result->Plan.patches.size();
            auto Mesh=bClast?star::terrain::BuildClastPatch(Result->Plan.chart,Patch,Sample,Cancel.get()):
                star::terrain::BuildPatch(Result->Plan.chart,Patch,Sample,Cancel.get());
            if (!Mesh.complete) return Result;
            FUpload Upload;
            Upload.PatchIndex = P;
            Upload.AnchorBodyMeters = Mesh.anchorBodyMeters;
            const int32 Count = static_cast<int32>(Mesh.vertices.size());
            Upload.Vertices.Reserve(Count); Upload.Normals.Reserve(Count); Upload.Tangents.Reserve(Count);
            Upload.UV0.Reserve(Count); Upload.UV1.Reserve(Count);
            Upload.UV2.Reserve(Count); Upload.UV3.Reserve(Count);
            for (const auto& V : Mesh.vertices)
            {
                Upload.Vertices.Add(Vector(star::terrain::UnrealLocalCentimeters(V.position)));
                Upload.Normals.Add(Direction(V.normal));
                Upload.Tangents.Emplace(Direction(V.tangent),true); // Reflected RH tangent frame.
                Upload.UV0.Emplace(V.uv0.u,V.uv0.v); Upload.UV1.Emplace(V.uv1.u,V.uv1.v);
                Upload.UV2.Emplace(V.uv2.u,V.uv2.v); Upload.UV3.Emplace(V.uv3.u,V.uv3.v);
            }
            Upload.Indices.Append(Mesh.indices.data(),static_cast<int32>(Mesh.indices.size()));
            Result->Uploads.push_back(std::move(Upload));
        }
        Result->bComplete = !Cancel->load(std::memory_order_relaxed);
        return Result;
    });
}
