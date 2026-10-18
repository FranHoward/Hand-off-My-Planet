#include "FirstPersonAIIntegrationComponent.h"
#include "CosmoFireProjectile.h"
#include "CosmoPlanetGame.h"
#include "CosmoHUDWidget.h"
#include "CosmoGameHUD.h"
#include "CosmoRealtimeComponent.h"
#include "CosmoRuntimePaths.h"
#include "GestureJsonReceiverComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Misc/Paths.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "AudioDevice.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "TextToSpeechEngineSubsystem.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

UFirstPersonAIIntegrationComponent::UFirstPersonAIIntegrationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UFirstPersonAIIntegrationComponent::InitializeIntegration()
{
    if (Initialized) return;
    Character = Cast<ACharacter>(GetOwner());
    if (!Character || !Character->IsLocallyControlled()) { SetComponentTickEnabled(false); return; }
    Gestures = Character->FindComponentByClass<UGestureJsonReceiverComponent>();
    Cosmo = Character->FindComponentByClass<UCosmoClientComponent>();
    if (!Cosmo || !Gestures) { LastError = TEXT("Character requires CosmoClient and GestureJsonReceiver components."); return; }
    Initialized = true;
    // Opening instruction belongs to the menu; the mission keeps Grok in free conversation.
    if(ACosmoPlanetDirector::Find(GetWorld())) FreeConversation=true;
#if WITH_EDITOR
    if (FParse::Param(FCommandLine::Get(), TEXT("CharacterAIPreview"))) Cosmo->BaseUrl = TEXT("http://127.0.0.1:18765");
    if(FParse::Param(FCommandLine::Get(),TEXT("PlanetPreview"))) { ShowHUD=false; EnableLocalSpeech=false; }
    if(FParse::Param(FCommandLine::Get(),TEXT("PlanetHUDPreview"))) EnableLocalSpeech=false;
    if(FParse::Param(FCommandLine::Get(),TEXT("PlanetPreview")) || FParse::Param(FCommandLine::Get(),TEXT("PlanetHUDPreview")))
    { Cosmo->BaseUrl=TEXT("http://127.0.0.1:18996"); Gestures->StopListening(); }
    if(FParse::Param(FCommandLine::Get(),TEXT("RealtimeAVPreview")))
    { AutoLaunchRecognition=false; EnableLocalSpeech=false; RealtimeVoiceUrl=TEXT("ws://127.0.0.1:18766"); CameraPreviewPort=18767; }
#endif
    Health = MaxHealth;
    LastMovementPosition = Character->GetActorLocation();
    Character->OnTakeAnyDamage.AddUniqueDynamic(this, &UFirstPersonAIIntegrationComponent::HandleDamage);
    SpeechChannel = FName(*FString::Printf(TEXT("COSMO_%u"), GetUniqueID()));
    if (EnableLocalSpeech && GEngine && GEngine->UseSound() && GEngine->GetMainAudioDeviceRaw())
        if (auto* Speech = GEngine->GetEngineSubsystem<UTextToSpeechEngineSubsystem>())
        { Speech->AddDefaultChannel(SpeechChannel); Speech->ActivateChannel(SpeechChannel); SpeechReady = true; }
    ConfigureGestureOnlyInput();
    Realtime=Character->FindComponentByClass<UCosmoRealtimeComponent>();
    if (!Realtime)
    {
        Realtime=NewObject<UCosmoRealtimeComponent>(Character,TEXT("CosmoRealtime"));
        Character->AddInstanceComponent(Realtime); Realtime->RegisterComponent();
    }
    Realtime->FreeConversation=FreeConversation;
    Realtime->EnableVoice=EnableRealtimeVoice; Realtime->EnableCameraPreview=EnableCameraPreview;
    Realtime->VoiceUrl=RealtimeVoiceUrl;
    Realtime->CameraUrl=FString::Printf(TEXT("http://127.0.0.1:%d/frame.jpg"),CameraPreviewPort);
    Realtime->VoiceVolume=VoiceVolume; Realtime->VoicePitch=VoicePitch;
    Realtime->AllowVoiceInterruptions=AllowVoiceInterruptions;
    Realtime->OnUpdated.AddUniqueDynamic(this,&UFirstPersonAIIntegrationComponent::HandleRealtimeUpdated);
    Realtime->Start();
    StartRecognition();
    if (ShowHUD)
        if (auto* PC = Cast<APlayerController>(Character->GetController()))
        {
            auto* HUDClass=LoadClass<AHUD>(nullptr,TEXT("/Game/UI/HUD/BP_PlanetHUD.BP_PlanetHUD_C"));
            PC->ClientSetHUD(HUDClass ? HUDClass : ACosmoGameHUD::StaticClass());
            if(auto* GameHUD=Cast<ACosmoGameHUD>(PC->GetHUD())) GameHUD->AttachIntegration(this);
        }
    CosmoConnected = Cosmo->Connected;
    GestureConnected = Gestures->Connected;
    // Reset only this local prototype's session, once per Play. Reconnection never resets progress.
    if (ResetTutorialOnStart) Cosmo->ResetSession();
    else Cosmo->GetSnapshot();
    NextConnectionCheck = GetWorld()->GetTimeSeconds() + 3.;
    NotifyHUD();
}

bool UFirstPersonAIIntegrationComponent::CanAct() const
{
    return Initialized && IsValid(Character) && !Dead && !Won && !ChatOpen && !UGameplayStatics::IsGamePaused(this) && HadFocus && Character->IsLocallyControlled();
}

void UFirstPersonAIIntegrationComponent::HandleGestureAxes(float MoveForward, float TurnRight)
{
    if (!CanAct()) return;
    if (FMath::IsNearlyZero(MoveForward) && !FMath::IsNearlyZero(PreviousGestureMove))
        Character->GetCharacterMovement()->StopMovementImmediately();
    PreviousGestureMove = MoveForward;
    const FRotator Yaw(0, Character->GetControlRotation().Yaw, 0);
    Character->AddMovementInput(Yaw.Vector(), FMath::Clamp(MoveForward, -1.f, 1.f));
    // Change control rotation in degrees/sec, independently of the legacy InputYawScale.
    if (auto* PC = Cast<APlayerController>(Character->GetController()))
    {
        auto Rotation = PC->GetControlRotation();
        Rotation.Yaw += FMath::Clamp(TurnRight, -1.f, 1.f) * GestureTurnSpeed * GetWorld()->GetDeltaSeconds();
        PC->SetControlRotation(Rotation);
    }
}

void UFirstPersonAIIntegrationComponent::HandleGesturePressed(FName Action, FName, int64)
{
    // Results must remain operable after CanAct() becomes false on death/victory.
    if (Action == TEXT("Retry"))
    {
        if (Initialized && HadFocus && (Dead || Won)) RetryFromCheckpoint();
        return;
    }
    if (!CanAct()) return;
    if (Action == TEXT("Shield")) { GestureHeldActions.Add(Action); TryActivateShield(); }
    else if (Action == TEXT("Laser")) { GestureHeldActions.Add(Action); TryActivateLaser(); }
    else if (Action == TEXT("Fire")) TryActivateFire();
    else if (Action == TEXT("Wave")) TryActivateWave();
    else if (Action == TEXT("Scan")) ScanEnemy();
    else if (!FreeConversation && Action == TEXT("Hint")) RequestHint();
    else if (!FreeConversation && Action == TEXT("Repeat")) RepeatInstruction();
}

void UFirstPersonAIIntegrationComponent::HandleGestureHeld(FName Action, FName, int64)
{
    // The component Tick updates sustained effects exactly once per frame. Never spawn from Held.
    if (Action == TEXT("Shield") || Action == TEXT("Laser"))
        if (CanAct()) GestureHeldActions.Add(Action);
}

void UFirstPersonAIIntegrationComponent::HandleGestureReleased(FName Action, FName, int64)
{
    GestureHeldActions.Remove(Action);
    if (Action == TEXT("Shield")) DeactivateShield();
    if (Action == TEXT("Laser")) DeactivateLaser();
}

void UFirstPersonAIIntegrationComponent::HandleGestureConnection(bool Connected)
{
    GestureConnected = Connected;
    if (!Connected)
    {
        ReleaseAllAbilities();
    }
    NotifyHUD();
}

bool UFirstPersonAIIntegrationComponent::AimRay(FVector& Origin, FVector& Direction, bool UsePointer) const
{
    if (!Character) return false;
    auto* PC = Cast<APlayerController>(Character->GetController());
    if (!PC) return false;
    FRotator Rotation;
    PC->GetPlayerViewPoint(Origin, Rotation);
    Direction = Rotation.Vector();
    if (UsePointer && Gestures && Gestures->Connected && Gestures->LatestFrame.HasPointer)
    {
        int32 Width = 0, Height = 0; PC->GetViewportSize(Width, Height);
        const auto Pointer = Gestures->LatestFrame.Pointer;
        // Stretch the normalized mirrored camera coordinates to match the viewport.
        if (Width > 0 && Height > 0)
            return PC->DeprojectScreenPositionToWorld(FMath::Clamp(Pointer.X, 0., 1.) * Width,
                FMath::Clamp(Pointer.Y, 0., 1.) * Height, Origin, Direction);
    }
    return true;
}

float UFirstPersonAIIntegrationComponent::GetCooldownRemaining(FName Action) const
{
    if (!GetWorld()) return 0;
    const double ReadyAt = Action == TEXT("Fire") ? FireReadyAt : Action == TEXT("Wave") ? WaveReadyAt : Action==TEXT("Shield") ? ShieldReadyAt : 0.;
    return FMath::Max(0., ReadyAt - GetWorld()->GetTimeSeconds());
}

bool UFirstPersonAIIntegrationComponent::IsAbilityAvailable(FName Action) const
{
    const bool Known = Action == TEXT("Shield") || Action == TEXT("Fire") || Action == TEXT("Wave") || Action == TEXT("Laser");
    return Known && !Dead && !Won && GetCooldownRemaining(Action) <= 0.f && (Action!=TEXT("Shield") || ShieldEnergy>=15.f);
}

static UStaticMeshComponent* MakeVisual(AActor* Owner, FName Name, const TCHAR* MeshPath, FLinearColor Color)
{
    auto* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
    if (!Mesh) return nullptr;
    auto* Visual = NewObject<UStaticMeshComponent>(Owner, Name);
    Owner->AddInstanceComponent(Visual);
    Visual->SetStaticMesh(Mesh);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetCastShadow(false);
    Visual->RegisterComponent();
    if (auto* Material = Visual->CreateAndSetMaterialInstanceDynamic(0)) Material->SetVectorParameterValue(TEXT("Color"), Color);
    return Visual;
}

bool UFirstPersonAIIntegrationComponent::TryActivateShield()
{
    if (!CanAct() || ShieldActive || !IsAbilityAvailable(TEXT("Shield"))) return false;
    if (!ShieldVisual)
    {
        ShieldVisual = MakeVisual(Character, TEXT("CosmoShield"), TEXT("/Game/Props/ShieldHealth/Meshes/SM_EnergyShield.SM_EnergyShield"), FLinearColor(.05f,.6f,1.f));
        if (!ShieldVisual) { LastError = TEXT("Shield visual could not be created."); NotifyHUD(); return false; }
        USceneComponent* Attach = Character->FindComponentByClass<UCameraComponent>();
        ShieldVisual->AttachToComponent(Attach ? Attach : Character->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        // The FBX imports facing +Y; turn its front away from the camera toward +X.
        ShieldVisual->SetRelativeLocation(FVector(125, -35, -45));
        ShieldVisual->SetRelativeRotation(FRotator(0, -90, 0));
        ShieldVisual->SetRelativeScale3D(FVector(.95f));
    }
    DeactivateLaser();
    ShieldActive = true;
    ShieldVisual->SetVisibility(true);
    ConfirmAbility(TEXT("Shield"));
    return true;
}

bool UFirstPersonAIIntegrationComponent::TryActivateFire()
{
    if (!CanAct() || !IsAbilityAvailable(TEXT("Fire"))) return false;
    FVector Origin, Direction;
    if (!AimRay(Origin, Direction) || !FireEffect) { LastError = TEXT("Fire effect is not configured."); NotifyHUD(); return false; }
    FTransform Transform(Direction.Rotation(), Origin + Direction * 100.f);
    auto* Projectile = GetWorld()->SpawnActorDeferred<ACosmoFireProjectile>(ACosmoFireProjectile::StaticClass(), Transform,
        Character, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Projectile) return false;
    Projectile->Damage = FireDamage;
    Projectile->Effect->SetAsset(FireEffect);
    UGameplayStatics::FinishSpawningActor(Projectile, Transform);
    if (!IsValid(Projectile)) return false;
    DeactivateShield();
    FireReadyAt = GetWorld()->GetTimeSeconds() + FireCooldown;
    FireActive = FireCooldown > 0.f;
    ConfirmAbility(TEXT("Fire"));
    return true;
}

bool UFirstPersonAIIntegrationComponent::TryActivateWave()
{
    if (!CanAct() || !IsAbilityAvailable(TEXT("Wave"))) return false;
    FVector Origin, Direction;
    if (!AimRay(Origin, Direction) || !WaveEffect) { LastError = TEXT("Wave effect is not configured."); NotifyHUD(); return false; }
    auto* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, WaveEffect, Origin + Direction * 120.f, Direction.Rotation(), FVector(1.f), true, true, ENCPoolMethod::None, false);
    if (!Effect && FApp::CanEverRender()) return false;
    // Explicit cleanup also handles source assets authored with an infinite emitter loop.
    FTimerHandle Cleanup;
    if (Effect) GetWorld()->GetTimerManager().SetTimer(Cleanup, FTimerDelegate::CreateWeakLambda(Effect, [Effect]() { Effect->DestroyComponent(); }), 2.f, false);
    TArray<FOverlapResult> Hits;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CosmoWave), false, Character);
    GetWorld()->OverlapMultiByObjectType(Hits, Character->GetActorLocation(), FQuat::Identity,
        FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllDynamicObjects), FCollisionShape::MakeSphere(WaveRadius), Query);
    TSet<AActor*> Damaged;
    for (const auto& Hit : Hits)
    {
        auto* Actor = Hit.GetActor(); auto* Primitive = Hit.GetComponent();
        if (!IsValid(Actor) || Actor == Character) continue;
        const FVector Push = (Actor->GetActorLocation() - Character->GetActorLocation()).GetSafeNormal();
        if (FVector::DotProduct(Push, Direction) < 0.f) continue;
        FHitResult Occlusion;
        if (GetWorld()->LineTraceSingleByChannel(Occlusion, Origin, Actor->GetActorLocation(), ECC_Visibility, Query) && Occlusion.GetActor() != Actor) continue;
        if (Primitive && Primitive->IsSimulatingPhysics()) Primitive->AddImpulse(Push * WaveImpulse, NAME_None, true);
        if (!Damaged.Contains(Actor))
        {
            Damaged.Add(Actor);
            UGameplayStatics::ApplyDamage(Actor, WaveDamage, Character->GetController(), Character, UCosmoWaveDamage::StaticClass());
            auto* Enemy=Cast<ACosmoPlanetEnemy>(Actor);
            if(!Enemy || (!Enemy->IsBoss() && !Enemy->bDead && !Enemy->Passive))
                if (auto* OtherCharacter = Cast<ACharacter>(Actor)) OtherCharacter->LaunchCharacter(Push * WaveImpulse + FVector(0,0,120), true, true);
        }
    }
    DeactivateShield();
    WaveReadyAt = GetWorld()->GetTimeSeconds() + WaveCooldown;
    WaveActive = WaveCooldown > 0.f;
    ConfirmAbility(TEXT("Wave"));
    return true;
}

bool UFirstPersonAIIntegrationComponent::TryActivateLaser()
{
    if (!CanAct() || LaserActive) return false;
    DeactivateShield();
    if (!LaserBeam) LaserBeam = MakeVisual(Character, TEXT("CosmoLaserBeam"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(1.f,.05f,.02f));
    if (!LaserBeam) return false;
    if (!LaserVFX && LaserEffect)
        LaserVFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, LaserEffect, Character->GetActorLocation(), FRotator::ZeroRotator,
            FVector(1), false, false, ENCPoolMethod::None, false);
    LaserActive = true;
    LaserBeam->SetVisibility(true);
    if (LaserVFX) LaserVFX->Activate(true);
    UpdateLaser(0.f);
    ConfirmAbility(TEXT("Laser"));
    return true;
}

void UFirstPersonAIIntegrationComponent::UpdateLaser(float DeltaTime)
{
    if (!LaserActive || !Character) return;
    FVector Origin, Direction;
    if (!AimRay(Origin, Direction, GestureHeldActions.Contains(TEXT("Laser")))) { DeactivateLaser(); return; }
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CosmoLaser), true, Character);
    FVector End = Origin + Direction * LaserRange;
    if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Query))
    {
        End = Hit.ImpactPoint;
        if (Hit.GetActor() && DeltaTime > 0.f)
            UGameplayStatics::ApplyPointDamage(Hit.GetActor(), LaserDamagePerSecond * DeltaTime, Direction, Hit,
                Character->GetController(), Character, UCosmoLaserDamage::StaticClass());
    }
    const FVector Start = Origin + Direction * 70.f + FVector(0,0,-8);
    if (LaserBeam)
    {
        LaserBeam->SetWorldLocation((Start + End) * .5f);
        LaserBeam->SetWorldRotation(FRotationMatrix::MakeFromZ(End - Start).Rotator());
        LaserBeam->SetWorldScale3D(FVector(.025f,.025f,FVector::Distance(Start,End) / 100.f));
    }
    if (LaserVFX)
    {
        LaserVFX->SetWorldLocation(Start);
        LaserVFX->SetWorldRotation(Direction.Rotation());
        LaserVFX->SetVariablePosition(TEXT("User.BeamStart"), Start);
        LaserVFX->SetVariablePosition(TEXT("User.BeamEnd"), End);
        LaserVFX->SetVariablePosition(TEXT("User.Target"), End);
    }
}

void UFirstPersonAIIntegrationComponent::DeactivateShield()
{
    const bool WasActive = ShieldActive;
    ShieldActive = false;
    if(WasActive && GetWorld()) ShieldRegenAt=GetWorld()->GetTimeSeconds()+1.f;
    if (ShieldVisual) ShieldVisual->SetVisibility(false);
    if (WasActive) SynchronizeGameState();
    NotifyHUD();
}

void UFirstPersonAIIntegrationComponent::DeactivateLaser()
{
    LaserActive = false;
    if (LaserBeam) LaserBeam->SetVisibility(false);
    if (LaserVFX) LaserVFX->DeactivateImmediate();
    NotifyHUD();
}

void UFirstPersonAIIntegrationComponent::ReleaseAllAbilities()
{
    GestureHeldActions.Reset();
    PreviousGestureMove = 0.f;
    if (Character) Character->GetCharacterMovement()->StopMovementImmediately();
    DeactivateShield(); DeactivateLaser();
    Scanning = false; ScanProgress = 0; ScanElapsed = 0; ScanTarget.Reset();
    if (Gestures && Initialized) Gestures->SetComponentTickEnabled(true);
}

void UFirstPersonAIIntegrationComponent::ConfirmAbility(FName Action)
{
    LastError.Reset();
    SynchronizeGameState();
    if (!FreeConversation && Cosmo && SessionReady) Cosmo->ReportConfirmedAbility(Action);
    OnAbilityConfirmed.Broadcast(Action, true);
    NotifyHUD();
}

bool UFirstPersonAIIntegrationComponent::IsEnemy(AActor* Actor) const
{
    if (!IsValid(Actor) || Actor == Character) return false;
    const auto* Pawn = Cast<APawn>(Actor);
    return Actor->ActorHasTag(EnemyTag) || Actor->ActorHasTag(BossTag) || Actor->ActorHasTag(TEXT("Scannable")) || (Pawn && !Pawn->IsPlayerControlled());
}

bool UFirstPersonAIIntegrationComponent::ScanEnemy()
{
    if (!CanAct() || Scanning) return false;
    FVector Origin, Direction;
    if (!AimRay(Origin, Direction)) return false;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CosmoScan), true, Character);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * ScanRange, ECC_Visibility, Query) || !IsEnemy(Hit.GetActor()))
    { LastError = TEXT("Aim at a nearby enemy, then hold your right index finger up for 0.6s."); NotifyHUD(); return false; }
    ScanTarget = Hit.GetActor(); ScanElapsed = 0; ScanProgress = 0; Scanning = true;
    SetVoiceListening(false);
    AnimationState = TEXT("scanning"); AnimationName = TEXT("scan_pulse");
    LastError = TEXT("Scanning: keep the target in view."); NotifyHUD();
    return true;
}

void UFirstPersonAIIntegrationComponent::UpdateScanning(float DeltaTime)
{
    if (!Scanning) return;
    FVector Origin, Direction;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CosmoScan), true, Character);
    if (!CanAct() || !ScanTarget.IsValid() || !AimRay(Origin, Direction) ||
        !GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * ScanRange, ECC_Visibility, Query) || Hit.GetActor() != ScanTarget.Get())
    {
        Scanning = false; ScanProgress = 0; ScanElapsed = 0; ScanTarget.Reset();
        LastError = TEXT("Scan interrupted. Aim at the enemy, release and repeat the pointing gesture."); NotifyHUD(); return;
    }
    ScanElapsed += DeltaTime;
    ScanProgress = ScanDuration > 0 ? FMath::Clamp(ScanElapsed / ScanDuration, 0.f, 1.f) : 1.f;
    if (ScanProgress >= 1.f)
    {
        ScannedTarget = ScanTarget.Get(); Scanning = false;
        LastError = FString::Printf(TEXT("Scan confirmed: %s"), *ScannedTarget->GetName());
        SynchronizeGameState();
        if (Cosmo && SessionReady) Cosmo->SendGameEvent(ECosmoGameEvent::EnemyScanned);
        OnScanConfirmed.Broadcast(ScannedTarget);
    }
    NotifyHUD();
}

bool UFirstPersonAIIntegrationComponent::ConfirmBossEncounter(AActor* Boss)
{
    if (!CanAct() || !IsValid(Boss) || !Boss->ActorHasTag(BossTag)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CosmoBoss), false, Character);
    FHitResult Hit;
    if (FVector::Dist(Character->GetActorLocation(), Boss->GetActorLocation()) > BossEncounterDistance) return false;
    if (GetWorld()->LineTraceSingleByChannel(Hit, Character->GetPawnViewLocation(), Boss->GetActorLocation(), ECC_Visibility, Query) && Hit.GetActor() != Boss) return false;
    CurrentBoss = Boss;
    SynchronizeGameState();
    return TrySendCosmoEvent(ECosmoGameEvent::BossEncountered);
}

bool UFirstPersonAIIntegrationComponent::CompleteTutorial()
{
    if (!CurrentBoss || !ScannedTarget || TutorialStatus.CurrentStageId != TEXT("complete")) return false;
    return TrySendCosmoEvent(ECosmoGameEvent::TutorialCompleted);
}

bool UFirstPersonAIIntegrationComponent::TrySendCosmoEvent(ECosmoGameEvent Event)
{
    if (FreeConversation || !Cosmo || !SessionReady || !CosmoConnected || !PendingTutorialEvent.IsEmpty()) return false;
    const auto Name = UCosmoClientComponent::GameEventToString(Event);
    if (TutorialStatus.IsComplete || TutorialStatus.ExpectedEvent != Name) return false;
    if (Event == ECosmoGameEvent::MovementCompleted && MovementDistance < MovementConfirmationDistance) return false;
    if (Event == ECosmoGameEvent::EnemyScanned && !IsValid(ScannedTarget)) return false;
    if (Event == ECosmoGameEvent::BossEncountered && !IsValid(CurrentBoss)) return false;
    if (Event == ECosmoGameEvent::TutorialCompleted && (!IsValid(CurrentBoss) || !IsValid(ScannedTarget))) return false;
    // Combat confirmations are emitted only inside successful activation functions.
    if (Event == ECosmoGameEvent::ShieldActivated || Event == ECosmoGameEvent::FireActivated ||
        Event == ECosmoGameEvent::WaveActivated || Event == ECosmoGameEvent::LaserActivated) return false;
    PendingTutorialEvent = Name;
    if (!Cosmo->SendGameEvent(Event)) { PendingTutorialEvent.Reset(); return false; }
    return true;
}

void UFirstPersonAIIntegrationComponent::HandleCosmoConnection(bool Connected)
{
    CosmoConnected = Connected;
    if (!Connected) { LastError = Cosmo ? Cosmo->LastError : TEXT("COSMO disconnected."); PendingTutorialEvent.Reset(); }
    NotifyHUD();
}

void UFirstPersonAIIntegrationComponent::HandleCosmoResponse(const FCosmoResponse& Response)
{
    if (Response.Endpoint == TEXT("/events")) PendingTutorialEvent.Reset();
    if (!Response.Success) { LastError = Response.Error; NotifyHUD(); return; }
    if (!(Realtime && Realtime->TutorialActive))
    {
        if (!Response.Provider.IsEmpty()) GuideProvider = Response.Provider;
        if (!Response.Source.IsEmpty()) GuideReplySource = Response.Source;
    }
    if (Response.Endpoint == TEXT("/reset") || Response.Endpoint == TEXT("/snapshot")) SessionReady = true;
    if (Response.HasTutorialStatus)
    {
        if (Response.TutorialStatus.CurrentStageId != TutorialStatus.CurrentStageId)
        { MovementDistance = 0; LastMovementPosition = Character->GetActorLocation(); }
        TutorialStatus = Response.TutorialStatus;
    }
    LastError = Response.ProviderError;
    if (Response.Endpoint == TEXT("/reset") || Response.Endpoint == TEXT("/snapshot"))
    {
        SynchronizeGameState();
        if(!FreeConversation) Cosmo->SendPlayerUtterance(TEXT("What should I do next?"));
    }
    NotifyHUD();
}

void UFirstPersonAIIntegrationComponent::HandleSpokenLine(const FString& SpokenLine)
{
    const bool ChatReply=Cosmo && (Cosmo->LastResponse.Endpoint==TEXT("/player/utterance") || Cosmo->LastResponse.Endpoint==TEXT("/voice"));
    if(!ChatReply) { if(!FreeConversation) PlayTutorialLine(SpokenLine); return; }
    if(FreeConversation) return; // Free chat is spoken only by the realtime Grok stream.
    if(Realtime && Realtime->TutorialActive) return;
    Subtitle = SpokenLine;
    SubtitleExpiresAt = GetWorld()->GetTimeSeconds() + FMath::Clamp(SpokenLine.Len() / 12.f, 5.f, 25.f);
    if (EnableLocalSpeech && SpeechReady && GEngine && GEngine->UseSound() && GEngine->GetMainAudioDeviceRaw())
        if (auto* Speech = GEngine->GetEngineSubsystem<UTextToSpeechEngineSubsystem>()) Speech->SpeakOnChannel(SpeechChannel, SpokenLine);
    NotifyHUD();
}

void UFirstPersonAIIntegrationComponent::PlayTutorialLine(const FString& Line)
{
    if(FreeConversation || Line.TrimStartAndEnd().IsEmpty()) return;
    if(Realtime && Realtime->SayLine(Line)) return;
    ShowTutorialLine(Line); BeginLocalTutorialSpeech(Line);
}
void UFirstPersonAIIntegrationComponent::ShowTutorialLine(const FString& Line)
{
    Subtitle=Line; SubtitleExpiresAt=GetWorld()->GetTimeSeconds()+FMath::Clamp(Line.Len()/12.f,5.f,25.f);
    GuideReplySource=TEXT("scripted");
    FCosmoAnimationEvent Event; Event.State=TEXT("speaking"); Event.Animation=TEXT("talk_gesture");
    Event.Reason=TEXT("tutorial_priority"); Event.Line=Line; HandleAnimation(Event);
    OnRealtimeSpokenLine.Broadcast(Line); NotifyHUD();
}
void UFirstPersonAIIntegrationComponent::BeginLocalTutorialSpeech(const FString& Line)
{
    if(EnableLocalSpeech && SpeechReady && GEngine && GEngine->UseSound() && GEngine->GetMainAudioDeviceRaw())
        if(auto* Speech=GEngine->GetEngineSubsystem<UTextToSpeechEngineSubsystem>())
        { Speech->StopSpeakingOnChannel(SpeechChannel); Speech->SpeakOnChannel(SpeechChannel,Line); }
}
bool UFirstPersonAIIntegrationComponent::LocalTutorialSpeechPlaying() const
{
    if(SpeechReady && GEngine)
        if(auto* Speech=GEngine->GetEngineSubsystem<UTextToSpeechEngineSubsystem>()) return Speech->IsSpeakingOnChannel(SpeechChannel);
    return false;
}
void UFirstPersonAIIntegrationComponent::StopLocalTutorialSpeech()
{
    if(SpeechReady && GEngine)
        if(auto* Speech=GEngine->GetEngineSubsystem<UTextToSpeechEngineSubsystem>()) Speech->StopSpeakingOnChannel(SpeechChannel);
}

void UFirstPersonAIIntegrationComponent::ShowRealtimeLine(const FString& Line)
{
    if(Realtime && Realtime->TutorialActive) return;
    Subtitle=Line;
    SubtitleExpiresAt=GetWorld()->GetTimeSeconds()+FMath::Clamp(Line.Len()/12.f,5.f,25.f);
    GuideReplySource=TEXT("grok"); GuideProvider=TEXT("xai");
    FCosmoAnimationEvent Event; Event.State=TEXT("speaking"); Event.Animation=TEXT("talk_gesture");
    Event.Reason=TEXT("grok_realtime"); Event.Line=Line; HandleAnimation(Event);
    OnRealtimeSpokenLine.Broadcast(Line);
    NotifyHUD();
}
void UFirstPersonAIIntegrationComponent::SetMicrophoneMuted(bool Muted)
{ if (Realtime) Realtime->SetMicrophoneMuted(Muted); }
void UFirstPersonAIIntegrationComponent::HandleRealtimeUpdated()
{
    if (Realtime && Realtime->VoiceReady && !Realtime->TutorialLocalSpeech && SpeechReady && GEngine)
        if (auto* Speech=GEngine->GetEngineSubsystem<UTextToSpeechEngineSubsystem>()) Speech->StopSpeakingOnChannel(SpeechChannel);
    NotifyHUD();
}

void UFirstPersonAIIntegrationComponent::HandleAnimation(const FCosmoAnimationEvent& AnimationEvent)
{
    if(Realtime && Realtime->TutorialActive && (AnimationEvent.Reason==TEXT("player_voice") || AnimationEvent.Reason==TEXT("grok_realtime"))) return;
    AnimationState = AnimationEvent.State; AnimationName = AnimationEvent.Animation;
    if (IsValid(CompanionMesh))
        if (UAnimMontage** Montage = CompanionMontages.Find(FName(*AnimationEvent.Animation)))
            if (*Montage && CompanionMesh->GetAnimInstance()) CompanionMesh->GetAnimInstance()->Montage_Play(*Montage);
    OnCompanionAnimationRequested.Broadcast(AnimationEvent);
    NotifyHUD();
}

void UFirstPersonAIIntegrationComponent::SetVoiceListening(bool Listening)
{
    if(Realtime && Realtime->TutorialActive) return;
    FCosmoAnimationEvent Event;
    Event.State = Listening ? TEXT("listening") : TEXT("speaking");
    Event.Animation = Listening ? TEXT("listen_lean_in") : TEXT("talk_gesture");
    Event.Reason = TEXT("player_voice"); HandleAnimation(Event);
    if (Listening && SpeechReady && GEngine)
        if (auto* Speech = GEngine->GetEngineSubsystem<UTextToSpeechEngineSubsystem>()) Speech->StopSpeakingOnChannel(SpeechChannel);
}

bool UFirstPersonAIIntegrationComponent::SubmitVoiceTranscript(const FString& Text)
{
    if(Realtime && Realtime->TutorialActive) return false;
    SetVoiceListening(false);
    if (Realtime && Realtime->AskText(Text)) return true;
    if(FreeConversation) { LastError=TEXT("Grok voice is reconnecting. Please try again shortly."); NotifyHUD(); return false; }
    if (!Cosmo || !CosmoConnected || !SessionReady) { LastError = TEXT("COSMO is reconnecting. Try again when connected."); NotifyHUD(); return false; }
    const bool Sent = Cosmo->SendPlayerUtterance(Text);
    if (!Sent) { LastError = Cosmo->LastError; NotifyHUD(); }
    return Sent;
}

void UFirstPersonAIIntegrationComponent::RequestHint() { SubmitVoiceTranscript(TEXT("Give me a hint.")); }
void UFirstPersonAIIntegrationComponent::RepeatInstruction() { SubmitVoiceTranscript(TEXT("Repeat the current instruction.")); }

void UFirstPersonAIIntegrationComponent::HandleDamage(AActor*, float Damage, const UDamageType*, AController*, AActor* Causer)
{
    if (!Initialized || Dead || Won || !FMath::IsFinite(Damage) || Damage <= 0.f) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if(ShieldActive)
    {
        const FVector Incoming=Causer ? (Causer->GetActorLocation()-Character->GetActorLocation()).GetSafeNormal2D() : Character->GetControlRotation().Vector();
        if(!Causer || FVector::DotProduct(FRotator(0,Character->GetControlRotation().Yaw,0).Vector(),Incoming)>=.342f)
        {
            ShieldEnergy=FMath::Max(0.f,ShieldEnergy-12.f);
            if(auto* Prop=Cast<ACosmoPlanetProp>(Causer)) if(Prop->Kind==EPlanetPropKind::ShieldTarget) Prop->ConfirmTraining();
            if(ShieldEnergy<=0.f) { DeactivateShield(); ShieldReadyAt=Now+1.5f; }
            NotifyHUD(); return;
        }
    }
    if(Now<DamageImmuneUntil) return;
    DamageImmuneUntil=Now+.55f;
    Health = FMath::Clamp(Health - Damage, 0.f, MaxHealth);
    // player_damaged sets server danger too; keep the local flag consistent so
    // a later SetDanger(false) can always emit danger_cleared.
    InDanger = true;
    SynchronizeGameState();
    if (Cosmo && CosmoConnected && Now - LastDamageWarning >= 2.) { Cosmo->SendGameEvent(ECosmoGameEvent::PlayerDamaged); LastDamageWarning = Now; }
    if (Health <= 0.f)
    {
        Dead = true; ReleaseAllAbilities();
        Character->GetCharacterMovement()->DisableMovement();
        LastError = TEXT("Explorer defeated. Hold both palms open for 1.5s to retry your checkpoint.");
    }
    if(auto* Director=ACosmoPlanetDirector::Find(GetWorld())) Director->PublishStatus();
    NotifyHUD();
}

float UFirstPersonAIIntegrationComponent::RestoreHealth(float Amount)
{
    if(Dead || Won || !FMath::IsFinite(Amount) || Amount<=0.f) return 0.f;
    const float Before=Health; Health=FMath::Min(MaxHealth,Health+Amount); NotifyHUD(); SynchronizeGameState(); return Health-Before;
}
void UFirstPersonAIIntegrationComponent::RestoreForCheckpoint()
{
    Dead=false; Won=false; Health=MaxHealth; ShieldEnergy=100.f;
    ReleaseAllAbilities(); FireReadyAt=WaveReadyAt=ShieldReadyAt=0; FireActive=WaveActive=false;
    DamageImmuneUntil=GetWorld()->GetTimeSeconds()+2.f; LastError.Reset();
    CurrentBoss=nullptr; ScannedTarget=nullptr; SetDanger(false);
    if(Character) { Character->GetCharacterMovement()->StopMovementImmediately(); Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking); }
    if(auto* PC=Cast<APlayerController>(Character->GetController()))
    { PC->ResetIgnoreMoveInput(); PC->ResetIgnoreLookInput(); PC->bShowMouseCursor=false; PC->SetInputMode(FInputModeGameOnly()); }
    NotifyHUD(); SynchronizeGameState();
}
void UFirstPersonAIIntegrationComponent::RetryFromCheckpoint()
{
    if(auto* Director=ACosmoPlanetDirector::Find(GetWorld()))
    { if(Won) Director->RestartRun(); else if(Dead) Director->RetryCheckpoint(); }
}
void UFirstPersonAIIntegrationComponent::RefreshGameplayHUD() { NotifyHUD(); }

void UFirstPersonAIIntegrationComponent::SetDanger(bool NewDanger)
{
    if (InDanger == NewDanger) return;
    InDanger = NewDanger;
    SynchronizeGameState();
    if (Cosmo && SessionReady) Cosmo->SendGameEvent(NewDanger ? ECosmoGameEvent::PlayerInDanger : ECosmoGameEvent::DangerCleared);
    NotifyHUD();
}

bool UFirstPersonAIIntegrationComponent::SynchronizeGameState()
{
    if (!Cosmo || !SessionReady) return false;
    auto State = MakeShared<FJsonObject>();
    State->SetNumberField(TEXT("player_health"), Health);
    State->SetBoolField(TEXT("shield_up"), ShieldActive);
    State->SetBoolField(TEXT("in_danger"), InDanger);
    if(!MissionObjective.IsEmpty()) State->SetStringField(TEXT("current_mission"),MissionObjective);
    if(!MissionObjective.IsEmpty()) { State->SetNumberField(TEXT("mission_progress"),MissionProgress); State->SetNumberField(TEXT("star_core_energy"),StarCoreEnergyPercent); }
    auto Abilities = MakeShared<FJsonObject>();
    for (FName Name : {FName(TEXT("Shield")), FName(TEXT("Fire")), FName(TEXT("Wave")), FName(TEXT("Laser"))})
        Abilities->SetBoolField(Name.ToString().ToLower(), IsAbilityAvailable(Name));
    State->SetObjectField(TEXT("abilities"), Abilities);
    AActor* Target = IsValid(CurrentBoss) ? CurrentBoss.Get() : ScannedTarget.Get();
    if (IsValid(Target)) State->SetStringField(TEXT("current_target"), Target->GetName());
    else State->SetField(TEXT("current_target"), MakeShared<FJsonValueNull>());
    if (IsValid(CurrentBoss))
    {
        const FNumericProperty* P = FindFProperty<FNumericProperty>(CurrentBoss->GetClass(), TEXT("Health"));
        if (P && P->IsFloatingPoint()) State->SetNumberField(TEXT("boss_health"), P->GetFloatingPointPropertyValue(P->ContainerPtrToValuePtr<void>(CurrentBoss)));
        else if (P && P->IsInteger()) State->SetNumberField(TEXT("boss_health"), P->GetSignedIntPropertyValue(P->ContainerPtrToValuePtr<void>(CurrentBoss)));
        else State->SetField(TEXT("boss_health"), MakeShared<FJsonValueNull>());
    }
    else State->SetField(TEXT("boss_health"), MakeShared<FJsonValueNull>());
    FString Json; FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
    return Cosmo->SendGameState(Json);
}

void UFirstPersonAIIntegrationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!Initialized || !Character) return;
    const double Now = GetWorld()->GetTimeSeconds();
    const FString NewGestureStatus = !Gestures->Connected
        ? TEXT("Waiting for camera - face forward with both hands visible")
        : !Gestures->LatestFrame.LeftCalibrated || !Gestures->LatestFrame.HeadCalibrated
        ? TEXT("Calibrating - face forward, hold left palm still for 1s")
        : !Gestures->LatestFrame.FaceTracked ? TEXT("Face lost - turning stopped")
        : !Gestures->LatestFrame.LeftTracked ? TEXT("Left hand lost - movement stopped")
        : !Gestures->LatestFrame.RightTracked ? TEXT("Right hand lost - abilities released")
        : TEXT("Hands and face ready");
    if (NewGestureStatus != GestureStatus) { GestureStatus = NewGestureStatus; NotifyHUD(); }
    auto* PC = Cast<APlayerController>(Character->GetController());
    if (PC && PC->GetLocalPlayer() && PC->GetLocalPlayer()->ViewportClient)
    {
        const bool Focused = PC->GetLocalPlayer()->ViewportClient->Viewport && PC->GetLocalPlayer()->ViewportClient->Viewport->HasFocus();
        if (HadFocus && !Focused) ReleaseAllAbilities();
        HadFocus = Focused;
    }
    if (!Character->IsLocallyControlled()) { ReleaseAllAbilities(); return; }
    if (!Subtitle.IsEmpty() && Now > SubtitleExpiresAt && !(Realtime && Realtime->TutorialActive)) { Subtitle.Reset(); NotifyHUD(); }
    const bool NewFire = Now < FireReadyAt, NewWave = Now < WaveReadyAt;
    if (FireActive != NewFire || WaveActive != NewWave)
    { FireActive = NewFire; WaveActive = NewWave; SynchronizeGameState(); NotifyHUD(); }
    const FVector Position = Character->GetActorLocation();
    if (TutorialStatus.ExpectedEvent == TEXT("movement_completed") && Character->GetCharacterMovement()->IsMovingOnGround() && Character->GetVelocity().SizeSquared2D() > 1.f)
    {
        const float Step = FVector::Dist2D(Position, LastMovementPosition);
        // Teleports do not constitute walking.
        if (Step <= Character->GetCharacterMovement()->MaxWalkSpeed * DeltaTime * 2.f + 10.f) MovementDistance += Step;
    }
    LastMovementPosition = Position;
    if (CanAct()) { UpdateLaser(DeltaTime); UpdateScanning(DeltaTime); }
    if (Now >= NextConnectionCheck && !Cosmo->HasPendingRequests())
    {
        NextConnectionCheck = Now + 3.;
        if (RecognitionProcess.IsValid() && !FPlatformProcess::IsProcRunning(RecognitionProcess))
        {
            FPlatformProcess::CloseProc(RecognitionProcess);
            LastError = FString::Printf(TEXT("Camera recognition stopped. Check camera access and %s/GestureRecognition.log, then restart Play."), *CosmoRuntimePaths::LogDir());
            ReleaseAllAbilities(); NotifyHUD();
        }
        if (!SessionReady)
        {
            if (ResetTutorialOnStart) Cosmo->ResetSession(); else Cosmo->GetSnapshot();
        }
        else if (!CosmoConnected) Cosmo->GetSnapshot();
    }
    if (Now >= NextWorldCheck)
    {
        NextWorldCheck = Now + .25;
        if (SessionReady && !Dead)
        {
            if (TutorialStatus.ExpectedEvent == TEXT("game_started")) TrySendCosmoEvent(ECosmoGameEvent::GameStarted);
            if (TutorialStatus.ExpectedEvent == TEXT("movement_completed")) TrySendCosmoEvent(ECosmoGameEvent::MovementCompleted);
            if (TutorialStatus.ExpectedEvent == TEXT("boss_encountered"))
                for (TActorIterator<AActor> It(GetWorld()); It; ++It)
                    if (It->ActorHasTag(BossTag) && ConfirmBossEncounter(*It)) break;
            if (TutorialStatus.ExpectedEvent == TEXT("tutorial_completed") && AutoCompleteFinalStage) CompleteTutorial();
        }
    }
    if(!Dead && !Won)
    {
        const float Before=ShieldEnergy;
        if(ShieldActive)
        {
            ShieldEnergy=FMath::Max(0.f,ShieldEnergy-ShieldDrainPerSecond*DeltaTime);
            if(ShieldEnergy<=0.f) { DeactivateShield(); ShieldReadyAt=Now+1.5f; }
        }
        else if(Now>=ShieldRegenAt) ShieldEnergy=FMath::Min(100.f,ShieldEnergy+ShieldRegenPerSecond*DeltaTime);
        if((Before!=ShieldEnergy || FireActive || WaveActive) && Now>=NextResourceHUD) { NextResourceHUD=Now+.1; NotifyHUD(); }
    }
}

void UFirstPersonAIIntegrationComponent::ConfigureGestureOnlyInput()
{
    auto* PC = Cast<APlayerController>(Character->GetController());
    if (!PC) return;
    if (auto* LocalPlayer = PC->GetLocalPlayer())
        if (auto* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Input->ClearAllMappings();
    Character->DisableInput(PC);
    PC->bShowMouseCursor = false;
    PC->bEnableClickEvents = false;
    PC->bEnableMouseOverEvents = false;
    PC->SetInputMode(FInputModeGameOnly());
}

void UFirstPersonAIIntegrationComponent::StartRecognition()
{
    if (!AutoLaunchRecognition || IsRunningCommandlet() || FApp::IsUnattended() || !Gestures->Listening || Gestures->Connected) return;
    const FString Root = CosmoRuntimePaths::Root();
    const FString Python = CosmoRuntimePaths::Python(Root);
    const FString Script = FPaths::Combine(Root, TEXT("app/gestures/gesture_recognition.py"));
    if (!FPaths::FileExists(Python) || !FPaths::FileExists(Script))
    { LastError = TEXT("Bundled camera runtime is missing. See RUNTIME_DEPLOYMENT.md."); NotifyHUD(); return; }
    const FString Log = FPaths::Combine(CosmoRuntimePaths::LogDir(), TEXT("GestureRecognition.log"));
    const FString Args = FString::Printf(TEXT("-B -u \"%s\" --require-local-models --headless --camera %d --udp-port %d --preview-port %d --runtime-log \"%s\"%s%s"),
        *Script, CameraIndex, Gestures->UdpPort, EnableCameraPreview ? CameraPreviewPort : 0, *Log, SwapHands ? TEXT(" --swap-hands") : TEXT(""), InvertHeadTurn ? TEXT(" --invert-turn") : TEXT(""));
    RecognitionProcess = FPlatformProcess::CreateProc(*Python, *Args, false, true, true, nullptr, 0, *Root, nullptr);
    if (!RecognitionProcess.IsValid()) LastError = FString::Printf(TEXT("Camera recognition could not start. See %s."), *Log);
}

void UFirstPersonAIIntegrationComponent::SyncLegacyBlueprintState()
{
    if (!GetOwner()) return;
    auto Bool = [&](FName Name, bool Value) { if (auto* P = FindFProperty<FBoolProperty>(GetOwner()->GetClass(), Name)) P->SetPropertyValue_InContainer(GetOwner(), Value); };
    auto String = [&](FName Name, const FString& Value) { if (auto* P = FindFProperty<FStrProperty>(GetOwner()->GetClass(), Name)) P->SetPropertyValue_InContainer(GetOwner(), Value); };
    Bool(TEXT("bShieldActive"), ShieldActive); Bool(TEXT("bLaserActive"), LaserActive);
    Bool(TEXT("bFireActive"), FireActive); Bool(TEXT("bWindActive"), WaveActive);
    Bool(TEXT("bCosmoConnected"), CosmoConnected); Bool(TEXT("bCosmoTutorialComplete"), TutorialStatus.IsComplete);
    Bool(TEXT("bCosmoGameStartedSent"), !TutorialStatus.CompletedStageIds.IsEmpty());
    Bool(TEXT("bMovementReported"), TutorialStatus.CompletedStageIds.Contains(TEXT("movement")));
    String(TEXT("CosmoExpectedEvent"), TutorialStatus.ExpectedEvent); String(TEXT("CosmoCurrentStage"), TutorialStatus.CurrentStageId);
    String(TEXT("CosmoSubtitle"), Subtitle); String(TEXT("CosmoLastError"), LastError); String(TEXT("CosmoAnimationState"), AnimationState);
}
void UFirstPersonAIIntegrationComponent::NotifyHUD() { SyncLegacyBlueprintState(); OnHUDChanged.Broadcast(); }

void UFirstPersonAIIntegrationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Realtime) { Realtime->OnUpdated.RemoveDynamic(this,&UFirstPersonAIIntegrationComponent::HandleRealtimeUpdated); Realtime->Stop(); }
    if (RecognitionProcess.IsValid())
    {
        FPlatformProcess::TerminateProc(RecognitionProcess);
        FPlatformProcess::CloseProc(RecognitionProcess);
    }
    ReleaseAllAbilities();
    if (Character)
    {
        Character->OnTakeAnyDamage.RemoveDynamic(this, &UFirstPersonAIIntegrationComponent::HandleDamage);
    }
    if (HUD) HUD->RemoveFromParent();
    if (LaserVFX) LaserVFX->DestroyComponent();
    if (GEngine && SpeechReady && !SpeechChannel.IsNone())
        if (auto* Speech = GEngine->GetEngineSubsystem<UTextToSpeechEngineSubsystem>()) Speech->RemoveChannel(SpeechChannel);
    Initialized = false;
    Super::EndPlay(Reason);
}
