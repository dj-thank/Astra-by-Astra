#pragma once
#include "Simulation/FlightSimulation.h"

namespace star {
// Persistence payload only. Root owns serialization/versioning and must first
// restore the original parked ship, then Initialize this model from that ship.
struct LunarWalkSaveState {
    Vec3d feetMeters;
    Vec3d headingSimulation;
    Vec3d boardingPointMeters;
    double pitchRadians = 0;
};
// Feet are constrained to the measured DEM; no synthetic visual grain or clast
// is treated as a physical observation. This first EVA slice has no jumping.
class LunarWalkModel {
public:
    void UpdateCelestialFrame(const BodyDefinition& moon);
    bool Initialize(const FlightState& ship, const BodyDefinition& moon,
                    TerrainSampler terrain, const FlightConfig& hull = {});
    // right/forward normalized input, look deltas in degrees (positive right/up).
    void Advance(double dt, double right, double forward, double yawDegrees,
                 double pitchDegrees, bool paused);
    bool CanBoard(const FlightState& ship) const;
    bool IsSameParkedShip(const FlightState& ship) const;
    LunarWalkSaveState ExportSaveState() const;
    // Load-only, transactional validation; never a travel/input operation.
    bool RestoreSaveState(const LunarWalkSaveState& saved);
    bool IsReady() const { return ready_; }
    bool MovementBlocked() const { return blocked_; }
    Vec3d Position() const { return feet_; }
    Vec3d Camera() const { return feet_ + UpDirection() * 1.7; }
    Vec3d UpDirection() const;
    Vec3d CameraForward() const;
    Vec3d CameraRight() const;
    Vec3d CameraUp() const;
    double DistanceToBoardingPoint() const { return (feet_ - boardingPoint_).Length(); }
private:
    bool Ground(const Vec3d& direction, Vec3d& feet, double& height) const;
    bool OutsideHull(const Vec3d& feet) const;
    BodyDefinition moon_;
    FlightState parkedShip_;
    FlightConfig hull_;
    TerrainSampler terrain_;
    Vec3d feet_, heading_, boardingPoint_;
    double height_ = 0.0, pitchRadians_ = 0.0;
    bool ready_ = false, blocked_ = false;
};
}
