#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace star::audio
{
enum class Event : std::uint32_t
{
    Gear = 1u << 0, Scan = 1u << 1, Landing = 1u << 2,
    Warning = 1u << 3, View = 1u << 4, RCS = 1u << 5
};

struct Parameters
{
    float throttle = 0.0f;
    double speedMps = 0.0;
    float masterVolume = 0.7f;
    bool cockpit = true;
    bool paused = false;
};

/** Pure C++17 DSP. All methods belong to one audio thread; no allocation in Render(). */
class ShipAudioDSP
{
public:
    static constexpr int ChannelCount = 2;
    explicit ShipAudioDSP(double sampleRate = 48000.0) noexcept;
    void Reset(double sampleRate) noexcept;
    void SetParameters(const Parameters& parameters) noexcept;
    void Trigger(Event event) noexcept;
    void TriggerMask(std::uint32_t events) noexcept;
    /** Interleaved stereo; frameCount is a number of frames, not float samples. */
    void Render(float* interleaved, std::size_t frameCount) noexcept;

    double SampleRate() const noexcept { return rate; }
    double CurrentOutputGain() const noexcept { return outputGain; }
    double CurrentThrottle() const noexcept { return throttle; }
    static double EventDuration(Event event) noexcept;

private:
    struct Voice { double age = 0.0; double phase = 0.0; bool active = false; };
    Parameters target;
    double rate = 48000.0;
    double dt = 1.0 / 48000.0;
    double outputGain = 0.0;
    double cockpitBlend = 1.0;
    double throttle = 0.0;
    double speedBlend = 0.0;
    double gainStep = 0.0;
    double throttleCoeff = 0.0;
    double speedCoeff = 0.0;
    double viewCoeff = 0.0;
    double lowCoeff = 0.0;
    double airCoeff = 0.0;
    double airLowCoeff = 0.0;
    double dcCoeff = 0.0;
    double enginePhase = 0.0;
    double fanPhase = 0.0;
    double slowPhase = 0.0;
    std::uint32_t noiseState = 0x73544152u;
    std::array<double, 2> rumble{};
    std::array<double, 2> air{};
    std::array<double, 2> airLow{};
    std::array<double, 2> dcInput{};
    std::array<double, 2> dcOutput{};
    std::array<Voice, 6> voices{};

    double Noise() noexcept;
    double EventSample(std::size_t index) noexcept;
};
}
