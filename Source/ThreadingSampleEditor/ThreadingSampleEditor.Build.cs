using UnrealBuildTool;

public class ThreadingSampleEditor : ModuleRules
{
    public ThreadingSampleEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

        PrivateDependencyModuleNames.AddRange(new string[] { "BlueprintGraph", "RenderCore", "HTTP" });
    }
}
