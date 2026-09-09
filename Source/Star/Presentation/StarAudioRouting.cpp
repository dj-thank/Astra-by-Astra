#include "StarAudioRouting.h"

#include <algorithm>
#include <cmath>

namespace star::audio::routing
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double Tau = Pi * 2.0;
constexpr double EnginePresence = 2.0; // +6.02 dB, main engine beds and turbine only.
constexpr double EVAPresence = 2.8183829312644537; // +9 dB, suit beds and boot contacts only.
constexpr std::array<CueSpec, CueCount> Specs{{
    {"SFX_CABIN_LIFE_LOW", true, 0, 0, 0, -1},
    {"SFX_CABIN_LIFE_MID", true, 0, 0, 0, -1},
    {"SFX_ENGINE_IDLE", true, 0, 0, 0, -1},
    {"SFX_ENGINE_THRUST_LOW", true, 0, 0, 0, -1},
    {"SFX_ENGINE_THRUST_MID", true, 0, 0, 0, -1},
    {"SFX_CRUISE_BED", true, 0, 0, 0, -1},
    {"SFX_ENGINE_THRUST_HIGH", true, 0, 0, 0, -1},
    {"SFX_EVA_SUIT_FAN", true, 0, 0, 0, -1},
    {"SFX_EVA_SUIT_COOLING", true, 0, 0, 0, -1},
    {"BGM_EARTH_SUNRISE", true, 0, 0, 0, -1},
    {"BGM_MOON_APPROACH", true, 0, 0, 0, -1},
    {"BGM_MOON_SURFACE", true, 0, 0, 0, -1},
    {"BGM_SATURN_RINGS", true, 0, 0, 0, -1},
    {"BGM_DEEP_CRUISE", true, 0, 0, 0, -1},
    {"SFX_GEAR_HYDRAULIC_PRESSURE", false, 0.10f, 0.60, 1.10, 0},
    {"SFX_GEAR_DEPLOY", false, 0.11f, 0.60, 1.35, 0},
    {"SFX_GEAR_STOW", false, 0.11f, 0.60, 1.35, 0},
    {"SFX_SCAN_START", false, 0.09f, 0.40, 0.35, 1},
    {"SFX_SCAN_COMPLETE", false, 0.10f, 0.60, 0.62, 1},
    {"SFX_TOUCHDOWN_LIGHT", false, 0.13f, 0.50, 1.10, 2},
    {"SFX_WARN_CAUTION", false, 0.12f, 1.00, 0.72, 3},
    {"SFX_CONTROL_ROCKER", false, 0.08f, 0.10, 0.10, 4},
    {"SFX_RCS_ATTITUDE_CLUSTER", false, 0.08f, 0.18, 0.26, 5},
    {"SFX_WARN_COLLISION", false, 0.13f, 1.00, 0.72, 3},
    {"SFX_ENGINE_SPOOL_UP", false, 0.085f, 1.8, 1.4, 6},
    {"SFX_ENGINE_SPOOL_DOWN", false, 0.085f, 1.8, 1.4, 6},
    {"SFX_CRUISE_CHARGE", false, 0.09f, 2.0, 2.0, 7},
    {"SFX_CRUISE_ENGAGE", false, 0.11f, 2.0, 1.2, 7},
    {"SFX_CRUISE_DISENGAGE", false, 0.10f, 2.0, 1.5, 7},
    {"SFX_EVA_FOOTSTEP_REGOLITH_1", false, static_cast<float>(0.12 * EVAPresence), 0.12, 0.36, 8},
    {"SFX_EVA_FOOTSTEP_REGOLITH_2", false, static_cast<float>(0.12 * EVAPresence), 0.12, 0.36, 8},
    {"SFX_EVA_FOOTSTEP_REGOLITH_3", false, static_cast<float>(0.12 * EVAPresence), 0.12, 0.36, 8},
    {"SFX_EVA_FOOTSTEP_REGOLITH_4", false, static_cast<float>(0.12 * EVAPresence), 0.12, 0.36, 8},
    {"SFX_EVA_HATCH_OPEN", false, 0.12f, 0.12, 1.80, 9},
    {"SFX_EVA_HATCH_CLOSE", false, 0.12f, 0.12, 1.80, 9},
    {"SFX_EVA_HATCH_LATCH", false, 0.12f, 0.12, 0.60, 9},
    {"SFX_EVA_SUIT_LATCH", false, 0.12f, 0.12, 0.50, 10},
    {"SFX_EVA_RADIO_EQUIP", false, 0.12f, 0.12, 0.50, 10},
    {"SFX_EVA_SUIT_VALVE", false, 0.12f, 0.12, 0.70, 10},
    {"SFX_EVA_TOOL_GRIP", false, 0.12f, 0.12, 0.50, 10},
    {"SFX_EVA_TETHER_ATTACH", false, 0.12f, 0.12, 0.50, 10},
    {"SFX_EVA_TETHER_DETACH", false, 0.12f, 0.12, 0.50, 10},
    {"SFX_EVA_PUSH_OFF", false, 0.12f, 0.12, 0.36, 8},
    {"SFX_EVA_THRUSTER_PULSE", false, 0.12f, 0.12, 0.30, 5},
    {"SFX_EVA_COOLING_RELAY", false, 0.12f, 0.12, 0.50, 10},
}};
// Only one voice in each eventGroup may play. Derive the reservation from
// real cue gains so a louder/new cue cannot silently bypass the peak budget.
constexpr double CalculateEventPeakReservation()
{
    std::array<double, CueCount> groups{};
    for (const auto& spec : Specs)
        if (spec.eventGroup >= 0 && spec.eventGroup < static_cast<int>(CueCount))
        {
            const auto group = static_cast<std::size_t>(spec.eventGroup);
            if (spec.eventGain > groups[group]) groups[group] = spec.eventGain;
        }
    double result = 0.0;
    for (std::size_t voice = 0; voice < EventVoiceCount; ++voice)
    {
        std::size_t largest = 0;
        for (std::size_t group = 1; group < groups.size(); ++group)
            if (groups[group] > groups[largest]) largest = group;
        result += groups[largest]; groups[largest] = 0.0;
    }
    return result;
}
constexpr double ReservedEventPeak = CalculateEventPeakReservation();
constexpr CueSpec InvalidSpec{"", false, 0, 0, 0, -1};

double Unit(double value) noexcept { return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0; }
double Move(double value, double target, double step) noexcept { return value + std::clamp(target - value, -step, step); }
double Smooth(double value, double target, double seconds, double timeConstant) noexcept
{
    return value + (target - value) * (1.0 - std::exp(-seconds / timeConstant));
}
double Window(double age, double duration, double attack, double release) noexcept
{
    if (age <= 0 || age >= duration) return 0.0;
    return (0.5 - 0.5 * std::cos(Pi * std::min(age / attack, 1.0)))
        * (0.5 - 0.5 * std::cos(Pi * std::min((duration - age) / release, 1.0)));
}
bool SameAsciiName(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const char a = left[i] >= 'a' && left[i] <= 'z' ? static_cast<char>(left[i] - 'a' + 'A') : left[i];
        const char b = right[i] >= 'a' && right[i] <= 'z' ? static_cast<char>(right[i] - 'a' + 'A') : right[i];
        if (a != b) return false;
    }
    return true;
}
}

const CueSpec& Spec(Cue cue) noexcept
{
    const auto index = static_cast<std::size_t>(cue);
    return index < Specs.size() ? Specs[index] : InvalidSpec;
}
bool IsEvent(Cue cue) noexcept { return Spec(cue).eventGroup >= 0; }
double EventPeakReservation() noexcept { return ReservedEventPeak; }

Cue EventCue(std::string_view name) noexcept
{
    if (SameAsciiName(name, "Footstep")) return Cue::Footstep1;
    constexpr std::array<std::string_view, 31> Names{{"Gear", "GearDeploy", "GearStow", "Scan", "ScanComplete", "Landing", "Warning", "View", "RCS", "Collision", "SpoolUp", "SpoolDown", "CruiseCharge", "CruiseEngage", "CruiseDisengage", "Footstep1", "Footstep2", "Footstep3", "Footstep4", "ExitHatch", "EnterHatch", "HatchLatch", "SuitLatch", "RadioEquip", "SuitValve", "ToolGrip", "TetherAttach", "TetherDetach", "EVAPush", "ThrusterPulse", "CoolingRelay"}};
    static_assert(Names.size() == CueCount - static_cast<std::size_t>(Cue::Gear));
    for (std::size_t i = 0; i < Names.size(); ++i)
    {
        const Cue cue = static_cast<Cue>(static_cast<std::size_t>(Cue::Gear) + i);
        if (SameAsciiName(name, Names[i]) || SameAsciiName(name, Spec(cue).id)) return cue;
    }
    return Cue::None;
}

void State::SetInput(const Input& input) noexcept
{
    const bool becameSilent = (!current.paused && input.paused)
        || (current.masterVolume > 0.0 && Unit(input.masterVolume) == 0.0);
    current = input;
    current.throttle = Unit(input.throttle);
    current.charge = Unit(input.charge);
    current.braking = Unit(input.braking);
    current.masterVolume = Unit(input.masterVolume);
    current.speedMps = std::isfinite(input.speedMps) ? std::clamp(input.speedMps, 0.0, 50.0 * 299792458.0) : 0.0;
    if (becameSilent) ResetTransientState();
}

void State::SetAvailable(Cue cue, bool value) noexcept
{
    const auto index = static_cast<std::size_t>(cue);
    if (index < available.size()) available[index] = value;
}

Cue State::DesiredMusic() const noexcept
{
    if (current.cruise || current.body == Body::Space) return Cue::MusicCruise;
    switch (current.body)
    {
    case Body::Earth: return Cue::MusicEarth;
    case Body::Moon: return current.landed ? Cue::MusicMoonSurface : Cue::MusicMoonApproach;
    case Body::Saturn: return Cue::MusicSaturn;
    default: return Cue::None;
    }
}

void State::ResetTransientState() noexcept
{
    master = 0.0;
    mix.eventMaster = mix.musicGain = mix.whineGain = 0.0;
    mix.assetLayers.fill(0.0); mix.fallbackLayers.fill(0.0);
    for (auto& voice : mix.music) voice.weight = 0.0;
    events.fill(Voice{}); cooldown.fill(0.0);
}

void State::StepMusic(double seconds) noexcept
{
    const Cue desired = DesiredMusic();
    // A late/missing next asset must not replace an audible current track with silence.
    if (desired == Cue::None || available[static_cast<std::size_t>(desired)]) selectedMusic = desired;
    int targetSlot = -1;
    for (std::size_t i = 0; i < MusicVoiceCount; ++i)
        if (mix.music[i].cue == selectedMusic && selectedMusic != Cue::None) targetSlot = static_cast<int>(i);
    if (targetSlot < 0 && selectedMusic != Cue::None)
    {
        for (std::size_t i = 0; i < MusicVoiceCount; ++i)
        {
            if (mix.music[i].weight <= 1e-9)
            {
                mix.music[i] = {selectedMusic, 0.0};
                targetSlot = static_cast<int>(i);
                break;
            }
        }
        if (targetSlot < 0)
        {
            // Rapid A -> B -> C: free the quieter slot before changing its sound.
            // Retain the louder track as an anchor; at most two real music voices exist.
            targetSlot = mix.music[0].weight >= mix.music[1].weight ? 0 : 1;
        }
    }
    for (std::size_t i = 0; i < MusicVoiceCount; ++i)
        mix.music[i].weight = Move(mix.music[i].weight, static_cast<int>(i) == targetSlot ? 1.0 : 0.0, seconds / 2.5);
    // Linear complementary fades cannot amplify correlated music above unity.
    const double sum = mix.music[0].weight + mix.music[1].weight;
    if (sum > 1.0) for (auto& voice : mix.music) voice.weight /= sum;
}

void State::Step(double seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds <= 0.0) return;
    const double dt = std::min(seconds, 0.25); // A hitch never creates a large audible parameter jump.
    if (current.paused || current.masterVolume == 0.0) { ResetTransientState(); return; }
    master = Move(master, current.masterVolume, dt / 0.25);
    throttle = Smooth(throttle, current.throttle, dt, current.engineOutputIsSpooled ? 0.035 : (current.throttle > throttle ? 0.42 : 0.70));
    charge = Smooth(charge, current.charge, dt, 0.20);
    braking = Smooth(braking, current.braking, dt, 0.12);
    cockpit = Smooth(cockpit, !current.eva && (current.cockpit || current.interiorMonitor) ? 1.0 : 0.0, dt, 0.15);
    suit = Smooth(suit, current.eva ? 1.0 : 0.0, dt, 0.15);
    cruise = Smooth(cruise, current.cruise ? 1.0 : 0.0, dt, 0.5);
    const double machinery = cockpit; // No machinery propagates across exterior vacuum.
    const double midBlend = Unit((throttle - 0.20) / 0.45);
    const double highBlend = Unit((throttle - 0.65) / 0.35);
    const std::array<double, LayerCount> base{{
        0.045 * cockpit, 0.030 * cockpit,
        EnginePresence * 0.020 * (1.0 - throttle) * machinery,
        EnginePresence * 0.080 * std::sqrt(throttle) * (1.0 - 0.65 * midBlend) * machinery,
        EnginePresence * 0.075 * throttle * midBlend * (1.0 - 0.65 * highBlend) * machinery,
        EnginePresence * 0.045 * cruise * machinery,
        EnginePresence * 0.090 * throttle * highBlend * machinery,
        EVAPresence * 0.065 * suit, EVAPresence * 0.035 * suit,
    }};
    for (std::size_t i = 0; i < LayerCount; ++i)
    {
        assetBlend[i] = Move(assetBlend[i], available[i] ? 1.0 : 0.0, dt / 0.25);
        mix.assetLayers[i] = base[i] * master * assetBlend[i];
        mix.fallbackLayers[i] = base[i] * master * (1.0 - assetBlend[i]);
    }
    mix.eventMaster = master * std::max(cockpit, suit);
    mix.musicGain = 0.065 * master;
    mix.throttle = throttle;
    mix.charge = charge; mix.cruise = cruise; mix.braking = braking;
    mix.enginePitch = 0.82 + 0.46 * throttle + 0.10 * charge;
    // Deliberate continuous turbine layer complements imported material, even when every asset is present.
    mix.whineGain = EnginePresence * master * machinery * (0.010 + 0.068 * std::pow(throttle, 0.7) + 0.026 * charge + 0.020 * cruise + 0.009 * braking);
    // Reserve worst-case four event peaks and a unity music peak. Trim beds smoothly
    // before the sum can clip, rather than amplifying low-level source recordings.
    double peakBudget = mix.whineGain + mix.musicGain + EventPeakReservation() * mix.eventMaster;
    for (std::size_t i = 0; i < LayerCount; ++i) peakBudget += mix.assetLayers[i] + mix.fallbackLayers[i];
    const double trim = peakBudget > 0.82 ? 0.82 / peakBudget : 1.0;
    mix.adaptiveGain = trim < mix.adaptiveGain ? trim : Smooth(mix.adaptiveGain, trim, dt, 0.30);
    mix.whineGain *= mix.adaptiveGain; mix.musicGain *= mix.adaptiveGain; mix.eventMaster *= mix.adaptiveGain;
    for (std::size_t i = 0; i < LayerCount; ++i) { mix.assetLayers[i] *= mix.adaptiveGain; mix.fallbackLayers[i] *= mix.adaptiveGain; }
    StepMusic(dt);
    for (double& remaining : cooldown) remaining = std::max(0.0, remaining - dt);
    for (auto& voice : events)
    {
        voice.remaining = std::max(0.0, voice.remaining - dt);
        if (voice.remaining == 0.0) voice.cue = Cue::None;
    }
}

int State::TryEvent(Cue cue, double durationSeconds, std::uint32_t usableSlots) noexcept
{
    if (!IsEvent(cue) || current.paused || current.masterVolume <= 0.0 || mix.eventMaster <= 1e-5) return -1;
    if (!current.eva && !current.cockpit && !current.interiorMonitor) return -1;
    // Contact/gear sounds are heard through the suit; ship engine events cannot leak into EVA.
    if (cue >= Cue::Footstep1 && cue <= Cue::Footstep4 && !current.eva) return -1;
    if (!current.eva && cue >= Cue::SuitLatch && cue < Cue::Count) return -1;
    if (current.eva && (cue == Cue::RCS || (cue >= Cue::SpoolUp && cue <= Cue::CruiseDisengage))) return -1;
    const auto index = static_cast<std::size_t>(cue);
    if (cooldown[index] > 0.0) return -1;
    for (const auto& voice : events)
        if (voice.cue != Cue::None && Spec(voice.cue).eventGroup == Spec(cue).eventGroup) return -1;
    for (std::size_t i = 0; i < EventVoiceCount; ++i)
    {
        if (events[i].cue == Cue::None && (usableSlots & (1u << i)) != 0)
        {
            const double duration = std::isfinite(durationSeconds) ? std::clamp(durationSeconds, 0.03, 120.0) : Spec(cue).fallbackSeconds;
            events[i] = {cue, duration};
            cooldown[index] = Spec(cue).cooldownSeconds;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void State::ReleaseEvent(std::size_t slot) noexcept
{
    if (slot < events.size()) events[slot] = Voice{};
}

FallbackDSP::FallbackDSP(double sampleRate) noexcept
    : rate(std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 192000.0 ? sampleRate : 48000.0), dt(1.0 / rate),
      lowCoefficient(1.0 - std::exp(-Tau * 130.0 / rate)), midCoefficient(1.0 - std::exp(-Tau * 1600.0 / rate)),
      gainCoefficient(1.0 / (0.03 * rate)) {}

void FallbackDSP::SetMix(const std::array<double, LayerCount>& layers, double newThrottle, double newEventMaster, bool newPaused) noexcept
{
    paused = newPaused;
    for (std::size_t i = 0; i < LayerCount; ++i) targetLayers[i] = paused ? 0.0 : Unit(layers[i]);
    targetThrottle = Unit(newThrottle);
    targetEventMaster = paused ? 0.0 : Unit(newEventMaster);
    if (paused) { targetWhine = 0.0; for (auto& voice : voices) voice.releasing = true; }
}

void FallbackDSP::SetEngine(double gain, double newCharge, double newCruise, double newBraking) noexcept
{
    targetWhine = paused ? 0.0 : Unit(gain);
    targetCharge = Unit(newCharge); targetCruise = Unit(newCruise); targetBraking = Unit(newBraking);
}

void FallbackDSP::Trigger(std::size_t slot, Cue cue) noexcept
{
    if (slot < voices.size() && IsEvent(cue) && !paused && targetEventMaster > 0.0)
        voices[slot] = {cue, 0.0};
}

void FallbackDSP::Cancel(std::size_t slot) noexcept
{
    if (slot < voices.size()) voices[slot].releasing = true;
}

void FallbackDSP::Render(float* stereo, std::size_t frames) noexcept
{
    if (!stereo) return;
    const bool noVoices = std::all_of(voices.begin(), voices.end(), [](const EventVoice& voice) { return voice.cue == Cue::None; });
    const bool noLayers = std::all_of(gains.begin(), gains.end(), [](double gain) { return gain == 0.0; })
        && std::all_of(targetLayers.begin(), targetLayers.end(), [](double gain) { return gain == 0.0; });
    if (noVoices && noLayers && whine == 0.0 && targetWhine == 0.0)
    {
        eventMaster = targetEventMaster;
        std::fill(stereo, stereo + frames * 2, 0.0f);
        return;
    }
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        eventMaster = Move(eventMaster, targetEventMaster, gainCoefficient);
        throttle = Smooth(throttle, targetThrottle, dt, 0.035);
        whine = Move(whine, targetWhine, gainCoefficient);
        charge = Smooth(charge, targetCharge, dt, 0.04);
        cruise = Smooth(cruise, targetCruise, dt, 0.08);
        braking = Smooth(braking, targetBraking, dt, 0.04);
        for (std::size_t i = 0; i < LayerCount; ++i) gains[i] = Move(gains[i], targetLayers[i], gainCoefficient);
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const double noise = static_cast<double>(random) * (2.0 / 4294967295.0) - 1.0;
        low += lowCoefficient * (noise - low); mid += midCoefficient * (noise - mid);
        const std::array<double, LayerCount> frequencies{{53.0, 170.0, 36.0, 45.0 + 70.0 * throttle, 145.0 + 170.0 * throttle, 210.0, 350.0 + 180.0 * throttle, 115.0, 76.0}};
        for (std::size_t i = 0; i < LayerCount; ++i)
        {
            phases[i] += Tau * frequencies[i] * dt;
            if (phases[i] >= Tau) phases[i] -= Tau;
        }
        const std::array<double, LayerCount> signals{{
            0.30 * std::sin(phases[0]) + 0.35 * low,
            0.15 * std::sin(phases[1]) + 0.35 * (mid - low),
            0.45 * std::sin(phases[2]),
            0.60 * std::sin(phases[3]) + 0.15 * low,
            0.40 * std::sin(phases[4]),
            0.30 * std::sin(phases[5]) + 0.15 * std::sin(phases[5] * 2.0),
            0.35 * std::sin(phases[6]) + 0.12 * (mid - low),
            0.25 * std::sin(phases[7]) + 0.45 * (mid - low),
            0.30 * std::sin(phases[8]) + 0.25 * low,
        }};
        // Integrate frequency: changing power cannot reset phase or splice a waveform.
        const double hz = 150.0 + 1250.0 * std::pow(throttle, 1.3) + 480.0 * charge + 220.0 * cruise;
        whinePhase = std::fmod(whinePhase + Tau * hz * dt, Tau);
        bladePhase = std::fmod(bladePhase + Tau * (31.0 + 64.0 * throttle) * dt, Tau);
        // Harmonic frequencies remain under Nyquist even at the minimum supported sample rate.
        const double harmonicWeight = Unit((rate * 0.45 - hz * 2.0) / (rate * 0.05));
        const double harmonic = harmonicWeight * 0.22 * std::sin(2.0 * whinePhase);
        const double turbine = (0.68 * std::sin(whinePhase) + harmonic) * (0.93 + 0.07 * std::sin(bladePhase));
        double sample = whine * (turbine + 0.12 * cruise * (mid - low) + 0.22 * braking * low);
        for (std::size_t i = 0; i < LayerCount; ++i) sample += signals[i] * gains[i];
        for (auto& voice : voices)
        {
            if (voice.cue == Cue::None) continue;
            const auto& spec = Spec(voice.cue);
            if (voice.releasing) voice.releaseGain = Move(voice.releaseGain, 0.0, dt / 0.03);
            if (voice.age >= spec.fallbackSeconds || eventMaster == 0.0 || voice.releaseGain == 0.0) { voice = {}; continue; }
            const double t = voice.age;
            const double env = Window(t, spec.fallbackSeconds, 0.012, std::min(0.18, spec.fallbackSeconds * 0.7));
            double tone = 0.0;
            switch (spec.eventGroup)
            {
            case 0: tone = 0.55 * std::sin(Tau * (voice.cue == Cue::GearStow ? 145.0 : 165.0) * t); break;
            case 1: tone = 0.5 * std::sin(Tau * (voice.cue == Cue::ScanComplete ? 932.33 : 622.25) * t); break;
            case 2: tone = 0.75 * std::sin(Tau * 56.0 * t) * std::exp(-t / 0.22); break;
            case 3: tone = 0.6 * std::sin(Tau * 440.0 * t) * (0.65 + 0.35 * std::cos(Tau * 3.0 * t)); break;
            case 4: tone = 0.6 * std::sin(Tau * 950.0 * t) * std::exp(-t / 0.04); break;
            case 6: tone = 0.5 * std::sin(Tau * (180.0 * t + (voice.cue == Cue::SpoolDown ? -35.0 : 240.0) * t * t)); break;
            case 7: tone = 0.45 * std::sin(Tau * (280.0 * t + (voice.cue == Cue::CruiseDisengage ? -55.0 : 120.0) * t * t)) + 0.15 * (mid - low); break;
            case 8: tone = (0.5 * std::sin(Tau * (68.0 + 7.0 * (static_cast<int>(voice.cue) % 4)) * t) + 0.35 * mid) * std::exp(-t / 0.10); break;
            case 9: tone = 0.25 * std::sin(Tau * 105.0 * t) + 0.32 * (mid - low); break;
            case 10: tone = (0.35 * std::sin(Tau * 310.0 * t) + 0.20 * mid) * std::exp(-t / 0.12); break;
            case 5: tone = 0.4 * std::sin(Tau * 122.0 * t) + 0.3 * (mid - low); break;
            default: break;
            }
            sample += spec.eventGain * eventMaster * env * tone * voice.releaseGain;
            voice.age += dt;
        }
        const float output = static_cast<float>(sample / (1.0 + std::abs(sample)));
        stereo[frame * 2] = output; stereo[frame * 2 + 1] = output;
    }
}
}
