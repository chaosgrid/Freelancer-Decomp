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

    constexpr unsigned kEffectFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    constexpr int kDefaultGrowth = 0x80;

    // DAT_005d9a04 — SetTextureStageState stage 0: (type,value) chain, type starts at 1.
    // Walked as: value=[p+4], next_type=[p+8], p+=8 until next_type==0.
    int const kTssStage0[] = {
        D3DTSS_COLOROP, D3DTOP_MODULATE,
        D3DTSS_COLORARG1, D3DTA_TEXTURE,
        D3DTSS_COLORARG2, D3DTA_DIFFUSE,
        D3DTSS_ALPHAOP, D3DTOP_MODULATE,
        D3DTSS_ALPHAARG1, D3DTA_TEXTURE,
        D3DTSS_ALPHAARG2, D3DTA_DIFFUSE,
        0
    };

    // DAT_005d9a3c — stage 1.
    int const kTssStage1[] = {
        D3DTSS_COLOROP, D3DTOP_DISABLE,
        D3DTSS_COLORARG1, D3DTA_TEXTURE,
        D3DTSS_COLORARG2, D3DTA_CURRENT,
        D3DTSS_ALPHAOP, D3DTOP_DISABLE,
        D3DTSS_ALPHAARG1, D3DTA_TEXTURE,
        D3DTSS_ALPHAARG2, D3DTA_CURRENT,
        0
    };

    // DAT_005d99dc — SetRenderState chain, state starts at 0x8d.
    int const kRsSetup[] = {
        D3DRS_COLORVERTEX, 1,
        D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1,
        0
    };

    // DAT_005d99f4 — restore.
    int const kRsRestore[] = {
        D3DRS_COLORVERTEX, 0,
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
        return (static_cast<FTexShape const*>(ShapeObj))->GetGpuHandle(Frame);
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

void FEffectVertexBatch::Destroy()
{
    // FUN_0046e9c0. The buffers are freed without a null test.
    if (Pipeline != nullptr)
    {
        Pipeline->Release();
        Pipeline = nullptr;
    }
    BatchFree(Vertices);
    Vertices = nullptr;
    BatchFree(Indices);
    Indices = nullptr;
}

void FEffectVertexBatch::GrowVertices()
{
    // FUN_0046ecd0: nothing while count < capacity (unsigned). Otherwise grow by +0x1c; a null
    // from VC6's operator new leaves the batch untouched (std::nothrow here), and the old buffer
    // is freed even when null.
    if (static_cast<unsigned>(VertCount) < VertCapacity)
        return;

    FEffectVertex* NewBuf = static_cast<FEffectVertex*>(operator new(
        static_cast<size_t>(static_cast<unsigned>(Growth) + VertCapacity) *
            sizeof(FEffectVertex),
        std::nothrow));
    if (NewBuf == nullptr)
        return;

    if (Vertices != nullptr)
        std::memcpy(NewBuf, Vertices,
                    static_cast<size_t>(VertCapacity) * sizeof(FEffectVertex));
    BatchFree(Vertices);
    Vertices = NewBuf;
    VertCapacity = VertCapacity + static_cast<unsigned>(Growth);
}

void FEffectVertexBatch::Reset()
{
    VertCount = 0;
    bStripMode = 0;
    bCloseLoop = 0;
    StripCounter = 0;
    IndexCount = 0;
    GrowVertices();
    if (IndexCapacity < static_cast<unsigned>(IndexCount) + 1u)
        GrowIndexBuffer(this, static_cast<unsigned>(Growth * 3));
}

int FEffectVertexBatch::Append(float X, float Y, float Z)
{
    if (bStripMode != 0)
    {
        if ((StripCounter & 3u) == 3u)
        {
            VertCount = VertCount + 2;
            GrowVertices();
            FEffectVertex* Base = Vertices;
            int const C = VertCount;
            Base[C - 2] = Base[C - 5];
            Base[C - 1] = Base[C - 3];
        }
        StripCounter = StripCounter + 1;
    }

    FEffectVertex* Vert = &Vertices[VertCount];
    Vert->R = ColorR;
    Vert->G = ColorG;
    Vert->B = ColorB;
    Vert->A = ColorA;
    Vert->U = U;
    Vert->V = V;
    Vert->X = X;
    Vert->Y = Y;
    Vert->Z = Z;
    VertCount = VertCount + 1;
    GrowVertices();
    return VertCount - 1;
}

void FEffectVertexBatch::EnsureIndices(int Needed)
{
    if (IndexCapacity >= static_cast<unsigned>(IndexCount + Needed))
        return;
    GrowIndexBuffer(this, static_cast<unsigned>(Growth * 3));
}

void FEffectVertexBatch::PushTriangle(unsigned short I0, unsigned short I1, unsigned short I2)
{
    if (IndexCapacity < static_cast<unsigned>(IndexCount + 3))
        GrowIndexBuffer(this, static_cast<unsigned>(Growth * 3));

    Indices[IndexCount] = I0;
    IndexCount = IndexCount + 1;
    Indices[IndexCount] = I1;
    IndexCount = IndexCount + 1;
    Indices[IndexCount] = I2;
    IndexCount = IndexCount + 1;
}

void FEffectVertexBatch::PushQuad(unsigned short I0, unsigned short I1, unsigned short I2,
                                  unsigned short I3)
{
    if (IndexCapacity < static_cast<unsigned>(IndexCount + 6))
        GrowIndexBuffer(this, static_cast<unsigned>(Growth * 3));

    Indices[IndexCount] = I0;
    IndexCount = IndexCount + 1;
    Indices[IndexCount] = I1;
    IndexCount = IndexCount + 1;
    Indices[IndexCount] = I2;
    IndexCount = IndexCount + 1;
    Indices[IndexCount] = I0;
    IndexCount = IndexCount + 1;
    Indices[IndexCount] = I2;
    IndexCount = IndexCount + 1;
    Indices[IndexCount] = I3;
    IndexCount = IndexCount + 1;
}

void FEffectVertexBatch::SetColor(float R, float G, float B)
{
    // FUN_004fc950: each argument * 255.0 (double 0x005c89c0) truncated, the low byte stored.
    ColorR = static_cast<unsigned char>(static_cast<long>(static_cast<double>(R) * 255.0));
    ColorG = static_cast<unsigned char>(static_cast<long>(static_cast<double>(G) * 255.0));
    ColorB = static_cast<unsigned char>(static_cast<long>(static_cast<double>(B) * 255.0));
    ColorA = 0xff;
}

// FUN_004f9de0. Its sole caller is the beam branch of FUN_004f7480. It works on the Effect.cpp
// batch DAT_00674c7c directly; DALib::Pipeline is re-read for every call and never tested.
void EffectVertexBatchFlush(ICamera* Cam)
{
    FEffectVertexBatch* const Batch = &G_EffectVertexBatch;

    DALib::Pipeline->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    ApplyTssTable(DALib::Pipeline, 0, kTssStage0);
    ApplyTssTable(DALib::Pipeline, 1, kTssStage1);
    ApplyRsTable(DALib::Pipeline, kRsSetup);

    // The code_beam texture (DAT_00674f54), frame 0.
    DALib::Pipeline->SetTextureStageTexture(0, ResolveBeamTextureHandle(G_BeamEffectTexture, 0));

    DALib::Pipeline->SetRenderState(D3DRS_CLIPPING, 1);  // CLIPPING
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);  // CULLMODE = NONE
    DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 1);   // ZENABLE
    DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);   // ZWRITEENABLE
    DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);  // ALPHABLENDENABLE
    DALib::Pipeline->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);  // SRCBLEND = ONE
    DALib::Pipeline->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);  // DESTBLEND = ONE

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
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    ApplyRsTable(DALib::Pipeline, kRsRestore);
}
