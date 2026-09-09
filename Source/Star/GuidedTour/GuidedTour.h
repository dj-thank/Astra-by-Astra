#pragma once
#include "Simulation/FlightSimulation.h"

namespace star::guided {
// Product tour intent only. Caller applies ordinary controls and may cancel any time.
struct TourCommand {
    FlightInput controls;
    std::string targetBodyId;
    FlightMode mode = FlightMode::Maneuver;
    bool gearDeployedDesired = false;
    bool scan = false;
    std::string scanObjectiveId;
    std::string stage;
    std::string stageTitle;
    double progress = 0;
    Vec3d lookAtMeters;
    bool failed = false;
    bool complete = false;
    std::string failure;
};
class GuidedTour {
public:
    GuidedTour(const std::vector<BodyDefinition>& bodies, TerrainSampler terrain);
    TourCommand Tick(const FlightSimulation& simulation, bool actualScanComplete);
private:
    TourCommand BuildCommand(const FlightSimulation& simulation, bool actualScanComplete);
    double lastTickTime_ = -1;
    TourCommand cachedCommand_;
    struct Waypoint { Vec3d position; std::string body; double passRadius = 0; };
    void Arc(const BodyDefinition& body, Vec3d from, Vec3d to, double radius);
    void Initialize(const FlightState& state);
    BodyDefinition earth_, moon_, saturn_;
    TerrainSampler terrain_;
    std::vector<Waypoint> waypoints_;
    std::size_t waypoint_ = 0;
    Vec3d siteRadial_, sitePosition_, siteNormal_, siteForward_, ringPosition_, ringLook_;
    int phase_ = -1;
    double startTime_ = -1, phaseTime_ = -1;
    std::uint64_t initialRecoveries_ = 0;
    bool flyoverComplete_ = false;
    double attitudeSettledSince_ = -1;
    std::string failure_;
};
}
