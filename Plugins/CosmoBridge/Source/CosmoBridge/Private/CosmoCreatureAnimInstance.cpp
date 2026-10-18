#include "CosmoCreatureAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UnrealType.h"

namespace
{
bool IsOneShot(ECosmoCreatureAnimState State)
{
    return State != ECosmoCreatureAnimState::Idle && State != ECosmoCreatureAnimState::Move && State != ECosmoCreatureAnimState::Death;
}
int32 Priority(ECosmoCreatureAnimState State)
{
    switch (State)
    {
        case ECosmoCreatureAnimState::Death: return 100;
        case ECosmoCreatureAnimState::Controlled: return 90;
        case ECosmoCreatureAnimState::Hit: return 80;
        case ECosmoCreatureAnimState::Pounce: return 60;
        case ECosmoCreatureAnimState::Jump: return 50;
        case ECosmoCreatureAnimState::Attack: return 40;
        default: return 0;
    }
}
FName StateName(ECosmoCreatureAnimState State)
{
    switch(State)
    {
        case ECosmoCreatureAnimState::Move: return TEXT("Move");
        case ECosmoCreatureAnimState::Attack: return TEXT("Attack");
        case ECosmoCreatureAnimState::Hit: return TEXT("Hit");
        case ECosmoCreatureAnimState::Jump: return TEXT("Jump");
        case ECosmoCreatureAnimState::Pounce: return TEXT("Pounce");
        case ECosmoCreatureAnimState::Controlled: return TEXT("Controlled");
        case ECosmoCreatureAnimState::Death: return TEXT("Death");
        default: return TEXT("Idle");
    }
}
}

UCosmoCreatureAnimInstance::UCosmoCreatureAnimInstance()
{
    RootMotionMode = ERootMotionMode::NoRootMotionExtraction;
}

void UCosmoCreatureAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    Dead = false; Controlled = false; ControlRemaining = 0.f;
    DesiredState = ECosmoCreatureAnimState::Idle; ActionElapsed = 0.f;
    Moving = false; InAir = false; WasInAir = false;
    RestartPending = ResetPending = RestartAction = ResetToIdle = false;
    DeliveredActionEvents.Reset(); ControlBeginDelivered = false;
    LastAnimationEvent = NAME_None; AnimationEventCounts.Reset();
    UpdateTransitionFlags();
}

bool UCosmoCreatureAnimInstance::SupportsAnimation(ECosmoCreatureAnimState State) const
{
    const auto* Sequence = Animations.Find(State);
    return Sequence && IsValid(Sequence->Get());
}

FName UCosmoCreatureAnimInstance::GetAnimationStateName() const { return StateName(DesiredState); }

void UCosmoCreatureAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const float Delta = FMath::Max(0.f, DeltaSeconds);
    if (ReadOwnerMovement)
        if (auto* Owner = GetOwningActor())
        {
            Speed = Owner->GetVelocity().Size2D();
            if (auto* Character = Cast<ACharacter>(Owner)) InAir = Character->GetCharacterMovement()->IsFalling();
            // Existing/future actor BPs can expose Health or bDead. Damage application remains in gameplay.
            if (const auto* Health = FindFProperty<FNumericProperty>(Owner->GetClass(), TEXT("Health")))
            {
                const void* Value = Health->ContainerPtrToValuePtr<void>(Owner);
                const double Amount = Health->IsFloatingPoint() ? Health->GetFloatingPointPropertyValue(Value) : double(Health->GetSignedIntPropertyValue(Value));
                if (Amount <= 0.) SetDead();
            }
            if (const auto* Death = FindFProperty<FBoolProperty>(Owner->GetClass(), TEXT("bDead")))
                if (Death->GetPropertyValue_InContainer(Owner)) SetDead();
        }
    Speed = FMath::Max(0.f, Speed);
    Moving = Moving ? Speed > MoveExitSpeed : Speed >= MoveEnterSpeed;
    MovePlayRate = FMath::Clamp(Speed / FMath::Max(ReferenceMoveSpeed, 1.f), .1f, 4.f);
    RestartAction = RestartPending; RestartPending = false;
    ResetToIdle = ResetPending; ResetPending = false;

    if (Controlled)
    {
        // Gameplay control lasts a full duration even if Boss Pounce finishes or this pose blends out.
        ControlRemaining = FMath::Max(0.f, ControlRemaining - Delta);
        if (ControlRemaining <= KINDA_SMALL_NUMBER) EndControl();
    }
    if (!Dead)
    {
        if (InAir && !WasInAir && !Controlled && !IsOneShot(DesiredState) && SupportsAnimation(ECosmoCreatureAnimState::Jump)) RequestJump();
        if (IsOneShot(DesiredState) && DesiredState != ECosmoCreatureAnimState::Controlled)
        {
            const int32 Machine = GetStateMachineIndex(TEXT("CreatureStateMachine"));
            const bool Playing = Machine != INDEX_NONE && GetCurrentStateName(Machine) == StateName(DesiredState);
            if (Playing && !RestartAction) ActionElapsed += Delta;
            if (const auto* Sequence = Animations.Find(DesiredState))
                if (IsValid(Sequence->Get()) && ActionElapsed >= (*Sequence)->GetPlayLength() &&
                    (DesiredState != ECosmoCreatureAnimState::Jump || !InAir))
                { DesiredState = Moving ? ECosmoCreatureAnimState::Move : ECosmoCreatureAnimState::Idle; ActionElapsed = 0.f; }
        }
        if (!IsOneShot(DesiredState)) DesiredState = Moving ? ECosmoCreatureAnimState::Move : ECosmoCreatureAnimState::Idle;
    }
    WasInAir = InAir;
    UpdateTransitionFlags();
}

bool UCosmoCreatureAnimInstance::RequestAction(ECosmoCreatureAnimState State)
{
    if (Dead || Controlled || !SupportsAnimation(State)) return false;
    if (Priority(State) < Priority(DesiredState)) return false;
    // Attacks/pounces cannot retrigger every tick. Repeated hits restart their visual reaction.
    if (DesiredState == State && State != ECosmoCreatureAnimState::Hit && State != ECosmoCreatureAnimState::Jump) return false;
    RestartPending = DesiredState == State;
    DesiredState = State; ActionElapsed = 0.f; DeliveredActionEvents.Reset();
    UpdateTransitionFlags();
    return true;
}
bool UCosmoCreatureAnimInstance::RequestAttack() { return RequestAction(ECosmoCreatureAnimState::Attack); }
bool UCosmoCreatureAnimInstance::RequestHit() { return RequestAction(ECosmoCreatureAnimState::Hit); }
bool UCosmoCreatureAnimInstance::RequestJump() { return RequestAction(ECosmoCreatureAnimState::Jump); }
bool UCosmoCreatureAnimInstance::RequestPounce() { return RequestAction(ECosmoCreatureAnimState::Pounce); }

bool UCosmoCreatureAnimInstance::ApplyControlled(float Duration)
{
    if (Dead || Species != ECosmoCreatureSpecies::CrystalEye || !SupportsAnimation(ECosmoCreatureAnimState::Controlled) || !FMath::IsFinite(Duration) || Duration <= 0.f) return false;
    RestartPending = Controlled;
    Controlled = true; ControlRemaining = Duration;
    DesiredState = ECosmoCreatureAnimState::Controlled; ActionElapsed = 0.f;
    DeliveredActionEvents.Reset(); ControlBeginDelivered = true;
    Emit(TEXT("ControlBegin")); UpdateTransitionFlags();
    return true;
}

void UCosmoCreatureAnimInstance::EndControl()
{
    if (!Controlled) return;
    Controlled = false; ControlRemaining = 0.f;
    if (!Dead) DesiredState = Moving ? ECosmoCreatureAnimState::Move : ECosmoCreatureAnimState::Idle;
    Emit(TEXT("ControlEnd"));
}

bool UCosmoCreatureAnimInstance::SetDead()
{
    if (Dead || !SupportsAnimation(ECosmoCreatureAnimState::Death)) return false;
    Dead = true; EndControl(); DesiredState = ECosmoCreatureAnimState::Death;
    ActionElapsed = 0.f; RestartPending = false; DeliveredActionEvents.Reset();
    UpdateTransitionFlags(); return true;
}

void UCosmoCreatureAnimInstance::ResetForRespawn()
{
    EndControl(); Dead = false; DesiredState = ECosmoCreatureAnimState::Idle;
    ActionElapsed = 0.f; Speed = 0.f; Moving = false; InAir = false; WasInAir = false;
    RestartPending = false; ResetPending = true; DeliveredActionEvents.Reset(); UpdateTransitionFlags();
}
void UCosmoCreatureAnimInstance::SetManualMovement(float MovementSpeed, bool IsInAir)
{ ReadOwnerMovement = false; Speed = FMath::IsFinite(MovementSpeed) ? FMath::Max(0.f, MovementSpeed) : 0.f; InAir = IsInAir; }
void UCosmoCreatureAnimInstance::ResumeOwnerMovement() { ReadOwnerMovement = true; }

void UCosmoCreatureAnimInstance::UpdateTransitionFlags()
{
    PlayIdle = DesiredState == ECosmoCreatureAnimState::Idle;
    PlayMove = DesiredState == ECosmoCreatureAnimState::Move;
    PlayAttack = DesiredState == ECosmoCreatureAnimState::Attack;
    PlayHit = DesiredState == ECosmoCreatureAnimState::Hit;
    PlayJump = DesiredState == ECosmoCreatureAnimState::Jump;
    PlayPounce = DesiredState == ECosmoCreatureAnimState::Pounce;
    PlayControlled = DesiredState == ECosmoCreatureAnimState::Controlled;
    PlayDeath = DesiredState == ECosmoCreatureAnimState::Death;
}

void UCosmoCreatureAnimInstance::Emit(FName Name)
{ LastAnimationEvent = Name; ++AnimationEventCounts.FindOrAdd(Name); OnAnimationEvent.Broadcast(Name); }

void UCosmoCreatureAnimInstance::HandleAnimationEvent(FName Name, UAnimSequenceBase* Source)
{
    if (Name == TEXT("ControlBegin")) { if (!ControlBeginDelivered) { ControlBeginDelivered = true; Emit(Name); } return; }
    if (Name == TEXT("ControlEnd")) return; // The independent gameplay timer owns the exact control end.
    if (Name.ToString().StartsWith(TEXT("Footstep_")))
    { if (!Dead && DesiredState == ECosmoCreatureAnimState::Move) Emit(Name); return; }
    // Ignore stale attack notifies from an interrupted/blending-out action.
    const auto* Current = Animations.Find(DesiredState);
    if (Dead || !Current || Current->Get() != Source || DeliveredActionEvents.Contains(Name)) return;
    DeliveredActionEvents.Add(Name); Emit(Name);
}

FString UCosmoCreatureAnimNotify::GetNotifyName_Implementation() const { return EventName.ToString(); }
void UCosmoCreatureAnimNotify::Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(Mesh, Animation, EventReference);
    if (Mesh) if (auto* Instance = Cast<UCosmoCreatureAnimInstance>(Mesh->GetAnimInstance())) Instance->HandleAnimationEvent(EventName, Animation);
}
