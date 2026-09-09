// Host-side policy test: the .inc contains the actual controller method bodies,
// extracted by Test-Runtime.ps1. UE input dispatch, Slate and hardware are not
// emulated or accepted here; trivial UI/device calls are inert host adapters.
#include "Navigation/NavigationPilot.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#define TEXT(value) value
namespace FMath { double Abs(double value) { return std::abs(value); } }
namespace EKeys { enum Key { W, S }; }
struct FInputModeGameOnly { void SetConsumeCaptureMouseDown(bool) {} };
struct FSlateApplication {
    static bool IsInitialized() { return false; }
    static FSlateApplication& Get() { static FSlateApplication app;return app; }
    bool IsActive() { return true; }
};
struct FStarFlightInputModule {
    static FStarFlightInputModule* GetIfAvailable() { return nullptr; }
    void SetGameplayEnabled(bool) {}
};
struct AxisMap { void Reset() {} };
struct ShipAdapter {
    star::FlightSimulation* simulation;
    star::FlightSimulation* Simulation() { return simulation; }
};
struct AStarPlayerController {
    ShipAdapter* Ship=nullptr;
    bool bGuidedTour=false,bBenchmark=false,bAcceptance=false,bEVAQA=false;
    bool bNavigationQA=false;
    bool bFlightPaused=false,bPhotoMode=false,bSessionStarted=true,bMainMenu=false;
    bool InputSettings=false,EVAPawn=false,bShowMouseCursor=false;
    bool bTakeoffRequested=false,bKeyboardLiftHeld=false,bScanHeld=false,bBrakeHeld=false;
    bool bNavigationInitialized=false,bNavigationSafetyPause=false,bNavigationBrakingRecovery=false;
    double KeyboardThrottle=0,AutopilotManualThrottle=0;
    bool W=false,S=false;
    AxisMap Axes;
    star::navigation::NavigationPilot Navigation;
    star::navigation::NavigationCommand NavigationCommand;
    bool IsInputKeyDown(EKeys::Key key) const { return key==EKeys::W?W:S; }
    void SetStatus(const char*,double=5) {}
    void SetMenuInputMode() {}
    void SetInputMode(const FInputModeGameOnly&) {}
    bool NavigationBypassed() const;
    void ResetNavigation();
    void SetFlightPaused(bool);
    void ToggleNavigationSafeBrake();
    void ApplyNavigationControls(star::FlightInput&);
};
#include "runtime-methods.inc"

namespace {
int checks=0;
void Check(bool value,const char* message) { ++checks;if(!value) throw std::runtime_error(message); }
std::vector<star::BodyDefinition> Bodies() {
    star::BodyDefinition earth,moon;
    earth.id="earth";earth.radiusMeters=6371000;earth.atmosphereHeightMeters=100000;
    moon.id="moon";moon.centerMeters={384400000,0,0};moon.radiusMeters=1737400;
    moon.landable=true;
    return {earth,moon};
}
star::FlightState State() {
    star::FlightState state;
    state.positionMeters={7371000,0,0};state.targetBodyId="moon";
    state.orientation=star::Quatd::FromForwardUp({1,0,0},{0,0,1});
    state.mode=star::FlightMode::Cruise;return state;
}
}
int main() {
    try {
        star::FlightSimulation sim(Bodies());
        auto state=State();Check(sim.RestoreState(state),"fixture restore");
        ShipAdapter ship{&sim};AStarPlayerController controller;controller.Ship=&ship;
        controller.ResetNavigation();
        star::FlightInput input;input.hasThrottle=true;input.throttle=1;
        controller.ApplyNavigationControls(input);
        Check(sim.State().mode==star::FlightMode::Maneuver,"selection alone gates retained Cruise");
        Check(!controller.NavigationCommand.highSpeedAuthorized,"selection cannot grant permission");
        Check(controller.Navigation.ConfirmTransfer(sim),"aligned explicit confirmation");
        sim.SetMode(star::FlightMode::Cruise);controller.ApplyNavigationControls(input);
        Check(sim.State().mode==star::FlightMode::Cruise&&input.smoothGuidance,"granted manual cruise is smooth");

        controller.ResetNavigation();
        Check(controller.Navigation.StartAutopilot(sim),"alignment AP starts");
        input={};controller.ApplyNavigationControls(input);
        Check(input.throttle==0&&input.brake&&!controller.NavigationCommand.highSpeedAuthorized,"AP before F cannot accelerate");
        input={};input.yaw=0.4;controller.ApplyNavigationControls(input);
        Check(!controller.Navigation.Tick(sim).autopilotActive,"flight input cancels AP");

        state=State();state.velocityMetersPerSecond={2500,0,0};
        state.orientation=star::Quatd::FromForwardUp({-1,0,0},{0,0,1});
        Check(sim.RestoreState(state),"wrong-heading high-energy restore");
        controller.ResetNavigation();input={};input.hasThrottle=true;input.throttle=1;
        controller.ApplyNavigationControls(input);
        Check(input.paused&&controller.bFlightPaused,"old highspeed save is held");
        Check((sim.State().positionMeters-state.positionMeters).Length()==0&&
            (sim.State().velocityMetersPerSecond-state.velocityMetersPerSecond).Length()==0&&
            sim.State().mode==state.mode,"safety hold preserves position velocity and mode");
        controller.Navigation.SetPaused(false);
        Check(!controller.Navigation.ConfirmTransfer(sim),"wrong heading still rejects F");
        controller.Navigation.SetPaused(true);
        controller.ToggleNavigationSafeBrake();
        Check(controller.bNavigationBrakingRecovery&&!controller.bFlightPaused,"explicit recovery releases brake-only path");
        const auto start=sim.State().positionMeters;
        double elapsed=0;
        while(!controller.bFlightPaused&&elapsed<60) {
            input={};input.hasThrottle=true;input.throttle=1;
            input.yaw=1;input.pitch=1;input.roll=1;input.strafeUp=1;input.strafeRight=1;input.takeoff=true;
            controller.W=true;
            controller.ApplyNavigationControls(input);
            Check(input.throttle==0&&input.brake&&input.smoothGuidance&&
                input.yaw==0&&input.pitch==0&&input.roll==0&&input.strafeUp==0&&input.strafeRight==0&&!input.takeoff,
                "recovery ignores forward throttle and every flight control");
            Check(!controller.Navigation.Tick(sim).highSpeedAuthorized,"brake recovery cannot grant transfer");
            sim.Advance(1.0/60.0,input);elapsed+=1.0/60.0;
        }
        Check(controller.bFlightPaused&&!controller.bNavigationBrakingRecovery,"recovery ends in paused manual");
        Check(sim.State().velocityMetersPerSecond.Length()<1&&sim.State().mode==star::FlightMode::Maneuver,"recovery physically brakes below one mps");
        Check((sim.State().positionMeters-start).Length()>1,"recovery moves continuously rather than resetting position");
        Check(sim.State().recoveryCount==0,"recovery did not use collision-reset fallback");
        controller.ToggleNavigationSafeBrake();
        Check(!controller.bNavigationBrakingRecovery,"ordinary low-speed state cannot enter special recovery");

        Check(sim.RestoreState(state),"cancel fixture restore");controller.ResetNavigation();
        controller.ApplyNavigationControls(input);controller.ToggleNavigationSafeBrake();
        controller.SetFlightPaused(true);
        Check(!controller.bNavigationBrakingRecovery&&controller.bFlightPaused,"pause cancels special recovery");
        controller.bGuidedTour=true;controller.bFlightPaused=false;input={};input.throttle=1;
        controller.ApplyNavigationControls(input);
        Check(input.throttle==1&&!input.paused,"explicit tour driver keeps its own controls");
        std::cout<<"PASS "<<checks<<" extracted runtime policy assertions (host adapters; not UE/game/hardware)\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
