#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Exploration/StarExplorationSubsystem.h"
#include "StarFlightInputModule.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectIterator.h"
#include "ContentStreaming.h"
#include "Camera/PlayerCameraManager.h"

#include "Runtime/StarTrackingProbe.h"
#include "UI/StarHUDWidget.h"

namespace
{
FString TourJsonLine(const AStarShipPawn& Ship,
    const FString& Stage, const FString& Title, double Progress, double ElapsedSeconds,
    uint64 Sample, const FString& Objective, bool bScanAcknowledged, const FString& Result = FString())
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("sample"), static_cast<double>(Sample));
    Json->SetNumberField(TEXT("elapsed_seconds"), ElapsedSeconds);
    Json->SetNumberField(TEXT("simulation_seconds"), Ship.Simulation() ? Ship.Simulation()->State().simulationTimeSeconds : 0.0);
    Json->SetStringField(TEXT("stage"), Stage);
    Json->SetStringField(TEXT("title"), Title);
    Json->SetNumberField(TEXT("progress"), FMath::Clamp(Progress, 0.0, 1.0));
    Json->SetStringField(TEXT("scan_objective"), Objective);
    Json->SetBoolField(TEXT("scan_acknowledged"), bScanAcknowledged);
    Json->SetStringField(TEXT("target"), Ship.Simulation() ? UTF8_TO_TCHAR(Ship.Simulation()->State().targetBodyId.c_str()) : TEXT(""));
    Json->SetStringField(TEXT("source"), TEXT("STAR guided tour runtime"));
    if(Ship.Simulation())
    {
        const auto& State=Ship.Simulation()->State();
        Json->SetNumberField(TEXT("engine_output"),Ship.Simulation()->Propulsion().engineOutput);
        Json->SetNumberField(TEXT("throttle"),State.throttle);
        Json->SetNumberField(TEXT("speed_mps"),State.velocityMetersPerSecond.Length());
        Json->SetNumberField(TEXT("recoveries"),static_cast<double>(State.recoveryCount));
        Json->SetBoolField(TEXT("cockpit"),Ship.IsCockpitView());
        Json->SetStringField(TEXT("flight"),UTF8_TO_TCHAR(star::SerializeFlightState(State).c_str()));
    }
    if (!Result.IsEmpty()) Json->SetStringField(TEXT("result"), Result);
    FString Text;
    FJsonSerializer::Serialize(Json, TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    return Text;
}
}

void AStarPlayerController::StartGuidedTour()
{
    if(bGuidedTourTest&&(IFileManager::Get().FileExists(*(GuidedTourQADirectory/TEXT("tour-progress.jsonl")))||
       IFileManager::Get().FileExists(*(GuidedTourQADirectory/TEXT("tour-result.json")))))
    {
        UE_LOG(LogTemp,Error,TEXT("STAR tour QA directory already contains a run; choose a new StarTourPath"));
        ConsoleCommand(TEXT("quit"),false);
        return;
    }
    if (bGuidedTour)
    {
        SetStatus(TEXT("ツアーはすでに進行中です。"));
        return;
    }
    if (bAcceptance)
    {
        SetStatus(TEXT("受入テスト中はツアーを開始できません。"));
        return;
    }
    if (!Ship || !Ship->IsReady())
    {
        SetStatus(Ship ? Ship->Error() : TEXT("機体を準備しています。"));
        return;
    }
    auto* Director = Ship->Director();
    auto* Simulation = Ship->Simulation();
    if (!Director || !Simulation || !Director->IsReady())
    {
        SetStatus(TEXT("宇宙空間を準備しています。"));
        return;
    }

    // Guidance takes over the current voyage; it never creates another world/pose.
    const bool ExistingVoyage=bSessionStarted;
    const auto* Earth=Simulation->FindBody("earth");
    const auto& Current=Simulation->State();
    const double EarthDistance=Earth?(Current.positionMeters-Earth->centerMeters).Length():0;
    if(EVAPawn||bPhotoMode||!Earth||Current.mode==star::FlightMode::Landed||
       EarthDistance<Earth->radiusMeters+Earth->atmosphereHeightMeters+1000||
       EarthDistance>Earth->radiusMeters*1.25||Current.velocityMetersPerSecond.Length()>1.0)
    {
        SetStatus(TEXT("ガイド航行は地球の軌道付近で減速してから開始できます。現在地と航海は保持しています。"));
        return;
    }
    ResetNavigation();
    Ship->ClearGuidedCamera();
    const auto Terrain = [Director](const star::BodyDefinition& Body, const star::Vec3d& Direction, star::TerrainSample& Out)
    {
        return IsValid(Director) && Director->Catalog().SampleTerrain(Body, Direction, Out);
    };
    GuidedTourRuntime = MakeUnique<star::guided::GuidedTour>(Director->Catalog().SimulationBodies(), Terrain);
    if (!GuidedTourRuntime.IsValid())
    {
        SetStatus(TEXT("ツアーの準備に失敗しました。"));
        return;
    }

    bGuidedTour = true;
    bTourPreparing=!ExistingVoyage;TourWarmSeconds=TourResidentSeconds=0;
    if(!ExistingVoyage)for(TObjectIterator<UTexture2D> It;It;++It)
        if(It->GetPathName().StartsWith(TEXT("/Game/Star/"))) It->SetForceMipLevelsToBeResident(900.0f);
    if(!ExistingVoyage&&PlayerCameraManager) PlayerCameraManager->SetManualCameraFade(1.0f,FLinearColor::Black,false);
    bTourSaveSlot = bTourSaveSlot || !ExistingVoyage;
    bGuidedTourFinished = false;
    bGuidedTourFailed = false;
    bGuidedTourActualScanComplete = false;
    bGuidedTourQuitRequested = false;
    bGuidedTourAssistCaptured = true;
    bGuidedTourSavedFlightAssist = Simulation->FlightAssistEnabled();
    bFlightAssistPreference = bGuidedTourSavedFlightAssist;
    // The guide has its own deterministic attitude commands. Keep the user's
    // preference so manual takeover can restore it exactly.
    Simulation->SetFlightAssistEnabled(false);
    ClearInputHandoff();
    GuidedTourPendingObjective.Reset();
    GuidedTourStage = TEXT("starting");
    GuidedTourTitle = TEXT("地球を眺める");
    GuidedTourFailure.Reset();
    GuidedTourProgress = 0.0;
    GuidedTourQAElapsed = 0.0;
    GuidedTourQASampleElapsed = 0.0;
    GuidedTourQASample = 0;
    GuidedTourLastCaptureStage.Reset();
    if(bGuidedTourTest&&FParse::Param(FCommandLine::Get(),TEXT("StarAudioCapture")))
    {
        UAudioMixerBlueprintLibrary::StartRecordingOutput(GetWorld(),80.0f);
        bGuidedTourRecording=true;
    }
    bSessionStarted = true;
    bMainMenu = false;
    // A body can lie below or behind the cockpit glazing during attitude
    // changes. Start sightseeing outside the hull so it remains visible.
    if(!ExistingVoyage)
    {
        if(Ship->IsCockpitView()) Ship->ToggleView();
        Ship->RecenterLook();
    }
    if (HUD) HUD->SetMainMenuVisible(false);
    // SetFlightPaused observes bGuidedTour and therefore leaves the flight
    // plugin disabled while the guide is active.
    SetFlightPaused(false);
    SetStatus(bGuidedTourTest ? TEXT("ツアーQAを開始しました。実際の観測記録を保存します。") : TEXT("現在地からガイド航行を開始しました。手動操縦へ戻しても同じ航海が続きます。"));
}

void AStarPlayerController::ManualTakeover()
{
    if (!bGuidedTour) return;
    GuidedTourRuntime.Reset();
    bGuidedTour = false;
    bTourPreparing=false;
    if(PlayerCameraManager) PlayerCameraManager->StopCameraFade();
    bGuidedTourFinished = false;
    bGuidedTourFailed = false;
    bGuidedTourActualScanComplete = false;
    GuidedTourPendingObjective.Reset();
    GuidedTourStage.Reset();
    GuidedTourTitle.Reset();
    GuidedTourFailure.Reset();
    GuidedTourProgress = 0.0;
    ClearInputHandoff();
    if (Ship && Ship->Simulation())
    {
        SetFlightAssistEnabled(bGuidedTourAssistCaptured ? bGuidedTourSavedFlightAssist : bFlightAssistPreference);
        Ship->ClearGuidedCamera();
        Ship->RecenterLook();
    }
    bGuidedTourAssistCaptured = false;
    bGuidedTourTestStarted = bGuidedTourTest;
    SetFlightPaused(true);
    bMainMenu = false;
    bSessionStarted = true;
    SetStatus(TEXT("手動操縦へ戻りました。現在の航海を続けます。入力を確認して再開してください。"));
}

void AStarPlayerController::UpdateGuidedTourCommand(double Dt)
{
    if (!bGuidedTour || !GuidedTourRuntime.IsValid() || !Ship || !Ship->Simulation()) return;
    if(bFlightPaused||bPhotoMode)
    {
        GuidedTourControls.paused=true;
        bScanHeld=false;bBrakeHeld=false;
        return;
    }
    auto* Simulation = Ship->Simulation();
    if(bTourPreparing)
    {
        TourWarmSeconds+=FMath::Max(0.0,Dt);
        const int32 Pending=IStreamingManager::Get().GetNumWantingResources();
        TourResidentSeconds=Pending==0?TourResidentSeconds+Dt:0;
        GuidedTourControls={};GuidedTourControls.paused=true;
        GuidedTourTitle=TEXT("景色を読み込んでいます");
        GuidedTourStage=TEXT("preparing");
        if(TourWarmSeconds>=3.0&&TourResidentSeconds>=0.5)
        {
            bTourPreparing=false;
            if(PlayerCameraManager) PlayerCameraManager->StartCameraFade(1,0,1.5f,FLinearColor::Black,false,false);
            UE_LOG(LogTemp,Display,TEXT("STAR tour prewarm ready after %.2fs; pending=%d"),TourWarmSeconds,Pending);
        }
        else
        {
            if(TourWarmSeconds>30)
            {
                bGuidedTourFailed=true;GuidedTourFailure=TEXT("景色の読み込みが完了しませんでした。");
                SetFlightPaused(true);
                if(PlayerCameraManager) PlayerCameraManager->StopCameraFade();
            }
            return;
        }
    }
    const auto Command = GuidedTourRuntime->Tick(*Simulation, bGuidedTourActualScanComplete);
    const FString NextObjective = UTF8_TO_TCHAR(Command.scanObjectiveId.c_str());
    if (GuidedTourPendingObjective != NextObjective) bGuidedTourActualScanComplete = false;
    GuidedTourPendingObjective = NextObjective;
    GuidedTourStage = UTF8_TO_TCHAR(Command.stage.c_str());
    GuidedTourTitle = UTF8_TO_TCHAR(Command.stageTitle.c_str());
    GuidedTourFailure = UTF8_TO_TCHAR(Command.failure.c_str());
    GuidedTourProgress = FMath::Clamp(Command.progress, 0.0, 1.0);
    GuidedTourMode = Command.mode;
    GuidedTourControls = Command.controls;
    GuidedTourControls.paused = bFlightPaused || bPhotoMode;
    bScanHeld = Command.scan && !bFlightPaused && !bPhotoMode;
    bBrakeHeld = Command.controls.brake && !bFlightPaused && !bPhotoMode;

    if (Simulation->State().mode != star::FlightMode::Landed) Simulation->SetMode(Command.mode);
    if (!Command.targetBodyId.empty()) Simulation->SetTarget(Command.targetBodyId);
    if (Ship->GearDesired() != Command.gearDeployedDesired) Ship->ToggleLandingGear();
    auto ViewTarget=Command.lookAtMeters;
    const auto Position=Simulation->State().positionMeters;
    const double CameraTime=Simulation->State().simulationTimeSeconds;
    auto CameraUp=Simulation->State().orientation.Rotate({0,0,1});
    auto CameraForward=Simulation->State().velocityMetersPerSecond.Length()>1?
        Simulation->State().velocityMetersPerSecond.Normalized():Simulation->State().orientation.Rotate({1,0,0});
    auto CameraSide=star::Vec3d::Cross(CameraUp,CameraForward).Normalized();
    if(CameraSide.LengthSquared()<0.1)CameraSide=Simulation->State().orientation.Rotate({0,1,0});
    CameraUp=star::Vec3d::Cross(CameraForward,CameraSide).Normalized();
    const double Orbit=FMath::DegreesToRadians(24.0*FMath::Sin(CameraTime/27.0)+12.0*FMath::Sin(CameraTime/13.0));
    auto CameraOffset=(-CameraForward*FMath::Cos(Orbit)+CameraSide*FMath::Sin(Orbit))*48.0+CameraUp*13.0;
    if(Command.stage=="earth_to_moon"||Command.stage=="moon_to_saturn")
        ViewTarget=Position+CameraForward*8.0;
    if(Command.stage=="earth_view")
    {
        const auto* Earth=Ship->Director()->Catalog().Find(TEXT("earth"));
        const auto Offset=Ship->CameraAbsoluteMeters()-Earth->Definition.centerMeters;
        const double Distance=Offset.Length(),Radius=Earth->Definition.radiusMeters;
        const auto Radial=Offset.Normalized();
        const auto* Sun=Ship->Director()->Catalog().Find(TEXT("sun"));
        auto Tangent=(Sun->Definition.centerMeters-Earth->Definition.centerMeters).Normalized();
        Tangent=(Tangent-Radial*star::Vec3d::Dot(Tangent,Radial)).Normalized();
        const double Ratio=FMath::Clamp(Radius/Distance,0.0,0.999999);
        ViewTarget=Earth->Definition.centerMeters+Radial*(Radius*Ratio)+Tangent*(Radius*FMath::Sqrt(1-Ratio*Ratio));
        const auto ToLimb=(ViewTarget-Ship->CameraAbsoluteMeters()).Normalized();
        const auto ToSun=(Sun->Definition.centerMeters-Ship->CameraAbsoluteMeters()).Normalized();
        // Keep the daylight limb in the lower half while bringing the real
        // solar disk into frame. The previous 80/20 blend often showed only
        // a blue/gold terminator, which read as a static Earth view rather
        // than sunrise.
        ViewTarget=Ship->CameraAbsoluteMeters()+(ToLimb*0.25+ToSun*0.75).Normalized()*1000000.0;
    }
    else if(Simulation->Telemetry("moon").surfaceAltitudeMeters<20000.0)
    {
        const auto* Moon=Ship->Director()->Catalog().Find(TEXT("moon"));
        const auto Radial=(Simulation->State().positionMeters-Moon->Definition.centerMeters).Normalized();
        const auto Pole=Moon->Definition.bodyFixedToSimulation.Rotate({0,0,1});
        const auto East=star::Vec3d::Cross(Pole,Radial).Normalized();
        const double Altitude=Simulation->Telemetry("moon").surfaceAltitudeMeters;
        const auto Side=star::Vec3d::Cross(Radial,East).Normalized();
        const double LowBlend=FMath::Clamp(Altitude/500.0,0.0,1.0);
        const double Pitch=FMath::DegreesToRadians(FMath::Lerp(8.0,-8.0,LowBlend));
        // Follow the measured valley's eastward flight, rather than looking
        // north while the craft moves sideways through a locked composition.
        CameraOffset=-East*42.0+Side*(5.0*FMath::Sin(CameraTime/19.0))+Radial*(6.0+8.0*LowBlend);
        ViewTarget=Position+(East*FMath::Cos(Pitch)+Radial*FMath::Sin(Pitch))*1000.0;
    }
    Ship->SetGuidedCameraOffset(CameraOffset,Dt);
    Ship->LookAtAbsolute(ViewTarget, Dt);

    if (Command.failed)
    {
        bGuidedTourFailed = true;
        bGuidedTourFinished = false;
        GuidedTourControls = {};
        GuidedTourControls.hasThrottle = true;
        GuidedTourControls.brake = true;
        bScanHeld = false;
        bBrakeHeld = false;
    }
    else if (Command.complete)
    {
        bGuidedTourFinished = true;
        bGuidedTourFailed = false;
        GuidedTourControls = {};
        GuidedTourControls.hasThrottle = true;
        GuidedTourControls.brake = true;
        bScanHeld = false;
        bBrakeHeld = true;
    }
    if ((bGuidedTourFinished || bGuidedTourFailed) && !bFlightPaused)
    {
        SetFlightPaused(true);
        GuidedTourControls.paused = true;
    }
}

void AStarPlayerController::ApplyGuidedTourControls(star::FlightInput& Controls) const
{
    Controls = GuidedTourControls;
    Controls.paused = bFlightPaused || bPhotoMode || bTourPreparing;
    if (Controls.paused)
    {
        Controls.hasThrottle = true;
        Controls.throttle = 0.0;
        Controls.takeoff = false;
        Controls.brake = false;
    }
}

void AStarPlayerController::UpdateGuidedTourObservation()
{
    if (!bGuidedTour || GuidedTourPendingObjective.IsEmpty() || bGuidedTourActualScanComplete) return;
    for (const auto& Objective : Snapshot.Objectives)
    {
        if (Objective.Id == GuidedTourPendingObjective && Objective.bComplete)
        {
            // This acknowledgement is produced only after the current frame's
            // Director::Observe and Exploration::SubmitObservation path.
            bGuidedTourActualScanComplete = true;
            return;
        }
    }
}

void AStarPlayerController::SetEnhancedStarsEnabled(bool Enabled)
{
    Enabled=false;bEnhancedStars = false;
    if (Ship && Ship->Director()) Ship->Director()->SetEnhancedStars(Enabled);
}

void AStarPlayerController::SetFlightAssistEnabled(bool Enabled)
{
    bFlightAssistPreference = Enabled;
    if (bGuidedTour && bGuidedTourAssistCaptured) bGuidedTourSavedFlightAssist = Enabled;
    if (Ship && Ship->Simulation()) Ship->Simulation()->SetFlightAssistEnabled(Enabled);
}

void AStarPlayerController::ClearInputHandoff()
{
    ResetNavigation();
    bScanHeld = false;
    bBrakeHeld = false;
    bTakeoffRequested = false;
    Axes.Reset();
    KeyboardThrottle = 0.0f;
    if (auto* Plugin = FStarFlightInputModule::GetIfAvailable()) Plugin->SetGameplayEnabled(false);
}

void AStarPlayerController::UpdateGuidedTourQA(double Dt)
{
    if (!bGuidedTourTest || !bGuidedTourTestStarted || !Ship || !Ship->Simulation()) return;
    GuidedTourQAElapsed += FMath::Max(0.0, Dt);
    GuidedTourQASampleElapsed += FMath::Max(0.0, Dt);
    if (GuidedTourQASampleElapsed < 1.0 && !bGuidedTourFinished && !bGuidedTourFailed) return;
    GuidedTourQASampleElapsed = 0.0;
    IFileManager::Get().MakeDirectory(*GuidedTourQADirectory, true);
    if(!bTourCameraHandoffChecked&&!bTourPreparing&&GuidedTourQAElapsed>=12.0)
    {
        bTourCameraHandoffChecked=true;
        const auto Position=Ship->CameraAbsoluteMeters(),Forward=Ship->CameraForwardSimulation();
        Ship->SetLook(1.0f,1.0f,false);
        const auto After=Ship->CameraForwardSimulation();
        const double Turn=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(star::Vec3d::Dot(Forward,After),-1.0,1.0)));
        const double PositionJump=(Ship->CameraAbsoluteMeters()-Position).Length();
        Ship->SetLook(0,0,false);
        const double IdleTurn=(Ship->CameraForwardSimulation()-After).Length();
        Ship->RecenterLook();
        const double HomeTurn=(Ship->CameraForwardSimulation()-After).Length();
        const bool Passed=Turn>0.5&&Turn<2.5&&PositionJump<0.001&&IdleTurn<1e-5&&HomeTurn<1e-5;
        const FString CameraResult=FString::Printf(TEXT("{\"passed\":%s,\"turnDegrees\":%.9f,\"positionJumpMeters\":%.9f,\"idleDirectionDelta\":%.9f,\"homeDirectionDelta\":%.9f,\"scope\":\"actual packaged camera methods; not physical input\"}"),Passed?TEXT("true"):TEXT("false"),Turn,PositionJump,IdleTurn,HomeTurn);
        if(!FFileHelper::SaveStringToFile(CameraResult,*(GuidedTourQADirectory/TEXT("camera-handoff.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!Passed)
        {bGuidedTourFailed=true;GuidedTourFailure=TEXT("ツアーの手動視点引継ぎ検査に失敗しました。");}
    }
    if(GuidedTourLastCaptureStage!=GuidedTourStage)
    {
        GuidedTourLastCaptureStage=GuidedTourStage;
        GuidedTourNextShotAt=GuidedTourQAElapsed+2.0;
    }
    if(GuidedTourNextShotAt>=0&&GuidedTourQAElapsed>=GuidedTourNextShotAt&&GuidedTourStage!=TEXT("preparing"))
    {
        GuidedTourNextShotAt=-1;
        if(!FParse::Param(FCommandLine::Get(),TEXT("StarTourNoScreenshots")))
        FScreenshotRequest::RequestScreenshot(GuidedTourQADirectory/(FString::Printf(TEXT("%03llu-"),GuidedTourQASample)+GuidedTourStage+TEXT(".png")),true,false);
    }
    if(bGuidedTourRecording&&(GuidedTourQAElapsed>=75.0||bGuidedTourFinished||bGuidedTourFailed))
    {
        UAudioMixerBlueprintLibrary::StopRecordingOutput(GetWorld(),EAudioRecordingExportType::WavFile,
            TEXT("tour-runtime-audio"),GuidedTourQADirectory);
        bGuidedTourRecording=false;
    }
    const FString ProgressFilename = GuidedTourQADirectory / TEXT("tour-progress.jsonl");
    const FString ProgressLine = TourJsonLine(*Ship, GuidedTourStage, GuidedTourTitle, GuidedTourProgress,
        GuidedTourQAElapsed, ++GuidedTourQASample, GuidedTourPendingObjective, bGuidedTourActualScanComplete);
    if(!FFileHelper::SaveStringToFile(ProgressLine + TEXT("\n"), *ProgressFilename,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead))
    {
        bGuidedTourFinished=false;bGuidedTourFailed=true;
        GuidedTourFailure=TEXT("ツアーの検証記録を書き込めませんでした。");
        UE_LOG(LogTemp,Error,TEXT("STAR tour QA progress write failed"));
    }
    StarTrackingProbe::Append(*this, *Ship, GuidedTourQADirectory / TEXT("tracking.jsonl"),&Snapshot);

    if ((bGuidedTourFinished || bGuidedTourFailed) && !bGuidedTourQuitRequested)
    {
        bGuidedTourQuitRequested = true;
        if(!SaveGame())
        {
            bGuidedTourFinished=false;bGuidedTourFailed=true;
            GuidedTourFailure=TEXT("ツアーの保存を確認できませんでした。");
        }
        const FString Result = bGuidedTourFinished ? TEXT("complete") : TEXT("failed");
        const FString Reason = bGuidedTourFailed ? GuidedTourFailure : TEXT("地球から月面着陸・離陸を経て、土星の環の観測を完了");
        const FString ResultLine = TourJsonLine(*Ship, GuidedTourStage, GuidedTourTitle, GuidedTourProgress,
            GuidedTourQAElapsed, GuidedTourQASample, GuidedTourPendingObjective, bGuidedTourActualScanComplete,
            Result + TEXT(": ") + Reason);
        if(!FFileHelper::SaveStringToFile(ResultLine, *(GuidedTourQADirectory / TEXT("tour-result.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            UE_LOG(LogTemp,Error,TEXT("STAR tour QA result write failed"));
        RequestQAQuit();
    }
}
