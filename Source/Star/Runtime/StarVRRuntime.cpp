#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "EVA/StarEVAPawn.h"
#include "UI/StarHUDWidget.h"
#include "UI/StarInputSettingsWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Camera/CameraComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

void AStarPlayerController::RecenterVR()
{
    UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0,EOrientPositionSelector::OrientationAndPosition);
}

void AStarPlayerController::UpdateVR()
{
    if(!bVRRequested||!Ship)return;
    bool UsesFocus=false,HasFocus=true;
    UHeadMountedDisplayFunctionLibrary::GetVRFocusState(UsesFocus,HasFocus);
    const bool Ready=UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected()&&
        UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled()&&
        UHeadMountedDisplayFunctionLibrary::HasValidTrackingPosition()&&(!UsesFocus||HasFocus);
    if(auto* Limit=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
        if(Limit->GetFloat()!=(Ready?0.0f:30.0f))Limit->Set(Ready?0.0f:30.0f,ECVF_SetByCode);
    if(!Ready&&bSessionStarted&&!bFlightPaused){
        SetFlightPaused(true);
        SetStatus(TEXT("VRの接続・追跡を確認してください。復帰後に手動で飛行を再開します。"));
    }
    if(!Ready)return;
    if(!VRPanel)
    {
        // A seated, world-space panel: it stays in the cockpit when the head turns.
        VRPanel=NewObject<UWidgetComponent>(Ship,TEXT("VRMenuPanel"));
        Ship->AddInstanceComponent(VRPanel);
        VRPanel->SetupAttachment(Ship->VRTrackingOrigin());
        VRPanel->SetWidgetSpace(EWidgetSpace::World);
        VRPanel->SetDrawSize(FVector2D(1920,1080));
        VRPanel->SetBlendMode(EWidgetBlendMode::Transparent);
        VRPanel->SetTwoSided(true);
        VRPanel->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Star/Materials/M_VRPanel.M_VRPanel")));
        VRPanel->SetRelativeLocation(FVector(180,0,0));
        VRPanel->SetRelativeRotation(FRotator(0,180,0));
        VRPanel->SetRelativeScale3D(FVector(0.13));
        VRPanel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        VRPanel->SetCollisionResponseToAllChannels(ECR_Ignore);
        VRPanel->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        VRPanel->RegisterComponent();
        VRPointer=NewObject<UWidgetInteractionComponent>(Ship,TEXT("VRGazePointer"));
        Ship->AddInstanceComponent(VRPointer);
        VRPointer->SetupAttachment(Ship->FindComponentByClass<UCameraComponent>());
        VRPointer->InteractionSource=EWidgetInteractionSource::World;
        VRPointer->InteractionDistance=500;
        VRPointer->RegisterComponent();
        if(auto* Limit=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))Limit->Set(0,ECVF_SetByCode);
        UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Local);
        UE_LOG(LogTemp,Display,TEXT("STAR PCVR: runtime=%s connected=%d enabled=%d. Device acceptance requires the actual headset."),
            *UHeadMountedDisplayFunctionLibrary::GetHMDDeviceName().ToString(),
            UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected(),UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled());
    }
    USceneComponent* Seat=EVAPawn?EVAPawn->VRTrackingOrigin():Ship->VRTrackingOrigin();
    if(VRPanel->GetAttachParent()!=Seat)VRPanel->AttachToComponent(Seat,FAttachmentTransformRules::KeepRelativeTransform);
    UCameraComponent* Camera=EVAPawn?EVAPawn->FindComponentByClass<UCameraComponent>():Ship->FindComponentByClass<UCameraComponent>();
    if(VRPointer->GetAttachParent()!=Camera)VRPointer->AttachToComponent(Camera,FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    UUserWidget* Active=InputSettings?static_cast<UUserWidget*>(InputSettings.Get()):static_cast<UUserWidget*>(HUD.Get());
    if(VRPanel->GetWidget()!=Active)VRPanel->SetWidget(Active);
    const bool Menu=InputSettings||(HUD&&HUD->IsMenuOpen());
    VRPointer->bEnableHitTesting=Menu;
    if(auto* UI=VRPanel->GetMaterialInstance())
    {
        const auto Hit=VRPointer->Get2DHitLocation();
        UI->SetVectorParameterValue(TEXT("PointerUV"),FLinearColor(Hit.X/1920.0f,Hit.Y/1080.0f,0,1));
        UI->SetScalarParameterValue(TEXT("PointerVisible"),Menu&&VRPointer->GetHoveredWidgetComponent()==VRPanel.Get()?1:0);
    }
    if(Menu)
    {
        const float Scroll=Axes.FindRef(TEXT("VRScroll"));
        if(FMath::Abs(Scroll)>0.2f)VRPointer->ScrollWheel(Scroll*GetWorld()->GetDeltaSeconds()*6);
    }
    // Gaze selects the target; Enter or the right Touch trigger clicks it.
    // These keys never become flight controls while the menu is open.
    if(Menu&&(WasInputKeyJustPressed(EKeys::Enter)||WasInputKeyJustPressed(EKeys::OculusTouch_Right_Trigger_Click)))
        VRPointer->PressPointerKey(EKeys::LeftMouseButton);
    if(WasInputKeyJustReleased(EKeys::Enter)||WasInputKeyJustReleased(EKeys::OculusTouch_Right_Trigger_Click))
        VRPointer->ReleasePointerKey(EKeys::LeftMouseButton);
    if(WasInputKeyJustPressed(EKeys::Home))RecenterVR();
}
