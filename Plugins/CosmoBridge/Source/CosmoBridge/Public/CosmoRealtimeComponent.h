#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "CosmoRealtimeComponent.generated.h"

class IWebSocket;
class UAudioComponent;
class USoundWaveProcedural;
class UTexture2D;
class UFirstPersonAIIntegrationComponent;
namespace Audio { class FAudioCapture; }

/** Capture callback owns only this bounded buffer; never touches a UObject. */
struct COSMOBRIDGE_API FCosmoPcmCaptureBuffer
{
    void Push(const float* Samples, int32 Frames, int32 Channels, int32 Rate);
    TArray<uint8> Drain();
    void SetActive(bool Active);
private:
    FCriticalSection Mutex;
    bool Enabled = false;
    TArray<float> Mono;
    TArray<uint8> Pcm;
    double Position = 0;
    int32 InputRate = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCosmoRealtimeUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCosmoTutorialFinished, const FString&, Line, bool, Success);

/** Local camera preview plus UE microphone -> Python -> Grok Voice -> UE audio. */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class COSMOBRIDGE_API UCosmoRealtimeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCosmoRealtimeComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Conversation") bool FreeConversation=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Conversation") bool EnableProactiveConversation=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Voice") bool RequireGrokTutorial=false;
    UPROPERTY(BlueprintAssignable, Category="COSMO|Voice") FCosmoTutorialFinished OnTutorialFinished;
    UPROPERTY(BlueprintAssignable, Category="COSMO|Voice") FCosmoRealtimeUpdated OnTutorialAudioReady;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") bool TutorialAudioReady=false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") FString ConnectionError;
    bool DeferTutorialPlayback=false;
    void ReleaseTutorialAudio();
    virtual ~UCosmoRealtimeComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Voice") bool EnableVoice = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Voice") FString VoiceUrl = TEXT("ws://127.0.0.1:8766");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Voice") bool MicrophoneMuted = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Voice") bool AllowVoiceInterruptions = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Voice", meta=(ClampMin="0",ClampMax="1")) float VoiceVolume = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Voice", meta=(ClampMin="0.5",ClampMax="2")) float VoicePitch = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Camera") bool EnableCameraPreview = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="COSMO|Camera") FString CameraUrl = TEXT("http://127.0.0.1:8767/frame.jpg");
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") bool VoiceReady = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") bool MicrophoneActive = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") FString VoiceStatus = TEXT("Voice connecting");
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") FString PlayerTranscript;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") FString AssistantTranscript;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") FString ReplySource;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") bool TutorialActive = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") bool TutorialLocalSpeech = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") FString TutorialLine;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") int32 LastTutorialAudioBytes = 0;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Voice") bool LastTutorialNativeComplete = false;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Camera") TObjectPtr<UTexture2D> CameraTexture;
    UPROPERTY(BlueprintReadOnly, Category="COSMO|Camera") FString CameraStatus = TEXT("Camera starting");
    UPROPERTY(BlueprintAssignable, Category="COSMO") FCosmoRealtimeUpdated OnUpdated;
    UFUNCTION(BlueprintCallable, Category="COSMO") void Start();
    UFUNCTION(BlueprintCallable, Category="COSMO") void Stop();
    UFUNCTION(BlueprintCallable, Category="COSMO|Voice") void SetMicrophoneMuted(bool Muted);
    UFUNCTION(BlueprintCallable, Category="COSMO|Voice") bool AskText(const FString& Text);
    UFUNCTION(BlueprintCallable, Category="COSMO|Voice") bool SayLine(const FString& Text);
    /** Server event entry point, also exercised by offline runtime checks. */
    void HandleVoiceEvent(const FString& Json);
    bool ApplyCameraJpeg(const TArray<uint8>& Jpeg);
    int32 QueuedVoiceBytes() const;
protected:
    virtual void TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    TSharedPtr<IWebSocket> Socket;
    TSharedPtr<Audio::FAudioCapture> Capture;
    TSharedPtr<FCosmoPcmCaptureBuffer, ESPMode::ThreadSafe> CaptureBuffer;
    UPROPERTY(Transient) TObjectPtr<UFirstPersonAIIntegrationComponent> Integration;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Playback;
    UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> SpeechWave;
    FHttpRequestPtr CameraRequest;
    bool Running = false;
    bool OutputComplete = false;
    double NextProactiveAt=0;
    double LastQueuedAudioAt=0;
    double NextConnect = 0, NextCamera = 0, NextAudioSend = 0, NextMicrophoneAttempt = 0, LastCameraFrame = 0, LastAudioPacket = 0;
    double RetryDelay = 1;
    FString ResponseId;
    FString LastVoiceError;
    TArray<FString> TutorialQueue;
    FString TutorialId;
    bool SupportsTutorialPriority = false;
    double TutorialDeadline = 0, TutorialStartedAt = 0;
    TSet<FString> InterruptedResponses;
    void ConnectVoice();
    void DisconnectVoice();
    void StartMicrophone();
    void StopMicrophone();
    void StopPlayback();
    void QueueVoiceAudio(const TArray<uint8>& Pcm);
    void PlayQueuedAudio();
    void MarkTutorialAudioReady();
    void RequestCamera();
    void CameraComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool Success);
    bool SendEvent(const FString& Type, const FString& Text = FString());
    void StartNextTutorial();
    void StartTutorialFallback();
    void FinishTutorial();
};
