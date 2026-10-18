#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CosmoClientComponent.h"
#include "AICompanionManager.generated.h"

class UFirstPersonAIIntegrationComponent;
class USkeletalMeshComponent;

/** Blueprint facade over the character's HTTP client and realtime voice subtitles. */
UCLASS(Blueprintable)
class COSMOBRIDGE_API AAICompanionManager : public AActor
{
    GENERATED_BODY()
public:
    AAICompanionManager();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Companion") TObjectPtr<USkeletalMeshComponent> CompanionMesh;
    UPROPERTY(BlueprintReadOnly, Category="Companion") TObjectPtr<UCosmoClientComponent> Client;
    UPROPERTY(BlueprintReadOnly, Category="Companion") TObjectPtr<UFirstPersonAIIntegrationComponent> Integration;
    UPROPERTY(BlueprintReadOnly, Category="Companion") FCosmoResponse LastResponse;
    UPROPERTY(BlueprintReadOnly, Category="Companion") FString Subtitle;
    UPROPERTY(BlueprintReadOnly, Category="Companion") FString ReplySource = TEXT("none");
    UPROPERTY(BlueprintReadOnly, Category="Companion") FCosmoAnimationEvent AnimationEvent;

    UPROPERTY(BlueprintAssignable, Category="Companion") FCosmoResponseDelegate OnResponse;
    UPROPERTY(BlueprintAssignable, Category="Companion") FCosmoSpokenLineDelegate OnSpokenLine;
    UPROPERTY(BlueprintAssignable, Category="Companion") FCosmoAnimationDelegate OnAnimation;
    UPROPERTY(BlueprintAssignable, Category="Companion") FCosmoConnectionDelegate OnConnectionChanged;

    /** Uses the same client, callbacks, subtitles and TTS as the local player. */
    UFUNCTION(BlueprintCallable, Category="Companion") bool ConnectToCharacter(AActor* Character);
    /** Uses realtime voice when connected, otherwise /player/utterance; never reports a gameplay event. */
    UFUNCTION(BlueprintCallable, Category="Companion") bool SubmitPlayerQuestion(const FString& Text);

    /** Attached actors have no movement component; use the parent's velocity for the locomotion ABP. */
    virtual FVector GetVelocity() const override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void UnbindClient();
    UFUNCTION() void HandleResponse(const FCosmoResponse& Response);
    UFUNCTION() void HandleSpokenLine(const FString& Line);
    UFUNCTION() void HandleRealtimeLine(const FString& Line);
    UFUNCTION() void HandleAnimation(const FCosmoAnimationEvent& Event);
    UFUNCTION() void HandleConnection(bool Connected);
};
