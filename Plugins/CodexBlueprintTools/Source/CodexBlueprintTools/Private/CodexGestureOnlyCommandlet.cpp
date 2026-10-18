#include "CodexGestureOnlyCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "InputMappingContext.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "GestureJsonReceiverComponent.h"
#include "CosmoPlanetGame.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace GestureOnly
{
bool Save(UObject* Asset)
{
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Asset->GetOutermost(),Asset,
        *FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);
}

bool HardwareNode(UEdGraphNode* Node)
{
    const FString Type=Node->GetClass()->GetName();
    if(Type.StartsWith(TEXT("K2Node_Input")) || Type.StartsWith(TEXT("K2Node_EnhancedInput"))) return true;
    if(auto* Call=Cast<UK2Node_CallFunction>(Node))
    {
        const FName Method=Call->FunctionReference.GetMemberName();
        return Method==TEXT("AddMappingContext") || Method==TEXT("ToggleChat")
            || Method==TEXT("IsInputKeyDown") || Method==TEXT("WasInputKeyJustPressed")
            || Method==TEXT("WasInputKeyJustReleased") || Method==TEXT("GetInputMouseDelta");
    }
    return false;
}
}

UCodexGestureOnlyCommandlet::UCodexGestureOnlyCommandlet()
{ IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true; }

int32 UCodexGestureOnlyCommandlet::Main(const FString& Params)
{
    using namespace GestureOnly;
    const bool Verify=Params.Contains(TEXT("Verify"));
    int32 Removed=0,Blueprints=0,Contexts=0,Failures=0;
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    FARFilter Filter; Filter.PackagePaths.Add(TEXT("/Game")); Filter.bRecursivePaths=true; Filter.bRecursiveClasses=true;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
    TArray<FAssetData> Assets; Registry.GetAssets(Filter,Assets);
    for(const auto& Data:Assets)
    {
        auto* Asset=Data.GetAsset();
        if(auto* Context=Cast<UInputMappingContext>(Asset))
        {
            ++Contexts;
            const int32 Count=Context->GetMappings().Num();
            if(Verify) { if(Count) ++Failures; }
            else { Context->Modify(); Context->UnmapAll(); if(!Save(Context)) ++Failures; }
            UE_LOG(LogTemp,Display,TEXT("GESTURE_ONLY_CONTEXT %s mappings=%d"),*Context->GetPathName(),Verify ? Count : Context->GetMappings().Num());
            continue;
        }
        auto* BP=Cast<UBlueprint>(Asset);
        if(!BP || !BP->ParentClass || !BP->ParentClass->IsChildOf(AActor::StaticClass())) continue;
        ++Blueprints;
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        TArray<UEdGraphNode*> Delete;
        for(auto* Graph:Graphs) for(auto& Node:Graph->Nodes) if(Node && HardwareNode(Node.Get())) Delete.Add(Node.Get());
        Removed+=Delete.Num();
        if(Verify) { if(!Delete.IsEmpty()) ++Failures; }
        else
        {
            bool Changed=!Delete.IsEmpty();
            BP->Modify();
            for(auto* Node:Delete) FBlueprintEditorUtils::RemoveNode(BP,Node,true);
            TArray<UEdGraph*> OldChat;
            for(auto& Graph:BP->FunctionGraphs) if(Graph && Graph->GetFName()==TEXT("ToggleChat")) OldChat.Add(Graph);
            for(auto* Graph:OldChat) { FBlueprintEditorUtils::RemoveGraph(BP,Graph); Changed=true; }
            if(BP->SimpleConstructionScript)
                for(auto* Node:BP->SimpleConstructionScript->GetAllNodes())
                    if(auto* Receiver=Cast<UGestureJsonReceiverComponent>(Node->ComponentTemplate))
                        if(!Receiver->AutoStart) { Receiver->AutoStart=true; Changed=true; }
            if(!Changed) continue;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
            FCompilerResultsLog Results;
            FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::None,&Results);
            if(Results.NumErrors || BP->Status==BS_Error || !Save(BP)) ++Failures;
        }
        UE_LOG(LogTemp,Display,TEXT("GESTURE_ONLY_BLUEPRINT %s hardware_nodes=%d"),*BP->GetPathName(),Delete.Num());
    }
    if(!Verify)
    {
        // Refresh the saved training signs without regenerating the level.
        auto* World=UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/AlienPlanet/Lvl_AlienPlanet"));
        if(!World) ++Failures;
        else
        {
            for(TActorIterator<ACosmoPlanetProp> It(World);It;++It) { It->Modify(); It->ConfigureVisuals(); }
            if(!UEditorLoadingAndSavingUtils::SaveMap(World,TEXT("/Game/AlienPlanet/Lvl_AlienPlanet"))) ++Failures;
        }
    }
    UE_LOG(LogTemp,Display,TEXT("GESTURE_ONLY_RESULT verify=%d blueprints=%d contexts=%d hardware_nodes=%d failures=%d"),
        Verify,Blueprints,Contexts,Removed,Failures);
    return Failures ? 1 : 0;
}
