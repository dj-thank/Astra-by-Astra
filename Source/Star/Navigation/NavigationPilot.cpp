#include "NavigationPilot.h"
#include "Simulation/Astronomy.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace star::navigation {
namespace {
double Clamp(double value, double low, double high) { return std::max(low,std::min(high,value)); }
double Shell(const BodyDefinition& body) {
    return body.radiusMeters + std::max(0.0,body.landable ? body.terrainMaxHeightMeters : body.atmosphereHeightMeters);
}
double Tolerance(const BodyDefinition& body) { return std::max(100.0,body.radiusMeters*0.0001); }
double Heading(const FlightState& state, const BodyDefinition& body) {
    return std::acos(Clamp(Vec3d::Dot(Forward(state.orientation),
        (body.centerMeters-state.positionMeters).Normalized()),-1,1))*180.0/Pi;
}
bool SameBody(const BodyDefinition& a, const BodyDefinition& b) {
    return a.id==b.id && (a.centerMeters-b.centerMeters).LengthSquared()==0 &&
        a.radiusMeters==b.radiusMeters && a.atmosphereHeightMeters==b.atmosphereHeightMeters &&
        a.landable==b.landable && a.terrainMaxHeightMeters==b.terrainMaxHeightMeters;
}
bool ClearSegment(Vec3d start, Vec3d end, const std::vector<BodyDefinition>& bodies, double margin) {
    const Vec3d delta=end-start;
    const double length2=delta.LengthSquared();
    if(!std::isfinite(length2)) return false;
    for(const auto& body:bodies) {
        const Vec3d offset=start-body.centerMeters;
        const double t=length2>1 ? Clamp(-Vec3d::Dot(offset,delta)/length2,0,1) : 0;
        if((offset+delta*t).Length()<Shell(body)+margin) return false;
    }
    return true;
}
// The same physical-axis convention as FlightSimulation. No global-up or UE
// quaternion assumption; the chosen up is perpendicularized by FromForwardUp.
double Steer(const FlightSimulation& simulation, Vec3d forward, Vec3d up, FlightInput& input) {
    const auto& state=simulation.State();
    auto error=(state.orientation.Conjugate()*Quatd::FromForwardUp(forward,up)).Normalized();
    if(error.w<0) error={-error.w,-error.x,-error.y,-error.z};
    const double length=std::hypot(error.x,error.y,error.z);
    const double angle=2*std::atan2(length,error.w);
    const double scale=(length>1e-12 ? angle/length : 0)*1.8/(state.mode==FlightMode::Landing ? 0.45 : 1.0);
    const auto& config=simulation.Config();
    input.roll=Clamp(error.x*scale/config.rollRateRadiansPerSecond,-1,1);
    input.pitch=Clamp(-error.y*scale/config.pitchRateRadiansPerSecond,-1,1);
    input.yaw=Clamp(-error.z*scale/config.yawRateRadiansPerSecond,-1,1);
    return angle;
}
Vec3d StableUp(Vec3d forward, Vec3d candidate) {
    if(std::abs(Vec3d::Dot(forward.Normalized(),candidate.Normalized()))>0.92)
        candidate=std::abs(forward.z)<0.8 ? Vec3d{0,0,1} : Vec3d{0,1,0};
    return candidate;
}
}

double NavigationPilot::StandOffMeters(const BodyDefinition& body) {
    return std::max(100000.0,body.radiusMeters*0.15);
}
const BodyDefinition* NavigationPilot::NearestBody(const FlightSimulation& simulation) const {
    const BodyDefinition* nearest=nullptr;
    double minimum=std::numeric_limits<double>::max();
    for(const auto& body:simulation.Bodies()) {
        const double distance=(simulation.State().positionMeters-body.centerMeters).Length()-Shell(body);
        if(distance<minimum) { minimum=distance;nearest=&body; }
    }
    return nearest;
}
void NavigationPilot::Revoke(NavigationStatus reason) {
    authorizedDestination_.clear();authorizationTime_=-1;route_.clear();waypoint_=0;
    status_=reason;
}
bool NavigationPilot::SelectDestination(const FlightSimulation& simulation, const std::string& id) {
    if(id==destination_ && !id.empty()) return Validate(simulation);
    Revoke(NavigationStatus::AwaitingConfirmation);autopilot_=false;localHold_=false;arrived_=false;
    destination_=id;selectedBodies_=simulation.Bodies();
    lastTime_=simulation.State().simulationTimeSeconds;recoveryCount_=simulation.State().recoveryCount;
    if(id.empty() || !simulation.FindBody(id) || simulation.State().targetBodyId!=id) {
        status_=id.empty() ? NavigationStatus::Idle : NavigationStatus::InvalidTarget;
        return false;
    }
    return true;
}
bool NavigationPilot::Validate(const FlightSimulation& simulation) {
    const auto& state=simulation.State();
    if(destination_.empty() || !simulation.FindBody(destination_) || state.targetBodyId!=destination_) {
        Revoke(destination_.empty() ? NavigationStatus::Idle : NavigationStatus::InvalidTarget);
        autopilot_=false;return false;
    }
    bool changed=simulation.Bodies().size()!=selectedBodies_.size();
    for(std::size_t i=0;!changed && i<selectedBodies_.size();++i)
        changed=!SameBody(selectedBodies_[i],simulation.Bodies()[i]);
    changed=changed || (lastTime_>=0 && state.simulationTimeSeconds<lastTime_) || state.recoveryCount!=recoveryCount_;
    lastTime_=state.simulationTimeSeconds;recoveryCount_=state.recoveryCount;
    if(changed || !state.positionMeters.IsFinite() || !state.velocityMetersPerSecond.IsFinite() || !state.orientation.IsFinite()) {
        Revoke(NavigationStatus::StateChanged);autopilot_=false;return false;
    }
    const auto& body=*simulation.FindBody(destination_);
    if(!authorizedDestination_.empty() && ((state.positionMeters-body.centerMeters).Length()<=
        Shell(body)+StandOffMeters(body)+Tolerance(body) || state.mode==FlightMode::Landed)) {
        Revoke(NavigationStatus::Arrived);arrived_=true;localHold_=true;
        holdForward_=Forward(state.orientation);holdUp_=Up(state.orientation);
    }
    return true;
}
bool NavigationPilot::CanConfirm(const FlightSimulation& simulation) const {
    const auto* target=simulation.FindBody(destination_);
    const auto* nearest=NearestBody(simulation);
    const auto& state=simulation.State();
    return !paused_ && !arrived_ && target && nearest && target->id!=nearest->id &&
        state.targetBodyId==destination_ && state.mode!=FlightMode::Landed &&
        !state.throttleNeutralRequired && Heading(state,*target)<=ConfirmationConeDegrees;
}
bool NavigationPilot::ConfirmTransfer(const FlightSimulation& simulation) {
    if(!Validate(simulation)) return false;
    if(authorizedDestination_==destination_) return true;
    if(!CanConfirm(simulation)) return false;
    authorizedDestination_=destination_;authorizationTime_=simulation.State().simulationTimeSeconds;
    localHold_=false;status_=NavigationStatus::TransferAuthorized;
    if(autopilot_ && !BuildRoute(simulation)) {
        Revoke(NavigationStatus::RouteBlocked);autopilot_=false;return false;
    }
    return true;
}
bool NavigationPilot::StartAutopilot(const FlightSimulation& simulation) {
    if(paused_ || !Validate(simulation)) return false;
    const auto* nearest=NearestBody(simulation);
    autopilot_=true;localHold_=arrived_ || (authorizedDestination_.empty() && nearest && nearest->id==destination_);
    holdForward_=Forward(simulation.State().orientation);holdUp_=Up(simulation.State().orientation);
    if(localHold_) { Revoke(arrived_ ? NavigationStatus::Arrived : NavigationStatus::LocalHold);return true; }
    if(!authorizedDestination_.empty() && !BuildRoute(simulation)) {
        Revoke(NavigationStatus::RouteBlocked);autopilot_=false;return false;
    }
    return true;
}
void NavigationPilot::Cancel() {
    Revoke(NavigationStatus::Cancelled);autopilot_=false;localHold_=false;
}
void NavigationPilot::SetPaused(bool paused) {
    if(paused) { Revoke(NavigationStatus::Paused);autopilot_=false; }
    paused_=paused;
}
void NavigationPilot::ResetAfterLoad() { *this=NavigationPilot{}; }
void NavigationPilot::FollowCelestialFrames(const FlightSimulation& simulation) {
    const auto& next=simulation.Bodies();
    if(selectedBodies_.empty())return;
    if(next.size()!=selectedBodies_.size()){Revoke(NavigationStatus::StateChanged);autopilot_=false;return;}
    for(std::size_t i=0;i<next.size();++i){
        auto expected=selectedBodies_[i];expected.centerMeters=next[i].centerMeters;
        if(!SameBody(expected,next[i])||!next[i].centerMeters.IsFinite()||!next[i].bodyFixedToSimulation.IsFinite())
        {Revoke(NavigationStatus::StateChanged);autopilot_=false;return;}
    }
    for(auto& point:route_) {
        FlightState anchor;anchor.positionMeters=point;
        const auto* from=NearestReferenceBody(selectedBodies_,anchor);if(!from)continue;
        const auto i=static_cast<std::size_t>(from-selectedBodies_.data());
        point=TransportFlightFrame(anchor,*from,next[i]).positionMeters;
    }
    // Local attitude hold rotates with the same body frame as the assisted ship.
    const auto* current=NearestBody(simulation);
    if(current)for(const auto& old:selectedBodies_)if(old.id==current->id){
        const auto q=(current->bodyFixedToSimulation*old.bodyFixedToSimulation.Conjugate()).Normalized();
        holdForward_=q.Rotate(holdForward_);holdUp_=q.Rotate(holdUp_);
    }
    selectedBodies_=next;
    auto previous=simulation.State().positionMeters;
    for(std::size_t i=waypoint_;i<route_.size();++i){
        if(!ClearSegment(previous,route_[i],next,std::max(100.0,simulation.Config().hullHalfLengthMeters*4)))
        {Revoke(NavigationStatus::RouteBlocked);autopilot_=false;return;}
        previous=route_[i];
    }
}

bool NavigationPilot::BuildRoute(const FlightSimulation& simulation) {
    route_.clear();waypoint_=0;
    const auto* target=simulation.FindBody(destination_);
    const auto* origin=NearestBody(simulation);
    if(!target || !origin) return false;
    const Vec3d start=simulation.State().positionMeters;
    const Vec3d incoming=(start-target->centerMeters).Normalized();
    const Vec3d end=target->centerMeters+incoming*(Shell(*target)+StandOffMeters(*target));
    const double margin=std::max(100.0,simulation.Config().hullHalfLengthMeters*4);
    if(!ClearSegment(start,end,simulation.Bodies(),margin)) {
        // Only departure-body detours are generated. Other obstructing bodies
        // fail closed; this is deliberately not an all-system route planner.
        const Vec3d radial=(start-origin->centerMeters).Normalized();
        const Vec3d outgoing=(end-origin->centerMeters).Normalized();
        const double radius=std::max((start-origin->centerMeters).Length(),Shell(*origin)+origin->radiusMeters*0.5+margin*2);
        route_.push_back(origin->centerMeters+radial*radius);
        const double angle=std::acos(Clamp(Vec3d::Dot(radial,outgoing),-1,1));
        Vec3d axis=Vec3d::Cross(radial,outgoing);
        if(axis.Length()<1e-8) axis=Vec3d::Cross(radial,StableUp(radial,{0,0,1}));
        const int steps=std::max(1,static_cast<int>(std::ceil(angle/(Pi/9))));
        for(int i=1;i<=steps;++i)
            route_.push_back(origin->centerMeters+Quatd::FromAxisAngle(axis,angle*i/steps).Rotate(radial)*radius);
    }
    route_.push_back(end);
    Vec3d previous=start;
    for(const auto& point:route_) {
        if(!ClearSegment(previous,point,simulation.Bodies(),margin)) {route_.clear();return false;}
        previous=point;
    }
    return true;
}

NavigationCommand NavigationPilot::Tick(const FlightSimulation& simulation) {
    const bool valid=Validate(simulation);
    const auto& state=simulation.State();
    const auto* target=valid ? simulation.FindBody(destination_) : nullptr;
    const auto* nearest=NearestBody(simulation);
    NavigationCommand out;
    out.mode=state.mode==FlightMode::Landing || state.mode==FlightMode::Landed ? state.mode : FlightMode::Maneuver;
    out.destinationBodyId=destination_;out.localBodyId=nearest ? nearest->id : "";
    out.status=status_;out.autopilotActive=autopilot_;
    out.controls.hasThrottle=true;out.controls.throttle=0;out.controls.brake=true;
    out.controls.smoothGuidance=true;
    out.highSpeedAuthorized=valid && !paused_ && authorizedDestination_==destination_ && !destination_.empty();
    const double unconfirmedLimit=state.mode==FlightMode::Cruise?simulation.Config().maxManeuverSpeedMps*2:
        simulation.Config().maxLocalCruiseSpeedMps*1.05;
    out.requiresSafetyPause=!out.highSpeedAuthorized && state.velocityMetersPerSecond.Length()>unconfirmedLimit;
    out.canConfirmTransfer=valid && CanConfirm(simulation);
    if(target) out.headingErrorDegrees=Heading(state,*target);
    if(paused_) { out.status=NavigationStatus::Paused;out.controls.paused=true;return out; }
    if(!valid) {out.controls.smoothGuidance=false;return out;}
    if(!autopilot_) {
        if(out.highSpeedAuthorized) out.status=NavigationStatus::TransferAuthorized;
        else if(status_==NavigationStatus::AwaitingConfirmation || status_==NavigationStatus::ReadyToConfirm)
            out.status=out.canConfirmTransfer ? NavigationStatus::ReadyToConfirm : NavigationStatus::AwaitingConfirmation;
        return out;
    }
    if(localHold_) {
        out.status=arrived_ ? NavigationStatus::Arrived : NavigationStatus::LocalHold;
        // Existing high energy requires pause at the runtime boundary. If root
        // explicitly elects to brake, ordinary simulation braking remains usable.
        out.controls.smoothGuidance=state.velocityMetersPerSecond.Length()<=simulation.Config().maxLandingSpeedMps;
        Steer(simulation,holdForward_,holdUp_,out.controls);
        return out;
    }
    if(!out.highSpeedAuthorized) {
        const Vec3d forward=(target->centerMeters-state.positionMeters).Normalized();
        Steer(simulation,forward,StableUp(forward,holdUp_),out.controls);
        out.status=out.canConfirmTransfer ? NavigationStatus::ReadyToConfirm : NavigationStatus::Aligning;
        return out;
    }
    if(route_.empty() && !BuildRoute(simulation)) {
        Revoke(NavigationStatus::RouteBlocked);autopilot_=false;
        out.status=status_;out.highSpeedAuthorized=false;out.autopilotActive=false;
        out.requiresSafetyPause=state.velocityMetersPerSecond.Length()>unconfirmedLimit;
        return out;
    }
    const bool final=waypoint_+1==route_.size();
    Vec3d delta=route_[waypoint_]-state.positionMeters;
    double distance=delta.Length();
    const double tolerance=final ? Tolerance(*target) : std::max(1000.0,nearest ? nearest->radiusMeters*0.015 : 1000.0);
    if(distance<tolerance && !final && state.velocityMetersPerSecond.Length()<10) {
        ++waypoint_;delta=route_[waypoint_]-state.positionMeters;distance=delta.Length();
    }
    out.mode=FlightMode::Cruise;out.guidancePointMeters=route_[waypoint_];
    const Vec3d forward=delta.Normalized();
    const double angle=Steer(simulation,forward,StableUp(forward,holdUp_),out.controls);
    out.status=waypoint_+1<route_.size() ? NavigationStatus::Departing : NavigationStatus::Travelling;
    if(angle>0.045) {out.status=NavigationStatus::Braking;return out;}
    double clearance=std::numeric_limits<double>::max();
    for(const auto& body:simulation.Bodies())
        clearance=std::min(clearance,std::max(0.0,(state.positionMeters-body.centerMeters).Length()-Shell(body)));
    const auto& config=simulation.Config();
    const double limit=std::min(config.maxCruiseSpeedMps,simulation.ApproachSpeedLimitMps());
    const double remaining=std::max(0.0,distance-tolerance*0.5);
    const double acceleration=std::min(config.cruiseAccelerationMps2,45.0+clearance*0.5);
    const double stopping=std::sqrt(2*acceleration*remaining)*0.3;
    const double ramp=Clamp((state.simulationTimeSeconds-authorizationTime_)/10.0,0,1);
    const double ease=ramp*ramp*(3-2*ramp);
    out.desiredSpeedMps=std::min({limit,30+clearance*0.12,remaining*0.16,stopping})*ease;
    // A waypoint bend is approached at walking speed before any substantial
    // reorientation, preventing finite turn rates from cutting across a body.
    if(waypoint_+1<route_.size() && distance<tolerance) out.desiredSpeedMps=0;
    out.controls.brake=out.desiredSpeedMps<=0;
    out.controls.throttle=limit>0 ? Clamp(out.desiredSpeedMps/limit,0,1) : 0;
    return out;
}
} // namespace star::navigation
