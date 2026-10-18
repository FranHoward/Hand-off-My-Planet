#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Consumes accepted recognizer frames only; missing hands are never successful gestures. */
struct FCosmoTutorialGestureMatcher
{
    FString Action, Session;
    int64 LastEvent=0;
    double HeldSince=-1, LastSample=-1;
    bool PinchConfirmed=false, ReleaseArmed=false, SawLeft=false, SawRight=false;
    FString HeldDirection;
    void Begin(const FString& NewAction)
    {
        Action=NewAction; HeldSince=LastSample=-1; SawLeft=SawRight=false; HeldDirection.Reset();
        ReleaseArmed=NewAction==TEXT("LaserRelease") && PinchConfirmed;
    }
    bool Observe(const TSharedPtr<FJsonObject>& O,double Now,bool Enabled)
    {
        FString NewSession; O->TryGetStringField(TEXT("session_id"),NewSession);
        if(NewSession!=Session) { Session=NewSession; LastEvent=0; HeldSince=LastSample=-1; ReleaseArmed=PinchConfirmed=SawLeft=SawRight=false; }
        const int64 PreviousEvent=LastEvent;
        bool Fire=false, Wave=false;
        const TArray<TSharedPtr<FJsonValue>>* Events=nullptr;
        if(O->TryGetArrayField(TEXT("events"),Events)) for(auto V:*Events)
        {
            auto E=V->AsObject(); if(!E) continue; double Id=0,Stamp=0,FrameStamp=0; FString Name;
            E->TryGetNumberField(TEXT("id"),Id); E->TryGetNumberField(TEXT("timestamp_ms"),Stamp); O->TryGetNumberField(TEXT("timestamp_ms"),FrameStamp);
            E->TryGetStringField(TEXT("action"),Name); LastEvent=FMath::Max(LastEvent,int64(Id));
            if(Id>PreviousEvent && FrameStamp>=Stamp && FrameStamp-Stamp<=500) { Fire|=Name==TEXT("Fire"); Wave|=Name==TEXT("Wave"); }
        }
        if(!Enabled) { HeldSince=LastSample=-1; return false; }
        if(LastSample>=0 && Now-LastSample>.25) { HeldSince=-1; SawLeft=SawRight=false; }
        LastSample=Now;
        bool Left=false,Right=false,Face=false,LeftCal=false,HeadCal=false;
        const TSharedPtr<FJsonObject>* T=nullptr;
        if(O->TryGetObjectField(TEXT("tracking"),T)) { (*T)->TryGetBoolField(TEXT("left_hand"),Left); (*T)->TryGetBoolField(TEXT("right_hand"),Right); (*T)->TryGetBoolField(TEXT("face"),Face); }
        if(O->TryGetObjectField(TEXT("calibrated"),T)) { (*T)->TryGetBoolField(TEXT("left_hand"),LeftCal); (*T)->TryGetBoolField(TEXT("head"),HeadCal); }
        FString LeftPose,RightPose;
        if(O->TryGetObjectField(TEXT("debug"),T)) { (*T)->TryGetStringField(TEXT("left_pose"),LeftPose); (*T)->TryGetStringField(TEXT("right_pose"),RightPose); }
        TSet<FString> Held; const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if(O->TryGetArrayField(TEXT("held_actions"),Values)) for(auto V:*Values) Held.Add(V->AsString());
        bool Match=false;
        if(Action==TEXT("Stop")) Match=Left && LeftPose==TEXT("FIST");
        else if(Action==TEXT("MoveForward") || Action==TEXT("MoveBackward")) Match=Left && LeftCal && Held.Contains(Action);
        else if(Action==TEXT("Shield")) Match=Right && Held.Contains(TEXT("Shield"));
        else if(Action==TEXT("Laser")) Match=Right && Held.Contains(TEXT("Laser"));
        else if(Action==TEXT("LaserRelease"))
        {
            if(Right && Held.Contains(TEXT("Laser"))) ReleaseArmed=true;
            Match=ReleaseArmed && Right && !Held.Contains(TEXT("Laser")) && !RightPose.IsEmpty() && RightPose!=TEXT("NONE") && RightPose!=TEXT("PINCH");
        }
        else if(Action==TEXT("Fire")) return Right && Fire;
        else if(Action==TEXT("Wave")) return Right && Wave;
        else if(Action==TEXT("HeadTurn"))
        {
            if(!Face || !HeadCal) { HeldSince=-1; SawLeft=SawRight=false; return false; }
            FString Direction=Held.Contains(TEXT("TurnLeft")) ? TEXT("Left") : Held.Contains(TEXT("TurnRight")) ? TEXT("Right") : TEXT("");
            if(Direction.IsEmpty()) { HeldSince=-1; HeldDirection.Reset(); return false; }
            if(Direction!=HeldDirection) { HeldDirection=Direction; HeldSince=Now; }
            if(Now-HeldSince>=.2) { SawLeft|=Direction==TEXT("Left"); SawRight|=Direction==TEXT("Right"); }
            return SawLeft && SawRight;
        }
        if(!Match) { HeldSince=-1; return false; }
        if(HeldSince<0) HeldSince=Now;
        if(Now-HeldSince<.45) return false;
        if(Action==TEXT("Laser")) PinchConfirmed=true;
        if(Action==TEXT("LaserRelease")) PinchConfirmed=false;
        return true;
    }
};
