#include "Core/StarInputCore.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace star::input {
Profile::Profile() {
    for (int i = 0; i < AxisCount; ++i) Axes[i].Index = i;
    Axes[3].Deadzone = 0.02f;
    Axes[3].Inverted = true;
    for (int i = 0; i < ButtonCount; ++i) ButtonMap[i] = i;
}
bool ValidateProfile(const Profile& p, std::string& error) {
    for (const auto& a : p.Axes) {
        if (a.Index < 0 || a.Index >= MaxRawAxes || a.Minimum < -32768 || a.Maximum > 32767 ||
            a.Minimum >= a.Center || a.Center >= a.Maximum ||
            !std::isfinite(a.Deadzone) || a.Deadzone < 0 || a.Deadzone > 0.5f ||
            !std::isfinite(a.Exponent) || a.Exponent < 0.2f || a.Exponent > 5.0f) {
            error = "Invalid axis index, calibration, deadzone or curve"; return false;
        }
    }
    for (int b : p.ButtonMap) if (b < -1 || b >= MaxRawButtons) {
        error = "Invalid physical button index"; return false;
    }
    if (p.HatIndex < -1 || p.HatIndex >= MaxRawHats ||
        !std::isfinite(p.NeutralThreshold) || p.NeutralThreshold < 0 || p.NeutralThreshold > 0.25f ||
        !std::isfinite(p.SafeThrottle) || p.SafeThrottle < 0 || p.SafeThrottle > 0.1f ||
        !std::isfinite(p.NeutralHoldSeconds) || p.NeutralHoldSeconds < 0.1f || p.NeutralHoldSeconds > 3) {
        error = "Invalid hat mapping or safe neutral limits"; return false;
    }
    error.clear(); return true;
}
bool ProfileFitsDevice(const Profile& p, const RawState& raw) {
    if (raw.NumAxes < 0 || raw.NumAxes > MaxRawAxes || raw.NumButtons < 0 ||
        raw.NumButtons > MaxRawButtons || raw.NumHats < 0 || raw.NumHats > MaxRawHats) return false;
    for (const auto& a : p.Axes) if (a.Index < 0 || a.Index >= raw.NumAxes) return false;
    for (int b : p.ButtonMap) if (b < -1 || b >= raw.NumButtons) return false;
    return p.HatIndex >= -1 && p.HatIndex < raw.NumHats;
}
float NormalizeAxis(std::int16_t value, const AxisBinding& a, bool throttle) {
    if (a.Minimum >= a.Center || a.Center >= a.Maximum) return 0;
    float v;
    if (throttle) {
        v = std::clamp((static_cast<float>(value) - a.Minimum) / (a.Maximum - a.Minimum), 0.0f, 1.0f);
        if (a.Inverted) v = 1 - v;
        v = std::clamp((v - a.Deadzone) / (1 - a.Deadzone), 0.0f, 1.0f);
        return std::pow(v, a.Exponent);
    }
    const int range = value >= a.Center ? a.Maximum - a.Center : a.Center - a.Minimum;
    v = std::clamp((static_cast<float>(value) - a.Center) / range, -1.0f, 1.0f);
    if (a.Inverted) v = -v;
    const float magnitude = std::clamp((std::abs(v) - a.Deadzone) / (1 - a.Deadzone), 0.0f, 1.0f);
    return std::copysign(std::pow(magnitude, a.Exponent), v);
}
ControlFrame MapControls(const RawState& raw, const Profile& p) {
    ControlFrame out;
    if (!raw.Connected || !raw.AxisDataReady || !ProfileFitsDevice(p, raw)) return out;
    out.Yaw = NormalizeAxis(raw.Axes[p.Axes[0].Index], p.Axes[0], false);
    out.Pitch = NormalizeAxis(raw.Axes[p.Axes[1].Index], p.Axes[1], false);
    out.Roll = NormalizeAxis(raw.Axes[p.Axes[2].Index], p.Axes[2], false);
    out.Throttle = NormalizeAxis(raw.Axes[p.Axes[3].Index], p.Axes[3], true);
    for (int i = 0; i < ButtonCount; ++i)
        out.Buttons[i] = p.ButtonMap[i] >= 0 && raw.Buttons[p.ButtonMap[i]];
    if (p.HatIndex >= 0) {
        const auto hat = raw.Hats[p.HatIndex]; // SDL_UP=1 RIGHT=2 DOWN=4 LEFT=8
        out.LookX = ((hat & 2) ? 1.0f : 0.0f) - ((hat & 8) ? 1.0f : 0.0f);
        out.LookY = ((hat & 1) ? 1.0f : 0.0f) - ((hat & 4) ? 1.0f : 0.0f);
        if (p.InvertHatX) out.LookX = -out.LookX;
        if (p.InvertHatY) out.LookY = -out.LookY;
    }
    return out;
}
void SafetyInterlock::Reset() { Armed = false; NeutralSeconds = 0; }
ControlFrame SafetyInterlock::Update(const RawState& raw, const Profile& p, bool focused,
                                    bool enabled, float seconds) {
    if (!raw.Connected || !raw.AxisDataReady || !focused || !enabled || !ProfileFitsDevice(p, raw)) { Reset(); return {}; }
    const auto mapped = MapControls(raw, p);
    if (!Armed) {
        // Safety uses linear calibrated position, not a curve that could conceal displacement.
        bool neutral = true;
        for (int i = 0; i < AxisCount; ++i) {
            auto linear = p.Axes[i]; linear.Exponent = 1; linear.Deadzone = 0;
            const float v = NormalizeAxis(raw.Axes[linear.Index], linear, i == 3);
            neutral &= i == 3 ? v <= p.SafeThrottle : std::abs(v) <= p.NeutralThreshold;
        }
        for (int i = 0; i < raw.NumButtons; ++i) neutral &= !raw.Buttons[i];
        for (int i = 0; i < raw.NumHats; ++i) neutral &= raw.Hats[i] == 0;
        NeutralSeconds = neutral ? NeutralSeconds + std::clamp(seconds, 0.0f, 0.1f) : 0;
        Armed = NeutralSeconds >= p.NeutralHoldSeconds;
        return {}; // The arming frame itself emits no actions.
    }
    return mapped;
}
std::string SerializeProfile(const Profile& p) {
    std::ostringstream s; s.imbue(std::locale::classic()); s << std::setprecision(9);
    s << "STAR_INPUT_PROFILE 1\n";
    for (const auto& a : p.Axes)
        s << "axis " << a.Index << ' ' << a.Minimum << ' ' << a.Center << ' ' << a.Maximum << ' '
          << a.Deadzone << ' ' << a.Exponent << ' ' << a.Inverted << '\n';
    s << "buttons"; for (int b : p.ButtonMap) s << ' ' << b;
    s << "\nhat " << p.HatIndex << ' ' << p.InvertHatX << ' ' << p.InvertHatY;
    s << "\nsafety " << p.NeutralThreshold << ' ' << p.SafeThrottle << ' ' << p.NeutralHoldSeconds;
    s << "\nconfirmed " << p.MappingConfirmed << '\n';
    return s.str();
}
bool ParseProfile(std::string_view text, Profile& out, std::string& error) {
    if (text.size() > 16384) { error = "Profile too large"; return false; }
    std::istringstream s{std::string(text)}; s.imbue(std::locale::classic());
    Profile p; std::string word; int version = 0;
    auto keyword = [&](const char* expected) { return static_cast<bool>(s >> word) && word == expected; };
    bool ok = keyword("STAR_INPUT_PROFILE") && static_cast<bool>(s >> version) && version == 1;
    for (auto& a : p.Axes) ok = ok && keyword("axis") && static_cast<bool>(s >> a.Index >> a.Minimum >> a.Center >> a.Maximum >> a.Deadzone >> a.Exponent >> a.Inverted);
    ok = ok && keyword("buttons");
    for (int& b : p.ButtonMap) ok = ok && static_cast<bool>(s >> b);
    ok = ok && keyword("hat") && static_cast<bool>(s >> p.HatIndex >> p.InvertHatX >> p.InvertHatY);
    ok = ok && keyword("safety") && static_cast<bool>(s >> p.NeutralThreshold >> p.SafeThrottle >> p.NeutralHoldSeconds);
    ok = ok && keyword("confirmed") && static_cast<bool>(s >> p.MappingConfirmed);
    s >> std::ws;
    if (!ok || !s.eof()) { error = "Malformed profile or unsupported version"; return false; }
    if (!ValidateProfile(p, error)) return false;
    out = p; return true;
}
std::string SerializeSelection(const Selection& p) {
    std::ostringstream s; s.imbue(std::locale::classic());
    s << "STAR_INPUT_SELECTION 1\n" << p.VendorId << ' ' << p.ProductId << ' ' << (p.Guid.empty() ? "-" : p.Guid) << '\n';
    return s.str();
}
bool ParseSelection(std::string_view text, Selection& out, std::string& error) {
    if (text.size() > 256) { error = "Selection too large"; return false; }
    std::istringstream s{std::string(text)}; s.imbue(std::locale::classic());
    std::string magic, guid; int version = 0, vendor = -1, product = -1;
    if (!(s >> magic >> version >> vendor >> product >> guid) || magic != "STAR_INPUT_SELECTION" || version != 1 ||
        vendor < 1 || vendor > 65535 || product < 0 || product > 65535) {
        error = "Invalid device selection"; return false;
    }
    s >> std::ws;
    if (!s.eof() || (guid != "-" && (guid.size() != 32 || guid.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos))) {
        error = "Invalid device GUID"; return false;
    }
    out.VendorId = static_cast<std::uint16_t>(vendor); out.ProductId = static_cast<std::uint16_t>(product);
    out.Guid = guid == "-" ? "" : guid;
    std::transform(out.Guid.begin(), out.Guid.end(), out.Guid.begin(), [](char c) { return static_cast<char>(c >= 'A' && c <= 'F' ? c + ('a' - 'A') : c); });
    error.clear(); return true;
}
}
