#include "Presentation/StarShipAudioComponent.h"
#include "Presentation/StarAudioRouting.h"
#include "Components/AudioComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundGenerator.h"
#include "Sound/SoundWave.h"
#include "AudioMixerBlueprintLibrary.h"

#include <atomic>

namespace StarShipAudio
{
using namespace star::audio::routing;
struct FPlayerState
{
    Cue CueId = Cue::None;
    float LastGain = -1.0f;
    bool bWaitingForStop = false;
};

FName AssetId(Cue CueId) { return FName(UTF8_TO_TCHAR(Spec(CueId).id)); }
FSoftObjectPath AssetPath(Cue CueId)
{
    const FString Name = FString(TEXT("SW_")) + UTF8_TO_TCHAR(Spec(CueId).id);
    return FSoftObjectPath(FString(TEXT("/Game/Star/Audio/")) + Name + TEXT(".") + Name);
}

void FadePlayerToStop(UAudioComponent* Player, FPlayerState& State)
{
    if (Player && Player->IsPlaying() && !State.bWaitingForStop)
    {
        Player->FadeOut(0.04f, 0.0f);
        State.bWaitingForStop = true;
    }
    State.LastGain = -1.0f;
}

// Never replace a playing waveform, and never cancel a pause fade with a nonzero target.
void ApplyPlayer(UAudioComponent* Player, USoundWave* Wave, Cue CueId, double Gain, FPlayerState& State)
{
    if (!Player) return;
    if (State.bWaitingForStop)
    {
        if (Player->IsPlaying()) return;
        State.bWaitingForStop = false;
    }
    const float Target = static_cast<float>(FMath::Clamp(Gain, 0.0, 1.0));
    if (!Wave || Target <= 0.00001f)
    {
        FadePlayerToStop(Player, State);
        return;
    }
    if (State.CueId != CueId && Player->IsPlaying())
    {
        FadePlayerToStop(Player, State);
        return;
    }
    if (!Player->IsPlaying())
    {
        Player->SetSound(Wave);
        Player->SetVolumeMultiplier(1.0f);
        Player->FadeIn(0.035f, Target, 0.0f);
        State.CueId = CueId;
        State.LastGain = Target;
    }
    else if (!FMath::IsNearlyEqual(State.LastGain, Target, 0.0001f))
    {
        Player->AdjustVolume(0.03f, Target);
        State.LastGain = Target;
    }
}
}

struct FStarShipAudioControls
{
    std::array<std::atomic<float>, star::audio::routing::LayerCount> Layers;
    std::array<std::atomic<int32>, star::audio::routing::EventVoiceCount> Events;
    std::atomic<float> Throttle{0.0f};
    std::atomic<float> WhineGain{0.0f}, Charge{0.0f}, Cruise{0.0f}, Braking{0.0f};
    std::atomic<float> EventMaster{0.0f};
    std::atomic<bool> Paused{true};
    FStarShipAudioControls()
    {
        for (auto& Gain : Layers) Gain.store(0.0f, std::memory_order_relaxed);
        for (auto& Cue : Events) Cue.store(-1, std::memory_order_relaxed);
    }
};

struct FStarAudioRuntime
{
    star::audio::routing::Input Input;
    star::audio::routing::State Router;
    std::array<bool, star::audio::routing::CueCount> Attempted{};
    std::array<bool, star::audio::routing::CueCount> Primed{};
    std::array<TSharedPtr<FStreamableHandle>, star::audio::routing::CueCount> Loads;
    std::array<StarShipAudio::FPlayerState, star::audio::routing::LayerCount> Layers;
    std::array<StarShipAudio::FPlayerState, star::audio::routing::MusicVoiceCount> Music;
    std::array<StarShipAudio::FPlayerState, star::audio::routing::EventVoiceCount> Events;
    std::array<bool, star::audio::routing::EventVoiceCount> EventFallback{};
    std::array<double, star::audio::routing::EventVoiceCount> EventRemaining{};
    bool bStarted = false;
    bool bThrustActive = false;
    bool bChargeActive = false;
    std::uint32_t FootstepSequence = 0;
    FStarAudioRuntime() { Input.paused = true; Router.SetInput(Input); }
};

namespace
{
static_assert(std::atomic<float>::is_always_lock_free && std::atomic<int32>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
class FStarShipSoundGenerator final : public ISoundGenerator
{
public:
    FStarShipSoundGenerator(const FSoundGeneratorInitParams& Init, TSharedPtr<FStarShipAudioControls, ESPMode::ThreadSafe> InControls)
        : Controls(MoveTemp(InControls)), DSP(Init.SampleRate), Channels(Init.NumChannels) {}
    virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
    {
        if (!OutAudio || NumSamples <= 0) return 0;
        if (Channels != 2)
        {
            FMemory::Memzero(OutAudio, sizeof(float) * NumSamples);
            return NumSamples;
        }
        std::array<double, star::audio::routing::LayerCount> Layers;
        const bool bPaused = Controls->Paused.load(std::memory_order_acquire);
        for (std::size_t I = 0; I < Layers.size(); ++I) Layers[I] = Controls->Layers[I].load(std::memory_order_relaxed);
        DSP.SetMix(Layers, Controls->Throttle.load(std::memory_order_relaxed), Controls->EventMaster.load(std::memory_order_relaxed), bPaused);
        DSP.SetEngine(Controls->WhineGain.load(std::memory_order_relaxed), Controls->Charge.load(std::memory_order_relaxed),
            Controls->Cruise.load(std::memory_order_relaxed), Controls->Braking.load(std::memory_order_relaxed));
        for (std::size_t I = 0; I < star::audio::routing::EventVoiceCount; ++I)
        {
            const int32 CueIndex = Controls->Events[I].exchange(-1, std::memory_order_acq_rel);
            if (CueIndex == -2) DSP.Cancel(I);
            if (CueIndex >= 0 && CueIndex < static_cast<int32>(star::audio::routing::CueCount))
                DSP.Trigger(I, static_cast<star::audio::routing::Cue>(CueIndex));
        }
        DSP.Render(OutAudio, static_cast<std::size_t>(NumSamples / 2));
        if ((NumSamples & 1) != 0) OutAudio[NumSamples - 1] = 0.0f;
        return NumSamples;
    }
    virtual int32 GetDesiredNumSamplesToRenderPerCallback() const override { return 1024; }
private:
    TSharedPtr<FStarShipAudioControls, ESPMode::ThreadSafe> Controls;
    star::audio::routing::FallbackDSP DSP;
    int32 Channels;
};
}

void FStarAudioRuntimeDeleter::operator()(FStarAudioRuntime* Value) const { delete Value; }

UStarShipAudioComponent::UStarShipAudioComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer), Controls(MakeShared<FStarShipAudioControls, ESPMode::ThreadSafe>()), Runtime(new FStarAudioRuntime())
{
    NumChannels = 2;
    bAutoActivate = false;
    bAutoDestroy = false;
    bAllowSpatialization = false;
    bIsUISound = true; // Mixer fade-outs must finish even when the game world is paused.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
}
UStarShipAudioComponent::~UStarShipAudioComponent() = default;

bool UStarShipAudioComponent::Init(int32& SampleRate)
{
    NumChannels = 2;
    return SampleRate >= 8000 && SampleRate <= 192000;
}
ISoundGeneratorPtr UStarShipAudioComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
    return MakeShared<FStarShipSoundGenerator, ESPMode::ThreadSafe>(InParams, Controls);
}

UAudioComponent* UStarShipAudioComponent::CreatePlayer()
{
    if (!GetOwner() || !GetWorld()) return nullptr;
    UAudioComponent* Player = NewObject<UAudioComponent>(GetOwner(), NAME_None, RF_Transient);
    Player->bAutoActivate = false;
    Player->bAutoDestroy = false;
    Player->bStopWhenOwnerDestroyed = true;
    Player->bAllowSpatialization = false;
    Player->bIsUISound = true;
    Player->bCanPlayMultipleInstances = false;
    Player->SetupAttachment(this);
    Player->RegisterComponent();
    return Player;
}

void UStarShipAudioComponent::StartShipAudio()
{
    using namespace star::audio::routing;
    if (!Runtime || Runtime->bStarted || !IsRegistered() || !GetWorld() || !GetOwner()) return;
    Runtime->bStarted = true;
    Runtime->Router.ResetTransientState();
    Runtime->Router.SetInput(Runtime->Input);
    if (LayerPlayers.IsEmpty()) for (std::size_t I = 0; I < LayerCount; ++I) LayerPlayers.Add(CreatePlayer());
    if (MusicPlayers.IsEmpty()) for (std::size_t I = 0; I < MusicVoiceCount; ++I) MusicPlayers.Add(CreatePlayer());
    if (EventPlayers.IsEmpty()) for (std::size_t I = 0; I < EventVoiceCount; ++I) EventPlayers.Add(CreatePlayer());
    // Only required loop/event assets are prefetched. Music is requested per environment.
    for (int32 I = 0; I < static_cast<int32>(CueCount); ++I)
        if (I < static_cast<int32>(LayerCount) || IsEvent(static_cast<Cue>(I))) RequestCue(I);
    RequestCue(static_cast<int32>(Runtime->Router.DesiredMusic()));
    Super::Start(); // Continuous turbine whine plus missing asset fallback; fixed voice count.
    SetComponentTickEnabled(true);
    UpdateMix(0.0f);
}

void UStarShipAudioComponent::StopShipAudio()
{
    if (!Runtime) return;
    Runtime->bStarted = false;
    Controls->Paused.store(true, std::memory_order_release);
    Controls->EventMaster.store(0.0f, std::memory_order_relaxed);
    for (auto& Gain : Controls->Layers) Gain.store(0.0f, std::memory_order_relaxed);
    for (auto& Event : Controls->Events) Event.store(-2, std::memory_order_release);
    for (const auto& Player : LayerPlayers) if (Player) Player->Stop();
    for (const auto& Player : MusicPlayers) if (Player) Player->Stop();
    for (const auto& Player : EventPlayers) if (Player) Player->Stop();
    for (std::size_t I = 0; I < Runtime->Loads.size(); ++I)
    {
        if (Runtime->Loads[I])
        {
            Runtime->Loads[I]->CancelHandle();
            Runtime->Loads[I].Reset();
            Runtime->Attempted[I] = false;
        }
    }
    Runtime->Router.ResetTransientState();
    Runtime->Layers = {}; Runtime->Music = {}; Runtime->Events = {};
    Runtime->EventFallback.fill(false); Runtime->EventRemaining.fill(0.0);
    SetComponentTickEnabled(false);
    Super::Stop();
}

void UStarShipAudioComponent::Activate(bool bReset)
{
    if (bReset) StopShipAudio();
    StartShipAudio();
    if (Runtime->bStarted) OnComponentActivated.Broadcast(this, bReset);
}
void UStarShipAudioComponent::Deactivate()
{
    StopShipAudio();
    OnComponentDeactivated.Broadcast(this);
}
void UStarShipAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopShipAudio();
    Super::EndPlay(Reason);
}
void UStarShipAudioComponent::OnUnregister()
{
    StopShipAudio();
    for (const auto& Player : LayerPlayers) if (Player) Player->DestroyComponent();
    for (const auto& Player : MusicPlayers) if (Player) Player->DestroyComponent();
    for (const auto& Player : EventPlayers) if (Player) Player->DestroyComponent();
    LayerPlayers.Reset(); MusicPlayers.Reset(); EventPlayers.Reset();
    Super::OnUnregister();
}

void UStarShipAudioComponent::RequestCue(int32 CueIndex)
{
    using namespace star::audio::routing;
    if (!Runtime->bStarted || CueIndex < 0 || CueIndex >= static_cast<int32>(CueCount) || Runtime->Attempted[CueIndex]) return;
    Runtime->Attempted[CueIndex] = true;
    const Cue CueId = static_cast<Cue>(CueIndex);
    if (GetCueWave(CueIndex)) { Runtime->Router.SetAvailable(CueId, true); return; }
    Runtime->Loads[CueIndex] = UAssetManager::GetStreamableManager().RequestAsyncLoad(
        StarShipAudio::AssetPath(CueId), FStreamableDelegate::CreateWeakLambda(this, [this, CueIndex] { CompleteCueLoad(CueIndex); }));
}
void UStarShipAudioComponent::CompleteCueLoad(int32 CueIndex)
{
    using namespace star::audio::routing;
    if (!Runtime || !Runtime->bStarted || CueIndex < 0 || CueIndex >= static_cast<int32>(CueCount)) return;
    const Cue CueId = static_cast<Cue>(CueIndex);
    USoundWave* Wave = Cast<USoundWave>(StarShipAudio::AssetPath(CueId).ResolveObject());
    // Never modify imported assets or accidentally start an infinite one-shot voice.
    if (Wave && static_cast<bool>(Wave->bLooping) == Spec(CueId).loop)
    {
        WaveAssets.Add(StarShipAudio::AssetId(CueId), Wave);
        // A loaded package does not imply that the first streamed audio chunk is ready.
        if(Wave->IsStreaming() && Wave->GetNumChunks()>1)
        {
            FOnSoundLoadComplete Ready;
            Ready.BindDynamic(this,&UStarShipAudioComponent::CompleteCuePrime);
            UAudioMixerBlueprintLibrary::PrimeSoundForPlayback(Wave,Ready);
        }
        else CompleteCuePrime(Wave,false);
    }
    else
    {
        Runtime->Router.SetAvailable(CueId, false);
        UE_LOG(LogTemp, Warning, TEXT("STAR audio cue unavailable or loop flag mismatch: %s. Missing SFX use scoped procedural fallback; missing music stays silent."), *StarShipAudio::AssetId(CueId).ToString());
    }
    Runtime->Loads[CueIndex].Reset();
}
void UStarShipAudioComponent::CompleteCuePrime(const USoundWave* Wave,bool bCancelled)
{
    using namespace star::audio::routing;
    if(!Runtime||!Wave)return;
    for(int32 I=0;I<static_cast<int32>(CueCount);++I)
    {
        const Cue Id=static_cast<Cue>(I);
        const auto* Found=WaveAssets.Find(StarShipAudio::AssetId(Id));
        if(Found && Found->Get()==Wave)
        {
            Runtime->Primed[I]=!bCancelled;
            Runtime->Router.SetAvailable(Id,!bCancelled);
            UE_LOG(LogTemp,Display,TEXT("STAR audio primed: %s ready=%d"),*StarShipAudio::AssetId(Id).ToString(),!bCancelled);
        }
    }
}
USoundWave* UStarShipAudioComponent::GetCueWave(int32 CueIndex) const
{
    if (CueIndex < 0 || CueIndex >= static_cast<int32>(star::audio::routing::CueCount)) return nullptr;
    if(!Runtime->Primed[CueIndex])return nullptr;
    const auto* Found = WaveAssets.Find(StarShipAudio::AssetId(static_cast<star::audio::routing::Cue>(CueIndex)));
    return Found ? Found->Get() : nullptr;
}

void UStarShipAudioComponent::SetEnvironment(FName BodyId, bool bCruise, bool bLanded)
{
    using AudioBody = star::audio::routing::Body;
    Runtime->Input.body = BodyId == TEXT("earth") ? AudioBody::Earth : BodyId == TEXT("moon") ? AudioBody::Moon
        : BodyId == TEXT("saturn") ? AudioBody::Saturn : BodyId == TEXT("space") ? AudioBody::Space : AudioBody::Unknown;
    const bool bCruiseChanged = Runtime->Input.cruise != bCruise;
    Runtime->Input.cruise = bCruise;
    Runtime->Input.landed = bLanded;
    Runtime->Router.SetInput(Runtime->Input);
    if (Runtime->bStarted) RequestCue(static_cast<int32>(Runtime->Router.DesiredMusic()));
    if (bCruiseChanged) PlayEvent(bCruise ? TEXT("CruiseEngage") : TEXT("CruiseDisengage"));
}
void UStarShipAudioComponent::SetFlightParameters(float Throttle, double SpeedMps, bool bCockpit, bool bPaused)
{
    const bool bWasSilent = Runtime->Input.paused || Runtime->Input.masterVolume <= 0.0;
    Runtime->Input.throttle = Throttle;
    Runtime->Input.engineOutputIsSpooled = true; // Runtime supplies Propulsion().engineOutput.
    Runtime->Input.speedMps = SpeedMps;
    Runtime->Input.cockpit = bCockpit;
    Runtime->Input.paused = bPaused;
    Runtime->Router.SetInput(Runtime->Input);
    if (!bWasSilent && bPaused) PausePlayers();
    const bool bThrust = FMath::IsFinite(Throttle) && Throttle > (Runtime->bThrustActive ? 0.06f : 0.18f);
    if (bThrust != Runtime->bThrustActive)
    {
        Runtime->bThrustActive = bThrust;
        PlayEvent(bThrust ? TEXT("SpoolUp") : TEXT("SpoolDown"));
    }
}
void UStarShipAudioComponent::SetInteriorMonitorEnabled(bool bEnabled)
{
    Runtime->Input.interiorMonitor = bEnabled;
    Runtime->Router.SetInput(Runtime->Input);
}
void UStarShipAudioComponent::SetEVAMode(bool bEnabled)
{
    if (Runtime->Input.eva == bEnabled) return;
    // Retire occupied one-shots, but keep the live listener envelope so a hatch
    // event sent in the same game frame is accepted. Loop beds crossfade normally.
    for (int32 I = 0; I < EventPlayers.Num(); ++I)
    {
        if (!star::audio::routing::IsEvent(Runtime->Events[I].CueId)) continue;
        StarShipAudio::FadePlayerToStop(EventPlayers[I], Runtime->Events[I]);
        Controls->Events[I].store(-2, std::memory_order_release);
        Runtime->Events[I].bWaitingForStop = true;
        Runtime->EventFallback[I] = true;
        Runtime->EventRemaining[I] = 0.05;
        Runtime->Router.ReleaseEvent(static_cast<std::size_t>(I));
    }
    Runtime->FootstepSequence = 0;
    Runtime->Input.eva = bEnabled;
    Runtime->Router.SetInput(Runtime->Input);
}
void UStarShipAudioComponent::SetEngineDynamics(float CruiseCharge, float BrakingEffort)
{
    const bool bCharging = FMath::IsFinite(CruiseCharge) && CruiseCharge > 0.02f;
    Runtime->Input.charge = CruiseCharge;
    Runtime->Input.braking = BrakingEffort;
    Runtime->Router.SetInput(Runtime->Input);
    if (bCharging && !Runtime->bChargeActive) PlayEvent(TEXT("CruiseCharge"));
    Runtime->bChargeActive = bCharging;
}
float UStarShipAudioComponent::GetEngineSpool() const
{
    return Runtime && !Runtime->Input.paused && Runtime->bStarted ? static_cast<float>(Runtime->Router.Output().throttle) : 0.0f;
}

void UStarShipAudioComponent::SetMasterVolume(float Volume)
{
    const bool bWasSilent = Runtime->Input.paused || Runtime->Input.masterVolume <= 0.0;
    Runtime->Input.masterVolume = FMath::IsFinite(Volume) ? FMath::Clamp(static_cast<double>(Volume), 0.0, 1.0) : 0.0;
    Runtime->Router.SetInput(Runtime->Input);
    if (!bWasSilent && Runtime->Input.masterVolume == 0.0) PausePlayers();
}

void UStarShipAudioComponent::PausePlayers()
{
    Controls->Paused.store(true, std::memory_order_release);
    Controls->EventMaster.store(0.0f, std::memory_order_relaxed);
    for (auto& Gain : Controls->Layers) Gain.store(0.0f, std::memory_order_relaxed);
    for (auto& Event : Controls->Events) Event.store(-2, std::memory_order_release);
    for (int32 I = 0; I < LayerPlayers.Num(); ++I) StarShipAudio::FadePlayerToStop(LayerPlayers[I], Runtime->Layers[I]);
    for (int32 I = 0; I < MusicPlayers.Num(); ++I) StarShipAudio::FadePlayerToStop(MusicPlayers[I], Runtime->Music[I]);
    for (int32 I = 0; I < EventPlayers.Num(); ++I)
    {
        StarShipAudio::FadePlayerToStop(EventPlayers[I], Runtime->Events[I]);
        Runtime->EventRemaining[I] = 0.0;
        Runtime->EventFallback[I] = false;
    }
}

void UStarShipAudioComponent::PlayEvent(FName EventName)
{
    using namespace star::audio::routing;
    if (!Runtime->bStarted || Runtime->Input.paused || Runtime->Input.masterVolume <= 0.0) return;
    const FString Name = EventName.ToString();
    const FTCHARToUTF8 Utf8Name(*Name);
    Cue CueId = EventCue(std::string_view(Utf8Name.Get(), static_cast<std::size_t>(Utf8Name.Length())));
    if (EventName == TEXT("Footstep")) CueId = static_cast<Cue>(static_cast<int32>(Cue::Footstep1) + (Runtime->FootstepSequence % 4));
    if (!IsEvent(CueId)) return;
    // Engine direction changes retire the preceding transient smoothly before reusing its slot.
    if (Spec(CueId).eventGroup == 6 || Spec(CueId).eventGroup == 7)
    {
        for (int32 I = 0; I < EventPlayers.Num(); ++I)
        {
            auto& Old = Runtime->Events[I];
            if (Old.CueId != CueId && Spec(Old.CueId).eventGroup == Spec(CueId).eventGroup)
            {
                StarShipAudio::FadePlayerToStop(EventPlayers[I], Old);
                Controls->Events[I].store(-2, std::memory_order_release);
                Old.bWaitingForStop = true;
                Runtime->EventFallback[I] = true; // Also reserve an asset-free slot for the render-thread fade.
                Runtime->EventRemaining[I] = 0.05;
                Runtime->Router.ReleaseEvent(static_cast<std::size_t>(I));
            }
        }
    }
    USoundWave* Wave = GetCueWave(static_cast<int32>(CueId));
    std::uint32_t UsableSlots = 0;
    for (int32 I = 0; I < EventPlayers.Num(); ++I)
        if (!EventPlayers[I] || (!EventPlayers[I]->IsPlaying() && !Runtime->Events[I].bWaitingForStop)) UsableSlots |= 1u << I;
    const double NaturalDuration = Wave ? static_cast<double>(Wave->GetDuration()) : Spec(CueId).fallbackSeconds;
    // RCS/spool and boot contacts are bounded so authored WAV tails cannot swallow the next gait step.
    // Other recorded events retain their authored duration (fallbackSeconds
    // is the synthesized replacement duration, not a universal WAV limit).
    const bool bBoundedPulse=Spec(CueId).eventGroup==5||Spec(CueId).eventGroup==6||Spec(CueId).eventGroup==8;
    const double Duration = bBoundedPulse ? FMath::Min(NaturalDuration, Spec(CueId).fallbackSeconds) : NaturalDuration;
    const int32 Slot = Runtime->Router.TryEvent(CueId, Duration, UsableSlots);
    if (Slot < 0) return;
    if (EventName == TEXT("Footstep")) ++Runtime->FootstepSequence;
    Runtime->Events[Slot].CueId = CueId;
    Runtime->EventRemaining[Slot] = Duration;
    Runtime->EventFallback[Slot] = !Wave;
    if (Wave && EventPlayers[Slot])
    {
        const float Gain = Spec(CueId).eventGain * static_cast<float>(Runtime->Router.Output().eventMaster);
        EventPlayers[Slot]->SetSound(Wave);
        EventPlayers[Slot]->SetVolumeMultiplier(1.0f);
        EventPlayers[Slot]->FadeIn(0.012f, Gain, 0.0f);
        Runtime->Events[Slot].LastGain = Gain;
    }
    else
    {
        Runtime->EventFallback[Slot] = true;
        Controls->Events[Slot].store(static_cast<int32>(CueId), std::memory_order_release);
    }
}

void UStarShipAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (Runtime->bStarted) UpdateMix(DeltaTime);
}
void UStarShipAudioComponent::UpdateMix(float DeltaTime)
{
    using namespace star::audio::routing;
    Runtime->Router.Step(DeltaTime);
    const Mix& MixState = Runtime->Router.Output();
    for (int32 I = 0; I < LayerPlayers.Num(); ++I)
    {
        StarShipAudio::ApplyPlayer(LayerPlayers[I], GetCueWave(I), static_cast<Cue>(I), MixState.assetLayers[I], Runtime->Layers[I]);
        Controls->Layers[I].store(static_cast<float>(MixState.fallbackLayers[I]), std::memory_order_relaxed);
    }
    for (int32 I = 0; I < MusicPlayers.Num(); ++I)
    {
        const Cue CueId = MixState.music[I].cue;
        StarShipAudio::ApplyPlayer(MusicPlayers[I], GetCueWave(static_cast<int32>(CueId)), CueId,
            MixState.musicGain * MixState.music[I].weight, Runtime->Music[I]);
    }
    for (int32 I = 0; I < EventPlayers.Num(); ++I)
    {
        auto& PlayerState = Runtime->Events[I];
        UAudioComponent* Player = EventPlayers[I];
        if (PlayerState.bWaitingForStop && (!Player || !Player->IsPlaying())
            && (!Runtime->EventFallback[I] || Runtime->EventRemaining[I] <= 0.0)) PlayerState.bWaitingForStop = false;
        if (!IsEvent(PlayerState.CueId)) continue;
        Runtime->EventRemaining[I] = FMath::Max(0.0, Runtime->EventRemaining[I] - FMath::Clamp(static_cast<double>(DeltaTime), 0.0, 0.25));
        if ((Runtime->EventFallback[I] && Runtime->EventRemaining[I] == 0.0)
            || (!Runtime->EventFallback[I] && (!Player || !Player->IsPlaying())))
        {
            Runtime->Router.ReleaseEvent(static_cast<std::size_t>(I));
            PlayerState.CueId = Cue::None;
            Runtime->EventFallback[I] = false;
        }
        else if (!Runtime->EventFallback[I] && !PlayerState.bWaitingForStop && Player)
        {
            if ((Spec(PlayerState.CueId).eventGroup == 5 || Spec(PlayerState.CueId).eventGroup == 6 || Spec(PlayerState.CueId).eventGroup == 8) && Runtime->EventRemaining[I] == 0.0)
            {
                StarShipAudio::FadePlayerToStop(Player, PlayerState);
                continue;
            }
            const float Gain = Spec(PlayerState.CueId).eventGain * static_cast<float>(MixState.eventMaster);
            if (!FMath::IsNearlyEqual(PlayerState.LastGain, Gain, 0.0001f))
            {
                Player->AdjustVolume(0.03f, Gain);
                PlayerState.LastGain = Gain;
            }
        }
    }
    // Imported engine loops move gently in pitch; the continuous oscillator carries the larger RPM sweep.
    for (const Cue Engine : {Cue::EngineIdle, Cue::EngineLow, Cue::EngineMid, Cue::EngineHigh})
    {
        const int32 Index = static_cast<int32>(Engine);
        if (LayerPlayers.IsValidIndex(Index) && LayerPlayers[Index])
            LayerPlayers[Index]->SetPitchMultiplier(static_cast<float>(MixState.enginePitch));
    }
    Controls->WhineGain.store(static_cast<float>(MixState.whineGain), std::memory_order_relaxed);
    Controls->Charge.store(static_cast<float>(MixState.charge), std::memory_order_relaxed);
    Controls->Cruise.store(static_cast<float>(MixState.cruise), std::memory_order_relaxed);
    Controls->Braking.store(static_cast<float>(MixState.braking), std::memory_order_relaxed);
    Controls->Throttle.store(static_cast<float>(MixState.throttle), std::memory_order_relaxed);
    Controls->EventMaster.store(static_cast<float>(MixState.eventMaster), std::memory_order_relaxed);
    Controls->Paused.store(Runtime->Input.paused || Runtime->Input.masterVolume <= 0.0 || !Runtime->bStarted, std::memory_order_release);
}
