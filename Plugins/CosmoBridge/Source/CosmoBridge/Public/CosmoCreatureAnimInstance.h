#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "CosmoCreatureAnimInstance.generated.h"

class UAnimSequence;

UENUM(BlueprintType)
enum class ECosmoCreatureSpecies : uint8 { CrystalEye, Minion, Boss };

UENUM(BlueprintType)
enum class ECosmoCreatureAnimState : uint8 { Idle, Move, Attack, Hit, Jump, Pounce, Controlled, Death };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmoCreatureAnimationEvent, FName, EventName);

/** Shared gameplay-facing driver; the poses, state machines and transitions live in each editable ABP. */
UCLASS(Blueprintable, BlueprintType, Transient)
class COSMOBRIDGE_API UCosmoCreatureAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UCosmoCreatureAnimInstance();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Creature") ECosmoCreatureSpecies Species = ECosmoCreatureSpecies::Minion;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Creature") TMap<ECosmoCreatureAnimState, TObjectPtr<UAnimSequence>> Animations;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Locomotion", meta=(ClampMin="1")) float ReferenceMoveSpeed = 80.882f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Locomotion", meta=(ClampMin="0")) float MoveEnterSpeed = 5.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Locomotion", meta=(ClampMin="0")) float MoveExitSpeed = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Locomotion") bool ReadOwnerMovement = true;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") float Speed = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") float MovePlayRate = 1.f;
    UPROPERTY(BlueprintReadOnly, Category="Locomotion") bool InAir = false;
    UPROPERTY(BlueprintReadOnly, Category="State") ECosmoCreatureAnimState DesiredState = ECosmoCreatureAnimState::Idle;
    UPROPERTY(BlueprintReadOnly, Category="State") bool Dead = false;
    UPROPERTY(BlueprintReadOnly, Category="State") bool Controlled = false;
    UPROPERTY(BlueprintReadOnly, Category="State") float ControlRemaining = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="State") float ActionElapsed = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Events") FName LastAnimationEvent;
    UPROPERTY(BlueprintReadOnly, Category="Events") TMap<FName, int32> AnimationEventCounts;

    // Direct member reads keep transition graphs and locomotion play rate on the animation fast path.
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool PlayIdle = true;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool PlayMove = false;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool PlayAttack = false;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool PlayHit = false;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool PlayJump = false;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool PlayPounce = false;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool PlayControlled = false;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool PlayDeath = false;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool RestartAction = false;
    UPROPERTY(BlueprintReadOnly, Category="Transitions") bool ResetToIdle = false;

    UPROPERTY(BlueprintAssignable, Category="Events") FCosmoCreatureAnimationEvent OnAnimationEvent;
    UFUNCTION(BlueprintCallable, Category="Creature|Animation") bool RequestAttack();
    UFUNCTION(BlueprintCallable, Category="Creature|Animation") bool RequestHit();
    UFUNCTION(BlueprintCallable, Category="Creature|Animation") bool RequestJump();
    UFUNCTION(BlueprintCallable, Category="Creature|Animation") bool RequestPounce();
    /** Call only after gameplay confirms a control hit. Timer is independent of the attacking Boss animation. */
    UFUNCTION(BlueprintCallable, Category="Creature|Animation", meta=(AdvancedDisplay="Duration")) bool ApplyControlled(float Duration = 1.f);
    UFUNCTION(BlueprintCallable, Category="Creature|Animation") bool SetDead();
    UFUNCTION(BlueprintCallable, Category="Creature|Animation") void ResetForRespawn();
    UFUNCTION(BlueprintCallable, Category="Creature|Animation") void SetManualMovement(float MovementSpeed, bool IsInAir = false);
    UFUNCTION(BlueprintCallable, Category="Creature|Animation") void ResumeOwnerMovement();
    UFUNCTION(BlueprintPure, Category="Creature|Animation") bool SupportsAnimation(ECosmoCreatureAnimState State) const;
    UFUNCTION(BlueprintPure, Category="Creature|Animation") FName GetAnimationStateName() const;
    void HandleAnimationEvent(FName Name, UAnimSequenceBase* Source);
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
private:
    bool Moving = false;
    bool RestartPending = false;
    bool ResetPending = false;
    bool WasInAir = false;
    bool ControlBeginDelivered = false;
    TSet<FName> DeliveredActionEvents;
    bool RequestAction(ECosmoCreatureAnimState State);
    void UpdateTransitionFlags();
    void Emit(FName Name);
    void EndControl();
};

/** Instanced notify preserves the exact event names/times from the supplied events.json files. */
UCLASS(meta=(DisplayName="COSMO Creature Event"))
class COSMOBRIDGE_API UCosmoCreatureAnimNotify : public UAnimNotify
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO") FName EventName;
    virtual FString GetNotifyName_Implementation() const override;
    virtual void Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
