#include "CosmoServiceSubsystem.h"
#include "CosmoRuntimePaths.h"
bool UCosmoServiceSubsystem::IsRunning()
{ return Process.IsValid() && FPlatformProcess::IsProcRunning(Process); }
bool UCosmoServiceSubsystem::EnsureStarted(FString& Error)
{
    if(IsRunning()) return true;
    if(Process.IsValid()) { FPlatformProcess::CloseProc(Process); Process.Reset(); }
    const FString Root=CosmoRuntimePaths::Root(),Python=CosmoRuntimePaths::Python(Root);
    const FString Script=FPaths::Combine(Root,TEXT("app/cosmo_service.py"));
    if(!FPaths::FileExists(Python) || !FPaths::FileExists(Script)) { Error=TEXT("Bundled COSMO runtime is missing."); return false; }
    const FString Args=FString::Printf(TEXT("-B -u \"%s\" --parent-pid %u --log-dir \"%s\""),*Script,FPlatformProcess::GetCurrentProcessId(),*CosmoRuntimePaths::LogDir());
    Process=FPlatformProcess::CreateProc(*Python,*Args,false,true,true,nullptr,0,*Root,nullptr);
    if(!Process.IsValid()) { Error=TEXT("The bundled COSMO service could not start."); return false; }
    return true;
}
void UCosmoServiceSubsystem::StopOwnedService()
{
    if(Process.IsValid()) { if(IsRunning()) FPlatformProcess::TerminateProc(Process,true); FPlatformProcess::CloseProc(Process); Process.Reset(); }
}
void UCosmoServiceSubsystem::Deinitialize()
{ StopOwnedService(); Super::Deinitialize(); }
