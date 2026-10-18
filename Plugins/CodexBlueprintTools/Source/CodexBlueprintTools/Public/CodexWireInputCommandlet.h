#pragma once

#include "Commandlets/Commandlet.h"
#include "CodexWireInputCommandlet.generated.h"

UCLASS()
class CODEXBLUEPRINTTOOLS_API UCodexWireInputCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCodexWireInputCommandlet();
    virtual int32 Main(const FString& Params) override;
};
