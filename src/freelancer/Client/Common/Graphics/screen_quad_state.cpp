// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/Graphics/screen_quad_state.h"

#include "common_csys.h"
#include "DALib.h"
#include "dacom.h"            // DACOM_CRC::GetCRC32
#include "Core/globals.h"          // G_RenderSurfaceWidth / G_RenderSurfaceHeight (DAT_00610850 / 854)
#include "render/IRenderPipeline.h"
#include "Space/Cameras/space_camera.h"
#include "Interface/win3d_immediate.h"  // Win3DPushProjection / Win3DSetOrtho2D / Win3DPopProjection
#include "x86math.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <utility>
#include <windows.h>

// DAT_006101d8 -- the default texture frame {no texture, u0 0, v0 0, u1 1, v1 1} (.data, never
// written); see the header.
unsigned long const G_ScreenQuadDefaultFrame[5] = {0u, 0u, 0u, 0x3f800000u, 0x3f800000u};

namespace
{
    static const unsigned kScreenQuadFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1;

    // FVF 0x1C4 vertex — FUN_004042c0 expand shape (8 dwords / 0x20).
    struct FScreenQuadGpuVert
    {
        float X;
        float Y;
        float Z;
        float Rhw;
        unsigned Color;
        unsigned Specular;
        float U;
        float V;
    };

    // Retail material node — exactly 0x28 (FUN_00403160 / FUN_00402ed0).
    // +0x08/+0x0c/+0x10 = AABB record buffer (stride 0x2C); +0x14/+0x18/+0x1c =
    // rotated FVF verts (stride 0x20); +0x20 texture key; +0x24 blend.
    struct FScreenQuadBatchNode
    {
        FScreenQuadBatchNode* Next;        // +0x00
        FScreenQuadBatchNode* Prev;        // +0x04
        FScreenQuadRecord* Records;        // +0x08
        int RecordCount;                   // +0x0c
        int RecordCapacity;                // +0x10
        FScreenQuadGpuVert* RotatedVerts;  // +0x14
        int RotVertCount;                  // +0x18
        int RotVertCapacity;               // +0x1c
        void* TextureHandle;               // +0x20
        int BlendMode;                     // +0x24
    };
    static_assert(sizeof(FScreenQuadBatchNode) == 0x28, "batch node 0x28");

    // DAT_00616498 batch container (map value body). Free list at +0x00,
    // live list at +0x0c, current at +0x18, 5-dword material key at +0x1c.
    struct FScreenQuadBatchContainer
    {
        FScreenQuadBatchNode* FreeHead; // +0x00
        FScreenQuadBatchNode* FreeTail; // +0x04
        int FreeCount;                  // +0x08
        FScreenQuadBatchNode* LiveHead; // +0x0c
        FScreenQuadBatchNode* LiveTail; // +0x10
        int LiveCount;                  // +0x14
        FScreenQuadBatchNode* Current;  // +0x18
        unsigned MatKey[5];             // +0x1c..+0x2c (key[0]=tex; [1..4]=UV floats)
        int Sentinel;                   // +0x30 = -1
    };

    // DAT_00616498 -- the container of the current material key inside the map DAT_00616478
    // (defined below, after the container destructor), or null.
    FScreenQuadBatchContainer* G_ActiveContainer = nullptr;

    // DAT_00616474 — FUN_00402d30 material-list key; change nulls DAT_00616498.
    unsigned long G_MaterialListKey = 0;
    // DAT_00616490 — default key restored by FUN_00402d50 / FUN_00403ea0 epilogue.
    unsigned long G_MaterialListKeyDefault = 0;

    // DAT_00616494 — lazy 3DMathEngine (FUN_005b73e0 CreateInstance "3DMathEngine").
    I3DMathEngine* G_ScreenQuadMath = nullptr;


    I3DMathEngine* AcquireScreenQuadMath()
    {
        // FUN_005b73e0 → CreateInstance("3DMathEngine"); cached as DAT_00616494.
        if (G_ScreenQuadMath == nullptr)
            G_ScreenQuadMath = Acquire3DMathEngine();
        return G_ScreenQuadMath;
    }

    // FUN_00403160 (thiscall on the node) -- every dword zeroed, then +0xc and +0x18 zeroed
    // again. No null check in retail.
    void InitBatchNode(FScreenQuadBatchNode* Node)
    {
        Node->Next = nullptr;
        Node->Prev = nullptr;
        Node->Records = nullptr;
        Node->RecordCount = 0;
        Node->RecordCapacity = 0;
        Node->RotatedVerts = nullptr;
        Node->RotVertCount = 0;
        Node->RotVertCapacity = 0;
        Node->TextureHandle = nullptr;
        Node->BlendMode = 0;
        Node->RecordCount = 0;
        Node->RotVertCount = 0;
    }

    // FUN_004031e0 (thiscall on the node) -- the fields are not cleared (the sole caller
    // FUN_00403190 frees the node next). No null check in retail.
    void ReleaseNodeBuffers(FScreenQuadBatchNode* Node)
    {
        operator delete(Node->RotatedVerts);
        operator delete(Node->Records);
    }

    // FUN_00402ed0 (thiscall on the free list at container+0) -- the free head, or a new node
    // zeroed as FUN_00403160; a failed new returns 0 (the VC6 operator new returns null, hence
    // nothrow here).
    FScreenQuadBatchNode* AllocBatchNode(FScreenQuadBatchContainer* Pool)
    {
        FScreenQuadBatchNode* Node = Pool->FreeHead;
        if (Node == nullptr)
        {
            Node = static_cast<FScreenQuadBatchNode*>(operator new(0x28u, std::nothrow));
            if (Node == nullptr)
                return nullptr;
            InitBatchNode(Node);
            return Node;
        }
        if (Node->Prev == nullptr)
            Pool->FreeHead = Node->Next;
        else
            Node->Prev->Next = Node->Next;
        if (Node->Next == nullptr)
            Pool->FreeTail = Node->Prev;
        else
            Node->Next->Prev = Node->Prev;
        Pool->FreeCount -= 1;
        return Node;
    }

    // FUN_00404580 (thiscall on the free list at container+0, RET 4) -- a null node is ignored
    // (the only check retail makes).
    void SpliceOntoFreeList(FScreenQuadBatchContainer* Pool, FScreenQuadBatchNode* Node)
    {
        if (Node == nullptr)
            return;
        FScreenQuadBatchNode* OldTail = Pool->FreeTail;
        Pool->FreeTail = Node;
        if (OldTail == nullptr)
        {
            Pool->FreeHead = Node;
            Node->Next = nullptr;
            Node->Prev = nullptr;
        }
        else
        {
            OldTail->Next = Node;
            Node->Next = nullptr;
            Node->Prev = OldTail;
        }
        Pool->FreeCount += 1;
    }

    // FUN_00404d90 (thiscall on a {head, tail, count} list, RET 4; here the live list at
    // container+0xc). No null checks in retail.
    void UnlinkFromLiveList(FScreenQuadBatchContainer* Pool, FScreenQuadBatchNode* Node)
    {
        if (Node->Prev == nullptr)
            Pool->LiveHead = Node->Next;
        else
            Node->Prev->Next = Node->Next;
        if (Node->Next == nullptr)
            Pool->LiveTail = Node->Prev;
        else
            Node->Next->Prev = Node->Prev;
        Pool->LiveCount -= 1;
    }

    // FUN_00403190 (thiscall on the free list). No null check in retail.
    void DestroyFreeList(FScreenQuadBatchContainer* Pool)
    {
        while (Pool->FreeHead != nullptr)
        {
            FScreenQuadBatchNode* Node = Pool->FreeHead;
            if (Node->Prev == nullptr)
                Pool->FreeHead = Node->Next;
            else
                Node->Prev->Next = Node->Next;
            if (Node->Next == nullptr)
                Pool->FreeTail = Node->Prev;
            else
                Node->Next->Prev = Node->Prev;
            Pool->FreeCount -= 1;
            ReleaseNodeBuffers(Node);
            operator delete(Node);
        }
    }

    // FUN_00403200 (thiscall on the container) -- live nodes moved to the free list (inline
    // FUN_00404d90 / FUN_00404580), then every free node freed. No null check, and +0x18 is
    // left as it is.
    void DestroyBatchContainer(FScreenQuadBatchContainer* Pool)
    {
        while (Pool->LiveHead != nullptr)
        {
            FScreenQuadBatchNode* Node = Pool->LiveHead;
            UnlinkFromLiveList(Pool, Node);
            SpliceOntoFreeList(Pool, Node);
        }
        while (Pool->FreeHead != nullptr)
        {
            FScreenQuadBatchNode* Node = Pool->FreeHead;
            if (Node->Prev == nullptr)
                Pool->FreeHead = Node->Next;
            else
                Node->Prev->Next = Node->Next;
            if (Node->Next == nullptr)
                Pool->FreeTail = Node->Prev;
            else
                Node->Next->Prev = Node->Prev;
            Pool->FreeCount -= 1;
            operator delete(Node->RotatedVerts);
            operator delete(Node->Records);
            operator delete(Node);
        }
    }

    // The value of one DAT_00616478 map node: the container at node+0x10. Its destructor is the
    // map's value destructor FUN_00405820 = FUN_004032a0. Retail inserts a copy of a temporary
    // (FUN_004045b0 / the inline copy in FUN_00402f40); constructing the entry in place gives the
    // same value. The key words +0x1c..+0x2c of that temporary are never initialised in retail;
    // every insert path overwrites them from DAT_006101d8 before they are read.
    struct FScreenQuadBatchEntry
    {
        FScreenQuadBatchContainer Container;

        FScreenQuadBatchEntry()
        {
            Container.FreeHead = nullptr;
            Container.FreeTail = nullptr;
            Container.FreeCount = 0;
            Container.LiveHead = nullptr;
            Container.LiveTail = nullptr;
            Container.LiveCount = 0;
            Container.Current = nullptr;
            for (int Index = 0; Index < 5; ++Index)
                Container.MatKey[Index] = 0;
            Container.Sentinel = -1;
        }

        ~FScreenQuadBatchEntry()
        {
            DestroyBatchContainer(&Container);
        }

        FScreenQuadBatchEntry(FScreenQuadBatchEntry const&) = delete;
        FScreenQuadBatchEntry& operator=(FScreenQuadBatchEntry const&) = delete;
    };

    // FUN_004032a0 -- FUN_00403200's body on the container at this + 4 (the map node's value).
    void DestroyBatchContainerEmbedded(
        std::pair<unsigned long const, FScreenQuadBatchEntry>* Owner)
    {
        FScreenQuadBatchContainer* Pool = &Owner->second.Container;
        DestroyBatchContainer(Pool);
    }

    // DAT_00616478 -- std::map<material-list key, container> (head DAT_0061647c, size
    // DAT_00616488). Static init LAB_00402c10 / atexit LAB_00402c40.
    std::map<unsigned long, FScreenQuadBatchEntry> G_BatchMap;


    // FUN_00404d50 as the insert paths use it (the live list at container+0xc, before node 0):
    // Node goes to the live tail.
    void AppendToLiveList(FScreenQuadBatchContainer* Pool, FScreenQuadBatchNode* Node)
    {
        FScreenQuadBatchNode* const OldTail = Pool->LiveTail;
        Pool->LiveTail = Node;
        if (OldTail == nullptr)
            Pool->LiveHead = Node;
        else
            OldTail->Next = Node;
        Node->Next = nullptr;
        Node->Prev = OldTail;
        Pool->LiveCount += 1;
    }

    // FUN_004045d0 (thiscall on a container, RET 0xc: frame, blend, bFind) -- the body
    // FUN_00402e30 inlines after FUN_00402f40.
    FScreenQuadBatchNode* ContainerBeginBatch(FScreenQuadBatchContainer* C,
                                              unsigned long const* Frame, int BlendMode,
                                              bool bFind)
    {
        for (int Index = 0; Index < 5; ++Index)
            C->MatKey[Index] = static_cast<unsigned>(Frame[Index]);
        void* const Texture = reinterpret_cast<void*>(static_cast<uintptr_t>(C->MatKey[0]));
        if (C->Current != nullptr && C->Current->TextureHandle != Texture)
        {
            if (!bFind)
            {
                C->Current = nullptr;
            }
            else
            {
                C->Current = C->LiveHead;
                while (C->Current != nullptr && C->Current->TextureHandle != Texture)
                    C->Current = C->Current->Next;
            }
        }
        if (C->Current == nullptr)
        {
            FScreenQuadBatchNode* const Node = AllocBatchNode(C);
            C->Current = Node;
            AppendToLiveList(C, Node);
            Node->TextureHandle = Texture;
            Node->BlendMode = BlendMode;
            Node->RecordCount = 0;
            Node->RotVertCount = 0;
        }
        return C->Current;
    }

    // FUN_00402f40 -- DAT_00616498, found or inserted.
    FScreenQuadBatchContainer* EnsureBatchContainer()
    {
        if (G_ActiveContainer != nullptr)
            return G_ActiveContainer;

        // FUN_00404f20; an existing entry for the key is left untouched.
        std::pair<std::map<unsigned long, FScreenQuadBatchEntry>::iterator, bool> const Inserted =
            G_BatchMap.try_emplace(G_MaterialListKey);
        FScreenQuadBatchContainer* const C = &Inserted.first->second.Container;
        G_ActiveContainer = C;
        if (!Inserted.second)
            return C;

        // A new entry takes the default frame as its key.
        for (int Index = 0; Index < 5; ++Index)
            C->MatKey[Index] = static_cast<unsigned>(G_ScreenQuadDefaultFrame[Index]);
        void* const Texture = reinterpret_cast<void*>(static_cast<uintptr_t>(C->MatKey[0]));

        // FUN_004045d0's search inlined (a new entry has no nodes).
        if (C->Current != nullptr && C->Current->TextureHandle != Texture)
        {
            C->Current = C->LiveHead;
            while (C->Current != nullptr && C->Current->TextureHandle != Texture)
                C->Current = C->Current->Next;
        }

        if (C->Current == nullptr)
        {
            FScreenQuadBatchNode* const Node = AllocBatchNode(C);
            C->Current = Node;
            AppendToLiveList(C, Node);
            Node->TextureHandle = Texture;
            Node->BlendMode = 0;
            Node->RecordCount = 0;
            Node->RotVertCount = 0;
        }
        return C;
    }

    // The container's UV rect, key words +0x20..+0x2c, read as dwords (FUN_004034a0,
    // FUN_004038f0). The container always exists when these are read (FUN_00402f40 never
    // returns null).
    float BatchU0(FScreenQuadBatchContainer* C)
    {
        float V = 0.0f;
        memcpy(&V, &C->MatKey[1], sizeof(V));
        return V;
    }
    float BatchV0(FScreenQuadBatchContainer* C)
    {
        float V = 0.0f;
        memcpy(&V, &C->MatKey[2], sizeof(V));
        return V;
    }
    float BatchU1(FScreenQuadBatchContainer* C)
    {
        float V = 0.0f;
        memcpy(&V, &C->MatKey[3], sizeof(V));
        return V;
    }
    float BatchV1(FScreenQuadBatchContainer* C)
    {
        float V = 0.0f;
        memcpy(&V, &C->MatKey[4], sizeof(V));
        return V;
    }
    // The record append retail inlines (FUN_004038f0, FUN_004034a0, LAB_004137a0). The capacity
    // compare is signed.
    void AppendRecordToNode(FScreenQuadBatchNode* Node, FScreenQuadRecord const& Record)
    {
        int const Count = Node->RecordCount;
        Node->RecordCount = Count + 1;
        if (Count + 1 > Node->RecordCapacity)
        {
            Node->RecordCapacity = Count + 1 + 0x20;
            FScreenQuadRecord* const Grown = static_cast<FScreenQuadRecord*>(
                operator new(static_cast<size_t>(Node->RecordCapacity) * 0x2cu));
            if (Node->Records != nullptr)
                memcpy(Grown, Node->Records, static_cast<size_t>(Count) * 0x2cu);
            operator delete(Node->Records);
            Node->Records = Grown;
        }
        Node->Records[Count] = Record;
    }

    // One FVF 0x1c4 vertex of an axis-aligned record, as FUN_004042c0 writes it: Z = +0x10,
    // RHW = +0x14, the colour dword +0x28 copied raw, specular 0.
    void WriteRecordVertex(FScreenQuadGpuVert* Out, FScreenQuadRecord const& Rec, float X, float Y,
                           float U, float V)
    {
        Out->X = X;
        Out->Y = Y;
        Out->Z = Rec.DepthA;
        Out->Rhw = Rec.DepthB;
        Out->Color = Rec.ColorBits;
        Out->Specular = 0;
        Out->U = U;
        Out->V = V;
    }

    // FUN_004042c0 (thiscall on a node, RET 0x10: pipeline, vertex-buffer manager, index buffer,
    // draw). No argument is null-checked in retail.
    void FlushOneBatch(FScreenQuadBatchNode* Node, IRenderPipeline* Pipe,
                       IVertexBufferManager* Vbm, IRPIndexBuffer* Ib, IRPDraw* Draw)
    {
        Pipe->SetTextureStageTexture(0, Node->TextureHandle);

        // Any other blend mode leaves the blend factors alone.
        int const BlendMode = Node->BlendMode;
        if (BlendMode == 1)
        {
            Pipe->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            Pipe->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        }
        else if (BlendMode == 2)
        {
            Pipe->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            Pipe->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        }
        Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, (BlendMode != 0) ? 1 : 0);

        unsigned int const VertexCount =
            static_cast<unsigned int>(Node->RotVertCount + Node->RecordCount * 4);
        if (VertexCount != 0)
        {
            FVertexMemoryLock Lock;
            if (Vbm->LockVertexMemory(kScreenQuadFvf, VertexCount, &Lock) >= 0)
            {
                // The record count is re-read every pass.
                FScreenQuadGpuVert* Dst =
                    static_cast<FScreenQuadGpuVert*>(Lock.Vertices);
                for (int Index = 0; Index < Node->RecordCount; ++Index)
                {
                    FScreenQuadRecord const& Rec = Node->Records[Index];
                    WriteRecordVertex(Dst + 0, Rec, Rec.X0, Rec.Y0, Rec.U0, Rec.V0);
                    WriteRecordVertex(Dst + 1, Rec, Rec.X0, Rec.Y1, Rec.U0, Rec.V1);
                    WriteRecordVertex(Dst + 2, Rec, Rec.X1, Rec.Y1, Rec.U1, Rec.V1);
                    WriteRecordVertex(Dst + 3, Rec, Rec.X1, Rec.Y0, Rec.U1, Rec.V0);
                    Dst += 4;
                }

                // The rotated vertices follow the records.
                memcpy(Dst, Node->RotatedVerts,
                       static_cast<size_t>(Node->RotVertCount) * sizeof(FScreenQuadGpuVert));

                Vbm->UnlockVertexMemory(&Lock);

                // Six indices per quad from the index buffer's scratch (-1).
                unsigned int const QuadCount = VertexCount >> 2;
                unsigned int const IndexCount = QuadCount * 6u;
                void* const ScratchIb = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
                unsigned int IndexOffset = 0;
                void* Indices = nullptr;
                if (Ib->LockIb(ScratchIb, &IndexOffset, &Indices, IndexCount) >= 0)
                {
                    unsigned short* Out = static_cast<unsigned short*>(Indices);
                    unsigned int Base = 0;
                    for (unsigned int Quad = 0; Quad < QuadCount; ++Quad)
                    {
                        Out[0] = static_cast<unsigned short>(Base);
                        Out[1] = static_cast<unsigned short>(Base + 1u);
                        Out[2] = static_cast<unsigned short>(Base + 2u);
                        Out[3] = static_cast<unsigned short>(Base);
                        Out[4] = static_cast<unsigned short>(Base + 2u);
                        Out[5] = static_cast<unsigned short>(Base + 3u);
                        Out += 6;
                        Base += 4u;
                    }

                    Ib->UnlockIb(ScratchIb);
                    if (Ib->SelectIb(ScratchIb, static_cast<unsigned int>(Lock.Listing.BaseVertex), 0, 0) >= 0)
                        Draw->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, VertexCount, IndexOffset, IndexCount);
                }
            }
        }

        // ALPHABLENDENABLE off on every path.
        Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    }

    // FUN_00403ea0's drain loop (inlined twice): FUN_004042c0 runs with the DALib globals (not
    // the caller's pipeline); the freed nodes keep their counts.
    void FlushContainer(FScreenQuadBatchContainer* C)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        IVertexBufferManager* const Vbm = DALib::VertexBuffer;
        IRPIndexBuffer* const Ib = DALib::RPIndexBuffer;
        IRPDraw* const Draw = DALib::RPDraw;
        while (C->LiveHead != nullptr)
        {
            FScreenQuadBatchNode* const Node = C->LiveHead;
            if (Node->RotVertCount + Node->RecordCount * 4 != 0)
                FlushOneBatch(Node, Pipe, Vbm, Ib, Draw);
            UnlinkFromLiveList(C, Node);
            SpliceOntoFreeList(C, Node);
        }
        C->Current = nullptr;
    }

}

FScreenQuadRenderStateSaver::FScreenQuadRenderStateSaver(IRenderPipeline* InPipe, int InState,
                                                         int Value)
    : Pipe(InPipe), State(InState), Saved(0)
{
    Pipe->GetRenderState((D3DRENDERSTATETYPE)State, &Saved);
    Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Value);
}

FScreenQuadRenderStateSaver::~FScreenQuadRenderStateSaver()
{
    // FUN_00405840 (inlined at most sites).
    Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Saved);
}

int ScreenQuadClip1D(float* Edge0, float* Edge1, float* Attr0, float* Attr1, float Lo, float Hi,
                     float Span)
{
    // FUN_00404670 (cdecl). No guard on Span: retail divides by it as given. Every compare's
    // unordered outcome is retail's (NaN edges pass both rejection tests).
    if (*Edge1 < Lo)
        return 0;
    if (static_cast<double>(*Edge0) >= static_cast<double>(Hi))
        return 0;
    int Result = 1;
    // The delta stays on the FPU.
    double const DeltaLo = static_cast<double>(Lo) - static_cast<double>(*Edge0);
    if (DeltaLo > 0.0)
    {
        Result = -1;
        *Edge0 = static_cast<float>(DeltaLo + static_cast<double>(*Edge0));
        *Attr0 = static_cast<float>(
            (static_cast<double>(*Attr1) - static_cast<double>(*Attr0)) *
                (DeltaLo / static_cast<double>(Span)) +
            static_cast<double>(*Attr0));
    }
    double const DeltaHi = static_cast<double>(Hi) - static_cast<double>(*Edge1);
    if (!(DeltaHi < 0.0))
        return Result;
    // Uses the Attr0 written above.
    *Edge1 = static_cast<float>(DeltaHi + static_cast<double>(*Edge1));
    *Attr1 = static_cast<float>(
        (static_cast<double>(*Attr1) - static_cast<double>(*Attr0)) *
            (DeltaHi / static_cast<double>(Span)) +
        static_cast<double>(*Attr1));
    return -1;
}

void ScreenQuadSetMaterialKey(unsigned long Key)
{
    // FUN_00402d30. The previous key's container keeps its queued nodes in the map until a
    // flush names that key.
    if (G_MaterialListKey != Key)
    {
        G_MaterialListKey = Key;
        G_ActiveContainer = nullptr;
    }
}

// FUN_00402d50 (sole caller the font shutdown FUN_004174a0). Retail bug kept: with the default
// key already current, DAT_00616498 is left pointing into the freed node when it was set.
void ScreenQuadResetDrawBatchCache()
{
    G_BatchMap.clear();
    if (G_MaterialListKey != G_MaterialListKeyDefault)
    {
        G_MaterialListKey = G_MaterialListKeyDefault;
        G_ActiveContainer = nullptr;
    }
}

namespace
{
    // One of FUN_004038f0's two quads (bulb, glow): nothing unless the horizontal radius is
    // above 1 (unordered skips) and the three colour bytes sum (unsigned) above 1. The colour
    // is 0xFFrrggbb from the components' low bytes.
    void EnqueueFlareQuad(FScreenQuadBatchContainer* C, int const* Rect, float const* Color,
                          float RadiusX, float RadiusY, float ScreenX, float ScreenY,
                          float DepthA, float InvDepth)
    {
        if (!(RadiusX > 1.0f))
            return;
        unsigned int const R = static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(Color[0]) * static_cast<double>(255.0f)));
        unsigned int const G = static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(Color[1]) * static_cast<double>(255.0f)));
        unsigned int const B = static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(Color[2]) * static_cast<double>(255.0f)));
        if (B + G + R <= 1u)
            return;
        unsigned int const Packed = (((R | 0xffffff00u) << 8 | (G & 0xffu)) << 8) | (B & 0xffu);

        float X0 = static_cast<float>(static_cast<double>(ScreenX) - RadiusX);
        float X1 = static_cast<float>(static_cast<double>(ScreenX) + RadiusX);
        float Y0 = static_cast<float>(static_cast<double>(ScreenY) - RadiusY);
        float Y1 = static_cast<float>(static_cast<double>(ScreenY) + RadiusY);
        float U0 = BatchU0(C);
        float V0 = BatchV0(C);
        float U1 = BatchU1(C);
        float V1 = BatchV1(C);
        if (ScreenQuadClip1D(&X0, &X1, &U0, &U1, static_cast<float>(Rect[0]),
                             static_cast<float>(Rect[2]), RadiusX + RadiusX) == 0)
            return;
        if (ScreenQuadClip1D(&Y0, &Y1, &V0, &V1, static_cast<float>(Rect[1]),
                             static_cast<float>(Rect[3]), RadiusY + RadiusY) == 0)
            return;

        FScreenQuadRecord Rec;
        Rec.X0 = X0;
        Rec.Y0 = Y0;
        Rec.X1 = X1;
        Rec.Y1 = Y1;
        Rec.DepthA = DepthA;
        Rec.DepthB = InvDepth;
        Rec.U0 = U0;
        Rec.V0 = V0;
        Rec.U1 = U1;
        Rec.V1 = V1;
        Rec.ColorBits = Packed;
        AppendRecordToNode(C->Current, Rec);
    }
}

unsigned char ScreenQuadEnqueueFlare(ICamera* Cam, float const* Position, float const* BulbColor,
                                     float const* GlowColor, float BulbSize, float GlowSize)
{
    // FUN_004038f0 (cdecl). Sole caller FUN_004fdda0. No argument is null-checked in retail.
    const float* const View = reinterpret_cast<const float*>(Cam->GetViewTransform());
    I3DMathEngine* const Math = AcquireScreenQuadMath();
    Vector CamSpace;
    Math->TransformXform(CamSpace, *reinterpret_cast<Transform const*>(View),
                         *reinterpret_cast<Vector const*>(Position));

    // Depth z - z * 0.001 (double DAT_005c75f8), stored as a float; only in front of the near
    // plane (unordered continues).
    float const Depth = static_cast<float>(static_cast<double>(CamSpace.z) -
                                           static_cast<double>(CamSpace.z) * 0.001);
    if (static_cast<double>(Depth) - static_cast<double>(Cam->GetZNear()) >= 0.0)
        return 0;

    // DAT_005c7500 = -1. Each screen scale goes through a float temporary, in retail's call
    // order (slot 11, 8, 12, 8).
    float const InvDepth = static_cast<float>(-1.0 / static_cast<double>(Depth));
    float const XScaleOverNear = static_cast<float>(Cam->GetXScaleOverNear());
    float const ScaleX =
        static_cast<float>(static_cast<double>(Cam->GetZNear()) * XScaleOverNear);
    float const YScaleOverNear = static_cast<float>(Cam->GetYScaleOverNear());
    float const ScaleY =
        static_cast<float>(static_cast<double>(Cam->GetZNear()) * YScaleOverNear);

    // Horizontal radii keep their sign, vertical ones are made positive.
    double const PerDepthX = static_cast<double>(ScaleX) * static_cast<double>(InvDepth);
    double const PerDepthY = static_cast<double>(ScaleY) * static_cast<double>(InvDepth);
    double const BulbRadiusXOnFpu = static_cast<double>(BulbSize) * PerDepthX;
    float const BulbRadiusX = static_cast<float>(BulbRadiusXOnFpu);
    float const BulbRadiusY =
        static_cast<float>(std::fabs(static_cast<double>(BulbSize) * PerDepthY));
    float const GlowRadiusX = static_cast<float>(PerDepthX * static_cast<double>(GlowSize));
    float const GlowRadiusY =
        static_cast<float>(std::fabs(PerDepthY * static_cast<double>(GlowSize)));

    // Culled when both horizontal radii are below 1 (DAT_005c75dc; unordered continues). The
    // bulb one is compared unrounded, off the FPU.
    if (BulbRadiusXOnFpu < 1.0 && static_cast<double>(GlowRadiusX) < 1.0)
        return 0;

    // DAT_005c75e0 = 0.5f.
    int const* const Rect = Cam->GetViewport();
    double const HalfSpanX = static_cast<double>(Rect[2] - Rect[0]) * static_cast<double>(0.5f);
    float const ScreenX = static_cast<float>(
        (HalfSpanX + static_cast<double>(CamSpace.x) * ScaleX * InvDepth) +
        static_cast<double>(Rect[0]));
    double const HalfSpanY = static_cast<double>(Rect[3] - Rect[1]) * static_cast<double>(0.5f);
    float const ScreenY = static_cast<float>(
        (HalfSpanY + static_cast<double>(CamSpace.y) * ScaleY * InvDepth) +
        static_cast<double>(Rect[1]));

    float const DepthA = static_cast<float>(
        1.0 - static_cast<double>(Cam->GetZNear()) * static_cast<double>(InvDepth));

    // FUN_00402f40, once for both quads.
    FScreenQuadBatchContainer* const C = EnsureBatchContainer();

    EnqueueFlareQuad(C, Rect, BulbColor, BulbRadiusX, BulbRadiusY, ScreenX, ScreenY, DepthA,
                     InvDepth);
    EnqueueFlareQuad(C, Rect, GlowColor, GlowRadiusX, GlowRadiusY, ScreenX, ScreenY, DepthA,
                     InvDepth);

    // 1 on every path past the near and size culls.
    return 1;
}

void ScreenQuadBeginBatchFrame(unsigned long const* Frame, int BlendMode, bool bFind)
{
    // FUN_00402e30 = FUN_00402f40, then the body FUN_004045d0 has as a container method.
    ContainerBeginBatch(EnsureBatchContainer(), Frame, BlendMode, bFind);
}

void ScreenQuadAppendRecord(FScreenQuadRecord const& Record)
{
    // See the header: the node FUN_00402e30 returned is the container's current node.
    AppendRecordToNode(EnsureBatchContainer()->Current, Record);
}

void ScreenQuadEnqueueIcon(ICamera* Cam, float const* Position, unsigned long Argb, float Size,
                           float Angle, int /*Unused*/)
{
    // FUN_004034a0. Retail's FUN_00403350 writes into stack and reused argument slots.
    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
    float HalfW = 0.0f;
    float HalfH = 0.0f;
    float InvDepth = 0.0f;
    if (!ScreenQuadProjectObjectExtent(Cam, Position, Size, &ScreenX, &ScreenY, &HalfW, &HalfH,
                                       &InvDepth))
        return;
    FScreenQuadBatchContainer* const C = EnsureBatchContainer();
    int const* const Rect = Cam->GetViewport();
    float const U0 = BatchU0(C);
    float const V0 = BatchV0(C);
    float const U1 = BatchU1(C);
    float const V1 = BatchV1(C);

    // |Angle| above 1e-5 (DAT_005c75f0) rotates; NaN does not.
    if (!(std::fabs(static_cast<double>(Angle)) > 1.0e-5))
    {
        // Each edge + 0.5 (DAT_005c75e8).
        float X0 = static_cast<float>(static_cast<double>(ScreenX) - HalfW + 0.5);
        float X1 = static_cast<float>(static_cast<double>(ScreenX) + HalfW + 0.5);
        float Y0 = static_cast<float>(static_cast<double>(ScreenY) - HalfH + 0.5);
        float Y1 = static_cast<float>(static_cast<double>(ScreenY) + HalfH + 0.5);
        float ClipU0 = U0;
        float ClipV0 = V0;
        float ClipU1 = U1;
        float ClipV1 = V1;

        if (ScreenQuadClip1D(&X0, &X1, &ClipU0, &ClipU1, static_cast<float>(Rect[0]),
                             static_cast<float>(Rect[2]), HalfW + HalfW) == 0)
            return;
        if (ScreenQuadClip1D(&Y0, &Y1, &ClipV0, &ClipV1, static_cast<float>(Rect[1]),
                             static_cast<float>(Rect[3]), HalfH + HalfH) == 0)
            return;

        // The record append inlined.
        float const DepthA = static_cast<float>(
            1.0 - static_cast<double>(Cam->GetZNear()) * static_cast<double>(InvDepth));
        FScreenQuadBatchNode* const Node = C->Current;
        int const Count = Node->RecordCount;
        Node->RecordCount = Count + 1;
        if (Count + 1 > Node->RecordCapacity)
        {
            Node->RecordCapacity = Count + 1 + 0x20;
            FScreenQuadRecord* const Grown = static_cast<FScreenQuadRecord*>(
                operator new(static_cast<size_t>(Node->RecordCapacity) * 0x2cu));
            if (Node->Records != nullptr)
                memcpy(Grown, Node->Records, static_cast<size_t>(Count) * 0x2cu);
            operator delete(Node->Records);
            Node->Records = Grown;
        }
        FScreenQuadRecord& Rec = Node->Records[Count];
        Rec.X0 = X0;
        Rec.Y0 = Y0;
        Rec.X1 = X1;
        Rec.Y1 = Y1;
        Rec.DepthA = DepthA;
        Rec.DepthB = InvDepth;
        Rec.U0 = ClipU0;
        Rec.V0 = ClipV0;
        Rec.U1 = ClipU1;
        Rec.V1 = ClipV1;
        Rec.ColorBits = Argb;
        return;
    }

    double const Cos = std::cos(static_cast<double>(Angle));
    double const Sin = std::sin(static_cast<double>(Angle));
    float const CosHalfW = static_cast<float>(Cos * HalfW);
    float const SinHalfW = static_cast<float>(static_cast<double>(HalfW) * Sin);
    float const CosHalfH = static_cast<float>(Cos * HalfH);
    float const SinHalfH = static_cast<float>(Sin * HalfH);
    float const DepthA = static_cast<float>(
        1.0 - static_cast<double>(Cam->GetZNear()) * static_cast<double>(InvDepth));

    // The corners, in retail's operand order.
    double const X = static_cast<double>(ScreenX);
    double const Y = static_cast<double>(ScreenY);
    double const XMinusCos = X - CosHalfW;
    double const YMinusSin = Y - SinHalfW;
    float CornerX[4];
    float CornerY[4];
    CornerX[0] = static_cast<float>(XMinusCos - SinHalfH);
    CornerY[0] = static_cast<float>((static_cast<double>(SinHalfW) + Y) - CosHalfH);
    CornerX[1] = static_cast<float>(XMinusCos + SinHalfH);
    CornerY[1] = static_cast<float>((static_cast<double>(CosHalfH) + SinHalfW) + Y);
    CornerX[2] = static_cast<float>((static_cast<double>(SinHalfH) + CosHalfW) + X);
    CornerY[2] = static_cast<float>(static_cast<double>(CosHalfH) + YMinusSin);
    CornerX[3] = static_cast<float>((static_cast<double>(CosHalfW) + X) - SinHalfH);
    CornerY[3] = static_cast<float>(YMinusSin - CosHalfH);

    // Every corner inside [left, right) x [top, bottom), unordered values passing.
    float const Left = static_cast<float>(Rect[0]);
    for (int Index = 0; Index < 4; ++Index)
    {
        if (CornerX[Index] < Left)
            return;
        if (static_cast<double>(Rect[2]) <= static_cast<double>(CornerX[Index]))
            return;
        if (static_cast<double>(Rect[1]) > static_cast<double>(CornerY[Index]))
            return;
        if (static_cast<double>(Rect[3]) <= static_cast<double>(CornerY[Index]))
            return;
    }

    // Four vertices appended (grown to count + 0x24 when full).
    FScreenQuadBatchNode* const Node = C->Current;
    int const Count = Node->RotVertCount;
    Node->RotVertCount = Count + 4;
    if (Count + 4 > Node->RotVertCapacity)
    {
        Node->RotVertCapacity = Count + 4 + 0x20;
        FScreenQuadGpuVert* const Grown = static_cast<FScreenQuadGpuVert*>(
            operator new(static_cast<size_t>(Node->RotVertCapacity) * 0x20u));
        if (Node->RotatedVerts != nullptr)
            memcpy(Grown, Node->RotatedVerts, static_cast<size_t>(Count) * 0x20u);
        operator delete(Node->RotatedVerts);
        Node->RotatedVerts = Grown;
    }
    float const VertU[4] = {U0, U0, U1, U1};
    float const VertV[4] = {V0, V1, V1, V0};
    for (int Index = 0; Index < 4; ++Index)
    {
        FScreenQuadGpuVert& Vert = Node->RotatedVerts[Count + Index];
        Vert.X = CornerX[Index];
        Vert.Y = CornerY[Index];
        Vert.Z = DepthA;
        Vert.Rhw = InvDepth;
        Vert.Color = static_cast<unsigned>(Argb);
        Vert.Specular = 0;
        Vert.U = VertU[Index];
        Vert.V = VertV[Index];
    }
}

// DAT_0061648c -- the static initializer LAB_00402c00 stores -1; never written again.
unsigned long G_ScreenQuadCurrentListKey = 0xffffffffu;

// FUN_00403ea0 (cdecl: pipeline, list key).
void ScreenQuadFlush(IRenderPipeline* Pipe, unsigned long Key)
{
    // Three savers on the caller's pipeline, restored in reverse order at the end.
    FScreenQuadRenderStateSaver const SaverA(Pipe, 0x88, 0);
    FScreenQuadRenderStateSaver const SaverB(Pipe, 0x8b, -1);
    FScreenQuadRenderStateSaver const SaverC(Pipe, 0x16, 1);

    // DAT_005c7520.
    Pipe->SetRenderState(D3DRS_LIGHTING, 0);
    Pipe->SetRenderState(D3DRS_SPECULARENABLE, 0);
    Pipe->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    // DAT_005c7540.
    Pipe->SetRenderState(D3DRS_COLORVERTEX, 1);
    Pipe->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    // Stage 0 type 0xb = 0 (the table loop leaves the register at 0).
    Pipe->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    // DAT_005c7568 on stage 0.
    Pipe->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Pipe->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Pipe->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    Pipe->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    Pipe->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    Pipe->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    // DAT_005c75a0 on stage 1.
    Pipe->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    Pipe->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Pipe->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    Pipe->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    Pipe->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    Pipe->SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    Pipe->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Pipe->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    Pipe->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

    if (Key == G_ScreenQuadCurrentListKey)
    {
        // The current container, created on demand (FUN_00404f00 / FUN_00404e40); a new entry
        // is given the default frame through FUN_004045d0(DAT_006101d8, 0, 1).
        if (G_ActiveContainer == nullptr)
        {
            std::pair<std::map<unsigned long, FScreenQuadBatchEntry>::iterator, bool> const
                Inserted = G_BatchMap.try_emplace(G_MaterialListKey);
            G_ActiveContainer = &Inserted.first->second.Container;
            if (Inserted.second)
                ContainerBeginBatch(G_ActiveContainer, G_ScreenQuadDefaultFrame, 0, true);
        }
        FlushContainer(G_ActiveContainer);
    }
    else
    {
        // FUN_00432240 (find); no entry -> nothing drawn.
        std::map<unsigned long, FScreenQuadBatchEntry>::iterator const Found =
            G_BatchMap.find(Key);
        if (Found != G_BatchMap.end())
            FlushContainer(&Found->second.Container);
    }

    // DAT_005c7558.
    Pipe->SetRenderState(D3DRS_COLORVERTEX, 0);
    Pipe->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    Pipe->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

    // DAT_00616498 is nulled only on a change.
    if (G_MaterialListKey != G_MaterialListKeyDefault)
    {
        G_MaterialListKey = G_MaterialListKeyDefault;
        G_ActiveContainer = nullptr;
    }
}

// DAT_006101ec -- .data byte, initial 1, never written; sole reader FUN_00404720. Set: the
// big-image tiles are drawn with stage 0 MAGFILTER / MINFILTER forced to point.
unsigned char G_BigImageTilePointFilter = 1;

void __cdecl ScreenQuadDrawBigImageTile(void* TextureHandle, unsigned long PackedColor,
                                        float* DestXy, float* UvQuad)
{
    // FUN_00404720 (cdecl). No argument is checked. The glMatrixMode(0x1701) call before it is
    // the empty RET at 0x00401e90.
    Win3DPushProjection();
    Win3DSetOrtho2D();

    // EH states 0..2.
    FScreenQuadRenderStateSaver const SaverA(DALib::Pipeline, 0x88, 0);
    FScreenQuadRenderStateSaver const SaverB(DALib::Pipeline, 0x8b, -1);
    FScreenQuadRenderStateSaver const SaverC(DALib::Pipeline, 0x16, 1);

    // DAT_005c7520.
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    DALib::Pipeline->SetRenderState(D3DRS_SPECULARENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    // DAT_005c7540.
    DALib::Pipeline->SetRenderState(D3DRS_COLORVERTEX, 1);
    DALib::Pipeline->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    // DAT_005c7568 on stage 0.
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    // DAT_005c75a0 on stage 1.
    DALib::Pipeline->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    DALib::Pipeline->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    DALib::Pipeline->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    DALib::Pipeline->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    DALib::Pipeline->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    DALib::Pipeline->SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

    int SavedStage0Type10 = 0;
    int SavedStage0Type11 = 0;
    DALib::Pipeline->GetTextureStageState(0, 0x10, &SavedStage0Type10);
    DALib::Pipeline->GetTextureStageState(0, 0x11, &SavedStage0Type11);

    if (G_BigImageTilePointFilter != 0)
    {
        DALib::Pipeline->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        DALib::Pipeline->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    }

    // EH states 3, 4.
    FScreenQuadRenderStateSaver const SaverD(DALib::Pipeline, 0x17, 8);
    FScreenQuadRenderStateSaver const SaverE(DALib::Pipeline, 0xe, 0);

    // FUN_00402d30 inlined.
    ScreenQuadSetMaterialKey(DACOM_CRC::GetCRC32("image"));

    // The UV quad's corners 0, 7, 6, 1 give (u0, v0, u1, v1), copied as dwords.
    unsigned long Frame[5];
    Frame[0] = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(TextureHandle));
    memcpy(&Frame[1], &UvQuad[0], sizeof(unsigned long));
    memcpy(&Frame[2], &UvQuad[7], sizeof(unsigned long));
    memcpy(&Frame[3], &UvQuad[6], sizeof(unsigned long));
    memcpy(&Frame[4], &UvQuad[1], sizeof(unsigned long));

    FScreenQuadBatchNode* const Node =
        ContainerBeginBatch(EnsureBatchContainer(), Frame, 2, false);

    float X0 = DestXy[0];
    float Y0 = DestXy[1];
    float X1 = DestXy[2];
    float Y1 = DestXy[3];
    float U0 = 0.0f;
    float V0 = 0.0f;
    float U1 = 0.0f;
    float V1 = 0.0f;
    memcpy(&U0, &Frame[1], sizeof(float));
    memcpy(&V0, &Frame[2], sizeof(float));
    memcpy(&U1, &Frame[3], sizeof(float));
    memcpy(&V1, &Frame[4], sizeof(float));

    // Clipped to the surface; the spans are stored as floats.
    float const SpanX = static_cast<float>(static_cast<double>(X1) - static_cast<double>(X0));
    float const HiX = static_cast<float>(static_cast<int>(G_RenderSurfaceWidth - 1u));
    if (ScreenQuadClip1D(&X0, &X1, &U0, &U1, 0.0f, HiX, SpanX) != 0)
    {
        float const SpanY = static_cast<float>(static_cast<double>(Y1) - static_cast<double>(Y0));
        float const HiY = static_cast<float>(static_cast<int>(G_RenderSurfaceHeight - 1u));
        if (ScreenQuadClip1D(&Y0, &Y1, &V0, &V1, 0.0f, HiY, SpanY) != 0)
        {
            // FUN_00404ce0(node + 8, 1).
            FScreenQuadRecord Rec;
            Rec.X0 = X0;
            Rec.Y0 = Y0;
            Rec.X1 = X1;
            Rec.Y1 = Y1;
            Rec.DepthA = 1.0f;
            Rec.DepthB = 1.0f;
            Rec.U0 = U0;
            Rec.V0 = V0;
            Rec.U1 = U1;
            Rec.V1 = V1;
            Rec.ColorBits = PackedColor;
            AppendRecordToNode(Node, Rec);

            ScreenQuadFlush(DALib::Pipeline, G_ScreenQuadCurrentListKey);
        }
    }

    // DAT_005c7558, then the filters back.
    DALib::Pipeline->SetRenderState(D3DRS_COLORVERTEX, 0);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_MAGFILTER, SavedStage0Type10);
    DALib::Pipeline->SetTextureStageState(0, D3DTSS_MINFILTER, SavedStage0Type11);

    // FUN_00422ab0; the savers then restore in reverse order.
    Win3DPopProjection();
}

unsigned char ScreenQuadProjectObjectExtent(ICamera* Cam, float const* Position, float Size,
                                            float* OutScreenX, float* OutScreenY,
                                            float* OutHalfW, float* OutHalfH,
                                            float* OutInvDepth)
{
    // FUN_00403350.
    const float* const View = reinterpret_cast<const float*>(Cam->GetViewTransform());
    I3DMathEngine* const Math = AcquireScreenQuadMath();
    Vector CamSpace;
    Math->TransformXform(CamSpace, *reinterpret_cast<Transform const*>(View),
                         *reinterpret_cast<Vector const*>(Position));

    // Only in front of the near plane (unordered continues).
    if (static_cast<double>(CamSpace.z) - static_cast<double>(Cam->GetZNear()) >= 0.0)
        return 0;

    // DAT_005c7500 = -1; each value stored as a float.
    float const InvDepth = static_cast<float>(-1.0 / static_cast<double>(CamSpace.z));
    *OutInvDepth = InvDepth;
    float const XScaleOverNear = static_cast<float>(Cam->GetXScaleOverNear());
    float const ScaleX = static_cast<float>(static_cast<double>(Cam->GetZNear()) * XScaleOverNear);
    float const YScaleOverNear = static_cast<float>(Cam->GetYScaleOverNear());
    float const ScaleY = static_cast<float>(static_cast<double>(Cam->GetZNear()) * YScaleOverNear);

    // DAT_005c75e0 = 0.5f. Culled when the horizontal extent (unrounded) is below 1.
    double const HalfW = static_cast<double>(ScaleX) * static_cast<double>(InvDepth) *
                         static_cast<double>(Size) * static_cast<double>(0.5f);
    *OutHalfW = static_cast<float>(HalfW);
    *OutHalfH = static_cast<float>(std::fabs(static_cast<double>(ScaleY) *
                                             static_cast<double>(InvDepth) *
                                             static_cast<double>(Size) *
                                             static_cast<double>(0.5f)));
    if (HalfW < 1.0)
        return 0;

    // Retail's operand order differs between x and y.
    int const* const Rect = Cam->GetViewport();
    double const HalfSpanY = static_cast<double>(Rect[3] - Rect[1]) * static_cast<double>(0.5f);
    double const HalfSpanX = static_cast<double>(Rect[2] - Rect[0]) * static_cast<double>(0.5f);
    *OutScreenX = static_cast<float>(
        (HalfSpanX + static_cast<double>(CamSpace.x) * ScaleX * InvDepth) +
        static_cast<double>(Rect[0]));
    *OutScreenY = static_cast<float>(
        (static_cast<double>(CamSpace.y) * ScaleY * InvDepth + static_cast<double>(Rect[1])) +
        HalfSpanY);
    return 1;
}
