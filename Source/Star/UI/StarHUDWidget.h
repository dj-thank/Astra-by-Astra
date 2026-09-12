#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "StarViewTypes.h"
#include "StarHUDWidget.generated.h"

class SButton;
class IInputProcessor;
class SBox;
class SScrollBox;
class SVerticalBox;
class SWidget;
class UFont;

DECLARE_DELEGATE_TwoParams(FStarUIAction, FName, float);
DECLARE_DELEGATE_OneParam(FStarDateTimeAction, const FString&);

/** Japanese flight HUD. Runtime handles every gameplay/persistence/device action. */
UCLASS()
class STAR_API UStarHUDWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    void ApplySnapshot(const FStarHUDSnapshot& Snapshot);
    void SetMainMenuVisible(bool bVisible);
    void SetUIScale(float Value);
    void ApplyUserSettings(float Scale, float Volume, float Sensitivity);
    float GetUIScale() const { return UIScale; }
    bool IsMenuOpen() const;
    FStarUIAction OnAction;
    FStarDateTimeAction OnDateTime;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
    TSharedPtr<IInputProcessor> MenuInputProcessor;
    enum class EPanel : uint8 { None, Main, Pause, Missions, Settings };
    struct FMenuAction { FName Name; float Value = 1; TSharedPtr<SButton> Button; };
    UPROPERTY(Transient) TObjectPtr<UFont> JapaneseFont;
    FStarHUDSnapshot Current;
    EPanel Panel = EPanel::None;
    EPanel ReturnPanel = EPanel::Pause;
    float UIScale = 1;
    float MasterVolume = 1;
    float LookSensitivity = 1;
    int32 FocusedAction = 0;
    bool bPhotoControlsVisible = true;
    TSharedPtr<SBox> MenuHost;
    TSharedPtr<SWidget> ViewportLayer;
    TSharedPtr<SWidget> TargetGuidance;
    TSharedPtr<SScrollBox> MenuScroll;
    TArray<FMenuAction> MenuActions;
    FButtonStyle ButtonStyle;

    FSlateFontInfo Font(float Size) const;
    TSharedRef<SWidget> BuildFlightHUD();
    TSharedRef<SWidget> BuildTargetGuidance();
    TSharedRef<SWidget> BuildPhotoHUD();
    TSharedRef<SWidget> BuildMenu();
    TSharedRef<SWidget> BuildMissionPanel();
    TSharedRef<SWidget> BuildSettingsPanel();
    TSharedRef<SWidget> MakeButton(const FString& Label, FName Action, float Value = 1);
    TSharedRef<SWidget> MakeText(TAttribute<FText> Text, float Size, FLinearColor Color, float Wrap = 0) const;
    TSharedRef<SWidget> MakeMetric(TAttribute<FText> Label, TAttribute<FText> Value, float Width) const;
    void ShowPanel(EPanel Value);
    void RebuildMenu();
    FReply Activate(FName Action, float Value);
    void FocusAction(int32 Index);
    EVisibility FlightVisibility() const;
    bool HasTargetGuidance() const;
    FVector2D GuidanceViewportSize() const;
    FVector2D TargetMarkerPosition() const;
    FString TargetDirectionSymbol() const;
    FString TargetDirectionText() const;
    static FString Distance(double Metres);
    static FString Speed(double Mps);
};
