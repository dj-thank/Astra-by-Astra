#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// No Unreal types: all astronomical positions are right-handed J2000 meters.
namespace star {
constexpr double SpeedOfLightMps = 299792458.0;
constexpr double Pi = 3.1415926535897932384626433832795;

struct Vec3d {
    double x = 0.0, y = 0.0, z = 0.0;
    Vec3d operator+(const Vec3d& b) const;
    Vec3d operator-(const Vec3d& b) const;
    Vec3d operator-() const;
    Vec3d operator*(double s) const;
    Vec3d operator/(double s) const;
    Vec3d& operator+=(const Vec3d& b);
    double Length() const;
    double LengthSquared() const;
    bool IsFinite() const;
    Vec3d Normalized(Vec3d fallback = {1.0, 0.0, 0.0}) const;
    static double Dot(const Vec3d& a, const Vec3d& b);
    static Vec3d Cross(const Vec3d& a, const Vec3d& b);
};

struct Quatd {
    double w = 1.0, x = 0.0, y = 0.0, z = 0.0;
    Quatd operator*(const Quatd& b) const;
    Quatd Conjugate() const;
    Quatd Normalized() const;
    bool IsFinite() const;
    Vec3d Rotate(const Vec3d& v) const;
    static Quatd FromAxisAngle(const Vec3d& axis, double radians);
    static Quatd FromForwardUp(const Vec3d& forward, const Vec3d& up);
};

// Body-local flight controls use +X forward, +Y right, +Z up (UE convention).
// A quaternion is right-handed: its internal +Y points LEFT. Use these helpers.
Vec3d Forward(const Quatd& orientation);
Vec3d Right(const Quatd& orientation);
Vec3d Up(const Quatd& orientation);
Vec3d SimulationDirectionToUnreal(const Vec3d& direction);
Vec3d UnrealDirectionToSimulation(const Vec3d& direction);
Vec3d ToUnrealCentimeters(const Vec3d& absoluteMeters, const Vec3d& originMeters);
Vec3d FromUnrealCentimeters(const Vec3d& localCentimeters, const Vec3d& originMeters);

struct BodyDefinition {
    std::string id;
    Vec3d centerMeters;
    double radiusMeters = 1.0;
    double atmosphereHeightMeters = 0.0; // Non-landable collision shell.
    bool landable = false;
    double terrainMinHeightMeters = 0.0;
    double terrainMaxHeightMeters = 0.0;
    double terrainMaxSlope = 4.0; // Conservative |d height / d surface distance| bound.
    Quatd bodyFixedToSimulation; // Datum/rotation supplied by dated body data.
};

struct TerrainSample {
    double heightMeters = 0.0; // Relative to BodyDefinition::radiusMeters.
    Vec3d normalSimulation;    // Outward, world/simulation-space unit normal.
};
// Callback receives a unit radial direction in simulation space. Return false
// when unavailable. Missing/invalid data conservatively uses terrainMaxHeight.
using TerrainSampler = std::function<bool(const BodyDefinition&, const Vec3d&, TerrainSample&)>;

enum class FlightMode : std::uint8_t { Maneuver, Landing, Cruise, Landed, LocalCruise };
enum class ContactKind : std::uint8_t { None, Landed, Recovered, TerrainSafetyLimit };

struct FlightInput {
    double yaw = 0.0;       // Positive: nose right.
    double pitch = 0.0;     // Positive: nose up.
    double roll = 0.0;      // Positive: bank right.
    double strafeRight = 0.0;
    double strafeUp = 0.0;
    double throttle = 0.0;
    bool hasThrottle = false; // False retains the previous throttle setting.
    bool brake = false;
    bool takeoff = false;
    // Opt-in tour velocity servo. Manual controls/emergency braking keep the
    // original response. Does not change save state or collision resolution.
    bool smoothGuidance = false;
    bool paused = false;      // Clears accumulated frame time; advances nothing.
};

struct FlightState {
    Vec3d positionMeters;
    Vec3d velocityMetersPerSecond;
    Quatd orientation;
    FlightMode mode = FlightMode::Maneuver;
    double throttle = 0.0;
    bool gearDeployed = false;
    bool throttleNeutralRequired = false; // Contact recovery waits for throttle <= 2%.
    std::string targetBodyId;
    std::string landedBodyId;
    std::uint64_t recoveryCount = 0;
    double simulationTimeSeconds = 0.0;
};

struct FlightConfig {
    double fixedStepSeconds = 1.0 / 120.0;
    double maxFrameSeconds = 0.25; // Excess wall time is reported, never fast-forwarded.
    double maxManeuverSpeedMps = 300.0;
    double maxLocalCruiseSpeedMps = 100000.0;
    double localCruiseAccelerationMps2 = 20000.0;
    double localCruiseBrakeMps2 = 40000.0;
    double maxLandingSpeedMps = 30.0;
    double maxCruiseSpeedMps = 50.0 * SpeedOfLightMps;
    double maneuverAccelerationMps2 = 45.0;
    double cruiseAccelerationMps2 = 2.0e9;
    double brakeAccelerationMps2 = 150.0;
    double cruiseBrakeAccelerationMps2 = 1.0e10;
    double yawRateRadiansPerSecond = 0.60;
    double pitchRateRadiansPerSecond = 0.55;
    double rollRateRadiansPerSecond = 0.80;
    double angularResponseSeconds = 0.22; // Smooth stick acceleration and release damping.
    double bankLevelRatePerSecond = 0.65;
    double maxBankAssistRadiansPerSecond = 0.30;
    double engineRiseSeconds = 0.28;
    double engineFallSeconds = 0.65;
    double cruiseChargeSeconds = 0.70;
    double strafeSpeedMps = 15.0;
    double landingTranslationSpeedMps = 2.0; // Combined right/up command, bounded by landing limits.
    double collisionRadiusMeters = 3.0; // Five longitudinal hull spheres.
    double hullHalfLengthMeters = 12.0;
    double hullHalfWidthMeters = 6.15;
    double hullTopMeters = 3.5;
    double lateralHullProbeRadiusMeters = 2.0;
    double lateralHullBottomMeters = -3.0; // Lateral rows stay above deployed feet.
    double landingClearanceMeters = 4.0; // Deployed-foot socket is z = -4 m.
    double landingFrontFootMeters = 7.0;
    double landingRearFootMeters = 5.0;
    double landingHalfTrackMeters = 4.8;
    double maxLandingDescentMps = 3.0;
    double maxLandingLateralMps = 2.0;
    double maxLandingTiltDegrees = 12.0;
    double maxLandingSlopeDegrees = 15.0;
    double recoveryClearanceMeters = 2000.0;
    double takeoffSpeedMps = 4.0;
    double approachRatePerSecond = 0.25; // Cruise limit = 30 m/s + clearance * rate.
    double terrainProbeSpacingMeters = 250.0;
    std::uint32_t maxTerrainProbesPerStep = 4096;
};

struct BodyTelemetry {
    std::string bodyId;
    double referenceAltitudeMeters = 0.0; // Signed altitude over datum sphere.
    double surfaceAltitudeMeters = 0.0;   // Signed altitude above sampled terrain.
    double latitudeDegrees = 0.0;
    double longitudeDegrees = 0.0;
    double closingSpeedMps = 0.0;
    double distanceMeters = 0.0;
    Vec3d outwardNormal;
    bool terrainAvailable = false;
};

// Presentation/assist effort, not a physical fuel or gravity model. All scalar
// fields are normalized [0,1]. Transient: never changes STAR_FLIGHT save format.
struct PropulsionTelemetry {
    double powerDemand = 0.0;       // Max of acceleration, retained drive, hover/RCS.
    double engineOutput = 0.0;      // Time-smoothed powerDemand for audio/exhaust.
    double accelerationDemand = 0.0; // Actual delta-v / available acceleration step.
    double braking = 0.0;           // Actual deceleration effort, not brake key state.
    double hoverDemand = 0.0;       // Fictional nearby-body flight-assist support.
    double cruiseCharge = 0.0;     // Engage ramp; never delays emergency braking.
};

struct ContactEvent {
    ContactKind kind = ContactKind::None;
    std::string bodyId;
    Vec3d contactCenterMeters;
    Vec3d normalSimulation;
    double downwardSpeedMps = 0.0;
    double lateralSpeedMps = 0.0;
    double slopeDegrees = 0.0;
    double tiltDegrees = 0.0;
};

struct AdvanceResult {
    std::uint32_t fixedSteps = 0;
    double discardedSeconds = 0.0;
    ContactEvent contact; // First contact in this Advance call, if any.
};

class FlightSimulation {
public:
    explicit FlightSimulation(std::vector<BodyDefinition> bodies,
                              FlightConfig config = {}, TerrainSampler terrain = {});
    const FlightState& State() const { return state_; }
    const FlightConfig& Config() const { return config_; }
    const std::vector<BodyDefinition>& Bodies() const { return bodies_; }
    bool RestoreState(const FlightState& state); // Validated save load; resets accumulator.
    // Update dated celestial frames, preserving local assisted flight and dynamics.
    bool UpdateCelestialFrames(const std::vector<BodyDefinition>& bodies);
    void SetMode(FlightMode mode); // Landed is entered by contact only.
    void SetGearDeployed(bool deployed);
    void SetThrottle(double throttle); // Recovery interlock ignores non-neutral throttle.
    bool SetTarget(const std::string& bodyId); // Empty clears target.
    AdvanceResult Advance(double frameSeconds, const FlightInput& input);
    BodyTelemetry Telemetry(const std::string& bodyId) const;
    double ApproachSpeedLimitMps() const;
    double LocalCruiseSpeedLimitMps() const;
    // Near-body radial bank leveling and fictional support effort; no global up
    // or gravity. Manual angular-rate controls retain damping in either setting.
    void SetFlightAssistEnabled(bool enabled);
    bool FlightAssistEnabled() const { return flightAssistEnabled_; }
    const PropulsionTelemetry& Propulsion() const { return propulsion_; }
    const BodyDefinition* FindBody(const std::string& bodyId) const;

private:
    struct Surface;
    struct SweepHit;
    Surface SampleSurface(const BodyDefinition& body, const Vec3d& direction) const;
    SweepHit Sweep(const Vec3d& start, const Vec3d& end, const Quatd& startOrientation) const;
    ContactEvent Step(const FlightInput& input);
    ContactEvent ResolveContact(const SweepHit& hit);
    void Takeoff(bool smooth);
    double ContactRadius() const;
    void ResetDynamics(bool clearSupport = true);
    Vec3d ReferenceUp(double& proximity) const;
    std::vector<BodyDefinition> bodies_;
    FlightConfig config_;
    TerrainSampler terrain_;
    FlightState state_;
    double accumulatorSeconds_ = 0.0;
    bool flightAssistEnabled_ = true;
    Vec3d angularVelocity_;
    Vec3d guidanceAcceleration_;
    std::string guidanceReleaseBody_;
    Vec3d guidanceReleasePosition_;
    PropulsionTelemetry propulsion_;
};

// Locale-independent, versioned flight-only persistence; exploration remains owned
// by the exploration subsystem and is never cleared by collision recovery.
std::string SerializeFlightState(const FlightState& state);
bool DeserializeFlightState(const std::string& text, FlightState& state);
} // namespace star
