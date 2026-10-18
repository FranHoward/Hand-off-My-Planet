#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "CosmoCreatureAnimInstance.h"
#include "CosmoPlanetGame.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UBoxComponent;
class UTextRenderComponent;
class UFirstPersonAIIntegrationComponent;
class ACosmoPlanetDirector;

UCLASS() class COSMOBRIDGE_API UCosmoFireDamage : public UDamageType { GENERATED_BODY() };
UCLASS() class COSMOBRIDGE_API UCosmoWaveDamage : public UDamageType { GENERATED_BODY() };
UCLASS() class COSMOBRIDGE_API UCosmoLaserDamage : public UDamageType { GENERATED_BODY() };

/** The normal difficulty profile is a real editable asset shared by player, enemies and supplies. */
UCLASS(BlueprintType)
class COSMOBRIDGE_API UCosmoPlanetBalance : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") float PlayerHealth=100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") float FireDamage=60.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") float FireCooldown=.85f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") float WaveDamage=50.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") float WaveCooldown=4.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") float LaserDPS=45.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") float ShieldDrainPerSecond=24.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") float RepairAmount=25.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Minion") float MinionHealth=70.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Minion") float MinionDamage=8.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Minion") float MinionSpeed=140.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Minion") float MinionAttackCooldown=1.9f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss") float BossHealth=720.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss") float BossMeleeDamage=18.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss") float BossPounceDamage=26.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss") float BossSpeed=100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss") float BossAttackCooldown=4.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss") float BossArmorMultiplier=.65f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss") float BossRecoveryMultiplier=1.5f;
};

UENUM(BlueprintType)
enum class EPlanetEnemyState : uint8 { Dormant, Chase, Windup, Acting, Recover, Stunned, Dead };

UCLASS(Blueprintable)
class COSMOBRIDGE_API ACosmoPlanetEnemy : public ACharacter
{
    GENERATED_BODY()
public:
    ACosmoPlanetEnemy();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Encounter") ECosmoCreatureSpecies Species=ECosmoCreatureSpecies::Minion;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Encounter") int32 EncounterGroup=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Encounter") bool Passive=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") float MaxHealth=70.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") float Health=70.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") bool bDead=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") bool Active=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") EPlanetEnemyState State=EPlanetEnemyState::Dormant;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> AttackMarker;
    UFUNCTION(BlueprintCallable, CallInEditor) void ConfigureVisuals();
    UFUNCTION(BlueprintCallable) void ActivateEncounter();
    UFUNCTION(BlueprintCallable) void ResetEncounter();
    UFUNCTION(BlueprintPure) bool IsBoss() const { return Species==ECosmoCreatureSpecies::Boss; }
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer) override;
    virtual void Tick(float Delta) override;
    virtual void OnConstruction(const FTransform& Transform) override;
protected:
    virtual void BeginPlay() override;
private:
    TWeakObjectPtr<ACosmoPlanetDirector> Director;
    FTransform Home;
    FVector LockedTarget=FVector::ZeroVector;
    float StateTime=0.f;
    float NextPathAt=0.f;
    float NextAttackAt=0.f;
    float StunReadyAt=0.f;
    bool Pouncing=false;
    bool AttackDelivered=false;
    void EnterState(EPlanetEnemyState NewState);
    UCosmoCreatureAnimInstance* Animation() const;
    const UCosmoPlanetBalance* Tuning() const;
    void DealAttack();
    UFUNCTION() void OnAnimationEvent(FName EventName);
};

UENUM(BlueprintType)
enum class EPlanetPropKind : uint8 { Repair, Checkpoint, ShieldTarget, FireTarget, WaveTarget, LaserTarget, Gate, StarCore };

UCLASS(Blueprintable)
class COSMOBRIDGE_API ACosmoPlanetProp : public AActor
{
    GENERATED_BODY()
public:
    ACosmoPlanetProp();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") EPlanetPropKind Kind=EPlanetPropKind::Repair;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") int32 Index=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Planet") float Amount=25.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool Used=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> Blocker;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UTextRenderComponent> Label;
    UFUNCTION(BlueprintCallable, CallInEditor) void ConfigureVisuals();
    UFUNCTION(BlueprintCallable) void ConfirmTraining();
    UFUNCTION(BlueprintCallable) void SetGateOpen(bool Open);
    UFUNCTION(BlueprintCallable) void ResetProp();
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer) override;
    virtual void Tick(float Delta) override;
    virtual void OnConstruction(const FTransform& Transform) override;
protected:
    virtual void BeginPlay() override;
private:
    TWeakObjectPtr<ACosmoPlanetDirector> Director;
    float ShieldPulseAt=0.f;
    float PulseWarnAt=0.f;
    bool PulsePending=false;
};

UCLASS(Blueprintable)
class COSMOBRIDGE_API ACosmoPlanetDirector : public AActor
{
    GENERATED_BODY()
public:
    ACosmoPlanetDirector();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Difficulty") TObjectPtr<UCosmoPlanetBalance> Balance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Kills=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Checkpoint=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool BossDefeated=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool Won=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float CoreCharge=0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TSet<FName> TrainingCompleted;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UFirstPersonAIIntegrationComponent> Player;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<TObjectPtr<ACosmoPlanetEnemy>> Enemies;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<TObjectPtr<ACosmoPlanetProp>> Props;
    /** Authored lines for actual region entry; never generated by the chat model. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial|Locations") TMap<FName,FString> RegionTutorialLines;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Tutorial|Locations") TSet<FName> AnnouncedRegions;
    UFUNCTION(BlueprintCallable, Category="Tutorial|Locations") bool AnnounceRegion(FName Region);
    UFUNCTION(BlueprintCallable) void RetryCheckpoint();
    UFUNCTION(BlueprintCallable) void RestartRun();
    UFUNCTION(BlueprintCallable) void RecordTraining(FName Ability);
    UFUNCTION(BlueprintCallable) void EnemyDefeated(ACosmoPlanetEnemy* Enemy);
    UFUNCTION(BlueprintCallable) void PublishStatus();
    bool RequestAttackSlot(ACosmoPlanetEnemy* Enemy);
    bool GroupCleared(int32 Group) const;
    static ACosmoPlanetDirector* Find(UWorld* World);
    virtual void Tick(float Delta) override;
protected:
    virtual void BeginPlay() override;
private:
    bool Attached=false;
    float NextAttackSlot=0.f;
    float NextSyncAt=0.f;
    FString LastStatus;
    void AttachPlayer();
    void ActivateGroup(int32 Group);
    void RestoreAt(float RouteProgress);
};
