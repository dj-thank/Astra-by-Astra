#include "Runtime/StarPlayerController.h"
#include "Simulation/EarthFlight.h"
#include "Misc/App.h"
#include "AudioMixerBlueprintLibrary.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Runtime/StarShipPawn.h"
#include "EVA/StarEVAPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Runtime/StarDataCatalog.h"
#include "UI/StarHUDWidget.h"
#include "UI/StarInputSettingsWidget.h"
#include "Exploration/StarExplorationSubsystem.h"
#include "StarFlightInputModule.h"
#include "StarFlightKeys.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerInput.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include <cfloat>

namespace {
bool IsEarthFlightPose(const FString& Pose) {
    return Pose==TEXT("flight")||Pose==TEXT("nightflight")||Pose==TEXT("sunriseflight")||Pose==TEXT("sunsetflight")||Pose==TEXT("orbitflight");
}
star::EarthFlightPreset EarthFlightPresetForPose(const FString& Pose) {
    if(Pose==TEXT("nightflight")) return star::EarthFlightPreset::Night;
    if(Pose==TEXT("sunriseflight")) return star::EarthFlightPreset::Sunrise;
    if(Pose==TEXT("sunsetflight")) return star::EarthFlightPreset::Sunset;
    if(Pose==TEXT("orbitflight")) return star::EarthFlightPreset::Orbit;
    return star::EarthFlightPreset::Coast;
}
void ConfigureFlightCaptureView(AStarShipPawn* Ship,star::EarthFlightPreset Preset,const star::BodyDefinition& Earth) {
    // Capture framing only. Ordinary voyages have no preset-dependent state.
    if(!Ship->IsVRRequested()&&Ship->IsCockpitView())Ship->ToggleView();
    const auto SetCameraPitch=[&](float Yaw,float DesiredPitch) {
        Ship->RecenterLook();Ship->SetLook(Yaw,0,false);
        const auto LocalForward=Ship->Simulation()->State().orientation.Conjugate().Rotate(Ship->CameraForwardSimulation());
        const float ActualPitch=static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(LocalForward.z,FMath::Sqrt(LocalForward.x*LocalForward.x+LocalForward.y*LocalForward.y))));
        Ship->SetLook(0,DesiredPitch-ActualPitch,false);
    };
    if(Preset==star::EarthFlightPreset::Sunrise||Preset==star::EarthFlightPreset::Sunset) {
        const double Radius=(Ship->Simulation()->State().positionMeters-Earth.centerMeters).Length();
        const float Dip=static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(Earth.radiusMeters/Radius)));
        SetCameraPitch(Preset==star::EarthFlightPreset::Sunset?156:-24,-Dip+3);
    } else if(Preset==star::EarthFlightPreset::Orbit) {
        SetCameraPitch(0,-88);
    }
}
}

AStarPlayerController::AStarPlayerController()
{
    bVRRequested=FParse::Param(FCommandLine::Get(),TEXT("StarVR"));
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    bShowMouseCursor=true;
}
void AStarPlayerController::BeginPlay()
{
    Super::BeginPlay();
    bVRRequested=FParse::Param(FCommandLine::Get(),TEXT("StarVR"));
    Exploration=GetGameInstance()->GetSubsystem<UStarExplorationSubsystem>();
    HUD=CreateWidget<UStarHUDWidget>(this,UStarHUDWidget::StaticClass());
    if(HUD)
    {
        HUD->OnAction.BindUObject(this,&AStarPlayerController::HandleAction);
        HUD->OnDateTime.BindUObject(this,&AStarPlayerController::SetWorldDateTime);
        if(!bVRRequested)HUD->AddToViewport(10);
        HUD->SetMainMenuVisible(true);
        HUD->ApplyUserSettings(UIScale,MasterVolume,LookSensitivity);
    }
    bBenchmark=FParse::Param(FCommandLine::Get(),TEXT("StarBenchmark"));
    bBenchmarkShowUI=FParse::Param(FCommandLine::Get(),TEXT("StarBenchmarkUI"));
    bGuidedTourTest=FParse::Param(FCommandLine::Get(),TEXT("StarTourTest"));
    bEVAQA=FParse::Param(FCommandLine::Get(),TEXT("StarEVAQA"));
    bLocalFlightQA=FParse::Param(FCommandLine::Get(),TEXT("StarLocalFlightQA"));
    bNavigationQA=bLocalFlightQA||FParse::Param(FCommandLine::Get(),TEXT("StarNavigationQA"));
    FParse::Value(FCommandLine::Get(),TEXT("StarEVAPath="),EVAQADirectory);
    if(bEVAQA&&EVAQADirectory.IsEmpty()) { bEVAQA=false;UE_LOG(LogTemp,Error,TEXT("StarEVAQA requires isolated StarEVAPath")); }
    if((bEVAQA||bBenchmark||bGuidedTourTest)&&FParse::Param(FCommandLine::Get(),TEXT("StarAudioCapture")))
    {
        // Automated captures may be unfocused; Windows otherwise mutes the
        // entire master bus before the capture. Normal focus/pause stays intact.
        FApp::SetUnfocusedVolumeMultiplier(1.0f);
        UE_LOG(LogTemp,Display,TEXT("STAR explicit QA audio capture: unfocused output enabled for this process"));
    }
    FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkStart="),BenchmarkStart);
    FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkCount="),BenchmarkCount);
    FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkSeconds="),BenchmarkStageSeconds);
    FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkShot="),BenchmarkShotSeconds);
    FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkThrottle="),BenchmarkThrottle);
    FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkYaw="),BenchmarkLookYaw);
    bBenchmarkPitchOverride=FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkPitch="),BenchmarkLookPitch);
    BenchmarkThrottle=FMath::Clamp(BenchmarkThrottle,0.0f,1.0f);
    BenchmarkStart=FMath::Clamp(BenchmarkStart,0,3);
    BenchmarkCount=FMath::Clamp(BenchmarkCount,1,4-BenchmarkStart);
    BenchmarkStageSeconds=FMath::Clamp(BenchmarkStageSeconds,8.0,360.0);
    BenchmarkShotSeconds=FMath::Clamp(BenchmarkShotSeconds,1.0,BenchmarkStageSeconds-1.0);
    BenchmarkStage=BenchmarkStart-1;
    BenchmarkStageTime=BenchmarkStageSeconds+1;
    FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkPath="),BenchmarkPath);
    if(BenchmarkPath.IsEmpty()) BenchmarkPath=FPaths::ProjectSavedDir()/TEXT("STAR-QA");
    FParse::Value(FCommandLine::Get(),TEXT("StarTourPath="),GuidedTourQADirectory);
    if(GuidedTourQADirectory.IsEmpty()) GuidedTourQADirectory=FPaths::ProjectSavedDir()/TEXT("STAR-QA/guided-tour-v1");
    if(bNavigationQA) ConfigureNavigationQA(); else ConfigureAcceptance();
    SetFlightPaused(true);
}
void AStarPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* Plugin=FStarFlightInputModule::GetIfAvailable()) Plugin->SetGameplayEnabled(false);
    if(InputSettings) { InputSettings->OnClose.Unbind();InputSettings->RemoveFromParent(); }
    if(HUD) { HUD->OnAction.Unbind();HUD->RemoveFromParent(); }
    Super::EndPlay(Reason);
}
void AStarPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    ConfigureInput();
}
void AStarPlayerController::ConfigureInput()
{
    auto* Enhanced=Cast<UEnhancedInputComponent>(InputComponent);
    auto* Local=GetLocalPlayer();
    if(!Enhanced||!Local) return;
    auto* Subsystem=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    if(!Subsystem) return;
    FlightMappings=NewObject<UInputMappingContext>(this,TEXT("STARFlightMappings"));
    const auto AddDigital=[&](FName Name,const TArray<FKey>& Keys)
    {
        auto* Action=NewObject<UInputAction>(this);
        Action->ValueType=EInputActionValueType::Boolean;
        Actions.Add(Action);
        for(const FKey& Key:Keys) FlightMappings->MapKey(Action,Key);
        Enhanced->BindAction(Action,ETriggerEvent::Started,this,&AStarPlayerController::InputStarted,Name);
        Enhanced->BindAction(Action,ETriggerEvent::Completed,this,&AStarPlayerController::InputCompleted,Name);
        Enhanced->BindAction(Action,ETriggerEvent::Canceled,this,&AStarPlayerController::InputCompleted,Name);
    };
    const auto AddAxis=[&](FName Name,const FKey& Key)
    {
        auto* Action=NewObject<UInputAction>(this);
        Action->ValueType=EInputActionValueType::Axis1D;
        Actions.Add(Action);
        FlightMappings->MapKey(Action,Key);
        Enhanced->BindAction(Action,ETriggerEvent::Triggered,this,&AStarPlayerController::AxisChanged,Name);
        Enhanced->BindAction(Action,ETriggerEvent::Completed,this,&AStarPlayerController::AxisCompleted,Name);
        Enhanced->BindAction(Action,ETriggerEvent::Canceled,this,&AStarPlayerController::AxisCompleted,Name);
    };
    AddAxis(TEXT("Yaw"),FStarFlightKeys::Yaw);
    AddAxis(TEXT("Pitch"),FStarFlightKeys::Pitch);
    AddAxis(TEXT("Roll"),FStarFlightKeys::Roll);
    AddAxis(TEXT("LookX"),FStarFlightKeys::LookX);
    AddAxis(TEXT("LookY"),FStarFlightKeys::LookY);
    AddAxis(TEXT("PadYaw"),EKeys::Gamepad_LeftX);
    AddAxis(TEXT("PadPitch"),EKeys::Gamepad_LeftY);
    AddAxis(TEXT("PadLookX"),EKeys::Gamepad_RightX);
    AddAxis(TEXT("PadLookY"),EKeys::Gamepad_RightY);
    AddAxis(TEXT("PadThrottle"),EKeys::Gamepad_RightTriggerAxis);
    AddAxis(TEXT("PadBrake"),EKeys::Gamepad_LeftTriggerAxis);
    AddDigital(TEXT("ToggleView"),{EKeys::V,FStarFlightKeys::ToggleView,EKeys::Gamepad_FaceButton_Top});
    AddDigital(TEXT("ToggleGear"),{EKeys::G,FStarFlightKeys::ToggleGear,EKeys::Gamepad_FaceButton_Left});
    AddDigital(TEXT("ToggleCruise"),{EKeys::C,FStarFlightKeys::ToggleCruise,EKeys::Gamepad_RightThumbstick});
    AddDigital(TEXT("Brake"),{EKeys::SpaceBar,FStarFlightKeys::Brake,EKeys::Gamepad_FaceButton_Right});
    AddDigital(TEXT("Scan"),{EKeys::R,FStarFlightKeys::Scan,EKeys::Gamepad_FaceButton_Bottom});
    AddDigital(TEXT("Takeoff"),{FStarFlightKeys::Aux11,EKeys::Gamepad_LeftThumbstick});
    AddDigital(TEXT("FlightContext"),{EKeys::F});
    AddDigital(TEXT("ToggleAutopilot"),{EKeys::O});
    AddDigital(TEXT("NavigationSafeBrake"),{EKeys::B});
    AddDigital(TEXT("TargetNext"),{EKeys::Tab,FStarFlightKeys::TargetNext,EKeys::Gamepad_Special_Left});
    AddDigital(TEXT("TogglePhoto"),{EKeys::P,FStarFlightKeys::TogglePhoto});
    AddDigital(TEXT("RecenterLook"),{EKeys::Home,FStarFlightKeys::RecenterLook});
    if(bVRRequested)AddDigital(TEXT("VRSelect"),{EKeys::Enter,EKeys::OculusTouch_Right_Trigger_Click});
    if(bVRRequested)AddAxis(TEXT("VRScroll"),EKeys::OculusTouch_Right_Thumbstick_Y);
    AddDigital(TEXT("Precision"),{EKeys::Z,FStarFlightKeys::Precision});
    AddDigital(TEXT("Pause"),{EKeys::Escape,FStarFlightKeys::Pause,EKeys::Gamepad_Special_Right});
    AddDigital(TEXT("ManualTakeover"),{EKeys::M});
    AddDigital(TEXT("ToggleEVA"),{EKeys::H});
    AddDigital(TEXT("Save"),{EKeys::F5,FStarFlightKeys::Aux12});
    AddDigital(TEXT("Load"),{EKeys::F9,FStarFlightKeys::Aux13});
    AddDigital(TEXT("PhotoCapture"),{EKeys::F12});
    AddDigital(TEXT("DescendOrCapture"),{FStarFlightKeys::Aux14});
    AddDigital(TEXT("TargetEarth"),{FStarFlightKeys::Aux15});
    AddDigital(TEXT("TargetSaturn"),{FStarFlightKeys::Aux16});
    Subsystem->AddMappingContext(FlightMappings,0);
}
void AStarPlayerController::InputStarted(const FInputActionValue&,FName Name)
{
    if(InputSettings) return;
    if(HUD&&HUD->IsMenuOpen()&&Name!=TEXT("Pause")&&Name!=TEXT("Save")&&Name!=TEXT("Load")&&Name!=TEXT("ManualTakeover")&&Name!=TEXT("FlightContext")&&Name!=TEXT("ToggleAutopilot")&&Name!=TEXT("NavigationSafeBrake")) return;
    HandleAction(Name,1);
}
void AStarPlayerController::InputCompleted(const FInputActionValue&,FName Name)
{
    if(Name==TEXT("Scan")) bScanHeld=false;
    if(Name==TEXT("Brake")) bBrakeHeld=false;
    if(Name==TEXT("FlightContext")) bKeyboardLiftHeld=false;
}
void AStarPlayerController::AxisChanged(const FInputActionValue& Value,FName Name) { Axes.FindOrAdd(Name)=Value.Get<float>(); }
void AStarPlayerController::AxisCompleted(const FInputActionValue&,FName Name) { Axes.FindOrAdd(Name)=0; }
void AStarPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if(bQAQuitPending)
    {
#if CSV_PROFILER
        if(FCsvProfiler::IsCapturing()||FCsvProfiler::Get()->IsWritingFile()||FCsvProfiler::Get()->IsEndCapturePending())return;
#endif
        ConsoleCommand(TEXT("quit"),false);return;
    }
    Clock+=DeltaTime;
    if(bNavigationQA&&!CheckNavigationQADeadline()) return;
    if(!Ship) Ship=Cast<AStarShipPawn>(GetPawn());
    if(!Ship) return;
    UpdateVR();
    if(!Ship->IsReady())
    {
        Ship->InitializeFlight();
        Snapshot.StatusText=Ship->Error();
        if(HUD) HUD->ApplySnapshot(Snapshot);
        return;
    }
    if(!bVisualSettingsApplied)
    {
        SetEnhancedStarsEnabled(bEnhancedStars);
        bFlightAssistPreference=Ship->Simulation()->FlightAssistEnabled();
        bVisualSettingsApplied=true;
    }
    if(!bNavigationInitialized) ResetNavigation();
    if(bEVAQA) { TickEVAQA(DeltaTime);return; }
    if(bGuidedTourTest&&!bGuidedTourTestStarted)
    {
        StartGuidedTour();
        bGuidedTourTestStarted=bGuidedTour;
    }
    if(bBenchmark) UpdateBenchmark(DeltaTime);
    if(!bVRRequested&&!bBenchmark&&!bAcceptance&&!bGuidedTourTest&&!bNavigationQA&&FSlateApplication::IsInitialized()&&!FSlateApplication::Get().IsActive()&&!bFlightPaused)
    { SetFlightPaused(true);SetStatus(TEXT("ウィンドウが非アクティブになったため一時停止しました。")); }
    auto* Plugin=FStarFlightInputModule::GetIfAvailable();
    if(EVAPawn) { if(!bFlightPaused)Ship->AdvanceWorldClock(DeltaTime);TickEVA(DeltaTime);return; }
    if(Plugin)
    {
        const auto InputStatus=Plugin->GetStatus();
        // Only stable-device focus loss is suppressed by navigation QA. A
        // disconnect/reconnect generation change still runs the real pause.
        const bool QAFocusOnly=bNavigationQA&&bNavigationQADeviceObserved&&!InputStatus.bFocused&&
            InputStatus.bConnected==bNavigationQADeviceConnected&&InputStatus.ConnectionGeneration==NavigationQADeviceGeneration;
        if(bNavigationQA)
        {
            bNavigationQADeviceObserved=true;bNavigationQADeviceConnected=InputStatus.bConnected;
            NavigationQADeviceGeneration=InputStatus.ConnectionGeneration;
        }
        if(InputStatus.bRequiresPause&&!bFlightPaused&&!bBenchmark&&!bAcceptance&&!bGuidedTour&&!QAFocusOnly)
        { SetFlightPaused(true);SetStatus(TEXT("入力機器を確認してください。安全のため一時停止しました。")); }
    }
    if(bGuidedTour) UpdateGuidedTourCommand(DeltaTime);
    if(bNavigationQA) UpdateNavigationQABeforeFlight();
    star::FlightInput Controls;
    Controls.paused=bFlightPaused||bPhotoMode;
    Controls.brake=bBrakeHeld||Axes.FindRef(TEXT("PadBrake"))>0.4f;
    Controls.takeoff=bTakeoffRequested;
    if(!Controls.paused&&!bAcceptance&&!bGuidedTour)
    {
        const auto Key=[&](const FKey& K) { return IsInputKeyDown(K)?1.0:0.0; };
        KeyboardThrottle=FMath::Clamp(KeyboardThrottle+static_cast<float>((Key(EKeys::W)-Key(EKeys::S))*DeltaTime*0.35),0.0f,1.0f);
        Controls.yaw=Axes.FindRef(TEXT("Yaw"))+Axes.FindRef(TEXT("PadYaw"))+Key(EKeys::L)-Key(EKeys::J);
        Controls.pitch=Axes.FindRef(TEXT("Pitch"))+Axes.FindRef(TEXT("PadPitch"))+Key(EKeys::I)-Key(EKeys::K);
        Controls.roll=Axes.FindRef(TEXT("Roll"))+Key(EKeys::E)-Key(EKeys::Q);
        Controls.strafeRight=Key(EKeys::D)-Key(EKeys::A);
        Controls.strafeUp=Key(EKeys::PageUp)+(bKeyboardLiftHeld?Key(EKeys::F):0.0)+Key(FStarFlightKeys::Aux11)+Key(EKeys::Gamepad_LeftShoulder)
            -Key(EKeys::PageDown)-Key(FStarFlightKeys::Aux14)-Key(EKeys::Gamepad_RightShoulder);
        Controls.throttle=KeyboardThrottle;
        Controls.hasThrottle=true;
        const float PadThrottle=Axes.FindRef(TEXT("PadThrottle"));
        if(PadThrottle>0.02f) Controls.throttle=PadThrottle;
        if(Plugin&&Plugin->GetStatus().bArmed) Controls.throttle=Plugin->GetControlFrame().Throttle;
        float MouseX=0,MouseY=0;
        GetInputMouseDelta(MouseX,MouseY);
        if(IsInputKeyDown(EKeys::RightMouseButton)&&!IsInputKeyDown(EKeys::LeftAlt)&&!IsInputKeyDown(EKeys::RightAlt))
        {
            Controls.yaw+=MouseX*0.018*LookSensitivity;
            Controls.pitch-=MouseY*0.018*LookSensitivity;
        }
    }
    if(!bFlightPaused||bPhotoMode)
    {
        float LookX=Axes.FindRef(TEXT("LookX"))+Axes.FindRef(TEXT("PadLookX"));
        float LookY=Axes.FindRef(TEXT("LookY"))+Axes.FindRef(TEXT("PadLookY"));
        if(IsInputKeyDown(EKeys::Right)) LookX+=1;
        if(IsInputKeyDown(EKeys::Left)) LookX-=1;
        if(IsInputKeyDown(EKeys::Up)) LookY+=1;
        if(IsInputKeyDown(EKeys::Down)) LookY-=1;
        if((bPhotoMode||bGuidedTour||IsInputKeyDown(EKeys::LeftAlt)||IsInputKeyDown(EKeys::RightAlt))&&IsInputKeyDown(EKeys::RightMouseButton))
        {
            float X=0,Y=0;GetInputMouseDelta(X,Y);
            LookX+=X/FMath::Max(DeltaTime*40,0.001f);
            LookY-=Y/FMath::Max(DeltaTime*40,0.001f);
        }
        Ship->SetLook(LookX*DeltaTime*55*LookSensitivity,LookY*DeltaTime*45*LookSensitivity,bPhotoMode);
        if(bPhotoMode)
        {
            FVector Move=FVector::ZeroVector;
            if(IsInputKeyDown(EKeys::W)) Move.X+=1;if(IsInputKeyDown(EKeys::S)) Move.X-=1;
            if(IsInputKeyDown(EKeys::D)) Move.Y+=1;if(IsInputKeyDown(EKeys::A)) Move.Y-=1;
            if(IsInputKeyDown(EKeys::E)) Move.Z+=1;if(IsInputKeyDown(EKeys::Q)) Move.Z-=1;
            Ship->SetPhotoOffset(Move*DeltaTime*500);
        }
    }
    if(bGuidedTour) ApplyGuidedTourControls(Controls);
    if(bAcceptance) UpdateAcceptanceBeforeFlight(Controls);
    if(bNavigationQA) InjectNavigationQAInput(Controls);
    ApplyNavigationControls(Controls);
    if(bNavigationQA)
    {
        ObserveNavigationQAControls(Controls);
        if(bNavigationQAFinished) Controls.paused=true;
    }
    const double PreviousSimulationTime=Ship->Simulation()->State().simulationTimeSeconds;
    if(bBenchmark && FParse::Param(FCommandLine::Get(),TEXT("StarEarthFlightQA")))
    {
        // Exercise the ordinary simulation/presentation path with explicit
        // scripted controls. This is never a physical-device test.
        if(FParse::Param(FCommandLine::Get(),TEXT("StarScenicControlsQA")))
        {
            static int32 Step=0;
            static double PauseTime=0;
            static star::Vec3d SavedPosition;
            const auto Event=[&](const TCHAR* Name,bool Pass,double Value=0.0)
            {
                const FString Line=FString::Printf(TEXT("{\"event\":\"%s\",\"pass\":%s,\"value\":%.9f,\"wallSeconds\":%.6f}\n"),Name,Pass?TEXT("true"):TEXT("false"),Value,static_cast<double>(BenchmarkStageTime));
                FFileHelper::SaveStringToFile(Line,*(BenchmarkPath/TEXT("controls-events.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
            };
            if(BenchmarkStageTime>=8&&BenchmarkStageTime<14) Ship->SetLook(static_cast<float>(DeltaTime*60),0,false);
            if(Step==0&&BenchmarkStageTime>=16)
            {
                const auto Before=Ship->Simulation()->State().positionMeters;
                HandleAction(TEXT("EarthOrbit"));
                const double Delta=(Ship->Simulation()->State().positionMeters-Before).Length();
                Event(TEXT("active-voyage-preset-does-not-relocate"),Delta<0.001&&!bPhotoMode,Delta);++Step;
            }
            if(Step==1&&BenchmarkStageTime>=18)
            { HandleAction(TEXT("Pause"));PauseTime=Ship->Simulation()->State().simulationTimeSeconds;Event(TEXT("pause"),bFlightPaused);++Step; }
            if(Step==2&&BenchmarkStageTime>=20)
            {
                const double Delta=Ship->Simulation()->State().simulationTimeSeconds-PauseTime;
                Event(TEXT("pause-freezes-simulation"),FMath::Abs(Delta)<1e-9,Delta);
                HandleAction(TEXT("Resume"));Event(TEXT("resume"),!bFlightPaused);++Step;
            }
            if(Step==3&&BenchmarkStageTime>=22)
            { HandleAction(TEXT("ToggleView"));Event(TEXT("cockpit-during-flight"),Ship->IsCockpitView()&&!bFlightPaused&&!bPhotoMode);++Step; }
            if(Step==4&&BenchmarkStageTime>=24)
            { HandleAction(TEXT("ToggleView"));Event(TEXT("chase-during-flight"),!Ship->IsCockpitView()&&!bFlightPaused&&!bPhotoMode);++Step; }
            if(Step==5&&BenchmarkStageTime>=26)
            { SavedPosition=Ship->Simulation()->State().positionMeters;Event(TEXT("isolated-save"),SaveGame());++Step; }
            if(Step==6&&BenchmarkStageTime>=28)
            {
                const bool Loaded=LoadGame();const double Delta=(Ship->Simulation()->State().positionMeters-SavedPosition).Length();
                Event(TEXT("isolated-reload-position"),Loaded&&Delta<0.001,Delta);++Step;
            }
        }
        Controls={};Controls.hasThrottle=true;Controls.throttle=BenchmarkThrottle;Controls.paused=bFlightPaused||bPhotoMode;
        Controls.yaw=(!FParse::Param(FCommandLine::Get(),TEXT("StarEarthLayerQA"))&&BenchmarkStageTime>=12.0&&BenchmarkStageTime<15.0)?0.12:0.0;
    }
    if(bBenchmark&&FParse::Param(FCommandLine::Get(),TEXT("StarTimeQA")))
    { TickAstronomyQA();Controls.paused=bFlightPaused||bPhotoMode; }
    if(!Controls.paused) { Ship->AdvanceWorldClock(DeltaTime);Navigation.FollowCelestialFrames(*Ship->Simulation()); }
    Ship->AdvanceFlight(DeltaTime,Controls);
    if(bBenchmark&&BenchmarkStage>=BenchmarkStart&&BenchmarkStage<BenchmarkStart+BenchmarkCount&&FParse::Param(FCommandLine::Get(),TEXT("StarEarthFlightQA")))
    {
        static int32 LastBucket=-1;
        const int32 Bucket=FMath::FloorToInt(BenchmarkStageTime*4.0);
        if(Bucket!=LastBucket)
        {
            LastBucket=Bucket;
            const auto& State=Ship->Simulation()->State();
            const auto& Earth=Ship->Director()->Catalog().Find(TEXT("earth"))->Definition;
            const auto& Sun=Ship->Director()->Catalog().Find(TEXT("sun"))->Definition;
            const auto Camera=Ship->CameraAbsoluteMeters(),Forward=Ship->CameraForwardSimulation();
            const FString Line=FString::Printf(TEXT("{\"wallSeconds\":%.6f,\"simulationSeconds\":%.9f,\"position\":[%.9f,%.9f,%.9f],\"camera\":[%.9f,%.9f,%.9f],\"forward\":[%.9f,%.9f,%.9f],\"altitudeMeters\":%.6f,\"sunClearanceDegrees\":%.9f,\"speedMps\":%.6f,\"recoveries\":%llu,\"paused\":%s,\"observationMode\":%s,\"photoMode\":%s}\n"),
                static_cast<double>(BenchmarkStageTime),State.simulationTimeSeconds,State.positionMeters.x,State.positionMeters.y,State.positionMeters.z,
                Camera.x,Camera.y,Camera.z,Forward.x,Forward.y,Forward.z,(State.positionMeters-Earth.centerMeters).Length()-Earth.radiusMeters,
                star::EarthSunHorizonClearance(Earth,Sun.centerMeters,State.positionMeters)*180/star::Pi,State.velocityMetersPerSecond.Length(),
                static_cast<unsigned long long>(State.recoveryCount),bFlightPaused?TEXT("true"):TEXT("false"),TEXT("false"),bPhotoMode?TEXT("true"):TEXT("false"));
            FFileHelper::SaveStringToFile(Line,*(BenchmarkPath/TEXT("flight-trajectory.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
            const auto Local=Earth.bodyFixedToSimulation.Conjugate().Rotate(State.positionMeters-Earth.centerMeters);
            const FString Astro=FString::Printf(TEXT("{\"engineSeconds\":%.9f,\"utc\":%.9f,\"rate\":%.0f,\"earthLocal\":[%.9f,%.9f,%.9f],\"earthCenter\":[%.9f,%.9f,%.9f],\"sunCenter\":[%.9f,%.9f,%.9f]}\n"),
                BenchmarkStageTime,Ship->Director()->WorldUtc(),Ship->Director()->ClockRate(),Local.x,Local.y,Local.z,
                Earth.centerMeters.x,Earth.centerMeters.y,Earth.centerMeters.z,Sun.centerMeters.x,Sun.centerMeters.y,Sun.centerMeters.z);
            FFileHelper::SaveStringToFile(Astro,*(BenchmarkPath/TEXT("astronomy-trajectory.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
        }
    }
    if(Ship->Simulation()->State().simulationTimeSeconds>PreviousSimulationTime) bTakeoffRequested=false;
    UpdateSnapshot(DeltaTime);
    if(bAcceptance) UpdateAcceptanceAfterFlight();
    if(bGuidedTour) UpdateGuidedTourQA(DeltaTime);
    if(bNavigationQA) UpdateNavigationQAAfterFlight();
    if(HUD) HUD->ApplySnapshot(Snapshot);
    Ship->UpdatePresentation(Snapshot,bFlightPaused||bPhotoMode,MasterVolume);
    if(bBenchmark&&FParse::Param(FCommandLine::Get(),TEXT("StarUnifiedWorldQA")))UpdateUnifiedWorldQA(DeltaTime);
    if(!LastPhotoRequest.IsEmpty()&&IFileManager::Get().FileExists(*LastPhotoRequest))
    { SetStatus(TEXT("写真を保存しました。"));LastPhotoRequest.Reset(); }
}
void AStarPlayerController::SetMenuInputMode()
{
    if(bVRRequested){SetInputMode(FInputModeGameOnly());bShowMouseCursor=false;return;}
    FInputModeGameAndUI Mode;
    if(InputSettings) Mode.SetWidgetToFocus(InputSettings->TakeWidget());
    else if(HUD) Mode.SetWidgetToFocus(HUD->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Mode.SetHideCursorDuringCapture(false);
    SetInputMode(Mode);
    bShowMouseCursor=true;
}
void AStarPlayerController::SetFlightPaused(bool Value)
{
    if(Value) bNavigationBrakingRecovery=false;
    Navigation.SetPaused(Value||bPhotoMode);
    if(Ship&&Ship->Simulation()&&!NavigationBypassed())
    {
        NavigationCommand=Navigation.Tick(*Ship->Simulation());
        bNavigationSafetyPause=NavigationCommand.requiresSafetyPause;
        if(bNavigationSafetyPause&&!bNavigationBrakingRecovery)
        {
            Value=true;
            Navigation.SetPaused(true);
            SetStatus(TEXT("高速状態を安全停止中です。目標に機首を向けた状態でFを押すと航行を確認できます。"),8);
        }
    }
    bFlightPaused=Value;
    bScanHeld=bBrakeHeld=false;
    Axes.Reset();
    if(Value)
    {
        bTakeoffRequested=false;
        bKeyboardLiftHeld=false;
        KeyboardThrottle=0;
        if(Ship&&Ship->Simulation()) Ship->Simulation()->SetThrottle(0);
    }
    if(auto* Plugin=FStarFlightInputModule::GetIfAvailable()) Plugin->SetGameplayEnabled(!Value&&!bPhotoMode&&!bGuidedTour&&!EVAPawn);
    if(Value||bPhotoMode) SetMenuInputMode();
    else { FInputModeGameOnly Mode;Mode.SetConsumeCaptureMouseDown(true);SetInputMode(Mode);bShowMouseCursor=false; }
}
void AStarPlayerController::OpenInputSettings()
{
    if(InputSettings) return;
    SetFlightPaused(true);
    InputSettings=CreateWidget<UStarInputSettingsWidget>(this,UStarInputSettingsWidget::StaticClass());
    InputSettings->OnClose.BindUObject(this,&AStarPlayerController::CloseInputSettings);
    InputSettings->SetUIScale(UIScale);
    if(!bVRRequested)InputSettings->AddToViewport(30);
    SetMenuInputMode();
}
void AStarPlayerController::CloseInputSettings()
{
    if(InputSettings) { InputSettings->OnClose.Unbind();InputSettings->RemoveFromParent();InputSettings=nullptr; }
    SetFlightPaused(true);
    SetMenuInputMode();
    SetStatus(TEXT("設定を閉じました。スロットルを戻してから飛行を再開してください。"));
}
void AStarPlayerController::SelectTarget(const FString& Id)
{
    if(!Ship||!Ship->Simulation()) return;
    if(Ship->Simulation()->SetTarget(TCHAR_TO_UTF8(*Id)))
    {
        Navigation.SelectDestination(*Ship->Simulation(),TCHAR_TO_UTF8(*Id));
        NavigationCommand=Navigation.Tick(*Ship->Simulation());
        if(!NavigationBypassed()&&NavigationCommand.requiresSafetyPause) SetFlightPaused(true);
        if(Exploration) Exploration->NotifyAction(FName(*(TEXT("Target")+Id)));
        Ship->PlaySoundEvent(TEXT("View"));
    }
}
bool AStarPlayerController::NavigationBypassed() const
{
    // Existing, explicit scripted acceptance drivers only. Future navigation QA
    // must exercise the same permission gate as ordinary free flight.
    return bGuidedTour||bBenchmark||bAcceptance||bEVAQA;
}
void AStarPlayerController::ResetNavigation()
{
    Navigation.ResetAfterLoad();
    bKeyboardLiftHeld=false;
    bNavigationSafetyPause=false;
    bNavigationBrakingRecovery=false;
    bNavigationInitialized=Ship&&Ship->Simulation();
    if(!bNavigationInitialized) return;
    Navigation.SelectDestination(*Ship->Simulation(),Ship->Simulation()->State().targetBodyId);
    Navigation.SetPaused(bFlightPaused||bPhotoMode);
    NavigationCommand=Navigation.Tick(*Ship->Simulation());
}
void AStarPlayerController::ConfirmNavigationTransfer()
{
    if(!Ship||!Ship->Simulation()||!bSessionStarted||bMainMenu||bPhotoMode||InputSettings||EVAPawn||NavigationBypassed()) return;
    if(!bNavigationQA&&FSlateApplication::IsInitialized()&&!FSlateApplication::Get().IsActive()) return;
    if(bNavigationBrakingRecovery) { SetStatus(TEXT("安全減速中です。停止するまで航行を開始できません。"));return; }
    auto& Sim=*Ship->Simulation();
    const bool WasPaused=bFlightPaused;
    // Temporarily release only the pilot latch. No simulation frame can advance
    // before confirmation succeeds, including an old save above local speed.
    Navigation.SetPaused(false);
    const bool Confirmed=Navigation.ConfirmTransfer(Sim);
    NavigationCommand=Navigation.Tick(Sim);
    if(!Confirmed||!NavigationCommand.highSpeedAuthorized)
    {
        Navigation.SetPaused(WasPaused);
        const auto* Target=Ship->Director()->Catalog().Find(Sim.State().targetBodyId);
        const FString Name=Target?Target->Name:TEXT("目標");
        if(NavigationCommand.status==star::navigation::NavigationStatus::RouteBlocked)
            SetStatus(TEXT("安全な経路を作れませんでした。航行は許可されていません。"),8);
        else if(Sim.State().throttleNeutralRequired)
            SetStatus(TEXT("スロットルを一度戻してから航行を確認してください。"),8);
        else if(NavigationCommand.destinationBodyId==NavigationCommand.localBodyId)
            SetStatus(Name+TEXT("付近の通常飛行です。Oで停止・姿勢保持を開始できます。"),8);
        else SetStatus(Name+FString::Printf(TEXT("へ機首を向けてFで確認してください（差 %.1f° / 許容15°）。"),NavigationCommand.headingErrorDegrees),8);
        return;
    }
    bNavigationSafetyPause=false;
    // Preserve live axes when confirming an already-running alignment AP.
    // A paused confirmation uses the existing device-neutral rearm path.
    if(WasPaused) SetFlightPaused(false);
    const auto* Target=Ship->Director()->Catalog().Find(Sim.State().targetBodyId);
    SetStatus((Target?Target->Name:TEXT("目標"))+FString(TEXT("への航行を確認しました。Cで巡航、Oで自動航行。")),8);
}
void AStarPlayerController::ToggleNavigationAutopilot()
{
    if(!Ship||!Ship->Simulation()||!bSessionStarted||bMainMenu||bPhotoMode||InputSettings||EVAPawn||NavigationBypassed()) return;
    if(bNavigationBrakingRecovery) { SetStatus(TEXT("安全減速中です。停止後に自動操縦を開始してください。"));return; }
    auto& Sim=*Ship->Simulation();
    NavigationCommand=Navigation.Tick(Sim);
    if(NavigationCommand.autopilotActive)
    {
        Navigation.Cancel();KeyboardThrottle=0;Sim.SetThrottle(0);
        if(Navigation.Tick(Sim).requiresSafetyPause) SetFlightPaused(true);
        SetStatus(TEXT("自動操縦を解除しました。惑星間航行の許可も解除しました。"),8);
        return;
    }
    if(bFlightPaused) SetFlightPaused(false);
    if(bFlightPaused) { SetStatus(TEXT("高速状態を安全停止中です。先にFで航行を確認してください。"),8);return; }
    if(!Navigation.StartAutopilot(Sim)) { SetStatus(TEXT("自動操縦を開始できません。目標と航行状態を確認してください。"));return; }
    AutopilotManualThrottle=KeyboardThrottle;
    if(Axes.FindRef(TEXT("PadThrottle"))>0.02f) AutopilotManualThrottle=Axes.FindRef(TEXT("PadThrottle"));
    if(auto* Plugin=FStarFlightInputModule::GetIfAvailable())
        if(Plugin->GetStatus().bArmed) AutopilotManualThrottle=Plugin->GetControlFrame().Throttle;
    NavigationCommand=Navigation.Tick(Sim);
    SetStatus(NavigationCommand.status==star::navigation::NavigationStatus::LocalHold?
        TEXT("現在地で減速し、停止・姿勢を保持します。"):
        NavigationCommand.highSpeedAuthorized?TEXT("確認済みの目標へ自動航行します。操船入力で解除できます。"):
        TEXT("機首を目標へ合わせます。加速にはFで航行の確認が必要です。"),8);
}
void AStarPlayerController::ToggleNavigationSafeBrake()
{
    if(!Ship||!Ship->Simulation()||!bSessionStarted||bMainMenu||bPhotoMode||InputSettings||EVAPawn||NavigationBypassed()) return;
    if(!bNavigationQA&&FSlateApplication::IsInitialized()&&!FSlateApplication::Get().IsActive()) return;
    if(bNavigationBrakingRecovery)
    { SetFlightPaused(true);SetStatus(TEXT("安全減速を中断しました。飛行状態を停止して保持しています。"),8);return; }
    NavigationCommand=Navigation.Tick(*Ship->Simulation());
    if(!NavigationCommand.requiresSafetyPause) return;
    Navigation.Cancel();
    bNavigationBrakingRecovery=true;
    KeyboardThrottle=0;bTakeoffRequested=bKeyboardLiftHeld=false;
    SetFlightPaused(false);
    SetStatus(TEXT("安全に減速しています。推力と操船入力を停止中です。B / Escで中断できます。"),8);
}
void AStarPlayerController::ApplyNavigationControls(star::FlightInput& Controls)
{
    auto& Sim=*Ship->Simulation();
    if(NavigationBypassed()) return;
    NavigationCommand=Navigation.Tick(Sim);
    if(bNavigationBrakingRecovery)
    {
        // Explicit recovery accepts only ordinary braking. Keep braking below
        // the safety-pause threshold until stopped; never assign state vectors.
        Controls=star::FlightInput{};
        Controls.hasThrottle=true;Controls.throttle=0;Controls.brake=true;
        Controls.smoothGuidance=true;Controls.paused=bFlightPaused||bPhotoMode;
        if(Sim.State().velocityMetersPerSecond.Length()<1.0)
        {
            Sim.SetMode(star::FlightMode::Maneuver);
            SetFlightPaused(true);Controls.paused=true;
            SetStatus(TEXT("安全減速が完了しました。通常飛行を再開できます。"),8);
        }
        return;
    }
    if(NavigationCommand.autopilotActive&&!Controls.paused)
    {
        // A steady throttle is not a new override. Camera/scan inputs are absent
        // deliberately; flight axes, translation, braking and throttle movement
        // return control to the pilot and revoke the transfer permission.
        const bool Override=FMath::Abs(Controls.yaw)>0.08||FMath::Abs(Controls.pitch)>0.08||
            FMath::Abs(Controls.roll)>0.08||FMath::Abs(Controls.strafeRight)>0.08||
            FMath::Abs(Controls.strafeUp)>0.08||Controls.brake||Controls.takeoff||
            IsInputKeyDown(EKeys::W)||IsInputKeyDown(EKeys::S)||
            FMath::Abs(Controls.throttle-AutopilotManualThrottle)>0.03;
        if(Override)
        {
            Navigation.Cancel();NavigationCommand=Navigation.Tick(Sim);
            SetStatus(TEXT("操船入力で自動操縦と航行許可を解除しました。"),8);
        }
    }
    if(NavigationCommand.requiresSafetyPause)
    {
        if(!bFlightPaused||!bNavigationSafetyPause) SetFlightPaused(true);
        bNavigationSafetyPause=true;
        Controls=star::FlightInput{};Controls.paused=true;
        return; // Preserve position, velocity and mode while waiting for F.
    }
    bNavigationSafetyPause=false;
    if(Controls.paused) return;
    if(NavigationCommand.autopilotActive)
    {
        Sim.SetMode(NavigationCommand.mode);
        Controls=NavigationCommand.controls;
        return;
    }
    if(!NavigationCommand.highSpeedAuthorized&&Sim.State().mode==star::FlightMode::Cruise)
        Sim.SetMode(star::FlightMode::Maneuver);
    if(NavigationCommand.highSpeedAuthorized&&Sim.State().mode==star::FlightMode::Cruise)
        Controls.smoothGuidance=true;
}
void AStarPlayerController::UpdateNavigationSnapshot()
{
    using star::navigation::NavigationStatus;
    NavigationCommand=Navigation.Tick(*Ship->Simulation());
    Snapshot.bNavigationAvailable=!NavigationBypassed()&&!EVAPawn;
    Snapshot.bAutopilotActive=Snapshot.bNavigationAvailable&&NavigationCommand.autopilotActive;
    Snapshot.bHighSpeedAuthorized=Snapshot.bNavigationAvailable&&NavigationCommand.highSpeedAuthorized;
    Snapshot.bNavigationSafetyPause=Snapshot.bNavigationAvailable&&bNavigationSafetyPause&&!bNavigationBrakingRecovery;
    Snapshot.bNavigationBrakingRecovery=Snapshot.bNavigationAvailable&&bNavigationBrakingRecovery;
    Snapshot.bCanConfirmTransfer=Snapshot.bNavigationAvailable&&NavigationCommand.canConfirmTransfer;
    const FString& Name=Snapshot.TargetName;
    const bool Local=NavigationCommand.localBodyId==NavigationCommand.destinationBodyId;
    Snapshot.TransferActionText=Ship->Simulation()->State().mode==star::FlightMode::Landed?TEXT("F  離陸"):
        Snapshot.bHighSpeedAuthorized?Name+TEXT("への航行：確認済み"):
        TEXT("F  ")+Name+TEXT("への航行を確認");
    Snapshot.AutopilotActionText=Snapshot.bAutopilotActive?TEXT("O  自動操縦を解除"):
        Local?TEXT("O  現在地で停止・姿勢保持"):TEXT("O  ")+Name+TEXT("へ自動操縦");
    switch(NavigationCommand.status)
    {
        case NavigationStatus::ReadyToConfirm: Snapshot.NavigationStatusText=Name+TEXT("へ機首が合いました · Fで航行を確認");break;
        case NavigationStatus::Aligning: Snapshot.NavigationStatusText=Name+TEXT("へ機首合わせ中 · 未確認・加速しません");break;
        case NavigationStatus::TransferAuthorized: Snapshot.NavigationStatusText=Name+TEXT("への航行：確認済み · Cで巡航");break;
        case NavigationStatus::Departing: Snapshot.NavigationStatusText=Name+TEXT("へ自動航行 · 出発天体から離脱中");break;
        case NavigationStatus::Travelling: Snapshot.NavigationStatusText=Name+TEXT("へ自動航行中");break;
        case NavigationStatus::Braking: Snapshot.NavigationStatusText=Name+TEXT("へ自動航行 · 減速・針路調整中");break;
        case NavigationStatus::LocalHold: Snapshot.NavigationStatusText=TEXT("自動操縦 · 現在地で停止・姿勢保持");break;
        case NavigationStatus::Arrived: Snapshot.NavigationStatusText=Name+TEXT("に到着 · 航行許可を解除")+(Snapshot.bAutopilotActive?TEXT(" · 停止保持"):TEXT(""));break;
        case NavigationStatus::RouteBlocked: Snapshot.NavigationStatusText=TEXT("安全な経路なし · 航行は未許可");break;
        case NavigationStatus::StateChanged: Snapshot.NavigationStatusText=TEXT("飛行状態の変更で航行許可を解除しました");break;
        case NavigationStatus::InvalidTarget: case NavigationStatus::Idle: Snapshot.NavigationStatusText=TEXT("目的地を選択してください · 通常飛行");break;
        case NavigationStatus::Paused: Snapshot.NavigationStatusText=bFlightPaused?TEXT("一時停止 · 航行許可なし"):
            Local?TEXT("付近の通常飛行 · 惑星間航行は未許可"):Name+TEXT("への航行：未確認 · 機首を向けてFで確認");break;
        default: Snapshot.NavigationStatusText=Local?TEXT("付近の通常飛行 · 惑星間航行は未許可"):
            Name+FString::Printf(TEXT("への航行：未確認 · 機首差 %.1f°"),NavigationCommand.headingErrorDegrees);break;
    }
    if(Snapshot.bNavigationSafetyPause)
        Snapshot.NavigationStatusText=TEXT("高速状態を安全停止 · ")+Name+FString::Printf(TEXT("へ機首差 %.1f° · Fで確認 / Bで安全に減速"),NavigationCommand.headingErrorDegrees);
    if(Snapshot.bNavigationBrakingRecovery)
        Snapshot.NavigationStatusText=TEXT("安全減速中 · 推力・操船入力停止 · B / Escで中断");
}
void AStarPlayerController::SetWorldDateTime(const FString& Value)
{
    if(!Ship||!Ship->IsReady()) return;
    FString Input=Value.TrimStartAndEnd().Replace(TEXT(" "),TEXT("T"));
    if(!Input.EndsWith(TEXT("Z"))) Input+=TEXT("Z");
    FDateTime Parsed;
    if(!FDateTime::ParseIso8601(*Input,Parsed)||!Ship->SetWorldUtc(static_cast<double>(Parsed.ToUnixTimestamp())))
    { SetStatus(TEXT("2026年のUTC日時を入力してください。例：2026-09-09T12:00:00Z"));return; }
    if(EVAPawn) EVAPawn->AdvanceWalking(0,FVector2D::ZeroVector,0,0,true);
    ResetNavigation();SetStatus(TEXT("日時を変更しました。現在の地理的位置は保持しています。"));
    UpdateSnapshot(0);
}
void AStarPlayerController::HandleAction(FName Action,float Value)
{
    if(Ship&&Ship->IsReady()&&(Action==TEXT("ClockNow")||Action==TEXT("ClockHour")||Action==TEXT("ClockRate")))
    {
        auto* Director=Ship->Director();
        if(Action==TEXT("ClockRate")) Director->SetClockRate(Value);
        else
        {
            const double Utc=Action==TEXT("ClockNow")?static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()):Director->WorldUtc()+Value*3600.0;
            if(!Ship->SetWorldUtc(Utc)) { SetStatus(TEXT("天体暦の対応日時は2026年です。"));return; }
            if(Action==TEXT("ClockNow"))Director->SetClockRate(1);
            if(EVAPawn)EVAPawn->AdvanceWalking(0,FVector2D::ZeroVector,0,0,true);
            ResetNavigation();
        }
        UpdateSnapshot(0);return;
    }
    if(Action==TEXT("ToggleEVA")) { ToggleEVA();return; }
    if(Action==TEXT("ToggleInteriorMonitor"))
    {
        bInteriorMonitor=!bInteriorMonitor;
        if(Ship) Ship->SetInteriorMonitor(bInteriorMonitor);
        return;
    }
    if(EVAPawn&&(Action==TEXT("Takeoff")||Action==TEXT("FlightContext")||Action==TEXT("ToggleAutopilot")||Action==TEXT("ToggleGear")||Action==TEXT("ToggleCruise")||
       Action==TEXT("ToggleView")||Action==TEXT("TogglePhoto")||Action==TEXT("ManualTakeover")||
       Action==TEXT("Load")||Action==TEXT("LoadTour")))
    { SetStatus(TEXT("船へ戻ってから操作してください。Hで帰船します。"));return; }
    if(Exploration) Exploration->NotifyAction(Action);
    if(Action==TEXT("OpenInputSettings")) { OpenInputSettings();return; }
    if(Action==TEXT("StartGuidedTour")) { StartGuidedTour();return; }
    if(Action==TEXT("ManualTakeover")) { ManualTakeover();return; }
    if(Action==TEXT("UIScaleDelta")) { UIScale=FMath::Clamp(UIScale+Value,0.8f,1.6f);if(HUD)HUD->ApplyUserSettings(UIScale,MasterVolume,LookSensitivity);return; }
    if(Action==TEXT("MasterVolumeDelta")) { MasterVolume=FMath::Clamp(MasterVolume+Value,0.0f,1.0f);if(HUD)HUD->ApplyUserSettings(UIScale,MasterVolume,LookSensitivity);return; }
    if(Action==TEXT("LookSensitivityDelta")) { LookSensitivity=FMath::Clamp(LookSensitivity+Value,0.3f,2.5f);if(HUD)HUD->ApplyUserSettings(UIScale,MasterVolume,LookSensitivity);return; }
    if(Action==TEXT("Quit"))
    {
        if(bSessionStarted&&!SaveGame()) return;
        UKismetSystemLibrary::QuitGame(this,this,EQuitPreference::Quit,false);
        return;
    }
    if(!Ship||!Ship->IsReady()) { SetStatus(Ship?Ship->Error():TEXT("機体を準備しています。"));return; }
    auto* Sim=Ship->Simulation();
    if(bNavigationBrakingRecovery&&(Action==TEXT("ToggleCruise")||Action==TEXT("Precision")||Action==TEXT("ToggleGear")||Action==TEXT("Takeoff")))
    { SetStatus(TEXT("安全減速中は操船を停止しています。B / Escで中断できます。"));return; }
    if(Action==TEXT("ToggleEnhancedStars"))
    {
        SetEnhancedStarsEnabled(!bEnhancedStars);
        SetStatus(bEnhancedStars?TEXT("星空の強調表示：オン"):TEXT("星空の強調表示：オフ"));
        return;
    }
    if(Action==TEXT("ToggleFlightAssist"))
    {
        const bool Enabled=!Sim->FlightAssistEnabled();
        SetFlightAssistEnabled(Enabled);
        SetStatus(Enabled?TEXT("操縦補助：オン"):TEXT("操縦補助：オフ"));
        return;
    }
    // Former scenic-start actions intentionally cannot relocate a voyage.
    if(Action==TEXT("StartFlight"))
    {
        bSessionStarted=true;
        bMainMenu=false;
        if(HUD) HUD->SetMainMenuVisible(false);
        if(auto* Plugin=FStarFlightInputModule::GetIfAvailable())
        {
            const auto Status=Plugin->GetStatus();
            if(Status.bConnected&&!Status.bMappingConfirmed&&!bOpenedInitialInputSettings)
            { bOpenedInitialInputSettings=true;OpenInputSettings();return; }
        }
        SetFlightPaused(false);
    }
    else if(Action==TEXT("Resume"))
    {
        if(bGuidedTour&&(bGuidedTourFinished||bGuidedTourFailed))
        { SetStatus(TEXT("ツアーは終了しました。手動操縦へ切り替えてください。"));SetFlightPaused(true);return; }
        bSessionStarted=true;bMainMenu=false;if(HUD)HUD->SetMainMenuVisible(false);SetFlightPaused(false);
    }
    else if(Action==TEXT("Pause")) { SetFlightPaused(!bFlightPaused); }
    else if(Action==TEXT("ToggleView")) Ship->ToggleView();
    else if(Action==TEXT("ToggleGear")) { if(!Ship->ToggleLandingGear())SetStatus(TEXT("離陸してから着陸脚を格納してください。")); }
    else if(Action==TEXT("ToggleCruise"))
    {
        if(bGuidedTour) { SetStatus(TEXT("手動操縦へ切り替えてから巡航を操作してください。"));return; }
        NavigationCommand=Navigation.Tick(*Sim);
        if(NavigationCommand.autopilotActive)
        { Navigation.Cancel();NavigationCommand=Navigation.Tick(*Sim); }
        if(bPhotoMode||bMainMenu||!bSessionStarted||InputSettings) { SetStatus(TEXT("飛行画面に戻ってから巡航を操作してください。"));return; }
        const bool WasPaused=bFlightPaused;
        if(Sim->State().mode==star::FlightMode::Landed) { SetStatus(TEXT("離陸してから巡航を操作してください。"));return; }
        if(Sim->State().mode==star::FlightMode::Cruise||Sim->State().mode==star::FlightMode::LocalCruise)
        { Sim->SetMode(star::FlightMode::Maneuver);SetStatus(TEXT("通常飛行へ減速します。Spaceで停止できます。")); }
        else
        {
            Sim->SetMode(NavigationCommand.highSpeedAuthorized?star::FlightMode::Cruise:star::FlightMode::LocalCruise);
            SetStatus(NavigationCommand.highSpeedAuthorized?TEXT("惑星間巡航：推力で加速します。"):
                TEXT("周辺巡航：W/Sで速度調整、Spaceで停止、Cで通常飛行。"));
        }
        if(WasPaused)SetFlightPaused(false);
        Ship->PlaySoundEvent(TEXT("View"));
    }
    else if(Action==TEXT("Precision"))
    {
        if(NavigationCommand.autopilotActive) Navigation.Cancel();
        Sim->SetMode(Sim->State().mode==star::FlightMode::Landing?star::FlightMode::Maneuver:star::FlightMode::Landing);
    }
    else if(Action==TEXT("Brake")) bBrakeHeld=Value>0;
    else if(Action==TEXT("Scan")) { bScanHeld=Value>0;Ship->PlaySoundEvent(TEXT("Scan")); }
    else if(Action==TEXT("Takeoff")) { if(!bPhotoMode&&!bFlightPaused) bTakeoffRequested=true; }
    else if(Action==TEXT("FlightContext"))
    {
        if(!bPhotoMode&&!bMainMenu&&bSessionStarted&&!NavigationBypassed()&&Sim->State().mode==star::FlightMode::Landed)
        {
            if(bFlightPaused) SetFlightPaused(false);
            if(!bFlightPaused) { bTakeoffRequested=true;bKeyboardLiftHeld=IsInputKeyDown(EKeys::F); }
        }
        else ConfirmNavigationTransfer();
    }
    else if(Action==TEXT("ToggleAutopilot")) ToggleNavigationAutopilot();
    else if(Action==TEXT("NavigationSafeBrake")) ToggleNavigationSafeBrake();
    else if(Action==TEXT("TargetEarth")) SelectTarget(TEXT("earth"));
    else if(Action==TEXT("TargetMoon")) SelectTarget(TEXT("moon"));
    else if(Action==TEXT("TargetSaturn")) SelectTarget(TEXT("saturn"));
    else if(Action==TEXT("TargetNext"))
    {
        const auto Id=Sim->State().targetBodyId;
        SelectTarget(Id=="earth"?TEXT("moon"):Id=="moon"?TEXT("saturn"):TEXT("earth"));
    }
    else if(Action==TEXT("RecenterLook")) Ship->RecenterLook();
    else if(Action==TEXT("TogglePhoto"))
    {
        bPhotoMode=!bPhotoMode;
        if(bPhotoMode&&bNavigationBrakingRecovery) SetFlightPaused(true);
        Navigation.SetPaused(bPhotoMode||bFlightPaused);
        bTakeoffRequested=false;
        Ship->ClearPhotoOffset();
        if(auto* Plugin=FStarFlightInputModule::GetIfAvailable()) Plugin->SetGameplayEnabled(!bPhotoMode&&!bFlightPaused);
        if(bPhotoMode) SetMenuInputMode(); else if(!bFlightPaused) { SetInputMode(FInputModeGameOnly());bShowMouseCursor=false; }
    }
    else if(Action==TEXT("ExposureDelta")) { Exposure=FMath::Clamp(Exposure+Value,-5.0f,5.0f);Ship->Director()->SetExposure(Exposure); }
    else if(Action==TEXT("Save")) SaveGame();
    else if(Action==TEXT("Load")||Action==TEXT("LoadTour"))
    {
        if(bGuidedTour) { LoadGame();return; }
        const bool PreviousSlot=bTourSaveSlot;
        bTourSaveSlot=Action==TEXT("LoadTour");
        if(LoadGame()) { bMainMenu=false;if(HUD)HUD->SetMainMenuVisible(false);SetFlightPaused(false); }
        else bTourSaveSlot=PreviousSlot;
    }
    else if(Action==TEXT("PhotoCapture")) CapturePhoto();
    else if(Action==TEXT("DescendOrCapture")) { if(bPhotoMode) CapturePhoto(); }
}
void AStarPlayerController::UpdateSnapshot(double Dt)
{
    Snapshot.WorldUtcSeconds=Ship->Director()->WorldUtc();
    Snapshot.WorldUtcText=FDateTime::FromUnixTimestamp(static_cast<int64>(Snapshot.WorldUtcSeconds)).ToIso8601();
    Snapshot.WorldClockRate=Ship->Director()->ClockRate();
    Snapshot.EarthSurfaceStatus=Ship->Director()->EarthSurfaceStatus();
    const auto& State=Ship->Simulation()->State();
    const auto& Catalog=Ship->Director()->Catalog();
    const auto CameraPosition=EVAPawn?EVAPawn->CameraAbsoluteMeters():Ship->CameraAbsoluteMeters();
    const auto CameraForward=EVAPawn?EVAPawn->CameraForwardSimulation():Ship->CameraForwardSimulation();
    const auto CameraRight=EVAPawn?EVAPawn->CameraRightSimulation():Ship->CameraRightSimulation();
    const auto CameraUp=EVAPawn?EVAPawn->CameraUpSimulation():Ship->CameraUpSimulation();
    const double CameraFOV=EVAPawn?EVAPawn->CameraHorizontalFOVDegrees():Ship->CameraHorizontalFOVDegrees();
    Snapshot.TargetId=UTF8_TO_TCHAR(State.targetBodyId.c_str());
    const auto* Target=Catalog.Find(State.targetBodyId);
    Snapshot.TargetName=Target?Target->Name:TEXT("未選択");
    UpdateNavigationSnapshot();
    GetViewportSize(Snapshot.RenderWidth,Snapshot.RenderHeight);
    static const auto* Scale=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    if(Scale)
        Snapshot.RenderScalePercent=Scale->GetFloat();
    Snapshot.bHasTargetDirection=Target!=nullptr;
    if(Target&&State.targetBodyId=="moon"&&Ship->Simulation()->Telemetry("moon").surfaceAltitudeMeters<10000)
        Snapshot.bHasTargetDirection=false;
    if(Target)
    {
        const auto Direction=(Target->Definition.centerMeters-CameraPosition).Normalized();
        const double Ahead=star::Vec3d::Dot(Direction,CameraForward);
        const double Right=star::Vec3d::Dot(Direction,CameraRight);
        const double Up=star::Vec3d::Dot(Direction,CameraUp);
        Snapshot.TargetYawDeg=static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Right,Ahead)));
        Snapshot.TargetPitchDeg=static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Up,FMath::Sqrt(Ahead*Ahead+Right*Right))));
        const double Aspect=Snapshot.RenderHeight>0?static_cast<double>(Snapshot.RenderWidth)/Snapshot.RenderHeight:16.0/9.0;
        const double TanHalfFov=FMath::Tan(FMath::DegreesToRadians(CameraFOV*0.5));
        Snapshot.bTargetBehind=Ahead<=0;
        double X=Right/FMath::Max(0.001,FMath::Abs(Ahead))/TanHalfFov;
        double Y=-Up/FMath::Max(0.001,FMath::Abs(Ahead))/TanHalfFov*Aspect;
        Snapshot.bTargetOnScreen=Ahead>0&&FMath::Abs(X)<=1&&FMath::Abs(Y)<=1;
        if(Snapshot.bTargetBehind)
        {
            X=Right;Y=-Up*Aspect;
            if(FMath::Abs(X)+FMath::Abs(Y)<1e-6) X=1;
            const double Edge=FMath::Max(FMath::Abs(X)/0.82,FMath::Abs(Y)/0.76);
            X/=Edge;Y/=Edge;
        }
        const double ClampScale=Snapshot.bTargetOnScreen?1.0:FMath::Max(1.0,FMath::Max(FMath::Abs(X)/0.82,FMath::Abs(Y)/0.76));
        Snapshot.TargetScreenPositionNormalized=FVector2D(0.5+X/ClampScale*0.5,0.5+Y/ClampScale*0.5);
    }
    const FStarBodyRecord* Nearest=nullptr;
    double Closest=DBL_MAX;
    for(const auto& Body:Catalog.Bodies())
    {
        if(Body.Definition.id=="sun") continue;
        const double Distance=(State.positionMeters-Body.Definition.centerMeters).Length()-Body.Definition.radiusMeters;
        if(Distance<Closest) { Closest=Distance;Nearest=&Body; }
    }
    Snapshot.BodyId=Nearest?UTF8_TO_TCHAR(Nearest->Definition.id.c_str()):TEXT("space");
    Snapshot.BodyName=Closest<10000000&&Nearest?Nearest->Name:TEXT("深宇宙");
    if(Nearest)
    {
        const auto Telemetry=Ship->Simulation()->Telemetry(Nearest->Definition.id);
        Snapshot.AltitudeM=Telemetry.surfaceAltitudeMeters;
        Snapshot.VerticalSpeedMps=-Telemetry.closingSpeedMps;
        Snapshot.LatitudeDeg=Telemetry.latitudeDegrees;
        Snapshot.LongitudeDeg=Telemetry.longitudeDegrees;
    }
    Snapshot.SpeedMps=State.velocityMetersPerSecond.Length();
    Snapshot.TargetDistanceM=Target?FMath::Max(0.0,Ship->Simulation()->Telemetry(Target->Definition.id).surfaceAltitudeMeters):0;
    Snapshot.Throttle=static_cast<float>(State.throttle);
    Snapshot.bGearDeployed=State.gearDeployed;
    Snapshot.bLanded=State.mode==star::FlightMode::Landed;
    Snapshot.bCruise=State.mode==star::FlightMode::Cruise||State.mode==star::FlightMode::LocalCruise;
    Snapshot.bCockpitView=!EVAPawn&&Ship->IsCockpitView();
    Snapshot.bPaused=bFlightPaused;
    Snapshot.bPhotoMode=bPhotoMode;
    Snapshot.bOnFoot=EVAPawn!=nullptr;
    Snapshot.bInteriorMonitor=bInteriorMonitor;
    Snapshot.ExposureBias=Exposure;
    Snapshot.EngineOutput=static_cast<float>(FMath::Clamp(Ship->Simulation()->Propulsion().engineOutput,0.0,1.0));
    Snapshot.bEnhancedStars=bEnhancedStars;
    Snapshot.bFlightAssist=Ship->Simulation()->FlightAssistEnabled();
    Snapshot.bGuidedTour=bGuidedTour;
    Snapshot.GuidedTourProgress=static_cast<float>(FMath::Clamp(GuidedTourProgress,0.0,1.0));
    Snapshot.GuidedTourTitle=GuidedTourTitle;
    Snapshot.ControllerName=TEXT("キーボード / マウス");
    Snapshot.bJoystickConnected=false;
    if(auto* Plugin=FStarFlightInputModule::GetIfAvailable())
    {
        const auto Status=Plugin->GetStatus();
        Snapshot.bJoystickConnected=Status.bConnected;
        if(Status.bConnected) Snapshot.ControllerName=Status.DeviceName+(Status.bArmed?TEXT(""):TEXT(" · 設定 / 中立待ち"));
    }
    auto ObservationState=State;
    if(EVAPawn) ObservationState.positionMeters=EVAPawn->PositionAbsoluteMeters();
    auto Observation=Ship->Director()->Observe(ObservationState,CameraPosition,CameraForward,Snapshot.TargetId,bScanHeld&&!bFlightPaused&&!bPhotoMode,Snapshot.bCockpitView,(bFlightPaused||bPhotoMode)?0:Dt,++ObservationSequence);
    if(Exploration)
    {
        Snapshot.bCanScan=Exploration->CanScan(Observation);
        Exploration->SubmitObservation(Observation);
        Snapshot.ScanProgress=Exploration->GetActiveScanProgress();
        Snapshot.Objectives=Exploration->GetObjectives();
        const auto Tutorial=Exploration->GetTutorialObjective();
        if(!Tutorial.bComplete) { Snapshot.MissionTitle=Tutorial.Title;Snapshot.MissionDetail=Tutorial.Detail; }
        else
        {
            Snapshot.MissionTitle=TEXT("自由探査");Snapshot.MissionDetail=TEXT("観測目標を選び、対象へ接近してください。");
            for(const auto& Objective:Snapshot.Objectives) if(!Objective.bComplete)
            { Snapshot.MissionTitle=Objective.Title;Snapshot.MissionDetail=Objective.Detail;break; }
        }
    }
    if(bGuidedTour) UpdateGuidedTourObservation();
    Snapshot.StatusText=Clock<StatusExpires?StatusMessage:Snapshot.bLanded?TEXT("着陸完了 · Fで再離陸"):State.throttleNeutralRequired?TEXT("スロットルを一度戻してください。"):TEXT("");
    if(Snapshot.StatusText.IsEmpty()&&Ship->IsGearAnimating()) Snapshot.StatusText=TEXT("着陸脚を動かしています。");
}
FString AStarPlayerController::SaveFilename() const
{
    if(bBenchmark&&(FParse::Param(FCommandLine::Get(),TEXT("StarScenicControlsQA"))||FParse::Param(FCommandLine::Get(),TEXT("StarTimeQA")))) return BenchmarkPath/TEXT("scenic-qa-save.json");
    if(bNavigationQA) return bNavigationQAPathReady?NavigationQADirectory/TEXT("navigation-save.json"):FString();
    if(bEVAQA) return EVAQADirectory/TEXT("star-v1.json");
    if(bAcceptance) return AcceptanceDirectory/TEXT("star-v1.json");
    if(bGuidedTour||bTourSaveSlot)
    {
        return (bGuidedTourTest?GuidedTourQADirectory:FPaths::ProjectSavedDir()/TEXT("SaveGames"))/TEXT("star-tour-v1.json");
    }
    return FPaths::ProjectSavedDir()/TEXT("SaveGames/star-v1.json");
}
bool AStarPlayerController::SaveGame()
{
    if(bNavigationQA&&!bNavigationQAPathReady) return false;
    if(!bSessionStarted) { SetStatus(TEXT("飛行を開始してから保存してください。"));return false; }
    if(!Ship||!Ship->IsReady()||!Exploration) return false;
    auto Json=MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("schemaVersion"),1);
    Json->SetStringField(TEXT("dataEpoch"),Ship->Director()->Catalog().DataEpoch());
    Json->SetStringField(TEXT("savedAt"),FDateTime::UtcNow().ToIso8601());
    Json->SetNumberField(TEXT("astronomyUtc"),Ship->Director()->WorldUtc());
    Json->SetNumberField(TEXT("astronomyRate"),Ship->Director()->ClockRate());
    Json->SetStringField(TEXT("flight"),UTF8_TO_TCHAR(star::SerializeFlightState(Ship->Simulation()->State()).c_str()));
    Json->SetStringField(TEXT("exploration"),Exploration->ExportProgressJson());
    if(EVAPawn)
    {
        const auto Saved=EVAPawn->ExportSaveState();
        auto EVA=MakeShared<FJsonObject>();
        const auto Put=[&](const TCHAR* Name,const star::Vec3d& V)
        { EVA->SetArrayField(Name,{MakeShared<FJsonValueNumber>(V.x),MakeShared<FJsonValueNumber>(V.y),MakeShared<FJsonValueNumber>(V.z)}); };
        Put(TEXT("feet"),Saved.feetMeters);Put(TEXT("heading"),Saved.headingSimulation);Put(TEXT("boarding"),Saved.boardingPointMeters);
        EVA->SetNumberField(TEXT("pitchRadians"),Saved.pitchRadians);
        Json->SetObjectField(TEXT("eva"),EVA);
    }
    auto Settings=MakeShared<FJsonObject>();
    Settings->SetNumberField(TEXT("uiScale"),UIScale);Settings->SetNumberField(TEXT("masterVolume"),MasterVolume);
    Settings->SetNumberField(TEXT("lookSensitivity"),LookSensitivity);Settings->SetNumberField(TEXT("exposure"),Exposure);
    Settings->SetBoolField(TEXT("enhancedStars"),bEnhancedStars);
    Settings->SetBoolField(TEXT("interiorMonitor"),bInteriorMonitor);
    Settings->SetBoolField(TEXT("flightAssist"),bFlightAssistPreference);
    Settings->SetBoolField(TEXT("guidedTour"),bGuidedTour);
    Settings->SetNumberField(TEXT("guidedTourProgress"),GuidedTourProgress);
    Json->SetObjectField(TEXT("settings"),Settings);
    FString Text;
    if(!FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text))) return false;
    const FString Filename=SaveFilename(),Temp=Filename+TEXT(".tmp"),Backup=Filename+TEXT(".bak");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    if(!FFileHelper::SaveStringToFile(Text,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { SetStatus(TEXT("セーブを書き込めませんでした。"));return false; }
    if(IFileManager::Get().FileExists(*Filename)) IFileManager::Get().Copy(*Backup,*Filename,true,true);
    if(!IFileManager::Get().Move(*Filename,*Temp,true,true,false,true))
    { SetStatus(TEXT("セーブを確定できませんでした。以前のデータを保持しています。"));return false; }
    SetStatus(TEXT("飛行位置と発見記録を保存しました。"));
    return true;
}
bool AStarPlayerController::LoadGame()
{
    if(bNavigationQA&&!bNavigationQAPathReady) return false;
    if(bGuidedTour)
    {
        SetStatus(TEXT("ツアー中はセーブの読み込みを止めています。手動操縦へ切り替えてください。"));
        return false;
    }
    if(!Ship||!Ship->IsReady()||!Exploration) return false;
    FString Text;
    if(!FFileHelper::LoadFileToString(Text,*SaveFilename())) { SetStatus(TEXT("保存された飛行記録がありません。"));return false; }
    TSharedPtr<FJsonObject> Json;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json)||!Json.IsValid())
    { SetStatus(TEXT("セーブを読み込めません。現在の飛行を保持します。"));return false; }
    double Version=0;FString Epoch,FlightText,ProgressText;
    if(!Json->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1||!Json->TryGetStringField(TEXT("dataEpoch"),Epoch)||Epoch!=Ship->Director()->Catalog().DataEpoch()||!Json->TryGetStringField(TEXT("flight"),FlightText)||!Json->TryGetStringField(TEXT("exploration"),ProgressText))
    { SetStatus(TEXT("セーブの形式または天体データの世代が一致しません。"));return false; }
    star::FlightState Pending;
    if(!star::DeserializeFlightState(TCHAR_TO_UTF8(*FlightText),Pending)) { SetStatus(TEXT("飛行記録が破損しています。"));return false; }
    auto* Director=Ship->Director();
    double SavedUtc=0,SavedRate=1;const bool HasClock=Json->TryGetNumberField(TEXT("astronomyUtc"),SavedUtc);
    if(!HasClock)
    { FDateTime Reference;if(!FDateTime::ParseIso8601(*Epoch,Reference))return false;SavedUtc=static_cast<double>(Reference.ToUnixTimestamp()); }
    Json->TryGetNumberField(TEXT("astronomyRate"),SavedRate);
    std::vector<star::BodyDefinition> SavedBodies;
    if(!(SavedRate==0||SavedRate==1||SavedRate==60||SavedRate==600)||!Director->Catalog().SimulationBodiesAt(SavedUtc,SavedBodies))
    { SetStatus(TEXT("保存日時が天体暦の対応範囲外です。現在の航海を保持します。"));return false; }
    if(!HasClock)
    {
        const auto& OldBodies=Director->Catalog().ReferenceBodies();const auto* From=star::NearestReferenceBody(OldBodies,Pending);
        if(!From)return false;
        for(const auto& To:SavedBodies)if(To.id==From->id)Pending=star::TransportFlightFrame(Pending,*From,To);
    }
    star::FlightSimulation Candidate(SavedBodies,Ship->Simulation()->Config(),[Director](const auto& Body,const auto& Direction,auto& Sample){return Director->Catalog().SampleTerrain(Body,Direction,Sample);});
    if(!Candidate.RestoreState(Pending)) { SetStatus(TEXT("安全に復元できない飛行位置です。"));return false; }
    const FString PreviousProgress=Exploration->ExportProgressJson();
    FString Error;
    if(!Exploration->RestoreProgressJson(ProgressText,Error)) { SetStatus(TEXT("発見記録を復元できません: ")+Error);return false; }
    const auto PreviousFlight=Ship->Simulation()->State();const double PreviousUtc=Director->WorldUtc();
    if(!Ship->SetWorldUtc(SavedUtc)||!Ship->RestoreFlight(Pending))
    { Ship->SetWorldUtc(PreviousUtc);Ship->RestoreFlight(PreviousFlight);FString Ignored;Exploration->RestoreProgressJson(PreviousProgress,Ignored);SetStatus(TEXT("飛行位置を復元できませんでした。"));return false; }
    Director->SetClockRate(SavedRate);
    ResetNavigation();
    const TSharedPtr<FJsonObject>* Settings=nullptr;
    if(Json->TryGetObjectField(TEXT("settings"),Settings))
    {
        double Value=0;
        if((*Settings)->TryGetNumberField(TEXT("uiScale"),Value)&&FMath::IsFinite(Value)) UIScale=FMath::Clamp(static_cast<float>(Value),0.8f,1.6f);
        if((*Settings)->TryGetNumberField(TEXT("masterVolume"),Value)&&FMath::IsFinite(Value)) MasterVolume=FMath::Clamp(static_cast<float>(Value),0.0f,1.0f);
        if((*Settings)->TryGetNumberField(TEXT("lookSensitivity"),Value)&&FMath::IsFinite(Value)) LookSensitivity=FMath::Clamp(static_cast<float>(Value),0.3f,2.5f);
        if((*Settings)->TryGetNumberField(TEXT("exposure"),Value)&&FMath::IsFinite(Value)) Exposure=FMath::Clamp(static_cast<float>(Value),-5.0f,5.0f);
        bool BoolValue=false;
        if((*Settings)->TryGetBoolField(TEXT("enhancedStars"),BoolValue)) bEnhancedStars=BoolValue;
        if((*Settings)->TryGetBoolField(TEXT("interiorMonitor"),BoolValue)) bInteriorMonitor=BoolValue;
        if((*Settings)->TryGetBoolField(TEXT("flightAssist"),BoolValue)) bFlightAssistPreference=BoolValue;
        if((*Settings)->TryGetNumberField(TEXT("guidedTourProgress"),Value)&&FMath::IsFinite(Value)) GuidedTourProgress=FMath::Clamp(Value,0.0,1.0);
    }
    KeyboardThrottle=0;Ship->Simulation()->SetThrottle(0);
    bScanHeld=bBrakeHeld=bTakeoffRequested=false;bPhotoMode=false;
    if(HUD) HUD->ApplyUserSettings(UIScale,MasterVolume,LookSensitivity);
    Ship->Director()->SetExposure(Exposure);
    SetEnhancedStarsEnabled(bEnhancedStars);
    Ship->SetInteriorMonitor(bInteriorMonitor);
    SetFlightAssistEnabled(bFlightAssistPreference);
    bSessionStarted=true;
    SetStatus(TEXT("飛行位置と発見記録を復元しました。"));
    if(!NavigationBypassed()&&Navigation.Tick(*Ship->Simulation()).requiresSafetyPause) SetFlightPaused(true);
    const TSharedPtr<FJsonObject>* EVA=nullptr;
    if(Json->TryGetObjectField(TEXT("eva"),EVA))
    {
        star::LunarWalkSaveState Saved;
        const auto Get=[&](const TCHAR* Name,star::Vec3d& V)
        {
            const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
            if(!(*EVA)->TryGetArrayField(Name,A)||A->Num()!=3)return false;
            return (*A)[0]->TryGetNumber(V.x)&&(*A)[1]->TryGetNumber(V.y)&&(*A)[2]->TryGetNumber(V.z);
        };
        const bool Valid=Get(TEXT("feet"),Saved.feetMeters)&&Get(TEXT("heading"),Saved.headingSimulation)&&
            Get(TEXT("boarding"),Saved.boardingPointMeters)&&(*EVA)->TryGetNumberField(TEXT("pitchRadians"),Saved.pitchRadians);
        if(Valid&&!HasClock)
        {
            const auto& Old=Director->Catalog().ReferenceBodies();
            for(const auto& From:Old)if(From.id=="moon")for(const auto& To:SavedBodies)if(To.id=="moon")
            {
                const auto Q=(To.bodyFixedToSimulation*From.bodyFixedToSimulation.Conjugate()).Normalized();
                Saved.feetMeters=To.centerMeters+Q.Rotate(Saved.feetMeters-From.centerMeters);
                Saved.boardingPointMeters=To.centerMeters+Q.Rotate(Saved.boardingPointMeters-From.centerMeters);
                Saved.headingSimulation=Q.Rotate(Saved.headingSimulation);
            }
        }
        auto* Walker=Valid?GetWorld()->SpawnActor<AStarEVAPawn>():nullptr;
        if(Walker&&Walker->InitializeFromShip(Ship)&&Walker->RestoreSaveState(Saved))
        {
            EVAPawn=Walker;Possess(Walker);SetViewTarget(Walker);ClearInputHandoff();Ship->SetEVAAudio(true);
            SetStatus(TEXT("月面の歩行位置を復元しました。"));
        }
        else { if(Walker)Walker->Destroy();SetStatus(TEXT("歩行位置を安全に復元できないため、着陸船から再開します。")); }
    }
    return true;
}
void AStarPlayerController::SetStatus(const FString& Message,double Seconds) { StatusMessage=Message;StatusExpires=Clock+Seconds; }
void AStarPlayerController::CapturePhoto()
{
    if(bNavigationQA&&!bNavigationQAPathReady) return;
    const FString Directory=bNavigationQA?NavigationQADirectory/TEXT("photos"):FPaths::ProjectSavedDir()/TEXT("Photos");
    IFileManager::Get().MakeDirectory(*Directory,true);
    LastPhotoRequest=Directory/FString::Printf(TEXT("STAR_%s_%llu.png"),*FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")),ObservationSequence);
    FScreenshotRequest::RequestScreenshot(LastPhotoRequest,false,false);
    SetStatus(TEXT("写真を保存しています。"));
}
void AStarPlayerController::TickAstronomyQA()
{
    if(BenchmarkStage!=0) return;
    static int32 Step=0;static double SavedUtc=0,RateStart=0,RateEngineStart=0,PauseUtc=0;static star::Vec3d SavedPosition;
    auto* D=Ship->Director();const double T=BenchmarkStageTime;
    const auto Event=[&](const TCHAR* Name,bool Passed,double Difference=0)
    {
        const FString Line=FString::Printf(TEXT("{\"event\":\"%s\",\"pass\":%s,\"utc\":%.9f,\"rate\":%.0f,\"engineSeconds\":%.9f,\"difference\":%.9f}\n"),
            Name,Passed?TEXT("true"):TEXT("false"),D->WorldUtc(),D->ClockRate(),T,Difference);
        FFileHelper::SaveStringToFile(Line,*(BenchmarkPath/TEXT("time-events.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    };
    if(Step==0&&T>=3){D->SetClockRate(0);PauseUtc=D->WorldUtc();++Step;}
    if(Step==1&&T>=5){Event(TEXT("fixed-clock-while-flying"),D->WorldUtc()==PauseUtc);++Step;}
    if(Step==2&&T>=6)
    {
        const auto& E=D->Catalog().Find(TEXT("earth"))->Definition;
        const auto Before=E.bodyFixedToSimulation.Conjugate().Rotate(Ship->Simulation()->State().positionMeters-E.centerMeters);
        const double OldUtc=D->WorldUtc();HandleAction(TEXT("ClockHour"),6);
        const auto After=E.bodyFixedToSimulation.Conjugate().Rotate(Ship->Simulation()->State().positionMeters-E.centerMeters);
        Event(TEXT("date-change-preserves-geography"),std::abs(D->WorldUtc()-OldUtc-21600)<1e-5&&(After-Before).Length()<0.001,(After-Before).Length());++Step;
    }
    if(Step==3&&T>=8){FScreenshotRequest::RequestScreenshot(BenchmarkPath/TEXT("time-six-hours.png"),true,false);++Step;}
    if(Step==4&&T>=10){HandleAction(TEXT("ClockHour"),12);++Step;}
    if(Step==5&&T>=12){FScreenshotRequest::RequestScreenshot(BenchmarkPath/TEXT("time-eighteen-hours.png"),true,false);++Step;}
    if(Step==6&&T>=14)
    {
        const double Before=D->WorldUtc();SetWorldDateTime(TEXT("2030-01-01T00:00:00Z"));Event(TEXT("reject-unsupported-time"),D->WorldUtc()==Before);
        SetWorldDateTime(TEXT("2026-06-21T12:00:00Z"));FDateTime Expected;FDateTime::ParseIso8601(TEXT("2026-06-21T12:00:00Z"),Expected);
        Event(TEXT("editable-utc-applied"),D->WorldUtc()==static_cast<double>(Expected.ToUnixTimestamp()));
        HandleAction(TEXT("ClockRate"),60);RateStart=D->WorldUtc();RateEngineStart=T;++Step;
    }
    if(Step==7&&T>=16)
    { const double Error=D->WorldUtc()-RateStart-(T-RateEngineStart)*60;Event(TEXT("sixty-times-clock"),std::abs(Error)<4,Error);++Step; }
    if(Step==8&&T>=18){SetFlightPaused(true);PauseUtc=D->WorldUtc();++Step;}
    if(Step==9&&T>=20){Event(TEXT("pause-freezes-utc"),D->WorldUtc()==PauseUtc);SetFlightPaused(false);++Step;}
    if(Step==10&&T>=22){SavedUtc=D->WorldUtc();SavedPosition=Ship->Simulation()->State().positionMeters;Event(TEXT("save-utc"),SaveGame());++Step;}
    if(Step==11&&T>=24){HandleAction(TEXT("ClockHour"),24);++Step;}
    if(Step==12&&T>=26)
    {
        const bool Loaded=LoadGame();const double Difference=(Ship->Simulation()->State().positionMeters-SavedPosition).Length();
        Event(TEXT("reload-utc-position-rate"),Loaded&&D->WorldUtc()==SavedUtc&&D->ClockRate()==60&&Difference<0.001,Difference);
        HandleAction(TEXT("ClockRate"),1);++Step;
    }
}
void AStarPlayerController::UpdateBenchmark(double Dt)
{
    if(!Ship||!Ship->IsReady()) return;
    BenchmarkTime+=Dt;BenchmarkStageTime+=Dt;
    if(BenchmarkStage<BenchmarkStart||BenchmarkStageTime>BenchmarkStageSeconds)
    {
        if(bBenchmarkCaptureStarted)
        {
            ConsoleCommand(TEXT("csvprofile stop"),false);bBenchmarkCaptureStarted=false;
            if(FParse::Param(FCommandLine::Get(),TEXT("StarAudioCapture")))
                UAudioMixerBlueprintLibrary::StopRecordingOutput(GetWorld(),EAudioRecordingExportType::WavFile,
                    FString::Printf(TEXT("benchmark-audio-%d"),BenchmarkStage),BenchmarkPath);
        }
        ++BenchmarkStage;BenchmarkStageTime=0;
        if(BenchmarkStage>=BenchmarkStart+BenchmarkCount) { RequestQAQuit();return; }
        const bool Unified=FParse::Param(FCommandLine::Get(),TEXT("StarUnifiedWorldQA"));
        const auto& Catalog=Ship->Director()->Catalog();
        auto State=Unified?Ship->Simulation()->State():Ship->Director()->InitialFlightState();
        if(BenchmarkStage==1)
        {
            const auto* Moon=Catalog.Find(TEXT("moon"));
            const double Lat=FMath::DegreesToRadians(20.1908),Lon=FMath::DegreesToRadians(30.7717);
            const star::Vec3d Local{FMath::Cos(Lat)*FMath::Cos(Lon),FMath::Cos(Lat)*FMath::Sin(Lon),FMath::Sin(Lat)};
            const auto Radial=Moon->Definition.bodyFixedToSimulation.Rotate(Local);
            State.positionMeters=Moon->Definition.centerMeters+Radial*(Moon->Definition.radiusMeters+Catalog.MoonHeight(20.1908,30.7717)+80);
            const auto East=Moon->Definition.bodyFixedToSimulation.Rotate({-FMath::Sin(Lon),FMath::Cos(Lon),0});
            State.orientation=star::Quatd::FromForwardUp((East-Radial*0.10).Normalized(),Radial);
            State.targetBodyId="moon";State.gearDeployed=true;
        }
        else if(BenchmarkStage==2)
        {
            const auto* Saturn=Catalog.Find(TEXT("saturn"));
            const auto Up=Saturn->Definition.bodyFixedToSimulation.Rotate({0,0,1});
            const auto Side=Saturn->Definition.bodyFixedToSimulation.Rotate({1,0,0});
            State.positionMeters=Saturn->Definition.centerMeters+Side*150000000+Up*70000000;
            State.orientation=star::Quatd::FromForwardUp((Saturn->Definition.centerMeters-State.positionMeters).Normalized(),Up);
            State.targetBodyId="saturn";
        }
        FString EarthPose;
        FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkEarth="),EarthPose);
        if(!EarthPose.IsEmpty() && BenchmarkStage==0)
        {
            const auto& Earth=Catalog.Find(TEXT("earth"))->Definition;
            const auto& Sun=Catalog.Find(TEXT("sun"))->Definition;
            const auto Sunward=(Sun.centerMeters-Earth.centerMeters).Normalized();
            const auto Pole=Earth.bodyFixedToSimulation.Rotate({0,0,1});
            const auto East=star::Vec3d::Cross(Pole,Sunward).Normalized();
            const double Phase=EarthPose==TEXT("night")?165.0:EarthPose==TEXT("limb")?75.0:EarthPose==TEXT("clouds")?45.0:EarthPose==TEXT("close")?33.4:30.0;
            const double A=FMath::DegreesToRadians(Phase);
            const auto Radial=(Sunward*FMath::Cos(A)+East*FMath::Sin(A)+Pole*0.12).Normalized();
            const double Distance=EarthPose==TEXT("clouds")?Earth.radiusMeters+100000.0:EarthPose==TEXT("close")?Earth.radiusMeters+250000.0:Earth.radiusMeters*2.65;
            State.positionMeters=Earth.centerMeters+Radial*Distance;
            State.orientation=star::Quatd::FromForwardUp(-Radial,Pole);
            State.targetBodyId="earth";
            State.velocityMetersPerSecond={0,0,0};
        }
        if(IsEarthFlightPose(EarthPose))
            State=star::MakeEarthFlight(Catalog.Find(TEXT("earth"))->Definition,Catalog.Find(TEXT("sun"))->Definition.centerMeters,EarthFlightPresetForPose(EarthPose));
        if(!Unified&&Ship->RestoreFlight(State)) ResetNavigation();
        if(BenchmarkStage==0)
        {
            const auto& E=Catalog.Find(TEXT("earth"))->Definition;const auto& S=Catalog.Find(TEXT("sun"))->Definition;
            BenchmarkInitialEarthLocal=E.bodyFixedToSimulation.Conjugate().Rotate(State.positionMeters-E.centerMeters);
            BenchmarkInitialSunClearance=star::EarthSunHorizonClearance(E,S.centerMeters,State.positionMeters)*180/star::Pi;
        }
        if(IsEarthFlightPose(EarthPose)) ConfigureFlightCaptureView(Ship,EarthFlightPresetForPose(EarthPose),Catalog.Find(TEXT("earth"))->Definition);
        const bool WantCockpit=Unified?Ship->IsCockpitView():bVRRequested || BenchmarkStage==3 || (IsEarthFlightPose(EarthPose)&&FParse::Param(FCommandLine::Get(),TEXT("StarEarthFlightCockpit")));
        if(WantCockpit!=Ship->IsCockpitView()) Ship->ToggleView();
        if(IsEarthFlightPose(EarthPose) && !bBenchmarkPitchOverride && WantCockpit) Ship->SetLook(0,EarthPose==TEXT("nightflight")?-25:-8,false);
        if(BenchmarkStage==3) Ship->SetLook(BenchmarkLookYaw,bBenchmarkPitchOverride?BenchmarkLookPitch:-12,false);
        else if(BenchmarkLookYaw!=0||bBenchmarkPitchOverride) Ship->SetLook(BenchmarkLookYaw,BenchmarkLookPitch,false);
        if(Unified)
        {
            const auto Before=Ship->Simulation()->State();
            const bool WasMain=bMainMenu&&!bSessionStarted;
            bOpenedInitialInputSettings=true; // Scripted runtime check, not hardware calibration.
            HandleAction(TEXT("StartFlight"),0);
            UnifiedStartedNormally=WasMain&&!bMainMenu&&bSessionStarted&&
                (Ship->Simulation()->State().positionMeters-Before.positionMeters).Length()<0.000001&&EarthPose.IsEmpty();
        }
        else {bMainMenu=false;bFlightPaused=false;bPhotoMode=false;bSessionStarted=true;}
        if(!EarthPose.IsEmpty() && !IsEarthFlightPose(EarthPose))
        {
            bPhotoMode=true;
            Ship->SetActorHiddenInGame(true);
            Ship->ClearGuidedCamera();
            if(HUD) HUD->SetVisibility(ESlateVisibility::Collapsed);
        }
        if(HUD) HUD->SetMainMenuVisible(false);
        if(FParse::Param(FCommandLine::Get(),TEXT("StarEarthLayerQA")))
        {
            Ship->SetActorHiddenInGame(!FParse::Param(FCommandLine::Get(),TEXT("StarEarthKeepShip")));
            if(HUD) HUD->SetVisibility(ESlateVisibility::Collapsed);
        }
        KeyboardThrottle=BenchmarkThrottle;
        IFileManager::Get().MakeDirectory(*BenchmarkPath,true);
        ConsoleCommand(TEXT("csvprofile start"),false);
        bBenchmarkCaptureStarted=true;
        if(FParse::Param(FCommandLine::Get(),TEXT("StarAudioCapture")))
            UAudioMixerBlueprintLibrary::StartRecordingOutput(GetWorld(),BenchmarkStageSeconds+2);
        const FString Json=FString::Printf(TEXT("{\"scene\":%d,\"resolutionTarget\":[3840,2160],\"nativeScreenPercentage\":100,\"note\":\"Scripted render benchmark; not physical-input or continuous-route acceptance\"}"),BenchmarkStage);
        FFileHelper::SaveStringToFile(Json,*(BenchmarkPath/FString::Printf(TEXT("scene-%d.json"),BenchmarkStage)),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    FString EarthCameraPose;
    if(FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkEarth="),EarthCameraPose) && !IsEarthFlightPose(EarthCameraPose))
    {
        const auto& Earth=Ship->Director()->Catalog().Find(TEXT("earth"))->Definition;
        auto Target=Earth.centerMeters;
        if(EarthCameraPose==TEXT("clouds"))
        {
            const auto Radial=(Ship->Simulation()->State().positionMeters-Earth.centerMeters).Normalized();
            const auto East=star::Vec3d::Cross(Earth.bodyFixedToSimulation.Rotate({0,0,1}),Radial).Normalized();
            Target=Earth.centerMeters+(Radial+East*0.10).Normalized()*Earth.radiusMeters;
        }
        Ship->LookAtAbsolute(Target,Dt);
    }
    float MotionStart=8.0f,MotionEnd=20.0f;
    if(bVRRequested&&FParse::Param(FCommandLine::Get(),TEXT("StarVRUIQA"))&&BenchmarkStageTime>=3)
    {
        bMainMenu=true;SetFlightPaused(true);if(HUD)HUD->SetMainMenuVisible(true);
    }
    FParse::Value(FCommandLine::Get(),TEXT("StarMotionStart="),MotionStart);
    FParse::Value(FCommandLine::Get(),TEXT("StarMotionEnd="),MotionEnd);
    if(FParse::Param(FCommandLine::Get(),TEXT("StarEarthMotionQA"))&&BenchmarkStageTime>=MotionStart&&BenchmarkStageTime<MotionEnd&&FMath::Abs(BenchmarkStageTime-BenchmarkShotSeconds)>0.35)
    {
        // Sample the actual running viewport; no interpolated or mocked frames.
        // This low-rate diagnostic clip is separate from native-FPS acceptance.
        static int32 LastMotionBucket=-1,MotionFrame=0;
        const int32 Bucket=FMath::FloorToInt((BenchmarkStageTime-MotionStart)*8.0);
        if(Bucket!=LastMotionBucket&&!FScreenshotRequest::IsScreenshotRequested())
        {
            LastMotionBucket=Bucket;
            const FString Name=FString::Printf(TEXT("motion-%04d.png"),MotionFrame++);
            FScreenshotRequest::RequestScreenshot(BenchmarkPath/Name,false,false);
            const FString Record=FString::Printf(TEXT("{\"file\":\"%s\",\"wallSeconds\":%.6f,\"simulationSeconds\":%.6f}\n"),*Name,
                static_cast<double>(BenchmarkStageTime),Ship->Simulation()->State().simulationTimeSeconds);
            FFileHelper::SaveStringToFile(Record,*(BenchmarkPath/TEXT("motion-frames.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
        }
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("StarScenicJourneyQA"))&&BenchmarkStageTime>=1.0)
    {
        static int32 LastJourneyBucket=-1;
        const int32 Bucket=FMath::FloorToInt(BenchmarkStageTime/60.0);
        if(Bucket!=LastJourneyBucket&&!FScreenshotRequest::IsScreenshotRequested()&&FMath::Abs(BenchmarkStageTime-BenchmarkShotSeconds)>0.5)
        {
            LastJourneyBucket=Bucket;
            const FString Name=FString::Printf(TEXT("journey-%03d.png"),Bucket);
            FScreenshotRequest::RequestScreenshot(BenchmarkPath/Name,false,false);
            const FString Record=FString::Printf(TEXT("{\"file\":\"%s\",\"wallSeconds\":%.6f,\"simulationSeconds\":%.6f}\n"),*Name,static_cast<double>(BenchmarkStageTime),Ship->Simulation()->State().simulationTimeSeconds);
            FFileHelper::SaveStringToFile(Record,*(BenchmarkPath/TEXT("journey-frames.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
        }
    }
    if(BenchmarkStageTime>=BenchmarkShotSeconds&&BenchmarkStageTime-Dt<BenchmarkShotSeconds)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("StarTextureReadback")))ConsoleCommand(TEXT("ListTextures -CSV"),false);
        int32 Width=0,Height=0;GetViewportSize(Width,Height);
        auto Metadata=MakeShared<FJsonObject>();
        Metadata->SetNumberField(TEXT("scene"),BenchmarkStage);
        Metadata->SetNumberField(TEXT("actualViewportWidth"),Width);
        Metadata->SetNumberField(TEXT("actualViewportHeight"),Height);
        Metadata->SetNumberField(TEXT("screenPercentage"),Snapshot.RenderScalePercent);
        FString EarthPose;
        FParse::Value(FCommandLine::Get(),TEXT("StarBenchmarkEarth="),EarthPose);
        Metadata->SetStringField(TEXT("earthPose"),EarthPose);
        if(IsEarthFlightPose(EarthPose))
        {
            const auto& Earth=Ship->Director()->Catalog().Find(TEXT("earth"))->Definition;
            const auto& CurrentFlight=Ship->Simulation()->State();
            const auto Local=Earth.bodyFixedToSimulation.Conjugate().Rotate(CurrentFlight.positionMeters-Earth.centerMeters);
            Metadata->SetNumberField(TEXT("flightDisplacementMeters"),(Local-BenchmarkInitialEarthLocal).Length());
            Metadata->SetNumberField(TEXT("flightAltitudeMeters"),(CurrentFlight.positionMeters-Earth.centerMeters).Length()-Earth.radiusMeters);
            Metadata->SetNumberField(TEXT("flightSpeedMps"),CurrentFlight.velocityMetersPerSecond.Length());
            Metadata->SetNumberField(TEXT("simulationTimeSeconds"),CurrentFlight.simulationTimeSeconds);
            Metadata->SetNumberField(TEXT("flightRecoveries"),static_cast<double>(CurrentFlight.recoveryCount));
            Metadata->SetBoolField(TEXT("observationMode"),false);
            Metadata->SetBoolField(TEXT("photoMode"),bPhotoMode);
            Metadata->SetBoolField(TEXT("shipHidden"),Ship->IsHidden());
            const auto& Sun=Ship->Director()->Catalog().Find(TEXT("sun"))->Definition;
            Metadata->SetNumberField(TEXT("sunHorizonClearanceDegrees"),star::EarthSunHorizonClearance(Earth,Sun.centerMeters,CurrentFlight.positionMeters)*180/star::Pi);
            Metadata->SetNumberField(TEXT("initialSunHorizonClearanceDegrees"),BenchmarkInitialSunClearance);
            Metadata->SetStringField(TEXT("flightInputSource"),TEXT("Scripted PlayerTick controls; no physical input acceptance"));
        }
        Metadata->SetStringField(TEXT("cameraMode"),Ship->IsCockpitView()?TEXT("cockpit"):TEXT("chase"));
        Metadata->SetStringField(TEXT("dataEpoch"),Ship->Director()->Catalog().DataEpoch());
        Metadata->SetNumberField(TEXT("astronomyUtc"),Ship->Director()->WorldUtc());
        Metadata->SetNumberField(TEXT("astronomyRate"),Ship->Director()->ClockRate());
        Metadata->SetStringField(TEXT("weatherMode"),TEXT("clear-sky scenario; no live meteorological data"));
        Metadata->SetBoolField(TEXT("syntheticCloudDetail"),false);
        Metadata->SetBoolField(TEXT("inventedWeatherAdvection"),false);
        Metadata->SetStringField(TEXT("worldUtc"),FDateTime::FromUnixTimestamp(static_cast<int64>(Ship->Director()->WorldUtc())).ToIso8601());
        TArray<TSharedPtr<FJsonValue>> Celestial;
        for(const auto& Record:Ship->Director()->Catalog().Bodies())
        {
            const auto& B=Record.Definition;auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("id"),UTF8_TO_TCHAR(B.id.c_str()));
            J->SetArrayField(TEXT("positionMeters"),{MakeShared<FJsonValueNumber>(B.centerMeters.x),MakeShared<FJsonValueNumber>(B.centerMeters.y),MakeShared<FJsonValueNumber>(B.centerMeters.z)});
            J->SetArrayField(TEXT("rotationWxyz"),{MakeShared<FJsonValueNumber>(B.bodyFixedToSimulation.w),MakeShared<FJsonValueNumber>(B.bodyFixedToSimulation.x),MakeShared<FJsonValueNumber>(B.bodyFixedToSimulation.y),MakeShared<FJsonValueNumber>(B.bodyFixedToSimulation.z)});
            Celestial.Add(MakeShared<FJsonValueObject>(J));
        }
        Metadata->SetArrayField(TEXT("celestialBodies"),Celestial);
        Metadata->SetStringField(TEXT("saveDirectory"),FPaths::ProjectSavedDir());
        Metadata->SetNumberField(TEXT("engineOutput"),Ship->Simulation()->Propulsion().engineOutput);
        Metadata->SetNumberField(TEXT("throttle"),Ship->Simulation()->State().throttle);
        Metadata->SetStringField(TEXT("flightState"),UTF8_TO_TCHAR(star::SerializeFlightState(Ship->Simulation()->State()).c_str()));
        const auto VectorField=[&](const TCHAR* Name,const star::Vec3d& Value)
        {Metadata->SetArrayField(Name,{MakeShared<FJsonValueNumber>(Value.x),MakeShared<FJsonValueNumber>(Value.y),MakeShared<FJsonValueNumber>(Value.z)});};
        VectorField(TEXT("cameraAbsoluteMeters"),Ship->CameraAbsoluteMeters());
        VectorField(TEXT("cameraForwardSimulation"),Ship->CameraForwardSimulation());
        VectorField(TEXT("cameraUpSimulation"),Ship->CameraUpSimulation());
        Metadata->SetNumberField(TEXT("horizontalFovDegrees"),Ship->CameraHorizontalFOVDegrees());
        Metadata->SetStringField(TEXT("scope"),TEXT("Scripted render pose; not continuous-route or physical-input acceptance"));
        FString Text;FJsonSerializer::Serialize(Metadata,TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(Text,*(BenchmarkPath/FString::Printf(TEXT("scene-%d.json"),BenchmarkStage)),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        FScreenshotRequest::RequestScreenshot(BenchmarkPath/FString::Printf(TEXT("scene-%d.png"),BenchmarkStage),bBenchmarkShowUI,false);
    }
}
