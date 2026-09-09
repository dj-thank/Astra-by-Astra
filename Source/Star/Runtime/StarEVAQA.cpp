#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "EVA/StarEVAPawn.h"
#include "UI/StarHUDWidget.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"

void AStarPlayerController::RequestQAQuit()
{
    if(bQAQuitPending)return;
    bQAQuitPending=true;SetFlightPaused(true);
#if CSV_PROFILER
    if(FCsvProfiler::IsCapturing())FCsvProfiler::Get()->EndCapture();
#endif
}

// Explicit automated runtime exercise, never represented as hardware input.
// Caller supplies a copy of an actual landed save in a fresh QA directory.
void AStarPlayerController::TickEVAQA(double Dt)
{
    const auto Finish=[&](bool Passed,const FString& Reason)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("StarAudioCapture")))
            UAudioMixerBlueprintLibrary::StopRecordingOutput(GetWorld(),EAudioRecordingExportType::WavFile,TEXT("eva-runtime-audio"),EVAQADirectory);
        auto Json=MakeShared<FJsonObject>();
        Json->SetBoolField(TEXT("passed"),Passed);Json->SetStringField(TEXT("reason"),Reason);
        Json->SetStringField(TEXT("input"),TEXT("automated runtime API; physical input not tested"));
        Json->SetNumberField(TEXT("stage"),EVAQAStage);
        Json->SetNumberField(TEXT("elapsed"),EVAQAElapsed);
        FString Text;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text));
        if(!FFileHelper::SaveStringToFile(Text,*(EVAQADirectory/TEXT("eva-result.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            UE_LOG(LogTemp,Error,TEXT("Failed to persist EVA QA result"));
        EVAQAStage=99;RequestQAQuit();
    };
    if(EVAQAStage==99)return;
    double WalkSeconds=15;
    FParse::Value(FCommandLine::Get(),TEXT("StarEVAWalkSeconds="),WalkSeconds);
    WalkSeconds=FMath::Clamp(WalkSeconds,15.0,100.0);
    EVAQAElapsed+=Dt;
    if(EVAQAElapsed>2*WalkSeconds+50) {Finish(false,TEXT("Timed out"));return;}
    if(EVAQAStage==0)
    {
        if(IFileManager::Get().FileExists(*(EVAQADirectory/TEXT("eva-result.json"))))
        {EVAQAStage=99;ConsoleCommand(TEXT("quit"),false);return;}
        if(!LoadGame()) {Finish(false,TEXT("Actual landed save could not load"));return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("StarAudioCapture")))
            UAudioMixerBlueprintLibrary::StartRecordingOutput(GetWorld(),80);
        bMainMenu=false;if(HUD)HUD->SetMainMenuVisible(false);SetFlightPaused(false);
        EVAQAParkedFlight=UTF8_TO_TCHAR(star::SerializeFlightState(Ship->Simulation()->State()).c_str());
        ToggleEVA();if(!EVAPawn){Finish(false,TEXT("Could not exit landed ship"));return;}
        float Yaw=0;FParse::Value(FCommandLine::Get(),TEXT("StarEVAYaw="),Yaw);
        EVAPawn->AdvanceWalking(0.001,FVector2D::ZeroVector,Yaw,0,false);
        EVAQAStage=1;
    }
    const FString Flight=UTF8_TO_TCHAR(star::SerializeFlightState(Ship->Simulation()->State()).c_str());
    if(Flight!=EVAQAParkedFlight){Finish(false,TEXT("Parked ship changed during EVA"));return;}
    if(!bFlightPaused)
    {
        Ship->AdvanceWorldClock(Dt);
        EVAQAParkedFlight=UTF8_TO_TCHAR(star::SerializeFlightState(Ship->Simulation()->State()).c_str());
    }
    if(EVAPawn)
    {
        // Continuous ordinary 1.4m/s walker integration, without teleportation.
        EVAQAMove=FVector2D::ZeroVector;
        if(EVAQAElapsed>=5&&EVAQAElapsed<5+WalkSeconds) EVAQAMove=FVector2D(1,0);
        if(EVAQAElapsed>=10+WalkSeconds&&EVAQAElapsed<10+2*WalkSeconds) EVAQAMove=FVector2D(-1,0);
        TickEVA(Dt);
        if(Snapshot.SpeedMps>1.6f){Finish(false,TEXT("Celestial-frame motion leaked into walking speed"));return;}
    }
    if(EVAQAStage==1&&EVAQAElapsed>=4)
    {FScreenshotRequest::RequestScreenshot(EVAQADirectory/TEXT("eva-exit.png"),true,false);EVAQAStage=2;}
    if(EVAQAStage==2&&EVAQAElapsed>=5+WalkSeconds)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("StarTextureReadback")))ConsoleCommand(TEXT("ListTextures -CSV"),false);
        if(!EVAPawn||EVAPawn->DistanceToBoardingPointMeters()<WalkSeconds){Finish(false,TEXT("Walker did not travel required distance"));return;}
        EVAQASavedFeet=EVAPawn->PositionAbsoluteMeters();
        if(!SaveGame()){Finish(false,TEXT("EVA save failed"));return;}
        FScreenshotRequest::RequestScreenshot(EVAQADirectory/TEXT("eva-walk.png"),true,false);EVAQAStage=3;
    }
    if(EVAQAStage==3&&EVAQAElapsed>=11+2*WalkSeconds)
    {
        ToggleEVA();if(EVAPawn){Finish(false,TEXT("Return walk could not board"));return;}
        if(!LoadGame()||!EVAPawn||(EVAPawn->PositionAbsoluteMeters()-EVAQASavedFeet).Length()>0.05)
        {Finish(false,TEXT("Saved EVA feet did not restore"));return;}
        EVAQAParkedFlight=UTF8_TO_TCHAR(star::SerializeFlightState(Ship->Simulation()->State()).c_str());
        FScreenshotRequest::RequestScreenshot(EVAQADirectory/TEXT("eva-restored.png"),true,false);EVAQAStage=4;
    }
    if(EVAQAStage==4&&EVAQAElapsed>=14+2*WalkSeconds)
    {
        if(!EVAPawn||EVAPawn->CanBoardShip()){Finish(false,TEXT("Restore lost distance from ship"));return;}
        Finish(true,TEXT("Exit, >15m walk, save, return, board, restore feet; parked ship unchanged"));
    }
}
