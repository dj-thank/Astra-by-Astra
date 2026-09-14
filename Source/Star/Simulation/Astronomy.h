#pragma once
#include "FlightSimulation.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace star {
// Offline geometric ephemeris in ECLIPJ2000. Positions stay double precision;
// camera-relative UE transforms are applied only by the rendering boundary.
struct AstronomicalPose { Vec3d position, velocity; Quatd rotation; };
inline Quatd Slerp(Quatd a,Quatd b,double u) {
    double dot=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;
    if(dot<0) { b={-b.w,-b.x,-b.y,-b.z};dot=-dot; }
    double x=1-u,y=u;
    if(dot<0.999999) {
        const double angle=std::acos(std::clamp(dot,-1.0,1.0));
        x=std::sin((1-u)*angle)/std::sin(angle);y=std::sin(u*angle)/std::sin(angle);
    }
    return Quatd{a.w*x+b.w*y,a.x*x+b.x*y,a.y*x+b.y*y,a.z*x+b.z*y}.Normalized();
}
class EphemerisTable {
public:
    static constexpr const char* Ids[4]={"sun","earth","moon","saturn"};
    bool Load(const std::uint8_t* bytes,std::size_t size) {
        // Validate a replacement completely before publishing it.
        if(!bytes||size<32||std::memcmp(bytes,"STAREPH1",8)!=0) return false;
        // File format is explicitly little-endian IEEE754, as on Win64.
        const std::uint32_t endian=1;if(*reinterpret_cast<const std::uint8_t*>(&endian)!=1) return false;
        std::uint32_t count=0,bodies=0;double start=0,step=0;
        std::memcpy(&count,bytes+8,4);std::memcpy(&bodies,bytes+12,4);
        std::memcpy(&start,bytes+16,8);std::memcpy(&step,bytes+24,8);
        if(count<2||count>200000||bodies!=4||!std::isfinite(start)||!std::isfinite(step)||step<=0||step>3600||
           size!=32+static_cast<std::size_t>(count)*4*10*sizeof(double)) return false;
        const double end=start+step*static_cast<double>(count-1);
        if(!std::isfinite(end)||end<=start||start+step<=start) return false;
        std::vector<std::array<AstronomicalPose,4>> pending(count);
        for(std::size_t i=0;i<count;++i) for(std::size_t j=0;j<4;++j) {
            double v[10];std::memcpy(v,bytes+32+(i*4+j)*sizeof(v),sizeof(v));
            for(double number:v) if(!std::isfinite(number)) return false;
            const double norm=v[6]*v[6]+v[7]*v[7]+v[8]*v[8]+v[9]*v[9];
            if(std::abs(norm-1.0)>1e-8) return false;
            pending[i][j]={{v[0],v[1],v[2]},{v[3],v[4],v[5]},{v[6],v[7],v[8],v[9]}};
        }
        start_=start;step_=step;rows_=std::move(pending);return true;
    }
    double Start() const { return start_; }
    double End() const { return rows_.empty()?start_:start_+step_*static_cast<double>(rows_.size()-1); }
    bool Contains(double utc) const { return !rows_.empty()&&std::isfinite(utc)&&utc>=Start()&&utc<=End(); }
    bool At(double utc,std::array<AstronomicalPose,4>& out) const {
        if(!Contains(utc)) return false;
        const double raw=(utc-start_)/step_;
        if(!std::isfinite(raw)) return false;
        // Bound the floating index before conversion, including endpoint rounding.
        const double t=std::clamp(raw,0.0,static_cast<double>(rows_.size()-1));
        const std::size_t i=std::min(static_cast<std::size_t>(t),rows_.size()-2);
        const double u=t-static_cast<double>(i),u2=u*u,u3=u2*u;
        std::array<AstronomicalPose,4> pending;
        for(std::size_t j=0;j<4;++j) {
            const auto& a=rows_[i][j];const auto& b=rows_[i+1][j];
            pending[j].position=a.position*(2*u3-3*u2+1)+a.velocity*((u3-2*u2+u)*step_)+
                b.position*(-2*u3+3*u2)+b.velocity*((u3-u2)*step_);
            pending[j].velocity=a.position*((6*u2-6*u)/step_)+a.velocity*(3*u2-4*u+1)+
                b.position*((-6*u2+6*u)/step_)+b.velocity*(3*u2-2*u);
            pending[j].rotation=Slerp(a.rotation,b.rotation,u);
            if(!pending[j].position.IsFinite()||!pending[j].velocity.IsFinite()||!pending[j].rotation.IsFinite()) return false;
        }
        out=pending;return true;
    }
private:
    double start_=0,step_=0;
    std::vector<std::array<AstronomicalPose,4>> rows_;
};

// A change in celestial epoch transports the assisted flight in its nearest
// body's frame. This is an explicit moving reference frame, not inertial
// orbital dynamics; pilot velocity is body-relative in this game.
inline FlightState TransportFlightFrame(const FlightState& state,const BodyDefinition& from,const BodyDefinition& to) {
    const auto q=(to.bodyFixedToSimulation*from.bodyFixedToSimulation.Conjugate()).Normalized();
    FlightState result=state;
    result.positionMeters=to.centerMeters+q.Rotate(state.positionMeters-from.centerMeters);
    result.velocityMetersPerSecond=q.Rotate(state.velocityMetersPerSecond);
    result.orientation=(q*state.orientation).Normalized();return result;
}
inline const BodyDefinition* NearestReferenceBody(const std::vector<BodyDefinition>& bodies,const FlightState& state) {
    const BodyDefinition* best=nullptr;double distance=std::numeric_limits<double>::infinity();
    for(const auto& b:bodies) {
        if(!state.landedBodyId.empty()&&b.id==state.landedBodyId) return &b;
        const double d=(state.positionMeters-b.centerMeters).Length()-b.radiusMeters;
        if(d<distance){distance=d;best=&b;}
    }
    return best;
}
}
