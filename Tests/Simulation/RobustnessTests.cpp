#include "Simulation/Astronomy.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* name) {
    ++checks;
    if(!ok){++failures;std::cerr<<"FAIL: "<<name<<'\n';}
}
bool Near(double a,double b){return std::abs(a-b)<1e-10;}
std::vector<std::uint8_t> Ephemeris(double start=1000,double step=1) {
    std::vector<std::uint8_t> bytes(32+2*4*10*sizeof(double));
    std::memcpy(bytes.data(),"STAREPH1",8);
    const std::uint32_t count=2,bodies=4;
    std::memcpy(bytes.data()+8,&count,4);std::memcpy(bytes.data()+12,&bodies,4);
    std::memcpy(bytes.data()+16,&start,8);std::memcpy(bytes.data()+24,&step,8);
    for(std::size_t row=0;row<2;++row)for(std::size_t body=0;body<4;++body){
        const double v[10]={static_cast<double>(body)+2*static_cast<double>(row),0,0,2,0,0,1,0,0,0};
        std::memcpy(bytes.data()+32+(row*4+body)*sizeof(v),v,sizeof(v));
    }
    return bytes;
}
void SetDouble(std::vector<std::uint8_t>& bytes,std::size_t row,std::size_t body,std::size_t field,double value) {
    std::memcpy(bytes.data()+32+((row*4+body)*10+field)*sizeof(double),&value,sizeof(value));
}
void TestEphemeris() {
    star::EphemerisTable table;const auto good=Ephemeris();
    Check(table.Load(good.data(),good.size()),"valid ephemeris loads");
    std::array<star::AstronomicalPose,4> output{};
    Check(table.At(1000.5,output)&&Near(output[0].position.x,1)&&Near(output[0].velocity.x,2),"Hermite midpoint remains correct");
    Check(table.At(1000,output)&&Near(output[0].position.x,0),"first endpoint");
    Check(table.At(1001,output)&&Near(output[0].position.x,2),"last endpoint");
    for(int kind=0;kind<5;++kind){
        auto bad=good;
        if(kind==0)bad[0]='X';
        if(kind==1)bad.pop_back();
        if(kind==2)SetDouble(bad,1,3,0,std::numeric_limits<double>::quiet_NaN());
        if(kind==3)SetDouble(bad,1,3,6,0);
        Check(table.Load(good.data(),good.size()),"restore valid table for each rejection");
        Check(!table.Load(kind==4?nullptr:bad.data(),bad.size()),"invalid replacement rejected");
        Check(table.Contains(1000.5)&&table.At(1000.5,output)&&Near(output[0].position.x,1),"failed replacement preserves previous ephemeris");
    }
    const auto collapsed=Ephemeris(1e308,1);
    Check(!table.Load(collapsed.data(),collapsed.size()),"unrepresentable time grid rejected");
    const auto tiny=Ephemeris(0,1e-310);
    Check(table.Load(tiny.data(),tiny.size()),"finite representable tiny grid loads");
    output[0].position.x=42;output[3].velocity.z=17;
    Check(!table.At(5e-311,output),"overflowing derivative does not succeed");
    Check(output[0].position.x==42&&output[3].velocity.z==17,"failed interpolation leaves output unchanged");
    Check(table.Load(good.data(),good.size()),"restore valid interpolation");
    for(double utc:{999.0,1002.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        output[0].position.x=42;
        Check(!table.At(utc,output)&&output[0].position.x==42,"invalid UTC leaves output unchanged");
    }
}
void TestNormalization() {
    const double huge=std::numeric_limits<double>::max();
    const auto v=star::Vec3d{huge,huge,huge}.Normalized();
    Check(v.IsFinite()&&Near(v.Length(),1)&&Near(v.x,1/std::sqrt(3.0)),"finite huge vector normalizes to unit direction");
    const auto q=star::Quatd{huge,huge,huge,huge}.Normalized();
    Check(q.IsFinite()&&Near(q.w,0.5)&&Near(q.x,0.5)&&Near(q.y,0.5)&&Near(q.z,0.5),"finite huge quaternion normalizes to unit rotation");
    Check(Near(star::Vec3d{}.Normalized({0,1,0}).y,1),"zero vector keeps caller fallback");
    Check(Near(star::Quatd{0,0,0,0}.Normalized().w,1),"zero quaternion keeps identity fallback");
    Check(Near(star::Vec3d{1e-16,0,0}.Normalized({0,1,0}).y,1),"tiny vector keeps caller fallback");
}
void TestFrameUpdates() {
    star::BodyDefinition earth;earth.id="earth";earth.radiusMeters=6371000;
    star::BodyDefinition moon;moon.id="moon";moon.radiusMeters=1737400;moon.centerMeters={384400000,0,0};moon.landable=true;
    star::FlightSimulation sim({earth,moon});
    star::FlightState state;state.positionMeters={7000000,0,0};state.velocityMetersPerSecond={0,20,0};state.targetBodyId="earth";
    Check(sim.RestoreState(state),"initialize frame test");
    for(int kind=0;kind<9;++kind) {
        auto frames=sim.Bodies();
        if(kind==0)frames[0].bodyFixedToSimulation={0,0,0,0};
        if(kind==1)frames[0].centerMeters={1e308,0,0};
        if(kind==2)frames[0].radiusMeters=-1;
        if(kind==3)frames[1].terrainMaxHeightMeters=std::numeric_limits<double>::quiet_NaN();
        if(kind==4)frames[0].atmosphereHeightMeters=-1;
        if(kind==5)frames[0].radiusMeters=1e12;
        if(kind==6)frames[1].terrainMaxSlope=101;
        if(kind==7)frames[0].id="other";
        if(kind==8)frames.pop_back();
        const auto before=star::SerializeFlightState(sim.State());
        Check(!sim.UpdateCelestialFrames(frames),"invalid frame rejected");
        Check(star::SerializeFlightState(sim.State())==before&&sim.Bodies()[0].radiusMeters==earth.radiusMeters&&sim.Bodies()[1].terrainMaxHeightMeters==0,"rejected frame leaves flight and bodies unchanged");
        // Keep tests independent even when running against the old buggy implementation.
        sim=star::FlightSimulation({earth,moon});sim.RestoreState(state);
    }
    auto frames=sim.Bodies();frames[0].centerMeters={100,200,300};
    frames[0].bodyFixedToSimulation=star::Quatd::FromAxisAngle({0,0,1},star::Pi/2);
    Check(sim.UpdateCelestialFrames(frames),"valid dated frame accepted");
    Check((sim.State().positionMeters-star::Vec3d{100,7000200,300}).Length()<1e-7,"valid frame transports position");
    Check((sim.State().velocityMetersPerSecond-star::Vec3d{-20,0,0}).Length()<1e-10,"valid frame transports velocity");
    Check(sim.RestoreState(sim.State()),"transported state remains save-load valid");
}
}
int main() {
    TestEphemeris();TestNormalization();TestFrameUpdates();
    std::cout<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
