#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Simulation/SolarLighting.h"
#include "Simulation/ObservationGeometry.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Engine/World.h"
#include "UnrealClient.h"

void AStarPlayerController::UpdateUnifiedWorldQA(double Dt)
{
    if(!Ship||!Ship->IsReady()||BenchmarkStage!=0)return;
    const double Time=BenchmarkStageTime;
    auto* Director=Ship->Director();
    if(!UnifiedGuideChecked&&Time>=1.0)
    {
        const auto Before=Ship->Simulation()->State();const auto Utc=Director->WorldUtc();
        const bool Slot=bTourSaveSlot;
        // Exercise the real in-voyage guide action before throttling up.
        StartGuidedTour();const bool Started=bGuidedTour;
        if(Started)ManualTakeover();
        const auto& After=Ship->Simulation()->State();
        UnifiedGuidePreserved=Started&&(After.positionMeters-Before.positionMeters).Length()<0.000001&&
            Director->WorldUtc()==Utc&&bTourSaveSlot==Slot;
        SetFlightPaused(false);UnifiedGuideChecked=true;
    }
    KeyboardThrottle=Time<5||UnifiedNightHoldStarted>=0?0.0f:0.4f;
    const auto& Earth=Director->Catalog().Find(TEXT("earth"))->Definition;
    const auto& Sun=Director->Catalog().Find(TEXT("sun"))->Definition;
    if(Time>=5)
    {
        if(Ship->IsCockpitView()){Ship->ToggleView();Ship->RecenterLook();}
        Ship->LookAtAbsolute(Sun.centerMeters,Dt);
    }
    const auto& State=Ship->Simulation()->State();
    const auto Camera=Ship->CameraAbsoluteMeters();
    const auto Local=Earth.bodyFixedToSimulation.Conjugate().Rotate(State.positionMeters-Earth.centerMeters);
    const auto Solar=star::ObserveSun(Camera,Sun.centerMeters,Sun.radiusMeters);
    const double Forward=star::Vec3d::Dot(Solar.direction,Ship->CameraForwardSimulation());
    const double TanHalf=FMath::Tan(FMath::DegreesToRadians(double(Ship->CameraHorizontalFOVDegrees())*0.5));
    const double Clearance=star::BodyHorizonClearance(Earth,Sun.centerMeters,Camera)*180/star::Pi;
    const double Visible=star::SolarDiskVisibleFraction(Camera,Sun.centerMeters,Sun.radiusMeters,Earth.centerMeters,Earth.radiusMeters);
    auto Json=MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("elapsed"),Time);Json->SetNumberField(TEXT("utc"),Director->WorldUtc());
    Json->SetNumberField(TEXT("rate"),Director->ClockRate());Json->SetNumberField(TEXT("simulationSeconds"),State.simulationTimeSeconds);
    Json->SetNumberField(TEXT("world"),GetWorld()->GetUniqueID());Json->SetNumberField(TEXT("director"),Director->GetUniqueID());
    Json->SetNumberField(TEXT("ship"),Ship->GetUniqueID());Json->SetStringField(TEXT("map"),GetWorld()->GetMapName());
    Json->SetBoolField(TEXT("normalStart"),UnifiedStartedNormally);Json->SetBoolField(TEXT("guidePreservedVoyage"),UnifiedGuidePreserved);
    Json->SetBoolField(TEXT("photoMode"),bPhotoMode);Json->SetBoolField(TEXT("shipHidden"),Ship->IsHidden());
    Json->SetNumberField(TEXT("recoveries"),static_cast<double>(State.recoveryCount));
    Json->SetNumberField(TEXT("localX"),Local.x);Json->SetNumberField(TEXT("localY"),Local.y);Json->SetNumberField(TEXT("localZ"),Local.z);
    Json->SetNumberField(TEXT("altitude"),Local.Length()-Earth.radiusMeters);
    Json->SetNumberField(TEXT("sunClearance"),Clearance);Json->SetNumberField(TEXT("sunVisible"),Visible);
    Json->SetNumberField(TEXT("solarLux"),Solar.illuminanceLux);Json->SetNumberField(TEXT("diskLuminance"),Solar.diskLuminance);
    Json->SetNumberField(TEXT("cameraDistance"),(Camera-State.positionMeters).Length());
    const auto Environment=Director->EnvironmentLightIntegral();
    Json->SetNumberField(TEXT("environmentR"),Environment.R);Json->SetNumberField(TEXT("environmentG"),Environment.G);Json->SetNumberField(TEXT("environmentB"),Environment.B);
    Json->SetNumberField(TEXT("minimumExposureEV"),Director->MinimumExposureEV());
    Json->SetNumberField(TEXT("sunAimErrorDegrees"),FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Forward,-1.0,1.0))));
    Json->SetNumberField(TEXT("sunU"),0.5+0.5*star::Vec3d::Dot(Solar.direction,Ship->CameraRightSimulation())/(FMath::Max(Forward,1e-6)*TanHalf));
    Json->SetNumberField(TEXT("sunV"),0.5-0.5*star::Vec3d::Dot(Solar.direction,Ship->CameraUpSimulation())/(FMath::Max(Forward,1e-6)*TanHalf/(16.0/9.0)));
    const int32 Bucket=FMath::FloorToInt(Time*4);
    if(Bucket!=UnifiedLastBucket)
    {
        UnifiedLastBucket=Bucket;FString Text;
        FJsonSerializer::Serialize(Json,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        FFileHelper::SaveStringToFile(Text+TEXT("\n"),*(BenchmarkPath/TEXT("world-continuity.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    }
    const bool Descending=Clearance<UnifiedPreviousClearance;
    auto Shot=[&](uint8 Bit,const TCHAR* Name)
    {
        if((UnifiedShots&Bit)||FScreenshotRequest::IsScreenshotRequested())return;
        UnifiedShots|=Bit;FScreenshotRequest::RequestScreenshot(BenchmarkPath/Name,true,false);
        FString Text;Json->SetStringField(TEXT("screenshot"),Name);
        FJsonSerializer::Serialize(Json,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        FFileHelper::SaveStringToFile(Text,*(BenchmarkPath/(FString(Name)+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    if(Time>=3&&Time<5)Shot(1,TEXT("normal-start.png"));
    if(Time>8&&Descending&&Clearance>0.8&&Clearance<2)Shot(2,TEXT("sun-above.png"));
    if(Time>8&&Descending&&Visible>0.2&&Visible<0.8)Shot(4,TEXT("sun-on-horizon.png"));
    if(Time>8&&Descending&&Visible<0.001&&Clearance<-0.6)Shot(8,TEXT("sun-below.png"));
    if(Time>20&&Clearance>65&&Forward>0.998)Shot(32,TEXT("earthshine-shadow.png"));
    if(FParse::Param(FCommandLine::Get(),TEXT("StarNightLightingQA"))&&(UnifiedShots&8))
    {
        if(UnifiedNightHoldStarted<0){UnifiedNightHoldStarted=Time;Director->SetClockRate(0);}
        if(Time-UnifiedNightHoldStarted>=20)Shot(16,TEXT("night-adapted.png"));
        if(Time-UnifiedNightHoldStarted>=24&&FParse::Param(FCommandLine::Get(),TEXT("StarExposureReadback")))
        {
            ConsoleCommand(TEXT("ShowFlag.VisualizeHDR 1"),false);
            if(Time-UnifiedNightHoldStarted>=26)Shot(64,TEXT("exposure-debug.png"));
        }
    }
    UnifiedPreviousClearance=Clearance;
}
