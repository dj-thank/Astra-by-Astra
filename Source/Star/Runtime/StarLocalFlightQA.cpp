#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "HAL/PlatformTime.h"
#include "UI/StarHUDWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

namespace {
const TCHAR* LocalStages[]={TEXT("00_normal_start"),TEXT("01_C_accelerate"),TEXT("02_turn_right"),TEXT("03_turn_left"),
 TEXT("04_brake"),TEXT("05_accelerate_again"),TEXT("06_pause"),TEXT("07_resume"),TEXT("08_C_decelerate"),
 TEXT("09_C_accelerate"),TEXT("10_save"),TEXT("11_load"),TEXT("12_resume_loaded"),TEXT("13_change_target"),
 TEXT("14_local_autopilot"),TEXT("15_manual_override"),TEXT("16_rapid_C"),TEXT("17_photo"),TEXT("18_photo_exit"),
 TEXT("19_precision"),TEXT("20_return_to_drive")};
const double LocalDurations[]={3,10,3,3,5,10,1,10,5,10,1,1,10,2,6,4,4,1,4,5,8};
const TCHAR* StressStages[]={TEXT("00_normal_start"),TEXT("01_cruise"),TEXT("02_climb_out_of_detail"),TEXT("03_stop_at_altitude"),
 TEXT("04_descend_into_detail"),TEXT("05_stop_after_descent"),TEXT("06_large_heading_change"),TEXT("07_finish")};
const double StressDurations[]={3,10,65,5,90,5,10,2};
}
void AStarPlayerController::UpdateLocalFlightQABefore()
{
    if(bNavigationQAFinished||!CheckNavigationQADeadline())return;
    auto& Sim=*Ship->Simulation();
    if(!bNavigationQAStarted){
        NavigationQAInitialFixture=Sim.State();
        if(!RequireNavigationQA(FMath::Abs(Sim.Telemetry("earth").referenceAltitudeMeters-450000)<1,
            TEXT("Local driving QA must start in the normal Earth voyage")))return;
        HandleAction(TEXT("StartFlight"));
        if(InputSettings){CloseInputSettings();HandleAction(TEXT("Resume"));}
        Ship->Director()->SetClockRate(1);
        bNavigationQAStarted=true;NavigationQAStageWall=FPlatformTime::Seconds();
    }
    if(bNavigationQAStageAction)return;
    if(bLocalFlightStressQA){
        NavigationQAStageName=StressStages[NavigationQAStage];
        if(NavigationQAStage==0&&Ship->IsCockpitView())HandleAction(TEXT("ToggleView"));
        if(NavigationQAStage==1)HandleAction(TEXT("ToggleCruise"));
        if(NavigationQAStage==6)LocalFlightQASaved=Sim.State();
        bNavigationQAStageAction=true;return;
    }
    NavigationQAStageName=LocalStages[NavigationQAStage];
    switch(NavigationQAStage){
    case 0: if(Ship->IsCockpitView())HandleAction(TEXT("ToggleView"));break;
    case 1: case 9: HandleAction(TEXT("ToggleCruise"));break;
    case 8:
        HandleAction(TEXT("Pause"));UpdateSnapshot(0);if(HUD)HUD->ApplySnapshot(Snapshot);
        if(!RequireNavigationQA(HUD&&HUD->IsMenuOpen(),TEXT("Pause menu did not open for C key test")))return;
        FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::C,FModifierKeysState(),0,false,0,0));
        FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::C,FModifierKeysState(),0,false,0,0));
        if(!RequireNavigationQA(!bFlightPaused&&Sim.State().mode==star::FlightMode::Maneuver,
            TEXT("Real Slate C shortcut did not leave the pause menu and exit cruise")))return;
        break;
    case 6: HandleAction(TEXT("Pause"));break;
    case 7: case 12: HandleAction(TEXT("Resume"));break;
    case 10: {
        LocalFlightQASaved=Sim.State();
        if(!RequireNavigationQA(SaveGame()&&SaveGame(),TEXT("Local driving save/backup failed")))return;
        const FString Main=SaveFilename(),Backup=Main+TEXT(".bak"),Held=Backup+TEXT(".qa-kept");FString Before,After;
        if(!RequireNavigationQA(FFileHelper::LoadFileToString(Before,*Main)&&IFileManager::Get().Move(*Held,*Backup)&&IFileManager::Get().MakeDirectory(*Backup),TEXT("Cannot prepare isolated backup failure probe")))return;
        const bool Refused=!SaveGame();FFileHelper::LoadFileToString(After,*Main);
        const bool Restored=IFileManager::Get().DeleteDirectory(*Backup)&&IFileManager::Get().Move(*Backup,*Held);
        if(!RequireNavigationQA(Refused&&Before==After&&Restored,TEXT("Failed backup replaced the live save")))return;
        break;
    }
    case 11:
        // Corrupt only this driver's isolated save; load through the normal F9 action.
        if(!RequireNavigationQA(FFileHelper::SaveStringToFile(TEXT("incomplete save"),*SaveFilename()),TEXT("Cannot prepare damaged-save probe")))return;
        HandleAction(TEXT("Load"));
        if(!RequireNavigationQA(bFlightPaused,TEXT("Normal Load action resumed without user consent")))return;
        if(!RequireNavigationQA((Sim.State().positionMeters-LocalFlightQASaved.positionMeters).Length()<.001&&
            Sim.State().mode==star::FlightMode::LocalCruise,TEXT("Load lost position or local cruise mode")))return;
        {
            FString Before,After;const FString Backup=SaveFilename()+TEXT(".bak");
            FFileHelper::LoadFileToString(Before,*Backup);
            TUniquePtr<IFileHandle> Locked(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*SaveFilename(),false));
            if(!RequireNavigationQA(Locked.IsValid(),TEXT("Cannot lock isolated damaged primary")))return;
            const bool Refused=!SaveGame();FFileHelper::LoadFileToString(After,*Backup);Locked.Reset();
            if(!RequireNavigationQA(Refused&&Before==After&&SaveGame(),TEXT("Recovery followed by failed save destroyed the valid backup")))return;
        }
        break;
    case 13: SelectTarget(TEXT("earth"));break;
    case 14: KeyboardThrottle=0;HandleAction(TEXT("ToggleAutopilot"));break;
    case 16: for(int i=0;i<7;++i)HandleAction(TEXT("ToggleCruise"));break;
    case 17: case 18: HandleAction(TEXT("TogglePhoto"));break;
    case 19: HandleAction(TEXT("Precision"));break;
    case 20: HandleAction(TEXT("ToggleCruise"));break;
    default:break;
    }
    bNavigationQAStageAction=true;
}
void AStarPlayerController::InjectLocalFlightQA(star::FlightInput& Controls)
{
    Controls={};Controls.hasThrottle=true;Controls.paused=bFlightPaused||bPhotoMode||bNavigationQAFinished;
    const int S=NavigationQAStage;
    Controls.throttle=(S==0||S==4||S==6||S==11||S==14||S==17)?0:1;
    Controls.brake=S==4;
    if(S==2)Controls.yaw=.4;
    if(S==3)Controls.yaw=-.4;
    if(S==15)Controls.yaw=.2;
    if(bLocalFlightStressQA){
        Controls.throttle=(S==1||S==2||S==4||S==6)?1:0;Controls.brake=S==3||S==5||S==7;
        Controls.yaw=S==6?.8:0;Controls.pitch=0;
        if(S==2||S==4||S==6){
            const auto& Sim=*Ship->Simulation();const auto* Earth=Sim.FindBody("earth");
            const auto Radial=(Sim.State().positionMeters-Earth->centerMeters).Normalized();
            const double Pitch=FMath::Asin(FMath::Clamp(star::Vec3d::Dot(star::Forward(Sim.State().orientation),Radial),-1.0,1.0));
            const double Desired=S==2?80.0:S==4?-80.0:0.0;
            Controls.pitch=FMath::Clamp((FMath::DegreesToRadians(Desired)-Pitch)*2.0,-.8,.8);
        }
    }
    KeyboardThrottle=static_cast<float>(Controls.throttle);
    NavigationQARequestedInput=Controls;NavigationQABeforeState=Ship->Simulation()->State();
    const auto* Earth=Ship->Simulation()->FindBody("earth");
    LocalFlightQABeforeBodyPosition=Earth->bodyFixedToSimulation.Conjugate().Rotate(NavigationQABeforeState.positionMeters-Earth->centerMeters);
}
void AStarPlayerController::UpdateLocalFlightQAAfter()
{
    if(bNavigationQAFinished||!bNavigationQAStarted)return;
    ++NavigationQAFrames;
    const auto& State=Ship->Simulation()->State();const auto& Before=NavigationQABeforeAdvanceState;
    const double Dt=State.simulationTimeSeconds-Before.simulationTimeSeconds;
    const auto* Earth=Ship->Simulation()->FindBody("earth");
    const auto BodyPosition=Earth->bodyFixedToSimulation.Conjugate().Rotate(State.positionMeters-Earth->centerMeters);
    // Compare within Earth's dated frame, excluding its 30 km/s orbital motion.
    const double Delta=(BodyPosition-LocalFlightQABeforeBodyPosition).Length();LocalFlightQATravel+=Delta;
    const double Speed=State.velocityMetersPerSecond.Length();
    if(!RequireNavigationQA(!NavigationBypassed()&&State.recoveryCount==0&&State.positionMeters.IsFinite()&&
        State.orientation.IsFinite()&&Dt>=0&&Delta<=(Ship->Simulation()->Config().maxLocalCruiseSpeedMps+1)*Dt+2&&Speed<=Ship->Simulation()->Config().maxLocalCruiseSpeedMps+1,
        TEXT("Local flight exceeded speed, recovered or jumped")))return;
    if(!RequireNavigationQA(!Navigation.Tick(*Ship->Simulation()).requiresSafetyPause,
        TEXT("Local cruise incorrectly requested interplanetary safety pause")))return;
    if(NavigationQAAppliedInput.paused&&!RequireNavigationQA(Delta<.001&&Dt==0,
        TEXT("Pause/photo advanced local flight")))return;
    const double Age=FPlatformTime::Seconds()-NavigationQAStageWall;
    if(bLocalFlightStressQA){
        const int S=NavigationQAStage;const double Altitude=Ship->Simulation()->Telemetry("earth").referenceAltitudeMeters;
        const bool DescentDone=S==4&&Age>5&&Altitude<450000;
        if(!DescentDone&&Age<StressDurations[S])return;
        if(S==2&&!RequireNavigationQA(Altitude>1550000,TEXT("Climb did not leave the detail-retention range")))return;
        if((S==3||S==5)&&!RequireNavigationQA(Speed<1,TEXT("High-altitude/descent braking failed")))return;
        if(S==4&&!RequireNavigationQA(Altitude<1200000,TEXT("Descent did not reenter detailed Earth range")))return;
        if(S==6&&!RequireNavigationQA(star::Vec3d::Dot(star::Forward(State.orientation),star::Forward(LocalFlightQASaved.orientation))<.5,
            TEXT("Stress route did not exercise a large heading change")))return;
        CaptureNavigationQAStage();
        if(S==7){FinishNavigationQA(true,TEXT("Normal voyage: high-speed climb, detail-range exit, stop, continuous descent, reentry and large turn passed; no physical controller claim"));return;}
        ++NavigationQAStage;bNavigationQAStageAction=false;NavigationQAStageWall=FPlatformTime::Seconds();return;
    }
    if(Age<LocalDurations[NavigationQAStage])return;
    const int S=NavigationQAStage;
    if((S==1||S==5||S==7||S==9||S==12||S==20)&&!RequireNavigationQA(Speed>75000&&!bFlightPaused,
        TEXT("Ordinary C/resume failed to sustain fast local flight")))return;
    if(S==4&&!RequireNavigationQA(Speed<1,TEXT("Space braking failed to stop local cruise")))return;
    if(S==8&&!RequireNavigationQA(!bFlightPaused&&Speed<301&&State.mode==star::FlightMode::Maneuver,
        TEXT("C exit failed to decelerate to ordinary flight")))return;
    if(S==14&&!RequireNavigationQA(Navigation.Tick(*Ship->Simulation()).autopilotActive,
        TEXT("Local autopilot did not remain active")))return;
    if(S==15&&!RequireNavigationQA(!Navigation.Tick(*Ship->Simulation()).autopilotActive,
        TEXT("Steering did not override local autopilot")))return;
    if(S==19&&!RequireNavigationQA(State.mode==star::FlightMode::Landing&&Speed<31,
        TEXT("Precision entry failed to reduce speed")))return;
    if(S==0||S==1||S==3||S==4||S==8||S==12||S==14||S==19||S==20)CaptureNavigationQAStage();
    if(S==20){
        if(!RequireNavigationQA(LocalFlightQATravel>500000,TEXT("Local driving route did not cover a meaningful distance")))return;
        FinishNavigationQA(true,TEXT("Normal Earth voyage: C acceleration, turns, brake, pause/resume, cruise exit, save/load, target changes, AP override, rapid toggles, photo and precision passed through real controller gates; physical controls untested"));return;
    }
    ++NavigationQAStage;bNavigationQAStageAction=false;NavigationQAStageWall=FPlatformTime::Seconds();
}
