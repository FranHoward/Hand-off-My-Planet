#include "CodexRealtimeAVCommandlet.h"
#include "CosmoRealtimeComponent.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "AICompanionManager.h"
#include "CosmoClientComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Misc/ScopeExit.h"

UCodexRealtimeAVCommandlet::UCodexRealtimeAVCommandlet()
{ IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true; }

int32 UCodexRealtimeAVCommandlet::Main(const FString& Params)
{
    const bool Live=Params.Contains(TEXT("LiveVoice"));
    int32 Checks=0,Failures=0;
    auto Check=[&](bool OK,const TCHAR* Name)
    {
        ++Checks; if(!OK) ++Failures;
        UE_LOG(LogTemp,Display,TEXT("REALTIME_AV_TEST %s %s"),OK?TEXT("PASS"):TEXT("FAIL"),Name);
    };
    FCosmoPcmCaptureBuffer Whole,Split;
    Whole.SetActive(true); Split.SetActive(true);
    TArray<float> Samples; Samples.SetNum(960*2);
    for(int32 I=0;I<960;++I) { Samples[I*2]=.8f; Samples[I*2+1]=.2f; }
    Whole.Push(Samples.GetData(),960,2,48000);
    Split.Push(Samples.GetData(),401,2,48000);
    Split.Push(Samples.GetData()+802,559,2,48000);
    const auto PCM=Whole.Drain();
    Check(PCM.Num()==960,TEXT("48k stereo becomes 24k mono PCM16"));
    Check(PCM==Split.Drain(),TEXT("Resampling preserves phase across hardware callbacks"));
    Check(PCM.Num()>1 && (int16(uint16(PCM[0])|(uint16(PCM[1])<<8)))==16384,TEXT("Stereo channels are averaged and encoded little-endian"));
    Whole.SetActive(false); Whole.Push(Samples.GetData(),960,2,48000);
    Check(Whole.Drain().IsEmpty(),TEXT("Muted microphone never queues samples"));
    Whole.SetActive(true);
    for(int32 I=0;I<120;++I) Whole.Push(Samples.GetData(),960,2,48000);
    Check(Whole.Drain().Num()<=96000,TEXT("Capture backlog is bounded"));

    UWorld* Previous=GWorld;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("RealtimeAVVerification"));
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* Instance=NewObject<UGameInstance>(GEngine); Context.OwningGameInstance=Instance; World->SetGameInstance(Instance); GWorld=World;
    ON_SCOPE_EXIT { World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); GWorld=Previous; };
    World->SetGameMode(FURL()); World->InitializeActorsForPlay(FURL());
    auto* Owner=World->SpawnActor<AActor>();
    auto* Integration=NewObject<UFirstPersonAIIntegrationComponent>(Owner);
    Owner->AddInstanceComponent(Integration); Integration->RegisterComponent();
    Integration->TutorialStatus.ExpectedEvent=TEXT("shield_activated");
    auto* AV=NewObject<UCosmoRealtimeComponent>(Owner); Owner->AddInstanceComponent(AV); AV->RegisterComponent();
    Integration->Realtime=AV;
    auto* Client=NewObject<UCosmoClientComponent>(Owner); Client->AutoCheckHealth=false;
    Owner->AddInstanceComponent(Client); Client->RegisterComponent();
    auto* Manager=World->SpawnActor<AAICompanionManager>();
    Manager->SetOwner(Owner);
    Check(Manager->ConnectToCharacter(Owner),TEXT("Blueprint facade shares the existing player integration"));
    AV->VoiceUrl=TEXT("ws://127.0.0.1:18766"); AV->CameraUrl=TEXT("http://127.0.0.1:18767/frame.jpg");
    if(Live) { AV->VoiceUrl=TEXT("ws://127.0.0.1:8766"); AV->EnableCameraPreview=false; }
    World->BeginPlay();
    Check(!AV->AskText(TEXT("before connection")),TEXT("Text cannot bypass disconnected relay"));
    Check(!AV->ApplyCameraJpeg(TArray<uint8>{1,2,3}),TEXT("Malformed camera bytes are rejected"));
    AV->Start();
    auto Pump=[&]()
    {
        ++GFrameCounter; FTSTicker::GetCoreTicker().Tick(.01f);
        FHttpModule::Get().GetHttpManager().Tick(.01f); World->Tick(LEVELTICK_All,.01f);
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); FPlatformProcess::Sleep(.005f);
    };
    auto Await=[&](TFunction<bool()> Condition,double Timeout=8.)
    { const double End=FPlatformTime::Seconds()+Timeout; while(!Condition() && FPlatformTime::Seconds()<End) Pump(); return Condition(); };
    Check(Await([&]{return AV->VoiceReady;},20),Live ? TEXT("UE WebSocket connects to authenticated Grok relay") : TEXT("UE WebSocket connects to offline fixture using loopback Origin"));
    Check(!AV->MicrophoneActive,TEXT("Unattended validation never opens a microphone"));
    if(Live)
    {
        Check(AV->AskText(TEXT("Briefly introduce yourself as COSMO and name the four supported combat abilities.")),TEXT("UE sends a real voice request"));
        Check(Await([&]{return AV->ReplySource==TEXT("grok") && AV->QueuedVoiceBytes()>0 && !AV->AssistantTranscript.IsEmpty();},35),TEXT("Real Grok audio and transcript reach native UE"));
        Check(Manager->Subtitle==AV->AssistantTranscript && Manager->ReplySource==TEXT("grok"),TEXT("Real Grok reply reaches Blueprint companion callbacks"));
        Check(Integration->TutorialStatus.ExpectedEvent==TEXT("shield_activated"),TEXT("Real voice reply cannot complete a gameplay tutorial"));
        UE_LOG(LogTemp,Display,TEXT("REALTIME_AV_LIVE source=%s audio_bytes=%d line=%s"),*AV->ReplySource,AV->QueuedVoiceBytes(),*AV->AssistantTranscript);
        AV->SayLine(TEXT("Hold your right hand open with the palm facing the camera to activate Shield."));
        Check(Await([&]{return !AV->TutorialActive && AV->LastTutorialNativeComplete;},40),TEXT("Authored tutorial returns real Grok TTS and finishes before chat resumes"));
        Check(Manager->Subtitle==TEXT("Hold your right hand open with the palm facing the camera to activate Shield.") && Manager->ReplySource==TEXT("scripted"),TEXT("Real tutorial audio retains exact authored text in Blueprint"));
        UE_LOG(LogTemp,Display,TEXT("TUTORIAL_PRIORITY_LIVE audio_bytes=%d line=%s"),AV->LastTutorialAudioBytes,*Manager->Subtitle);
        AV->Stop();
        Check(!AV->VoiceReady && AV->QueuedVoiceBytes()==0,TEXT("Live session releases its connection and audio queue"));
        UE_LOG(LogTemp,Display,TEXT("REALTIME_AV_LIVE_RESULT checks=%d failures=%d microphone_recorded=false"),Checks,Failures);
        return Failures ? 1 : 0;
    }
    Check(Await([&]{return AV->CameraTexture!=nullptr;}),TEXT("UE HTTP receives a camera JPEG and creates a texture"));
    Check(AV->CameraTexture && AV->CameraTexture->GetSizeX()==480 && AV->CameraTexture->GetSizeY()==270,TEXT("Camera aspect ratio survives JPEG decoding"));
    Check(AV->AskText(TEXT("offline validation")),TEXT("Player text reaches fixture through native WebSocket"));
    Check(Await([&]{return AV->ReplySource==TEXT("grok") && AV->QueuedVoiceBytes()>0;}),TEXT("Streamed PCM reaches the procedural voice queue"));
    Check(Integration->Subtitle==TEXT("Offline voice fixture.") && AV->PlayerTranscript==TEXT("I am done"),TEXT("Player transcription and assistant subtitle reach the HUD state"));
    Check(Manager->Subtitle==Integration->Subtitle && Manager->ReplySource==TEXT("grok"),TEXT("Realtime subtitles and source also reach BP_AICompanionManager"));
    Check(Integration->TutorialStatus.ExpectedEvent==TEXT("shield_activated"),TEXT("Saying done cannot advance a tutorial or activate a skill"));
    Check(AV->SayLine(TEXT("First authored tutorial line")),TEXT("Authored tutorial preempts the current conversation immediately"));
    Check(AV->TutorialActive && AV->QueuedVoiceBytes()==0 && Integration->Subtitle==TEXT("First authored tutorial line"),TEXT("Tutorial clears old audio and pins the exact authored subtitle"));
    AV->SayLine(TEXT("Second authored tutorial line"));
    Check(Integration->Subtitle==TEXT("First authored tutorial line") && !AV->AskText(TEXT("interrupt the tutorial")),TEXT("Continuous tutorials queue while new chat is paused"));
    Integration->ShowRealtimeLine(TEXT("A late chat transcript"));
    Check(Integration->Subtitle==TEXT("First authored tutorial line"),TEXT("Late conversation subtitle cannot overwrite a tutorial"));
    Check(Await([&]{return !AV->TutorialActive && Integration->Subtitle==TEXT("Second authored tutorial line");},6),TEXT("Tutorial audio completes in order before conversation resumes"));
    Check(Manager->Subtitle==TEXT("Second authored tutorial line") && Manager->ReplySource==TEXT("scripted"),TEXT("Blueprint callbacks retain authored tutorial text and source"));
    AV->ReplySource.Reset();
    Check(AV->AskText(TEXT("after tutorial")) && Await([&]{return AV->ReplySource==TEXT("grok") && AV->QueuedVoiceBytes()>0;}),TEXT("Fresh dialogue resumes after the last tutorial line"));
    AV->HandleVoiceEvent(TEXT("{\"type\":\"input_audio_buffer.speech_started\"}"));
    Check(AV->QueuedVoiceBytes()==0,TEXT("Normal conversation still supports speech interruption"));
    AV->AskText(TEXT("offline stale camera"));
    Check(Await([&]{return AV->CameraTexture==nullptr;},5),TEXT("Lost camera clears the old frame instead of freezing a fake live view"));
    AV->Stop();
    Check(!AV->VoiceReady && !AV->MicrophoneActive && !AV->CameraTexture && AV->QueuedVoiceBytes()==0,TEXT("Stop releases microphone, socket, playback and camera texture"));
    UE_LOG(LogTemp,Display,TEXT("REALTIME_AV_TEST_RESULT checks=%d failures=%d fixture_only=true"),Checks,Failures);
    return Failures ? 1 : 0;
}
