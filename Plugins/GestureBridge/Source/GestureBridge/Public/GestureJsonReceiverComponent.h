#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GestureJsonReceiverComponent.generated.h"

class FSocket;
namespace GestureBridgeCore { class State; }

USTRUCT(BlueprintType)
struct GESTUREBRIDGE_API FGestureJsonFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Gesture") FString SessionId;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") int64 FrameId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") int64 TimestampMs = 0;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") TArray<FName> HeldActions;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") float MoveAxis = 0;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") float TurnAxis = 0;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") bool HasPointer = false;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") FVector2D Pointer = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") bool LeftCalibrated = false;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") bool HeadCalibrated = false;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") bool LeftTracked = false;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") bool RightTracked = false;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") bool FaceTracked = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGestureActionEvent, FName, Action, FName, KeyName, int64, EventId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGestureAxesEvent, float, MoveForward, float, TurnRight);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGestureJsonEvent, const FString&, Json);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGestureConnectionEvent, bool, Connected);

UCLASS(ClassGroup=(Input), meta=(BlueprintSpawnableComponent))
class GESTUREBRIDGE_API UGestureJsonReceiverComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UGestureJsonReceiverComponent();
    virtual ~UGestureJsonReceiverComponent() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gesture") bool AutoStart = true;
    // Empty uses Plugins/GestureBridge/Resources/gesture_bindings.json.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gesture") FString ConfigFile;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") int32 UdpPort = 5052;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") bool Listening = false;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") bool Connected = false;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") FString LastError;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") FGestureJsonFrame LatestFrame;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") FString LatestJson;

    UPROPERTY(BlueprintAssignable, Category="Gesture") FGestureAxesEvent OnAxes;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") float MoveAxis = 0;
    UPROPERTY(BlueprintReadOnly, Category="Gesture") float TurnAxis = 0;
    UPROPERTY(BlueprintAssignable, Category="Gesture") FGestureActionEvent OnActionPressed;
    UPROPERTY(BlueprintAssignable, Category="Gesture") FGestureActionEvent OnActionHeld;
    UPROPERTY(BlueprintAssignable, Category="Gesture") FGestureActionEvent OnActionReleased;
    UPROPERTY(BlueprintAssignable, Category="Gesture") FGestureJsonEvent OnJsonReceived;
    UPROPERTY(BlueprintAssignable, Category="Gesture") FGestureConnectionEvent OnConnectionChanged;

    UFUNCTION(BlueprintCallable, Category="Gesture") bool StartListening();
    UFUNCTION(BlueprintCallable, Category="Gesture") void StopListening();
    UFUNCTION(BlueprintCallable, Category="Gesture") bool ReloadBindings();
    // Optional: feed JSON from an existing Blueprint network receiver instead of this component's socket.
    UFUNCTION(BlueprintCallable, Category="Gesture") bool SubmitJson(const FString& Json);
    UFUNCTION(BlueprintPure, Category="Gesture") bool IsActionHeld(FName Action) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    GestureBridgeCore::State* State = nullptr;
    FSocket* Socket = nullptr;
    bool Configured = false;
    uint64 Generation = 0;
    void Dispatch(double Now);
    void CloseSocket();
    FSocket* OpenSocket(int32 Port);
};
