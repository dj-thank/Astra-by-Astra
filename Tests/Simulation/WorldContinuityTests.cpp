#include "Astronomy.h"
#include "SolarLighting.h"
#include "ObservationGeometry.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    using namespace star;
    Require(argc==3,"ephemeris and UTC required");
    const auto one=ObserveSun({}, {AstronomicalUnitMeters,0,0},695700000);
    const auto two=ObserveSun({}, {2*AstronomicalUnitMeters,0,0},695700000);
    Require(std::abs(one.illuminanceLux/SolarIlluminanceAtOneAU-1)<1e-12,"one-AU illuminance");
    Require(std::abs(two.illuminanceLux/one.illuminanceLux-0.25)<1e-12,"inverse square illumination");
    Require(std::abs(two.diskLuminance/one.diskLuminance-1)<1e-12,"distance-independent disk brightness");
    Require(std::abs(Pi*one.diskLuminance*std::pow(std::sin(one.angularRadiusRadians),2)/one.illuminanceLux-1)<1e-12,"disk/direct-light energy agreement");
    Require(one.diskLuminance>1e9&&one.diskLuminance<3e9,"physical solar disk luminance range");
    Require(ObserveSun({}, {},1).illuminanceLux==0,"finite invalid geometry");
    Require(EarthDetailInRange(450000,false),"original voyage sees observed Earth detail");
    Require(EarthDetailInRange(1250000,true)&&!EarthDetailInRange(1250000,false),"detail boundary hysteresis");
    Require(!EarthDetailInRange(2000000,true),"bounded far-away streaming");
    Require(std::abs(EarthChaseDistanceScale(500001)-EarthChaseDistanceScale(499999))<1e-4,"no old camera jump at 500 km");
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    EphemerisTable table;Require(table.Load(bytes.data(),bytes.size()),"real ephemeris loads");
    double utc=std::stod(argv[2]);std::array<AstronomicalPose,4> poses;
    const double radii[]{695700000,6371000,1737400,58232000};
    auto bodiesAt=[&](double time){
        Require(table.At(time,poses),"time inside real ephemeris");std::vector<BodyDefinition> bodies;
        for(int i=0;i<4;++i){BodyDefinition b;b.id=EphemerisTable::Ids[i];b.radiusMeters=radii[i];b.centerMeters=poses[i].position;b.bodyFixedToSimulation=poses[i].rotation;b.atmosphereHeightMeters=i==1?100000:0;bodies.push_back(b);}return bodies;
    };
    auto bodies=bodiesAt(utc);FlightSimulation sim(bodies);
    Require(sim.RestoreState(InitialVoyage(bodies[1],bodies[0])),"ordinary initial voyage");
    auto local=bodies[1].bodyFixedToSimulation.Conjugate().Rotate(sim.State().positionMeters-bodies[1].centerMeters);
    Require(std::abs(local.Length()-6371000-450000)<0.001,"original start preserved");
    bool day=false,partial=false,night=false,sunset=false;double previous=1;
    FlightInput controls;controls.hasThrottle=true;controls.throttle=0.4;
    for(int step=0;step<8640;++step){
        constexpr double dt=1.0/60;utc+=600*dt;bodies=bodiesAt(utc);
        Require(sim.UpdateCelestialFrames(bodies),"same simulation follows celestial frames");
        sim.Advance(dt,controls);
        const auto now=bodies[1].bodyFixedToSimulation.Conjugate().Rotate(sim.State().positionMeters-bodies[1].centerMeters);
        Require((now-local).Length()<6,"no geographic teleport during ordinary flight");local=now;
        const double visible=SolarDiskVisibleFraction(sim.State().positionMeters,bodies[0].centerMeters,radii[0],bodies[1].centerMeters,radii[1]);
        day|=visible>0.999;partial|=visible>0.1&&visible<0.9;night|=visible<0.001;
        sunset|=previous>0.01&&visible<0.01;previous=visible;
        const auto light=ObserveSun(sim.State().positionMeters,bodies[0].centerMeters,radii[0]);
        Require(std::isfinite(light.diskLuminance)&&light.illuminanceLux>0,"continuous finite sunlight");
    }
    Require(day&&partial&&night&&sunset,"same moving voyage crosses a real sunset");
    Require(sim.State().recoveryCount==0,"no recovery while crossing day/night");
    std::cout<<"PASS shared sunlight, ordinary 450 km detail, smooth camera, continuous dated sunset and flight\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
