#include "CosmoClientComponent.h"
#include "CosmoRuntimePaths.h"
#include "CosmoServiceSubsystem.h"
#include "Engine/GameInstance.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformProcess.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
const TSet<FString>& ValidEvents()
{
    static const TSet<FString> Names = {
        TEXT("game_started"), TEXT("movement_completed"), TEXT("shield_activated"),
        TEXT("fire_activated"), TEXT("wave_activated"), TEXT("laser_activated"),
        TEXT("enemy_scanned"), TEXT("boss_encountered"), TEXT("tutorial_completed"),
        TEXT("player_damaged"), TEXT("player_in_danger"), TEXT("danger_cleared")
    };
    return Names;
}

TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (Object.IsValid() && Object->TryGetObjectField(Name, Value) && Value)
    {
        return *Value;
    }
    return nullptr;
}

void OptionalString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, FString& Out)
{
    if (Object.IsValid())
    {
        Object->TryGetStringField(Name, Out);
    }
}

void StringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, TArray<FString>& Out)
{
    Out.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object.IsValid() || !Object->TryGetArrayField(Name, Values) || !Values)
    {
        return;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString Text;
        if (Value.IsValid() && Value->TryGetString(Text))
        {
            Out.Add(Text);
        }
    }
}

void ReadStatus(const TSharedPtr<FJsonObject>& Root, FCosmoTutorialStatus& Out)
{
    TSharedPtr<FJsonObject> Status = ObjectField(Root, TEXT("status"));
    if (!Status.IsValid())
    {
        Status = ObjectField(Root, TEXT("tutorial"));
    }
    if (!Status.IsValid())
    {
        Status = Root;
    }
    OptionalString(Status, TEXT("tutorial_id"), Out.TutorialId);
    OptionalString(Status, TEXT("current_stage_id"), Out.CurrentStageId);
    OptionalString(Status, TEXT("current_stage_name"), Out.CurrentStageName);
    OptionalString(Status, TEXT("expected_event"), Out.ExpectedEvent);
    OptionalString(Status, TEXT("last_instruction"), Out.LastInstruction);
    OptionalString(Status, TEXT("last_hint"), Out.LastHint);
    StringArray(Status, TEXT("completed_stage_ids"), Out.CompletedStageIds);
    StringArray(Status, TEXT("available_hints"), Out.AvailableHints);
    Status->TryGetBoolField(TEXT("is_complete"), Out.IsComplete);
}

void ReadAnimation(const TSharedPtr<FJsonObject>& Root, FCosmoAnimationEvent& Out)
{
    TSharedPtr<FJsonObject> Animation = ObjectField(Root, TEXT("animation_event"));
    if (!Animation.IsValid())
    {
        const TSharedPtr<FJsonObject> Tutorial = ObjectField(Root, TEXT("tutorial"));
        Animation = ObjectField(Tutorial, TEXT("animation_event"));
    }
    OptionalString(Animation, TEXT("state"), Out.State);
    OptionalString(Animation, TEXT("animation"), Out.Animation);
    OptionalString(Animation, TEXT("reason"), Out.Reason);
    OptionalString(Animation, TEXT("line"), Out.Line);
    OptionalString(Animation, TEXT("stage_id"), Out.StageId);
}
}

UCosmoClientComponent::UCosmoClientComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UCosmoClientComponent::BeginPlay()
{
    Super::BeginPlay();
    FString LocalUrl = BaseUrl;
    LocalUrl.RemoveFromEnd(TEXT("/"));
    if (AutoLaunchService && !IsRunningCommandlet() && (!FApp::IsUnattended() || FParse::Param(FCommandLine::Get(),TEXT("UIMenuServiceTest")))
        && LocalUrl == TEXT("http://127.0.0.1:8765"))
    {
        WaitingForService = true;
        StartupDeadline = FPlatformTime::Seconds() + 20.;
        ProbeService();
    }
    if (AutoCheckHealth)
    {
        CheckHealth();
    }
}

void UCosmoClientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    EndingPlay = true;
    GetWorld()->GetTimerManager().ClearTimer(ReadinessTimer);
    if (ReadinessRequest.IsValid())
    {
        ReadinessRequest->OnProcessRequestComplete().Unbind();
        ReadinessRequest->CancelRequest();
        ReadinessRequest.Reset();
    }
    ServiceReady = false;

    QueuedRequests.Reset();
    for (const FHttpRequestPtr& Request : ActiveRequests)
    {
        if (Request.IsValid())
        {
            Request->OnProcessRequestComplete().Unbind();
            Request->CancelRequest();
        }
    }
    ActiveRequests.Reset();
    Super::EndPlay(EndPlayReason);
}

void UCosmoClientComponent::FailService(const FString& Error)
{
    WaitingForService = false;
    QueuedRequests.Reset();
    LastError = Error;
    SetConnected(false);
    FCosmoResponse Result;
    Result.Endpoint = TEXT("/health");
    Result.Error = Error;
    LastResponse = Result;
    UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
    OnResponse.Broadcast(Result);
}

void UCosmoClientComponent::ProbeService()
{
    if (EndingPlay) return;
    ReadinessRequest = FHttpModule::Get().CreateRequest();
    ReadinessRequest->SetURL(TEXT("http://127.0.0.1:8765/health"));
    ReadinessRequest->SetVerb(TEXT("GET"));
    ReadinessRequest->SetTimeout(1.f);
    ReadinessRequest->OnProcessRequestComplete().BindWeakLambda(this,
        [this](FHttpRequestPtr, FHttpResponsePtr Response, bool Success)
    {
        if (EndingPlay) return;
        TSharedPtr<FJsonObject> Health;
        bool Ok = false;
        FString ServiceName;
        const bool HasResponse = Success && Response.IsValid();
        if (HasResponse && Response->GetResponseCode() == 200
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Health)
            && Health.IsValid())
        {
            Health->TryGetBoolField(TEXT("ok"), Ok);
            Health->TryGetStringField(TEXT("service"), ServiceName);
        }
        if (Ok && ServiceName == TEXT("ai-companion-tutorial"))
        {
            ServiceReady = true;
            WaitingForService = false;
            StartNextRequest();
            return;
        }
        if (HasResponse) { FailService(TEXT("Port 8765 is occupied by an incompatible service.")); return; }
        if (!LaunchAttempted)
        {
            LaunchAttempted = true;
            auto* Service=GetWorld()->GetGameInstance()->GetSubsystem<UCosmoServiceSubsystem>();
            FString Error;
            if(!Service || !Service->EnsureStarted(Error)) { FailService(Error.IsEmpty() ? TEXT("COSMO service unavailable.") : Error); return; }
        }
        auto* Service=GetWorld()->GetGameInstance()->GetSubsystem<UCosmoServiceSubsystem>();
        if (!Service || !Service->IsRunning() || FPlatformTime::Seconds() >= StartupDeadline)
        {
            FailService(FString::Printf(TEXT("COSMO startup failed or timed out. See %s/CosmoService.log."), *CosmoRuntimePaths::LogDir()));
            return;
        }
        GetWorld()->GetTimerManager().SetTimer(ReadinessTimer, this, &UCosmoClientComponent::ProbeService, .25f, false);
    });
    if (!ReadinessRequest->ProcessRequest())
        ReadinessRequest->OnProcessRequestComplete().ExecuteIfBound(ReadinessRequest, nullptr, false);
}

FString UCosmoClientComponent::GameEventToString(ECosmoGameEvent Event)
{
    switch (Event)
    {
        case ECosmoGameEvent::GameStarted: return TEXT("game_started");
        case ECosmoGameEvent::MovementCompleted: return TEXT("movement_completed");
        case ECosmoGameEvent::ShieldActivated: return TEXT("shield_activated");
        case ECosmoGameEvent::FireActivated: return TEXT("fire_activated");
        case ECosmoGameEvent::WaveActivated: return TEXT("wave_activated");
        case ECosmoGameEvent::LaserActivated: return TEXT("laser_activated");
        case ECosmoGameEvent::EnemyScanned: return TEXT("enemy_scanned");
        case ECosmoGameEvent::BossEncountered: return TEXT("boss_encountered");
        case ECosmoGameEvent::TutorialCompleted: return TEXT("tutorial_completed");
        case ECosmoGameEvent::PlayerDamaged: return TEXT("player_damaged");
        case ECosmoGameEvent::PlayerInDanger: return TEXT("player_in_danger");
        case ECosmoGameEvent::DangerCleared: return TEXT("danger_cleared");
        default: return FString();
    }
}

bool UCosmoClientComponent::CheckHealth()
{
    return StartRequest(TEXT("GET"), TEXT("/health"));
}

bool UCosmoClientComponent::SendGameEvent(ECosmoGameEvent Event)
{
    return SendGameEventByName(GameEventToString(Event));
}

bool UCosmoClientComponent::SendGameEventByName(const FString& EventName)
{
    const FString Clean = EventName.TrimStartAndEnd().ToLower();
    if (!ValidEvents().Contains(Clean))
    {
        LastError = FString::Printf(TEXT("Unsupported COSMO game event: %s"), *Clean);
        return false;
    }
    const FString Body = FString::Printf(TEXT("{\"event\":\"%s\"}"), *Clean);
    return StartRequest(TEXT("POST"), TEXT("/events"), Body);
}

bool UCosmoClientComponent::SendPlayerUtterance(const FString& Text)
{
    const FString Clean = Text.TrimStartAndEnd();
    if (Clean.IsEmpty())
    {
        LastError = TEXT("Player utterance cannot be empty.");
        return false;
    }
    TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("text"), Clean);
    FString Body;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Object, Writer);
    return StartRequest(TEXT("POST"), TEXT("/player/utterance"), Body);
}

bool UCosmoClientComponent::GetTutorialStatus() { return StartRequest(TEXT("GET"), TEXT("/tutorial/status")); }
bool UCosmoClientComponent::GetCurrentTutorialStep() { return StartRequest(TEXT("GET"), TEXT("/tutorial/step")); }
bool UCosmoClientComponent::GetNextHint() { return StartRequest(TEXT("GET"), TEXT("/tutorial/hint")); }
bool UCosmoClientComponent::GetSnapshot() { return StartRequest(TEXT("GET"), TEXT("/snapshot")); }
bool UCosmoClientComponent::SendGameState(const FString& JsonState)
{
    TSharedPtr<FJsonObject> Parsed;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonState), Parsed) || !Parsed.IsValid())
    { LastError = TEXT("Game state must be a JSON object."); return false; }
    return StartRequest(TEXT("POST"), TEXT("/game/state"), JsonState);
}
bool UCosmoClientComponent::ResetSession() { return StartRequest(TEXT("POST"), TEXT("/reset"), TEXT("{}")); }

bool UCosmoClientComponent::ReportConfirmedAbility(FName AbilityAction)
{
    const FString Action = AbilityAction.ToString().TrimStartAndEnd().ToLower();
    if (Action == TEXT("shield")) return SendGameEvent(ECosmoGameEvent::ShieldActivated);
    if (Action == TEXT("fire")) return SendGameEvent(ECosmoGameEvent::FireActivated);
    if (Action == TEXT("wave")) return SendGameEvent(ECosmoGameEvent::WaveActivated);
    if (Action == TEXT("laser")) return SendGameEvent(ECosmoGameEvent::LaserActivated);
    LastError = FString::Printf(TEXT("Unsupported ability action: %s"), *AbilityAction.ToString());
    return false;
}

bool UCosmoClientComponent::StartRequest(const FString& Verb, const FString& Endpoint, const FString& JsonBody)
{
    if (EndingPlay) return false;
    FString Root = BaseUrl.TrimStartAndEnd();
    while (Root.EndsWith(TEXT("/")))
    {
        Root.LeftChopInline(1);
    }
    if (!Root.StartsWith(TEXT("http://")) && !Root.StartsWith(TEXT("https://")))
    {
        LastError = TEXT("COSMO BaseUrl must start with http:// or https://.");
        return false;
    }

    FHttpRequestPtr Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Root + Endpoint);
    Request->SetVerb(Verb);
    Request->SetTimeout(RequestTimeoutSeconds);
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    if (!JsonBody.IsEmpty())
    {
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
        Request->SetContentAsString(JsonBody);
    }
    Request->OnProcessRequestComplete().BindUObject(this, &UCosmoClientComponent::HandleRequestComplete, Endpoint);
    // One in-flight request preserves tutorial order and prevents stale status overwrites.
    if (QueuedRequests.Num() >= 128) { LastError = TEXT("COSMO request queue is full."); return false; }
    QueuedRequests.Add(Request);
    StartNextRequest();
    return true;
}

void UCosmoClientComponent::StartNextRequest()
{
    if (WaitingForService || EndingPlay || HandlingResponse || !ActiveRequests.IsEmpty() || QueuedRequests.IsEmpty()) return;
    const FHttpRequestPtr Request = QueuedRequests[0];
    QueuedRequests.RemoveAt(0);
    ActiveRequests.Add(Request);
    if (!Request->ProcessRequest())
    {
        // Dispatch the same failure path as asynchronous transport errors.
        Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
    }
}

void UCosmoClientComponent::HandleRequestComplete(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool WasSuccessful,
    FString Endpoint)
{
    if (EndingPlay) return;
    HandlingResponse = true;
    ActiveRequests.Remove(Request);

    FCosmoResponse Result;
    Result.Endpoint = Endpoint;
    Result.HttpStatus = Response.IsValid() ? Response->GetResponseCode() : 0;
    Result.RawJson = Response.IsValid() ? Response->GetContentAsString() : FString();
    Result.Success = WasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Result.HttpStatus);

    TSharedPtr<FJsonObject> Root;
    if (!Result.RawJson.IsEmpty())
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.RawJson);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            Result.Error = TEXT("COSMO returned invalid JSON.");
            Result.Success = false;
        }
    }

    if (Root.IsValid())
    {
        OptionalString(Root, TEXT("spoken_line"), Result.SpokenLine);
        OptionalString(Root, TEXT("provider"), Result.Provider);
        OptionalString(Root, TEXT("source"), Result.Source);
        OptionalString(Root, TEXT("provider_error"), Result.ProviderError);
        if (Result.SpokenLine.IsEmpty()) OptionalString(Root, TEXT("line"), Result.SpokenLine);
        Root->TryGetBoolField(TEXT("advanced"), Result.Advanced);
        const auto Tutorial = ObjectField(Root, TEXT("tutorial"));
        if (Tutorial.IsValid())
        {
            Tutorial->TryGetBoolField(TEXT("advanced"), Result.Advanced);
            Tutorial->TryGetBoolField(TEXT("accepted"), Result.Accepted);
        }
        ReadStatus(Root, Result.TutorialStatus);
        Result.HasTutorialStatus = !Result.TutorialStatus.TutorialId.IsEmpty();
        // /tutorial/step and /tutorial/hint use instruction/hint, rather than spoken_line.
        if (Endpoint == TEXT("/tutorial/step")) OptionalString(Root, TEXT("instruction"), Result.SpokenLine);
        if (Endpoint == TEXT("/tutorial/hint")) OptionalString(Root, TEXT("hint"), Result.SpokenLine);
        ReadAnimation(Root, Result.AnimationEvent);
        if (!Result.Success)
        {
            OptionalString(Root, TEXT("error"), Result.Error);
        }
    }

    if (!Result.Success && Result.Error.IsEmpty())
    {
        Result.Error = Response.IsValid()
            ? FString::Printf(TEXT("COSMO HTTP request failed with status %d."), Result.HttpStatus)
            : TEXT("Cannot reach COSMO. Start the local service and check BaseUrl.");
    }

    LastResponse = Result;
    LastError = Result.Error;
    SetConnected(Result.Success);
    OnResponse.Broadcast(LastResponse);
    if (Result.Success && !Result.SpokenLine.IsEmpty())
    {
        OnSpokenLine.Broadcast(Result.SpokenLine);
    }
    if (Result.Success && !Result.AnimationEvent.State.IsEmpty())
    {
        OnAnimation.Broadcast(Result.AnimationEvent);
    }
    HandlingResponse = false;
    StartNextRequest();
}

void UCosmoClientComponent::SetConnected(bool NewConnected)
{
    if (Connected != NewConnected)
    {
        Connected = NewConnected;
        OnConnectionChanged.Broadcast(Connected);
    }
}
