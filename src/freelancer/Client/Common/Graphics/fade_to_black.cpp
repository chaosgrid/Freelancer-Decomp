// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FadeToBlack (retail `E:\FL\Scratch\Source\Client\cmn\FadeToBlack.cpp`):
// FUN_00412010 .. FUN_004122a0 setters and queries, FUN_00412360 the tick.

#include "Common/Graphics/fade_to_black.h"

#include "Core/globals.h"
#include "Space/Cameras/space_camera.h"
#include "Interface/win3d_immediate.h"

namespace
{
    // .data:006105a8 = 1.0f. The client boots with the screen covered.
    float G_FadeLevel = 1.0f;

    // .data:006105ac = 1. With the level at 1.0 and the target at 0 the client boots already
    // animating, so the first ticks fade the covered screen in on their own. 0x00616510 /
    // 0x00616514 are zero.
    bool  G_bFadeAnimating = true;
    float G_FadeTarget = 0.0f;
    // 0x00616508: static initializer LAB_00411fa0 (right after LAB_00411f90) stores 1.0 /
    // duration.
    float G_FadeSpeed = 1.0f;
    // 0x0061650c: static initializer LAB_00411f90 copies .rdata 0x005c8b64 = 1.0f into it.
    float G_FadeDuration = 1.0f;
    bool  G_bFadeSkipStep = false;

    // 0x006164f0 the live colour, 0x006164fc the default. Nothing in the exe ever writes the
    // default, so it stays the black it is in .data -- but the setters still copy it every time,
    // which is what clears a colour a previous FadeInColoured left behind.
    float G_FadeColour[3] = {0.0f, 0.0f, 0.0f};
    const float G_FadeDefaultColour[3] = {0.0f, 0.0f, 0.0f};

    // The per-tick clamp is compared as the double 0.1 (.rdata 0x005c7720) and stored as the
    // float 0.1f.
    const double kMaxFadeStepCompare = 0.1;
    const float kMaxFadeStepSeconds = 0.1f;

    // IsFadeOpaque's threshold is the double 0.999 (.rdata 0x005c8b68); DrawFade's blend test
    // compares against the float 0.999f (0x005c8b70).
    const double kFadeOpaqueCompare = 0.999;
    const float kFadeOpaqueThreshold = 0.999f;

    // The source file and line retail hands the viewport call.
    const char kFadeSource[] = "E:\\FL\\Scratch\\Source\\Client\\cmn\\FadeToBlack.cpp";
    const int kFadeDrawLine = 0xb4;

    void ResetColourToDefault()
    {
        G_FadeColour[0] = G_FadeDefaultColour[0];
        G_FadeColour[1] = G_FadeDefaultColour[1];
        G_FadeColour[2] = G_FadeDefaultColour[2];
    }

    // Both coloured fades read the three floats with no null test.
    void CopyColour(const float* Colour3)
    {
        G_FadeColour[0] = Colour3[0];
        G_FadeColour[1] = Colour3[1];
        G_FadeColour[2] = Colour3[2];
    }
}

bool IsFadeOpaque()
{
    // FUN_00412010: unordered answers false.
    return static_cast<double>(G_FadeLevel) >= kFadeOpaqueCompare;
}

float GetFadeLevel()
{
    return G_FadeLevel;                             // FUN_00412030
}

float GetFadeTarget()
{
    return G_FadeTarget;                            // FUN_00412040
}

float GetFadeDuration()
{
    return G_FadeDuration;
}

const float* GetFadeColour()
{
    return G_FadeColour;
}

void SetFadeTarget(float Target, float Speed)
{
    // FUN_00412050. Note it does NOT touch the level or the colour -- it only redirects a fade
    // that is already in whatever state it is in.
    G_FadeTarget = Target;
    G_bFadeAnimating = true;
    G_FadeSpeed = Speed;
    G_bFadeSkipStep = true;
}

void SetFadeImmediate(float Level)
{
    // FUN_00412070. The hard cut: level and target both land on the argument and the animation
    // is switched off, so the next tick has nothing to do.
    G_FadeTarget = Level;
    G_FadeLevel = Level;
    G_FadeDuration = 1.0f;
    G_bFadeAnimating = false;
    ResetColourToDefault();
    G_FadeSpeed = 1.0f;
}

void FadeIn(bool bStartOpaque)
{
    // FUN_004120d0. The flag is the only conditional part; everything below it runs either way.
    if (bStartOpaque)
        G_FadeLevel = 1.0f;

    G_FadeDuration = 1.0f;
    ResetColourToDefault();
    G_FadeTarget = 0.0f;
    G_bFadeAnimating = true;
    G_bFadeSkipStep = true;
    G_FadeSpeed = 1.0f;
}

void FadeOut()
{
    // FUN_00412170. Unlike FadeIn this one always forces the starting level, because fading out
    // from a partly covered screen would shorten the fade.
    G_FadeDuration = 1.0f;
    ResetColourToDefault();
    G_FadeLevel = 0.0f;
    G_FadeTarget = 1.0f;
    G_bFadeAnimating = true;
    G_bFadeSkipStep = true;
    G_FadeSpeed = 1.0f;
}

void FadeInColoured(const float* Colour3, float Duration, bool bStartOpaque)
{
    // FUN_004121d0. The duration default is applied INSIDE the flag test, so a call with
    // bStartOpaque false and a non-positive duration keeps whatever duration was last used --
    // including a zero one, which then divides by zero below. Retail does not guard it.
    if (bStartOpaque)
    {
        G_FadeDuration = 1.0f;
        G_FadeLevel = 1.0f;
    }

    CopyColour(Colour3);

    if (Duration > 0.0f)
        G_FadeDuration = Duration;

    G_FadeTarget = 0.0f;
    G_bFadeAnimating = true;
    G_bFadeSkipStep = true;
    G_FadeSpeed = static_cast<float>(1.0 / static_cast<double>(G_FadeDuration));
}

void FadeOutColoured(const float* Colour3, float Duration)
{
    // FUN_004122a0. This one always seeds the duration, so it has no such hole.
    G_FadeDuration = 1.0f;
    G_FadeLevel = 0.0f;

    CopyColour(Colour3);

    if (Duration > 0.0f)
        G_FadeDuration = Duration;

    G_FadeTarget = 1.0f;
    G_bFadeAnimating = true;
    G_bFadeSkipStep = true;
    G_FadeSpeed = static_cast<float>(1.0 / static_cast<double>(G_FadeDuration));
}

void UpdateFade(float DeltaSeconds)
{
    // FUN_00412360.
    //
    // The frame a fade is started on advances it by nothing at all, which stops a long load
    // frame from swallowing the whole fade.
    if (G_bFadeSkipStep)
    {
        G_bFadeSkipStep = false;
        DeltaSeconds = 0.0f;
    }
    else if (static_cast<double>(DeltaSeconds) > kMaxFadeStepCompare)
    {
        DeltaSeconds = kMaxFadeStepSeconds;
    }

    if (!G_bFadeAnimating)
        return;

    // Unordered compares take the falling branch.
    if (G_FadeLevel < G_FadeTarget)
    {
        // One rounding at the store (x87).
        G_FadeLevel = static_cast<float>(static_cast<double>(G_FadeSpeed) * DeltaSeconds +
                                         static_cast<double>(G_FadeLevel));
        if (!(G_FadeLevel >= G_FadeTarget))
            return;
        G_FadeLevel = G_FadeTarget;
        G_bFadeAnimating = false;
        return;
    }

    // Equal or unordered stops the animation with the level left as it is.
    if (!(G_FadeLevel > G_FadeTarget))
    {
        G_bFadeAnimating = false;
        return;
    }

    // One rounding at the store (x87).
    G_FadeLevel = static_cast<float>(static_cast<double>(G_FadeLevel) -
                                     static_cast<double>(G_FadeSpeed) * DeltaSeconds);
    if (!(G_FadeLevel <= G_FadeTarget))
        return;
    G_FadeLevel = G_FadeTarget;
    G_bFadeAnimating = false;
}

void DrawFade(ICamera* Cam)
{
    // FUN_00412430. A NaN level draws nothing.
    const float Level = G_FadeLevel;
    if (!(Level > 0.0f))
        return;

    Win3DSetOrtho2D();
    Win3DSetViewport(nullptr, kFadeSource, kFadeDrawLine);

    // Blending goes on only while the fade is see-through.
    if (Level < kFadeOpaqueThreshold)
    {
        Win3DEnable(kWin3DBlend);
        Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
    }
    else
    {
        Win3DDisable(kWin3DBlend);
    }

    Win3DDisable(kWin3DCullFace);
    Win3DDisable(kWin3DDepthTest);
    Win3DSetDepthWrite(0);

    // A null camera covers the surface RenderMgr last laid out (DAT_00610850 / DAT_00610854)
    // -- not the options object's requested mode.
    int Left = 0;
    int Top = 0;
    int Right = static_cast<int>(G_RenderSurfaceWidth);
    int Bottom = static_cast<int>(G_RenderSurfaceHeight);
    if (Cam != nullptr)
    {
        // The space camera from LAB_0054b000, the room camera (FUN_00438c90) from FUN_00558c10.
        const int* const Rect = Cam->GetViewport();
        Left = Rect[0];
        Top = Rect[1];
        Right = Rect[2];
        Bottom = Rect[3];
    }

    Win3DColor4f(G_FadeColour[0], G_FadeColour[1], G_FadeColour[2], Level);

    // Each corner pushed half a pixel outward.
    Win3DBegin(kWin3DQuads);
    Win3DVertex3f(static_cast<float>(Left) - 0.5f, static_cast<float>(Top) - 0.5f, 0.0f);
    Win3DVertex3f(static_cast<float>(Right) + 0.5f, static_cast<float>(Top) - 0.5f, 0.0f);
    Win3DVertex3f(static_cast<float>(Right) + 0.5f, static_cast<float>(Bottom) + 0.5f, 0.0f);
    Win3DVertex3f(static_cast<float>(Left) - 0.5f, static_cast<float>(Bottom) + 0.5f, 0.0f);
    Win3DEnd();

    // Depth test and write go back on; culling and blending are NOT restored, as in retail.
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
}
