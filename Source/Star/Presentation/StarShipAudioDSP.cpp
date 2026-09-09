#include "StarShipAudioDSP.h"

#include <algorithm>
#include <cmath>

namespace star::audio
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double Tau = 2.0 * Pi;
constexpr std::array<Event, 6> Events{Event::Gear, Event::Scan, Event::Landing,
    Event::Warning, Event::View, Event::RCS};

double SafeUnit(double value, double fallback = 0.0) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
}

double Coefficient(double seconds, double rate) noexcept
{
    return 1.0 - std::exp(-1.0 / (seconds * rate));
}

double LowPassCoefficient(double hz, double rate) noexcept
{
    return 1.0 - std::exp(-Tau * hz / rate);
}

// Finite duration, raised-cosine attack/release. Time is seconds at every sample rate.
double Envelope(double age, double duration, double attack, double release) noexcept
{
    if (age <= 0.0 || age >= duration) return 0.0;
    const double up = std::min(age / attack, 1.0);
    const double down = std::min((duration - age) / release, 1.0);
    return (0.5 - 0.5 * std::cos(Pi * up)) * (0.5 - 0.5 * std::cos(Pi * down));
}

void AdvancePhase(double& phase, double hz, double dt) noexcept
{
    phase += Tau * hz * dt;
    if (phase >= Tau) phase -= Tau;
}
}

ShipAudioDSP::ShipAudioDSP(double sampleRate) noexcept { Reset(sampleRate); }

void ShipAudioDSP::Reset(double sampleRate) noexcept
{
    rate = std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 192000.0 ? sampleRate : 48000.0;
    dt = 1.0 / rate;
    target = Parameters{};
    outputGain = 0.0;
    cockpitBlend = 1.0;
    throttle = speedBlend = 0.0;
    // At most 35 ms to traverse full scale, including mute/pause. Exact zero is reached.
    gainStep = 1.0 / (0.035 * rate);
    throttleCoeff = Coefficient(0.18, rate);
    speedCoeff = Coefficient(0.4, rate);
    viewCoeff = Coefficient(0.10, rate);
    lowCoeff = LowPassCoefficient(130.0, rate);
    airCoeff = LowPassCoefficient(1700.0, rate);
    airLowCoeff = LowPassCoefficient(380.0, rate);
    dcCoeff = std::exp(-Tau * 18.0 / rate);
    enginePhase = fanPhase = slowPhase = 0.0;
    noiseState = 0x73544152u;
    rumble.fill(0.0); air.fill(0.0); airLow.fill(0.0);
    dcInput.fill(0.0); dcOutput.fill(0.0);
    voices.fill(Voice{});
}

void ShipAudioDSP::SetParameters(const Parameters& parameters) noexcept
{
    target.throttle = static_cast<float>(SafeUnit(parameters.throttle));
    target.speedMps = std::isfinite(parameters.speedMps) ? std::clamp(parameters.speedMps, 0.0, 50.0 * 299792458.0) : 0.0;
    target.masterVolume = static_cast<float>(SafeUnit(parameters.masterVolume));
    target.cockpit = parameters.cockpit;
    target.paused = parameters.paused;
}

double ShipAudioDSP::EventDuration(Event event) noexcept
{
    switch (event)
    {
    case Event::Gear: return 1.35;
    case Event::Scan: return 0.62;
    case Event::Landing: return 1.10;
    case Event::Warning: return 0.72;
    case Event::View: return 0.10;
    case Event::RCS: return 0.26;
    default: return 0.0;
    }
}

void ShipAudioDSP::Trigger(Event event) noexcept
{
    if (target.paused || target.masterVolume <= 0.0f) return;
    for (std::size_t i = 0; i < Events.size(); ++i)
    {
        // Bounded polyphony; a repeated event cannot restart a non-zero waveform and click.
        if (Events[i] == event && !voices[i].active) voices[i] = {0.0, 0.0, true};
    }
}

void ShipAudioDSP::TriggerMask(std::uint32_t events) noexcept
{
    for (const Event event : Events)
        if ((events & static_cast<std::uint32_t>(event)) != 0) Trigger(event);
}

double ShipAudioDSP::Noise() noexcept
{
    // Fixed seed, independent of wall time. Never use global RNG state on the audio thread.
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return static_cast<double>(noiseState) * (2.0 / 4294967295.0) - 1.0;
}

double ShipAudioDSP::EventSample(std::size_t index) noexcept
{
    Voice& voice = voices[index];
    if (!voice.active) return 0.0;
    const Event event = Events[index];
    const double t = voice.age;
    const double duration = EventDuration(event);
    if (t >= duration) { voice.active = false; return 0.0; }
    double sample = 0.0;
    switch (event)
    {
    case Event::Gear:
    {
        const double env = Envelope(t, duration, 0.07, 0.20);
        AdvancePhase(voice.phase, 138.0 + 24.0 * std::sin(Pi * t / duration), dt);
        const double latch = Envelope(t - 1.08, 0.18, 0.008, 0.13);
        sample = 0.036 * env * (std::sin(voice.phase) + 0.18 * std::sin(2.0 * voice.phase));
        sample += 0.032 * latch * std::sin(Tau * 76.0 * t);
        break;
    }
    case Event::Scan:
        sample = 0.030 * Envelope(t, 0.24, 0.018, 0.17) * std::sin(Tau * 622.25 * t)
            + 0.026 * Envelope(t - 0.27, 0.35, 0.018, 0.25) * std::sin(Tau * 932.33 * t);
        break;
    case Event::Landing:
        sample = 0.14 * Envelope(t, duration, 0.012, 0.5) * std::exp(-t / 0.23)
            * (0.72 * std::sin(Tau * 47.0 * t) + 0.28 * std::sin(Tau * 81.0 * t));
        break;
    case Event::Warning:
    {
        const double pulses = Envelope(t, 0.24, 0.022, 0.10) + Envelope(t - 0.36, 0.30, 0.022, 0.14);
        sample = 0.040 * pulses * (std::sin(Tau * 440.0 * t) + 0.15 * std::sin(Tau * 660.0 * t));
        break;
    }
    case Event::View:
        sample = 0.025 * Envelope(t, duration, 0.004, 0.07) * std::exp(-t / 0.035)
            * (0.8 * std::sin(Tau * 950.0 * t) + 0.2 * std::sin(Tau * 1425.0 * t));
        break;
    case Event::RCS:
        // Hull-conducted valve resonance and filtered air, not a vacuum whoosh.
        sample = Envelope(t, duration, 0.012, 0.20)
            * (0.026 * std::sin(Tau * 122.0 * t) * std::exp(-t / 0.09)
                + 0.027 * (air[0] - airLow[0]));
        break;
    default: break;
    }
    voice.age += dt;
    return sample;
}

void ShipAudioDSP::Render(float* interleaved, std::size_t frameCount) noexcept
{
    if (interleaved == nullptr) return;
    const double gainTarget = target.paused ? 0.0 : target.masterVolume;
    const double viewTarget = target.cockpit ? 1.0 : 0.0;
    // Velocity only gently changes machinery character. There is no wind sound in vacuum.
    const double speedTarget = std::log1p(target.speedMps) / std::log1p(50.0 * 299792458.0);
    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
        outputGain += std::clamp(gainTarget - outputGain, -gainStep, gainStep);
        // Retire voices only after fading to zero, so a very fast pause/resume cannot cut
        // a nonzero transient abruptly. Muted/paused triggers are never queued here.
        if (outputGain == 0.0 && gainTarget == 0.0) voices.fill(Voice{});
        throttle += throttleCoeff * (target.throttle - throttle);
        speedBlend += speedCoeff * (speedTarget - speedBlend);
        cockpitBlend += viewCoeff * (viewTarget - cockpitBlend);
        AdvancePhase(enginePhase, 36.0 + 52.0 * throttle + 4.0 * speedBlend, dt);
        AdvancePhase(fanPhase, 117.0, dt);
        AdvancePhase(slowPhase, 0.37, dt);
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            const double noise = Noise();
            rumble[ch] += lowCoeff * (noise - rumble[ch]);
            air[ch] += airCoeff * (noise - air[ch]);
            airLow[ch] += airLowCoeff * (air[ch] - airLow[ch]);
        }
        double transient = 0.0;
        for (std::size_t i = 0; i < voices.size(); ++i) transient += EventSample(i);
        const double motor = (0.011 + 0.095 * throttle)
            * (std::sin(enginePhase) + 0.24 * std::sin(2.0 * enginePhase) + 0.075 * std::sin(3.0 * enginePhase));
        // Exterior is a quiet, explicitly artistic hull-monitor perspective (-32 dB motor).
        const double machineryGain = 0.025 + 0.975 * cockpitBlend;
        const double eventGain = 0.015 + 0.985 * cockpitBlend;
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            const double offset = ch == 0 ? -0.08 : 0.08;
            const double cabin = (0.048 * (air[ch] - airLow[ch])
                + 0.0038 * std::sin(fanPhase + offset)) * (0.94 + 0.06 * std::sin(slowPhase));
            const double raw = cabin * cockpitBlend
                + machineryGain * (motor + (0.012 + 0.038 * throttle) * rumble[ch])
                + transient * eventGain;
            const double dcFree = raw - dcInput[ch] + dcCoeff * dcOutput[ch];
            dcInput[ch] = raw; dcOutput[ch] = dcFree;
            // Smooth, monotonic saturation with headroom; master ramp follows it for exact mute.
            interleaved[frame * 2 + ch] = static_cast<float>(outputGain * dcFree / (1.0 + std::abs(dcFree)));
        }
    }
}
}
