using UnrealBuildTool;
public class Star : ModuleRules
{
    public Star(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.Add(ModuleDirectory);
        PrivateDependencyModuleNames.AddRange(new[] { "HeadMountedDisplay", "XRBase", "HTTP", "ImageWrapper" });
        AddEngineThirdPartyPrivateStaticDependencies(Target, "LibTiff", "zlib", "LibJpegTurbo");
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "Json", "JsonUtilities", "ProceduralMeshComponent", "RenderCore", "RHI", "StarFlightInput", "ApplicationCore", "AudioMixer", "DeveloperSettings" });
    }
}
