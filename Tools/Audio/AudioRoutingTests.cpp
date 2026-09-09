#include "StarAudioRouting.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

using namespace star::audio::routing;

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void Run(State& state, double seconds, int hz = 120)
{
    for (int i = 0; i < static_cast<int>(std::lround(seconds * hz)); ++i) state.Step(1.0 / hz);
}
double LoopSum(const Mix& mix)
{
    double total = 0;
    for (std::size_t i = 0; i < LayerCount; ++i) total += mix.assetLayers[i] + mix.fallbackLayers[i];
    return total;
}
void ValidateMix(const Mix& mix)
{
    for (std::size_t i = 0; i < LayerCount; ++i)
        Check(std::isfinite(mix.assetLayers[i]) && std::isfinite(mix.fallbackLayers[i]) && mix.assetLayers[i] >= 0 && mix.fallbackLayers[i] >= 0, "finite nonnegative layer gain");
    Check(std::isfinite(mix.eventMaster) && mix.eventMaster >= 0 && mix.eventMaster <= 1, "bounded event master");
    Check(mix.music[0].weight + mix.music[1].weight <= 1.0 + 1e-12, "music crossfade never amplifies above unity");
    Check(LoopSum(mix) + mix.musicGain + EventPeakReservation() * mix.eventMaster < 0.85, "combined mix has theoretical PCM peak headroom at max master");
}

void MappingAndMissingParts()
{
    Check(EventCue("Gear") == Cue::Gear && EventCue("GearDeploy") == Cue::GearDeploy && EventCue("GearStow") == Cue::GearStow, "gear compatibility and direction mapping");
    Check(EventCue("gEaR") == Cue::Gear && EventCue("rcs") == Cue::RCS, "case-insensitive FName compatibility");
    Check(EventCue("Scan") == Cue::Scan && EventCue("ScanComplete") == Cue::ScanComplete, "scan start and completion distinct");
    Check(EventCue("BGM_EARTH_SUNRISE") == Cue::None && EventCue("invented") == Cue::None, "music/unknown cannot become one-shot events");
    Check(EventCue("SFX_WARN_COLLISION") == Cue::Collision, "documented cue ID alias");
    State assets, baseline;
    Input input; input.masterVolume = 1; input.throttle = 0.85; input.cruise = true;
    assets.SetInput(input); baseline.SetInput(input);
    Run(assets, 1); Run(baseline, 1);
    Check(LoopSum(assets.Output()) > 0.15, "missing pack has quiet machinery fallback");
    for (std::size_t i = 0; i < LayerCount; ++i) Check(assets.Output().assetLayers[i] == 0, "missing assets never reported as active");
    // Only one asset arrives: only that fallback layer may disappear.
    assets.SetAvailable(Cue::EngineLow, true);
    for (int frame = 0; frame < 60; ++frame)
    {
        assets.Step(1.0 / 120); baseline.Step(1.0 / 120);
        ValidateMix(assets.Output());
        for (std::size_t i = 0; i < LayerCount; ++i)
            Check(std::abs(assets.Output().assetLayers[i] + assets.Output().fallbackLayers[i] - baseline.Output().fallbackLayers[i]) < 1e-12, "asset/fallback transition preserves one layer's gain budget");
    }
    Check(assets.Output().fallbackLayers[3] == 0 && assets.Output().assetLayers[3] > 0, "arriving engine asset replaces only its matching fallback");
    Check(assets.Output().fallbackLayers[0] > 0 && assets.Output().assetLayers[0] == 0, "missing cabin is still synthesized");
    for (std::size_t i = 0; i < LayerCount; ++i) assets.SetAvailable(static_cast<Cue>(i), true);
    Run(assets, 0.3);
    for (double gain : assets.Output().fallbackLayers) Check(gain == 0, "complete pack removes the procedural bed");
    std::cout << "PASS exact event mapping and missing-part-only asset crossfades\n";
}

void MusicSelectionAndRapidSwitches()
{
    State state;
    Input input; input.masterVolume = 1;
    state.SetInput(input); state.SetAvailable(Cue::MusicEarth, true); Run(state, 3);
    Check(state.Output().music[0].cue == Cue::MusicEarth && state.Output().music[0].weight == 1, "Earth music starts softly");
    input.body = Body::Moon; state.SetInput(input); Run(state, 1);
    Check(state.DesiredMusic() == Cue::MusicMoonApproach && state.Output().music[0].weight == 1, "missing next music retains audible current track");
    for (int i = static_cast<int>(Cue::MusicEarth); i <= static_cast<int>(Cue::MusicCruise); ++i) state.SetAvailable(static_cast<Cue>(i), true);
    auto previous = state.Output().music;
    for (int frame = 0; frame < 2400; ++frame)
    {
        if (frame % 31 == 0)
        {
            input.body = static_cast<Body>(1 + (frame / 31) % 4);
            input.landed = (frame / 31) % 2 == 0;
            input.cruise = frame % 124 == 0;
            state.SetInput(input);
        }
        state.Step(1.0 / 120);
        ValidateMix(state.Output());
        for (std::size_t slot = 0; slot < MusicVoiceCount; ++slot)
            if (previous[slot].cue != state.Output().music[slot].cue) Check(previous[slot].weight <= 1e-9, "rapid music changes never replace an audible slot");
        previous = state.Output().music;
    }
    input.body = Body::Moon; input.cruise = false; input.landed = true; state.SetInput(input);
    Check(state.DesiredMusic() == Cue::MusicMoonSurface, "actual landed state selects Moon surface music");
    input.body = Body::Space; input.landed = false; state.SetInput(input);
    Check(state.DesiredMusic() == Cue::MusicCruise, "explicit deep-space body selects cruise music");
    input.body = Body::Unknown; state.SetInput(input); Run(state, 3);
    Check(state.Output().music[0].weight == 0 && state.Output().music[1].weight == 0, "unknown environment fades music to silence");
    std::cout << "PASS actual environment selection, late music load and rapid two-voice crossfade\n";
}

void PauseMuteAndTime()
{
    State state;
    Input input; input.masterVolume = 1; input.throttle = 1;
    state.SetInput(input); Run(state, 2); const double cabin = LoopSum(state.Output());
    input.cockpit = false; input.interiorMonitor = false; state.SetInput(input); Run(state, 3);
    Check(LoopSum(state.Output()) < cabin * 0.026, "quiet exterior with no vacuum air layer");
    input.paused = true; state.SetInput(input);
    Check(LoopSum(state.Output()) == 0 && state.Output().musicGain == 0 && state.Output().eventMaster == 0, "pause immediately publishes silent routing targets");
    Check(state.TryEvent(Cue::Warning, 1) < 0, "pause discards events");
    input.paused = false; state.SetInput(input);
    Check(LoopSum(state.Output()) == 0, "resume starts at zero");
    state.Step(0.01);
    Check(state.Output().eventMaster > 0 && state.Output().eventMaster <= 0.04, "resume fades from zero within fixed gain slope");
    input.masterVolume = std::numeric_limits<double>::quiet_NaN(); input.throttle = std::numeric_limits<double>::infinity();
    state.SetInput(input); state.Step(1);
    Check(LoopSum(state.Output()) == 0 && state.Output().musicGain == 0, "invalid master fails silent");
    Check(state.TryEvent(Cue::View, 1) < 0, "mute discards events");
    state.Step(std::numeric_limits<double>::quiet_NaN()); ValidateMix(state.Output());
    State slow, fast;
    input = {}; input.throttle = 0.9; input.masterVolume = 0.8; input.cruise = true;
    slow.SetInput(input); fast.SetInput(input); Run(slow, 2, 30); Run(fast, 2, 240);
    Check(std::abs(LoopSum(slow.Output()) - LoopSum(fast.Output())) < 1e-12, "steady routing envelopes use elapsed seconds across frame rates");
    std::cout << "PASS pause, exact mute targets, zero-start resume, exterior and frame-rate envelopes\n";
}

void VoiceCapsAndCooldowns()
{
    State state; Input input; input.masterVolume = 1; state.SetInput(input); Run(state, 0.3);
    Check(state.TryEvent(Cue::GearDeploy, 4) == 0, "first gear voice accepted");
    Check(state.TryEvent(Cue::GearStow, 4) < 0, "gear family does not stack");
    Check(state.TryEvent(Cue::Scan, 4) == 1 && state.TryEvent(Cue::Landing, 4) == 2 && state.TryEvent(Cue::View, 4) == 3, "four independent event groups fit");
    Check(state.TryEvent(Cue::Warning, 4) < 0, "global event voice cap is four");
    state.ReleaseEvent(3);
    Check(state.TryEvent(Cue::View, 1) < 0, "early completion does not bypass cooldown");
    Check(state.TryEvent(Cue::Warning, 1, 0x0u) < 0, "fading audio slots cannot be reused");
    Check(state.TryEvent(Cue::Warning, 1, 0x8u) == 3, "available slot selected without spawning");
    Check(state.TryEvent(Cue::Collision, 1) < 0, "warnings share non-overlapping family");
    input.paused = true; state.SetInput(input); input.paused = false; state.SetInput(input); Run(state, 0.3);
    Check(state.TryEvent(Cue::RCS, 0.26) == 0, "pause clears old voice reservations");
    Run(state, 0.3);
    Check(state.TryEvent(Cue::RCS, 0.26) == 0, "completed RCS can fire again after cooldown");
    std::cout << "PASS four-voice cap, per-family overlap guard, cooldown and fade-slot guard\n";
}

void FallbackOnlyAndSilence()
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        FallbackDSP dsp(rate);
        std::vector<float> samples(static_cast<std::size_t>(rate) * 2, 1.0f);
        std::array<double, LayerCount> layers{};
        dsp.SetMix(layers, 1, 1, false); dsp.Render(samples.data(), samples.size() / 2);
        Check(std::all_of(samples.begin(), samples.end(), [](float x) { return x == 0; }), "complete asset mix produces no procedural background");
        layers[0] = 0.08;
        dsp.SetMix(layers, 0.5, 1, false); dsp.Render(samples.data(), samples.size() / 2);
        Check(std::any_of(samples.begin(), samples.end(), [](float x) { return std::abs(x) > 0.005f; }), "one missing cabin layer remains audible");
        for (std::size_t slot = 0; slot < EventVoiceCount; ++slot) dsp.Trigger(slot, static_cast<Cue>(static_cast<int>(Cue::Gear) + static_cast<int>(slot)));
        dsp.Render(samples.data(), samples.size() / 2);
        for (float sample : samples) Check(std::isfinite(sample) && std::abs(sample) < 0.5f, "fallback finite with simultaneous events");
        dsp.SetMix(layers, std::numeric_limits<double>::infinity(), 1, true);
        dsp.Trigger(0, Cue::Warning); dsp.Render(samples.data(), samples.size() / 2);
        const auto silenceStart = samples.begin() + static_cast<std::ptrdiff_t>(rate * 0.04) * 2;
        Check(std::all_of(silenceStart, samples.end(), [](float x) { return x == 0; }), "fallback pause settles to exact silence within 40 ms");
        layers.fill(0); dsp.SetMix(layers, 0, 1, false); dsp.Render(samples.data(), samples.size() / 2);
        Check(std::all_of(samples.begin(), samples.end(), [](float x) { return x == 0; }), "paused fallback events do not replay on resume");
        dsp.Trigger(0, Cue::ScanComplete);
        dsp.Render(samples.data(), samples.size() / 2);
        Check(std::any_of(samples.begin(), samples.end(), [](float x) { return std::abs(x) > 0.01f; }), "event-only fallback works with a complete asset bed");
        const auto afterEvent = samples.begin() + static_cast<std::ptrdiff_t>(rate * 0.65) * 2;
        Check(std::all_of(afterEvent, samples.end(), [](float x) { return x == 0; }), "actual fallback envelope ends at the same time across sample rates");
    }
    std::cout << "PASS isolated fallback, finite samples, actual envelopes and pause at 44.1/48/96 kHz\n";
}
}

int main(int argc, char** argv)
{
    MappingAndMissingParts(); MusicSelectionAndRapidSwitches(); PauseMuteAndTime(); VoiceCapsAndCooldowns(); FallbackOnlyAndSilence();
    if (argc == 2)
    {
        std::ofstream out(argv[1]);
        out << "{\"cues\":[";
        for (std::size_t i = 0; i < CueCount; ++i)
        {
            const auto& spec = Spec(static_cast<Cue>(i));
            if (i) out << ',';
            out << "{\"id\":\"" << spec.id << "\",\"loop\":" << (spec.loop ? "true" : "false") << '}';
        }
        out << "]}\n";
        Check(static_cast<bool>(out), "compiled routing contract written");
    }
    std::cout << "ALL PASS: native routing/fallback only; Unreal compilation and audible game acceptance are not covered.\n";
    return 0;
}
