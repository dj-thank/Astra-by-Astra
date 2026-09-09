#include "StarShipAudioDSP.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void Write16(std::ofstream& out, std::uint16_t value)
{
    const char bytes[2]{static_cast<char>(value & 255), static_cast<char>((value >> 8) & 255)};
    out.write(bytes, 2);
}
void Write32(std::ofstream& out, std::uint32_t value)
{
    Write16(out, static_cast<std::uint16_t>(value & 65535)); Write16(out, static_cast<std::uint16_t>(value >> 16));
}

void WriteWav(const std::filesystem::path& path, const std::vector<float>& samples)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot create output WAV");
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4); Write32(out, bytes + 36); out.write("WAVEfmt ", 8);
    Write32(out, 16); Write16(out, 1); Write16(out, 2); Write32(out, 48000); Write32(out, 192000);
    Write16(out, 4); Write16(out, 16); out.write("data", 4); Write32(out, bytes);
    for (float sample : samples)
    {
        const auto pcm = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        Write16(out, static_cast<std::uint16_t>(pcm));
    }
    if (!out) throw std::runtime_error("WAV write failed");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) { std::cerr << "Usage: RenderAudioPreview.exe OUTPUT_DIRECTORY\n"; return 2; }
        const auto outputDirectory = std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(outputDirectory);
        constexpr int Rate = 48000;
        constexpr int Seconds = 30;
        constexpr int Block = 480;
        std::vector<float> samples(static_cast<std::size_t>(Rate) * Seconds * 2);
        star::audio::ShipAudioDSP dsp(Rate);
        star::audio::Parameters parameters;
        // This scripted preview exercises the exact runtime DSP. It is not recorded gameplay.
        for (int frame = 0; frame < Rate * Seconds; frame += Block)
        {
            const double t = static_cast<double>(frame) / Rate;
            parameters.throttle = t < 3 ? 0.0f : t < 8 ? static_cast<float>((t - 3.0) / 5.0)
                : t < 14 ? 0.85f : t < 18 ? 0.25f : 0.0f;
            parameters.speedMps = t < 3 ? 0 : std::min((t - 3.0) * 160.0, 2400.0);
            parameters.cockpit = !(t >= 10 && t < 13);
            parameters.paused = t >= 24 && t < 26;
            parameters.masterVolume = t >= 28 ? 0.0f : 0.7f;
            dsp.SetParameters(parameters);
            if (frame == Rate * 1) dsp.Trigger(star::audio::Event::View);
            if (frame == Rate * 2 || frame == Rate * 16) dsp.Trigger(star::audio::Event::Gear);
            if (frame == Rate * 6 || frame == Rate * 15) dsp.Trigger(star::audio::Event::RCS);
            if (frame == Rate * 8 || frame == Rate * 22) dsp.Trigger(star::audio::Event::Scan);
            if (frame == Rate * 17) dsp.Trigger(star::audio::Event::Warning);
            if (frame == Rate * 19) dsp.Trigger(star::audio::Event::Landing);
            dsp.Render(samples.data() + static_cast<std::size_t>(frame) * 2, Block);
        }
        WriteWav(outputDirectory / "star-cockpit-audio-preview.wav", samples);
        double peak = 0, sum = 0;
        for (const float value : samples) { peak = std::max(peak, std::abs(static_cast<double>(value))); sum += static_cast<double>(value) * value; }
        std::ofstream report(outputDirectory / "audio-preview-metrics.json");
        report << "{\n  \"kind\": \"scripted DSP preview; not recorded gameplay\",\n  \"sample_rate_hz\": 48000,\n  \"channels\": 2,\n"
            << "  \"pcm_bits\": 16,\n  \"duration_seconds\": 30,\n  \"normalization_applied\": false,\n  \"peak_linear\": " << peak
            << ",\n  \"peak_dbfs\": " << 20 * std::log10(peak) << ",\n  \"rms_dbfs\": " << 20 * std::log10(std::sqrt(sum / static_cast<double>(samples.size()))) << "\n}\n";
        std::cout << "Generated 30 s / 48 kHz stereo PCM preview; no playback. Peak " << 20 * std::log10(peak) << " dBFS\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
