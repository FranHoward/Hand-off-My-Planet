#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "HAL/PlatformProcess.h"
#include "CosmoClientComponent.generated.h"

UENUM(BlueprintType)
enum class ECosmoGameEvent : uint8
{
    GameStarted,
    MovementCompleted,
    ShieldActivated,
    FireActivated,
    WaveActivated,
    LaserActivated,
    EnemyScanned,
    BossEncountered,
    TutorialCompleted,
    PlayerDamaged,
    PlayerInDanger,
    DangerCleared
};

USTRUCT(BlueprintType)
struct COSMOBRIDGE_API FCosmoTutorialStatus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString TutorialId;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString CurrentStageId;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString CurrentStageName;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") TArray<FString> CompletedStageIds;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString ExpectedEvent;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") TArray<FString> AvailableHints;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") bool IsComplete = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString LastInstruction;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString LastHint;
};

USTRUCT(BlueprintType)
struct COSMOBRIDGE_API FCosmoAnimationEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString State;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString Animation;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString Reason;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString Line;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString StageId;
};

USTRUCT(BlueprintType)
struct COSMOBRIDGE_API FCosmoResponse
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="COSMO") bool Success = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") int32 HttpStatus = 0;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString Endpoint;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString Error;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString Provider;
    /** Actual reply origin: grok, mock, or fallback. Health/provider alone cannot confirm Grok. */
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString Source;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString ProviderError;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString RawJson;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString SpokenLine;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") bool Advanced = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") bool Accepted = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") bool HasTutorialStatus = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FCosmoTutorialStatus TutorialStatus;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FCosmoAnimationEvent AnimationEvent;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmoResponseDelegate, const FCosmoResponse&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmoConnectionDelegate, bool, Connected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmoSpokenLineDelegate, const FString&, SpokenLine);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmoAnimationDelegate, const FCosmoAnimationEvent&, AnimationEvent);

/**
 * Thin Blueprint client for the local COSMO service. Gameplay events are the
 * only calls that can advance the tutorial; player utterances only request help.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class COSMOBRIDGE_API UCosmoClientComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCosmoClientComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Connection")
    FString BaseUrl = TEXT("http://127.0.0.1:8765");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Connection", meta=(ClampMin="1.0", ClampMax="60.0"))
    float RequestTimeoutSeconds = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Connection")
    bool AutoCheckHealth = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Service") bool AutoLaunchService = true;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Service") bool ServiceReady = false;

    UPROPERTY(BlueprintReadOnly, Category="COSMO|Connection") bool Connected = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Connection") FString LastError;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FCosmoResponse LastResponse;

    UPROPERTY(BlueprintAssignable, Category="COSMO") FCosmoResponseDelegate OnResponse;
    UPROPERTY(BlueprintAssignable, Category="COSMO") FCosmoConnectionDelegate OnConnectionChanged;
    UPROPERTY(BlueprintAssignable, Category="COSMO") FCosmoSpokenLineDelegate OnSpokenLine;
    UPROPERTY(BlueprintAssignable, Category="COSMO") FCosmoAnimationDelegate OnAnimation;

    UFUNCTION(BlueprintCallable, Category="COSMO|Connection") bool CheckHealth();
    UFUNCTION(BlueprintPure, Category="COSMO|Connection") bool HasPendingRequests() const { return !ActiveRequests.IsEmpty() || !QueuedRequests.IsEmpty(); }
    UFUNCTION(BlueprintCallable, Category="COSMO|Tutorial") bool SendGameEvent(ECosmoGameEvent Event);
    UFUNCTION(BlueprintCallable, Category="COSMO|Tutorial") bool SendGameEventByName(const FString& EventName);
    UFUNCTION(BlueprintCallable, Category="COSMO|Voice") bool SendPlayerUtterance(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="COSMO|Tutorial") bool GetTutorialStatus();
    UFUNCTION(BlueprintCallable, Category="COSMO|Tutorial") bool GetCurrentTutorialStep();
    UFUNCTION(BlueprintCallable, Category="COSMO|Tutorial") bool GetNextHint();
    UFUNCTION(BlueprintCallable, Category="COSMO") bool GetSnapshot();
    /** Live UE state only; never completes a tutorial stage. */
    UFUNCTION(BlueprintCallable, Category="COSMO") bool SendGameState(const FString& JsonState);
    UFUNCTION(BlueprintCallable, Category="COSMO|Development") bool ResetSession();

    /** Call only after the corresponding gameplay ability actually succeeds. */
    UFUNCTION(BlueprintCallable, Category="COSMO|Gesture") bool ReportConfirmedAbility(FName AbilityAction);

    UFUNCTION(BlueprintPure, Category="COSMO|Tutorial")
    static FString GameEventToString(ECosmoGameEvent Event);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    FHttpRequestPtr ReadinessRequest;
    FTimerHandle ReadinessTimer;
    double StartupDeadline = 0;
    bool WaitingForService = false;
    bool LaunchAttempted = false;
    void ProbeService();
    void FailService(const FString& Error);
    TArray<FHttpRequestPtr> ActiveRequests;
    TArray<FHttpRequestPtr> QueuedRequests;
    bool HandlingResponse = false;
    bool EndingPlay = false;
    void StartNextRequest();

    bool StartRequest(const FString& Verb, const FString& Endpoint, const FString& JsonBody = FString());
    void HandleRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool WasSuccessful, FString Endpoint);
    void SetConnected(bool NewConnected);
};
