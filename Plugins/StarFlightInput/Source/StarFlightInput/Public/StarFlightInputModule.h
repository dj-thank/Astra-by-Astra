#pragma once
#include "IInputDeviceModule.h"
#include "StarFlightInputTypes.h"

class FStarFlightInputDevice;
class STARFLIGHTINPUT_API FStarFlightInputModule : public IInputDeviceModule
{
public:
    static FStarFlightInputModule* GetIfAvailable();
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    virtual TSharedPtr<IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
    FStarFlightInputStatus GetStatus() const;
    FStarFlightControlFrame GetControlFrame() const;
    FStarFlightRawState GetRawState() const;
    FStarFlightProfile GetProfile() const;
    FStarFlightDeviceSelection GetDeviceSelection() const;
    TArray<FStarFlightDeviceInfo> GetDevices() const;
    bool SetProfile(const FStarFlightProfile& Profile, bool bPersist = true);
    bool SetDeviceSelection(const FStarFlightDeviceSelection& Selection, bool bPersist = true);
    // Call false on pause/menu, true on deliberate resume. Never acknowledges while unfocused.
    // Acknowledging a pause request does not bypass the physical neutral/throttle interlock.
    void SetGameplayEnabled(bool bEnabled);
private:
    TSharedPtr<FStarFlightInputDevice> Device;
    void* SdlLibraryHandle = nullptr;
    FString StartupError;
};
