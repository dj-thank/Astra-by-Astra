#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StarGameMode.generated.h"
UCLASS()
class STAR_API AStarGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AStarGameMode();
    virtual void StartPlay() override;
};
