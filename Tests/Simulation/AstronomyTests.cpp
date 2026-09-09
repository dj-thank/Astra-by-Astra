#include "Astronomy.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace star;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
 try {
    Require(argc==3,"Need ephemeris and independent check file");
    std::ifstream stream(argv[1],std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
    EphemerisTable table;Require(table.Load(bytes.data(),bytes.size()),"Valid ephemeris load");
    std::array<AstronomicalPose,4> poses;
    Require(table.At(table.Start(),poses)&&table.At(table.End(),poses),"Inclusive endpoints");
    Require(!table.At(table.Start()-1,poses)&&!table.At(table.End()+1,poses),"Reject out-of-range time");
    Require(!table.At(std::numeric_limits<double>::quiet_NaN(),poses),"Reject non-finite time");
    EphemerisTable invalid;Require(!invalid.Load(bytes.data(),bytes.size()-1),"Reject truncated source");
    auto corrupt=bytes;corrupt[0]='X';Require(!invalid.Load(corrupt.data(),corrupt.size()),"Reject bad format");
    std::ifstream checks(argv[2]);double utc=0;int index=0,count=0;double maxPosition=0,maxRotation=0;
    while(checks>>utc>>index) {
        Vec3d position;Quatd q;checks>>position.x>>position.y>>position.z>>q.w>>q.x>>q.y>>q.z;
        Require(static_cast<bool>(checks)&&index>=0&&index<4,"Check fixture parse");
        Require(table.At(utc,poses),"Independent sample in range");
        const auto& value=poses[static_cast<std::size_t>(index)];
        maxPosition=std::max(maxPosition,(value.position-position).Length());
        const auto residual=(q.Conjugate()*value.rotation).Normalized();
        maxRotation=std::max(maxRotation,2*std::atan2(Vec3d{residual.x,residual.y,residual.z}.Length(),std::abs(residual.w))*180/Pi);
        ++count;
    }
    Require(count>=40&&maxPosition<0.05&&maxRotation<0.000001,"Independent Horizons/SPICE holdouts");
    BodyDefinition earth;earth.id="earth";earth.radiusMeters=6371008.4;earth.centerMeters={1.4e11,-1e10,3e8};
    BodyDefinition later=earth;later.centerMeters=earth.centerMeters+Vec3d{5e8,-1e7,4e5};
    later.bodyFixedToSimulation=Quatd::FromAxisAngle({0,0,1},Pi*.5);
    FlightState s;s.positionMeters=earth.centerMeters+Vec3d{earth.radiusMeters+35000,0,0};s.velocityMetersPerSecond={0,240,0};s.orientation=Quatd::FromForwardUp({0,1,0},{1,0,0});s.simulationTimeSeconds=10;
    FlightSimulation sim({earth});Require(sim.RestoreState(s),"Start geographic flight");
    const auto before=sim.Telemetry("earth");Require(sim.UpdateCelestialFrames({later}),"Apply rotating translated reference");
    const auto after=sim.Telemetry("earth");
    Require(std::abs(before.latitudeDegrees-after.latitudeDegrees)<1e-8&&std::abs(before.longitudeDegrees-after.longitudeDegrees)<1e-8,"No geographic teleport");
    Require(std::abs(before.referenceAltitudeMeters-after.referenceAltitudeMeters)<0.001,"Altitude preservation");
    Require(std::abs(sim.State().velocityMetersPerSecond.Length()-240)<1e-8&&sim.State().simulationTimeSeconds==10,"Pilot speed and simulation clock unchanged by date selection");
    Require(sim.UpdateCelestialFrames({earth}),"Reverse reference");
    Require((sim.State().positionMeters-s.positionMeters).Length()<0.001,"Round trip under large absolute coordinates");
    auto sign=Slerp({1,0,0,0},{-1,0,0,0},.5);Require(std::abs(sign.w)-1<1e-12&&sign.IsFinite(),"Quaternion sign continuity");
    std::cout<<"PASS "<<count<<" independent ephemeris samples; maximum position error "<<maxPosition<<" m, orientation error "<<maxRotation<<" degrees; frame transport and invalid-input checks passed\n";
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
 return 0;
}
