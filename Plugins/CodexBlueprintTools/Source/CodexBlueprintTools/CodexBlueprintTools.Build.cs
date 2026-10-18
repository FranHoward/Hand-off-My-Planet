using UnrealBuildTool;

public class CodexBlueprintTools : ModuleRules
{
    public CodexBlueprintTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd", "BlueprintGraph", "Kismet", "GraphEditor", "GestureBridge", "CosmoBridge", "Niagara", "Json", "HTTP",
            "AnimGraph", "AnimGraphRuntime", "AnimationBlueprintLibrary", "AssetTools", "NavigationSystem", "AIModule", "AssetRegistry", "EnhancedInput", "Sockets", "InputCore", "SlateCore"
        });
    }
}
