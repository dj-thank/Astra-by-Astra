using UnrealBuildTool;
using System.IO;

public class StarFlightInput : ModuleRules
{
    public StarFlightInput(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "InputCore", "InputDevice", "ApplicationCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "Engine", "Slate", "SlateCore", "Projects" });
        if (Target.Platform != UnrealTargetPlatform.Win64)
            throw new BuildException("StarFlightInput supports Win64 only.");
        string Sdl = Path.Combine(PluginDirectory, "ThirdParty", "SDL3");
        string ImportLibrary = Path.Combine(Sdl, "lib", "Win64", "SDL3.lib");
        string RuntimeLibrary = Path.Combine(Sdl, "bin", "Win64", "SDL3.dll");
        if (!File.Exists(ImportLibrary) || !File.Exists(RuntimeLibrary))
            throw new BuildException("STAR SDL3 files missing. Run pwsh -File Tools/Input/Setup-SDL3.ps1 from the project.");
        PublicSystemIncludePaths.Add(Path.Combine(Sdl, "include"));
        PublicAdditionalLibraries.Add(ImportLibrary);
        PublicDelayLoadDLLs.Add("SDL3.dll");
        RuntimeDependencies.Add("$(TargetOutputDir)/SDL3.dll", RuntimeLibrary);
        RuntimeDependencies.Add(Path.Combine(Sdl, "LICENSE.txt"), StagedFileType.NonUFS);
    }
}
