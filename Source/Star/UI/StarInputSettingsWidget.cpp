#include "UI/StarInputSettingsWidget.h"
#include "UI/StarMenuInputProcessor.h"
#include "UI/StarFonts.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Font.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
using namespace star::input::calibration;
FText FT(const FString& Value) { return FText::FromString(Value); }
const TCHAR* AxisNames[] = {TEXT("ヨー / 左右"), TEXT("ピッチ / 前後"), TEXT("ロール / ねじり"), TEXT("スロットル / 推力")};
const TCHAR* PointNames[] = {TEXT("最小"), TEXT("中立"), TEXT("最大")};
const TCHAR* ActionNames[] = {TEXT("スキャン"), TEXT("視点切替"), TEXT("制動"), TEXT("着陸脚"),
    TEXT("巡航切替"), TEXT("次の目標"), TEXT("写真モード"), TEXT("一時停止"), TEXT("視点を正面へ"),
    TEXT("精密操作"), TEXT("離陸 / 上昇"), TEXT("保存"), TEXT("読み込み"), TEXT("降下 / 写真モードで撮影"), TEXT("地球を目標に"), TEXT("土星を目標に")};
const FLinearColor Pale(0.9f, 0.93f, 0.95f, 1);
FString NumberLabel(int32 Number, bool bAxis = false)
{
    if (Number < 0) return TEXT("未割当");
    if (bAxis) return FString::Printf(TEXT("軸 %d"), Number);
    return FString::Printf(TEXT("%d (SDL %d)"), Number + 1, Number);
}
}

FSlateFontInfo UStarInputSettingsWidget::Font(float Size) const
{
    return StarJapaneseFont(Size, JapaneseFont.Get());
}
TSharedRef<SWidget> UStarInputSettingsWidget::Text(TAttribute<FText> Value, float Size, float Wrap) const
{ return SNew(STextBlock).Text(Value).Font(Font(Size)).ColorAndOpacity(Pale).WrapTextAt(Wrap); }

TSharedRef<SWidget> UStarInputSettingsWidget::Button(const FString& Label, TFunction<FReply()> Callback, TAttribute<bool> Enabled)
{
    auto Widget = SNew(SButton).ButtonStyle(&ButtonStyle).IsEnabled(Enabled).IsFocusable(true)
        .OnClicked_Lambda([Callback = MoveTemp(Callback)]() { return Callback(); })[Text(FT(Label), 14)];
    Focusables.Add(Widget);
    return Widget;
}

TSharedRef<SWidget> UStarInputSettingsWidget::NumberChoice(TArray<TSharedPtr<int32>>& Options, int32 Initial,
    TFunction<void(int32)> Changed, bool bAxis, TFunction<int32()> CurrentValue)
{
    TSharedPtr<int32> Selected;
    for (const auto& Option : Options) if (*Option == Initial) Selected = Option;
    // Display follows the combo selection itself, and it never substitutes an unavailable source.
    auto Display = MakeShared<int32>(Initial);
    auto WeakCombo = MakeShared<TWeakPtr<SComboBox<TSharedPtr<int32>>>>();
    auto Getter = MakeShared<TFunction<int32()>>(MoveTemp(CurrentValue));
    auto Widget = SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&Options).InitiallySelectedItem(Selected)
        .EnableGamepadNavigationMode(true)
        .OnGenerateWidget_Lambda([this, bAxis](TSharedPtr<int32> Option) { return Text(FT(NumberLabel(*Option, bAxis)), 14); })
        .OnSelectionChanged_Lambda([Changed = MoveTemp(Changed), Display](TSharedPtr<int32> Option, ESelectInfo::Type)
        { if (Option.IsValid()) { *Display = *Option; Changed(*Option); } })
        .OnComboBoxOpening_Lambda([WeakCombo, Getter, &Options]
        {
            if (!*Getter) return;
            if (auto Combo = WeakCombo->Pin())
                for (const auto& Option : Options) if (*Option == (*Getter)()) { Combo->SetSelectedItem(Option); break; }
        })
        [Text(TAttribute<FText>::CreateLambda([Display, Getter, bAxis] { return FT(NumberLabel(*Getter ? (*Getter)() : *Display, bAxis)); }), 14)];
    *WeakCombo = Widget;
    ComboMenus.Add(Widget);
    Focusables.Add(Widget);
    return Widget;
}

void UStarInputSettingsWidget::NativeConstruct()
{
    Super::NativeConstruct();
    const TWeakObjectPtr<UStarInputSettingsWidget> WeakThis(this);
    MenuInputProcessor=MakeShared<FStarMenuInputProcessor>([WeakThis](const FKeyEvent& Event)
    {
        auto* Widget=WeakThis.Get();
        if(!Widget || !Widget->IsInViewport() || !Widget->bOpen) return false;
        if(Event.IsRepeat()) return true;
        return Widget->NativeOnPreviewKeyDown(Widget->GetCachedGeometry(),Event).IsEventHandled();
    });
    FSlateApplication::Get().RegisterInputPreProcessor(MenuInputProcessor,0);

    bOpen = true;
    SetIsFocusable(true);
    if (auto* Input = FStarFlightInputModule::GetIfAvailable()) Input->SetGameplayEnabled(false);
    RefreshState(true);
    if (!Focusables.IsEmpty())
        if (auto First = Focusables[0].Pin()) FSlateApplication::Get().SetKeyboardFocus(First, EFocusCause::SetDirectly);
}

void UStarInputSettingsWidget::NativeDestruct()
{
    if(FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(MenuInputProcessor);
    MenuInputProcessor.Reset();
    bOpen = false; CancelActive();
    for (const auto& WeakMenu : ComboMenus) if (auto Menu = WeakMenu.Pin()) Menu->SetIsOpen(false, false, 0);
    if (auto* Input = FStarFlightInputModule::GetIfAvailable()) Input->SetGameplayEnabled(false);
    Super::NativeDestruct();
}

void UStarInputSettingsWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    ContentsHost.Reset(); Scroll.Reset(); Focusables.Reset(); ComboMenus.Reset();
}

void UStarInputSettingsWidget::SetUIScale(float Value)
{ if (FMath::IsFinite(Value)) UIScale = FMath::Clamp(Value, 0.8f, 1.2f); }

bool UStarInputSettingsWidget::SameConnection(const FStarFlightInputStatus& CurrentStatus) const
{ return CurrentStatus.bConnected && CurrentStatus.GUID == ExpectedGUID && CurrentStatus.ConnectionGeneration == ExpectedGeneration; }
bool UStarInputSettingsWidget::CanRead() const
{ return Raw.Connected && Raw.AxisDataReady && Status.bAxisDataReady && Status.bFocused && SameConnection(Status); }

void UStarInputSettingsWidget::CancelActive()
{ Capture.Cancel(); Listener.Cancel(); CaptureAxis = ListenAction = -1; }
void UStarInputSettingsWidget::EditChanged()
{ Draft.MappingConfirmed = false; bDirectionsConfirmed = false; bDirty = true; }

void UStarInputSettingsWidget::LoadCurrentProfile()
{
    CancelActive();
    if (auto* Input = FStarFlightInputModule::GetIfAvailable()) Draft = Input->GetProfile();
    Draft.MappingConfirmed = false;
    ExpectedGUID = Status.GUID; ExpectedGeneration = Status.ConnectionGeneration;
    bDirectionsConfirmed = false; bDirty = false;
    for (int32 I = 0; I < star::input::AxisCount; ++I) Evidence[I].Reset(Draft.Axes[I].Index);
}

void UStarInputSettingsWidget::RefreshState(bool bForce)
{
    auto* Input = FStarFlightInputModule::GetIfAvailable();
    if (!Input)
    {
        Raw = {}; Status = {};
        Status.Message = TEXT("フライトスティック入力を利用できません。");
        CancelActive();
        if (!bInitialized || bForce) { bInitialized = true; RebuildContents(); }
        return;
    }
    const auto Previous = Status;
    Status = Input->GetStatus();
    const auto PreviousRaw = Raw;
    Raw = Input->GetRawState();
    const bool bConnectionChanged = Status.ConnectionGeneration != ExpectedGeneration || Status.GUID != ExpectedGUID;
    if (!bInitialized || bConnectionChanged)
    {
        if (bInitialized) Feedback = TEXT("接続が変わりました。保存済み設定を読み直しました。校正は最初から確認してください。");
        LoadCurrentProfile(); bForce = true;
    }
    if (!CanRead())
    {
        if (Capture.Active() || Listener.Active()) Feedback = TEXT("実入力またはフォーカスが途切れたため、記録・待受を中止しました。");
        CancelActive();
    }
    bool bDevicesChanged = false;
    if (bForce || RefreshElapsed >= 1)
    {
        RefreshElapsed = 0;
        TArray<TSharedPtr<FStarFlightDeviceInfo>> Updated;
        for (const auto& Device : Input->GetDevices())
            if (Device.bEligible && !Device.bVirtual && !Device.bWheel && !Device.bGamepad)
                Updated.Add(MakeShared<FStarFlightDeviceInfo>(Device));
        bDevicesChanged = Updated.Num() != DeviceOptions.Num();
        for (int32 I = 0; !bDevicesChanged && I < Updated.Num(); ++I)
            bDevicesChanged = Updated[I]->GUID != DeviceOptions[I]->GUID || Updated[I]->DeviceName != DeviceOptions[I]->DeviceName;
        if (bDevicesChanged) DeviceOptions = MoveTemp(Updated);
    }
    if (!bInitialized || bForce || bDevicesChanged || Raw.NumAxes != PreviousRaw.NumAxes || Raw.NumButtons != PreviousRaw.NumButtons ||
        Raw.NumHats != PreviousRaw.NumHats || Previous.bConnected != Status.bConnected)
    {
        AxisOptions.Reset(); ButtonOptions.Reset(); HatOptions.Reset();
        for (int32 I = 0; I < FMath::Clamp(Raw.NumAxes, 0, star::input::MaxRawAxes); ++I) AxisOptions.Add(MakeShared<int32>(I));
        ButtonOptions.Add(MakeShared<int32>(-1));
        for (int32 I = 0; I < FMath::Clamp(Raw.NumButtons, 0, star::input::MaxRawButtons); ++I) ButtonOptions.Add(MakeShared<int32>(I));
        HatOptions.Add(MakeShared<int32>(-1));
        for (int32 I = 0; I < FMath::Clamp(Raw.NumHats, 0, star::input::MaxRawHats); ++I) HatOptions.Add(MakeShared<int32>(I));
        RebuildContents();
    }
    bInitialized = true;
}

void UStarInputSettingsWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (!bOpen || !IsVisible()) return;
    RefreshElapsed += DeltaSeconds;
    if (auto* Input = FStarFlightInputModule::GetIfAvailable()) Input->SetGameplayEnabled(false);
    RefreshState(false);
    if (Capture.Active() && CaptureAxis >= 0 && CaptureAxis < star::input::AxisCount)
    {
        const int32 Source = Draft.Axes[CaptureAxis].Index;
        const bool bAvailable = CanRead() && Source >= 0 && Source < Raw.NumAxes && Source < star::input::MaxRawAxes;
        int Value = 0;
        const auto Result = Capture.Observe(bAvailable ? Raw.Axes[Source] : 0, DeltaSeconds, bAvailable, Value);
        if (Result == CaptureResult::Captured)
        {
            auto& A = Draft.Axes[CaptureAxis];
            Evidence[CaptureAxis].Record(CapturePoint, Value);
            if (CapturePoint == Point::Minimum) A.Minimum = Value;
            else if (CapturePoint == Point::Center) A.Center = Value;
            else A.Maximum = Value;
            Feedback = FString::Printf(TEXT("%s の%sを %d で記録しました。"), AxisNames[CaptureAxis], PointNames[static_cast<int32>(CapturePoint)], Value);
            EditChanged(); CaptureAxis = -1;
        }
        else if (Result != CaptureResult::Waiting)
        { Feedback = TEXT("校正を中止しました。画面を選択し、軸を指定位置で止めて再度記録してください。"); CaptureAxis = -1; }
    }
    if (Listener.Active())
    {
        const int32 Action = ListenAction;
        const int32 Physical = Listener.Observe(Raw, DeltaSeconds);
        if (Physical >= 0 && Action >= 0 && Action < star::input::ButtonCount)
        { SetButtonMapping(Action, Physical); ListenAction = -1; RebuildContents(); }
        else if (!Listener.Active()) { Feedback = TEXT("ボタン待受を終了しました。再度「押して割当」を選べます。"); ListenAction = -1; }
    }
}

void UStarInputSettingsWidget::StartCapture(int32 Axis, Point PointToCapture)
{
    CancelActive();
    // Re-read synchronously so a just-disconnected device cannot start a capture from stale cache.
    RefreshState(false);
    if (!CanRead() || Draft.Axes[Axis].Index < 0 || Draft.Axes[Axis].Index >= Raw.NumAxes)
    { Feedback = TEXT("接続と実入力の準備ができてから、記録してください。"); return; }
    CaptureAxis = Axis; CapturePoint = PointToCapture; Capture.Start(); EditChanged();
    Feedback = FString::Printf(TEXT("%s の%sを記録中。指定位置で0.4秒以上止めてください。"), AxisNames[Axis], PointNames[static_cast<int32>(PointToCapture)]);
}

void UStarInputSettingsWidget::SetButtonMapping(int32 Action, int32 Physical)
{
    if (Action < 0 || Action >= star::input::ButtonCount || Physical < -1 || Physical >= Raw.NumButtons) return;
    if (Draft.ButtonMap[Action] == Physical) return;
    FString PreviousAction;
    if (Physical >= 0)
        for (int32 I = 0; I < star::input::ButtonCount; ++I)
            if (I != Action && Draft.ButtonMap[I] == Physical) { Draft.ButtonMap[I] = -1; PreviousAction = ActionNames[I]; }
    Draft.ButtonMap[Action] = Physical;
    EditChanged();
    Feedback = FString::Printf(TEXT("%s：%s。%s"), ActionNames[Action], *NumberLabel(Physical),
        PreviousAction.IsEmpty() ? TEXT("適用するまで保存されません") : *(PreviousAction + TEXT(" の旧割当を解除しました")));
}

Validation UStarInputSettingsWidget::ValidateDraft(FStarFlightProfile& Candidate) const
{
    return BuildConfirmedProfile(Draft, Evidence, Raw, Status.bFocused, SameConnection(Status), bDirectionsConfirmed, Candidate);
}

FString UStarInputSettingsWidget::ValidationText() const
{
    FStarFlightProfile Candidate;
    switch (ValidateDraft(Candidate))
    {
    case Validation::Ready: return bDirty ? TEXT("校正と操作方向を確認できました。設定を適用できます。") : TEXT("設定は保存済みです。戻ってから飛行を再開できます。");
    case Validation::NoDevice: return TEXT("使用するフライトスティックを接続・選択してください。");
    case Validation::NoSamples: return TEXT("実入力を待っています。ゲーム画面を選択してください。");
    case Validation::Unfocused: return TEXT("ゲーム画面を選択してください。非アクティブ中は記録しません。");
    case Validation::ChangedDevice: return TEXT("接続が変わりました。校正をやり直してください。");
    case Validation::NeedMeasurements: return TEXT("4軸の最小・中立・最大を実測してください。全幅30,000以上、中心から両側4,000以上の生値が必要です。");
    case Validation::NeedConfirmation: return TEXT("各軸の実方向とスロットル最小を確認し、下のチェックを入れてください。");
    case Validation::DuplicateAxis: return TEXT("同じ物理軸が複数の操作に割り当てられています。軸を選び直してください。");
    case Validation::DuplicateButton: return TEXT("同じボタンの重複割当があります。割当を選び直してください。");
    default: return TEXT("校正値は 最小 < 中立 < 最大 の順です。軸・ボタン番号と校正値を確認してください。");
    }
}

FReply UStarInputSettingsWidget::Apply()
{
    CancelActive();
    auto* Input = FStarFlightInputModule::GetIfAvailable();
    if (!Input) { Feedback = TEXT("入力プラグインを利用できません。"); return FReply::Handled(); }
    Input->SetGameplayEnabled(false);
    Status = Input->GetStatus(); Raw = Input->GetRawState();
    FStarFlightProfile Candidate;
    if (ValidateDraft(Candidate) != Validation::Ready) { Feedback = ValidationText(); return FReply::Handled(); }
    if (!Input->SetProfile(Candidate, true))
    { Feedback = TEXT("設定を保存できませんでした。接続と保存先を確認してください。以前の設定を保持します。"); return FReply::Handled(); }
    const auto Readback = Input->GetProfile();
    if (star::input::SerializeProfile(Readback) != star::input::SerializeProfile(Candidate))
    { Feedback = TEXT("保存後の設定を確認できません。入力は停止したままです。"); return FReply::Handled(); }
    Draft = Readback; bDirty = false;
    Feedback = TEXT("設定を保存しました。スティックを中立、スロットルを最小に戻し、ボタンとPOVを離してから飛行を再開してください。");
    OnSettingsChanged.ExecuteIfBound();
    return FReply::Handled();
}

FReply UStarInputSettingsWidget::Close()
{
    CancelActive(); bOpen = false;
    if (auto* Input = FStarFlightInputModule::GetIfAvailable()) Input->SetGameplayEnabled(false);
    if (OnClose.IsBound()) OnClose.Execute(); else RemoveFromParent();
    return FReply::Handled();
}

TSharedRef<SWidget> UStarInputSettingsWidget::RebuildWidget()
{
    JapaneseFont = LoadObject<UFont>(nullptr, TEXT("/Game/Star/UI/Fonts/NotoSansJP.NotoSansJP"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    ButtonStyle = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.05f, 0.07f, 0.085f, 1)));
    ButtonStyle.SetHovered(FSlateColorBrush(FLinearColor(0.12f, 0.17f, 0.20f, 1)));
    ButtonStyle.SetPressed(FSlateColorBrush(FLinearColor(0.19f, 0.15f, 0.09f, 1)));
    ButtonStyle.SetNormalPadding(FMargin(10, 8)); ButtonStyle.SetPressedPadding(FMargin(10, 8));
    auto Root = SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
    [ SNew(SBox).WidthOverride_Lambda([this] { return 1920.f / UIScale; }).HeightOverride_Lambda([this] { return 1080.f / UIScale; })
        [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(FLinearColor(0.002f, 0.009f, 0.016f, 0.80f)).Padding(30).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(FLinearColor(0.015f, 0.023f, 0.03f, 1)).Padding(26)
                [ SAssignNew(ContentsHost, SBox).WidthOverride(1420).HeightOverride(770) ]
            ]
        ]
    ];
    RefreshState(true);
    return Root;
}

TSharedRef<SWidget> UStarInputSettingsWidget::BuildAxis(int32 Index)
{
    auto Invert = SNew(SCheckBox).IsChecked_Lambda([this, Index] { return Draft.Axes[Index].Inverted ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State) { Draft.Axes[Index].Inverted = State == ECheckBoxState::Checked; EditChanged(); })
        [Text(FT(TEXT("方向を反転")), 14)];
    Focusables.Add(Invert);
    auto Points = SNew(SVerticalBox);
    for (int32 P = 0; P < 3; ++P)
    {
        const Point Which = static_cast<Point>(P);
        Points->AddSlot().AutoHeight().Padding(0, 3)
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)
              [Text(TAttribute<FText>::CreateLambda([this, Index, P]
              { const auto& A = Draft.Axes[Index]; const int Value = P == 0 ? A.Minimum : P == 1 ? A.Center : A.Maximum;
                return FT(FString::Printf(TEXT("%s %d %s"), PointNames[P], Value, Evidence[Index].Measured[P] ? TEXT("実測") : TEXT("未実測"))); }), 13)]
            + SHorizontalBox::Slot().AutoWidth()
              [Button(TEXT("記録"), [this, Index, Which] { StartCapture(Index, Which); return FReply::Handled(); }, TAttribute<bool>::CreateLambda([this] { return CanRead(); }))]
        ];
    }
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.028f, 0.04f, 0.05f, 1)).Padding(14)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[Text(FT(AxisNames[Index]), 19)]
            + SVerticalBox::Slot().AutoHeight()[NumberChoice(AxisOptions, Draft.Axes[Index].Index, [this, Index](int32 Source)
              { if (Draft.Axes[Index].Index == Source) return; CancelActive(); Draft.Axes[Index].Index = Source; Evidence[Index].Reset(Source); EditChanged(); }, true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10)
              [Text(TAttribute<FText>::CreateLambda([this, Index]
              { const int32 Source = Draft.Axes[Index].Index;
                if (!CanRead() || Source < 0 || Source >= Raw.NumAxes) return FT(TEXT("実入力：待機中"));
                const auto& A = Draft.Axes[Index];
                return FT(FString::Printf(TEXT("生値 %d   出力 %+.2f"), Raw.Axes[Source], star::input::NormalizeAxis(Raw.Axes[Source], A, Index == 3))); }), 15)]
            + SVerticalBox::Slot().AutoHeight()[Invert]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 6)[Text(TAttribute<FText>::CreateLambda([this, Index] { return FT(FString::Printf(TEXT("デッドゾーン %.0f%%"), Draft.Axes[Index].Deadzone * 100)); }), 14)]
            + SVerticalBox::Slot().AutoHeight()
              [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 5, 0)[Button(TEXT("−"), [this, Index] { Draft.Axes[Index].Deadzone = FMath::Clamp(Draft.Axes[Index].Deadzone - 0.01f, 0.f, 0.3f); EditChanged(); return FReply::Handled(); })]
                + SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("＋"), [this, Index] { Draft.Axes[Index].Deadzone = FMath::Clamp(Draft.Axes[Index].Deadzone + 0.01f, 0.f, 0.3f); EditChanged(); return FReply::Handled(); })] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 6)[Text(TAttribute<FText>::CreateLambda([this, Index] { return FT(FString::Printf(TEXT("応答曲線 %.1f  /  1.0は直線"), Draft.Axes[Index].Exponent)); }), 14)]
            + SVerticalBox::Slot().AutoHeight()
              [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 5, 0)[Button(TEXT("−"), [this, Index] { Draft.Axes[Index].Exponent = FMath::Clamp(Draft.Axes[Index].Exponent - 0.1f, 0.2f, 5.f); EditChanged(); return FReply::Handled(); })]
                + SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("＋"), [this, Index] { Draft.Axes[Index].Exponent = FMath::Clamp(Draft.Axes[Index].Exponent + 0.1f, 0.2f, 5.f); EditChanged(); return FReply::Handled(); })] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[Points]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
              [Button(TEXT("この軸を測り直す"), [this, Index] { CancelActive(); Evidence[Index].Reset(Draft.Axes[Index].Index); EditChanged(); return FReply::Handled(); })]
        ];
}

TSharedRef<SWidget> UStarInputSettingsWidget::BuildButtons()
{
    auto Rows = SNew(SVerticalBox);
    for (int32 Row = 0; Row < 8; ++Row)
    {
        auto Pair = SNew(SHorizontalBox);
        for (int32 Col = 0; Col < 2; ++Col)
        {
            const int32 Action = Row + Col * 8;
            Pair->AddSlot().FillWidth(1).Padding(0, 4, 24, 4)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)[Text(FT(ActionNames[Action]), 16)]
                + SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
                  [SNew(SBox).WidthOverride(125)[NumberChoice(ButtonOptions, Draft.ButtonMap[Action], [this, Action](int32 Physical) { SetButtonMapping(Action, Physical); }, false, [this, Action] { return Draft.ButtonMap[Action]; })]]
                + SHorizontalBox::Slot().AutoWidth()
                  [Button(TEXT("押して割当"), [this, Action]
                    { CancelActive(); RefreshState(false); if (CanRead()) { ListenAction = Action; Listener.Start(); Feedback = TEXT("一度すべてのボタンを離し、割り当てたいボタンを1つ押してください。Escで中止。"); } return FReply::Handled(); },
                    TAttribute<bool>::CreateLambda([this] { return CanRead(); }))]
            ];
        }
        Rows->AddSlot().AutoHeight()[Pair];
    }
    return Rows;
}

void UStarInputSettingsWidget::RebuildContents()
{
    if (!ContentsHost.IsValid()) return;
    const float OldOffset = Scroll.IsValid() ? Scroll->GetScrollOffset() : 0;
    for (const auto& WeakMenu : ComboMenus) if (auto Menu = WeakMenu.Pin()) Menu->SetIsOpen(false, false, 0);
    Focusables.Reset();
    ComboMenus.Reset();
    TSharedPtr<FStarFlightDeviceInfo> InitialDevice;
    for (const auto& Device : DeviceOptions) if (Device->GUID == Status.GUID) InitialDevice = Device;
    auto DeviceCombo = SNew(SComboBox<TSharedPtr<FStarFlightDeviceInfo>>).OptionsSource(&DeviceOptions).InitiallySelectedItem(InitialDevice)
        .EnableGamepadNavigationMode(true)
        .OnGenerateWidget_Lambda([this](TSharedPtr<FStarFlightDeviceInfo> Device) { return Text(FT(Device->DeviceName), 16); })
        .OnSelectionChanged_Lambda([this](TSharedPtr<FStarFlightDeviceInfo> Device, ESelectInfo::Type)
        {
            if (!Device.IsValid() || !Device->bEligible || Device->bWheel || Device->bGamepad || Device->bVirtual) return;
            auto* Input = FStarFlightInputModule::GetIfAvailable(); if (!Input) return;
            const auto CurrentSelection = Input->GetDeviceSelection();
            if (Device->GUID == CurrentSelection.GUID && Device->VendorId == CurrentSelection.VendorId && Device->ProductId == CurrentSelection.ProductId) return;
            // Revalidate the actual object immediately before persisting selection.
            bool bStillEligible = false;
            for (const auto& Live : Input->GetDevices())
                if (Live.GUID == Device->GUID && Live.VendorId == Device->VendorId && Live.ProductId == Device->ProductId &&
                    Live.bEligible && !Live.bVirtual && !Live.bWheel && !Live.bGamepad) bStillEligible = true;
            if (!bStillEligible) { Feedback = TEXT("その機器は現在選択できません。"); return; }
            CancelActive(); Input->SetGameplayEnabled(false);
            FStarFlightDeviceSelection Choice; Choice.VendorId = Device->VendorId; Choice.ProductId = Device->ProductId; Choice.GUID = Device->GUID;
            if (!Input->SetDeviceSelection(Choice, true)) { Feedback = TEXT("使用機器を保存できませんでした。"); return; }
            const auto Readback = Input->GetDeviceSelection();
            Feedback = Readback.GUID == Choice.GUID && Readback.VendorId == Choice.VendorId && Readback.ProductId == Choice.ProductId ?
                TEXT("使用機器を保存しました。実入力の接続を待っています。") : TEXT("使用機器の保存結果を確認できません。入力は停止したままです。");
        })
        [Text(TAttribute<FText>::CreateLambda([this] { return FT(Status.bConnected ? Status.DeviceName : TEXT("使用するフライトスティックを選択")); }), 16)];
    Focusables.Add(DeviceCombo);
    ComboMenus.Add(DeviceCombo);

    auto Axes = SNew(SHorizontalBox);
    for (int32 I = 0; I < star::input::AxisCount; ++I) Axes->AddSlot().FillWidth(1).Padding(0, 0, I == 3 ? 0 : 10, 0)[BuildAxis(I)];
    auto Confirm = SNew(SCheckBox).IsChecked_Lambda([this] { return bDirectionsConfirmed ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bDirectionsConfirmed = State == ECheckBoxState::Checked; })
        [Text(FT(TEXT("4軸の操作方向と出力を実際に確認しました。スロットル最小で出力0になります。")), 15)];
    Focusables.Add(Confirm);
    auto HatX = SNew(SCheckBox).IsChecked_Lambda([this] { return Draft.InvertHatX ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Draft.InvertHatX = State == ECheckBoxState::Checked; EditChanged(); })[Text(FT(TEXT("左右反転")), 14)];
    auto HatY = SNew(SCheckBox).IsChecked_Lambda([this] { return Draft.InvertHatY ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Draft.InvertHatY = State == ECheckBoxState::Checked; EditChanged(); })[Text(FT(TEXT("上下反転")), 14)];
    Focusables.Add(HatX); Focusables.Add(HatY);
    ContentsHost->SetContent(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Text(FT(TEXT("フライトスティック設定")), 27)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 7, 0, 12)
          [Text(FT(TEXT("設定中は操縦入力を停止しています。Tab / 十字キーで選択、Enter / Aで操作。PageUp・PageDown / LB・RBでスクロール。")), 13, 1400)]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 20, 0)[DeviceCombo]
            + SHorizontalBox::Slot().FillWidth(1)[Text(TAttribute<FText>::CreateLambda([this]
              { return FT(!Status.bInitialized ? Status.Message : !Status.bConnected ? TEXT("未接続 / 操縦入力は停止中") : !Status.bFocused ? TEXT("接続済 / 画面のフォーカス待ち") :
                  !Raw.AxisDataReady ? TEXT("接続済 / 実入力待ち / 操縦入力は停止中") : TEXT("実入力を取得中 / 操縦入力は停止中")); }), 14, 680)] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 12)
          [Text(TAttribute<FText>::CreateLambda([this]
          { if (!CanRead()) return FT(TEXT("実入力：待機中。機器名の表示だけでは校正完了になりません。"));
            FString Values = TEXT("実入力  ");
            for (int32 I = 0; I < FMath::Clamp(Raw.NumAxes, 0, star::input::MaxRawAxes); ++I) Values += FString::Printf(TEXT("軸%d: %d   "), I, Raw.Axes[I]);
            return FT(Values); }), 13, 1400)]
        + SVerticalBox::Slot().FillHeight(1)
          [SAssignNew(Scroll, SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll) + SScrollBox::Slot()
            [SNew(SVerticalBox)
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
                [Text(FT(TEXT("1. 各操作を動かし、変化する物理軸を選びます。2. 生値が最小になる端、中央、最大になる端で、それぞれ「記録」を押して0.4秒止めます。\nスティック3軸の中立は手を離した位置。スロットルの中立はストローク中央付近です。端から端まで動かし、出力を確認して必要なら反転してください。\nヨー・ピッチ・ロールの出力は −1～＋1、スロットルは0～1です。曲線1.0は直線、大きい値ほど中心付近の反応が緩やかです。")), 14, 1380)]
              + SVerticalBox::Slot().AutoHeight()[Axes]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 25, 0, 8)[Text(FT(TEXT("ボタンの割り当て")), 23)]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [Text(TAttribute<FText>::CreateLambda([this]
                { return FT(Listener.Active() && ListenAction >= 0 ? FString(ActionNames[ListenAction]) + (Listener.WaitingForRelease() ? TEXT("：すべてのボタンを離してください。") : TEXT("：ボタンを1つ押してください。")) : TEXT("番号を選ぶか「押して割当」で実ボタンを指定します。重複する旧割当は解除します。補助11～16は拡張用の操作です。")); }), 14, 1380)]
              + SVerticalBox::Slot().AutoHeight()[BuildButtons()]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 22, 0, 8)[Text(FT(TEXT("POV / 見回し")), 23)]
              + SVerticalBox::Slot().AutoHeight()
                [SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 20, 0)[SNew(SBox).WidthOverride(180)[NumberChoice(HatOptions, Draft.HatIndex, [this](int32 Value) { Draft.HatIndex = Value; EditChanged(); })]]
                  + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 20, 0)[HatX]
                  + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 20, 0)[HatY]
                  + SHorizontalBox::Slot().FillWidth(1)[Text(TAttribute<FText>::CreateLambda([this]
                    { return FT(CanRead() && Draft.HatIndex >= 0 && Draft.HatIndex < Raw.NumHats ? FString::Printf(TEXT("現在値 %d  (0=中央 / 1=上 / 2=右 / 4=下 / 8=左)"), Raw.Hats[Draft.HatIndex]) : TEXT("POV：未割当 / 入力待ち")); }), 14)] ]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 10)[Text(FT(TEXT("生値の単位はSDLの符号付き16bit値（−32768～32767）です。設定はこの機器のプロファイルに保存します。")), 13, 1380)]
            ]
          ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 13, 0, 5)[Confirm]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 5)
          [Text(TAttribute<FText>::CreateLambda([this] { return FT(ValidationText()); }), 13, 1400)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 10)
          [Text(TAttribute<FText>::CreateLambda([this] { return FT(Feedback); }), 14, 1400)]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
              [Button(TEXT("確認して適用"), [this] { return Apply(); }, TAttribute<bool>::CreateLambda([this] { FStarFlightProfile Candidate; return bDirty && ValidateDraft(Candidate) == Validation::Ready && !Capture.Active() && !Listener.Active(); }))]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
              [Button(TEXT("記録・待受を中止"), [this] { CancelActive(); Feedback = TEXT("記録・待受を中止しました。"); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth()[Button(TEXT("設定に戻る（未適用の変更は破棄）"), [this] { return Close(); })]
          ]
    );
    Scroll->SetScrollOffset(OldOffset);
}

FReply UStarInputSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    const FKey Key = Event.GetKey();
    if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
    {
        for (const auto& WeakMenu : ComboMenus)
            if (auto Menu = WeakMenu.Pin()) if (Menu->IsOpen()) { Menu->SetIsOpen(false, true, 0); return FReply::Handled(); }
        if (Capture.Active() || Listener.Active()) { CancelActive(); Feedback = TEXT("記録・待受を中止しました。"); return FReply::Handled(); }
        return Close();
    }
    if (Key == EKeys::PageUp || Key == EKeys::PageDown || Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder)
    {
        const float Sign = Key == EKeys::PageUp || Key == EKeys::Gamepad_LeftShoulder ? -1.f : 1.f;
        if (Scroll.IsValid()) Scroll->SetScrollOffset(FMath::Max(0.f, Scroll->GetScrollOffset() + Sign * 280.f));
        return FReply::Handled();
    }
    // Slate's native Tab, arrow/D-pad, combo-box and A/Enter navigation is retained.
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
