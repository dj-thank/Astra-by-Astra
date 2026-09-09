#include "Validation/RoutePilot.h"
#include "Exploration/StarExplorationCore.h"
#include "RealDem.h"

int main(int argc,char** argv) {
 try {
    Require(argc==4,"data directory, DEM metadata, body fixture required");
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
    TerrainSampler sampler=[&](const BodyDefinition& b,const Vec3d& direction,TerrainSample& out) {
        if(b.id!="moon") return false;
        const auto local=b.bodyFixedToSimulation.Conjugate().Rotate(direction).Normalized();
        const double lat=std::asin(Clamp(local.z,-1,1))*180/Pi,lon=std::atan2(local.y,local.x)*180/Pi;
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
    validation::RoutePilot pilot(bodies,sampler);exploration::Tracker tracker;
    std::string lastStage,objective;bool landed=false,tookOff=false,complete=false,saved=false;
    for(std::uint64_t frame=1;frame<216001;++frame) {
        const bool scanComplete=objective=="moon_taurus_littrow"?tracker.Complete(3):objective=="saturn_rings"?tracker.Complete(4):false;
        const auto before=SerializeFlightState(simulation.State());
        const auto command=pilot.Tick(simulation,scanComplete);
        Require(before==SerializeFlightState(simulation.State()),"pilot does not mutate flight");
        if(command.stage!=lastStage) {
            std::cout<<simulation.State().simulationTimeSeconds<<" stage="<<command.stage<<" altitudeMoon="<<simulation.Telemetry("moon").surfaceAltitudeMeters<<" speed="<<simulation.State().velocityMetersPerSecond.Length()<<std::endl;
            lastStage=command.stage;
        }
        if(command.failed) throw std::runtime_error(command.failure);
        if(command.complete) {complete=true;break;}
        objective=command.scanObjectiveId;
        simulation.SetTarget(command.targetBodyId);simulation.SetMode(command.mode);simulation.SetGearDeployed(command.gearDeployedDesired);
        const auto advance=simulation.Advance(1.0/30,command.controls);
        Require(advance.discardedSeconds==0,"no time skipping");
        const auto& s=simulation.State();
        if(s.recoveryCount) {
            std::ofstream receipt("recovery-flight-before.txt");receipt<<before;
            std::cerr<<"contact kind="<<static_cast<int>(advance.contact.kind)<<" down="<<advance.contact.downwardSpeedMps<<" lateral="<<advance.contact.lateralSpeedMps<<" slope="<<advance.contact.slopeDegrees<<" tilt="<<advance.contact.tiltDegrees<<" time="<<s.simulationTimeSeconds<<std::endl;
        }
        Require(s.recoveryCount==0,"no recovery");
        const auto tele=simulation.Telemetry(command.targetBodyId);
        if(s.mode==FlightMode::Landed) landed=true;
        if(landed&&s.mode!=FlightMode::Landed&&simulation.Telemetry("moon").surfaceAltitudeMeters>=20) tookOff=true;
        exploration::Sample sample;sample.body=command.targetBodyId=="moon"?exploration::Body::Moon:command.targetBodyId=="saturn"?exploration::Body::Saturn:exploration::Body::Earth;
        sample.sequence=frame;sample.deltaSeconds=1.0/30;sample.altitudeM=tele.surfaceAltitudeMeters;sample.latitudeDeg=tele.latitudeDegrees;sample.longitudeDeg=tele.longitudeDegrees;
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
    Require(regionalSamples>0,"real regional DEM sampled");
    std::cout<<"PASS continuous route; regional DEM calls="<<regionalSamples<<" simulationSeconds="<<simulation.State().simulationTimeSeconds<<std::endl;
    return 0;
 } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<std::endl;return 1; }
}
