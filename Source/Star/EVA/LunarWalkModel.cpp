#include "LunarWalkModel.h"
#include "Simulation/Astronomy.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace star {
namespace {
constexpr double MaxSlopeRadians = 35.0 * Pi / 180.0;
constexpr double WalkSpeed = 1.4;
constexpr double ProbeSpacing = 0.10;
bool ValidRotation(const Quatd& q) {
    const double norm = q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;
    return q.IsFinite() && std::isfinite(norm) && norm > 1e-12;
}
bool ValidMoon(const BodyDefinition& moon) {
    return moon.id == "moon" && moon.landable && moon.centerMeters.IsFinite() &&
        moon.centerMeters.Length() < 1e18 && std::isfinite(moon.radiusMeters) &&
        moon.radiusMeters >= 1 && moon.radiusMeters <= 1e11 && ValidRotation(moon.bodyFixedToSimulation);
}
bool Parked(const FlightState& ship) {
    return ship.mode == FlightMode::Landed && ship.landedBodyId == "moon" && ship.gearDeployed &&
        ship.positionMeters.IsFinite() && ship.velocityMetersPerSecond.IsFinite() &&
        ship.velocityMetersPerSecond.Length() <= 0.001 && ValidRotation(ship.orientation);
}
Vec3d Tangent(const Vec3d& v, const Vec3d& up) {
    Vec3d result = v - up * Vec3d::Dot(v, up);
    if (result.LengthSquared() < 1e-12) {
        const Vec3d axis = std::abs(up.z) < 0.9 ? Vec3d{0,0,1} : Vec3d{1,0,0};
        result = Vec3d::Cross(axis, up);
    }
    return result.Normalized();
}
}
void LunarWalkModel::UpdateCelestialFrame(const BodyDefinition& moon) {
    // A celestial frame change is a rigid transport, not a change of surface.
    // Compute every candidate first so a rejected epoch cannot strand the walker.
    if (!ready_ || !ValidMoon(moon) || moon.radiusMeters != moon_.radiusMeters) return;
    auto nextMoon = moon;
    nextMoon.bodyFixedToSimulation = moon.bodyFixedToSimulation.Normalized();
    const auto q = (nextMoon.bodyFixedToSimulation * moon_.bodyFixedToSimulation.Conjugate()).Normalized();
    const auto feet = nextMoon.centerMeters + q.Rotate(feet_ - moon_.centerMeters);
    const auto boarding = nextMoon.centerMeters + q.Rotate(boardingPoint_ - moon_.centerMeters);
    const auto heading = q.Rotate(heading_);
    const auto ship = TransportFlightFrame(parkedShip_, moon_, nextMoon);
    if (!feet.IsFinite() || !boarding.IsFinite() || !heading.IsFinite() || !Parked(ship)) return;
    feet_ = feet; boardingPoint_ = boarding; heading_ = heading;
    parkedShip_ = ship; moon_ = std::move(nextMoon);
}
bool LunarWalkModel::Ground(const Vec3d& direction, Vec3d& feet, double& height) const {
    TerrainSample sample;
    if (!terrain_ || !terrain_(moon_, direction, sample) ||
        !std::isfinite(sample.heightMeters) || !sample.normalSimulation.IsFinite() ||
        sample.normalSimulation.LengthSquared() < 0.5 ||
        Vec3d::Dot(sample.normalSimulation.Normalized(), direction) < std::cos(MaxSlopeRadians) ||
        moon_.radiusMeters + sample.heightMeters <= 0) return false;
    height = sample.heightMeters;
    feet = moon_.centerMeters + direction * (moon_.radiusMeters + height);
    return feet.IsFinite();
}
bool LunarWalkModel::OutsideHull(const Vec3d& feet) const {
    // Conservative projected hull prism, including gear: never walk through or
    // underneath the craft and never transfer an impulse to the ship.
    const Vec3d delta = feet - parkedShip_.positionMeters;
    return std::abs(Vec3d::Dot(delta, Forward(parkedShip_.orientation))) > hull_.hullHalfLengthMeters + 0.5 ||
           std::abs(Vec3d::Dot(delta, Right(parkedShip_.orientation))) > hull_.hullHalfWidthMeters + 0.5;
}
bool LunarWalkModel::Initialize(const FlightState& ship, const BodyDefinition& moon,
                                TerrainSampler terrain, const FlightConfig& hull) {
    if (!Parked(ship) || !ValidMoon(moon) || !terrain ||
        !std::isfinite(hull.hullHalfLengthMeters) || hull.hullHalfLengthMeters <= 0 ||
        !std::isfinite(hull.hullHalfWidthMeters) || hull.hullHalfWidthMeters <= 0) return false;
    LunarWalkModel pending;
    pending.parkedShip_ = ship; pending.parkedShip_.orientation = ship.orientation.Normalized();
    pending.moon_ = moon; pending.moon_.bodyFixedToSimulation = moon.bodyFixedToSimulation.Normalized();
    pending.terrain_ = std::move(terrain); pending.hull_ = hull;
    const Vec3d up = (ship.positionMeters - moon.centerMeters).Normalized();
    const Vec3d right = Tangent(Right(ship.orientation), up);
    const Vec3d forward = Tangent(Forward(ship.orientation), up);
    // Search bounded ground immediately beside the hull/gear, never a remote
    // landing-site teleport. Try starboard first, then port if steep/unavailable.
    for (double side : {1.0, -1.0}) for (double along : {0.0, 3.0, -3.0}) {
        const Vec3d candidate = ship.positionMeters + right * (side * (hull.hullHalfWidthMeters + 2.0)) + forward * along;
        Vec3d ground; double height = 0;
        if (!pending.Ground((candidate - moon.centerMeters).Normalized(), ground, height) || !pending.OutsideHull(ground) ||
            (ground - ship.positionMeters).Length() > 18.0) continue;
        pending.feet_ = pending.boardingPoint_ = ground; pending.height_ = height;
        pending.heading_ = Tangent(forward, pending.UpDirection());
        pending.ready_ = true;
        *this = std::move(pending);
        return true;
    }
    return false;
}
Vec3d LunarWalkModel::UpDirection() const { return (feet_ - moon_.centerMeters).Normalized({0,0,1}); }
Vec3d LunarWalkModel::CameraForward() const { return (heading_ * std::cos(pitchRadians_) + UpDirection() * std::sin(pitchRadians_)).Normalized(); }
Vec3d LunarWalkModel::CameraRight() const { return Vec3d::Cross(heading_, UpDirection()).Normalized(); }
Vec3d LunarWalkModel::CameraUp() const { return Vec3d::Cross(CameraRight(), CameraForward()).Normalized(); }
void LunarWalkModel::Advance(double dt, double right, double forward, double yaw, double pitch, bool paused) {
    if (!ready_ || paused || !std::isfinite(dt) || dt <= 0 || !std::isfinite(right) ||
        !std::isfinite(forward) || !std::isfinite(yaw) || !std::isfinite(pitch)) return;
    blocked_ = false;
    heading_ = Quatd::FromAxisAngle(UpDirection(), -std::remainder(yaw,360.0) * Pi/180.0).Rotate(heading_).Normalized();
    pitchRadians_ = std::clamp(pitchRadians_ + pitch * Pi/180.0, -85.0*Pi/180.0, 85.0*Pi/180.0);
    right = std::clamp(right,-1.0,1.0); forward = std::clamp(forward,-1.0,1.0);
    const double inputLength = std::hypot(right, forward);
    if (inputLength < 1e-9) return;
    const double norm = std::max(1.0,inputLength);
    right /= norm; forward /= norm;
    const double distance = WalkSpeed * std::min(dt,0.25);
    const int steps = static_cast<int>(std::ceil(distance / ProbeSpacing));
    for (int step = 0; step < steps; ++step) {
        const Vec3d travel = (heading_ * forward + CameraRight() * right) * (distance/steps);
        const Vec3d radial = (feet_ + travel - moon_.centerMeters).Normalized();
        Vec3d ground; double height = 0;
        // Height change additionally catches DEM edges whose interpolated normal
        // does not describe the discontinuity. Both ascent and drop are blocked.
        if (!Ground(radial,ground,height) || std::abs(height-height_) > 0.25 ||
            std::abs(height-height_) > travel.Length()*std::tan(MaxSlopeRadians)+0.005 ||
            !OutsideHull(ground)) { blocked_ = true; break; }
        feet_ = ground; height_ = height;
        heading_ = Tangent(heading_, UpDirection());
    }
}
bool LunarWalkModel::IsSameParkedShip(const FlightState& ship) const {
    return ready_ && Parked(ship) &&
        (ship.positionMeters-parkedShip_.positionMeters).Length() < 0.05 &&
        Vec3d::Dot(Forward(ship.orientation),Forward(parkedShip_.orientation)) > 0.99999 &&
        Vec3d::Dot(Up(ship.orientation),Up(parkedShip_.orientation)) > 0.99999;
}
bool LunarWalkModel::CanBoard(const FlightState& ship) const {
    return IsSameParkedShip(ship) && DistanceToBoardingPoint() <= 2.5;
}
LunarWalkSaveState LunarWalkModel::ExportSaveState() const {
    return {feet_, heading_, boardingPoint_, pitchRadians_};
}
bool LunarWalkModel::RestoreSaveState(const LunarWalkSaveState& saved) {
    if (!ready_ || !saved.feetMeters.IsFinite() || !saved.headingSimulation.IsFinite() ||
        !saved.boardingPointMeters.IsFinite() || !std::isfinite(saved.pitchRadians) ||
        std::abs(saved.pitchRadians) > 85.0 * Pi / 180.0 ||
        (saved.boardingPointMeters - boardingPoint_).Length() > 0.05 ||
        std::abs(saved.headingSimulation.LengthSquared() - 1.0) > 0.001) return false;
    const Vec3d radial = (saved.feetMeters - moon_.centerMeters).Normalized();
    if (std::abs(Vec3d::Dot(saved.headingSimulation, radial)) > 0.001) return false;
    Vec3d grounded; double height = 0;
    if (!Ground(radial, grounded, height) || !OutsideHull(grounded) ||
        (grounded - saved.feetMeters).Length() > 0.05) return false;
    // Commit only after all checks. Project rounding error onto current DEM and
    // its tangent plane rather than trusting a serialized physical contact.
    feet_ = grounded; height_ = height;
    heading_ = Tangent(saved.headingSimulation, radial);
    pitchRadians_ = saved.pitchRadians; blocked_ = false;
    return true;
}
}
