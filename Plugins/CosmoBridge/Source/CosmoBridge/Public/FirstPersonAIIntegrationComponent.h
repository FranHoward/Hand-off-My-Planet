#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmoClientComponent.h"
#include "HAL/PlatformProcess.h"
#include "FirstPersonAIIntegrationComponent.generated.h"

class ACharacter;
class UGestureJsonReceiverComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UStaticMeshComponent;
class UAnimMontage;
class USkeletalMeshComponent;
class UCosmoHUDWidget;
class UCosmoRealtimeComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCosmoHUDChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCosmoAbilityConfirmed, FName, Action, bool, Success);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmoScanConfirmed, AActor*, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCosmoAnimationRequested, const FCosmoAnimationEvent&, Animation);

/** Gameplay implementation used by BP_FirstPersonCharacterAI. All gesture entry points are wired in its graph. */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class COSMOBRIDGE_API UFirstPersonAIIntegrationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFirstPersonAIIntegrationComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") float GestureTurnSpeed = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") bool AutoLaunchRecognition = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") int32 CameraIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") bool SwapHands = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Controls") bool InvertHeadTurn = false;
    UPROPERTY(BlueprintReadOnly, Category="Controls") FString GestureStatus = TEXT("Waiting for hand and face tracking");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial", meta=(ClampMin="1")) float MovementConfirmationDistance = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tutorial") bool AutoCompleteFinalStage = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO") bool ShowHUD = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Conversation") bool FreeConversation=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO") bool EnableLocalSpeech = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Realtime") bool EnableRealtimeVoice = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Realtime") FString RealtimeVoiceUrl = TEXT("ws://127.0.0.1:8766");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Realtime", meta=(ClampMin="0",ClampMax="1")) float VoiceVolume = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Realtime", meta=(ClampMin="0.5",ClampMax="2")) float VoicePitch = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Realtime") bool AllowVoiceInterruptions = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Camera") bool EnableCameraPreview = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Camera", meta=(ClampMin="1",ClampMax="65535")) int32 CameraPreviewPort = 8767;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Realtime") TObjectPtr<UCosmoRealtimeComponent> Realtime;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO") bool ResetTutorialOnStart = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO") TObjectPtr<USkeletalMeshComponent> CompanionMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO") TMap<FName, UAnimMontage*> CompanionMontages;

    // Initial tuning for the playable prototype; designers can change these on the Blueprint component.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="1")) float MaxHealth = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0")) float FireDamage = 25.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0")) float FireCooldown = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0")) float WaveDamage = 15.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0")) float WaveCooldown = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="1")) float WaveRadius = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0")) float WaveImpulse = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0")) float LaserDamagePerSecond = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="1")) float LaserRange = 6000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") float ShieldDrainPerSecond=24.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") float ShieldRegenPerSecond=18.f;
    UPROPERTY(BlueprintReadOnly, Category="Combat") float ShieldEnergy=100.f;
    UPROPERTY(BlueprintReadOnly, Category="State") bool Won=false;
    UPROPERTY(BlueprintReadOnly, Category="Mission") FString MissionObjective;
    UPROPERTY(BlueprintReadOnly, Category="Mission") FString EncounterStatus;
    UPROPERTY(BlueprintReadOnly, Category="Mission") int32 EnemyKills=0;
    UPROPERTY(BlueprintReadOnly, Category="Mission") int32 RemainingEnemies=0;
    UPROPERTY(BlueprintReadOnly, Category="Mission") float EncounterBossHealth=0.f;
    UPROPERTY(BlueprintReadOnly, Category="Mission") float EncounterBossMaxHealth=0.f;
    UPROPERTY(BlueprintReadOnly, Category="Mission") float MissionProgress=0.f;
    UPROPERTY(BlueprintReadOnly, Category="Mission") float StarCoreEnergyPercent=0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scanning", meta=(ClampMin="1")) float ScanRange = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scanning", meta=(ClampMin="0")) float ScanDuration = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scanning", meta=(ClampMin="1")) float BossEncounterDistance = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scanning") FName EnemyTag = TEXT("Enemy");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scanning") FName BossTag = TEXT("Boss");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects") TObjectPtr<UNiagaraSystem> FireEffect;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects") TObjectPtr<UNiagaraSystem> WaveEffect;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects") TObjectPtr<UNiagaraSystem> LaserEffect;

    UPROPERTY(BlueprintReadOnly, Category="State") bool Initialized = false;
    UPROPERTY(BlueprintReadOnly, Category="State") bool ShieldActive = false;
    UPROPERTY(BlueprintReadOnly, Category="State") bool LaserActive = false;
    UPROPERTY(BlueprintReadOnly, Category="State") bool FireActive = false;
    UPROPERTY(BlueprintReadOnly, Category="State") bool WaveActive = false;
    UPROPERTY(BlueprintReadOnly, Category="State") bool Dead = false;
    UPROPERTY(BlueprintReadOnly, Category="State") float Health = 100.f;
    UPROPERTY(BlueprintReadOnly, Category="State") float ScanProgress = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="State") bool ChatOpen = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString Subtitle;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString LastError;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString GuideProvider = TEXT("local");
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString GuideReplySource = TEXT("none");
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString AnimationState = TEXT("speaking");
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FString AnimationName = TEXT("talk_gesture");
    UPROPERTY(BlueprintReadOnly, Category="COSMO") FCosmoTutorialStatus TutorialStatus;
    UPROPERTY(BlueprintReadOnly, Category="COSMO") bool CosmoConnected = false;
    UPROPERTY(BlueprintReadOnly, Category="State") bool GestureConnected = false;
    UPROPERTY(BlueprintReadOnly, Category="State") TObjectPtr<AActor> ScannedTarget;
    UPROPERTY(BlueprintReadOnly, Category="State") TObjectPtr<AActor> CurrentBoss;

    UPROPERTY(BlueprintAssignable, Category="Events") FCosmoHUDChanged OnHUDChanged;
    UPROPERTY(BlueprintAssignable, Category="Events") FCosmoAbilityConfirmed OnAbilityConfirmed;
    UPROPERTY(BlueprintAssignable, Category="Events") FCosmoScanConfirmed OnScanConfirmed;
    UPROPERTY(BlueprintAssignable, Category="Events") FCosmoAnimationRequested OnCompanionAnimationRequested;
    UPROPERTY(BlueprintAssignable, Category="COSMO|Realtime") FCosmoSpokenLineDelegate OnRealtimeSpokenLine;

    UFUNCTION(BlueprintCallable, Category="Integration") void InitializeIntegration();
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleGestureAxes(float MoveForward, float TurnRight);
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleGesturePressed(FName Action, FName KeyName, int64 EventId);
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleGestureHeld(FName Action, FName KeyName, int64 EventId);
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleGestureReleased(FName Action, FName KeyName, int64 EventId);
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleGestureConnection(bool Connected);
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleCosmoConnection(bool Connected);
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleCosmoResponse(const FCosmoResponse& Response);
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleSpokenLine(const FString& SpokenLine);
    UFUNCTION(BlueprintCallable, Category="Integration") void HandleAnimation(const FCosmoAnimationEvent& AnimationEvent);
    UFUNCTION(BlueprintCallable, Category="Abilities") bool TryActivateShield();
    UFUNCTION(BlueprintCallable, Category="Abilities") bool TryActivateFire();
    UFUNCTION(BlueprintCallable, Category="Abilities") bool TryActivateWave();
    UFUNCTION(BlueprintCallable, Category="Abilities") bool TryActivateLaser();
    UFUNCTION(BlueprintCallable, Category="Abilities") void DeactivateShield();
    UFUNCTION(BlueprintCallable, Category="Abilities") void DeactivateLaser();
    UFUNCTION(BlueprintCallable, Category="Abilities") void ReleaseAllAbilities();
    UFUNCTION(BlueprintCallable, Category="Scanning") bool ScanEnemy();
    UFUNCTION(BlueprintCallable, Category="Tutorial") bool ConfirmBossEncounter(AActor* Boss);
    UFUNCTION(BlueprintCallable, Category="Tutorial") bool CompleteTutorial();
    UFUNCTION(BlueprintCallable, Category="Tutorial") bool TrySendCosmoEvent(ECosmoGameEvent Event);
    UFUNCTION(BlueprintCallable, Category="Voice") bool SubmitVoiceTranscript(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="Voice") void SetVoiceListening(bool Listening);
    /** Display a Grok audio transcript without synthesizing it a second time. */
    UFUNCTION(BlueprintCallable, Category="Voice") void ShowRealtimeLine(const FString& Line);
    /** Authored location/tutorial lines always preempt realtime conversation. */
    UFUNCTION(BlueprintCallable, Category="Tutorial") void PlayTutorialLine(const FString& Line);
    void ShowTutorialLine(const FString& Line);
    void BeginLocalTutorialSpeech(const FString& Line);
    bool LocalTutorialSpeechPlaying() const;
    void StopLocalTutorialSpeech();
    UFUNCTION(BlueprintCallable, Category="Voice") void SetMicrophoneMuted(bool Muted);
    UFUNCTION() void HandleRealtimeUpdated();
    bool CanStartAmbientConversation() const { return Initialized && !InDanger && !Dead && !Won && !ChatOpen; }
    UFUNCTION(BlueprintCallable, Category="Voice") void RequestHint();
    UFUNCTION(BlueprintCallable, Category="Voice") void RepeatInstruction();
    UFUNCTION(BlueprintCallable, Category="Combat") void SetDanger(bool InDanger);
    UFUNCTION(BlueprintCallable, Category="COSMO") bool SynchronizeGameState();
    UFUNCTION(BlueprintPure, Category="Combat") bool IsAbilityAvailable(FName Action) const;
    UFUNCTION(BlueprintPure, Category="Abilities") float GetCooldownRemaining(FName Action) const;
    UFUNCTION(BlueprintCallable, Category="Combat") float RestoreHealth(float Amount);
    UFUNCTION(BlueprintCallable, Category="Mission") void RestoreForCheckpoint();
    UFUNCTION(BlueprintCallable, Category="Mission") void RetryFromCheckpoint();
    UFUNCTION(BlueprintCallable, Category="Mission") void RefreshGameplayHUD();

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TObjectPtr<ACharacter> Character;
    UPROPERTY(Transient) TObjectPtr<UGestureJsonReceiverComponent> Gestures;
    UPROPERTY(Transient) TObjectPtr<UCosmoClientComponent> Cosmo;
    UPROPERTY(Transient) TObjectPtr<UCosmoHUDWidget> HUD;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> LaserVFX;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LaserBeam;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ShieldVisual;
    TWeakObjectPtr<AActor> ScanTarget;
    TSet<FName> GestureHeldActions;
    FProcHandle RecognitionProcess;
    float PreviousGestureMove = 0.f;
    FVector LastMovementPosition = FVector::ZeroVector;
    float MovementDistance = 0.f;
    float ScanElapsed = 0.f;
    double FireReadyAt = 0;
    double WaveReadyAt = 0;
    double NextConnectionCheck = 0;
    double NextWorldCheck = 0;
    double LastDamageWarning = -10;
    double SubtitleExpiresAt = 0;
    double ShieldReadyAt=0;
    double ShieldRegenAt=0;
    double DamageImmuneUntil=0;
    double NextResourceHUD=0;
    FString PendingTutorialEvent;
    bool SessionReady = false;
    bool InDanger = false;
    bool Scanning = false;
    bool HadFocus = true;
    FName SpeechChannel;
    bool SpeechReady = false;

    bool CanAct() const;
    bool AimRay(FVector& Origin, FVector& Direction, bool UsePointer = false) const;
    bool IsEnemy(AActor* Actor) const;
    void UpdateLaser(float DeltaTime);
    void UpdateScanning(float DeltaTime);
    void ConfigureGestureOnlyInput();
    void StartRecognition();
    void ConfirmAbility(FName Action);
    void NotifyHUD();
    void SyncLegacyBlueprintState();
    UFUNCTION() void HandleDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType, class AController* InstigatedBy, AActor* DamageCauser);
};
