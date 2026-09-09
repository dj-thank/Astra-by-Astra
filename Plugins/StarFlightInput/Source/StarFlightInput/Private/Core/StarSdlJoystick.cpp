#include "Core/StarSdlJoystick.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace star::input {
bool MatchesSelection(const DeviceDescriptor& d, const Selection& s) {
    return d.Eligible && !d.IsWheel && !d.IsGamepad && d.VendorId == s.VendorId &&
        (!s.ProductId || d.ProductId == s.ProductId) && (s.Guid.empty() || d.Guid == s.Guid);
}
SdlJoystickBackend::~SdlJoystickBackend() { Shutdown(); }
bool SdlJoystickBackend::Initialize() {
    if (Initialized) return true;
    // UE owns the window and application focus. No SDL video/audio/gamepad subsystem.
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1", SDL_HINT_OVERRIDE);
#ifdef _WIN32
    // SDL's stock DirectInput open takes exclusive access and can reset a wheel's
    // force feedback. Windows.Gaming.Input raw controllers use shared read access.
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_DIRECTINPUT, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_GAMEINPUT, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_RAWINPUT, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_WGI, "1", SDL_HINT_OVERRIDE);
#endif
    Initialized = SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    if (!Initialized) Error = SDL_GetError();
    return Initialized;
}
void SdlJoystickBackend::Shutdown() {
    if (Joystick) SDL_CloseJoystick(Joystick);
    Joystick = nullptr; Active = {};
    if (Initialized) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    Initialized = false;
}
void SdlJoystickBackend::SetSelection(const Selection& s) {
    if (Joystick) SDL_CloseJoystick(Joystick);
    Joystick = nullptr; Active = {}; Selected = s;
}
DeviceDescriptor SdlJoystickBackend::Describe(std::uint32_t id, bool counts) {
    DeviceDescriptor d; d.InstanceId = id;
    const char* name = SDL_GetJoystickNameForID(id); d.Name = name ? name : "Unknown joystick";
    d.VendorId = SDL_GetJoystickVendorForID(id); d.ProductId = SDL_GetJoystickProductForID(id);
    char guid[33]{}; SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid)); d.Guid = guid;
    const auto type = SDL_GetJoystickTypeForID(id);
    d.IsWheel = type == SDL_JOYSTICK_TYPE_WHEEL;
    // The entire Logitech G29/G920/G923 wheel family stays out, including unknown-type reports.
    d.IsWheel |= d.VendorId == 0x046d && (d.ProductId == 0xc24f || d.ProductId == 0xc260 || d.ProductId == 0xc262 || d.ProductId == 0xc266 || d.ProductId == 0xc267);
    d.IsGamepad = type == SDL_JOYSTICK_TYPE_GAMEPAD || SDL_IsGamepad(id);
    d.IsVirtual = SDL_IsJoystickVirtual(id);
    // Never open a wheel/gamepad: the stock DirectInput backend can acquire it
    // exclusively and reset force-feedback actuators merely while opening it.
    if (counts && !d.IsWheel && !d.IsGamepad) {
        SDL_Joystick* j = SDL_OpenJoystick(id);
        if (j) {
            d.NumAxes = SDL_GetNumJoystickAxes(j); d.NumButtons = SDL_GetNumJoystickButtons(j); d.NumHats = SDL_GetNumJoystickHats(j);
            SDL_CloseJoystick(j);
        }
    }
    d.Eligible = !d.IsWheel && !d.IsGamepad &&
        (type == SDL_JOYSTICK_TYPE_FLIGHT_STICK || type == SDL_JOYSTICK_TYPE_UNKNOWN) &&
        d.NumAxes >= AxisCount && d.NumButtons >= ButtonCount && d.NumHats >= 1;
    return d;
}
std::vector<DeviceDescriptor> SdlJoystickBackend::Enumerate() {
    std::vector<DeviceDescriptor> devices;
    if (!Initialized) return devices;
    SDL_UpdateJoysticks();
    int count = 0; auto* ids = SDL_GetJoysticks(&count);
    if (!ids) { Error = SDL_GetError(); return devices; }
    for (int i = 0; i < count; ++i) devices.push_back(Describe(ids[i], true));
    SDL_free(ids); return devices;
}
RawState SdlJoystickBackend::Poll(bool refresh) {
    RawState raw;
    if (!Initialized) return raw;
    SDL_UpdateJoysticks();
    // Drain only SDL joystick events, leaving unrelated subsystems' events untouched.
    SDL_Event events[64];
    while (SDL_PeepEvents(events, 64, SDL_GETEVENT, SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_UPDATE_COMPLETE) > 0) {}
    if (Joystick && !SDL_JoystickConnected(Joystick)) {
        SDL_CloseJoystick(Joystick); Joystick = nullptr; Active = {};
        return raw; // Expose an unambiguous disconnected frame even if replacement is already present.
    }
    if (!Joystick && refresh) {
        std::vector<DeviceDescriptor> matches;
        for (const auto& d : Enumerate()) if (MatchesSelection(d, Selected)) matches.push_back(d);
        if (matches.size() > 1) {
            Error = "Multiple matching flight sticks; select an unambiguous device GUID"; return raw;
        }
        if (matches.size() == 1) {
            Joystick = SDL_OpenJoystick(matches[0].InstanceId);
            if (Joystick) { Active = matches[0]; Error.clear(); }
            else Error = SDL_GetError();
        } else Error = "Selected flight stick is not connected";
    }
    if (!Joystick) return raw;
    raw.Connected = true;
    raw.NumAxes = std::clamp(SDL_GetNumJoystickAxes(Joystick), 0, MaxRawAxes);
    raw.NumButtons = std::clamp(SDL_GetNumJoystickButtons(Joystick), 0, MaxRawButtons);
    raw.NumHats = std::clamp(SDL_GetNumJoystickHats(Joystick), 0, MaxRawHats);
    raw.AxisDataReady = raw.NumAxes > 0;
    for (int i = 0; i < raw.NumAxes; ++i) {
        Sint16 Initial = 0;
        raw.AxisDataReady &= SDL_GetJoystickAxisInitialState(Joystick, i, &Initial);
        raw.Axes[i] = SDL_GetJoystickAxis(Joystick, i);
    }
    for (int i = 0; i < raw.NumButtons; ++i) raw.Buttons[i] = SDL_GetJoystickButton(Joystick, i);
    for (int i = 0; i < raw.NumHats; ++i) raw.Hats[i] = SDL_GetJoystickHat(Joystick, i);
    return raw;
}
}
