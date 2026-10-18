#include "CodexCompleteCharacterCommandlet.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "GestureJsonReceiverComponent.h"
#include "CosmoClientComponent.h"
#include "AICompanionManager.h"
#include "CosmoCreatureAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/ChildActorComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/GameModeBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "CosmoFireProjectile.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/PlayerInput.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

ACodexCombatTestTarget::ACodexCombatTestTarget()
{
    auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
}
float ACodexCombatTestTarget::TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
    Health -= Amount;
    return Super::TakeDamage(Amount, Event, EventInstigator, Causer);
}

namespace CompleteCharacter
{
constexpr const TCHAR* AssetPath = TEXT("/Game/Blueprint/BP_FirstPersonCharacterAI.BP_FirstPersonCharacterAI");
const FName IntegrationName(TEXT("FirstPersonAIIntegration"));
bool Failed = false;

UEdGraphPin* Pin(UEdGraphNode* Node, FName Name, EEdGraphPinDirection Direction)
{
    if (Node) for (auto* P : Node->Pins) if (P && P->PinName == Name && P->Direction == Direction) return P;
    return nullptr;
}
void Link(UEdGraph* Graph, UEdGraphPin* Output, UEdGraphPin* Input)
{
    if (!Output || !Input || !Graph->GetSchema()->TryCreateConnection(Output, Input))
    {
        Failed = true;
        UE_LOG(LogTemp, Error, TEXT("CHARACTER_WIRE_FAILED %s -> %s"), Output ? *Output->PinName.ToString() : TEXT("null"), Input ? *Input->PinName.ToString() : TEXT("null"));
    }
}
template<class T> T* Node(UEdGraph* Graph, int32 X, int32 Y)
{
    auto* Result = NewObject<T>(Graph);
    Graph->AddNode(Result, true, false); Result->CreateNewGuid();
    Result->NodePosX = X; Result->NodePosY = Y;
    return Result;
}
UK2Node_CallFunction* CallIntegration(UEdGraph* Graph, FName Function, int32 X, int32 Y)
{
    auto* Call = Node<UK2Node_CallFunction>(Graph, X, Y);
    Call->FunctionReference.SetExternalMember(Function, UFirstPersonAIIntegrationComponent::StaticClass());
    Call->PostPlacedNewNode(); Call->AllocateDefaultPins();
    auto* Get = Node<UK2Node_VariableGet>(Graph, X, Y + 200);
    Get->VariableReference.SetSelfMember(IntegrationName);
    Get->PostPlacedNewNode(); Get->AllocateDefaultPins();
    Link(Graph, Pin(Get, IntegrationName, EGPD_Output), Pin(Call, UEdGraphSchema_K2::PN_Self, EGPD_Input));
    return Call;
}
bool Save(UBlueprint* BP)
{
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
    if (BP->Status == BS_Error || Results.NumErrors > 0 || Failed) return false;
    const FString File = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(BP->GetOutermost(), BP, *File, Args);
}
int32 CreateCompanionManager(UBlueprint* CharacterBP)
{
    const FString PackageName = TEXT("/Game/Blueprint/BP_AICompanionManager");
    auto* ManagerBP = LoadObject<UBlueprint>(nullptr, *(PackageName + TEXT(".BP_AICompanionManager")));
    if (!ManagerBP)
    {
        auto* Package = CreatePackage(*PackageName);
        ManagerBP = FKismetEditorUtilities::CreateBlueprint(AAICompanionManager::StaticClass(), Package,
            TEXT("BP_AICompanionManager"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    }
    if (!ManagerBP || !ManagerBP->ParentClass->IsChildOf(AAICompanionManager::StaticClass())) return 20;
    auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Creatures/CrystalEye/Mesh/SK_CrystalEye"));
    auto* AnimBP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Creatures/CrystalEye/ABP_CrystalEye"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Creatures/CrystalEye/Materials/M_CrystalEye"));
    if (!MeshAsset || !AnimBP || !AnimBP->GeneratedClass || !Material || AnimBP->TargetSkeleton != MeshAsset->GetSkeleton()) return 23;
    if (!Save(ManagerBP)) return 20;
    // Set inherited component defaults after compilation, which reinstantiates the generated CDO.
    auto* Mesh = CastChecked<AAICompanionManager>(ManagerBP->GeneratedClass->GetDefaultObject())->CompanionMesh.Get();
    Mesh->Modify();
    Mesh->SetSkeletalMesh(MeshAsset);
    for (int32 Slot = 0; Slot < MeshAsset->GetMaterials().Num(); ++Slot) Mesh->SetMaterial(Slot, Material);
    Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    Mesh->SetAnimInstanceClass(AnimBP->GeneratedClass);
    Mesh->SetRelativeRotation(FRotator(0, -90, 0));
    Mesh->SetRelativeScale3D(FVector(.30f));
    FSavePackageArgs ManagerSave; ManagerSave.TopLevelFlags = RF_Public | RF_Standalone; ManagerSave.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(ManagerBP->GetOutermost(), ManagerBP,
        *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), ManagerSave)) return 20;
    CharacterBP->Modify();
    auto* SCS = CharacterBP->SimpleConstructionScript.Get();
    auto* Child = SCS->FindSCSNode(TEXT("AICompanionManager"));
    if (!Child)
    {
        Child = SCS->CreateNode(UChildActorComponent::StaticClass(), TEXT("AICompanionManager"));
        SCS->AddNode(Child);
    }
    auto* ChildComponent = Cast<UChildActorComponent>(Child->ComponentTemplate);
    if (!ChildComponent) return 21;
    // Refresh the existing child actor template as well as its class.
    ChildComponent->SetChildActorClass(nullptr);
    ChildComponent->SetChildActorClass(ManagerBP->GeneratedClass.Get());
    // Attach to the capsule, so view pitch does not swing the companion above/below the player.
    if (auto* Parent = SCS->FindParentNode(Child))
    {
        Parent->RemoveChildNode(Child);
        SCS->AddNode(Child);
    }
    Child->SetParent(CastChecked<ACharacter>(CharacterBP->GeneratedClass->GetDefaultObject())->GetRootComponent());
    ChildComponent->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(160, 100, 20)));
    if (!Save(CharacterBP)) return 22;
    UE_LOG(LogTemp, Display, TEXT("COMPANION_MANAGER_READY asset=%s mesh=SK_CrystalEye anim=ABP_CrystalEye parent=PlayerCapsule offset=(160,100,20) shared_client=CosmoClient"), *PackageName);
    return 0;
}

int32 VerifyCompanion(UBlueprint* CharacterBP)
{
    int32 Checks = 0, Failures = 0;
    auto Check = [&](bool Pass, const TCHAR* Description)
    {
        ++Checks;
        if (!Pass) { ++Failures; UE_LOG(LogTemp, Error, TEXT("COMPANION_TEST FAIL %s"), Description); }
        else UE_LOG(LogTemp, Display, TEXT("COMPANION_TEST PASS %s"), Description);
    };
    // Use the real saved player and child blueprints without starting microphone, gestures or HTTP.
    UWorld* PreviousWorld = GWorld;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CrystalEyeCompanionVerification"));
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    GWorld = World;
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); GWorld = PreviousWorld; };
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Character = World->SpawnActor<ACharacter>(CharacterBP->GeneratedClass, FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
    Check(Character != nullptr, TEXT("saved player blueprint spawns"));
    if (!Character) return 24;
    AAICompanionManager* Manager = nullptr;
    TInlineComponentArray<UChildActorComponent*> Children(Character);
    int32 Companions = 0;
    for (auto* Child : Children)
        if (auto* Candidate = Cast<AAICompanionManager>(Child->GetChildActor())) { Manager = Candidate; ++Companions; }
    Check(Companions == 1, TEXT("player owns exactly one companion child actor"));
    if (!Manager) return 24;
    Check(Manager->GetParentActor() == Character && Manager->GetAttachParentActor() == Character,
        TEXT("Grok is a child actor attached to the player"));
    Check(Manager->GetRootComponent()->GetAttachParent()->GetAttachParent() == Character->GetRootComponent(),
        TEXT("child component attaches to the player capsule"));
    auto* Mesh = Manager->CompanionMesh.Get();
    auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Creatures/CrystalEye/Mesh/SK_CrystalEye"));
    auto* AnimBP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Creatures/CrystalEye/ABP_CrystalEye"));
    Check(Mesh && MeshAsset && Mesh->GetSkeletalMeshAsset() == MeshAsset, TEXT("Grok uses the Crystal Eye skeletal mesh"));
    if (!Mesh || !MeshAsset || !AnimBP) return 24;
    Check(Mesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint && Mesh->GetAnimClass() == AnimBP->GeneratedClass,
        TEXT("Grok uses ABP_CrystalEye in Animation Blueprint mode"));
    Check(AnimBP->TargetSkeleton == MeshAsset->GetSkeleton(), TEXT("mesh and ABP use the same skeleton"));
    Check(Mesh->GetMaterial(0) == LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Creatures/CrystalEye/Materials/M_CrystalEye")),
        TEXT("Crystal Eye source material is assigned"));
    Check(Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Mesh->GetGenerateOverlapEvents(),
        TEXT("companion does not block movement or ability traces"));
    Check(!Mesh->IsUsingAbsoluteLocation() && !Mesh->IsUsingAbsoluteRotation() && Mesh->GetRelativeScale3D().Equals(FVector(.30f)),
        TEXT("companion inherits the player transform at the configured size"));
    const FTransform Relative = Manager->GetActorTransform().GetRelativeTransform(Character->GetActorTransform());
    Check(Relative.GetLocation().Equals(FVector(160, 100, 20), .01f), TEXT("companion is positioned to the player's front right"));
    Character->SetActorLocationAndRotation(FVector(500, -300, 250), FRotator(0, 90, 0), false, nullptr, ETeleportType::TeleportPhysics);
    Check(Manager->GetActorTransform().GetRelativeTransform(Character->GetActorTransform()).Equals(Relative, .01f),
        TEXT("translation, turning and teleport preserve the follow offset"));
    Check(Manager->ConnectToCharacter(Character) && Manager->Client == Character->FindComponentByClass<UCosmoClientComponent>(),
        TEXT("visual companion shares the player's existing Grok client"));
    Check(Manager->Integration && Manager->Integration->CompanionMesh == Mesh, TEXT("integration animation target points to Crystal Eye"));
    Mesh->InitAnim(true);
    Mesh->SetComponentTickEnabled(false);
    auto* Anim = Cast<UCosmoCreatureAnimInstance>(Mesh->GetAnimInstance());
    Check(Anim && Anim->Species == ECosmoCreatureSpecies::CrystalEye, TEXT("saved ABP creates the Crystal Eye animation driver"));
    if (!Anim) return 24;
    auto Tick = [&](int32 Frames)
    {
        for (int32 I = 0; I < Frames; ++I)
        {
            ++GFrameCounter;
            Mesh->TickAnimation(1.f / 60.f, false); Mesh->RefreshBoneTransforms();
            FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        }
    };
    auto Current = [&]() { return Anim->GetCurrentStateName(Anim->GetStateMachineIndex(TEXT("CreatureStateMachine"))); };
    Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Tick(15); Check(Current() == TEXT("Idle"), TEXT("stationary player plays Crystal Eye Idle"));
    const FVector Body = Mesh->GetSocketLocation(TEXT("body"));
    Tick(30); Check(!Mesh->GetSocketLocation(TEXT("body")).Equals(Body, .001f), TEXT("actual skeletal pose animates"));
    Character->GetCharacterMovement()->Velocity = FVector(150, 0, 0);
    Tick(15);
    Check(Current() == TEXT("Move") && FMath::IsNearlyEqual(Anim->Speed, 150.f), TEXT("player velocity drives FloatForward through the ABP"));
    Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Tick(15); Check(Current() == TEXT("Idle"), TEXT("stopping the player returns the ABP to Idle"));
    Character->Destroy();
    Check(!IsValid(Manager) || Manager->IsActorBeingDestroyed(), TEXT("destroying the player also destroys its companion"));
    UE_LOG(LogTemp, Display, TEXT("COMPANION_TEST_RESULT checks=%d failures=%d"), Checks, Failures);
    return Failures ? 24 : 0;
}
void Event(UBlueprint* BP, UEdGraph* Graph, FName Component, UClass* ComponentClass, FName Delegate, FName Handler, int32 Y)
{
    auto* Property = FindFProperty<FObjectProperty>(BP->GeneratedClass, Component);
    auto* Signature = FindFProperty<FMulticastDelegateProperty>(ComponentClass, Delegate);
    if (!Property || !Signature) { Failed = true; return; }
    auto* Event = Node<UK2Node_ComponentBoundEvent>(Graph, 0, Y);
    Event->InitializeComponentBoundEventParams(Property, Signature);
    Event->PostPlacedNewNode(); Event->AllocateDefaultPins();
    auto* Call = CallIntegration(Graph, Handler, 500, Y);
    Link(Graph, Pin(Event, TEXT("then"), EGPD_Output), Pin(Call, TEXT("execute"), EGPD_Input));
    for (auto* P : Event->Pins)
        if (P && P->Direction == EGPD_Output && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Delegate)
            Link(Graph, P, Pin(Call, P->PinName, EGPD_Input));
}
void Wrapper(UBlueprint* BP, FName Name, FName Method)
{
    UEdGraph* Graph = nullptr;
    for (auto& G : BP->FunctionGraphs) if (G && G->GetFName() == Name) { Graph = G; break; }
    if (!Graph)
    {
        auto* Function = UFirstPersonAIIntegrationComponent::StaticClass()->FindFunctionByName(Method);
        Graph = FBlueprintEditorUtils::CreateNewGraph(BP, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph(BP, Graph, true, Function);
    }
    UK2Node_FunctionEntry* Entry = nullptr; UK2Node_FunctionResult* Result = nullptr;
    for (auto& N : Graph->Nodes)
    {
        if (auto* E = Cast<UK2Node_FunctionEntry>(N.Get())) Entry = E;
        if (auto* R = Cast<UK2Node_FunctionResult>(N.Get())) if (!Result) Result = R;
    }
    if (!Entry) { Failed = true; return; }
    TArray<UEdGraphNode*> Remove;
    for (auto& N : Graph->Nodes)
    {
        if (N.Get() != Entry && N.Get() != Result) Remove.Add(N.Get());
        else for (auto* P : N->Pins) if (P) P->BreakAllPinLinks();
    }
    for (auto* N : Remove) FBlueprintEditorUtils::RemoveNode(BP, N, true);
    Entry->NodePosX = 0; Entry->NodePosY = 0;
    auto* Call = CallIntegration(Graph, Method, 350, 0);
    Link(Graph, Pin(Entry, TEXT("then"), EGPD_Output), Pin(Call, TEXT("execute"), EGPD_Input));
    for (auto* P : Entry->Pins)
    {
        if (!P || P->Direction != EGPD_Output || P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
        FName Param = P->PinName;
        if (Param == TEXT("Event To Send")) Param = TEXT("Event");
        if (Param == TEXT("Required Expected Event")) continue; // Expected-event validation is authoritative in the implementation.
        Link(Graph, P, Pin(Call, Param, EGPD_Input));
    }
    if (Result)
    {
        Result->NodePosX = 750; Result->NodePosY = 0;
        Link(Graph, Pin(Call, TEXT("then"), EGPD_Output), Pin(Result, TEXT("execute"), EGPD_Input));
        for (auto* P : Result->Pins)
            if (P && P->Direction == EGPD_Input && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                Link(Graph, Pin(Call, TEXT("ReturnValue"), EGPD_Output), P);
    }
}
void Comment(UEdGraph* Graph, const FString& Text, int32 Y, int32 Height)
{
    auto* N = Node<UEdGraphNode_Comment>(Graph, -80, Y - 60);
    N->NodeComment = Text; N->NodeWidth = 1200; N->NodeHeight = Height;
}

int32 RuntimeTest(UBlueprint* BP)
{
    int32 Failures = 0, Checks = 0;
    auto Check = [&](bool Condition, const TCHAR* Name)
    {
        ++Checks;
        if (!Condition) { ++Failures; UE_LOG(LogTemp, Error, TEXT("CHARACTER_TEST FAIL %s"), Name); }
        else UE_LOG(LogTemp, Display, TEXT("CHARACTER_TEST PASS %s"), Name);
    };
    UWorld* PreviousWorld = GWorld;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CosmoVerificationWorld"));
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    auto* Instance = NewObject<UGameInstance>(GEngine);
    Context.OwningGameInstance = Instance;
    World->SetGameInstance(Instance);
    GWorld = World;
    ON_SCOPE_EXIT { World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); GWorld = PreviousWorld; };
    World->SetGameMode(FURL());
    World->InitializeActorsForPlay(FURL());
    auto* ControllerClass=LoadClass<APlayerController>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonPlayerController.BP_FirstPersonPlayerController_C"));
    auto* PC = World->SpawnActor<APlayerController>(ControllerClass);
    auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
    PC->SetPlayer(LocalPlayer);
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Character = World->SpawnActor<ACharacter>(BP->GeneratedClass, FVector(0,0,100), FRotator::ZeroRotator, Spawn);
    auto* Integration = Character->FindComponentByClass<UFirstPersonAIIntegrationComponent>();
    auto* Gestures = Character->FindComponentByClass<UGestureJsonReceiverComponent>();
    auto* Cosmo = Character->FindComponentByClass<UCosmoClientComponent>();
    if (!Integration || !Gestures || !Cosmo) return 10;
    Integration->ShowHUD = false; Integration->EnableLocalSpeech = false; Integration->AutoCompleteFinalStage = false;
    Integration->FreeConversation = false; // This regression exercises the opt-in tutorial progression.
    Cosmo->BaseUrl = TEXT("http://127.0.0.1:18765");
    Gestures->AutoStart = false;
    PC->Possess(Character);
    auto Cube = [&](FVector Position, FVector Scale)
    {
        auto* Actor = World->SpawnActor<AActor>();
        auto* Mesh = NewObject<UStaticMeshComponent>(Actor);
        Actor->SetRootComponent(Mesh); Actor->AddInstanceComponent(Mesh);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic")); Mesh->RegisterComponent();
        Actor->SetActorLocation(Position); Actor->SetActorScale3D(Scale);
        return Actor;
    };
    Cube(FVector(0,0,-60), FVector(200,200,1));
    World->BeginPlay();
    auto* ManagerChild = Character->FindComponentByClass<UChildActorComponent>();
    auto* Manager = ManagerChild ? Cast<AAICompanionManager>(ManagerChild->GetChildActor()) : nullptr;
    Check(Manager && Manager->Client == Cosmo && Manager->Integration == Integration,
        TEXT("BP_AICompanionManager reuses the player's client and integration"));
    Integration->SetVoiceListening(true);
    Check(Manager && Manager->AnimationEvent.State == TEXT("listening"), TEXT("Local listening state reaches manager animation callbacks"));
    Integration->SetVoiceListening(false);
    auto Pump = [&](int32 Frames)
    {
        for (int32 I=0; I<Frames; ++I)
        {
            ++GFrameCounter;
            FHttpModule::Get().GetHttpManager().Tick(1.f/60.f);
            World->Tick(LEVELTICK_All, 1.f/60.f);
            if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(1.f/60.f);
            FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
            FPlatformProcess::Sleep(.005f);
        }
    };
    auto Await = [&](TFunction<bool()> Condition)
    { for (int32 I=0; I<1800 && !Condition(); ++I) Pump(1); return Condition(); };
    Check(Integration->Initialized, TEXT("Blueprint BeginPlay initializes integration"));
    Check(PC->PlayerInput && PC->PlayerInput->DebugExecBindings.IsEmpty(),TEXT("Gameplay debug keyboard shortcuts are cleared"));
    Check(Await([&] { return Integration->TutorialStatus.ExpectedEvent == TEXT("movement_completed"); }), TEXT("HTTP reset, game_started acknowledgement, movement stage"));
    const FVector BeforeHardware=Character->GetActorLocation();
    const FRotator BeforeLook=PC->GetControlRotation();
    for(const FKey Key:{EKeys::W,EKeys::A,EKeys::S,EKeys::D,EKeys::F,EKeys::Q,EKeys::E,EKeys::LeftShift,EKeys::LeftMouseButton,EKeys::Enter,EKeys::H,EKeys::R,EKeys::T})
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f));
        Pump(2);
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Released,0.f));
    }
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX,IE_Axis,100.f)); Pump(2);
    Check(FVector::Dist2D(BeforeHardware,Character->GetActorLocation())<1.f && BeforeLook.Equals(PC->GetControlRotation(),.01f),
        TEXT("Real player controller ignores keyboard movement and mouse look"));
    Check(!Integration->ShieldActive && !Integration->LaserActive && !Integration->FireActive && !Integration->WaveActive && !Integration->ChatOpen,
        TEXT("Keyboard and mouse cannot activate abilities or text chat"));
    UE_LOG(LogTemp, Display, TEXT("CHARACTER_TEST_HTTP endpoint=%s status=%d expected=%s error=%s"), *Cosmo->LastResponse.Endpoint, Cosmo->LastResponse.HttpStatus, *Integration->TutorialStatus.ExpectedEvent, *Cosmo->LastError);
    Gestures->OnAxes.Broadcast(1.f, .5f);
    Check(Character->GetPendingMovementInputVector().X > .5f, TEXT("Blueprint OnAxes feeds real movement"));
    Check(PC->GetControlRotation().Yaw > 0.f, TEXT("Blueprint OnAxes turns the camera"));
    PC->SetControlRotation(FRotator::ZeroRotator);
    for (int32 I=0; I<90; ++I)
    {
        Gestures->SubmitJson(FString::Printf(TEXT("{\"version\":3,\"session_id\":\"walking-test\",\"frame_id\":%d,\"timestamp_ms\":%d,\"held_actions\":[\"MoveForward\"],\"events\":[],\"move_axis\":1,\"turn_axis\":0}"),I,I*33));
        Pump(1);
    }
    Gestures->SubmitJson(TEXT("{\"version\":3,\"session_id\":\"walking-test\",\"frame_id\":90,\"timestamp_ms\":3000,\"held_actions\":[],\"events\":[],\"move_axis\":0,\"turn_axis\":0}"));
    Pump(1);
    Check(Await([&] { return Integration->TutorialStatus.ExpectedEvent == TEXT("shield_activated"); }), TEXT("Actual walking completes movement"));
    Gestures->OnActionPressed.Broadcast(TEXT("Shield"), TEXT("LeftShift"), 0);
    Check(Integration->ShieldActive, TEXT("Blueprint Pressed enables shield"));
    const float Before = Integration->Health;
    UGameplayStatics::ApplyDamage(Character, 25, PC, nullptr, UDamageType::StaticClass());
    Check(Integration->Health == Before, TEXT("Shield blocks incoming damage"));
    Gestures->OnActionReleased.Broadcast(TEXT("Shield"), TEXT("LeftShift"), 0);
    Check(!Integration->ShieldActive, TEXT("Blueprint Released disables shield"));
    Check(Await([&] { return Integration->TutorialStatus.ExpectedEvent == TEXT("fire_activated"); }), TEXT("Successful shield advances tutorial"));
    auto BlueprintAbility = [&](FName Name)
    {
        UFunction* Function = Character->FindFunction(Name);
        if (!Function) return false;
        FStructOnScope Buffer(Function);
        Character->ProcessEvent(Function, Buffer.GetStructMemory());
        for (TFieldIterator<FBoolProperty> It(Function); It; ++It)
            if (It->HasAnyPropertyFlags(CPF_OutParm)) return It->GetPropertyValue_InContainer(Buffer.GetStructMemory());
        return false;
    };
    Check(BlueprintAbility(TEXT("Try Activate Fire")), TEXT("Blueprint Fire wrapper spawns projectile"));
    bool ProjectileExists = false;
    for (TActorIterator<ACosmoFireProjectile> It(World); It; ++It) ProjectileExists = true;
    Check(ProjectileExists, TEXT("Fire creates actual projectile actor"));
    Check(!BlueprintAbility(TEXT("Try Activate Fire")), TEXT("Fire cooldown rejects duplicate activation"));
    Check(Await([&] { return Integration->TutorialStatus.ExpectedEvent == TEXT("wave_activated"); }), TEXT("Fire success acknowledgement advances tutorial"));
    Check(BlueprintAbility(TEXT("Try Activate Wave")), TEXT("Blueprint Wave wrapper executes shockwave"));
    Check(!BlueprintAbility(TEXT("Try Activate Wave")), TEXT("Wave cooldown rejects duplicate activation"));
    Check(Await([&] { return Integration->TutorialStatus.ExpectedEvent == TEXT("laser_activated"); }), TEXT("Wave success acknowledgement advances tutorial"));
    Gestures->OnActionPressed.Broadcast(TEXT("Laser"), TEXT("LeftMouseButton"), 0);
    Check(Integration->LaserActive, TEXT("Blueprint Pressed enables laser"));
    Gestures->OnActionHeld.Broadcast(TEXT("Laser"), TEXT("LeftMouseButton"), 0);
    Pump(2);
    Gestures->OnActionReleased.Broadcast(TEXT("Laser"), TEXT("LeftMouseButton"), 0);
    Check(!Integration->LaserActive, TEXT("Blueprint Released stops laser"));
    Check(Await([&] { return Integration->TutorialStatus.ExpectedEvent == TEXT("enemy_scanned"); }), TEXT("Laser success advances tutorial"));
    Check(!Integration->ScanEnemy(), TEXT("Empty scan does not confirm success"));
    FVector Eye; FRotator Look; PC->GetPlayerViewPoint(Eye, Look);
    auto* Target = Cube(Eye + Look.Vector() * 500.f, FVector(1,1,1));
    Target->Tags.Add(TEXT("Enemy")); Target->Tags.Add(TEXT("Boss"));
    Integration->ScanDuration = .2f;
    Pump(2);
    const FString ScanPacket=TEXT("{\"version\":3,\"session_id\":\"utility-test\",\"frame_id\":1,\"timestamp_ms\":1000,\"held_actions\":[],\"events\":[{\"id\":1,\"action\":\"Scan\",\"timestamp_ms\":1000}],\"move_axis\":0,\"turn_axis\":0}");
    Check(Gestures->SubmitJson(ScanPacket),TEXT("Gesture scan utility event is accepted")); Pump(2);
    Check(Integration->ScanProgress>0.f, TEXT("Gesture scan begins only on a visible enemy"));
    Pump(20);
    Check(Integration->ScannedTarget == Target, TEXT("Continuous aim confirms actual scan"));
    Check(Await([&] { return Integration->TutorialStatus.ExpectedEvent == TEXT("tutorial_completed"); }), TEXT("Real scan and nearby tagged boss advance to final stage"));
    Check(!Integration->TutorialStatus.IsComplete, TEXT("Tutorial waits for gameplay final confirmation"));
    Check(Integration->CompleteTutorial(), TEXT("Final confirmation submitted"));
    Check(Await([&] { return Integration->TutorialStatus.IsComplete; }), TEXT("Full nine-stage tutorial completes"));
    Gestures->OnActionPressed.Broadcast(TEXT("Hint"),NAME_None,2);
    Check(Cosmo->HasPendingRequests(),TEXT("Hint gesture submits a companion request"));
    Check(Await([&] { return !Cosmo->HasPendingRequests(); }),TEXT("Hint gesture receives a companion response"));
    Gestures->OnActionPressed.Broadcast(TEXT("Repeat"),NAME_None,3);
    Check(Cosmo->HasPendingRequests(),TEXT("Repeat gesture submits a companion request"));
    Check(Await([&] { return !Cosmo->HasPendingRequests(); }),TEXT("Repeat gesture receives a companion response"));
    Check(Manager && Manager->SubmitPlayerQuestion(TEXT("I already activated every ability.")), TEXT("Manager sends player questions through the existing utterance route")); Pump(60);
    Check(Integration->TutorialStatus.IsComplete && !Integration->Subtitle.IsEmpty(), TEXT("Voice reply produces subtitle without changing progress"));
    Check(Integration->GuideReplySource == TEXT("mock"), TEXT("HTTP reply source reaches Blueprint integration without claiming Grok"));
    Check(Manager && Manager->ReplySource == TEXT("mock") && Manager->Subtitle == Integration->Subtitle && !Manager->AnimationEvent.State.IsEmpty(),
        TEXT("Manager completion callbacks expose reply source, subtitle and animation"));
    Cosmo->CheckHealth();
    Check(Await([&] { return Cosmo->LastResponse.Endpoint == TEXT("/health"); }) && Integration->GuideReplySource == TEXT("mock"),
        TEXT("Health checks preserve actual last reply source"));
    Target->Destroy();
    PC->GetPlayerViewPoint(Eye, Look);
    auto* CombatTarget = World->SpawnActor<ACodexCombatTestTarget>(Eye + Look.Vector() * 400.f, FRotator::ZeroRotator);
    Pump(60);
    Check(Integration->TryActivateFire(), TEXT("Fire can be cast again after cooldown"));
    Pump(20);
    Check(FMath::IsNearlyEqual(CombatTarget->Health, 100.f - Integration->FireDamage), TEXT("Fire projectile collision applies damage once"));
    const float BeforeWave = CombatTarget->Health;
    Check(Integration->TryActivateWave(), TEXT("Wave can be cast again after cooldown"));
    Check(FMath::IsNearlyEqual(CombatTarget->Health, BeforeWave - Integration->WaveDamage), TEXT("Wave applies one area hit per actor"));
    const float BeforeLaser = CombatTarget->Health;
    Integration->TryActivateLaser(); Pump(60); Integration->DeactivateLaser();
    Check(FMath::IsNearlyEqual(CombatTarget->Health, BeforeLaser - Integration->LaserDamagePerSecond, .1f), TEXT("Laser applies frame-rate independent sustained damage"));
    Check(Gestures->StartListening(),TEXT("Local UDP receiver binds gesture port"));
    Pump(1);
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* Sender=Sockets->CreateSocket(NAME_DGram,TEXT("GestureIntegrationTest"),false);
    auto Destination=Sockets->CreateInternetAddr(); bool IpValid=false;
    Destination->SetIp(TEXT("127.0.0.1"),IpValid); Destination->SetPort(Gestures->UdpPort);
    const FString Wire=TEXT("{\"version\":3,\"session_id\":\"udp-test\",\"frame_id\":1,\"timestamp_ms\":1000,\"held_actions\":[\"MoveForward\",\"TurnRight\",\"Shield\"],\"events\":[],\"move_axis\":1,\"turn_axis\":0.5,\"calibrated\":{\"left_hand\":true,\"head\":true},\"tracking\":{\"left_hand\":true,\"right_hand\":true,\"face\":true}}");
    FTCHARToUTF8 Bytes(*Wire); int32 Sent=0;
    Check(Sender && Sender->SendTo(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length(),Sent,*Destination) && Sent==Bytes.Length(),
        TEXT("Real UDP gesture datagram sent on loopback"));
    Pump(2);
    Check(Gestures->Connected && Gestures->MoveAxis==1.f && Gestures->TurnAxis==.5f && Integration->ShieldActive,
        TEXT("UDP datagram drives concurrent movement, head turn and shield through Blueprint"));
    Check(Gestures->LatestFrame.LeftCalibrated && Gestures->LatestFrame.HeadCalibrated && Gestures->LatestFrame.FaceTracked,
        TEXT("Calibration and face tracking reach the gameplay HUD"));
    if(Sender) { Sender->Close(); Sockets->DestroySocket(Sender); }
    Gestures->StopListening();
    const FString ShieldPacket = TEXT("{\"version\":3,\"session_id\":\"character-test\",\"frame_id\":1,\"timestamp_ms\":1000,\"held_actions\":[\"Shield\"],\"events\":[],\"move_axis\":0,\"turn_axis\":0,\"pointer\":null}");
    Check(Gestures->SubmitJson(ShieldPacket), TEXT("Real JSON packet accepted by receiver")); Pump(1);
    Check(Integration->ShieldActive && Integration->GestureConnected, TEXT("JSON dispatch crosses Blueprint into shield and connection state"));
    Check(!Gestures->SubmitJson(ShieldPacket), TEXT("Duplicate JSON frame rejected"));
    const FString LaserPacket = TEXT("{\"version\":3,\"session_id\":\"character-test\",\"frame_id\":2,\"timestamp_ms\":1033,\"held_actions\":[\"Laser\"],\"events\":[],\"move_axis\":0,\"turn_axis\":0,\"pointer\":[0.5,0.5]}");
    Check(Gestures->SubmitJson(LaserPacket), TEXT("Laser pointer JSON packet accepted")); Pump(1);
    Check(!Integration->ShieldActive && Integration->LaserActive && Gestures->LatestFrame.HasPointer, TEXT("JSON switches sustained ability and carries normalized pointer"));
    Pump(95);
    Check(!Integration->LaserActive && !Integration->GestureConnected, TEXT("Actual receiver timeout releases laser automatically"));
    Gestures->OnActionPressed.Broadcast(TEXT("Shield"), TEXT("LeftShift"), 0);
    Gestures->OnActionPressed.Broadcast(TEXT("Laser"), TEXT("LeftMouseButton"), 0);
    Gestures->OnConnectionChanged.Broadcast(false);
    Check(!Integration->ShieldActive && !Integration->LaserActive, TEXT("Gesture disconnect releases sustained abilities"));
    UGameplayStatics::ApplyDamage(Character, 20, PC, Target, UDamageType::StaticClass());
    Check(Integration->Health == Before - 20.f, TEXT("Unshielded real damage updates player health"));
    Integration->SetDanger(false);
    // Separate the lethal hit from the short damage grace period used by the playable level.
    Pump(36);
    UGameplayStatics::ApplyDamage(Character, 1000, PC, Target, UDamageType::StaticClass());
    Check(Integration->Dead && !Integration->TryActivateFire(), TEXT("Death disables combat and releases controls"));
    Pump(30);
    UE_LOG(LogTemp, Display, TEXT("CHARACTER_TEST_RESULT checks=%d failures=%d"), Checks, Failures);
    return Failures == 0 ? 0 : 11;
}
}

UCodexCompleteCharacterCommandlet::UCodexCompleteCharacterCommandlet()
{ IsClient = false; IsEditor = true; IsServer = false; LogToConsole = true; }

int32 UCodexCompleteCharacterCommandlet::Main(const FString& Params)
{
    using namespace CompleteCharacter;
    auto* BP = LoadObject<UBlueprint>(nullptr, AssetPath);
    if (!BP || BP->UbergraphPages.IsEmpty()) return 1;
    if (Params.Contains(TEXT("VerifyCompanion"))) return VerifyCompanion(BP);
    if (Params.Contains(TEXT("CompanionManager"))) return CreateCompanionManager(BP);
    if (Params.Contains(TEXT("RuntimeTest"))) return RuntimeTest(BP);
    if (Params.Contains(TEXT("Inspect")))
    {
        for (auto* SCS : BP->SimpleConstructionScript->GetAllNodes())
            UE_LOG(LogTemp, Display, TEXT("CHARACTER_COMPONENT %s %s"), *SCS->GetVariableName().ToString(), *SCS->ComponentClass->GetName());
        for (auto& G : BP->FunctionGraphs)
            UE_LOG(LogTemp, Display, TEXT("CHARACTER_FUNCTION %s nodes=%d"), *G->GetName(), G->Nodes.Num());
        for (auto& G : BP->UbergraphPages)
            for (auto& N : G->Nodes)
                if (auto* E = Cast<UK2Node_ComponentBoundEvent>(N.Get()))
                    UE_LOG(LogTemp, Display, TEXT("CHARACTER_EVENT %s.%s links=%d"), *E->ComponentPropertyName.ToString(), *E->DelegatePropertyName.ToString(), Pin(E, TEXT("then"), EGPD_Output)->LinkedTo.Num());
        FKismetEditorUtilities::CompileBlueprint(BP);
        UE_LOG(LogTemp, Display, TEXT("CHARACTER_STATUS %d"), int32(BP->Status));
        return BP->Status == BS_Error ? 2 : 0;
    }
    BP->Modify();
    auto* SCS = BP->SimpleConstructionScript.Get();
    auto* IntegrationNode = SCS->FindSCSNode(IntegrationName);
    if (!IntegrationNode)
    { IntegrationNode = SCS->CreateNode(UFirstPersonAIIntegrationComponent::StaticClass(), IntegrationName); SCS->AddNode(IntegrationNode); }
    auto* Integration = CastChecked<UFirstPersonAIIntegrationComponent>(IntegrationNode->ComponentTemplate);
    Integration->FireEffect = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Niagara/Fireball/NS_02.NS_02"));
    Integration->WaveEffect = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Niagara/SoundWave/NS_01.NS_01"));
    Integration->LaserEffect = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Niagara/Laser/NS_01.NS_01"));
    if (!Integration->FireEffect || !Integration->WaveEffect || !Integration->LaserEffect) return 3;
    if (auto* CosmoNode = SCS->FindSCSNode(TEXT("CosmoClient")))
    {
        auto* Client = CastChecked<UCosmoClientComponent>(CosmoNode->ComponentTemplate);
        Client->AutoCheckHealth = false;
        Client->RequestTimeoutSeconds = 30.f; // Accommodates the bounded, optional Grok tool loop.
    }
    if (auto* VFX = SCS->FindSCSNode(TEXT("LaserVFX"))) if (auto* Niagara = Cast<UNiagaraComponent>(VFX->ComponentTemplate)) Niagara->SetAutoActivate(false);
    // Hardware input nodes are removed by CodexGestureOnly after this wiring pass.
    auto* Graph = BP->UbergraphPages[0].Get();
    TArray<UEdGraphNode*> Remove;
    for (auto& N : Graph->Nodes)
        if (N->NodePosY > -650 || Cast<UK2Node_ComponentBoundEvent>(N.Get())) Remove.Add(N.Get());
    for (auto* N : Remove) FBlueprintEditorUtils::RemoveNode(BP, N, true);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    auto* Begin = Node<UK2Node_Event>(Graph, 0, 0);
    Begin->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    Begin->bOverrideFunction = true; Begin->PostPlacedNewNode(); Begin->AllocateDefaultPins();
    auto* Init = CallIntegration(Graph, TEXT("InitializeIntegration"), 500, 0);
    Link(Graph, Pin(Begin, TEXT("then"), EGPD_Output), Pin(Init, TEXT("execute"), EGPD_Input));
    Comment(Graph, TEXT("START: gesture-only local player, camera recognition, HUD, TTS and session"), 0, 350);
    Event(BP, Graph, TEXT("GestureJsonReceiver"), UGestureJsonReceiverComponent::StaticClass(), TEXT("OnAxes"), TEXT("HandleGestureAxes"), 500);
    Event(BP, Graph, TEXT("GestureJsonReceiver"), UGestureJsonReceiverComponent::StaticClass(), TEXT("OnActionPressed"), TEXT("HandleGesturePressed"), 850);
    Event(BP, Graph, TEXT("GestureJsonReceiver"), UGestureJsonReceiverComponent::StaticClass(), TEXT("OnActionHeld"), TEXT("HandleGestureHeld"), 1200);
    Event(BP, Graph, TEXT("GestureJsonReceiver"), UGestureJsonReceiverComponent::StaticClass(), TEXT("OnActionReleased"), TEXT("HandleGestureReleased"), 1550);
    Event(BP, Graph, TEXT("GestureJsonReceiver"), UGestureJsonReceiverComponent::StaticClass(), TEXT("OnConnectionChanged"), TEXT("HandleGestureConnection"), 1900);
    Comment(Graph, TEXT("GESTURES: left-hand movement + head yaw + four abilities; Held updates, release and timeout cleanup"), 500, 1750);
    Event(BP, Graph, TEXT("CosmoClient"), UCosmoClientComponent::StaticClass(), TEXT("OnConnectionChanged"), TEXT("HandleCosmoConnection"), 2400);
    Event(BP, Graph, TEXT("CosmoClient"), UCosmoClientComponent::StaticClass(), TEXT("OnResponse"), TEXT("HandleCosmoResponse"), 2750);
    Event(BP, Graph, TEXT("CosmoClient"), UCosmoClientComponent::StaticClass(), TEXT("OnSpokenLine"), TEXT("HandleSpokenLine"), 3100);
    Event(BP, Graph, TEXT("CosmoClient"), UCosmoClientComponent::StaticClass(), TEXT("OnAnimation"), TEXT("HandleAnimation"), 3450);
    Comment(Graph, TEXT("COSMO: ordered HTTP responses, tutorial state, subtitles/TTS and five companion animation states"), 2400, 1400);
    Wrapper(BP, TEXT("Try Activate Shield"), TEXT("TryActivateShield"));
    Wrapper(BP, TEXT("Try Activate Fire"), TEXT("TryActivateFire"));
    Wrapper(BP, TEXT("Try Activate Wave"), TEXT("TryActivateWave"));
    Wrapper(BP, TEXT("Try Activate Laser"), TEXT("TryActivateLaser"));
    Wrapper(BP, TEXT("Try Send Cosmo Event"), TEXT("TrySendCosmoEvent"));
    const TArray<FName> Methods = { TEXT("DeactivateShield"), TEXT("DeactivateLaser"), TEXT("ReleaseAllAbilities"),
        TEXT("ScanEnemy"), TEXT("ConfirmBossEncounter"), TEXT("CompleteTutorial"), TEXT("SubmitVoiceTranscript"),
        TEXT("SetVoiceListening"), TEXT("RequestHint"), TEXT("RepeatInstruction"), TEXT("SetDanger"), TEXT("SynchronizeGameState") };
    for (const FName Method : Methods) Wrapper(BP, Method, Method);
    if (!Save(BP)) return 4;
    // Ensure the configured default map actually spawns this character.
    auto* Mode = LoadObject<UBlueprint>(nullptr, TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode.BP_FirstPersonGameMode"));
    if (!Mode) return 5;
    CastChecked<AGameModeBase>(Mode->GeneratedClass->GetDefaultObject())->DefaultPawnClass = BP->GeneratedClass;
    if (!Save(Mode)) return 6;
    UE_LOG(LogTemp, Display, TEXT("CHARACTER_COMPLETE saved=%s functions=%d graph_nodes=%d"), AssetPath, BP->FunctionGraphs.Num(), Graph->Nodes.Num());
    return 0;
}
