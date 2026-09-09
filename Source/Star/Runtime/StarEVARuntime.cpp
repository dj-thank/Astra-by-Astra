#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "EVA/StarEVAPawn.h"
#include "UI/StarHUDWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

void AStarPlayerController::ToggleEVA()
{
    if(!Ship||!Ship->IsReady()||bMainMenu) return;
    if(EVAPawn)
    {
        if(!EVAPawn->CanBoardShip()) { SetStatus(TEXT("船の出入口へ戻ってください。"));return; }
        auto* Previous=EVAPawn.Get();EVAPawn=nullptr;
        Possess(Ship);SetViewTarget(Ship);Previous->Destroy();
        Ship->SetEVAAudio(false);Ship->PlaySoundEvent(TEXT("EnterHatch"));
        ClearInputHandoff();SetFlightPaused(false);
        if(PlayerCameraManager) PlayerCameraManager->StartCameraFade(1,0,0.5f,FLinearColor::Black,false,false);
        SetStatus(TEXT("帰船しました。"));return;
    }
    if(Ship->Simulation()->State().mode!=star::FlightMode::Landed||Ship->Simulation()->State().landedBodyId!="moon")
    { SetStatus(TEXT("月面に安全に着陸してから降船できます。"));return; }
    auto* Walker=GetWorld()->SpawnActor<AStarEVAPawn>();
    if(!Walker||!Walker->InitializeFromShip(Ship))
    { if(Walker) Walker->Destroy();SetStatus(TEXT("船外に安全な足場がありません。"));return; }
    EVAPawn=Walker;EVAFootstepDistance=0;
    ClearInputHandoff();bPhotoMode=false;
    Possess(Walker);SetViewTarget(Walker);SetFlightPaused(false);
    Ship->SetEVAAudio(true);Ship->PlaySoundEvent(TEXT("ExitHatch"));
    if(PlayerCameraManager) PlayerCameraManager->StartCameraFade(1,0,0.6f,FLinearColor::Black,false,false);
    SetStatus(TEXT("WASDで移動、マウスで視点、Hで帰船。ツアーは船で待機します。"),10);
}

void AStarPlayerController::TickEVA(double Dt)
{
    if(!EVAPawn||!Ship) return;
    const auto Key=[&](const FKey& K){return IsInputKeyDown(K)?1.0:0.0;};
    float MouseX=0,MouseY=0;GetInputMouseDelta(MouseX,MouseY);
    if(bEVAQA)MouseX=MouseY=0; // Reproducible automated view; normal mouse path unchanged.
    // Rebase into the current celestial frame before measuring actual footsteps.
    // The Moon's astronomical translation/rotation is not walking speed.
    EVAPawn->AdvanceWalking(0,FVector2D::ZeroVector,0,0,true);
    const auto Previous=EVAPawn->PositionAbsoluteMeters();
    const FVector2D Move=bEVAQA?EVAQAMove:FVector2D(Key(EKeys::D)-Key(EKeys::A),Key(EKeys::W)-Key(EKeys::S));
    EVAPawn->AdvanceWalking(Dt,Move,
        MouseX*0.15f*LookSensitivity,-MouseY*0.15f*LookSensitivity,bFlightPaused);
    const double Walked=(EVAPawn->PositionAbsoluteMeters()-Previous).Length();
    if(!bFlightPaused)
    {
        EVAFootstepDistance+=Walked;
        if(EVAFootstepDistance>=0.75) { EVAFootstepDistance=FMath::Fmod(EVAFootstepDistance,0.75);Ship->PlaySoundEvent(TEXT("Footstep")); }
    }
    // Only presentation follows the walker; the parked ship remains untouched.
    auto Presentation=Ship->Simulation()->State();
    Presentation.positionMeters=EVAPawn->PositionAbsoluteMeters();
    Ship->Director()->UpdateScene(Presentation,Ship->OriginMeters(),EVAPawn->CameraAbsoluteMeters(),bFlightPaused?0:Dt,true);
    UpdateSnapshot(Dt);
    Snapshot.bOnFoot=true;Snapshot.bCockpitView=false;
    Snapshot.BodyName=TEXT("月面 EVA");Snapshot.SpeedMps=Dt>0?Walked/Dt:0;Snapshot.AltitudeM=1.7;
    Snapshot.MissionTitle=TEXT("月面を歩く");Snapshot.MissionDetail=TEXT("出入口の近くでHを押すと船へ戻れます。");
    Snapshot.bCanScan=false;Snapshot.ScanProgress=0;
    if(const auto* Moon=Ship->Director()->Catalog().Find(TEXT("moon")))
    {
        const auto Local=Moon->Definition.bodyFixedToSimulation.Conjugate().Rotate(
            (EVAPawn->PositionAbsoluteMeters()-Moon->Definition.centerMeters).Normalized());
        Snapshot.LatitudeDeg=FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Local.z,-1.0,1.0)));
        Snapshot.LongitudeDeg=FMath::RadiansToDegrees(FMath::Atan2(Local.y,Local.x));
    }
    Snapshot.ControllerName=TEXT("WASD 移動 / マウス 視点 / H 帰船");
    Snapshot.TargetName=TEXT("着陸船");
    Snapshot.TargetDistanceM=EVAPawn->DistanceToBoardingPointMeters();
    const auto Direction=(Ship->Simulation()->State().positionMeters-EVAPawn->CameraAbsoluteMeters()).Normalized();
    const double A=star::Vec3d::Dot(Direction,EVAPawn->CameraForwardSimulation());
    const double R=star::Vec3d::Dot(Direction,EVAPawn->CameraRightSimulation());
    const double U=star::Vec3d::Dot(Direction,EVAPawn->CameraUpSimulation());
    const double Aspect=Snapshot.RenderHeight>0?double(Snapshot.RenderWidth)/Snapshot.RenderHeight:16.0/9.0;
    const double Tan=FMath::Tan(FMath::DegreesToRadians(42.5));
    double X=R/FMath::Max(0.001,FMath::Abs(A))/Tan,Y=-U/FMath::Max(0.001,FMath::Abs(A))/Tan*Aspect;
    Snapshot.bHasTargetDirection=true;Snapshot.bTargetBehind=A<=0;
    Snapshot.bTargetOnScreen=A>0&&FMath::Abs(X)<=1&&FMath::Abs(Y)<=1;
    if(A<=0) {X=R;Y=-U*Aspect;if(FMath::Abs(X)+FMath::Abs(Y)<1e-6)X=1;}
    const double Scale=Snapshot.bTargetOnScreen?1.0:FMath::Max(1.0,FMath::Max(FMath::Abs(X)/0.82,FMath::Abs(Y)/0.76));
    Snapshot.TargetScreenPositionNormalized=FVector2D(.5+.5*X/Scale,.5+.5*Y/Scale);
    Snapshot.TargetYawDeg=FMath::RadiansToDegrees(FMath::Atan2(R,A));
    Snapshot.TargetPitchDeg=FMath::RadiansToDegrees(FMath::Atan2(U,FMath::Sqrt(A*A+R*R)));
    if(Clock>=StatusExpires) Snapshot.StatusText=EVAPawn->CanBoardShip()?TEXT("Hで帰船できます。"):
        EVAPawn->MovementBlocked()?TEXT("この先は急斜面・段差、または船体です。"):
        FString::Printf(TEXT("出入口まで %.0f m / H 帰船"),EVAPawn->DistanceToBoardingPointMeters());
    Ship->UpdatePresentation(Snapshot,bFlightPaused,MasterVolume);
    if(HUD) HUD->ApplySnapshot(Snapshot);
}
