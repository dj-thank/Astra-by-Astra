#pragma once
#include "CoreMinimal.h"
#include <atomic>

// Numeric data only. Runs on one worker; never touches UObjects or the render origin.
struct FStarRemoteRaster
{
    int32 Width=0,Height=0,NativeWidth=0,NativeHeight=0,Channels=0,Bits=0;
    bool Floating=false;
    TArray<uint8> Samples;
    FString SourceUrl,Error;
    // Failure changes Error only; dimensions, samples and source remain the last
    // successful raster. Unsupported sample formats/orientations fail closed.
    bool Load(const FString& Url,int32 MaximumDimension,const FString& CacheDirectory,const std::atomic<bool>& Cancel);
};
