#include "StarFlightInputDevice.h"
#include "StarFlightKeys.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FStarFlightInputDevice::FStarFlightInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& Handler)
    : MessageHandler(Handler)
{
    StorageRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StarFlightInput"));
    auto& Mapper = IPlatformInputDeviceMapper::Get();
    PlatformUser = Mapper.GetPrimaryPlatformUser();
    DeviceId = Mapper.AllocateNewInputDeviceId();
    FString SavedSelection;
    const FString SelectionPath = FPaths::Combine(StorageRoot, TEXT("selection.starinput"));
    if (IFileManager::Get().FileExists(*SelectionPath))
    {
        std::string Error;
        bSelectionValid = FFileHelper::LoadFileToString(SavedSelection, *SelectionPath) &&
            star::input::ParseSelection(TCHAR_TO_UTF8(*SavedSelection), Selection, Error);
        if (!bSelectionValid) SettingsError = TEXT("デバイス選択の保存内容を読み込めません。入力設定で選び直してください");
    }
    Status.bInitialized = Backend.Initialize();
    Backend.SetSelection(Selection);
    if (!Status.bInitialized) Status.Message = TEXT("フライトスティック入力を初期化できません");
}
FStarFlightInputDevice::~FStarFlightInputDevice() { Shutdown(); }
void FStarFlightInputDevice::Shutdown()
{
    if (bShutdown) return;
    EmitFrame({});
    if (bDeviceMapped) IPlatformInputDeviceMapper::Get().Internal_SetInputDeviceConnectionState(DeviceId, EInputDeviceConnectionState::Disconnected);
    Backend.Shutdown(); bShutdown = true;
}
FStarFlightDeviceInfo FStarFlightInputDevice::ToInfo(const star::input::DeviceDescriptor& D)
{
    FStarFlightDeviceInfo Out;
    Out.ReportedName = UTF8_TO_TCHAR(D.Name.c_str()); Out.DeviceName = Out.ReportedName;
    if (D.VendorId == 0x044f && D.ProductId == 0xb10a) Out.DeviceName = TEXT("Thrustmaster T.16000M");
    Out.GUID = UTF8_TO_TCHAR(D.Guid.c_str());
    Out.VendorId = D.VendorId; Out.ProductId = D.ProductId;
    Out.NumAxes = D.NumAxes; Out.NumButtons = D.NumButtons; Out.NumHats = D.NumHats;
    Out.bEligible = D.Eligible; Out.bVirtual = D.IsVirtual; Out.bWheel = D.IsWheel; Out.bGamepad = D.IsGamepad;
    return Out;
}
void FStarFlightInputDevice::Tick(float DeltaTime)
{
    if (bShutdown) return;
    const bool bFocused = FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive();
    if (Status.bFocused && !bFocused) Status.bRequiresPause = true;
    Status.bFocused = bFocused;
    RefreshSeconds += FMath::Clamp(DeltaTime, 0.0f, 0.1f);
    const bool bRefresh = RefreshSeconds >= 0.75f;
    if (bRefresh)
    {
        Devices.Reset();
        for (const auto& D : Backend.Enumerate()) Devices.Add(ToInfo(D));
        RefreshSeconds = 0;
    }
    Raw = bSelectionValid ? Backend.Poll(bRefresh) : FStarFlightRawState{};
    const bool bWasConnected = Status.bConnected;
    if (bWasConnected && !Raw.Connected) Status.bRequiresPause = true;
    if (Raw.Connected && !bWasConnected)
    {
        ++Status.ConnectionGeneration;
        static_cast<FStarFlightDeviceInfo&>(Status) = ToInfo(Backend.ActiveDevice());
        LoadProfileForDevice();
        Interlock.Reset();
        IPlatformInputDeviceMapper::Get().Internal_MapInputDeviceToUser(DeviceId, PlatformUser, EInputDeviceConnectionState::Connected);
        bDeviceMapped = true;
    }
    if (bWasConnected && !Raw.Connected)
    {
        EmitFrame({}); // Release held UE keys before marking the device disconnected.
        IPlatformInputDeviceMapper::Get().Internal_SetInputDeviceConnectionState(DeviceId, EInputDeviceConnectionState::Disconnected);
        bDeviceMapped = false;
    }
    Status.bConnected = Raw.Connected;
    Status.bAxisDataReady = Raw.AxisDataReady;
    Status.bProfileValid = bProfileLoadedValid && (!Raw.Connected || star::input::ProfileFitsDevice(Profile, Raw));
    Frame = Interlock.Update(Raw, Profile, bFocused,
        bGameplayEnabled && !Status.bRequiresPause && Status.bProfileValid, DeltaTime);
    Status.bArmed = Interlock.IsArmed();
    Status.bMappingConfirmed = Profile.MappingConfirmed;
    if (!SettingsError.IsEmpty()) Status.Message = SettingsError;
    else if (!Status.bInitialized) Status.Message = TEXT("フライトスティック入力を初期化できません");
    else if (!Status.bConnected) Status.Message = TEXT("選択したフライトスティックが接続されていません");
    else if (!Status.bAxisDataReady) Status.Message = TEXT("スティック入力を待っています。ゲーム画面を選択してください");
    else if (!Status.bProfileValid) Status.Message = TEXT("軸・ボタンの割り当てを入力設定で確認してください");
    else if (!bFocused) Status.Message = TEXT("画面が非アクティブのため入力を停止しています");
    else if (Status.bRequiresPause) Status.Message = TEXT("入力を停止しました。中立・低スロットルに戻して再開してください");
    else if (!bGameplayEnabled) Status.Message = TEXT("一時停止中です");
    else if (!Status.bArmed) Status.Message = TEXT("スティックを中立に、スロットルを最小にしてボタンとPOVを離してください");
    else if (!Profile.MappingConfirmed) Status.Message = TEXT("接続済み。初期の軸割り当てを入力設定で確認してください");
    else Status.Message = TEXT("フライトスティックで操縦できます");
}
void FStarFlightInputDevice::EmitFrame(const FStarFlightControlFrame& NewFrame)
{
    const FKey* Keys[] = {&FStarFlightKeys::Yaw,&FStarFlightKeys::Pitch,&FStarFlightKeys::Roll,&FStarFlightKeys::Throttle,&FStarFlightKeys::LookX,&FStarFlightKeys::LookY};
    const float Values[] = {NewFrame.Yaw,NewFrame.Pitch,NewFrame.Roll,NewFrame.Throttle,NewFrame.LookX,NewFrame.LookY};
    const float Previous[] = {LastEmitted.Yaw,LastEmitted.Pitch,LastEmitted.Roll,LastEmitted.Throttle,LastEmitted.LookX,LastEmitted.LookY};
    for (int32 i=0; i<6; ++i)
        if (Values[i] != Previous[i]) MessageHandler->OnControllerAnalog(Keys[i]->GetFName(), PlatformUser, DeviceId, Values[i]);
    for (int32 i=0; i<16; ++i)
    {
        if (NewFrame.Buttons[i] == LastEmitted.Buttons[i]) continue;
        if (NewFrame.Buttons[i]) MessageHandler->OnControllerButtonPressed(FStarFlightKeys::Button(i).GetFName(), PlatformUser, DeviceId, false);
        else MessageHandler->OnControllerButtonReleased(FStarFlightKeys::Button(i).GetFName(), PlatformUser, DeviceId, false);
    }
    LastEmitted = NewFrame;
}
void FStarFlightInputDevice::SendControllerEvents() { if (!bShutdown) EmitFrame(Frame); }
void FStarFlightInputDevice::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& Handler)
{
    EmitFrame({}); MessageHandler = Handler; Interlock.Reset(); Frame = {};
}
FString FStarFlightInputDevice::ProfilePath() const
{
    return FPaths::Combine(StorageRoot, TEXT("Profiles"), Status.GUID + TEXT(".starinput"));
}
void FStarFlightInputDevice::LoadProfileForDevice()
{
    Profile = FStarFlightProfile{}; bProfileLoadedValid = true; SettingsError.Empty();
    const FString Path = ProfilePath();
    if (!IFileManager::Get().FileExists(*Path)) return;
    FString Text; std::string Error;
    bProfileLoadedValid = FFileHelper::LoadFileToString(Text, *Path) && star::input::ParseProfile(TCHAR_TO_UTF8(*Text), Profile, Error);
    if (!bProfileLoadedValid) SettingsError = TEXT("保存済みの入力設定を読み込めません。入力設定で修正してください");
}
bool FStarFlightInputDevice::WriteSettings(const FString& Path, const std::string& Data)
{
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) return false;
    const FString Temporary = Path + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(UTF8_TO_TCHAR(Data.c_str()), *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
    return IFileManager::Get().Move(*Path, *Temporary, true, true, false, true);
}
bool FStarFlightInputDevice::SetProfile(const FStarFlightProfile& NewProfile, bool bPersist)
{
    std::string Error;
    if (!star::input::ValidateProfile(NewProfile, Error) ||
        (Raw.Connected && !star::input::ProfileFitsDevice(NewProfile, Raw))) return false;
    if (bPersist && (!Raw.Connected || !WriteSettings(ProfilePath(), star::input::SerializeProfile(NewProfile)))) return false;
    Profile = NewProfile; bProfileLoadedValid = true; SettingsError.Empty();
    Interlock.Reset(); Frame = {}; EmitFrame(Frame); Status.bArmed = false;
    return true;
}
FStarFlightDeviceSelection FStarFlightInputDevice::GetDeviceSelection() const
{
    FStarFlightDeviceSelection Out; Out.VendorId = Selection.VendorId; Out.ProductId = Selection.ProductId; Out.GUID = UTF8_TO_TCHAR(Selection.Guid.c_str());
    return Out;
}
bool FStarFlightInputDevice::SetDeviceSelection(const FStarFlightDeviceSelection& NewSelection, bool bPersist)
{
    star::input::Selection Candidate; Candidate.VendorId = NewSelection.VendorId; Candidate.ProductId = NewSelection.ProductId; Candidate.Guid = TCHAR_TO_UTF8(*NewSelection.GUID);
    star::input::Selection Validated; std::string Error;
    if (!star::input::ParseSelection(star::input::SerializeSelection(Candidate), Validated, Error)) return false;
    if (bPersist && !WriteSettings(FPaths::Combine(StorageRoot, TEXT("selection.starinput")), star::input::SerializeSelection(Validated))) return false;
    const bool bWasConnected = Status.bConnected;
    Selection = Validated; bSelectionValid = true; SettingsError.Empty();
    Backend.SetSelection(Selection); Interlock.Reset(); Frame = {}; Raw = {}; EmitFrame(Frame);
    if (bDeviceMapped) IPlatformInputDeviceMapper::Get().Internal_SetInputDeviceConnectionState(DeviceId, EInputDeviceConnectionState::Disconnected);
    bDeviceMapped = false; Status.bConnected = false; Status.bAxisDataReady = false; Status.bArmed = false;
    Status.bRequiresPause |= bWasConnected; RefreshSeconds = 1;
    return true;
}
void FStarFlightInputDevice::SetGameplayEnabled(bool bEnabled)
{
    const bool bCanAcknowledge = bEnabled && Status.bFocused;
    if (bEnabled != bGameplayEnabled || (bCanAcknowledge && Status.bRequiresPause))
    {
        Interlock.Reset(); Frame = {}; EmitFrame(Frame); Status.bArmed = false;
    }
    bGameplayEnabled = bEnabled;
    if (bCanAcknowledge) Status.bRequiresPause = false;
}
