#pragma once
#include "Commandlets/Commandlet.h"
#include "GameFramework/Actor.h"
#include "CodexCompleteCharacterCommandlet.generated.h"

UCLASS()
class CODEXBLUEPRINTTOOLS_API UCodexCompleteCharacterCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCodexCompleteCharacterCommandlet();
    virtual int32 Main(const FString& Params) override;
};

/** Disposable test target; never included in the game runtime plugin. */
UCLASS()
class ACodexCombatTestTarget : public AActor
{
    GENERATED_BODY()
public:
    ACodexCombatTestTarget();
    UPROPERTY() float Health = 100.f;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
};
