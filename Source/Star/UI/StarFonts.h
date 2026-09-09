#pragma once

#include "CoreMinimal.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/Paths.h"

// The licensed OTF is staged as NonUFS so the same font works in Editor and
// packaged builds, even where Python cannot author FFontData for a UFont asset.
inline FSlateFontInfo StarJapaneseFont(float Size, UFont* Candidate)
{
    if (Candidate)
    {
        const auto* Composite = Candidate->GetCompositeFont();
        if (Composite && !Composite->DefaultTypeface.Fonts.IsEmpty()) return FSlateFontInfo(Candidate, Size);
    }
    static const TSharedPtr<const FCompositeFont> Fallback = []
    {
        auto Font = MakeShared<FCompositeFont>();
        Font->DefaultTypeface.Fonts.Emplace(FName(TEXT("Regular")),
            FPaths::ProjectContentDir() / TEXT("Star/UI/Fonts/NotoSansCJKjp-Regular.otf"),
            EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
        return Font;
    }();
    return FSlateFontInfo(Fallback, Size, TEXT("Regular"));
}
