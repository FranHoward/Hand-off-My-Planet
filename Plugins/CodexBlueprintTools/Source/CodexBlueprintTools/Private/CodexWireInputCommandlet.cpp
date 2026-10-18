#include "CodexWireInputCommandlet.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "GestureJsonReceiverComponent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_SwitchName.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

UCodexWireInputCommandlet::UCodexWireInputCommandlet()
{
    IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true;
}

static UEdGraphPin* P(UEdGraphNode* N,FName Name,EEdGraphPinDirection Dir)
{
    if(!N)return nullptr;
    for(UEdGraphPin* X:N->Pins)if(X&&X->PinName==Name&&X->Direction==Dir)return X;
    return nullptr;
}
static bool C(UEdGraph* G,UEdGraphPin* A,UEdGraphPin* B)
{
    return G&&A&&B&&G->GetSchema()->TryCreateConnection(A,B);
}
static UK2Node_CallFunction* Call(UEdGraph* G,FName Name,int32 X,int32 Y)
{
    auto* N=NewObject<UK2Node_CallFunction>(G); N->FunctionReference.SetSelfMember(Name);
    G->AddNode(N,true,false); N->CreateNewGuid(); N->NodePosX=X; N->NodePosY=Y;
    N->PostPlacedNewNode(); N->AllocateDefaultPins(); return N;
}
static UK2Node_VariableSet* SetBool(UEdGraph* G,FName Name,bool Value,int32 X,int32 Y)
{
    auto* N=NewObject<UK2Node_VariableSet>(G); N->VariableReference.SetSelfMember(Name);
    G->AddNode(N,true,false); N->CreateNewGuid(); N->NodePosX=X; N->NodePosY=Y;
    N->PostPlacedNewNode(); N->AllocateDefaultPins();
    if(auto* V=P(N,Name,EGPD_Input))V->DefaultValue=Value?TEXT("true"):TEXT("false"); return N;
}
static bool Simplify(UBlueprint* BP,FName Name)
{
    UEdGraph* G=nullptr;
    for(const TObjectPtr<UEdGraph>& Candidate:BP->FunctionGraphs)
        if(Candidate&&Candidate->GetFName()==Name){G=Candidate.Get();break;}
    if(!G)return false;
    UK2Node_FunctionEntry* Entry=nullptr; UK2Node_VariableSet* Set=nullptr; UK2Node_FunctionResult* Result=nullptr;
    for(const TObjectPtr<UEdGraphNode>& NodePtr:G->Nodes)
    {
        UEdGraphNode* N=NodePtr.Get();
        if(auto* X=Cast<UK2Node_FunctionEntry>(N))Entry=X;
        if(auto* X=Cast<UK2Node_VariableSet>(N))Set=X;
        if(auto* X=Cast<UK2Node_FunctionResult>(N))
        { auto* S=P(X,TEXT("Success"),EGPD_Input); if(S&&S->DefaultValue==TEXT("true"))Result=X; }
    }
    if(!Entry||!Result)return false;
    for(auto* X:Entry->Pins)if(X)X->BreakAllPinLinks();
    if(Set)
    {
        for(auto* X:Set->Pins)if(X)X->BreakAllPinLinks();
        C(G,P(Entry,TEXT("then"),EGPD_Output),P(Set,TEXT("execute"),EGPD_Input));
        C(G,P(Set,TEXT("then"),EGPD_Output),P(Result,TEXT("execute"),EGPD_Input));
    }
    else C(G,P(Entry,TEXT("then"),EGPD_Output),P(Result,TEXT("execute"),EGPD_Input));
    TArray<UEdGraphNode*> Remove;
    for(const TObjectPtr<UEdGraphNode>& NodePtr:G->Nodes)
    { UEdGraphNode* N=NodePtr.Get(); if(N!=Entry&&N!=Set&&N!=Result)Remove.Add(N); }
    for(auto* N:Remove)FBlueprintEditorUtils::RemoveNode(BP,N,true);
    return true;
}
static UK2Node_ComponentBoundEvent* AxesEvent(UBlueprint* BP,UEdGraph* G)
{
    for(const TObjectPtr<UEdGraphNode>& NodePtr:G->Nodes)if(auto* E=Cast<UK2Node_ComponentBoundEvent>(NodePtr.Get()))
        if(E->ComponentPropertyName==TEXT("GestureJsonReceiver")&&E->DelegatePropertyName==TEXT("OnAxes"))return E;
    auto* CP=FindFProperty<FObjectProperty>(BP->GeneratedClass,TEXT("GestureJsonReceiver"));
    auto* DP=FindFProperty<FMulticastDelegateProperty>(UGestureJsonReceiverComponent::StaticClass(),TEXT("OnAxes"));
    if(!CP||!DP)return nullptr;
    auto* E=NewObject<UK2Node_ComponentBoundEvent>(G); E->InitializeComponentBoundEventParams(CP,DP);
    G->AddNode(E,true,false); E->CreateNewGuid(); E->NodePosX=2048; E->NodePosY=3264;
    E->PostPlacedNewNode(); E->AllocateDefaultPins(); return E;
}
static bool WireAxes(UBlueprint* BP,UEdGraph* G)
{
    auto* E=AxesEvent(BP,G); if(!E)return false;
    auto* Exec=P(E,TEXT("then"),EGPD_Output); auto* Forward=P(E,TEXT("MoveForward"),EGPD_Output); auto* Turn=P(E,TEXT("TurnRight"),EGPD_Output);
    if(!Exec||!Forward||!Turn)return false; Exec->BreakAllPinLinks(); Forward->BreakAllPinLinks(); Turn->BreakAllPinLinks();
    auto* M=Call(G,TEXT("Move"),2448,3216); auto* A=Call(G,TEXT("Aim"),2752,3216);
    C(G,Exec,P(M,TEXT("execute"),EGPD_Input)); C(G,P(M,TEXT("then"),EGPD_Output),P(A,TEXT("execute"),EGPD_Input));
    C(G,Forward,P(M,TEXT("Forward / Backward"),EGPD_Input)); C(G,Turn,P(A,TEXT("Yaw"),EGPD_Input));
    if(auto* X=P(M,TEXT("Left / Right"),EGPD_Input))X->DefaultValue=TEXT("0.0");
    if(auto* X=P(A,TEXT("Pitch"),EGPD_Input))X->DefaultValue=TEXT("0.0"); return true;
}
static bool WireHands(UEdGraph* G)
{
    UK2Node_ComponentBoundEvent* Pressed=nullptr; UK2Node_ComponentBoundEvent* Released=nullptr; TMap<FName,UK2Node_CallFunction*> Reports;
    for(const TObjectPtr<UEdGraphNode>& NodePtr:G->Nodes)
    {
        UEdGraphNode* N=NodePtr.Get();
        if(auto* E=Cast<UK2Node_ComponentBoundEvent>(N))
        { if(E->DelegatePropertyName==TEXT("OnActionPressed"))Pressed=E; if(E->DelegatePropertyName==TEXT("OnActionReleased"))Released=E; }
        if(auto* F=Cast<UK2Node_CallFunction>(N))if(F->GetNodeTitle(ENodeTitleType::ListView).ToString().Contains(TEXT("ReportAbilitySuccess")))
            if(auto* A=P(F,TEXT("Ability Action"),EGPD_Input))Reports.Add(FName(*A->DefaultValue),F);
    }
    if(!Pressed||!Released)return false;
    auto* PX=P(Pressed,TEXT("then"),EGPD_Output); auto* RX=P(Released,TEXT("then"),EGPD_Output);
    auto* PS=PX&&PX->LinkedTo.Num()?Cast<UK2Node_SwitchName>(PX->LinkedTo[0]->GetOwningNode()):nullptr;
    auto* RS=RX&&RX->LinkedTo.Num()?Cast<UK2Node_SwitchName>(RX->LinkedTo[0]->GetOwningNode()):nullptr;
    if(!PS||!RS)return false;
    TArray<FName> Actions={TEXT("Shield"),TEXT("Fire"),TEXT("Wave"),TEXT("Laser")}; int32 Y=3504;
    for(FName Action:Actions)
    {
        auto* Out=P(PS,Action,EGPD_Output); auto** R=Reports.Find(Action); if(!Out||!R)return false;
        Out->BreakAllPinLinks(); auto* In=P(*R,TEXT("execute"),EGPD_Input); In->BreakAllPinLinks();
        auto* A=Call(G,FName(*(TEXT("Try Activate ")+Action.ToString())),2816,Y);
        C(G,Out,P(A,TEXT("execute"),EGPD_Input)); C(G,P(A,TEXT("then"),EGPD_Output),In); Y+=176;
    }
    auto* SO=P(RS,TEXT("Shield"),EGPD_Output); auto* LO=P(RS,TEXT("Laser"),EGPD_Output); if(!SO||!LO)return false;
    SO->BreakAllPinLinks(); LO->BreakAllPinLinks(); auto* S=SetBool(G,TEXT("bShieldActive"),false,2768,4320); auto* L=SetBool(G,TEXT("bLaserActive"),false,2768,4496);
    C(G,SO,P(S,TEXT("execute"),EGPD_Input)); C(G,LO,P(L,TEXT("execute"),EGPD_Input)); return true;
}
static int32 CleanupOldAbilityConditions(UBlueprint* BP,UEdGraph* G)
{
    TSet<UEdGraphNode*> Remove;
    for(const TObjectPtr<UEdGraphNode>& NodePtr:G->Nodes)
    {
        auto* Event=Cast<UK2Node_CustomEvent>(NodePtr.Get());
        if(!Event||Event->GetNodeTitle(ENodeTitleType::ListView).ToString()!=TEXT("HandleAbilityPressed"))continue;
        if(auto* Then=P(Event,TEXT("then"),EGPD_Output);Then&&Then->LinkedTo.Num())
        {
            UEdGraphNode* Switch=Then->LinkedTo[0]->GetOwningNode();
            Remove.Add(Switch);
            for(UEdGraphPin* Pin:Switch->Pins)
                if(Pin&&Pin->Direction==EGPD_Output)
                    for(UEdGraphPin* Linked:Pin->LinkedTo)if(Linked)Remove.Add(Linked->GetOwningNode());
        }
        Remove.Add(Event);
    }
    for(const TObjectPtr<UEdGraphNode>& NodePtr:G->Nodes)
    {
        auto* Branch=Cast<UK2Node_IfThenElse>(NodePtr.Get());
        if(!Branch||Branch->NodePosY<3400||Branch->NodePosY>4200)continue;
        auto* Exec=P(Branch,TEXT("execute"),EGPD_Input);
        if(Exec&&Exec->LinkedTo.Num())continue;
        if(auto* Condition=P(Branch,TEXT("Condition"),EGPD_Input);Condition&&Condition->LinkedTo.Num())
        {
            UEdGraphNode* NotNode=Condition->LinkedTo[0]->GetOwningNode(); Remove.Add(NotNode);
            if(auto* A=P(NotNode,TEXT("A"),EGPD_Input);A&&A->LinkedTo.Num())Remove.Add(A->LinkedTo[0]->GetOwningNode());
        }
        if(auto* Then=P(Branch,TEXT("then"),EGPD_Output);Then&&Then->LinkedTo.Num())
        {
            if(Cast<UK2Node_VariableSet>(Then->LinkedTo[0]->GetOwningNode()))Remove.Add(Then->LinkedTo[0]->GetOwningNode());
        }
        Remove.Add(Branch);
    }
    for(const TObjectPtr<UEdGraphNode>& NodePtr:G->Nodes)
    {
        auto* CallNode=Cast<UK2Node_CallFunction>(NodePtr.Get());
        if(!CallNode||CallNode->NodePosY<4200)continue;
        if(!CallNode->GetNodeTitle(ENodeTitleType::ListView).ToString().Contains(TEXT("Try Send Cosmo Event")))continue;
        auto* Exec=P(CallNode,TEXT("execute"),EGPD_Input); if(Exec&&!Exec->LinkedTo.Num())Remove.Add(CallNode);
    }
    for(UEdGraphNode* N:Remove)FBlueprintEditorUtils::RemoveNode(BP,N,true);
    return Remove.Num();
}
int32 UCodexWireInputCommandlet::Main(const FString& Params)
{
    auto* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/Blueprint/BP_FirstPersonCharacterAI.BP_FirstPersonCharacterAI")); if(!BP)return 1;
    UEdGraph* G=BP->UbergraphPages.Num()?BP->UbergraphPages[0].Get():nullptr;
    if(Params.Contains(TEXT("Validate"),ESearchCase::IgnoreCase))
    {
        UE_LOG(LogTemp,Display,TEXT("CODEX_VALIDATE blueprint_status=%d event_nodes=%d"),(int32)BP->Status,G?G->Nodes.Num():0);
        for(const TObjectPtr<UEdGraphNode>& NodePtr:G->Nodes)
        {
            UEdGraphNode* N=NodePtr.Get();
            if(auto* E=Cast<UK2Node_ComponentBoundEvent>(N))
            {
                auto* Exec=P(E,TEXT("then"),EGPD_Output); FString Target=Exec&&Exec->LinkedTo.Num()?Exec->LinkedTo[0]->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString():TEXT("NONE");
                UE_LOG(LogTemp,Display,TEXT("CODEX_EVENT %s -> %s"),*E->DelegatePropertyName.ToString(),*Target);
            }
            if(auto* S=Cast<UK2Node_SwitchName>(N))
            {
                for(FName Action:{FName(TEXT("Shield")),FName(TEXT("Fire")),FName(TEXT("Wave")),FName(TEXT("Laser"))})
                {
                    auto* Out=P(S,Action,EGPD_Output); if(!Out)continue;
                    FString Target=Out->LinkedTo.Num()?Out->LinkedTo[0]->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString():TEXT("NONE");
                    UE_LOG(LogTemp,Display,TEXT("CODEX_SWITCH y=%d %s -> %s"),S->NodePosY,*Action.ToString(),*Target);
                }
            }
        }
        for(const TObjectPtr<UEdGraph>& GraphPtr:BP->FunctionGraphs)
        {
            UEdGraph* FG=GraphPtr.Get(); if(!FG||!FG->GetName().StartsWith(TEXT("Try Activate")))continue;
            int32 Branches=0; for(const TObjectPtr<UEdGraphNode>& N:FG->Nodes)if(Cast<UK2Node_IfThenElse>(N.Get()))++Branches;
            UE_LOG(LogTemp,Display,TEXT("CODEX_FUNCTION %s nodes=%d branches=%d"),*FG->GetName(),FG->Nodes.Num(),Branches);
        }
        return 0;
    }
    BP->Modify();
    if(Params.Contains(TEXT("Cleanup"),ESearchCase::IgnoreCase))
    {
        const int32 Removed=CleanupOldAbilityConditions(BP,G);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP); if(BP->Status==BS_Error)return 3;
        auto* Package=BP->GetOutermost(); FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Package,BP,*File,Args))return 4;
        UE_LOG(LogTemp,Display,TEXT("CODEX_CLEANUP removed=%d saved=%s"),Removed,*File); return 0;
    }
    bool F=Simplify(BP,TEXT("Try Activate Shield"))&&Simplify(BP,TEXT("Try Activate Fire"))&&Simplify(BP,TEXT("Try Activate Wave"))&&Simplify(BP,TEXT("Try Activate Laser"));
    bool A=WireAxes(BP,G), H=WireHands(G); if(!F||!A||!H){UE_LOG(LogTemp,Error,TEXT("CODEX_ERROR functions=%d axes=%d hands=%d"),F,A,H);return 2;}
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP); if(BP->Status==BS_Error)return 3;
    auto* Package=BP->GetOutermost(); FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    if(!UPackage::SavePackage(Package,BP,*File,Args))return 4; UE_LOG(LogTemp,Display,TEXT("CODEX_DONE %s"),*File); return 0;
}
