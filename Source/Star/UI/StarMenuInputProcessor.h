#pragma once
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

// Modal Back must work even when a Slate child or popup owns keyboard focus.
class FStarMenuInputProcessor final : public IInputProcessor
{
public:
    explicit FStarMenuInputProcessor(TFunction<bool(const FKeyEvent&)> InBack) : Back(MoveTemp(InBack)) {}
    void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
    bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
    {
        if (Event.GetKey()!=EKeys::Escape && Event.GetKey()!=EKeys::Gamepad_FaceButton_Right) return false;
        return Back(Event);
    }
private:
    TFunction<bool(const FKeyEvent&)> Back;
};
