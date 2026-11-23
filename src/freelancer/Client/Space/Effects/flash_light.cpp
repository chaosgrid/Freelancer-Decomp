// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/flash_light.h"

#include "Common/engine_light.h"   // ScaleVec3 (FUN_00411d60)
#include "Core/game_options.h"
#include "Common/misc_util.h"      // MiscUtilLoadTextureLibrary (FUN_0041c7e0)
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"   // Win3DApplyCamera (FUN_00422950)

#include "common_goods.h"
#include "common_misc.h"    // ICamera
#include "DALib.h"
#include "dacom.h"          // DACOM_CRC::GetCRC32
#include "render/shading.h" // ITextureLibrary2

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// DAT_006799c0 / DAT_006799c4 -- the "bulb" and "shine" textures of fx\efx.txm, the
// ITextureLibrary2 records slot 12 hands out. Written by FUN_00557550 / FUN_005575e0, read by
// the flare passes of FUN_00558000.
void* G_FlashlightBulbTexture = nullptr;
void* G_FlashlightShineTexture = nullptr;

namespace
{
    // The {name, CRC32} query TextureLib slot +0x30 reads (Query+4 = CRC).
    struct FFlareTextureQuery
    {
        char const* Name;  // +0
        unsigned long Crc; // +4
    };

    // DAT_006799c8 -- set by FUN_00558580 when a set holds more than 0x20 lights.
    bool G_FlashlightBatchOverflow = false;

    // DAT_005caeec (1 / 32768), DAT_005da4c0 (0.2), DAT_005da4b8 (0.9).
    float const kRandScale = 3.0517578125e-05f;
    double const kJitterSpan = 0.20000004768371582;
    double const kJitterBase = 0.8999999761581421;

    float CompanionFloat(Archetype::ICliObj* Companion, unsigned Offset)
    {
        return *reinterpret_cast<float const*>(reinterpret_cast<unsigned char const*>(Companion) +
                                               Offset);
    }

    Vector CompanionVector(Archetype::ICliObj* Companion, unsigned Offset)
    {
        return *reinterpret_cast<Vector const*>(reinterpret_cast<unsigned char const*>(Companion) +
                                                Offset);
    }

    char const* const kLightsCameraCpp = "E:\\FL\\Scratch\\Source\\Client\\supt\\LightsCamera.cpp";

    // FUN_00557d00 -- normalise in place with the 0x5f3759df inverse square root and one Newton
    // step (1.5f DAT_005ca220, 0.5f DAT_005c75e0); LenSq is rounded to float, the factor kept
    // double.
    void FlashLightNormalizeFast(float* V)
    {
        float const LenSq = static_cast<float>(
            (static_cast<double>(V[0]) * V[0] + static_cast<double>(V[1]) * V[1]) +
            static_cast<double>(V[2]) * V[2]);
        int Bits = 0;
        memcpy(&Bits, &LenSq, sizeof(Bits));
        int const GuessBits = 0x5f3759df - (Bits >> 1);
        float Guess = 0.0f;
        memcpy(&Guess, &GuessBits, sizeof(Guess));
        double const Scale =
            (1.5 - static_cast<double>(Guess) * Guess * LenSq * 0.5) * static_cast<double>(Guess);
        V[0] = static_cast<float>(Scale * V[0]);
        V[1] = static_cast<float>(Scale * V[1]);
        V[2] = static_cast<float>(Scale * V[2]);
    }

    // The quad both flare passes emit (inlined identically in FUN_005579b0 and FUN_00557d70).
    // Corners Center - R + U, Center + R + U, Center + R - U, Center - R - U at texture (0,0),
    // (1,0), (1,1), (0,1), rounded as retail rounds them.
    void EmitFlareQuad(float const* Center, float const* R, float const* U)
    {
        Win3DTexCoord2f(0.0f, 0.0f);
        float Left[3];
        Win3DVec3Sub(Left, Center, R);
        float Corner[3];
        Win3DVec3Add(Corner, Left, U);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);

        Win3DTexCoord2f(1.0f, 0.0f);
        float RightEdge[3] = {Center[0] + R[0], Center[1] + R[1], Center[2] + R[2]};
        Corner[0] = U[0] + RightEdge[0];
        Corner[1] = U[1] + RightEdge[1];
        Corner[2] = U[2] + RightEdge[2];
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);

        // Center + R is recomputed.
        Win3DTexCoord2f(1.0f, 1.0f);
        RightEdge[0] = Center[0] + R[0];
        RightEdge[1] = Center[1] + R[1];
        RightEdge[2] = Center[2] + R[2];
        Corner[0] = RightEdge[0] - U[0];
        Corner[1] = RightEdge[1] - U[1];
        Corner[2] = RightEdge[2] - U[2];
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);

        // Center - R - U: x and y keep Center - R unrounded, z rounds it to float first.
        Win3DTexCoord2f(0.0f, 1.0f);
        float const LeftZ = Center[2] - R[2];
        Corner[0] = static_cast<float>(
            (static_cast<double>(Center[0]) - static_cast<double>(R[0])) - static_cast<double>(U[0]));
        Corner[1] = static_cast<float>(
            (static_cast<double>(Center[1]) - static_cast<double>(R[1])) - static_cast<double>(U[1]));
        Corner[2] = LeftZ - U[2];
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
    }
}

FFlashLight::FFlashLight(Archetype::ICliObj* Companion)
{
    // FUN_00557700.
    MatrixSetIdentity(Orient);
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;

    // The CliEQLight companion's colours, sizes and cone.
    Color = CompanionVector(Companion, 0x54);
    MinColor = CompanionVector(Companion, 0x6c);
    BulbSize = CompanionFloat(Companion, 0x78);
    GlowSize = CompanionFloat(Companion, 0x7c);
    FlareConeOuterCos = CompanionFloat(Companion, 0x8c);
    FlareConeInnerCos = CompanionFloat(Companion, 0x90);
    Rate = 0.0f;
    BlinkFraction = 1.0f;

    // The period: avg_delay (+0x80) jittered to 0.9 .. 1.1 of itself.
    double const Period =
        (static_cast<double>(static_cast<float>(rand()) * kRandScale) * kJitterSpan + kJitterBase) *
        CompanionFloat(Companion, 0x80);
    float const PeriodF = static_cast<float>(Period);
    // Only a positive period sets the rate and the lit fraction (blink_duration at +0x84).
    if (Period > 0.0)
    {
        double const Inverse = 1.0 / static_cast<double>(PeriodF);
        Rate = static_cast<float>(Inverse);
        float const BlinkDuration = CompanionFloat(Companion, 0x84);
        float const Lit = BlinkDuration <= PeriodF ? BlinkDuration : PeriodF;
        BlinkFraction = static_cast<float>(Inverse * Lit);
    }

    Phase = static_cast<float>(rand()) * kRandScale;

    // +0x18 / +0x24 (BlendColor / GlowColor) are not written by the constructor; FUN_00557860
    // sets both before anything reads them.
}

FFlashLight::FFlashLight(Vector const* InColor, Vector const* InMinColor, float Period,
                         float InPhase, float BlinkDuration, float InBulbSize, float InGlowSize)
{
    // FUN_00557630.
    MatrixSetIdentity(Orient);
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;

    Color = *InColor;
    MinColor = *InMinColor;
    Phase = InPhase;
    BulbSize = InBulbSize;
    GlowSize = InGlowSize;
    FlareConeInnerCos = 0.0f;
    FlareConeOuterCos = 0.0f;
    Rate = 0.0f;
    BlinkFraction = 1.0f;

    // Only a positive period (not NaN) goes on.
    if (Period > 0.0f)
    {
        // The quotient is kept unrounded for the lit fraction.
        double const Inverse = 1.0 / static_cast<double>(Period);
        Rate = static_cast<float>(Inverse);
        // Unordered takes BlinkDuration.
        float const Lit = Period < BlinkDuration ? Period : BlinkDuration;
        BlinkFraction = static_cast<float>(Inverse * static_cast<double>(Lit));
    }

    // +0x18 / +0x24 (BlendColor / GlowColor) are not written, as in FUN_00557700.
}

void FFlashLight::SetFrame(Csys const& Frame)
{
    // FUN_00557810.
    Orient = Frame.Orient;
    Pos = Frame.Pos;
}

void FFlashLight::Update(float Delta)
{
    // FUN_00557860.
    BlendColor = Color;
    GlowColor = Color;
    if (Rate == 0.0f)
        return;

    // Phase wraps by its floor once it reaches 1.
    double const Advanced = static_cast<double>(Delta) * Rate + Phase;
    if (Advanced < 1.0)
        Phase = static_cast<float>(Advanced);
    else
        Phase = static_cast<float>(Advanced - floor(Advanced));

    // A triangle over the lit fraction of the period, 0 outside it.
    float Level = 0.0f;
    if (Phase <= BlinkFraction)
    {
        Level = Phase * (2.0f / BlinkFraction);
        if (Level > 1.0f)
            Level = 2.0f - Level;
    }

    GlowColor.x = Level * Color.x;
    GlowColor.y = Level * Color.y;
    GlowColor.z = Level * Color.z;
    BlendColor.x = (Color.x - MinColor.x) * Level + MinColor.x;
    BlendColor.y = (Color.y - MinColor.y) * Level + MinColor.y;
    BlendColor.z = (Color.z - MinColor.z) * Level + MinColor.z;
}

void FFlashLight::DrawShine(ICamera* Camera)
{
    // FUN_005579b0. (Retail also copies the orientation to the stack and never reads it.)
    float const Center[3] = {Pos.x, Pos.y, Pos.z};

    // Nothing for a black GlowColor (any NaN component draws).
    if (GlowColor.x == 0.0f && GlowColor.y == 0.0f && GlowColor.z == 0.0f)
        return;

    double Level = 1.0;
    if (FlareConeOuterCos > FlareConeInnerCos)
    {
        float Dir[3];
        Win3DVec3Sub(Dir, &Pos.x, Camera->GetPosition());
        FlashLightNormalizeFast(Dir);
        // The light's third axis; the dot (z + y) + x stays unrounded.
        float const Axis[3] = {Orient.E[0][2], Orient.E[1][2], Orient.E[2][2]};
        double const DotWide =
            (static_cast<double>(Dir[2]) * Axis[2] + static_cast<double>(Dir[1]) * Axis[1]) +
            static_cast<double>(Dir[0]) * Axis[0];
        // The first compare uses the unrounded dot, the second the rounded copy.
        float const Dot = static_cast<float>(DotWide);
        if (DotWide < static_cast<double>(FlareConeInnerCos))
        {
            Level = 0.0;
        }
        else if (Dot < FlareConeOuterCos)
        {
            Level = (static_cast<double>(Dot) - static_cast<double>(FlareConeInnerCos)) /
                    (static_cast<double>(FlareConeOuterCos) -
                     static_cast<double>(FlareConeInnerCos));
        }
    }

    float Shade[3];
    ScaleVec3(Shade, &GlowColor.x, static_cast<float>(Level));
    Win3DColor3f(Shade[0], Shade[1], Shade[2]);

    // The camera's first two columns (orientation read twice), scaled by GlowSize.
    float const* View = Camera->GetOrientation();
    float const Right[3] = {View[0], View[3], View[6]};
    View = Camera->GetOrientation();
    float const Up[3] = {View[1], View[4], View[7]};
    float R[3];
    ScaleVec3(R, Right, GlowSize);
    float U[3];
    ScaleVec3(U, Up, GlowSize);

    EmitFlareQuad(Center, R, U);
}

void FFlashLight::DrawBulb(ICamera* Camera)
{
    // FUN_00557d70. (Retail also copies the orientation to the stack and never reads it.)
    float const Center[3] = {Pos.x, Pos.y, Pos.z};

    // Nothing for a black BlendColor.
    if (BlendColor.x == 0.0f && BlendColor.y == 0.0f && BlendColor.z == 0.0f)
        return;

    Win3DColor3f(BlendColor.x, BlendColor.y, BlendColor.z);

    // The camera's first two columns, scaled by BulbSize.
    float const* View = Camera->GetOrientation();
    float const Right[3] = {View[0], View[3], View[6]};
    View = Camera->GetOrientation();
    float const Up[3] = {View[1], View[4], View[7]};
    float R[3];
    ScaleVec3(R, Right, BulbSize);
    float U[3];
    ScaleVec3(U, Up, BulbSize);

    EmitFlareQuad(Center, R, U);
}

FFlashLightSet::FFlashLightSet()
{
    // FUN_00558180.
}

FFlashLightSet::~FFlashLightSet()
{
    // FUN_005581b0.
    std::list<FFlashLightEntry*>::iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        FFlashLightEntry* Entry = *It;
        if (Entry == nullptr)
            continue;
        if (Entry->Light != nullptr)
            delete Entry->Light;
        delete Entry;
    }
}

void FFlashLightSet::Clear()
{
    // FUN_005582a0.
    std::list<FFlashLightEntry*>::iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        FFlashLightEntry* Entry = *It;
        if (Entry == nullptr)
            continue;
        if (Entry->Light != nullptr)
            delete Entry->Light;
        delete Entry;
    }
    Entries.clear();
}

void FFlashLightSet::Add(Archetype::ICliObj* Companion, int HardpointIndex)
{
    // FUN_00558310.
    FFlashLightEntry* Entry = new FFlashLightEntry;
    FFlashLight* Light = new FFlashLight(Companion);
    // An identity frame copied in again (the constructor already left the same values there).
    Matrix Identity;
    MatrixSetIdentity(Identity);
    Light->Orient = Identity;
    Light->Pos.x = 0.0f;
    Light->Pos.y = 0.0f;
    Light->Pos.z = 0.0f;
    Entry->Light = Light;
    Entry->HardpointIndex = HardpointIndex;
    Entries.push_back(Entry);
}

void FFlashLightSet::Update(HardpointSummary const* Hardpoints, float Delta)
{
    // FUN_00558470.
    std::list<FFlashLightEntry*>::iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        FFlashLightEntry* Entry = *It;
        Csys const World = DALib_Engine_get_csys(Hardpoints->get_instance(Entry->HardpointIndex));
        // Retail reads get_info twice.
        Csys Local;
        Local.Pos = Hardpoints->get_info(Entry->HardpointIndex).Position;
        Local.Orient = Hardpoints->get_info(Entry->HardpointIndex).Orient;
        Csys const Placed = World.trans_from_frame(Local);
        Entry->Light->Orient = Placed.Orient;
        Entry->Light->Pos = Placed.Pos;
        Entry->Light->Update(Delta);
    }
}

void FFlashLightSet::Draw(ICamera* Camera)
{
    // FUN_00558580: at most 0x20 lights on the stack; one more raises DAT_006799c8.
    FFlashLight* Batch[0x20];
    int Count = 0;
    std::list<FFlashLightEntry*>::iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        if (Count >= 0x20)
        {
            G_FlashlightBatchOverflow = true;
            break;
        }
        Batch[Count] = (*It)->Light;
        ++Count;
    }
    if (Count > 0)
        DrawFlashlightFlares(Batch, Count, Camera);
}

void DrawFlashlightFlares(FFlashLight* const* Lights, int Count, ICamera* Camera)
{
    // FUN_00558000.
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptFlashlights) == 0)
        return;
    if (Count == 0)
        return;

    Win3DSetViewport(Camera, kLightsCameraCpp, 0x339);
    Win3DApplyCamera(Camera);
    // Additive, depth-tested (LESSEQUAL) without depth write, no culling, no fog.
    Win3DDisable(kWin3DCullFace);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);
    Win3DSetDepthWrite(0);
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthFunc(4);
    Win3DDisable(kWin3DFog);

    // The "shine" frame is bound without testing the result. Deviation: retail's out buffer is
    // uninitialised stack; the port zeroes it.
    unsigned long ShineFrame[5] = {0, 0, 0, 0, 0};
    DALib::TextureLib->GetTextureFrame(G_FlashlightShineTexture, 0, ShineFrame);
    Win3DBindTexture(reinterpret_cast<void*>(ShineFrame[0]));
    Win3DBegin(kWin3DQuads);
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        Lights[Index]->DrawShine(Camera);
    Win3DEnd();

    unsigned long BulbFrame[5] = {0, 0, 0, 0, 0};
    DALib::TextureLib->GetTextureFrame(G_FlashlightBulbTexture, 0, BulbFrame);
    Win3DBindTexture(reinterpret_cast<void*>(BulbFrame[0]));
    Win3DBegin(kWin3DQuads);
    for (Index = 0; Index < Count; ++Index)
        Lights[Index]->DrawBulb(Camera);
    Win3DEnd();

    Win3DSetDepthFunc(2);
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DCullFace);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
}

void LoadFlashlightTextures()
{
    // FUN_00557550. The texture group is loaded, the two records taken by {name, CRC} through
    // TextureLib slot 12 (+0x30), and the group released straight away with slot 7 (+0x1c).
    int const Group = MiscUtilLoadTextureLibrary("fx\\efx.txm");

    FFlareTextureQuery Query;
    // .rdata 0x005db6f8.
    Query.Name = "bulb";
    Query.Crc = DACOM_CRC::GetCRC32("bulb");
    DALib::TextureLib->FindTextureByCrc(&Query, &G_FlashlightBulbTexture);

    Query.Name = "shine";
    Query.Crc = DACOM_CRC::GetCRC32("shine");
    DALib::TextureLib->FindTextureByCrc(&Query, &G_FlashlightShineTexture);

    DALib::TextureLib->GarbageCollect(Group);
}

void ReleaseFlashlightTextures()
{
    // FUN_005575e0. TextureLib slot 14 (+0x38) on each record that is set, then clear it.
    if (G_FlashlightBulbTexture != nullptr)
    {
        DALib::TextureLib->ReleaseTextureRecord(G_FlashlightBulbTexture);
        G_FlashlightBulbTexture = nullptr;
    }
    if (G_FlashlightShineTexture != nullptr)
    {
        DALib::TextureLib->ReleaseTextureRecord(G_FlashlightShineTexture);
        G_FlashlightShineTexture = nullptr;
    }
}
