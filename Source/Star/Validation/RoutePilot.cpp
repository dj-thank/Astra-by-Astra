#include "RoutePilot.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace star::validation {
namespace {
double Clamp(double v,double lo,double hi) { return std::max(lo,std::min(hi,v)); }
double Attitude(const FlightState& state, Vec3d forward, Vec3d up, FlightInput& input) {
    auto error=(state.orientation.Conjugate()*Quatd::FromForwardUp(forward,up)).Normalized();
    if(error.w<0) error={-error.w,-error.x,-error.y,-error.z};
    const double length=std::hypot(error.x,error.y,error.z);
    const double angle=2*std::atan2(length,error.w);
    const double scale=(length>1e-12?angle/length:0)*2.5/(state.mode==FlightMode::Landing?0.45:1.0);
    input.roll=Clamp(error.x*scale/0.80,-1,1);
    input.pitch=Clamp(-error.y*scale/0.55,-1,1);
    input.yaw=Clamp(-error.z*scale/0.60,-1,1);
    return angle;
}
}
RoutePilot::RoutePilot(const std::vector<BodyDefinition>& bodies,TerrainSampler terrain):terrain_(std::move(terrain)) {
    bool e=false,m=false,s=false;
    for(const auto& b:bodies) {
        if(b.id=="earth") {earth_=b;e=true;}
        if(b.id=="moon") {moon_=b;m=true;}
        if(b.id=="saturn") {saturn_=b;s=true;}
    }
    if(!e||!m||!s||!terrain_) failure_="Required dated bodies/terrain sampler unavailable";
}
void RoutePilot::Arc(const BodyDefinition& body,Vec3d from,Vec3d to,double radius) {
    from=from.Normalized();to=to.Normalized();
    const double angle=std::acos(Clamp(Vec3d::Dot(from,to),-1,1));
    auto axis=Vec3d::Cross(from,to);
    if(axis.Length()<1e-8) axis=Vec3d::Cross(from,Vec3d{0,0,1});
    const int steps=std::max(1,static_cast<int>(std::ceil(angle/(Pi/6))));
    for(int i=1;i<=steps;++i) waypoints_.push_back({body.centerMeters+Quatd::FromAxisAngle(axis,angle*i/steps).Rotate(from)*radius,body.id});
}
void RoutePilot::Initialize(const FlightState& state) {
    startTime_=phaseTime_=state.simulationTimeSeconds;initialRecoveries_=state.recoveryCount;
    const double lat=20.1908*Pi/180,lon=30.7717*Pi/180;
    siteRadial_=moon_.bodyFixedToSimulation.Rotate({std::cos(lat)*std::cos(lon),std::cos(lat)*std::sin(lon),std::sin(lat)});
    TerrainSample sample;
    if(!terrain_||!terrain_(moon_,siteRadial_,sample)||!std::isfinite(sample.heightMeters)||!sample.normalSimulation.IsFinite()) { failure_="Apollo17 measured terrain unavailable";return; }
    sitePosition_=moon_.centerMeters+siteRadial_*(moon_.radiusMeters+sample.heightMeters);
    siteNormal_=sample.normalSimulation.Normalized();
    siteForward_=Vec3d::Cross(moon_.bodyFixedToSimulation.Rotate({0,0,1}),siteNormal_).Normalized();
    if(Vec3d::Dot(siteNormal_,siteRadial_)<std::cos(12*Pi/180)) {failure_="Apollo17 site exceeds conservative landing slope";return;}
    const auto earthRadial=(state.positionMeters-earth_.centerMeters).Normalized();
    const auto earthMoon=(moon_.centerMeters-earth_.centerMeters).Normalized();
    waypoints_.push_back({earth_.centerMeters+earthRadial*(earth_.radiusMeters*3),"earth"});
    Arc(earth_,earthRadial,earthMoon,earth_.radiusMeters*3);
    waypoints_.push_back({moon_.centerMeters-earthMoon*(moon_.radiusMeters*3),"moon"});
    Arc(moon_,-earthMoon,siteRadial_,moon_.radiusMeters*3);
    waypoints_.push_back({sitePosition_+siteRadial_*80,"moon"});
    // Above the ring plane, looking down onto the outer B ring. All coordinates
    // are transformed by the dated Saturn orientation, never a fixed world Z.
    ringPosition_=saturn_.centerMeters+saturn_.bodyFixedToSimulation.Rotate({120000000,0,40000000});
    ringLook_=saturn_.centerMeters+saturn_.bodyFixedToSimulation.Rotate({105000000,0,0});
}
RouteCommand RoutePilot::Tick(const FlightSimulation& simulation,bool actualScanComplete) {
    const auto& state=simulation.State();
    if(startTime_<0&&failure_.empty()) Initialize(state);
    RouteCommand out;out.controls.hasThrottle=true;out.controls.throttle=0;
    out.targetBodyId=phase_<5?"moon":"saturn";
    if(state.recoveryCount!=initialRecoveries_) failure_="Collision recovery observed";
    if(startTime_>=0&&state.simulationTimeSeconds-startTime_>1800) failure_="Route exceeded 1800 simulation seconds";
    if(!failure_.empty()) {out.failed=true;out.failure=failure_;out.stage="failed";out.controls.brake=true;return out;}
    auto next=[&](int phase){phase_=phase;phaseTime_=state.simulationTimeSeconds;};
    if(phase_==0||phase_==5) {
        out.stage=phase_==0?"earth_to_moon":"moon_to_saturn";out.mode=FlightMode::Cruise;
        const auto& point=waypoints_[waypoint_];out.targetBodyId=point.body;
        const auto delta=point.position-state.positionMeters;const double distance=delta.Length();
        const bool final=waypoint_+1==waypoints_.size();
        const double tolerance=final?1.0:1000.0;
        if(distance<tolerance) {
            if(state.velocityMetersPerSecond.Length()<0.5) {
                ++waypoint_;if(waypoint_==waypoints_.size()) next(phase_==0?1:6);
            }
            out.controls.brake=true;return out;
        }
        const Vec3d up=std::abs(Vec3d::Dot(delta.Normalized(),siteNormal_))<0.9?siteNormal_:siteForward_;
        const double angle=Attitude(state,delta.Normalized(),up,out.controls);
        // Stop before substantial heading changes: finite turn rate cannot bend
        // a 50c trajectory around a planet. Exterior arc chords remain >2.8R.
        if(angle>0.015) out.controls.brake=true;
        else {
            // Local maneuver descent uses the normal 300m/s mode, after reaching
            // the exterior lunar radial. Cruise conservatively clamps to 30m/s
            // below the global maximum mountain even over the measured valley.
            const bool local=phase_==0&&final&&distance<15000;
            if(local) out.mode=FlightMode::Maneuver;
            const double limit=local?simulation.Config().maxManeuverSpeedMps:std::min(simulation.Config().maxCruiseSpeedMps,simulation.ApproachSpeedLimitMps());
            const double speed=std::min({limit*0.65,distance*(final?0.25:0.45),50*SpeedOfLightMps});
            out.controls.throttle=limit>0?speed/limit:0;
        }
    } else if(phase_==1||phase_==2) {
        out.stage=phase_==1?"moon_nearby_80m":"moon_descent";out.mode=FlightMode::Landing;out.gearDeployedDesired=true;
        const auto radial=(state.positionMeters-moon_.centerMeters).Normalized();TerrainSample sample;
        if(!terrain_(moon_,radial,sample)) {failure_="Landing terrain sample unavailable";out.controls.brake=true;return out;}
        const auto normal=sample.normalSimulation.Normalized();
        const double angle=Attitude(state,siteForward_,normal,out.controls);
        if(phase_==1) {out.controls.brake=true;if(angle<0.005&&state.simulationTimeSeconds-phaseTime_>3) next(2);}
        else if(state.mode==FlightMode::Landed) next(3);
        else if(angle<0.015) out.controls.strafeUp=-0.65;
        else out.controls.brake=true;
    } else if(phase_==3) {
        out.stage="moon_landed_scan";out.mode=FlightMode::Landing;out.gearDeployedDesired=true;out.scan=true;out.scanObjectiveId="moon_taurus_littrow";
        if(state.mode!=FlightMode::Landed) failure_="Landed state unexpectedly lost";
        if(actualScanComplete) next(4);
    } else if(phase_==4) {
        out.stage="moon_takeoff";out.mode=FlightMode::Landing;out.gearDeployedDesired=true;
        out.controls.takeoff=state.mode==FlightMode::Landed;out.controls.strafeUp=1;
        if(state.mode!=FlightMode::Landed&&simulation.Telemetry("moon").surfaceAltitudeMeters>=25) {
            waypoints_.clear();waypoint_=0;
            const auto radial=(state.positionMeters-moon_.centerMeters).Normalized();
            const auto toSaturn=(saturn_.centerMeters-moon_.centerMeters).Normalized();
            waypoints_.push_back({moon_.centerMeters+radial*(moon_.radiusMeters*3),"moon"});
            Arc(moon_,radial,toSaturn,moon_.radiusMeters*3);
            const auto saturnIncoming=(moon_.centerMeters-saturn_.centerMeters).Normalized();
            const auto ringRadial=(ringPosition_-saturn_.centerMeters).Normalized();
            waypoints_.push_back({saturn_.centerMeters+saturnIncoming*(saturn_.radiusMeters*3),"saturn"});
            Arc(saturn_,saturnIncoming,ringRadial,saturn_.radiusMeters*3);
            waypoints_.push_back({ringPosition_,"saturn"});next(5);
        }
    } else {
        out.stage=phase_==7?"complete":"saturn_rings_scan";out.mode=FlightMode::Maneuver;out.controls.brake=true;
        const double angle=Attitude(state,(ringLook_-state.positionMeters).Normalized(),saturn_.bodyFixedToSimulation.Rotate({1,0,0}),out.controls);
        out.scan=angle<0.005;out.scanObjectiveId="saturn_rings";
        if(phase_==6&&actualScanComplete&&out.scan) next(7);
        out.complete=phase_==7;
    }
    return out;
}
}
