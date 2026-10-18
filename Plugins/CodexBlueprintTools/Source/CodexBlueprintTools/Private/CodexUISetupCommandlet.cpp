#include "CodexUISetupCommandlet.h"
#include "CosmoGameHUD.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "FileHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

namespace UISetup
{
bool Save(UObject* A)
{ FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; return UPackage::SavePackage(A->GetOutermost(),A,*FPackageName::LongPackageNameToFilename(A->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args); }
UBlueprint* BP(UClass* Parent,const FString& Name)
{
    FString Path=TEXT("/Game/UI/HUD/")+Name; auto* A=LoadObject<UBlueprint>(nullptr,*Path);
    if(!A) { A=FKismetEditorUtilities::CreateBlueprint(Parent,CreatePackage(*Path),FName(*Name),BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass()); FAssetRegistryModule::AssetCreated(A); }
    FKismetEditorUtilities::CompileBlueprint(A); return A;
}
int32 VerifyAnchors()
{
    int32 Failures=0;
    auto Check=[&](bool Pass,const TCHAR* Name) { if(!Pass) { ++Failures; UE_LOG(LogTemp,Error,TEXT("UI_ANCHOR_TEST FAIL %s"),Name); } };
    for(FVector2D Resolution:{FVector2D(1920,1080),FVector2D(1280,720),FVector2D(1024,768),FVector2D(2560,1080)})
    {
        FCosmoUILayout L; L.SetViewport(Resolution);
        for(FString Name:{TEXT("TopLeft"),TEXT("Top"),TEXT("TopRight"),TEXT("Left"),TEXT("Center"),TEXT("Right"),TEXT("BottomLeft"),TEXT("Bottom"),TEXT("BottomRight")})
        { const auto A=L.Anchor(Name); Check(L.Position(FVector2D(1920,1080)*A,A).Equals(Resolution*A,.01),TEXT("anchor remains attached to viewport")); }
        auto TL=L.Bounds({55,60},{300,180},L.Anchor(TEXT("TopLeft")));
        Check(FMath::IsNearlyEqual(TL.Min.X,55*L.Scale,.01f) && FMath::IsNearlyEqual(TL.Min.Y,60*L.Scale,.01f),TEXT("top left safe inset"));
        auto TR=L.Bounds({1540,60},{320,180},L.Anchor(TEXT("TopRight")));
        Check(FMath::IsNearlyEqual(Resolution.X-TR.Max.X,60*L.Scale,.01f) && FMath::IsNearlyEqual(TR.Min.Y,60*L.Scale,.01f),TEXT("camera top right inset"));
        auto BL=L.Bounds({55,1000},{300,25},L.Anchor(TEXT("BottomLeft")));
        Check(FMath::IsNearlyEqual(BL.Min.X,55*L.Scale,.01f) && FMath::IsNearlyEqual(Resolution.Y-BL.Max.Y,55*L.Scale,.01f),TEXT("bottom left inset"));
        auto BR=L.Bounds({1650,1020},{200,25},L.Anchor(TEXT("BottomRight")));
        Check(FMath::IsNearlyEqual(Resolution.X-BR.Max.X,70*L.Scale,.01f) && FMath::IsNearlyEqual(Resolution.Y-BR.Max.Y,35*L.Scale,.01f),TEXT("bottom right inset"));
        auto Reticle=L.Bounds({906,486},{108,108},L.Anchor(TEXT("Center")));
        Check(Reticle.GetCenter().Equals(Resolution*.5,.01),TEXT("reticle stays at viewport center"));
        Check(FMath::IsNearlyEqual(TR.GetSize().X/TR.GetSize().Y,320./180.,.001),TEXT("images preserve aspect ratio"));
        auto Button=L.Bounds({629,500},{665,223},L.Anchor(TEXT("Bottom")));
        Check(Button.IsInside(Button.GetCenter()) && !Button.IsInside(Button.Min-FVector2D(1,1)),TEXT("button hit region matches anchored image"));
        const auto Left=L.Position({143,214},L.Anchor(TEXT("Left"))),Right=L.Position({728,491},L.Anchor(TEXT("Right")));
        Check(FMath::IsNearlyEqual(Left.X,143*L.Scale,.01f) && FMath::IsNearlyEqual(Resolution.X-Right.X,1192*L.Scale,.01f),TEXT("tutorial left and right anchors"));
    }
    FString Json; TSharedPtr<FJsonObject> Data;
    if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectDir(),TEXT("External/UI/Layout.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)) return 21;
    const TSet<FString> Valid={TEXT("TopLeft"),TEXT("Top"),TEXT("TopRight"),TEXT("Left"),TEXT("Center"),TEXT("Right"),TEXT("BottomLeft"),TEXT("Bottom"),TEXT("BottomRight"),TEXT("Stretch"),TEXT("Cover")};
    for(auto V:Data->GetArrayField(TEXT("screens"))) for(auto Field:{TEXT("assets"),TEXT("texts")}) for(auto E:V->AsObject()->GetArrayField(Field))
    { FString A; Check(E->AsObject()->TryGetStringField(TEXT("anchor"),A) && Valid.Contains(A),TEXT("every UI element declares its anchor")); }
    UE_LOG(LogTemp,Display,TEXT("UI_ANCHOR_RESULT %s resolutions=4 failures=%d"),Failures ? TEXT("FAIL") : TEXT("PASS"),Failures);
    return Failures ? 21 : 0;
}

int32 VerifyGestures()
{
    int32 Failures=0;
    auto Check=[&](bool Pass,const TCHAR* Name) { if(!Pass) { ++Failures; UE_LOG(LogTemp,Error,TEXT("UI_GESTURE_TEST FAIL %s"),Name); } };
    auto Frame=[](TArray<FString> Held,FString LP=TEXT("FIST"),FString RP=TEXT("OPEN"),bool Tracked=true,bool Calibrated=true,int32 Event=0,FString Pulse=TEXT(""),int32 Age=0)
    {
        auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("session_id"),TEXT("test")); O->SetNumberField(TEXT("timestamp_ms"),1000);
        TArray<TSharedPtr<FJsonValue>> H; for(auto& A:Held) H.Add(MakeShared<FJsonValueString>(A)); O->SetArrayField(TEXT("held_actions"),H);
        auto T=MakeShared<FJsonObject>(); for(auto K:{TEXT("left_hand"),TEXT("right_hand"),TEXT("face")}) T->SetBoolField(K,Tracked); O->SetObjectField(TEXT("tracking"),T);
        auto C=MakeShared<FJsonObject>(); C->SetBoolField(TEXT("left_hand"),Calibrated); C->SetBoolField(TEXT("head"),Calibrated); O->SetObjectField(TEXT("calibrated"),C);
        auto D=MakeShared<FJsonObject>(); D->SetStringField(TEXT("left_pose"),LP); D->SetStringField(TEXT("right_pose"),RP); O->SetObjectField(TEXT("debug"),D);
        TArray<TSharedPtr<FJsonValue>> E;
        if(Event) { auto P=MakeShared<FJsonObject>(); P->SetNumberField(TEXT("id"),Event); P->SetNumberField(TEXT("timestamp_ms"),1000-Age); P->SetStringField(TEXT("action"),Pulse); E.Add(MakeShared<FJsonValueObject>(P)); }
        O->SetArrayField(TEXT("events"),E); return O;
    };
    for(FString Action:{TEXT("Stop"),TEXT("MoveForward"),TEXT("MoveBackward"),TEXT("Shield"),TEXT("Laser")})
    {
        FCosmoTutorialGestureMatcher M; M.Begin(Action); TArray<FString> H; if(Action!=TEXT("Stop")) H.Add(Action);
        auto Good=Frame(H);
        Check(!M.Observe(Good,0,true),TEXT("hold cannot instantly pass"));
        Check(!M.Observe(Good,.2,true),TEXT("hold threshold"));
        Check(!M.Observe(Frame(H,TEXT("FIST"),TEXT("OPEN"),false),.3,true),TEXT("lost tracking is not success"));
        Check(!M.Observe(Good,.4,true),TEXT("hold restarts after loss"));
        M.Observe(Good,.6,true); M.Observe(Good,.8,true); Check(M.Observe(Good,.9,true),TEXT("stable action passes"));
    }
    FCosmoTutorialGestureMatcher Stop; Stop.Begin(TEXT("Stop"));
    Check(!Stop.Observe(Frame({},TEXT("NONE")),0,true) && !Stop.Observe(Frame({},TEXT("NONE")),1,true),TEXT("neutral is not left fist"));
    FCosmoTutorialGestureMatcher Move; Move.Begin(TEXT("MoveForward"));
    Check(!Move.Observe(Frame({TEXT("MoveForward")},TEXT("OPEN"),TEXT("OPEN"),true,false),0,true),TEXT("uncalibrated movement rejected"));
    FCosmoTutorialGestureMatcher Release; Release.Begin(TEXT("LaserRelease"));
    Check(!Release.Observe(Frame({}),0,true) && !Release.Observe(Frame({}),.6,true),TEXT("release must be armed by pinch"));
    Release.Observe(Frame({TEXT("Laser")},TEXT("FIST"),TEXT("PINCH")),.7,true);
    Release.Observe(Frame({},TEXT("FIST"),TEXT("OPEN"),false),.8,true);
    Check(!Release.Observe(Frame({},TEXT("FIST"),TEXT("OPEN"),false),1.4,true),TEXT("hand loss is not pinch release"));
    auto Open=Frame({}); Release.Observe(Open,1.5,true); Release.Observe(Open,1.7,true); Release.Observe(Open,1.9,true);
    Check(Release.Observe(Open,2,true),TEXT("tracked pinch release passes"));
    FCosmoTutorialGestureMatcher Head; Head.Begin(TEXT("HeadTurn"));
    Head.Observe(Frame({TEXT("TurnLeft")}),0,true); Check(!Head.Observe(Frame({TEXT("TurnLeft")}),.21,true),TEXT("head requires both directions"));
    Head.Observe(Frame({TEXT("TurnRight")}),.3,true); Check(Head.Observe(Frame({TEXT("TurnRight")}),.51,true),TEXT("both head directions pass"));
    for(FString Action:{TEXT("Fire"),TEXT("Wave")})
    {
        FCosmoTutorialGestureMatcher Pulse; Pulse.Begin(Action);
        auto Old=Frame({},TEXT("FIST"),TEXT("FIST"),true,true,1,Action);
        Pulse.Observe(Old,0,false); Check(!Pulse.Observe(Old,.1,true),TEXT("instruction-time pulse cannot replay"));
        Check(!Pulse.Observe(Frame({},TEXT("FIST"),TEXT("FIST"),true,true,2,Action,900),.2,true),TEXT("stale pulse rejected"));
        Check(!Pulse.Observe(Frame({},TEXT("FIST"),TEXT("FIST"),false,true,3,Action),.3,true),TEXT("pulse without tracked hand rejected"));
        Check(Pulse.Observe(Frame({},TEXT("FIST"),TEXT("FIST"),true,true,4,Action),.4,true),TEXT("fresh pulse passes"));
    }
    UE_LOG(LogTemp,Display,TEXT("UI_GESTURE_MATCHER_RESULT %s failures=%d"),Failures ? TEXT("FAIL") : TEXT("PASS"),Failures);
    return Failures ? 20 : 0;
}

UFont* Font(const FString& Name,const FString& File)
{
    FString Base=TEXT("/Game/UI/Fonts/")+Name;
    auto* Face=NewObject<UFontFace>(CreatePackage(*(Base+TEXT("_Face"))),FName(*(Name+TEXT("_Face"))),RF_Public|RF_Standalone);
    TArray<uint8> Bytes; if(!FFileHelper::LoadFileToArray(Bytes,*File)) return nullptr;
    Face->InitializeFromBulkData(File,EFontHinting::Default,Bytes.GetData(),Bytes.Num()); Face->LoadingPolicy=EFontLoadingPolicy::Inline;
    FAssetRegistryModule::AssetCreated(Face); if(!Save(Face)) return nullptr;
    auto* F=NewObject<UFont>(CreatePackage(*Base),FName(*Name),RF_Public|RF_Standalone); F->FontCacheType=EFontCacheType::Runtime;
    FTypefaceEntry Entry(FName(TEXT("Regular"))); Entry.Font=FFontData(Face); F->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(Entry);
    FAssetRegistryModule::AssetCreated(F); if(!Save(F)) return nullptr; return F;
}
}
UCodexUISetupCommandlet::UCodexUISetupCommandlet() { IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true; }
int32 UCodexUISetupCommandlet::Main(const FString& Params)
{
    using namespace UISetup;
    if(FParse::Param(*Params,TEXT("VerifyAnchors"))) return VerifyAnchors();
    if(FParse::Param(*Params,TEXT("VerifyGestures"))) return VerifyGestures();
    FString Json; const FString Root=FPaths::Combine(FPaths::ProjectDir(),TEXT("External/UI/Source/Hand_off_my_planet_UI_Assets"));
    if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectDir(),TEXT("External/UI/Layout.json")))) return 1;
    TSharedPtr<FJsonObject> Layout; if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Layout)) return 2;
    const FString DataPath=TEXT("/Game/UI/DA_PlanetUI");
    auto* Data=LoadObject<UCosmoUIData>(nullptr,*DataPath);
    if(!Data) { Data=NewObject<UCosmoUIData>(CreatePackage(*DataPath),TEXT("DA_PlanetUI"),RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Data); }
    Data->LayoutJson=Json;
    TSet<FString> Files;
    int32 Tutorials=0;
    for(auto S:Layout->GetArrayField(TEXT("screens")))
    { if(S->AsObject()->GetStringField(TEXT("screen")).StartsWith(TEXT("WBP_Tutorial_"))) ++Tutorials;
      for(auto A:S->AsObject()->GetArrayField(TEXT("assets"))) Files.Add(A->AsObject()->GetStringField(TEXT("file"))); }
    for(auto& File:Files)
    {
        FString Name=FPaths::GetBaseFilename(File); for(auto& Ch:Name) if(!FChar::IsAlnum(Ch) && Ch!='_') Ch='_';
        const FString Path=TEXT("/Game/UI/Textures/T_")+Name;
        auto* T=LoadObject<UTexture2D>(nullptr,*Path);
        if(!T) { auto* Factory=NewObject<UTextureFactory>(); Factory->SuppressImportOverwriteDialog(); bool Cancel=false;
            T=Cast<UTexture2D>(UFactory::StaticImportObject(UTexture2D::StaticClass(),CreatePackage(*Path),FName(*(TEXT("T_")+Name)),RF_Public|RF_Standalone,Cancel,*FPaths::Combine(Root,File),nullptr,Factory,nullptr,GWarn)); }
        if(!T) { UE_LOG(LogTemp,Error,TEXT("UI_IMPORT_FAILED %s"),*File); return 3; }
        if(T->AssetImportData) T->AssetImportData->UpdateFilenameOnly(FPaths::ConvertRelativePathToFull(FPaths::Combine(Root,File)));
        T->LODGroup=TEXTUREGROUP_UI; T->CompressionSettings=TC_EditorIcon; T->MipGenSettings=TMGS_NoMipmaps; T->SRGB=true;
        T->PostEditChange(); FAssetRegistryModule::AssetCreated(T); if(!Save(T)) return 4; Data->Textures.Add(File,T);
    }
    Data->Orbitron=Font(TEXT("F_Orbitron"),FPaths::Combine(Root,TEXT("05_Typography/Fonts/Orbitron-SemiBold.ttf")));
    Data->Mono=Font(TEXT("F_PlexMono"),FPaths::Combine(Root,TEXT("05_Typography/Fonts/IBMPlexMono-Regular.ttf")));
    if(!Data->Orbitron || !Data->Mono || !Save(Data)) return 5;
    auto* HUD=BP(ACosmoGameHUD::StaticClass(),TEXT("BP_PlanetHUD"));
    CastChecked<ACosmoGameHUD>(HUD->GeneratedClass->GetDefaultObject())->UIData=Data; if(!Save(HUD)) return 6;
    auto* Mode=BP(ACosmoMenuGameMode::StaticClass(),TEXT("BP_MainMenuGameMode"));
    CastChecked<ACosmoMenuGameMode>(Mode->GeneratedClass->GetDefaultObject())->HUDClass=HUD->GeneratedClass; if(!Save(Mode)) return 7;
    const FString MapPath=TEXT("/Game/UI/Maps/Lvl_MainMenu");
    auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false); if(!World) return 8;
    World->GetWorldSettings()->DefaultGameMode=Mode->GeneratedClass;
    if(!UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)) return 9;
    if(Tutorials!=9 || Data->Textures.Num()!=Files.Num()) return 10;
    UE_LOG(LogTemp,Display,TEXT("UI_SETUP_RESULT PASS screens=%d tutorials=%d textures=%d HUD=/Game/UI/HUD/BP_PlanetHUD map=%s"),Layout->GetArrayField(TEXT("screens")).Num(),Tutorials,Data->Textures.Num(),*MapPath);
    return 0;
}
