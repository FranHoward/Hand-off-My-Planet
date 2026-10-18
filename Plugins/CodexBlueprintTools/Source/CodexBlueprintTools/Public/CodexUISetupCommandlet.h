#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CodexUISetupCommandlet.generated.h"
UCLASS()
class UCodexUISetupCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCodexUISetupCommandlet();
    virtual int32 Main(const FString& Params) override;
};
