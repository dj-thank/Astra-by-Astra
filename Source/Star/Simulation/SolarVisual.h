#pragma once
#include <algorithm>
#include <cmath>
namespace star::solarvisual {
inline double Smooth(double a,double b,double x) { const double t=std::clamp((x-a)/(b-a),0.0,1.0);return t*t*(3-2*t); }
struct State { double surface=0,plasma=0,seconds=0; };
inline State Evaluate(double degrees,double utc,bool enabled=true) {
    if(!enabled||!std::isfinite(degrees)||!std::isfinite(utc))return {};
    double t=std::fmod(utc,3600.0);if(t<0)t+=3600;
    return {Smooth(1,5,degrees),Smooth(3,8,degrees),t};
}
}
