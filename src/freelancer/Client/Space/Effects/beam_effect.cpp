// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/beam_effect.h"

#include "Space/Effects/effect_base.h"
#include "Space/Effects/effect_type_info.h"
#include "Space/Effects/effect_type_list.h"
#include "Space/Effects/effect_vertex_batch.h"
#include "Core/game_options.h"
#include "Core/globals.h"
#include "Space/HUD/hud_target.h"     // HudTransformPoint (FUN_004e7090)
#include "Core/perf_options.h"
#include "Common/Graphics/texture_shapes.h"
#include "Common/Graphics/vis_effect_arch.h"

#include "DALib.h"
#include "dacom.h"
#include "common_csys.h"
#include "engine/engbase.h"
#include "Space/Cameras/space_camera.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

int G_BeamEffectLiveCount = 0;
void* G_BeamEffectTexture = nullptr;

namespace
{
    constexpr float kZero = 0.0f;
    constexpr float kHalf = 0.5f;
    constexpr float kOne = 1.0f;
    constexpr float kUvCore = 0.75f;     // 0x3f400000
    constexpr float kUvMid = 0.25f;      // 0x3e800000
    constexpr float kUvUAlt0 = 0.55f;    // 0x3f0ccccd
    constexpr float kUvUAlt1 = 0.45f;    // 0x3ee66666
    constexpr double kByteScale = 255.0; // DAT_005c89c0
    constexpr float kLifetimeIntensityCap = 0.3f; // DAT_006129cc

    bool BeamGeometryOptionEnabled()
    {
        // FUN_005792b0(0x26) == kGameOptParticles.
        return GetGameOption(kGameOptParticles) != 0;
    }

    I3DMathEngine* EffectMath()
    {
        if (G_EffectMathEngine == nullptr)
            G_EffectMathEngine = Acquire3DMathEngine();
        return G_EffectMathEngine;
    }

    // The beam as the shared Effect head (same first 0x6c bytes).
    FEffectBase* AsEffect(FBeamEffect* This)
    {
        return This;
    }

    void SetVec(FVector* Out, float X, float Y, float Z)
    {
        Out->X = X;
        Out->Y = Y;
        Out->Z = Z;
    }

    // The builders' inline colour: Scale * colour * 255.0 in double, truncated, and only the
    // low byte stored -- no clamp.
    unsigned char ColorByte(float Component, float Scale)
    {
        double const V = static_cast<double>(Scale) * static_cast<double>(Component) * kByteScale;
        return static_cast<unsigned char>(static_cast<long>(V));
    }

    void SetBatchColorRgb(FEffectVertexBatch* Batch, float Scale, float R, float G, float B)
    {
        // DAT_00674cac..DAT_00674caf.
        Batch->ColorR = ColorByte(R, Scale);
        Batch->ColorG = ColorByte(G, Scale);
        Batch->ColorB = ColorByte(B, Scale);
        Batch->ColorA = 0xff;
    }
}

// FUN_004f9540
FBeamEffect::FBeamEffect(unsigned TypeId) : FEffectBase(TypeId)
{
    Arch = nullptr;
    Flags70 = 0;
    std::memset(Pad71, 0, sizeof(Pad71));
    CullRadius = 50.0f;
    Age = -1.0f;
    FadeTimeLeft = -1.0f;
    ExtendTime = 0.1f;
    G_BeamEffectLiveCount = G_BeamEffectLiveCount + 1;
}

void BeamEffectConstruct(FBeamEffect* This, unsigned TypeId)
{
    ::new (This) FBeamEffect(TypeId);
}


void BeamEffectAddRef(FBeamEffect* This)
{
    // FUN_004f7a80.
    This->RefCount = This->RefCount + 1;
}

int BeamEffectRelease(FBeamEffect* This)
{
    return This->Release();
}

// FUN_004f9510 (slot +0x20) around FUN_004f9450.
FBeamEffect::~FBeamEffect()
{
    G_BeamEffectLiveCount = G_BeamEffectLiveCount - 1;
}

void BeamEffectDestroy(FBeamEffect* This, int FreeMemory)
{
    if ((FreeMemory & 1) != 0)
        delete This;
    else
        This->~FBeamEffect();
}


void BeamEffectAttach(FBeamEffect* This, void* WatchableOwner, int EngineIndex,
                      FTransform const* LocalXform)
{
    This->Attach(WatchableOwner, EngineIndex, LocalXform);
}

// LAB_004f9600 — vtable[0] start(EffectArch*).
unsigned char FBeamEffect::Start(FEffectArch* EffectArch)
{
    FBeamEffect* const This = this;
    // No null test on the arch.
    FBeamEffectArch* const BeamArch = EffectArch->VisBeam;
    This->Arch = BeamArch;
    if (BeamArch == nullptr)
        return 0;

    // Retail hardcodes EFT_CODE_PROJ_EFFECT (0x2c) for the beam bucket.
    constexpr unsigned kBeamEffectTypeId = 0x2cu;
    This->EffectTypeId = kBeamEffectTypeId;
    FEffectTypeInfo* TypeInfo = EffectTypeInfoByIndex(static_cast<int>(kBeamEffectTypeId));
    if (TypeInfo != nullptr)
        This->Priority = TypeInfo->Priority;

    This->Flags08 = static_cast<unsigned char>(This->Flags08 & 0xfdu);
    // Inlined FUN_004f7af0.
    EffectTypeListLink(This);

    // Already resolved once (DAT_00674f54).
    if (G_BeamEffectTexture != nullptr)
        return 1;

    // "code_beam" from the effect ShapeDB (DAT_00674f24); a miss is FDUMPed by Find.
    FTexShape const* Shape = G_EffectShapeDB.Find("code_beam", kShapeDbHeaderLower);
    G_BeamEffectTexture = Shape != nullptr ? static_cast<void*>(const_cast<FTexShape*>(Shape))
                                           : nullptr;

    if (G_BeamEffectTexture != nullptr)
        return 1;

    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp", 0x5df,
          "texture ( code_beam ) for BeamEffect not found");

    // Texture miss: frozen and marked dead.
    EffectTypeListLink(This);
    EffectFreezeToWorld(AsEffect(This));
    This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
    return 0;
}

unsigned char BeamEffectStart(FBeamEffect* This, FEffectArch* EffectArch)
{
    return This->FBeamEffect::Start(EffectArch);
}

// Beams are created by FUN_0053f340 (cli_projectile_fx.cpp) through FUN_004f79a0(0x84).

// LAB_004f9480 — slot +8, shared with ParticleEffect: the world translation out, returns +0x74.
// Retail inlines FUN_004f7ef0's body.
float FBeamEffect::GetWorldPos(FVector* Out)
{
    FBeamEffect* const This = this;
    FTransform World;
    EffectGetWorldTransform(AsEffect(This), &World);
    Out->X = World.Translation.X;
    Out->Y = World.Translation.Y;
    Out->Z = World.Translation.Z;
    return This->CullRadius;
}

float BeamEffectGetWorldPos(FBeamEffect* This, FVector* Out)
{
    return This->FBeamEffect::GetWorldPos(Out);
}

// LAB_004f97a0 — vtable[+0x04].
void FBeamEffect::Kill()
{
    FBeamEffect* const This = this;
    FEffectBase* const Base = AsEffect(This);

    if ((This->Flags70 & 2u) != 0u)
    {
        EffectTypeListLink(This);
        EffectFreezeToWorld(Base);
        This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        return;
    }

    EffectTypeListLink(This);
    if (This->EngineIndex != -1)
    {
        if (EffectWatchedObject(Base) == nullptr)
        {
            EffectTypeListLink(This);
            EffectFreezeToWorld(Base);
            This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        }
        else
        {
            // FUN_004f7db0's body inlined; not marked dead.
            EffectFreezeToWorld(Base);
        }
    }

    // Below 0.0 or above the cap (DAT_006129cc) becomes the cap; exactly 0.0 and NaN are left
    // alone.
    if (This->FadeTimeLeft < 0.0f || This->FadeTimeLeft > kLifetimeIntensityCap)
        This->FadeTimeLeft = kLifetimeIntensityCap;
}

void BeamEffectArmDeath(FBeamEffect* This)
{
    This->FBeamEffect::Kill();
}

// LAB_004f9980 — vtable[+0x14].
unsigned char FBeamEffect::UpdateVisibility(float Dt, FSpaceCamera* Cam)
{
    FBeamEffect* const This = this;
    // No code_beam texture: the visibility bit as it stands.
    if (G_BeamEffectTexture == nullptr)
        return static_cast<unsigned char>(This->Flags70 & 1u);

    // Below the perf level (or unordered) the beam is killed (FUN_004f7bd0 inlined).
    float const Perf = GetPerfGenericEffectPerf();
    if (!(This->Priority >= Perf))
    {
        EffectKill(AsEffect(This));
        return static_cast<unsigned char>(This->Flags70 & 1u);
    }

    FVector WorldPos;
    float const Radius = This->GetWorldPos(&WorldPos);

    ICamera* const CamIface = AsCamera(Cam);
    FTransform const* const ViewXform =
        reinterpret_cast<FTransform const*>(CamIface->GetViewTransform());
    FVector ViewPos;
    EffectMath()->TransformXform(ViewPos, *ViewXform, WorldPos);

    // The sphere's far edge must lie strictly beyond the near plane.
    float const NegZ = -ViewPos.Z;
    float const FarEdge = NegZ + Radius;
    if (!(CamIface->GetZNear() < FarEdge))
        return static_cast<unsigned char>(This->Flags70 & 1u);

    // The pbubble test: NegZ - Radius must be strictly inside Near + (Far - Near) * (1 - perf)
    // (NaN leaves).
    FEffectTypeInfo const* const TypeInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    float const NearDistance = NegZ - Radius;
    float const Threshold = (TypeInfo->PbubbleFar - TypeInfo->PbubbleNear) *
                                static_cast<float>(1.0 - static_cast<double>(Perf)) +
                            TypeInfo->PbubbleNear;
    if (!(Threshold > NearDistance))
        return static_cast<unsigned char>(This->Flags70 & 1u);

    // The two side planes (camera +0x80/+0x88 and +0x90/+0x94): outside when the plane distance
    // is greater than the radius or NaN.
    float const DistX = fabsf(ViewPos.X) * Cam->PlaneX[0] + ViewPos.Z * Cam->PlaneX[2];
    float const DistY = fabsf(ViewPos.Y) * Cam->PlaneY[1] + ViewPos.Z * Cam->PlaneY[2];
    if (!(DistX <= Radius) || !(DistY <= Radius))
    {
        This->Flags70 = static_cast<unsigned char>(This->Flags70 & 0xfeu);
        return static_cast<unsigned char>(This->Flags70 & 1u);
    }

    This->Flags70 = static_cast<unsigned char>(This->Flags70 | 1u);
    return static_cast<unsigned char>(This->Flags70 & 1u);
}

unsigned char BeamEffectUpdateVisibility(FBeamEffect* This, float Dt, FSpaceCamera* Cam)
{
    return This->FBeamEffect::UpdateVisibility(Dt, Cam);
}

// LAB_004f9c20 — vtable[+0x18].
unsigned char FBeamEffect::TickDraw(float Dt, FSpaceCamera* Cam)
{
    FBeamEffect* const This = this;
    if (GetGameOption(kGameOptParticles) == 0)
        return 0;

    if (ShouldSkipLocalServerTick())
        return 1;

    // Below zero (or NaN) restarts at 0, else accumulates.
    if (!(This->Age >= 0.0f))
        This->Age = 0.0f;
    else
        This->Age = This->Age + Dt;

    // No lifetime (<= 0 or NaN): keep drawing.
    if (!(This->FadeTimeLeft > 0.0f))
        return 1;

    // Still above zero (or NaN) after the tick: keep drawing.
    This->FadeTimeLeft = This->FadeTimeLeft - Dt;
    if (!(This->FadeTimeLeft <= 0.0f))
        return 1;

    // FUN_004f7bd0 inlined, then the dead bit retail sets again (already set by the kill).
    EffectKill(AsEffect(This));
    This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
    return 0;
}

unsigned char BeamEffectTickDraw(FBeamEffect* This, float Dt, FSpaceCamera* Cam)
{
    return This->FBeamEffect::TickDraw(Dt, Cam);
}

// LAB_004fa0a0.
unsigned char FBeamEffect::BuildGeometry(FSpaceCamera* Cam)
{
    FBeamEffect* const This = this;
    This->Flags70 = static_cast<unsigned char>(This->Flags70 & 0xfeu);
    if (!BeamGeometryOptionEnabled())
        return 0;

    FTransform StackXform;
    FTransform* UseXform = &This->LocalXform;

    // With a watched object (+0x44, and that minus eight, non-null) and an engine instance
    // (+0x40 != -1): instance transform x local; otherwise the local transform at +0x10.
    Watchable const* const Watched = This->Watcher.target();
    if (Watched != nullptr && reinterpret_cast<uintptr_t>(Watched) - 8u != 0u &&
        This->EngineIndex != -1)
    {
        float* const InstanceRaw = DALib::Engine->GetInstanceTransform(
            reinterpret_cast<void*>(static_cast<intptr_t>(This->EngineIndex)));
        EffectMath()->MulTransform(StackXform, *reinterpret_cast<FTransform const*>(InstanceRaw),
                                   This->LocalXform);
        UseXform = &StackXform;
    }

    // T = +0x78 / +0x80 only when strictly below (ordered), else 1.
    float T = 1.0f;
    if (This->Age < This->ExtendTime)
        T = This->Age / This->ExtendTime;

    // With +0x7c above 0, intensity = +0x7c / 0.3f; the unrounded quotient at or below 0
    // returns 0.
    float Intensity = 1.0f;
    if (This->FadeTimeLeft > 0.0f)
    {
        double const Quotient = static_cast<double>(This->FadeTimeLeft) / 0.3f;
        Intensity = static_cast<float>(Quotient);
        if (Quotient <= 0.0)
            return 0;
    }

    FBeamEffectArch* const Arch = This->Arch;

    if ((This->Flags70 & 2u) != 0u)
    {
        BeamEffectBuildSingleSegment(This, UseXform, 0.0f, Intensity);
        return 1;
    }
    if (T == 0.0f)
    {
        BeamEffectBuildSingleSegment(This, UseXform, 0.0f, Intensity);
        return 1;
    }

    if (Arch->SegmentMode == 0)
        BeamEffectBuildSingleSegment(This, UseXform, T, Intensity);
    else if (Arch->SegmentMode == 1)
        BeamEffectBuildMultiSegment(This, UseXform, T, Intensity);
    return 1;
}

unsigned char BeamEffectBuildGeometry(FBeamEffect* This, FSpaceCamera* Cam)
{
    return This->FBeamEffect::BuildGeometry(Cam);
}

// FUN_004fa270 (returns 1).
int BeamEffectBuildSingleSegment(FBeamEffect* This, FTransform const* WorldXform, float T,
                                 float Intensity)
{
    FBeamEffectArch* const Arch = This->Arch;
    FEffectVertexBatch* const Batch = &G_EffectVertexBatch;

    float NearR = Arch->RadiusNear;
    if (T == 0.0f && (This->Flags70 & 8u) != 0u)
    {
        if (!IsFirstPersonCameraCurrent())
            NearR = Arch->RadiusNearAlt;
    }

    float TipR = Arch->RadiusTip;
    if (TipR < NearR)
        TipR = NearR;

    // No clamps here (unlike FUN_004fb1e0). The Z sums round once at the store.
    float const Delta0 = Arch->Radius0 - NearR;
    float const Delta1 = Arch->Radius1 - NearR;
    float const EndR = static_cast<float>(static_cast<double>(Delta1) * T + NearR);
    float const StartRNeg = static_cast<float>(-(static_cast<double>(Delta0) * T + NearR));

    FVector WorldT;
    WorldT.X = WorldXform->Translation.X;
    WorldT.Y = WorldXform->Translation.Y;
    WorldT.Z = WorldXform->Translation.Z;

    // Local axis samples: cross arms in local XY, beam extent along local Z.
    FVector VX;
    SetVec(&VX, NearR, 0.0f, 0.0f);
    FVector VY;
    SetVec(&VY, 0.0f, NearR, 0.0f);
    FVector TipX;
    SetVec(&TipX, TipR, 0.0f, 0.0f);
    FVector TipY;
    SetVec(&TipY, 0.0f, TipR, 0.0f);
    FVector EndZ;
    SetVec(&EndZ, 0.0f, 0.0f, EndR);
    FVector StartZ;
    SetVec(&StartZ, 0.0f, 0.0f, StartRNeg);

    FVector XfPosX;
    FVector XfNegX;
    FVector XfPosY;
    FVector XfNegY;
    FVector XfTipPosX;
    FVector XfTipNegX;
    FVector XfTipPosY;
    FVector XfTipNegY;
    FVector XfEnd;   // stack +0x30 after transforms
    FVector XfStart; // stack +0x90
    FVector Tmp;

    // The NearR axes and their negations go through HudTransformPoint (FUN_004e7090)...
    HudTransformPoint(WorldXform, &XfPosX, &VX);
    SetVec(&Tmp, -VX.X, -VX.Y, -VX.Z);
    HudTransformPoint(WorldXform, &XfNegX, &Tmp);
    HudTransformPoint(WorldXform, &XfPosY, &VY);
    SetVec(&Tmp, -VY.X, -VY.Y, -VY.Z);
    HudTransformPoint(WorldXform, &XfNegY, &Tmp);

    // ...the rest through inlined TransformXform on DAT_00674f50 (re-acquired before each).
    EffectMath()->TransformXform(XfTipPosX, *WorldXform, TipX);
    SetVec(&Tmp, -TipX.X, -TipX.Y, -TipX.Z);
    EffectMath()->TransformXform(XfTipNegX, *WorldXform, Tmp);
    EffectMath()->TransformXform(XfTipPosY, *WorldXform, TipY);
    SetVec(&Tmp, -TipY.X, -TipY.Y, -TipY.Z);
    EffectMath()->TransformXform(XfTipNegY, *WorldXform, Tmp);
    EffectMath()->TransformXform(XfStart, *WorldXform, StartZ);
    EffectMath()->TransformXform(XfEnd, *WorldXform, EndZ);

    // UV endpoints from Arch+0x8 / +0xc.
    float UvV0;
    float UvV1;
    if (Arch->UvModeV == 1)
    {
        UvV0 = 0.0f;
        UvV1 = kHalf;
    }
    else
    {
        UvV0 = kHalf;
        UvV1 = kOne;
    }
    float const UvVMid = (UvV0 + UvV1) * static_cast<float>(kHalf); // *0.5 double

    float UvU0;
    float UvU1;
    if (Arch->UvModeU == 1)
    {
        UvU0 = 0.0f;
        UvU1 = kUvUAlt1; // 0.45
    }
    else
    {
        UvU0 = kUvUAlt0; // 0.55
        UvU1 = kOne;
    }

    float const Scale0 = Intensity * Arch->IntensityScale0;
    float const Scale1 = Intensity * Arch->IntensityScale1;

    // Tip cross: V0 WorldT, V1 tip+X, V2 tip+Y, V3 tip-X, V4 tip-Y.
    // Fan: (0,1,2)(0,3,2)(0,3,4)(0,1,4).
    SetBatchColorRgb(Batch, Scale0, Arch->ColorOuterR, Arch->ColorOuterG, Arch->ColorOuterB);
    Batch->U = kUvCore;
    Batch->V = UvVMid;
    unsigned short const I0 =
        static_cast<unsigned short>(EffectVertexBatchAppend(Batch, WorldT.X, WorldT.Y, WorldT.Z));

    SetBatchColorRgb(Batch, Scale0, Arch->ColorTipR, Arch->ColorTipG, Arch->ColorTipB);
    Batch->U = kHalf;
    Batch->V = UvV0;
    unsigned short const I1 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfTipPosX.X, XfTipPosX.Y, XfTipPosX.Z));

    Batch->V = UvV1;
    unsigned short const I2 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfTipPosY.X, XfTipPosY.Y, XfTipPosY.Z));

    Batch->U = kOne;
    Batch->V = UvV1;
    unsigned short const I3 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfTipNegX.X, XfTipNegX.Y, XfTipNegX.Z));

    Batch->V = UvV0;
    unsigned short const I4 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfTipNegY.X, XfTipNegY.Y, XfTipNegY.Z));

    // Second structure (NOT a tip-radius mirror): NearR XY arms + local-Z Start/End, Scale1,
    // UVs from Arch+0xc. The tip fan's indices are pushed AFTER these vertices.
    // A0 WorldT outer U=0 V=UvU0
    // A1 XfStart core  U=0.25 V=UvU1
    // A2 XfEnd   mid   U=0.25 V=UvU1
    // A3 XfPosX  tip   U=0.25 V=UvU0
    // A4 XfNegX / A5 XfPosY / A6 XfNegY (reuse tip colour/UV)
    SetBatchColorRgb(Batch, Scale1, Arch->ColorOuterR, Arch->ColorOuterG, Arch->ColorOuterB);
    Batch->U = 0.0f;
    Batch->V = UvU0;
    unsigned short const A0 =
        static_cast<unsigned short>(EffectVertexBatchAppend(Batch, WorldT.X, WorldT.Y, WorldT.Z));

    SetBatchColorRgb(Batch, Scale1, Arch->ColorCoreR, Arch->ColorCoreG, Arch->ColorCoreB);
    Batch->U = kUvMid;
    Batch->V = UvU1;
    unsigned short const A1 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfStart.X, XfStart.Y, XfStart.Z));

    SetBatchColorRgb(Batch, Scale1, Arch->ColorMidR, Arch->ColorMidG, Arch->ColorMidB);
    Batch->U = kUvMid;
    Batch->V = UvU1;
    unsigned short const A2 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfEnd.X, XfEnd.Y, XfEnd.Z));

    SetBatchColorRgb(Batch, Scale1, Arch->ColorTipR, Arch->ColorTipG, Arch->ColorTipB);
    Batch->U = kUvMid;
    Batch->V = UvU0;
    unsigned short const A3 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfPosX.X, XfPosX.Y, XfPosX.Z));

    unsigned short const A4 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfNegX.X, XfNegX.Y, XfNegX.Z));
    unsigned short const A5 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfPosY.X, XfPosY.Y, XfPosY.Z));
    unsigned short const A6 = static_cast<unsigned short>(
        EffectVertexBatchAppend(Batch, XfNegY.X, XfNegY.Y, XfNegY.Z));

    // All indices (tip, then second).
    EffectVertexBatchEnsureIndices(Batch, 3);
    Batch->Indices[Batch->IndexCount] = I0;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I1;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I2;
    Batch->IndexCount = Batch->IndexCount + 1;
    EffectVertexBatchPushTriangle(Batch, I0, I3, I2);
    EffectVertexBatchPushTriangle(Batch, I0, I3, I4);
    EffectVertexBatchPushTriangle(Batch, I0, I1, I4);

    EffectVertexBatchPushTriangle(Batch, A0, A1, A3);
    EffectVertexBatchPushTriangle(Batch, A0, A1, A4);
    EffectVertexBatchPushTriangle(Batch, A0, A1, A5);
    EffectVertexBatchPushTriangle(Batch, A0, A1, A6);
    EffectVertexBatchPushTriangle(Batch, A0, A2, A3);
    EffectVertexBatchPushTriangle(Batch, A0, A2, A4);
    // Third index A5, as retail: 0x004fb1ae reads frame +0x3c, A0's slot until 0x004fb16d
    // stored the MOVZX'd A5 over it (MOV [ESP+0x48],EAX with three arguments pushed); A0
    // itself stays in ESI.
    EffectVertexBatchPushTriangle(Batch, A0, A2, A5);
    EffectVertexBatchPushTriangle(Batch, A0, A2, A6);
    return 1;
}

// FUN_00423b30: Out = A + B, per component.
static void VecAdd(FVector* Out, FVector const* A, FVector const* B)
{
    Out->X = A->X + B->X;
    Out->Y = A->Y + B->Y;
    Out->Z = A->Z + B->Z;
}

// FUN_00411350: Out = -In.
static void VecNegate(FVector* Out, FVector const* In)
{
    Out->X = -In->X;
    Out->Y = -In->Y;
    Out->Z = -In->Z;
}

// FUN_004fb1e0 (returns 1) -- the SegmentMode 1 ("BeamBolt") builder. A tip cross (TipR)
// fanned from the origin, then a tube: origin -> Start behind it, the NearR ring around the
// origin, the ring pushed forward to Mid, and Mid -> Mid+End in front. The trailing `+0x..`
// comments on locals are retail's ESP-relative frame slots.
int BeamEffectBuildMultiSegment(FBeamEffect* This, FTransform const* WorldXform, float T,
                                float Intensity)
{
    FBeamEffectArch* const Arch = This->Arch;
    FEffectVertexBatch* const Batch = &G_EffectVertexBatch;

    // NearR = +0x6c, or flash_size +0x78 when T == 0; TipR = +0x74, raised to NearR.
    float NearR = Arch->RadiusNear;
    if (T == 0.0f)
        NearR = Arch->RadiusNearAlt;
    float TipR = Arch->RadiusTip;
    if (TipR < NearR)
        TipR = NearR;

    // The two lengths less NearR and core_length, each zeroed when strictly below 0.
    float Delta0 = Arch->Radius0 - NearR;   // +0x28
    float Delta1 = Arch->Radius1 - NearR;   // +0x20
    float MidR = Arch->RadiusMid;           // +0x14
    if (Delta0 < 0.0f)
        Delta0 = 0.0f;
    if (Delta1 < 0.0f)
        Delta1 = 0.0f;
    if (MidR < 0.0f)
        MidR = 0.0f;

    // The local vectors. The Z sums round once at the store.
    FVector NearX;                                         // +0x60
    SetVec(&NearX, NearR, 0.0f, 0.0f);
    FVector NearY;                                         // +0x3c
    SetVec(&NearY, 0.0f, NearR, 0.0f);
    FVector TipX;                                          // +0x6c
    SetVec(&TipX, TipR, 0.0f, 0.0f);
    FVector TipY;                                          // +0x78
    SetVec(&TipY, 0.0f, TipR, 0.0f);
    FVector WorldT;                                        // +0x48
    WorldT.X = WorldXform->Translation.X;
    WorldT.Y = WorldXform->Translation.Y;
    WorldT.Z = WorldXform->Translation.Z;
    FVector MidLocal;                                      // +0x54
    SetVec(&MidLocal, 0.0f, 0.0f, MidR * T);
    FVector StartLocal;                                    // +0x14c
    SetVec(&StartLocal, 0.0f, 0.0f,
           static_cast<float>(-(static_cast<double>(Delta0) * T + NearR)));
    FVector EndLocal;                                      // +0x128
    SetVec(&EndLocal, 0.0f, 0.0f, static_cast<float>(static_cast<double>(Delta1) * T + NearR));

    // Nine HudTransformPoint calls: each axis, its negation, then the Start point.
    FVector Tmp;                                           // +0x30
    FVector XfNearPosX;                                    // +0x134
    FVector XfNearNegX;                                    // +0x104
    FVector XfNearPosY;                                    // +0x158
    FVector XfNearNegY;                                    // +0xbc
    FVector XfTipPosX;                                     // +0x140
    FVector XfTipNegX;                                     // +0xd4
    FVector XfTipPosY;                                     // +0x11c
    FVector XfTipNegY;                                     // +0xb0
    FVector XfStart;                                       // +0xec
    HudTransformPoint(WorldXform, &XfNearPosX, &NearX);
    VecNegate(&Tmp, &NearX);
    HudTransformPoint(WorldXform, &XfNearNegX, &Tmp);
    HudTransformPoint(WorldXform, &XfNearPosY, &NearY);
    VecNegate(&Tmp, &NearY);
    HudTransformPoint(WorldXform, &XfNearNegY, &Tmp);
    HudTransformPoint(WorldXform, &XfTipPosX, &TipX);
    VecNegate(&Tmp, &TipX);
    HudTransformPoint(WorldXform, &XfTipNegX, &Tmp);
    HudTransformPoint(WorldXform, &XfTipPosY, &TipY);
    VecNegate(&Tmp, &TipY);
    HudTransformPoint(WorldXform, &XfTipNegY, &Tmp);
    HudTransformPoint(WorldXform, &XfStart, &StartLocal);

    // Six inlined TransformXform calls through DAT_00674f50 (re-acquired before each).
    FVector Sum;
    FVector XfEndMid;                                      // +0xe0
    FVector XfMid;                                         // +0xc8
    FVector XfNearPosXMid;                                 // +0xf8
    FVector XfNearNegXMid;                                 // +0x110
    FVector XfNearPosYMid;                                 // +0xa4
    FVector XfNearNegYMid;                                 // +0x30
    VecAdd(&Sum, &EndLocal, &MidLocal);
    EffectMath()->TransformXform(XfEndMid, *WorldXform, Sum);
    EffectMath()->TransformXform(XfMid, *WorldXform, MidLocal);
    VecAdd(&Sum, &NearX, &MidLocal);
    EffectMath()->TransformXform(XfNearPosXMid, *WorldXform, Sum);
    VecNegate(&Tmp, &NearX);
    VecAdd(&Sum, &Tmp, &MidLocal);
    EffectMath()->TransformXform(XfNearNegXMid, *WorldXform, Sum);
    VecAdd(&Sum, &NearY, &MidLocal);
    EffectMath()->TransformXform(XfNearPosYMid, *WorldXform, Sum);
    VecNegate(&Tmp, &NearY);
    VecAdd(&Sum, &Tmp, &MidLocal);
    EffectMath()->TransformXform(XfNearNegYMid, *WorldXform, Sum);

    // head_texture (+0x08) == 1 -> V {0, 0.5}, else {0.5, 1}, and the midpoint (in double);
    // trail_texture (+0x0c) == 1 -> {0, 0.45}, else {0.55, 1}.
    float UvV0;                                            // +0x24
    float UvV1;                                            // +0x14
    if (Arch->UvModeV == 1)
    {
        UvV0 = 0.0f;
        UvV1 = kHalf;
    }
    else
    {
        UvV0 = kHalf;
        UvV1 = kOne;
    }
    float const UvVMid = static_cast<float>((static_cast<double>(UvV0) + UvV1) * 0.5);

    float UvU0;                                            // +0x20
    float UvU1;                                            // +0x28
    if (Arch->UvModeU == 1)
    {
        UvU0 = 0.0f;
        UvU1 = kUvUAlt1;
    }
    else
    {
        UvU0 = kUvUAlt0;
        UvU1 = kOne;
    }

    // head_brightness and trail_brightness times the intensity.
    float const Scale0 = Intensity * Arch->IntensityScale0;   // +0x2c
    float const Scale1 = Intensity * Arch->IntensityScale1;   // +0x10

    // Tip cross, head_brightness. Retail inlines the first eight appends (FUN_0046eab0).
    SetBatchColorRgb(Batch, Scale0, Arch->ColorOuterR, Arch->ColorOuterG, Arch->ColorOuterB);
    Batch->U = kUvCore;
    Batch->V = UvVMid;
    unsigned short const I0 =                              // +0x90
        static_cast<unsigned short>(EffectVertexBatchAppend(Batch, WorldT.X, WorldT.Y, WorldT.Z));

    SetBatchColorRgb(Batch, Scale0, Arch->ColorTipR, Arch->ColorTipG, Arch->ColorTipB);
    Batch->U = kHalf;
    Batch->V = UvV0;
    unsigned short const I1 = static_cast<unsigned short>( // +0x98
        EffectVertexBatchAppend(Batch, XfTipPosX.X, XfTipPosX.Y, XfTipPosX.Z));
    // V is read from +0x24 before that slot is reused for the position -- still V0.
    Batch->U = kOne;
    Batch->V = UvV0;
    unsigned short const I2 = static_cast<unsigned short>( // +0x2c
        EffectVertexBatchAppend(Batch, XfTipPosY.X, XfTipPosY.Y, XfTipPosY.Z));
    Batch->U = kOne;
    Batch->V = UvV1;
    unsigned short const I3 = static_cast<unsigned short>( // EBP
        EffectVertexBatchAppend(Batch, XfTipNegX.X, XfTipNegX.Y, XfTipNegX.Z));
    Batch->U = kHalf;
    Batch->V = UvV1;
    unsigned short const I4 = static_cast<unsigned short>( // +0x84
        EffectVertexBatchAppend(Batch, XfTipNegY.X, XfTipNegY.Y, XfTipNegY.Z));

    // Tube, trail_brightness.
    SetBatchColorRgb(Batch, Scale1, Arch->ColorOuterR, Arch->ColorOuterG, Arch->ColorOuterB);
    Batch->U = 0.0f;
    Batch->V = UvU1;
    unsigned short const A0 =                              // +0x8c
        static_cast<unsigned short>(EffectVertexBatchAppend(Batch, WorldT.X, WorldT.Y, WorldT.Z));

    SetBatchColorRgb(Batch, Scale1, Arch->ColorCoreR, Arch->ColorCoreG, Arch->ColorCoreB);
    Batch->U = kUvMid;
    Batch->V = UvU0;
    unsigned short const A1 = static_cast<unsigned short>( // +0x88
        EffectVertexBatchAppend(Batch, XfStart.X, XfStart.Y, XfStart.Z));

    // outter_color, U 0.25, the second trail V: the NearR ring.
    SetBatchColorRgb(Batch, Scale1, Arch->ColorTipR, Arch->ColorTipG, Arch->ColorTipB);
    Batch->U = kUvMid;
    Batch->V = UvU1;
    unsigned short const A2 = static_cast<unsigned short>( // +0x1c
        EffectVertexBatchAppend(Batch, XfNearPosX.X, XfNearPosX.Y, XfNearPosX.Z));
    unsigned short const B0 = static_cast<unsigned short>( // +0x18
        EffectVertexBatchAppend(Batch, XfNearNegX.X, XfNearNegX.Y, XfNearNegX.Z));
    unsigned short const B1 = static_cast<unsigned short>( // +0x14
        EffectVertexBatchAppend(Batch, XfNearPosY.X, XfNearPosY.Y, XfNearPosY.Z));
    unsigned short const B2 = static_cast<unsigned short>( // +0x24
        EffectVertexBatchAppend(Batch, XfNearNegY.X, XfNearNegY.Y, XfNearNegY.Z));

    // sec_core_color, U 0, the first trail V: Mid.
    SetBatchColorRgb(Batch, Scale1, Arch->ColorBand1R, Arch->ColorBand1G, Arch->ColorBand1B);
    Batch->U = 0.0f;
    Batch->V = UvU0;
    unsigned short const B3 = static_cast<unsigned short>( // +0xa0
        EffectVertexBatchAppend(Batch, XfMid.X, XfMid.Y, XfMid.Z));

    // tail_color, U 0.25, the second trail V: Mid + End.
    SetBatchColorRgb(Batch, Scale1, Arch->ColorMidR, Arch->ColorMidG, Arch->ColorMidB);
    Batch->U = kUvMid;
    Batch->V = UvU1;
    unsigned short const B4 = static_cast<unsigned short>( // +0x94 (stored after SUB ESP,0xc)
        EffectVertexBatchAppend(Batch, XfEndMid.X, XfEndMid.Y, XfEndMid.Z));

    // FUN_004fc950 with the float-rounded products trail_brightness * sec_outter_color, U 0.25,
    // the first trail V: the ring pushed forward to Mid.
    EffectVertexBatchSetColor(Batch, Scale1 * Arch->ColorBand2R, Scale1 * Arch->ColorBand2G,
                              Scale1 * Arch->ColorBand2B);
    Batch->V = UvU0;
    Batch->U = kUvMid;
    unsigned short const C0 = static_cast<unsigned short>( // +0x20
        EffectVertexBatchAppend(Batch, XfNearPosXMid.X, XfNearPosXMid.Y, XfNearPosXMid.Z));
    unsigned short const C1 = static_cast<unsigned short>( // +0x10
        EffectVertexBatchAppend(Batch, XfNearNegXMid.X, XfNearNegXMid.Y, XfNearNegXMid.Z));
    unsigned short const C2 = static_cast<unsigned short>( // +0x9c
        EffectVertexBatchAppend(Batch, XfNearPosYMid.X, XfNearPosYMid.Y, XfNearPosYMid.Z));
    unsigned short const C3 = static_cast<unsigned short>( // +0x28 (stored after three PUSHes)
        EffectVertexBatchAppend(Batch, XfNearNegYMid.X, XfNearNegYMid.Y, XfNearNegYMid.Z));

    EffectVertexBatchPushTriangle(Batch, I0, I1, I2);
    EffectVertexBatchPushTriangle(Batch, I0, I3, I2);
    EffectVertexBatchPushTriangle(Batch, I0, I3, I4);
    EffectVertexBatchPushTriangle(Batch, I0, I1, I4);

    EffectVertexBatchPushTriangle(Batch, A0, A1, A2);
    EffectVertexBatchPushTriangle(Batch, A0, A1, B0);
    EffectVertexBatchPushTriangle(Batch, A0, A1, B1);
    EffectVertexBatchPushTriangle(Batch, A0, A1, B2);

    EffectVertexBatchPushQuad(Batch, A0, B3, C0, A2);
    EffectVertexBatchPushQuad(Batch, A0, B3, C1, B0);
    EffectVertexBatchPushQuad(Batch, A0, B3, C2, B1);
    EffectVertexBatchPushQuad(Batch, A0, B3, C3, B2);

    EffectVertexBatchPushTriangle(Batch, B3, B4, C0);
    EffectVertexBatchPushTriangle(Batch, B3, B4, C1);
    EffectVertexBatchPushTriangle(Batch, B3, B4, C2);
    EffectVertexBatchPushTriangle(Batch, B3, B4, C3);
    return 1;
}
