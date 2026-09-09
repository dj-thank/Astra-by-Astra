#pragma once
#include "CoreMinimal.h"
class APlayerController;
class AStarShipPawn;
struct FStarHUDSnapshot;
namespace StarTrackingProbe
{
    // Explicit QA caller only; records real component centers without moving them.
    void Append(APlayerController& Controller,AStarShipPawn& Ship,const FString& Filename,const FStarHUDSnapshot* HUD=nullptr);
}
