#include "Core/StarSdlJoystick.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

static int failures = 0, checks = 0;
#define EXPECT(condition) do { ++checks; if (!(condition)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; } } while (false)
static bool Near(float a, float b) { return std::abs(a-b) < 0.0002f; }
static SDL_JoystickID Attach(Uint16 vendor, Uint16 product, SDL_JoystickType type = SDL_JOYSTICK_TYPE_FLIGHT_STICK) {
    SDL_VirtualJoystickDesc desc; SDL_INIT_INTERFACE(&desc);
    desc.type = static_cast<Uint16>(type); desc.vendor_id = vendor; desc.product_id = product;
    desc.naxes = 4; desc.nbuttons = 16; desc.nhats = 1; desc.name = "STAR synthetic test flight stick";
    return SDL_AttachVirtualJoystick(&desc);
}
int main() {
    using namespace star::input;
    SdlJoystickBackend backend;
    EXPECT(backend.Initialize());
    EXPECT(!SDL_GetHintBoolean(SDL_HINT_JOYSTICK_DIRECTINPUT, true));
    EXPECT(SDL_GetHintBoolean(SDL_HINT_JOYSTICK_WGI, false));
    EXPECT((SDL_WasInit(0) & (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) == 0);
    Selection selection; selection.VendorId = 0xfffd; selection.ProductId = 0xfffe;
    backend.SetSelection(selection);
    EXPECT(!backend.Poll().Connected); // independent of any real controllers present
    const auto id = Attach(selection.VendorId, selection.ProductId);
    EXPECT(id != 0);
    auto* writer = SDL_OpenJoystick(id); EXPECT(writer != nullptr);
    EXPECT(SDL_SetJoystickVirtualAxis(writer, 3, 32767));
    auto raw = backend.Poll();
    EXPECT(raw.Connected && raw.NumAxes == 4 && raw.NumButtons == 16 && raw.NumHats == 1);
    EXPECT(raw.AxisDataReady);
    Profile p;
    for (int axis = 0; axis < 4; ++axis) {
        EXPECT(SDL_SetJoystickVirtualAxis(writer, axis, -32768));
        raw = backend.Poll(); auto frame = MapControls(raw, p);
        const float values[] = {frame.Yaw, frame.Pitch, frame.Roll, frame.Throttle};
        EXPECT(Near(values[axis], axis == 3 ? 1.0f : -1.0f));
        EXPECT(SDL_SetJoystickVirtualAxis(writer, axis, 32767));
        raw = backend.Poll(); frame = MapControls(raw, p);
        const float upper[] = {frame.Yaw, frame.Pitch, frame.Roll, frame.Throttle};
        EXPECT(Near(upper[axis], axis == 3 ? 0.0f : 1.0f));
        EXPECT(SDL_SetJoystickVirtualAxis(writer, axis, axis == 3 ? 32767 : 0));
    }
    const Uint8 hats[] = {SDL_HAT_CENTERED,SDL_HAT_UP,SDL_HAT_RIGHTUP,SDL_HAT_RIGHT,SDL_HAT_RIGHTDOWN,SDL_HAT_DOWN,SDL_HAT_LEFTDOWN,SDL_HAT_LEFT,SDL_HAT_LEFTUP};
    const int lookX[] = {0,0,1,1,1,0,-1,-1,-1}, lookY[] = {0,1,1,0,-1,-1,-1,0,1};
    for (int i = 0; i < 9; ++i) {
        EXPECT(SDL_SetJoystickVirtualHat(writer, 0, hats[i]));
        const auto f = MapControls(backend.Poll(), p);
        EXPECT(Near(f.LookX, static_cast<float>(lookX[i])) && Near(f.LookY, static_cast<float>(lookY[i])));
    }
    EXPECT(SDL_SetJoystickVirtualHat(writer, 0, SDL_HAT_CENTERED));
    for (int i = 0; i < 16; ++i) {
        EXPECT(SDL_SetJoystickVirtualButton(writer, i, true));
        auto f = MapControls(backend.Poll(), p);
        for (int j = 0; j < 16; ++j) EXPECT(f.Buttons[j] == (i == j));
        EXPECT(SDL_SetJoystickVirtualButton(writer, i, false));
    }
    // Calibration, response, disabled/remapped buttons, and full persistence round trip.
    p.Axes[0] = {2,-30000,1000,29000,0.1f,2.0f,true}; p.ButtonMap[0] = 15; p.ButtonMap[15] = -1;
    p.HatIndex = -1; p.MappingConfirmed = true;
    EXPECT(Near(NormalizeAxis(1000, p.Axes[0], false), 0));
    EXPECT(Near(NormalizeAxis(29000, p.Axes[0], false), -1));
    EXPECT(Near(NormalizeAxis(-30000, p.Axes[0], false), 1));
    EXPECT(Near(NormalizeAxis(16400, p.Axes[0], false), -0.25f));
    EXPECT(SDL_SetJoystickVirtualButton(writer, 15, true));
    auto remapped = MapControls(backend.Poll(), p);
    EXPECT(remapped.Buttons[0] && !remapped.Buttons[15]);
    EXPECT(SDL_SetJoystickVirtualButton(writer, 15, false));
    auto invertedHat = Profile{}; invertedHat.InvertHatX = true; invertedHat.InvertHatY = true;
    EXPECT(SDL_SetJoystickVirtualHat(writer, 0, SDL_HAT_RIGHTUP));
    auto look = MapControls(backend.Poll(), invertedHat); EXPECT(look.LookX == -1 && look.LookY == -1);
    EXPECT(SDL_SetJoystickVirtualHat(writer, 0, SDL_HAT_CENTERED));
    std::string error; Profile parsed;
    const auto saved = SerializeProfile(p);
    { std::ofstream file("profile-roundtrip.starinput", std::ios::binary); file << saved; EXPECT(file.good()); }
    std::ifstream file("profile-roundtrip.starinput", std::ios::binary); std::ostringstream text; text << file.rdbuf();
    EXPECT(ParseProfile(text.str(), parsed, error)); EXPECT(SerializeProfile(parsed) == saved);
    EXPECT(!ParseProfile(saved + "unexpected", parsed, error)); EXPECT(SerializeProfile(parsed) == saved);
    EXPECT(!ParseProfile("STAR_INPUT_PROFILE 99", parsed, error));
    auto invalid = p; invalid.Axes[0].Minimum = invalid.Axes[0].Center;
    EXPECT(!ValidateProfile(invalid, error));
    auto badMapping = p; badMapping.Axes[0].Index = 31;
    EXPECT(!ProfileFitsDevice(badMapping, backend.Poll()));
    Selection loaded; selection.Guid = backend.ActiveDevice().Guid;
    EXPECT(ParseSelection(SerializeSelection(selection), loaded, error));
    EXPECT(loaded.Guid == selection.Guid && loaded.ProductId == selection.ProductId);
    EXPECT(!ParseSelection("STAR_INPUT_SELECTION 1\n1135 0 ../../escape", loaded, error));
    const auto desc = backend.ActiveDevice();
    auto wheel = desc; wheel.IsWheel = true; EXPECT(!MatchesSelection(wheel, selection));
    auto gamepad = desc; gamepad.IsGamepad = true; EXPECT(!MatchesSelection(gamepad, selection));
    auto wrong = selection; wrong.ProductId = 1; EXPECT(!MatchesSelection(desc, wrong));
    const auto wheelId = Attach(0x046d, 0xc24f, SDL_JOYSTICK_TYPE_WHEEL);
    const auto gamepadId = Attach(0xfffc, 1, SDL_JOYSTICK_TYPE_GAMEPAD);
    EXPECT(wheelId != 0 && gamepadId != 0);
    backend.Enumerate();
    EXPECT(SDL_GetJoystickFromID(wheelId) == nullptr);
    EXPECT(SDL_GetJoystickFromID(gamepadId) == nullptr);
    EXPECT(SDL_DetachVirtualJoystick(wheelId));
    EXPECT(SDL_DetachVirtualJoystick(gamepadId));
    // Physical-neutral safety cannot be defeated by response curve/deadzone.
    p = Profile{}; SafetyInterlock safety;
    raw = backend.Poll();
    for (int i = 0; i < 5; ++i) safety.Update(raw,p,true,true,0.1f);
    EXPECT(safety.IsArmed());
    auto unreadable = raw; unreadable.AxisDataReady = false;
    EXPECT(Near(safety.Update(unreadable,p,true,true,0.1f).Throttle, 0)); EXPECT(!safety.IsArmed());
    for (int i = 0; i < 5; ++i) safety.Update(raw,p,true,true,0.1f);
    EXPECT(safety.IsArmed());
    EXPECT(SDL_SetJoystickVirtualButton(writer, 0, true));
    raw = backend.Poll(); EXPECT(safety.Update(raw,p,true,true,0.1f).Buttons[0]);
    EXPECT(!safety.Update(raw,p,false,true,0.1f).Buttons[0]); EXPECT(!safety.IsArmed());
    for (int i = 0; i < 5; ++i) safety.Update(raw,p,true,true,0.1f);
    EXPECT(!safety.IsArmed()); // held trigger cannot arm
    EXPECT(SDL_SetJoystickVirtualButton(writer, 0, false));
    EXPECT(SDL_SetJoystickVirtualAxis(writer, 3, -32768)); raw = backend.Poll();
    for (int i = 0; i < 5; ++i) EXPECT(Near(safety.Update(raw,p,true,true,0.1f).Throttle, 0));
    EXPECT(!safety.IsArmed());
    EXPECT(SDL_SetJoystickVirtualAxis(writer, 3, 32767));
    EXPECT(SDL_SetJoystickVirtualAxis(writer, 0, 10000)); raw = backend.Poll();
    p.Axes[0].Deadzone = 0.5f; p.Axes[0].Exponent = 5;
    for (int i = 0; i < 5; ++i) safety.Update(raw,p,true,true,0.1f);
    EXPECT(!safety.IsArmed());
    EXPECT(SDL_SetJoystickVirtualAxis(writer, 0, 0)); raw = backend.Poll();
    for (int i = 0; i < 5; ++i) safety.Update(raw,p,true,true,0.1f);
    EXPECT(safety.IsArmed());
    safety.Update(raw,p,true,false,0.1f); EXPECT(!safety.IsArmed());
    for (int i = 0; i < 5; ++i) safety.Update(raw,p,true,true,0.1f);
    EXPECT(safety.IsArmed());
    EXPECT(SDL_DetachVirtualJoystick(id)); raw = backend.Poll();
    EXPECT(!raw.Connected); safety.Update(raw,p,true,true,0.1f); EXPECT(!safety.IsArmed());
    SDL_CloseJoystick(writer);
    const auto reconnected = Attach(selection.VendorId, selection.ProductId);
    writer = SDL_OpenJoystick(reconnected); EXPECT(writer != nullptr);
    EXPECT(SDL_SetJoystickVirtualAxis(writer, 3, -32768)); raw = backend.Poll(); EXPECT(raw.Connected);
    for (int i = 0; i < 5; ++i) safety.Update(raw,p,true,true,0.1f);
    EXPECT(!safety.IsArmed());
    EXPECT(SDL_SetJoystickVirtualAxis(writer, 3, 32767)); raw = backend.Poll();
    for (int i = 0; i < 5; ++i) safety.Update(raw,p,true,true,0.1f);
    EXPECT(safety.IsArmed());
    SDL_DetachVirtualJoystick(reconnected); backend.Poll(); SDL_CloseJoystick(writer);
    std::cout << "{\"suite\":\"SDL3 virtual joystick and pure input core\",\"checks\":" << checks << ",\"failures\":" << failures << ",\"physicalHardwareTested\":false}\n";
    return failures ? 1 : 0;
}
