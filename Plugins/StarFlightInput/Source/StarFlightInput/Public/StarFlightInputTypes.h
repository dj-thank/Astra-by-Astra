#pragma once
#include "CoreMinimal.h"
#include "Core/StarInputCore.h"

// Plain C++ types, no UObject lifetime required. All plugin calls are game-thread only.
using FStarFlightProfile = star::input::Profile;
using FStarFlightAxisBinding = star::input::AxisBinding;
using FStarFlightControlFrame = star::input::ControlFrame;
using FStarFlightRawState = star::input::RawState;

struct STARFLIGHTINPUT_API FStarFlightDeviceSelection
{
    uint16 VendorId = 0x044f;
    uint16 ProductId = 0;
    FString GUID;
};
struct STARFLIGHTINPUT_API FStarFlightDeviceInfo
{
    FString DeviceName;
    FString ReportedName; // Preserve the backend name even when VID/PID has a known friendly name.
    FString GUID;
    uint16 VendorId = 0;
    uint16 ProductId = 0;
    int32 NumAxes = 0, NumButtons = 0, NumHats = 0;
    bool bEligible = false, bVirtual = false, bWheel = false, bGamepad = false;
};
struct STARFLIGHTINPUT_API FStarFlightInputStatus : FStarFlightDeviceInfo
{
    bool bInitialized = false;
    bool bConnected = false;
    bool bAxisDataReady = false;
    bool bFocused = false;
    bool bArmed = false;
    bool bRequiresPause = false;
    bool bMappingConfirmed = false;
    bool bProfileValid = true;
    uint32 ConnectionGeneration = 0;
    FString Message = TEXT("フライトスティックを確認しています");
};
