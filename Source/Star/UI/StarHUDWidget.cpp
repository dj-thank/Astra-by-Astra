#include "UI/StarHUDWidget.h"
#include "UI/StarMenuInputProcessor.h"
#include "UI/StarFonts.h"
#include "Exploration/StarExplorationSubsystem.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor White(0.90f, 0.93f, 0.95f, 1);
const FLinearColor Muted(0.58f, 0.65f, 0.69f, 1);
const FLinearColor Amber(0.94f, 0.66f, 0.29f, 1);
const FLinearColor PanelColor(0.015f, 0.023f, 0.03f, 0.94f);
FText T(const FString& Value) { return FText::FromString(Value); }
}

void UStarHUDWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
}

FSlateFontInfo UStarHUDWidget::Font(float Size) const
{
    return StarJapaneseFont(Size, JapaneseFont.Get());
}

TSharedRef<SWidget> UStarHUDWidget::MakeText(TAttribute<FText> Text, float Size, FLinearColor Color, float Wrap) const
{
    return SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Color)
        .WrapTextAt(Wrap).ShadowOffset(FVector2D(0, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.8f));
}

FString UStarHUDWidget::Distance(double Metres)
{
    if (!FMath::IsFinite(Metres)) return TEXT("—");
    const double M = FMath::Abs(Metres);
    if (M >= 1.495978707e11) return FString::Printf(TEXT("%.3f AU"), Metres / 1.495978707e11);
    if (M >= 1e9) return FString::Printf(TEXT("%.2f 百万km"), Metres / 1e9);
    if (M >= 1000) return FString::Printf(TEXT("%.1f km"), Metres / 1000);
    return FString::Printf(TEXT("%.1f m"), Metres);
}

FString UStarHUDWidget::Speed(double Mps)
{
    if (!FMath::IsFinite(Mps)) return TEXT("—");
    if (FMath::Abs(Mps) >= 299792458) return FString::Printf(TEXT("%.2f c"), Mps / 299792458);
    if (FMath::Abs(Mps) >= 1000) return FString::Printf(TEXT("%.2f km/s"), Mps / 1000);
    return FString::Printf(TEXT("%.1f m/s"), Mps);
}

TSharedRef<SWidget> UStarHUDWidget::MakeMetric(TAttribute<FText> Label, TAttribute<FText> Value, float Width) const
{
    return SNew(SBox).WidthOverride(Width)
    [ SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)[MakeText(Label, 13, Muted)]
        + SVerticalBox::Slot().AutoHeight()[MakeText(Value, 24, White)] ];
}

TSharedRef<SWidget> UStarHUDWidget::RebuildWidget()
{
    JapaneseFont = LoadObject<UFont>(nullptr, TEXT("/Game/Star/UI/Fonts/NotoSansJP.NotoSansJP"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    ButtonStyle = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.045f, 0.065f, 0.078f, 1)));
    ButtonStyle.SetHovered(FSlateColorBrush(FLinearColor(0.12f, 0.16f, 0.19f, 1)));
    ButtonStyle.SetPressed(FSlateColorBrush(FLinearColor(0.19f, 0.15f, 0.09f, 1)));
    ButtonStyle.SetNormalPadding(FMargin(18, 11));
    ButtonStyle.SetPressedPadding(FMargin(18, 11));
    TSharedRef<SWidget> Root = SAssignNew(ViewportLayer, SOverlay)
        + SOverlay::Slot()
        [ SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
            [ SNew(SBox)
                .WidthOverride_Lambda([this] { return 1920.0f / UIScale; })
                .HeightOverride_Lambda([this] { return 1080.0f / UIScale; })
                [ SNew(SOverlay)
                    + SOverlay::Slot()[BuildFlightHUD()]
                    + SOverlay::Slot()[BuildPhotoHUD()]
                    + SOverlay::Slot()
                    [ SAssignNew(MenuHost, SBox).Visibility_Lambda([this] { return IsMenuOpen() ? EVisibility::Visible : EVisibility::Collapsed; }) ]
                ]
            ]
        ]
        // The camera projects into the whole viewport, not the letterboxed HUD rectangle.
        + SOverlay::Slot()
        [ SNew(SDPIScaler).DPIScale_Lambda([this]
            {
                const FVector2D Size = GuidanceViewportSize();
                return static_cast<float>(FMath::Min(Size.X / 1920.0, Size.Y / 1080.0)) * UIScale;
            })
            [BuildTargetGuidance()]
        ];
    RebuildMenu();
    return Root;
}

void UStarHUDWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    MenuHost.Reset(); MenuScroll.Reset(); MenuActions.Reset(); TargetGuidance.Reset(); ViewportLayer.Reset();
}

EVisibility UStarHUDWidget::FlightVisibility() const
{
    return Current.bPhotoMode || IsMenuOpen() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
}

TSharedRef<SWidget> UStarHUDWidget::BuildFlightHUD()
{
    return SNew(SOverlay).Visibility_Lambda([this] { return FlightVisibility(); })
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(52, 42)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[MakeText(T(TEXT("S T A R   /   EXPLORATION")), 15, Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
              [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.BodyName + (Current.bOnFoot?TEXT(""):TEXT(" 宙域"))); }), 26, White)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
              [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.WorldUtcText+FString::Printf(TEXT("  UTC · %.0f倍"),Current.WorldClockRate)); }), 13, Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
              [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.EarthSurfaceStatus); }),13,Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
              [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.StatusText); }), 15, Amber, 540)]
        ]
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(52, 42)
        [ SNew(SBox).WidthOverride(420)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[MakeText(T(TEXT("航法目標")), 13, Muted)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                  [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.TargetName); }), 28, White)]
                + SVerticalBox::Slot().AutoHeight()
                  [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Distance(Current.TargetDistanceM)); }), 20, White)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                  [SNew(SBox).Visibility_Lambda([this] { return Current.bNavigationAvailable ? EVisibility::Visible : EVisibility::Collapsed; })
                    [SNew(SVerticalBox)
                      + SVerticalBox::Slot().AutoHeight()[MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.NavigationStatusText); }), 15, Amber, 420)]
                      + SVerticalBox::Slot().AutoHeight().Padding(0, 7, 0, 0)[MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.TransferActionText); }), 14, White, 420)]
                      + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)[MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.AutopilotActionText); }), 14, White, 420)]
                    ]]
                 + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)
                   [SNew(SBox).Visibility_Lambda([this] { return Current.bGuidedTour ? EVisibility::Collapsed : EVisibility::Visible; })
                     [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.MissionTitle); }), 17, Amber, 420)]]
                 + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
                   [SNew(SBox).Visibility_Lambda([this] { return Current.bGuidedTour ? EVisibility::Collapsed : EVisibility::Visible; })
                     [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.MissionDetail); }), 14, Muted, 420)]]
                 + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)
                   [SNew(SBox).Visibility_Lambda([this] { return Current.bGuidedTour ? EVisibility::Visible : EVisibility::Collapsed; })
                     [SNew(SVerticalBox)
                       + SVerticalBox::Slot().AutoHeight()[MakeText(T(TEXT("ガイドツアー")), 14, Muted)]
                       + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
                         [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.GuidedTourTitle); }), 20, Amber, 420)]
                       + SVerticalBox::Slot().AutoHeight().Padding(0, 9, 0, 0)
                         [SNew(SBox).HeightOverride(4)
                           [SNew(SProgressBar).Percent_Lambda([this] { return FMath::Clamp(Current.GuidedTourProgress, 0.f, 1.f); }).FillColorAndOpacity(Amber)]]
                       + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
                         [MakeText(TAttribute<FText>::CreateLambda([this]
                           { return T(FString::Printf(TEXT("進行  %.0f%%"), FMath::Clamp(Current.GuidedTourProgress, 0.f, 1.f) * 100)); }), 13, Muted)]
                     ]]
            ]
        ]
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(52, 44)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[MakeMetric(T(TEXT("対天体速度")), TAttribute<FText>::CreateLambda([this] { return T(Speed(Current.SpeedMps)); }), 215)]
                + SHorizontalBox::Slot().AutoWidth()[MakeMetric(TAttribute<FText>::CreateLambda([this] { return T(Current.AltitudeReferenceName+TEXT(" 基準の高度")); }), TAttribute<FText>::CreateLambda([this] { return T(Distance(Current.AltitudeM)); }), 270)]
                + SHorizontalBox::Slot().AutoWidth()[MakeMetric(T(TEXT("昇降速度")), TAttribute<FText>::CreateLambda([this] { return T(Speed(Current.VerticalSpeedMps)); }), 205)]
            ]
            + SVerticalBox::Slot().AutoHeight()
              [MakeText(TAttribute<FText>::CreateLambda([this]
              { if(Current.bOnFoot)return T(TEXT("月面探査    |    着陸船は待機中"));
                return T(FString::Printf(TEXT("脚 %s    |    %s    |    推力指令 %.0f%%    |    エンジン出力 %.0f%%"),
                  Current.bGearDeployed ? TEXT("展開") : TEXT("格納"), Current.bCruise ? TEXT("巡航") : TEXT("通常飛行"),
                  FMath::Clamp(Current.Throttle, 0.f, 1.f) * 100, FMath::Clamp(Current.EngineOutput, 0.f, 1.f) * 100)); }), 16, White)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 9, 0, 0)
              [MakeText(TAttribute<FText>::CreateLambda([this]
              { return T(Current.bJoystickConnected && !Current.ControllerName.IsEmpty() ? Current.ControllerName : TEXT("キーボード・マウス / ゲームパッド")); }), 12, Muted)]
        ]
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(52, 44)
        [ SNew(SBox).WidthOverride(360).Visibility_Lambda([this] { return Current.bOnFoot?EVisibility::Collapsed:EVisibility::Visible; })
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                  [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.bCanScan ? TEXT("観測条件を満たしています") : TEXT("目標に向いて減速すると観測できます")); }), 14, Muted, 360)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12)
                  [SNew(SBox).HeightOverride(3)[SNew(SProgressBar).Percent_Lambda([this] { return FMath::Clamp(Current.ScanProgress, 0.f, 1.f); }).FillColorAndOpacity(Amber)] ]
                + SVerticalBox::Slot().AutoHeight()
                  [MakeText(TAttribute<FText>::CreateLambda([this] { return T(FString::Printf(TEXT("スキャン  %.0f%%"), FMath::Clamp(Current.ScanProgress, 0.f, 1.f) * 100)); }), 17, White)]
                 + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                   [MakeText(TAttribute<FText>::CreateLambda([this]
                     { return T(Current.bGuidedTour ? TEXT("Esc メニュー    M 手動操縦へ    V 視点    P 写真") : TEXT("Esc メニュー    V 視点    P 写真")); }), 12, Muted)]
            ]
        ];
}

bool UStarHUDWidget::HasTargetGuidance() const
{
    return Current.bHasTargetDirection && !Current.TargetName.IsEmpty()
        && FMath::IsFinite(Current.TargetScreenPositionNormalized.X)
        && FMath::IsFinite(Current.TargetScreenPositionNormalized.Y)
        && FMath::IsFinite(Current.TargetYawDeg) && FMath::IsFinite(Current.TargetPitchDeg);
}

FVector2D UStarHUDWidget::GuidanceViewportSize() const
{
    if (ViewportLayer.IsValid())
    {
        const FVector2D Size = ViewportLayer->GetCachedGeometry().GetLocalSize();
        if (Size.X > 0 && Size.Y > 0) return Size;
    }
    return FVector2D(1920, 1080);
}

FVector2D UStarHUDWidget::TargetMarkerPosition() const
{
    if (!HasTargetGuidance()) return FVector2D(0.5, 0.5);
    // Onscreen positions must remain exact; runtime clamps only offscreen / behind-camera cues.
    return Current.TargetScreenPositionNormalized;
}

FString UStarHUDWidget::TargetDirectionSymbol() const
{
    if (Current.bTargetOnScreen && !Current.bTargetBehind) return TEXT("◇");
    // Point toward the projected safe-area position, including runtime's behind-camera handling.
    const FVector2D Direction = (TargetMarkerPosition() - FVector2D(0.5, 0.5)) * GuidanceViewportSize();
    if (Direction.IsNearlyZero()) return TEXT("◇");
    const double X = FMath::Abs(Direction.X);
    const double Y = FMath::Abs(Direction.Y);
    if (Y < X * 0.4142) return Direction.X >= 0 ? TEXT("→") : TEXT("←");
    if (X < Y * 0.4142) return Direction.Y >= 0 ? TEXT("↓") : TEXT("↑");
    if (Direction.Y < 0) return Direction.X >= 0 ? TEXT("↗") : TEXT("↖");
    return Direction.X >= 0 ? TEXT("↘") : TEXT("↙");
}

FString UStarHUDWidget::TargetDirectionText() const
{
    FString Result = Current.bTargetBehind ? TEXT("後方") : TEXT("");
    const auto AddAngle = [&Result](float Angle, const TCHAR* Positive, const TCHAR* Negative)
    {
        if (FMath::Abs(Angle) < 0.5f) return;
        if (!Result.IsEmpty()) Result += TEXT("  ·  ");
        Result += FString::Printf(TEXT("%s %.0f°"), Angle >= 0 ? Positive : Negative, FMath::Abs(Angle));
    };
    AddAngle(Current.TargetYawDeg, TEXT("右"), TEXT("左"));
    AddAngle(Current.TargetPitchDeg, TEXT("上"), TEXT("下"));
    return Result;
}

TSharedRef<SWidget> UStarHUDWidget::BuildTargetGuidance()
{
    const auto Anchors = [this]
    {
        const FVector2D Position = TargetMarkerPosition();
        return FAnchors(static_cast<float>(Position.X), static_cast<float>(Position.Y));
    };
    // Labels grow inward at screen edges; the symbol stays exactly on the supplied marker center.
    const auto LabelAlignment = [this]
    {
        const FVector2D Position = TargetMarkerPosition();
        return FVector2D(Position.X < 0.2 ? 0.0 : Position.X > 0.8 ? 1.0 : 0.5, Position.Y > 0.75 ? 1.0 : 0.0);
    };
    const auto Justification = [this]
    {
        const double X = TargetMarkerPosition().X;
        return X < 0.2 ? ETextJustify::Left : X > 0.8 ? ETextJustify::Right : ETextJustify::Center;
    };
    TargetGuidance = SNew(SConstraintCanvas)
        .Visibility_Lambda([this] { return FlightVisibility().IsVisible() && HasTargetGuidance() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
        + SConstraintCanvas::Slot().Anchors_Lambda(Anchors).Alignment(FVector2D(0.5, 0.5)).AutoSize(true)
        [ SNew(SBox).WidthOverride(36).HeightOverride(36).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [MakeText(TAttribute<FText>::CreateLambda([this] { return T(TargetDirectionSymbol()); }), 28, Amber)] ]
        + SConstraintCanvas::Slot().Anchors_Lambda(Anchors).Alignment_Lambda(LabelAlignment).AutoSize(true)
          .Offset_Lambda([this] { return FMargin(0, TargetMarkerPosition().Y > 0.75 ? -24 : 24, 0, 0); })
        [ SNew(SBox).WidthOverride(280)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Text_Lambda([this] { return T(Current.TargetName); })
                    .Font(Font(16)).ColorAndOpacity(Amber).Justification_Lambda(Justification)
                    .ShadowOffset(FVector2D(0, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f)) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
                [ SNew(STextBlock).Text_Lambda([this] { return T(TargetDirectionText()); })
                    .Visibility_Lambda([this] { return Current.bTargetOnScreen && !Current.bTargetBehind ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
                    .Font(Font(13)).ColorAndOpacity(White).Justification_Lambda(Justification)
                    .ShadowOffset(FVector2D(0, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f)) ]
            ]
        ];
    return TargetGuidance.ToSharedRef();
}

TSharedRef<SWidget> UStarHUDWidget::BuildPhotoHUD()
{
    return SNew(SBox).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(30)
        .Visibility_Lambda([this] { return Current.bPhotoMode && !IsMenuOpen() && bPhotoControlsVisible ? EVisibility::Visible : EVisibility::Collapsed; })
        [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(PanelColor).Padding(20)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [MakeText(TAttribute<FText>::CreateLambda([this] { return T(FString::Printf(TEXT("写真   /   露出 %+.1f EV"), Current.ExposureBias)); }), 18, White)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                [ SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(4)[MakeButton(TEXT("露出 −"), TEXT("ExposureDelta"), -0.25f)]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4)[MakeButton(TEXT("撮影"), TEXT("PhotoCapture"))]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4)[MakeButton(TEXT("露出 ＋"), TEXT("ExposureDelta"), 0.25f)]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4)[MakeButton(TEXT("操作を隠す"), TEXT("HidePhotoControls"))]
                    + SHorizontalBox::Slot().AutoWidth().Padding(4)[MakeButton(TEXT("飛行に戻る"), TEXT("TogglePhoto"))]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                [MakeText(T(TEXT("H 操作表示    Enter 撮影    [ / ] 露出    P 戻る")), 12, Muted)]
            ]
        ];
}

TSharedRef<SWidget> UStarHUDWidget::MakeButton(const FString& Label, FName Action, float Value)
{
    TSharedPtr<SButton> Button;
    TSharedRef<SWidget> Widget = SAssignNew(Button, SButton).ButtonStyle(&ButtonStyle)
        .ButtonColorAndOpacity(FLinearColor::White).IsFocusable(true)
        .OnClicked_Lambda([this, Action, Value]
        {
            for (int32 I = 0; I < MenuActions.Num(); ++I)
                if (MenuActions[I].Name == Action && MenuActions[I].Value == Value) { FocusedAction = I; break; }
            return Activate(Action, Value);
        })
        [MakeText(TAttribute<FText>::CreateLambda([this, Label, Action]
        {
            if(Action==TEXT("FlightContext")) return T(Current.TransferActionText);
            if(Action==TEXT("ToggleAutopilot")) return T(Current.AutopilotActionText);
            return T(Label);
        }), 17, White, 420)];
    if (IsMenuOpen()) MenuActions.Add({Action, Value, Button});
    return Widget;
}

TSharedRef<SWidget> UStarHUDWidget::BuildMenu()
{
    const bool bMain = Panel == EPanel::Main;
    TSharedRef<SVerticalBox> NavigationPanel = SNew(SVerticalBox);
    const auto Add = [&](const FString& Label, FName Action)
    { NavigationPanel->AddSlot().AutoHeight().Padding(0, 4)[MakeButton(Label, Action)]; };
    Add(bMain ? TEXT("探査を始める") : (Current.bGuidedTour ? TEXT("ツアーを再開") : TEXT("飛行に戻る")), bMain ? FName(TEXT("StartFlight")) : FName(TEXT("Resume")));
    if (!bMain&&!Current.bGuidedTour&&!Current.bOnFoot) Add(TEXT("この場所からガイド航行"), TEXT("StartGuidedTour"));
    else if (Current.bGuidedTour) Add(Current.GuidedTourProgress >= 1.0f ? TEXT("ツアーを閉じて手動操縦へ") : TEXT("手動操縦へ切り替え"), TEXT("ManualTakeover"));
    Add(TEXT("観測目標・探査日誌"), TEXT("OpenMissions"));
    Add(TEXT("写真を撮る"), TEXT("TogglePhoto"));
    Add(TEXT("設定"), TEXT("OpenSettings"));
    if (!bMain) Add(TEXT("航海を保存"), TEXT("Save"));
    Add(TEXT("保存した航海を読み込む"), TEXT("Load"));
    if(!bMain) Add(TEXT("不具合調査のログを開く"),TEXT("OpenDiagnostics"));
    if(!bMain&&(Current.bLanded||Current.bOnFoot)) Add(Current.bOnFoot?TEXT("H  船へ戻る"):TEXT("H  船外へ出て月面を歩く"),TEXT("ToggleEVA"));
    if(!Current.bGuidedTour) Add(TEXT("ツアー後の航海を読み込む"), TEXT("LoadTour"));
    Add(TEXT("終了"), TEXT("Quit"));
    NavigationPanel->AddSlot().AutoHeight().Padding(0, 24, 0, 8)[MakeText(T(TEXT("航法目標")), 13, Muted)];
    Add(TEXT("地球   /   昼と夜の観測"), TEXT("TargetEarth"));
    Add(TEXT("月   /   軌道観測と着陸"), TEXT("TargetMoon"));
    Add(TEXT("土星   /   環と影の観測"), TEXT("TargetSaturn"));
    Add(TEXT("太陽   /   プラズマの観測"), TEXT("TargetSun"));
    if(!bMain&&!Current.bGuidedTour&&!Current.bOnFoot)
    {
        NavigationPanel->AddSlot().AutoHeight().Padding(0, 16, 0, 8)
            [MakeText(TAttribute<FText>::CreateLambda([this] { return T(Current.NavigationStatusText); }), 15, Amber, 420)];
        Add(TEXT("航行を確認"),TEXT("FlightContext"));
        Add(TEXT("自動操縦"),TEXT("ToggleAutopilot"));
        if(Current.bNavigationSafetyPause)
            Add(TEXT("B  安全に減速する（操船入力を停止）"),TEXT("NavigationSafeBrake"));
        Add(TEXT("C  周辺巡航 / 通常飛行"),TEXT("ToggleCruise"));
        NavigationPanel->AddSlot().AutoHeight().Padding(0, 8)
            [MakeText(T(TEXT("Cで周辺を高速飛行。W/Sで速度、Spaceで停止。惑星間を移動するときだけ、機首を目標へ向けFで確認します。Oは機首合わせ・自動航行、操船入力で解除できます。")), 13, Muted, 420)];
    }

    TSharedRef<SWidget> Contents = NavigationPanel;
    if (Panel == EPanel::Missions) Contents = BuildMissionPanel();
    else if (Panel == EPanel::Settings) Contents = BuildSettingsPanel();
    const bool bShowBack = Panel == EPanel::Missions || Panel == EPanel::Settings;
    TSharedRef<SWidget> Footer = bShowBack ? MakeButton(TEXT("戻る"), TEXT("Back")) : SNullWidget::NullWidget;
    const FString Heading = Panel == EPanel::Missions ? TEXT("観測目標・探査日誌") :
        Panel == EPanel::Settings ? TEXT("設定") : bMain ? TEXT("S T A R") : TEXT("航海を一時停止");
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.004f, 0.01f, 0.017f, 0.58f)).Padding(52, 32)
        [ SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Center)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(PanelColor).Padding(32, 25)
                [ SNew(SBox).WidthOverride(Panel == EPanel::Missions ? 1100 : Panel == EPanel::Settings ? 650 : 460).HeightOverride(720)
                    [ SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[MakeText(T(Heading), bMain ? 42 : 27, White)]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 22)
                          [MakeText(T(bMain ? TEXT("まだ見ぬ景色へ。地球、月、そして土星。\n世界の日時は設定で変更できます。") : TEXT("矢印 / 十字キー 選択   Enter / A 決定   Esc / B 戻る\nPageUp・PageDown / LB・RB スクロール")), 13, Muted, bMain?420:620)]
                        + SVerticalBox::Slot().FillHeight(1)[SAssignNew(MenuScroll, SScrollBox) + SScrollBox::Slot()[Contents]]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
                          [SNew(SBox).Visibility(bShowBack ? EVisibility::Visible : EVisibility::Collapsed)[Footer]]
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> UStarHUDWidget::BuildMissionPanel()
{
    // No hidden navigation buttons may remain in the keyboard focus ring.
    MenuActions.Reset();
    TSharedRef<SVerticalBox> Objectives = SNew(SVerticalBox);
    TSharedRef<SVerticalBox> Journal = SNew(SVerticalBox);
    Objectives->AddSlot().AutoHeight().Padding(0, 0, 0, 16)[MakeText(T(TEXT("6つの観測")), 20, White)];
    for (int32 I = 0; I < Current.Objectives.Num(); ++I)
    {
        Objectives->AddSlot().AutoHeight().Padding(0, 0, 0, 7)
          [MakeText(TAttribute<FText>::CreateLambda([this, I]
          { return Current.Objectives.IsValidIndex(I) ? T((Current.Objectives[I].bComplete ? TEXT("記録済  ") : TEXT("未記録  ")) + Current.Objectives[I].Title) : FText::GetEmpty(); }), 17, White, 500)];
        Objectives->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
          [MakeText(TAttribute<FText>::CreateLambda([this, I] { return Current.Objectives.IsValidIndex(I) ? T(Current.Objectives[I].Detail) : FText::GetEmpty(); }), 14, Muted, 500)];
        Objectives->AddSlot().AutoHeight().Padding(0, 0, 0, 20)
          [SNew(SBox).HeightOverride(3)[SNew(SProgressBar).Percent_Lambda([this, I] { return Current.Objectives.IsValidIndex(I) ? FMath::Clamp(Current.Objectives[I].Progress, 0.f, 1.f) : 0.f; }).FillColorAndOpacity(Amber)]];
    }
    Journal->AddSlot().AutoHeight().Padding(0, 0, 0, 16)[MakeText(T(TEXT("探査日誌")), 20, White)];
    TArray<FStarJournalEntry> Entries;
    if (auto* Instance = GetGameInstance())
        if (auto* Exploration = Instance->GetSubsystem<UStarExplorationSubsystem>()) Entries = Exploration->GetJournalEntries();
    if (Entries.IsEmpty()) Journal->AddSlot().AutoHeight()[MakeText(T(TEXT("最初の観測を終えると、ここに飛行記録が残ります。")), 15, Muted, 480)];
    for (const auto& Entry : Entries)
    {
        Journal->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[MakeText(T(Entry.Title), 17, Amber, 480)];
        Journal->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[MakeText(T(Entry.RuntimeSummary), 14, White, 480)];
        Journal->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[MakeText(T(Entry.ReferenceText), 13, Muted, 480)];
        Journal->AddSlot().AutoHeight().Padding(0, 0, 0, 24)[MakeText(T(TEXT("出典：") + Entry.SourceUrl), 11, Muted, 480)];
    }
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 30, 0)[Objectives]
            + SHorizontalBox::Slot().FillWidth(1).Padding(20, 0, 0, 0)[Journal]
        ];
}

TSharedRef<SWidget> UStarHUDWidget::BuildSettingsPanel()
{
    MenuActions.Reset();
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [MakeText(TAttribute<FText>::CreateLambda([this] { return T(TEXT("世界の日時（UTC）  ")+Current.WorldUtcText); }), 18, White)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SEditableTextBox).Font(Font(17)).Text(T(Current.WorldUtcText))
            .HintText(T(TEXT("2026-09-09T12:00:00Z")))
            .OnTextCommitted_Lambda([this](const FText& Text,ETextCommit::Type Type)
            { if(Type==ETextCommit::OnEnter)OnDateTime.ExecuteIfBound(Text.ToString()); })]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[MakeButton(TEXT("1時間前"),TEXT("ClockHour"),-1)]
            + SHorizontalBox::Slot().FillWidth(1)[MakeButton(TEXT("1時間後"),TEXT("ClockHour"),1)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
        [MakeButton(TEXT("現実の日時に合わせる"),TEXT("ClockNow"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
        [MakeText(TAttribute<FText>::CreateLambda([this] { return T(FString::Printf(TEXT("日時の進み方  %.0f倍"),Current.WorldClockRate)); }),16,White)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[MakeButton(TEXT("固定"),TEXT("ClockRate"),0)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[MakeButton(TEXT("1倍"),TEXT("ClockRate"),1)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[MakeButton(TEXT("60倍"),TEXT("ClockRate"),60)]
            + SHorizontalBox::Slot().FillWidth(1)[MakeButton(TEXT("600倍"),TEXT("ClockRate"),600)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,22)
        [MakeText(T(TEXT("入力後Enterで適用。UTCは日本時間の9時間前です。対応：2026年。メニュー中は進行停止。日時の倍率は機体の操縦速度を変えません。詳細地表は2021年の観測合成。未取得の範囲は従来の全球地図。天候は晴天です。現在の街並み・実際の天気とは異なります。")),13,Muted,640)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
        [MakeText(TAttribute<FText>::CreateLambda([this]
        {
            return T(Current.RenderWidth > 0 && Current.RenderHeight > 0
                ? FString::Printf(TEXT("表示解像度  %d × %d"), Current.RenderWidth, Current.RenderHeight)
                : TEXT("表示解像度  取得中"));
        }), 18, White)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
        [MakeText(TAttribute<FText>::CreateLambda([this]
        {
            if (!FMath::IsFinite(Current.RenderScalePercent) || Current.RenderScalePercent <= 0) return T(TEXT("描画倍率  取得中"));
            return T(FString::Printf(TEXT("描画倍率  %.0f%%%s"), Current.RenderScalePercent,
                FMath::IsNearlyEqual(Current.RenderScalePercent, 100.f, 0.01f) ? TEXT("（ネイティブ）") : TEXT("")));
        }), 14, Muted)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
        [MakeButton(TEXT("フライトスティック設定・校正"), TEXT("OpenInputSettings"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
        [MakeButton(TEXT("船内モニター音声 / 外部音響を切り替え"), TEXT("ToggleInteriorMonitor"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 22, 0, 7)
        [MakeText(TAttribute<FText>::CreateLambda([this]
        { return T(FString::Printf(TEXT("操縦補助  %s"), Current.bFlightAssist ? TEXT("オン") : TEXT("オフ"))); }), 19, White)]
        + SVerticalBox::Slot().AutoHeight()
        [MakeButton(TEXT("操縦補助を切り替え"), TEXT("ToggleFlightAssist"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
        [MakeText(T(TEXT("真空でのロールは姿勢を変えます。補助は姿勢をゆっくり整え、進路は推力で変わります。")), 13, Muted, 640)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [MakeText(TAttribute<FText>::CreateLambda([this] { return T(FString::Printf(TEXT("表示サイズ  %.0f%%"), UIScale * 100)); }), 20, White)]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)[MakeButton(TEXT("小さく"), TEXT("UIScaleDelta"), -0.05f)]
            + SHorizontalBox::Slot().FillWidth(1)[MakeButton(TEXT("大きく"), TEXT("UIScaleDelta"), 0.05f)] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 24, 0, 10)
        [MakeText(TAttribute<FText>::CreateLambda([this] { return T(FString::Printf(TEXT("音量  %.0f%%"), MasterVolume * 100)); }), 20, White)]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)[MakeButton(TEXT("下げる"), TEXT("MasterVolumeDelta"), -0.1f)]
            + SHorizontalBox::Slot().FillWidth(1)[MakeButton(TEXT("上げる"), TEXT("MasterVolumeDelta"), 0.1f)] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 24, 0, 10)
        [MakeText(TAttribute<FText>::CreateLambda([this] { return T(FString::Printf(TEXT("視点感度  %.1f"), LookSensitivity)); }), 20, White)]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)[MakeButton(TEXT("ゆっくり"), TEXT("LookSensitivityDelta"), -0.1f)]
            + SHorizontalBox::Slot().FillWidth(1)[MakeButton(TEXT("速く"), TEXT("LookSensitivityDelta"), 0.1f)] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 26, 0, 0)
          [MakeText(T(TEXT("W/S 推力  I/K ピッチ  J/L ヨー  Q/E ロール\n右マウス＋マウス 操船  矢印 見回す\nG 着陸脚  F 離陸 / 飛行中は航行確認\nO 自動操縦  C 周辺巡航 / 通常  Space 制動\nTab 目標  V 視点  R スキャン  P 写真\nPageUp / PageDown 上昇 / 下降\nF5 保存  F9 読み込み  F12 撮影  Esc 一時停止\n機器の切断・フォーカス喪失・一時停止・読込で航行許可を解除します。")), 14, Muted, 640)];
}

void UStarHUDWidget::ApplySnapshot(const FStarHUDSnapshot& Snapshot)
{
    const bool WasPhoto = Current.bPhotoMode;
    const bool WasGuidedTour = Current.bGuidedTour;
    const bool WasTourComplete = Current.GuidedTourProgress >= 1.0f;
    const bool WasSafetyPause = Current.bNavigationSafetyPause;
    const bool bGuidanceLayoutChanged = Current.TargetScreenPositionNormalized != Snapshot.TargetScreenPositionNormalized
        || Current.bHasTargetDirection != Snapshot.bHasTargetDirection || Current.bTargetOnScreen != Snapshot.bTargetOnScreen
        || Current.bTargetBehind != Snapshot.bTargetBehind;
    Current = Snapshot;
    // Canvas slot anchors are TAttributes; invalidate layout when their live projection changes.
    if (bGuidanceLayoutChanged && TargetGuidance.IsValid()) TargetGuidance->Invalidate(EInvalidateWidgetReason::Layout);
    if ((WasGuidedTour != Current.bGuidedTour || WasTourComplete != (Current.GuidedTourProgress >= 1.0f) || WasSafetyPause != Current.bNavigationSafetyPause) && IsMenuOpen()) RebuildMenu();
    if (!WasPhoto && Current.bPhotoMode) bPhotoControlsVisible = true;
    if (Current.bPaused && Panel == EPanel::None) ShowPanel(EPanel::Pause);
    else if (!Current.bPaused && Panel != EPanel::None && Panel != EPanel::Main) ShowPanel(EPanel::None);
}

void UStarHUDWidget::SetUIScale(float Value) { if (FMath::IsFinite(Value)) UIScale = FMath::Clamp(Value, 0.8f, 1.2f); }
void UStarHUDWidget::ApplyUserSettings(float Scale, float Volume, float Sensitivity)
{
    SetUIScale(Scale);
    if (FMath::IsFinite(Volume)) MasterVolume = FMath::Clamp(Volume, 0.f, 1.f);
    if (FMath::IsFinite(Sensitivity)) LookSensitivity = FMath::Clamp(Sensitivity, 0.1f, 3.f);
}
bool UStarHUDWidget::IsMenuOpen() const { return Panel != EPanel::None; }
void UStarHUDWidget::SetMainMenuVisible(bool bVisible) { ShowPanel(bVisible ? EPanel::Main : EPanel::None); }

void UStarHUDWidget::ShowPanel(EPanel Value)
{
    Panel = Value;
    RebuildMenu();
    if (IsMenuOpen()) FocusAction(0);
}

void UStarHUDWidget::RebuildMenu()
{
    MenuActions.Reset();
    if (MenuHost.IsValid() && IsMenuOpen()) MenuHost->SetContent(BuildMenu());
}

void UStarHUDWidget::FocusAction(int32 Index)
{
    if (MenuActions.IsEmpty()) return;
    FocusedAction = (Index % MenuActions.Num() + MenuActions.Num()) % MenuActions.Num();
    if (MenuActions[FocusedAction].Button.IsValid())
        FSlateApplication::Get().SetKeyboardFocus(MenuActions[FocusedAction].Button, EFocusCause::Navigation);
}

FReply UStarHUDWidget::Activate(FName Action, float Value)
{
    if (Action == TEXT("OpenMissions") || Action == TEXT("OpenSettings"))
    {
        ReturnPanel = Panel == EPanel::Main ? EPanel::Main : EPanel::Pause;
        ShowPanel(Action == TEXT("OpenMissions") ? EPanel::Missions : EPanel::Settings);
        return FReply::Handled();
    }
    if (Action == TEXT("Back")) { ShowPanel(ReturnPanel); return FReply::Handled(); }
    if (Action == TEXT("HidePhotoControls")) { bPhotoControlsVisible = false; return FReply::Handled(); }
    if (Action == TEXT("UIScaleDelta")) SetUIScale(UIScale + Value);
    if (Action == TEXT("MasterVolumeDelta")) MasterVolume = FMath::Clamp(MasterVolume + Value, 0.f, 1.f);
    if (Action == TEXT("LookSensitivityDelta")) LookSensitivity = FMath::Clamp(LookSensitivity + Value, 0.1f, 3.f);
    if (Action == TEXT("StartFlight") || Action == TEXT("StartGuidedTour") || Action == TEXT("Resume") || Action == TEXT("TogglePhoto")) ShowPanel(EPanel::None);
    OnAction.ExecuteIfBound(Action, Value);
    return FReply::Handled();
}

FReply UStarHUDWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    const FKey Key = Event.GetKey();
    if(IsMenuOpen()&&Key!=EKeys::Escape&&FSlateApplication::IsInitialized())
    {
        const auto Focus=FSlateApplication::Get().GetKeyboardFocusedWidget();
        if(Focus.IsValid()&&Focus->GetTypeAsString().Contains(TEXT("EditableText"))) return FReply::Unhandled();
    }
    if (IsMenuOpen())
    {
        if(Panel!=EPanel::Main&&!Current.bPhotoMode&&!Event.IsRepeat())
        {
            if(Key==EKeys::F) return Activate(TEXT("FlightContext"),1);
            if(Key==EKeys::O) return Activate(TEXT("ToggleAutopilot"),1);
            if(Key==EKeys::B) return Activate(TEXT("NavigationSafeBrake"),1);
            if(Key==EKeys::C) return Activate(TEXT("ToggleCruise"),1);
        }
        if (Key == EKeys::PageUp || Key == EKeys::PageDown || Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder)
        {
            const float Direction = Key == EKeys::PageUp || Key == EKeys::Gamepad_LeftShoulder ? -1.f : 1.f;
            if (MenuScroll.IsValid()) MenuScroll->SetScrollOffset(FMath::Max(0.f, MenuScroll->GetScrollOffset() + Direction * 280.f));
            return FReply::Handled();
        }
        if (Key == EKeys::Down || Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Tab)
        { FocusAction(FocusedAction + (Event.IsShiftDown() ? -1 : 1)); return FReply::Handled(); }
        if (Key == EKeys::Up || Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Left)
        { FocusAction(FocusedAction - 1); return FReply::Handled(); }
        if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
        {
            if (Event.IsRepeat()) return FReply::Handled();
            if (MenuActions.IsValidIndex(FocusedAction))
            { const auto Action = MenuActions[FocusedAction]; return Activate(Action.Name, Action.Value); }
        }
        if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
        {
            if (Panel == EPanel::Missions || Panel == EPanel::Settings) return Activate(TEXT("Back"), 1);
            if (Panel != EPanel::Main) return Activate(TEXT("Resume"), 1);
            return FReply::Handled();
        }
    }
    if (Current.bPhotoMode && !Event.IsRepeat())
    {
        if (Key == EKeys::H) { bPhotoControlsVisible = !bPhotoControlsVisible; return FReply::Handled(); }
        if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom) return Activate(TEXT("PhotoCapture"), 1);
        if (Key == EKeys::LeftBracket || Key == EKeys::Gamepad_LeftShoulder) return Activate(TEXT("ExposureDelta"), -0.25f);
        if (Key == EKeys::RightBracket || Key == EKeys::Gamepad_RightShoulder) return Activate(TEXT("ExposureDelta"), 0.25f);
        if (Key == EKeys::P || Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right) return Activate(TEXT("TogglePhoto"), 1);
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UStarHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();
    const TWeakObjectPtr<UStarHUDWidget> WeakThis(this);
    MenuInputProcessor=MakeShared<FStarMenuInputProcessor>([WeakThis](const FKeyEvent& Event)
    {
        auto* Widget=WeakThis.Get();
        if(!Widget || !Widget->IsInViewport() || (!Widget->IsMenuOpen()&&!Widget->Current.bPhotoMode)) return false;
        if(Event.IsRepeat()) return true;
        return Widget->NativeOnPreviewKeyDown(Widget->GetCachedGeometry(),Event).IsEventHandled();
    });
    FSlateApplication::Get().RegisterInputPreProcessor(MenuInputProcessor,0);
}

void UStarHUDWidget::NativeDestruct()
{
    if(FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(MenuInputProcessor);
    MenuInputProcessor.Reset();
    Super::NativeDestruct();
}
