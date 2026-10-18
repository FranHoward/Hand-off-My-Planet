#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CosmoFireProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UNiagaraComponent;

UCLASS()
class COSMOBRIDGE_API ACosmoFireProjectile : public AActor
{
    GENERATED_BODY()
public:
    ACosmoFireProjectile();
    UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Effect;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ExposeOnSpawn="true")) float Damage = 25.f;
protected:
    virtual void BeginPlay() override;
private:
    bool Impacted = false;
    UFUNCTION() void OnImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
};
