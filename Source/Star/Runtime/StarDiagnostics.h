#pragma once
#include "CoreMinimal.h"
#include "Simulation/FlightSimulation.h"

// Local-only, bounded flight recorder. No account, device serial or command line.
namespace StarDiagnostics {
void Initialize();
void Shutdown();
FString Directory();
void Event(const TCHAR* Type,const FString& Detail=FString(),bool Flush=true,double Milliseconds=-1);
void BeginFrame(double DeltaSeconds);
void Phase(const TCHAR* Name);
void EndFrame();
void Flight(const star::FlightSimulation& Simulation,const star::FlightInput& Input,
    const star::Vec3d& Origin,const star::Vec3d& Camera,double Utc,const FString& Terrain,const TCHAR* PhaseName);
struct FFrame {
    explicit FFrame(double Dt){BeginFrame(Dt);}
    ~FFrame(){EndFrame();}
};
struct FScope {
    FString Name;double Started;
    explicit FScope(const TCHAR* InName);
    ~FScope();
};
}
