#pragma once
#include "InputCoreTypes.h"

struct STARFLIGHTINPUT_API FStarFlightKeys
{
    // MSVC requires separate declarations for static data in this exported class.
    static const FKey Yaw;
    static const FKey Pitch;
    static const FKey Roll;
    static const FKey Throttle;
    static const FKey LookX;
    static const FKey LookY;
    static const FKey Scan;
    static const FKey ToggleView;
    static const FKey Brake;
    static const FKey ToggleGear;
    static const FKey ToggleCruise;
    static const FKey TargetNext;
    static const FKey TogglePhoto;
    static const FKey Pause;
    static const FKey RecenterLook;
    static const FKey Precision;
    static const FKey Aux11;
    static const FKey Aux12;
    static const FKey Aux13;
    static const FKey Aux14;
    static const FKey Aux15;
    static const FKey Aux16;
    static const FKey& Button(int32 LogicalIndex);
    static void Register();
};
