// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The cinematic letterbox: FUN_0041a0f0 (set target), FUN_0041a3d0 (forget camera),
// FUN_0041a3e0 / FUN_0041a400 / FUN_0041a410 (queries), FUN_0041a420 (per-frame update for a
// space camera) and FUN_0041a140 (its twin for the room camera).

#include "Space/space_letterbox.h"

#include "Common/Graphics/fade_to_black.h"
#include "Core/globals.h"
#include "GameFlow/room_camera.h"
#include "Space/Cameras/space_camera.h"
#include "Core/widescreen.h"   // port-only Hor+ (FREELANCER_WIDESCREEN_SUPPORT)

#include "common_text.h"

#include <math.h>

namespace
{
    // All zero in the image except the speed (0x00610690), which holds 3.0f -- the same speed
    // every retail caller passes.
    float       G_LetterboxAmount = 0.0f;
    float       G_LetterboxTarget = 0.0f;
    float       G_LetterboxSpeed = 3.0f;
    bool        G_bLetterboxAnimating = false;
    // DAT_006166b8 -- ONE slot shared by both updates: the space camera (FUN_0041a420) and the
    // room camera (FUN_0041a140) store and compare the same dword, so switching between the two
    // kinds of camera also counts as a camera change.
    void const* G_LetterboxLastCamera = nullptr;

    // .rdata 0x005c9020. The cinema aspect the bars converge on.
    const float kCinemaAspect = 1.85f;

    // .rdata 0x005c901c (float) and 0x005c8928 (double): pi/360 and 180/pi, the two constants
    // FUN_0041a140 turns the half-angle into a tangent and back with.
    const float kPiOver360 = 0.008726646192371845f;
    const double kRadToDeg = 57.29577791868205;

    // DAT_00610850 / DAT_00610854, read with FILD dword (signed) -- the surface size
    // ApplyGameWindowLayout (FUN_00424870) records, not the requested display mode.
    void GetSurfaceSize(int* OutWidth, int* OutHeight)
    {
        *OutWidth = static_cast<int>(G_RenderSurfaceWidth);
        *OutHeight = static_cast<int>(G_RenderSurfaceHeight);
    }

    // The head both updates share, inlined identically in FUN_0041a420 and FUN_0041a140.
    // Returns true whenever the amount was animating on entry, whether or not it has now
    // arrived. Arriving is NOT a return -- every path falls through to the camera part, which
    // is what re-applies the viewport on every tick of the slide.
    //
    // x87: the sum/difference stays on the FPU after the store, so the arrival test compares
    // the UNROUNDED value with the target.
    bool StepLetterboxAmount(float DeltaSeconds)
    {
        if (!G_bLetterboxAnimating)
            return false;

        if (G_LetterboxAmount < G_LetterboxTarget)
        {
            const double Sum = static_cast<double>(G_LetterboxSpeed) * DeltaSeconds +
                               static_cast<double>(G_LetterboxAmount);
            G_LetterboxAmount = static_cast<float>(Sum);
            // Still less, or unordered, keeps the animation going.
            if (!(Sum >= static_cast<double>(G_LetterboxTarget)))
                return true;
            G_LetterboxAmount = G_LetterboxTarget;
        }
        else if (G_LetterboxAmount > G_LetterboxTarget)
        {
            const double Difference = static_cast<double>(G_LetterboxAmount) -
                                      static_cast<double>(G_LetterboxSpeed) * DeltaSeconds;
            G_LetterboxAmount = static_cast<float>(Difference);
            // Still greater, or unordered, keeps the animation going.
            if (!(Difference <= static_cast<double>(G_LetterboxTarget)))
                return true;
            G_LetterboxAmount = G_LetterboxTarget;
        }
        // else: equal, or unordered -- the flag is simply dropped.

        G_bLetterboxAnimating = false;
        return true;
    }
}

void SetLetterboxTarget(float Target, float Speed)
{
    // FUN_0041a0f0. The fade test comes FIRST and assigns the TARGET to the amount, not the
    // other way round -- so a covered screen gets the finished bars immediately.
    if (IsFadeOpaque())
        G_LetterboxAmount = Target;

    G_LetterboxTarget = Target;
    G_LetterboxSpeed = Speed;
    G_bLetterboxAnimating = true;

    // The camera is forgotten, so the very next update reapplies the viewport even when the
    // amount landed on its target above and nothing is left to animate.
    G_LetterboxLastCamera = nullptr;
}

void ForgetLetterboxCamera()
{
    G_LetterboxLastCamera = nullptr;                // FUN_0041a3d0
}

bool IsLetterboxVisible()
{
    // FUN_0041a3e0: strictly positive.
    return G_LetterboxAmount > 0.0f;
}

float GetLetterboxAmount()
{
    return G_LetterboxAmount;                       // FUN_0041a400
}

float GetLetterboxTarget()
{
    return G_LetterboxTarget;                       // FUN_0041a410
}

int GetLetterboxBarHeight(float Amount)
{
    int Width = 0;
    int Height = 0;
    GetSurfaceSize(&Width, &Height);

    // Every intermediate stays on the x87 stack, so nothing is rounded to float on the way
    // (0.5 is the double 0x005c75e8).
    const double SurfaceAspect = static_cast<double>(Width) / static_cast<double>(Height);
    const double Divisor =
        (static_cast<double>(kCinemaAspect) - SurfaceAspect) * Amount + SurfaceAspect;
    const double Bars =
        (static_cast<double>(Height) - static_cast<double>(Width) / Divisor) * 0.5 + 0.5;

    // floor then ftol, so the +0.5 above makes it round-half-up rather than a truncation.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only: retail never sees a surface wider than 1.85, where the formula goes negative
    // and the "bars" would grow the viewport past the surface. Such a surface gets none.
    const int BarHeight = static_cast<int>(floor(Bars));
    return BarHeight > 0 ? BarHeight : 0;
#else
    return static_cast<int>(floor(Bars));
#endif
}

void UpdateLetterbox(FSpaceCamera* Cam, float DeltaSeconds)
{
    // FUN_0041a420. Merely BEING animated counts as a change, so the bars are applied on every
    // tick of the slide.
    const bool bStepped = StepLetterboxAmount(DeltaSeconds);

    // A null camera is not an error -- the amount has been advanced and that is all this call
    // can do.
    if (Cam == nullptr)
        return;

    // The camera is compared and stored before the change test.
    const bool bCameraChanged = (G_LetterboxLastCamera != Cam);
    G_LetterboxLastCamera = Cam;
    if (!bStepped && !bCameraChanged)
        return;

    int Width = 0;
    int Height = 0;
    GetSurfaceSize(&Width, &Height);

    const int Bars = GetLetterboxBarHeight(G_LetterboxAmount);

    // Built as extents and converted to inclusive edges before it reaches the camera.
    Rect Bounds;
    Bounds.Left = 0;
    Bounds.Top = Bars;
    Bounds.Width = Width;
    Bounds.Height = Height - Bars * 2;

    const ViewRect View = Rect_to_ViewRect(Bounds);

    // The flag argument is dead.
    Cam->SetViewportEx(&View.Left, 0);
}

void UpdateRoomLetterbox(FRoomCamera* Cam, float DeltaSeconds)
{
    // FUN_0041a140. The room camera has no two-argument viewport setter, so the viewport and
    // the projection fields are rebuilt inline.
    const bool bStepped = StepLetterboxAmount(DeltaSeconds);

    if (Cam == nullptr)
        return;

    const bool bCameraChanged = (G_LetterboxLastCamera != Cam);
    G_LetterboxLastCamera = Cam;
    if (!bStepped && !bCameraChanged)
        return;

    int Width = 0;
    int Height = 0;
    GetSurfaceSize(&Width, &Height);

    const int Bars = GetLetterboxBarHeight(G_LetterboxAmount);

    Rect Bounds;
    Bounds.Left = 0;
    Bounds.Top = Bars;
    Bounds.Width = Width;
    Bounds.Height = Height - Bars * 2;

    const ViewRect View = Rect_to_ViewRect(Bounds);

    // ICamera slot 6 -- read BEFORE the viewport is stored.
    const float HalfFovXDeg = Cam->GetHalfFovXDeg();

    Cam->Viewport[0] = View.Left;
    Cam->Viewport[1] = View.Top;
    Cam->Viewport[2] = View.Right;
    Cam->Viewport[3] = View.Bottom;

    // Half extents from the edges, NO +1; 0.5f is .rdata 0x005c75e0.
    Cam->HalfWidth = static_cast<float>(Cam->Viewport[2] - Cam->Viewport[0]) * 0.5f;
    Cam->HalfHeight = static_cast<float>(Cam->Viewport[3] - Cam->Viewport[1]) * 0.5f;

    // Both scales against the tangents the camera had BEFORE this call. Both are overwritten
    // below and nothing reads them in between; retail still makes the stores.
    Cam->XScale = Cam->HalfWidth / Cam->TanHalfFovX;
    Cam->YScale = -(Cam->HalfHeight / Cam->TanHalfFovY);

    // The X side plane is normalised through FUN_0041a390.
    const double TanX =
        tan((static_cast<double>(HalfFovXDeg) + static_cast<double>(HalfFovXDeg)) * kPiOver360);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+ (room_camera.cpp): +0x14 keeps the 4:3 angle; the full-width rectangle
    // widens +0x24 and the X plane, so the bars only crop height.
    const double TanXWide = TanX * WidescreenHorPlusScale(Bounds.Width);
#else
    const double TanXWide = TanX;
#endif
    Cam->TanHalfFovX = static_cast<float>(TanXWide);
    Cam->HalfFovXDeg = static_cast<float>(atan2(TanX, 1.0) * kRadToDeg);
    Cam->PlaneX[0] = 1.0f;
    Cam->PlaneX[1] = 0.0f;
    Cam->PlaneX[2] = static_cast<float>(TanXWide);
    RoomVec3Normalize(Cam->PlaneX);

    Cam->XScale = Cam->HalfWidth / Cam->TanHalfFovX;

    // The aspect is the RECTANGLE's, not the edges'; it stays unrounded for the division below.
    const double Aspect = static_cast<double>(Bounds.Width) / static_cast<double>(Bounds.Height);
    Cam->Aspect = static_cast<float>(Aspect);

    // tanY from the stored tanX.
    const double TanY = static_cast<double>(Cam->TanHalfFovX) / Aspect;
    Cam->TanHalfFovY = static_cast<float>(TanY);
    Cam->HalfFovYDeg = static_cast<float>(atan2(TanY, 1.0) * kRadToDeg);
    Cam->PlaneY[0] = 0.0f;
    Cam->PlaneY[1] = 1.0f;
    Cam->PlaneY[2] = static_cast<float>(TanY);
    RoomVec3Normalize(Cam->PlaneY);

    Cam->YScale = -(Cam->HalfHeight / Cam->TanHalfFovY);
}
