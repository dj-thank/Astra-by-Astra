#pragma once
#include "CoreMinimal.h"
#include "Simulation/FlightSimulation.h"
#include "Simulation/Astronomy.h"

struct FStarBodyRecord
{
    star::BodyDefinition Definition;
    FString Name;
    FString BodyFixedFrame;
    FString Epoch;
};

// Scientific samples are always meters above the documented lunar datum.
// Rendering-only grain never changes these measurements.
class FStarDataCatalog
{
public:
    bool Load(FString& OutError);
    const TArray<FStarBodyRecord>& Bodies() const { return BodyRecords; }
    const FStarBodyRecord* Find(const FString& Id) const;
    const FStarBodyRecord* Find(const std::string& Id) const;
    std::vector<star::BodyDefinition> SimulationBodies() const;
    const std::vector<star::BodyDefinition>& ReferenceBodies() const { return ReferenceDefinitions; }
    bool SetAstronomicalUtc(double UnixSeconds);
    bool SimulationBodiesAt(double UnixSeconds,std::vector<star::BodyDefinition>& Out) const;
    bool SupportsUtc(double UnixSeconds) const { return Ephemeris.Contains(UnixSeconds); }
    double FirstUtc() const { return Ephemeris.Start(); }
    double LastUtc() const { return Ephemeris.End(); }
    bool SampleTerrain(const star::BodyDefinition& Body, const star::Vec3d& Direction, star::TerrainSample& Out) const;
    double MoonHeight(double LatitudeDeg, double LongitudeDeg, bool* OutRegional = nullptr) const;
    bool IsReady() const { return bReady; }
    bool HasRegionalTerrain() const { return RegionalHeights.Num() > 0 || FarHeights.Num() > 0; }
    FString DataEpoch() const { return Epoch; }
    double TerrainMinimum() const { return MinimumHeight; }
    double TerrainMaximum() const { return MaximumHeight; }
    double RingInnerRadiusMeters() const { return RingInner; }
    double RingOuterRadiusMeters() const { return RingOuter; }
    static FVector UEVector(const star::Vec3d& Value);
    static star::Vec3d SimVector(const FVector& Value);
    static FQuat UERotation(const star::Quatd& Orientation);
private:
    double GlobeHeight(double LatitudeDeg, double LongitudeDeg) const;
    bool RegionalHeight(double LatitudeDeg, double LongitudeDeg, double& Height, double& Blend) const;
    bool FarRegionalHeight(double LatitudeDeg, double LongitudeDeg, double& Height, double& Blend) const;
    TArray<FStarBodyRecord> BodyRecords;
    std::vector<star::BodyDefinition> ReferenceDefinitions;
    star::EphemerisTable Ephemeris;
    TArray<int16> GlobalHeights;
    TArray<float> RegionalHeights;
    TArray<uint8> RegionalValid;
    TArray<int16> FarHeights;
    TArray<uint8> FarConfidence;
    int32 GlobalWidth = 5760;
    int32 GlobalHeight = 2880;
    int32 RegionalWidth = 0;
    int32 RegionalHeightCount = 0;
    double RegionWest = 0, RegionEast = 0, RegionNorth = 0, RegionSouth = 0;
    int32 FarWidth = 0, FarHeightCount = 0;
    double FarWest = 0, FarEast = 0, FarNorth = 0, FarSouth = 0;
    double FarHeightScale = 0.25, FarBlendWidth = 500, FarBlendInset = 20;
    double MinimumHeight = -12000, MaximumHeight = 12000;
    double RingInner = 74268296.0, RingOuter = 140478768.0;
    FString Epoch;
    bool bReady = false;
};
