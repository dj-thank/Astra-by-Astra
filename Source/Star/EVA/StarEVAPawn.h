#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "EVA/LunarWalkModel.h"
#include "StarEVAPawn.generated.h"

class AStarShipPawn;
class UCameraComponent;

UCLASS()
class STAR_API AStarEVAPawn : public APawn
{
    GENERATED_BODY()
public:
    AStarEVAPawn();
    bool InitializeFromShip(AStarShipPawn* Ship);
    void AdvanceWalking(double Dt, FVector2D Move, float YawDelta, float PitchDelta, bool bPaused);
    bool CanBoardShip() const;
    star::LunarWalkSaveState ExportSaveState() const { return Walk.ExportSaveState(); }
    bool RestoreSaveState(const star::LunarWalkSaveState& Saved);
    bool MovementBlocked() const { return Walk.MovementBlocked(); }
    double DistanceToBoardingPointMeters() const { return Walk.DistanceToBoardingPoint(); }
    star::Vec3d PositionAbsoluteMeters() const { return Walk.Position(); }
    star::Vec3d CameraAbsoluteMeters() const;
    star::Vec3d CameraForwardSimulation() const;
    star::Vec3d CameraRightSimulation() const;
    star::Vec3d CameraUpSimulation() const;
    USceneComponent* VRTrackingOrigin() const { return TrackingOrigin; }
    float CameraHorizontalFOVDegrees() const { return 85.0f; }
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
private:
    bool bVR=false;
    UPROPERTY() TObjectPtr<USceneComponent> TrackingOrigin;
    void RefreshTransform();
    UPROPERTY() TObjectPtr<USceneComponent> EVARoot;
    UPROPERTY() TObjectPtr<UCameraComponent> EVACamera;
    UPROPERTY() TWeakObjectPtr<AStarShipPawn> ParkedShip;
    star::LunarWalkModel Walk;
};
