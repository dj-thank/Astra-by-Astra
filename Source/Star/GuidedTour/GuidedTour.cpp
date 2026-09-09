#include "GuidedTour.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace star::guided {
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
GuidedTour::GuidedTour(const std::vector<BodyDefinition>& bodies,TerrainSampler terrain):terrain_(std::move(terrain)) {
    bool e=false,m=false,s=false;
    for(const auto& b:bodies) {
        if(b.id=="earth") {earth_=b;e=true;}
        if(b.id=="moon") {moon_=b;m=true;}
        if(b.id=="saturn") {saturn_=b;s=true;}
    }
    if(!e||!m||!s||!terrain_) failure_="Required dated bodies/terrain sampler unavailable";
}
void GuidedTour::Arc(const BodyDefinition& body,Vec3d from,Vec3d to,double radius) {
    from=from.Normalized();to=to.Normalized();
    const double angle=std::acos(Clamp(Vec3d::Dot(from,to),-1,1));
    auto axis=Vec3d::Cross(from,to);
    if(axis.Length()<1e-8) axis=Vec3d::Cross(from,Vec3d{0,0,1});
    const int steps=std::max(1,static_cast<int>(std::ceil(angle/(Pi/6))));
    for(int i=1;i<=steps;++i) waypoints_.push_back({body.centerMeters+Quatd::FromAxisAngle(axis,angle*i/steps).Rotate(from)*radius,body.id,radius*0.12});
}
void GuidedTour::Initialize(const FlightState& state) {
    startTime_=phaseTime_=state.simulationTimeSeconds;initialRecoveries_=state.recoveryCount;
    const double earthDistance=(state.positionMeters-earth_.centerMeters).Length();
    if(earthDistance<earth_.radiusMeters+earth_.atmosphereHeightMeters+1000 ||
       earthDistance>earth_.radiusMeters*1.25 || state.velocityMetersPerSecond.Length()>1 || state.mode==FlightMode::Landed) {
        failure_="Start this tour from the fresh Earth scenario";return;
    }
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
    waypoints_.push_back({earth_.centerMeters+earthRadial*(earth_.radiusMeters*3),"earth",earth_.radiusMeters*0.15});
    Arc(earth_,earthRadial,earthMoon,earth_.radiusMeters*3);
    waypoints_.push_back({moon_.centerMeters-earthMoon*(moon_.radiusMeters*3),"moon",moon_.radiusMeters*0.15});
    Arc(moon_,-earthMoon,siteRadial_,moon_.radiusMeters*3);
    // Enter the measured valley 3 km west of the site, already well above the
    // local relief. No route position is assigned to the simulation.
    const auto entryRadial=(sitePosition_-moon_.centerMeters-siteForward_*3000).Normalized();
    TerrainSample entry;
    if(!terrain_(moon_,entryRadial,entry)||!std::isfinite(entry.heightMeters)) {failure_="Valley entry terrain unavailable";return;}
    waypoints_.push_back({moon_.centerMeters+entryRadial*(moon_.radiusMeters+entry.heightMeters+650),"moon"});
    // Above the ring plane, looking down onto the outer B ring. All coordinates
    // are transformed by the dated Saturn orientation, never a fixed world Z.
    ringPosition_=saturn_.centerMeters+saturn_.bodyFixedToSimulation.Rotate({120000000,0,40000000});
    ringLook_=saturn_.centerMeters+saturn_.bodyFixedToSimulation.Rotate({105000000,0,0});
}
TourCommand GuidedTour::Tick(const FlightSimulation& simulation,bool actualScanComplete) {
    for(auto* previous:{&earth_,&moon_,&saturn_})if(const auto* current=simulation.FindBody(previous->id)) {
        const auto q=(current->bodyFixedToSimulation*previous->bodyFixedToSimulation.Conjugate()).Normalized();
        const auto point=[&](const Vec3d& p){return current->centerMeters+q.Rotate(p-previous->centerMeters);};
        for(auto& waypoint:waypoints_)if(waypoint.body==previous->id)waypoint.position=point(waypoint.position);
        if(startTime_>=0&&previous->id=="moon"){
            siteRadial_=q.Rotate(siteRadial_);siteNormal_=q.Rotate(siteNormal_);siteForward_=q.Rotate(siteForward_);sitePosition_=point(sitePosition_);
        }
        if(startTime_>=0&&previous->id=="saturn"){ringPosition_=point(ringPosition_);ringLook_=point(ringLook_);}
        *previous=*current;
    }
    const double now=simulation.State().simulationTimeSeconds;
    // A paused frame cannot consume a waypoint, hold or scan acknowledgement.
    if(now==lastTickTime_) return cachedCommand_;
    if(lastTickTime_>=0&&now<lastTickTime_) failure_="Tour clock moved backwards; start a new tour";
    lastTickTime_=now;
    cachedCommand_=BuildCommand(simulation,actualScanComplete);
    return cachedCommand_;
}
TourCommand GuidedTour::BuildCommand(const FlightSimulation& simulation,bool actualScanComplete) {
    const auto& state=simulation.State();
    if(startTime_<0&&failure_.empty()) Initialize(state);
    TourCommand out;out.controls.hasThrottle=true;out.controls.throttle=0;out.controls.smoothGuidance=true;
    out.targetBodyId=phase_<5?"moon":"saturn";
    if(state.recoveryCount!=initialRecoveries_) failure_="Collision recovery observed";
    if(startTime_>=0&&state.simulationTimeSeconds-startTime_>1800) failure_="Route exceeded 1800 simulation seconds";
    const int displayPhase=phase_;
    auto finish=[&]() {
        out.progress=Clamp((displayPhase+1)/8.0,0,1);
        const char* titles[]={"地球を眺める","月へ向かう","谷の上空で水平飛行へ","タウルス・リットローを飛ぶ","月面を観測","月から離陸","土星へ向かう","土星の環を観測","ツアー完了"};
        out.stageTitle=titles[std::max(0,std::min(8,displayPhase+1))];
        if(out.stage=="moon_descent") out.stageTitle="月面へゆっくり着陸";
        out.lookAtMeters=displayPhase<0?earth_.centerMeters:displayPhase<5?sitePosition_:ringLook_;
        if(!failure_.empty()) {
            out.failed=true;out.failure=failure_;out.stage="failed";out.stageTitle="ツアーを停止しました";
            out.controls={};out.controls.hasThrottle=true;out.controls.brake=true;out.scan=false;out.complete=false;
        }
        if(out.complete) {out.progress=1;out.stage="complete";out.stageTitle="ツアー完了";}
        return out;
    };
    if(!failure_.empty()) return finish();
    auto next=[&](int phase){phase_=phase;phaseTime_=state.simulationTimeSeconds;};
    if(phase_==-1) {
        out.stage="earth_view"; out.targetBodyId="earth"; out.controls.brake=true;
        out.lookAtMeters=earth_.centerMeters;
        if(state.simulationTimeSeconds-phaseTime_>=8) next(0);
    } else if(phase_==0||phase_==5) {
        out.stage=phase_==0?"earth_to_moon":"moon_to_saturn";out.mode=FlightMode::Cruise;
        // Skip exterior pivots only when the entire chord is clear of every
        // physical body with a full reference-radius margin. Keep the final lunar
        // entry waypoint: horizontal flight begins above the measured valley.
        for(std::size_t candidate=waypoints_.size()-2;candidate>waypoint_;--candidate) {
            const auto segment=waypoints_[candidate].position-state.positionMeters;
            const double length2=segment.LengthSquared(); bool clear=length2>1;
            for(const auto& body:simulation.Bodies()) {
                const auto offset=state.positionMeters-body.centerMeters;
                const double shell=body.radiusMeters*2.0+std::max(body.atmosphereHeightMeters,body.terrainMaxHeightMeters);
                const double t=Clamp(-Vec3d::Dot(offset,segment)/std::max(1.0,length2),0,1);
                if((offset+segment*t).Length()<shell) {clear=false;break;}
            }
            if(clear) {waypoint_=candidate;break;}
        }
        const auto& point=waypoints_[waypoint_];
        // Route waypoints may belong to the departure body while the tour is
        // already travelling toward its next destination. Keep the UI and
        // telemetry bound to the tour's actual destination instead of showing
        // a misleading temporary Earth/Moon waypoint.
        const auto delta=point.position-state.positionMeters;const double distance=delta.Length();
        const bool final=waypoint_+1==waypoints_.size();
        const double tolerance=final?(phase_==5?1000000.0:1.0):point.passRadius;
        if(distance<tolerance) {
            if(!final||state.velocityMetersPerSecond.Length()<0.5) {
                ++waypoint_;if(waypoint_==waypoints_.size()) next(phase_==0?1:6);
            }
            out.controls.brake=final;return finish();
        }
        const Vec3d up=std::abs(Vec3d::Dot(delta.Normalized(),siteNormal_))<0.9?siteNormal_:siteForward_;
        const double angle=Attitude(state,delta.Normalized(),up,out.controls);
        // Stop before substantial heading changes: finite turn rate cannot bend
        // a 50c trajectory around a planet. Shortcut chords remain outside 2R.
        if(angle>(final?0.025:0.45)) out.controls.brake=true;
        else {
            // Local maneuver descent uses the normal 300m/s mode, after reaching
            // the exterior lunar radial. Cruise conservatively clamps to 30m/s
            // below the global maximum mountain even over the measured valley.
            const bool local=(phase_==0&&final&&distance<15000)||
                (phase_==5&&waypoint_==0&&simulation.Telemetry("moon").surfaceAltitudeMeters<15000);
            if(local) out.mode=FlightMode::Maneuver;
            const double limit=local?simulation.Config().maxManeuverSpeedMps:std::min(simulation.Config().maxCruiseSpeedMps,simulation.ApproachSpeedLimitMps());
            const double brakeAcceleration=local?simulation.Config().maneuverAccelerationMps2*0.25:simulation.Config().cruiseBrakeAccelerationMps2;
            const double brakingLimit=std::sqrt(2*brakeAcceleration*std::max(0.0,distance-tolerance))*0.8;
            const double elapsed=state.simulationTimeSeconds-phaseTime_;
            const double ramp=Clamp(elapsed/12.0,0,1);
            const double engage=ramp*ramp*ramp*(10+ramp*(-15+6*ramp));
            double gentleApproach=simulation.Config().maxCruiseSpeedMps;
            for(const auto& body:simulation.Bodies()) {
                const double shell=body.radiusMeters+std::max(body.atmosphereHeightMeters,body.terrainMaxHeightMeters);
                gentleApproach=std::min(gentleApproach,30.0+std::max(0.0,(state.positionMeters-body.centerMeters).Length()-shell)*0.22);
            }
            const double speed=std::min({local?limit:std::min(limit,gentleApproach),distance*(final?0.30:0.65),brakingLimit,50*SpeedOfLightMps})*engage;
            out.controls.throttle=limit>0?speed/limit:0;
        }
    } else if(phase_==1||phase_==2) {
        out.stage=phase_==1?"moon_approach_level":flyoverComplete_?"moon_descent":"moon_valley_flyover";
        out.mode=flyoverComplete_?FlightMode::Landing:FlightMode::Maneuver;
        out.gearDeployedDesired=flyoverComplete_;
        if(state.mode==FlightMode::Landed) {next(3);return finish();}
        const auto radial=(state.positionMeters-moon_.centerMeters).Normalized();TerrainSample sample;
        if(!terrain_(moon_,radial,sample)) {failure_="Landing terrain sample unavailable";out.controls.brake=true;return finish();}
        if(!std::isfinite(sample.heightMeters)||!sample.normalSimulation.IsFinite()||sample.normalSimulation.Length()<0.5) {
            failure_="Invalid measured landing terrain";return finish();
        }
        const auto normal=sample.normalSimulation.Normalized();
        // During the flyover, +X is the local east tangent and +Z is radial up.
        // Terrain gradients set clearance, not rolling/pitching every DEM cell.
        // Only the slow final descent adopts the actual contact normal.
        const auto forward=(siteForward_-radial*Vec3d::Dot(siteForward_,radial)).Normalized();
        const double angle=Attitude(state,forward,flyoverComplete_?normal:radial,out.controls);
        if(phase_==1) {
            out.controls.brake=true;
            // A single near-zero crossing can retain angular momentum. Require
            // continuous settling before forward translation at every cadence.
            if(angle<0.005&&state.velocityMetersPerSecond.Length()<0.05) {
                if(attitudeSettledSince_<0) attitudeSettledSince_=state.simulationTimeSeconds;
                if(state.simulationTimeSeconds-attitudeSettledSince_>=0.75) next(2);
            } else attitudeSettledSince_=-1;
        } else if(!flyoverComplete_) {
            const auto delta=sitePosition_-state.positionMeters;
            const double remaining=std::max(0.0,Vec3d::Dot(delta,forward));
            const double height=simulation.Telemetry("moon").surfaceAltitudeMeters;
            const double speed=state.velocityMetersPerSecond.Length();
            // Reserve a full servo settling interval plus conservative braking,
            // and sample a 60 m wide corridor at 25 m spacing. Swept collision
            // still validates every ordinary simulation step independently.
            const double reserve=std::min(remaining,speed*speed/(2*simulation.Config().maneuverAccelerationMps2*0.25)+speed*2+35);
            const auto right=Vec3d::Cross(forward,radial).Normalized();
            double corridorHeight=sample.heightMeters;
            for(double ahead=0;ahead<=reserve+24.999;ahead+=25) {
                for(double across:{-30.0,0.0,30.0}) {
                    const auto probe=(state.positionMeters-moon_.centerMeters+forward*std::min(ahead,reserve)+right*across).Normalized();
                    TerrainSample terrain;
                    if(!terrain_(moon_,probe,terrain)||!std::isfinite(terrain.heightMeters)) {failure_="Flyover clearance sample unavailable";return finish();}
                    corridorHeight=std::max(corridorHeight,terrain.heightMeters);
                }
            }
            const double siteHeight=(sitePosition_-moon_.centerMeters).Length()-moon_.radiusMeters;
            const double desiredHeight=std::max(siteHeight+25+remaining*0.20,corridorHeight+25+std::min(55.0,remaining*0.15));
            const double radialHeight=(state.positionMeters-moon_.centerMeters).Length()-moon_.radiusMeters;
            const double vertical=Clamp((desiredHeight-radialHeight)*0.45,-10,10);
            const double brakingLimit=std::sqrt(2*simulation.Config().maneuverAccelerationMps2*0.25*remaining)*0.65;
            const double translationSpeed=std::min({45.0,remaining*0.30,brakingLimit});
            if(angle>0.03||height<15) out.controls.brake=true;
            else {
                out.controls.throttle=translationSpeed/simulation.Config().maxManeuverSpeedMps;
                out.controls.strafeRight=Clamp(Vec3d::Dot(delta,Right(state.orientation))*0.3/simulation.Config().strafeSpeedMps,-1,1);
                out.controls.strafeUp=vertical/simulation.Config().strafeSpeedMps;
            }
            if(remaining<1&&height<29&&speed<0.5) flyoverComplete_=true;
        } else if(angle<0.015) {
            // Flare over the measured surface. A 0.12m/s crawl guarantees contact
            // even when individual swept feet meet higher terrain than the center.
            const double height=simulation.Telemetry("moon").surfaceAltitudeMeters;
            const double gap=std::max(0.0,height-simulation.Config().landingClearanceMeters);
            const double descent=Clamp(gap*0.22,0.12,1.5);
            out.controls.strafeUp=-descent/simulation.Config().landingTranslationSpeedMps;
        }
        else out.controls.brake=true;
    } else if(phase_==3) {
        out.stage="moon_landed_scan";out.mode=FlightMode::Landing;out.gearDeployedDesired=true;out.scan=true;out.scanObjectiveId="moon_taurus_littrow";
        if(state.mode!=FlightMode::Landed) failure_="Landed state unexpectedly lost";
        if(actualScanComplete&&state.simulationTimeSeconds-phaseTime_>=8) next(4);
    } else if(phase_==4) {
        out.stage="moon_takeoff";out.mode=FlightMode::Landing;out.gearDeployedDesired=true;
        out.controls.takeoff=state.mode==FlightMode::Landed;out.controls.strafeUp=1;
        if(state.mode!=FlightMode::Landed&&simulation.Telemetry("moon").surfaceAltitudeMeters>=15) {
            waypoints_.clear();waypoint_=0;
            const auto radial=(state.positionMeters-moon_.centerMeters).Normalized();
            const auto toSaturn=(saturn_.centerMeters-moon_.centerMeters).Normalized();
            waypoints_.push_back({moon_.centerMeters+radial*(moon_.radiusMeters*3),"moon",moon_.radiusMeters*0.15});
            Arc(moon_,radial,toSaturn,moon_.radiusMeters*3);
            const auto saturnIncoming=(moon_.centerMeters-saturn_.centerMeters).Normalized();
            const auto ringRadial=(ringPosition_-saturn_.centerMeters).Normalized();
            waypoints_.push_back({saturn_.centerMeters+saturnIncoming*(saturn_.radiusMeters*3),"saturn",saturn_.radiusMeters*0.15});
            Arc(saturn_,saturnIncoming,ringRadial,saturn_.radiusMeters*3);
            waypoints_.push_back({ringPosition_,"saturn"});next(5);
        }
    } else {
        out.stage=phase_==7?"complete":"saturn_rings_scan";out.mode=FlightMode::Maneuver;out.controls.brake=true;
        const double angle=Attitude(state,(ringLook_-state.positionMeters).Normalized(),saturn_.bodyFixedToSimulation.Rotate({1,0,0}),out.controls);
        out.scan=angle<0.005;out.scanObjectiveId="saturn_rings";
        if(phase_==6&&actualScanComplete&&out.scan&&state.simulationTimeSeconds-phaseTime_>=12) next(7);
        out.complete=phase_==7;
    }
    return finish();
}
}
