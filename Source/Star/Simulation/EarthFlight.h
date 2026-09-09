#pragma once
#include "FlightSimulation.h"
#include <cmath>

namespace star {
// Fictional flight safety limits relative to the game's mean-radius Earth.
// Rendering still uses a 100 km atmosphere; this is not an atmospheric datum.
constexpr double EarthFlightFloorMeters = 20000.0;
constexpr double EarthScenicAltitudeMeters = 35000.0;
constexpr double EarthNightAltitudeMeters = 160000.0;
constexpr double CairoNightAltitudeMeters = 35000.0;
constexpr double EarthScenicLatitudeDegrees = 11.8;
constexpr double EarthScenicLongitudeDegrees = 92.9;
enum class EarthFlightPreset { Coast, Night, Sunrise, Sunset, Orbit };
constexpr double EarthSunriseAltitudeMeters = 120000.0;
constexpr double EarthSunsetAltitudeMeters = 35000.0;

inline double EarthSunHorizonClearance(const BodyDefinition& earth,const Vec3d& sunCenter,const Vec3d& position) {
    const auto offset=position-earth.centerMeters;
    const double radius=offset.Length();
    const double elevation=std::asin(std::fmax(-1.0,std::fmin(1.0,Vec3d::Dot(offset.Normalized(),(sunCenter-position).Normalized()))));
    return elevation+std::acos(std::fmax(0.0,std::fmin(1.0,earth.radiusMeters/radius)));
}

inline FlightState MakeEarthScenicFlight(const BodyDefinition& earth,
    double latitudeDegrees=EarthScenicLatitudeDegrees,
    double longitudeDegrees=EarthScenicLongitudeDegrees,
    double altitudeMeters=EarthScenicAltitudeMeters) {
    const double latitude=latitudeDegrees*Pi/180.0;
    const double longitude=longitudeDegrees*Pi/180.0;
    const Vec3d radialLocal{std::cos(latitude)*std::cos(longitude),
        std::cos(latitude)*std::sin(longitude),std::sin(latitude)};
    const Vec3d northLocal{-std::sin(latitude)*std::cos(longitude),
        -std::sin(latitude)*std::sin(longitude),std::cos(latitude)};
    const Vec3d eastLocal{-std::sin(longitude),std::cos(longitude),0};
    const double heading=-10.0*Pi/180.0;
    const auto radial=earth.bodyFixedToSimulation.Rotate(radialLocal);
    const auto forward=earth.bodyFixedToSimulation.Rotate(
        northLocal*std::cos(heading)+eastLocal*std::sin(heading));
    FlightState state;
    state.positionMeters=earth.centerMeters+radial*(earth.radiusMeters+altitudeMeters);
    state.orientation=Quatd::FromForwardUp(forward,radial);
    state.targetBodyId=earth.id;
    return state;
}

// Select a real urban region on the current night side without changing time,
// Earth's orientation, or the astronomical Sun position.
inline FlightState MakeEarthNightFlight(const BodyDefinition& earth,const Vec3d& sunCenter) {
    // The registered ISS photograph covers Cairo. Prefer a view of this real
    // high-detail region only when it is actually on the night side.
    auto cairo=MakeEarthScenicFlight(earth,29.9,31.24,CairoNightAltitudeMeters);
    if(Vec3d::Dot((cairo.positionMeters-earth.centerMeters).Normalized(),(sunCenter-cairo.positionMeters).Normalized())<-.25)
        return cairo;
    constexpr double cities[][2]={{35.5,139.8},{31.2,121.5},{19.1,72.9},
        {30.0,31.2},{48.9,2.3},{40.7,-74.0},{34.0,-118.2},{-23.6,-46.6},{-33.9,151.2}};
    FlightState best;
    double darkest=2.0;
    for(const auto& city:cities) {
        auto candidate=MakeEarthScenicFlight(earth,city[0],city[1],EarthNightAltitudeMeters);
        const auto radial=(candidate.positionMeters-earth.centerMeters).Normalized();
        const double light=Vec3d::Dot(radial,(sunCenter-candidate.positionMeters).Normalized());
        if(light<darkest) { darkest=light;best=candidate; }
    }
    return best;
}

// New-flight starting positions only. After restoration, ordinary pilot inputs
// move the ship; no camera animation, changed Sun or hidden state is required.
inline FlightState MakeEarthTwilightFlight(const BodyDefinition& earth,const Vec3d& sunCenter,bool sunset) {
    const double altitude=sunset?EarthSunsetAltitudeMeters:EarthSunriseAltitudeMeters;
    const double dip=std::acos(earth.radiusMeters/(earth.radiusMeters+altitude));
    const double elevation=-dip+(sunset?0.25:-0.20)*Pi/180.0;
    const auto sunward=(sunCenter-earth.centerMeters).Normalized();
    const auto pole=earth.bodyFixedToSimulation.Rotate({0,0,1});
    auto side=Vec3d::Cross(pole,sunward);
    if(side.LengthSquared()<1e-12) side=Vec3d::Cross(Vec3d{1,0,0},sunward);
    if(side.LengthSquared()<1e-12) side=Vec3d::Cross(Vec3d{0,1,0},sunward);
    side=side.Normalized()*(sunset?1.0:-1.0);
    const auto radial=(side*std::cos(elevation)+sunward*std::sin(elevation)).Normalized();
    const auto towardSun=(sunward-radial*Vec3d::Dot(sunward,radial)).Normalized();
    FlightState state;
    state.positionMeters=earth.centerMeters+radial*(earth.radiusMeters+altitude);
    state.orientation=Quatd::FromForwardUp(towardSun*(sunset?-1.0:1.0),radial);
    state.targetBodyId=earth.id;
    return state;
}

inline FlightState MakeEarthFlight(const BodyDefinition& earth,const Vec3d& sunCenter,EarthFlightPreset preset) {
    if(preset==EarthFlightPreset::Night) return MakeEarthNightFlight(earth,sunCenter);
    if(preset==EarthFlightPreset::Sunrise||preset==EarthFlightPreset::Sunset)
        return MakeEarthTwilightFlight(earth,sunCenter,preset==EarthFlightPreset::Sunset);
    if(preset==EarthFlightPreset::Orbit) {
        const auto sunward=(sunCenter-earth.centerMeters).Normalized();
        const auto pole=earth.bodyFixedToSimulation.Rotate({0,0,1});
        auto side=Vec3d::Cross(pole,sunward).Normalized();
        if(side.LengthSquared()<0.5) side=Vec3d::Cross(Vec3d{1,0,0},sunward).Normalized();
        const auto radial=(sunward*0.82+side*0.57).Normalized();
        FlightState state;
        state.positionMeters=earth.centerMeters+radial*(earth.radiusMeters*4.0);
        state.orientation=Quatd::FromForwardUp(Vec3d::Cross(pole,radial).Normalized(),radial);
        state.targetBodyId=earth.id;
        return state;
    }
    return MakeEarthScenicFlight(earth);
}
}
