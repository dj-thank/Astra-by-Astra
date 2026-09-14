#include "Navigation/NavigationPilot.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace star;
using namespace star::navigation;
namespace {
int checks=0;
void Check(bool condition,const char* message) {
    ++checks;if(!condition) throw std::runtime_error(message);
}
std::vector<BodyDefinition> LoadBodies(const char* path) {
    std::ifstream input(path);std::vector<BodyDefinition> bodies;
    BodyDefinition body;
    while(input >> body.id >> body.centerMeters.x >> body.centerMeters.y >> body.centerMeters.z
        >> body.radiusMeters >> body.atmosphereHeightMeters >> body.landable >> body.terrainMaxHeightMeters)
        bodies.push_back(body);
    Check(bodies.size()>=3,"dated body fixture unavailable");return bodies;
}
const BodyDefinition& Body(const std::vector<BodyDefinition>& bodies,std::string_view id) {
    for(const auto& body:bodies) if(body.id==id) return body;
    throw std::runtime_error("body missing");
}
FlightState AtEarth(const std::vector<BodyDefinition>& bodies,bool blocked=false) {
    const auto& earth=Body(bodies,"earth");const auto& moon=Body(bodies,"moon");
    const Vec3d direction=(moon.centerMeters-earth.centerMeters).Normalized();
    FlightState state;state.targetBodyId="moon";
    state.positionMeters=earth.centerMeters+direction*(blocked ? -1.0 : 1.0)*(earth.radiusMeters+250000);
    const Vec3d forward=(moon.centerMeters-state.positionMeters).Normalized();
    state.orientation=Quatd::FromForwardUp(forward,{0,0,1});return state;
}
FlightSimulation MakeSimulation(const std::vector<BodyDefinition>& bodies,bool blocked=false) {
    FlightSimulation simulation(bodies);Check(simulation.RestoreState(AtEarth(bodies,blocked)),"initial restore");return simulation;
}
void Select(FlightSimulation& simulation,NavigationPilot& pilot,const std::string& id) {
    Check(simulation.SetTarget(id),"simulation target valid");
    Check(pilot.SelectDestination(simulation,id),"pilot target valid");
}
void GateTests(const std::vector<BodyDefinition>& bodies) {
    auto simulation=MakeSimulation(bodies);NavigationPilot pilot;Select(simulation,pilot,"moon");
    auto command=pilot.Tick(simulation);
    Check(command.canConfirmTransfer,"facing moon permits explicit confirmation");
    Check(!command.highSpeedAuthorized,"selection alone never arms");
    FlightState wrong=simulation.State();
    const Vec3d toward=(Body(bodies,"moon").centerMeters-wrong.positionMeters).Normalized();
    wrong.orientation=Quatd::FromForwardUp(Quatd::FromAxisAngle({0,0,1},20*Pi/180).Rotate(toward),{0,0,1});
    Check(simulation.RestoreState(wrong),"wrong heading fixture");
    Check(!pilot.ConfirmTransfer(simulation),"20 degree heading rejects confirmation");
    wrong.orientation=Quatd::FromForwardUp(Quatd::FromAxisAngle({0,0,1},10*Pi/180).Rotate(toward),{0,0,1});
    Check(simulation.RestoreState(wrong),"valid heading fixture");
    Check(pilot.ConfirmTransfer(simulation),"10 degree heading accepted");
    Check(pilot.Tick(simulation).highSpeedAuthorized,"explicit confirmation grants destination");
    Select(simulation,pilot,"moon");Check(pilot.Tick(simulation).highSpeedAuthorized,"same selection does not reset active flight");
    Select(simulation,pilot,"saturn");Check(!pilot.Tick(simulation).highSpeedAuthorized,"target swap revokes grant");
    Select(simulation,pilot,"moon");Check(!pilot.Tick(simulation).highSpeedAuthorized,"returning target never restores old grant");
    Check(pilot.ConfirmTransfer(simulation),"fresh explicit confirmation");pilot.Cancel();
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"cancel revokes grant");
    Check(pilot.ConfirmTransfer(simulation),"confirmation after cancel");pilot.SetPaused(true);
    Check(pilot.Tick(simulation).controls.paused,"pause output freezes simulation");pilot.SetPaused(false);
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"resume does not restore grant");
    Check(pilot.ConfirmTransfer(simulation),"confirmation after pause");
    Check(simulation.SetTarget("saturn"),"external target mutation fixture");
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"unannounced target mismatch fails closed");
    Check(simulation.SetTarget("moon"),"restore selected target fixture");
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"external restore cannot restore grant");
    Check(!pilot.SelectDestination(simulation,"invalid"),"invalid target rejected");
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"invalid target revokes grant");
    Select(simulation,pilot,"earth");Check(!pilot.ConfirmTransfer(simulation),"same local body cannot arm escape");
}
void LocalSpeedDoesNotFreeze(const std::vector<BodyDefinition>& bodies) {
    auto simulation=MakeSimulation(bodies);
    auto state=simulation.State();state.velocityMetersPerSecond=Forward(state.orientation)*10000;
    Check(simulation.RestoreState(state),"local-speed fixture restored");
    NavigationPilot pilot;Select(simulation,pilot,"earth");
    Check(!pilot.Tick(simulation).requiresSafetyPause,"local planetary driving must not require interplanetary permission");
    pilot.SetPaused(true);pilot.SetPaused(false);
    Check(!pilot.Tick(simulation).requiresSafetyPause,"resume at local speed must not immediately freeze again");
}
void LifecycleTests(const std::vector<BodyDefinition>& bodies) {
    auto simulation=MakeSimulation(bodies);NavigationPilot pilot;Select(simulation,pilot,"moon");
    Check(pilot.ConfirmTransfer(simulation),"lifecycle initial confirm");
    auto state=simulation.State();const auto& moon=Body(bodies,"moon");
    const Vec3d incoming=(Body(bodies,"earth").centerMeters-moon.centerMeters).Normalized();
    state.positionMeters=moon.centerMeters+incoming*100000000;
    Check(simulation.RestoreState(state),"nearer moon fixture");
    Check(pilot.Tick(simulation).highSpeedAuthorized,"nearest body switch is not arrival");
    Check(pilot.StartAutopilot(simulation),"enable autopilot during authorized transfer");
    Check(pilot.Tick(simulation).highSpeedAuthorized,"autopilot toggle does not mistake nearest moon for arrival");
    state.positionMeters=moon.centerMeters+incoming*(moon.radiusMeters+moon.terrainMaxHeightMeters+NavigationPilot::StandOffMeters(moon));
    Check(simulation.RestoreState(state),"arrival fixture");auto arrived=pilot.Tick(simulation);
    Check(!arrived.highSpeedAuthorized && arrived.status==NavigationStatus::Arrived,"standoff arrival revokes transfer");
    Check(arrived.controls.throttle==0 && arrived.controls.brake,"arrival commands bounded hold");

    simulation=MakeSimulation(bodies);pilot.ResetAfterLoad();Select(simulation,pilot,"moon");
    Check(pilot.ConfirmTransfer(simulation),"recovery initial confirm");state=simulation.State();++state.recoveryCount;
    Check(simulation.RestoreState(state),"recovery fixture");
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"contact recovery revokes grant");
    Check(pilot.ConfirmTransfer(simulation),"reconfirm recovered state");state.simulationTimeSeconds=10;
    Check(simulation.RestoreState(state),"later time fixture");pilot.Tick(simulation);state.simulationTimeSeconds=1;
    Check(simulation.RestoreState(state),"rewind fixture");
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"clock rewind revokes grant");
    Check(pilot.ConfirmTransfer(simulation),"reconfirm after rewind");
    pilot.ResetAfterLoad();Select(simulation,pilot,"moon");
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"explicit load reset never serializes permission");
    state=simulation.State();state.mode=FlightMode::Cruise;state.velocityMetersPerSecond=Forward(state.orientation)*1000000;
    state.throttle=1;Check(simulation.RestoreState(state),"legacy highspeed fixture");
    pilot.ResetAfterLoad();Select(simulation,pilot,"moon");const auto pending=pilot.Tick(simulation);
    Check(pending.requiresSafetyPause && !pending.highSpeedAuthorized,"legacy highspeed restore requires runtime safety pause");
    Check((simulation.State().velocityMetersPerSecond-state.velocityMetersPerSecond).Length()==0,"navigation never snaps loaded velocity");
    auto paused=pending.controls;paused.paused=true;simulation.Advance(0.1,paused);
    Check((simulation.State().positionMeters-state.positionMeters).Length()==0,"runtime safety pause preserves exact loaded position");
    Check((simulation.State().velocityMetersPerSecond-state.velocityMetersPerSecond).Length()==0,"runtime safety pause preserves exact loaded velocity");

    simulation=MakeSimulation(bodies);pilot.ResetAfterLoad();Select(simulation,pilot,"moon");
    Check(pilot.ConfirmTransfer(simulation),"catalog change initial confirmation");
    auto changedBodies=bodies;for(auto& body:changedBodies) if(body.id=="moon") body.centerMeters.x+=1000;
    auto changed=MakeSimulation(changedBodies);
    Check(!pilot.Tick(changed).highSpeedAuthorized,"same id with changed position invalidates authorization");
}
void BlockedRouteTest(const std::vector<BodyDefinition>& bodies) {
    auto obstructed=bodies;BodyDefinition blocker;blocker.id="blocking_fixture";
    blocker.centerMeters=(Body(bodies,"moon").centerMeters+Body(bodies,"earth").centerMeters)*0.5;
    blocker.radiusMeters=10000000;obstructed.push_back(blocker);
    auto simulation=MakeSimulation(obstructed);NavigationPilot pilot;Select(simulation,pilot,"moon");
    Check(pilot.StartAutopilot(simulation),"blocked route can align while unconfirmed");
    Check(!pilot.ConfirmTransfer(simulation),"unhandled intervening body refuses autopilot transfer");
    const auto command=pilot.Tick(simulation);
    Check(command.status==NavigationStatus::RouteBlocked && !command.highSpeedAuthorized && !command.autopilotActive,
        "route failure leaves no highspeed grant or driving autopilot");
}
void HoldAndAlignmentTests(const std::vector<BodyDefinition>& bodies) {
    auto simulation=MakeSimulation(bodies);NavigationPilot pilot;
    Select(simulation,pilot,"earth");Check(pilot.StartAutopilot(simulation),"start local hold");
    const auto start=simulation.State();
    for(int i=0;i<300;++i) {
        const auto command=pilot.Tick(simulation);
        Check(command.status==NavigationStatus::LocalHold && !command.highSpeedAuthorized,"local hold stays local");
        simulation.SetMode(command.mode);simulation.Advance(1.0/30,command.controls);
    }
    Check((simulation.State().positionMeters-start.positionMeters).Length()<0.001,"stationary local hold retains altitude and position");
    pilot.Cancel();Select(simulation,pilot,"moon");
    auto state=simulation.State();state.orientation=Quatd::FromForwardUp(-(Body(bodies,"moon").centerMeters-state.positionMeters).Normalized(),{0,0,1});
    Check(simulation.RestoreState(state),"opposite heading fixture");
    Check(pilot.StartAutopilot(simulation),"start unconfirmed alignment");
    bool ready=false;
    for(int i=0;i<900;++i) {
        const auto command=pilot.Tick(simulation);
        Check(!command.highSpeedAuthorized && command.controls.throttle==0,"alignment never arms or drives");
        ready=ready||command.canConfirmTransfer;
        simulation.SetMode(command.mode);simulation.Advance(1.0/30,command.controls);
    }
    Check(ready && pilot.Tick(simulation).headingErrorDegrees<1,"ordinary angular input aligns target from opposite heading");
    Check((simulation.State().positionMeters-state.positionMeters).Length()<0.001,"unconfirmed autopilot does not depart");
    pilot.Cancel();Check(!pilot.Tick(simulation).autopilotActive,"manual override cancels autopilot");
}
void TransferIntegration(const std::vector<BodyDefinition>& bodies,bool blocked,const std::string& destination="moon") {
    auto simulation=MakeSimulation(bodies,blocked);NavigationPilot pilot;Select(simulation,pilot,destination);
    if(destination!="moon") {
        auto state=simulation.State();
        state.orientation=Quatd::FromForwardUp((Body(bodies,destination).centerMeters-state.positionMeters).Normalized(),{0,0,1});
        Check(simulation.RestoreState(state),"face nonlunar destination");
    }
    Check(pilot.StartAutopilot(simulation),"start transfer autopilot");Check(pilot.ConfirmTransfer(simulation),"explicit transfer confirmation");
    const Vec3d start=simulation.State().positionMeters;
    bool departed=false,arrived=false;double maximumSpeed=0;int frames=0;
    for(;frames<30000;++frames) {
        const auto command=pilot.Tick(simulation);
        Check(command.status!=NavigationStatus::RouteBlocked,"departure detour route remains clear");
        Check(!command.requiresSafetyPause,"ordinary guided approach does not revoke at unsafe speed");
        const auto before=simulation.State();
        simulation.SetMode(command.mode);const auto result=simulation.Advance(1.0/30,command.controls);
        Check(result.contact.kind==ContactKind::None,"transfer has no body contact or recovery");
        const auto& after=simulation.State();maximumSpeed=std::max(maximumSpeed,after.velocityMetersPerSecond.Length());
        const double upper=std::max(before.velocityMetersPerSecond.Length(),after.velocityMetersPerSecond.Length())/30+100;
        Check((after.positionMeters-before.positionMeters).Length()<=upper,"position evolves continuously through actual integrator");
        departed=departed||(after.positionMeters-start).Length()>1000000;
        if(command.status==NavigationStatus::Arrived && after.velocityMetersPerSecond.Length()<0.5) {arrived=true;break;}
    }
    std::cout << "transfer target=" << destination << " blocked=" << blocked << " seconds=" << frames/30.0 << " max_mps=" << maximumSpeed << "\n";
    Check(departed && maximumSpeed>1000,"confirmed transfer actually travels above local speed");
    Check(arrived,"confirmed actual-simulation transfer reaches stopped safe standoff");
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"integration arrival leaves no grant");
    Check(simulation.State().recoveryCount==0,"transfer never uses collision reset as travel");
}
}
void MovingCelestialFrameTest(const std::vector<BodyDefinition>& bodies) {
    auto simulation=MakeSimulation(bodies);NavigationPilot pilot;Select(simulation,pilot,"moon");
    Check(pilot.ConfirmTransfer(simulation),"confirm before clock advance");
    auto moved=bodies;
    const auto q=Quatd::FromAxisAngle({0,0,1},0.001);
    for(auto& body:moved){body.centerMeters=q.Rotate(body.centerMeters)+Vec3d{2000,5000,-100};body.bodyFixedToSimulation=q*body.bodyFixedToSimulation;}
    Check(simulation.UpdateCelestialFrames(moved),"real celestial frame transport");
    pilot.FollowCelestialFrames(simulation);
    const auto command=pilot.Tick(simulation);
    Check(command.highSpeedAuthorized&&!command.requiresSafetyPause,"clock motion preserves explicit navigation grant");
    moved[0].radiusMeters+=10;
    Check(simulation.UpdateCelestialFrames(moved),"changed physical fixture");
    pilot.FollowCelestialFrames(simulation);
    Check(!pilot.Tick(simulation).highSpeedAuthorized,"physical changes still revoke grant");
}
int main(int argc,char** argv) {
    try {
        Check(argc==2,"pass dated body fixture path");const auto bodies=LoadBodies(argv[1]);
        for(const auto& body:bodies)if(body.id=="sun")Check(NavigationPilot::StandOffMeters(body)==body.radiusMeters*7.0,"Sun observing standoff is seven radii above photosphere");
        LocalSpeedDoesNotFreeze(bodies);
        GateTests(bodies);LifecycleTests(bodies);BlockedRouteTest(bodies);HoldAndAlignmentTests(bodies);
        MovingCelestialFrameTest(bodies);
        TransferIntegration(bodies,false);TransferIntegration(bodies,true);TransferIntegration(bodies,false,"saturn");
        TransferIntegration(bodies,false,"sun");
        std::cout << "PASS " << checks << " navigation assertions (native core only)\n";return 0;
    } catch(const std::exception& error) {std::cerr << "FAIL after " << checks << ": " << error.what() << "\n";return 1;}
}
