#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CodexGestureOnlyCommandlet.generated.h"

UCLASS()
class UCodexGestureOnlyCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCodexGestureOnlyCommandlet();
    virtual int32 Main(const FString& Params) override;
};
