#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Runtime/StarDataCatalog.h"
#include "Runtime/StarTrackingProbe.h"
#include "Validation/RoutePilot.h"
#include "Exploration/StarExplorationSubsystem.h"
#include "UI/StarHUDWidget.h"
#include "StarFlightInputModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UnrealClient.h"

// Explicit command-line acceptance mode. It uses ordinary flight controls and
// actual camera observations; it is not exposed as a player autopilot.
void AStarPlayerController::ConfigureAcceptance()
{
    bAcceptance=FParse::Param(FCommandLine::Get(),TEXT("StarAcceptance"));
    if(!bAcceptance) return;
    bBenchmark=false;
    bAcceptanceResume=FParse::Param(FCommandLine::Get(),TEXT("StarAcceptanceResume"));
    FString Run;
    FParse::Value(FCommandLine::Get(),TEXT("StarAcceptanceRun="),Run);
    if(Run.IsEmpty()) Run=FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
    bool Valid=Run.Len()<=80;
    for(const TCHAR C:Run) Valid&=FChar::IsAlnum(C)||C==TEXT('-')||C==TEXT('_');
    if(!Valid) { AcceptanceFailure=TEXT("Invalid acceptance run identifier");bAcceptancePendingFailure=true;Run=TEXT("invalid-run"); }
    AcceptanceDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("STAR-QA/Acceptance")/Run);
    IFileManager::Get().MakeDirectory(*AcceptanceDirectory,true);
}

void AStarPlayerController::UpdateAcceptanceBeforeFlight(star::FlightInput& Controls)
{
    Controls={};Controls.hasThrottle=true;
    if(bAcceptanceFinished) { Controls.paused=true;return; }
    if(bAcceptancePendingFailure) { Controls.paused=true;FinishAcceptance(false,AcceptanceFailure);return; }
    auto* Sim=Ship->Simulation();
    if(!bAcceptanceStarted)
    {
        bAcceptanceStarted=true;AcceptanceStartedAt=Clock;
        if(auto* Input=FStarFlightInputModule::GetIfAvailable()) Input->SetGameplayEnabled(false);
        if(bAcceptanceResume)
        {
            FString Previous;
            TSharedPtr<FJsonObject> Json;
            if(!FFileHelper::LoadFileToString(Previous,*SaveFilename())||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Previous),Json)||!Json.IsValid())
            { FinishAcceptance(false,TEXT("Restart save is unreadable"));Controls.paused=true;return; }
            FString Flight,Progress;
            star::FlightState Expected;
            if(!Json->TryGetStringField(TEXT("flight"),Flight)||!Json->TryGetStringField(TEXT("exploration"),Progress)||
                !star::DeserializeFlightState(TCHAR_TO_UTF8(*Flight),Expected)||!LoadGame())
            { FinishAcceptance(false,TEXT("Normal LoadGame failed"));Controls.paused=true;return; }
            const auto& Actual=Sim->State();
            const double QDot=FMath::Abs(Actual.orientation.w*Expected.orientation.w+Actual.orientation.x*Expected.orientation.x+
                Actual.orientation.y*Expected.orientation.y+Actual.orientation.z*Expected.orientation.z);
            if((Actual.positionMeters-Expected.positionMeters).Length()>1e-5||
                (Actual.velocityMetersPerSecond-Expected.velocityMetersPerSecond).Length()>1e-8||QDot<1-1e-10||
                Actual.mode!=Expected.mode||Actual.recoveryCount!=Expected.recoveryCount||
                Actual.targetBodyId!=Expected.targetBodyId||Actual.landedBodyId!=Expected.landedBodyId||
                Actual.gearDeployed!=Expected.gearDeployed||Actual.simulationTimeSeconds!=Expected.simulationTimeSeconds||
                Exploration->ExportProgressJson()!=Progress)
            { FinishAcceptance(false,TEXT("Restart state or discovery mismatch"));Controls.paused=true;return; }
            AcceptanceResumeSimulationTime=Actual.simulationTimeSeconds;
            bAcceptanceResumeLanded=Actual.mode==star::FlightMode::Landed;
            AcceptanceResumeBody=UTF8_TO_TCHAR(Actual.landedBodyId.c_str());
            if(bAcceptanceResumeLanded)
            {
                AcceptanceResumeAltitude=Sim->Telemetry(Actual.landedBodyId).surfaceAltitudeMeters;
                if(Ship->IsCockpitView()) Ship->ToggleView();
                Ship->SetLook(20,-25,false);
            }
            AcceptanceStage=TEXT("restart_continue");
        }
        else
        {
            if(IFileManager::Get().FileExists(*SaveFilename()))
            { FinishAcceptance(false,TEXT("Run already contains a save; choose a new run identifier"));Controls.paused=true;return; }
            // Exercise the actual menu save protection before starting flight.
            if(SaveGame()||IFileManager::Get().FileExists(*SaveFilename()))
            { FinishAcceptance(false,TEXT("Initial menu save guard failed"));Controls.paused=true;return; }
            auto* Director=Ship->Director();
            AcceptancePilot=MakeShared<star::validation::RoutePilot>(Director->Catalog().SimulationBodies(),
                [Director](const auto& Body,const auto& Direction,auto& Sample){return Director->Catalog().SampleTerrain(Body,Direction,Sample);});
            bSessionStarted=true;
        }
        bMainMenu=false;bFlightPaused=false;bPhotoMode=false;
        if(HUD) HUD->SetMainMenuVisible(false);
        AcceptanceInitialRecoveries=Sim->State().recoveryCount;
    }
    if(Clock-AcceptanceStartedAt>1200)
    { Controls.paused=true;FinishAcceptance(false,TEXT("Acceptance exceeded 20 minutes"));return; }
    AcceptancePreviousFlight=UTF8_TO_TCHAR(star::SerializeFlightState(Sim->State()).c_str());
    if(bAcceptanceResume)
    {
        if(bAcceptanceResumeLanded)
        {
            Controls.takeoff=Sim->State().mode==star::FlightMode::Landed;
            Controls.strafeUp=1;
        }
        else Controls.strafeRight=0.2;
        return;
    }
    bool ScanComplete=false;
    for(const auto& Entry:Exploration->GetJournalEntries()) if(Entry.ObjectiveId==AcceptanceObjective) ScanComplete=true;
    const auto Command=AcceptancePilot->Tick(*Sim,ScanComplete);
    if(Command.failed) { Controls.paused=true;FinishAcceptance(false,UTF8_TO_TCHAR(Command.failure.c_str()));return; }
    const FString Stage=UTF8_TO_TCHAR(Command.stage.c_str());
    if(Stage!=AcceptanceStage)
    {
        AcceptanceStage=Stage;
        UE_LOG(LogTemp,Display,TEXT("STAR acceptance stage: %s"),*Stage);
        FScreenshotRequest::RequestScreenshot(AcceptanceDirectory/(Stage+TEXT(".png")),true,false);
        Ship->RecenterLook();
        if(Stage==TEXT("moon_landed_scan")) Ship->SetLook(0,-35,false);
    }
    AcceptanceObjective=UTF8_TO_TCHAR(Command.scanObjectiveId.c_str());
    if(!Command.targetBodyId.empty()) Sim->SetTarget(Command.targetBodyId);
    Sim->SetMode(Command.mode);
    if(Ship->GearDesired()!=Command.gearDeployedDesired) Ship->ToggleLandingGear();
    Controls=Command.controls;
    bScanHeld=Command.scan;
    bAcceptanceRouteComplete=Command.complete;
}

void AStarPlayerController::UpdateAcceptanceAfterFlight()
{
    if(bAcceptanceFinished||!bAcceptanceStarted) return;
    auto* Sim=Ship->Simulation();
    const auto& State=Sim->State();
    if(State.recoveryCount!=AcceptanceInitialRecoveries)
    { FinishAcceptance(false,TEXT("Collision recovery during continuous route"));return; }
    star::FlightState Previous;
    if(star::DeserializeFlightState(TCHAR_TO_UTF8(*AcceptancePreviousFlight),Previous))
    {
        const double Dt=State.simulationTimeSeconds-Previous.simulationTimeSeconds;
        if(Dt<0||(State.positionMeters-Previous.positionMeters).Length()>50*star::SpeedOfLightMps*Dt+0.2)
        { FinishAcceptance(false,TEXT("Discontinuous position or simulation time"));return; }
    }
    if(Clock-AcceptanceLastSample>=1)
    {
        AcceptanceLastSample=Clock;
        StarTrackingProbe::Append(*this,*Ship,AcceptanceDirectory/TEXT("tracking.jsonl"),&Snapshot);
        auto Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("stage"),AcceptanceStage);
        Row->SetStringField(TEXT("flight"),UTF8_TO_TCHAR(star::SerializeFlightState(State).c_str()));
        Row->SetStringField(TEXT("exploration"),Exploration->ExportProgressJson());
        Row->SetNumberField(TEXT("wallSeconds"),Clock-AcceptanceStartedAt);
        Row->SetStringField(TEXT("dataEpoch"),Ship->Director()->Catalog().DataEpoch());
        FString Text;FJsonSerializer::Serialize(Row,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));Text+=TEXT("\n");
        FFileHelper::SaveStringToFile(Text,*(AcceptanceDirectory/(bAcceptanceResume?TEXT("resume.jsonl"):TEXT("flight.jsonl"))),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    }
    if(!bAcceptanceResume&&State.mode==star::FlightMode::Landed&&!bAcceptanceLandedSaved)
    {
        bAcceptanceLandedSaved=SaveGame();
        if(!bAcceptanceLandedSaved) { FinishAcceptance(false,TEXT("Landed SaveGame failed"));return; }
        const auto PositionBeforeLoad=State.positionMeters;
        const FString ProgressBeforeLoad=Exploration->ExportProgressJson();
        if(IFileManager::Get().Copy(*(AcceptanceDirectory/TEXT("landed-save.json")),*SaveFilename(),false,true)!=COPY_OK)
        { bAcceptanceLandedSaved=false;FinishAcceptance(false,TEXT("Landed save evidence copy failed"));return; }
        if(!LoadGame()||Sim->State().mode!=star::FlightMode::Landed||
            (Sim->State().positionMeters-PositionBeforeLoad).Length()>1e-5||
            Exploration->ExportProgressJson()!=ProgressBeforeLoad)
        { bAcceptanceLandedSaved=false;FinishAcceptance(false,TEXT("Normal landed LoadGame roundtrip failed"));return; }
    }
    if(bAcceptanceResume&&!bAcceptanceResumePhoto&&State.simulationTimeSeconds-AcceptanceResumeSimulationTime>=1)
    {
        bAcceptanceResumePhoto=true;
        FScreenshotRequest::RequestScreenshot(AcceptanceDirectory/TEXT("resume-flight.png"),true,false);
    }
    if(bAcceptanceResume&&State.simulationTimeSeconds-AcceptanceResumeSimulationTime>=3)
    {
        if(bAcceptanceResumeLanded&&(State.mode==star::FlightMode::Landed||
            Sim->Telemetry(TCHAR_TO_UTF8(*AcceptanceResumeBody)).surfaceAltitudeMeters<AcceptanceResumeAltitude+0.5))
        { FinishAcceptance(false,TEXT("Landed restart did not take off safely"));return; }
        if(!SaveGame()) { FinishAcceptance(false,TEXT("Save after resumed play failed"));return; }
        FinishAcceptance(true,TEXT("Normal load, state comparison, continued flight and save passed"));
    }
    else if(!bAcceptanceResume&&bAcceptanceRouteComplete)
    {
        if(!bAcceptanceLandedSaved||!SaveGame()) { FinishAcceptance(false,TEXT("Route or final save incomplete"));return; }
        FinishAcceptance(true,TEXT("Continuous automated route complete; separate process restart still required"));
    }
}

void AStarPlayerController::FinishAcceptance(bool Success,const FString& Reason)
{
    if(bAcceptanceFinished) return;
    bAcceptanceFinished=true;bFlightPaused=true;bScanHeld=false;
    auto Result=MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"),Success);
    Result->SetBoolField(TEXT("restartRun"),bAcceptanceResume);
    Result->SetBoolField(TEXT("restartFromLanded"),bAcceptanceResumeLanded);
    Result->SetBoolField(TEXT("landedSaveVerified"),bAcceptanceLandedSaved);
    Result->SetStringField(TEXT("reason"),Reason);
    Result->SetStringField(TEXT("stage"),AcceptanceStage);
    Result->SetStringField(TEXT("scope"),TEXT("Actual runtime with automated controls; physical input and subjective play remain separate"));
    Result->SetStringField(TEXT("timestamp"),FDateTime::UtcNow().ToIso8601());
    if(Ship&&Ship->Simulation()) Result->SetStringField(TEXT("flight"),UTF8_TO_TCHAR(star::SerializeFlightState(Ship->Simulation()->State()).c_str()));
    if(Exploration) Result->SetStringField(TEXT("exploration"),Exploration->ExportProgressJson());
    FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(Text,*(AcceptanceDirectory/(bAcceptanceResume?TEXT("restart-result.json"):TEXT("route-result.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("STAR acceptance %s: %s"),Success?TEXT("PASS"):TEXT("FAIL"),*Reason);
    ConsoleCommand(TEXT("quit"),false);
}
