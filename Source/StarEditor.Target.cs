using UnrealBuildTool;
using System.Collections.Generic;
public class StarEditorTarget : TargetRules
{
    public StarEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("Star");
    }
}
