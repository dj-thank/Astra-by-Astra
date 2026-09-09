#pragma once
#include "Simulation/FlightSimulation.h"

namespace star::navigation {
enum class NavigationStatus {
    Idle, InvalidTarget, AwaitingConfirmation, Aligning, ReadyToConfirm,
    TransferAuthorized, Departing, Travelling, Braking, LocalHold, Arrived,
    Paused, Cancelled, RouteBlocked, StateChanged
};

struct NavigationCommand {
    FlightInput controls;
    FlightMode mode = FlightMode::Maneuver;
    NavigationStatus status = NavigationStatus::Idle;
    bool autopilotActive = false;
    bool highSpeedAuthorized = false;
    // Caller must pause an old/unconfirmed high-energy state. Never zero its
    // velocity or continue applying old throttle while showing confirmation UI.
    bool requiresSafetyPause = false;
    bool canConfirmTransfer = false;
    std::string destinationBodyId;
    std::string localBodyId;
    double headingErrorDegrees = 180.0;
    double desiredSpeedMps = 0.0;
    Vec3d guidancePointMeters;
};

// Transient permission and ordinary-input guidance only. Never mutates the
// simulation, saves authorization, teleports, or assigns velocity/orientation.
// All methods belong to the runtime's one simulation owner.
class NavigationPilot {
public:
    // Call AFTER simulation.SetTarget succeeds. Changing/invalid selection
    // clears authorization and autopilot; selecting an unchanged target is inert.
    bool SelectDestination(const FlightSimulation& simulation, const std::string& id);
    bool ConfirmTransfer(const FlightSimulation& simulation);
    bool StartAutopilot(const FlightSimulation& simulation);
    void Cancel(); // Manual override / stop: revokes permission, retains selection.
    void SetPaused(bool paused); // Pause revokes permission; resume never rearms.
    void ResetAfterLoad(); // Required after every restore, even same timestamp/target.
    // Only the clock owner may call this for validated continuous ephemerides.
    // Manual date selection still calls ResetAfterLoad and revokes permission.
    void FollowCelestialFrames(const FlightSimulation& simulation);
    // Call every frame, including manual flight, to validate permission. Inactive
    // controls are not pilot input; caller uses its manual input with the gate.
    NavigationCommand Tick(const FlightSimulation& simulation);
    static constexpr double ConfirmationConeDegrees = 15.0;
    static double StandOffMeters(const BodyDefinition& body);

private:
    bool Validate(const FlightSimulation& simulation);
    bool CanConfirm(const FlightSimulation& simulation) const;
    bool BuildRoute(const FlightSimulation& simulation);
    void Revoke(NavigationStatus reason);
    const BodyDefinition* NearestBody(const FlightSimulation& simulation) const;
    std::string destination_;
    std::string authorizedDestination_;
    std::vector<BodyDefinition> selectedBodies_;
    std::vector<Vec3d> route_;
    std::size_t waypoint_ = 0;
    bool autopilot_ = false;
    bool paused_ = false;
    bool localHold_ = false;
    bool arrived_ = false;
    Vec3d holdForward_{1,0,0};
    Vec3d holdUp_{0,0,1};
    double lastTime_ = -1;
    double authorizationTime_ = -1;
    std::uint64_t recoveryCount_ = 0;
    NavigationStatus status_ = NavigationStatus::Idle;
};
} // namespace star::navigation
