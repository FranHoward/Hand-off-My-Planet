#pragma once
#include "Commandlets/Commandlet.h"
#include "CodexRealtimeAVCommandlet.generated.h"

/** Offline protocol validation. Never opens the microphone or a camera. */
UCLASS()
class CODEXBLUEPRINTTOOLS_API UCodexRealtimeAVCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCodexRealtimeAVCommandlet();
    virtual int32 Main(const FString& Params) override;
};
