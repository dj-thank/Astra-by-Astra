#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Runtime/StarDataCatalog.h"
#include "Exploration/StarExplorationSubsystem.h"
#include "UI/StarHUDWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace
{
FString FlightJson(const star::FlightState& State)
{ return UTF8_TO_TCHAR(star::SerializeFlightState(State).c_str()); }
TSharedPtr<FJsonObject> InputJson(const star::FlightInput& Input)
{
    auto Json=MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("throttle"),Input.throttle);
    Json->SetNumberField(TEXT("yaw"),Input.yaw);Json->SetNumberField(TEXT("pitch"),Input.pitch);
    Json->SetNumberField(TEXT("roll"),Input.roll);Json->SetNumberField(TEXT("strafeRight"),Input.strafeRight);
    Json->SetNumberField(TEXT("strafeUp"),Input.strafeUp);
    Json->SetBoolField(TEXT("hasThrottle"),Input.hasThrottle);Json->SetBoolField(TEXT("brake"),Input.brake);
    Json->SetBoolField(TEXT("takeoff"),Input.takeoff);Json->SetBoolField(TEXT("paused"),Input.paused);
    Json->SetBoolField(TEXT("smoothGuidance"),Input.smoothGuidance);return Json;
}
bool SamePhysicalState(const star::FlightState& A,const star::FlightState& B)
{
    const double Dot=FMath::Abs(A.orientation.w*B.orientation.w+A.orientation.x*B.orientation.x+
        A.orientation.y*B.orientation.y+A.orientation.z*B.orientation.z);
    return (A.positionMeters-B.positionMeters).Length()<1e-7&&
        (A.velocityMetersPerSecond-B.velocityMetersPerSecond).Length()<1e-9&&
        Dot>1-1e-12&&A.simulationTimeSeconds==B.simulationTimeSeconds&&
        A.targetBodyId==B.targetBodyId&&A.recoveryCount==B.recoveryCount;
}
bool SameHeldState(const star::FlightState& A,const star::FlightState& B)
{ return SamePhysicalState(A,B)&&A.mode==B.mode; }
}

// This is an explicit UE test driver, never a NavigationBypassed mode. Actions
// enter HandleAction and all frames still pass through ApplyNavigationControls.
void AStarPlayerController::ConfigureNavigationQA()
{
    NavigationQAStartedWall=FPlatformTime::Seconds();
    FString Requested;
    FParse::Value(FCommandLine::Get(),TEXT("StarNavigationPath="),Requested);
    if(Requested.IsEmpty()||FPaths::IsRelative(Requested))
    { FinishNavigationQA(false,TEXT("StarNavigationQA requires an absolute fresh StarNavigationPath"));return; }
    NavigationQADirectory=FPaths::ConvertRelativePathToFull(Requested);
    FPaths::NormalizeDirectoryName(NavigationQADirectory);
    if(!FPaths::CollapseRelativeDirectories(NavigationQADirectory))
    { FinishNavigationQA(false,TEXT("Invalid navigation QA path"));return; }
    FString UserSaves=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("SaveGames"));
    FPaths::NormalizeDirectoryName(UserSaves);FPaths::CollapseRelativeDirectories(UserSaves);
    if(NavigationQADirectory.Equals(UserSaves,ESearchCase::IgnoreCase)||
       NavigationQADirectory.StartsWith(UserSaves+TEXT("/"),ESearchCase::IgnoreCase)||
       IFileManager::Get().DirectoryExists(*NavigationQADirectory)||IFileManager::Get().FileExists(*NavigationQADirectory))
    { FinishNavigationQA(false,TEXT("QA path must be new and outside player SaveGames; existing data is preserved"));return; }
    if(!IFileManager::Get().MakeDirectory(*NavigationQADirectory,true))
    { FinishNavigationQA(false,TEXT("Cannot create isolated navigation QA directory"));return; }
    bNavigationQAPathReady=true;
    if(bBenchmark||bEVAQA||bGuidedTourTest||FParse::Param(FCommandLine::Get(),TEXT("StarAcceptance")))
    { FinishNavigationQA(false,TEXT("Navigation QA cannot be combined with a bypassing QA driver"));return; }
    UE_LOG(LogTemp,Display,TEXT("STAR navigation QA: real permission gate; isolated output %s; 120 second limit"),*NavigationQADirectory);
}
bool AStarPlayerController::CheckNavigationQADeadline()
{
    if(bNavigationQAFinished) return false;
    if(FPlatformTime::Seconds()-NavigationQAStartedWall>(bLocalFlightQA?240:120))
    { FinishNavigationQA(false,TEXT("Navigation QA exceeded 120 wall seconds"));return false; }
    return true;
}
bool AStarPlayerController::RequireNavigationQA(bool Condition,const TCHAR* Reason)
{
    ++NavigationQAAssertions;
    if(!Condition) FinishNavigationQA(false,Reason);
    return Condition;
}
void AStarPlayerController::UpdateNavigationQABeforeFlight()
{
    if(bLocalFlightQA){UpdateLocalFlightQABefore();return;}
    if(bNavigationQAFinished||!CheckNavigationQADeadline()) return;
    if(!RequireNavigationQA(!NavigationBypassed(),TEXT("Navigation QA must not bypass navigation permission"))) return;
    auto& Sim=*Ship->Simulation();
    if(!bNavigationQAStarted)
    {
        const auto* Earth=Sim.FindBody("earth");const auto* Moon=Sim.FindBody("moon");
        if(!RequireNavigationQA(Earth&&Moon&&Exploration,TEXT("Navigation QA requires Earth, Moon and exploration subsystem"))) return;
        const auto Direction=(Moon->centerMeters-Earth->centerMeters).Normalized();
        star::Vec3d Up{0,0,1};if(FMath::Abs(star::Vec3d::Dot(Direction,Up))>0.9) Up={0,1,0};
        // The only live fixture restore in this QA. Subsequent attitude and
        // movement are produced by real simulation controls, never state edits.
        NavigationQAInitialFixture=star::FlightState{};
        NavigationQAInitialFixture.positionMeters=Earth->centerMeters+Direction*(Earth->radiusMeters+Earth->atmosphereHeightMeters+1000000);
        NavigationQAInitialFixture.orientation=star::Quatd::FromForwardUp(Direction*-1,Up);
        NavigationQAInitialFixture.targetBodyId="earth";
        if(!RequireNavigationQA(Ship->RestoreFlight(NavigationQAInitialFixture),TEXT("Initial navigation fixture restore failed"))) return;
        ResetNavigation();bSessionStarted=true;bMainMenu=false;bPhotoMode=false;
        if(HUD) HUD->SetMainMenuVisible(false);
        SetFlightPaused(false);bNavigationQAStarted=true;
        NavigationQAStageWall=FPlatformTime::Seconds();
    }
    if(bNavigationQAStageAction) return;
    switch(NavigationQAStage)
    {
        case 0:
            NavigationQAStageName=TEXT("00_selection_only");HandleAction(TEXT("TargetMoon"));
            if(!RequireNavigationQA(Sim.State().targetBodyId=="moon"&&!Navigation.Tick(Sim).highSpeedAuthorized,
                TEXT("Selecting Moon must not grant transfer"))) return;
            break;
        case 1:
            NavigationQAStageName=TEXT("01_cruise_rejected");HandleAction(TEXT("ToggleCruise"));
            if(!RequireNavigationQA(Sim.State().mode!=star::FlightMode::Cruise&&!Navigation.Tick(Sim).highSpeedAuthorized,
                TEXT("C accepted an unconfirmed transfer"))) return;
            break;
        case 2:
            NavigationQAStageName=TEXT("02_wrong_heading_F_rejected");HandleAction(TEXT("FlightContext"));
            if(!RequireNavigationQA(Navigation.Tick(Sim).headingErrorDegrees>15&&!Navigation.Tick(Sim).highSpeedAuthorized,
                TEXT("Wrong-heading F unexpectedly granted permission"))) return;
            break;
        case 3:
            NavigationQAStageName=TEXT("03_AP_aligning_without_permission");HandleAction(TEXT("ToggleAutopilot"));
            if(!RequireNavigationQA(Navigation.Tick(Sim).autopilotActive&&!Navigation.Tick(Sim).highSpeedAuthorized,
                TEXT("O failed to start unconfirmed alignment AP"))) return;
            break;
        case 4:
            NavigationQAStageName=TEXT("04_aligned_F_confirmed");HandleAction(TEXT("FlightContext"));
            if(!RequireNavigationQA(Navigation.Tick(Sim).highSpeedAuthorized&&Navigation.Tick(Sim).autopilotActive,
                TEXT("Aligned F through HandleAction did not authorize the active AP"))) return;
            break;
        case 5:
            NavigationQAStageName=TEXT("05_target_change_revoked");HandleAction(TEXT("TargetSaturn"));
            if(!RequireNavigationQA(Sim.State().targetBodyId=="saturn"&&!Navigation.Tick(Sim).highSpeedAuthorized&&
                !Navigation.Tick(Sim).autopilotActive,TEXT("Target change retained transfer permission or AP"))) return;
            break;
        case 6:
        {
            NavigationQAStageName=TEXT("06_highspeed_load_held");
            // Independent legacy-save fixture. Creating the file never changes
            // the live simulation; the normal Load action is its only entry.
            NavigationQAHighSpeedFixture=NavigationQAInitialFixture;
            NavigationQAHighSpeedFixture.targetBodyId="moon";
            NavigationQAHighSpeedFixture.mode=star::FlightMode::Cruise;
            NavigationQAHighSpeedFixture.simulationTimeSeconds=17;
            NavigationQAHighSpeedFixture.throttle=1;
            NavigationQAHighSpeedFixture.velocityMetersPerSecond=star::Forward(NavigationQAInitialFixture.orientation)*-2500;
            auto Save=MakeShared<FJsonObject>();Save->SetNumberField(TEXT("schemaVersion"),1);
            Save->SetStringField(TEXT("dataEpoch"),Ship->Director()->Catalog().DataEpoch());
            Save->SetStringField(TEXT("flight"),FlightJson(NavigationQAHighSpeedFixture));
            Save->SetStringField(TEXT("exploration"),Exploration->ExportProgressJson());
            FString Text;FJsonSerializer::Serialize(Save,TJsonWriterFactory<>::Create(&Text));
            if(!RequireNavigationQA(FFileHelper::SaveStringToFile(Text,*(NavigationQADirectory/TEXT("fixture-input.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&
                FFileHelper::SaveStringToFile(Text,*SaveFilename(),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),TEXT("Cannot write isolated highspeed fixture"))) return;
            HandleAction(TEXT("Load"));
            if(!RequireNavigationQA(bFlightPaused&&bNavigationSafetyPause&&!Navigation.Tick(Sim).highSpeedAuthorized&&
                SameHeldState(Sim.State(),NavigationQAHighSpeedFixture),TEXT("Normal Load did not preserve and pause the unconfirmed highspeed state"))) return;
            break;
        }
        case 7:
        {
            NavigationQAStageName=TEXT("07_paused_F_rejected_and_saved");HandleAction(TEXT("FlightContext"));
            if(!RequireNavigationQA(bFlightPaused&&!Navigation.Tick(Sim).highSpeedAuthorized&&
                SameHeldState(Sim.State(),NavigationQAHighSpeedFixture),TEXT("Wrong-heading F released or changed the highspeed hold"))) return;
            HandleAction(TEXT("Save"));
            // The independent input fixture has no savedAt envelope. Reading
            // the same old fixture after a no-op/failed Save must not pass.
            FString Text,Flight,SavedAt;TSharedPtr<FJsonObject> Json;star::FlightState Saved;
            if(!RequireNavigationQA(FFileHelper::LoadFileToString(Text,*SaveFilename())&&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json)&&Json.IsValid()&&
                Json->TryGetStringField(TEXT("savedAt"),SavedAt)&&!SavedAt.IsEmpty()&&
                Json->TryGetStringField(TEXT("flight"),Flight)&&star::DeserializeFlightState(TCHAR_TO_UTF8(*Flight),Saved)&&
                SameHeldState(Saved,NavigationQAHighSpeedFixture),TEXT("Paused Save did not write its envelope or changed the held flight state"))) return;
            break;
        }
        case 8:
            NavigationQAStageName=TEXT("08_brake_only_recovery");HandleAction(TEXT("NavigationSafeBrake"));
            if(!RequireNavigationQA(bNavigationBrakingRecovery&&!bFlightPaused&&!Navigation.Tick(Sim).highSpeedAuthorized,
                TEXT("B did not enter the explicit unpermitted brake-only path"))) return;
            break;
        case 9:
        {
            NavigationQAStageName=TEXT("09_stopped_manual_pause");HandleAction(TEXT("Save"));
            if(!RequireNavigationQA(bFlightPaused&&!bNavigationBrakingRecovery&&Sim.State().mode==star::FlightMode::Maneuver&&
                Sim.State().velocityMetersPerSecond.Length()<1,TEXT("Recovery did not finish in paused low-speed manual flight"))) return;
            FString Text,Flight,SavedAt;TSharedPtr<FJsonObject> Json;
            if(!RequireNavigationQA(FFileHelper::LoadFileToString(Text,*SaveFilename())&&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json)&&Json.IsValid()&&
                Json->TryGetStringField(TEXT("savedAt"),SavedAt)&&!SavedAt.IsEmpty()&&
                Json->TryGetStringField(TEXT("flight"),Flight)&&Flight==FlightJson(Sim.State())&&
                Flight!=FlightJson(NavigationQAHighSpeedFixture),TEXT("Stopped Save readback is stale, missing or differs from the complete current flight state"))) return;
            break;
        }
        default: FinishNavigationQA(false,TEXT("Unknown navigation QA stage"));return;
    }
    bNavigationQAStageAction=true;bNavigationQACapturePending=true;
}
void AStarPlayerController::InjectNavigationQAInput(star::FlightInput& Controls)
{
    if(bLocalFlightQA){InjectLocalFlightQA(Controls);return;}
    if(bNavigationQAFinished) { Controls=star::FlightInput{};Controls.paused=true;return; }
    // Scripted input, labelled as such in the result. All ordinary permission,
    // disconnect and pause gates still consume this input in PlayerTick.
    if(NavigationQAStage==1) { Controls.hasThrottle=true;Controls.throttle=1; }
    if(NavigationQAStage==8)
    {
        Controls.hasThrottle=true;Controls.throttle=1;
        Controls.yaw=Controls.pitch=Controls.roll=1;
        Controls.strafeRight=Controls.strafeUp=1;Controls.takeoff=true;
    }
    NavigationQARequestedInput=Controls;NavigationQABeforeState=Ship->Simulation()->State();
    bNavigationQARecoveryBeforeGate=bNavigationBrakingRecovery;
}
void AStarPlayerController::ObserveNavigationQAControls(const star::FlightInput& Controls)
{
    NavigationQAAppliedInput=Controls;
    if(bNavigationQAFinished) return;
    // ApplyNavigationControls may finish braking by changing Cruise to Maneuver
    // and pausing. It must not alter physical state; Advance is tested against
    // this post-gate snapshot, including the resulting mode.
    NavigationQABeforeAdvanceState=Ship->Simulation()->State();
    if(bLocalFlightQA)return;
    if(!RequireNavigationQA(SamePhysicalState(NavigationQABeforeAdvanceState,NavigationQABeforeState),
        TEXT("Navigation gate changed position, velocity, attitude, time, target or recovery count"))) return;
    if(Controls.paused)
    {
        const bool FinishedBraking=NavigationQAStage==8&&bNavigationQARecoveryBeforeGate&&
            !bNavigationBrakingRecovery&&bFlightPaused&&
            NavigationQABeforeState.mode==star::FlightMode::Cruise&&
            NavigationQABeforeAdvanceState.mode==star::FlightMode::Maneuver&&
            NavigationQABeforeState.velocityMetersPerSecond.Length()<1;
        if(!RequireNavigationQA(SameHeldState(NavigationQABeforeAdvanceState,NavigationQABeforeState)||FinishedBraking,
            TEXT("Paused navigation gate made an unexpected mode transition"))) return;
    }
    if(NavigationQAStage==3)
        RequireNavigationQA(Controls.hasThrottle&&Controls.throttle==0&&Controls.brake&&!Controls.paused&&
            Navigation.Tick(*Ship->Simulation()).autopilotActive,TEXT("Unconfirmed AP accelerated, stopped steering or became paused"));
    if(NavigationQAStage==8)
        RequireNavigationQA(Controls.hasThrottle&&Controls.throttle==0&&Controls.brake&&Controls.smoothGuidance&&
            Controls.yaw==0&&Controls.pitch==0&&Controls.roll==0&&Controls.strafeRight==0&&Controls.strafeUp==0&&!Controls.takeoff,
            TEXT("Brake-only recovery forwarded a non-braking flight input"));
}
void AStarPlayerController::UpdateNavigationQAAfterFlight()
{
    if(bLocalFlightQA){UpdateLocalFlightQAAfter();return;}
    if(bNavigationQAFinished||!bNavigationQAStarted) return;
    ++NavigationQAFrames;
    const auto& State=Ship->Simulation()->State();
    const double Dt=State.simulationTimeSeconds-NavigationQABeforeAdvanceState.simulationTimeSeconds;
    const double Distance=(State.positionMeters-NavigationQABeforeAdvanceState.positionMeters).Length();
    const double MaximumSpeed=FMath::Max(State.velocityMetersPerSecond.Length(),NavigationQABeforeAdvanceState.velocityMetersPerSecond.Length());
    if(!RequireNavigationQA(Dt>=0&&Distance<=MaximumSpeed*Dt+2&&State.recoveryCount==NavigationQABeforeAdvanceState.recoveryCount,
        TEXT("Position/time discontinuity or collision recovery outside fixture/load boundary"))) return;
    if(NavigationQAAppliedInput.paused&&!RequireNavigationQA(SameHeldState(State,NavigationQABeforeAdvanceState),TEXT("Paused Advance changed physical flight state or mode"))) return;
    if((NavigationQAStage<=3||NavigationQAStage>=5)&&!RequireNavigationQA(!Navigation.Tick(*Ship->Simulation()).highSpeedAuthorized,
        TEXT("Unconfirmed QA stage acquired transfer permission"))) return;
    if((NavigationQAStage==6||NavigationQAStage==7)&&!RequireNavigationQA(SameHeldState(State,NavigationQAHighSpeedFixture)&&bFlightPaused,
        TEXT("Loaded highspeed state advanced while unconfirmed"))) return;
    if(bNavigationQACapturePending) { CaptureNavigationQAStage();bNavigationQACapturePending=false; }
    if(bNavigationQAFinished) return;
    bool Advance=FPlatformTime::Seconds()-NavigationQAStageWall>=0.75;
    if(NavigationQAStage==3)
    {
        const auto Command=Navigation.Tick(*Ship->Simulation());
        Advance=Command.canConfirmTransfer&&Command.headingErrorDegrees<10&&State.velocityMetersPerSecond.Length()<1;
        if(Advance&&!RequireNavigationQA(star::Vec3d::Dot(star::Forward(State.orientation),star::Forward(NavigationQAInitialFixture.orientation))<0&&
            State.simulationTimeSeconds>NavigationQAInitialFixture.simulationTimeSeconds,TEXT("AP alignment did not result from actual simulation steering"))) return;
    }
    if(NavigationQAStage==8)
    {
        Advance=bFlightPaused&&!bNavigationBrakingRecovery&&State.velocityMetersPerSecond.Length()<1;
        if(Advance&&!RequireNavigationQA((State.positionMeters-NavigationQAHighSpeedFixture.positionMeters).Length()>1&&
            State.simulationTimeSeconds>NavigationQAHighSpeedFixture.simulationTimeSeconds&&State.recoveryCount==0,
            TEXT("Braking did not use continuous physical motion without recovery reset"))) return;
    }
    if(!Advance) return;
    if(NavigationQAStage==9)
    {
        for(const auto& Screenshot:NavigationQAScreenshots)
            if(!RequireNavigationQA(IFileManager::Get().FileSize(*Screenshot)>0,TEXT("A stage screenshot was not written"))) return;
        FinishNavigationQA(true,TEXT("Real UE action, AP steering, permission, load hold and brake-only control gates passed; physical input not tested"));
        return;
    }
    ++NavigationQAStage;bNavigationQAStageAction=false;NavigationQAStageWall=FPlatformTime::Seconds();
}
void AStarPlayerController::CaptureNavigationQAStage()
{
    if(!bNavigationQAPathReady||!Ship||!Ship->Simulation()) return;
    const auto Command=Navigation.Tick(*Ship->Simulation());
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("stage"),NavigationQAStageName);
    Json->SetNumberField(TEXT("wallSeconds"),FPlatformTime::Seconds()-NavigationQAStartedWall);
    Json->SetStringField(TEXT("flight"),FlightJson(Ship->Simulation()->State()));
    Json->SetStringField(TEXT("flightBeforeGate"),FlightJson(NavigationQABeforeState));
    Json->SetStringField(TEXT("flightBeforeAdvance"),FlightJson(NavigationQABeforeAdvanceState));
    Json->SetBoolField(TEXT("highSpeedAuthorized"),Command.highSpeedAuthorized);
    Json->SetBoolField(TEXT("autopilotActive"),Command.autopilotActive);
    Json->SetBoolField(TEXT("requiresSafetyPause"),Command.requiresSafetyPause);
    Json->SetBoolField(TEXT("paused"),bFlightPaused);Json->SetBoolField(TEXT("brakeRecovery"),bNavigationBrakingRecovery);
    Json->SetNumberField(TEXT("headingErrorDegrees"),Command.headingErrorDegrees);
    Json->SetStringField(TEXT("hudStatus"),Snapshot.NavigationStatusText);
    Json->SetObjectField(TEXT("requestedInput"),InputJson(NavigationQARequestedInput));
    Json->SetObjectField(TEXT("appliedInput"),InputJson(NavigationQAAppliedInput));
    FString Text;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text));NavigationQAEvents.Add(Text);
    if(!FFileHelper::SaveStringToFile(Text,*(NavigationQADirectory/(NavigationQAStageName+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { FinishNavigationQA(false,TEXT("Cannot write navigation stage evidence"));return; }
    const FString Screenshot=NavigationQADirectory/(NavigationQAStageName+TEXT(".png"));
    NavigationQAScreenshots.Add(Screenshot);FScreenshotRequest::RequestScreenshot(Screenshot,true,false);
    UE_LOG(LogTemp,Display,TEXT("STAR navigation QA stage: %s"),*NavigationQAStageName);
}
void AStarPlayerController::FinishNavigationQA(bool Success,const FString& Reason)
{
    if(bNavigationQAFinished) return;
    bNavigationQAFinished=true;SetFlightPaused(true);
    if(bNavigationQAPathReady)
    {
        auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("success"),Success);
        Result->SetStringField(TEXT("reason"),Reason);Result->SetStringField(TEXT("stage"),NavigationQAStageName);
        Result->SetStringField(TEXT("scope"),TEXT("UE runtime with scripted HandleAction and control inputs; normal navigation gate; physical keyboard, HOTAS, screenshot visual quality and whole-route acceptance are not established"));
        Result->SetNumberField(TEXT("wallSeconds"),FPlatformTime::Seconds()-NavigationQAStartedWall);
        Result->SetNumberField(TEXT("maximumWallSeconds"),bLocalFlightQA?240:120);
        Result->SetBoolField(TEXT("scriptedInput"),true);Result->SetBoolField(TEXT("physicalInputTested"),false);
        Result->SetNumberField(TEXT("assertions"),NavigationQAAssertions);Result->SetNumberField(TEXT("frames"),NavigationQAFrames);
        Result->SetBoolField(TEXT("navigationBypassed"),NavigationBypassed());
        Result->SetStringField(TEXT("savePath"),SaveFilename());
        if(Ship&&Ship->Simulation())
        {
            Result->SetStringField(TEXT("finalFlight"),FlightJson(Ship->Simulation()->State()));
            Result->SetStringField(TEXT("flightBeforeGate"),FlightJson(NavigationQABeforeState));
            Result->SetStringField(TEXT("flightBeforeAdvance"),FlightJson(NavigationQABeforeAdvanceState));
            if(Ship->Director()) Result->SetStringField(TEXT("dataEpoch"),Ship->Director()->Catalog().DataEpoch());
        }
        TArray<TSharedPtr<FJsonValue>> Screenshots;
        for(const auto& Path:NavigationQAScreenshots) Screenshots.Add(MakeShared<FJsonValueString>(Path));
        Result->SetArrayField(TEXT("screenshots"),Screenshots);
        TArray<TSharedPtr<FJsonValue>> Events;
        for(const auto& Event:NavigationQAEvents)
        { TSharedPtr<FJsonObject> Json;if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Event),Json)&&Json.IsValid()) Events.Add(MakeShared<FJsonValueObject>(Json)); }
        Result->SetArrayField(TEXT("stages"),Events);
        FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));
        if(!FFileHelper::SaveStringToFile(Text,*(NavigationQADirectory/TEXT("result.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            UE_LOG(LogTemp,Error,TEXT("STAR navigation QA could not write result.json"));
    }
    UE_LOG(LogTemp,Display,TEXT("STAR navigation QA %s: %s"),Success?TEXT("PASS"):TEXT("FAIL"),*Reason);
    RequestQAQuit();
}
