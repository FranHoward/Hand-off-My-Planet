#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "CosmoCreatureAnimInstance.h"
#include "CosmoPlanetPreview.h"

// Opt-in, transient visual QA scene. It never modifies a gameplay map or runs in a packaged game.
static FTSTicker::FDelegateHandle StartCreatureAnimationPreview()
{
    return FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [Elapsed=0.f, Stage=0, Ready=false, PreviewCamera=TWeakObjectPtr<ACameraActor>(), Drivers=TArray<TWeakObjectPtr<UCosmoCreatureAnimInstance>>()](float Delta) mutable
        {
            UWorld* World = nullptr;
            if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.World() && Context.World()->IsGameWorld() && Context.World()->HasBegunPlay()) { World=Context.World(); break; }
            if (!World || !World->GetFirstPlayerController()) return true;
            if (!Ready)
            {
                Ready = true;
                auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0,0,-15), FRotator::ZeroRotator);
                Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
                Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
                Floor->SetActorScale3D(FVector(7,16,.2f));
                int32 Column = 0;
                for (const FString Kind : {TEXT("CrystalEye"),TEXT("Minion"),TEXT("Boss")})
                {
                    auto* Class = LoadClass<AActor>(nullptr, *(TEXT("/Game/Creatures/")+Kind+TEXT("/BP_")+Kind+TEXT("_AnimationPreview.BP_")+Kind+TEXT("_AnimationPreview_C")));
                    auto* Actor = Class ? World->SpawnActor<AActor>(Class) : nullptr;
                    auto* Mesh = Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
                    if (!Mesh) { UE_LOG(LogTemp, Error, TEXT("CREATURE_PREVIEW missing %s"), *Kind); FPlatformMisc::RequestExit(false); return false; }
                    const FBox Bounds = Mesh->GetLocalBounds().GetBox();
                    const FVector Min=Bounds.Min, Max=Bounds.Max;
                    const float Scale = (Kind == TEXT("Boss") ? 190.f : 130.f) / FMath::Max(Max.Z-Min.Z,1.f);
                    Actor->SetActorScale3D(FVector(Scale));
                    Actor->SetActorLocation(FVector(0,(Column++-1)*420.f,5.f-Min.Z*Scale));
                    auto* Driver = Cast<UCosmoCreatureAnimInstance>(Mesh->GetAnimInstance());
                    if (!Driver) { FPlatformMisc::RequestExit(false); return false; }
                    Driver->SetManualMovement(0.f); Drivers.Add(Driver);
                    UE_LOG(LogTemp, Display, TEXT("CREATURE_PREVIEW %s local bounds %s %s scale %.3f"), *Kind,*Min.ToString(),*Max.ToString(),Scale);
                }
                for (int32 I=0; I<2; ++I)
                {
                    auto* Light = World->SpawnActor<ADirectionalLight>(FVector(0,0,500), FRotator(-45.f,I ? 145.f : -35.f,0));
                    Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
                    CastChecked<UDirectionalLightComponent>(Light->GetLightComponent())->SetIntensity(I ? 2.f : 4.f);
                }
                auto* Camera = World->SpawnActor<ACameraActor>();
                const FVector Eye(1150,-650,580), Target(0,0,115);
                Camera->SetActorLocationAndRotation(Eye,(Target-Eye).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(65.f);
                Camera->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureMethod=true;
                Camera->GetCameraComponent()->PostProcessSettings.AutoExposureMethod=AEM_Manual;
                Camera->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureBias=true;
                Camera->GetCameraComponent()->PostProcessSettings.AutoExposureBias=0.f;
                Camera->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
                Camera->GetCameraComponent()->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
                PreviewCamera=Camera;
                World->GetFirstPlayerController()->bAutoManageActiveCameraTarget=false;
                World->GetFirstPlayerController()->SetViewTarget(Camera);
            }
            if(PreviewCamera.IsValid()) World->GetFirstPlayerController()->SetViewTarget(PreviewCamera.Get());
            if(APawn* Pawn=World->GetFirstPlayerController()->GetPawn()) Pawn->SetActorHiddenInGame(true);
            Elapsed += FMath::Min(Delta,.1f);
            auto Shot = [](const TCHAR* Name)
            {
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots"),Name),false,false);
                UE_LOG(LogTemp,Display,TEXT("CREATURE_PREVIEW screenshot %s"),Name);
            };
            if (Stage==0 && Elapsed>10.f) { Shot(TEXT("Creatures_Idle.png")); ++Stage; }
            else if (Stage==1 && Elapsed>11.f)
            { for(auto Driver:Drivers) if(Driver.IsValid()) Driver->SetManualMovement(Driver->ReferenceMoveSpeed); ++Stage; }
            else if (Stage==2 && Elapsed>12.f) { Shot(TEXT("Creatures_Move.png")); ++Stage; }
            else if (Stage==3 && Elapsed>14.f)
            {
                for(auto Driver:Drivers) if(Driver.IsValid())
                { Driver->SetManualMovement(0.f); if(Driver->Species==ECosmoCreatureSpecies::CrystalEye) Driver->ApplyControlled(); else Driver->RequestAttack(); }
                ++Stage;
            }
            else if (Stage==4 && Elapsed>14.45f) { Shot(TEXT("Creatures_Actions.png")); ++Stage; }
            if (Elapsed>17.f) { FPlatformMisc::RequestExit(false); return false; }
            return true;
        }));
}
#endif

class FCosmoBridgeModule final : public IModuleInterface
{
#if WITH_EDITOR
    FTSTicker::FDelegateHandle PreviewTicker;
public:
    virtual void StartupModule() override
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("PlanetPreview")) || FParse::Param(FCommandLine::Get(),TEXT("PlanetHUDPreview")))
        { PreviewTicker=StartPlanetPreview(); return; }
        if (FParse::Param(FCommandLine::Get(), TEXT("CreatureAnimationPreview")))
        { PreviewTicker=StartCreatureAnimationPreview(); return; }
        if (!FParse::Param(FCommandLine::Get(), TEXT("CharacterAIPreview"))) return;
        PreviewTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [Elapsed=0.f, Shot=false](float Delta) mutable
            {
                Elapsed += Delta;
                if (Elapsed > 12.f && !Shot && GEngine && GEngine->GameViewport)
                {
                    Shot = true;
                    FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots/CharacterAI_Preview.png")), true, false);
                    UE_LOG(LogTemp, Display, TEXT("CHARACTER_PREVIEW requested HUD screenshot"));
                }
                if (Elapsed > 18.f) { FPlatformMisc::RequestExit(false); return false; }
                return true;
            }));
    }
    virtual void ShutdownModule() override
    { if (PreviewTicker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(PreviewTicker); }
#endif
};
IMPLEMENT_MODULE(FCosmoBridgeModule, CosmoBridge)
