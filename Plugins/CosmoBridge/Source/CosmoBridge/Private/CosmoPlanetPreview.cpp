#include "CosmoPlanetPreview.h"
#if WITH_EDITOR
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavigationData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CosmoPlanetGame.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FTSTicker::FDelegateHandle StartPlanetPreview()
{
    return FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [Elapsed=0.f,Stage=0,Camera=TWeakObjectPtr<ACameraActor>()](float Delta) mutable
        {
            UWorld* World=nullptr;
            if(GEngine) for(const auto& Context:GEngine->GetWorldContexts())
                if(Context.World() && Context.World()->IsGameWorld() && Context.World()->HasBegunPlay()) { World=Context.World(); break; }
            if(!World || !World->GetFirstPlayerController()) return true;
            auto* PC=World->GetFirstPlayerController();
            if(!Camera.IsValid())
            {
                Camera=World->SpawnActor<ACameraActor>(); PC->bAutoManageActiveCameraTarget=false;
                Camera->GetCameraComponent()->SetFieldOfView(65.f);
                Camera->SetActorLocationAndRotation(FVector(6200,4700,10500),(FVector(0,-3500,100)-FVector(6200,4700,10500)).Rotation());
                if(APawn* Pawn=PC->GetPawn()) Pawn->SetActorHiddenInGame(true);
            }
            PC->SetViewTarget(Camera.Get()); Elapsed+=FMath::Min(Delta,.1f);
            auto View=[&](FVector Eye,FVector Target) { Camera->SetActorLocationAndRotation(Eye,(Target-Eye).Rotation()); };
            auto Shot=[](const TCHAR* Name)
            {
                FString File=Name;
                if(FParse::Param(FCommandLine::Get(),TEXT("RealtimeAVPreview"))) File=TEXT("RealtimeAV_")+File;
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots"),File),true,false);
                UE_LOG(LogTemp,Display,TEXT("PLANET_PREVIEW screenshot %s"),*File);
            };
            if(Stage==0 && Elapsed>8.f)
            {
                auto* Director=ACosmoPlanetDirector::Find(World);
                auto* Enemy=Director && Director->Enemies.Num() ? Director->Enemies[0].Get() : nullptr;
                auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,FVector(0,850,20),FVector(0,-6500,50),Enemy);
                auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
                auto* Data=Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
                UE_LOG(LogTemp,Display,TEXT("PLANET_PREVIEW_NAV actual_game_path=%d partial=%d data=%s bounds=%s"),Path && Path->IsValid(),Path ? Path->IsPartial() : true,*GetNameSafe(Data),Data ? *Data->GetBounds().ToString() : TEXT("missing"));
                Shot(TEXT("Planet_Overview.png")); ++Stage;
            }
            else if(Stage==1 && Elapsed>9.f) { View(FVector(950,1100,850),FVector(-150,0,80)); ++Stage; }
            else if(Stage==2 && Elapsed>11.f) { Shot(TEXT("Planet_Spawn.png")); ++Stage; }
            else if(Stage==3 && Elapsed>12.f) { View(FVector(1800,-2000,1650),FVector(0,-3550,80)); ++Stage; }
            else if(Stage==4 && Elapsed>14.f) { Shot(TEXT("Planet_Training.png")); ++Stage; }
            else if(Stage==5 && Elapsed>15.f) { View(FVector(1700,-5250,1900),FVector(0,-6800,100)); ++Stage; }
            else if(Stage==6 && Elapsed>17.f) { Shot(TEXT("Planet_Arena.png")); ++Stage; }
            else if(Stage==7 && Elapsed>18.f) { View(FVector(0,850,170),FVector(0,-1500,170)); ++Stage; }
            else if(Stage==8 && Elapsed>20.f) { Shot(TEXT("Planet_PlayerView.png")); ++Stage; }
            if(FParse::Param(FCommandLine::Get(),TEXT("PlanetHUDPreview")))
            {
                auto* Director=ACosmoPlanetDirector::Find(World);
                if(Director && Director->Player)
                {
                    if(Stage==9 && Elapsed>21.f) { UGameplayStatics::ApplyDamage(Director->Player->GetOwner(),1000,nullptr,nullptr,UDamageType::StaticClass()); ++Stage; }
                    else if(Stage==10 && Elapsed>22.f) { Shot(TEXT("Planet_Failure.png")); ++Stage; }
                    else if(Stage==11 && Elapsed>23.f) { Director->Player->RetryFromCheckpoint(); ++Stage; }
                    else if(Stage==12 && Elapsed>24.f)
                    { Shot(TEXT("Planet_Retry.png")); UE_LOG(LogTemp,Display,TEXT("PLANET_PREVIEW_RETRY alive=%d health=%.0f"),!Director->Player->Dead,Director->Player->Health); ++Stage; }
                }
                if(Elapsed<26.f) return true;
            }
            if(Elapsed>22.f) { FPlatformMisc::RequestExit(false); return false; }
            return true;
        }));
}
#endif
