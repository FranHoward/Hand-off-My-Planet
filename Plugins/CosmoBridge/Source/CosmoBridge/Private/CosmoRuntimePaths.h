#pragma once

#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

namespace CosmoRuntimePaths
{
inline FString Root()
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CosmoBridge"));
    return Plugin.IsValid()
        ? FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/Runtime/Win64")))
        : FString();
}
inline FString Python(const FString& Root)
{
    return FPaths::Combine(Root, TEXT("python/python.exe"));
}
inline FString LogDir()
{
#if WITH_EDITOR
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs")));
#else
    return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("COSMO/Logs"));
#endif
}
}
