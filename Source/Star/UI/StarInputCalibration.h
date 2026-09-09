#pragma once
#include "Core/StarInputCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// UI-owned calibration policy, independent of Unreal and using the plugin's real raw units.
namespace star::input::calibration
{
enum class Point { Minimum, Center, Maximum };
enum class CaptureResult { Waiting, Captured, Invalid, TimedOut };
class StableCapture
{
public:
    void Start() { active_ = true; elapsed_ = stable_ = 0; samples_ = 0; total_ = 0; }
    void Cancel() { active_ = false; }
    bool Active() const { return active_; }
    CaptureResult Observe(int raw, double seconds, bool available, int& value)
    {
        if (!active_ || !available || raw < -32768 || raw > 32767 || !std::isfinite(seconds) || seconds <= 0 || seconds > 0.2)
        { Cancel(); return CaptureResult::Invalid; }
        elapsed_ += seconds;
        if (elapsed_ > 8) { Cancel(); return CaptureResult::TimedOut; }
        if (samples_ == 0 || raw < low_ - 256 || raw > high_ + 256 || std::max(high_, raw) - std::min(low_, raw) > 512)
        { low_ = high_ = raw; stable_ = 0; total_ = 0; samples_ = 0; }
        low_ = std::min(low_, raw); high_ = std::max(high_, raw);
        total_ += raw; ++samples_; stable_ += std::min(seconds, 0.1);
        if (stable_ + 1e-8 < 0.4 || samples_ < 6) return CaptureResult::Waiting;
        value = static_cast<int>(std::lround(static_cast<double>(total_) / samples_));
        Cancel(); return CaptureResult::Captured;
    }
private:
    bool active_ = false;
    double elapsed_ = 0, stable_ = 0;
    int low_ = 0, high_ = 0, samples_ = 0;
    std::int64_t total_ = 0;
};

struct Evidence
{
    int Source = -1;
    std::array<bool, 3> Measured{};
    std::array<int, 3> Values{};
    void Reset(int source) { Source = source; Measured = {}; Values = {}; }
    void Record(Point point, int raw)
    {
        const auto index = static_cast<std::size_t>(point);
        if (index < Values.size() && raw >= -32768 && raw <= 32767)
        { Values[index] = raw; Measured[index] = true; }
    }
    bool Ready() const
    {
        // These are game calibration tolerances, not a claim of full hardware resolution.
        return Source >= 0 && Measured[0] && Measured[1] && Measured[2] &&
            Values[0] >= -32768 && Values[2] <= 32767 &&
            Values[2] - Values[0] >= 30000 && Values[1] - Values[0] >= 4000 && Values[2] - Values[1] >= 4000;
    }
    bool Matches(const AxisBinding& axis) const
    { return Ready() && Source == axis.Index && Values[0] == axis.Minimum && Values[1] == axis.Center && Values[2] == axis.Maximum; }
};

enum class Validation { Ready, NoDevice, NoSamples, Unfocused, ChangedDevice, NeedMeasurements, NeedConfirmation, InvalidProfile, DuplicateAxis, DuplicateButton };
inline Validation BuildConfirmedProfile(const Profile& draft, const std::array<Evidence, AxisCount>& evidence,
    const RawState& raw, bool focused, bool sameConnection, bool userConfirmed, Profile& output)
{
    if (!raw.Connected) return Validation::NoDevice;
    if (!raw.AxisDataReady) return Validation::NoSamples;
    if (!focused) return Validation::Unfocused;
    if (!sameConnection) return Validation::ChangedDevice;
    std::string error;
    if (!ValidateProfile(draft, error) || !ProfileFitsDevice(draft, raw)) return Validation::InvalidProfile;
    for (int i = 0; i < AxisCount; ++i)
    {
        if (!evidence[i].Matches(draft.Axes[i])) return Validation::NeedMeasurements;
        for (int j = 0; j < i; ++j) if (draft.Axes[i].Index == draft.Axes[j].Index) return Validation::DuplicateAxis;
    }
    for (int i = 0; i < ButtonCount; ++i)
        for (int j = 0; j < i; ++j)
            if (draft.ButtonMap[i] >= 0 && draft.ButtonMap[i] == draft.ButtonMap[j]) return Validation::DuplicateButton;
    if (!userConfirmed) return Validation::NeedConfirmation;
    auto candidate = draft; candidate.MappingConfirmed = true;
    output = candidate; return Validation::Ready;
}

// Listening never consumes a button held when the listener was opened. Multiple simultaneous
// buttons return to the release gate; no arbitrary first-button winner is chosen.
class ButtonListener
{
public:
    void Start() { active_ = true; released_ = false; elapsed_ = 0; }
    void Cancel() { active_ = false; }
    bool Active() const { return active_; }
    bool WaitingForRelease() const { return !released_; }
    int Observe(const RawState& raw, double seconds)
    {
        if (!active_ || !raw.Connected || !raw.AxisDataReady || raw.NumButtons < 0 || raw.NumButtons > MaxRawButtons ||
            !std::isfinite(seconds) || seconds <= 0 || seconds > 0.2) { Cancel(); return -1; }
        elapsed_ += seconds;
        if (elapsed_ > 12) { Cancel(); return -1; }
        int count = 0, found = -1;
        for (int i = 0; i < raw.NumButtons; ++i) if (raw.Buttons[i]) { ++count; found = i; }
        if (count == 0) { released_ = true; return -1; }
        if (!released_) return -1;
        if (count > 1) { released_ = false; return -1; }
        Cancel(); return found;
    }
private:
    bool active_ = false, released_ = false;
    double elapsed_ = 0;
};
}
