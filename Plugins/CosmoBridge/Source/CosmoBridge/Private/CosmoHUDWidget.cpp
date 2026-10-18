#include "CosmoHUDWidget.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "CosmoRealtimeComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SafeZone.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "GameFramework/PlayerController.h"

void UCosmoHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();
    Refresh();
}

TSharedRef<SWidget> UCosmoHUDWidget::RebuildWidget()
{
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
    if (WidgetTree->RootWidget) return Super::RebuildWidget();
    auto* Safe = WidgetTree->ConstructWidget<USafeZone>();
    WidgetTree->RootWidget = Safe;
    auto* Overlay = WidgetTree->ConstructWidget<UOverlay>();
    Safe->AddChild(Overlay);
    auto Panel = [&](EVerticalAlignment Align)
    {
        auto* Border = WidgetTree->ConstructWidget<UBorder>();
        Border->SetBrushColor(FLinearColor(.015f, .025f, .055f, .85f));
        Border->SetPadding(FMargin(18.f, 12.f));
        auto* Slot = Overlay->AddChildToOverlay(Border);
        Slot->SetHorizontalAlignment(HAlign_Fill);
        Slot->SetVerticalAlignment(Align);
        Slot->SetPadding(FMargin(24.f));
        auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
        Border->AddChild(Box);
        return Box;
    };
    auto Label = [&](UVerticalBox* Box, int32 Size, FLinearColor Color)
    {
        auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
        auto Font = Text->GetFont(); Font.Size = Size; Text->SetFont(Font);
        Text->SetColorAndOpacity(FSlateColor(Color));
        Text->SetAutoWrapText(true);
        Box->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.f, 3.f));
        return Text;
    };
    auto* Header = Panel(VAlign_Top);
    auto* HeaderRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("CosmoHeaderRow"));
    Header->AddChildToVerticalBox(HeaderRow);
    auto* Top=WidgetTree->ConstructWidget<UVerticalBox>();
    HeaderRow->AddChildToHorizontalBox(Top)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto* CameraColumn=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CosmoCameraColumn"));
    auto* CameraSlot=HeaderRow->AddChildToHorizontalBox(CameraColumn);
    CameraSlot->SetPadding(FMargin(20.f,0,0,0)); CameraSlot->SetVerticalAlignment(VAlign_Top);
    CameraStatusText=Label(CameraColumn,13,FLinearColor(.35f,.9f,.8f));
    CameraStatusText->SetWrapTextAt(288.f);
    auto* CameraSize=WidgetTree->ConstructWidget<USizeBox>();
    CameraSize->SetWidthOverride(288.f); CameraSize->SetHeightOverride(162.f);
    CameraColumn->AddChildToVerticalBox(CameraSize);
    auto* CameraBorder=WidgetTree->ConstructWidget<UBorder>();
    CameraBorder->SetBrushColor(FLinearColor(.005f,.01f,.02f,1.f)); CameraBorder->SetPadding(FMargin(2));
    CameraSize->AddChild(CameraBorder);
    auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit);
    CameraBorder->AddChild(Scale);
    CameraImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("CosmoCameraImage")); Scale->AddChild(CameraImage);
    VoiceStatusText=Label(CameraColumn,13,FLinearColor(.7f,.85f,1.f));
    PlayerSpeechText=Label(CameraColumn,12,FLinearColor(.8f,.8f,.85f));
    VoiceStatusText->SetWrapTextAt(288.f); PlayerSpeechText->SetWrapTextAt(288.f);
    StatusText = Label(Top, 16, FLinearColor(.4f,.85f,1.f));
    ObjectiveText = Label(Top, 22, FLinearColor::White);
    CombatText = Label(Top, 16, FLinearColor(.8f,.9f,1.f));
    HealthBar=WidgetTree->ConstructWidget<UProgressBar>(); HealthBar->SetFillColorAndOpacity(FLinearColor(.1f,.8f,.65f));
    Top->AddChildToVerticalBox(HealthBar)->SetPadding(FMargin(0,4));
    BossText=Label(Top,18,FLinearColor(1.f,.5f,.15f));
    BossBar=WidgetTree->ConstructWidget<UProgressBar>(); BossBar->SetFillColorAndOpacity(FLinearColor(1.f,.3f,.05f));
    Top->AddChildToVerticalBox(BossBar);
    FeedbackText = Label(Top, 16, FLinearColor(1.f,.65f,.25f));
    auto* Bottom = Panel(VAlign_Bottom);
    auto* Crosshair = WidgetTree->ConstructWidget<UTextBlock>();
    Crosshair->SetText(FText::FromString(TEXT("+")));
    Crosshair->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    auto* CrosshairSlot = Overlay->AddChildToOverlay(Crosshair);
    CrosshairSlot->SetHorizontalAlignment(HAlign_Center);
    CrosshairSlot->SetVerticalAlignment(VAlign_Center);
    SubtitleText = Label(Bottom, 22, FLinearColor::White);
    Label(Bottom, 14, FLinearColor(.65f,.75f,.85f))->SetText(FText::FromString(
        TEXT("Left hand push / pull: move | Left fist: STOP | Head: turn | Right palm: Shield | Fist push: Fire | Swipe: Wave | Pinch: Laser")));
    Label(Bottom, 14, FLinearColor(.65f,.75f,.85f))->SetText(FText::FromString(
        TEXT("Right point 0.6s: Scan | Right V 1s: Hint | Left V 1s: Repeat | Both V 1.5s: Recalibrate")));
    auto* Results=Panel(VAlign_Center);
    ResultPanel=Cast<UBorder>(Results->GetParent());
    ResultText=Label(Results,30,FLinearColor::White);
    RetryText=Label(Results,22,FLinearColor(.2f,.85f,1.f));
    ResultPanel->SetVisibility(ESlateVisibility::Collapsed);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return Super::RebuildWidget();
}

void UCosmoHUDWidget::SetIntegration(UFirstPersonAIIntegrationComponent* Source)
{
    if (Integration) Integration->OnHUDChanged.RemoveDynamic(this, &UCosmoHUDWidget::Refresh);
    Integration = Source;
    if (Integration) Integration->OnHUDChanged.AddUniqueDynamic(this, &UCosmoHUDWidget::Refresh);
    Refresh();
}

void UCosmoHUDWidget::Refresh()
{
    if (!Integration || !StatusText) return;
    const auto* S = Integration.Get();
    if (S->Realtime)
    {
        const auto* AV=S->Realtime.Get();
        CameraStatusText->SetText(FText::FromString(AV->CameraStatus));
        CameraImage->SetBrushFromTexture(AV->CameraTexture,true);
        VoiceStatusText->SetText(FText::FromString(AV->VoiceStatus));
        PlayerSpeechText->SetText(FText::FromString(AV->PlayerTranscript.IsEmpty() ? TEXT("Speak naturally after voice connects") : TEXT("You: ")+AV->PlayerTranscript.Left(120)));
    }
    StatusText->SetText(FText::FromString(FString::Printf(TEXT("COSMO %s  |  %s  |  %s"),
        S->CosmoConnected ? TEXT("online") : TEXT("reconnecting"), *S->GestureStatus,*S->EncounterStatus)));
    ObjectiveText->SetText(FText::FromString(!S->MissionObjective.IsEmpty() ? S->MissionObjective : S->TutorialStatus.IsComplete ? TEXT("Tutorial complete. Ready to explore.") :
        (S->TutorialStatus.CurrentStageName.IsEmpty() ? TEXT("Explore the cyan route. All abilities work offline.") : S->TutorialStatus.CurrentStageName)));
    CombatText->SetText(FText::FromString(FString::Printf(TEXT("Health %.0f / %.0f   |   Shield %.0f%% %s   |   Fire %.1fs   |   Wave %.1fs   |   Laser %s"),
        S->Health,S->MaxHealth,S->ShieldEnergy,S->ShieldActive ? TEXT("ON") : TEXT(""),S->GetCooldownRemaining(TEXT("Fire")),S->GetCooldownRemaining(TEXT("Wave")),S->LaserActive ? TEXT("ON") : TEXT("ready"))));
    HealthBar->SetPercent(S->Health/FMath::Max(1.f,S->MaxHealth));
    const bool ShowBoss=S->EncounterBossMaxHealth>0.f;
    BossText->SetVisibility(ShowBoss ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    BossBar->SetVisibility(ShowBoss ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if(ShowBoss) { BossText->SetText(FText::FromString(FString::Printf(TEXT("STARCORE GUARDIAN %.0f / %.0f"),S->EncounterBossHealth,S->EncounterBossMaxHealth))); BossBar->SetPercent(S->EncounterBossHealth/S->EncounterBossMaxHealth); }
    const bool ShowResult=S->Dead || S->Won;
    ResultPanel->SetVisibility(ShowResult ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    ResultText->SetText(FText::FromString(S->Won ? TEXT("STAR CORE RESTORED - MISSION COMPLETE") : TEXT("EXPLORER DOWN - YOUR CHECKPOINT IS SAVED")));
    RetryText->SetText(FText::FromString(S->Won ? TEXT("Hold both palms open for 1.5s to play again") : TEXT("Hold both palms open for 1.5s to retry checkpoint")));
    ResultVisible=ShowResult;
    SubtitleText->SetText(FText::FromString(S->Subtitle));
    FString Feedback=S->LastError;
    if(Feedback.Contains(TEXT("BaseUrl")) || Feedback.StartsWith(TEXT("Cannot reach COSMO")))
        Feedback=TEXT("COSMO is reconnecting. You can keep exploring.");
    FeedbackText->SetText(FText::FromString(S->ScanProgress > 0.f && S->ScanProgress < 1.f
        ? FString::Printf(TEXT("Scanning %.0f%%: keep the target in view."), S->ScanProgress * 100.f) : Feedback));
}
void UCosmoHUDWidget::NativeDestruct()
{
    if (Integration) Integration->OnHUDChanged.RemoveDynamic(this, &UCosmoHUDWidget::Refresh);
    Super::NativeDestruct();
}

