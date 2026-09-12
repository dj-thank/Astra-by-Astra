#include "Runtime/StarPlayerController.h"
#include "Runtime/StarShipPawn.h"
#include "Runtime/StarWorldDirector.h"
#include "Simulation/SolarLighting.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

// Opt-in movie driver: only the camera is directed here. The ship still passes
// through the ordinary simulation, collision and navigation permission gates.
void AStarPlayerController::UpdateVoyageMovie(double Dt)
{
    if(!bNavigationQAStarted||!bNavigationQAPathReady||!Ship)return;
    if(bNavigationQAFinished){
        if(!bVoyageAudioStopped){
            UAudioMixerBlueprintLibrary::StopRecordingOutput(GetWorld(),EAudioRecordingExportType::WavFile,TEXT("voyage-audio"),NavigationQADirectory);
            bVoyageAudioStopped=true;
        }
        return;
    }
    const auto& Sim=*Ship->Simulation();const auto& S=Sim.State();
    const auto* Earth=Sim.FindBody("earth");const auto* Sun=Sim.FindBody("sun");if(!Earth||!Sun)return;
    const double Time=FPlatformTime::Seconds()-VoyageRecordingStart;
    if(SolarLastCapture==-1)Ship->ClearGuidedCamera();
    const bool Sunset=NavigationQAStage==-3||NavigationQAStage==-2;
    if(NavigationQAStage==2&&FPlatformTime::Seconds()-NavigationQAStageWall>35&&!bVoyageAudioStopped){
        UAudioMixerBlueprintLibrary::StopRecordingOutput(GetWorld(),EAudioRecordingExportType::WavFile,TEXT("voyage-audio"),NavigationQADirectory);
        bVoyageAudioStopped=true;
    }
    const auto EarthUp=(S.positionMeters-Earth->centerMeters).Normalized();
    const auto Forward=star::Forward(S.orientation);const auto Right=star::Right(S.orientation);
    if(NavigationQAStage==-1){
        Ship->SetGuidedCameraOffset(Forward*-230+EarthUp*100+Right*(60*std::sin(Time*.07)),Dt);
        Ship->LookAtAbsolute(S.positionMeters+Forward*300-EarthUp*120,Dt*2);
    }else{
        const auto Radial=(S.positionMeters-Sun->centerMeters).Normalized();
        const auto Up=Sun->bodyFixedToSimulation.Rotate({0,0,1});
        const auto Side=star::Vec3d::Cross(Up,Radial).Normalized();
        Ship->SetGuidedCameraOffset(Radial*((NavigationQAStage==2||Sunset)?300:220)+Side*(Sunset?25:55)+Up*((NavigationQAStage==2||Sunset)?15:40),Dt);
        Ship->LookAtAbsolute(Sun->centerMeters,Dt*2);
    }
    // RefreshTransform restores the gameplay FOV, so retain the movie's lens
    // envelope independently. This driver has one controller per capture process.
    static float MovieFOV=80;
    if(Time<.1)MovieFOV=80;
    MovieFOV=FMath::FInterpTo(MovieFOV,NavigationQAStage==2?35.0f:Sunset?24.0f:80.0f,static_cast<float>(Dt),.4f);
    if(auto* Camera=Ship->FindComponentByClass<UCameraComponent>())Camera->SetFieldOfView(MovieFOV);
    const FString Directory=NavigationQADirectory/TEXT("movie");
    if(!SolarScreenshotHandle.IsValid()){
        IFileManager::Get().MakeDirectory(*Directory,true);
        SolarScreenshotHandle=UGameViewportClient::OnScreenshotCaptured().AddLambda([](int32 W,int32 H,const TArray<FColor>& Pixels){
            const FString Filename=FScreenshotRequest::GetFilename();const bool Movie=Filename.Contains(TEXT("/movie/"));
            auto& Module=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            auto Wrapper=Module.CreateImageWrapper(Movie?EImageFormat::JPEG:EImageFormat::PNG);
            TArray<FColor> Small;
            if(Movie){FImageUtils::ImageResize(W,H,Pixels,1920,1080,Small,true);W=1920;H=1080;}
            const auto& Image=Movie?Small:Pixels;
            if(Wrapper->SetRaw(Image.GetData(),Image.Num()*sizeof(FColor),W,H,ERGBFormat::BGRA,8))
                FFileHelper::SaveArrayToFile(Wrapper->GetCompressed(Movie?90:100),*FPaths::ChangeExtension(Filename,Movie?TEXT("jpg"):TEXT("png")));
        });
    }
    const int32 Bucket=FMath::FloorToInt(Time*15);
    if(Bucket==SolarLastCapture||FScreenshotRequest::IsScreenshotRequested())return;
    SolarLastCapture=Bucket;
    const FString Name=FString::Printf(TEXT("frame-%06d.jpg"),Bucket);
    const FString Row=FString::Printf(TEXT("{\"file\":\"%s\",\"t\":%.6f,\"utc\":%.6f,\"phase\":%d,\"earthAltitude\":%.3f,\"horizonDegrees\":%.6f,\"sunRadius\":%.6f,\"speed\":%.3f,\"recoveries\":%llu}\n"),
        *Name,Time,Ship->Director()->WorldUtc(),NavigationQAStage,(S.positionMeters-Earth->centerMeters).Length()-Earth->radiusMeters,
        FMath::RadiansToDegrees(star::BodyHorizonClearance(*Earth,Sun->centerMeters,S.positionMeters)),
        (S.positionMeters-Sun->centerMeters).Length()/Sun->radiusMeters,S.velocityMetersPerSecond.Length(),static_cast<unsigned long long>(S.recoveryCount));
    FFileHelper::SaveStringToFile(Row,*(Directory/TEXT("frames.jsonl")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
    FScreenshotRequest::RequestScreenshot(Directory/Name,false,false);
}
