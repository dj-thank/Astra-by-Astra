#pragma once
#include "FlightSimulation.h"
#include <algorithm>
#include <cmath>

namespace star {
constexpr double AstronomicalUnitMeters=149597870700.0;
// One photometric reference for the solar disk, direct light and planetary shading.
constexpr double SolarIlluminanceAtOneAU=127500.0;
struct SolarView {
    Vec3d direction;
    double distanceMeters=0;
    double angularRadiusRadians=0;
    double illuminanceLux=0;
    double diskLuminance=0;
};
inline SolarView ObserveSun(const Vec3d& observer,const Vec3d& sunCenter,double sunRadius) {
    SolarView view;
    const auto offset=sunCenter-observer;
    const double distance=offset.Length();
    if(!offset.IsFinite()||!std::isfinite(distance)||!std::isfinite(sunRadius)||sunRadius<=0||distance<=sunRadius) return view;
    view.direction=offset/distance;view.distanceMeters=distance;
    const double sine=sunRadius/distance;
    view.angularRadiusRadians=std::asin(std::clamp(sine,0.0,1.0));
    const double ratio=AstronomicalUnitMeters/distance;
    view.illuminanceLux=SolarIlluminanceAtOneAU*ratio*ratio;
    // E = integral(L cos(theta) dOmega) = pi L sin(alpha)^2.
    // The disk stays equally bright as its apparent area changes with distance.
    view.diskLuminance=view.illuminanceLux/(Pi*sine*sine);
    return view;
}
inline double BodyHorizonClearance(const BodyDefinition& body,const Vec3d& sunCenter,const Vec3d& observer) {
    const auto radial=observer-body.centerMeters;
    const double distance=radial.Length();
    if(!radial.IsFinite()||distance<=0||body.radiusMeters<=0) return 0;
    const double elevation=std::asin(std::clamp(Vec3d::Dot(radial.Normalized(),(sunCenter-observer).Normalized()),-1.0,1.0));
    const double dip=std::acos(std::clamp(body.radiusMeters/distance,0.0,1.0));
    return elevation+dip;
}
// The same camera spacing applies regardless of the action used to start a voyage.
inline double EarthChaseDistanceScale(double altitudeMeters) {
    const double t=std::clamp((altitudeMeters-200000.0)/800000.0,0.0,1.0);
    return 1.8+1.2*t*t*(3.0-2.0*t);
}
// Preload before the ordinary 450 km start, retaining data across the far boundary.
inline bool EarthDetailInRange(double altitudeMeters,bool alreadyActive) {
    return std::isfinite(altitudeMeters)&&altitudeMeters<(alreadyActive?1500000.0:1200000.0);
}
inline FlightState InitialVoyage(const BodyDefinition& earth,const BodyDefinition& sun) {
    const auto towardSun=(sun.centerMeters-earth.centerMeters).Normalized();
    const auto pole=earth.bodyFixedToSimulation.Rotate({0,0,1});
    const auto east=Vec3d::Cross(pole,towardSun).Normalized();
    const auto radial=(-east+towardSun*0.14+pole*0.20).Normalized();
    FlightState state;
    state.positionMeters=earth.centerMeters+radial*(earth.radiusMeters+450000.0);
    const auto forward=(Vec3d::Cross(radial,towardSun).Normalized()-radial*0.35).Normalized();
    state.orientation=Quatd::FromForwardUp(forward,radial);
    state.targetBodyId="moon";
    return state;
}
}
