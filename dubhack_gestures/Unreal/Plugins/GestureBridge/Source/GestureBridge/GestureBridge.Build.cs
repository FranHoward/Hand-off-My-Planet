using UnrealBuildTool;
public class GestureBridge : ModuleRules
{
    public GestureBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Sockets", "Networking", "Json", "InputCore", "Projects" });
        RuntimeDependencies.Add("$(PluginDir)/Resources/gesture_bindings.json", StagedFileType.NonUFS);
    }
}
