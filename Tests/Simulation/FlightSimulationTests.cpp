#include "FlightSimulation.h"
#include "EarthFlight.h"
#include "../../Source/Star/Terrain/RangeCachePolicy.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace star;
namespace {
void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void Near(double a,double b,double tolerance,const char* message) { Check(std::isfinite(a)&&std::abs(a-b)<=tolerance,message); }
void NearVec(Vec3d a,Vec3d b,double tolerance,const char* message) { Near((a-b).Length(),0.0,tolerance,message); }
BodyDefinition Moon() { BodyDefinition b;b.id="Moon";b.radiusMeters=1000.0;b.landable=true;return b; }
bool FlatTerrain(const BodyDefinition&,const Vec3d& radial,TerrainSample& sample) { sample={0,radial};return true; }
void AdvanceFor(FlightSimulation& sim,double seconds,double dt,FlightInput input={}) {
    const int count=static_cast<int>(std::round(seconds/dt));
    for(int i=0;i<count;++i)sim.Advance(dt,input);
}
FlightState LandingState(double altitude=4.01) {
    FlightState s;s.positionMeters={0,0,1000.0+altitude};s.velocityMetersPerSecond={0,0,-1.5};
    s.mode=FlightMode::Landing;s.gearDeployed=true;return s;
}
FlightInput Down() { FlightInput i;i.strafeUp=-0.75;return i; }
AdvanceResult UntilContact(FlightSimulation& sim,FlightInput input) {
    for(int i=0;i<120;++i) {
        const auto result=sim.Advance(1.0/120,input);
        if(result.contact.kind!=ContactKind::None)return result;
    }
    return {};
}

void Coordinates() {
    const Vec3d origin{1.434e12,-2.315e11,7.3e10};
    const Vec3d position=origin+Vec3d{12.125,-9.5,3.75};
    NearVec(ToUnrealCentimeters(position,origin),{1212.5,950,375},0.03,"Saturn floating-origin local centimeters");
    NearVec(FromUnrealCentimeters(ToUnrealCentimeters(position,origin),origin),position,0.0003,"Saturn double round trip");
    const Vec3d rebased=origin+Vec3d{10000,20000,-10000};
    const Vec3d cockpit=position+Vec3d{6,0,1.3};
    const Vec3d oldOffset=ToUnrealCentimeters(cockpit,origin)-ToUnrealCentimeters(position,origin);
    const Vec3d newOffset=ToUnrealCentimeters(cockpit,rebased)-ToUnrealCentimeters(position,rebased);
    NearVec(oldOffset,newOffset,0.00001,"Rebase preserves cockpit relative position");
    NearVec(SimulationDirectionToUnreal(Right({})),{0,1,0},1e-12,"UE right axis reflection");
    NearVec(UnrealDirectionToSimulation(SimulationDirectionToUnreal({0.2,-0.3,0.7})),{0.2,-0.3,0.7},1e-12,"Direction frame round trip");
}
void QuaternionAndControls() {
    const std::vector<Vec3d> forward{{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0.3,-0.7,0.2}};
    for(auto v:forward) {
        const auto q=Quatd::FromForwardUp(v,{0,0,1});
        NearVec(Forward(q),v.Normalized(),1e-12,"FromForwardUp forward");
        Near(Vec3d::Dot(Forward(q),Up(q)),0,1e-12,"Basis orthogonal");
    }
    FlightSimulation yaw({}),pitch({}),roll({}),strafe({});
    FlightInput input;input.yaw=1;AdvanceFor(yaw,1,1.0/60,input);
    Check(Forward(yaw.State().orientation).y<0,"Stick right yaws right in simulation");
    input={};input.pitch=1;AdvanceFor(pitch,1,1.0/60,input);
    Check(Forward(pitch.State().orientation).z>0,"Stick pitch raises nose");
    input={};input.roll=1;AdvanceFor(roll,1,1.0/60,input);
    Check(Up(roll.State().orientation).y<0,"Twist banks right");
    input={};input.strafeRight=1;AdvanceFor(strafe,1,1.0/60,input);
    Check(strafe.State().positionMeters.y<0,"Strafe moves right");
    input.yaw=0.8;input.pitch=0.7;input.roll=-0.9;AdvanceFor(roll,120,1.0/60,input);
    const auto q=roll.State().orientation;
    Near(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z,1,1e-12,"Long flight quaternion normalization");
}
void RenderRateIndependence() {
    FlightSimulation a({}),b({}),c({});
    FlightInput input;input.hasThrottle=true;input.throttle=0.8;input.yaw=0.15;input.pitch=-0.2;input.roll=0.1;input.strafeRight=0.1;
    AdvanceFor(a,30,1.0/30,input);AdvanceFor(b,30,1.0/60,input);AdvanceFor(c,30,1.0/144,input);
    Near(a.State().simulationTimeSeconds,30,1e-10,"Fixed steps simulate 30 seconds");
    NearVec(a.State().positionMeters,b.State().positionMeters,1e-8,"30 and 60 fps same flight path");
    NearVec(a.State().positionMeters,c.State().positionMeters,1e-8,"30 and 144 fps same flight path");
    NearVec(Forward(a.State().orientation),Forward(c.State().orientation),1e-12,"Render delta does not alter attitude");
}
void CruiseAndBraking() {
    FlightSimulation sim({});sim.SetMode(FlightMode::Cruise);sim.SetThrottle(1.0);
    AdvanceFor(sim,10,1.0/60);
    Near(sim.State().velocityMetersPerSecond.Length(),50.0*SpeedOfLightMps,1,"50c cruise cap");
    Check(sim.State().positionMeters.x>1.0e10,"Cruise actually integrates continuous distance");
    sim.SetMode(FlightMode::Maneuver);
    FlightInput brake;brake.brake=true;AdvanceFor(sim,2,1.0/60,brake);
    Near(sim.State().velocityMetersPerSecond.Length(),0,0.01,"Mode switch retains effective high-energy braking");
    Near(sim.State().throttle,0,1e-12,"Brake resets throttle setting");
    sim.SetThrottle(0.4);AdvanceFor(sim,4,1.0/60);
    Near(sim.State().velocityMetersPerSecond.Length(),120,1e-8,"Throttle is a retained speed setting");
}
void LandingAndRelaunch() {
    FlightSimulation sim({Moon()},{},FlatTerrain);Check(sim.RestoreState(LandingState()),"Restore landing approach");
    const auto result=UntilContact(sim,Down());
    Check(result.contact.kind==ContactKind::Landed,"Gear down low-speed level contact lands");
    Check(sim.State().mode==FlightMode::Landed&&sim.State().landedBodyId=="Moon","Landed state owns body id");
    Near(sim.Propulsion().engineOutput,0,0,"Contact immediately removes residual engine output");
    NearVec(sim.State().positionMeters,result.contact.contactCenterMeters,0,"Landing preserves swept contact center");
    const double expectedAltitude=std::sqrt(1000.05*1000.05-5*5-4.8*4.8)+3.95-1000;
    Near(sim.Telemetry("Moon").surfaceAltitudeMeters,expectedAltitude,0.0021,"Feet rest on curved terrain within sweep tolerance");
    FlightInput shove;shove.hasThrottle=true;shove.throttle=1;shove.yaw=1;shove.strafeUp=1;
    const auto resting=sim.State().positionMeters;AdvanceFor(sim,2,1.0/60,shove);
    NearVec(sim.State().positionMeters,resting,1e-12,"Landed controls do not move ship");
    sim.SetGearDeployed(false);Check(sim.State().gearDeployed,"Cannot retract supporting gear while landed");
    FlightInput launch;launch.takeoff=true;sim.Advance(1.0/120,launch);
    Check(sim.State().mode==FlightMode::Landing&&sim.State().landedBodyId.empty(),"Explicit takeoff returns control");
    launch={};launch.strafeUp=0.5;AdvanceFor(sim,2,1.0/60,launch);
    Check(sim.Telemetry("Moon").surfaceAltitudeMeters>6,"Relaunch climbs away without contact loop");
}
void HeldLandingTranslation() {
    auto body=Moon();body.terrainMaxHeightMeters=100;body.terrainMaxSlope=1;
    TerrainSampler plane=[](const BodyDefinition& b,const Vec3d& radial,TerrainSample& sample) {
        if(radial.z<0.95)return false;
        sample={b.radiusMeters/radial.z-b.radiusMeters,{0,0,1}};return true;
    };
    FlightSimulation sim({body},{},plane);
    auto state=LandingState(20);state.velocityMetersPerSecond={};
    Check(sim.RestoreState(state),"Held descent starts safely at 20 m");
    FlightInput input;input.strafeUp=-1;
    bool landed=false;
    for(int frame=0;frame<1200;++frame) {
        const auto result=sim.Advance(1.0/60,input);
        Check(sim.State().recoveryCount==0,"Full held descent never triggers recovery");
        Check(result.contact.kind==ContactKind::None||result.contact.kind==ContactKind::Landed,"Held descent stays within terrain safety limits");
        if(sim.State().mode==FlightMode::Landed) {
            Check(result.contact.downwardSpeedMps<=sim.Config().maxLandingDescentMps,"Actual touchdown descent is safe");
            landed=true;break;
        }
    }
    Check(landed,"Full held descent from 20 m reaches Landed");
    Near(sim.Telemetry("Moon").surfaceAltitudeMeters,4,0.0021,"Held descent rests on planar feet within sweep tolerance");
    FlightConfig config;config.landingTranslationSpeedMps=100;
    FlightSimulation diagonal({},config);diagonal.SetMode(FlightMode::Landing);
    input.strafeRight=1;AdvanceFor(diagonal,1,1.0/60,input);
    Near(diagonal.State().velocityMetersPerSecond.Length(),2,1e-12,"Diagonal landing translation respects lateral ceiling even with excessive config");
    FlightSimulation maneuver({});AdvanceFor(maneuver,1,1.0/60,input);
    Near(maneuver.State().velocityMetersPerSecond.Length(),15*std::sqrt(2.0),1e-12,"Maneuver diagonal translation is preserved");
}
void UnevenTerrainDeparture() {
    auto body=Moon();body.terrainMinHeightMeters=-10;body.terrainMaxHeightMeters=10;
    TerrainSampler terrain=[](const BodyDefinition& b,const Vec3d& radial,TerrainSample& sample) {
        const double x=b.radiusMeters*radial.x;
        const double h=0.4*std::exp(-(x-7)*(x-7)/8);
        const double gradient=-(x-7)*h/4;
        sample={h,(radial-Vec3d{gradient,0,0}).Normalized()};return true;
    };
    FlightSimulation sim({body},{},terrain);auto state=LandingState(20);state.velocityMetersPerSecond={};
    Check(sim.RestoreState(state),"Uneven approach initialized");
    FlightInput down;down.strafeUp=-1;
    for(int i=0;i<1200&&sim.State().mode!=FlightMode::Landed;++i) {
        sim.Advance(1.0/60,down);Check(sim.State().recoveryCount==0,"Uneven approach remains safe");
    }
    Check(sim.State().mode==FlightMode::Landed,"Uneven terrain supports landing");
    const auto landed=sim.State();
    FlightSimulation loaded({body},{},terrain);Check(loaded.RestoreState(landed),"Uneven landed pose restores");
    FlightInput launch;launch.takeoff=true;launch.strafeUp=1;launch.hasThrottle=true;
    sim.Advance(1.0/30,launch);loaded.Advance(1.0/30,launch);
    launch.takeoff=false;AdvanceFor(sim,2,1.0/60,launch);AdvanceFor(loaded,2,1.0/60,launch);
    Check(sim.State().recoveryCount==0&&loaded.State().recoveryCount==0,"Uneven landing and restored landing depart without recovery");
    Check(sim.State().mode==FlightMode::Landing,"Uneven takeoff remains flying");
    Check(sim.Telemetry("Moon").surfaceAltitudeMeters>8,"Uneven departure climbs clear");
}
void UnsafeLandingCases() {
    auto test=[](FlightState state,FlightInput input,const char* reason,TerrainSampler terrain={}) {
        FlightSimulation sim({Moon()},{},terrain?terrain:TerrainSampler{FlatTerrain});Check(sim.RestoreState(state),"Unsafe approach initialized");
        const auto result=UntilContact(sim,input);
        Check(result.contact.kind==ContactKind::Recovered,reason);
        Check(sim.State().recoveryCount==1&&sim.State().throttle==0,"Recovery clears kinetic state");
        Check(sim.Telemetry("Moon").surfaceAltitudeMeters>=2000,"Recovery is outside terrain");
    };
    auto gearUp=LandingState(2.51);gearUp.gearDeployed=false;test(gearUp,Down(),"Gear-up contact recovers");
    auto fast=LandingState();fast.velocityMetersPerSecond={0,0,-20};auto down=Down();down.strafeUp=-1;
    test(fast,down,"Excessive descent recovers");
    auto sideways=LandingState();sideways.velocityMetersPerSecond={10,0,-1.5};test(sideways,Down(),"Excessive lateral speed recovers");
    auto tilted=LandingState();tilted.orientation=Quatd::FromAxisAngle({0,1,0},25.0*Pi/180.0);test(tilted,Down(),"Unsafe attitude or hull contact recovers");
    test(LandingState(),Down(),"Steep terrain recovers",[](const BodyDefinition&,const Vec3d& radial,TerrainSample& out){out={0,(radial+Vec3d{0.5,0,0}).Normalized()};return true;});
}
void ActualHullWidthAndTop() {
    FlightSimulation side({Moon()});
    FlightState state;state.positionMeters={0,1005,0};
    Check(side.RestoreState(state),"Wide hull side case initialized");
    const auto edge=side.Advance(1.0/120,{});
    Check(edge.contact.kind==ContactKind::Recovered,"12.3 m hull width collides where centerline spheres do not");
    FlightSimulation top({Moon()});
    state={};state.positionMeters={0,0,1003.4};state.orientation=Quatd::FromAxisAngle({1,0,0},Pi);
    Check(top.RestoreState(state),"Hull top case initialized");
    Check(top.Advance(1.0/120,{}).contact.kind==ContactKind::Recovered,"Hull top at 3.5 m protects actual 3.46 m mesh");
}
void SafeSlopedLanding() {
    auto body=Moon();body.terrainMinHeightMeters=-200;body.terrainMaxHeightMeters=200;body.terrainMaxSlope=0.2;
    const double slope=std::tan(8.0*Pi/180.0);
    TerrainSampler terrain=[slope](const BodyDefinition& b,const Vec3d& radial,TerrainSample& sample) {
        sample.heightMeters=-slope*b.radiusMeters*radial.x;
        sample.normalSimulation=(radial+(Vec3d{1,0,0}-radial*radial.x)*(slope*b.radiusMeters/(b.radiusMeters+sample.heightMeters))).Normalized();
        return true;
    };
    FlightSimulation sim({body},{},terrain);
    auto state=LandingState(5.3);
    const Vec3d normal=Vec3d{slope,0,1}.Normalized();
    state.orientation=Quatd::FromForwardUp({1,0,-slope},normal);
    state.velocityMetersPerSecond=normal*-1.5;
    Check(sim.RestoreState(state),"Safe sloped approach initialized");
    const auto contact=UntilContact(sim,Down());
    Check(contact.contact.kind==ContactKind::Landed,"An aligned 8 degree slope accepts gentle landing");
    Check(contact.contact.slopeDegrees>7&&contact.contact.slopeDegrees<9,"Slope calculated from actual terrain normal");
    Check(sim.Telemetry("Moon").surfaceAltitudeMeters>4.0,"Sloped normal clearance includes radial cosine correction");
    FlightSimulation loaded({body},{},terrain);
    Check(loaded.RestoreState(sim.State()),"A landed slope state restores consistently");
}
void EarthScenicFlight() {
    BodyDefinition earth;earth.id="earth";earth.radiusMeters=6371008.4;
    earth.centerMeters={1.49e11,-1.2e10,1.4e7};
    earth.atmosphereHeightMeters=EarthFlightFloorMeters;
    earth.bodyFixedToSimulation=Quatd::FromAxisAngle({0.2,0.8,0.4},0.7);
    auto state=MakeEarthScenicFlight(earth);
    const auto radial=(state.positionMeters-earth.centerMeters).Normalized();
    Near((state.positionMeters-earth.centerMeters).Length()-earth.radiusMeters,35000,0.001,"Earth flight starts above the 20 km safety floor");
    Near(Vec3d::Dot(Forward(state.orientation),radial),0,1e-9,"Initial heading is horizontal in the local Earth frame");
    Check(state.throttle==0&&state.velocityMetersPerSecond.Length()==0,"Scenic flight waits for pilot throttle");
    FlightSimulation sim({earth});Check(sim.RestoreState(state),"Earth flight initializes");
    FlightInput input;input.hasThrottle=true;input.throttle=1;
    AdvanceFor(sim,30,1.0/60,input);
    Check((sim.State().positionMeters-state.positionMeters).Length()>5000,"Ordinary simulation actually advances over Earth scenery");
    Check(sim.State().recoveryCount==0,"35 km flight does not hit the former 100 km boundary");
    auto dive=state;dive.velocityMetersPerSecond=-radial*(50*SpeedOfLightMps);dive.mode=FlightMode::Cruise;dive.throttle=1;
    Check(sim.RestoreState(dive),"Earth floor sweep initializes");
    auto result=sim.Advance(1.0/120,{});
    Check(result.contact.kind==ContactKind::Recovered,"High speed cannot tunnel through Earth flight floor");
    Check(sim.State().mode!=FlightMode::Landed,"Earth remains non-landable");
    Check(sim.Telemetry("earth").referenceAltitudeMeters>=EarthFlightFloorMeters+1999,"Earth recovery stays above the flight floor");
    for(const auto& direction:std::vector<Vec3d>{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}) {
        const auto sun=earth.centerMeters+direction*1.49e11;
        const auto night=MakeEarthNightFlight(earth,sun);
        const auto up=(night.positionMeters-earth.centerMeters).Normalized();
        const double nightAltitude=(night.positionMeters-earth.centerMeters).Length()-earth.radiusMeters;
        Check(std::abs(nightAltitude-EarthNightAltitudeMeters)<.001||std::abs(nightAltitude-CairoNightAltitudeMeters)<.001,"Night flight starts at a supported scenic altitude");
        Check(Vec3d::Dot(up,(sun-night.positionMeters).Normalized())<-0.16,"City starts beyond twilight on actual night side");
        Near(Vec3d::Dot(Forward(night.orientation),up),0,1e-9,"Night heading remains tangent under body rotation");
        Check(sim.RestoreState(night),"Night flight restores through ordinary simulation");
        AdvanceFor(sim,30,1.0/60,input);
        Check((sim.State().positionMeters-night.positionMeters).Length()>5000&&sim.State().recoveryCount==0,"Night flight moves safely with pilot throttle");
    }
}
void LocalPlanetDrive() {
    using star::terrain::CompleteRangeBlock;
    constexpr std::uint64_t block=star::terrain::RangeBlockBytes;
    Check(!CompleteRangeBlock(0,0,block),"cache without complete size metadata must refetch");
    Check(!CompleteRangeBlock(block*3,1,1234),"interrupted middle block must refetch");
    Check(CompleteRangeBlock(block*3,1,block),"complete middle block is reusable");
    Check(CompleteRangeBlock(block+123,1,123),"short final range is valid only at exact expected size");
    Check(!CompleteRangeBlock(block+123,1,122)&&!CompleteRangeBlock(block+123,2,1),"truncated and out-of-file ranges rejected");
    BodyDefinition earth;earth.id="earth";earth.radiusMeters=6371000;earth.atmosphereHeightMeters=15000;
    earth.centerMeters={1.49e11,-4e10,2e10};
    FlightSimulation sim({earth});FlightState s;
    s.positionMeters=earth.centerMeters+Vec3d{0,0,earth.radiusMeters+450000};
    s.orientation=Quatd::FromForwardUp({1,0,0},{0,0,1});s.mode=FlightMode::LocalCruise;
    Check(sim.RestoreState(s),"local cruise restore");
    FlightInput input;input.hasThrottle=true;input.throttle=1;
    double peak=0;
    for(int i=0;i<3600;++i){
        const auto before=sim.State();const auto result=sim.Advance(1.0/30,input);
        Check(result.contact.kind==ContactKind::None,"level planet drive has no contact");
        peak=std::max(peak,sim.State().velocityMetersPerSecond.Length());
        Check((sim.State().positionMeters-before.positionMeters).Length()<670,"no local travel jump");
    }
    Check(peak>19000&&peak<=20000.001,"local cruise reaches bounded 20 km/s");
    Near(sim.Telemetry("earth").referenceAltitudeMeters,450000,100,"assisted drive follows curved horizon");
    Check((sim.State().positionMeters-s.positionMeters).Length()>2000000,"drive visibly covers regional distances");
    FlightState loaded;Check(DeserializeFlightState(SerializeFlightState(sim.State()),loaded),"local mode save round trip");
    Check(loaded.mode==FlightMode::LocalCruise,"save retains local speed mode");
    input.paused=true;const auto paused=sim.State();sim.Advance(1,input);
    NearVec(sim.State().positionMeters,paused.positionMeters,0,"pause preserves local flight");
    input.paused=false;input.yaw=.5;AdvanceFor(sim,4,1.0/30,input);
    Check(sim.State().recoveryCount==0,"turning at local speed does not recover");
    input.yaw=0;input.brake=true;AdvanceFor(sim,5,1.0/30,input);
    Check(sim.State().velocityMetersPerSecond.Length()<.01,"local brake stops within five seconds");
    input.brake=false;AdvanceFor(sim,8,1.0/30,input);
    sim.SetMode(FlightMode::Maneuver);AdvanceFor(sim,5,1.0/30,input);
    Check(sim.State().velocityMetersPerSecond.Length()<=300.001,"cruise exit decelerates to maneuver speed");
    sim.SetMode(FlightMode::LocalCruise);AdvanceFor(sim,8,1.0/30,input);
    sim.SetMode(FlightMode::Landing);AdvanceFor(sim,5,1.0/30,input);
    Check(sim.State().velocityMetersPerSecond.Length()<=30.001,"precision entry removes local cruise speed within five seconds");
    s.positionMeters=earth.centerMeters+Vec3d{0,0,earth.radiusMeters+16000};
    Check(sim.RestoreState(s),"near-ground drive fixture");
    Check(sim.LocalCruiseSpeedLimitMps()<111,"near-ground local speed envelope");
    s.orientation=Quatd::FromForwardUp({0,0,-1},{1,0,0});Check(sim.RestoreState(s),"inward fixture");
    const double inward=sim.LocalCruiseSpeedLimitMps();s.orientation=Quatd::FromForwardUp({0,0,1},{1,0,0});
    Check(sim.RestoreState(s),"outward fixture");Near(sim.LocalCruiseSpeedLimitMps(),inward,1e-9,"turning across horizon never unlocks transfer speed");
    FlightState cadenceReference;
    for(int hz:{15,30,60,144}) {
        FlightSimulation drive({earth});
        s.positionMeters=earth.centerMeters+Vec3d{0,0,earth.radiusMeters+450000};
        s.orientation=Quatd::FromForwardUp({1,0,0},{0,0,1});
        Check(drive.RestoreState(s),"cadence start");
        for(int frame=0;frame<hz*10;++frame){
            FlightInput command;command.hasThrottle=true;command.throttle=.8;
            command.yaw=frame>=hz*2&&frame<hz*4?.3:0;
            command.pitch=frame>=hz*5&&frame<hz*6?.15:0;
            command.brake=frame>=hz*8;
            Check(drive.Advance(1.0/hz,command).contact.kind==ContactKind::None,"multi-cadence turning stays clear");
        }
        if(hz==15)cadenceReference=drive.State();
        else NearVec(drive.State().positionMeters,cadenceReference.positionMeters,.01,"local driving independent of render rate");
    }
    s.positionMeters=earth.centerMeters+Vec3d{0,0,earth.radiusMeters+16000};
    s.orientation=Quatd::FromForwardUp({0,0,-1},{1,0,0});
    Check(sim.RestoreState(s),"local dive fixture");input={};input.hasThrottle=true;input.throttle=1;
    AdvanceFor(sim,60,1.0/30,input);
    Check(sim.State().recoveryCount==1&&sim.State().throttleNeutralRequired,"local ground contact recovers once and waits for neutral");
    Check(sim.Telemetry("earth").referenceAltitudeMeters>=15000,"local dive cannot tunnel through Earth");
}
void TwilightIsRealFlight() {
    BodyDefinition earth;earth.id="earth";earth.radiusMeters=6371008.4;
    earth.centerMeters={1.49e11,-2e10,3e8};earth.atmosphereHeightMeters=EarthFlightFloorMeters;
    for(const auto& direction:std::vector<Vec3d>{{1,0,0},{0,1,0},{0,0,1},{0,0,-1},{0.3,-0.7,0.2}}) {
        const auto sun=earth.centerMeters+direction.Normalized()*1.49e11;
        for(bool sunset:{false,true}) {
            const auto start=MakeEarthTwilightFlight(earth,sun,sunset);
            const auto radial=(start.positionMeters-earth.centerMeters).Normalized();
            Check(start.positionMeters.IsFinite()&&start.orientation.IsFinite(),"Twilight pose finite including polar Sun");
            Near(Vec3d::Dot(Forward(start.orientation),radial),0,1e-9,"Twilight pilot heading is tangent");
            const double before=EarthSunHorizonClearance(earth,sun,start.positionMeters);
            Near(before*180/Pi,sunset?0.25:-0.20,0.004,"Sun starts at the actual planetary horizon");
            FlightSimulation sim({earth});Check(sim.RestoreState(start),"Twilight uses ordinary flight state");
            AdvanceFor(sim,2,1.0/60);NearVec(sim.State().positionMeters,start.positionMeters,0.001,"No camera-only approach when throttle is zero");
            FlightInput pilot;pilot.hasThrottle=true;pilot.throttle=0.8;
            AdvanceFor(sim,120,1.0/60,pilot);
            const double after=EarthSunHorizonClearance(earth,sun,sim.State().positionMeters);
            Check(sunset?after<before-0.003:after>before+0.003,"Pilot translation really moves Sun across horizon");
            Check((sim.State().positionMeters-start.positionMeters).Length()>25000&&sim.State().recoveryCount==0,"Twilight route moves safely without teleport or recovery");
            FlightState loaded;Check(DeserializeFlightState(SerializeFlightState(sim.State()),loaded),"Twilight flight saves normally");
            NearVec(loaded.positionMeters,sim.State().positionMeters,0,"Twilight save retains actual position");
        }
    }
}
void SweptAtmospheres() {
    for(const auto& spec:std::vector<std::pair<std::string,double>>{{"Earth",6378137},{"Saturn",58232000}}) {
        BodyDefinition body;body.id=spec.first;body.radiusMeters=spec.second;body.atmosphereHeightMeters=100000;
        body.centerMeters=spec.first=="Saturn"?Vec3d{1.434e12,2.0e11,-3.0e10}:Vec3d{};
        FlightConfig config;config.cruiseBrakeAccelerationMps2=1.0;
        FlightSimulation sim({body},config);
        FlightState state;state.positionMeters=body.centerMeters+Vec3d{-1.0e8,0,0};state.velocityMetersPerSecond={50.0*SpeedOfLightMps,0,0};state.mode=FlightMode::Cruise;state.throttle=1;
        Check(sim.RestoreState(state),"Atmosphere sweep initialized");
        const auto result=sim.Advance(1.0/120,{});
        Check(result.contact.kind==ContactKind::Recovered,"50c segment cannot tunnel through atmosphere");
        Check(result.contact.bodyId==spec.first,"Correct body hit");
        Check(result.contact.contactCenterMeters.x<body.centerMeters.x,"Swept entry collision occurs on near side");
        Check(sim.State().positionMeters.x<body.centerMeters.x,"Recovery remains on incoming side");
        Check(sim.Telemetry(spec.first).referenceAltitudeMeters>=body.atmosphereHeightMeters+2000,"Non-landable atmosphere recovery clearance");
    }
}
void TerrainRidgeAndFallback() {
    auto body=Moon();body.terrainMinHeightMeters=0;body.terrainMaxHeightMeters=40;body.terrainMaxSlope=4;
    TerrainSampler terrain=[](const BodyDefinition& b,const Vec3d& radial,TerrainSample& sample) {
        const double x=radial.x*b.radiusMeters;
        sample.heightMeters=40*std::exp(-x*x/(2*20.0*20.0));
        sample.normalSimulation=radial;return true;
    };
    FlightConfig config;config.cruiseBrakeAccelerationMps2=1;
    FlightSimulation sim({body},config,terrain);
    FlightState state;state.positionMeters={-400,0,1020};state.velocityMetersPerSecond={100000,0,0};state.mode=FlightMode::Cruise;
    Check(sim.RestoreState(state),"Terrain ridge approach initialized");
    const auto result=sim.Advance(1.0/120,{});
    Check(result.contact.kind==ContactKind::Recovered,"Swept terrain catches intervening ridge");
    Check(result.contact.contactCenterMeters.x<0,"Terrain ridge caught before peak");
    FlightSimulation absent({body},config,[](const BodyDefinition&,const Vec3d&,TerrainSample&){return false;});
    auto near=LandingState(44.01);Check(absent.RestoreState(near),"Missing terrain approach initialized");
    const auto fallback=UntilContact(absent,Down());
    Check(fallback.contact.kind==ContactKind::Recovered,"Missing terrain fails safely at maximum-height shell");
    Check(!absent.Telemetry("Moon").terrainAvailable,"Missing terrain never reports observation data available");
    FlightSimulation noSampler({Moon()});Check(noSampler.RestoreState(LandingState()),"Absent sampler approach initialized");
    Check(UntilContact(noSampler,Down()).contact.kind==ContactKind::Recovered,"No sampler cannot authorize terrain landing");
    Check(!noSampler.Telemetry("Moon").terrainAvailable,"No sampler never claims terrain available");
    config.maxTerrainProbesPerStep=1;
    FlightSimulation limited({body},config,terrain);Check(limited.RestoreState(state),"Limited terrain sweep initialized");
    Check(limited.Advance(1.0/120,{}).contact.kind==ContactKind::TerrainSafetyLimit,"Probe exhaustion cannot silently tunnel");
}
void RecoveryRenderRateAndNeutral() {
    FlightConfig config;config.cruiseBrakeAccelerationMps2=1.0;
    FlightSimulation a({Moon()},config),b({Moon()},config);
    FlightState start;start.positionMeters={-2000,0,0};start.velocityMetersPerSecond={10000,0,0};start.mode=FlightMode::Cruise;start.throttle=1;
    Check(a.RestoreState(start)&&b.RestoreState(start),"Collision cadence cases initialized");
    FlightInput held;held.hasThrottle=true;held.throttle=1;
    AdvanceFor(a,1,1.0/30,held);AdvanceFor(b,1,1.0/144,held);
    Check(a.State().recoveryCount==1&&b.State().recoveryCount==1,"Both render cadences detect one collision");
    NearVec(a.State().positionMeters,b.State().positionMeters,1e-9,"Collision recovery path remains render-rate independent");
    Near(a.State().simulationTimeSeconds,b.State().simulationTimeSeconds,1e-12,"Collision does not discard different simulation time by render cadence");
    Check(a.State().throttleNeutralRequired&&a.State().throttle==0,"Held controller throttle cannot restart after collision");
    a.SetThrottle(0.01);Check(!a.State().throttleNeutralRequired,"Neutral acknowledges recovery interlock");
    a.SetThrottle(0.5);Check(a.State().throttle==0.5,"Fresh throttle can accelerate after neutral");
}
void TargetTelemetryAndPersistence() {
    auto body=Moon();body.bodyFixedToSimulation=Quatd::FromAxisAngle({0,0,1},Pi*0.5);
    FlightSimulation sim({body});FlightState state;state.positionMeters={0,1100,0};state.velocityMetersPerSecond={0,-10,0};
    Check(sim.RestoreState(state),"Telemetry state initialized");Check(sim.SetTarget("Moon"),"Set known target");Check(!sim.SetTarget("Pluto"),"Unknown target rejected");
    const auto t=sim.Telemetry("Moon");Near(t.referenceAltitudeMeters,100,1e-9,"Signed altitude over datum");Near(t.longitudeDegrees,0,1e-9,"Body fixed longitude uses explicit rotation");Near(t.latitudeDegrees,0,1e-9,"Body fixed latitude");Near(t.closingSpeedMps,10,1e-9,"Target closing speed");
    Check(sim.ApproachSpeedLimitMps()<100,"Nearby target limits approach speed");
    const auto serialized=SerializeFlightState(sim.State());FlightState loaded;Check(DeserializeFlightState(serialized,loaded),"Versioned flight save round trip");
    NearVec(loaded.positionMeters,sim.State().positionMeters,0,"Serialization keeps exact double position");
    Check(loaded.targetBodyId=="Moon","Target survives save load");
    const auto original=loaded;Check(!DeserializeFlightState(serialized+"garbage",loaded),"Trailing corrupt bytes rejected");NearVec(loaded.positionMeters,original.positionMeters,0,"Failed deserialize does not mutate output");
    Check(!DeserializeFlightState("STAR_FLIGHT 999",loaded),"Unknown save schema rejected");
    state.positionMeters={0,900,0};Check(sim.RestoreState(state),"Embedded position can be restored for recovery");
    Check(sim.Telemetry("Moon").referenceAltitudeMeters<0,"Below-datum altitude remains signed");
    sim.SetTarget("Moon");
    sim.Advance(1.0/120,{});
    Check(sim.State().targetBodyId=="Moon","Recovery preserves the exploration target");
    Check(sim.State().recoveryCount==1,"Embedded saved position receives safe collision recovery");
}
void CinematicAttitudeAndPower() {
    FlightSimulation assisted({Moon()}),manual({Moon()});
    FlightState tilted;tilted.positionMeters={0,0,1100};tilted.orientation=Quatd::FromAxisAngle({1,0,0},0.8);
    Check(assisted.RestoreState(tilted)&&manual.RestoreState(tilted),"Banked flight initialized");
    manual.SetFlightAssistEnabled(false);
    AdvanceFor(assisted,6,1.0/60);AdvanceFor(manual,6,1.0/60);
    Check(Up(assisted.State().orientation).z>0.999,"Released bank gently levels near a body");
    NearVec(Up(manual.State().orientation),Up(tilted.orientation),1e-12,"Disabled assist retains free bank");
    FlightSimulation deepSpace({});Check(deepSpace.RestoreState(tilted),"Deep space bank initialized");
    AdvanceFor(deepSpace,8,1.0/60);
    NearVec(Up(deepSpace.State().orientation),Up(tilted.orientation),1e-12,"Deep space has no artificial world-up leveling");
    // Precision pilot commands must not be mistaken for a released stick.
    Check(assisted.RestoreState(tilted)&&manual.RestoreState(tilted),"Precision bank initialized");
    FlightInput precise;precise.roll=1.0e-8;
    AdvanceFor(assisted,1,1.0/60,precise);AdvanceFor(manual,1,1.0/60,precise);
    NearVec(Up(assisted.State().orientation),Up(manual.State().orientation),1e-12,"Even tiny deliberate roll overrides assist");
    FlightSimulation local({Moon()},{},FlatTerrain);
    FlightState localBank;localBank.positionMeters={0,1050,0};
    localBank.orientation=Quatd::FromForwardUp({1,0,0},{0,1,0})*Quatd::FromAxisAngle({1,0,0},0.6);
    Check(local.RestoreState(localBank),"Local horizon bank initialized");AdvanceFor(local,8,1.0/60);
    Check(Vec3d::Dot(Up(local.State().orientation),Vec3d{0,1,0})>0.999,"Near Moon released bank follows local radial horizon");
    FlightSimulation held({}),heldManual({});heldManual.SetFlightAssistEnabled(false);
    FlightInput roll;roll.roll=1;
    held.Advance(1.0/120,roll);
    const double firstAngle=std::acos(Up(held.State().orientation).z);
    Check(firstAngle>0&&firstAngle<held.Config().rollRateRadiansPerSecond/120*0.1,"Stick ramps angular velocity instead of jumping");
    heldManual.Advance(1.0/120,roll);
    AdvanceFor(held,5,1.0/60,roll);AdvanceFor(heldManual,5,1.0/60,roll);
    NearVec(Up(held.State().orientation),Up(heldManual.State().orientation),1e-12,"Held roll has full authority through inversion");
    FlightInput pause;pause.paused=true;heldManual.Advance(0.1,pause);
    const auto paused=heldManual.State();AdvanceFor(heldManual,1,1.0/60);
    NearVec(Up(heldManual.State().orientation),Up(paused.orientation),1e-12,"Pause clears angular inertia before resume");
    heldManual.Advance(0.1,roll);const auto saved=heldManual.State();
    Check(heldManual.RestoreState(saved),"Restore after angular motion succeeds");
    AdvanceFor(heldManual,1,1.0/60);
    NearVec(Up(heldManual.State().orientation),Up(saved.orientation),1e-12,"Restore clears hidden angular inertia");
    FlightSimulation nearMoon({Moon()},{},FlatTerrain);auto hover=LandingState(100);hover.velocityMetersPerSecond={};
    Check(nearMoon.RestoreState(hover),"Hover initialized");AdvanceFor(nearMoon,1,1.0/60);
    Check(nearMoon.Propulsion().hoverDemand>0.1&&nearMoon.Propulsion().engineOutput>0.1,"Near-body stationkeeping has declared support output");
    FlightInput strafe;strafe.strafeRight=1;nearMoon.Advance(1.0/120,strafe);
    Check(nearMoon.Propulsion().accelerationDemand>0.9&&nearMoon.Propulsion().powerDemand>0.9,"Translation reports applied acceleration at zero throttle");
    FlightSimulation drive({});drive.SetThrottle(1);drive.Advance(1.0/120,{});
    Check(drive.Propulsion().engineOutput>0&&drive.Propulsion().engineOutput<drive.Propulsion().powerDemand,"Engine presentation rises progressively");
    AdvanceFor(drive,1,1.0/60);FlightInput brake;brake.brake=true;drive.Advance(1.0/120,brake);
    Check(drive.Propulsion().braking>0.99,"Braking reports actual opposing propulsion");
    drive.Advance(0.1,pause);Near(drive.Propulsion().engineOutput,0,0,"Pause immediately clears output");
    FlightSimulation cruise({});cruise.SetMode(FlightMode::Cruise);cruise.SetThrottle(1);
    cruise.Advance(1.0/120,{});
    Check(cruise.Propulsion().cruiseCharge>0&&cruise.Propulsion().cruiseCharge<0.02,"Cruise charge begins smoothly");
    Check(cruise.State().velocityMetersPerSecond.Length()<cruise.Config().cruiseAccelerationMps2/120*0.2,"Charge ramp changes actual cruise acceleration");
    FlightSimulation a({}),b({});a.SetMode(FlightMode::Cruise);b.SetMode(FlightMode::Cruise);
    FlightInput mixed;mixed.hasThrottle=true;mixed.throttle=0.8;mixed.roll=0.3;mixed.pitch=0.2;
    AdvanceFor(a,3,1.0/30,mixed);AdvanceFor(b,3,1.0/144,mixed);
    AdvanceFor(a,3,1.0/30);AdvanceFor(b,3,1.0/144);
    NearVec(a.State().positionMeters,b.State().positionMeters,1e-5,"Charge and released attitude preserve render cadence independence");
    Near(a.Propulsion().engineOutput,b.Propulsion().engineOutput,1e-12,"Propulsion response is fixed-step deterministic");
}
void GuidedMotionBounds() {
    FlightSimulation sim({});FlightInput input;input.hasThrottle=true;input.throttle=1;input.smoothGuidance=true;
    Vec3d previousAcceleration;
    const double dt=sim.Config().fixedStepSeconds;
    for(int step=0;step<3600;++step) {
        if(step==1200) input.throttle=0;
        const auto velocity=sim.State().velocityMetersPerSecond;
        sim.Advance(dt,input);
        const auto acceleration=(sim.State().velocityMetersPerSecond-velocity)/dt;
        Check(acceleration.Length()<=45.00001,"guided maneuver acceleration bounded");
        Check((acceleration-previousAcceleration).Length()/dt<=90.00001,"guided maneuver jerk bounded including throttle release");
        previousAcceleration=acceleration;
    }
    Check(sim.State().velocityMetersPerSecond.Length()<0.1,"guided servo settles after stopping");
    FlightSimulation a({}),b({});input.throttle=0.6;
    AdvanceFor(a,10,1.0/30,input);AdvanceFor(b,10,1.0/60,input);
    NearVec(a.State().positionMeters,b.State().positionMeters,1e-9,"guided response independent of render cadence");
    input.smoothGuidance=false;input.brake=true;AdvanceFor(a,2,1.0/60,input);
    Near(a.State().velocityMetersPerSecond.Length(),0,0,"manual emergency brake bypasses guidance smoothing");
}
void GuidedSupportRelease() {
    FlightSimulation sim({Moon()},{},FlatTerrain);Check(sim.RestoreState(LandingState()),"guided launch setup");
    Check(UntilContact(sim,Down()).contact.kind==ContactKind::Landed,"guided launch has actual supporting contact");
    const auto pose=sim.State();FlightInput launch;launch.takeoff=true;launch.strafeUp=1;launch.smoothGuidance=true;
    sim.Advance(sim.Config().fixedStepSeconds,launch);
    NearVec(sim.State().positionMeters,pose.positionMeters,0,"guided support release never displaces ship");
    NearVec(sim.State().velocityMetersPerSecond,{},0,"guided support release never injects velocity");
    auto descending=sim;FlightInput down;down.smoothGuidance=true;down.strafeUp=-1;
    descending.Advance(sim.Config().fixedStepSeconds,down);
    Check(descending.State().mode==FlightMode::Landed,"support release cannot bypass inward contact");
    FlightInput pause;pause.paused=true;sim.Advance(0.2,pause);
    launch.takeoff=false;AdvanceFor(sim,4,1.0/60,launch);
    Check(sim.State().recoveryCount==0&&sim.State().mode==FlightMode::Landing,"guided feet separate without contact chatter");
    Check(sim.Telemetry("Moon").surfaceAltitudeMeters>8,"guided support release really climbs");
}
void InvalidInputsAndPause() {
    const double nan=std::numeric_limits<double>::quiet_NaN();
    FlightConfig invalid;invalid.fixedStepSeconds=nan;invalid.maxCruiseSpeedMps=1e100;invalid.maxTerrainProbesPerStep=0;
    FlightSimulation sim({},invalid);Near(sim.Config().fixedStepSeconds,1.0/120,1e-15,"Invalid fixed step defaults safely");
    FlightInput input;input.yaw=nan;input.pitch=std::numeric_limits<double>::infinity();input.roll=-nan;input.hasThrottle=true;input.throttle=nan;input.strafeUp=nan;
    Check(sim.Advance(nan,input).fixedSteps==0&&sim.Advance(-1,input).fixedSteps==0,"Invalid frame delta is ignored");
    sim.Advance(0.1,input);Check(sim.State().positionMeters.IsFinite()&&sim.State().orientation.IsFinite(),"Invalid axes cannot contaminate state");
    const auto valid=sim.State();auto bad=valid;bad.positionMeters.x=nan;Check(!sim.RestoreState(bad),"Invalid saved position rejected");bad=valid;bad.orientation={0,0,0,0};Check(!sim.RestoreState(bad),"Zero quaternion save rejected");
    input={};input.paused=true;sim.Advance(0.25,input);Near(sim.State().simulationTimeSeconds,valid.simulationTimeSeconds,1e-12,"Pause advances no simulation");
    input={};const auto hitch=sim.Advance(10,input);Near(hitch.discardedSeconds,9.75,1e-12,"Large hitch reports dropped wall time");Check(hitch.fixedSteps==30,"Large hitch has bounded fixed step work");
}
} // namespace

int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"local planet drive, horizon, lifecycle and braking",LocalPlanetDrive},
        {"coordinate precision and floating origin",Coordinates},
        {"quaternions and intuitive controls",QuaternionAndControls},
        {"render rate independent fixed-step path",RenderRateIndependence},
        {"continuous 50c cruise and braking",CruiseAndBraking},
        {"gear landing and controlled relaunch",LandingAndRelaunch},
        {"held full descent and bounded landing translation",HeldLandingTranslation},
        {"uneven terrain departure and reload",UnevenTerrainDeparture},
        {"unsafe landing recovery cases",UnsafeLandingCases},
        {"actual hull width and top envelope",ActualHullWidthAndTop},
        {"safe terrain slope landing",SafeSlopedLanding},
        {"Earth scenic flight and non-landable floor",EarthScenicFlight},
        {"sunrise and sunset through actual pilot motion",TwilightIsRealFlight},
        {"50c swept Earth and Saturn atmospheres",SweptAtmospheres},
        {"terrain ridge, missing data and probe bound",TerrainRidgeAndFallback},
        {"recovery cadence and throttle neutral interlock",RecoveryRenderRateAndNeutral},
        {"target telemetry and flight persistence",TargetTelemetryAndPersistence},
        {"cinematic attitude and applied power feedback",CinematicAttitudeAndPower},
        {"guided acceleration and jerk bounds",GuidedMotionBounds},
        {"guided continuous support release",GuidedSupportRelease},
        {"invalid inputs, pause and hitch bound",InvalidInputsAndPause}};
    int failed=0;
    for(const auto& test:tests) {
        try {test.second();std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& error) {++failed;std::cerr<<"FAIL "<<test.first<<": "<<error.what()<<'\n';}
    }
    std::cout<<(tests.size()-failed)<<"/"<<tests.size()<<" test groups passed\n";
    return failed==0?0:1;
}
