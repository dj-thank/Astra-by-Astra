#pragma once

// Portable gameplay rules. All distances are metres; lat/lon are body-fixed degrees.
// Thresholds are game mission tolerances, not instrument accuracy or scientific claims.
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace star::exploration
{
enum class Body : std::uint8_t { Unknown, Earth, Moon, Saturn };
inline constexpr std::size_t ObjectiveCount = 6;
inline constexpr std::array<const char*, ObjectiveCount> Ids = {
    "earth_day", "earth_night", "moon_orbit", "moon_taurus_littrow", "saturn_rings", "saturn_shadow"};
inline constexpr std::array<double, ObjectiveCount> Durations = {12, 12, 10, 8, 12, 12};
inline constexpr std::uint32_t TutorialAll = 31;

struct Sample
{
    Body body = Body::Unknown;
    std::uint64_t sequence = 0;
    double deltaSeconds = 0;
    double altitudeM = 0;
    double latitudeDeg = 0;
    double longitudeDeg = 0;
    double speedMps = 0;
    double verticalSpeedMps = 0;
    double sunIllumination = 0; // dot(surface normal, direction toward Sun), [-1,1]
    double ringRadiusM = 0; // actual observed ring point, not ship position
    bool landed = false;
    bool scanning = false;
    bool viewingBody = false;
    bool cockpitView = true;
    bool viewingRings = false;
    bool observedPointInPlanetShadow = false;
};

inline bool IsValid(const Sample& s)
{
    return s.body >= Body::Earth && s.body <= Body::Saturn &&
        std::isfinite(s.deltaSeconds) && s.deltaSeconds > 0 && s.deltaSeconds <= 0.5 &&
        std::isfinite(s.altitudeM) && s.altitudeM >= -10000 && s.altitudeM <= 1e14 &&
        std::isfinite(s.latitudeDeg) && std::abs(s.latitudeDeg) <= 90 &&
        std::isfinite(s.longitudeDeg) && std::abs(s.longitudeDeg) <= 180 &&
        std::isfinite(s.speedMps) && s.speedMps >= 0 && s.speedMps <= 1.6e10 &&
        std::isfinite(s.verticalSpeedMps) && std::abs(s.verticalSpeedMps) <= 1.6e10 &&
        std::isfinite(s.sunIllumination) && std::abs(s.sunIllumination) <= 1 &&
        std::isfinite(s.ringRadiusM) && s.ringRadiusM >= 0 && s.ringRadiusM <= 1e12;
}

inline bool Qualifies(std::size_t objective, const Sample& s)
{
    if (!IsValid(s) || !s.scanning) return false;
    const bool earth = s.body == Body::Earth && s.viewingBody && !s.landed &&
        s.altitudeM >= 100000 && s.altitudeM <= 50000000 && s.speedMps <= 12000;
    const bool rings = s.body == Body::Saturn && s.viewingRings && !s.landed &&
        s.altitudeM >= 10000000 && s.altitudeM <= 150000000 && s.speedMps <= 30000 &&
        s.ringRadiusM >= 74500000 && s.ringRadiusM <= 140300000;
    switch (objective)
    {
    case 0: return earth && s.sunIllumination >= 0.25;
    case 1: return earth && s.sunIllumination <= -0.25;
    case 2: return s.body == Body::Moon && s.viewingBody && !s.landed &&
        s.altitudeM >= 20000 && s.altitudeM <= 500000 && s.speedMps <= 3000;
    case 3: return s.body == Body::Moon && s.landed && s.speedMps <= 0.5 &&
        std::abs(s.verticalSpeedMps) <= 0.25 && s.altitudeM <= 100 &&
        std::abs(s.latitudeDeg - 20.1908) <= 0.35 &&
        std::abs(std::remainder(s.longitudeDeg - 30.7717, 360.0)) <= 0.35;
    case 4: return rings;
    case 5: return rings && s.observedPointInPlanetShadow;
    default: return false;
    }
}

struct ProgressState
{
    std::array<double, ObjectiveCount> seconds{};
    std::array<Sample, ObjectiveCount> discoveries{};
    std::uint32_t tutorialMask = 0;
};

class Tracker
{
public:
    // Returns bit mask of discoveries completed by this sample, exactly once.
    std::uint32_t Observe(const Sample& s)
    {
        if (!IsValid(s) || s.sequence == 0 || s.sequence <= lastSequence_) return 0;
        lastSequence_ = s.sequence;
        if (s.landed && s.body == Body::Moon && s.speedMps <= 0.5)
        {
            sawLanded_ = true;
            state_.tutorialMask |= 16;
        }
        // Orbit starts demonstrate controlled flight; a surface start requires observed takeoff.
        if (!s.landed && ((sawLanded_ && s.altitudeM > 10 && s.verticalSpeedMps > 0.2) ||
            (!sawLanded_ && s.altitudeM > 20000 && s.speedMps >= 20 && s.speedMps <= 12000)))
            state_.tutorialMask |= 1;
        std::uint32_t newlyCompleted = 0;
        for (std::size_t i = 0; i < ObjectiveCount; ++i)
        {
            if (Complete(i)) continue;
            if (!Qualifies(i, s)) { state_.seconds[i] = 0; continue; }
            // Long hitches cannot stand in for unobserved time.
            state_.seconds[i] = std::min(Durations[i], state_.seconds[i] + std::min(s.deltaSeconds, 0.25));
            state_.tutorialMask |= 8;
            if (state_.seconds[i] + 1e-8 >= Durations[i])
            {
                state_.seconds[i] = Durations[i];
                state_.discoveries[i] = s;
                newlyCompleted |= 1u << i;
            }
        }
        return newlyCompleted;
    }
    void TargetSelected() { state_.tutorialMask |= 2; }
    void ViewChanged() { state_.tutorialMask |= 4; }
    bool Complete(std::size_t i) const { return i < ObjectiveCount && state_.seconds[i] >= Durations[i]; }
    double Progress(std::size_t i) const { return i < ObjectiveCount ? state_.seconds[i] / Durations[i] : 0; }
    const ProgressState& State() const { return state_; }
    // Restore is all-or-nothing. Completed records must match the mission's actual conditions.
    bool Restore(const ProgressState& incoming)
    {
        if ((incoming.tutorialMask & ~TutorialAll) != 0) return false;
        for (std::size_t i = 0; i < ObjectiveCount; ++i)
        {
            if (!std::isfinite(incoming.seconds[i]) || incoming.seconds[i] < 0 || incoming.seconds[i] > Durations[i]) return false;
            if (incoming.seconds[i] == Durations[i] && !Qualifies(i, incoming.discoveries[i])) return false;
        }
        state_ = incoming;
        lastSequence_ = 0; // root may start a new simulation sequence after load
        sawLanded_ = false;
        return true;
    }
private:
    ProgressState state_{};
    std::uint64_t lastSequence_ = 0;
    bool sawLanded_ = false;
};
}
