#pragma once
#include "IInputDevice.h"
#include "StarFlightInputTypes.h"
#include "Core/StarSdlJoystick.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

class FStarFlightInputDevice final : public IInputDevice
{
public:
    explicit FStarFlightInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& Handler);
    virtual ~FStarFlightInputDevice() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SendControllerEvents() override;
    virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& Handler) override;
    virtual bool Exec(UWorld* World, const TCHAR* Cmd, FOutputDevice& Ar) override { return false; }
    virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType Type, float Value) override {}
    virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override {}
    virtual bool IsGamepadAttached() const override { return Status.bConnected; }
    virtual bool SupportsForceFeedback(int32 ControllerId) override { return false; }
    void Shutdown();
    FStarFlightInputStatus GetStatus() const { return Status; }
    FStarFlightControlFrame GetControlFrame() const { return Frame; }
    FStarFlightRawState GetRawState() const { return Raw; }
    FStarFlightProfile GetProfile() const { return Profile; }
    FStarFlightDeviceSelection GetDeviceSelection() const;
    TArray<FStarFlightDeviceInfo> GetDevices() const { return Devices; }
    bool SetProfile(const FStarFlightProfile& NewProfile, bool bPersist);
    bool SetDeviceSelection(const FStarFlightDeviceSelection& NewSelection, bool bPersist);
    void SetGameplayEnabled(bool bEnabled);
private:
    void LoadProfileForDevice();
    void EmitFrame(const FStarFlightControlFrame& NewFrame);
    FString ProfilePath() const;
    static FStarFlightDeviceInfo ToInfo(const star::input::DeviceDescriptor& Descriptor);
    static bool WriteSettings(const FString& Path, const std::string& Data);
    star::input::SdlJoystickBackend Backend;
    star::input::Selection Selection;
    star::input::SafetyInterlock Interlock;
    FStarFlightProfile Profile;
    FStarFlightRawState Raw;
    FStarFlightControlFrame Frame, LastEmitted;
    FStarFlightInputStatus Status;
    TArray<FStarFlightDeviceInfo> Devices;
    TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
    FPlatformUserId PlatformUser;
    FInputDeviceId DeviceId;
    FString StorageRoot, SettingsError;
    float RefreshSeconds = 1;
    bool bGameplayEnabled = true;
    bool bSelectionValid = true;
    bool bProfileLoadedValid = true;
    bool bDeviceMapped = false;
    bool bShutdown = false;
};
