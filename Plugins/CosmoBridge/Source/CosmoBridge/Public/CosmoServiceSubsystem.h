#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "CosmoServiceSubsystem.generated.h"
/** One bundled service per game instance; survives menu -> gameplay map travel. */
UCLASS()
class COSMOBRIDGE_API UCosmoServiceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    bool EnsureStarted(FString& Error);
    bool IsRunning();
    void StopOwnedService();
    virtual void Deinitialize() override;
private:
    FProcHandle Process;
};
