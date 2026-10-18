#pragma once
#include "Commandlets/Commandlet.h"
#include "CodexCreatureAnimationsCommandlet.generated.h"

UCLASS()
class CODEXBLUEPRINTTOOLS_API UCodexCreatureAnimationsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCodexCreatureAnimationsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
