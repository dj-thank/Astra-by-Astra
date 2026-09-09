#include "EVA/LunarWalkModel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace star;
namespace {
int Checks = 0;
void Require(bool condition,const char* message) { ++Checks; if (!condition) throw std::runtime_error(message); }
#include "RealDEMFixture.h"
BodyDefinition Moon() {
    BodyDefinition moon; moon.id="moon"; moon.landable=true; moon.radiusMeters=1737400;
    moon.centerMeters={1.49e11,-2e10,3e9}; return moon;
}
TerrainSampler Flat() { return [](const BodyDefinition&,const Vec3d& radial,TerrainSample& out) { out={0,radial}; return true; }; }
FlightState Park(const BodyDefinition& moon,const TerrainSampler& sampler,Vec3d radial={0,0,1}) {
    FlightState ship; TerrainSample sample; Require(sampler(moon,radial,sample),"parking ground sample");
    ship.positionMeters=moon.centerMeters+radial*(moon.radiusMeters+sample.heightMeters+4);
    ship.orientation=Quatd::FromForwardUp(Vec3d::Cross({0,1,0},radial).Normalized(),radial);
    ship.mode=FlightMode::Landed; ship.gearDeployed=true; ship.landedBodyId="moon";
    ship.targetBodyId="moon"; ship.recoveryCount=3; ship.simulationTimeSeconds=100;
    return ship;
}
void Basic() {
    const auto moon=Moon(); auto ship=Park(moon,Flat()); const auto saved=SerializeFlightState(ship);
    LunarWalkModel model; Require(model.Initialize(ship,moon,Flat()),"landed Moon exit");
    Require(model.CanBoard(ship),"exit point boards");
    const auto start=model.Position(),look=model.CameraForward();
    model.Advance(0.2,1,1,90,45,true);
    Require((model.Position()-start).Length()==0 && (model.CameraForward()-look).Length()==0,"pause freezes walking and look");
    model.Advance(0.2,0,0,std::numeric_limits<double>::quiet_NaN(),0,false);
    Require((model.CameraForward()-look).Length()==0,"nonfinite look rejected");
    model.Advance(0.1,0,0,90,0,false);
    Require(Vec3d::Dot(model.CameraForward(),Vec3d::Cross(look,model.UpDirection()))>0.999,"positive yaw turns right");
    model.Advance(0.1,0,0,-90,0,false);
    for(int i=0;i<1000;++i) model.Advance(0.25,0,1,0,0,false);
    Require((model.Position()-start).Length()>349,"curved Moon 350m walk");
    Require(!model.CanBoard(ship),"remote boarding denied");
    Require(std::abs((model.Position()-moon.centerMeters).Length()-moon.radiusMeters)<0.0001,"feet on curved datum");
    Require(std::abs((model.Camera()-model.Position()).Length()-1.7)<0.0001,"radial eye height");
    for(int i=0;i<1000;++i) model.Advance(0.25,0,-1,0,0,false);
    Require(model.CanBoard(ship),"walk back to boarding point");
    Require(SerializeFlightState(ship)==saved,"ship flight persistence byte unchanged");
    auto other=ship; other.mode=FlightMode::Maneuver;
    Require(!model.CanBoard(other),"flying ship cannot board");
    LunarWalkModel rejected; Require(!rejected.Initialize(other,moon,Flat()),"cannot EVA in flight");
    other=ship; other.landedBodyId="earth";
    Require(!rejected.Initialize(other,moon,Flat()),"cannot EVA on Earth");
    other=ship; other.positionMeters.x+=1;
    Require(!model.CanBoard(other),"moved ship cannot stale-board");
    for(int i=0;i<100;++i) model.Advance(0.25,-1,0,0,0,false);
    Require(model.MovementBlocked(),"ship hull blocks inward walking");
    const auto local=ToUnrealCentimeters(model.Camera(),ship.positionMeters);
    Require((FromUnrealCentimeters(local,ship.positionMeters)-model.Camera()).Length()<0.0001,"shared origin cm roundtrip");
    LunarWalkModel diagonal,straight; Require(diagonal.Initialize(ship,moon,Flat())&&straight.Initialize(ship,moon,Flat()),"speed test init");
    const auto origin=straight.Position();
    straight.Advance(0.25,0,1,0,0,false); diagonal.Advance(0.25,1,1,0,0,false);
    Require(std::abs((straight.Position()-origin).Length()-(diagonal.Position()-origin).Length())<0.0001,"diagonal normalized speed");
    const auto before=straight.Position(); straight.Advance(100,0,1,0,0,false);
    Require((straight.Position()-before).Length()<0.351,"long frame bounded");
}
void Obstructions() {
    const auto moon=Moon(); const auto ship=Park(moon,Flat());
    for(int mode=0;mode<4;++mode) {
        const auto sampler=[mode](const BodyDefinition& body,const Vec3d& radial,TerrainSample& out) {
            const double x=radial.x*body.radiusMeters;
            if(x>2 && mode==0) return false;
            out={x>2 && mode==1?1.0:0.0,radial};
            if(x>2 && mode==2) out.normalSimulation=Vec3d::Cross(radial,{0,1,0}).Normalized();
            if(x>2 && mode==3) out.heightMeters=-2;
            return true;
        };
        LunarWalkModel model; Require(model.Initialize(ship,moon,sampler),"obstruction init");
        for(int i=0;i<40;++i) model.Advance(0.25,0,1,0,0,false);
        Require(model.MovementBlocked(),"missing/step/steep/drop blocks");
        Require(model.Position().x-moon.centerMeters.x<=2.001,"no boundary tunneling");
    }
}
void Persistence() {
    const auto moon=Moon(); const auto ship=Park(moon,Flat());
    const auto originalFlight=SerializeFlightState(ship);
    LunarWalkModel source,loaded;
    Require(source.Initialize(ship,moon,Flat()) && loaded.Initialize(ship,moon,Flat()),"save models init");
    for(int i=0;i<100;++i) source.Advance(0.25,0,1,0,0,false);
    source.Advance(0.01,0,0,27,40,false);
    const auto saved=source.ExportSaveState();
    Require(loaded.RestoreSaveState(saved),"restore walked save");
    Require((loaded.Position()-source.Position()).Length()<0.0001,"saved position roundtrip");
    Require((loaded.CameraForward()-source.CameraForward()).Length()<0.000001,"saved view roundtrip");
    Require(!loaded.CanBoard(ship),"restored away from ship cannot board");
    const auto unchangedPosition=loaded.Position(),unchangedLook=loaded.CameraForward();
    auto reject=[&](const LunarWalkSaveState& bad) {
        Require(!loaded.RestoreSaveState(bad),"tampered save refused");
        Require((loaded.Position()-unchangedPosition).Length()==0 && (loaded.CameraForward()-unchangedLook).Length()==0,"restore failure transactional");
    };
    auto bad=saved; bad.feetMeters.x=std::numeric_limits<double>::quiet_NaN(); reject(bad);
    bad=saved; bad.headingSimulation.z=std::numeric_limits<double>::infinity(); reject(bad);
    bad=saved; bad.boardingPointMeters.x=std::numeric_limits<double>::quiet_NaN(); reject(bad);
    bad=saved; bad.pitchRadians=std::numeric_limits<double>::quiet_NaN(); reject(bad);
    bad=saved; bad.pitchRadians=Pi; reject(bad);
    bad=saved; bad.boardingPointMeters.x+=1; reject(bad);
    bad=saved; bad.headingSimulation={}; reject(bad);
    bad=saved; bad.headingSimulation=loaded.UpDirection(); reject(bad);
    bad=saved; bad.feetMeters+=loaded.UpDirection(); reject(bad);
    bad=saved; bad.feetMeters=moon.centerMeters+Vec3d{2100,0,moon.radiusMeters}.Normalized()*(moon.radiusMeters+1); reject(bad);
    bad=saved; bad.feetMeters=moon.centerMeters+Vec3d{0,0,moon.radiusMeters}; reject(bad);
    LunarWalkModel uninitialized; Require(!uninitialized.RestoreSaveState(saved),"must initialize parked ship before restore");
    auto moved=ship; moved.positionMeters.x+=10;
    LunarWalkModel elsewhere; Require(elsewhere.Initialize(moved,moon,Flat()),"other ship location init");
    Require(!elsewhere.RestoreSaveState(saved),"save from other landing refused");
    for(int mode=0;mode<2;++mode) {
        auto changed=[mode](const BodyDefinition& body,const Vec3d& radial,TerrainSample& out) {
            if(radial.x*body.radiusMeters>10) {
                if(mode==0) return false;
                out={0,Vec3d::Cross(radial,{0,1,0}).Normalized()}; return true;
            }
            out={0,radial}; return true;
        };
        LunarWalkModel newTerrain; Require(newTerrain.Initialize(ship,moon,changed),"changed terrain init");
        Require(!newTerrain.RestoreSaveState(saved),"missing or steep saved DEM rejected");
    }
    Require(SerializeFlightState(ship)==originalFlight,"persistence does not change parked flight");
    LunarWalkModel distant,reloaded;
    Require(distant.Initialize(ship,moon,Flat())&&reloaded.Initialize(ship,moon,Flat()),"long walk save init");
    for(int i=0;i<9000;++i)distant.Advance(0.25,0,1,0,0,false);
    Require(distant.DistanceToBoardingPoint()>3000,"ordinary walking may exceed two kilometres");
    Require(reloaded.RestoreSaveState(distant.ExportSaveState()),"long ordinary walk remains saveable");
    Require((reloaded.Position()-distant.Position()).Length()<0.0001,"long walk roundtrip feet");
}
void RealDEM(const std::filesystem::path& directory,const std::filesystem::path& metadata) {
    const RealData data(directory,metadata); const auto moon=Moon();
    const auto sampler=[&data](const BodyDefinition& body,const Vec3d& radial,TerrainSample& out) {
        const double lat=std::asin(std::clamp(radial.z,-1.0,1.0))*180/Pi;
        const double lon=std::atan2(radial.y,radial.x)*180/Pi;
        const double dlat=5.0/body.radiusMeters*180/Pi,dLon=dlat/std::max(0.01,std::cos(lat*Pi/180));
        const double east=(data.Height(lat,lon+dLon)-data.Height(lat,lon-dLon))/10;
        const double north=(data.Height(lat+dlat,lon)-data.Height(lat-dlat,lon))/10;
        const double l=lon*Pi/180,p=lat*Pi/180;
        const Vec3d e{-std::sin(l),std::cos(l),0},n{-std::sin(p)*std::cos(l),-std::sin(p)*std::sin(l),std::cos(p)};
        out={data.Height(lat,lon),(radial-e*east-n*north).Normalized()}; return true;
    };
    const auto ship=Park(moon,sampler,Radial(20.1908,30.7717)); const auto saved=SerializeFlightState(ship);
    LunarWalkModel model; Require(model.Initialize(ship,moon,sampler),"Apollo17 measured DEM exit");
    const auto initial=model.Position();
    for(int i=0;i<1000;++i) {
        model.Advance(0.25,0,1,0,0,false);
        TerrainSample sample; const auto radial=(model.Position()-moon.centerMeters).Normalized(); sampler(moon,radial,sample);
        Require(std::abs((model.Position()-moon.centerMeters).Length()-moon.radiusMeters-sample.heightMeters)<0.0001,"real DEM feet follow measured height");
    }
    const double distance=(model.Position()-initial).Length();
    Require(distance>300,"Apollo17 real DEM route exceeds 300m");
    LunarWalkModel restored; Require(restored.Initialize(ship,moon,sampler),"real DEM restore init");
    Require(restored.RestoreSaveState(model.ExportSaveState()),"real DEM save/load");
    Require((restored.Position()-model.Position()).Length()<0.0001,"real DEM saved feet roundtrip");
    for(int i=0;i<1000;++i) model.Advance(0.25,0,-1,0,0,false);
    Require(model.CanBoard(ship),"Apollo17 measured DEM return proximity");
    Require(SerializeFlightState(ship)==saved,"real route preserves parked ship state");
    std::cout<<"Apollo17 measured DEM outward distance="<<distance<<" m; return error="<<(model.Position()-initial).Length()<<" m\n";
}
}
int main(int argc,char** argv) {
    try { Require(argc==3,"expected data directory and metadata"); Basic(); Obstructions(); Persistence(); RealDEM(argv[1],argv[2]);
        std::cout<<"PASS "<<Checks<<" EVA native checks (algorithmic input; no packaged/hardware claim)\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL after "<<Checks<<": "<<e.what()<<"\n"; return 1; }
}
