#include "FlightSimulation.h"
#include "Astronomy.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace star {
namespace {
double Clamp(double x, double lo, double hi) { return std::max(lo, std::min(x, hi)); }
double Axis(double x) { return std::isfinite(x) ? Clamp(x, -1.0, 1.0) : 0.0; }
double Degrees(double radians) { return radians * (180.0 / Pi); }
double AngleDegrees(Vec3d a, Vec3d b) {
    return Degrees(std::acos(Clamp(Vec3d::Dot(a.Normalized(), b.Normalized()), -1.0, 1.0)));
}
bool ValidMode(FlightMode mode) { return static_cast<unsigned>(mode) <= 4; }
bool ValidStateData(const FlightState& s) {
    const double qn = std::hypot(std::hypot(s.orientation.w, s.orientation.x),
                                 std::hypot(s.orientation.y, s.orientation.z));
    return s.positionMeters.IsFinite() && s.positionMeters.Length() < 1.0e18 &&
        s.velocityMetersPerSecond.IsFinite() && s.velocityMetersPerSecond.Length() <= 50.0 * SpeedOfLightMps * 1.000001 &&
        s.orientation.IsFinite() && qn > 1.0e-12 && std::isfinite(qn) &&
        ValidMode(s.mode) && std::isfinite(s.throttle) && s.throttle >= 0.0 && s.throttle <= 1.0 &&
        (!s.throttleNeutralRequired || s.throttle == 0.0) &&
        std::isfinite(s.simulationTimeSeconds) && s.simulationTimeSeconds >= 0.0 &&
        s.targetBodyId.size() <= 256 && s.landedBodyId.size() <= 256;
}
Vec3d MoveToward(Vec3d current, Vec3d desired, double maxChange) {
    const Vec3d delta = desired - current;
    const double length = delta.Length();
    return length <= maxChange || length < 1.0e-12 ? desired : current + delta * (maxChange / length);
}
// Finite line segment / sphere interval in meters. Projection avoids subtracting
// two enormous squared distances in the quadratic discriminant.
bool SphereInterval(Vec3d start, Vec3d end, Vec3d center, double radius, double& entry, double& exit) {
    const Vec3d delta = end - start;
    const double length = delta.Length();
    const Vec3d offset = start - center;
    if (length < 1.0e-10) {
        entry = exit = 0.0;
        return offset.Length() <= radius;
    }
    const Vec3d direction = delta / length;
    const double projection = Vec3d::Dot(offset, direction);
    const Vec3d nearest = offset - direction * projection;
    const double perpendicular = nearest.Length();
    if (perpendicular > radius) return false;
    const double half = std::sqrt(std::max(0.0, (radius - perpendicular) * (radius + perpendicular)));
    entry = std::max(0.0, (-projection - half) / length);
    exit = std::min(1.0, (-projection + half) / length);
    return entry <= exit && exit >= 0.0 && entry <= 1.0;
}
} // namespace

Vec3d Vec3d::operator+(const Vec3d& b) const { return {x+b.x,y+b.y,z+b.z}; }
Vec3d Vec3d::operator-(const Vec3d& b) const { return {x-b.x,y-b.y,z-b.z}; }
Vec3d Vec3d::operator-() const { return {-x,-y,-z}; }
Vec3d Vec3d::operator*(double s) const { return {x*s,y*s,z*s}; }
Vec3d Vec3d::operator/(double s) const { return {x/s,y/s,z/s}; }
Vec3d& Vec3d::operator+=(const Vec3d& b) { x+=b.x; y+=b.y; z+=b.z; return *this; }
double Vec3d::Length() const { return std::hypot(x,y,z); }
double Vec3d::LengthSquared() const { return x*x+y*y+z*z; }
bool Vec3d::IsFinite() const { return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z); }
Vec3d Vec3d::Normalized(Vec3d fallback) const {
    const double length = Length();
    if(!IsFinite()||length<=1.0e-14) return fallback;
    if(std::isfinite(length)) return *this/length;
    // Finite components can still overflow their norm. Scale first in that case.
    const double scale=std::max({std::abs(x),std::abs(y),std::abs(z)});
    const Vec3d scaled=*this/scale;
    return scaled/scaled.Length();
}
double Vec3d::Dot(const Vec3d& a,const Vec3d& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3d Vec3d::Cross(const Vec3d& a,const Vec3d& b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }

Quatd Quatd::operator*(const Quatd& b) const {
    return {w*b.w-x*b.x-y*b.y-z*b.z, w*b.x+x*b.w+y*b.z-z*b.y,
        w*b.y-x*b.z+y*b.w+z*b.x, w*b.z+x*b.y-y*b.x+z*b.w};
}
Quatd Quatd::Conjugate() const { return {w,-x,-y,-z}; }
Quatd Quatd::Normalized() const {
    const double n = std::hypot(std::hypot(w,x),std::hypot(y,z));
    if(!IsFinite()||n<=1.0e-14) return {};
    if(std::isfinite(n)) return {w/n,x/n,y/n,z/n};
    const double scale=std::max({std::abs(w),std::abs(x),std::abs(y),std::abs(z)});
    const Quatd scaled{w/scale,x/scale,y/scale,z/scale};
    return scaled.Normalized();
}
bool Quatd::IsFinite() const { return std::isfinite(w)&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z); }
Vec3d Quatd::Rotate(const Vec3d& v) const {
    const Quatd q = Normalized();
    const Vec3d qv{q.x,q.y,q.z};
    const Vec3d t = Vec3d::Cross(qv,v)*2.0;
    return v+t*q.w+Vec3d::Cross(qv,t);
}
Quatd Quatd::FromAxisAngle(const Vec3d& axis, double radians) {
    if (!axis.IsFinite() || axis.Length() < 1.0e-14 || !std::isfinite(radians)) return {};
    const Vec3d v = axis.Normalized()*std::sin(radians*0.5);
    return Quatd{std::cos(radians*0.5),v.x,v.y,v.z}.Normalized();
}
Quatd Quatd::FromForwardUp(const Vec3d& forward, const Vec3d& up) {
    const Vec3d f = forward.Normalized();
    Vec3d left = Vec3d::Cross(up.Normalized({0,0,1}),f);
    if (left.Length() < 1.0e-8) left = Vec3d::Cross(std::abs(f.z)<0.9?Vec3d{0,0,1}:Vec3d{0,1,0},f);
    left = left.Normalized();
    const Vec3d u = Vec3d::Cross(f,left).Normalized({0,0,1});
    const double m00=f.x,m01=left.x,m02=u.x,m10=f.y,m11=left.y,m12=u.y,m20=f.z,m21=left.z,m22=u.z;
    Quatd q;
    const double trace=m00+m11+m22;
    if (trace>0) {
        const double s=2.0*std::sqrt(trace+1.0); q={s*0.25,(m21-m12)/s,(m02-m20)/s,(m10-m01)/s};
    } else if (m00>m11 && m00>m22) {
        const double s=2.0*std::sqrt(1.0+m00-m11-m22); q={(m21-m12)/s,s*0.25,(m01+m10)/s,(m02+m20)/s};
    } else if (m11>m22) {
        const double s=2.0*std::sqrt(1.0+m11-m00-m22); q={(m02-m20)/s,(m01+m10)/s,s*0.25,(m12+m21)/s};
    } else {
        const double s=2.0*std::sqrt(1.0+m22-m00-m11); q={(m10-m01)/s,(m02+m20)/s,(m12+m21)/s,s*0.25};
    }
    return q.Normalized();
}

Vec3d Forward(const Quatd& q) { return q.Rotate({1,0,0}); }
Vec3d Right(const Quatd& q) { return q.Rotate({0,-1,0}); }
Vec3d Up(const Quatd& q) { return q.Rotate({0,0,1}); }
Vec3d SimulationDirectionToUnreal(const Vec3d& v) { return {v.x,-v.y,v.z}; }
Vec3d UnrealDirectionToSimulation(const Vec3d& v) { return {v.x,-v.y,v.z}; }
Vec3d ToUnrealCentimeters(const Vec3d& p,const Vec3d& origin) { return SimulationDirectionToUnreal(p-origin)*100.0; }
Vec3d FromUnrealCentimeters(const Vec3d& p,const Vec3d& origin) { return origin+UnrealDirectionToSimulation(p)*0.01; }

struct FlightSimulation::Surface {
    double height=0.0;
    Vec3d normal;
    bool available=false;
};
struct FlightSimulation::SweepHit {
    const BodyDefinition* body=nullptr;
    double time=2.0;
    Vec3d shipCenter;
    Vec3d radial;
    Surface surface;
    bool safetyLimit=false;
    bool gearContact=false;
};

FlightSimulation::FlightSimulation(std::vector<BodyDefinition> bodies, FlightConfig config, TerrainSampler terrain)
    : config_(config),terrain_(std::move(terrain)) {
    const FlightConfig defaults;
    auto positive=[](double& v,double fallback,double upper=1.0e12){ if(!std::isfinite(v)||v<=0.0||v>upper)v=fallback; };
    positive(config_.fixedStepSeconds,defaults.fixedStepSeconds,0.1);
    config_.fixedStepSeconds=std::max(1.0e-4,config_.fixedStepSeconds);
    positive(config_.maxFrameSeconds,defaults.maxFrameSeconds,1.0);
    config_.maxFrameSeconds=std::max(config_.maxFrameSeconds,config_.fixedStepSeconds);
    positive(config_.maxManeuverSpeedMps,defaults.maxManeuverSpeedMps);
    positive(config_.maxLocalCruiseSpeedMps,defaults.maxLocalCruiseSpeedMps,100000.0);
    positive(config_.localCruiseAccelerationMps2,defaults.localCruiseAccelerationMps2,100000.0);
    positive(config_.localCruiseBrakeMps2,defaults.localCruiseBrakeMps2,100000.0);
    positive(config_.maxLandingSpeedMps,defaults.maxLandingSpeedMps);
    positive(config_.maxCruiseSpeedMps,defaults.maxCruiseSpeedMps,50.0*SpeedOfLightMps);
    positive(config_.maneuverAccelerationMps2,defaults.maneuverAccelerationMps2);
    positive(config_.cruiseAccelerationMps2,defaults.cruiseAccelerationMps2);
    positive(config_.brakeAccelerationMps2,defaults.brakeAccelerationMps2);
    positive(config_.cruiseBrakeAccelerationMps2,defaults.cruiseBrakeAccelerationMps2);
    positive(config_.yawRateRadiansPerSecond,defaults.yawRateRadiansPerSecond,10.0);
    positive(config_.pitchRateRadiansPerSecond,defaults.pitchRateRadiansPerSecond,10.0);
    positive(config_.rollRateRadiansPerSecond,defaults.rollRateRadiansPerSecond,10.0);
    positive(config_.angularResponseSeconds,defaults.angularResponseSeconds,5.0);
    positive(config_.bankLevelRatePerSecond,defaults.bankLevelRatePerSecond,5.0);
    positive(config_.maxBankAssistRadiansPerSecond,defaults.maxBankAssistRadiansPerSecond,2.0);
    positive(config_.engineRiseSeconds,defaults.engineRiseSeconds,5.0);
    positive(config_.engineFallSeconds,defaults.engineFallSeconds,5.0);
    positive(config_.cruiseChargeSeconds,defaults.cruiseChargeSeconds,5.0);
    positive(config_.strafeSpeedMps,defaults.strafeSpeedMps);
    positive(config_.collisionRadiusMeters,defaults.collisionRadiusMeters,1000.0);
    positive(config_.hullHalfLengthMeters,defaults.hullHalfLengthMeters,1000.0);
    config_.hullHalfLengthMeters=std::max(config_.hullHalfLengthMeters,config_.collisionRadiusMeters);
    positive(config_.hullHalfWidthMeters,defaults.hullHalfWidthMeters,1000.0);
    positive(config_.hullTopMeters,defaults.hullTopMeters,1000.0);
    positive(config_.lateralHullProbeRadiusMeters,defaults.lateralHullProbeRadiusMeters,1000.0);
    config_.lateralHullProbeRadiusMeters=std::min(config_.lateralHullProbeRadiusMeters,config_.hullHalfWidthMeters);
    if(!std::isfinite(config_.lateralHullBottomMeters)||config_.lateralHullBottomMeters>=config_.hullTopMeters||config_.lateralHullBottomMeters<-1000.0)
        config_.lateralHullBottomMeters=defaults.lateralHullBottomMeters;
    positive(config_.landingClearanceMeters,defaults.landingClearanceMeters,1000.0);
    positive(config_.landingFrontFootMeters,defaults.landingFrontFootMeters,1000.0);
    positive(config_.landingRearFootMeters,defaults.landingRearFootMeters,1000.0);
    positive(config_.landingHalfTrackMeters,defaults.landingHalfTrackMeters,1000.0);
    positive(config_.maxLandingDescentMps,defaults.maxLandingDescentMps);
    positive(config_.maxLandingLateralMps,defaults.maxLandingLateralMps);
    positive(config_.landingTranslationSpeedMps,defaults.landingTranslationSpeedMps);
    config_.landingTranslationSpeedMps=std::min({config_.landingTranslationSpeedMps,
        config_.maxLandingDescentMps,config_.maxLandingLateralMps});
    positive(config_.maxLandingTiltDegrees,defaults.maxLandingTiltDegrees,90.0);
    positive(config_.maxLandingSlopeDegrees,defaults.maxLandingSlopeDegrees,90.0);
    positive(config_.recoveryClearanceMeters,defaults.recoveryClearanceMeters);
    positive(config_.takeoffSpeedMps,defaults.takeoffSpeedMps);
    positive(config_.approachRatePerSecond,defaults.approachRatePerSecond,10.0);
    positive(config_.terrainProbeSpacingMeters,defaults.terrainProbeSpacingMeters,10000.0);
    config_.maxTerrainProbesPerStep=std::max(1u,std::min(16384u,config_.maxTerrainProbesPerStep));
    for (auto& b:bodies) {
        if(b.id.empty()||b.id.size()>256||!b.centerMeters.IsFinite()||b.centerMeters.Length()>=1.0e18||
            !std::isfinite(b.radiusMeters)||b.radiusMeters<1.0||b.radiusMeters>1.0e11||FindBody(b.id)) continue;
        if(!std::isfinite(b.atmosphereHeightMeters)||b.atmosphereHeightMeters<0.0||b.atmosphereHeightMeters>b.radiusMeters)b.atmosphereHeightMeters=0.0;
        if(!std::isfinite(b.terrainMinHeightMeters)||b.terrainMinHeightMeters<-b.radiusMeters*0.5||b.terrainMinHeightMeters>b.radiusMeters*0.5)b.terrainMinHeightMeters=0.0;
        if(!std::isfinite(b.terrainMaxHeightMeters)||b.terrainMaxHeightMeters<b.terrainMinHeightMeters||b.terrainMaxHeightMeters>b.radiusMeters*0.5)b.terrainMaxHeightMeters=b.terrainMinHeightMeters;
        if(!std::isfinite(b.terrainMaxSlope)||b.terrainMaxSlope<0.0||b.terrainMaxSlope>100.0)b.terrainMaxSlope=4.0;
        b.bodyFixedToSimulation=b.bodyFixedToSimulation.Normalized();
        bodies_.push_back(std::move(b));
    }
}

const BodyDefinition* FlightSimulation::FindBody(const std::string& id) const {
    for(const auto& b:bodies_)if(b.id==id)return &b;
    return nullptr;
}
FlightSimulation::Surface FlightSimulation::SampleSurface(const BodyDefinition& b,const Vec3d& direction) const {
    const Vec3d radial=direction.Normalized();
    Surface result{b.landable?b.terrainMaxHeightMeters:b.atmosphereHeightMeters,radial,false};
    if(!b.landable)return result;
    if(!terrain_)return result;
    TerrainSample sample;
    if(terrain_(b,radial,sample)&&std::isfinite(sample.heightMeters)&&
       sample.heightMeters>=b.terrainMinHeightMeters&&sample.heightMeters<=b.terrainMaxHeightMeters&&
       sample.normalSimulation.IsFinite()&&sample.normalSimulation.Length()>1.0e-8&&
       Vec3d::Dot(sample.normalSimulation.Normalized(),radial)>0.0) {
        result={sample.heightMeters,sample.normalSimulation.Normalized(),true};
    }
    return result;
}
bool FlightSimulation::UpdateCelestialFrames(const std::vector<BodyDefinition>& bodies) {
    if(bodies.size()!=bodies_.size()) return false;
    auto pending=bodies;
    for(std::size_t i=0;i<pending.size();++i) {
        auto& b=pending[i];
        const auto& r=b.bodyFixedToSimulation;
        const double norm=std::hypot(std::hypot(r.w,r.x),std::hypot(r.y,r.z));
        if(b.id!=bodies_[i].id||!b.centerMeters.IsFinite()||b.centerMeters.Length()>=1.0e18||
           !std::isfinite(b.radiusMeters)||b.radiusMeters<1.0||b.radiusMeters>1.0e11||
           !std::isfinite(b.atmosphereHeightMeters)||b.atmosphereHeightMeters<0||b.atmosphereHeightMeters>b.radiusMeters||
           !std::isfinite(b.terrainMinHeightMeters)||b.terrainMinHeightMeters<-b.radiusMeters*0.5||b.terrainMinHeightMeters>b.radiusMeters*0.5||
           !std::isfinite(b.terrainMaxHeightMeters)||b.terrainMaxHeightMeters<b.terrainMinHeightMeters||b.terrainMaxHeightMeters>b.radiusMeters*0.5||
           !std::isfinite(b.terrainMaxSlope)||b.terrainMaxSlope<0||b.terrainMaxSlope>100||
           !r.IsFinite()||!std::isfinite(norm)||norm<=1.0e-12) return false;
        b.bodyFixedToSimulation=b.bodyFixedToSimulation.Normalized();
    }
    const auto* from=NearestReferenceBody(bodies_,state_);if(!from)return false;
    const auto index=static_cast<std::size_t>(from-bodies_.data());const auto& to=pending[index];
    const auto q=(to.bodyFixedToSimulation*from->bodyFixedToSimulation.Conjugate()).Normalized();
    const auto state=TransportFlightFrame(state_,*from,to);
    const auto acceleration=q.Rotate(guidanceAcceleration_);
    const auto release=to.centerMeters+q.Rotate(guidanceReleasePosition_-from->centerMeters);
    if(!ValidStateData(state)||!acceleration.IsFinite()||!release.IsFinite()) return false;
    if(state.mode==FlightMode::Landed) {
        const auto offset=state.positionMeters-to.centerMeters;
        const auto surface=SampleSurface(to,offset.Normalized());
        if(!to.landable||std::abs(offset.Length()-to.radiusMeters-surface.height-config_.landingClearanceMeters)>1.0) return false;
    }
    // No partially updated flight, guidance, or body data on rejection.
    state_=state;guidanceAcceleration_=acceleration;guidanceReleasePosition_=release;
    bodies_=std::move(pending);return true;
}
bool FlightSimulation::RestoreState(const FlightState& s) {
    if(!ValidStateData(s)||(!s.targetBodyId.empty()&&!FindBody(s.targetBodyId)))return false;
    if(s.mode==FlightMode::Landed) {
        const auto* body=FindBody(s.landedBodyId);
        if(!body||!body->landable||!s.gearDeployed||s.velocityMetersPerSecond.Length()>0.001)return false;
        const Vec3d radial=(s.positionMeters-body->centerMeters).Normalized();
        const auto surface=SampleSurface(*body,radial);
        const double clearance=(s.positionMeters-body->centerMeters).Length()-body->radiusMeters-surface.height;
        if(std::abs(clearance-config_.landingClearanceMeters)>1.0)return false;
    } else if(!s.landedBodyId.empty())return false;
    state_=s; state_.orientation=state_.orientation.Normalized(); accumulatorSeconds_=0.0; ResetDynamics();
    return true;
}
void FlightSimulation::ResetDynamics(bool clearSupport) {
    guidanceAcceleration_={};if(clearSupport)guidanceReleaseBody_.clear();
    angularVelocity_={};propulsion_={};
}
void FlightSimulation::SetFlightAssistEnabled(bool enabled) {
    if(flightAssistEnabled_==enabled)return;
    flightAssistEnabled_=enabled;angularVelocity_={};
}
Vec3d FlightSimulation::ReferenceUp(double& proximity) const {
    Vec3d up{0,0,1};proximity=0.0;
    double nearest=std::numeric_limits<double>::max();
    for(const auto& body:bodies_) {
        const Vec3d offset=state_.positionMeters-body.centerMeters;
        const double altitude=offset.Length()-body.radiusMeters;
        // Smoothly fade assist strength into the local radial horizon. Distant bodies do not compete
        // for an arbitrary up direction during interplanetary cruise.
        const double range=std::max(10000.0,body.radiusMeters*0.25);
        const double weight=Clamp(1.0-std::max(0.0,altitude)/range,0.0,1.0);
        if(weight>0.0&&altitude<nearest) {
            nearest=altitude;proximity=weight;
            up=offset.Normalized();
        }
    }
    return up;
}
void FlightSimulation::SetMode(FlightMode mode) {
    if(!ValidMode(mode)||mode==FlightMode::Landed||state_.mode==FlightMode::Landed)return;
    state_.mode=mode;
}
void FlightSimulation::SetGearDeployed(bool deployed) {
    if(state_.mode!=FlightMode::Landed)state_.gearDeployed=deployed;
}
void FlightSimulation::SetThrottle(double throttle) {
    if(!std::isfinite(throttle))return;
    throttle=Clamp(throttle,0.0,1.0);
    if(state_.throttleNeutralRequired) {
        if(throttle>0.02)return;
        state_.throttleNeutralRequired=false;state_.throttle=0.0;return;
    }
    state_.throttle=throttle;
}
bool FlightSimulation::SetTarget(const std::string& id) {
    if(!id.empty()&&!FindBody(id))return false;
    state_.targetBodyId=id; return true;
}
double FlightSimulation::ContactRadius() const {
    return state_.gearDeployed?std::max(config_.landingClearanceMeters,config_.collisionRadiusMeters):config_.collisionRadiusMeters;
}

BodyTelemetry FlightSimulation::Telemetry(const std::string& id) const {
    BodyTelemetry result;
    const auto* body=FindBody(id); if(!body)return result;
    const Vec3d offset=state_.positionMeters-body->centerMeters;
    const Vec3d radial=offset.Normalized();
    const auto surface=SampleSurface(*body,radial);
    const Vec3d local=body->bodyFixedToSimulation.Conjugate().Rotate(radial);
    result.bodyId=id;result.distanceMeters=offset.Length();
    result.referenceAltitudeMeters=result.distanceMeters-body->radiusMeters;
    // Atmospheres are collision shells, not terrain heights.
    result.surfaceAltitudeMeters=result.referenceAltitudeMeters-(body->landable?surface.height:0.0);
    result.latitudeDegrees=Degrees(std::asin(Clamp(local.z,-1.0,1.0)));
    result.longitudeDegrees=Degrees(std::atan2(local.y,local.x));
    result.closingSpeedMps=-Vec3d::Dot(state_.velocityMetersPerSecond,radial);
    result.outwardNormal=surface.normal; result.terrainAvailable=surface.available;
    return result;
}

double FlightSimulation::LocalCruiseSpeedLimitMps() const {
    double limit=config_.maxLocalCruiseSpeedMps;
    for(const auto& body:bodies_) {
        const double shell=body.radiusMeters+(body.landable?body.terrainMaxHeightMeters:body.atmosphereHeightMeters)+ContactRadius();
        const double clearance=std::max(0.0,(state_.positionMeters-body.centerMeters).Length()-shell);
        // All directions share this envelope: crossing the horizon cannot
        // switch between an approach cap and an interplanetary speed target.
        limit=std::min(limit,config_.maxLandingSpeedMps+clearance*0.08);
    }
    return std::min(limit,config_.maxCruiseSpeedMps);
}
double FlightSimulation::ApproachSpeedLimitMps() const {
    double limit=config_.maxCruiseSpeedMps;
    const Vec3d motion=state_.velocityMetersPerSecond.Length()>1.0?state_.velocityMetersPerSecond.Normalized():Forward(state_.orientation);
    for(const auto& body:bodies_) {
        const Vec3d offset=state_.positionMeters-body.centerMeters;
        const double distance=offset.Length();
        const double shell=body.radiusMeters+(body.landable?body.terrainMaxHeightMeters:body.atmosphereHeightMeters)+ContactRadius();
        const double clearance=std::max(0.0,distance-shell);
        // Target always has an approach limit; nearby non-target bodies also
        // limit any inward path. Outward escape is unrestricted.
        if(Vec3d::Dot(motion,offset.Normalized())>=0.0 || (body.id!=state_.targetBodyId&&clearance>1.0e9))continue;
        const double kinematic=std::sqrt(2.0*config_.cruiseBrakeAccelerationMps2*clearance);
        const double cinematic=config_.maxLandingSpeedMps+clearance*config_.approachRatePerSecond;
        limit=std::min(limit,std::max(config_.maxLandingSpeedMps,std::min(kinematic,cinematic)));
    }
    return limit;
}

FlightSimulation::SweepHit FlightSimulation::Sweep(const Vec3d& start,const Vec3d& end,const Quatd& startOrientation) const {
    SweepHit nearest;
    const Vec3d shipDelta=end-start;
    const double halfSpan=config_.hullHalfLengthMeters-config_.collisionRadiusMeters;
    const Quatd& q=state_.orientation;
    const double quaternionDot=startOrientation.w*q.w+startOrientation.x*q.x+startOrientation.y*q.y+startOrientation.z*q.z;
    const double turn=2.0*std::acos(Clamp(std::abs(quaternionDot),0.0,1.0));
    constexpr int hullShapes=33; // Five centerline + 7 columns * 2 sides * 2 rows.
    for(int shape=0;shape<(state_.gearDeployed?hullShapes+3:hullShapes);++shape) {
        const bool gear=shape>=hullShapes;
        Vec3d offset{halfSpan*(static_cast<double>(shape)-2.0)*0.5,0,config_.hullTopMeters-config_.collisionRadiusMeters};
        double sphereRadius=config_.collisionRadiusMeters;
        constexpr double footRadius=0.05;
        if(gear) {
            const int foot=shape-hullShapes;
            offset={foot==0?config_.landingFrontFootMeters:-config_.landingRearFootMeters,
                foot==0?0.0:(foot==1?config_.landingHalfTrackMeters:-config_.landingHalfTrackMeters),
                -config_.landingClearanceMeters+footRadius};
            sphereRadius=footRadius;
        } else if(shape>=5) {
            const int index=shape-5;
            const int column=index%7,side=(index/7)%2,row=index/14;
            sphereRadius=config_.lateralHullProbeRadiusMeters;
            offset={-halfSpan+2.0*halfSpan*static_cast<double>(column)/6.0,
                (side==0?1.0:-1.0)*(config_.hullHalfWidthMeters-sphereRadius),
                row==0?config_.hullTopMeters-sphereRadius:config_.lateralHullBottomMeters+sphereRadius};
        }
        const double rotationMargin=offset.Length()*(1.0-std::cos(turn*0.5));
        const Vec3d shapeStart=start+startOrientation.Rotate(offset);
        const Vec3d shapeEnd=end+state_.orientation.Rotate(offset);
        const Vec3d delta=shapeEnd-shapeStart;
        const double length=delta.Length();
        const double radius=sphereRadius+rotationMargin;
        for(const auto& body:bodies_) {
            const double top=body.landable?body.terrainMaxHeightMeters:body.atmosphereHeightMeters;
            double begin=0.0,finish=0.0;
            if(!SphereInterval(shapeStart,shapeEnd,body.centerMeters,body.radiusMeters+top+radius,begin,finish)||begin>nearest.time)continue;
            double t=begin;
            bool hit=false,safety=false;
            Surface surface;
            Vec3d radial;
            for(std::uint32_t probe=0;probe<config_.maxTerrainProbesPerStep;++probe) {
                const Vec3d location=shapeStart+delta*t;
                const Vec3d relative=location-body.centerMeters;
                radial=relative.Normalized(); surface=SampleSurface(body,radial);
                const double clearance=relative.Length()-body.radiusMeters-surface.height-radius;
                // A smooth launch releases only the known supporting feet, only
                // during a sub-centimeter outward step with no attitude change.
                // Check both terrain samples; hulls and all other contacts remain swept.
                if(gear&&surface.available&&body.id==guidanceReleaseBody_&&t==0.0&&turn<1e-7&&length<0.01&&
                    (start-guidanceReleasePosition_).Length()<0.10&&clearance>=-0.002&&
                    Vec3d::Dot(delta,surface.normal)>0) {
                    const auto endRelative=shapeEnd-body.centerMeters;
                    const auto endSurface=SampleSurface(body,endRelative.Normalized());
                    const double endClearance=endRelative.Length()-body.radiusMeters-endSurface.height-radius;
                    const double outward=Vec3d::Dot(delta,radial);
                    const double lateral=(delta-radial*outward).Length();
                    // The same conservative slope bound used by normal sweeps
                    // proves clearance is increasing throughout this tiny step,
                    // rather than trusting endpoints across an unseen ridge. Absolute
                    // J2000 coordinate roundoff is bounded below the 2mm contact tolerance.
                    const double radialScale=body.radiusMeters/std::max(body.radiusMeters*0.5,relative.Length());
                    const double roundoff=std::min(0.001,8*std::numeric_limits<double>::epsilon()*
                        (shapeStart.Length()+body.centerMeters.Length()));
                    if(endSurface.available&&endClearance>=clearance&&
                        outward+roundoff>1.01*body.terrainMaxSlope*radialScale*lateral) break;
                }
                if(clearance<=0.002 || !body.landable || body.terrainMinHeightMeters==body.terrainMaxHeightMeters) {hit=true;break;}
                if(length<1.0e-10||t>=finish)break;
                // The terrain slope bound makes this conservative even when a
                // ridge lies between probes. Never take a minimum stride larger
                // than clearance: a tiny feature must not be skipped.
                const double radialScale=body.radiusMeters/std::max(body.radiusMeters*0.5,relative.Length());
                const double stride=std::min(config_.terrainProbeSpacingMeters,clearance/(2.0*(1.0+body.terrainMaxSlope*radialScale)));
                const double next=std::min(finish,t+stride/length);
                if(next<=t || probe+1==config_.maxTerrainProbesPerStep) {hit=true;safety=true;break;}
                t=next;
            }
            if(hit&&t<nearest.time)nearest={&body,t,start+shipDelta*t,radial,surface,safety,gear};
        }
    }
    return nearest;
}

ContactEvent FlightSimulation::ResolveContact(const SweepHit& hit) {
    ContactEvent event;
    event.bodyId=hit.body->id;event.contactCenterMeters=hit.shipCenter;
    event.normalSimulation=hit.surface.normal;
    const double normalSpeed=Vec3d::Dot(state_.velocityMetersPerSecond,hit.surface.normal);
    event.downwardSpeedMps=std::max(0.0,-normalSpeed);
    event.lateralSpeedMps=(state_.velocityMetersPerSecond-hit.surface.normal*normalSpeed).Length();
    event.slopeDegrees=AngleDegrees(hit.surface.normal,hit.radial);
    event.tiltDegrees=AngleDegrees(Up(state_.orientation),hit.surface.normal);
    const bool safe=hit.body->landable&&hit.gearContact&&state_.gearDeployed&&hit.surface.available&&!hit.safetyLimit&&
        state_.mode!=FlightMode::Cruise&&state_.mode!=FlightMode::LocalCruise&&normalSpeed<=0.001&&
        event.downwardSpeedMps<=config_.maxLandingDescentMps&&event.lateralSpeedMps<=config_.maxLandingLateralMps&&
        event.slopeDegrees<=config_.maxLandingSlopeDegrees&&event.tiltDegrees<=config_.maxLandingTiltDegrees;
    ResetDynamics();
    state_.velocityMetersPerSecond={};state_.throttle=0.0;state_.throttleNeutralRequired=true;
    if(safe) {
        event.kind=ContactKind::Landed;state_.mode=FlightMode::Landed;state_.landedBodyId=hit.body->id;
        // Keep the pose accepted by the full hull/foot sweep. Repositioning from
        // the center terrain sample can bury another foot on uneven ground.
        state_.positionMeters=hit.shipCenter;
    } else {
        event.kind=hit.safetyLimit?ContactKind::TerrainSafetyLimit:ContactKind::Recovered;
        state_.mode=FlightMode::Maneuver;state_.landedBodyId.clear();
        if(state_.recoveryCount<std::numeric_limits<std::uint64_t>::max())++state_.recoveryCount;
        const double shell=hit.body->radiusMeters+(hit.body->landable?hit.body->terrainMaxHeightMeters:hit.body->atmosphereHeightMeters);
        state_.positionMeters=hit.body->centerMeters+hit.radial*(shell+config_.recoveryClearanceMeters+config_.hullHalfLengthMeters);
        Vec3d tangent=Forward(state_.orientation)-hit.radial*Vec3d::Dot(Forward(state_.orientation),hit.radial);
        if(tangent.Length()<0.01)tangent=Vec3d::Cross(std::abs(hit.radial.z)<0.9?Vec3d{0,0,1}:Vec3d{0,1,0},hit.radial);
        state_.orientation=Quatd::FromForwardUp(tangent,hit.radial);
    }
    return event;
}

void FlightSimulation::Takeoff(bool smooth) {
    const auto* body=FindBody(state_.landedBodyId);
    if(!body)return;
    const Vec3d radial=(state_.positionMeters-body->centerMeters).Normalized();
    const auto surface=SampleSurface(*body,radial);
    if(smooth) {
        guidanceReleaseBody_=body->id;guidanceReleasePosition_=state_.positionMeters;
        // Release the support constraint without assigning position or velocity.
        guidanceAcceleration_={};
    } else {
        state_.positionMeters+=surface.normal*0.05;
        state_.velocityMetersPerSecond=surface.normal*config_.takeoffSpeedMps;
    }
    state_.mode=FlightMode::Landing;state_.landedBodyId.clear();state_.throttle=0.0;
}

ContactEvent FlightSimulation::Step(const FlightInput& raw) {
    const double dt=config_.fixedStepSeconds;
    state_.simulationTimeSeconds+=dt;
    if(state_.mode==FlightMode::Landed) {
        if(raw.takeoff)Takeoff(raw.smoothGuidance);
        return {};
    }
    if(!raw.smoothGuidance||(state_.positionMeters-guidanceReleasePosition_).Length()>=0.10) guidanceReleaseBody_.clear();
    if(raw.hasThrottle)SetThrottle(raw.throttle);
    const Quatd before=state_.orientation;
    const double attitudeScale=state_.mode==FlightMode::Landing?0.45:1.0;
    double proximity=0.0;
    const Vec3d referenceUp=ReferenceUp(proximity);
    Vec3d angular{Axis(raw.roll)*config_.rollRateRadiansPerSecond,
        -Axis(raw.pitch)*config_.pitchRateRadiansPerSecond,-Axis(raw.yaw)*config_.yawRateRadiansPerSecond};
    angular=angular*attitudeScale;
    // Space has no global up: leveling is an explicit near-body pilot aid.
    // Device deadzones belong to input mapping. Even a small intentional roll
    // command (including precise route alignment) must outrank horizon assist.
    if(flightAssistEnabled_&&proximity>0.0&&Axis(raw.roll)==0.0) {
        const Vec3d forward=Forward(before);
        const Vec3d horizon=referenceUp-forward*Vec3d::Dot(referenceUp,forward);
        // Near vertical flight has no reliable bank reference: damp only.
        if(horizon.Length()>0.15) {
            const Vec3d targetUp=horizon.Normalized();
            const double error=std::atan2(Vec3d::Dot(Vec3d::Cross(Up(before),targetUp),forward),
                Vec3d::Dot(Up(before),targetUp));
            angular.x=Clamp(error*config_.bankLevelRatePerSecond*proximity,
                -config_.maxBankAssistRadiansPerSecond,config_.maxBankAssistRadiansPerSecond)*attitudeScale;
        }
    }
    const double response=1.0-std::exp(-dt/config_.angularResponseSeconds);
    angularVelocity_+=(angular-angularVelocity_)*response;
    const double magnitude=angularVelocity_.Length();
    if(magnitude>1.0e-10)state_.orientation=(before*Quatd::FromAxisAngle(angularVelocity_,magnitude*dt)).Normalized();
    const bool localCruise=state_.mode==FlightMode::LocalCruise;
    const bool cruise=state_.mode==FlightMode::Cruise||localCruise;
    const double chargeTarget=cruise&&state_.throttle>0.02&&!raw.brake?1.0:0.0;
    propulsion_.cruiseCharge+=(chargeTarget-propulsion_.cruiseCharge)*(1.0-std::exp(-dt/config_.cruiseChargeSeconds));
    double maxSpeed=localCruise?LocalCruiseSpeedLimitMps():cruise?std::min(config_.maxCruiseSpeedMps,ApproachSpeedLimitMps()):
        state_.mode==FlightMode::Landing?config_.maxLandingSpeedMps:config_.maxManeuverSpeedMps;
    maxSpeed=std::min(maxSpeed,config_.maxCruiseSpeedMps);
    Vec3d desired=Forward(state_.orientation)*(state_.throttle*maxSpeed);
    Vec3d translation=Right(state_.orientation)*Axis(raw.strafeRight)+Up(state_.orientation)*Axis(raw.strafeUp);
    if(state_.mode==FlightMode::Landing) {
        // Normalize the pair, so diagonal input cannot exceed the landing limit.
        translation=translation/std::max(1.0,translation.Length());
        desired+=translation*config_.landingTranslationSpeedMps;
    } else desired+=translation*config_.strafeSpeedMps;
    if(raw.brake) {desired={};state_.throttle=0.0;}
    double acceleration=localCruise?config_.localCruiseAccelerationMps2:cruise?config_.cruiseAccelerationMps2:config_.maneuverAccelerationMps2;
    // Leaving cruise must retain high-energy braking; using local acceleration
    // here would leave a ship moving at c for years after a mode switch.
    const bool decelerating=raw.brake || state_.velocityMetersPerSecond.Length()>desired.Length()+1.0;
    if(decelerating) {
        const double speed=state_.velocityMetersPerSecond.Length();
        acceleration=speed>config_.maxLocalCruiseSpeedMps*1.05?
            config_.cruiseBrakeAccelerationMps2:(localCruise||speed>config_.maxManeuverSpeedMps+1.0)?
            config_.localCruiseBrakeMps2:config_.brakeAccelerationMps2;
    }
    if(cruise&&!decelerating)acceleration*=0.15+0.85*propulsion_.cruiseCharge;
    const Vec3d oldVelocity=state_.velocityMetersPerSecond;
    if(raw.smoothGuidance&&!localCruise) {
        // Critically damped velocity tracking, integrated at the fixed step.
        // Acceleration and jerk envelopes follow the nearest physical surface;
        // deep-space values explicitly belong to the fictional cruise drive.
        double clearance=1.0e20;
        for(const auto& body:bodies_) {
            const double shell=body.radiusMeters+(body.landable?body.terrainMaxHeightMeters:body.atmosphereHeightMeters);
            clearance=std::min(clearance,std::max(0.0,(state_.positionMeters-body.centerMeters).Length()-shell));
        }
        const double cap=cruise?std::min(config_.cruiseAccelerationMps2,45.0+clearance*0.50):
            (state_.mode==FlightMode::Landing?1.0:config_.maneuverAccelerationMps2);
        const double omega=cruise?4.0:1.5;
        const double jerkCap=cap*(cruise?4.0:2.0);
        Vec3d jerk=(desired-oldVelocity)*(omega*omega)-guidanceAcceleration_*(2*omega);
        if(jerk.Length()>jerkCap) jerk=jerk.Normalized()*jerkCap;
        guidanceAcceleration_+=jerk*dt;
        if(guidanceAcceleration_.Length()>cap) guidanceAcceleration_=guidanceAcceleration_.Normalized()*cap;
        state_.velocityMetersPerSecond=oldVelocity+guidanceAcceleration_*dt;
    } else {
        guidanceAcceleration_={};
        state_.velocityMetersPerSecond=MoveToward(oldVelocity,desired,acceleration*dt);
    }
    propulsion_.accelerationDemand=Clamp((state_.velocityMetersPerSecond-oldVelocity).Length()/(acceleration*dt),0.0,1.0);
    propulsion_.braking=Clamp((oldVelocity.Length()-state_.velocityMetersPerSecond.Length())/(acceleration*dt),0.0,1.0);
    const double bankLoad=1.0-std::abs(Vec3d::Dot(Up(state_.orientation),referenceUp));
    propulsion_.hoverDemand=flightAssistEnabled_&&!cruise&&!state_.throttleNeutralRequired?
        proximity*(0.18+0.18*bankLoad):0.0;
    const double translationLoad=Clamp(translation.Length(),0.0,1.0)*0.45;
    const double rotationLoad=Clamp(angularVelocity_.Length(),0.0,1.0)*0.30;
    propulsion_.powerDemand=Clamp(std::max({propulsion_.accelerationDemand,
        state_.throttle*0.65,propulsion_.hoverDemand+translationLoad+rotationLoad}),0.0,1.0);
    const double engineTime=propulsion_.powerDemand>propulsion_.engineOutput?config_.engineRiseSeconds:config_.engineFallSeconds;
    propulsion_.engineOutput+=(propulsion_.powerDemand-propulsion_.engineOutput)*(1.0-std::exp(-dt/engineTime));
    if(state_.velocityMetersPerSecond.Length()>config_.maxCruiseSpeedMps)
        state_.velocityMetersPerSecond=state_.velocityMetersPerSecond.Normalized()*config_.maxCruiseSpeedMps;
    const Vec3d destination=state_.positionMeters+state_.velocityMetersPerSecond*dt;
    const auto hit=Sweep(state_.positionMeters,destination,before);
    if(hit.body)return ResolveContact(hit);
    if(localCruise&&flightAssistEnabled_) {
        // Transport the local horizon through the actual integrated motion.
        // This flight assist keeps a level course around a curved planet;
        // pitch/roll inputs still change the attitude relative to that horizon.
        const BodyDefinition* nearby=nullptr;double relativeDistance=1.5;
        for(const auto& body:bodies_) {
            const double ratio=(state_.positionMeters-body.centerMeters).Length()/body.radiusMeters;
            if(ratio<relativeDistance){nearby=&body;relativeDistance=ratio;}
        }
        if(nearby) {
            const auto from=(state_.positionMeters-nearby->centerMeters).Normalized();
            const auto to=(destination-nearby->centerMeters).Normalized();
            const auto axis=Vec3d::Cross(from,to);
            if(axis.Length()>1e-12) {
                const auto transport=Quatd::FromAxisAngle(axis,std::atan2(axis.Length(),Vec3d::Dot(from,to)));
                state_.orientation=(transport*state_.orientation).Normalized();
                state_.velocityMetersPerSecond=transport.Rotate(state_.velocityMetersPerSecond);
            }
        }
    }
    state_.positionMeters=destination;
    return {};
}

AdvanceResult FlightSimulation::Advance(double frameSeconds,const FlightInput& input) {
    AdvanceResult result;
    if(input.paused) {accumulatorSeconds_=0.0;ResetDynamics(false);return result;}
    if(!std::isfinite(frameSeconds)||frameSeconds<=0.0)return result;
    const double accepted=std::min(frameSeconds,config_.maxFrameSeconds);
    result.discardedSeconds=frameSeconds-accepted;
    accumulatorSeconds_+=accepted;
    const double epsilon=config_.fixedStepSeconds*1.0e-9;
    while(accumulatorSeconds_+epsilon>=config_.fixedStepSeconds) {
        accumulatorSeconds_=std::max(0.0,accumulatorSeconds_-config_.fixedStepSeconds);
        const auto event=Step(input);++result.fixedSteps;
        if(event.kind!=ContactKind::None) {
            if(result.contact.kind==ContactKind::None)result.contact=event;
            // Continue fixed steps after contact. The persistent neutral interlock
            // prevents held throttle from restarting, independently of render dt.
        }
    }
    return result;
}

std::string SerializeFlightState(const FlightState& s) {
    if(!ValidStateData(s))return {};
    std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::setprecision(17);
    stream<<"STAR_FLIGHT 1\n"<<s.positionMeters.x<<' '<<s.positionMeters.y<<' '<<s.positionMeters.z<<'\n'
        <<s.velocityMetersPerSecond.x<<' '<<s.velocityMetersPerSecond.y<<' '<<s.velocityMetersPerSecond.z<<'\n'
        <<s.orientation.w<<' '<<s.orientation.x<<' '<<s.orientation.y<<' '<<s.orientation.z<<'\n'
        <<static_cast<unsigned>(s.mode)<<' '<<s.throttle<<' '<<s.gearDeployed<<' '<<s.throttleNeutralRequired<<' '<<s.recoveryCount<<' '<<s.simulationTimeSeconds<<'\n'
        <<std::quoted(s.targetBodyId)<<' '<<std::quoted(s.landedBodyId)<<'\n';
    return stream.str();
}
bool DeserializeFlightState(const std::string& text,FlightState& state) {
    if(text.size()>8192)return false;
    std::istringstream stream(text);stream.imbue(std::locale::classic());
    FlightState parsed;std::string magic;unsigned version=0,mode=0,gear=0,neutral=0;
    if(!(stream>>magic>>version)||magic!="STAR_FLIGHT"||version!=1)return false;
    if(!(stream>>parsed.positionMeters.x>>parsed.positionMeters.y>>parsed.positionMeters.z
        >>parsed.velocityMetersPerSecond.x>>parsed.velocityMetersPerSecond.y>>parsed.velocityMetersPerSecond.z
        >>parsed.orientation.w>>parsed.orientation.x>>parsed.orientation.y>>parsed.orientation.z
        >>mode>>parsed.throttle>>gear>>neutral>>parsed.recoveryCount>>parsed.simulationTimeSeconds
        >>std::quoted(parsed.targetBodyId)>>std::quoted(parsed.landedBodyId))||mode>4||gear>1||neutral>1)return false;
    parsed.mode=static_cast<FlightMode>(mode);parsed.gearDeployed=gear!=0;parsed.throttleNeutralRequired=neutral!=0;
    stream>>std::ws;if(!stream.eof()||!ValidStateData(parsed))return false;
    parsed.orientation=parsed.orientation.Normalized();state=std::move(parsed);return true;
}
} // namespace star
