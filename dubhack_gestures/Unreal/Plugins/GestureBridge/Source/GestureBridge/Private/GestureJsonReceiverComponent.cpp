#include "GestureJsonReceiverComponent.h"
#include "GestureCore.h"
#include "Common/UdpSocketBuilder.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Containers/StringConv.h"

namespace {
std::string Narrow(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
FName NameOf(const std::string& S) { return FName(UTF8_TO_TCHAR(S.c_str())); }
template<typename T>
bool Integer(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, T& Out, int64 Min, int64 Max)
{
    double N = 0;
    if(!O.IsValid() || !O->TryGetNumberField(Key,N) || !FMath::IsFinite(N)
        || N < static_cast<double>(Min) || N > static_cast<double>(Max) || std::floor(N)!=N) return false;
    Out=static_cast<T>(N); return true;
}
bool ReadObject(const FString& Text, TSharedPtr<FJsonObject>& Out)
{
    auto Reader=TJsonReaderFactory<>::Create(Text);
    return FJsonSerializer::Deserialize(Reader,Out) && Out.IsValid();
}
}

UGestureJsonReceiverComponent::UGestureJsonReceiverComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
    State=new GestureBridgeCore::State();
}
UGestureJsonReceiverComponent::~UGestureJsonReceiverComponent()
{
    CloseSocket(); delete State; State=nullptr;
}
void UGestureJsonReceiverComponent::BeginPlay()
{
    Super::BeginPlay();
    if(AutoStart) StartListening(); else ReloadBindings();
}
void UGestureJsonReceiverComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopListening(); Super::EndPlay(Reason);
}
FSocket* UGestureJsonReceiverComponent::OpenSocket(int32 Port)
{
    return FUdpSocketBuilder(TEXT("GestureJsonBlueprint"))
        .AsNonBlocking().BoundToEndpoint(FIPv4Endpoint(FIPv4Address(127,0,0,1),static_cast<uint16>(Port)))
        .WithReceiveBufferSize(2*1024*1024);
}
void UGestureJsonReceiverComponent::CloseSocket()
{
    if(Socket) {
        Socket->Close();
        if(auto* System=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) System->DestroySocket(Socket);
        Socket=nullptr;
    }
    Listening=false;
}
bool UGestureJsonReceiverComponent::ReloadBindings()
{
    FString Path=ConfigFile;
    if(Path.IsEmpty()) {
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("GestureBridge"));
        if(!Plugin.IsValid()) {LastError=TEXT("GestureBridge plugin directory was not found."); return false;}
        Path=FPaths::Combine(Plugin->GetBaseDir(),TEXT("Resources/gesture_bindings.json"));
    } else if(FPaths::IsRelative(Path)) Path=FPaths::Combine(FPaths::ProjectDir(),Path);
    FString Text; TSharedPtr<FJsonObject> O;
    if(!FFileHelper::LoadFileToString(Text,*Path) || !ReadObject(Text,O)) {
        LastError=TEXT("Cannot load JSON config: ")+Path; return false;
    }
    int64 Version=0, Port=0, Timeout=0, MaxAge=0; FString Target;
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!Integer(O,TEXT("version"),Version,3,3)
        || !O->TryGetStringField(TEXT("target"),Target) || Target!=TEXT("unreal_blueprint")
        || !Integer(O,TEXT("udp_port"),Port,1,65535)
        || !Integer(O,TEXT("disconnect_timeout_ms"),Timeout,50,5000)
        || !Integer(O,TEXT("event_max_age_ms"),MaxAge,1,5000)
        || !O->TryGetArrayField(TEXT("bindings"),Rows) || Rows->Num()==0 || Rows->Num()>12) {
        LastError=TEXT("Invalid config version, target, port, timeout or bindings."); return false;
    }
    std::vector<GestureBridgeCore::Binding> Bindings;
    TSet<FName> SeenActions;
    for(const auto& Row:*Rows) {
        const TSharedPtr<FJsonObject>* B=nullptr;
        FString Action,Key,Mode; int64 Cooldown=0; bool Enabled=false;
        if(!Row.IsValid() || !Row->TryGetObject(B) || !B || !B->IsValid()
            || !(*B)->TryGetStringField(TEXT("action"),Action)
            || !(*B)->TryGetStringField(TEXT("key"),Key)
            || !(*B)->TryGetStringField(TEXT("mode"),Mode)
            || !(*B)->TryGetBoolField(TEXT("enabled"),Enabled)
            || !Integer(*B,TEXT("cooldown_ms"),Cooldown,0,600000)
            || !GestureBridgeCore::State::Known(Narrow(Action))
            || Action.IsEmpty() || Action.Len()>80 || FName(*Action).IsNone()
            || Mode!=(GestureBridgeCore::State::IsPulse(Narrow(Action)) ? TEXT("press") : TEXT("hold"))
            || !Key.IsEmpty()
            || SeenActions.Contains(FName(*Action))) {
            LastError=TEXT("Invalid/duplicate gesture action; key must be empty for gesture-only controls."); return false;
        }
        SeenActions.Add(FName(*Action));
        Bindings.push_back({Narrow(Action),Narrow(Key),Narrow(Mode),static_cast<int>(Cooldown),Enabled});
    }
    if(Socket && Port!=UdpPort) {
        FSocket* Replacement=OpenSocket(static_cast<int32>(Port));
        if(!Replacement) {LastError=TEXT("New UDP port is unavailable; old config retained."); return false;}
        CloseSocket(); Socket=Replacement; Listening=true;
    }
    ++Generation;
    State->Configure(Bindings,static_cast<int>(Timeout),static_cast<int>(MaxAge));
    UdpPort=static_cast<int32>(Port); Configured=true; LastError.Empty();
    return true;
}
bool UGestureJsonReceiverComponent::StartListening()
{
    if(Socket) return true;
    if(!ReloadBindings()) {UE_LOG(LogTemp,Error,TEXT("GestureBridge: %s"),*LastError); return false;}
    Socket=OpenSocket(UdpPort);
    Listening=Socket!=nullptr;
    if(!Listening) {LastError=TEXT("Cannot bind UDP port; close other receivers using this port."); UE_LOG(LogTemp,Error,TEXT("%s"),*LastError);}
    ++Generation; return Listening;
}
void UGestureJsonReceiverComponent::StopListening()
{
    ++Generation; CloseSocket();
    State->Disconnect(); LatestFrame=FGestureJsonFrame(); LatestJson.Empty();
    Dispatch(FPlatformTime::Seconds());
}
bool UGestureJsonReceiverComponent::SubmitJson(const FString& Json)
{
    if(!Configured || Json.Len()>32768) return false;
    TSharedPtr<FJsonObject> O;
    if(!ReadObject(Json,O)) return false;
    GestureBridgeCore::Packet P; FString Session; int64 Version=0;
    constexpr int64 MaxExact=9007199254740991LL;
    const TArray<TSharedPtr<FJsonValue>>* Held=nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Events=nullptr;
    if(!Integer(O,TEXT("version"),Version,3,3)
        || !Integer(O,TEXT("frame_id"),P.frame,0,MaxExact)
        || !Integer(O,TEXT("timestamp_ms"),P.timestamp,0,MaxExact)
        || !O->TryGetStringField(TEXT("session_id"),Session)
        || !O->TryGetNumberField(TEXT("move_axis"),P.move)
        || !O->TryGetNumberField(TEXT("turn_axis"),P.turn)
        || !O->TryGetArrayField(TEXT("held_actions"),Held) || Held->Num()>6
        || !O->TryGetArrayField(TEXT("events"),Events) || Events->Num()>16) return false;
    for(const auto& V:*Held) {FString A;if(!V->TryGetString(A))return false;P.held.push_back(Narrow(A));}
    for(const auto& V:*Events) {
        const TSharedPtr<FJsonObject>* E=nullptr;GestureBridgeCore::Pulse Pulse;FString A;
        if(!V->TryGetObject(E) || !E || !E->IsValid()
            || !Integer(*E,TEXT("id"),Pulse.id,1,MaxExact)
            || !Integer(*E,TEXT("timestamp_ms"),Pulse.timestamp,0,MaxExact)
            || !(*E)->TryGetStringField(TEXT("action"),A))return false;
        Pulse.action=Narrow(A);P.events.push_back(Pulse);
    }
    P.version=static_cast<int>(Version);P.session=Narrow(Session);
    if(!State->Accept(P,FPlatformTime::Seconds())) return false;
    LatestFrame.SessionId=Session;LatestFrame.FrameId=P.frame;LatestFrame.TimestampMs=P.timestamp;
    LatestFrame.HeldActions.Reset();
    for(const auto& A:P.held)LatestFrame.HeldActions.Add(NameOf(A));
    LatestFrame.MoveAxis=P.move;LatestFrame.TurnAxis=P.turn;LatestFrame.HasPointer=false;
    LatestFrame.Pointer=FVector2D::ZeroVector;
    const TSharedPtr<FJsonObject>* Calibration=nullptr;
    const TSharedPtr<FJsonObject>* Tracking=nullptr;
    LatestFrame.LeftCalibrated=LatestFrame.HeadCalibrated=false;
    LatestFrame.LeftTracked=LatestFrame.RightTracked=LatestFrame.FaceTracked=false;
    if(O->TryGetObjectField(TEXT("calibrated"),Calibration)) {
        (*Calibration)->TryGetBoolField(TEXT("left_hand"),LatestFrame.LeftCalibrated);
        (*Calibration)->TryGetBoolField(TEXT("head"),LatestFrame.HeadCalibrated);
    }
    if(O->TryGetObjectField(TEXT("tracking"),Tracking)) {
        (*Tracking)->TryGetBoolField(TEXT("left_hand"),LatestFrame.LeftTracked);
        (*Tracking)->TryGetBoolField(TEXT("right_hand"),LatestFrame.RightTracked);
        (*Tracking)->TryGetBoolField(TEXT("face"),LatestFrame.FaceTracked);
    }
    const TArray<TSharedPtr<FJsonValue>>* Pointer=nullptr;
    double X=0,Y=0;
    if(O->TryGetArrayField(TEXT("pointer"),Pointer) && Pointer->Num()==2
        && (*Pointer)[0]->TryGetNumber(X) && (*Pointer)[1]->TryGetNumber(Y)
        && FMath::IsFinite(X) && FMath::IsFinite(Y)) {
        LatestFrame.HasPointer=true;
        LatestFrame.Pointer=FVector2D(FMath::Clamp(X,0.0,1.0),FMath::Clamp(Y,0.0,1.0));
    }
    LatestJson=Json;
    const FString Copy=Json; OnJsonReceived.Broadcast(Copy);
    return true;
}
void UGestureJsonReceiverComponent::Dispatch(double Now)
{
    const auto Events=State->Tick(Now);
    MoveAxis=State->Move();TurnAxis=State->Turn();
    const uint64 Snapshot=Generation;
    const bool NewConnected=State->IsConnected();
    const bool ConnectionChanged=Connected!=NewConnected;
    if(ConnectionChanged) {
        Connected=NewConnected;
        if(!Connected) {LatestFrame.HeldActions.Reset(); LatestFrame.HasPointer=false; LatestFrame.MoveAxis=0; LatestFrame.TurnAxis=0;}
    }
    for(const auto& E:Events) {
        const auto& B=E.action.binding;
        const FName Action=NameOf(B.action),Key=NameOf(B.key);
        const int64 Id=E.action.eventId;
        switch(E.phase) {
            case GestureBridgeCore::Phase::Pressed: OnActionPressed.Broadcast(Action,Key,Id); break;
            case GestureBridgeCore::Phase::Released: OnActionReleased.Broadcast(Action,Key,Id); break;
            case GestureBridgeCore::Phase::Held: OnActionHeld.Broadcast(Action,Key,Id); break;
        }
        if(Snapshot!=Generation) return;
    }
    OnAxes.Broadcast(MoveAxis,TurnAxis);
    if(Snapshot!=Generation) return;
    if(ConnectionChanged) OnConnectionChanged.Broadcast(Connected);
}
void UGestureJsonReceiverComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    uint32 Pending=0; int32 Count=0;
    while(Socket && Count++<64 && Socket->HasPendingData(Pending)) {
        // Drain even oversized datagrams without parsing them. Never block the game thread.
        TArray<uint8> Data; Data.SetNumUninitialized(65508);
        const auto Sender=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        int32 Read=0;
        if(!Socket->RecvFrom(Data.GetData(),65507,Read,*Sender) || Read<=0) break;
        if(Read>32768 || Sender->ToString(false)!=TEXT("127.0.0.1")) continue;
        Data[Read]=0;
        FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Data.GetData()),Read);
        SubmitJson(FString(Text.Length(),Text.Get()));
    }
    Dispatch(FPlatformTime::Seconds());
}
bool UGestureJsonReceiverComponent::IsActionHeld(FName Action) const
{
    // The core also folds case to match Blueprint FName comparisons.
    if(!State || !Connected) return false;
    return State->IsActionHeld(Narrow(Action.ToString()));
}
