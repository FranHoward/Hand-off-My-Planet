#pragma once
#include "CoreMinimal.h"

/** Native HUD equivalent of normalized canvas anchors, with uniform DPI scaling. */
struct FCosmoUILayout
{
    FVector2D Viewport=FVector2D(1920,1080);
    float Scale=1;
    void SetViewport(FVector2D Size) { Viewport=Size; Scale=FMath::Min(Size.X/1920.f,Size.Y/1080.f); }
    static FVector2D Anchor(const FString& Name)
    {
        if(Name==TEXT("TopLeft")) return {0,0};
        if(Name==TEXT("Top")) return {.5,0};
        if(Name==TEXT("TopRight")) return {1,0};
        if(Name==TEXT("Left")) return {0,.5};
        if(Name==TEXT("Right")) return {1,.5};
        if(Name==TEXT("BottomLeft")) return {0,1};
        if(Name==TEXT("Bottom")) return {.5,1};
        if(Name==TEXT("BottomRight")) return {1,1};
        return {.5,.5};
    }
    FVector2D Position(FVector2D P,FVector2D A) const
    { return P*Scale+(Viewport-FVector2D(1920,1080)*Scale)*A; }
    FBox2D Bounds(FVector2D P,FVector2D Size,FVector2D A) const
    { const auto Min=Position(P,A); return FBox2D(Min,Min+Size*Scale); }
};
