#include "Exploration/StarExplorationSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStarExplorationJsonTest, "STAR.Exploration.JsonAtomicRestore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStarExplorationJsonTest::RunTest(const FString& Parameters)
{
    UGameInstance* Instance = NewObject<UGameInstance>();
    UStarExplorationSubsystem* A = NewObject<UStarExplorationSubsystem>(Instance);
    FStarObservationSample S; S.BodyId = TEXT("earth"); S.AltitudeM = 400000; S.SpeedMps = 7700;
    S.bViewingBody = true; S.bScanning = true; S.SunIllumination = 0.8; S.DeltaSeconds = 0.1;
    for (uint64 I = 1; I <= 120; ++I) { S.Sequence = I; A->SubmitObservation(S); }
    TestTrue(TEXT("Source observation complete"), A->GetObjectives()[0].bComplete);
    const FString Saved = A->ExportProgressJson();
    UStarExplorationSubsystem* B = NewObject<UStarExplorationSubsystem>(Instance);
    FString Error;
    TestTrue(TEXT("Round trip"), B->RestoreProgressJson(Saved, Error));
    TestEqual(TEXT("Journal restored"), B->GetJournalEntries().Num(), 1);
    TestFalse(TEXT("Truncated JSON rejected"), B->RestoreProgressJson(Saved.Left(Saved.Len() / 2), Error));
    TestTrue(TEXT("Existing discovery retained"), B->GetObjectives()[0].bComplete);
    TestFalse(TEXT("Duplicate objective IDs rejected"), B->RestoreProgressJson(Saved.Replace(TEXT("earth_night"), TEXT("earth_day")), Error));
    TestFalse(TEXT("Unknown objective rejected"), B->RestoreProgressJson(Saved.Replace(TEXT("earth_night"), TEXT("unknown_id")), Error));
    TestFalse(TEXT("Schema mismatch rejected"), B->RestoreProgressJson(TEXT("{\"version\":99}"), Error));
    TestFalse(TEXT("Oversize input rejected"), B->RestoreProgressJson(FString::ChrN(131073, ' '), Error));
    TestEqual(TEXT("Failed restores leave all bytes unchanged"), B->ExportProgressJson(), Saved);
    return true;
}
#endif
