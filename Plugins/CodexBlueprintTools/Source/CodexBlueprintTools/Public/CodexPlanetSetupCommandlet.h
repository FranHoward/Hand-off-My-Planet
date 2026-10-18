#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CodexPlanetSetupCommandlet.generated.h"
UCLASS()
class UCodexPlanetSetupCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCodexPlanetSetupCommandlet();
    virtual int32 Main(const FString& Params) override;
};
