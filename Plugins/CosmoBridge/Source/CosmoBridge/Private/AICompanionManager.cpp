#include "AICompanionManager.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "CosmoRealtimeComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CosmoCreatureAnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

AAICompanionManager::AAICompanionManager()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    CompanionMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CompanionMesh"));
    CompanionMesh->SetupAttachment(GetRootComponent());
    CompanionMesh->SetCollisionProfileName(TEXT("NoCollision"));
    CompanionMesh->SetGenerateOverlapEvents(false);
    CompanionMesh->SetCanEverAffectNavigation(false);
    CompanionMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(TEXT("/Game/Creatures/CrystalEye/Mesh/SK_CrystalEye"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Creatures/CrystalEye/Materials/M_CrystalEye"));
    static ConstructorHelpers::FClassFinder<UCosmoCreatureAnimInstance> Animation(TEXT("/Game/Creatures/CrystalEye/ABP_CrystalEye"));
    CompanionMesh->SetSkeletalMesh(MeshAsset.Object);
    if (MeshAsset.Object && Material.Object)
        for (int32 Slot = 0; Slot < MeshAsset.Object->GetMaterials().Num(); ++Slot) CompanionMesh->SetMaterial(Slot, Material.Object);
    CompanionMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    CompanionMesh->SetAnimInstanceClass(Animation.Class);
    CompanionMesh->SetRelativeRotation(FRotator(0, -90, 0));
    CompanionMesh->SetRelativeScale3D(FVector(.30f));
}

FVector AAICompanionManager::GetVelocity() const
{
    if (const AActor* Parent = GetAttachParentActor()) return Parent->GetVelocity();
    return Super::GetVelocity();
}

void AAICompanionManager::BeginPlay()
{
    Super::BeginPlay();
    AActor* Character = GetParentActor();
    if (!Character) Character = GetOwner();
    if (!Character) Character = UGameplayStatics::GetPlayerPawn(this, 0);
    ConnectToCharacter(Character);
}

bool AAICompanionManager::ConnectToCharacter(AActor* Character)
{
    UnbindClient();
    if (!IsValid(Character)) return false;
    Integration = Character->FindComponentByClass<UFirstPersonAIIntegrationComponent>();
    Client = Character->FindComponentByClass<UCosmoClientComponent>();
    if (!Integration || !Client) { Client = nullptr; Integration = nullptr; return false; }
    if (GetAttachParentActor() != Character)
        AttachToComponent(Character->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    Integration->CompanionMesh = CompanionMesh;
    Client->OnResponse.AddUniqueDynamic(this, &AAICompanionManager::HandleResponse);
    if(!Integration->FreeConversation) Client->OnSpokenLine.AddUniqueDynamic(this, &AAICompanionManager::HandleSpokenLine);
    Integration->OnCompanionAnimationRequested.AddUniqueDynamic(this, &AAICompanionManager::HandleAnimation);
    Integration->OnRealtimeSpokenLine.AddUniqueDynamic(this, &AAICompanionManager::HandleRealtimeLine);
    Client->OnConnectionChanged.AddUniqueDynamic(this, &AAICompanionManager::HandleConnection);
    return true;
}

bool AAICompanionManager::SubmitPlayerQuestion(const FString& Text)
{
    return IsValid(Integration) && Integration->SubmitVoiceTranscript(Text);
}

void AAICompanionManager::HandleResponse(const FCosmoResponse& Response)
{
    LastResponse = Response;
    if (!Response.Source.IsEmpty() && !(Integration && Integration->Realtime && Integration->Realtime->TutorialActive)) ReplySource = Response.Source;
    OnResponse.Broadcast(Response);
}

void AAICompanionManager::HandleSpokenLine(const FString& Line)
{
    if(Integration && Integration->Realtime && Integration->Realtime->TutorialActive && Line!=Integration->Subtitle) return;
    Subtitle = Line;
    OnSpokenLine.Broadcast(Line);
}

void AAICompanionManager::HandleAnimation(const FCosmoAnimationEvent& Event)
{
    AnimationEvent = Event;
    OnAnimation.Broadcast(Event);
}

void AAICompanionManager::HandleRealtimeLine(const FString& Line)
{
    ReplySource=Integration ? Integration->GuideReplySource : TEXT("grok");
    HandleSpokenLine(Line);
}

void AAICompanionManager::HandleConnection(bool Connected)
{
    OnConnectionChanged.Broadcast(Connected);
}

void AAICompanionManager::UnbindClient()
{
    if (IsValid(Client))
    {
        Client->OnResponse.RemoveDynamic(this, &AAICompanionManager::HandleResponse);
        Client->OnSpokenLine.RemoveDynamic(this, &AAICompanionManager::HandleSpokenLine);
        Client->OnConnectionChanged.RemoveDynamic(this, &AAICompanionManager::HandleConnection);
    }
    if (IsValid(Integration))
    {
        if (Integration->CompanionMesh == CompanionMesh) Integration->CompanionMesh = nullptr;
        Integration->OnCompanionAnimationRequested.RemoveDynamic(this, &AAICompanionManager::HandleAnimation);
        Integration->OnRealtimeSpokenLine.RemoveDynamic(this, &AAICompanionManager::HandleRealtimeLine);
    }
    Client = nullptr;
    Integration = nullptr;
}

void AAICompanionManager::EndPlay(const EEndPlayReason::Type Reason)
{
    UnbindClient();
    Super::EndPlay(Reason);
}
