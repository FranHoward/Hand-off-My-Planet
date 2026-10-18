#include "CosmoRealtimeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "AudioCaptureCore.h"
#include "AudioCaptureDeviceInterface.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "HttpModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/ScopeLock.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWaveProcedural.h"
#include "WebSocketsModule.h"

namespace
{
constexpr int32 VoiceRate = 24000;
FString StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{ FString Value; if (Object) Object->TryGetStringField(Name, Value); return Value; }
bool LocalUrl(const FString& Url, bool WebSocket)
{
    const FString Prefix = WebSocket ? TEXT("ws://127.0.0.1:") : TEXT("http://127.0.0.1:");
    return Url.StartsWith(Prefix);
}
}

void FCosmoPcmCaptureBuffer::SetActive(bool Active)
{
    FScopeLock Lock(&Mutex);
    if (Enabled != Active) { Mono.Reset(); Pcm.Reset(); Position = 0; }
    Enabled = Active;
}
void FCosmoPcmCaptureBuffer::Push(const float* Samples, int32 Frames, int32 Channels, int32 Rate)
{
    FScopeLock Lock(&Mutex);
    if (!Enabled || !Samples || Frames <= 0 || Channels < 1 || Channels > 32 || Rate < 8000 || Rate > 192000) return;
    if (InputRate != Rate) { Mono.Reset(); Position = 0; InputRate = Rate; }
    for (int32 Frame=0; Frame<Frames; ++Frame)
    {
        float Value=0;
        for (int32 Channel=0; Channel<Channels; ++Channel) Value += Samples[Frame*Channels+Channel];
        Mono.Add(FMath::IsFinite(Value) ? FMath::Clamp(Value/Channels,-1.f,1.f) : 0.f);
    }
    const double Step = double(Rate)/VoiceRate;
    while (Position+1 < Mono.Num())
    {
        const int32 Index = int32(Position);
        const float Value = FMath::Lerp(Mono[Index],Mono[Index+1],float(Position-Index));
        const int16 Sample = int16(FMath::RoundToInt(Value * (Value < 0 ? 32768.f : 32767.f)));
        Pcm.Add(uint8(Sample & 255)); Pcm.Add(uint8((uint16(Sample)>>8)&255));
        Position += Step;
    }
    const int32 Consumed = FMath::Min(int32(Position), Mono.Num()-1);
    if (Consumed > 0) { Mono.RemoveAt(0,Consumed,EAllowShrinking::No); Position -= Consumed; }
    if (Pcm.Num() > VoiceRate*4) Pcm.Reset(); // Drop stale capture rather than grow a queue.
}
TArray<uint8> FCosmoPcmCaptureBuffer::Drain()
{ FScopeLock Lock(&Mutex); TArray<uint8> Result = MoveTemp(Pcm); Pcm.Reset(); return Result; }

UCosmoRealtimeComponent::UCosmoRealtimeComponent()
{ PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickInterval=.02f; }
UCosmoRealtimeComponent::~UCosmoRealtimeComponent() = default;
void UCosmoRealtimeComponent::Start()
{
    Integration=GetOwner() ? GetOwner()->FindComponentByClass<UFirstPersonAIIntegrationComponent>() : nullptr;
    const bool Verification=FParse::Param(FCommandLine::Get(),TEXT("RealtimeAVTest")) || FParse::Param(FCommandLine::Get(),TEXT("RealtimeAVPreview")) || FParse::Param(FCommandLine::Get(),TEXT("UIFlowTest"));
    if (Running || (!Verification && (IsRunningCommandlet() || FApp::IsUnattended()))) return;
    ConnectionError.Reset();
    Running=true; NextConnect=0; NextCamera=0; NextProactiveAt=FPlatformTime::Seconds()+FMath::FRandRange(75.f,150.f);
    if (EnableVoice) ConnectVoice();
}
void UCosmoRealtimeComponent::ConnectVoice()
{
    if (!Running || !EnableVoice || VoiceReady) return;
    DisconnectVoice();
    LastVoiceError.Reset();
    if (!LocalUrl(VoiceUrl,true)) { VoiceStatus=TEXT("Voice URL must use the local Python relay"); OnUpdated.Broadcast(); return; }
    VoiceStatus=TEXT("Connecting to Grok Voice");
    Socket=FWebSocketsModule::Get().CreateWebSocket(VoiceUrl);
    Socket->OnMessage().AddWeakLambda(this,[this](const FString& Json) { HandleVoiceEvent(Json); });
    Socket->OnConnectionError().AddWeakLambda(this,[this](const FString&)
    { VoiceReady=false; StopMicrophone(); StopPlayback(); NextConnect=FPlatformTime::Seconds()+RetryDelay; RetryDelay=FMath::Min(RetryDelay*2,15.0); VoiceStatus=TEXT("Could not connect to Grok Voice."); ConnectionError=VoiceStatus; OnUpdated.Broadcast(); });
    Socket->OnClosed().AddWeakLambda(this,[this](int32,const FString&,bool)
    { VoiceReady=false; StopMicrophone(); StopPlayback(); NextConnect=FPlatformTime::Seconds()+RetryDelay; RetryDelay=FMath::Min(RetryDelay*2,15.0); VoiceStatus=LastVoiceError.IsEmpty() ? TEXT("Voice disconnected - reconnecting") : LastVoiceError; if(TutorialActive && !TutorialLocalSpeech) StartTutorialFallback(); OnUpdated.Broadcast(); });
    Socket->Connect();
    NextConnect=FPlatformTime::Seconds()+20;
    OnUpdated.Broadcast();
}
void UCosmoRealtimeComponent::DisconnectVoice()
{
    VoiceReady=false;
    StopMicrophone(); StopPlayback();
    if (Socket)
    {
        Socket->OnMessage().Clear(); Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear();
        Socket->Close(); Socket.Reset();
    }
}
void UCosmoRealtimeComponent::StartMicrophone()
{
    if (Capture || MicrophoneMuted || !VoiceReady || IsRunningCommandlet() || FApp::IsUnattended()) return;
    CaptureBuffer=MakeShared<FCosmoPcmCaptureBuffer,ESPMode::ThreadSafe>();
    Capture=MakeShared<Audio::FAudioCapture>();
    NextMicrophoneAttempt=FPlatformTime::Seconds()+3;
    Audio::FAudioCaptureDeviceParams Params; Params.bUseHardwareAEC=true;
    auto Buffer=CaptureBuffer;
    if (!Capture->OpenAudioCaptureStream(Params,[Buffer](const void* Audio,int32 Frames,int32 Channels,int32 Rate,double,bool)
        { Buffer->Push(static_cast<const float*>(Audio),Frames,Channels,Rate); },1024) || !Capture->StartStream())
    {
        if (Capture->IsStreamOpen()) Capture->AbortStream();
        Capture.Reset(); CaptureBuffer.Reset(); MicrophoneActive=false;
        VoiceStatus=TEXT("Microphone unavailable - check Windows microphone permission");
    }
    else { MicrophoneActive=true; CaptureBuffer->SetActive(!TutorialActive); VoiceStatus=TutorialActive ? TEXT("Tutorial speaking - conversation paused") : TEXT("Listening - speak to COSMO"); }
    OnUpdated.Broadcast();
}
void UCosmoRealtimeComponent::StopMicrophone()
{
    if (CaptureBuffer) CaptureBuffer->SetActive(false);
    if (Capture) { if (Capture->IsStreamOpen()) Capture->AbortStream(); Capture.Reset(); }
    CaptureBuffer.Reset(); MicrophoneActive=false;
}
void UCosmoRealtimeComponent::SetMicrophoneMuted(bool Muted)
{
    MicrophoneMuted=Muted;
    if (Muted) { StopMicrophone(); SendEvent(TEXT("input_audio_buffer.clear")); VoiceStatus=TEXT("Microphone muted"); }
    else StartMicrophone();
    OnUpdated.Broadcast();
}
bool UCosmoRealtimeComponent::SendEvent(const FString& Type,const FString& Text)
{
    if (!VoiceReady || !Socket || !Socket->IsConnected()) return false;
    auto Object=MakeShared<FJsonObject>(); Object->SetStringField(TEXT("type"),Type);
    if (!Text.IsEmpty()) Object->SetStringField(TEXT("text"),Text);
    FString Json; FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Json)); Socket->Send(Json); return true;
}
bool UCosmoRealtimeComponent::AskText(const FString& Text)
{ return !TutorialActive && !Text.TrimStartAndEnd().IsEmpty() && SendEvent(TEXT("cosmo.text"),Text.Left(2000)); }
bool UCosmoRealtimeComponent::SayLine(const FString& Text)
{
    const FString Line=Text.TrimStartAndEnd().Left(3000);
    if(!Running || Line.IsEmpty() || TutorialQueue.Num()>=64) return false;
    if(Line==TutorialLine && TutorialActive) return true;
    if(TutorialQueue.IsEmpty() || TutorialQueue.Last()!=Line) TutorialQueue.Add(Line);
    if(!TutorialActive) StartNextTutorial();
    return true;
}
void UCosmoRealtimeComponent::StartNextTutorial()
{
    if(TutorialQueue.IsEmpty()) return;
    TutorialActive=true; TutorialLocalSpeech=false;
    LastTutorialAudioBytes=0; LastTutorialNativeComplete=false; TutorialAudioReady=false;
    TutorialLine=TutorialQueue[0]; TutorialQueue.RemoveAt(0);
    TutorialId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if(!ResponseId.IsEmpty()) InterruptedResponses.Add(ResponseId);
    StopPlayback(); AssistantTranscript.Reset();
    if(CaptureBuffer) CaptureBuffer->SetActive(false);
    SendEvent(TEXT("response.cancel")); SendEvent(TEXT("input_audio_buffer.clear"));
    TutorialStartedAt=FPlatformTime::Seconds(); TutorialDeadline=TutorialStartedAt+25;
    VoiceStatus=TEXT("Tutorial speaking - conversation paused");
    if(Integration) Integration->ShowTutorialLine(TutorialLine);
    if(VoiceReady && SupportsTutorialPriority && Socket && Socket->IsConnected())
    {
        auto Event=MakeShared<FJsonObject>(); Event->SetStringField(TEXT("type"),TEXT("cosmo.say"));
        Event->SetStringField(TEXT("text"),TutorialLine); Event->SetStringField(TEXT("utterance_id"),TutorialId);
        FString Json; FJsonSerializer::Serialize(Event,TJsonWriterFactory<>::Create(&Json)); Socket->Send(Json);
    }
    else StartTutorialFallback();
    OnUpdated.Broadcast();
}
void UCosmoRealtimeComponent::StartTutorialFallback()
{
    if(RequireGrokTutorial)
    {
        const FString FailedLine=TutorialLine;
        // Release the relay's occupied utterance slot before retrying. This is a
        // failed-playback acknowledgement, never a successful page completion.
        if(VoiceReady && Socket && Socket->IsConnected() && !TutorialId.IsEmpty())
        {
            auto Ack=MakeShared<FJsonObject>(); Ack->SetStringField(TEXT("type"),TEXT("cosmo.tutorial_played"));
            Ack->SetStringField(TEXT("utterance_id"),TutorialId); Ack->SetBoolField(TEXT("playback_failed"),true);
            Ack->SetBoolField(TEXT("keep_priority"),true);
            FString Json; FJsonSerializer::Serialize(Ack,TJsonWriterFactory<>::Create(&Json)); Socket->Send(Json);
        }
        StopPlayback(); TutorialQueue.Reset(); TutorialActive=false; TutorialLine.Reset(); TutorialId.Reset();
        VoiceStatus=TEXT("Grok tutorial unavailable - retry to continue");
        OnTutorialFinished.Broadcast(FailedLine,false); OnUpdated.Broadcast(); return;
    }
    StopPlayback(); TutorialLocalSpeech=true; TutorialStartedAt=FPlatformTime::Seconds();
    TutorialDeadline=TutorialStartedAt+FMath::Clamp(TutorialLine.Len()/12.,2.,25.);
    VoiceStatus=TEXT("Tutorial speaking - local voice");
    if(Integration) Integration->BeginLocalTutorialSpeech(TutorialLine);
    OnUpdated.Broadcast();
}
void UCosmoRealtimeComponent::FinishTutorial()
{
    const FString FinishedLine=TutorialLine;
    LastTutorialNativeComplete=!TutorialLocalSpeech && OutputComplete && LastTutorialAudioBytes>0;
    const bool More=!TutorialQueue.IsEmpty();
    if(VoiceReady && SupportsTutorialPriority && Socket && Socket->IsConnected())
    {
        auto Event=MakeShared<FJsonObject>(); Event->SetStringField(TEXT("type"),TEXT("cosmo.tutorial_played"));
        Event->SetStringField(TEXT("utterance_id"),TutorialId); Event->SetBoolField(TEXT("keep_priority"),More);
        FString Json; FJsonSerializer::Serialize(Event,TJsonWriterFactory<>::Create(&Json)); Socket->Send(Json);
    }
    if(TutorialLocalSpeech && Integration) Integration->StopLocalTutorialSpeech();
    StopPlayback(); TutorialActive=false; TutorialLocalSpeech=false; TutorialLine.Reset(); TutorialId.Reset();
    OnTutorialFinished.Broadcast(FinishedLine,LastTutorialNativeComplete);
    if(More) StartNextTutorial();
    else { VoiceStatus=MicrophoneMuted ? TEXT("Microphone muted") : TEXT("Listening - speak to COSMO"); OnUpdated.Broadcast(); }
}
void UCosmoRealtimeComponent::StopPlayback()
{
    if (Playback) Playback->Stop();
    if (SpeechWave) SpeechWave->ResetAudio();
    SpeechWave=nullptr; OutputComplete=false;
}
int32 UCosmoRealtimeComponent::QueuedVoiceBytes() const
{ return SpeechWave ? SpeechWave->GetAvailableAudioByteCount() : 0; }
void UCosmoRealtimeComponent::QueueVoiceAudio(const TArray<uint8>& Pcm)
{
    if (Pcm.IsEmpty() || Pcm.Num()%2 || Pcm.Num()>VoiceRate*4) return;
    if(TutorialActive && !TutorialLocalSpeech) LastTutorialAudioBytes+=Pcm.Num();
    if (!SpeechWave)
    {
        SpeechWave=NewObject<USoundWaveProcedural>(this);
        SpeechWave->SetSampleRate(VoiceRate); SpeechWave->NumChannels=1;
        SpeechWave->Duration=INDEFINITELY_LOOPING_DURATION; SpeechWave->SoundGroup=SOUNDGROUP_Voice;
    }
    if (SpeechWave->GetAvailableAudioByteCount()>VoiceRate*(TutorialActive ? 120 : 20)) { StopPlayback(); return; }
    SpeechWave->QueueAudio(Pcm.GetData(),Pcm.Num()); LastAudioPacket=FPlatformTime::Seconds();
    if (!Playback && GetOwner())
    {
        Playback=NewObject<UAudioComponent>(GetOwner()); Playback->bAutoActivate=false; Playback->bIsUISound=true;
        Playback->bAllowSpatialization=false; Playback->RegisterComponent();
    }
    if(!DeferTutorialPlayback) PlayQueuedAudio();
    if(TutorialActive && LastTutorialAudioBytes>=VoiceRate*2/5) MarkTutorialAudioReady();
    VoiceStatus=TutorialActive ? TEXT("Tutorial speaking - conversation paused") : TEXT("COSMO speaking");
}
void UCosmoRealtimeComponent::PlayQueuedAudio()
{
    if (Playback && GEngine && GEngine->UseSound())
    {
        Playback->SetVolumeMultiplier(FMath::Clamp(VoiceVolume,0.f,1.f));
        Playback->SetPitchMultiplier(FMath::Clamp(VoicePitch,.5f,2.f));
        if (Playback->Sound != SpeechWave) Playback->SetSound(SpeechWave);
        if (!Playback->IsPlaying()) Playback->Play();
    }
}
void UCosmoRealtimeComponent::ReleaseTutorialAudio()
{ DeferTutorialPlayback=false; PlayQueuedAudio(); }
void UCosmoRealtimeComponent::MarkTutorialAudioReady()
{
    if(TutorialAudioReady || !TutorialActive || LastTutorialAudioBytes<=0) return;
    TutorialAudioReady=true; OnTutorialAudioReady.Broadcast();
}
void UCosmoRealtimeComponent::HandleVoiceEvent(const FString& Json)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root) || !Root) return;
    const FString Type=StringField(Root,TEXT("type"));
    if(Type==TEXT("input_audio_buffer.speech_started") || Type==TEXT("input_audio_buffer.speech_stopped") || Type==TEXT("response.created") || Type==TEXT("response.done") || Type.Contains(TEXT("audio.delta")))
        NextProactiveAt=FPlatformTime::Seconds()+FMath::FRandRange(75.f,150.f);
    const bool TutorialPacket=StringField(Root,TEXT("cosmo_channel"))==TEXT("tutorial");
    if(Type==TEXT("cosmo.tutorial.error"))
    { if(TutorialActive && StringField(Root,TEXT("utterance_id"))==TutorialId && !TutorialLocalSpeech) StartTutorialFallback(); return; }
    if(TutorialPacket && (!TutorialActive || TutorialLocalSpeech || StringField(Root,TEXT("utterance_id"))!=TutorialId)) return;
    if(TutorialActive && !TutorialPacket && Type!=TEXT("cosmo.ready")) return;
    const FString EventResponse=StringField(Root,TEXT("response_id"));
    if (!EventResponse.IsEmpty() && InterruptedResponses.Contains(EventResponse)) return;
    if (Type==TEXT("cosmo.ready"))
    {
        double Rate=0; Root->TryGetNumberField(TEXT("sample_rate"),Rate);
        if (Rate!=VoiceRate || StringField(Root,TEXT("source"))!=TEXT("grok")) return;
        Root->TryGetBoolField(TEXT("tutorial_priority"),SupportsTutorialPriority);
        if(FreeConversation) SendEvent(TEXT("cosmo.free_chat"));
        VoiceReady=true; RetryDelay=1; LastVoiceError.Reset(); VoiceStatus=TutorialActive ? TEXT("Tutorial speaking - conversation paused") : TEXT("Grok Voice connected"); StartMicrophone();
    }
    else if (Type==TEXT("cosmo.error"))
    {
        LastVoiceError=StringField(Root,TEXT("message")).Left(300); ConnectionError=LastVoiceError; VoiceStatus=LastVoiceError;
        if (!VoiceReady) StopMicrophone();
    }
    else if (Type==TEXT("input_audio_buffer.speech_started"))
    {
        if (InterruptedResponses.Num()>32) InterruptedResponses.Reset();
        if (!ResponseId.IsEmpty()) InterruptedResponses.Add(ResponseId);
        StopPlayback(); PlayerTranscript.Reset(); VoiceStatus=TEXT("Listening to you");
        if (Integration) Integration->SetVoiceListening(true);
    }
    else if (Type==TEXT("input_audio_buffer.speech_stopped"))
    { VoiceStatus=TEXT("COSMO thinking"); if (Integration) Integration->SetVoiceListening(false); }
    else if (Type==TEXT("conversation.item.input_audio_transcription.completed") || Type==TEXT("conversation.item.input_audio_transcription.updated"))
        PlayerTranscript=StringField(Root,TEXT("transcript")).Left(2000);
    else if (Type==TEXT("response.created"))
    {
        StopPlayback(); AssistantTranscript.Reset();
        const TSharedPtr<FJsonObject>* Response=nullptr;
        if (Root->TryGetObjectField(TEXT("response"),Response)) ResponseId=StringField(*Response,TEXT("id"));
    }
    else if (Type==TEXT("response.output_audio.delta") || Type==TEXT("response.audio.delta"))
    {
        TArray<uint8> Pcm;
        if (FBase64::Decode(StringField(Root,TEXT("delta")),Pcm)) QueueVoiceAudio(Pcm);
        if(TutorialPacket) TutorialDeadline=FPlatformTime::Seconds()+15;
    }
    else if (Type==TEXT("response.output_audio_transcript.delta") || Type==TEXT("response.audio_transcript.delta") || Type==TEXT("response.text.delta") || Type==TEXT("response.output_text.delta"))
    {
        AssistantTranscript=(AssistantTranscript+StringField(Root,TEXT("delta"))).Left(4000);
        if (Integration && !TutorialPacket) Integration->ShowRealtimeLine(AssistantTranscript);
    }
    else if (Type==TEXT("response.output_audio_transcript.done") || Type==TEXT("response.audio_transcript.done"))
    {
        const FString Line=StringField(Root,TEXT("transcript"));
        if (!Line.IsEmpty()) { AssistantTranscript=Line.Left(4000); if (Integration && !TutorialPacket) Integration->ShowRealtimeLine(AssistantTranscript); }
    }
    else if (Type==TEXT("response.done"))
    {
        const TSharedPtr<FJsonObject>* Response=nullptr;
        const bool Completed=Root->TryGetObjectField(TEXT("response"),Response) && StringField(*Response,TEXT("status"))==TEXT("completed");
        if(TutorialPacket && RequireGrokTutorial && !Completed) { StartTutorialFallback(); return; }
        OutputComplete=true;
        if(Completed) { ReplySource=TEXT("grok"); if(TutorialPacket) MarkTutorialAudioReady(); }
    }
    OnUpdated.Broadcast();
}
bool UCosmoRealtimeComponent::ApplyCameraJpeg(const TArray<uint8>& Jpeg)
{
    if (Jpeg.IsEmpty() || Jpeg.Num()>2*1024*1024) return false;
    auto& Images=FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    auto Wrapper=Images.CreateImageWrapper(EImageFormat::JPEG);
    if (!Wrapper || !Wrapper->SetCompressed(Jpeg.GetData(),Jpeg.Num())) return false;
    const int32 Width=Wrapper->GetWidth(), Height=Wrapper->GetHeight();
    if (Width<16 || Height<16 || Width>1280 || Height>720) return false;
    TArray64<uint8> Raw;
    if (!Wrapper->GetRaw(ERGBFormat::BGRA,8,Raw) || Raw.Num()!=int64(Width)*Height*4) return false;
    if (!CameraTexture || CameraTexture->GetSizeX()!=Width || CameraTexture->GetSizeY()!=Height)
    {
        CameraTexture=UTexture2D::CreateTransient(Width,Height,PF_B8G8R8A8);
        if (!CameraTexture) return false;
        CameraTexture->NeverStream=true; CameraTexture->SRGB=true; CameraTexture->UpdateResource();
    }
    auto* Data=new uint8[Raw.Num()]; FMemory::Memcpy(Data,Raw.GetData(),Raw.Num());
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,Width,Height);
    CameraTexture->UpdateTextureRegions(0,1,Region,Width*4,4,Data,
        [](uint8* Bytes,const FUpdateTextureRegion2D* Regions) { delete[] Bytes; delete Regions; });
    LastCameraFrame=FPlatformTime::Seconds(); CameraStatus=TEXT("LIVE - hands and face"); OnUpdated.Broadcast(); return true;
}
void UCosmoRealtimeComponent::RequestCamera()
{
    if (CameraRequest || !LocalUrl(CameraUrl,false)) return;
    CameraRequest=FHttpModule::Get().CreateRequest(); CameraRequest->SetURL(CameraUrl);
    CameraRequest->SetVerb(TEXT("GET")); CameraRequest->SetTimeout(2.f);
    CameraRequest->OnProcessRequestComplete().BindUObject(this,&UCosmoRealtimeComponent::CameraComplete);
    if (!CameraRequest->ProcessRequest()) CameraRequest.Reset();
}
void UCosmoRealtimeComponent::CameraComplete(FHttpRequestPtr,FHttpResponsePtr Response,bool Success)
{
    CameraRequest.Reset();
    if (!Running) return;
    if (Success && Response && Response->GetResponseCode()==200 && ApplyCameraJpeg(Response->GetContent())) return;
    if (FPlatformTime::Seconds()-LastCameraFrame>2)
    { CameraTexture=nullptr; CameraStatus=TEXT("Camera unavailable - check tracking camera"); OnUpdated.Broadcast(); }
}
void UCosmoRealtimeComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(Delta,Type,Tick);
    if (!Running) return;
    const double Now=FPlatformTime::Seconds();
    if (EnableCameraPreview && Now>=NextCamera) { NextCamera=Now+.1; RequestCamera(); }
    if (EnableVoice && !VoiceReady && Now>=NextConnect) ConnectVoice();
    if (!EnableVoice && (Socket || Capture)) DisconnectVoice();
    if (MicrophoneMuted && Capture) StopMicrophone();
    if (VoiceReady && !MicrophoneMuted && !Capture && Now>=NextMicrophoneAttempt) StartMicrophone();
    if (CaptureBuffer)
    {
        const bool Playing=SpeechWave && (!OutputComplete || QueuedVoiceBytes()>0 || Now-LastAudioPacket<.2);
        CaptureBuffer->SetActive(VoiceReady && !MicrophoneMuted && !TutorialActive && FApp::HasFocus() && (AllowVoiceInterruptions || !Playing));
        if (Now>=NextAudioSend && Socket && Socket->IsConnected())
        {
            NextAudioSend=Now+.04;
            const auto Pcm=CaptureBuffer->Drain();
            if (!Pcm.IsEmpty()) Socket->Send(TEXT("{\"type\":\"input_audio_buffer.append\",\"audio\":\"")+FBase64::Encode(Pcm)+TEXT("\"}"));
        }
    }
    if(FreeConversation && EnableProactiveConversation && Now>=NextProactiveAt)
    {
        const bool Quiet=VoiceReady && !MicrophoneMuted && !TutorialActive && !SpeechWave && FApp::HasFocus()
            && Integration && !Integration->Dead && !Integration->Won && !Integration->ChatOpen
            && !UGameplayStatics::IsGamePaused(this) && Integration->CanStartAmbientConversation();
        if(Quiet) { SendEvent(TEXT("cosmo.proactive")); NextProactiveAt=Now+FMath::FRandRange(75.f,150.f); }
        else NextProactiveAt=Now+15;
    }
    if(TutorialActive)
    {
        if(QueuedVoiceBytes()>0) LastQueuedAudioAt=Now;
        if(TutorialLocalSpeech)
        {
            const bool Speaking=Integration && Integration->LocalTutorialSpeechPlaying();
            if(Now-TutorialStartedAt>.15 && !Speaking && (Now>TutorialDeadline || IsRunningCommandlet())) FinishTutorial();
        }
        else if(!DeferTutorialPlayback && OutputComplete && (!GEngine || !GEngine->UseSound() || QueuedVoiceBytes()==0) && Now-LastAudioPacket>.2 && Now-LastQueuedAudioAt>.3) { if(RequireGrokTutorial && (!GEngine || !GEngine->UseSound() || LastTutorialAudioBytes==0)) StartTutorialFallback(); else FinishTutorial(); }
        else if(!OutputComplete && Now>TutorialDeadline) StartTutorialFallback();
    }
    else if (OutputComplete && SpeechWave && QueuedVoiceBytes()==0 && Now-LastAudioPacket>.2)
    { StopPlayback(); VoiceStatus=MicrophoneMuted ? TEXT("Microphone muted") : TEXT("Listening - speak to COSMO"); OnUpdated.Broadcast(); }
}
void UCosmoRealtimeComponent::Stop()
{
    if(TutorialLocalSpeech && Integration) Integration->StopLocalTutorialSpeech();
    TutorialQueue.Reset(); TutorialActive=false; TutorialLocalSpeech=false; TutorialId.Reset(); TutorialLine.Reset();
    Running=false; DisconnectVoice();
    if (CameraRequest) { CameraRequest->OnProcessRequestComplete().Unbind(); CameraRequest->CancelRequest(); CameraRequest.Reset(); }
    if (Playback) { Playback->DestroyComponent(); Playback=nullptr; }
    CameraTexture=nullptr;
}
void UCosmoRealtimeComponent::EndPlay(const EEndPlayReason::Type Reason)
{ Stop(); Super::EndPlay(Reason); }
