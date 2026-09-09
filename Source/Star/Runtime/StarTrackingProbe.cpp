#include "Runtime/StarTrackingProbe.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Runtime/StarDataCatalog.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

void StarTrackingProbe::Append(APlayerController& Controller,AStarShipPawn& Ship,const FString& Filename,const FStarHUDSnapshot* HUD)
{
    auto* Director=Ship.Director();
    if(!Director||!Ship.Simulation()) return;
    int32 Width=0,Height=0;Controller.GetViewportSize(Width,Height);
    if(Width<=0||Height<=0) return;
    FMinimalViewInfo Camera;Ship.CalcCamera(0,Camera);
    const double TanHalf=FMath::Tan(FMath::DegreesToRadians(Ship.CameraHorizontalFOVDegrees()*0.5));
    const FVector Forward=Camera.Rotation.Vector();
    const FVector Right=FRotationMatrix(Camera.Rotation).GetUnitAxis(EAxis::Y);
    const FVector Up=FRotationMatrix(Camera.Rotation).GetUnitAxis(EAxis::Z);
    for(const FString BodyId:{FString(TEXT("moon")),FString(TEXT("saturn"))})
    {
        const auto* Body=Director->Catalog().Find(BodyId);FVector Center;
        if(!Body||!Director->GetRenderedBodyCenter(BodyId,Center)) continue;
        const auto Direction=(Body->Definition.centerMeters-Ship.CameraAbsoluteMeters()).Normalized();
        const double Ahead=star::Vec3d::Dot(Direction,Ship.CameraForwardSimulation());
        const FVector RenderDirection=(Center-Camera.Location).GetSafeNormal();
        const double RenderAhead=FVector::DotProduct(RenderDirection,Forward);
        auto Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("body"),BodyId);
        Row->SetNumberField(TEXT("simulationSeconds"),Ship.Simulation()->State().simulationTimeSeconds);
        Row->SetBoolField(TEXT("cockpit"),Ship.IsCockpitView());
        Row->SetNumberField(TEXT("width"),Width);Row->SetNumberField(TEXT("height"),Height);
        const auto& Origin=Ship.OriginMeters();
        Row->SetArrayField(TEXT("originMeters"),{MakeShared<FJsonValueNumber>(Origin.x),MakeShared<FJsonValueNumber>(Origin.y),MakeShared<FJsonValueNumber>(Origin.z)});
        Row->SetNumberField(TEXT("ahead"),Ahead);
        Row->SetNumberField(TEXT("renderAhead"),RenderAhead);
        if(Ahead>0.001&&RenderAhead>0.001)
        {
            const FVector2D Expected(Width*0.5+star::Vec3d::Dot(Direction,Ship.CameraRightSimulation())/Ahead/TanHalf*Width*0.5,
                Height*0.5-star::Vec3d::Dot(Direction,Ship.CameraUpSimulation())/Ahead/TanHalf*Width*0.5);
            const FVector2D Actual(Width*0.5+FVector::DotProduct(RenderDirection,Right)/RenderAhead/TanHalf*Width*0.5,
                Height*0.5-FVector::DotProduct(RenderDirection,Up)/RenderAhead/TanHalf*Width*0.5);
            Row->SetNumberField(TEXT("componentProjectionErrorPixels"),(Actual-Expected).Size());
            Row->SetBoolField(TEXT("onScreen"),Expected.X>=0&&Expected.X<=Width&&Expected.Y>=0&&Expected.Y<=Height);
            if(HUD&&HUD->TargetId==BodyId&&HUD->bTargetOnScreen)
            {
                const FVector2D Anchor(HUD->TargetScreenPositionNormalized.X*Width,HUD->TargetScreenPositionNormalized.Y*Height);
                Row->SetStringField(TEXT("hudTarget"),HUD->TargetId);
                Row->SetNumberField(TEXT("hudAnchorErrorPixels"),(Anchor-Actual).Size());
            }
            FVector2D Projected;
            if(Controller.ProjectWorldLocationToScreen(Center,Projected))
                Row->SetNumberField(TEXT("controllerProjectionErrorPixels"),(Projected-Expected).Size());
            if(Controller.PlayerCameraManager)
            {
                Row->SetNumberField(TEXT("cachedCameraLagCm"),(Controller.PlayerCameraManager->GetCameraLocation()-Camera.Location).Size());
                Row->SetNumberField(TEXT("cachedCameraLagDegrees"),(Controller.PlayerCameraManager->GetCameraRotation()-Camera.Rotation).GetNormalized().GetManhattanDistance(FRotator::ZeroRotator));
            }
        }
        FString Text;FJsonSerializer::Serialize(Row,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));Text+=TEXT("\n");
        FFileHelper::SaveStringToFile(Text,*Filename,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append | FILEWRITE_AllowRead);
    }
}
