#include "CosmoPlanetGame.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

namespace Planet
{
UFirstPersonAIIntegrationComponent* Player(UWorld* World)
{
    auto* Pawn=UGameplayStatics::GetPlayerPawn(World,0);
    return Pawn ? Pawn->FindComponentByClass<UFirstPersonAIIntegrationComponent>() : nullptr;
}
bool ClearSight(UWorld* World,AActor* From,AActor* To)
{
    if(!From || !To) return false;
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(PlanetSight),false,From);
    return !World->LineTraceSingleByChannel(Hit,From->GetActorLocation()+FVector(0,0,30),To->GetActorLocation(),ECC_Visibility,Query) || Hit.GetActor()==To;
}
FName Ability(EPlanetPropKind Kind)
{
    switch(Kind)
    {
        case EPlanetPropKind::ShieldTarget:return TEXT("Shield");
        case EPlanetPropKind::FireTarget:return TEXT("Fire");
        case EPlanetPropKind::WaveTarget:return TEXT("Wave");
        case EPlanetPropKind::LaserTarget:return TEXT("Laser");
        default:return NAME_None;
    }
}
}

ACosmoPlanetEnemy::ACosmoPlanetEnemy()
{
    PrimaryActorTick.bCanEverTick=true;
    AIControllerClass=AAIController::StaticClass(); AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;
    GetCapsuleComponent()->InitCapsuleSize(70.f,95.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCanEverAffectNavigation(false);
    GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetCharacterMovement()->MaxWalkSpeed=140.f;
    GetCharacterMovement()->bOrientRotationToMovement=true;
    GetCharacterMovement()->RotationRate=FRotator(0,180,0);
    GetCharacterMovement()->bUseRVOAvoidance=true;
    GetCharacterMovement()->AvoidanceConsiderationRadius=400.f;
    GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(false);
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentRadius=150.f;
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentHeight=330.f;
    bUseControllerRotationYaw=false;
    AttackMarker=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AttackWarning"));
    AttackMarker->SetupAttachment(GetRootComponent()); AttackMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AttackMarker->SetCanEverAffectNavigation(false); AttackMarker->SetCastShadow(false);
    AttackMarker->SetHiddenInGame(true);
}
void ACosmoPlanetEnemy::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); ConfigureVisuals(); }
void ACosmoPlanetEnemy::ConfigureVisuals()
{
    const FString Kind=Species==ECosmoCreatureSpecies::Boss ? TEXT("Boss") : Species==ECosmoCreatureSpecies::CrystalEye ? TEXT("CrystalEye") : TEXT("Minion");
    auto* Asset=LoadObject<USkeletalMesh>(nullptr,*(TEXT("/Game/Creatures/")+Kind+TEXT("/Mesh/SK_")+Kind));
    auto* Class=LoadClass<UAnimInstance>(nullptr,*(TEXT("/Game/Creatures/")+Kind+TEXT("/ABP_")+Kind+TEXT(".ABP_")+Kind+TEXT("_C")));
    const float Scale=IsBoss() ? .50f : Passive ? .60f : .72f;
    const float Height=IsBoss() ? 165.f : Passive ? 65.f : 95.f;
    GetCapsuleComponent()->SetCapsuleSize(IsBoss() ? 150.f : Passive ? 40.f : 70.f,Height);
    GetMesh()->SetSkeletalMesh(Asset);
    GetMesh()->SetRelativeLocation(FVector(0,0,-Height));
    GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
    GetMesh()->SetRelativeScale3D(FVector(Scale));
    GetMesh()->SetAnimInstanceClass(Class);
    AttackMarker->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/AlienPlanet/Meshes/SM_FXM_WaveRing")));
    Tags.Remove(TEXT("Enemy")); Tags.Remove(TEXT("Boss")); Tags.Remove(TEXT("Scannable"));
    Tags.AddUnique(Passive ? FName(TEXT("Scannable")) : FName(TEXT("Enemy")));
    if(IsBoss()) Tags.AddUnique(TEXT("Boss"));
}
void ACosmoPlanetEnemy::BeginPlay()
{
    Super::BeginPlay(); Home=GetActorTransform(); Director=ACosmoPlanetDirector::Find(GetWorld());
    const auto* B=Tuning(); MaxHealth=Passive ? 40.f : IsBoss() ? B->BossHealth : B->MinionHealth; Health=MaxHealth;
    if(auto* Anim=Animation())
    {
        Anim->OnAnimationEvent.AddUniqueDynamic(this,&ACosmoPlanetEnemy::OnAnimationEvent);
        Anim->ReferenceMoveSpeed=(IsBoss() ? 77.381f : 80.882f)*GetMesh()->GetRelativeScale3D().X;
    }
    GetCharacterMovement()->MaxWalkSpeed=IsBoss() ? B->BossSpeed : B->MinionSpeed;
    if(Passive) { GetCharacterMovement()->SetMovementMode(MOVE_Flying); GetCharacterMovement()->StopMovementImmediately(); }
    EnterState(EPlanetEnemyState::Dormant);
    ActivateEncounter();
}
UCosmoCreatureAnimInstance* ACosmoPlanetEnemy::Animation() const { return Cast<UCosmoCreatureAnimInstance>(GetMesh()->GetAnimInstance()); }
const UCosmoPlanetBalance* ACosmoPlanetEnemy::Tuning() const
{ return Director.IsValid() && Director->Balance ? Director->Balance.Get() : GetDefault<UCosmoPlanetBalance>(); }
void ACosmoPlanetEnemy::EnterState(EPlanetEnemyState NewState)
{
    State=NewState; StateTime=0.f;
    if(NewState!=EPlanetEnemyState::Chase)
    { if(auto* AI=Cast<AAIController>(GetController())) AI->StopMovement(); GetCharacterMovement()->StopMovementImmediately(); }
    if(NewState!=EPlanetEnemyState::Windup && NewState!=EPlanetEnemyState::Acting) AttackMarker->SetHiddenInGame(true);
}
void ACosmoPlanetEnemy::ActivateEncounter()
{
    if(Active || bDead || Passive) return;
    Active=true; NextAttackAt=GetWorld()->GetTimeSeconds()+1.2f; EnterState(EPlanetEnemyState::Chase);
}
void ACosmoPlanetEnemy::ResetEncounter()
{
    bDead=false; Active=false; Health=MaxHealth;
    SetActorHiddenInGame(false); SetActorTickEnabled(true);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(Passive ? MOVE_Flying : MOVE_Walking);
    SetActorTransform(Home,false,nullptr,ETeleportType::TeleportPhysics);
    if(auto* Anim=Animation()) Anim->ResetForRespawn();
    NextPathAt=0; NextAttackAt=0; StunReadyAt=0; EnterState(EPlanetEnemyState::Dormant);
    ActivateEncounter();
}
void ACosmoPlanetEnemy::Tick(float Delta)
{
    Super::Tick(Delta); StateTime+=Delta;
    if(bDead) { if(StateTime>6.f) { SetActorHiddenInGame(true); SetActorTickEnabled(false); } return; }
    if(Passive || !Active) return;
    if(!Director.IsValid()) Director=ACosmoPlanetDirector::Find(GetWorld());
    auto* Player=Director.IsValid() ? Director->Player.Get() : Planet::Player(GetWorld());
    if(!Player || Player->Dead || Player->Won || Player->ChatOpen) { EnterState(EPlanetEnemyState::Dormant); return; }
    AActor* Target=Player->GetOwner(); const float Now=GetWorld()->GetTimeSeconds();
    const float Distance=FVector::Dist2D(GetActorLocation(),Target->GetActorLocation());
    const float Reach=IsBoss() ? 330.f : 200.f;
    switch(State)
    {
        case EPlanetEnemyState::Dormant: EnterState(EPlanetEnemyState::Chase); break;
        case EPlanetEnemyState::Chase:
        {
            const bool CanPounce=IsBoss() && Distance>Reach && Distance<1150.f;
            if((Distance<Reach || CanPounce) && Now>=NextAttackAt && Planet::ClearSight(GetWorld(),this,Target) && (!Director.IsValid() || Director->RequestAttackSlot(this)))
            {
                Pouncing=CanPounce; LockedTarget=Target->GetActorLocation(); AttackDelivered=false;
                SetActorRotation(FRotator(0,(LockedTarget-GetActorLocation()).Rotation().Yaw,0));
                EnterState(EPlanetEnemyState::Windup);
                AttackMarker->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                const FVector Marker=Pouncing ? LockedTarget : GetActorLocation()+GetActorForwardVector()*(Reach*.5f);
                AttackMarker->SetWorldLocation(FVector(Marker.X,Marker.Y,IsBoss() ? 50.f : 12.f));
                AttackMarker->SetWorldRotation(FRotator::ZeroRotator);
                AttackMarker->SetWorldScale3D(FVector((Pouncing ? 320.f : Reach)/104.5f,(Pouncing ? 320.f : Reach)/104.5f,.20f));
                AttackMarker->SetHiddenInGame(false);
                if(Director.IsValid()) Director->PublishStatus();
            }
            else if(Now>=NextPathAt)
            {
                NextPathAt=Now+.4f;
                if(auto* AI=Cast<AAIController>(GetController())) AI->MoveToLocation(Target->GetActorLocation(),Reach*.6f,false,true,false,true,nullptr,true);
            }
            break;
        }
        case EPlanetEnemyState::Windup:
            if(StateTime>=(IsBoss() ? .75f : .40f))
            {
                auto* Anim=Animation(); const bool Started=Anim && (Pouncing ? Anim->RequestPounce() : Anim->RequestAttack());
                if(Started) EnterState(EPlanetEnemyState::Acting); else EnterState(EPlanetEnemyState::Recover);
            }
            break;
        case EPlanetEnemyState::Acting:
            if(auto* Anim=Animation())
                if((StateTime>.2f && Anim->DesiredState!=ECosmoCreatureAnimState::Attack && Anim->DesiredState!=ECosmoCreatureAnimState::Pounce) || StateTime>2.5f)
                { EnterState(EPlanetEnemyState::Recover); NextAttackAt=Now+(IsBoss() ? (Health<MaxHealth*.5f ? 3.5f : Tuning()->BossAttackCooldown) : Tuning()->MinionAttackCooldown); }
            break;
        case EPlanetEnemyState::Recover:
            if(StateTime>(IsBoss() ? 1.5f : .6f)) EnterState(EPlanetEnemyState::Chase);
            break;
        case EPlanetEnemyState::Stunned:
            if(StateTime>.7f) EnterState(EPlanetEnemyState::Recover);
            break;
        default: break;
    }
}
void ACosmoPlanetEnemy::OnAnimationEvent(FName Event)
{
    if(!Active || bDead || State!=EPlanetEnemyState::Acting) return;
    if(Event==TEXT("Launch") && Pouncing)
    {
        FVector Velocity=(LockedTarget-GetActorLocation())/.40f; Velocity.Z=150.f;
        Velocity=Velocity.GetClampedToMaxSize(1800.f); LaunchCharacter(Velocity,true,true);
    }
    if(Event==TEXT("Attack") || Event==TEXT("Hit_Controlled_1s")) DealAttack();
}
void ACosmoPlanetEnemy::DealAttack()
{
    if(AttackDelivered) return; AttackDelivered=true;
    auto* Player=Director.IsValid() ? Director->Player.Get() : Planet::Player(GetWorld());
    if(!Player || Player->Dead || Player->Won) return;
    const FVector Center=Pouncing ? LockedTarget : GetActorLocation();
    const float Radius=Pouncing ? 320.f : IsBoss() ? 360.f : 225.f;
    if((!Pouncing || FVector::Dist2D(GetActorLocation(),LockedTarget)<450.f) && FVector::Dist2D(Center,Player->GetOwner()->GetActorLocation())<=Radius && Planet::ClearSight(GetWorld(),this,Player->GetOwner()))
    {
        if(Pouncing || FVector::DotProduct(GetActorForwardVector(),(Player->GetOwner()->GetActorLocation()-GetActorLocation()).GetSafeNormal2D())>.15f)
            UGameplayStatics::ApplyDamage(Player->GetOwner(),Pouncing ? Tuning()->BossPounceDamage : IsBoss() ? Tuning()->BossMeleeDamage : Tuning()->MinionDamage,GetController(),this,UDamageType::StaticClass());
    }
    if(Pouncing && Director.IsValid())
        for(ACosmoPlanetEnemy* Other:Director->Enemies)
            if(IsValid(Other) && Other->Passive && FVector::Dist2D(Center,Other->GetActorLocation())<Radius)
                if(auto* Anim=Other->Animation()) Anim->ApplyControlled(1.f);
}
float ACosmoPlanetEnemy::TakeDamage(float Amount,const FDamageEvent& Event,AController* EventInstigator,AActor* Causer)
{
    if(bDead || Passive || !Active || !FMath::IsFinite(Amount) || Amount<=0.f) return 0.f;
    if(Causer==this || Cast<ACosmoPlanetEnemy>(Causer)) return 0.f;
    const bool Wave=Event.DamageTypeClass && Event.DamageTypeClass->IsChildOf(UCosmoWaveDamage::StaticClass());
    const float Multiplier=IsBoss() ? (State==EPlanetEnemyState::Recover ? Tuning()->BossRecoveryMultiplier : Tuning()->BossArmorMultiplier) : 1.f;
    const float Applied=FMath::Min(Health,Amount*Multiplier); Health=FMath::Max(0.f,Health-Applied);
    Super::TakeDamage(Applied,Event,EventInstigator,Causer);
    if(Health<=0.f)
    {
        bDead=true; Active=false; EnterState(EPlanetEnemyState::Dead);
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision); GetCharacterMovement()->DisableMovement();
        if(auto* Anim=Animation()) Anim->SetDead();
        if(Director.IsValid()) Director->EnemyDefeated(this);
    }
    else if(Wave && !IsBoss() && GetWorld()->GetTimeSeconds()>=StunReadyAt)
    {
        StunReadyAt=GetWorld()->GetTimeSeconds()+1.f; EnterState(EPlanetEnemyState::Stunned);
        if(auto* Anim=Animation()) Anim->RequestHit();
    }
    else if(!IsBoss() && State==EPlanetEnemyState::Chase && GetWorld()->GetTimeSeconds()>=StunReadyAt)
    { StunReadyAt=GetWorld()->GetTimeSeconds()+1.5f; if(auto* Anim=Animation()) Anim->RequestHit(); }
    if(Director.IsValid()) Director->PublishStatus();
    return Applied;
}

ACosmoPlanetProp::ACosmoPlanetProp()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.1f;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("Root")); SetRootComponent(Root);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual")); Visual->SetupAttachment(Root);
    Visual->SetMobility(EComponentMobility::Movable); Visual->SetCanEverAffectNavigation(false);
    Blocker=CreateDefaultSubobject<UBoxComponent>(TEXT("GateBlocker")); Blocker->SetupAttachment(Root);
    Blocker->SetBoxExtent(FVector(1050,40,230)); Blocker->SetRelativeLocation(FVector(0,0,200));
    Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision); Blocker->SetCanEverAffectNavigation(false);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label")); Label->SetupAttachment(Root);
    Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(24.f); Label->SetRelativeLocation(FVector(0,0,260));
    Label->SetTextRenderColor(FColor(65,220,245));
}
void ACosmoPlanetProp::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); ConfigureVisuals(); }
void ACosmoPlanetProp::ConfigureVisuals()
{
    FString Asset,Text; float Scale=1.f;
    switch(Kind)
    {
        case EPlanetPropKind::Repair: Text=TEXT("REPAIR +25"); break;
        case EPlanetPropKind::Checkpoint: Asset=TEXT("MissionBeacon"); Text=TEXT("CHECKPOINT"); Scale=.65f; break;
        case EPlanetPropKind::ShieldTarget: Asset=TEXT("TrainingTarget_Shield"); Text=TEXT("SHIELD / right open palm - block the pulse"); break;
        case EPlanetPropKind::FireTarget: Asset=TEXT("TrainingTarget_Fire"); Text=TEXT("FIRE / right fist then push"); break;
        case EPlanetPropKind::WaveTarget: Asset=TEXT("TrainingTarget_Wave"); Text=TEXT("WAVE / right swipe - approach within 5m"); break;
        case EPlanetPropKind::LaserTarget: Asset=TEXT("TrainingTarget_Laser"); Text=TEXT("LASER / right pinch - hit this target"); break;
        case EPlanetPropKind::Gate: Asset=TEXT("EnergyBarrier"); Text=Index==0 ? TEXT("TRAINING CANYON") : TEXT("COMPLETE TRAINING + CLEAR SENTRIES"); break;
        case EPlanetPropKind::StarCore: Asset=TEXT("StarCore"); Text=TEXT("STAR CORE - defeat the guardian"); break;
    }
    const FString MeshPath=Kind==EPlanetPropKind::Repair
        ? TEXT("/Game/Props/ShieldHealth/Meshes/SM_HealthPickup.SM_HealthPickup")
        : TEXT("/Game/AlienPlanet/Meshes/SM_")+Asset;
    Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*MeshPath));
    Visual->SetRelativeScale3D(Kind==EPlanetPropKind::Gate ? FVector(4.f,1.f,1.f) : FVector(Scale));
    const bool Target=!Planet::Ability(Kind).IsNone();
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(Target)
    {
        Blocker->SetBoxExtent(FVector(85,75,125)); Blocker->SetRelativeLocation(FVector(0,0,125));
        Blocker->SetCollisionObjectType(ECC_WorldDynamic); Blocker->SetCollisionResponseToAllChannels(ECR_Block);
        Blocker->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore); Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }
    else if(Kind==EPlanetPropKind::Gate)
    {
        Blocker->SetBoxExtent(FVector(1050,40,230)); Blocker->SetRelativeLocation(FVector(0,0,200));
        Blocker->SetCollisionProfileName(TEXT("BlockAllDynamic"));
        Blocker->SetCollisionEnabled(Used ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    }
    else Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetCollisionObjectType(ECC_WorldDynamic); Visual->SetCollisionResponseToAllChannels(ECR_Block);
    Visual->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    Label->SetText(FText::FromString(Text)); Label->SetRelativeLocation(FVector(0,0,Kind==EPlanetPropKind::Repair ? 110.f : Kind==EPlanetPropKind::Gate ? 440.f : 260.f));
}
void ACosmoPlanetProp::BeginPlay()
{
    Super::BeginPlay(); Director=ACosmoPlanetDirector::Find(GetWorld());
    // The opening menu owns instruction. Training props are absent during the mission.
    if(!Planet::Ability(Kind).IsNone())
    {
        Used=true; SetActorHiddenInGame(true); SetActorEnableCollision(false); SetActorTickEnabled(false);
    }
    if(Kind==EPlanetPropKind::Gate) SetGateOpen(true);
}
void ACosmoPlanetProp::SetGateOpen(bool Open)
{
    if(Used==Open && ((Blocker->GetCollisionEnabled()==ECollisionEnabled::NoCollision)==Open) && (Open || Blocker->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block)) return;
    Blocker->SetCollisionProfileName(Open ? TEXT("NoCollision") : TEXT("BlockAllDynamic"));
    Used=Open; Blocker->SetCollisionEnabled(Open ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    Visual->SetHiddenInGame(Open); Label->SetHiddenInGame(Open);
}
void ACosmoPlanetProp::ResetProp()
{
    if(!Planet::Ability(Kind).IsNone()) return;
    Used=false; SetActorHiddenInGame(false); Visual->SetHiddenInGame(false); Label->SetHiddenInGame(false);
    PulsePending=false; ShieldPulseAt=GetWorld()->GetTimeSeconds()+1.5f;
    ConfigureVisuals(); if(Kind==EPlanetPropKind::Gate) SetGateOpen(true);
}
void ACosmoPlanetProp::ConfirmTraining()
{
    if(Used || Planet::Ability(Kind).IsNone()) return; Used=true;
    Label->SetText(FText::FromString(Planet::Ability(Kind).ToString()+TEXT(" - COMPLETE"))); Label->SetTextRenderColor(FColor::Green);
    if(!Director.IsValid()) Director=ACosmoPlanetDirector::Find(GetWorld());
    if(Director.IsValid()) Director->RecordTraining(Planet::Ability(Kind));
}
float ACosmoPlanetProp::TakeDamage(float DamageAmount,const FDamageEvent& Event,AController*,AActor*)
{
    if(Used || DamageAmount<=0.f || !Event.DamageTypeClass) return 0.f;
    const bool Match=(Kind==EPlanetPropKind::FireTarget && Event.DamageTypeClass->IsChildOf(UCosmoFireDamage::StaticClass())) ||
        (Kind==EPlanetPropKind::WaveTarget && Event.DamageTypeClass->IsChildOf(UCosmoWaveDamage::StaticClass())) ||
        (Kind==EPlanetPropKind::LaserTarget && Event.DamageTypeClass->IsChildOf(UCosmoLaserDamage::StaticClass()));
    if(Match) { ConfirmTraining(); return DamageAmount; } return 0.f;
}
void ACosmoPlanetProp::Tick(float Delta)
{
    Super::Tick(Delta);
    if(!Director.IsValid()) Director=ACosmoPlanetDirector::Find(GetWorld());
    auto* Player=Director.IsValid() ? Director->Player.Get() : Planet::Player(GetWorld());
    if(!Player || Player->Dead || Player->Won) return;
    const float Distance=FVector::Dist2D(GetActorLocation(),Player->GetOwner()->GetActorLocation());
    if(Distance<1800.f) Label->SetWorldRotation((Player->GetOwner()->GetActorLocation()+FVector(0,0,100)-Label->GetComponentLocation()).Rotation());
    if(Used) return;
    if(Kind==EPlanetPropKind::Repair && Distance<135.f && Player->Health<Player->MaxHealth)
    {
        if(Player->RestoreHealth(Director.IsValid() && Director->Balance ? Director->Balance->RepairAmount : Amount)>0.f)
        { Used=true; SetActorHiddenInGame(true); if(Director.IsValid()) Director->PublishStatus(); }
    }
    if(Kind==EPlanetPropKind::ShieldTarget && Distance<650.f && !Player->ChatOpen)
    {
        const float Now=GetWorld()->GetTimeSeconds();
        if(!PulsePending && Now>=ShieldPulseAt) { PulsePending=true; PulseWarnAt=Now+1.25f; Label->SetText(FText::FromString(TEXT("INCOMING PULSE - FACE TARGET + SHIELD"))); }
        if(PulsePending && Now>=PulseWarnAt)
        {
            PulsePending=false; ShieldPulseAt=Now+3.f;
            if(Planet::ClearSight(GetWorld(),this,Player->GetOwner()))
            {
                const FVector ToTarget=(GetActorLocation()-Player->GetOwner()->GetActorLocation()).GetSafeNormal2D();
                const auto* Pawn=Cast<APawn>(Player->GetOwner());
                const bool Blocking=Pawn && Player->ShieldActive && FVector::DotProduct(FRotator(0,Pawn->GetControlRotation().Yaw,0).Vector(),ToTarget)>=.342f;
                UGameplayStatics::ApplyDamage(Player->GetOwner(),Blocking ? 4.f : FMath::Min(4.f,FMath::Max(0.f,Player->Health-1.f)),nullptr,this,UDamageType::StaticClass());
            }
        }
    }
}

ACosmoPlanetDirector::ACosmoPlanetDirector()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.1f;
    RegionTutorialLines.Add(TEXT("landing"),TEXT("Welcome, explorer! I'm COSMO. We're on an unknown planet. Wait for the game to confirm that exploration has started."));
    RegionTutorialLines.Add(TEXT("training"),TEXT("Hold your right hand open with the palm facing the camera to activate Shield. I'll wait for gameplay confirmation."));
    RegionTutorialLines.Add(TEXT("ruins"),TEXT("Starcore ruins - clear the two arena sentries"));
    RegionTutorialLines.Add(TEXT("boss"),TEXT("Defeat the guardian - conserve Shield for its attacks"));
}
bool ACosmoPlanetDirector::AnnounceRegion(FName Region)
{
    // Keep opening-menu instruction and realtime Grok conversation separate from the mission.
    return false;
}
ACosmoPlanetDirector* ACosmoPlanetDirector::Find(UWorld* World)
{ if(World) for(TActorIterator<ACosmoPlanetDirector> It(World);It;++It) return *It; return nullptr; }
void ACosmoPlanetDirector::BeginPlay()
{
    Super::BeginPlay();
    if(!Balance) Balance=LoadObject<UCosmoPlanetBalance>(nullptr,TEXT("/Game/AlienPlanet/DA_NormalBalance"));
    if(!Balance) Balance=NewObject<UCosmoPlanetBalance>(this);
    for(TActorIterator<ACosmoPlanetEnemy> It(GetWorld());It;++It) Enemies.Add(*It);
    for(TActorIterator<ACosmoPlanetProp> It(GetWorld());It;++It) Props.Add(*It);
}
void ACosmoPlanetDirector::AttachPlayer()
{
    Player=Planet::Player(GetWorld()); if(!Player || !Player->Initialized) return;
    Attached=true; Player->MaxHealth=Balance->PlayerHealth; Player->Health=Player->MaxHealth;
    Player->FireDamage=Balance->FireDamage; Player->FireCooldown=Balance->FireCooldown;
    Player->WaveDamage=Balance->WaveDamage; Player->WaveCooldown=Balance->WaveCooldown; Player->WaveRadius=500.f; Player->WaveImpulse=450.f;
    Player->LaserDamagePerSecond=Balance->LaserDPS; Player->LaserRange=2200.f;
    Player->ShieldDrainPerSecond=Balance->ShieldDrainPerSecond; Player->AutoCompleteFinalStage=false;
    if(auto* Character=Cast<ACharacter>(Player->GetOwner())) Character->GetCharacterMovement()->MaxWalkSpeed=300.f;
    // Data imported into a new editor map can arrive before navigation registration.
    // Ensure the playable world's dynamic mesh is ready once, while the landing area is safe.
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(Nav)
    {
        if(!Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) Nav->OnWorldInitDone(FNavigationSystemRunMode::GameMode);
        auto* Data=Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
        if(Data && !Data->GetBounds().IsValid) Nav->Build();
    }
    for(ACosmoPlanetEnemy* Enemy:Enemies) if(IsValid(Enemy)) Enemy->ActivateEncounter();
    PublishStatus();
}
bool ACosmoPlanetDirector::GroupCleared(int32 Group) const
{
    bool Found=false; for(ACosmoPlanetEnemy* Enemy:Enemies) if(IsValid(Enemy) && !Enemy->Passive && Enemy->EncounterGroup==Group)
    { Found=true; if(!Enemy->bDead) return false; } return Found;
}
void ACosmoPlanetDirector::ActivateGroup(int32 Group)
{ for(ACosmoPlanetEnemy* Enemy:Enemies) if(IsValid(Enemy) && Enemy->EncounterGroup==Group) Enemy->ActivateEncounter(); }
bool ACosmoPlanetDirector::RequestAttackSlot(ACosmoPlanetEnemy*)
{
    const float Now=GetWorld()->GetTimeSeconds(); if(Now<NextAttackSlot) return false; NextAttackSlot=Now+.7f; return true;
}
void ACosmoPlanetDirector::Tick(float Delta)
{
    Super::Tick(Delta); if(!Attached) { AttachPlayer(); return; }
    if(!Player || Player->Dead || Won) return;
    const float Progress=-Player->GetOwner()->GetActorLocation().Y;
    if(Progress>2150.f && Checkpoint==0) Checkpoint=1;
    if(Progress>5300.f && Checkpoint<2) Checkpoint=2;
    for(ACosmoPlanetEnemy* Enemy:Enemies) if(IsValid(Enemy) && Enemy->IsBoss() && !Enemy->bDead)
    { Player->CurrentBoss=Enemy; }
    for(ACosmoPlanetProp* Prop:Props) if(IsValid(Prop) && Prop->Kind==EPlanetPropKind::Gate)
        Prop->SetGateOpen(true);
    bool Threat=false; for(ACosmoPlanetEnemy* Enemy:Enemies) if(IsValid(Enemy) && Enemy->Active && !Enemy->bDead) Threat=true;
    Player->SetDanger(Threat);
    // Dormant enemies count too; passive wildlife is not a combat target.
    bool HasEnemies=false, AllDefeated=true;
    for(ACosmoPlanetEnemy* Enemy:Enemies) if(IsValid(Enemy) && !Enemy->Passive)
    { HasEnemies=true; if(!Enemy->bDead) AllDefeated=false; }
    if(HasEnemies && AllDefeated)
    {
        Won=true; Player->Won=true; Player->ReleaseAllAbilities();
        if(auto* Character=Cast<ACharacter>(Player->GetOwner())) Character->GetCharacterMovement()->DisableMovement();
    }
    PublishStatus();
}
void ACosmoPlanetDirector::RecordTraining(FName Ability)
{ TrainingCompleted.Add(Ability); PublishStatus(); }
void ACosmoPlanetDirector::EnemyDefeated(ACosmoPlanetEnemy* Enemy)
{ if(!Enemy) return; ++Kills; if(Enemy->IsBoss()) BossDefeated=true; PublishStatus(); }
void ACosmoPlanetDirector::PublishStatus()
{
    if(!Player) return;
    int32 Alive=0; ACosmoPlanetEnemy* Boss=nullptr;
    for(ACosmoPlanetEnemy* Enemy:Enemies) if(IsValid(Enemy) && !Enemy->Passive)
    { if(!Enemy->bDead) ++Alive; if(Enemy->IsBoss() && Enemy->Active && !Enemy->bDead) Boss=Enemy; }
    FString Objective;
    if(Won) Objective=TEXT("MISSION COMPLETE - All enemies defeated");
    else if(Player->Dead) Objective=TEXT("EXPLORER DOWN - hold both palms open for 1.5s to retry checkpoint");
    else if(BossDefeated) Objective=TEXT("Defeat the remaining enemies");
    else if(Boss) Objective=Boss->State==EPlanetEnemyState::Windup || Boss->State==EPlanetEnemyState::Acting ? TEXT("GUARDIAN ATTACK - leave the marked area or face it with Shield") : Boss->State==EPlanetEnemyState::Recover ? TEXT("GUARDIAN EXPOSED - attack during recovery") : TEXT("Defeat the guardian - conserve Shield for its attacks");
    else Objective=TEXT("Defeat all enemies");
    Player->MissionObjective=Objective; Player->EnemyKills=Kills; Player->RemainingEnemies=Alive;
    Player->EncounterBossHealth=Boss ? Boss->Health : 0.f; Player->EncounterBossMaxHealth=Boss ? Boss->MaxHealth : 0.f;
    Player->StarCoreEnergyPercent=CoreCharge*100.f;
    Player->MissionProgress=Won ? 100.f : Kills*10.f+(Boss ? 35.f*(1.f-Boss->Health/Boss->MaxHealth) : 0.f);
    Player->EncounterStatus=FString::Printf(TEXT("Checkpoint %d/2  |  Defeated %d/6  |  Threats %d"),Checkpoint,Kills,Alive);
    const FString Status=Objective+Player->EncounterStatus+FString::SanitizeFloat(Player->EncounterBossHealth)+FString::FromInt(Player->Dead);
    if(Status!=LastStatus)
    {
        LastStatus=Status; Player->RefreshGameplayHUD();
        if(GetWorld()->GetTimeSeconds()>=NextSyncAt) { NextSyncAt=GetWorld()->GetTimeSeconds()+.25f; Player->SynchronizeGameState(); }
    }
}
void ACosmoPlanetDirector::RestoreAt(float Progress)
{
    if(!Player) return; NextAttackSlot=GetWorld()->GetTimeSeconds()+1.f;
    BossDefeated=false; Won=false; CoreCharge=0.f; Kills=Checkpoint==2 ? 3 : 0;
    for(ACosmoPlanetEnemy* Enemy:Enemies) if(IsValid(Enemy))
    {
        if(Checkpoint==2 && Enemy->EncounterGroup<3 && !Enemy->Passive) continue;
        Enemy->ResetEncounter();
    }
    for(ACosmoPlanetProp* Prop:Props) if(IsValid(Prop))
    {
        if(Prop->Kind==EPlanetPropKind::Gate || (Prop->Kind==EPlanetPropKind::Repair && -Prop->GetActorLocation().Y>Progress)) Prop->ResetProp();
    }
    Player->RestoreForCheckpoint();
    const FVector Location(0,-Progress,110.f);
    Player->GetOwner()->SetActorLocation(Location,false,nullptr,ETeleportType::TeleportPhysics);
    if(auto* PC=UGameplayStatics::GetPlayerController(GetWorld(),0)) PC->SetControlRotation(FRotator(0,-90,0));
    LastStatus.Reset(); PublishStatus();
}
void ACosmoPlanetDirector::RetryCheckpoint()
{ RestoreAt(Checkpoint==2 ? 5350.f : Checkpoint==1 ? 2200.f : -850.f); }
void ACosmoPlanetDirector::RestartRun()
{ UGameplayStatics::OpenLevel(this,FName(*UGameplayStatics::GetCurrentLevelName(this,true))); }
