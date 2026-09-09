#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "HAL/PlatformTime.h"

namespace {
const TCHAR* LocalStages[]={TEXT("00_normal_start"),TEXT("01_C_accelerate"),TEXT("02_turn_right"),TEXT("03_turn_left"),
 TEXT("04_brake"),TEXT("05_accelerate_again"),TEXT("06_pause"),TEXT("07_resume"),TEXT("08_C_decelerate"),
 TEXT("09_C_accelerate"),TEXT("10_save"),TEXT("11_load"),TEXT("12_resume_loaded"),TEXT("13_change_target"),
 TEXT("14_local_autopilot"),TEXT("15_manual_override"),TEXT("16_rapid_C"),TEXT("17_photo"),TEXT("18_photo_exit"),
 TEXT("19_precision"),TEXT("20_return_to_drive")};
const double LocalDurations[]={3,10,3,3,5,10,1,10,5,10,1,1,10,2,6,4,4,1,4,5,8};
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
    NavigationQAStageName=LocalStages[NavigationQAStage];
    switch(NavigationQAStage){
    case 0: if(Ship->IsCockpitView())HandleAction(TEXT("ToggleView"));break;
    case 1: case 9: HandleAction(TEXT("ToggleCruise"));break;
    case 8: HandleAction(TEXT("Pause"));HandleAction(TEXT("ToggleCruise"));break;
    case 6: HandleAction(TEXT("Pause"));break;
    case 7: case 12: HandleAction(TEXT("Resume"));break;
    case 10: LocalFlightQASaved=Sim.State();if(!RequireNavigationQA(SaveGame(),TEXT("Local driving save failed")))return;break;
    case 11:
        if(!RequireNavigationQA(LoadGame(),TEXT("Local driving load failed")))return;
        if(!RequireNavigationQA((Sim.State().positionMeters-LocalFlightQASaved.positionMeters).Length()<.001&&
            Sim.State().mode==star::FlightMode::LocalCruise,TEXT("Load lost position or local cruise mode")))return;
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
        State.orientation.IsFinite()&&Dt>=0&&Delta<=20001*Dt+2&&Speed<=20001,
        TEXT("Local flight exceeded speed, recovered or jumped")))return;
    if(!RequireNavigationQA(!Navigation.Tick(*Ship->Simulation()).requiresSafetyPause,
        TEXT("Local cruise incorrectly requested interplanetary safety pause")))return;
    if(NavigationQAAppliedInput.paused&&!RequireNavigationQA(Delta<.001&&Dt==0,
        TEXT("Pause/photo advanced local flight")))return;
    const double Age=FPlatformTime::Seconds()-NavigationQAStageWall;
    if(Age<LocalDurations[NavigationQAStage])return;
    const int S=NavigationQAStage;
    if((S==1||S==5||S==7||S==9||S==12||S==20)&&!RequireNavigationQA(Speed>15000&&!bFlightPaused,
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
