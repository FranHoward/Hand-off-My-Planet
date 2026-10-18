#include "CodexCreatureAnimationsCommandlet.h"
#include "CosmoCreatureAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformProcess.h"

namespace CreatureAnimations
{
bool Failed = false;
UEdGraphPin* Pin(UEdGraphNode* N, FName Name, EEdGraphPinDirection Direction)
{ if (N) for (auto* P : N->Pins) if (P && P->PinName == Name && P->Direction == Direction) return P; return nullptr; }
UEdGraphPin* FirstPin(UEdGraphNode* N, EEdGraphPinDirection Direction)
{ if (N) for (auto* P : N->Pins) if (P && P->Direction == Direction) return P; return nullptr; }
void Link(UEdGraph* G, UEdGraphPin* A, UEdGraphPin* B)
{
    if (!A || !B || !G->GetSchema()->TryCreateConnection(A, B))
    { Failed = true; UE_LOG(LogTemp, Error, TEXT("CREATURE_LINK %s -> %s"), A ? *A->PinName.ToString() : TEXT("null"), B ? *B->PinName.ToString() : TEXT("null")); }
}
template<class T> T* AddNode(UEdGraph* G, int32 X, int32 Y)
{
    auto* N = NewObject<T>(G); G->AddNode(N, true, false); N->CreateNewGuid(); N->NodePosX = X; N->NodePosY = Y;
    N->PostPlacedNewNode(); N->AllocateDefaultPins(); return N;
}
UK2Node_VariableGet* GetVariable(UEdGraph* G, FName Name, int32 X, int32 Y)
{
    auto* N = NewObject<UK2Node_VariableGet>(G);
    N->VariableReference.SetSelfMember(Name);
    G->AddNode(N, true, false); N->CreateNewGuid(); N->NodePosX = X; N->NodePosY = Y;
    N->PostPlacedNewNode(); N->AllocateDefaultPins(); return N;
}
bool SaveAsset(UObject* Asset)
{
    auto* Package = Asset->GetOutermost();
    const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Package, Asset, *File, Args);
}
bool Compile(UBlueprint* BP)
{
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Log);
    UE_LOG(LogTemp, Display, TEXT("CREATURE_COMPILE %s errors=%d warnings=%d"), *BP->GetName(), Log.NumErrors, Log.NumWarnings);
    return Log.NumErrors == 0 && BP->Status != BS_Error;
}
ECosmoCreatureAnimState StateFromSuffix(const FString& Suffix)
{
    if (Suffix == TEXT("Walk") || Suffix == TEXT("FloatForward")) return ECosmoCreatureAnimState::Move;
    const int64 Value = StaticEnum<ECosmoCreatureAnimState>()->GetValueByNameString(Suffix);
    check(Value != INDEX_NONE); return ECosmoCreatureAnimState(Value);
}
FName StateName(ECosmoCreatureAnimState State)
{ return FName(*StaticEnum<ECosmoCreatureAnimState>()->GetNameStringByValue(int64(State))); }
int32 StatePriority(ECosmoCreatureAnimState State)
{
    switch(State)
    {
        case ECosmoCreatureAnimState::Death: return 0;
        case ECosmoCreatureAnimState::Controlled: return 1;
        case ECosmoCreatureAnimState::Hit: return 2;
        case ECosmoCreatureAnimState::Pounce: return 3;
        case ECosmoCreatureAnimState::Jump: return 4;
        case ECosmoCreatureAnimState::Attack: return 5;
        default: return 10;
    }
}
void Transition(UAnimationStateMachineGraph* Graph, UAnimStateNode* From, UAnimStateNode* To, FName Variable, float Blend, int32 Priority)
{
    auto* T = AddNode<UAnimStateTransitionNode>(Graph, (From->NodePosX + To->NodePosX) / 2, (From->NodePosY + To->NodePosY) / 2);
    T->CreateConnections(From, To); T->CrossfadeDuration = Blend; T->PriorityOrder = Priority;
    T->bAutomaticRuleBasedOnSequencePlayerInState = false;
    T->bAllowInertializationForSelfTransitions = false;
    auto* Rule = T->BoundGraph.Get();
    auto* Result = CastChecked<UAnimationTransitionGraph>(Rule)->GetResultNode();
    auto* Value = GetVariable(Rule, Variable, -300, 0);
    Link(Rule, Pin(Value, Variable, EGPD_Output), FirstPin(Result, EGPD_Input));
}

bool GenerateSpecies(const FString& SpeciesName)
{
    const FString Folder = TEXT("/Game/Creatures/") + SpeciesName;
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Folder + TEXT("/Mesh/SK_") + SpeciesName));
    if (!Mesh || !Mesh->GetSkeleton()) { UE_LOG(LogTemp, Error, TEXT("Missing creature mesh/skeleton %s"), *SpeciesName); return false; }
    USkeleton* Skeleton = Mesh->GetSkeleton();
    // A mesh reimport can create a second skeleton after an asset rename. Preserve the
    // animation skeleton only when every bone and parent chain is compatible.
    if (auto* Idle = LoadObject<UAnimSequence>(nullptr, *(Folder + TEXT("/Animations/AS_") + SpeciesName + TEXT("_Idle"))))
        if (auto* AnimationSkeleton = Idle->GetSkeleton(); AnimationSkeleton && AnimationSkeleton != Skeleton)
        {
            if (!AnimationSkeleton->IsCompatibleMesh(Mesh))
            { UE_LOG(LogTemp, Error, TEXT("CREATURE_SKELETON incompatible mesh and animation skeleton %s"), *SpeciesName); return false; }
            Mesh->Modify(); Mesh->SetSkeleton(AnimationSkeleton); Skeleton = AnimationSkeleton;
            if (!SaveAsset(Mesh)) return false;
            UE_LOG(LogTemp, Display, TEXT("CREATURE_SKELETON repaired mesh reference %s"), *Skeleton->GetPathName());
        }
    FString Json;
    const FString Source = FPaths::Combine(FPaths::ProjectDir(), TEXT("External/CreatureAnimations"), SpeciesName, TEXT("events.json"));
    TArray<TSharedPtr<FJsonValue>> Specs;
    if (!FFileHelper::LoadFileToString(Json, *Source) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Specs))
    { UE_LOG(LogTemp, Error, TEXT("CREATURE_SPEC failed to read %s"), *Source); return false; }
    TMap<ECosmoCreatureAnimState, UAnimSequence*> Sequences;
    for (const auto& Value : Specs)
    {
        const auto Spec = Value->AsObject(); const FString ClipName = Spec->GetStringField(TEXT("name"));
        auto* Seq = LoadObject<UAnimSequence>(nullptr, *(Folder + TEXT("/Animations/AS_") + ClipName));
        if (!Seq || Seq->GetSkeleton() != Skeleton || FMath::Abs(Seq->GetPlayLength() - Spec->GetNumberField(TEXT("duration_seconds"))) > .001f)
        {
            UE_LOG(LogTemp, Error, TEXT("CREATURE_SEQUENCE %s loaded=%d skeleton=%s expected=%s duration=%.6f expected=%.6f"), *ClipName, Seq != nullptr,
                Seq && Seq->GetSkeleton() ? *Seq->GetSkeleton()->GetPathName() : TEXT("null"), *Skeleton->GetPathName(),
                Seq ? Seq->GetPlayLength() : -1.f, Spec->GetNumberField(TEXT("duration_seconds")));
            return false;
        }
        Seq->Modify(); Seq->bEnableRootMotion = false; Seq->bLoop = Spec->GetBoolField(TEXT("loop"));
        Sequences.Add(StateFromSuffix(ClipName.RightChop(SpeciesName.Len() + 1)), Seq);
        const FName TrackName(TEXT("COSMO Events"));
        bool HasTrack = false;
        for (const auto& Track : Seq->AnimNotifyTracks) if (Track.TrackName == TrackName) HasTrack = true;
        if (HasTrack) UAnimationBlueprintLibrary::RemoveAnimationNotifyEventsByTrack(Seq, TrackName);
        if (!HasTrack) UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Seq, TrackName, FLinearColor(.2f,.8f,1.f));
        for (const auto& EventValue : Spec->GetArrayField(TEXT("events")))
        {
            const auto Event = EventValue->AsObject();
            const float Time = Event->GetNumberField(TEXT("time_seconds"));
            auto* Notify = Cast<UCosmoCreatureAnimNotify>(UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Seq, TrackName, Time, UCosmoCreatureAnimNotify::StaticClass()));
            if (!Notify) return false;
            Notify->EventName = FName(*Event->GetStringField(TEXT("name")));
            UE_LOG(LogTemp, Display, TEXT("CREATURE_NOTIFY %s %s time=%.6f"), *ClipName, *Notify->EventName.ToString(), Time);
        }
        if (!SaveAsset(Seq)) return false;
    }
    const FString BlueprintPath = Folder + TEXT("/ABP_") + SpeciesName;
    auto* BP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
    if (!BP)
    {
        auto* Factory = NewObject<UAnimBlueprintFactory>();
        Factory->ParentClass = UCosmoCreatureAnimInstance::StaticClass(); Factory->TargetSkeleton = Skeleton; Factory->PreviewSkeletalMesh = Mesh;
        auto* Package = CreatePackage(*BlueprintPath);
        BP = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, FName(*(TEXT("ABP_") + SpeciesName)), RF_Public | RF_Standalone, nullptr, GWarn));
        if (!BP) return false;
        FAssetRegistryModule::AssetCreated(BP);
    }
    BP->Modify(); BP->TargetSkeleton = Skeleton; BP->SetPreviewMesh(Mesh);
    UEdGraph* AnimGraph = nullptr;
    for (const auto& G : BP->FunctionGraphs) if (G && G->GetFName() == TEXT("AnimGraph")) AnimGraph = G;
    if (!AnimGraph) { UE_LOG(LogTemp, Error, TEXT("ABP has no AnimGraph")); return false; }
    UAnimGraphNode_Root* Root = nullptr;
    TArray<UEdGraphNode*> Old;
    for (const auto& N : AnimGraph->Nodes)
        if (auto* R = Cast<UAnimGraphNode_Root>(N.Get())) Root = R; else Old.Add(N.Get());
    for (auto* N : Old) FBlueprintEditorUtils::RemoveNode(BP, N, true);
    if (!Root) return false;
    for (auto* P : Root->Pins) P->BreakAllPinLinks();
    Root->NodePosX = 500; Root->NodePosY = 0;
    auto* Machine = AddNode<UAnimGraphNode_StateMachine>(AnimGraph, 0, 0);
    Machine->EditorStateMachineGraph->Rename(TEXT("CreatureStateMachine"), Machine, REN_DontCreateRedirectors);
    Machine->Node.MaxTransitionsPerFrame = 1;
    Machine->Node.bSkipFirstUpdateTransition = false;
    Link(AnimGraph, FirstPin(Machine, EGPD_Output), FirstPin(Root, EGPD_Input));
    auto* SM = Machine->EditorStateMachineGraph.Get();
    TMap<ECosmoCreatureAnimState, UAnimStateNode*> States;
    int32 Index = 0;
    for (uint8 Raw = 0; Raw <= uint8(ECosmoCreatureAnimState::Death); ++Raw)
    {
        const auto State = ECosmoCreatureAnimState(Raw); auto** Sequence = Sequences.Find(State); if (!Sequence) continue;
        auto* N = AddNode<UAnimStateNode>(SM, (Index % 3) * 400 + 250, (Index / 3) * 350); ++Index;
        N->BoundGraph->Rename(*StateName(State).ToString(), N, REN_DontCreateRedirectors);
        N->bAlwaysResetOnEntry = true;
        auto* Player = AddNode<UAnimGraphNode_SequencePlayer>(N->BoundGraph, -320, 0);
        Player->SetAnimationAsset(*Sequence);
        Player->Node.SetLoopAnimation(State == ECosmoCreatureAnimState::Idle || State == ECosmoCreatureAnimState::Move);
        Player->Node.SetPlayRate(1.f);
        if (State == ECosmoCreatureAnimState::Move)
        {
            for (int32 P=0; P<Player->ShowPinForProperties.Num(); ++P)
                if (Player->ShowPinForProperties[P].PropertyName == TEXT("PlayRate")) { Player->ShowPinForProperties[P].bShowPin = true; break; }
            Player->ReconstructNode();
            auto* Rate = GetVariable(N->BoundGraph, TEXT("MovePlayRate"), -650, 160);
            Link(N->BoundGraph, Pin(Rate, TEXT("MovePlayRate"), EGPD_Output), Pin(Player, TEXT("PlayRate"), EGPD_Input));
        }
        Link(N->BoundGraph, FirstPin(Player, EGPD_Output), N->GetPoseSinkPinInsideState());
        States.Add(State, N);
    }
    Link(SM, SM->EntryNode->GetOutputPin(), States[ECosmoCreatureAnimState::Idle]->GetInputPin());
    for (const auto& From : States) for (const auto& To : States)
    {
        if (From.Key == ECosmoCreatureAnimState::Death)
        {
            if (To.Key == ECosmoCreatureAnimState::Idle) Transition(SM, From.Value, To.Value, TEXT("ResetToIdle"), .08f, 0);
            continue;
        }
        if (From.Key == To.Key)
        {
            if (From.Key == ECosmoCreatureAnimState::Hit || From.Key == ECosmoCreatureAnimState::Jump || From.Key == ECosmoCreatureAnimState::Controlled)
                Transition(SM, From.Value, To.Value, TEXT("RestartAction"), 0.f, 0);
            continue;
        }
        const bool ToLocomotion = To.Key == ECosmoCreatureAnimState::Idle || To.Key == ECosmoCreatureAnimState::Move;
        const bool FromLocomotion = From.Key == ECosmoCreatureAnimState::Idle || From.Key == ECosmoCreatureAnimState::Move;
        if (!ToLocomotion && !FromLocomotion && StatePriority(To.Key) >= StatePriority(From.Key)) continue;
        const FName Variable(*(TEXT("Play") + StateName(To.Key).ToString()));
        const float Blend = To.Key == ECosmoCreatureAnimState::Death ? .08f : FromLocomotion && ToLocomotion ? .15f : .06f;
        Transition(SM, From.Value, To.Value, Variable, Blend, StatePriority(To.Key));
    }
    if (Failed || !Compile(BP)) return false;
    auto* Defaults = CastChecked<UCosmoCreatureAnimInstance>(BP->GeneratedClass->GetDefaultObject());
    Defaults->Species = SpeciesName == TEXT("CrystalEye") ? ECosmoCreatureSpecies::CrystalEye : SpeciesName == TEXT("Boss") ? ECosmoCreatureSpecies::Boss : ECosmoCreatureSpecies::Minion;
    Defaults->ReferenceMoveSpeed = SpeciesName == TEXT("Boss") ? 77.381f : 80.882f;
    Defaults->Animations.Reset();
    for (const auto& Pair : Sequences) Defaults->Animations.Add(Pair.Key, Pair.Value);
    if (!SaveAsset(BP)) return false;
    const FString PreviewPath = Folder + TEXT("/BP_") + SpeciesName + TEXT("_AnimationPreview");
    auto* Preview = LoadObject<UBlueprint>(nullptr, *PreviewPath);
    if (!Preview)
    {
        Preview = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), CreatePackage(*PreviewPath), FName(*(TEXT("BP_") + SpeciesName + TEXT("_AnimationPreview"))), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        FAssetRegistryModule::AssetCreated(Preview);
    }
    auto* SCS = Preview->SimpleConstructionScript.Get(); auto* MeshNode = SCS->FindSCSNode(TEXT("CreatureMesh"));
    if (!MeshNode) { MeshNode = SCS->CreateNode(USkeletalMeshComponent::StaticClass(), TEXT("CreatureMesh")); SCS->AddNode(MeshNode); }
    auto* Template = CastChecked<USkeletalMeshComponent>(MeshNode->ComponentTemplate);
    Template->SetSkeletalMesh(Mesh); Template->SetAnimationMode(EAnimationMode::AnimationBlueprint); Template->SetAnimInstanceClass(BP->GeneratedClass);
    Template->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Template->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    if (!Compile(Preview) || !SaveAsset(Preview)) return false;
    UE_LOG(LogTemp, Display, TEXT("CREATURE_ABP_COMPLETE %s states=%d skeleton=%s"), *BP->GetPathName(), States.Num(), *Skeleton->GetPathName());
    return true;
}

int32 VerifyAssetsAndPlayback()
{
    int32 Checks = 0, Failures = 0;
    auto Check = [&](bool Pass, const FString& Description)
    {
        ++Checks;
        if (!Pass) { ++Failures; UE_LOG(LogTemp, Error, TEXT("CREATURE_TEST FAIL %s"), *Description); }
        else UE_LOG(LogTemp, Display, TEXT("CREATURE_TEST PASS %s"), *Description);
    };
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CreatureAnimationVerification"));
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* Instance = NewObject<UGameInstance>(GEngine); Context.OwningGameInstance = Instance; World->SetGameInstance(Instance);
    UWorld* PreviousWorld = GWorld; GWorld = World;
    ON_SCOPE_EXIT { World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); GWorld = PreviousWorld; };
    World->SetGameMode(FURL()); World->InitializeActorsForPlay(FURL()); World->BeginPlay();
    for (const FString Kind : {TEXT("CrystalEye"), TEXT("Minion"), TEXT("Boss")})
    {
        const FString Folder = TEXT("/Game/Creatures/") + Kind;
        auto* BP = LoadObject<UAnimBlueprint>(nullptr, *(Folder + TEXT("/ABP_") + Kind));
        auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, *(Folder + TEXT("/Mesh/SK_") + Kind));
        Check(BP && MeshAsset, Kind + TEXT(" actual ABP and mesh exist"));
        if (!BP || !MeshAsset) continue;
        Check(Compile(BP), Kind + TEXT(" ABP compiles"));
        Check(BP->TargetSkeleton == MeshAsset->GetSkeleton(), Kind + TEXT(" own skeleton matches"));
        auto* Material = LoadObject<UMaterialInterface>(nullptr, *(Folder + TEXT("/Materials/M_") + Kind));
        bool MaterialsAssigned = Material && !MeshAsset->GetMaterials().IsEmpty();
        for (const auto& Slot : MeshAsset->GetMaterials()) MaterialsAssigned &= Slot.MaterialInterface == Material;
        Check(MaterialsAssigned, Kind + TEXT(" source material assigned to every mesh slot"));
        FString Json; TArray<TSharedPtr<FJsonValue>> Specs;
        FFileHelper::LoadFileToString(Json, *FPaths::Combine(FPaths::ProjectDir(), TEXT("External/CreatureAnimations"), Kind, TEXT("events.json")));
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Specs);
        for (const auto& Value : Specs)
        {
            const auto Spec = Value->AsObject(); const FString Name = Spec->GetStringField(TEXT("name"));
            auto* Seq = LoadObject<UAnimSequence>(nullptr, *(Folder + TEXT("/Animations/AS_") + Name));
            Check(Seq && Seq->GetSkeleton() == BP->TargetSkeleton && !Seq->bEnableRootMotion && FMath::Abs(Seq->GetPlayLength() - Spec->GetNumberField(TEXT("duration_seconds"))) < .001f,
                Name + TEXT(" duration, skeleton and in-place settings"));
            if (!Seq) continue;
            Check(Seq->bLoop == Spec->GetBoolField(TEXT("loop")), Name + TEXT(" correct looping flag"));
            for (const auto& EventValue : Spec->GetArrayField(TEXT("events")))
            {
                const auto Event = EventValue->AsObject(); const FName Expected(*Event->GetStringField(TEXT("name")));
                int32 Found = 0;
                for (const auto& Notify : Seq->Notifies)
                    if (auto* Object = Cast<UCosmoCreatureAnimNotify>(Notify.Notify))
                        if (Object->EventName == Expected && FMath::Abs(Notify.GetTime() - Event->GetNumberField(TEXT("time_seconds"))) < .0001f) ++Found;
                Check(Found == 1, Name + TEXT(" exact event ") + Expected.ToString());
            }
        }
        auto* Actor = World->SpawnActor<AActor>();
        auto* Mesh = NewObject<USkeletalMeshComponent>(Actor); Actor->AddInstanceComponent(Mesh); Actor->SetRootComponent(Mesh);
        Mesh->SetSkeletalMesh(MeshAsset);
        Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Mesh->bEnableUpdateRateOptimizations = false;
        Mesh->RegisterComponent(); Mesh->SetComponentTickEnabled(false);
        Mesh->SetAnimInstanceClass(BP->GeneratedClass); Mesh->InitAnim(true);
        auto* Anim = Cast<UCosmoCreatureAnimInstance>(Mesh->GetAnimInstance());
        Check(Anim != nullptr, Kind + TEXT(" spawns native driver through generated ABP"));
        if (!Anim) { Actor->Destroy(); continue; }
        Anim->SetManualMovement(0.f);
        auto Tick = [&](float Seconds)
        {
            const int32 Frames = FMath::Max(1, FMath::RoundToInt(Seconds * 60.f));
            for (int32 I = 0; I < Frames; ++I)
            {
                ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f/60.f);
                Mesh->TickAnimation(1.f/60.f, false); Mesh->RefreshBoneTransforms();
                FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
            }
        };
        auto Current = [&]() { return Anim->GetCurrentStateName(Anim->GetStateMachineIndex(TEXT("CreatureStateMachine"))); };
        Tick(.1f); Check(Current() == TEXT("Idle"), Kind + TEXT(" starts Idle"));
        const FVector BodyStart = Mesh->GetSocketTransform(TEXT("body"), RTS_Component).GetLocation();
        const FVector RootStart = Mesh->GetSocketTransform(TEXT("root"), RTS_Component).GetLocation();
        Tick(.6f);
        Check(!Mesh->GetSocketTransform(TEXT("body"), RTS_Component).GetLocation().Equals(BodyStart, .001f), Kind + TEXT(" actual skeleton pose animates"));
        Check(Mesh->GetSocketTransform(TEXT("root"), RTS_Component).GetLocation().Equals(RootStart, .01f), Kind + TEXT(" root remains in-place"));
        bool Finite = true; for (const auto& Transform : Mesh->GetComponentSpaceTransforms()) if (Transform.ContainsNaN()) Finite = false;
        Check(Finite, Kind + TEXT(" finite evaluated bone transforms"));
        Anim->SetManualMovement(Anim->ReferenceMoveSpeed); Tick(.2f);
        Check(Current() == TEXT("Move") && FMath::IsNearlyEqual(Anim->MovePlayRate, 1.f, .001f), Kind + TEXT(" movement enters Move at matched play rate"));
        Anim->SetManualMovement(Anim->ReferenceMoveSpeed * 2.f); Tick(.1f);
        Check(FMath::IsNearlyEqual(Anim->MovePlayRate, 2.f, .001f), Kind + TEXT(" movement scales playback to speed"));
        Anim->SetManualMovement(0.f); Tick(.2f);
        Check(Current() == TEXT("Idle"), Kind + TEXT(" stopping returns Idle"));
        if (Kind == TEXT("CrystalEye"))
        {
            Check(!Anim->RequestAttack() && !Anim->RequestPounce(), Kind + TEXT(" unsupported actions rejected"));
            Check(Anim->ApplyControlled(), Kind + TEXT(" confirmed hit can apply control")); Tick(.5f);
            Check(Current() == TEXT("Controlled") && Anim->Controlled, Kind + TEXT(" control not released early"));
            Check(!Anim->RequestHit(), Kind + TEXT(" unsupported hit cannot interrupt control"));
            Tick(.55f); Check(!Anim->Controlled && Current() == TEXT("Idle"), Kind + TEXT(" control ends at its own one-second deadline"));
            Check(Anim->AnimationEventCounts.FindRef(TEXT("ControlBegin")) == 1 && Anim->AnimationEventCounts.FindRef(TEXT("ControlEnd")) == 1, Kind + TEXT(" control lifecycle emitted exactly once"));
            Anim->ApplyControlled(); Tick(.6f); Anim->ApplyControlled(); Tick(.6f);
            Check(Anim->Controlled, Kind + TEXT(" repeated confirmed control restarts full duration"));
            Tick(.45f); Check(!Anim->Controlled, Kind + TEXT(" refreshed control expires"));
        }
        else
        {
            int32 Attacks = Anim->AnimationEventCounts.FindRef(TEXT("Attack"));
            Check(Anim->RequestAttack(), Kind + TEXT(" starts attack")); Tick(.15f);
            Check(Current() == TEXT("Attack") && !Anim->RequestAttack(), Kind + TEXT(" attack cannot be retriggered every frame"));
            Tick(1.4f);
            Check(Current() == TEXT("Idle") && Anim->AnimationEventCounts.FindRef(TEXT("Attack")) == Attacks + 1, Kind + TEXT(" attack notify once and automatic recovery"));
            Attacks = Anim->AnimationEventCounts.FindRef(TEXT("Attack"));
            Anim->RequestAttack(); Tick(.1f); Check(Anim->RequestHit(), Kind + TEXT(" hit interrupts attack")); Tick(1.f);
            Check(Current() == TEXT("Idle") && Anim->AnimationEventCounts.FindRef(TEXT("Attack")) == Attacks, Kind + TEXT(" interrupted attack cannot emit stale gameplay hit"));
            Anim->RequestHit(); Tick(.2f); Check(Anim->RequestHit(), Kind + TEXT(" repeated hit restarts reaction")); Tick(.1f);
            Check(Current() == TEXT("Hit"), Kind + TEXT(" restarted hit remains visible")); Tick(.8f);
            Anim->SetManualMovement(0.f, true); Tick(1.6f);
            Check(Current() == TEXT("Jump"), Kind + TEXT(" airborne jump holds pose until grounded"));
            Anim->SetManualMovement(0.f, false); Tick(.2f);
            Check(Current() == TEXT("Idle"), Kind + TEXT(" grounded jump recovers"));
            if (Kind == TEXT("Boss"))
            {
                Check(Anim->RequestPounce(), Kind + TEXT(" starts pounce")); Tick(1.8f);
                Check(Current() == TEXT("Idle"), Kind + TEXT(" pounce recovers"));
                for (FName Event : {FName(TEXT("Launch")), FName(TEXT("Hit_Controlled_1s"))})
                    Check(Anim->AnimationEventCounts.FindRef(Event) == 1, Kind + TEXT(" pounce emits ") + Event.ToString() + TEXT(" once"));
            }
            else Check(!Anim->RequestPounce(), Kind + TEXT(" no unsupported pounce"));
            Check(Anim->SetDead(), Kind + TEXT(" death requested")); Tick(2.5f);
            const TArray<FTransform> FinalPose = Mesh->GetComponentSpaceTransforms(); Tick(.5f);
            bool Held = FinalPose.Num() == Mesh->GetComponentSpaceTransforms().Num();
            for (int32 I=0; Held && I<FinalPose.Num(); ++I) Held = FinalPose[I].Equals(Mesh->GetComponentSpaceTransforms()[I], .001f);
            Check(Current() == TEXT("Death") && Held, Kind + TEXT(" non-looping death holds final pose"));
            Check(!Anim->RequestAttack() && !Anim->RequestHit() && !Anim->RequestJump(), Kind + TEXT(" death blocks normal action transitions"));
            Anim->ResetForRespawn(); Tick(.2f); Check(Current() == TEXT("Idle") && !Anim->Dead, Kind + TEXT(" explicit respawn reset restores Idle"));
        }
        Actor->Destroy();
    }
    const FString Summary = FString::Printf(TEXT("{\"checks\":%d,\"failures\":%d}"), Checks, Failures);
    FFileHelper::SaveStringToFile(Summary, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CreatureAnimationVerification.json")));
    UE_LOG(LogTemp, Display, TEXT("CREATURE_TEST_RESULT checks=%d failures=%d"), Checks, Failures);
    return Failures == 0 ? 0 : 2;
}
}

UCodexCreatureAnimationsCommandlet::UCodexCreatureAnimationsCommandlet()
{ IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true; }

int32 UCodexCreatureAnimationsCommandlet::Main(const FString& Params)
{
    if (Params.Contains(TEXT("Verify"))) return CreatureAnimations::VerifyAssetsAndPlayback();
    for (const FString Species : {TEXT("CrystalEye"), TEXT("Minion"), TEXT("Boss")})
        if (!CreatureAnimations::GenerateSpecies(Species)) return 1;
    return 0;
}
