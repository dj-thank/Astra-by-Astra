#include "StarShipAudioDSP.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using star::audio::Event;
using star::audio::Parameters;
using star::audio::ShipAudioDSP;

namespace
{
int Checks = 0;
void Check(bool condition, const char* message)
{
    ++Checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

std::vector<float> Render(ShipAudioDSP& dsp, double seconds)
{
    std::vector<float> output(static_cast<std::size_t>(std::llround(seconds * dsp.SampleRate())) * 2);
    dsp.Render(output.data(), output.size() / 2);
    return output;
}

double RMS(const std::vector<float>& output, std::size_t start = 0, std::size_t end = 0)
{
    if (end == 0) end = output.size();
    double sum = 0.0;
    for (std::size_t i = start; i < end; ++i) sum += static_cast<double>(output[i]) * output[i];
    return std::sqrt(sum / static_cast<double>(end - start));
}

void FiniteAndBounded()
{
    for (const double sampleRate : {8000.0, 16000.0, 44100.0, 48000.0, 96000.0, 192000.0})
    {
        ShipAudioDSP dsp(sampleRate);
        Parameters parameters;
        std::vector<float> output(514 * 2, 2.0f);
        double peak = 0.0;
        for (int block = 0; block < 220; ++block)
        {
            parameters.throttle = static_cast<float>((block % 5) - 1) * 0.5f;
            parameters.speedMps = block % 2 ? 50.0 * 299792458.0 : 0.0;
            parameters.masterVolume = 1.0f;
            parameters.cockpit = block % 13 < 9;
            if (block % 23 == 0) parameters.throttle = std::numeric_limits<float>::quiet_NaN();
            if (block % 31 == 0) parameters.speedMps = std::numeric_limits<double>::infinity();
            if (block % 37 == 0) parameters.masterVolume = std::numeric_limits<float>::infinity();
            dsp.SetParameters(parameters);
            dsp.TriggerMask(0xffffffffu);
            // Guard values also verify frameCount cannot write beyond the supplied span.
            dsp.Render(output.data() + 2, 512);
            Check(output.front() == 2.0f && output.back() == 2.0f, "render buffer guards preserved");
            for (std::size_t i = 2; i < output.size() - 2; ++i)
            {
                Check(std::isfinite(output[i]) && std::abs(output[i]) <= 1.0f, "finite bounded audio under invalid controls/event flood");
                peak = std::max(peak, std::abs(static_cast<double>(output[i])));
            }
        }
        Check(peak > 0.02 && peak < 0.5, "audible signal with at least 6 dB peak headroom");
    }
    ShipAudioDSP invalid(std::numeric_limits<double>::quiet_NaN());
    Check(invalid.SampleRate() == 48000.0, "invalid sample rate uses safe default");
    invalid.Render(nullptr, 500);
    float guard = 123.0f;
    invalid.Render(&guard, 0);
    Check(guard == 123.0f, "zero frames preserve output");
    std::cout << "PASS finite/bounds, hostile parameters, six sample rates, buffer guards\n";
}

void DeterministicAndBlockIndependent()
{
    ShipAudioDSP whole(48000), blocks(48000);
    Parameters parameters; parameters.throttle = 0.83f; parameters.speedMps = 8.5e8;
    whole.SetParameters(parameters); blocks.SetParameters(parameters);
    whole.TriggerMask(0x3fu); blocks.TriggerMask(0x3fu);
    const auto expected = Render(whole, 2.0);
    std::vector<float> actual(expected.size());
    const std::size_t frames = actual.size() / 2;
    for (std::size_t offset = 0; offset < frames;)
    {
        const std::size_t count = std::min<std::size_t>(1 + (offset % 997), frames - offset);
        blocks.Render(actual.data() + offset * 2, count);
        offset += count;
    }
    Check(actual == expected, "deterministic output independent of callback chunk sizes");
    blocks.Reset(48000); blocks.SetParameters(parameters); blocks.TriggerMask(0x3fu);
    Check(Render(blocks, 2.0) == expected, "reset restores deterministic oscillator/filter/noise state");
    std::cout << "PASS deterministic reset and callback-size independence\n";
}

void RampsAndMute()
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        ShipAudioDSP dsp(rate);
        Parameters parameters; parameters.masterVolume = 1.0f; parameters.throttle = 1.0f;
        dsp.SetParameters(parameters);
        float frame[2]{};
        double previous = 0.0;
        for (int i = 0; i < static_cast<int>(rate * 0.05); ++i)
        {
            dsp.Render(frame, 1);
            const double gain = dsp.CurrentOutputGain();
            Check(gain >= previous && gain - previous <= 1.0 / (0.035 * rate) + 1e-12, "bounded monotonic sample gain ramp");
            previous = gain;
        }
        Check(dsp.CurrentOutputGain() == 1.0, "full gain reached within 35 ms");
        Check(dsp.CurrentThrottle() > 0.23 && dsp.CurrentThrottle() < 0.25, "throttle response based on seconds, not frames");
        Render(dsp, 0.3);
        parameters.paused = true;
        dsp.SetParameters(parameters);
        dsp.Trigger(Event::Warning);
        const auto paused = Render(dsp, 0.10);
        const auto firstSilent = paused.begin() + static_cast<std::ptrdiff_t>(std::ceil(rate * 0.036)) * 2;
        Check(std::all_of(firstSilent, paused.end(), [](float sample) { return sample == 0.0f; }), "pause reaches exact silence with no tail beyond ramp");
        parameters.paused = false; dsp.SetParameters(parameters); Render(dsp, 0.1);
        parameters.masterVolume = 0.0f; dsp.SetParameters(parameters);
        const auto muted = Render(dsp, 0.1);
        Check(RMS(muted, static_cast<std::size_t>(rate * 0.04) * 2) == 0.0, "master mute reaches exact silence");
        parameters.masterVolume = std::numeric_limits<float>::quiet_NaN(); dsp.SetParameters(parameters);
        Check(RMS(Render(dsp, 0.1)) == 0.0, "invalid master volume fails silent");
    }
    ShipAudioDSP withEvents, control;
    Parameters paused; paused.paused = true;
    withEvents.SetParameters(paused); control.SetParameters(paused);
    withEvents.TriggerMask(0x3fu);
    Check(Render(withEvents, 0.2) == Render(control, 0.2), "events ignored while paused");
    paused.paused = false; withEvents.SetParameters(paused); control.SetParameters(paused);
    Check(Render(withEvents, 0.8) == Render(control, 0.8), "paused events never replay on resume");
    std::cout << "PASS 35 ms ramps, sample-rate throttle response, pause and mute\n";
}

void EnvelopeTiming()
{
    std::vector<double> energies;
    std::vector<double> endTimes;
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        ShipAudioDSP event(rate), baseline(rate);
        Render(event, 0.2); Render(baseline, 0.2);
        event.Trigger(Event::Scan);
        auto signal = Render(event, 1.1);
        const auto bed = Render(baseline, 1.1);
        for (std::size_t i = 0; i < signal.size(); ++i) signal[i] -= bed[i];
        energies.push_back(RMS(signal, static_cast<std::size_t>(rate * 0.03) * 2, static_cast<std::size_t>(rate * 0.62) * 2));
        std::size_t lastFrame = 0;
        for (std::size_t i = 0; i < signal.size(); i += 2)
            if (std::abs(signal[i]) > 1e-6f) lastFrame = i / 2;
        endTimes.push_back(static_cast<double>(lastFrame) / rate);
        Check(RMS(signal, static_cast<std::size_t>(rate * 0.90) * 2) < 1e-8, "scan envelope and DC filter settle after event duration");
        Check(energies.back() > 0.004, "scan event is present");
    }
    const auto energyBounds = std::minmax_element(energies.begin(), energies.end());
    const auto timeBounds = std::minmax_element(endTimes.begin(), endTimes.end());
    Check(*energyBounds.second / *energyBounds.first < 1.05, "envelope energy stays within 5 percent across sample rates");
    Check(*timeBounds.second - *timeBounds.first < 0.003, "audible event end stays within 3 ms across sample rates");
    std::cout << "PASS audible envelope energy and duration at 44.1/48/96 kHz\n";
}

void ExteriorAndSignals()
{
    ShipAudioDSP cockpit, exterior;
    Parameters parameters; parameters.throttle = 0.8f;
    cockpit.SetParameters(parameters);
    parameters.cockpit = false; exterior.SetParameters(parameters);
    Render(cockpit, 1.0); Render(exterior, 1.0);
    const double cabinRms = RMS(Render(cockpit, 1.0));
    const double exteriorRms = RMS(Render(exterior, 1.0));
    Check(exteriorRms < cabinRms * 0.035, "vacuum exterior at least 29 dB quieter than cabin");
    ShipAudioDSP idle, powered;
    parameters.cockpit = true; parameters.throttle = 0; idle.SetParameters(parameters);
    parameters.throttle = 1; powered.SetParameters(parameters);
    Render(idle, 1.0); Render(powered, 1.0);
    Check(RMS(Render(powered, 0.5)) > RMS(Render(idle, 0.5)) * 3.0, "throttle produces meaningful mechanical engine change");
    for (const auto eventType : {Event::Gear, Event::Scan, Event::Landing, Event::Warning, Event::View, Event::RCS})
    {
        ShipAudioDSP event, baseline;
        Render(event, 0.2); Render(baseline, 0.2);
        event.Trigger(eventType);
        auto signal = Render(event, 1.8); const auto bed = Render(baseline, 1.8);
        for (std::size_t i = 0; i < signal.size(); ++i) signal[i] -= bed[i];
        Check(RMS(signal) > 1e-5, "each published event generates nonzero audio");
        Check(RMS(signal, static_cast<std::size_t>((ShipAudioDSP::EventDuration(eventType) + 0.3) * 48000.0) * 2) < 1e-7,
            "each published event finishes and settles");
    }
    std::cout << "PASS exterior attenuation, throttle response, all six finite-duration events\n";
}
}

int main()
{
    FiniteAndBounded(); DeterministicAndBlockIndependent(); RampsAndMute(); EnvelopeTiming(); ExteriorAndSignals();
    std::cout << "ALL PASS (" << Checks << " assertions; native DSP only, Unreal/device tests pending)\n";
    return 0;
}
