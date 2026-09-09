#include "EVA/StarEVAPawn.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Math/RotationMatrix.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AStarEVAPawn::AStarEVAPawn()
{
    PrimaryActorTick.bCanEverTick = false;
    EVARoot = CreateDefaultSubobject<USceneComponent>(TEXT("EVARoot"));
    SetRootComponent(EVARoot);
    bVR=FParse::Param(FCommandLine::Get(),TEXT("StarVR"));
    TrackingOrigin=CreateDefaultSubobject<USceneComponent>(TEXT("EVATrackingOrigin"));
    TrackingOrigin->SetupAttachment(EVARoot);
    EVACamera = CreateDefaultSubobject<UCameraComponent>(TEXT("EVACamera"));
    EVACamera->SetupAttachment(bVR?TrackingOrigin.Get():EVARoot.Get());
    EVACamera->bLockToHmd=bVR;
    EVACamera->FieldOfView = CameraHorizontalFOVDegrees();
    EVACamera->bUsePawnControlRotation = false;
    SetActorEnableCollision(false); // DEM and conservative hull checks own movement.
}
bool AStarEVAPawn::InitializeFromShip(AStarShipPawn* Ship)
{
    if (!IsValid(Ship) || !Ship->Simulation() || !IsValid(Ship->Director()) || !Ship->Director()->IsReady()) return false;
    const auto* Moon = Ship->Simulation()->FindBody("moon");
    if (!Moon) return false;
    TWeakObjectPtr<AStarWorldDirector> Director = Ship->Director();
    const auto Sampler = [Director](const star::BodyDefinition& Body,const star::Vec3d& Direction,star::TerrainSample& Out) {
        return Director.IsValid() && Director->Catalog().SampleTerrain(Body,Direction,Out);
    };
    if (!Walk.Initialize(Ship->Simulation()->State(),*Moon,Sampler,Ship->Simulation()->Config())) return false;
    ParkedShip = Ship;
    RefreshTransform();
    return true;
}
void AStarEVAPawn::AdvanceWalking(double Dt,FVector2D Move,float YawDelta,float PitchDelta,bool bPaused)
{
    if (!ParkedShip.IsValid() || !ParkedShip->Simulation()) return;
    if(const auto* Moon=ParkedShip->Simulation()->FindBody("moon")) Walk.UpdateCelestialFrame(*Moon);
    const auto& State = ParkedShip->Simulation()->State();
    if (State.mode != star::FlightMode::Landed || State.landedBodyId != "moon") return;
    Walk.Advance(Dt,Move.X,Move.Y,YawDelta,PitchDelta,bPaused);
    RefreshTransform();
}
bool AStarEVAPawn::CanBoardShip() const
{
    return ParkedShip.IsValid() && ParkedShip->Simulation() && Walk.CanBoard(ParkedShip->Simulation()->State());
}
bool AStarEVAPawn::RestoreSaveState(const star::LunarWalkSaveState& Saved)
{
    if (!ParkedShip.IsValid() || !ParkedShip->Simulation()) return false;
    const auto& State = ParkedShip->Simulation()->State();
    if (!Walk.IsSameParkedShip(State)) return false;
    if (!Walk.RestoreSaveState(Saved)) return false;
    RefreshTransform();
    return true;
}
void AStarEVAPawn::RefreshTransform()
{
    if (!ParkedShip.IsValid() || !Walk.IsReady()) return;
    const FVector Feet = FStarDataCatalog::UEVector(star::ToUnrealCentimeters(Walk.Position(),ParkedShip->OriginMeters()));
    const FVector Forward = FStarDataCatalog::UEVector(star::SimulationDirectionToUnreal(Walk.CameraForward()));
    const FVector Up = FStarDataCatalog::UEVector(star::SimulationDirectionToUnreal(Walk.CameraUp()));
    SetActorLocation(Feet);
    USceneComponent* ViewOrigin=bVR?TrackingOrigin.Get():EVACamera.Get();
    ViewOrigin->SetWorldLocationAndRotation(
        FStarDataCatalog::UEVector(star::ToUnrealCentimeters(Walk.Camera(),ParkedShip->OriginMeters())),
        FRotationMatrix::MakeFromXZ(Forward,Up).ToQuat());
}
star::Vec3d AStarEVAPawn::CameraAbsoluteMeters() const
{
    if(!bVR||!ParkedShip.IsValid())return Walk.Camera();
    auto P=EVACamera->GetComponentLocation();return ParkedShip->OriginMeters()+star::Vec3d{P.X,-P.Y,P.Z}/100;
}
star::Vec3d AStarEVAPawn::CameraForwardSimulation() const
{if(!bVR)return Walk.CameraForward();auto P=EVACamera->GetForwardVector();return {P.X,-P.Y,P.Z};}
star::Vec3d AStarEVAPawn::CameraRightSimulation() const
{if(!bVR)return Walk.CameraRight();auto P=EVACamera->GetRightVector();return {P.X,-P.Y,P.Z};}
star::Vec3d AStarEVAPawn::CameraUpSimulation() const
{if(!bVR)return Walk.CameraUp();auto P=EVACamera->GetUpVector();return {P.X,-P.Y,P.Z};}
void AStarEVAPawn::CalcCamera(float DeltaTime,FMinimalViewInfo& OutResult)
{
    EVACamera->GetCameraView(DeltaTime,OutResult);
}
