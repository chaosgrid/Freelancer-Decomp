// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll FAiTarget (the 0x88-byte AI-target cache record): FAiTarget::Init
// (0x062d09b0), FAiTarget::RefreshPos (0x062c47c0) and GetAiTargetEffectiveRadius
// (0x062e6070). See src/include/common_aisteer.h for the layout.

#include "common_aisteer.h"

// FUN_062d09b0. Sets +0x80 / +0x84 and clears the four guard bytes +0x7c..+0x7f (retail order
// 7d, 7c, 7e, 7f; no observable difference).
void FAiTarget::Init(IObjInspect const* NewAttitudeRef, IObjInspect const* NewIface)
{
    AttitudeRefIface = const_cast<IObjInspect*>(NewAttitudeRef);
    Iface = const_cast<IObjInspect*>(NewIface);
    bAttitudeValid = 0;
    bFlagsValid = 0;
    bPosValid = 0;
    bCornersValid = 0;
}

// FUN_062c47c0. Retail raises bPosValid before the vcall, unlike the get_type refresh below
// which raises it after -- reproduced as written, since a re-entrant call would see a
// different guard state. No null test on Iface.
void FAiTarget::RefreshPos()
{
    if (bPosValid != 0)
        return;
    bPosValid = 1;
    Iface->get_physical_radius(CachedRadius, CachedPos);
}

// FUN_062ca1b0. get_type is Iface slot 0x88. No null test.
unsigned FAiTarget::GetFlags()
{
    if (bFlagsValid == 0)
    {
        Iface->get_type(CachedFlags);
        bFlagsValid = 1;
    }
    return CachedFlags;
}

// FUN_062e6070. Retail reuses its own incoming parameter slot as get_atmosphere_range's
// out-parameter (Ghidra's unaff_ESI/unaff_retaddr). FAiTarget::RefreshPos is inlined.
float GetAiTargetEffectiveRadius(FAiTarget* Target)
{
    Target->RefreshPos();

    // Snapshotted before the second vtable call.
    float const Radius = Target->CachedRadius;

    // Pre-zeroed, so a failing get_atmosphere_range (Iface +0x140, no null test, return
    // ignored) yields 0.0f.
    float AtmosphereRange = 0.0f;
    Target->Iface->get_atmosphere_range(AtmosphereRange);

    // Equal and unordered return the radius.
    return (AtmosphereRange > Radius) ? AtmosphereRange : Radius;
}
