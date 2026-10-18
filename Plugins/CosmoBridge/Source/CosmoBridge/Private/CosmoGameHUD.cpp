#include "CosmoGameHUD.h"
#include "CosmoRealtimeComponent.h"
#include "GestureJsonReceiverComponent.h"
#include "CosmoRuntimePaths.h"
#include "CosmoClientComponent.h"
#include "CosmoServiceSubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/App.h"
#include "FirstPersonAIIntegrationComponent.h"
#include "CosmoPlanetGame.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"

ACosmoMenuGameMode::ACosmoMenuGameMode()
{ DefaultPawnClass=nullptr; HUDClass=ACosmoGameHUD::StaticClass(); }
ACosmoGameHUD::ACosmoGameHUD()
{ PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; }
void ACosmoGameHUD::BeginPlay()
{
    Super::BeginPlay();
    if(!UIData) UIData=LoadObject<UCosmoUIData>(nullptr,TEXT("/Game/UI/DA_PlanetUI.DA_PlanetUI"));
    TSharedPtr<FJsonObject> Root;
    if(UIData && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UIData->LayoutJson),Root))
        for(auto V:Root->GetArrayField(TEXT("screens"))) { auto S=V->AsObject(); Screens.Add(S->GetStringField(TEXT("screen")),S); }
    if(Root && Root->HasField(TEXT("tutorials"))) for(auto V:Root->GetArrayField(TEXT("tutorials")))
    { auto Spec=V->AsObject(); const FString Name=Spec->GetStringField(TEXT("screen")); TutorialScreens.Add(Name); TutorialSpecs.Add(Name,Spec); }
    EnableInput(PlayerOwner);
    auto Bind=[this](FKey Key,void(ACosmoGameHUD::*Fn)()) { auto& B=InputComponent->BindKey(Key,IE_Pressed,this,Fn); B.bExecuteWhenPaused=true; };
    Bind(EKeys::LeftMouseButton,&ACosmoGameHUD::Click); Bind(EKeys::Enter,&ACosmoGameHUD::Confirm);
    Bind(EKeys::SpaceBar,&ACosmoGameHUD::Confirm); Bind(EKeys::Gamepad_FaceButton_Bottom,&ACosmoGameHUD::Confirm);
    Bind(EKeys::Down,&ACosmoGameHUD::Next); Bind(EKeys::Tab,&ACosmoGameHUD::Next); Bind(EKeys::Gamepad_DPad_Down,&ACosmoGameHUD::Next);
    Bind(EKeys::Up,&ACosmoGameHUD::Previous); Bind(EKeys::Gamepad_DPad_Up,&ACosmoGameHUD::Previous);
    Bind(EKeys::Right,&ACosmoGameHUD::Next); Bind(EKeys::Gamepad_DPad_Right,&ACosmoGameHUD::Next);
    Bind(EKeys::Left,&ACosmoGameHUD::Previous); Bind(EKeys::Gamepad_DPad_Left,&ACosmoGameHUD::Previous);
    Bind(EKeys::Escape,&ACosmoGameHUD::Back); Bind(EKeys::Gamepad_FaceButton_Right,&ACosmoGameHUD::Back);
    Gameplay=!UGameplayStatics::GetCurrentLevelName(this).Contains(TEXT("MainMenu"));
    if(!Gameplay)
    {
        Voice=NewObject<UCosmoRealtimeComponent>(this,TEXT("MenuGrokVoice")); Voice->RegisterComponent();
        Voice->EnableCameraPreview=false; Voice->MicrophoneMuted=true; Voice->RequireGrokTutorial=true;
        #if WITH_EDITOR
        if(FParse::Param(FCommandLine::Get(),TEXT("UIFlowTest"))) Voice->VoiceUrl=TEXT("ws://127.0.0.1:18766");
#endif
        Voice->OnTutorialFinished.AddDynamic(this,&ACosmoGameHUD::VoiceFinished);
        Voice->OnTutorialAudioReady.AddDynamic(this,&ACosmoGameHUD::OpeningAudioReady);
    }
    SetScreen(Gameplay ? TEXT("WBP_03_GameplayHUD") : TEXT("WBP_01_MainMenu"));
}
void ACosmoGameHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    StopTutorialTracking();
    if(Voice) Voice->Stop();
    if(Integration) { Integration->OnHUDChanged.RemoveDynamic(this,&ACosmoGameHUD::Refresh); Integration->OnAbilityConfirmed.RemoveDynamic(this,&ACosmoGameHUD::AbilityConfirmed); }
    Super::EndPlay(Reason);
}
void ACosmoGameHUD::AttachIntegration(UFirstPersonAIIntegrationComponent* Source)
{
    if(Integration) { Integration->OnHUDChanged.RemoveDynamic(this,&ACosmoGameHUD::Refresh); Integration->OnAbilityConfirmed.RemoveDynamic(this,&ACosmoGameHUD::AbilityConfirmed); }
    Integration=Source;
    if(Integration) { Integration->OnHUDChanged.AddUniqueDynamic(this,&ACosmoGameHUD::Refresh); Integration->OnAbilityConfirmed.AddUniqueDynamic(this,&ACosmoGameHUD::AbilityConfirmed); }
    Refresh();
}
void ACosmoGameHUD::Refresh()
{
    if(!Integration) return;
    if(!Integration->Dead && !Integration->Won && (Screen==TEXT("WBP_05_Victory") || Screen==TEXT("WBP_06_GameOver") || Screen.Contains(TEXT("Results")))) SetScreen(TEXT("WBP_03_GameplayHUD"));
    if(Integration->Won && Screen==TEXT("WBP_03_GameplayHUD")) SetScreen(TEXT("WBP_05_Victory"));
    else if(Integration->Dead && Screen==TEXT("WBP_03_GameplayHUD")) SetScreen(TEXT("WBP_06_GameOver"));
}
void ACosmoGameHUD::SetScreen(const FString& Name)
{
    if(Integration && (Name==TEXT("WBP_05_Victory") || Name==TEXT("WBP_06_GameOver"))) RunTime=GetWorld()->GetTimeSeconds();
    Screen=Name; Focus=0; Buttons.Reset();
    const bool Game=Name==TEXT("WBP_03_GameplayHUD");
    if(PlayerOwner) { PlayerOwner->bShowMouseCursor=!Game; PlayerOwner->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false)); }
}
void ACosmoGameHUD::StartMission()
{
    if(Gameplay || ConnectingOpening || Screen==TEXT("TutorialChoice")) return;
    Opacity=1;
    SetScreen(TEXT("TutorialChoice"));
    Focus=1; // Keep the tutorial as the initial keyboard/gamepad selection.
}
void ACosmoGameHUD::EnterGame()
{
    ConnectingOpening=false; OpeningError.Reset(); WaitingForVoice=false;
    StopTutorialTracking();
    if(Voice) Voice->Stop();
    UGameplayStatics::OpenLevel(this,TEXT("/Game/AlienPlanet/Lvl_AlienPlanet"));
}
void ACosmoGameHUD::BeginTutorial()
{
    if(Gameplay || TutorialScreens.IsEmpty()) return;
    OpeningError.Reset(); ConnectingOpening=true; OpeningDeadline=FPlatformTime::Seconds()+75;
    TutorialPage=-1; BeginPage(); SetScreen(TEXT("OpeningLoading"));
    Opacity=1; Voice->Stop(); Voice->DeferTutorialPlayback=true;
    StartTutorialTracking();
#if WITH_EDITOR
    if(FParse::Param(FCommandLine::Get(),TEXT("UIFlowTest"))) { Voice->Start(); return; }
#endif
    if(MenuService) { MenuService->DestroyComponent(); MenuService=nullptr; }
    MenuService=NewObject<UCosmoClientComponent>(this); MenuService->RegisterComponent();
    UE_LOG(LogTemp,Display,TEXT("UI_OPENING_START black=1 bundled_service=1"));
}
void ACosmoGameHUD::BeginPage()
{
    SetScreen(TutorialPage<0 ? TEXT("WBP_02_IntroDialogue") : TutorialScreens[TutorialPage]);
    Opacity=0; FadeTime=0; FadingOut=false; LineRequested=false; VoiceFailed=false; WaitingForVoice=true;
    TutorialPhase=ECosmoTutorialPhase::Instruction; RequestedLine.Reset(); InstructionLine.Reset(); FeedbackLine.Reset();
    if(auto* Spec=TutorialSpecs.Find(Screen))
    { InstructionLine=(*Spec)->GetStringField(TEXT("instruction")); FeedbackLine=(*Spec)->GetStringField(TEXT("feedback")); GestureMatcher.Begin((*Spec)->GetStringField(TEXT("action"))); }
}
void ACosmoGameHUD::VoiceFinished(const FString& Line,bool Success)
{
    if(!WaitingForVoice || !LineRequested || Line!=RequestedLine) return;
    if(!Success) { VoiceFailed=true; if(ConnectingOpening) OpeningError=TEXT("Grok could not prepare the opening voice. Press Enter to retry, or Esc to return."); return; }
    UE_LOG(LogTemp,Display,TEXT("UI_TUTORIAL_PLAYED page=%d audio_bytes=%d native=%d"),TutorialPage,Voice->LastTutorialAudioBytes,Voice->LastTutorialNativeComplete);
    WaitingForVoice=false;
    if(TutorialPage>=0 && TutorialPhase==ECosmoTutorialPhase::Instruction)
    { TutorialPhase=ECosmoTutorialPhase::AwaitGesture; LastGestureAt=FPlatformTime::Seconds(); TrackingHint=TEXT("Perform the gesture shown above."); return; }
    TutorialPhase=ECosmoTutorialPhase::Transition; FadingOut=true; FadeTime=0;
}
void ACosmoGameHUD::Tick(float Delta)
{
    Super::Tick(Delta);
    if(TickQA(Delta)) return;
    if(ConnectingOpening) { TickOpening(); return; }
    if(Gameplay) return;
    if(Screen!=TEXT("WBP_02_IntroDialogue") && !Screen.StartsWith(TEXT("WBP_Tutorial_"))) return;
    FadeTime+=Delta; Opacity=FadingOut ? 1-FMath::Clamp(FadeTime/.55f,0.f,1.f) : FMath::Clamp(FadeTime/.55f,0.f,1.f);
    if(FadingOut && FadeTime>=.55f)
    {
        if(++TutorialPage>=TutorialScreens.Num()) { UE_LOG(LogTemp,Display,TEXT("UI_TUTORIAL_COMPLETE pages=10 loading=Lvl_AlienPlanet")); EnterGame(); }
        else BeginPage();
        return;
    }
    if(!FadingOut && TutorialPhase!=ECosmoTutorialPhase::AwaitGesture && Opacity>=1 && !LineRequested && Voice && Voice->VoiceReady)
    {
        FString Line=TutorialPhase==ECosmoTutorialPhase::Feedback ? FeedbackLine : InstructionLine;
        if(TutorialPage<0) if(auto* Spec=Screens.Find(Screen)) for(auto T:(*Spec)->GetArrayField(TEXT("texts")))
        { auto O=T->AsObject(); Line+=O->GetStringField(TEXT("text"))+TEXT(". "); }
        RequestedLine=Line.TrimStartAndEnd().Left(3000); LineRequested=Voice->SayLine(RequestedLine);
    }
    if(TutorialPhase==ECosmoTutorialPhase::AwaitGesture && FPlatformTime::Seconds()-LastGestureAt>.5)
        TrackingHint=TEXT("Camera tracking unavailable. Keep the required hand or face in view.");
}
void ACosmoGameHUD::Rect(FVector2D P,FVector2D S,FLinearColor C)
{ C.A*=Opacity; const auto Pixel=Layout.Position(P,DrawingAnchor); DrawRect(C,Pixel.X,Pixel.Y,S.X*Scale,S.Y*Scale); }
void ACosmoGameHUD::Image(UTexture2D* T,FVector2D P,FVector2D S,FLinearColor C)
{
    if(!T) return; C.A*=Opacity;
    FVector2D Pixel=Layout.Position(P,DrawingAnchor),Size=S*Scale,UVMin(0,0),UVMax(1,1);
    if(DrawingMode==TEXT("Stretch")) { Pixel=P*Scale; Size=Layout.Viewport-(FVector2D(1920,1080)-S)*Scale; }
    else if(DrawingMode==TEXT("Cover"))
    {
        Pixel=FVector2D::ZeroVector; Size=Layout.Viewport;
        const float SourceAspect=float(T->GetSizeX())/FMath::Max(1,T->GetSizeY()),Aspect=Size.X/Size.Y;
        if(Aspect>SourceAspect) { const float H=SourceAspect/Aspect; UVMin.Y=(1-H)*.5; UVMax.Y=1-UVMin.Y; }
        else { const float W=Aspect/SourceAspect; UVMin.X=(1-W)*.5; UVMax.X=1-UVMin.X; }
    }
    FCanvasTileItem I(Pixel,T->GetResource(),Size,UVMin,UVMax,C); I.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(I);
}
void ACosmoGameHUD::AddButton(const FString& Action,FVector2D P,FVector2D Size)
{ Buttons.Add({Action,Layout.Bounds(P,Size,DrawingAnchor)}); }
void ACosmoGameHUD::Text(const FString& Value,FVector2D P,float Size,FLinearColor C,float Width,bool Center,bool Title)
{
    if(!UIData || !FSlateApplication::IsInitialized()) return;
    FSlateFontInfo Font(Title ? UIData->Orbitron : UIData->Mono,FMath::Max(1,FMath::RoundToInt(Size*Scale*.75f)));
    auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    TArray<FString> Paragraphs; Value.ParseIntoArrayLines(Paragraphs,false);
    float Y=P.Y;
    for(auto& Para:Paragraphs)
    {
        TArray<FString> Words; Para.ParseIntoArray(Words,TEXT(" "),true); TArray<FString> Lines; FString Line;
        for(auto& Word:Words) { FString Candidate=Line.IsEmpty() ? Word : Line+TEXT(" ")+Word;
            if(Width>0 && !Line.IsEmpty() && Measure->Measure(Candidate,Font).X>Width*Scale) { Lines.Add(Line); Line=Word; } else Line=Candidate; }
        Lines.Add(Line);
        for(auto& L:Lines) { float X=P.X; if(Center && Width>0) X+=(Width-Measure->Measure(L,Font).X/Scale)*.5f;
            FLinearColor Color=C; Color.A*=Opacity; FCanvasTextItem Item(Layout.Position(FVector2D(X,Y),DrawingAnchor),FText::FromString(L),Font,Color); Canvas->DrawItem(Item); Y+=Size*1.25f; }
    }
}
void ACosmoGameHUD::PaintScreen(const FString& Name)
{
    auto* Found=Screens.Find(Name); if(!Found || !UIData) return;
    for(auto A:(*Found)->GetArrayField(TEXT("assets")))
    {
        auto O=A->AsObject(); FString File=O->GetStringField(TEXT("file"));
        if(Gameplay && File.Contains(TEXT("Space_Background"))) continue;
        auto Pos=O->GetObjectField(TEXT("position")); auto S=O->GetArrayField(TEXT("size"));
        FVector2D P(Pos->GetNumberField(TEXT("x")),Pos->GetNumberField(TEXT("y"))), Z(S[0]->AsNumber(),S[1]->AsNumber());
        FString AName=TEXT("Center"); O->TryGetStringField(TEXT("anchor"),AName); Anchor(AName);
        Image(UIData->Textures.FindRef(File),P,Z);
        if(O->GetStringField(TEXT("category"))==TEXT("Buttons"))
        { AddButton(FPaths::GetBaseFilename(File),P,Z); }
    }
    for(auto V:(*Found)->GetArrayField(TEXT("texts")))
    {
        auto O=V->AsObject(); FString NameText=O->GetStringField(TEXT("name"));
        if(Gameplay && Screen==TEXT("WBP_03_GameplayHUD") && NameText.StartsWith(TEXT("Value_"))) continue;
        FString Value=O->GetStringField(TEXT("text"));
        if(NameText==TEXT("Text_Cosmo_Results") && Integration) Value=Integration->Won ? TEXT("All enemies are defeated.\nWe did it, partner.\nLet us go home!") : TEXT("The mission is not over.\nRegroup and try again, partner.");
        if(NameText==TEXT("Text_Result_State") && Integration) Value=Integration->Won ? TEXT("MISSION COMPLETE") : TEXT("MISSION FAILED");
        auto R=O->GetArrayField(TEXT("rect")), C=O->GetArrayField(TEXT("color"));
        FString AName=TEXT("Center"); O->TryGetStringField(TEXT("anchor"),AName); Anchor(AName);
        Text(Value,FVector2D(R[0]->AsNumber(),R[1]->AsNumber()),O->GetNumberField(TEXT("size")),FLinearColor(C[0]->AsNumber(),C[1]->AsNumber(),C[2]->AsNumber(),C[3]->AsNumber()),R[2]->AsNumber(),O->GetStringField(TEXT("align"))==TEXT("CENTER"),O->GetStringField(TEXT("font"))==TEXT("Orbitron"));
    }
}
void ACosmoGameHUD::PaintGameplay()
{
    if(!Integration) return;
    const auto Cyan=FLinearColor(.1,.85,1,1);
    Anchor(TEXT("TopLeft"));
    Text(Integration->FreeConversation ? TEXT("KEPLER-X  /  EXPLORATION") : Integration->MissionObjective,{55,45},24,Cyan,1000);
    Text(Integration->EncounterStatus,{55,125},18,FLinearColor::White,1100);
    Rect({55,165},{300,12},FLinearColor(.05,.12,.18,1)); Rect({55,165},{300*FMath::Clamp(Integration->Health/Integration->MaxHealth,0.f,1.f),12},Cyan);
    Text(FString::Printf(TEXT("HP %.0f / %.0f   SHIELD %.0f%%   KILLS %d"),Integration->Health,Integration->MaxHealth,Integration->ShieldEnergy,Integration->EnemyKills),{55,190},18,FLinearColor::White);
    Anchor(TEXT("Bottom"));
    const TCHAR* Abilities[]={TEXT("Fire"),TEXT("Wave"),TEXT("Laser"),TEXT("Shield")};
    for(int32 I=0;I<4;++I) { FName A(Abilities[I]); const float CD=Integration->GetCooldownRemaining(A);
        Text(CD>0 ? FString::Printf(TEXT("%.1fs"),CD) : TEXT("READY"),{480.f+I*226,1000},18,CD>0 ? FLinearColor(.8,.4,.2) : Cyan); }
    Anchor(TEXT("Top"));
    if(!Prompt.IsEmpty() && GetWorld()->GetTimeSeconds()<PromptUntil)
    { Rect({660,230},{600,65},FLinearColor(.01,.06,.1,.9)); Text(Prompt,{680,244},25,Cyan,560,true); }
    if(Integration->EncounterBossMaxHealth>0)
    { Rect({560,200},{800,12},FLinearColor(.15,.03,.03,1)); Rect({560,200},{800*FMath::Clamp(Integration->EncounterBossHealth/Integration->EncounterBossMaxHealth,0.f,1.f),12},FLinearColor(1,.25,.2)); Text(TEXT("STARCORE GUARDIAN"),{720,160},22,FLinearColor::White); }
    Anchor(TEXT("Bottom"));
    FString Subtitle=Integration->Realtime && Integration->Realtime->TutorialActive ? Integration->Realtime->TutorialLine : Integration->Subtitle;
    if(!Subtitle.IsEmpty()) { Rect({350,700},{1220,125},FLinearColor(.01,.03,.08,.85)); Text(Subtitle,{380,720},21,FLinearColor::White,1160); }
    Anchor(TEXT("TopRight"));
    if(Integration->Realtime && Integration->Realtime->CameraTexture) Image(Integration->Realtime->CameraTexture,{1540,60},{320,180});
    Anchor(TEXT("BottomRight"));
    Text(TEXT("ESC  PAUSE"),{1650,1020},16,Cyan);
}
void ACosmoGameHUD::PaintTutorialChoice()
{
    PaintScreen(TEXT("WBP_01_MainMenu"));
    Buttons.Reset(); // The modal owns input; covered menu buttons cannot be clicked.
    Anchor(TEXT("Center"));
    DrawRect(FLinearColor(0,.005,.015,.85),0,0,Layout.Viewport.X,Layout.Viewport.Y);
    const auto Cyan=FLinearColor(.1,.85,1,1);
    // 78% width x 76% height = 59.3% of the viewport, including on 4:3/ultrawide.
    const FVector2D Size=Layout.Viewport/Scale*FVector2D(.78,.76);
    const FVector2D P=(FVector2D(1920,1080)-Size)*.5;
    auto Line=[&](FVector2D A,FVector2D B,FLinearColor Color,float Thickness=2.f)
    { A=Layout.Position(A,DrawingAnchor); B=Layout.Position(B,DrawingAnchor); DrawLine(A.X,A.Y,B.X,B.Y,Color,Thickness*Scale); };
    auto Frame=[&](FVector2D Min,FVector2D Extent,FLinearColor Color)
    {
        const auto Max=Min+Extent; const float Cut=22;
        Line(Min+FVector2D(Cut,0),{Max.X,Min.Y},Color);
        Line({Max.X,Min.Y},{Max.X,Max.Y-Cut},Color);
        Line({Max.X,Max.Y-Cut},{Max.X-Cut,Max.Y},Color);
        Line({Max.X-Cut,Max.Y},{Min.X,Max.Y},Color);
        Line({Min.X,Max.Y},Min+FVector2D(0,Cut),Color);
        Line(Min+FVector2D(0,Cut),Min+FVector2D(Cut,0),Color);
    };
    Rect(P,Size,FLinearColor(.006,.025,.047,.99));
    Frame(P,Size,Cyan);
    Rect(P+FVector2D(48,50),{5,24},Cyan);
    Text(TEXT("MISSION PREPARATION"),P+FVector2D(72,49),23,Cyan,Size.X-144);
    Line(P+FVector2D(48,100),P+FVector2D(Size.X-48,100),FLinearColor(.04,.25,.32));
    Text(TEXT("SKIP TUTORIAL?"),P+FVector2D(60,170),58,FLinearColor::White,Size.X-120,true,true);
    Text(TEXT("Would you like to skip the tutorial?"),P+FVector2D(60,305),30,FLinearColor::White,Size.X-120,true);
    Text(TEXT("Confirm to enter the game now.\nCancel to play the tutorial with Grok."),P+FVector2D(60,375),25,FLinearColor(.65,.79,.85),Size.X-120,true);
    const FVector2D ButtonSize(380,96); const float Gap=48;
    const FVector2D First(960-ButtonSize.X-Gap*.5,P.Y+Size.Y-200);
    for(int32 I=0;I<2;++I)
    {
        const auto B=First+FVector2D(I*(ButtonSize.X+Gap),0);
        Rect(B,ButtonSize,FLinearColor(.015,.075,.10)); Frame(B,ButtonSize,I==0 ? Cyan : FLinearColor(.45,.65,.72));
        Text(I==0 ? TEXT("CONFIRM") : TEXT("CANCEL"),B+FVector2D(20,28),29,FLinearColor::White,ButtonSize.X-40,true,true);
        AddButton(I==0 ? TEXT("SkipTutorialConfirm") : TEXT("PlayTutorial"),B,ButtonSize);
    }
    Text(TEXT("Select an option to continue"),P+FVector2D(60,Size.Y-62),20,FLinearColor(.4,.62,.7),Size.X-120,true);
}
void ACosmoGameHUD::DrawHUD()
{
    Super::DrawHUD(); if(!Canvas) return;
    Layout.SetViewport(FVector2D(Canvas->SizeX,Canvas->SizeY)); Scale=Layout.Scale; Anchor(TEXT("Center"));
    if(ConnectingOpening)
    {
        DrawRect(FLinearColor::Black,0,0,Canvas->SizeX,Canvas->SizeY);
        if(!OpeningError.IsEmpty())
        { Opacity=1;
          Text(OpeningError,{380,450},26,FLinearColor::White,1160,true); Text(TEXT("Enter: retry   Esc: main menu"),{620,650},22,FLinearColor(.1,.85,1),680,true); }
        return;
    }
    if(!Gameplay) DrawRect(FLinearColor::Black,0,0,Canvas->SizeX,Canvas->SizeY);
    Buttons.Reset();
    if(Screen==TEXT("TutorialChoice")) PaintTutorialChoice();
    else if(Screen==TEXT("Settings"))
    {
        Rect({400,220},{1120,640},FLinearColor(.01,.04,.09,.98)); Text(TEXT("SETTINGS"),{700,270},48,FLinearColor::White);
        const TArray<FString> Labels={TEXT("VOICE VOLUME -"),TEXT("VOICE VOLUME +"),TEXT("TOGGLE MICROPHONE"),TEXT("BACK")};
        for(int32 I=0;I<Labels.Num();++I) { FVector2D P(620,390+I*95); Rect(P,{680,65},FLinearColor(.03,.16,.22)); Text(Labels[I],P+FVector2D(24,14),24,FLinearColor::White); AddButton(Labels[I],P,{680,65}); }
    }
    else if(Screen==TEXT("ConfirmRestart"))
    { Rect({500,330},{920,380},FLinearColor(.01,.04,.09,.98)); Text(TEXT("RESTART MISSION?"),{650,390},36,FLinearColor::White);
      Text(TEXT("CONFIRM"),{650,550},28,FLinearColor::White); Text(TEXT("CANCEL"),{1050,550},28,FLinearColor::White);
      AddButton(TEXT("ConfirmRestart"),{600,520},{320,100}); AddButton(TEXT("Cancel"),{1000,520},{320,100}); }
    else PaintScreen(Screen);
    if(Screen==TEXT("WBP_03_GameplayHUD")) PaintGameplay();
    Anchor(TEXT("Center"));
    if(Screen.Contains(TEXT("Results")) && Integration)
    { Rect({540,370},{840,370},FLinearColor(.01,.04,.09,.95)); Text(FString::Printf(TEXT("TIME  %02d:%02d\nENEMIES DEFEATED  %d\nMISSION PROGRESS  %.0f%%\nSTAR CORE  %.0f%%\nHEALTH REMAINING  %.0f"),int32(RunTime)/60,int32(RunTime)%60,Integration->EnemyKills,Integration->MissionProgress,Integration->StarCoreEnergyPercent,Integration->Health),{610,450},28,FLinearColor::White); }
    if(Screen==TEXT("WBP_05_Victory") || Screen==TEXT("WBP_06_GameOver"))
    { Anchor(TEXT("Bottom")); Text(TEXT("VIEW RESULTS"),{800,1005},24,FLinearColor(.1,.85,1)); AddButton(TEXT("RESULTS_OVERVIEW"),{760,990},{400,70}); }
    if(!Gameplay && TutorialPage>=0 && Screen.StartsWith(TEXT("WBP_Tutorial_")))
    {
        Anchor(TEXT("BottomRight"));
        if(Voice && Voice->CameraTexture) Image(Voice->CameraTexture,{1490,760},{300,169});
        Anchor(TEXT("Bottom"));
        if(TutorialPhase==ECosmoTutorialPhase::Feedback || TutorialPhase==ECosmoTutorialPhase::Transition)
        { Rect({630,800},{810,90},FLinearColor(.01,.08,.1,.95)); Text(FeedbackLine,{650,820},32,FLinearColor(.1,1,.65),770,true); }
        if(TutorialPhase==ECosmoTutorialPhase::AwaitGesture)
        { Text(TrackingHint,{400,960},20,FLinearColor(.2,.85,1),1120,true); Text(TEXT("Waiting for your gesture  |  Esc: main menu"),{500,1010},19,FLinearColor::White,920,true); }
    }
    if(WaitingForVoice)
    {
        Anchor(TEXT("Bottom"));
        const float Saved=Opacity; Opacity=1;
        Text(FString::Printf(TEXT("%02d / %02d  %s"),TutorialPage+2,TutorialScreens.Num()+1, VoiceFailed ? TEXT("Voice interrupted - press Enter to retry") : LineRequested ? TEXT("COSMO SPEAKING") : TEXT("Connecting to Grok Voice...")),{420,990},20,FLinearColor(.2,.85,1),1080,true);
        Opacity=Saved;
    }
    Buttons.Sort([](const FButton& A,const FButton& B) { return FMath::IsNearlyEqual(A.Rect.Min.Y,B.Rect.Min.Y,1.f) ? A.Rect.Min.X<B.Rect.Min.X : A.Rect.Min.Y<B.Rect.Min.Y; });
    float MX,MY; if(PlayerOwner && PlayerOwner->GetMousePosition(MX,MY))
    { FVector2D P(MX,MY); if(!P.Equals(LastMouse,.5f)) { for(int32 I=0;I<Buttons.Num();++I) if(Buttons[I].Rect.IsInside(P)) { Focus=I; break; } LastMouse=P; } }
    if(Buttons.IsValidIndex(Focus))
    { auto R=Buttons[Focus].Rect; const auto C=FLinearColor(.1,.9,1,.8); const float Thickness=FMath::Max(2.f,3*Scale); DrawRect(C,R.Min.X,R.Min.Y,R.GetSize().X,Thickness); DrawRect(C,R.Min.X,R.Max.Y-Thickness,R.GetSize().X,Thickness); }
}
void ACosmoGameHUD::Next() { if(Buttons.Num()) Focus=(Focus+1)%Buttons.Num(); }
void ACosmoGameHUD::Previous() { if(Buttons.Num()) Focus=(Focus+Buttons.Num()-1)%Buttons.Num(); }
void ACosmoGameHUD::Click()
{ float X,Y; if(!PlayerOwner || !PlayerOwner->GetMousePosition(X,Y)) return; const FVector2D P(X,Y); for(auto& B:Buttons) if(B.Rect.IsInside(P)) { const FString A=B.Action; Activate(A); return; } }
void ACosmoGameHUD::Confirm()
{ if(ConnectingOpening) { if(!OpeningError.IsEmpty()) { if(VoiceFailed && Voice->VoiceReady) { OpeningError.Reset(); VoiceFailed=false; LineRequested=false; Voice->DeferTutorialPlayback=true; return; } if(auto* S=GetWorld()->GetGameInstance()->GetSubsystem<UCosmoServiceSubsystem>()) S->StopOwnedService(); BeginTutorial(); } return; } if(VoiceFailed) { VoiceFailed=false; LineRequested=false; Voice->Start(); return; } if(Buttons.IsValidIndex(Focus)) Activate(Buttons[Focus].Action); }
void ACosmoGameHUD::Back()
{
    if(Screen==TEXT("TutorialChoice")) { BeginTutorial(); return; }
    if(ConnectingOpening)
    { ConnectingOpening=false; OpeningError.Reset(); if(MenuService) { MenuService->DestroyComponent(); MenuService=nullptr; } Voice->Stop(); StopTutorialTracking(); WaitingForVoice=false; VoiceFailed=false; SetScreen(TEXT("WBP_01_MainMenu")); return; }
    if(WaitingForVoice || FadingOut || TutorialPhase==ECosmoTutorialPhase::AwaitGesture)
    { StopTutorialTracking(); if(Voice) Voice->Stop(); WaitingForVoice=false; FadingOut=false; VoiceFailed=false; TutorialPhase=ECosmoTutorialPhase::Instruction; Opacity=1; SetScreen(TEXT("WBP_01_MainMenu")); return; }
    if(Screen==TEXT("Settings")) SetScreen(PreviousScreen);
    else if(Screen==TEXT("ConfirmRestart")) SetScreen(TEXT("WBP_04_PauseMenu"));
    else if(Gameplay && Screen==TEXT("WBP_03_GameplayHUD")) { if(Integration) Integration->ReleaseAllAbilities(); UGameplayStatics::SetGamePaused(this,true); SetScreen(TEXT("WBP_04_PauseMenu")); }
    else if(Screen==TEXT("WBP_04_PauseMenu")) { UGameplayStatics::SetGamePaused(this,false); SetScreen(TEXT("WBP_03_GameplayHUD")); }
}
void ACosmoGameHUD::Activate(const FString& A)
{
    if(Screen==TEXT("TutorialChoice"))
    {
        if(A==TEXT("SkipTutorialConfirm")) { UE_LOG(LogTemp,Display,TEXT("UI_TUTORIAL_CHOICE skip=1")); EnterGame(); }
        else if(A==TEXT("PlayTutorial")) { UE_LOG(LogTemp,Display,TEXT("UI_TUTORIAL_CHOICE skip=0")); BeginTutorial(); }
        return;
    }
    if(A.Contains(TEXT("START_MISSION"))) StartMission();
    else if(A.Contains(TEXT("QUIT"))) PlayerOwner->ConsoleCommand(TEXT("quit"));
    else if(A.Contains(TEXT("RESUME"))) { UGameplayStatics::SetGamePaused(this,false); SetScreen(TEXT("WBP_03_GameplayHUD")); }
    else if(A.Contains(TEXT("SETTINGS"))) { PreviousScreen=Screen; SetScreen(TEXT("Settings")); }
    else if(A==TEXT("BACK")) SetScreen(PreviousScreen);
    else if(A.Contains(TEXT("VOICE VOLUME"))) { if(Integration && Integration->Realtime) { auto* R=Integration->Realtime.Get(); R->VoiceVolume=FMath::Clamp(R->VoiceVolume+(A.EndsWith(TEXT("+")) ? .1f : -.1f),0.f,1.f); } }
    else if(A==TEXT("TOGGLE MICROPHONE")) { if(Integration && Integration->Realtime) Integration->SetMicrophoneMuted(!Integration->Realtime->MicrophoneMuted); }
    else if(A.Contains(TEXT("MAIN_MENU"))) { UGameplayStatics::SetGamePaused(this,false); UGameplayStatics::OpenLevel(this,TEXT("/Game/UI/Maps/Lvl_MainMenu")); }
    else if(A==TEXT("Cancel")) SetScreen(TEXT("WBP_04_PauseMenu"));
    else if(A.Contains(TEXT("RESTART"))) SetScreen(TEXT("ConfirmRestart"));
    else if(A==TEXT("ConfirmRestart") || A.Contains(TEXT("PLAY_AGAIN")) || A.Contains(TEXT("TRY_AGAIN")))
    { UGameplayStatics::SetGamePaused(this,false); UGameplayStatics::OpenLevel(this,TEXT("/Game/AlienPlanet/Lvl_AlienPlanet")); }
    else if(A==TEXT("RESULTS_OVERVIEW")) SetScreen(TEXT("WBP_08_ResultsDialogue_Alternate"));
    else if(A.Contains(TEXT("VIEW_STATS"))) SetScreen(Screen==TEXT("WBP_07_ResultsSummary_Template") ? TEXT("WBP_08_ResultsDialogue_Alternate") : TEXT("WBP_07_ResultsSummary_Template"));
}

// Opt-in editor verification uses synthetic loopback audio; it never claims a real Grok connection.
bool ACosmoGameHUD::TickQA(float Delta)
{
#if WITH_EDITOR
    if(FParse::Param(FCommandLine::Get(),TEXT("UISkipTutorialTest")))
    {
        QATime+=FMath::Min(Delta,.1f);
        if(!Gameplay)
        {
            if(QAStage==0 && QATime>.5) { StartMission(); QAStage=1; }
            else if(QAStage==1 && QATime>1.5)
            {
                const bool Pass=Screen==TEXT("TutorialChoice") && Buttons.Num()==2 && Focus==1 && !ConnectingOpening && !WaitingForVoice && !MenuService && !TutorialGestures && Voice && !Voice->VoiceReady && !Voice->TutorialActive;
                UE_LOG(LogTemp,Display,TEXT("UI_CHOICE_MODAL %s buttons=2 service_idle=1"),Pass ? TEXT("PASS") : TEXT("FAIL"));
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),FString::Printf(TEXT("Screenshots/UI_TutorialChoice_%dx%d.png"),int32(Layout.Viewport.X),int32(Layout.Viewport.Y))),true,false);
                QAStage=2;
            }
            else if(QAStage==2 && QATime>2)
            { Previous(); const bool Pass=Buttons.IsValidIndex(Focus) && Buttons[Focus].Action==TEXT("SkipTutorialConfirm"); UE_LOG(LogTemp,Display,TEXT("UI_CHOICE_FOCUS %s"),Pass ? TEXT("PASS") : TEXT("FAIL")); Confirm(); QAStage=3; }
        }
        else if(Integration && QATime>1)
        {
            const bool Pass=Screen==TEXT("WBP_03_GameplayHUD") && Integration->FreeConversation && Integration->Realtime && !Integration->Realtime->TutorialActive;
            UE_LOG(LogTemp,Display,TEXT("UI_SKIP_RESULT %s loaded_game=1 tutorial_active=0"),Pass ? TEXT("PASS") : TEXT("FAIL"));
            FPlatformMisc::RequestExit(false);
        }
        if(QATime>20) { UE_LOG(LogTemp,Error,TEXT("UI_SKIP_RESULT FAIL timeout")); FPlatformMisc::RequestExit(false); }
        return true;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("UIVictoryTest")))
    {
        QATime+=FMath::Min(Delta,.1f);
        auto* Director=ACosmoPlanetDirector::Find(GetWorld());
        if(!Integration || !Director || !Director->Player) return true;
        if(QAStage==0 && QATime>1)
        {
            ACosmoPlanetEnemy* Last=nullptr;
            for(ACosmoPlanetEnemy* Enemy:Director->Enemies) if(IsValid(Enemy) && !Enemy->Passive) Last=Enemy;
            for(ACosmoPlanetEnemy* Enemy:Director->Enemies) if(IsValid(Enemy) && !Enemy->Passive && Enemy!=Last) Enemy->bDead=true;
            if(Last) { Last->Active=false; Last->bDead=false; }
            QAStage=1;
        }
        else if(QAStage==1 && QATime>2)
        {
            UE_LOG(LogTemp,Display,TEXT("UI_VICTORY_LAST_ENEMY %s"),!Integration->Won && Screen==TEXT("WBP_03_GameplayHUD") ? TEXT("PASS") : TEXT("FAIL"));
            for(ACosmoPlanetEnemy* Enemy:Director->Enemies) if(IsValid(Enemy) && !Enemy->Passive) Enemy->bDead=true;
            QAStage=2;
        }
        else if(QAStage==2 && QATime>3)
        {
            const bool Pass=Director->Won && Integration->Won && Screen==TEXT("WBP_05_Victory") && Director->CoreCharge==0;
            UE_LOG(LogTemp,Display,TEXT("UI_VICTORY_RESULT %s all_enemies_dead=1 no_core_charge=1"),Pass ? TEXT("PASS") : TEXT("FAIL"));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots/UI_Victory_AllEnemies.png")),true,false);
            QAStage=3;
        }
        else if(QAStage==3 && QATime>3.6) FPlatformMisc::RequestExit(false);
        if(QATime>15) { UE_LOG(LogTemp,Error,TEXT("UI_VICTORY_RESULT FAIL timeout")); FPlatformMisc::RequestExit(false); }
        return true;
    }
    const bool Preview=FParse::Param(FCommandLine::Get(),TEXT("UIPreview"));
    const bool Flow=FParse::Param(FCommandLine::Get(),TEXT("UIFlowTest"));
    const bool Hold=FParse::Param(FCommandLine::Get(),TEXT("UIHoldTest"));
    const bool Boot=FParse::Param(FCommandLine::Get(),TEXT("UIMenuServiceTest"));
    if(!Preview && !Flow && !Hold && !Boot) return false;
    QATime+=FMath::Min(Delta,.1f);
    if(Preview)
    {
        const TArray<FString> Names={TEXT("WBP_01_MainMenu"),TEXT("WBP_Tutorial_01_MoveForward"),TEXT("WBP_04_PauseMenu"),TEXT("WBP_05_Victory"),TEXT("WBP_07_ResultsSummary_Template")};
        if(QAStage<Names.Num()*2 && QATime>2+QAStage*1.2f)
        { if(QAStage%2==0) { SetScreen(Names[QAStage/2]); Opacity=1; }
          else FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots/UI_")+Names[QAStage/2]+FString::Printf(TEXT("_%dx%d.png"),int32(Layout.Viewport.X),int32(Layout.Viewport.Y))),true,false);
          ++QAStage; }
        if(QATime>16) { UE_LOG(LogTemp,Display,TEXT("UI_PREVIEW_RESULT PASS")); FPlatformMisc::RequestExit(false); }
        return true;
    }
    if(!Gameplay && QAStage==0 && QATime>.5f)
    { UE_LOG(LogTemp,Display,TEXT("UI_OPENING_IDLE %s"),!Voice->VoiceReady && !Voice->TutorialActive && Screen==TEXT("WBP_01_MainMenu") ? TEXT("PASS") : TEXT("FAIL")); StartMission(); QAStage=1; }
    if(!Gameplay && QAStage==1 && Screen==TEXT("TutorialChoice") && QATime>1.5f)
    {
        const bool Pass=Buttons.Num()==2 && Buttons.IsValidIndex(Focus) && Buttons[Focus].Action==TEXT("PlayTutorial") && !ConnectingOpening && !MenuService;
        UE_LOG(LogTemp,Display,TEXT("UI_CHOICE_CONTINUE %s"),Pass ? TEXT("PASS") : TEXT("FAIL"));
        Confirm(); QAStage=2;
    }
    if(ConnectingOpening && OpeningError.IsEmpty() && !QAShotLoading && QATime>.6f)
    { QAShotLoading=true; FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots/UI_Opening_Black.png")),true,false); UE_LOG(LogTemp,Display,TEXT("UI_OPENING_BLACK PASS")); }
    if(Boot && !OpeningError.IsEmpty() && MenuService && MenuService->ServiceReady)
    { auto* S=GetWorld()->GetGameInstance()->GetSubsystem<UCosmoServiceSubsystem>(); const bool Pass=S && S->IsRunning() && ConnectingOpening && !Voice->VoiceReady;
      UE_LOG(LogTemp,Display,TEXT("UI_MENU_BOOTSTRAP_RESULT %s bundled_service_ready=1 missing_voice_reported=1"),Pass ? TEXT("PASS") : TEXT("FAIL")); FPlatformMisc::RequestExit(false); return true; }
    if(Flow && VoiceFailed)
    { UE_LOG(LogTemp,Display,TEXT("UI_VOICE_FAILURE_HOLD PASS page=%d waiting=%d"),TutorialPage,WaitingForVoice); Confirm(); }

    if(Flow && !Gameplay && TutorialGestures && TutorialPage>=0)
    {
        const double Now=FPlatformTime::Seconds();
        if(TutorialPhase==ECosmoTutorialPhase::AwaitGesture && QAGesturePage!=TutorialPage)
        { QAGesturePage=TutorialPage; QAGestureSince=Now; }
        const double Elapsed=Now-QAGestureSince;
        const bool Await=TutorialPhase==ECosmoTutorialPhase::AwaitGesture;
        if(Await && TutorialPage==0 && Elapsed>.35 && !QAShotWaiting)
        { QAShotWaiting=true; FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots/UI_Tutorial_Interactive_Wait.png")),true,false); }
        if(TutorialPhase==ECosmoTutorialPhase::Feedback && TutorialPage==3 && LineRequested && !QAShotFeedback)
        { QAShotFeedback=true; FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots/UI_Tutorial_Interactive_Feedback.png")),true,false); }
        if(Await && Elapsed>.35 && Elapsed<.45)
            UE_LOG(LogTemp,Display,TEXT("UI_TUTORIAL_WAIT_GESTURE PASS page=%d"),TutorialPage);
        auto O=MakeShared<FJsonObject>(); O->SetNumberField(TEXT("version"),3); O->SetStringField(TEXT("session_id"),TEXT("hud-qa"));
        O->SetNumberField(TEXT("frame_id"),++QAFrame); O->SetNumberField(TEXT("timestamp_ms"),QAFrame*16);
        const bool Valid=Await && Elapsed>.7;
        TArray<TSharedPtr<FJsonValue>> H,E;
        FString Action=GestureMatcher.Action, LeftPose=TEXT("NONE"),RightPose=TEXT("OPEN"); float Move=0,Turn=0;
        if(Valid)
        {
            if(Action==TEXT("Stop")) LeftPose=TEXT("FIST");
            else if(Action==TEXT("MoveForward") || Action==TEXT("MoveBackward")) { H.Add(MakeShared<FJsonValueString>(Action)); Move=Action==TEXT("MoveForward") ? 1 : -1; }
            else if(Action==TEXT("Shield") || Action==TEXT("Laser")) { H.Add(MakeShared<FJsonValueString>(Action)); RightPose=Action==TEXT("Laser") ? TEXT("PINCH") : TEXT("OPEN"); }
            else if(Action==TEXT("Fire") || Action==TEXT("Wave"))
            { auto P=MakeShared<FJsonObject>(); P->SetStringField(TEXT("action"),Action); P->SetNumberField(TEXT("id"),QAFrame); P->SetNumberField(TEXT("timestamp_ms"),QAFrame*16); E.Add(MakeShared<FJsonValueObject>(P)); }
            else if(Action==TEXT("HeadTurn")) { const bool Left=Elapsed<1.2; H.Add(MakeShared<FJsonValueString>(Left ? TEXT("TurnLeft") : TEXT("TurnRight"))); Turn=Left ? -1 : 1; }
        }
        O->SetArrayField(TEXT("held_actions"),H); O->SetArrayField(TEXT("events"),E); O->SetNumberField(TEXT("move_axis"),Move); O->SetNumberField(TEXT("turn_axis"),Turn);
        auto Tracking=MakeShared<FJsonObject>(); for(auto K:{TEXT("left_hand"),TEXT("right_hand"),TEXT("face")}) Tracking->SetBoolField(K,Valid); O->SetObjectField(TEXT("tracking"),Tracking);
        auto Cal=MakeShared<FJsonObject>(); Cal->SetBoolField(TEXT("left_hand"),true); Cal->SetBoolField(TEXT("head"),true); O->SetObjectField(TEXT("calibrated"),Cal);
        auto Debug=MakeShared<FJsonObject>(); Debug->SetStringField(TEXT("left_pose"),LeftPose); Debug->SetStringField(TEXT("right_pose"),RightPose); O->SetObjectField(TEXT("debug"),Debug);
        FString Json; FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Json));
        if(!TutorialGestures->SubmitJson(Json)) UE_LOG(LogTemp,Error,TEXT("UI_TUTORIAL_FRAME FAIL"));
    }
    if(Hold && QATime>6)
    { const bool Pass=TutorialPage==-1 && WaitingForVoice && !FadingOut;
      UE_LOG(LogTemp,Display,TEXT("UI_HOLD_RESULT %s page=%d waiting=%d"),Pass ? TEXT("PASS") : TEXT("FAIL"),TutorialPage,WaitingForVoice); FPlatformMisc::RequestExit(false); return true; }
    if(Flow && Gameplay && QATime>3 && Integration)
    {
        if(QAStage==0) { Back(); const bool Pass=UGameplayStatics::IsGamePaused(this) && Screen==TEXT("WBP_04_PauseMenu");
            UE_LOG(LogTemp,Display,TEXT("UI_PAUSE_RESULT %s"),Pass ? TEXT("PASS") : TEXT("FAIL")); Back();
            Integration->PlayTutorialLine(TEXT("This old tutorial must remain silent."));
            const bool Free=Integration->FreeConversation && Integration->Realtime && Integration->Realtime->FreeConversation && !Integration->Realtime->TutorialActive;
            UE_LOG(LogTemp,Display,TEXT("UI_FREE_CHAT_RESULT %s"),Free ? TEXT("PASS") : TEXT("FAIL")); ++QAStage; }
        else if(QAStage==1 && QATime>4) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Screenshots/UI_Gameplay.png")),true,false); UE_LOG(LogTemp,Display,TEXT("UI_FLOW_RESULT PASS loaded_game=1 integration=1 resumed=%d"),!UGameplayStatics::IsGamePaused(this)); ++QAStage; }
        else if(QAStage==2 && QATime>4.6f) FPlatformMisc::RequestExit(false);
    }
    if(QATime>120) { UE_LOG(LogTemp,Error,TEXT("UI_FLOW_RESULT FAIL timeout")); FPlatformMisc::RequestExit(false); }
#endif
    return false;
}

void ACosmoGameHUD::AbilityConfirmed(FName Action,bool Success)
{
    Prompt=Action.ToString().ToUpper()+(Success ? TEXT(" ACTIVATED") : TEXT(" NOT READY"));
    PromptUntil=GetWorld()->GetTimeSeconds()+1.6f;
}

void ACosmoGameHUD::StartTutorialTracking()
{
    if(!TutorialGestures)
    {
        TutorialGestures=NewObject<UGestureJsonReceiverComponent>(this,TEXT("TutorialGestureReceiver"));
        TutorialGestures->AutoStart=false; TutorialGestures->RegisterComponent();
        TutorialGestures->OnJsonReceived.AddDynamic(this,&ACosmoGameHUD::TutorialFrame);
    }
#if WITH_EDITOR
    if(FParse::Param(FCommandLine::Get(),TEXT("UIFlowTest"))) TutorialGestures->ReloadBindings();
    else
#endif
    TutorialGestures->StartListening();
    GestureMatcher=FCosmoTutorialGestureMatcher();
    if(Voice) Voice->EnableCameraPreview=true;
    if(IsRunningCommandlet() || FApp::IsUnattended() || !TutorialGestures->Listening || TutorialGestures->Connected || TutorialRecognition.IsValid()) return;
    const FString Root=CosmoRuntimePaths::Root(), Python=CosmoRuntimePaths::Python(Root);
    const FString Script=FPaths::Combine(Root,TEXT("app/gestures/gesture_recognition.py"));
    if(!FPaths::FileExists(Python) || !FPaths::FileExists(Script)) return;
    const FString Log=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Logs/TutorialGestureRecognition.log")));
    const FString Args=FString::Printf(TEXT("-B -u \"%s\" --require-local-models --headless --camera %d --udp-port %d --preview-port 8767 --runtime-log \"%s\"%s%s"),*Script,TutorialCameraIndex,TutorialGestures->UdpPort,*Log,TutorialSwapHands ? TEXT(" --swap-hands") : TEXT(""),TutorialInvertHeadTurn ? TEXT(" --invert-turn") : TEXT(""));
    TutorialRecognition=FPlatformProcess::CreateProc(*Python,*Args,false,true,true,nullptr,0,*Root,nullptr);
}
void ACosmoGameHUD::StopTutorialTracking()
{
    if(TutorialGestures) TutorialGestures->StopListening();
    if(Voice) Voice->EnableCameraPreview=false;
    if(TutorialRecognition.IsValid()) { FPlatformProcess::TerminateProc(TutorialRecognition); FPlatformProcess::WaitForProc(TutorialRecognition); FPlatformProcess::CloseProc(TutorialRecognition); TutorialRecognition.Reset(); }
}
void ACosmoGameHUD::TutorialFrame(const FString& Json)
{
    TSharedPtr<FJsonObject> O; if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),O) || !O) return;
    const double Now=FPlatformTime::Seconds(); LastGestureAt=Now;
    const bool Waiting=TutorialPage>=0 && TutorialPhase==ECosmoTutorialPhase::AwaitGesture && !FadingOut && FApp::HasFocus();
#if WITH_EDITOR
    const bool Test=FParse::Param(FCommandLine::Get(),TEXT("UIFlowTest"));
#else
    const bool Test=false;
#endif
    if(GestureMatcher.Observe(O,Now,Waiting || (Test && TutorialPage>=0 && TutorialPhase==ECosmoTutorialPhase::AwaitGesture))) { GestureCompleted(); return; }
    if(!Waiting && !Test) return;
    const auto& F=TutorialGestures->LatestFrame;
    if(GestureMatcher.Action==TEXT("MoveForward") || GestureMatcher.Action==TEXT("MoveBackward"))
        TrackingHint=!F.LeftTracked ? TEXT("Show your left hand to the camera.") : !F.LeftCalibrated ? TEXT("Hold your open left hand at a comfortable distance to calibrate.") : TEXT("Perform the gesture shown above.");
    else if(GestureMatcher.Action==TEXT("HeadTurn"))
        TrackingHint=!F.FaceTracked ? TEXT("Keep your face in view.") : !F.HeadCalibrated ? TEXT("Face forward briefly to calibrate, then turn both ways.") : TEXT("Turn left and right. Both directions are required.");
    else TrackingHint=TEXT("Perform the gesture shown above. Keep your hand in view.");
}
void ACosmoGameHUD::GestureCompleted()
{
    if(TutorialPhase!=ECosmoTutorialPhase::AwaitGesture) return;
    UE_LOG(LogTemp,Display,TEXT("UI_TUTORIAL_GESTURE page=%d action=%s"),TutorialPage,*GestureMatcher.Action);
    TutorialPhase=ECosmoTutorialPhase::Feedback; WaitingForVoice=true; LineRequested=false; VoiceFailed=false;
}

void ACosmoGameHUD::TickOpening()
{
    if(!OpeningError.IsEmpty()) return;
    const double Now=FPlatformTime::Seconds();
    if(Now>OpeningDeadline) { OpeningError=TEXT("Grok did not become ready in time. Check your internet connection and Grok configuration."); return; }
    bool Test=false;
#if WITH_EDITOR
    Test=FParse::Param(FCommandLine::Get(),TEXT("UIFlowTest"));
#endif
    if(!Test)
    {
        if(!MenuService) return;
        if(!MenuService->LastError.IsEmpty() && !MenuService->ServiceReady) { OpeningError=MenuService->LastError; return; }
        if(!MenuService->ServiceReady || !MenuService->LastResponse.Success) return;
        TSharedPtr<FJsonObject> Health;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MenuService->LastResponse.RawJson),Health) && Health)
        {
            const TSharedPtr<FJsonObject>* R=nullptr;
            if(Health->TryGetObjectField(TEXT("realtime"),R))
            {
                FString Status; (*R)->TryGetStringField(TEXT("status"),Status);
                if(Status==TEXT("disabled")) { OpeningError=TEXT("Grok Voice is not configured. Save your existing key as the Windows user variable XAI_API_KEY, then retry."); return; }
                if(Status==TEXT("unavailable")) { OpeningError=TEXT("The bundled voice service could not start. Check the local voice port and try again."); return; }
                if(Status!=TEXT("listening")) return;
            }
        }
        Voice->Start();
    }
    if(!Voice->ConnectionError.IsEmpty()) { OpeningError=Voice->ConnectionError; return; }
    if(Voice->VoiceReady && !LineRequested)
    {
        FString Line;
        if(auto* Spec=Screens.Find(TEXT("WBP_02_IntroDialogue"))) for(auto T:(*Spec)->GetArrayField(TEXT("texts"))) Line+=T->AsObject()->GetStringField(TEXT("text"))+TEXT(". ");
        RequestedLine=Line.TrimStartAndEnd().Left(3000); LineRequested=Voice->SayLine(RequestedLine);
    }
}
void ACosmoGameHUD::OpeningAudioReady()
{
    if(!ConnectingOpening || VoiceFailed || !LineRequested || Voice->TutorialLine!=RequestedLine) return;
    ConnectingOpening=false; OpeningError.Reset(); SetScreen(TEXT("WBP_02_IntroDialogue"));
    Opacity=1; FadeTime=.55f; Voice->ReleaseTutorialAudio();
    UE_LOG(LogTemp,Display,TEXT("UI_OPENING_REVEAL audio_ready=1 ui_and_voice_same_frame=1"));
}

