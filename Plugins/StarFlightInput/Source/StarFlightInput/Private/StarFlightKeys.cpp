#include "StarFlightKeys.h"

#define STAR_KEY(Name) const FKey FStarFlightKeys::Name(TEXT("StarFlight_" #Name));
STAR_KEY(Yaw) STAR_KEY(Pitch) STAR_KEY(Roll) STAR_KEY(Throttle) STAR_KEY(LookX) STAR_KEY(LookY)
STAR_KEY(Scan) STAR_KEY(ToggleView) STAR_KEY(Brake) STAR_KEY(ToggleGear) STAR_KEY(ToggleCruise) STAR_KEY(TargetNext)
STAR_KEY(TogglePhoto) STAR_KEY(Pause) STAR_KEY(RecenterLook) STAR_KEY(Precision)
STAR_KEY(Aux11) STAR_KEY(Aux12) STAR_KEY(Aux13) STAR_KEY(Aux14) STAR_KEY(Aux15) STAR_KEY(Aux16)
#undef STAR_KEY
const FKey& FStarFlightKeys::Button(int32 Index)
{
    static const FKey* Keys[] = { &Scan,&ToggleView,&Brake,&ToggleGear,&ToggleCruise,&TargetNext,
        &TogglePhoto,&Pause,&RecenterLook,&Precision,&Aux11,&Aux12,&Aux13,&Aux14,&Aux15,&Aux16 };
    return *Keys[FMath::Clamp(Index, 0, 15)];
}
void FStarFlightKeys::Register()
{
    const FName Category(TEXT("StarFlightInput"));
    EKeys::AddMenuCategoryDisplayInfo(Category, FText::FromString(TEXT("STAR フライトスティック")), TEXT("GraphEditor.PadEvent_16x"));
    const FKey* Axes[] = {&Yaw,&Pitch,&Roll,&Throttle,&LookX,&LookY};
    const TCHAR* Labels[] = {TEXT("ヨー"),TEXT("ピッチ"),TEXT("ロール"),TEXT("スロットル"),TEXT("視点 左右"),TEXT("視点 上下")};
    for (int32 i=0; i<6; ++i)
        EKeys::AddKey(FKeyDetails(*Axes[i], FText::FromString(Labels[i]), FKeyDetails::GamepadKey | FKeyDetails::Axis1D, Category));
    const TCHAR* Buttons[] = {TEXT("スキャン"),TEXT("視点切替"),TEXT("ブレーキ"),TEXT("着陸脚"),TEXT("巡航切替"),TEXT("次の目標"),TEXT("写真モード"),TEXT("一時停止"),TEXT("視点を戻す"),TEXT("精密操縦"),TEXT("離陸 / 上昇"),TEXT("保存"),TEXT("読み込み"),TEXT("降下 / 写真モードで撮影"),TEXT("地球を目標に"),TEXT("土星を目標に")};
    for (int32 i=0; i<16; ++i)
        EKeys::AddKey(FKeyDetails(Button(i), FText::FromString(Buttons[i]), FKeyDetails::GamepadKey, Category));
}
