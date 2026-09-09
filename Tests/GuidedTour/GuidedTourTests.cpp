#include "GuidedTour/GuidedTour.h"
#include "Exploration/StarExplorationCore.h"
#include "Validation/RealDem.h"
#include <map>

int main(int argc,char** argv) {
 try {
    Require(argc==4||argc==5,"data directory, DEM metadata, body fixture, optional FPS required");
    const double fps=argc==5?std::stod(argv[4]):30;
    Require(fps>=15&&fps<=120,"test FPS range");
    const double dt=1.0/fps;
    RealData dem(argv[1],argv[2]);
    const int siteX=static_cast<int>(std::floor((30.7717-dem.west)/(dem.east-dem.west)*dem.rw-0.5));
    const int siteY=static_cast<int>(std::floor((dem.north-20.1908)/(dem.north-dem.south)*dem.rh-0.5));
    Require(siteX>=0&&siteY>=0&&siteX+1<dem.rw&&siteY+1<dem.rh,"Apollo17 within regional source");
    for(int y=siteY;y<=siteY+1;++y) for(int x=siteX;x<=siteX+1;++x) {
        const auto id=static_cast<std::size_t>(y)*dem.rw+x;
        Require(dem.mask[id]!=0&&std::isfinite(dem.region[id]),"Apollo17 real measured four-cell support");
    }
    std::vector<BodyDefinition> bodies;std::ifstream input(argv[3]);
    std::size_t count=0;input>>count;
    for(std::size_t i=0;i<count;++i) {
        BodyDefinition b;input>>b.id>>b.radiusMeters>>b.centerMeters.x>>b.centerMeters.y>>b.centerMeters.z;
        double m[9];for(auto& v:m) input>>v;
        b.bodyFixedToSimulation=Quatd::FromForwardUp({m[0],m[3],m[6]},{m[2],m[5],m[8]});
        b.landable=b.id=="moon";b.atmosphereHeightMeters=b.id=="earth"?100000:b.id=="saturn"?150000:0;
        if(b.landable) { b.terrainMinHeightMeters=-8991.5;b.terrainMaxHeightMeters=10695.5; }
        bodies.push_back(b);
    }
    Require(!input.fail(),"dated body fixture parsed");
    std::size_t regionalSamples=0;
    bool requireMeasuredCorridor=false,rejectTerrain=false;
    std::size_t verifiedCorridorSamples=0;
    TerrainSampler sampler=[&](const BodyDefinition& b,const Vec3d& direction,TerrainSample& out) {
        if(b.id!="moon") return false;
        if(rejectTerrain) return false;
        const auto local=b.bodyFixedToSimulation.Conjugate().Rotate(direction).Normalized();
        const double lat=std::asin(Clamp(local.z,-1,1))*180/Pi,lon=std::atan2(local.y,local.x)*180/Pi;
        if(requireMeasuredCorridor) {
            const int x=static_cast<int>(std::floor((lon-dem.west)/(dem.east-dem.west)*dem.rw-0.5));
            const int y=static_cast<int>(std::floor((dem.north-lat)/(dem.north-dem.south)*dem.rh-0.5));
            Require(x>=0&&y>=0&&x+1<dem.rw&&y+1<dem.rh,"entire flyover corridor is inside regional DEM");
            for(int row=y;row<=y+1;++row) for(int col=x;col<=x+1;++col) {
                const auto index=static_cast<std::size_t>(row)*dem.rw+col;
                Require(dem.mask[index]!=0&&std::isfinite(dem.region[index]),"flyover samples have four measured support cells");
            }
            ++verifiedCorridorSamples;
        }
        out.heightMeters=dem.Height(lat,lon);
        if(lat>dem.south&&lat<dem.north&&lon>dem.west&&lon<dem.east) ++regionalSamples;
        const double step=5/b.radiusMeters*180/Pi,lonStep=step/std::max(0.01,std::cos(lat*Pi/180));
        const double eg=(dem.Height(lat,lon+lonStep)-dem.Height(lat,lon-lonStep))/10;
        const double ng=(dem.Height(std::min(90.0,lat+step),lon)-dem.Height(std::max(-90.0,lat-step),lon))/10;
        const double la=lat*Pi/180,lo=lon*Pi/180;
        const Vec3d east{-std::sin(lo),std::cos(lo),0},north{-std::sin(la)*std::cos(lo),-std::sin(la)*std::sin(lo),std::cos(la)};
        out.normalSimulation=b.bodyFixedToSimulation.Rotate((local-east*eg-north*ng).Normalized());return true;
    };
    FlightSimulation simulation(bodies,{},sampler);
    simulation.SetFlightAssistEnabled(false);
    const auto* earth=simulation.FindBody("earth");const auto* sun=simulation.FindBody("sun");
    Require(earth&&sun,"required bodies");
    // Exact AStarWorldDirector::InitialFlightState construction.
    const auto towardSun=(sun->centerMeters-earth->centerMeters).Normalized();
    const auto pole=earth->bodyFixedToSimulation.Rotate({0,0,1});
    const auto east=Vec3d::Cross(pole,towardSun).Normalized();
    const auto radial=(-east+towardSun*0.14+pole*0.20).Normalized();
    FlightState initial;initial.positionMeters=earth->centerMeters+radial*(earth->radiusMeters+450000);
    initial.orientation=Quatd::FromForwardUp((Vec3d::Cross(radial,towardSun).Normalized()-radial*0.35).Normalized(),radial);
    initial.targetBodyId="moon";Require(simulation.RestoreState(initial),"exact initial setup");
    guided::GuidedTour pilot(bodies,sampler);exploration::Tracker tracker;
    std::map<std::string,double> stageStarts;
    double peakSpeed=0,lastProgress=0;
    std::ofstream motion("motion.csv");
    motion << "time,stage,speed,acceleration,jerk,moon_height,earth_height,contact_down,mode,latitude,longitude,pitch_degrees,up_tilt_degrees,down_mps\n";
    Vec3d previousAcceleration;
    double peakAcceleration=0,peakJerk=0,touchdownSpeed=0;
    double flyoverDistance=0,flyoverMinHeight=1e20,flyoverMaxHeight=0,flyoverMaxPitch=0,flyoverMaxTilt=0,flyoverMaxDown=0;
    guided::GuidedTour missing({},sampler);Require(missing.Tick(simulation,false).failed,"missing bodies fail closed");
    guided::GuidedTour missingTerrain(bodies,{});Require(missingTerrain.Tick(simulation,false).failed,"missing terrain fails closed");
    std::string lastStage,objective;bool landed=false,tookOff=false,complete=false,saved=false;
    for(std::uint64_t frame=1;frame<216001;++frame) {
        const bool scanComplete=objective=="moon_taurus_littrow"?tracker.Complete(3):objective=="saturn_rings"?tracker.Complete(4):false;
        const auto before=SerializeFlightState(simulation.State());
        const auto command=pilot.Tick(simulation,scanComplete);
        Require(before==SerializeFlightState(simulation.State()),"pilot does not mutate flight");
        Require(!command.stageTitle.empty()&&command.lookAtMeters.IsFinite(),"all commands carry presentation metadata");
        if(command.stage=="earth_to_moon") Require(command.targetBodyId=="moon","Earth departure exposes Moon as the tour destination");
        if(command.stage=="moon_to_saturn") Require(command.targetBodyId=="saturn","Moon departure exposes Saturn as the tour destination");
        Require(command.progress>=lastProgress&&command.progress<=1,"monotonic bounded progress");lastProgress=command.progress;
        if(command.stage!=lastStage) {
            stageStarts[command.stage]=simulation.State().simulationTimeSeconds;
            if(command.stage=="moon_landed_scan") {
                auto waiting=simulation;auto waitingPilot=pilot;
                for(int waitFrame=0;waitFrame<static_cast<int>(20*fps);++waitFrame) {
                    auto waitCommand=waitingPilot.Tick(waiting,false);
                    Require(!waitCommand.complete&&!waitCommand.failed&&waitCommand.stage=="moon_landed_scan","scan acknowledgement required before takeoff");
                    waiting.SetTarget(waitCommand.targetBodyId);waiting.SetMode(waitCommand.mode);waiting.SetGearDeployed(waitCommand.gearDeployedDesired);
                    waiting.Advance(dt,waitCommand.controls);
                    Require((waiting.State().positionMeters-simulation.State().positionMeters).Length()==0,"landed hold never changes position");
                }
            }
            if(command.stage=="moon_valley_flyover") {
                auto invalid=simulation;auto invalidPilot=pilot;
                FlightInput hold;hold.hasThrottle=true;hold.brake=true;hold.smoothGuidance=true;
                invalid.Advance(dt,hold);rejectTerrain=true;
                const auto failed=invalidPilot.Tick(invalid,false);rejectTerrain=false;
                Require(failed.failed&&failed.controls.brake&&!failed.scan,"missing live terrain safely stops scenic guidance");
            }
            for(int pausedFrame=0;pausedFrame<20;++pausedFrame) {
                const auto paused=pilot.Tick(simulation,!scanComplete);
                Require(paused.stage==command.stage&&paused.progress==command.progress&&paused.scan==command.scan,"paused tick holds route state");
            }
            // Cancellation is relinquishing the helper: ordinary manual controls
            // operate this exact state without restoring or assigning a position.
            auto manual=simulation;FlightInput takeover;takeover.hasThrottle=true;takeover.brake=true;takeover.yaw=0.25;
            for(int manualFrame=0;manualFrame<30;++manualFrame) manual.Advance(dt,takeover);
            Require(manual.State().recoveryCount==0,"manual takeover from each stage remains collision-free");
            Require(before==SerializeFlightState(simulation.State()),"takeover test does not mutate active tour");
            std::cout<<simulation.State().simulationTimeSeconds<<" stage="<<command.stage<<" altitudeMoon="<<simulation.Telemetry("moon").surfaceAltitudeMeters<<" speed="<<simulation.State().velocityMetersPerSecond.Length()<<std::endl;
            lastStage=command.stage;
        }
        if(command.failed) throw std::runtime_error(command.failure);
        if(command.complete) {complete=true;break;}
        requireMeasuredCorridor=command.stage=="moon_valley_flyover";
        objective=command.scanObjectiveId;
        simulation.SetTarget(command.targetBodyId);simulation.SetMode(command.mode);simulation.SetGearDeployed(command.gearDeployedDesired);
        const auto oldVelocity=simulation.State().velocityMetersPerSecond;
        const auto oldPosition=simulation.State().positionMeters;
        const auto advance=simulation.Advance(dt,command.controls);
        const auto acceleration=(simulation.State().velocityMetersPerSecond-oldVelocity)/dt;
        const double jerk=(acceleration-previousAcceleration).Length()/dt;
        previousAcceleration=acceleration;
        peakAcceleration=std::max(peakAcceleration,acceleration.Length());peakJerk=std::max(peakJerk,jerk);
        if(advance.contact.kind==ContactKind::Landed) touchdownSpeed=advance.contact.downwardSpeedMps;
        const auto moonTelemetry=simulation.Telemetry("moon");
        const auto moonRadial=(simulation.State().positionMeters-simulation.FindBody("moon")->centerMeters).Normalized();
        const double pitch=std::asin(Clamp(Vec3d::Dot(Forward(simulation.State().orientation),moonRadial),-1,1))*180/Pi;
        const double tilt=std::acos(Clamp(Vec3d::Dot(Up(simulation.State().orientation),moonRadial),-1,1))*180/Pi;
        motion << std::setprecision(15) << simulation.State().simulationTimeSeconds << ',' << command.stage << ',' << simulation.State().velocityMetersPerSecond.Length() << ',' << acceleration.Length() << ',' << jerk << ',' << moonTelemetry.surfaceAltitudeMeters << ',' << simulation.Telemetry("earth").surfaceAltitudeMeters << ',' << advance.contact.downwardSpeedMps << ',' << static_cast<int>(simulation.State().mode) << ',' << moonTelemetry.latitudeDegrees << ',' << moonTelemetry.longitudeDegrees << ',' << pitch << ',' << tilt << ',' << moonTelemetry.closingSpeedMps << '\n';
        if(requireMeasuredCorridor) {
            const auto step=simulation.State().positionMeters-oldPosition;
            flyoverDistance+=(step-moonRadial*Vec3d::Dot(step,moonRadial)).Length();
            flyoverMinHeight=std::min(flyoverMinHeight,moonTelemetry.surfaceAltitudeMeters);
            flyoverMaxHeight=std::max(flyoverMaxHeight,moonTelemetry.surfaceAltitudeMeters);
            flyoverMaxPitch=std::max(flyoverMaxPitch,std::abs(pitch));flyoverMaxTilt=std::max(flyoverMaxTilt,tilt);
            flyoverMaxDown=std::max(flyoverMaxDown,moonTelemetry.closingSpeedMps);
        }
        Require(advance.discardedSeconds==0,"no time skipping");
        const auto& s=simulation.State();
        peakSpeed=std::max(peakSpeed,s.velocityMetersPerSecond.Length());
        Require(peakSpeed<=50*SpeedOfLightMps+0.01,"fictional drive stays within disclosed 50c");
        if(s.recoveryCount) {
            std::ofstream receipt("recovery-flight-before.txt");receipt<<before;
            std::cerr<<"contact kind="<<static_cast<int>(advance.contact.kind)<<" down="<<advance.contact.downwardSpeedMps<<" lateral="<<advance.contact.lateralSpeedMps<<" slope="<<advance.contact.slopeDegrees<<" tilt="<<advance.contact.tiltDegrees<<" time="<<s.simulationTimeSeconds<<std::endl;
        }
        Require(s.recoveryCount==0,"no recovery");
        const auto tele=simulation.Telemetry(command.targetBodyId);
        if(s.mode==FlightMode::Landed) landed=true;
        if(landed&&s.mode!=FlightMode::Landed&&simulation.Telemetry("moon").surfaceAltitudeMeters>=20) tookOff=true;
        exploration::Sample sample;sample.body=command.targetBodyId=="moon"?exploration::Body::Moon:command.targetBodyId=="saturn"?exploration::Body::Saturn:exploration::Body::Earth;
        sample.sequence=frame;sample.deltaSeconds=dt;sample.altitudeM=tele.surfaceAltitudeMeters;sample.latitudeDeg=tele.latitudeDegrees;sample.longitudeDeg=tele.longitudeDegrees;
        sample.speedMps=s.velocityMetersPerSecond.Length();sample.verticalSpeedMps=-tele.closingSpeedMps;sample.landed=s.mode==FlightMode::Landed;sample.scanning=command.scan;
        if(command.targetBodyId=="saturn") {
            const auto* saturn=simulation.FindBody("saturn");
            const auto local=saturn->bodyFixedToSimulation.Conjugate().Rotate(s.positionMeters-saturn->centerMeters);
            const auto direction=saturn->bodyFixedToSimulation.Conjugate().Rotate(Forward(s.orientation));
            if(std::abs(direction.z)>1e-12) {const double t=-local.z/direction.z;if(t>0){sample.ringRadiusM=(local+direction*t).Length();sample.viewingRings=sample.ringRadiusM>=74500000&&sample.ringRadiusM<=140300000;}}
        }
        tracker.Observe(sample);
        if(tracker.Complete(3)&&!saved) {
            FlightState loaded;Require(DeserializeFlightState(SerializeFlightState(s),loaded),"real landed save parse");
            FlightSimulation restart(bodies,{},sampler);Require(restart.RestoreState(loaded),"real landed restart");
            Require(restart.State().mode==FlightMode::Landed,"restart preserves contact state");
            exploration::Tracker loadedTracker;Require(loadedTracker.Restore(tracker.State())&&loadedTracker.Complete(3),"restart preserves real scan");saved=true;
        }
    }
    Require(complete&&landed&&tookOff&&saved&&tracker.Complete(4),"full route and scan/save restart");
    Require(stageStarts["earth_to_moon"]-stageStarts["earth_view"]>=8,"Earth view is held for eight real simulation seconds");
    Require(stageStarts["moon_takeoff"]-stageStarts["moon_landed_scan"]>=8,"landed viewing hold");
    Require(simulation.State().simulationTimeSeconds-stageStarts["saturn_rings_scan"]>=12,"Saturn viewing hold");
    Require(simulation.State().simulationTimeSeconds<720,"smooth tour stays below twelve minutes");
    Require(touchdownSpeed<0.20,"real DEM flare reaches a gentle touchdown");
    Require(regionalSamples>0,"real regional DEM sampled");
    Require(verifiedCorridorSamples>0,"live flyover corridor uses measured DTM, never global fallback");
    Require(stageStarts["moon_valley_flyover"]-stageStarts["moon_approach_level"]<12,"high approach attitude settles without a long empty dwell");
    Require(stageStarts["moon_descent"]-stageStarts["moon_valley_flyover"]>=50,"meaningful moving terrain view lasts at least fifty seconds");
    Require(flyoverDistance>2900&&flyoverDistance<3100,"continuous three-kilometer horizontal scenic flight");
    Require(flyoverMinHeight>20&&flyoverMaxHeight>600&&flyoverMaxHeight<750,"scenic approach descends from medium altitude with safe measured clearance");
    Require(flyoverMaxPitch<0.5&&flyoverMaxTilt<0.5,"ship stays horizontal to lunar tangent throughout flyover");
    Require(flyoverMaxDown<10.5,"controlled scenic descent stays inside vertical command envelope");
    std::ofstream profile("route-profile.json");
    profile<<std::setprecision(12)<<"{\"fps\":"<<fps<<",\"durationSeconds\":"<<simulation.State().simulationTimeSeconds<<",\"flyoverSeconds\":"<<stageStarts["moon_descent"]-stageStarts["moon_valley_flyover"]<<",\"flyoverMeters\":"<<flyoverDistance<<",\"minimumFlyoverAglMeters\":"<<flyoverMinHeight<<",\"maximumFlyoverAglMeters\":"<<flyoverMaxHeight<<",\"maximumFlyoverPitchDegrees\":"<<flyoverMaxPitch<<",\"maximumFlyoverUpTiltDegrees\":"<<flyoverMaxTilt<<",\"maximumFlyoverDescentMps\":"<<flyoverMaxDown<<",\"touchdownMps\":"<<touchdownSpeed<<",\"recoveries\":"<<simulation.State().recoveryCount<<",\"measuredCorridorSamples\":"<<verifiedCorridorSamples<<",\"moonScanSaveRestart\":true,\"ringScan\":true,\"gpuVerified\":false}\n";
    std::cout<<"scenic distance="<<flyoverDistance<<" height="<<flyoverMinHeight<<".."<<flyoverMaxHeight<<" pitch="<<flyoverMaxPitch<<" tilt="<<flyoverMaxTilt<<" down="<<flyoverMaxDown<<std::endl;
    std::cout<<"motion peakAcceleration="<<peakAcceleration<<" peakJerk="<<peakJerk<<" touchdownSpeed="<<touchdownSpeed<<std::endl;
    std::cout<<"PASS continuous route; regional DEM calls="<<regionalSamples<<" simulationSeconds="<<simulation.State().simulationTimeSeconds<<" FPS="<<fps<<" peakC="<<peakSpeed/SpeedOfLightMps<<std::endl;
    return 0;
 } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<std::endl;return 1; }
}
