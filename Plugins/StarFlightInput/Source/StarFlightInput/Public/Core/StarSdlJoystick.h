#pragma once
#include "Core/StarInputCore.h"
#include <vector>

struct SDL_Joystick;
namespace star::input {
struct DeviceDescriptor {
    std::uint32_t InstanceId = 0;
    std::uint16_t VendorId = 0, ProductId = 0;
    std::string Name, Guid;
    int NumAxes = 0, NumButtons = 0, NumHats = 0;
    bool IsWheel = false, IsGamepad = false, IsVirtual = false, Eligible = false;
};
bool MatchesSelection(const DeviceDescriptor& Device, const Selection& SelectionToMatch);
class SdlJoystickBackend {
public:
    SdlJoystickBackend() = default;
    ~SdlJoystickBackend();
    SdlJoystickBackend(const SdlJoystickBackend&) = delete;
    SdlJoystickBackend& operator=(const SdlJoystickBackend&) = delete;
    bool Initialize();
    void Shutdown();
    void SetSelection(const Selection& NewSelection);
    // Every call detects disconnect. Enumeration/reconnect runs at a bounded cadence in UE.
    RawState Poll(bool RefreshDevices = true);
    std::vector<DeviceDescriptor> Enumerate();
    const DeviceDescriptor& ActiveDevice() const { return Active; }
    const std::string& LastError() const { return Error; }
    bool IsInitialized() const { return Initialized; }
private:
    DeviceDescriptor Describe(std::uint32_t InstanceId, bool ReadCounts);
    bool Initialized = false;
    Selection Selected;
    DeviceDescriptor Active;
    SDL_Joystick* Joystick = nullptr;
    std::string Error;
};
}
