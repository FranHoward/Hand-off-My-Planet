#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/PlatformProcess.h"
#include "CosmoTutorialGestureMatcher.h"
#include "CosmoUILayout.h"
#include "CosmoGameHUD.generated.h"
class UCosmoRealtimeComponent;
class UFirstPersonAIIntegrationComponent;
class UTexture2D;
class UFont;
class UGestureJsonReceiverComponent;
class UCosmoClientComponent;
UENUM(BlueprintType)
enum class ECosmoTutorialPhase : uint8 { Instruction, AwaitGesture, Feedback, Transition };

UCLASS(BlueprintType)
class COSMOBRIDGE_API UCosmoUIData : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) FString LayoutJson;
    UPROPERTY(EditAnywhere) TMap<FString,TObjectPtr<UTexture2D>> Textures;
    UPROPERTY(EditAnywhere) TObjectPtr<UFont> Orbitron;
    UPROPERTY(EditAnywhere) TObjectPtr<UFont> Mono;
};

/** All screens are painted by one AHUD; tutorial pages are data, not individual HUD classes. */
UCLASS(Blueprintable)
class COSMOBRIDGE_API ACosmoGameHUD : public AHUD
{
    GENERATED_BODY()
public:
    ACosmoGameHUD();
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable) void SetScreen(const FString& Name);
    UFUNCTION(BlueprintCallable) void StartMission();
    UFUNCTION(BlueprintCallable) void AttachIntegration(UFirstPersonAIIntegrationComponent* Source);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UCosmoUIData> UIData;
    UPROPERTY(BlueprintReadOnly) FString Screen;
    UPROPERTY(BlueprintReadOnly) int32 TutorialPage=-1;
    UPROPERTY(BlueprintReadOnly) bool WaitingForVoice=false;
    UPROPERTY(BlueprintReadOnly) bool Gameplay=false;
    UPROPERTY(BlueprintReadOnly) ECosmoTutorialPhase TutorialPhase=ECosmoTutorialPhase::Instruction;
    UPROPERTY(EditAnywhere, Category="Tutorial|Camera") int32 TutorialCameraIndex=0;
    UPROPERTY(EditAnywhere, Category="Tutorial|Camera") bool TutorialSwapHands=false;
    UPROPERTY(EditAnywhere, Category="Tutorial|Camera") bool TutorialInvertHeadTurn=false;
private:
    UPROPERTY(Transient) TObjectPtr<UCosmoRealtimeComponent> Voice;
    UPROPERTY(Transient) TObjectPtr<UCosmoClientComponent> MenuService;
    bool ConnectingOpening=false;
    double OpeningDeadline=0;
    FString OpeningError;
    void TickOpening();
    UFUNCTION() void OpeningAudioReady();
    UPROPERTY(Transient) TObjectPtr<UGestureJsonReceiverComponent> TutorialGestures;
    FProcHandle TutorialRecognition;
    FCosmoTutorialGestureMatcher GestureMatcher;
    TMap<FString,TSharedPtr<FJsonObject>> TutorialSpecs;
    FString InstructionLine, FeedbackLine, RequestedLine, TrackingHint;
    double LastGestureAt=0;
    void StartTutorialTracking();
    void StopTutorialTracking();
    UFUNCTION() void TutorialFrame(const FString& Json);
    void GestureCompleted();
    UPROPERTY(Transient) TObjectPtr<UFirstPersonAIIntegrationComponent> Integration;
    TMap<FString,TSharedPtr<class FJsonObject>> Screens;
    TArray<FString> TutorialScreens;
    struct FButton { FString Action; FBox2D Rect; };
    TArray<FButton> Buttons;
    int32 Focus=0;
    float Scale=1, Opacity=1, FadeTime=0;
    FCosmoUILayout Layout;
    FVector2D DrawingAnchor=FVector2D(.5,.5);
    FString DrawingMode=TEXT("Center");
    void Anchor(const FString& Name) { DrawingMode=Name; DrawingAnchor=FCosmoUILayout::Anchor(Name); }
    void AddButton(const FString& Action,FVector2D Position,FVector2D Size);
    bool FadingOut=false, LineRequested=false, VoiceFailed=false;
    FString PreviousScreen;
    FVector2D LastMouse=FVector2D(-1,-1);
    float QATime=0;
    int32 QAStage=0;
    int64 QAFrame=0;
    bool QAShotWaiting=false,QAShotFeedback=false,QAShotLoading=false;
    int32 QAGesturePage=-2;
    double QAGestureSince=0;
    bool TickQA(float Delta);
    void PaintScreen(const FString& Name);
    void PaintTutorialChoice();
    void BeginTutorial();
    void EnterGame();
    void Text(const FString& Value,FVector2D Pos,float Size,FLinearColor Color,float Width=0,bool Center=false,bool Title=false);
    void Rect(FVector2D Pos,FVector2D Size,FLinearColor Color);
    void Image(UTexture2D* Texture,FVector2D Pos,FVector2D Size,FLinearColor Tint=FLinearColor::White);
    void Click(); void Next(); void Previous(); void Back(); void Confirm();
    void Activate(const FString& Action);
    void BeginPage();
    UFUNCTION() void VoiceFinished(const FString& Line,bool Success);
    UFUNCTION() void Refresh();
    void PaintGameplay();
    UFUNCTION() void AbilityConfirmed(FName Action,bool Success);
    FString Prompt;
    float PromptUntil=0;
    double RunTime=0;
};
UCLASS()
class COSMOBRIDGE_API ACosmoMenuGameMode : public AGameModeBase
{
    GENERATED_BODY()
public: ACosmoMenuGameMode();
};
