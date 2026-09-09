#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace star::audio::routing
{
enum class Cue : std::uint8_t
{
    CabinLow, CabinMid, EngineIdle, EngineLow, EngineMid, Cruise, EngineHigh, SuitFan, SuitCooling,
    MusicEarth, MusicMoonApproach, MusicMoonSurface, MusicSaturn, MusicCruise,
    Gear, GearDeploy, GearStow, Scan, ScanComplete, Landing, Warning, View, RCS, Collision, SpoolUp, SpoolDown, CruiseCharge, CruiseEngage, CruiseDisengage,
    Footstep1, Footstep2, Footstep3, Footstep4, ExitHatch, EnterHatch, HatchLatch, SuitLatch, RadioEquip, SuitValve, ToolGrip, TetherAttach, TetherDetach, EVAPush, ThrusterPulse, CoolingRelay,
    Count, None = 255
};
constexpr std::size_t CueCount = static_cast<std::size_t>(Cue::Count);
constexpr std::size_t LayerCount = 9;
constexpr std::size_t MusicVoiceCount = 2;
constexpr std::size_t EventVoiceCount = 4;
enum class Body : std::uint8_t { Unknown, Earth, Moon, Saturn, Space };

struct CueSpec
{
    const char* id;
    bool loop;
    float eventGain;
    double cooldownSeconds;
    double fallbackSeconds;
    int eventGroup; // -1 for loop/music, same group never overlaps.
};
const CueSpec& Spec(Cue cue) noexcept;
Cue EventCue(std::string_view name) noexcept;
bool IsEvent(Cue cue) noexcept;
/** Sum of the four loudest distinct event groups; includes boosted boot contacts. */
double EventPeakReservation() noexcept;

struct Input
{
    double throttle = 0.0;
    double speedMps = 0.0;
    double charge = 0.0;
    double braking = 0.0;
    double masterVolume = 0.7;
    bool engineOutputIsSpooled = false; // Runtime telemetry can provide the physical/presentation spool already.
    bool cockpit = true; // Camera only; monitor keeps the pilot's ears inside.
    bool interiorMonitor = true;
    bool eva = false;
    bool paused = false;
    bool cruise = false;
    bool landed = false;
    Body body = Body::Earth;
};

struct MusicVoice { Cue cue = Cue::None; double weight = 0.0; };
struct Mix
{
    std::array<double, LayerCount> assetLayers{};
    std::array<double, LayerCount> fallbackLayers{};
    std::array<MusicVoice, MusicVoiceCount> music{};
    double musicGain = 0.0;
    double eventMaster = 0.0;
    double throttle = 0.0;
    double whineGain = 0.0;
    double charge = 0.0;
    double braking = 0.0;
    double cruise = 0.0;
    double enginePitch = 1.0;
    double adaptiveGain = 1.0; // Bounded peak-budget trim; never boosts quiet assets.
};

/** Deterministic game-thread routing/envelopes; no assets, Unreal, clocks or audio devices. */
class State
{
public:
    void SetInput(const Input& input) noexcept;
    void SetAvailable(Cue cue, bool available) noexcept;
    void Step(double seconds) noexcept;
    const Mix& Output() const noexcept { return mix; }
    Cue DesiredMusic() const noexcept;
    /** Returns a fixed voice slot, or -1 if muted, cooling down, same-group busy or capped. */
    int TryEvent(Cue cue, double durationSeconds, std::uint32_t usableSlots = 0xfu) noexcept;
    void ReleaseEvent(std::size_t slot) noexcept;
    void ResetTransientState() noexcept;

private:
    struct Voice { Cue cue = Cue::None; double remaining = 0.0; };
    Input current;
    Mix mix;
    std::array<bool, CueCount> available{};
    std::array<double, LayerCount> assetBlend{};
    std::array<double, CueCount> cooldown{};
    std::array<Voice, EventVoiceCount> events{};
    double master = 0.0;
    double throttle = 0.0;
    double cockpit = 1.0;
    double suit = 0.0;
    double cruise = 0.0;
    double charge = 0.0;
    double braking = 0.0;
    Cue selectedMusic = Cue::None;
    void StepMusic(double seconds) noexcept;
};

/** Missing-part-only fallback. One audio thread; per-layer gain ramps and four event voices. */
class FallbackDSP
{
public:
    explicit FallbackDSP(double sampleRate = 48000.0) noexcept;
    void SetMix(const std::array<double, LayerCount>& layers, double throttle, double eventMaster, bool paused) noexcept;
    void SetEngine(double gain, double charge, double cruise, double braking) noexcept;
    void Trigger(std::size_t slot, Cue cue) noexcept;
    void Cancel(std::size_t slot) noexcept;
    void Render(float* stereo, std::size_t frames) noexcept;

private:
    struct EventVoice { Cue cue = Cue::None; double age = 0.0; double releaseGain = 1.0; bool releasing = false; };
    double rate;
    double dt;
    std::array<double, LayerCount> targetLayers{};
    std::array<double, LayerCount> gains{};
    std::array<double, LayerCount> phases{};
    std::array<EventVoice, EventVoiceCount> voices{};
    double whinePhase = 0.0;
    double bladePhase = 0.0;
    double targetWhine = 0.0, whine = 0.0;
    double targetCharge = 0.0, charge = 0.0;
    double targetCruise = 0.0, cruise = 0.0;
    double targetBraking = 0.0, braking = 0.0;
    double targetThrottle = 0.0;
    double throttle = 0.0;
    double targetEventMaster = 0.0;
    double eventMaster = 0.0;
    double low = 0.0;
    double mid = 0.0;
    double lowCoefficient;
    double midCoefficient;
    double gainCoefficient;
    std::uint32_t random = 0x53544152u;
    bool paused = false;
};
}
