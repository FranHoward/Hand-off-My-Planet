#include "CosmoFireProjectile.h"
#include "CosmoPlanetGame.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"

ACosmoFireProjectile::ACosmoFireProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    InitialLifeSpan = 5.f;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(12.f);
    Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Collision->SetNotifyRigidBodyCollision(true);
    Collision->OnComponentHit.AddDynamic(this, &ACosmoFireProjectile::OnImpact);
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->UpdatedComponent = Collision;
    Movement->InitialSpeed = 2200.f;
    Movement->MaxSpeed = 2200.f;
    Movement->ProjectileGravityScale = 0.f;
    Movement->bRotationFollowsVelocity = true;
    Effect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FireEffect"));
    Effect->SetupAttachment(Collision);
    Effect->SetAutoActivate(false);
}

void ACosmoFireProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (GetOwner()) Collision->IgnoreActorWhenMoving(GetOwner(), true);
    if (GetInstigator()) Collision->IgnoreActorWhenMoving(GetInstigator(), true);
    if (Effect->GetAsset()) Effect->Activate(true);
}

void ACosmoFireProjectile::OnImpact(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector, const FHitResult& Hit)
{
    if (Impacted || OtherActor == GetOwner() || OtherActor == GetInstigator()) return;
    Impacted = true;
    if (IsValid(OtherActor))
        UGameplayStatics::ApplyPointDamage(OtherActor, Damage, GetVelocity().GetSafeNormal(), Hit, GetInstigatorController(), this, UCosmoFireDamage::StaticClass());
    if (OtherComponent && OtherComponent->IsSimulatingPhysics())
        OtherComponent->AddImpulseAtLocation(GetActorForwardVector() * 15000.f, Hit.ImpactPoint);
    Destroy();
}
