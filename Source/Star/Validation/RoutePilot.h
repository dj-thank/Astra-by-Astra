#pragma once
#include "Simulation/FlightSimulation.h"

namespace star::validation {
// Acceptance-driver intent only. The caller applies controls through normal runtime APIs.
struct RouteCommand {
    FlightInput controls;
    std::string targetBodyId;
    FlightMode mode = FlightMode::Maneuver;
    bool gearDeployedDesired = false;
    bool scan = false;
    std::string scanObjectiveId;
    std::string stage;
    bool failed = false;
    bool complete = false;
    std::string failure;
};
class RoutePilot {
public:
    RoutePilot(const std::vector<BodyDefinition>& bodies, TerrainSampler terrain);
    RouteCommand Tick(const FlightSimulation& simulation, bool actualScanComplete);
private:
    struct Waypoint { Vec3d position; std::string body; };
    void Arc(const BodyDefinition& body, Vec3d from, Vec3d to, double radius);
    void Initialize(const FlightState& state);
    BodyDefinition earth_, moon_, saturn_;
    TerrainSampler terrain_;
    std::vector<Waypoint> waypoints_;
    std::size_t waypoint_ = 0;
    Vec3d siteRadial_, sitePosition_, siteNormal_, siteForward_, ringPosition_, ringLook_;
    int phase_ = 0;
    double startTime_ = -1, phaseTime_ = -1;
    std::uint64_t initialRecoveries_ = 0;
    std::string failure_;
};
}
