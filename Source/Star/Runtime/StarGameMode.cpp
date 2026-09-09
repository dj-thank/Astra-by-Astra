#include "Runtime/StarGameMode.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarPlayerController.h"
#include "Runtime/StarWorldDirector.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
AStarGameMode::AStarGameMode()
{
    DefaultPawnClass=AStarShipPawn::StaticClass();
    PlayerControllerClass=AStarPlayerController::StaticClass();
    HUDClass=nullptr;
}
void AStarGameMode::StartPlay()
{
    if(!UGameplayStatics::GetActorOfClass(GetWorld(),AStarWorldDirector::StaticClass()))
        GetWorld()->SpawnActor<AStarWorldDirector>();
    Super::StartPlay();
}
