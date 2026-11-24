// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/Effects/effect_vertex_batch.h"

#include "DALib.h"
#include "Space/Effects/beam_effect.h"
#include "common_csys.h"
#include "common_misc.h"   // ICamera (EffectVertexBatchFlush, vt+0x40)
#include "render/shading.h"
#include "Common/Graphics/texture_shapes.h"
#include "x86math.h"

#include <cstdint>
#include <cstring>
#include <new>

// Retail DAT_00674c7c … DAT_00674cb4.
FEffectVertexBatch G_EffectVertexBatch = {};
unsigned char G_EffectVertexBatchDtorOnce = 0;
I3DMathEngine* G_EffectMathEngine = nullptr; // DAT_00674f50 (Effect.cpp math cache)

namespace
{
    struct FEffectVertexBatchAutoInit
    {
        FEffectVertexBatchAutoInit()
        {
            EffectVertexBatchStaticInit();
        }
        ~FEffectVertexBatchAutoInit()
        {
            EffectVertexBatchStaticShutdown();
        }
    };

    FEffectVertexBatchAutoInit G_EffectVertexBatchAutoInit;

    constexpr unsigned kEffectFvf = 0x142;
    constexpr int kDefaultGrowth = 0x80;

    // DAT_005d9a04 — SetTextureStageState stage 0: (type,value) chain, type starts at 1.
    // Walked as: value=[p+4], next_type=[p+8], p+=8 until next_type==0.
    int const kTssStage0[] = {
        1, 4, // COLOROP = MODULATE
        2, 2, // COLORARG1 = TEXTURE
        3, 0, // COLORARG2 = DIFFUSE (0)
        4, 4, // ALPHAOP = MODULATE
        5, 2, // ALPHAARG1 = TEXTURE
        6, 0, // ALPHAARG2 = DIFFUSE
        0
    };

    // DAT_005d9a3c — stage 1.
    int const kTssStage1[] = {
        1, 1, // COLOROP = DISABLE
        2, 2,
        3, 1,
        4, 1, // ALPHAOP = DISABLE
        5, 2,
        6, 1,
        0
    };

    // DAT_005d99dc — SetRenderState chain, state starts at 0x8d.
    int const kRsSetup[] = {
        0x8d, 1, // COLORVERTEX = TRUE
        0x91, 1, // DIFFUSEMATERIALSOURCE = COLOR1
        0
    };

    // DAT_005d99f4 — restore.
    int const kRsRestore[] = {
        0x8d, 0,
        0
    };

    void BatchFree(void* Ptr)
    {
        operator delete(Ptr);
    }

    // The index-buffer growth body of FUN_004fcb40, which the reset and push paths repeat
    // inline. VC6's operator new returns null rather than throwing, and a null leaves the batch
    // as it is (std::nothrow here). The old buffer is freed even when null.
    void GrowIndexBuffer(FEffectVertexBatch* Batch, unsigned ExtraWords)
    {
        unsigned short* NewBuf = static_cast<unsigned short*>(operator new(
            static_cast<size_t>(Batch->IndexCapacity + ExtraWords) * 2u, std::nothrow));
        if (NewBuf == nullptr)
            return;
        if (Batch->Indices != nullptr)
            std::memcpy(NewBuf, Batch->Indices, static_cast<size_t>(Batch->IndexCapacity) * 2u);
        BatchFree(Batch->Indices);
        Batch->Indices = NewBuf;
        Batch->IndexCapacity = Batch->IndexCapacity + ExtraWords;
    }

    void ApplyTssTable(IRenderPipeline* Pipe, int Stage, int const* Table)
    {
        int Type = Table[0];
        int const* P = Table;
        while (Type != 0)
        {
            int const Value = P[1];
            Pipe->SetTextureStageState(Stage, Type, Value);
            Type = P[2];
            P = P + 2;
        }
    }

    void ApplyRsTable(IRenderPipeline* Pipe, int const* Table)
    {
        int State = Table[0];
        int const* P = Table;
        while (State != 0)
        {
            int const Value = P[1];
            Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Value);
            State = P[2];
            P = P + 2;
        }
    }

    // FUN_00425640 — the shape's GPU texture handle for a frame, or 0 without a texture.
    void* ResolveBeamTextureHandle(void* ShapeObj, int Frame)
    {
        return TexShapeGetGpuHandle(static_cast<FTexShape const*>(ShapeObj), Frame);
    }
}

void EffectVertexBatchStaticInit()
{
    G_EffectVertexBatch.Vertices = nullptr;
    G_EffectVertexBatch.VertCount = 0;
    G_EffectVertexBatch.Indices = nullptr;
    G_EffectVertexBatch.IndexCount = 0;
    G_EffectVertexBatch.Pipeline = nullptr;
    G_EffectVertexBatch.VertCapacity = 0;
    G_EffectVertexBatch.IndexCapacity = 0;
    G_EffectVertexBatch.bCloseLoop = 0;
    G_EffectVertexBatch.bStripMode = 0;
    G_EffectVertexBatch.StripCounter = 0;
    G_EffectVertexBatch.Type = 1;
    G_EffectVertexBatch.Fvf = kEffectFvf;
    G_EffectVertexBatch.Growth = kDefaultGrowth;
    G_EffectVertexBatch.ColorA = 0xff;
    G_EffectVertexBatch.ColorB = 0xff;
    G_EffectVertexBatch.ColorG = 0xff;
    G_EffectVertexBatch.ColorR = 0xff;
    G_EffectVertexBatch.V = 0.0f;
    G_EffectVertexBatch.U = 0.0f;
}

void EffectVertexBatchStaticShutdown()
{
    if ((G_EffectVertexBatchDtorOnce & 1u) != 0u)
        return;
    G_EffectVertexBatchDtorOnce =
        static_cast<unsigned char>(G_EffectVertexBatchDtorOnce | 1u);

    if (G_EffectVertexBatch.Pipeline != nullptr)
    {
        G_EffectVertexBatch.Pipeline->Release();
        G_EffectVertexBatch.Pipeline = nullptr;
    }
    if (G_EffectVertexBatch.Vertices != nullptr)
    {
        BatchFree(G_EffectVertexBatch.Vertices);
        G_EffectVertexBatch.Vertices = nullptr;
    }
    if (G_EffectVertexBatch.Indices != nullptr)
    {
        BatchFree(G_EffectVertexBatch.Indices);
        G_EffectVertexBatch.Indices = nullptr;
    }
}

void EffectVertexBatchDestroy(FEffectVertexBatch* Batch)
{
    // FUN_0046e9c0. The buffers are freed without a null test.
    if (Batch->Pipeline != nullptr)
    {
        Batch->Pipeline->Release();
        Batch->Pipeline = nullptr;
    }
    BatchFree(Batch->Vertices);
    Batch->Vertices = nullptr;
    BatchFree(Batch->Indices);
    Batch->Indices = nullptr;
}

void EffectVertexBatchGrowVertices(FEffectVertexBatch* Batch)
{
    // FUN_0046ecd0: nothing while count < capacity (unsigned). Otherwise grow by +0x1c; a null
    // from VC6's operator new leaves the batch untouched (std::nothrow here), and the old buffer
    // is freed even when null.
    if (static_cast<unsigned>(Batch->VertCount) < Batch->VertCapacity)
        return;

    FEffectVertex* NewBuf = static_cast<FEffectVertex*>(operator new(
        static_cast<size_t>(static_cast<unsigned>(Batch->Growth) + Batch->VertCapacity) *
            sizeof(FEffectVertex),
        std::nothrow));
    if (NewBuf == nullptr)
        return;

    if (Batch->Vertices != nullptr)
        std::memcpy(NewBuf, Batch->Vertices,
                    static_cast<size_t>(Batch->VertCapacity) * sizeof(FEffectVertex));
    BatchFree(Batch->Vertices);
    Batch->Vertices = NewBuf;
    Batch->VertCapacity = Batch->VertCapacity + static_cast<unsigned>(Batch->Growth);
}

void EffectVertexBatchReset(FEffectVertexBatch* Batch)
{
    Batch->VertCount = 0;
    Batch->bStripMode = 0;
    Batch->bCloseLoop = 0;
    Batch->StripCounter = 0;
    Batch->IndexCount = 0;
    EffectVertexBatchGrowVertices(Batch);
    if (Batch->IndexCapacity < static_cast<unsigned>(Batch->IndexCount) + 1u)
        GrowIndexBuffer(Batch, static_cast<unsigned>(Batch->Growth * 3));
}

int EffectVertexBatchAppend(FEffectVertexBatch* Batch, float X, float Y, float Z)
{
    if (Batch->bStripMode != 0)
    {
        if ((Batch->StripCounter & 3u) == 3u)
        {
            Batch->VertCount = Batch->VertCount + 2;
            EffectVertexBatchGrowVertices(Batch);
            FEffectVertex* Base = Batch->Vertices;
            int const C = Batch->VertCount;
            Base[C - 2] = Base[C - 5];
            Base[C - 1] = Base[C - 3];
        }
        Batch->StripCounter = Batch->StripCounter + 1;
    }

    FEffectVertex* Vert = &Batch->Vertices[Batch->VertCount];
    Vert->R = Batch->ColorR;
    Vert->G = Batch->ColorG;
    Vert->B = Batch->ColorB;
    Vert->A = Batch->ColorA;
    Vert->U = Batch->U;
    Vert->V = Batch->V;
    Vert->X = X;
    Vert->Y = Y;
    Vert->Z = Z;
    Batch->VertCount = Batch->VertCount + 1;
    EffectVertexBatchGrowVertices(Batch);
    return Batch->VertCount - 1;
}

void EffectVertexBatchEnsureIndices(FEffectVertexBatch* Batch, int Needed)
{
    if (Batch->IndexCapacity >= static_cast<unsigned>(Batch->IndexCount + Needed))
        return;
    GrowIndexBuffer(Batch, static_cast<unsigned>(Batch->Growth * 3));
}

void EffectVertexBatchPushTriangle(FEffectVertexBatch* Batch, unsigned short I0,
                                   unsigned short I1, unsigned short I2)
{
    if (Batch->IndexCapacity < static_cast<unsigned>(Batch->IndexCount + 3))
        GrowIndexBuffer(Batch, static_cast<unsigned>(Batch->Growth * 3));

    Batch->Indices[Batch->IndexCount] = I0;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I1;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I2;
    Batch->IndexCount = Batch->IndexCount + 1;
}

void EffectVertexBatchPushQuad(FEffectVertexBatch* Batch, unsigned short I0,
                               unsigned short I1, unsigned short I2, unsigned short I3)
{
    if (Batch->IndexCapacity < static_cast<unsigned>(Batch->IndexCount + 6))
        GrowIndexBuffer(Batch, static_cast<unsigned>(Batch->Growth * 3));

    Batch->Indices[Batch->IndexCount] = I0;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I1;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I2;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I0;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I2;
    Batch->IndexCount = Batch->IndexCount + 1;
    Batch->Indices[Batch->IndexCount] = I3;
    Batch->IndexCount = Batch->IndexCount + 1;
}

void EffectVertexBatchSetColor(FEffectVertexBatch* Batch, float R, float G, float B)
{
    // FUN_004fc950: each argument * 255.0 (double 0x005c89c0) truncated, the low byte stored.
    Batch->ColorR = static_cast<unsigned char>(static_cast<long>(static_cast<double>(R) * 255.0));
    Batch->ColorG = static_cast<unsigned char>(static_cast<long>(static_cast<double>(G) * 255.0));
    Batch->ColorB = static_cast<unsigned char>(static_cast<long>(static_cast<double>(B) * 255.0));
    Batch->ColorA = 0xff;
}

// FUN_004f9de0. Its sole caller is the beam branch of FUN_004f7480. It works on the Effect.cpp
// batch DAT_00674c7c directly; DALib::Pipeline is re-read for every call and never tested.
void EffectVertexBatchFlush(ICamera* Cam)
{
    FEffectVertexBatch* const Batch = &G_EffectVertexBatch;

    DALib::Pipeline->SetTextureStageState(0, 0xb, 0);
    ApplyTssTable(DALib::Pipeline, 0, kTssStage0);
    ApplyTssTable(DALib::Pipeline, 1, kTssStage1);
    ApplyRsTable(DALib::Pipeline, kRsSetup);

    // The code_beam texture (DAT_00674f54), frame 0.
    DALib::Pipeline->SetTextureStageTexture(0, ResolveBeamTextureHandle(G_BeamEffectTexture, 0));

    DALib::Pipeline->SetRenderState(D3DRS_CLIPPING, 1);  // CLIPPING
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 1);  // CULLMODE = NONE
    DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 1);   // ZENABLE
    DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);   // ZWRITEENABLE
    DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);  // ALPHABLENDENABLE
    DALib::Pipeline->SetRenderState(D3DRS_SRCBLEND, 2);  // SRCBLEND = ONE
    DALib::Pipeline->SetRenderState(D3DRS_DESTBLEND, 2);  // DESTBLEND = ONE

    int SavedLighting = 0;
    DALib::Pipeline->GetRenderState(D3DRS_LIGHTING, &SavedLighting);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);

    IRenderPipeline* const ModelviewPipe = DALib::Pipeline;
    ModelviewPipe->SetModelview(Cam->GetViewTransform());

    // The batch's pipeline reference (DAT_00674c94) is replaced; retail's own null test on the
    // smart-pointer assignment.
    if (Batch->Pipeline != nullptr)
    {
        Batch->Pipeline->Release();
        Batch->Pipeline = nullptr;
    }
    IRenderPipeline* const Taken = DALib::Pipeline;
    if (Taken != nullptr)
    {
        Batch->Pipeline = Taken;
        Taken->AddRef();
    }

    // No count or buffer test.
    if (Batch->Pipeline != nullptr)
    {
        Batch->Pipeline->DrawIndexedPrimitiveUp(
            4u, Batch->Fvf, Batch->Vertices, static_cast<unsigned>(Batch->VertCount),
            Batch->Indices, static_cast<unsigned>(Batch->IndexCount), 0u);
        if (Batch->Pipeline != nullptr)
        {
            Batch->Pipeline->Release();
            Batch->Pipeline = nullptr;
        }
    }

    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, SavedLighting);
    DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 2);
    ApplyRsTable(DALib::Pipeline, kRsRestore);
}
