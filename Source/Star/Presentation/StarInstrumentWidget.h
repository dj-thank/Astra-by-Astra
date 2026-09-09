#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "StarViewTypes.h"
#include "StarInstrumentWidget.generated.h"

class SWidget;
class UFont;

/** Read-only, 1024 x 512 cockpit display. The runtime supplies all flight data. */
UCLASS()
class STAR_API UStarInstrumentWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "STAR|Instruments")
    void ApplySnapshot(const FStarHUDSnapshot& Snapshot);

    /** Valid names are Flight (default) and Navigation. Safe before widget creation. */
    UFUNCTION(BlueprintCallable, Category = "STAR|Instruments")
    void SetDisplayMode(FName Mode);

protected:
    virtual void NativeOnInitialized() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UPROPERTY(Transient) TObjectPtr<UFont> JapaneseFont;
    FStarHUDSnapshot Current;
    bool bHasSnapshot = false;
    bool bNavigation = false;

    FSlateFontInfo Font(float Size) const;
    TSharedRef<SWidget> MakeText(TAttribute<FText> Value, float Size, FLinearColor Color) const;
    TSharedRef<SWidget> MakeMetric(const FString& Label, TAttribute<FText> Value, float Size = 48) const;
    TSharedRef<SWidget> BuildFlightDisplay();
    TSharedRef<SWidget> BuildNavigationDisplay();
    FText FlightState() const;
    static FString Distance(double Metres);
    static FString Speed(double MetresPerSecond, bool bSigned = false);
};
