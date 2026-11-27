// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Asteroid-field billboards -- retail Client\Space\SpaceTerrainRenderComp.cpp.

#include "Space/asteroid_billboard.h"

#include "Common/engine_light.h"
#include "Core/game_options.h"
#include "Common/Graphics/part_cache.h"
#include "Space/Cameras/space_camera.h"
#include "Space/space_scene_obj_draw.h"
#include "Common/Graphics/texture_shapes.h"

#include "DALib.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_net.h"
#include "common_universe.h"
#include "dacom.h"
#include "render/IRenderPipeline.h"

#include <math.h>
#include <string.h>

namespace
{
    char const kSpaceTerrainRenderCompCpp[] =
        "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp";

    // DAT_006140a8 / DAT_006140ac (.data, never written): the length of the side offset added
    // to Forward, and the wrap term of the light weight.
    float const kLightSideOffset = 1.0f;
    float const kLightWrap = 0.5f;

    // {state, value} tables, zero-terminated (FUN_0054f800 and its inlined copies), values read
    // out of the retail .rdata.
    // DAT_005e09f0 -- texture stage 0.
    unsigned const kStage0States[] = {D3DTSS_COLOROP, D3DTOP_MODULATE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
                                      D3DTSS_COLORARG2, D3DTA_DIFFUSE, D3DTSS_ALPHAOP, D3DTOP_MODULATE,
                                      D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE,
                                      0, 0};
    // DAT_005e0a28 -- texture stage 1.
    unsigned const kStage1States[] = {D3DTSS_COLOROP, D3DTOP_DISABLE, D3DTSS_COLORARG1, D3DTA_TEXTURE,
                                      D3DTSS_COLORARG2, D3DTA_CURRENT, D3DTSS_ALPHAOP, D3DTOP_DISABLE,
                                      D3DTSS_ALPHAARG1, D3DTA_TEXTURE, D3DTSS_ALPHAARG2, D3DTA_CURRENT,
                                      0, 0};
    // DAT_005e09c8.
    unsigned const kRenderStatesMaterial[] = {D3DRS_COLORVERTEX, 1, D3DRS_DIFFUSEMATERIALSOURCE,
                                              D3DMCS_COLOR1, 0, 0};
    // DAT_005e0978.
    unsigned const kRenderStatesOpaque[] = {D3DRS_LIGHTING, 0, D3DRS_SPECULARENABLE, 0,
                                            D3DRS_ALPHABLENDENABLE, 0, 0, 0};
    // DAT_005e0998.
    unsigned const kRenderStatesBlend[] = {D3DRS_LIGHTING, 0, D3DRS_SPECULARENABLE, 0,
                                           D3DRS_ALPHABLENDENABLE, 1, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA,
                                           D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, 0, 0};
    // DAT_005e09e0.
    unsigned const kRenderStatesMaterialOff[] = {D3DRS_COLORVERTEX, 0, 0, 0};
    // DAT_005e0958.
    unsigned const kRenderStatesRestore[] = {D3DRS_LIGHTING, 1, D3DRS_SPECULARENABLE, 0,
                                             D3DRS_ALPHABLENDENABLE, 0, 0, 0};

    // The SetTextureStageState walk FUN_0054e900 inlines (stage 0 and stage 1).
    void ApplyTextureStageStates(IRenderPipeline* Pipeline, int Stage, unsigned const* Table)
    {
        unsigned const* Entry = Table;
        while (Entry[0] != 0)
        {
            Pipeline->SetTextureStageState(Stage, static_cast<int>(Entry[0]),
                                           static_cast<int>(Entry[1]));
            Entry += 2;
        }
    }

    // FUN_00411350: Out = -In.
    void Negate3(float* Out, float const* In)
    {
        Out[0] = -In[0];
        Out[1] = -In[1];
        Out[2] = -In[2];
    }

    // FUN_00411d60: three products, each stored as a float.
    void Scale3(float* Out, float const* V, float Scalar)
    {
        float const Z = Scalar * V[2];
        float const Y = Scalar * V[1];
        float const X = Scalar * V[0];
        Out[0] = X;
        Out[1] = Y;
        Out[2] = Z;
    }

    // FUN_00423b30: Out = A + B.
    void Add3(float* Out, float const* A, float const* B)
    {
        float const Z = A[2] + B[2];
        float const Y = A[1] + B[1];
        float const X = A[0] + B[0];
        Out[0] = X;
        Out[1] = Y;
        Out[2] = Z;
    }

    // FUN_004d7270 (Vector::operator+=).
    void AddInPlace3(float* This, float const* Other)
    {
        This[0] = Other[0] + This[0];
        This[1] = Other[1] + This[1];
        This[2] = Other[2] + This[2];
    }

    // FUN_0041a390: (x*x + y*y) + z*z, the reciprocal length kept unrounded, each product
    // stored as a float.
    void Normalise3(float* V)
    {
        double const Len = sqrt((static_cast<double>(V[0]) * V[0] +
                                 static_cast<double>(V[1]) * V[1]) +
                                static_cast<double>(V[2]) * V[2]);
        double const Inv = 1.0 / Len;
        V[0] = static_cast<float>(Inv * V[0]);
        V[1] = static_cast<float>(Inv * V[1]);
        V[2] = static_cast<float>(Inv * V[2]);
    }

    // FUN_004cbf40: (a.z*b.z + a.y*b.y) + a.x*b.x, returned unrounded.
    double Dot3(float const* A, float const* B)
    {
        double const Sum = static_cast<double>(A[2]) * B[2] + static_cast<double>(A[1]) * B[1];
        return Sum + static_cast<double>(A[0]) * B[0];
    }

    // The reciprocal square root FUN_0054e900 inlines twice: magic 0x5f3759df minus the bits
    // shifted right (arithmetic), one Newton step y * (1.5 - y*y*LenSq*0.5) (DAT_005ca220 = 1.5,
    // DAT_005c75e0 = 0.5). The first site rounds the result to float before using it, the
    // second multiplies with the unrounded value -- hence the double, and a name distinct from
    // common_misc.h's FastInvSqrt, which rounds its result to float.
    double BillboardInvSqrt(float LenSq)
    {
        int Bits = 0;
        memcpy(&Bits, &LenSq, sizeof(Bits));
        int const Guess = 0x5f3759df - (Bits >> 1);
        float Y = 0.0f;
        memcpy(&Y, &Guess, sizeof(Y));
        double const Step = 1.5 - static_cast<double>(Y) * Y * LenSq * 0.5;
        return Step * Y;
    }

    // FUN_0054e130's ambient/light weight: 0 below -Wrap, otherwise
    // (Dot + Wrap) / (Wrap + 1.0), stored as a float.
    float LightWeight(double Dot)
    {
        if (Dot < -static_cast<double>(kLightWrap))
            return 0.0f;
        return static_cast<float>((Dot + kLightWrap) / (static_cast<double>(kLightWrap) + 1.0f));
    }

    // One of FUN_0054e130's five directions: normalise(Forward + Side * kLightSideOffset), its
    // weight against the light direction, then Out += Ambient and Out += Weight * Color.
    void AccumulateDirection(float* Out, float const* Forward, float const* Side,
                             float const* LightDir, float const* Ambient, float const* Color)
    {
        float Offset[3];
        Scale3(Offset, Side, kLightSideOffset);
        float Dir[3];
        Add3(Dir, Forward, Offset);
        Normalise3(Dir);
        float const Weight = LightWeight(Dot3(Dir, LightDir));
        AddInPlace3(Out, Ambient);
        float Lit[3];
        Scale3(Lit, Color, Weight);
        AddInPlace3(Out, Lit);
    }

    // FUN_00521fa0's body, which FUN_0054e130 inlines on four outputs: 1.0 when not below 1.0
    // (NaN included). The fifth goes through FUN_00521fa0 itself (CliAsteroidClampColorToOne).
    void ClampToOne3(float* V)
    {
        if (!(V[0] < 1.0f))
            V[0] = 1.0f;
        if (!(V[1] < 1.0f))
            V[1] = 1.0f;
        if (!(V[2] < 1.0f))
            V[2] = 1.0f;
    }

    // Colour pack of FUN_0054f9f0: ftol of each component * 255 (DAT_005c75d8), alpha shifted
    // up unmasked, the others masked.
    int Ftol255(float Value)
    {
        // The product is truncated unrounded.
        return static_cast<int>(static_cast<double>(Value) * 255.0);
    }

    unsigned PackColor(float Alpha, float const* Rgb)
    {
        unsigned Packed = static_cast<unsigned>(Ftol255(Alpha));
        Packed = (Packed << 8) | (static_cast<unsigned>(Ftol255(Rgb[0])) & 0xffu);
        Packed = (Packed << 8) | (static_cast<unsigned>(Ftol255(Rgb[1])) & 0xffu);
        Packed = (Packed << 8) | (static_cast<unsigned>(Ftol255(Rgb[2])) & 0xffu);
        return Packed;
    }

    // Position / normal / colour sizes FUN_0054f830 derives from an FVF.
    int FvfPositionBytes(unsigned Fvf)
    {
        if ((Fvf & D3DFVF_XYZB4) == D3DFVF_XYZB4)
            return 0x1c;
        if ((Fvf & D3DFVF_XYZRHW) == D3DFVF_XYZRHW)
            return 0x10;
        if ((Fvf & D3DFVF_XYZ) == D3DFVF_XYZ)
            return 0xc;
        return 0;
    }

    int FvfNormalBytes(unsigned Fvf)
    {
        return (Fvf & D3DFVF_NORMAL) != 0 ? 0xc : 0;
    }

    int FvfColorBytes(unsigned Fvf)
    {
        int Bytes = (Fvf & D3DFVF_DIFFUSE) != 0 ? 4 : 0;
        if ((Fvf & D3DFVF_SPECULAR) != 0)
            Bytes += 4;
        return Bytes;
    }

    // The "closing vertex" FUN_0054e900 inlines in both passes: with flag bit 1, the saved
    // first vertex written once more.
    void BatchCloseLoop(FAsteroidBillboardBatch& Batch)
    {
        if ((Batch.Flags & kBillboardFlagCloseLoop) == 0)
            return;
        float* const Pos = reinterpret_cast<float*>(Batch.PosCursor.Ptr);
        Pos[0] = Batch.Saved[0].Pos.x;
        Pos[1] = Batch.Saved[0].Pos.y;
        Pos[2] = Batch.Saved[0].Pos.z;
        Batch.PosCursor.Advance(0);
        *reinterpret_cast<unsigned*>(Batch.ColorCursor.Ptr) = Batch.Saved[0].Color;
        Batch.ColorCursor.Advance(0);
        float* const Uv = reinterpret_cast<float*>(Batch.UvCursor.Ptr);
        Uv[0] = Batch.Saved[0].U;
        Uv[1] = Batch.Saved[0].V;
        Batch.UvCursor.Advance(0);
        ++Batch.Count;
    }

    // The batch begin FUN_0054e900 inlines in both passes: flag bits 1 and 2 cleared, bit 0
    // set from bit 1 (the compiler's bitfield copy), the triangle-list primitive.
    void BatchBeginTriangles(FAsteroidBillboardBatch& Batch)
    {
        unsigned char Flags = static_cast<unsigned char>(Batch.Flags & ~(kBillboardFlagCloseLoop | kBillboardFlagQuads));
        unsigned char const Bit0 = static_cast<unsigned char>(((Flags >> 1) ^ Flags) & 1u);
        Flags = static_cast<unsigned char>(Flags ^ Bit0);
        Batch.Prim = D3DPT_TRIANGLELIST;
        Batch.Flags = Flags;
    }

    // The batch draw FUN_0054e900 inlines in both passes: unlock, then with vertices written
    // the closing vertex and DrawPrimitiveVb.
    void BatchFlush(FAsteroidBillboardBatch& Batch)
    {
        Batch.VbMgr->UnlockVertexMemory(&Batch.Lock);
        if (Batch.Count == 0)
            return;
        BatchCloseLoop(Batch);
        Batch.Pipeline->DrawPrimitiveVb(Batch.Prim, Batch.Lock.Listing.VertexBuffer,
                                        static_cast<unsigned int>(Batch.Lock.Listing.BaseVertex),
                                        static_cast<unsigned int>(Batch.Count), nullptr);
    }

    // FUN_0054e900's two corner jump tables (0x0054f7e0 / 0x0054f7f0): Index 0..3 direct,
    // 4..7 minus 4, anything else case 0.
    float const* PickCorner(int Index, float const* Corner0, float const* Corner1,
                            float const* Corner2, float const* Corner3)
    {
        if (Index > 3)
        {
            Index -= 4;
            if (static_cast<unsigned>(Index) > 3u)
                Index = 0;
        }
        switch (Index)
        {
        case 1:
            return Corner1;
        case 2:
            return Corner2;
        case 3:
            return Corner3;
        default:
            return Corner0;
        }
    }
}

// ------------------------------------------------------------------ FBillboardVertexCursor

FBillboardVertexCursor::FBillboardVertexCursor(int InStride)
    : Ptr(nullptr)
    , Stride(InStride)
{
}

FBillboardVertexCursor::~FBillboardVertexCursor()
{
    Ptr = nullptr;
    Stride = 0;
}

void FBillboardVertexCursor::Advance(int /*Unused*/)
{
    Ptr += Stride;
}

// ------------------------------------------------------------------ FAsteroidBillboardBatch

static_assert(sizeof(FBillboardVertexCursor) == 8, "cursor is {ptr, stride}");
static_assert(sizeof(FAsteroidBillboardBatch) == 0x84, "DAT_006797d0 object is 0x84 bytes");
static_assert(offsetof(FAsteroidBillboardBatch, Lock) == 0x14, "batch lock out +0x14");
static_assert(offsetof(FAsteroidBillboardBatch, Saved) == 0x28, "batch saved vertices +0x28");
static_assert(offsetof(FAsteroidBillboardBatch, Flags) == 0x58, "batch flags +0x58");
static_assert(offsetof(FAsteroidBillboardBatch, Color) == 0x68, "batch colour +0x68");
static_assert(offsetof(FAsteroidBillboardBatch, PosCursor) == 0x6c, "batch cursors +0x6c");

// LAB_0054dad0: FUN_0054daf0(0x142), atexit(LAB_0054db50).
FAsteroidBillboardBatch G_AsteroidBillboardBatch(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

FAsteroidBillboardBatch::FAsteroidBillboardBatch(unsigned InFvf)
    : Prim(D3DPT_FORCE_DWORD)
    , Count(0)
    , Fvf(InFvf)
    , Pipeline(nullptr)
    , VbMgr(nullptr)
    , LockedCount(0)
    , Flags(0)
    , QuadCounter(0)
    , U(0.0f)
    , V(0.0f)
    , Color(0xffffffffu)
    , PosCursor(0xc)
    , ColorCursor(4)
    , UvCursor(8)
{
    // Retail does not write Lock and Saved; the static object starts zeroed.
    memset(&Lock, 0, sizeof(Lock));
    memset(Saved, 0, sizeof(Saved));
    memset(Pad59, 0, sizeof(Pad59));
}

FAsteroidBillboardBatch::~FAsteroidBillboardBatch()
{
    // Called through the LAB_0054db50 once-guard DAT_006797c8.
    if (Pipeline != nullptr)
    {
        Pipeline->Release();
        Pipeline = nullptr;
    }
    if (VbMgr != nullptr)
    {
        VbMgr->Release();
        VbMgr = nullptr;
    }
}

void FAsteroidBillboardBatch::SetInterfaces(IRenderPipeline* InPipeline,
                                            IVertexBufferManager* InVbMgr)
{
    if (Pipeline != nullptr)
    {
        Pipeline->Release();
        Pipeline = nullptr;
    }
    if (VbMgr != nullptr)
    {
        VbMgr->Release();
        VbMgr = nullptr;
    }
    if (InPipeline != nullptr)
    {
        Pipeline = InPipeline;
        InPipeline->AddRef();
    }
    if (InVbMgr != nullptr)
    {
        VbMgr = InVbMgr;
        InVbMgr->AddRef();
    }
}

void FAsteroidBillboardBatch::Reset()
{
    LockedCount = 0;
    Count = 0;
    QuadCounter = 0;
    Flags = static_cast<unsigned char>(Flags & ~(kBillboardFlagSaveFirst | kBillboardFlagCloseLoop | kBillboardFlagQuads));
    Color = 0xffffffffu;
    V = 0.0f;
    U = 0.0f;
    Prim = D3DPT_FORCE_DWORD;
}

void FAsteroidBillboardBatch::BindCursors(unsigned char* Base, unsigned InFvf)
{
    unsigned const OwnFvf = static_cast<unsigned>(Lock.Listing.Fvf);
    int const VertexStride = FvfTexcoordBlockBytes(OwnFvf) + FvfColorBytes(OwnFvf) +
                             FvfNormalBytes(OwnFvf) + FvfPositionBytes(OwnFvf);

    PosCursor.Ptr = Base;
    PosCursor.Stride = VertexStride;

    ColorCursor.Ptr = Base + FvfNormalBytes(InFvf) + FvfPositionBytes(InFvf);
    ColorCursor.Stride = VertexStride;

    UvCursor.Ptr =
        Base + FvfColorBytes(InFvf) + FvfNormalBytes(InFvf) + FvfPositionBytes(InFvf);
    UvCursor.Stride = VertexStride;
}

void FAsteroidBillboardBatch::Vertex(float X, float Y, float Z)
{
    ++Count;
    float* const Pos = reinterpret_cast<float*>(PosCursor.Ptr);
    Pos[0] = X;
    Pos[1] = Y;
    Pos[2] = Z;
    PosCursor.Ptr += PosCursor.Stride;
    *reinterpret_cast<unsigned*>(ColorCursor.Ptr) = Color;
    ColorCursor.Ptr += ColorCursor.Stride;
    float* const Uv = reinterpret_cast<float*>(UvCursor.Ptr);
    Uv[0] = U;
    Uv[1] = V;
    UvCursor.Ptr += UvCursor.Stride;

    // Bit 0: keep this vertex as the loop's first.
    if ((Flags & kBillboardFlagSaveFirst) != 0)
    {
        Flags = static_cast<unsigned char>(Flags & ~kBillboardFlagSaveFirst);
        Saved[0].Color = Color;
        Saved[0].U = U;
        Saved[0].V = V;
        Saved[0].Pos.x = X;
        Saved[0].Pos.y = Y;
        Saved[0].Pos.z = Z;
    }

    // Bit 2: quads. Vertices 0 and 2 of each four are saved; after the fourth
    // both are written again, turning the quad into two triangles.
    if ((Flags & kBillboardFlagQuads) == 0)
        return;
    if ((QuadCounter & 1u) == 0)
    {
        FBillboardSavedVertex& Slot = Saved[QuadCounter >> 1];
        Slot.Color = Color;
        Slot.U = U;
        Slot.V = V;
        Slot.Pos.x = X;
        Slot.Pos.y = Y;
        Slot.Pos.z = Z;
    }
    ++QuadCounter;
    if (QuadCounter < 4)
        return;
    QuadCounter = 0;
    int Index = 0;
    for (Index = 0; Index < 2; ++Index)
    {
        float* const OutPos = reinterpret_cast<float*>(PosCursor.Ptr);
        OutPos[0] = Saved[Index].Pos.x;
        OutPos[1] = Saved[Index].Pos.y;
        OutPos[2] = Saved[Index].Pos.z;
        PosCursor.Ptr += PosCursor.Stride;
        *reinterpret_cast<unsigned*>(ColorCursor.Ptr) = Saved[Index].Color;
        ColorCursor.Ptr += ColorCursor.Stride;
        float* const OutUv = reinterpret_cast<float*>(UvCursor.Ptr);
        OutUv[0] = Saved[Index].U;
        OutUv[1] = Saved[Index].V;
        UvCursor.Ptr += UvCursor.Stride;
    }
    Count += 2;
}

// ------------------------------------------------------------------ free functions

void BillboardApplyRenderStates(IRenderPipeline* Pipeline, unsigned const* Table)
{
    unsigned const* Entry = Table;
    while (Entry[0] != 0)
    {
        Pipeline->SetRenderState((D3DRENDERSTATETYPE)Entry[0], static_cast<int>(Entry[1]));
        Entry += 2;
    }
}

void FAsteroidBillboardBlock::ComputeLighting(float const* Pos, float const* Forward,
                                              float const* Up, float const* Right, float* OutCenter,
                                              float* OutCorner0, float* OutCorner1,
                                              float* OutCorner2, float* OutCorner3)
{
    FEngineLight* Slots[16];
    int const LightCount = SpaceCollectNearbyLights(Slots, 0x10, Pos, 100.0f, 0);

    // D3DRS_AMBIENT bytes as floats into OutCorner0.
    unsigned char Ambient[3];
    EngineLightGetAmbientRgbBytes(Ambient);
    OutCorner0[0] = static_cast<float>(Ambient[0]);
    OutCorner0[1] = static_cast<float>(Ambient[1]);
    OutCorner0[2] = static_cast<float>(Ambient[2]);

    // ambient_intensity * 1/255 (DAT_005c7f68).
    float const AmbientScale = AmbientIntensity * 0.003921568859368563f;
    OutCorner0[0] = AmbientScale * OutCorner0[0];
    OutCorner0[1] = AmbientScale * OutCorner0[1];
    OutCorner0[2] = AmbientScale * OutCorner0[2];

    // Times the tinted ambient (+0x58), then times color_shift (+0x64), each through a
    // temporary (FUN_00422b80).
    float const Ax = OutCorner0[0] * TintedAmbient0;
    float const Ay = OutCorner0[1] * TintedAmbient1;
    float const Az = OutCorner0[2] * TintedAmbient2;
    OutCorner0[0] = Ax;
    OutCorner0[1] = Ay;
    OutCorner0[2] = Az;
    float const Sx = OutCorner0[0] * ColorShift0;
    float const Sy = OutCorner0[1] * ColorShift1;
    float const Sz = OutCorner0[2] * ColorShift2;
    OutCorner0[0] = Sx;
    OutCorner0[1] = Sy;
    OutCorner0[2] = Sz;

    int Axis = 0;
    for (Axis = 0; Axis < 3; ++Axis)
    {
        OutCorner3[Axis] = OutCorner0[Axis];
        OutCorner2[Axis] = OutCorner0[Axis];
        OutCorner1[Axis] = OutCorner0[Axis];
        OutCenter[Axis] = OutCorner0[Axis];
    }

    // Deviation: retail's direction and colour locals are not initialised, so a light that
    // FEngineLight::Sample rejects (not enabled) leaves the previous light's values -- or stack
    // garbage for the first one -- in them. They start at zero here.
    float LightDir[3] = {0.0f, 0.0f, 0.0f};
    float LightColor[3] = {0.0f, 0.0f, 0.0f};
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    int Lit = 0;
    int SlotIndex = 0;
    for (SlotIndex = 0; SlotIndex < LightCount; ++SlotIndex)
    {
        if (Lit >= MaxLights)
            break;
        FEngineLight* const Light = Slots[SlotIndex];
        // Only directional lights (+0xb0 == 3).
        if (Light->Type != kLightDirectional)
            continue;

        float LightAmbient[3] = {0.0f, 0.0f, 0.0f};
        Light->Sample(Pos, LightDir, LightAmbient, LightColor);

        // The colour times the tinted diffuse (+0x4c).
        float const Cx = LightColor[0] * TintedDiffuse0;
        float const Cy = LightColor[1] * TintedDiffuse1;
        float const Cz = LightColor[2] * TintedDiffuse2;
        LightColor[0] = Cx;
        LightColor[1] = Cy;
        LightColor[2] = Cz;

        AccumulateDirection(OutCorner0, Forward, Right, LightDir, LightAmbient, LightColor);
        float NegRight[3];
        Negate3(NegRight, Right);
        AccumulateDirection(OutCorner1, Forward, NegRight, LightDir, LightAmbient, LightColor);
        AccumulateDirection(OutCorner2, Forward, Up, LightDir, LightAmbient, LightColor);
        float NegUp[3];
        Negate3(NegUp, Up);
        AccumulateDirection(OutCorner3, Forward, NegUp, LightDir, LightAmbient, LightColor);
        // Forward + 0 -> the centre (retail writes the last add inline).
        AccumulateDirection(OutCenter, Forward, Zero, LightDir, LightAmbient, LightColor);

        ++Lit;
    }

    ClampToOne3(OutCorner0);
    ClampToOne3(OutCorner1);
    ClampToOne3(OutCorner2);
    ClampToOne3(OutCorner3);
    CliAsteroidClampColorToOne(OutCenter);
}

void BillboardEmitRock(FAsteroidBillboardBlock* /*Bb*/, FAsteroidBillboardRock* Rock,
                       float const* Right, float const* Up, float const* CornerColor,
                       float const* CenterColor)
{
    float const Size = Rock->HalfSize + Rock->HalfSize;
    float RightScaled[3];
    RightScaled[0] = Right[0] * Size;
    RightScaled[1] = Right[1] * Size;
    RightScaled[2] = Right[2] * Size;
    float UpScaled[3];
    UpScaled[0] = Up[0] * Size;
    UpScaled[1] = Up[1] * Size;
    UpScaled[2] = Up[2] * Size;

    FAsteroidBillboardBatch& Batch = G_AsteroidBillboardBatch;

    Batch.Color = PackColor(Rock->Alpha, CornerColor);

    // The base UV pair from FlipU + 2 * FlipV.
    float U0 = 0.0f;
    float V0 = 0.0f;
    float U1 = 0.0f;
    float V1 = 0.0f;
    int const Corner = static_cast<int>(Rock->bFlipU) + static_cast<int>(Rock->bFlipV) * 2;
    if (Corner == 0)
    {
        V0 = 0.0f;
        U1 = 1.0f;
        U0 = 0.0f;
        V1 = 0.0f;
    }
    else if (Corner == 1)
    {
        U0 = 1.0f;
        V0 = 0.0f;
        U1 = 1.0f;
        V1 = 1.0f;
    }
    else
    {
        // Anything other than 0..2 shares corner 0's U0 / V1 tail.
        U1 = 0.0f;
        V0 = 1.0f;
        if (Corner == 2)
        {
            U0 = 1.0f;
            V1 = 1.0f;
        }
        else
        {
            U0 = 0.0f;
            V1 = 0.0f;
        }
    }

    // A spans the base, B reaches the apex.
    float A[3];
    float B[3];
    if (Rock->bSwapAxes != 0)
    {
        A[0] = RightScaled[0];
        A[1] = RightScaled[1];
        A[2] = RightScaled[2];
        B[0] = UpScaled[0];
        B[1] = UpScaled[1];
        B[2] = UpScaled[2];
    }
    else
    {
        A[0] = UpScaled[0];
        A[1] = UpScaled[1];
        A[2] = UpScaled[2];
        B[0] = RightScaled[0];
        B[1] = RightScaled[1];
        B[2] = RightScaled[2];
    }
    // B times -1.0 (DAT_005c7500).
    if (Rock->bNegateApex != 0)
    {
        B[0] = B[0] * -1.0f;
        B[1] = B[1] * -1.0f;
        B[2] = B[2] * -1.0f;
    }

    float const BaseLow[3] = {Rock->Pos[0] - A[0], Rock->Pos[1] - A[1], Rock->Pos[2] - A[2]};
    float const BaseHigh[3] = {A[0] + Rock->Pos[0], A[1] + Rock->Pos[1], A[2] + Rock->Pos[2]};
    float const Apex[3] = {B[0] + Rock->Pos[0], B[1] + Rock->Pos[1], B[2] + Rock->Pos[2]};

    Batch.U = U0;
    Batch.V = V0;
    Batch.Vertex(BaseLow[0], BaseLow[1], BaseLow[2]);
    Batch.U = U1;
    Batch.V = V1;
    Batch.Vertex(BaseHigh[0], BaseHigh[1], BaseHigh[2]);

    // The apex in the centre colour.
    Batch.Color = PackColor(Rock->Alpha, CenterColor);
    Batch.U = 0.5f;
    Batch.V = 0.5f;
    Batch.Vertex(Apex[0], Apex[1], Apex[2]);
}

void FAsteroidBillboardBlock::Draw(FSpaceCamera* Camera, Exclusion::AsteroidExclusionZoneMgr* Excl,
                                   int ExcludeType)
{
    ICamera* const Cam = AsCamera(Camera);

    const Vector* const CamPosSource = Cam->GetPosition();
    float CamPos[3] = {CamPosSource->x, CamPosSource->y, CamPosSource->z};

    // Forward = -(orientation column 2).
    const Matrix* const Orient = Cam->GetOrientation();
    float Forward[3];
    Forward[0] = -Orient->E[0][2];
    Forward[1] = -Orient->E[1][2];
    Forward[2] = -Orient->E[2][2];

    // The centre of the rock sphere, SphereAhead ahead of the camera.
    float FarCenter[3];
    FarCenter[0] = static_cast<float>(static_cast<double>(Forward[0]) * SphereAhead + CamPos[0]);
    FarCenter[1] = static_cast<float>(static_cast<double>(Forward[1]) * SphereAhead + CamPos[1]);
    FarCenter[2] = static_cast<float>(static_cast<double>(Forward[2]) * SphereAhead + CamPos[2]);

    // Right = normalise(Forward x Up).
    float* const Up = this->Up;
    double const CrossX =
        static_cast<double>(Forward[1]) * Up[2] - static_cast<double>(Forward[2]) * Up[1];
    double const CrossY =
        static_cast<double>(Forward[2]) * Up[0] - static_cast<double>(Forward[0]) * Up[2];
    float const CrossZ = static_cast<float>(static_cast<double>(Forward[0]) * Up[1] -
                                            static_cast<double>(Forward[1]) * Up[0]);
    float const RightLenSq = static_cast<float>(
        (CrossX * CrossX + static_cast<double>(CrossZ) * CrossZ) + CrossY * CrossY);
    float const RightInv = static_cast<float>(BillboardInvSqrt(RightLenSq));
    float Right[3];
    Right[0] = static_cast<float>(CrossX * RightInv);
    Right[1] = static_cast<float>(CrossY * RightInv);
    Right[2] = CrossZ * RightInv;

    // Up = normalise(Right x Forward), written back into the block.
    Up[0] = static_cast<float>(static_cast<double>(Right[1]) * Forward[2] -
                               static_cast<double>(Right[2]) * Forward[1]);
    Up[1] = static_cast<float>(static_cast<double>(Right[2]) * Forward[0] -
                               static_cast<double>(Forward[2]) * Right[0]);
    Up[2] = static_cast<float>(static_cast<double>(Forward[1]) * Right[0] -
                               static_cast<double>(Right[1]) * Forward[0]);
    float const UpLenSq = static_cast<float>(
        (static_cast<double>(Up[0]) * Up[0] + static_cast<double>(Up[1]) * Up[1]) +
        static_cast<double>(Up[2]) * Up[2]);
    double const UpInv = BillboardInvSqrt(UpLenSq);
    Up[0] = static_cast<float>(UpInv * Up[0]);
    Up[1] = static_cast<float>(UpInv * Up[1]);
    Up[2] = static_cast<float>(UpInv * Up[2]);

    // The five colours: lit, or the plain tinted diffuse (+0x4c).
    float CenterColor[3];
    float Corner0[3];
    float Corner1[3];
    float Corner2[3];
    float Corner3[3];
    if (GetGameOption(kGameOptLighting) != 0)
    {
        ComputeLighting(CamPos, Forward, this->Up, Right, CenterColor, Corner0,
                        Corner1, Corner2, Corner3);
    }
    else
    {
        float const Diffuse[3] = {TintedDiffuse0, TintedDiffuse1, TintedDiffuse2};
        int Axis = 0;
        for (Axis = 0; Axis < 3; ++Axis)
        {
            Corner3[Axis] = Diffuse[Axis];
            Corner2[Axis] = Diffuse[Axis];
            Corner1[Axis] = Diffuse[Axis];
            Corner0[Axis] = Diffuse[Axis];
            CenterColor[Axis] = Diffuse[Axis];
        }
    }

    // No shape: nothing drawn and no state touched.
    if (Shape == nullptr)
        return;

    // All doubles. The camera byte +0xbc re-scatters every rock.
    double const RadiusSq = static_cast<double>(SphereRadius) * SphereRadius;
    unsigned char const bScatterAll = Camera->FlagBC;
    double const FadeSq = static_cast<double>(SphereFadeWidth) * SphereFadeWidth;
    double const InvFadeSq = 1.0 / FadeSq;
    double const InnerSq = RadiusSq - FadeSq;

    IRenderPipeline* const Pipe = DALib::Pipeline;
    Pipe->SetModelview(Cam->GetViewTransform());
    // CULLMODE none, ALPHAREF saved and set to 0x80.
    Pipe->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    int OldAlphaRef = 0;
    Pipe->GetRenderState(D3DRS_ALPHAREF, &OldAlphaRef);
    Pipe->SetRenderState(D3DRS_ALPHAREF, 0x80);
    // The shape's frame-0 texture on stage 0, TEXCOORDINDEX 0.
    Pipe->SetTextureStageTexture(0, Shape->GetGpuHandle(0));
    Pipe->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    ApplyTextureStageStates(Pipe, 0, kStage0States);
    ApplyTextureStageStates(Pipe, 1, kStage1States);
    BillboardApplyRenderStates(Pipe, kRenderStatesMaterial);
    BillboardApplyRenderStates(Pipe, kRenderStatesOpaque);

    // Retail inlines SetInterfaces and Reset here; three vertices per rock.
    FAsteroidBillboardBatch& Batch = G_AsteroidBillboardBatch;
    Batch.SetInterfaces(DALib::Pipeline, DALib::VertexBuffer);

    int Deferred = 0;
    int const VertexTotal = RockCount * 3;
    Batch.Reset();
    if (VertexTotal != 0)
    {
        BatchBeginTriangles(Batch);
        bool const bLocked = Batch.VbMgr->LockVertexMemory(
                                 Batch.Fvf, static_cast<unsigned int>(VertexTotal), &Batch.Lock) == 0;
        if (bLocked)
        {
            Batch.LockedCount = VertexTotal;
            Batch.BindCursors((unsigned char*)Batch.Lock.Vertices,
                              static_cast<unsigned>(Batch.Lock.Listing.Fvf));

            // Place, fade and draw the opaque rocks.
            int RockIndex = 0;
            for (RockIndex = 0; RockIndex < RockCount; ++RockIndex)
            {
                FAsteroidBillboardRock* const Rock = &Rocks[RockIndex];
                Vector const& RockPos = *reinterpret_cast<Vector const*>(Rock->Pos);
                double DistSq = 0.0;

                if (bScatterAll != 0)
                {
                    // Anywhere inside the sphere.
                    Vector const Offset = random_inside_sphere(SphereRadius);
                    Rock->Pos[0] = static_cast<float>(static_cast<double>(FarCenter[0]) + Offset.x);
                    Rock->Pos[1] = static_cast<float>(static_cast<double>(FarCenter[1]) + Offset.y);
                    Rock->Pos[2] = static_cast<float>(static_cast<double>(FarCenter[2]) + Offset.z);

                    // Outside the zone (immersion exactly 0) -> hidden.
                    if (ZoneRef != nullptr && ZoneRef->GetImmersion(RockPos) == 0.0f)
                    {
                        Rock->bVisible = 0;
                        continue;
                    }
                    Rock->bVisible = 1;
                    if (Excl != nullptr)
                    {
                        float ExclusionDepth = -1.0f;
                        if (Excl->is_excluded(RockPos,
                                              static_cast<Exclusion::ExcludeObjectType>(ExcludeType),
                                              &ExclusionDepth))
                        {
                            Rock->bVisible = 0;
                            continue;
                        }
                        if (ExclusionDepth > 0.0f)
                        {
                            if (Rock->Phase > ExclusionDepth)
                                Rock->bVisible = 0;
                        }
                        else
                        {
                            if (Rock->Phase > DensityScale)
                                Rock->bVisible = 0;
                        }
                    }

                    double const Dx = static_cast<double>(Rock->Pos[0]) - FarCenter[0];
                    double const Dy = static_cast<double>(Rock->Pos[1]) - FarCenter[1];
                    double const Dz = static_cast<double>(Rock->Pos[2]) - FarCenter[2];
                    DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
                }
                else
                {
                    // Within RadiusSq + 10 (DAT_005c9ce8) the rock stays.
                    double const Dx = static_cast<double>(Rock->Pos[0]) - FarCenter[0];
                    double const Dy = static_cast<double>(Rock->Pos[1]) - FarCenter[1];
                    double const Dz = static_cast<double>(Rock->Pos[2]) - FarCenter[2];
                    DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
                    if (DistSq > RadiusSq + 10.0)
                    {
                        // Respawned on the sphere's surface, invisible (alpha 0) for this frame.
                        Rock->bVisible = 1;
                        Vector const Dir = random_on_unit_sphere();
                        float const Radius = SphereRadius;
                        float const OffsetZ = static_cast<float>(static_cast<double>(Radius) * Dir.z);
                        Rock->Pos[0] = static_cast<float>(static_cast<double>(Radius) * Dir.x + FarCenter[0]);
                        Rock->Pos[1] = static_cast<float>(static_cast<double>(Radius) * Dir.y + FarCenter[1]);
                        Rock->Pos[2] = static_cast<float>(static_cast<double>(OffsetZ) + FarCenter[2]);
                        Rock->Alpha = 0.0f;

                        if (ZoneRef != nullptr && ZoneRef->GetImmersion(RockPos) == 0.0f)
                        {
                            Rock->bVisible = 0;
                            continue;
                        }
                        // Without an exclusion manager nothing more this frame.
                        if (Excl == nullptr)
                            continue;
                        float ExclusionDepth = -1.0f;
                        if (Excl->is_excluded(RockPos,
                                              static_cast<Exclusion::ExcludeObjectType>(ExcludeType),
                                              &ExclusionDepth))
                        {
                            Rock->bVisible = 0;
                            continue;
                        }
                        if (ExclusionDepth > 0.0f)
                        {
                            if (Rock->Phase > ExclusionDepth)
                                Rock->bVisible = 0;
                        }
                        else
                        {
                            if (Rock->Phase > DensityScale)
                                Rock->bVisible = 0;
                        }
                        continue;
                    }
                }

                if (Rock->bVisible == 0)
                    continue;

                // Depth along Forward; the unrounded value is compared, the float kept.
                double const DepthD =
                    (static_cast<double>(Rock->Pos[0] - CamPos[0]) * Forward[0] +
                     static_cast<double>(Rock->Pos[2] - CamPos[2]) * Forward[2]) +
                    static_cast<double>(Rock->Pos[1] - CamPos[1]) * Forward[1];
                float const Depth = static_cast<float>(DepthD);

                if (DepthD > SphereAhead)
                {
                    // Beyond the sphere centre the fade band narrows with depth.
                    double T = 1.0 - (static_cast<double>(SphereRadius) -
                                      (static_cast<double>(Depth) - SphereAhead)) /
                                         SphereRadius;
                    if (T > 1.0)
                        T = 1.0;
                    double const Band =
                        ((1.0 - FadeDistPercent) * T * 0.5 + FadeDistPercent) *
                        SphereRadius;
                    double const BandSq = Band * Band;
                    if (!(DistSq > RadiusSq - BandSq))
                        Rock->Alpha = 1.0f;
                    else
                        Rock->Alpha = static_cast<float>((RadiusSq - DistSq) / BandSq);
                }
                else
                {
                    if (!(DistSq > InnerSq))
                        Rock->Alpha = 1.0f;
                    else
                        Rock->Alpha = static_cast<float>((RadiusSq - DistSq) * InvFadeSq);
                }

                if (Rock->Alpha < 0.02f) // DAT_005d1fe4
                    continue;
                // The near fade planes (FUN_0054feb0's NearFadeEnd/NearFadeStart).
                if (Depth < NearFadeEnd)
                {
                    if (Depth < NearFadeStart)
                        Rock->Alpha = 0.0f;
                    else
                        Rock->Alpha = static_cast<float>(
                            (static_cast<double>(Depth) - NearFadeStart) /
                            (static_cast<double>(NearFadeEnd) - NearFadeStart) * Rock->Alpha);
                }
                // Translucent rocks wait for the blended pass.
                if (Rock->Alpha < 1.0f)
                {
                    ++Deferred;
                    continue;
                }

                // Retail quirk, kept: the corner colour uses rock 0's FlipU (+0x15 off the array
                // pointer, not the element) and this rock's FlipV.
                int const Index =
                    static_cast<int>(Rocks[0].bFlipU) + static_cast<int>(Rock->bFlipV) * 2;
                float const* const Pick = PickCorner(Index, Corner0, Corner1, Corner2, Corner3);
                float CornerColor[3] = {Pick[0], Pick[1], Pick[2]};
                BillboardEmitRock(this, Rock, Right, this->Up, CornerColor, CenterColor);
            }

            BatchFlush(Batch);

            // The blended pass.
            if (Deferred > 0)
            {
                Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
                Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
                BillboardApplyRenderStates(Pipe, kRenderStatesBlend);
                Batch.Reset();
                int const BlendTotal = Deferred * 3;
                if (BlendTotal != 0)
                {
                    BatchBeginTriangles(Batch);
                    bool const bBlendLocked =
                        Batch.VbMgr->LockVertexMemory(Batch.Fvf,
                                                      static_cast<unsigned int>(BlendTotal),
                                                      &Batch.Lock) == 0;
                    if (bBlendLocked)
                    {
                        Batch.LockedCount = BlendTotal;
                        Batch.BindCursors((unsigned char*)Batch.Lock.Vertices,
                                          static_cast<unsigned>(Batch.Lock.Listing.Fvf));

                        int Drawn = 0;
                        for (RockIndex = 0; RockIndex < RockCount; ++RockIndex)
                        {
                            FAsteroidBillboardRock* const Rock = &Rocks[RockIndex];
                            if (Rock->bVisible == 0)
                                continue;
                            if (!(Rock->Alpha < 1.0f))
                                continue;
                            if (!(Rock->Alpha > 0.02f))
                                continue;
                            ++Drawn;
                            if (Drawn > Deferred)
                            {
                                FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                                      kSpaceTerrainRenderCompCpp, 0x410,
                                      "asteroid billboard render count mistake");
                                continue;
                            }
                            int const Index = static_cast<int>(Rocks[0].bFlipU) +
                                              static_cast<int>(Rock->bFlipV) * 2;
                            float const* const Pick =
                                PickCorner(Index, Corner0, Corner1, Corner2, Corner3);
                            float CornerColor[3] = {Pick[0], Pick[1], Pick[2]};
                            BillboardEmitRock(this, Rock, Right, this->Up, CornerColor, CenterColor);
                        }

                        BatchFlush(Batch);
                    }
                }
            }
        }
    }

    Batch.SetInterfaces(nullptr, nullptr);
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    Pipe->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    Pipe->SetRenderState(D3DRS_ALPHAREF, OldAlphaRef);
    Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    BillboardApplyRenderStates(Pipe, kRenderStatesMaterialOff);
    BillboardApplyRenderStates(Pipe, kRenderStatesRestore);
}
