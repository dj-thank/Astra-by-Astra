#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StarViewTypes.h"
#include "Exploration/StarExplorationCore.h"
#include "StarExplorationSubsystem.generated.h"

// Populate from simulation and actual active camera. Never infer scan validity from a button alone.
USTRUCT()
struct FStarObservationSample
{
    GENERATED_BODY()
    FString BodyId;
    uint64 Sequence = 0;
    double DeltaSeconds = 0;
    double AltitudeM = 0;
    double LatitudeDeg = 0;
    double LongitudeDeg = 0;
    double SpeedMps = 0;
    double VerticalSpeedMps = 0;
    double SunIllumination = 0;
    double RingRadiusM = 0;
    bool bLanded = false;
    bool bScanning = false;
    bool bViewingBody = false;
    bool bCockpitView = true;
    bool bViewingRings = false;
    bool bObservedPointInPlanetShadow = false;
};

USTRUCT()
struct FStarJournalEntry
{
    GENERATED_BODY()
    FString ObjectiveId;
    FString Title;
    FString RuntimeSummary;
    FString ReferenceText;
    FString SourceUrl;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FStarDiscoveryRecorded, const FString&);

UCLASS()
class STAR_API UStarExplorationSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    void SubmitObservation(const FStarObservationSample& Sample);
    void NotifyAction(FName Action);
    TArray<FStarObjectiveView> GetObjectives() const;
    FStarObjectiveView GetTutorialObjective() const;
    TArray<FStarJournalEntry> GetJournalEntries() const;
    float GetActiveScanProgress() const;
    bool CanScan(const FStarObservationSample& Sample) const;
    FString ExportProgressJson() const;
    bool RestoreProgressJson(const FString& Json, FString& OutError);
    FStarDiscoveryRecorded OnDiscoveryRecorded;
private:
    star::exploration::Tracker Tracker;
};
