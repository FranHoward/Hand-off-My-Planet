using System.IO;
using UnrealBuildTool;

public class CosmoBridge : ModuleRules
{
    public CosmoBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "HTTP", "Json" });
        PublicDependencyModuleNames.AddRange(new[] { "GestureBridge", "Niagara", "UMG", "SlateCore", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "Slate", "TextToSpeech", "EnhancedInput" });
        PrivateDependencyModuleNames.AddRange(new[] { "WebSockets", "AudioCaptureCore", "AudioCapture", "AudioMixer", "ImageWrapper", "RenderCore", "RHI" });
        PublicDependencyModuleNames.AddRange(new[] { "AIModule", "NavigationSystem", "GameplayTasks" });
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string RuntimeRoot = Path.Combine(PluginDirectory, "Resources", "Runtime", "Win64");
            if (!File.Exists(Path.Combine(RuntimeRoot, "manifest.json")))
                throw new BuildException("Missing COSMO runtime. Run Scripts/build_runtime.py before building Win64.");
            foreach (string File in Directory.GetFiles(RuntimeRoot, "*", SearchOption.AllDirectories))
            {
                if (File.Contains("__pycache__") || File.EndsWith(".pyc")) continue;
                RuntimeDependencies.Add(File, StagedFileType.NonUFS);
            }
        }

    }
}
