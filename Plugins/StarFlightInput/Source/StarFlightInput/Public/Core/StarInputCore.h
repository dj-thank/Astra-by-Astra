#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#ifdef STARFLIGHTINPUT_API
#define STAR_INPUT_API STARFLIGHTINPUT_API
#else
#define STAR_INPUT_API
#endif

namespace star::input {
constexpr int AxisCount = 4;
constexpr int ButtonCount = 16;
constexpr int MaxRawAxes = 32;
constexpr int MaxRawButtons = 128;
constexpr int MaxRawHats = 8;

// SDL axis numbers are a provisional initial mapping, never a measured hardware claim.
struct AxisBinding {
    int Index = 0;
    int Minimum = -32768;
    int Center = 0;
    int Maximum = 32767;
    float Deadzone = 0.06f;
    float Exponent = 1.0f;
    bool Inverted = false;
};
struct Profile {
    std::array<AxisBinding, AxisCount> Axes{}; // yaw, pitch, roll, throttle
    std::array<int, ButtonCount> ButtonMap{}; // logical action -> physical button; -1 disables
    int HatIndex = 0;
    bool InvertHatX = false;
    bool InvertHatY = false;
    float NeutralThreshold = 0.12f;
    float SafeThrottle = 0.05f;
    float NeutralHoldSeconds = 0.35f;
    bool MappingConfirmed = false;
    STAR_INPUT_API Profile();
};
struct Selection {
    std::uint16_t VendorId = 0x044f; // Thrustmaster; no wheel/gamepad fallback
    std::uint16_t ProductId = 0; // zero accepts a flight stick from this vendor
    std::string Guid; // empty accepts matching VID/PID; otherwise exact SDL hardware GUID
};
struct RawState {
    std::array<std::int16_t, MaxRawAxes> Axes{};
    std::array<bool, MaxRawButtons> Buttons{};
    std::array<std::uint8_t, MaxRawHats> Hats{};
    int NumAxes = 0, NumButtons = 0, NumHats = 0;
    bool Connected = false;
    bool AxisDataReady = false; // enumeration alone does not mean a hardware sample exists
};
struct ControlFrame {
    float Yaw = 0, Pitch = 0, Roll = 0, Throttle = 0;
    float LookX = 0, LookY = 0;
    std::array<bool, ButtonCount> Buttons{};
};
STAR_INPUT_API bool ValidateProfile(const Profile& ProfileToCheck, std::string& Error);
STAR_INPUT_API bool ProfileFitsDevice(const Profile& ProfileToCheck, const RawState& Raw);
STAR_INPUT_API float NormalizeAxis(std::int16_t Value, const AxisBinding& Binding, bool Throttle);
STAR_INPUT_API ControlFrame MapControls(const RawState& Raw, const Profile& Settings);
STAR_INPUT_API std::string SerializeProfile(const Profile& Settings);
STAR_INPUT_API bool ParseProfile(std::string_view Text, Profile& OutProfile, std::string& Error);
STAR_INPUT_API std::string SerializeSelection(const Selection& Settings);
STAR_INPUT_API bool ParseSelection(std::string_view Text, Selection& OutSelection, std::string& Error);

// A disconnected/unfocused/disabled frame always clears all controls. Rearming requires
// centered physical attitude axes, low throttle, released buttons and centered POV.
class SafetyInterlock {
public:
    ControlFrame Update(const RawState& Raw, const Profile& Settings, bool Focused,
                        bool GameplayEnabled, float DeltaSeconds);
    void Reset();
    bool IsArmed() const { return Armed; }
private:
    bool Armed = false;
    float NeutralSeconds = 0;
};
}
