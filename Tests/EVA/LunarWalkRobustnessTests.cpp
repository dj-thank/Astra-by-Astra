#include "EVA/LunarWalkModel.h"
#include "Simulation/Astronomy.h"
#include <iostream>
#include <limits>
using namespace star;
namespace {
int checks=0, failures=0;
void Check(bool ok,const char* name) { ++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';} }
bool Same(Vec3d a,Vec3d b) { return (a-b).Length()<1e-5; }
BodyDefinition Moon() { BodyDefinition b;b.id="moon";b.landable=true;b.radiusMeters=1737400;b.centerMeters={1.49e11,-2e10,3e9};return b; }
FlightState Ship(const BodyDefinition& moon) { FlightState s;s.mode=FlightMode::Landed;s.landedBodyId="moon";s.targetBodyId="moon";s.gearDeployed=true;s.positionMeters=moon.centerMeters+Vec3d{0,0,moon.radiusMeters+4};return s; }
TerrainSampler Flat() { return [](const BodyDefinition&,const Vec3d& r,TerrainSample& out){out={0,r};return true;}; }
}
int main() {
    auto moon=Moon();auto ship=Ship(moon);LunarWalkModel model;
    Check(model.Initialize(ship,moon,Flat()),"synthetic flat ground initializes");
    const auto initial=model.ExportSaveState();
    for(int kind=0;kind<6;++kind) {
        auto bad=moon;
        switch(kind) {
        case 0:bad.centerMeters.x=std::numeric_limits<double>::quiet_NaN();break;
        case 1:bad.bodyFixedToSimulation={0,0,0,0};break;
        case 2:bad.radiusMeters=0;break;
        case 3:bad.radiusMeters+=100;break;
        case 4:bad.landable=false;break;
        default:bad.bodyFixedToSimulation.w=std::numeric_limits<double>::infinity();break;
        }
        model.UpdateCelestialFrame(bad);
        Check(Same(model.Position(),initial.feetMeters)&&Same(model.CameraForward(),initial.headingSimulation),"invalid frame leaves walker unchanged");
        Check(model.CanBoard(ship),"invalid frame preserves boarding");
        // Reset for independent reproduction even on the old implementation.
        Check(model.Initialize(ship,moon,Flat()),"restore fixture");
    }
    for(int kind=0;kind<4;++kind) {
        auto badShip=ship;
        if(kind==0)badShip.orientation={0,0,0,0};
        if(kind==1)badShip.velocityMetersPerSecond={10,0,0};
        if(kind==2)badShip.gearDeployed=false;
        if(kind==3)badShip.positionMeters.x=std::numeric_limits<double>::quiet_NaN();
        Check(!model.Initialize(badShip,moon,Flat()),"invalid parked ship rejected");
        Check(model.IsReady()&&Same(model.Position(),initial.feetMeters)&&model.CanBoard(ship),"failed initialization retains valid walk");
        model.Initialize(ship,moon,Flat());
    }
    const auto missing=[](const BodyDefinition&,const Vec3d&,TerrainSample&){return false;};
    Check(!model.Initialize(ship,moon,missing),"missing terrain rejected");
    Check(model.IsReady()&&model.CanBoard(ship),"terrain failure retains existing model");
    model.Initialize(ship,moon,Flat());
    auto moved=moon;moved.centerMeters+=Vec3d{1e7,-2e6,3e5};moved.bodyFixedToSimulation=Quatd::FromAxisAngle({0,0,1},.4);
    model.UpdateCelestialFrame(moved);
    const auto parked=TransportFlightFrame(ship,moon,moved);
    Check(model.CanBoard(parked),"valid epoch transport preserves boarding");
    Check(std::abs((model.Position()-moved.centerMeters).Length()-moon.radiusMeters)<.001,"transport stays on surface");
    model.UpdateCelestialFrame(moon);
    Check(Same(model.Position(),initial.feetMeters),"epoch transport round trip");
    const auto before=model.Position();model.Advance(.2,0,1,0,0,false);
    Check((model.Position()-before).Length()>.1,"walking still works");
    auto saved=model.ExportSaveState();Check(model.RestoreSaveState(saved),"walk save round trip");
    saved.headingSimulation.x=std::numeric_limits<double>::quiet_NaN();
    Check(!model.RestoreSaveState(saved),"invalid saved heading rejected");
    std::cout<<checks<<" EVA robustness checks; failures="<<failures<<" (synthetic terrain, not measured DEM)\n";return failures?1:0;
}
