#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "StarShipAudioComponent.generated.h"

struct FStarShipAudioControls;
struct FStarAudioRuntime;
struct FStarAudioRuntimeDeleter
{
    void operator()(FStarAudioRuntime* Value) const;
};
class UAudioComponent;
class USoundWave;

/** Offline SoundWave ship mix, with procedural fallback only for missing layers/events. */
UCLASS(ClassGroup = (STAR), meta = (BlueprintSpawnableComponent))
class STAR_API UStarShipAudioComponent : public USynthComponent
{
    GENERATED_BODY()
public:
    UStarShipAudioComponent(const FObjectInitializer& ObjectInitializer);
    virtual ~UStarShipAudioComponent() override;

    /** Use these lifecycle methods for the complete asset + fallback mix. */
    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void StartShipAudio();

    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void StopShipAudio();

    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void SetEnvironment(FName BodyId, bool bCruise, bool bLanded = false);

    virtual void Activate(bool bReset = false) override;
    virtual void Deactivate() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Throttle is normalized, already-spooled Propulsion().engineOutput, not speed or raw key state. */
    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void SetFlightParameters(float Throttle, double SpeedMps, bool bCockpit, bool bPaused);

    /** Interior monitor keeps pilot hearing independent of the cinematic camera; default enabled. */
    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void SetInteriorMonitorEnabled(bool bEnabled);

    /** EVA uses suit-internal/conducted sound, never exterior vacuum propagation. */
    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void SetEVAMode(bool bEnabled);

    /** Feed normalized cruise charge and braking effort from the flight state, never from speed alone. */
    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void SetEngineDynamics(float CruiseCharge, float BrakingEffort);

    /** Smoothed actual engine output, suitable for matching the exhaust VFX. */
    UFUNCTION(BlueprintPure, Category = "STAR|Audio")
    float GetEngineSpool() const;

    /** Existing names plus engine spool and cruise transition events. */
    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void PlayEvent(FName EventName);

    UFUNCTION(BlueprintCallable, Category = "STAR|Audio")
    void SetMasterVolume(float Volume);

protected:
    virtual bool Init(int32& SampleRate) override;
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
    // Generator keeps this alive without retaining or accessing this UObject on the render thread.
    TSharedPtr<FStarShipAudioControls, ESPMode::ThreadSafe> Controls;
    TUniquePtr<FStarAudioRuntime,FStarAudioRuntimeDeleter> Runtime;
    UPROPERTY(Transient) TMap<FName, TObjectPtr<USoundWave>> WaveAssets;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> LayerPlayers;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> MusicPlayers;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> EventPlayers;

    UAudioComponent* CreatePlayer();
    void RequestCue(int32 CueIndex);
    void CompleteCueLoad(int32 CueIndex);
    USoundWave* GetCueWave(int32 CueIndex) const;
    void PausePlayers();
    void UpdateMix(float DeltaTime);
};
