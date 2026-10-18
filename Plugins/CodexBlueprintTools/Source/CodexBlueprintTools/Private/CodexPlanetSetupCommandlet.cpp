#include "CodexPlanetSetupCommandlet.h"
#include "CosmoPlanetGame.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/DamageEvents.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "GestureJsonReceiverComponent.h"
#include "CosmoClientComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformProcess.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

namespace PlanetSetup
{
const FString MapPath=TEXT("/Game/AlienPlanet/Lvl_AlienPlanet");
bool Save(UObject* Asset)
{
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Asset->GetOutermost(),Asset,*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);
}
UBlueprint* Blueprint(UClass* Parent,const FString& Name)
{
    const FString Path=TEXT("/Game/AlienPlanet/Blueprints/")+Name;
    auto* BP=LoadObject<UBlueprint>(nullptr,*Path);
    if(!BP) { BP=FKismetEditorUtilities::CreateBlueprint(Parent,CreatePackage(*Path),FName(*Name),BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass()); FAssetRegistryModule::AssetCreated(BP); }
    FCompilerResultsLog Log; FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::None,&Log);
    if(Log.NumErrors) return nullptr; return BP;
}
UClass* EnemyClass(const FString& Name,ECosmoCreatureSpecies Species,bool Passive=false)
{
    auto* BP=Blueprint(ACosmoPlanetEnemy::StaticClass(),Name); if(!BP) return nullptr;
    auto* Defaults=CastChecked<ACosmoPlanetEnemy>(BP->GeneratedClass->GetDefaultObject());
    Defaults->Species=Species; Defaults->Passive=Passive; Defaults->ConfigureVisuals();
    if(!Save(BP)) return nullptr; return BP->GeneratedClass;
}
void Label(AActor* Actor,const FString& Name,const FString& Folder)
{ Actor->SetActorLabel(Name); Actor->SetFolderPath(FName(*Folder)); }
ACosmoPlanetProp* Prop(UWorld* World,UClass* Class,EPlanetPropKind Kind,FVector Location,int32 Index=0)
{
    auto* Actor=World->SpawnActor<ACosmoPlanetProp>(Class,Location,FRotator::ZeroRotator);
    Actor->Kind=Kind; Actor->Index=Index; Actor->ConfigureVisuals();
    Label(Actor,FString::Printf(TEXT("%s_%d"),*StaticEnum<EPlanetPropKind>()->GetNameStringByValue(int64(Kind)),Index),TEXT("Gameplay/Props"));
    return Actor;
}
int32 BuildMap()
{
    auto* MinionClass=EnemyClass(TEXT("BP_PlanetMinion"),ECosmoCreatureSpecies::Minion);
    auto* BossClass=EnemyClass(TEXT("BP_PlanetBoss"),ECosmoCreatureSpecies::Boss);
    auto* GuideClass=EnemyClass(TEXT("BP_PlanetCrystalEye"),ECosmoCreatureSpecies::CrystalEye,true);
    auto* PropBP=Blueprint(ACosmoPlanetProp::StaticClass(),TEXT("BP_PlanetProp"));
    auto* DirectorBP=Blueprint(ACosmoPlanetDirector::StaticClass(),TEXT("BP_PlanetDirector"));
    if(!MinionClass || !BossClass || !GuideClass || !PropBP || !DirectorBP || !Save(PropBP) || !Save(DirectorBP)) return 1;
    const FString BalancePath=TEXT("/Game/AlienPlanet/DA_NormalBalance");
    auto* Balance=LoadObject<UCosmoPlanetBalance>(nullptr,*BalancePath);
    if(!Balance) { Balance=NewObject<UCosmoPlanetBalance>(CreatePackage(*BalancePath),TEXT("DA_NormalBalance"),RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Balance); }
    if(!Save(Balance)) return 2;
    auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false); if(!World) return 3;
    World->GetWorldSettings()->DefaultGameMode=LoadClass<AGameModeBase>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode_C"));
    FString Json; TSharedPtr<FJsonObject> Layout;
    if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectDir(),TEXT("External/AlienPlanet/scene_layout.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Layout)) return 4;
    int32 Count=0;
    for(const auto& Value:Layout->GetArrayField(TEXT("instances")))
    {
        const auto Spec=Value->AsObject(); const FString Asset=Spec->GetStringField(TEXT("asset"));
        const auto Pos=Spec->GetArrayField(TEXT("location_cm")); const auto Rot=Spec->GetArrayField(TEXT("rotation_deg")); const auto Scale=Spec->GetArrayField(TEXT("scale"));
        FVector Location(Pos[0]->AsNumber(),-Pos[1]->AsNumber(),Pos[2]->AsNumber());
        if(Asset==TEXT("CrashedShip")) Location=FVector(-650,250,0);
        AActor* Actor=nullptr;
        if(Asset==TEXT("Boss_StarcoreGuardian")) continue; // Replace the static placeholder with the animated creature below.
        if(Asset.StartsWith(TEXT("TrainingTarget_")))
        {
            const auto Kind=Asset.EndsWith(TEXT("Shield")) ? EPlanetPropKind::ShieldTarget : Asset.EndsWith(TEXT("Fire")) ? EPlanetPropKind::FireTarget : Asset.EndsWith(TEXT("Wave")) ? EPlanetPropKind::WaveTarget : EPlanetPropKind::LaserTarget;
            Actor=Prop(World,PropBP->GeneratedClass,Kind,Location);
        }
        else if(Asset==TEXT("EnergyBarrier")) Actor=Prop(World,PropBP->GeneratedClass,EPlanetPropKind::Gate,Location,Location.Y>-3000.f ? 0 : 1);
        else if(Asset==TEXT("StarCore")) Actor=Prop(World,PropBP->GeneratedClass,EPlanetPropKind::StarCore,Location);
        else
        {
            auto* Mesh=LoadObject<UStaticMesh>(nullptr,*(TEXT("/Game/AlienPlanet/Meshes/SM_")+Asset));
            if(!Mesh) { UE_LOG(LogTemp,Error,TEXT("PLANET_BUILD missing %s"),*Asset); return 5; }
            auto* Static=World->SpawnActor<AStaticMeshActor>(Location,FRotator(0,Asset==TEXT("CrashedShip") ? 0.f : -Rot[2]->AsNumber(),0));
            auto* Component=Static->GetStaticMeshComponent(); Component->SetStaticMesh(Mesh);
            const bool Cosmetic=Asset==TEXT("GlowPlant") || Asset.StartsWith(TEXT("LavaFissure")) || Asset==TEXT("ScoutRobot") || Asset==TEXT("MissionBeacon");
            Component->SetCollisionEnabled(Cosmetic ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
            Component->SetCollisionProfileName(Cosmetic ? TEXT("NoCollision") : TEXT("BlockAll"));
            Component->SetCanEverAffectNavigation(!Cosmetic);
            Static->SetActorScale3D(FVector(Scale[0]->AsNumber(),Scale[1]->AsNumber(),Scale[2]->AsNumber()));
            Actor=Static;
        }
        Label(Actor,Spec->GetStringField(TEXT("object")),TEXT("Environment/")+Spec->GetStringField(TEXT("zone"))); ++Count;
    }
    auto* Start=World->SpawnActor<APlayerStart>(FVector(0,850,120),FRotator(0,-90,0)); Label(Start,TEXT("PlayerStart_SafeLanding"),TEXT("Gameplay"));
    auto* Director=World->SpawnActor<ACosmoPlanetDirector>(DirectorBP->GeneratedClass); Director->Balance=Balance;
    Label(Director,TEXT("Planet_DifficultyAndProgress"),TEXT("Gameplay"));
    struct FSpawn { float X,Y; int32 Group; };
    for(const FSpawn Spec:TArray<FSpawn>{{-160,3610,1},{-360,4580,2},{360,4720,2},{-350,6150,3},{350,6530,3}})
    {
        auto* Enemy=World->SpawnActor<ACosmoPlanetEnemy>(MinionClass,FVector(Spec.X,-Spec.Y,Spec.Group==3 ? 138.f : 105.f),FRotator(0,90,0));
        Enemy->EncounterGroup=Spec.Group;
        Label(Enemy,FString::Printf(TEXT("Minion_Group%d_%d"),Spec.Group,int32(Spec.X)),TEXT("Gameplay/Enemies"));
    }
    auto* Boss=World->SpawnActor<ACosmoPlanetEnemy>(BossClass,FVector(0,-7170,210),FRotator(0,90,0)); Boss->EncounterGroup=4;
    Label(Boss,TEXT("Boss_AnimatedStarcoreGuardian"),TEXT("Gameplay/Enemies"));
    auto* Guide=World->SpawnActor<ACosmoPlanetEnemy>(GuideClass,FVector(620,-4460,190),FRotator(0,90,0)); Guide->EncounterGroup=0;
    Label(Guide,TEXT("CrystalEye_ScanGuide"),TEXT("Gameplay/Guide"));
    int32 Supply=0;
    for(FVector Position:TArray<FVector>{{280,-3930,65},{-280,-5080,65},{320,-5520,65},{-620,-6660,108},{620,-6940,108}})
        Prop(World,PropBP->GeneratedClass,EPlanetPropKind::Repair,Position,Supply++);
    Prop(World,PropBP->GeneratedClass,EPlanetPropKind::Checkpoint,FVector(-260,-2200,0),1);
    Prop(World,PropBP->GeneratedClass,EPlanetPropKind::Checkpoint,FVector(-280,-5350,0),2);
    // Explicit perimeter collision keeps falls outside the authored floor from trapping the player.
    for(int32 I=0;I<4;++I)
    {
        auto* Boundary=World->SpawnActor<AActor>();
        auto* Box=NewObject<UBoxComponent>(Boundary); Boundary->AddInstanceComponent(Box); Boundary->SetRootComponent(Box);
        Box->SetBoxExtent(I<2 ? FVector(30,4900,500) : FVector(1250,30,500)); Box->SetCollisionProfileName(TEXT("BlockAll"));
        Box->SetCanEverAffectNavigation(false); Box->RegisterComponent();
        Boundary->SetActorLocation(I<2 ? FVector(I ? 1220.f : -1220.f,-3600,400) : FVector(0,I==2 ? 1220.f : -8430.f,400));
        Label(Boundary,FString::Printf(TEXT("RouteBoundary_%d"),I),TEXT("Gameplay/Bounds"));
    }
    for(int32 I=0;I<2;++I)
    {
        auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,1500),FRotator(I ? -30.f : -45.f,I ? 150.f : -35.f,0));
        Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Sun->GetLightComponent()->SetIntensity(I ? .65f : 3.5f);
        CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->SetForwardShadingPriority(I ? 0 : 1);
        Sun->GetLightComponent()->SetLightColor(I ? FLinearColor(.4f,.65f,1.f) : FLinearColor(1.f,.86f,.73f));
        Label(Sun,I ? TEXT("BlueAmbientFill") : TEXT("WarmKeyLight"),TEXT("Lighting"));
    }
    auto* Fog=World->SpawnActor<AExponentialHeightFog>(); Fog->GetComponent()->SetFogDensity(.008f); Fog->GetComponent()->SetStartDistance(1800.f);
    Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.035f,.055f,.08f)); Fog->GetComponent()->SetFogMaxOpacity(.45f);
    Label(Fog,TEXT("VolcanicAtmosphere"),TEXT("Lighting"));
    auto* Post=World->SpawnActor<APostProcessVolume>(); Post->bUnbound=true;
    Post->Settings.bOverride_AutoExposureMethod=true; Post->Settings.AutoExposureMethod=AEM_Manual;
    Post->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure=true; Post->Settings.AutoExposureApplyPhysicalCameraExposure=false;
    Post->Settings.bOverride_AutoExposureBias=true; Post->Settings.AutoExposureBias=0.f;
    Post->Settings.bOverride_BloomIntensity=true; Post->Settings.BloomIntensity=.45f;
    Label(Post,TEXT("StableExposureAndGlow"),TEXT("Lighting"));
    for(float Route:{-500.f,2000.f,3200.f,4300.f,5600.f,6500.f,7350.f})
    {
        auto* Light=World->SpawnActor<APointLight>(FVector(0,-Route,380),FRotator::ZeroRotator);
        Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Light->GetLightComponent()->SetIntensity(1800.f);
        Light->GetLightComponent()->SetLightColor(FLinearColor(.08f,.65f,1.f));
        Light->PointLightComponent->SetAttenuationRadius(900.f); Light->GetLightComponent()->SetCastShadows(false);
        Label(Light,FString::Printf(TEXT("CyanRouteLight_%.0f"),Route),TEXT("Lighting"));
    }
    auto* Bounds=World->SpawnActor<ANavMeshBoundsVolume>(FVector(0,-3600,300),FRotator::ZeroRotator);
    auto* Builder=NewObject<UCubeBuilder>(); Builder->X=2400; Builder->Y=9800; Builder->Z=1000;
    UActorFactory::CreateBrushForVolumeActor(Bounds,Builder);
    Bounds->GetBrushComponent()->SetCollisionProfileName(TEXT("NoCollision"));
    UE_LOG(LogTemp,Display,TEXT("PLANET_NAV bounds=%s world=%s"),*Bounds->GetComponentsBoundingBox(true).ToString(),*World->GetPathName());
    Label(Bounds,TEXT("NavMesh_EntirePlayableRoute"),TEXT("Navigation"));
    FNavigationSystem::AddNavigationSystemToWorld(*World,FNavigationSystemRunMode::EditorMode);
    if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
    {
        Nav->OnWorldInitDone(FNavigationSystemRunMode::EditorMode);
        Nav->OnNavigationBoundsUpdated(Bounds); Nav->Build();
        for(TActorIterator<ANavigationData> It(World);It;++It)
        {
            It->EnsureBuildCompletion(); TArray<FBox> NavBounds; Nav->GetNavigationBoundsForNavData(**It,NavBounds);
            UE_LOG(LogTemp,Display,TEXT("PLANET_NAV data=%s bounds=%s flags=%d agent_index=%d volume_count=%d"),*It->GetPathName(),*It->GetBounds().ToString(),int32(It->GetFlags()),Nav->GetSupportedAgentIndex(*It),NavBounds.Num());
        }
    }
    if(!UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)) return 6;
    UE_LOG(LogTemp,Display,TEXT("PLANET_BUILD_COMPLETE map=%s source_instances=%d enemies=5 boss=1 guide=1 supplies=5"),*MapPath,Count);
    return 0;
}
int32 Verify();
}
UCodexPlanetSetupCommandlet::UCodexPlanetSetupCommandlet() { IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true; }
int32 UCodexPlanetSetupCommandlet::Main(const FString& Params)
{ return Params.Contains(TEXT("Verify")) ? PlanetSetup::Verify() : PlanetSetup::BuildMap(); }

// Verification is kept with generation so it exercises the saved map and actual gameplay classes.
int32 PlanetSetup::Verify()
{
    int32 Checks=0,Failures=0;
    auto Check=[&](bool Pass,const TCHAR* Description)
    { ++Checks; if(!Pass) { ++Failures; UE_LOG(LogTemp,Error,TEXT("PLANET_TEST FAIL %s"),Description); } else UE_LOG(LogTemp,Display,TEXT("PLANET_TEST PASS %s"),Description); };
    auto* Package=LoadPackage(nullptr,*MapPath,LOAD_None);
    auto* World=Package ? UWorld::FindWorldInPackage(Package) : nullptr;
    Check(World!=nullptr,TEXT("saved planet map loads")); if(!World) return 10;
    for(TActorIterator<ANavMeshBoundsVolume> It(World);It;++It) UE_LOG(LogTemp,Display,TEXT("PLANET_NAV_LOAD bounds=%s"),*It->GetComponentsBoundingBox(true).ToString());
    for(TActorIterator<ANavigationData> It(World);It;++It) UE_LOG(LogTemp,Display,TEXT("PLANET_NAV_LOAD data=%s"),*It->GetPathName());
    UWorld* Previous=GWorld; GWorld=World; World->WorldType=EWorldType::Game;
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* Instance=NewObject<UGameInstance>(GEngine); Context.OwningGameInstance=Instance; World->SetGameInstance(Instance);
    if(!World->IsInitialized()) World->InitWorld();
    ON_SCOPE_EXIT { World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); GWorld=Previous; };
    World->SetGameMode(FURL()); World->InitializeActorsForPlay(FURL());
    auto* PC=World->SpawnActor<APlayerController>(); PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));
    auto* PlayerClass=LoadClass<ACharacter>(nullptr,TEXT("/Game/Blueprint/BP_FirstPersonCharacterAI.BP_FirstPersonCharacterAI_C"));
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Character=World->SpawnActor<ACharacter>(PlayerClass,FVector(0,850,120),FRotator(0,-90,0),Spawn);
    auto* Integration=Character ? Character->FindComponentByClass<UFirstPersonAIIntegrationComponent>() : nullptr;
    Check(Integration!=nullptr,TEXT("playable AI character spawns")); if(!Integration) return 11;
    Integration->ShowHUD=false; Integration->EnableLocalSpeech=false;
    Character->FindComponentByClass<UGestureJsonReceiverComponent>()->AutoStart=false;
    Character->FindComponentByClass<UCosmoClientComponent>()->BaseUrl=TEXT("http://127.0.0.1:18997");
    PC->Possess(Character); PC->SetControlRotation(FRotator(0,-90,0));
    World->BeginPlay();
    auto Pump=[&](float Seconds)
    {
        for(int32 I=0;I<FMath::Max(1,FMath::RoundToInt(Seconds*60));++I)
        {
            ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60);
            if(PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(1.f/60);
            FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        }
    };
    auto Move=[&](FVector Position,FRotator Rotation=FRotator(0,-90,0))
    { Character->GetCharacterMovement()->StopMovementImmediately(); Character->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics); PC->SetControlRotation(Rotation); Pump(.1f); };
    Pump(.3f);
    auto* Director=ACosmoPlanetDirector::Find(World);
    Check(Director && Director->Player==Integration,TEXT("offline mission director attaches to the actual player")); if(!Director) return 12;
    int32 Hostile=0,Guides=0,Repairs=0,Targets=0;
    ACosmoPlanetEnemy* First=nullptr; ACosmoPlanetEnemy* Boss=nullptr;
    for(ACosmoPlanetEnemy* Enemy:Director->Enemies)
    {
        if(Enemy->Passive) ++Guides; else ++Hostile;
        if(Enemy->EncounterGroup==1) First=Enemy; if(Enemy->IsBoss()) Boss=Enemy;
        Enemy->SetActorTickEnabled(false);
        Check(Enemy->GetMesh()->GetAnimInstance() && Cast<UCosmoCreatureAnimInstance>(Enemy->GetMesh()->GetAnimInstance()),TEXT("placed creature uses its real ABP"));
    }
    ACosmoPlanetProp* ShieldTarget=nullptr; ACosmoPlanetProp* FireTarget=nullptr; ACosmoPlanetProp* WaveTarget=nullptr; ACosmoPlanetProp* LaserTarget=nullptr; ACosmoPlanetProp* ExitGate=nullptr; ACosmoPlanetProp* Repair=nullptr; ACosmoPlanetProp* Core=nullptr;
    for(ACosmoPlanetProp* Prop:Director->Props)
    {
        if(Prop->Kind==EPlanetPropKind::Repair) { ++Repairs; if(Prop->Index==2) Repair=Prop; }
        if(Prop->Kind==EPlanetPropKind::ShieldTarget) ShieldTarget=Prop;
        if(Prop->Kind==EPlanetPropKind::FireTarget) FireTarget=Prop;
        if(Prop->Kind==EPlanetPropKind::WaveTarget) WaveTarget=Prop;
        if(Prop->Kind==EPlanetPropKind::LaserTarget) LaserTarget=Prop;
        if(Prop->Kind==EPlanetPropKind::Gate && Prop->Index==1) ExitGate=Prop;
        if(Prop->Kind==EPlanetPropKind::StarCore) Core=Prop;
        if(Prop->Kind>=EPlanetPropKind::ShieldTarget && Prop->Kind<=EPlanetPropKind::LaserTarget) ++Targets;
    }
    Check(Hostile==6 && Guides==1,TEXT("five minions, one animated boss and one non-hostile scan guide"));
    Check(Repairs==5 && Targets==4 && ExitGate && Core,TEXT("all training, supplies and final objective are placed"));
    if(!First || !Boss || !ShieldTarget || !FireTarget || !WaveTarget || !LaserTarget || !ExitGate || !Repair || !Core) return 13;
    if(FParse::Param(FCommandLine::Get(),TEXT("CombatOnly")))
    {
        Check(Integration->FreeConversation && Integration->Realtime,TEXT("mission preserves realtime conversation without the old tutorial"));
        Check(Director->TrainingCompleted.IsEmpty() && Director->AnnouncedRegions.IsEmpty(),TEXT("mission requires no training and announces no region tutorial"));
        for(ACosmoPlanetEnemy* Enemy:Director->Enemies) if(!Enemy->Passive)
        {
            Check(Enemy->Active,TEXT("every hostile including the boss is active at spawn"));
            const float Before=Enemy->Health;
            UGameplayStatics::ApplyDamage(Enemy,10.f,nullptr,Character,UCosmoFireDamage::StaticClass());
            Check(Enemy->Health<Before,TEXT("spawned enemy blueprint immediately receives damage"));
        }
        for(ACosmoPlanetProp* Prop:Director->Props)
        {
            if(Prop->Kind>=EPlanetPropKind::ShieldTarget && Prop->Kind<=EPlanetPropKind::LaserTarget)
                Check(Prop->IsHidden() && !Prop->IsActorTickEnabled() && !Prop->GetActorEnableCollision(),TEXT("in-mission training target is hidden and inactive"));
            if(Prop->Kind==EPlanetPropKind::Gate) Check(Prop->Used,TEXT("gate is open without tutorial completion"));
        }
        UGameplayStatics::ApplyDamage(First,1000.f,nullptr,Character,UCosmoFireDamage::StaticClass());
        Check(First->bDead && Director->Kills==1,TEXT("enemy dies and records a kill without training"));
        First->ResetEncounter();
        Check(First->Active && !First->bDead && First->Health==First->MaxHealth,TEXT("reset enemy immediately returns to active combat"));
        UE_LOG(LogTemp,Display,TEXT("PLANET_COMBAT_TEST_RESULT checks=%d failures=%d"),Checks,Failures);
        return Failures ? 20 : 0;
    }
    FHitResult GateHit; FCollisionQueryParams GateQuery(SCENE_QUERY_STAT(PlanetGate),false,Character);
    Check(World->SweepSingleByChannel(GateHit,FVector(0,-4800,130),FVector(0,-5000,130),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,90),GateQuery) && GateHit.GetActor()==ExitGate,TEXT("locked exit gate physically blocks the player capsule"));
    Check(Integration->Health==100.f && Integration->FireDamage==60.f && Integration->WaveDamage==50.f && Integration->LaserDamagePerSecond==45.f && First->Health==70.f && Boss->Health==720.f,TEXT("saved balance applies boosted skill damage and existing health to actual gameplay"));
    Check(FMath::CeilToInt(First->Health/Integration->FireDamage)==2 && First->Health/Integration->LaserDamagePerSecond<1.6f && Integration->FireDamage+Integration->WaveDamage>=First->Health,TEXT("minions require two fireballs, a fire-wave combo or under 1.6 seconds of laser"));
    Check(!First->Active && !Boss->Active,TEXT("safe spawn does not activate distant enemies"));
    Check(Director->AnnouncedRegions.Contains(TEXT("landing")) && Director->RegionTutorialLines.Num()==4,TEXT("landing entry triggers the authored region tutorial table"));
    Check(!Director->AnnounceRegion(TEXT("landing")),TEXT("remaining in or revisiting a region does not repeat its tutorial"));
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    Check(Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate),TEXT("saved map contains navigation data"));
    if(Nav)
    {
        const auto* Data=Nav->GetNavDataForProps(First->GetCharacterMovement()->GetNavAgentPropertiesRef());
        Check(Data && Data->GetConfig().AgentRadius>=149.f,TEXT("navigation clearance matches the largest enemy"));
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,FVector(0,850,20),FVector(0,-6500,50),First);
        Check(Path && Path->IsValid() && !Path->IsPartial(),TEXT("complete navigation path connects spawn to arena"));
    }
    bool FloorContinuous=true,PlayerClear=true; float LastHeight=0.f; bool FirstPoint=true;
    FCollisionQueryParams RouteQuery(SCENE_QUERY_STAT(PlanetRoute),true,Character);
    for(ACosmoPlanetEnemy* Enemy:Director->Enemies) RouteQuery.AddIgnoredActor(Enemy);
    for(ACosmoPlanetProp* Prop:Director->Props) RouteQuery.AddIgnoredActor(Prop);
    for(float Y=800;Y>=-6500;Y-=100)
    {
        FHitResult Floor;
        if(!World->LineTraceSingleByChannel(Floor,FVector(0,Y,550),FVector(0,Y,-200),ECC_Visibility,RouteQuery)) { FloorContinuous=false; continue; }
        if(!FirstPoint && FMath::Abs(Floor.ImpactPoint.Z-LastHeight)>45.f) { PlayerClear=false; UE_LOG(LogTemp,Display,TEXT("PLANET_ROUTE step y=%.0f height=%.1f previous=%.1f actor=%s"),Y,Floor.ImpactPoint.Z,LastHeight,*GetNameSafe(Floor.GetActor())); }
        LastHeight=Floor.ImpactPoint.Z; FirstPoint=false;
        FHitResult Obstacle;
        if(World->SweepSingleByChannel(Obstacle,FVector(0,Y,LastHeight+101),FVector(0,Y-20,LastHeight+101),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeCapsule(34,90),RouteQuery))
        { PlayerClear=false; UE_LOG(LogTemp,Display,TEXT("PLANET_ROUTE blocked y=%.0f height=%.1f actor=%s point=%s"),Y,LastHeight,*GetNameSafe(Obstacle.GetActor()),*Obstacle.ImpactPoint.ToString()); }
    }
    Check(FloorContinuous,TEXT("walking lane has continuous collision from landing to arena"));
    Check(PlayerClear,TEXT("player capsule clears the center route and ramp"));
    Check(Integration->TryActivateShield(),TEXT("shield can start offline"));
    const float HP=Integration->Health,Energy=Integration->ShieldEnergy;
    UGameplayStatics::ApplyDamage(Character,18.f,nullptr,First,UDamageType::StaticClass());
    Check(Integration->Health==HP && Integration->ShieldEnergy<Energy,TEXT("frontal shield blocks damage and spends energy"));
    First->SetActorLocation(Character->GetActorLocation()+FVector(0,200,0));
    UGameplayStatics::ApplyDamage(Character,8.f,nullptr,First,UDamageType::StaticClass());
    Check(Integration->Health==HP-8.f,TEXT("shield leaves attacks from behind unblocked"));
    UGameplayStatics::ApplyDamage(Character,8.f,nullptr,First,UDamageType::StaticClass());
    Check(Integration->Health==HP-8.f,TEXT("brief damage grace prevents simultaneous enemy hits from stacking"));
    First->ResetEncounter();
    Check(Integration->TryActivateLaser() && !Integration->ShieldActive && Integration->LaserActive,TEXT("laser and shield are mutually exclusive"));
    Integration->DeactivateLaser(); Integration->TryActivateShield(); Pump(4.5f);
    Check(!Integration->ShieldActive && Integration->ShieldEnergy<25.f,TEXT("holding shield exhausts it instead of granting permanent invulnerability"));
    Integration->RestoreForCheckpoint();
    Move(FVector(-380,-2200,105)); Integration->TryActivateShield(); Pump(1.6f);
    Check(Director->TrainingCompleted.Contains(TEXT("Shield")),TEXT("shield lesson requires blocking the actual pulse"));
    Check(Director->AnnouncedRegions.Contains(TEXT("training")),TEXT("physically entering training triggers its authored line"));
    Integration->ReleaseAllAbilities(); ShieldTarget->ResetProp();
    Integration->Health=1.f; Integration->ShieldEnergy=100.f; Integration->TryActivateShield(); Pump(3.f);
    Check(ShieldTarget->Used && Integration->Health==1.f,TEXT("shield training remains completable and non-lethal at one health"));
    Integration->Health=100.f;
    Integration->ReleaseAllAbilities();
    Move(FVector(380,-2800,105));
    Check(Integration->TryActivateFire(),TEXT("fire launches a real projectile at the training target"));
    Check(!Integration->TryActivateFire(),TEXT("fire cooldown prevents immediate repeat casts")); Pump(.7f);
    Check(Director->TrainingCompleted.Contains(TEXT("Fire")),TEXT("fire target completes only after projectile collision"));
    UGameplayStatics::ApplyDamage(LaserTarget,Integration->FireDamage,nullptr,Character,UCosmoFireDamage::StaticClass());
    Check(!Director->TrainingCompleted.Contains(TEXT("Laser")),TEXT("wrong ability cannot complete a training station"));
    Move(FVector(-380,-3420,105));
    Check(Integration->TryActivateWave(),TEXT("wave executes its range query")); Pump(.2f);
    Check(Director->TrainingCompleted.Contains(TEXT("Wave")),TEXT("wave target receives the matching area damage"));
    Move(FVector(380,-4000,105)); Integration->TryActivateLaser(); Pump(.25f); Integration->DeactivateLaser();
    Check(Director->TrainingCompleted.Contains(TEXT("Laser")) && Director->TrainingCompleted.Num()==4,TEXT("laser ray hits the final target and completes local training offline"));
    Check(!ExitGate->Used,TEXT("training completion alone cannot skip live sentries"));
    Check(First->Active,TEXT("first sentry becomes active during the canyon encounter"));
    First->ResetEncounter(); First->ActivateEncounter();
    Integration->Health=100.f; First->SetActorTickEnabled(true);
    Move(FVector(-160,-3200,105)); const float BeforeDistance=FVector::Dist2D(Character->GetActorLocation(),First->GetActorLocation());
    Pump(1.f);
    Check(FVector::Dist2D(Character->GetActorLocation(),First->GetActorLocation())<BeforeDistance-20.f,TEXT("minion actually follows its navigation path toward the player"));
    Pump(4.f);
    auto* FirstAnim=Cast<UCosmoCreatureAnimInstance>(First->GetMesh()->GetAnimInstance());
    UE_LOG(LogTemp,Display,TEXT("PLANET_COMBAT minion_state=%d distance=%.1f player_hp=%.1f attacks=%d"),int32(First->State),FVector::Dist2D(Character->GetActorLocation(),First->GetActorLocation()),Integration->Health,FirstAnim->AnimationEventCounts.FindRef(TEXT("Attack")));
    Check(Integration->Health<=92.f && Integration->Health>=76.f && FirstAnim->AnimationEventCounts.FindRef(TEXT("Attack"))>0,TEXT("telegraphed minion attack deals bounded damage at its animation notify"));
    UGameplayStatics::ApplyDamage(First,Integration->FireDamage,nullptr,Character,UCosmoFireDamage::StaticClass());
    Check(FMath::IsNearlyEqual(First->Health,10.f),TEXT("one boosted fireball leaves a normal minion with ten health"));
    UGameplayStatics::ApplyDamage(First,Integration->FireDamage,nullptr,Character,UCosmoFireDamage::StaticClass());
    UGameplayStatics::ApplyDamage(First,100,nullptr,Character,UCosmoFireDamage::StaticClass()); Pump(1.6f);
    Check(First->bDead && Director->Kills==1 && FirstAnim->Dead,TEXT("second boosted fireball kills, plays death and counts exactly once"));
    Move(FVector(0,-4450,105)); Pump(.2f);
    for(ACosmoPlanetEnemy* Enemy:Director->Enemies) if(Enemy->EncounterGroup==2)
    { Check(Enemy->Active,TEXT("second group activates only after training and first fight")); UGameplayStatics::ApplyDamage(Enemy,1000,nullptr,Character,UCosmoFireDamage::StaticClass()); }
    Pump(.2f); Check(ExitGate->Used && Director->Kills==3,TEXT("clearing canyon sentries opens the exit gate"));
    Check(!World->SweepSingleByChannel(GateHit,FVector(0,-4800,130),FVector(0,-5000,130),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,90),GateQuery),TEXT("unlocked gate lets the player capsule pass"));
    Move(FVector(0,-5400,105)); Check(Director->Checkpoint==2,TEXT("boss approach saves the second checkpoint"));
    Check(Director->AnnouncedRegions.Contains(TEXT("ruins")),TEXT("reaching the unlocked ruins triggers its authored line"));
    Integration->Health=100; Move(Repair->GetActorLocation()+FVector(0,0,45)); Pump(.2f);
    Check(!Repair->Used,TEXT("full-health player does not waste a repair pickup"));
    Integration->Health=70; Pump(.2f);
    Check(Repair->Used && Integration->Health==95.f,TEXT("repair pickup restores the tuned 25 health once"));
    Pump(2.1f); UGameplayStatics::ApplyDamage(Character,1000,nullptr,nullptr,UDamageType::StaticClass());
    Check(Integration->Dead,TEXT("lethal combat damage enters the failure state"));
    Integration->HandleGesturePressed(TEXT("Retry"),NAME_None,1); Pump(.2f);
    Check(!Integration->Dead && Integration->Health==100 && Director->Kills==3 && Director->TrainingCompleted.Num()==4 && FMath::Abs(Character->GetActorLocation().Y+5350)<5.f,TEXT("retry restores health and location while keeping completed canyon progress"));
    Check(!Boss->Active && Boss->Health==720.f && !Repair->Used,TEXT("retry resets the upcoming boss and local supplies"));
    Move(FVector(0,-6050,140)); Pump(.2f);
    for(ACosmoPlanetEnemy* Enemy:Director->Enemies) if(Enemy->EncounterGroup==3)
    { Check(Enemy->Active,TEXT("arena sentries activate before the boss")); UGameplayStatics::ApplyDamage(Enemy,1000,nullptr,Character,UCosmoFireDamage::StaticClass()); }
    Move(FVector(0,-6400,140)); Pump(.2f); Boss->SetActorTickEnabled(true);
    Check(Boss->Active && Integration->CurrentBoss==Boss,TEXT("boss activates only after both arena sentries are defeated"));
    Check(Director->AnnouncedRegions.Contains(TEXT("boss")) && Director->AnnouncedRegions.Num()==4,TEXT("real boss entry triggers the fourth region line exactly once"));
    for(int32 I=0;I<180 && Boss->State!=EPlanetEnemyState::Windup;++I) Pump(1.f/60);
    Check(Boss->State==EPlanetEnemyState::Windup && !Boss->AttackMarker->bHiddenInGame,TEXT("boss exposes an attack marker before its pounce"));
    const float BeforeDodge=Integration->Health; const FVector MarkedPoint=Boss->AttackMarker->GetComponentLocation();
    Move(FVector(650,-6300,140)); Pump(.35f);
    Check(Boss->AttackMarker->GetComponentLocation().Equals(MarkedPoint,1.f),TEXT("pounce warning stays fixed after the player moves"));
    Pump(2.2f);
    Check(Integration->Health==BeforeDodge,TEXT("moving out of the marked area avoids pounce damage"));
    const float BossBefore=Boss->Health;
    Boss->State=EPlanetEnemyState::Recover;
    UGameplayStatics::ApplyDamage(Boss,Integration->FireDamage,nullptr,Character,UCosmoFireDamage::StaticClass());
    Check(FMath::IsNearlyEqual(BossBefore-Boss->Health,90.f,.01f),TEXT("boosted fireball deals ninety damage during boss recovery"));
    Boss->State=EPlanetEnemyState::Chase; const float ArmoredBefore=Boss->Health;
    UGameplayStatics::ApplyDamage(Boss,Integration->FireDamage,nullptr,Character,UCosmoFireDamage::StaticClass());
    Check(FMath::IsNearlyEqual(ArmoredBefore-Boss->Health,39.f,.01f),TEXT("boosted fireball deals thirty-nine damage through boss armor"));
    UGameplayStatics::ApplyDamage(Boss,3000,nullptr,Character,UCosmoFireDamage::StaticClass()); Pump(.2f);
    Check(Director->BossDefeated && Director->Kills==6 && !Director->Won,TEXT("boss death records all six kills and unlocks the final objective"));
    Move(Core->GetActorLocation()+FVector(0,0,100)); Pump(3.4f);
    Check(Director->Won && Integration->Won && Director->CoreCharge==1.f,TEXT("standing at the unlocked core completes the playable mission"));
    Integration->HandleGesturePressed(TEXT("Retry"),NAME_None,2); Pump(.2f);
    Check(GEngine->GetWorldContextFromWorldChecked(World).TravelURL.Contains(TEXT("Lvl_AlienPlanet")),
        TEXT("retry gesture requests a fresh level from the victory screen"));
    FString Report=FString::Printf(TEXT("{\"checks\":%d,\"failures\":%d,\"map\":\"%s\",\"difficulty\":\"Normal\"}"),Checks,Failures,*MapPath);
    FFileHelper::SaveStringToFile(Report,*FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("PlanetVerification.json")));
    UE_LOG(LogTemp,Display,TEXT("PLANET_TEST_RESULT checks=%d failures=%d"),Checks,Failures);
    return Failures==0 ? 0 : 20;
}
