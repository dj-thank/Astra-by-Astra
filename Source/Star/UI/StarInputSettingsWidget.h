#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StarFlightInputModule.h"
#include "UI/StarInputCalibration.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "StarInputSettingsWidget.generated.h"

class IInputProcessor;
class SBox;
class SScrollBox;
class SMenuAnchor;
class SWidget;
class UFont;
DECLARE_DELEGATE(FStarInputSettingsClosed);

/** Only the input-settings adapter touches the module; no runtime pawn or controller dependency. */
UCLASS()
class STAR_API UStarInputSettingsWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    FStarInputSettingsClosed OnClose;
    FSimpleDelegate OnSettingsChanged;
    void SetUIScale(float Value);
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    TSharedPtr<IInputProcessor> MenuInputProcessor;
    UPROPERTY(Transient) TObjectPtr<UFont> JapaneseFont;
    FStarFlightProfile Draft;
    FStarFlightInputStatus Status;
    FStarFlightRawState Raw;
    std::array<star::input::calibration::Evidence, star::input::AxisCount> Evidence;
    star::input::calibration::StableCapture Capture;
    star::input::calibration::ButtonListener Listener;
    int32 CaptureAxis = -1, ListenAction = -1;
    star::input::calibration::Point CapturePoint = star::input::calibration::Point::Minimum;
    uint32 ExpectedGeneration = 0;
    FString ExpectedGUID;
    bool bInitialized = false, bOpen = false, bDirectionsConfirmed = false, bDirty = false;
    float UIScale = 1, RefreshElapsed = 0;
    FString Feedback;
    TArray<TSharedPtr<FStarFlightDeviceInfo>> DeviceOptions;
    TArray<TSharedPtr<int32>> AxisOptions, ButtonOptions, HatOptions;
    TArray<TWeakPtr<SWidget>> Focusables;
    TArray<TWeakPtr<SMenuAnchor>> ComboMenus;
    TSharedPtr<SBox> ContentsHost;
    TSharedPtr<SScrollBox> Scroll;
    FButtonStyle ButtonStyle;

    void RefreshState(bool bForce);
    void LoadCurrentProfile();
    void RebuildContents();
    void EditChanged();
    void CancelActive();
    void StartCapture(int32 Axis, star::input::calibration::Point Point);
    void SetButtonMapping(int32 Action, int32 Physical);
    FReply Apply();
    FReply Close();
    star::input::calibration::Validation ValidateDraft(FStarFlightProfile& Candidate) const;
    FString ValidationText() const;
    FSlateFontInfo Font(float Size) const;
    TSharedRef<SWidget> Text(TAttribute<FText> Value, float Size = 16, float Wrap = 0) const;
    TSharedRef<SWidget> Button(const FString& Label, TFunction<FReply()> Callback, TAttribute<bool> Enabled = true);
    TSharedRef<SWidget> NumberChoice(TArray<TSharedPtr<int32>>& Options, int32 Initial, TFunction<void(int32)> Changed,
        bool bAxis = false, TFunction<int32()> CurrentValue = {});
    TSharedRef<SWidget> BuildAxis(int32 Index);
    TSharedRef<SWidget> BuildButtons();
    bool CanRead() const;
    bool SameConnection(const FStarFlightInputStatus& CurrentStatus) const;
};
