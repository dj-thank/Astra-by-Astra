#include "Presentation/StarInstrumentWidget.h"
#include "UI/StarFonts.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor InstrumentScreen(0.006f, 0.014f, 0.021f, 1.0f);
const FLinearColor InstrumentInk(0.78f, 0.94f, 0.95f, 1.0f);
const FLinearColor InstrumentMuted(0.34f, 0.58f, 0.63f, 1.0f);
const FLinearColor InstrumentAccent(0.17f, 0.80f, 0.75f, 1.0f);
const FLinearColor InstrumentAmber(0.96f, 0.65f, 0.28f, 1.0f);
const FLinearColor InstrumentRule(0.03f, 0.14f, 0.18f, 1.0f);
FText InstrumentText(const FString& Value) { return FText::FromString(Value); }
FText InstrumentUnavailable() { return InstrumentText(TEXT("—")); }

TSharedRef<SWidget> InstrumentDivider()
{
    return SNew(SBox).HeightOverride(2)
        [SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(InstrumentRule)];
}
}

void UStarInstrumentWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);
}

void UStarInstrumentWidget::ApplySnapshot(const FStarHUDSnapshot& Snapshot)
{
    Current = Snapshot;
    bHasSnapshot = true;
    InvalidateLayoutAndVolatility();
}

void UStarInstrumentWidget::SetDisplayMode(FName Mode)
{
    if (Mode == TEXT("Flight")) bNavigation = false;
    else if (Mode == TEXT("Navigation")) bNavigation = true;
    else return;
    InvalidateLayoutAndVolatility();
}

FSlateFontInfo UStarInstrumentWidget::Font(float Size) const
{
    return StarJapaneseFont(Size, JapaneseFont.Get());
}

TSharedRef<SWidget> UStarInstrumentWidget::MakeText(TAttribute<FText> Value, float Size, FLinearColor Color) const
{
    return SNew(STextBlock).Text(Value).Font(Font(Size)).ColorAndOpacity(Color);
}

TSharedRef<SWidget> UStarInstrumentWidget::MakeMetric(const FString& Label, TAttribute<FText> Value, float Size) const
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[MakeText(InstrumentText(Label), 18, InstrumentMuted)]
        + SVerticalBox::Slot().FillHeight(1).VAlign(VAlign_Center).Padding(0, 3, 10, 0)
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            .HAlign(HAlign_Left)[MakeText(Value, Size, InstrumentInk)]];
}

FString UStarInstrumentWidget::Distance(double Metres)
{
    if (!FMath::IsFinite(Metres)) return TEXT("—");
    const double Abs = FMath::Abs(Metres);
    if (Abs >= 1.495978707e11) return FString::Printf(TEXT("%.3f AU"), Metres / 1.495978707e11);
    if (Abs >= 1e9) return FString::Printf(TEXT("%.2f Gm"), Metres / 1e9);
    if (Abs >= 1e6) return FString::Printf(TEXT("%.2f Mm"), Metres / 1e6);
    if (Abs >= 1000) return FString::Printf(TEXT("%.2f km"), Metres / 1000);
    return FString::Printf(TEXT("%.1f m"), Metres);
}

FString UStarInstrumentWidget::Speed(double MetresPerSecond, bool bSigned)
{
    if (!FMath::IsFinite(MetresPerSecond)) return TEXT("—");
    const double Abs = FMath::Abs(MetresPerSecond);
    if (Abs >= 299792458) return bSigned
        ? FString::Printf(TEXT("%+.2f c"), MetresPerSecond / 299792458)
        : FString::Printf(TEXT("%.2f c"), MetresPerSecond / 299792458);
    if (Abs >= 1000) return bSigned
        ? FString::Printf(TEXT("%+.2f km/s"), MetresPerSecond / 1000)
        : FString::Printf(TEXT("%.2f km/s"), MetresPerSecond / 1000);
    // Avoid a distracting -0.0 at rest, preserving meaningful signed descent speed.
    const double Value = Abs < 0.05 ? 0.0 : MetresPerSecond;
    return bSigned ? FString::Printf(TEXT("%+.1f m/s"), Value) : FString::Printf(TEXT("%.1f m/s"), Value);
}

FText UStarInstrumentWidget::FlightState() const
{
    if (!bHasSnapshot) return InstrumentText(TEXT("データ待機"));
    if (Current.bPaused) return InstrumentText(TEXT("一時停止"));
    if (Current.bLanded) return InstrumentText(TEXT("接地"));
    return InstrumentText(Current.bCruise ? TEXT("巡航") : TEXT("通常航行"));
}

TSharedRef<SWidget> UStarInstrumentWidget::RebuildWidget()
{
    JapaneseFont = LoadObject<UFont>(nullptr, TEXT("/Game/Star/UI/Fonts/NotoSansJP.NotoSansJP"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    return SNew(SScaleBox).Stretch(EStretch::ScaleToFit).Visibility(EVisibility::HitTestInvisible)
    [SNew(SBox).WidthOverride(1024).HeightOverride(512)
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(InstrumentScreen).Padding(FMargin(28, 20))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [MakeText(TAttribute<FText>::CreateLambda([this]
                        { return InstrumentText(bNavigation ? TEXT("航法   /   NAVIGATION") : TEXT("航行   /   FLIGHT")); }), 21, InstrumentAccent)]
                    + SHorizontalBox::Slot().AutoWidth()
                    [MakeText(TAttribute<FText>::CreateLambda([this] { return FlightState(); }), 21, InstrumentInk)]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 14)[InstrumentDivider()]
                + SVerticalBox::Slot().FillHeight(1)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SBox).Visibility_Lambda([this] { return bNavigation ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
                        [BuildFlightDisplay()]]
                    + SOverlay::Slot()
                    [SNew(SBox).Visibility_Lambda([this] { return bNavigation ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                        [BuildNavigationDisplay()]]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 10)[InstrumentDivider()]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 26, 0)
                    [MakeText(TAttribute<FText>::CreateLambda([this]
                        { return bHasSnapshot ? InstrumentText(Current.bGearDeployed ? TEXT("脚  展開") : TEXT("脚  格納")) : InstrumentUnavailable(); }), 22, InstrumentAmber)]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly).HAlign(HAlign_Left)
                        [MakeText(TAttribute<FText>::CreateLambda([this]
                            { return bHasSnapshot ? InstrumentText(Current.StatusText) : InstrumentText(TEXT("飛行データを待っています")); }), 17, InstrumentMuted)]]
                    + SHorizontalBox::Slot().AutoWidth().Padding(16, 0, 0, 0)
                    [MakeText(InstrumentText(TEXT("S T A R")), 17, InstrumentMuted)]]
            ]
        ]
    ];
}

TSharedRef<SWidget> UStarInstrumentWidget::BuildFlightDisplay()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 28, 10)
            [MakeMetric(TEXT("対天体速度"), TAttribute<FText>::CreateLambda([this]
                { return bHasSnapshot ? InstrumentText(Speed(Current.SpeedMps)) : InstrumentUnavailable(); }), 51)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(20, 0, 0, 10)
            [MakeMetric(TEXT("高度"), TAttribute<FText>::CreateLambda([this]
                { return bHasSnapshot ? InstrumentText(Distance(Current.AltitudeM)) : InstrumentUnavailable(); }), 51)]]
        + SVerticalBox::Slot().FillHeight(1).Padding(0, 14, 0, 0)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 28, 0)
            [MakeMetric(TEXT("昇降速度   ＋上昇 / −降下"), TAttribute<FText>::CreateLambda([this]
                { return bHasSnapshot ? InstrumentText(Speed(Current.VerticalSpeedMps, true)) : InstrumentUnavailable(); }), 43)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(20, 0, 0, 0)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().FillHeight(0.55f)
                [MakeMetric(TEXT("推力指令（入力）"), TAttribute<FText>::CreateLambda([this]
                    { return bHasSnapshot && FMath::IsFinite(Current.Throttle)
                        ? InstrumentText(FString::Printf(TEXT("%.0f %%"), FMath::Clamp(Current.Throttle, 0.f, 1.f) * 100)) : InstrumentUnavailable(); }), 38)]
                + SVerticalBox::Slot().FillHeight(0.45f)
                [MakeMetric(TEXT("エンジン出力（実負荷）"), TAttribute<FText>::CreateLambda([this]
                    { return bHasSnapshot && FMath::IsFinite(Current.EngineOutput)
                        ? InstrumentText(FString::Printf(TEXT("%.0f %%"), FMath::Clamp(Current.EngineOutput, 0.f, 1.f) * 100)) : InstrumentUnavailable(); }), 34)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 10, 2)
                [SNew(SBox).HeightOverride(4)
                    [SNew(SProgressBar).Percent_Lambda([this]
                        { return bHasSnapshot && FMath::IsFinite(Current.EngineOutput) ? FMath::Clamp(Current.EngineOutput, 0.f, 1.f) : 0.f; })
                        .FillColorAndOpacity(InstrumentAccent)]]]];
}

TSharedRef<SWidget> UStarInstrumentWidget::BuildNavigationDisplay()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.62f).Padding(0, 0, 30, 0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[MakeText(InstrumentText(TEXT("航法目標")), 18, InstrumentMuted)]
            + SVerticalBox::Slot().FillHeight(0.50f).Padding(0, 0, 0, 8)
            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly).HAlign(HAlign_Left)
                [MakeText(TAttribute<FText>::CreateLambda([this]
                    { return bHasSnapshot && !Current.TargetName.IsEmpty() ? InstrumentText(Current.TargetName) : InstrumentUnavailable(); }), 61, InstrumentInk)]]
            + SVerticalBox::Slot().FillHeight(0.50f)
            [MakeMetric(TEXT("目標までの距離"), TAttribute<FText>::CreateLambda([this]
                { return bHasSnapshot ? InstrumentText(Distance(Current.TargetDistanceM)) : InstrumentUnavailable(); }), 47)]]
        + SHorizontalBox::Slot().FillWidth(0.38f).Padding(18, 0, 0, 0)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[MakeText(InstrumentText(TEXT("基準天体")), 18, InstrumentMuted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 16)
            [MakeText(TAttribute<FText>::CreateLambda([this]
                { return bHasSnapshot && !Current.BodyName.IsEmpty() ? InstrumentText(Current.BodyName) : InstrumentUnavailable(); }), 29, InstrumentInk)]
            + SVerticalBox::Slot().FillHeight(1)
            [MakeMetric(TEXT("対天体速度"), TAttribute<FText>::CreateLambda([this]
                { return bHasSnapshot ? InstrumentText(Speed(Current.SpeedMps)) : InstrumentUnavailable(); }), 30)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 8)
            [MakeText(TAttribute<FText>::CreateLambda([this]
                { return !bHasSnapshot ? InstrumentUnavailable() : InstrumentText(Current.bCanScan ? TEXT("観測可能") : TEXT("観測条件待ち")); }), 20, InstrumentAccent)]
            + SVerticalBox::Slot().AutoHeight()
            [MakeText(TAttribute<FText>::CreateLambda([this]
                { return bHasSnapshot && FMath::IsFinite(Current.ScanProgress)
                    ? InstrumentText(FString::Printf(TEXT("スキャン  %.0f %%"), FMath::Clamp(Current.ScanProgress, 0.f, 1.f) * 100)) : InstrumentUnavailable(); }), 20, InstrumentInk)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 6)
            [SNew(SBox).HeightOverride(5)
                [SNew(SProgressBar).Percent_Lambda([this]
                    { return bHasSnapshot && FMath::IsFinite(Current.ScanProgress) ? FMath::Clamp(Current.ScanProgress, 0.f, 1.f) : 0.f; })
                    .FillColorAndOpacity(InstrumentAccent)]]];
}
