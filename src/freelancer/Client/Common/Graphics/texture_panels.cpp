// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TexturePanels / planet-ring — PTR_LAB_005db1b4: FUN_0050a040..FUN_0050bfa0, FUN_00551500,
// LAB_005091a0.

#include "Common/Graphics/texture_panels.h"

#include "DALib.h"
#include "Space/asteroid_billboard.h" // FBillboardVertexCursor ({pointer, stride}, dtor FUN_00552b20)
#include "common_csys.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_universe.h"
#include "Space/Effects/effect_vertex_batch.h" // EffectVertexBatchDestroy (FUN_0046e9c0), the atexit LAB_0050a020
#include "Common/engine_light.h"
#include "Common/Graphics/part_cache.h"    // FvfTexcoordBlockBytes (FUN_004061a0)
#include "Core/perf_options.h"
#include "Common/Graphics/pixel_format.h"  // PixelFormatFromD3D / PixelFormatGetDesc (FUN_005b7010 / 005b7030)
#include "render/RenderPipeline.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"
#include "Space/star_sphere.h"
#include "Space/star_sys.h"
#include "Interface/win3d_immediate.h"
#include "x86math.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>

namespace
{
    float const kColorScale = 1.0f / 255.0f; // DAT_005c7f68 = 0x3b808081
    float const kZero = 0.0f;                // DAT_005c7474

    // DAT_00675060 / DAT_00675064 — global TexturePanels instance list (Create).
    FTexturePanelsGlobalNode G_PanelsListSentinel = {&G_PanelsListSentinel, &G_PanelsListSentinel,
                                                     nullptr};
    int G_PanelsListCount = 0;

    // DAT_00613324 — the panels detail level FUN_00509f60 stores (.data initial 1.0f).
    float G_PanelsDetail = 1.0f;

    // FUN_00554e90 — StarSys active-system id thunk (DAT_00679954).
    unsigned QueryActiveSystemIdStarSys()
    {
        return StarSysGetActiveSystemId();
    }

    // The {state, value} tables below are retail's .rdata, terminator included.
    // DAT_005db0d8 -- RS: LIGHTING (0x89) 0, SPECULARENABLE (0x1d) 0, ALPHABLENDENABLE (0x1b) 1,
    // SRCBLEND (0x13) SRCALPHA, DESTBLEND (0x14) INVSRCALPHA.
    unsigned long const kRsPanelBoth[] = {0x89, 0, 0x1d, 0, 0x1b, 1, 0x13, 5, 0x14, 6, 0};
    // DAT_005db108 stage0 / DAT_005db140 stage1 / DAT_005db178 stage2 -- TSS COLOROP, COLORARG1,
    // COLORARG2, ALPHAOP, ALPHAARG1, ALPHAARG2.
    unsigned long const kTssStage0[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0};
    unsigned long const kTssStage1[] = {1, 5, 2, 2, 3, 1, 4, 4, 5, 2, 6, 1, 0};
    unsigned long const kTssStage2[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    // DAT_005e09c8 used by FUN_00551500
    unsigned long const kRsCloudsDraw[] = {0x8d, 1, 0x91, 1, 0};
    // DAT_005e09e0 -- restore after DrawClouds: only 0x8d (the table is {0x8d, 0, 0}).
    unsigned long const kRsCloudsRestore[] = {0x8d, 0, 0};
    // DAT_005e09f0 / DAT_005e0a28 -- TSS stage0/1 tables used by FUN_00551500
    unsigned long const kTssClouds0[] = {1, 4, 2, 2, 3, 0, 4, 4, 5, 2, 6, 0, 0};
    unsigned long const kTssClouds1[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    // FUN_00552420 (thiscall on the format id) — ids 10..14 (the DXT formats) have alpha.
    unsigned char TextureFormatHasAlpha(int FormatId)
    {
        if (FormatId >= 10 && FormatId <= 14)
            return 1;
        return static_cast<unsigned char>(PixelFormatGetDesc(FormatId)->HasAlpha & 0xffu);
    }

    // FUN_00552440 — XYZ/XYZRHW size from FVF low byte.
    int FvfPositionBytes(unsigned char FvfLow)
    {
        if ((FvfLow & 0xc) == 0xc)
            return 0x1c;
        if ((FvfLow & 4) == 4)
            return 0x10;
        if ((FvfLow & 2) == 2)
            return 0xc;
        return 0;
    }

    // FUN_00552480 — diffuse (+normal) size from FVF low byte.
    int FvfDiffuseNormalBytes(unsigned char FvfLow)
    {
        int Bytes = 0;
        if ((FvfLow & 0x40) != 0)
            Bytes = 4;
        if (static_cast<char>(FvfLow) < 0)
            Bytes += 4;
        return Bytes;
    }

    // FUN_004d7270 (thiscall, RET 4), in retail's operand order.
    float* Vec3AddInPlace(float* This, float const* Other)
    {
        This[0] = Other[0] + This[0];
        This[1] = Other[1] + This[1];
        This[2] = Other[2] + This[2];
        return This;
    }

    // DAT_00675018 — TexturePanels piece vertex batcher (embedded object, not a pointer).
    struct FPanelVert20
    {
        float X, Y, Z;           // +0
        unsigned char B, G, R, A; // +0xc
        float U, V;              // +0x10
        float U1, V1;            // +0x18
    };
    static_assert(sizeof(FPanelVert20) == 0x20, "panel batcher vert 0x20");

    // FUN_00405840's object: a render-state saver built in place; the destructor FUN_00405840
    // puts Old back.
    struct FRenderStateRestorer
    {
        explicit FRenderStateRestorer(int InState);
        ~FRenderStateRestorer() noexcept(false);

        IRenderPipeline* Pipeline; // +0
        int State;                   // +4
        int Old;                     // +8
    };

    void RenderStateSave(FRenderStateRestorer* Saver, int State)
    {
        Saver->Pipeline = DALib::Pipeline;
        Saver->State = State;
        Saver->Pipeline->GetRenderState((D3DRENDERSTATETYPE)State, &Saver->Old);
    }

    // FUN_00405840.
    void RenderStateRestore(FRenderStateRestorer* Saver)
    {
        Saver->Pipeline->SetRenderState((D3DRENDERSTATETYPE)Saver->State, Saver->Old);
    }

    FRenderStateRestorer::FRenderStateRestorer(int InState)
    {
        RenderStateSave(this, InState);
    }

    FRenderStateRestorer::~FRenderStateRestorer() noexcept(false)
    {
        RenderStateRestore(this);
    }

    struct FPanelVertexBatcher
    {
        unsigned long PrimitiveType; // +0x00  ctor 1, DrawPiece writes 5 (triangle strip)
        FPanelVert20* Verts;       // +0x04
        int VertCount;             // +0x08
        unsigned long Fvf;         // +0x0c  ctor 0x242 (XYZ|DIFFUSE|TEX2, 0x20-byte vertex)
        unsigned short* Indices;   // +0x10
        int IndexCount;            // +0x14
        // +0x18 DAT_00675030: the pipeline the batch draws through, AddRef'd while held
        // (DrawPiece swaps it in and releases it).
        IRenderPipeline* Pipeline;
        int VertGrow;              // +0x1c
        unsigned VertCap;          // +0x20
        unsigned IndexCap;         // +0x24
        unsigned char bCloseLoop;  // +0x28  the flush repeats vertex 0 at the end
        unsigned char bStripMode;  // +0x29  quads as one strip (PushVert's joining copies)
        unsigned char Pad2A[2];
        int QuadCounter;           // +0x2c
        // DrawPiece stores the ring colour * 255 here; PushVert writes it as a D3DCOLOR (BGRA).
        unsigned char ColorR;      // +0x30
        unsigned char ColorG;      // +0x31
        unsigned char ColorB;      // +0x32
        unsigned char ColorA;      // +0x33
        float StageU;              // +0x34
        float StageV;              // +0x38
        float StageU1;             // +0x3c
        float StageV1;             // +0x40
    };

    // Static initializer LAB_00509f90: FUN_00509fb0(this = DAT_00675018, 0, 0x80). Retail then
    // registers LAB_0050a020 with atexit -- G_PanelBatcherAtExit below.
    FPanelVertexBatcher G_PanelBatcher = {
        1u, nullptr, 0, 0x242u, nullptr, 0, nullptr, 0x80, 0u, 0u, 0, 0, {0, 0}, 0,
        0xffu, 0xffu, 0xffu, 0xffu, 0.0f, 0.0f, 0.0f, 0.0f};

    // The destructor retail runs on this batch is the effect batch's FUN_0046e9c0 (one body for
    // both under /OPT:ICF): it touches only +0x04, +0x10 and +0x18, which sit at the same offsets
    // in both port types.
    static_assert(offsetof(FPanelVertexBatcher, Verts) == offsetof(FEffectVertexBatch, Vertices),
                  "FUN_0046e9c0 frees +0x04");
    static_assert(offsetof(FPanelVertexBatcher, Indices) == offsetof(FEffectVertexBatch, Indices),
                  "FUN_0046e9c0 frees +0x10");
    static_assert(offsetof(FPanelVertexBatcher, Pipeline) == offsetof(FEffectVertexBatch, Pipeline),
                  "FUN_0046e9c0 releases +0x18");

    // DAT_00675010 -- LAB_0050a020's once-flag.
    unsigned char G_PanelBatcherDtorOnce = 0;

    // LAB_0050a020 (the atexit entry), once-guarded by DAT_00675010. A file-scope object's
    // destructor is the port's atexit registration, as for the effect batch.
    struct FPanelBatcherAtExit
    {
        ~FPanelBatcherAtExit()
        {
            if ((G_PanelBatcherDtorOnce & 1u) != 0u)
                return;
            G_PanelBatcherDtorOnce = static_cast<unsigned char>(G_PanelBatcherDtorOnce | 1u);
            EffectVertexBatchDestroy(reinterpret_cast<FEffectVertexBatch*>(&G_PanelBatcher));
        }
    };

    FPanelBatcherAtExit G_PanelBatcherAtExit;

    // FUN_0050c120 — grow vertex buffer when VertCount >= VertCap.
    void PanelBatcherGrowVerts(FPanelVertexBatcher* B)
    {
        if (B->VertCap > static_cast<unsigned>(B->VertCount))
            return;
        int const Grow = B->VertGrow;
        unsigned const NewCap = B->VertCap + static_cast<unsigned>(Grow);
        FPanelVert20* const Neu =
            static_cast<FPanelVert20*>(operator new(NewCap * sizeof(FPanelVert20)));
        if (Neu == nullptr)
            return;
        if (B->Verts != nullptr)
        {
            memcpy(Neu, B->Verts, B->VertCap * sizeof(FPanelVert20));
            operator delete(B->Verts);
        }
        B->Verts = Neu;
        B->VertCap = NewCap;
    }

    // FUN_0050c190 — reset counters and ensure index capacity for one more slot.
    void PanelBatcherReset(FPanelVertexBatcher* B)
    {
        B->VertCount = 0;
        B->bStripMode = 0;
        B->bCloseLoop = 0;
        B->QuadCounter = 0;
        B->IndexCount = 0;
        PanelBatcherGrowVerts(B);
        if (B->IndexCap < static_cast<unsigned>(B->IndexCount) + 1u)
        {
            int const Grow = B->VertGrow * 3;
            unsigned const NewCap = B->IndexCap + static_cast<unsigned>(Grow);
            unsigned short* const Neu =
                static_cast<unsigned short*>(operator new(NewCap * 2u));
            if (Neu != nullptr)
            {
                if (B->Indices != nullptr)
                {
                    memcpy(Neu, B->Indices, B->IndexCap * 2u);
                    operator delete(B->Indices);
                }
                B->Indices = Neu;
                B->IndexCap = NewCap;
            }
        }
    }

    // FUN_0050bb60 — push one XYZ vertex from staging color/UV.
    void PanelBatcherPushVert(FPanelVertexBatcher* B, float X, float Y, float Z)
    {
        if (B->bStripMode != 0)
        {
            if ((B->QuadCounter & 3) == 3)
            {
                B->VertCount += 2;
                PanelBatcherGrowVerts(B);
                int const C = B->VertCount;
                memcpy(&B->Verts[C - 2], &B->Verts[C - 5], sizeof(FPanelVert20));
                memcpy(&B->Verts[C - 1], &B->Verts[C - 3], sizeof(FPanelVert20));
            }
            B->QuadCounter += 1;
        }
        FPanelVert20* const V = &B->Verts[B->VertCount];
        V->B = B->ColorB;
        V->G = B->ColorG;
        V->R = B->ColorR;
        V->A = B->ColorA;
        V->U = B->StageU;
        V->V = B->StageV;
        V->U1 = B->StageU1;
        V->V1 = B->StageV1;
        V->X = X;
        V->Y = Y;
        V->Z = Z;
        B->VertCount += 1;
        PanelBatcherGrowVerts(B);
    }

    // FUN_0050bb20 (cdecl: pipeline, stage, table). Neither the pipeline nor the table is tested.
    void PipelineApplyTssPairTable(IRenderPipeline* Pipe, unsigned long Stage,
                                   unsigned long const* Table)
    {
        unsigned long const* Cursor = Table;
        unsigned long State = Cursor[0];
        while (State != 0)
        {
            Pipe->SetTextureStageState(static_cast<int>(Stage), static_cast<int>(State),
                                       static_cast<int>(Cursor[1]));
            State = Cursor[2];
            Cursor += 2;
        }
    }

    // FUN_0050b2d0 (__thiscall) -- bind the ring's textures and states. Only the two-texture
    // path runs VerifyState (result dropped). FUN_00425640 returns null for a reference that
    // holds no record, so the handles need no separate test.
    int SetupPanelPipeline(FTexturePanels* This)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        if (This->DetailShape != nullptr && This->Shape != nullptr)
        {
            Pipe->SetTextureStageTexture(0, TexShapeGetGpuHandle(This->Shape, 0));
            Pipe->SetTextureStageState(0, 0xb, 0);
            Pipe->SetTextureStageTexture(1, TexShapeGetGpuHandle(This->DetailShape, 0));
            Pipe->SetTextureStageState(1, 0xb, 1);
            PipelineApplyTssTable(Pipe, 0, kTssStage0);
            PipelineApplyTssTable(Pipe, 1, kTssStage1);
            PipelineApplyTssTable(Pipe, 2, kTssStage2);
            PipelineApplyRsTable(Pipe, kRsPanelBoth);
            Pipe->VerifyState();
            return 1;
        }
        if (This->Shape != nullptr)
        {
            Pipe->SetTextureStageTexture(0, TexShapeGetGpuHandle(This->Shape, 0));
            Pipe->SetTextureStageState(0, 0xb, 0);
            PipelineApplyTssTable(Pipe, 0, kTssStage0);
            PipelineApplyTssTable(Pipe, 1, kTssStage2);
            PipelineApplyRsTable(Pipe, kRsPanelBoth);
            return 1;
        }
        return 0;
    }

    // The list erase FUN_0050b170 and FUN_0050a170 (FUN_00516f70 per node) run. No null test.
    void ListUnlinkAndDelete(FTexturePanelsListNode* Sentinel, FTexturePanels* Owner)
    {
        FTexturePanelsListNode* Node = Sentinel->Next;
        while (Node != Sentinel)
        {
            FTexturePanelsListNode* const Next = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            operator delete(Node);
            Owner->ListCount -= 1;
            Node = Next;
        }
    }

    void PushCloudsNode(FTexturePanels* This, FNebulaClouds* Clouds)
    {
        FTexturePanelsListNode* const Sentinel = This->ListSentinel;
        FTexturePanelsListNode* const Node =
            static_cast<FTexturePanelsListNode*>(operator new(sizeof(FTexturePanelsListNode)));
        Node->Clouds = Clouds;
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Sentinel->Prev->Next = Node;
        Sentinel->Prev = Node;
        This->ListCount += 1;
    }

    // Camera vt+0x0c -> world position. The slot is __stdcall with `this` on the stack, as in
    // space_camera.h.
    void CameraReadPos(void* Camera, float* Out3)
    {
        float const* const Pos = ((ICamera*)Camera)->GetPosition();
        Out3[0] = Pos[0];
        Out3[1] = Pos[1];
        Out3[2] = Pos[2];
    }
} // namespace

// -------------------------------------------------------------------- FUN_0050a040

FTexturePanels::FTexturePanels()
{
    // FUN_0050a040 (thiscall). No test of `this` (FUN_0050bfa0 constructs only a non-null
    // block). The memset only covers what retail leaves as it was (the padding and the
    // allocator byte at +0x64, which retail copies from an uninitialised stack byte); every
    // field is stored below as retail stores it.
    memset(ShapePadding, 0, sizeof(ShapePadding));
    memset(&Zone, 0, sizeof(FTexturePanels) - offsetof(FTexturePanels, Zone));
    // List sentinel 0xc (Next/Prev self).
    FTexturePanelsListNode* const Sentinel =
        static_cast<FTexturePanelsListNode*>(operator new(sizeof(FTexturePanelsListNode)));
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Sentinel->Clouds = nullptr;
    ListSentinel = Sentinel;
    ListCount = 0;
    ThicknessEdge = 0.1f;
    InnerEdge = 0.1f;
    OuterEdge = 0.1f;
    bLoaded = 0;
    Zone = nullptr;
    Visibility = 0.0f;
    MeshBuffer = nullptr;
    PieceCount = 0;
    Pieces = nullptr;
    MeshVertexCount = 0;
    NumSegments = 0x20;
    TransitionDist = 150.0f;
    DetailTileU = 10.0f;
    DetailTileV = 2;
    ColorR = 1.0f;
    ColorG = 1.0f;
    ColorB = 1.0f;
    Opacity = 1.0f;
    Shape = nullptr;
    DetailShape = nullptr;
}

// -------------------------------------------------------------------- FUN_0050a120

IPlanetRing::~IPlanetRing()
{
}

// -------------------------------------------------------------------- FUN_0050b170

void FTexturePanels::Clear()
{
    if (bLoaded == 0)
        return;

    // FUN_005501e0 per clouds object; the node keeps its (now dangling) pointer until the
    // erase below.
    FTexturePanelsListNode* const Sentinel = ListSentinel;
    for (FTexturePanelsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
    {
        if (N->Clouds != nullptr)
        {
            NebulaCloudsReleaseBits(N->Clouds);
            operator delete(N->Clouds);
        }
    }
    ListUnlinkAndDelete(Sentinel, this);

    // Retail quirk: the ring mesh and the pieces are freed but neither pointer is cleared, and
    // their counts keep their values. Clear runs from Load (which reassigns all four unless
    // the zone is missing) and from the destructor.
    operator delete(MeshBuffer);
    operator delete(Pieces);

    Shapes.Clear(); // FUN_00425a20

    Zone = nullptr;
    bLoaded = 0;
}

// -------------------------------------------------------------------- FUN_0050a170 / 0050a150

FTexturePanels::~FTexturePanels()
{
    // FUN_0050a170 (thiscall): no test of `this` or of the sentinel.
    FTexturePanels::Clear(); // direct call during destruction
    ListUnlinkAndDelete(ListSentinel, this);
    operator delete(ListSentinel);
    ListSentinel = nullptr;
    ListCount = 0;
}

// -------------------------------------------------------------------- FUN_0050bfa0

FTexturePanels* TexturePanelsCreate()
{
    // FUN_0050bfa0: the node is appended even when new(0x78) failed.
    FTexturePanels* Panels = new FTexturePanels;

    FTexturePanelsGlobalNode* const Node =
        static_cast<FTexturePanelsGlobalNode*>(operator new(sizeof(FTexturePanelsGlobalNode)));
    Node->Panels = Panels;
    Node->Next = &G_PanelsListSentinel;
    Node->Prev = G_PanelsListSentinel.Prev;
    G_PanelsListSentinel.Prev->Next = Node;
    G_PanelsListSentinel.Prev = Node;
    G_PanelsListCount += 1;
    return Panels;
}

// The walk inlined in the StarSys tick FUN_00554eb0. Update forwards the dt as FUN_00550d40's
// SECOND argument.
void TexturePanelsUpdateAll(void* Camera, float Dt)
{
    for (FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next; N != &G_PanelsListSentinel;
         N = N->Next)
        N->Panels->Update(Camera, Dt);
}

// FUN_00509f60 — store the panels detail (DAT_00613324) and hand it to slot 12 (+0x30) of
// every live TexturePanels. Caller: the Nebula perf option FUN_00509b80.
void TexturePanelsApplyDetailAll(float Detail)
{
    G_PanelsDetail = Detail;
    for (FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next; N != &G_PanelsListSentinel;
         N = N->Next)
        N->Panels->ApplyDetail(G_PanelsDetail);
}

// FUN_00509f30 — slot 5 (+0x14, DrawPanels) with the camera on every live TexturePanels.
// Caller: the space draw pass FUN_0051f950, between bucket 0 and FUN_00520170.
void TexturePanelsDrawAll(void* Camera)
{
    for (FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next; N != &G_PanelsListSentinel;
         N = N->Next)
        N->Panels->DrawPanels(Camera);
}

// FUN_00509f20 (&DAT_0067505c) followed by the callers' read of +4.
FTexturePanelsGlobalNode* TexturePanelsListSentinel()
{
    return &G_PanelsListSentinel;
}

// FUN_0050c040.
void TexturePanelsDestroyAll()
{
    for (FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next; N != &G_PanelsListSentinel;
         N = N->Next)
    {
        if (N->Panels != nullptr)
            delete N->Panels;
    }
    FTexturePanelsGlobalNode* N = G_PanelsListSentinel.Next;
    while (N != &G_PanelsListSentinel)
    {
        FTexturePanelsGlobalNode* const Next = N->Next;
        N->Prev->Next = N->Next;
        N->Next->Prev = N->Prev;
        operator delete(N);
        G_PanelsListCount -= 1;
        N = Next;
    }
}

// -------------------------------------------------------------------- slot10 FUN_00504870

Universe::IZone const* FTexturePanels::GetZone()
{
    return Zone;
}

// -------------------------------------------------------------------- slot9 LAB_0050bf30

float FTexturePanels::GetVisibility()
{
    return Visibility;
}

// -------------------------------------------------------------------- slot6 LAB_0050bcd0

int FTexturePanels::GetPieceCount()
{
    return PieceCount;
}

namespace
{
    char const kPlanetRingCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\PlanetRing.cpp"; // 005db2e4
    char const kPieceOutOfRange[] = "piece num specified in plantary ring is out of range";   // 005db318
    char const kShapeNotFoundForRings[] =
        "%s(%d) : *** ERROR: shape \"%s\" not found for planet rings"; // 005db2a8
}

// -------------------------------------------------------------------- slot7 LAB_0050bce0

float const* FTexturePanels::GetPiece(int Index)
{
    // Signed compare: a negative index is not caught.
    if (Index < PieceCount)
        return Pieces[Index].Center;
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPlanetRingCppPath, 0x279, kPieceOutOfRange);
    return nullptr;
}

// -------------------------------------------------------------------- slot8 LAB_0050bd20

float FTexturePanels::GetPieceFloat(int Index)
{
    // Same bounds test.
    if (Index < PieceCount)
        return Pieces[Index].Radius;
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPlanetRingCppPath, 0x284, kPieceOutOfRange);
    return kZero;
}

// -------------------------------------------------------------------- slot11 FUN_0050bf40

double FTexturePanels::GetZonePos(float* Out3)
{
    // FUN_0050bf40 (thiscall, RET 4): the radius is left on the FPU. Nothing is tested.
    Vector const& Pos = Zone->GetPos();
    Out3[0] = Pos.x;
    Out3[1] = Pos.y;
    Out3[2] = Pos.z;
    return static_cast<double>(Zone->GetRadius());
}

// -------------------------------------------------------------------- slot12 LAB_0050bf70

void FTexturePanels::ApplyDetail(float Detail)
{
    // LAB_0050bf70 (thiscall, RET 4): FUN_005524a0 per clouds object. Nothing is tested.
    FTexturePanelsListNode* const Sentinel = ListSentinel;
    for (FTexturePanelsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        NebulaCloudsApplyDetail(N->Clouds, Detail);
}

namespace
{
    I3DMathEngine* AcquirePanelsMath()
    {
        // DAT_00675074 — same lazy 3DMathEngine as Nebula (FUN_005b73e0).
        static I3DMathEngine* G_PanelsMath = nullptr;
        if (G_PanelsMath == nullptr)
            G_PanelsMath = Acquire3DMathEngine();
        return G_PanelsMath;
    }

    // The zone's vt+0x0c transform (orientation, then position), as retail reads it.
    void ZoneToTransform(Universe::IZone const& Zone, FTransform& Out)
    {
        static_assert(sizeof(FTransform) == sizeof(Transform), "FTransform matches Transform");
        memcpy(&Out, &Zone.GetTransform(), sizeof(Out));
    }
} // namespace

// -------------------------------------------------------------------- slot0 LAB_0050bd70

double FTexturePanels::GetImmersion(Vector const& Point) const
{
    // LAB_0050bd70 (thiscall, RET 4: camera position). The result is left on the FPU. Nothing
    // is tested. The zone's size is fetched three times, once per component.
    float const OuterRadius = Zone->GetSize().x;
    float const InnerRadius = Zone->GetSize().y;
    float const HalfHeight = Zone->GetSize().z;
    // The squares are stored as floats.
    float const OuterSq = static_cast<float>(static_cast<double>(OuterRadius) * OuterRadius);
    float const InnerSq = static_cast<float>(static_cast<double>(InnerRadius) * InnerRadius);
    double const OuterPlus = static_cast<double>(OuterRadius) + TransitionDist;
    float const SoftOuterSq = static_cast<float>(OuterPlus * OuterPlus);
    double const InnerMinus = static_cast<double>(InnerRadius) - TransitionDist;
    float const SoftInnerSq = static_cast<float>(InnerMinus * InnerMinus);

    FTransform ZoneXf;
    ZoneToTransform(*Zone, ZoneXf);
    FVector Local;
    AcquirePanelsMath()->InverseTransform(Local, ZoneXf,
                                          *reinterpret_cast<FVector const*>(&Point));

    // The height factor (double 1.0 DAT_005c89b8); unordered compares take the ramp and the
    // early 0.
    double const AbsY = std::fabs(static_cast<double>(Local.Y));
    float Factor = 1.0f;
    if (!(AbsY < static_cast<double>(HalfHeight)))
    {
        // FST keeps the unrounded value on the FPU; FCOMP tests that one.
        double const FactorExt =
            1.0 - (AbsY - HalfHeight) / static_cast<double>(TransitionDist);
        Factor = static_cast<float>(FactorExt);
        if (!(FactorExt > 0.0))
            return 0.0 * static_cast<double>(Factor);
    }

    // r^2 is compared unrounded against the soft outer square, then as the stored float.
    double const RadialSq = static_cast<double>(Local.X) * Local.X +
                            static_cast<double>(Local.Z) * Local.Z;
    double const RadialSqF = static_cast<double>(static_cast<float>(RadialSq));
    if (RadialSq > static_cast<double>(SoftOuterSq))
        return 0.0 * static_cast<double>(Factor);

    if (RadialSqF > static_cast<double>(OuterSq))
        return (static_cast<double>(SoftOuterSq) - RadialSqF) /
               (static_cast<double>(SoftOuterSq) - OuterSq) * Factor;

    if (RadialSqF > static_cast<double>(InnerSq))
        return 1.0 * static_cast<double>(Factor);

    if (RadialSqF > static_cast<double>(SoftInnerSq))
        return (RadialSqF - SoftInnerSq) /
               (static_cast<double>(InnerSq) - SoftInnerSq) * Factor;
    return 0.0 * static_cast<double>(Factor);
}

// -------------------------------------------------------------------- slot3 LAB_0050b210

void FTexturePanels::Update(void* Camera, float Dt)
{
    // LAB_0050b210.
    float CamPos[3];
    CameraReadPos(Camera, CamPos);

    // Retail computes the zone-local camera position and, outside the zone, the surface point
    // -- both into the same local, which nothing reads afterwards. Slot 0 below gets the
    // camera's WORLD position.
    {
        Vector CamV;
        CamV.x = CamPos[0];
        CamV.y = CamPos[1];
        CamV.z = CamPos[2];
        FTransform ZoneXf;
        ZoneToTransform(*Zone, ZoneXf);
        FVector Local;
        AcquirePanelsMath()->InverseTransform(Local, ZoneXf, *reinterpret_cast<FVector const*>(&CamV));
        if (!Zone->InZone(CamV, 0.0f))
        {
            Vector const Surface = Zone->GetSurfacePoint(CamV);
            (void)Surface;
        }
    }

    Visibility = static_cast<float>(GetImmersion(*reinterpret_cast<Vector const*>(CamPos)));

    // FUN_00550d40 per clouds object.
    FTexturePanelsListNode* const Sentinel = ListSentinel;
    for (FTexturePanelsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        NebulaCloudsUpdate(N->Clouds, Camera, Dt, Visibility, nullptr);
}

// -------------------------------------------------------------------- slot4 LAB_0050b510

void FTexturePanels::DrawPiece(void* Camera, int PieceIndex)
{
    // LAB_0050b510 (__thiscall, RET 8).
    if (bLoaded == 0)
        return;
    if (PieceIndex < 0)
        return;
    if (PieceIndex >= PieceCount)
        return;

    // Three FUN_00405840 savers.
    FRenderStateRestorer Save16(0x16);
    Save16.Pipeline->SetRenderState((D3DRENDERSTATETYPE)Save16.State, 1);
    FRenderStateRestorer Save0e(0xe);
    Save0e.Pipeline->SetRenderState((D3DRENDERSTATETYPE)Save0e.State, 0);
    FRenderStateRestorer Save17(0x17);
    Save17.Pipeline->SetRenderState((D3DRENDERSTATETYPE)Save17.State, 4);

    // FUN_004223d0 (its argument is never read), then FUN_00422410.
    Win3DSetTextureFilterLinear(nullptr);
    Win3DSetTextureFilterPoint();

    // The zone-local camera position goes into a scratch the loop overwrites before anything
    // reads it.
    FTransform ZoneXf;
    ZoneToTransform(*Zone, ZoneXf);
    FVector const CamPos = *(FVector const*)((ICamera*)Camera)->GetPosition();
    FVector Scratch;
    AcquirePanelsMath()->InverseTransform(Scratch, ZoneXf, CamPos);

    // FUN_004215b0, then FUN_00422690(camera, &matrix).
    float World12[12];
    Win3DIdentityRotation3x3(World12);
    World12[11] = 0.0f;
    World12[10] = 0.0f;
    World12[9] = 0.0f;
    Win3DBindModelview(Camera, World12);

    // Zero skips straight to the restorers.
    if (SetupPanelPipeline(this) != 0)
    {
        FTexturePanelPiece const* const Piece = &Pieces[PieceIndex];
        int const First = Piece->FirstSegment;
        int const Last = Piece->LastSegment;

        IRenderPipeline* const Pipe = DALib::Pipeline;
        if (G_PanelBatcher.Pipeline != nullptr)
        {
            G_PanelBatcher.Pipeline->Release();
            G_PanelBatcher.Pipeline = nullptr;
        }
        if (Pipe != nullptr)
        {
            G_PanelBatcher.Pipeline = Pipe;
            Pipe->AddRef();
        }

        // Primitive 5 (triangle strip).
        PanelBatcherReset(&G_PanelBatcher);
        G_PanelBatcher.PrimitiveType = 5;
        G_PanelBatcher.bCloseLoop = 0;
        G_PanelBatcher.bStripMode = 0;
        // Double 255.0 (.rdata 0x005c89c0), truncated, low byte kept. No clamp.
        double const K255 = 255.0;
        G_PanelBatcher.ColorR = static_cast<unsigned char>(static_cast<int>(ColorR * K255));
        G_PanelBatcher.ColorG = static_cast<unsigned char>(static_cast<int>(ColorG * K255));
        G_PanelBatcher.ColorB = static_cast<unsigned char>(static_cast<int>(ColorB * K255));
        G_PanelBatcher.ColorA = static_cast<unsigned char>(static_cast<int>(Opacity * K255));

        // 2*I indexes the ring mesh, wrapped by ONE subtraction each.
        int Twice = First * 2;
        for (int I = First; I <= Last; ++I)
        {
            int const MeshCount = MeshVertexCount;
            int IndexA = Twice;
            if (IndexA >= MeshCount)
                IndexA -= MeshCount;
            int IndexB = Twice + 1;
            if (IndexB >= MeshCount)
                IndexB -= MeshCount;
            float const* const Mesh = static_cast<float const*>(MeshBuffer);
            float const* const A = Mesh + IndexA * 3;
            float const* const B = Mesh + IndexB * 3;
            float const Ax = A[0];
            float const Ay = A[1];
            float const Az = A[2];
            float const Bx = B[0];
            float const By = B[1];
            float const Bz = B[2];

            // Odd segments take the shape's right edge and the integer detail_tile V, even
            // ones the left edge and 0.
            FTexShape const* const Shape = this->Shape;
            float const U = ((I & 1) != 0) ? Shape->U1 : Shape->U0;
            int const DetailUInt = ((I & 1) != 0) ? DetailTileV : 0;
            float const DetailU = static_cast<float>(DetailUInt);

            // Vertex a (FUN_0050bb60 inlined here).
            G_PanelBatcher.StageV = Shape->V0;
            G_PanelBatcher.StageU = U;
            G_PanelBatcher.StageV1 = 0.0f;
            G_PanelBatcher.StageU1 = DetailU;
            PanelBatcherPushVert(&G_PanelBatcher, Ax, Ay, Az);

            // Vertex b: detail V = detail_tile U.
            G_PanelBatcher.StageV = Shape->V1;
            G_PanelBatcher.StageU = U;
            G_PanelBatcher.StageV1 = DetailTileU;
            G_PanelBatcher.StageU1 = DetailU;
            PanelBatcherPushVert(&G_PanelBatcher, Bx, By, Bz);

            Twice += 2;
        }

        // Flush. With bCloseLoop set vertex 0 is copied past the end (no grow). The held pipeline
        // is released either way.
        int Count = G_PanelBatcher.VertCount;
        if (Count != 0)
        {
            if (G_PanelBatcher.bCloseLoop != 0)
            {
                memcpy(&G_PanelBatcher.Verts[Count], &G_PanelBatcher.Verts[0], sizeof(FPanelVert20));
                Count += 1;
                G_PanelBatcher.VertCount = Count;
            }
            if (G_PanelBatcher.Pipeline != nullptr)
            {
                G_PanelBatcher.Pipeline->DrawPrimitive(G_PanelBatcher.PrimitiveType,
                                                       static_cast<unsigned int>(G_PanelBatcher.Fvf),
                                                       G_PanelBatcher.Verts,
                                                       static_cast<unsigned int>(Count), nullptr);
            }
        }
        if (G_PanelBatcher.Pipeline != nullptr)
        {
            G_PanelBatcher.Pipeline->Release();
            G_PanelBatcher.Pipeline = nullptr;
        }

        // With a detail shape, stage 1 goes back to DAT_005db178.
        if (DetailShape != nullptr)
            PipelineApplyTssPairTable(DALib::Pipeline, 1, kTssStage2);

        DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    }
}

// -------------------------------------------------------------------- slot5 LAB_0050bc90

// LAB_0050bc90 (__thiscall, RET 4). No null tests on This, the list or a node's clouds.
void FTexturePanels::DrawPanels(void* Camera)
{
    if (bLoaded == 0)
        return;
    if (!(Visibility > kZero))
        return;
    FTexturePanelsListNode* const Sentinel = ListSentinel;
    for (FTexturePanelsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        TexturePanelDrawClouds(N->Clouds, Camera, nullptr);
}

// -------------------------------------------------------------------- slot1 LAB_0050a240

void FTexturePanels::Load(char const* IniPath,
                                       char const* Nickname)
{
    // LAB_0050a240.
    if (bLoaded != 0)
        Clear();

    // GetShape's result is dropped. Retail dereferences the zone here with no null test.
    Zone = Universe::get_zone(QueryActiveSystemIdStarSys(), Nickname);
    Zone->GetShape();

    // Colour and detail_tile keep whatever they held.
    OuterEdge = 0.1f;
    InnerEdge = 0.1f;
    ThicknessEdge = 0.1f;
    Opacity = 1.0f;
    Shape = nullptr;
    DetailShape = nullptr;
    NumSegments = 0x20;
    TransitionDist = 150.0f;

    // A failed open or an empty file skips the parse but NOT the rest.
    INI_Reader Reader;
    char FullPath[0x104];
    GetFileSysPath(FullPath, IniPath, DALib::Data);
    if (Reader.open(FullPath, false) && Reader.read_header())
    {
        do
        {
            if (Reader.is_header("TexturePanels"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("file"))
                    {
                        // FUN_00425b30 with no base filesystem.
                        char const* Full = GetFullFilename(Reader.get_value_string());
                        Shapes.Load(Full, nullptr);
                    }
                }
            }
            else if (Reader.is_header("Exterior"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("color"))
                    {
                        ColorR = Reader.get_value_float(0) * kColorScale;
                        ColorG = Reader.get_value_float(1) * kColorScale;
                        ColorB = Reader.get_value_float(2) * kColorScale;
                    }
                    else if (Reader.is_value("shape"))
                    {
                        // The inlined ShapeDB.h find warns on a miss too.
                        Shape = Shapes.Find(Reader.get_value_string());
                        if (Shape == nullptr)
                            FDUMP(0x100001, kShapeNotFoundForRings, kPlanetRingCppPath, 0x108,
                                  Reader.get_value_string());
                    }
                    else if (Reader.is_value("detail_shape"))
                    {
                        // Zeroed before the find.
                        DetailShape = nullptr;
                        DetailShape =
                            Shapes.Find(Reader.get_value_string());
                        if (DetailShape == nullptr)
                            FDUMP(0x100001, kShapeNotFoundForRings, kPlanetRingCppPath, 0x10f,
                                  Reader.get_value_string());
                    }
                    else if (Reader.is_value("detail_tile"))
                    {
                        DetailTileU = Reader.get_value_float(0);
                        DetailTileV = static_cast<int>(Reader.get_value_float(1)); // ftol
                    }
                    else if (Reader.is_value("opacity"))
                        Opacity = Reader.get_value_float(0);
                    else if (Reader.is_value("thickness_edge"))
                        ThicknessEdge = Reader.get_value_float(0);
                    else if (Reader.is_value("inner_edge"))
                        InnerEdge = Reader.get_value_float(0);
                    else if (Reader.is_value("outer_edge"))
                        OuterEdge = Reader.get_value_float(0);
                    else if (Reader.is_value("num_segments"))
                        NumSegments = static_cast<int>(Reader.get_value_float(0)); // ftol
                    else if (Reader.is_value("transition_dist"))
                        TransitionDist = Reader.get_value_float(0);
                }
            }
            else if (Reader.is_header("Clouds"))
            {
                // FUN_00550180 / FUN_00550240 against the ring's own ShapeDB; owner at +0xc4.
                FNebulaClouds* const Clouds =
                    static_cast<FNebulaClouds*>(operator new(sizeof(FNebulaClouds)));
                NebulaCloudsConstruct(Clouds);
                NebulaCloudsParseIni(Clouds, Reader, &Shapes);
                Clouds->Owner = this;
                Clouds->FullVisibility = 0.5f;   // 0x3f000000
                Clouds->BudgetVisibility = 0.9f; // 0x3f666666
                Clouds->AcceptVisibility = 1.1f; // 0x3f8ccccd
                PushCloudsNode(this, Clouds);
            }
        } while (Reader.read_header());
    }

    // The ring counts as loaded from here on, zone or not.
    bLoaded = 1;
    if (Zone == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPlanetRingCppPath, 0x135,
              "planet ring must have zone");
        return;
    }

    // GetSize three times -- x and y kept, the third result dropped.
    float const SizeX = Zone->GetSize().x;
    float const SizeY = Zone->GetSize().y;
    Zone->GetSize();

    // DAT_005c97f8 is the double of the float 2pi. Neither buffer is cleared.
    double const kTwoPi = 6.2831854820251465; // DAT_005c97f8
    float const AngleStep = static_cast<float>(kTwoPi / static_cast<double>(NumSegments));
    MeshVertexCount = NumSegments * 2;
    MeshBuffer = operator new(static_cast<size_t>(NumSegments * 2 * 3) * 4u);
    PieceCount = 8;
    Pieces = static_cast<FTexturePanelPiece*>(operator new(0xc0));

    FTransform ZoneXf;
    ZoneToTransform(*Zone, ZoneXf);

    // Segment I puts the ring's two edges at mesh vertices 2I and 2I+1 (the wrap is never taken
    // for I < NumSegments). The products are taken in extended precision off FSIN / FCOS and
    // stored as floats; the angle accumulates in a float.
    float* const Mesh = static_cast<float*>(MeshBuffer);
    float Angle = 0.0f;
    int Twice = 0;
    for (int Segment = 0; Segment < NumSegments; ++Segment)
    {
        double const Sin = sin(static_cast<double>(Angle));
        double const Cos = cos(static_cast<double>(Angle));
        int OuterIndex = Twice;
        if (OuterIndex >= MeshVertexCount)
            OuterIndex -= MeshVertexCount;
        int InnerIndex = Twice + 1;
        if (InnerIndex >= MeshVertexCount)
            InnerIndex -= MeshVertexCount;

        // Retail's operand order.
        FVector LocalY;
        LocalY.X = static_cast<float>(Sin * SizeY);
        LocalY.Y = 0.0f;
        LocalY.Z = static_cast<float>(SizeY * Cos);
        FVector LocalX;
        LocalX.X = static_cast<float>(Sin * SizeX);
        LocalX.Y = 0.0f;
        LocalX.Z = static_cast<float>(Cos * SizeX);

        FVector World;
        AcquirePanelsMath()->TransformXform(World, ZoneXf, LocalX);
        Mesh[OuterIndex * 3 + 0] = World.X;
        Mesh[OuterIndex * 3 + 1] = World.Y;
        Mesh[OuterIndex * 3 + 2] = World.Z;
        AcquirePanelsMath()->TransformXform(World, ZoneXf, LocalY);
        Mesh[InnerIndex * 3 + 0] = World.X;
        Mesh[InnerIndex * 3 + 1] = World.Y;
        Mesh[InnerIndex * 3 + 2] = World.Z;

        Angle = static_cast<float>(static_cast<double>(Angle) + AngleStep);
        Twice += 2;
    }

    // The pieces: the running end stays on the FPU. Each piece gets the bounding box of mesh
    // vertices 2*First .. 2*Last+1.
    float const Span = static_cast<float>(static_cast<double>(NumSegments) /
                                          static_cast<double>(PieceCount));
    float const kLastBias = 9.99999975e-05f; // DAT_005db098 (float 1e-4)
    double End = 0.0;                         // DAT_005c7474
    int PrevLast = 0;
    // The box lives across pieces: a piece with First > Last reuses the previous one.
    float MinX = 0.0f;
    float MinY = 0.0f;
    float MinZ = 0.0f;
    float MaxX = 0.0f;
    float MaxY = 0.0f;
    float MaxZ = 0.0f;
    for (int PieceIndex = 0; PieceIndex < PieceCount; ++PieceIndex)
    {
        FTexturePanelPiece* const Piece = &Pieces[PieceIndex];
        End += Span;
        Piece->FirstSegment = PrevLast;
        Piece->LastSegment = static_cast<int>(static_cast<double>(kLastBias) + End); // ftol
        PrevLast = Piece->LastSegment;

        int const FirstVertex = Piece->FirstSegment * 2;
        int const LastVertex = Piece->LastSegment * 2 + 1;
        for (int Vertex = FirstVertex; Vertex <= LastVertex; ++Vertex)
        {
            int MeshIndex = Vertex;
            if (Vertex >= MeshVertexCount)
                MeshIndex = Vertex - MeshVertexCount;
            float const X = Mesh[MeshIndex * 3 + 0];
            float const Y = Mesh[MeshIndex * 3 + 1];
            float const Z = Mesh[MeshIndex * 3 + 2];
            if (Vertex == FirstVertex)
            {
                MaxX = X;
                MinX = X;
                MaxY = Y;
                MinY = Y;
                MaxZ = Z;
                MinZ = Z;
            }
            // Per axis: a new minimum is not also tested as a maximum.
            if (X < MinX)
                MinX = X;
            else if (X > MaxX)
                MaxX = X;
            if (Y < MinY)
                MinY = Y;
            else if (Y > MaxY)
                MaxY = Y;
            if (Z < MinZ)
                MinZ = Z;
            else if (Z > MaxZ)
                MaxZ = Z;
        }

        // DAT_005c75e8 is the double 0.5.
        Piece->Center[0] = static_cast<float>((static_cast<double>(MaxX) + MinX) * 0.5);
        Piece->Center[1] = static_cast<float>((static_cast<double>(MaxY) + MinY) * 0.5);
        Piece->Center[2] = static_cast<float>((static_cast<double>(MaxZ) + MinZ) * 0.5);
        double const Dx = static_cast<double>(MaxX) - Piece->Center[0];
        double const Dy = static_cast<double>(MaxY) - Piece->Center[1];
        double const Dz = static_cast<double>(MaxZ) - Piece->Center[2];
        Piece->Radius = static_cast<float>(sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
    }

    // The perf option with IDS 0x658 (high_detail_textures, FUN_005b3770) drops the detail
    // shape when its applied value is below 0.4f (DAT_005d9638) or unordered.
    FPerfOption const* const HighDetail = FindPerfOptionByIds(0x658);
    if (HighDetail != nullptr && !(HighDetail->Applied >= 0.4f))
        DetailShape = nullptr;

    // A shape whose frame-0 handle (FUN_00425640) the pipeline rejects is dropped.
    if (Shape != nullptr &&
        DALib::Pipeline->IsTexture(TexShapeGetGpuHandle(Shape, 0)) != 0)
        Shape = nullptr;
    if (DetailShape != nullptr &&
        DALib::Pipeline->IsTexture(TexShapeGetGpuHandle(DetailShape, 0)) != 0)
        DetailShape = nullptr;

    // A VerifyState failure drops the detail shape and tries again, a second failure drops the
    // shape. Without a detail shape (from the start or dropped) the opacity is halved.
    if (Shape != nullptr || DetailShape != nullptr)
    {
        SetupPanelPipeline(this);
        if (DALib::Pipeline->VerifyState() < 0 && DetailShape != nullptr)
        {
            DetailShape = nullptr;
            SetupPanelPipeline(this);
            if (DALib::Pipeline->VerifyState() < 0)
                Shape = nullptr;
        }
        if (DetailShape == nullptr)
            Opacity = static_cast<float>(static_cast<double>(Opacity) * 0.5);
    }
}

// -------------------------------------------------------------------- FUN_00551500

namespace
{
    // DAT_00679864 -- incremented once per written puff; nothing in retail reads it back.
    int G_CloudsPuffsDrawn = 0;

    // DAT_00679860 through the lazy test retail repeats inline at each of its three reads.
    I3DMathEngine* CloudsMath()
    {
        if (G_CloudsMath == nullptr)
            G_CloudsMath = Acquire3DMathEngine();
        return G_CloudsMath;
    }

    // FUN_00507e70 (cdecl): one Newton step, returned on the FPU (same body as cli_nebula.cpp's
    // NebulaInvSqrt, which lives in that file's anonymous namespace).
    double CloudsInvSqrt(float X)
    {
        int XBits = 0;
        memcpy(&XBits, &X, sizeof(XBits));
        int const YBits = 0x5f3759df - (XBits >> 1);
        float Y = 0.0f;
        memcpy(&Y, &YBits, sizeof(Y));
        return (1.5f - static_cast<double>(Y) * Y * X * 0.5f) * Y;
    }

    // MSVCRT _ftol (FUN_005b7ec0): truncation toward zero into EDX:EAX; the callers here keep EAX.
    unsigned CloudsFtol(double X)
    {
        return static_cast<unsigned>(static_cast<long long>(X));
    }
}

// FUN_00551500 (__thiscall, RET 8): this = the clouds object, arg2 = the exclusion manager
// handed to FUN_005513b0. No null tests anywhere: not on the camera, its orientation, the
// pipeline, the vertex-buffer manager, the puff table or a puff.
void TexturePanelDrawClouds(FNebulaClouds* Clouds, void* Camera, void* ExclusionCtx)
{
    ICamera* const Cam = static_cast<ICamera*>(Camera);

    if (static_cast<unsigned char*>(Camera)[0xbc] != 0)
        NebulaCloudsRegenExcluded(Clouds, ExclusionCtx);

    // bLoaded +0xec, the active count +0x14 and the first shape +0x28.
    if (Clouds->bLoaded == 0)
        return;
    if (Clouds->ActiveCount == 0)
        return;
    if (Clouds->PuffShapes[0] == nullptr)
        return;

    // Twelve dwords copied from camera slot 4.
    FTransform CamXform;
    memcpy(&CamXform, Cam->GetOrientation(), sizeof(CamXform));
    FVector UnitZ = {0.0f, 0.0f, 1.0f};

    FVector W;
    CloudsMath()->Rotate(W, CamXform, UnitZ);

    // C1 = N x W (N = Up, +0x6c) through FUN_00422b80, each component stored as a float.
    float* const N = Clouds->Up;
    float C1[3];
    C1[0] = static_cast<float>(static_cast<double>(W.Z) * N[1] - static_cast<double>(W.Y) * N[2]);
    C1[1] = static_cast<float>(static_cast<double>(W.X) * N[2] - static_cast<double>(W.Z) * N[0]);
    C1[2] = static_cast<float>(static_cast<double>(W.Y) * N[0] - static_cast<double>(W.X) * N[1]);

    // C2 = W x C1, copied to N.
    float C2[3];
    C2[0] = static_cast<float>(static_cast<double>(W.Y) * C1[2] - static_cast<double>(W.Z) * C1[1]);
    C2[1] = static_cast<float>(static_cast<double>(W.Z) * C1[0] - static_cast<double>(W.X) * C1[2]);
    C2[2] = static_cast<float>(static_cast<double>(W.X) * C1[1] - static_cast<double>(W.Y) * C1[0]);
    N[0] = C2[0];
    N[1] = C2[1];
    N[2] = C2[2];

    // N normalised through FUN_00507e70.
    {
        float const LenSq = static_cast<float>(
            (static_cast<double>(N[0]) * N[0] + static_cast<double>(N[1]) * N[1]) +
            static_cast<double>(N[2]) * N[2]);
        double const Inv = CloudsInvSqrt(LenSq);
        N[0] = static_cast<float>(Inv * N[0]);
        N[1] = static_cast<float>(Inv * N[1]);
        N[2] = static_cast<float>(Inv * N[2]);
    }

    // DAT_005d8d40 = 0.8f, DAT_005d8e80 = 0.7f.
    float const Radius = Clouds->CloudRadius;
    float const AheadThreshold =
        static_cast<float>(static_cast<double>(Clouds->CentreAhead) * 0.8f);
    int const VertTotal = Clouds->PuffCount * 6;
    float const InnerRadius = static_cast<float>(static_cast<double>(Radius) * 0.7f);
    int VertCount = 0;

    // Three {pointer, stride} cursors, EH states 0, 1, 2 (unwound through FUN_00552b20).
    FBillboardVertexCursor PosCursor(0xc);
    FBillboardVertexCursor DiffCursor(4);
    FBillboardVertexCursor UvCursor(8);
    unsigned char bLocked = 0;

    // The first shape's alpha probe, cached in its +0x04 while negative (the format query's
    // result is untested).
    FTexShape* const Shape0 = Clouds->PuffShapes[0];
    if (Shape0->AlphaCache < 0)
    {
        int Fmt = PixelFormatFromD3D(0);
        DALib::TextureLib->GetGroupPixelFormat(Shape0->Texture, nullptr, &Fmt);
        Shape0->AlphaCache = static_cast<int>(TextureFormatHasAlpha(Fmt));
    }
    unsigned char const bAlpha = static_cast<unsigned char>(Shape0->AlphaCache != 0);

    IRenderPipeline* const Pipe = DALib::Pipeline;

    // DAT_005e09c8.
    for (unsigned long const* Pair = kRsCloudsDraw; Pair[0] != 0; Pair += 2)
        Pipe->SetRenderState((D3DRENDERSTATETYPE)Pair[0], static_cast<int>(Pair[1]));

    int Saved89 = 0;
    Pipe->GetRenderState(D3DRS_LIGHTING, &Saved89);
    Pipe->SetRenderState(D3DRS_LIGHTING, 0);
    int Saved16 = 0;
    Pipe->GetRenderState(D3DRS_CULLMODE, &Saved16);
    Pipe->SetRenderState(D3DRS_CULLMODE, 1);
    Pipe->SetRenderState(D3DRS_ZENABLE, 1);
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
    Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);

    // FUN_004215b0, FUN_00422690(0, &m).
    {
        float Ident12[12];
        Win3DIdentityRotation3x3(Ident12);
        Ident12[9] = 0.0f;
        Ident12[10] = 0.0f;
        Ident12[11] = 0.0f;
        Win3DBindModelview(nullptr, Ident12);
    }

    // Retail quirk: without alpha both stores name SRCBLEND (0x13); DESTBLEND is never set.
    if (bAlpha != 0)
    {
        Pipe->SetRenderState(D3DRS_SRCBLEND, 5);
        Pipe->SetRenderState(D3DRS_DESTBLEND, 6);
    }
    else
    {
        Pipe->SetRenderState(D3DRS_SRCBLEND, 2);
        Pipe->SetRenderState(D3DRS_SRCBLEND, 4);
    }

    Pipe->SetTextureStageTexture(0, TexShapeGetGpuHandle(Shape0, 0));

    // DAT_005e09f0 on stage 0, DAT_005e0a28 on stage 1.
    for (unsigned long const* Pair = kTssClouds0; Pair[0] != 0; Pair += 2)
        Pipe->SetTextureStageState(0, static_cast<int>(Pair[0]), static_cast<int>(Pair[1]));
    for (unsigned long const* Pair = kTssClouds1; Pair[0] != 0; Pair += 2)
        Pipe->SetTextureStageState(1, static_cast<int>(Pair[0]), static_cast<int>(Pair[1]));

    int Saved18 = 0;
    Pipe->GetRenderState(D3DRS_ALPHAREF, &Saved18);
    Pipe->SetRenderState(D3DRS_ALPHAREF, 5);

    FVector V;
    CloudsMath()->InverseRotate(V, CamXform, *reinterpret_cast<FVector*>(N));

    // P = V projected onto the view XY plane and normalised. P.z is float(Inv * 0); the FPU
    // keeps that product and multiplies it by 0 again for the cross product below.
    float P[3];
    double PzZero = 0.0;
    {
        double const Inv = CloudsInvSqrt(static_cast<float>(
            static_cast<double>(V.X) * V.X + static_cast<double>(V.Y) * V.Y));
        P[0] = static_cast<float>(V.X * Inv);
        P[1] = static_cast<float>(V.Y * Inv);
        double const Pz = Inv * 0.0f;
        P[2] = static_cast<float>(Pz);
        PzZero = Pz * 0.0f;
    }

    // Q = P x (0, 0, 1) with the zero terms kept (FUN_00422b80).
    float Q[3];
    Q[0] = static_cast<float>(P[1] - PzZero);
    Q[1] = static_cast<float>(PzZero - P[0]);
    Q[2] = static_cast<float>(static_cast<double>(P[0]) * 0.0f - static_cast<double>(P[1]) * 0.0f);

    // Q normalised, in retail's summation order.
    {
        float const LenSq = static_cast<float>(
            (static_cast<double>(Q[1]) * Q[1] + static_cast<double>(Q[2]) * Q[2]) +
            static_cast<double>(Q[0]) * Q[0]);
        double const Inv = CloudsInvSqrt(LenSq);
        Q[0] = static_cast<float>(Q[0] * Inv);
        Q[1] = static_cast<float>(Q[1] * Inv);
        Q[2] = static_cast<float>(Q[2] * Inv);
    }

    // DAT_005c75dc = 1.0f.
    float const RadiusSq = static_cast<float>(static_cast<double>(Radius) * Radius);
    float const InnerSq = static_cast<float>(static_cast<double>(InnerRadius) * InnerRadius);
    float const InvRing =
        static_cast<float>(1.0f / (static_cast<double>(RadiusSq) - InnerSq));
    float const NearLo = Clouds->NearFadeStart;
    float const NearHi = Clouds->NearFadeEnd;
    float const InvNear = static_cast<float>(1.0f / (static_cast<double>(NearHi) - NearLo));

    FVertexMemoryLock Desc = {};
    FNebulaCloudPuff** const Table = Clouds->PuffTable;
    // The count +0xc is re-read at every iteration's test; the table pointer is read once.
    for (int I = 0; I < Clouds->PuffCount; ++I)
    {
        // The puff must be active (+0x50) and not hidden (+0x51).
        float Alpha = static_cast<float>(static_cast<double>(Clouds->PuffMaxAlpha) * Clouds->Fade);
        FNebulaCloudPuff* const Puff = Table[I];
        if (Puff->bActive == 0 || Puff->bHidden != 0)
            continue;

        FVector VP;
        CloudsMath()->InverseTransform(VP, CamXform, *reinterpret_cast<FVector*>(Puff->Position));
        double const Depth = -static_cast<double>(VP.Z);
        Puff->ViewDepth = static_cast<float>(Depth);

        // The ordered/unordered outcome of every compare below is retail's.
        if (Depth < NearLo)
            continue;
        if (Depth > AheadThreshold)
        {
            // The squared length is kept on the FPU and also stored as a float.
            float T[3];
            T[0] = 0.0f;
            T[1] = 0.0f;
            T[2] = -Clouds->CentreAhead;
            float D[3];
            Win3DVec3Sub(D, T, &VP.X);
            double const Dist2 = (static_cast<double>(D[2]) * D[2] + static_cast<double>(D[1]) * D[1]) +
                                 static_cast<double>(D[0]) * D[0];
            float const Dist2f = static_cast<float>(Dist2);
            if (Dist2 > RadiusSq)
                continue;
            if (Dist2f > InnerSq)
                Alpha = static_cast<float>(((static_cast<double>(RadiusSq) - Dist2f) * Alpha) * InvRing);
        }
        // DAT_005e0e08 (double 0.02).
        if (Alpha < 0.02)
            continue;

        // FUN_00411d60 twice.
        float Wisp1[3];
        ScaleVec3(Wisp1, Clouds->LightningColor, Puff->LightningFlash);
        float Wisp2[3];
        ScaleVec3(Wisp2, Wisp1, Clouds->LightningIntensity);

        // The stored float depth is tested again here.
        float const Size = Puff->Size;
        float Alpha2 = Alpha;
        float const PuffDepth = Puff->ViewDepth;
        if (PuffDepth <= NearLo)
            continue;
        if (PuffDepth < NearHi)
            Alpha2 = static_cast<float>(((static_cast<double>(PuffDepth) - NearLo) * Alpha) * InvNear);
        if (Alpha2 < 0.02)
            continue;

        float Col[3];
        Col[0] = Puff->Color[0];
        Col[1] = Puff->Color[1];
        Col[2] = Puff->Color[2];

        // The wisp is added (FUN_004d7270) and each channel not ordered-less than 1 is set to 1.0f.
        if (Clouds->DisableLightningFlash == 0 && Clouds->LightningIntensity > 0.0f &&
            Puff->LightningTimer >= 0.0f)
        {
            Vec3AddInPlace(Col, Wisp2);
            if (!(Col[0] < 1.0f))
                Col[0] = 1.0f;
            if (!(Col[1] < 1.0f))
                Col[1] = 1.0f;
            if (!(Col[2] < 1.0f))
                Col[2] = 1.0f;
        }

        // The diffuse word through _ftol (DAT_005c75d8 = 255.0f). Without alpha A = 0xff and each
        // channel is colour * alpha2. Only the low byte of each _ftol survives.
        unsigned Diffuse = 0;
        if (bAlpha != 0)
        {
            unsigned const R = CloudsFtol(static_cast<double>(Col[0]) * 255.0f);
            unsigned const A = CloudsFtol(static_cast<double>(Alpha2) * 255.0f);
            unsigned Word = ((R & 0xffu) | (A << 8)) << 8;
            unsigned const G = CloudsFtol(static_cast<double>(Col[1]) * 255.0f);
            Word = (Word | (G & 0xffu)) << 8;
            unsigned const Bl = CloudsFtol(static_cast<double>(Col[2]) * 255.0f);
            Diffuse = Word | (Bl & 0xffu);
        }
        else
        {
            unsigned const R = CloudsFtol((static_cast<double>(Col[0]) * Alpha2) * 255.0f);
            unsigned Word = (R | 0xffffff00u) << 8;
            unsigned const G = CloudsFtol((static_cast<double>(Col[1]) * Alpha2) * 255.0f);
            Word = (Word | (G & 0xffu)) << 8;
            unsigned const Bl = CloudsFtol((static_cast<double>(Col[2]) * Alpha2) * 255.0f);
            Diffuse = Word | (Bl & 0xffu);
        }

        // The first puff that gets here locks the vertex buffer. The flag is set before the
        // call, so a failed lock is never retried and the cursors keep their null pointers (and
        // the writes below go through them).
        if (bLocked == 0)
        {
            bLocked = 1;
            if (DALib::VertexBuffer->LockVertexMemory(0x142, static_cast<unsigned int>(VertTotal),
                                                       &Desc) == GR_OK)
            {
                unsigned const Fvf = static_cast<unsigned>(Desc.Listing.Fvf);
                unsigned char* const Base = (unsigned char*)Desc.Vertices;
                int const NormalBytes = (Fvf & 0x10) != 0 ? 0xc : 0;
                int const DiffBytes = FvfDiffuseNormalBytes(static_cast<unsigned char>(Fvf));
                int const PosBytes = FvfPositionBytes(static_cast<unsigned char>(Fvf)) + NormalBytes;
                int const Stride = FvfTexcoordBlockBytes(Fvf) + (PosBytes + DiffBytes);
                PosCursor.Ptr = Base;
                PosCursor.Stride = Stride;
                DiffCursor.Ptr = Base + (FvfPositionBytes(static_cast<unsigned char>(Fvf)) + NormalBytes);
                DiffCursor.Stride = Stride;
                UvCursor.Ptr = Base + (FvfPositionBytes(static_cast<unsigned char>(Fvf)) +
                                       (NormalBytes + FvfDiffuseNormalBytes(static_cast<unsigned char>(Fvf))));
                UvCursor.Stride = Stride;
            }
        }

        FTexShape const* const Shape = Clouds->PuffShapes[Puff->ShapeIndex];
        if (Shape == nullptr)
            continue;

        // FUN_00411d60, FUN_00423b60 / FUN_00423b30.
        float SP[3];
        ScaleVec3(SP, P, Size);
        float SQ[3];
        ScaleVec3(SQ, Q, Size);
        float Tmp[3];
        float C0[3];
        Win3DVec3Sub(Tmp, &VP.X, SQ);
        Win3DVec3Sub(C0, Tmp, SP);
        float Cc1[3];
        Win3DVec3Add(Tmp, &VP.X, SQ);
        Win3DVec3Sub(Cc1, Tmp, SP);
        float Cc2[3];
        Win3DVec3Add(Tmp, &VP.X, SQ);
        Win3DVec3Add(Cc2, Tmp, SP);
        float Cc3[3];
        Win3DVec3Sub(Tmp, &VP.X, SQ);
        Win3DVec3Add(Cc3, Tmp, SP);

        // Two triangles, one diffuse word.
        float const* const Pos[6] = {C0, Cc1, Cc2, Cc2, Cc3, C0};
        float const Us[6] = {Shape->U0, Shape->U1, Shape->U1, Shape->U1, Shape->U0, Shape->U0};
        float const Vs[6] = {Shape->V0, Shape->V0, Shape->V1, Shape->V1, Shape->V1, Shape->V0};
        for (int Vert = 0; Vert < 6; ++Vert)
        {
            float* const OutPos = reinterpret_cast<float*>(PosCursor.Ptr);
            OutPos[0] = Pos[Vert][0];
            OutPos[1] = Pos[Vert][1];
            OutPos[2] = Pos[Vert][2];
            *reinterpret_cast<unsigned*>(DiffCursor.Ptr) = Diffuse;
            float* const OutUv = reinterpret_cast<float*>(UvCursor.Ptr);
            OutUv[0] = Us[Vert];
            OutUv[1] = Vs[Vert];
            PosCursor.Ptr += PosCursor.Stride;
            DiffCursor.Ptr += DiffCursor.Stride;
            UvCursor.Ptr += UvCursor.Stride;
        }

        ++G_CloudsPuffsDrawn;
        VertCount += 6;
    }

    // After a lock (even a failed one) the buffer is unlocked and drawn.
    if (bLocked != 0)
    {
        DALib::VertexBuffer->UnlockVertexMemory(&Desc);
        Pipe->DrawPrimitiveVb(4, Desc.Listing.VertexBuffer, static_cast<unsigned int>(Desc.Listing.BaseVertex),
                              static_cast<unsigned int>(VertCount), nullptr);
    }

    Pipe->SetRenderState(D3DRS_ALPHAFUNC, 5);
    Pipe->SetRenderState(D3DRS_ALPHAREF, Saved18);
    Pipe->SetRenderState(D3DRS_LIGHTING, Saved89);
    Pipe->SetRenderState(D3DRS_CULLMODE, Saved16);
    for (unsigned long const* Pair = kRsCloudsRestore; Pair[0] != 0; Pair += 2)
        Pipe->SetRenderState((D3DRENDERSTATETYPE)Pair[0], static_cast<int>(Pair[1]));
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
}

// -------------------------------------------------------------------- LAB_005091a0

namespace
{
    // DAT_00675004 -- write-only (zeroed by LAB_005091a0).
    int G_NebulaCloudsPassWord = 0;
    // DAT_0067500c -- read once, never written in retail, so the back-buffer lock below never
    // runs; kept with its test as written.
    int G_NebulaCloudsLockBackBuffer = 0;
}

// LAB_005091a0 (__thiscall, RET 4). No null tests on This, the list, a node's clouds, the
// Pipeline or the MaterialBatcher.
void NebulaDrawCloudsPanels(FNebulaEffect* Nebula, void* Camera)
{
    if (Nebula->FlagLoaded == 0)
        return;

    // FUN_004101e0 turns every registered light off; FUN_00422190(0).
    DALib::MaterialBatcher->SetBinOrder(1u, -1);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_LIGHTING, 0);
    EngineLightClearRegisteredList();
    unsigned long Saved8b = 0;
    PipelineGetRenderState(DALib::Pipeline, D3DRS_AMBIENT, &Saved8b);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_AMBIENT, static_cast<unsigned long>(-1));
    Win3DSetDepthWrite(0);

    // FUN_00422440.
    FogApplyColorAndRange(Nebula->FogColor, Nebula->FogStart, Nebula->FogEnd);
    G_NebulaCloudsPassWord = 0;
    PipelineSetRenderState(DALib::Pipeline, D3DRS_CULLMODE, 1);

    FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
    for (FNebulaCloudsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        TexturePanelDrawClouds(N->Clouds, Camera, Nebula->ExclusionMgr);

    // FUN_00422190(1); FUN_00421ed0(0xb60).
    DALib::MaterialBatcher->SetBinOrder(1u, 1);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_CULLMODE, 2);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_LIGHTING, 1);
    PipelineSetRenderState(DALib::Pipeline, D3DRS_AMBIENT, Saved8b);
    Win3DSetDepthWrite(1);
    Win3DDisable(0xb60);

    // Dead in retail (see DAT_0067500c).
    if (G_NebulaCloudsLockBackBuffer != 0)
    {
        unsigned long LockDesc[5];
        LockDesc[4] = static_cast<unsigned long>(PixelFormatFromD3D(0));
        DALib::Pipeline->LockBuffer(LockDesc);
        DALib::Pipeline->UnlockBuffer();
    }
}

unsigned char TexturePanelsFormatHasAlpha(int FormatId)
{
    return TextureFormatHasAlpha(FormatId);
}

int TexturePanelsFvfPositionBytes(unsigned char FvfLow)
{
    return FvfPositionBytes(FvfLow);
}

int TexturePanelsFvfDiffuseNormalBytes(unsigned char FvfLow)
{
    return FvfDiffuseNormalBytes(FvfLow);
}
