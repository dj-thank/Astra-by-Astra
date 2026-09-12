#include "Runtime/StarDiagnostics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMemory.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <atomic>

namespace {
struct FRecorder {
    FCriticalSection Mutex;
    TUniquePtr<FArchive> Writer;
    TQueue<FString,EQueueMode::Mpsc> Pending;
    TFuture<void> Worker;
    FEvent* Wake=nullptr;
    std::atomic<int32> PendingBytes{0};
    std::atomic<bool> Closing{false};
    int32 Dropped=0,SlowWriteMs=0;
    bool Enabled=false;
    FString Folder;
    uint64 Sequence=0;
    int Part=0;
    bool Initialized=false,Closed=false;
};
FRecorder& Recorder(){static FRecorder Value;return Value;}
uint64 FrameNumber=0;
double FrameStart=0,FrameDelta=0,NextSample=0,PreviousFrameStart=0,WallFrameDelta=0;
bool SampleFrame=false;
const TCHAR* CurrentPhase=TEXT("startup");
void Drain(FRecorder& R)
{
    // The writer and every disk flush belong exclusively to this thread.
    while(!R.Closing.load()||!R.Pending.IsEmpty()){
        R.Wake->Wait(1000);
        if(R.SlowWriteMs>0)FPlatformProcess::Sleep(R.SlowWriteMs/1000.0f);
        FString Text;
        while(R.Pending.Dequeue(Text)){
            R.PendingBytes.fetch_sub(Text.Len()*sizeof(TCHAR));
            if(!R.Writer)continue;
            FTCHARToUTF8 Bytes(*Text);
            if(R.Writer->Tell()+Bytes.Length()>8*1024*1024){
                R.Writer->Flush();R.Writer.Reset();R.Part=(R.Part+1)%2;
                R.Writer.Reset(IFileManager::Get().CreateFileWriter(*(R.Folder/FString::Printf(TEXT("flight-%d.jsonl"),R.Part)),FILEWRITE_AllowRead));
            }
            if(R.Writer)R.Writer->Serialize(const_cast<ANSICHAR*>(Bytes.Get()),Bytes.Length());
        }
        if(R.Writer)R.Writer->Flush();
    }
    R.Writer.Reset();
}
void WriteLocked(FRecorder& R,const TCHAR* Type,const TSharedRef<FJsonObject>& Json,bool Flush)
{
    if(!R.Enabled||R.Closed)return;
    // Bound backlog during a slow/full disk. Gaps remain explicit in the next record.
    if(R.PendingBytes.load()>1024*1024){++R.Dropped;return;}
    if(R.Dropped){Json->SetNumberField(TEXT("droppedBefore"),R.Dropped);R.Dropped=0;}
    Json->SetStringField(TEXT("type"),Type);
    Json->SetNumberField(TEXT("sequence"),static_cast<double>(++R.Sequence));
    Json->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());
    Json->SetNumberField(TEXT("monotonicSeconds"),FPlatformTime::Seconds());
    Json->SetNumberField(TEXT("frame"),IsInGameThread()?static_cast<double>(FrameNumber):-1);
    Json->SetStringField(TEXT("thread"),IsInGameThread()?TEXT("game"):TEXT("worker"));
    FString Text;FJsonSerializer::Serialize(Json,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    Text+=TEXT("\n");R.PendingBytes.fetch_add(Text.Len()*sizeof(TCHAR));R.Pending.Enqueue(MoveTemp(Text));
    if(Flush)R.Wake->Trigger();
}
void Write(const TCHAR* Type,const TSharedRef<FJsonObject>& Json,bool Flush)
{
    auto& R=Recorder();FScopeLock Lock(&R.Mutex);WriteLocked(R,Type,Json,Flush);
}
void Vector(const TSharedRef<FJsonObject>& Json,const TCHAR* Name,const star::Vec3d& V)
{Json->SetArrayField(Name,{MakeShared<FJsonValueNumber>(V.x),MakeShared<FJsonValueNumber>(V.y),MakeShared<FJsonValueNumber>(V.z)});}
}
void StarDiagnostics::Initialize()
{
    auto& R=Recorder();FScopeLock Lock(&R.Mutex);if(R.Initialized)return;R.Initialized=true;
    R.Folder=FPaths::ProjectSavedDir()/TEXT("Diagnostics")/(FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+
        FString::Printf(TEXT("-%u-"),FPlatformProcess::GetCurrentProcessId())+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    IFileManager::Get().MakeDirectory(*R.Folder,true);
    R.Writer.Reset(IFileManager::Get().CreateFileWriter(*(R.Folder/TEXT("flight-0.jsonl")),FILEWRITE_AllowRead));
    R.Enabled=R.Writer.IsValid();
    if(R.Enabled){
        R.Wake=FPlatformProcess::GetSynchEventFromPool(false);
        FParse::Value(FCommandLine::Get(),TEXT("StarDiagnosticsSlowWriteMs="),R.SlowWriteMs);
        R.SlowWriteMs=FMath::Clamp(R.SlowWriteMs,0,1000);
        R.Worker=Async(EAsyncExecution::Thread,[&R]{Drain(R);});
    }
    auto Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("schema"),1);
    FString Version;if(GConfig)GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"),TEXT("ProjectVersion"),Version,GGameIni);
    Json->SetStringField(TEXT("version"),Version);Json->SetNumberField(TEXT("maximumSessionBytes"),16*1024*1024);
    Json->SetStringField(TEXT("scope"),TEXT("local flight diagnostics; no automatic upload"));
    FString Metadata;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Metadata));
    FFileHelper::SaveStringToFile(Metadata,*(R.Folder/TEXT("session.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    WriteLocked(R,TEXT("session_start"),Json,true);
    FCoreDelegates::OnPreExit.AddStatic(&StarDiagnostics::Shutdown);
    if(R.Enabled){UE_LOG(LogTemp,Display,TEXT("STAR flight recorder: %s (async, 1 MiB backlog)"),*R.Folder);}
    else {UE_LOG(LogTemp,Error,TEXT("STAR flight recorder cannot write to %s"),*R.Folder);}
}
void StarDiagnostics::Shutdown()
{
    auto& R=Recorder();
    {FScopeLock Lock(&R.Mutex);if(R.Closed)return;
        WriteLocked(R,TEXT("session_end"),MakeShared<FJsonObject>(),true);R.Closed=true;R.Closing.store(true);
        if(R.Wake)R.Wake->Trigger();}
    if(R.Worker.IsValid())R.Worker.Wait();
    if(R.Wake){FPlatformProcess::ReturnSynchEventToPool(R.Wake);R.Wake=nullptr;}
}
FString StarDiagnostics::Directory(){return FPaths::ProjectSavedDir()/TEXT("Diagnostics");}
void StarDiagnostics::Event(const TCHAR* Type,const FString& Detail,bool Flush,double Milliseconds)
{
    auto Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("detail"),Detail.Left(2048));
    if(Milliseconds>=0)Json->SetNumberField(TEXT("milliseconds"),Milliseconds);
    Write(Type,Json,Flush);
}
StarDiagnostics::FScope::FScope(const TCHAR* InName):Name(InName),Started(FPlatformTime::Seconds())
{Event(TEXT("operation_begin"),Name);}
StarDiagnostics::FScope::~FScope(){Event(TEXT("operation_end"),Name,true,(FPlatformTime::Seconds()-Started)*1000);}
void StarDiagnostics::BeginFrame(double DeltaSeconds)
{
    ++FrameNumber;FrameStart=FPlatformTime::Seconds();FrameDelta=DeltaSeconds;
    WallFrameDelta=PreviousFrameStart>0?FrameStart-PreviousFrameStart:DeltaSeconds;PreviousFrameStart=FrameStart;
    SampleFrame=FrameStart>=NextSample;if(SampleFrame)NextSample=FrameStart+0.5;
    Phase(TEXT("input"));
    if(WallFrameDelta>0.25)Event(TEXT("frame_gap"),TEXT("Actual time between game ticks exceeded 250 ms"),true,WallFrameDelta*1000);
}
void StarDiagnostics::Phase(const TCHAR* Name)
{CurrentPhase=Name;if(SampleFrame)Event(TEXT("phase_begin"),Name);}
void StarDiagnostics::EndFrame()
{
    const double Ms=(FPlatformTime::Seconds()-FrameStart)*1000;
    if(SampleFrame||Ms>100)Event(Ms>100?TEXT("game_thread_hitch"):TEXT("frame_end"),CurrentPhase,true,Ms);
}
void StarDiagnostics::Flight(const star::FlightSimulation& Sim,const star::FlightInput& Input,
    const star::Vec3d& Origin,const star::Vec3d& Camera,double Utc,const FString& Terrain,const TCHAR* PhaseName)
{
    if(!SampleFrame)return;
    const auto& S=Sim.State();auto Json=MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("phase"),PhaseName);Json->SetNumberField(TEXT("frameDelta"),FrameDelta);
    Json->SetNumberField(TEXT("wallFrameDelta"),WallFrameDelta);
    Json->SetNumberField(TEXT("worldUtc"),Utc);Json->SetNumberField(TEXT("simulationSeconds"),S.simulationTimeSeconds);
    Json->SetNumberField(TEXT("mode"),static_cast<int>(S.mode));Json->SetNumberField(TEXT("speedMps"),S.velocityMetersPerSecond.Length());
    Json->SetNumberField(TEXT("throttle"),S.throttle);Json->SetNumberField(TEXT("inputThrottle"),Input.throttle);
    Json->SetNumberField(TEXT("yaw"),Input.yaw);Json->SetNumberField(TEXT("pitch"),Input.pitch);Json->SetNumberField(TEXT("roll"),Input.roll);
    Json->SetBoolField(TEXT("brake"),Input.brake);Json->SetBoolField(TEXT("paused"),Input.paused);
    Json->SetBoolField(TEXT("neutralRequired"),S.throttleNeutralRequired);Json->SetNumberField(TEXT("recoveries"),S.recoveryCount);
    Json->SetStringField(TEXT("target"),UTF8_TO_TCHAR(S.targetBodyId.c_str()));Json->SetStringField(TEXT("terrain"),Terrain);
    Vector(Json,TEXT("positionMeters"),S.positionMeters);Vector(Json,TEXT("velocityMps"),S.velocityMetersPerSecond);
    Vector(Json,TEXT("renderLocalCm"),star::ToUnrealCentimeters(S.positionMeters,Origin));
    Vector(Json,TEXT("cameraLocalCm"),star::ToUnrealCentimeters(Camera,Origin));
    double Nearest=1e30;const star::BodyDefinition* Body=nullptr;
    for(const auto& B:Sim.Bodies()){const double D=(S.positionMeters-B.centerMeters).Length()-B.radiusMeters;if(D<Nearest){Nearest=D;Body=&B;}}
    if(Body){Json->SetStringField(TEXT("nearBody"),UTF8_TO_TCHAR(Body->id.c_str()));Json->SetNumberField(TEXT("altitudeMeters"),Nearest);
        Vector(Json,TEXT("bodyFixedMeters"),Body->bodyFixedToSimulation.Conjugate().Rotate(S.positionMeters-Body->centerMeters));}
    Json->SetNumberField(TEXT("memoryUsedMiB"),static_cast<double>(FPlatformMemory::GetStats().UsedPhysical)/(1024*1024));
    Write(TEXT("flight"),Json,true);
}
