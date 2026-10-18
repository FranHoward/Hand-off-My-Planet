#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CosmoHUDWidget.generated.h"

class UFirstPersonAIIntegrationComponent;
class UTextBlock;
class UVerticalBox;
class UProgressBar;
class UBorder;
class UImage;

/** Safe-area HUD; updates on integration events, without per-frame property bindings. */
UCLASS()
class COSMOBRIDGE_API UCosmoHUDWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetIntegration(UFirstPersonAIIntegrationComponent* Source);
    UFUNCTION() void Refresh();
protected:
    virtual void NativeConstruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;
private:
    UPROPERTY(Transient) TObjectPtr<UFirstPersonAIIntegrationComponent> Integration;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ObjectiveText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CombatText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SubtitleText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FeedbackText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BossText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RetryText;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> BossBar;
    UPROPERTY(Transient) TObjectPtr<UBorder> ResultPanel;
    UPROPERTY(Transient) TObjectPtr<UImage> CameraImage;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CameraStatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VoiceStatusText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PlayerSpeechText;
    bool ResultVisible=false;
};
