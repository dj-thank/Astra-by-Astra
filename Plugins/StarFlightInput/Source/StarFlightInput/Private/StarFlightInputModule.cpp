#include "StarFlightInputModule.h"
#include "StarFlightInputDevice.h"
#include "StarFlightKeys.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"

IMPLEMENT_MODULE(FStarFlightInputModule, StarFlightInput)
FStarFlightInputModule* FStarFlightInputModule::GetIfAvailable()
{
    return FModuleManager::GetModulePtr<FStarFlightInputModule>(TEXT("StarFlightInput"));
}
void FStarFlightInputModule::StartupModule()
{
    FStarFlightKeys::Register();
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("StarFlightInput"));
    if (Plugin.IsValid())
    {
        const FString Library = FPaths::Combine(Plugin->GetBaseDir(), TEXT("ThirdParty/SDL3/bin/Win64/SDL3.dll"));
        SdlLibraryHandle = FPlatformProcess::GetDllHandle(*Library);
    }
    if (!SdlLibraryHandle)
    {
        const FString StagedLibrary = FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("SDL3.dll"));
        SdlLibraryHandle = FPlatformProcess::GetDllHandle(*StagedLibrary);
    }
    if (!SdlLibraryHandle) StartupError = TEXT("SDL3 が見つからないためフライトスティックを利用できません");
    IInputDeviceModule::StartupModule();
}
void FStarFlightInputModule::ShutdownModule()
{
    if (Device.IsValid()) Device->Shutdown();
    Device.Reset();
    IModularFeatures::Get().UnregisterModularFeature(IInputDeviceModule::GetModularFeatureName(), this);
    if (SdlLibraryHandle) FPlatformProcess::FreeDllHandle(SdlLibraryHandle);
    SdlLibraryHandle = nullptr;
}
TSharedPtr<IInputDevice> FStarFlightInputModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& Handler)
{
    if (!SdlLibraryHandle) return nullptr;
    if (!Device.IsValid()) Device = MakeShared<FStarFlightInputDevice>(Handler);
    else Device->SetMessageHandler(Handler);
    return Device;
}
FStarFlightInputStatus FStarFlightInputModule::GetStatus() const
{
    if (Device.IsValid()) return Device->GetStatus();
    FStarFlightInputStatus Status;
    if (!StartupError.IsEmpty()) Status.Message = StartupError;
    return Status;
}
FStarFlightControlFrame FStarFlightInputModule::GetControlFrame() const { return Device.IsValid() ? Device->GetControlFrame() : FStarFlightControlFrame{}; }
FStarFlightRawState FStarFlightInputModule::GetRawState() const { return Device.IsValid() ? Device->GetRawState() : FStarFlightRawState{}; }
FStarFlightProfile FStarFlightInputModule::GetProfile() const { return Device.IsValid() ? Device->GetProfile() : FStarFlightProfile{}; }
FStarFlightDeviceSelection FStarFlightInputModule::GetDeviceSelection() const { return Device.IsValid() ? Device->GetDeviceSelection() : FStarFlightDeviceSelection{}; }
TArray<FStarFlightDeviceInfo> FStarFlightInputModule::GetDevices() const { return Device.IsValid() ? Device->GetDevices() : TArray<FStarFlightDeviceInfo>{}; }
bool FStarFlightInputModule::SetProfile(const FStarFlightProfile& Profile, bool bPersist) { return Device.IsValid() && Device->SetProfile(Profile, bPersist); }
bool FStarFlightInputModule::SetDeviceSelection(const FStarFlightDeviceSelection& Selection, bool bPersist) { return Device.IsValid() && Device->SetDeviceSelection(Selection, bPersist); }
void FStarFlightInputModule::SetGameplayEnabled(bool bEnabled) { if (Device.IsValid()) Device->SetGameplayEnabled(bEnabled); }
