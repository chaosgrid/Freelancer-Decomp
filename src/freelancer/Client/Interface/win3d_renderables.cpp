// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/win3d_renderables.h"
#include "Interface/win3d.h"
#include "Interface/win3d_render.h"
#include "Interface/win3d_immediate.h"
#include "dacom.h"
#include "Common/Fonts/font_atlas.h"
#include "NeuroNet/nn_montage_draw.h"
#include "Common/Graphics/screen_quad_state.h"
#include "Space/Cameras/space_camera.h"
#include "DALib.h"
#include "render/IRenderPipeline.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

int G_DrawCallCount = 0;

static_assert(sizeof(FImmedVertex) == 0x10, "FImmedVertex is 0x10 bytes");
static_assert(offsetof(FImmedBatcher, VertBase) == 0x0c, "immed VertBase");
static_assert(offsetof(FImmedBatcher, VertCount) == 0x1c, "immed VertCount");
static_assert(offsetof(FImmedBatcher, Dirty) == 0x24, "immed Dirty");
static_assert(sizeof(FImmedBatcher) == 0x28, "FImmedBatcher is 0x28 bytes");

// Static initializers LAB_005a28c0 / LAB_005a2900.
FImmedBatcher G_ImmedBatchA = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, 0, 0, 1, 0, {0, 0}}; // DAT_0067dd90
FImmedBatcher G_ImmedBatchB = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, 0, 0, 1, 0, {0, 0}}; // DAT_0067dd68

namespace
{
    // DAT_006105b5 — default 1; forces texture stage states 0x10 / 0x11 on in the overlay prep.
    unsigned char G_UiOverlayForceTss = 1;

    IRenderPipeline* GetPipe()
    {
        if (DALib::Pipeline == nullptr)
            return nullptr;
        return DALib::Pipeline;
    }

    void ResetImmedBatchDirty()
    {
        // FUN_005a29b0
        if (G_ImmedBatchA.Dirty != 0)
        {
            G_ImmedBatchA.Dirty = 0;
            G_ImmedBatchA.VertCount = 0;
            G_ImmedBatchA.IndexCount = 0;
            G_ImmedBatchA.VertCursor = G_ImmedBatchA.VertBase;
            G_ImmedBatchA.IndexCursor = G_ImmedBatchA.IndexBase;
        }
        if (G_ImmedBatchB.Dirty != 0)
        {
            G_ImmedBatchB.Dirty = 0;
            G_ImmedBatchB.VertCount = 0;
            G_ImmedBatchB.IndexCount = 0;
            G_ImmedBatchB.VertCursor = G_ImmedBatchB.VertBase;
            G_ImmedBatchB.IndexCursor = G_ImmedBatchB.IndexBase;
        }
    }

    // {state, value} tables the flush walks until a zero state: DAT_005e6014 (render states
    // on), DAT_005e602c (render states off), DAT_005e603c (texture stage states, applied to
    // stage 0 and then stage 1).
    struct FImmedStatePair
    {
        int State;
        int Value;
    };
    FImmedStatePair const kImmedRsOn[] = {{0x8d, 1}, {0x91, 1}, {0, 0}};
    FImmedStatePair const kImmedRsOff[] = {{0x8d, 0}, {0, 0}};
    FImmedStatePair const kImmedTss[] = {{1, 1}, {2, 2}, {3, 1}, {4, 1}, {5, 2}, {6, 1}, {0, 0}};

    // The handle the flush passes for "the pipeline's current buffer".
    void* const kImmedCurrentBuffer = reinterpret_cast<void*>(static_cast<intptr_t>(-1));

    // Retail's EH-guarded stack object: the state is read into it and set to 0, and written
    // back when it goes out of scope.
    struct FImmedStateSaver
    {
        IRenderPipeline* Pipe;
        int State;
        int Saved;

        explicit FImmedStateSaver(int InState) : Pipe(DALib::Pipeline), State(InState), Saved(0)
        {
            Pipe->GetRenderState((D3DRENDERSTATETYPE)State, &Saved);
            Pipe->SetRenderState((D3DRENDERSTATETYPE)State, 0);
        }

        ~FImmedStateSaver()
        {
            Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Saved);
        }
    };

    // FUN_005a2ee0 (thiscall on a batcher).
    void FlushImmedBatch(FImmedBatcher* Batch)
    {
        // The dirty byte is raised before the empty test.
        Batch->Dirty = 1;
        if (Batch->VertCount == 0)
            return;

        FSpaceCamera* const WinCamera = Win3DGetCamera();
        if (Batch->bOrtho == 0)
        {
            Win3DApplyCamera(AsCamera(WinCamera));
            Win3DDisable(kWin3DLightingCap);
            Win3DDisable(kWin3DDepthTest);
            Win3DDisable(kWin3DCullFace);
            Win3DDisable(kWin3DTexture2D);
            Win3DEnable(kWin3DBlend);
            Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
        }
        else
        {
            Win3DDisable(kWin3DLightingCap);
            Win3DDisable(kWin3DTexture2D);
            Win3DDisable(kWin3DCullFace);
            Win3DDisable(kWin3DDepthTest);
            Win3DDisable(kWin3DFog);
            DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            Win3DSetOrtho2DForCamera(WinCamera);
            DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, -1);
            Win3DEnable(kWin3DBlend);
            Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
        }

        FRpVertexSrcDesc Desc;
        memset(&Desc, 0, sizeof(Desc));
        Desc.Flags = 2;
        Desc.Position.Base = reinterpret_cast<unsigned char const*>(Batch->VertBase);
        Desc.Position.ByteSize = 0xc;
        Desc.Position.Stride = 0x10;
        Desc.Diffuse.Base = reinterpret_cast<unsigned char const*>(Batch->VertBase) + 0xc;
        Desc.Diffuse.ByteSize = 4;
        Desc.Diffuse.Stride = 0x10;
        Desc.SrcFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;
        Desc.SrcCount = static_cast<unsigned int>(Batch->VertCount);

        for (FImmedStatePair const* Pair = kImmedRsOn; Pair->State != 0; ++Pair)
            DALib::Pipeline->SetRenderState((D3DRENDERSTATETYPE)Pair->State, Pair->Value);
        {
            FImmedStateSaver LightingSaver(0x89);
            for (FImmedStatePair const* Pair = kImmedTss; Pair->State != 0; ++Pair)
                DALib::Pipeline->SetTextureStageState(0, Pair->State, Pair->Value);
            for (FImmedStatePair const* Pair = kImmedTss; Pair->State != 0; ++Pair)
                DALib::Pipeline->SetTextureStageState(1, Pair->State, Pair->Value);

            // The first non-zero result skips the rest; the draw's own result is not read.
            unsigned int VbOffset = 0;
            unsigned int IbOffset = 0;
            if (Batch->Vb->CopyVertices(kImmedCurrentBuffer, &VbOffset, &Desc, 0,
                                        (unsigned int)Batch->VertCount) == 0 &&
                Batch->Ib->CopyIndices(kImmedCurrentBuffer, &IbOffset, Batch->IndexBase,
                                       static_cast<unsigned int>(Batch->IndexCount)) == 0 &&
                Batch->Vb->SelectVb(kImmedCurrentBuffer) == 0 &&
                Batch->Ib->SelectIb(kImmedCurrentBuffer, VbOffset, 0, 0) == 0)
            {
                Batch->Draw->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0,
                                                  static_cast<unsigned int>(Batch->VertCount),
                                                  IbOffset,
                                                  static_cast<unsigned int>(Batch->IndexCount));
            }

            // The perspective again after an ortho flush; the saver restores RS 0x89 last.
            for (FImmedStatePair const* Pair = kImmedRsOff; Pair->State != 0; ++Pair)
                DALib::Pipeline->SetRenderState((D3DRENDERSTATETYPE)Pair->State, Pair->Value);
            if (Batch->bOrtho != 0)
                Win3DApplyCamera(AsCamera(WinCamera));
        }
    }

    // FUN_005a2df0 (thiscall). The cursors are left to the reset; the query results are not
    // read.
    void CreateImmedBatch(FImmedBatcher* Batch)
    {
        Batch->VertBase = static_cast<FImmedVertex*>(operator new(0xc000));
        Batch->IndexBase = static_cast<unsigned short*>(operator new(0x3000));
        Batch->VertCount = 0;
        Batch->IndexCount = 0;
        if (Batch->Vb == nullptr)
            DALib::Pipeline->QueryInterface(Iids::RPVertexBuffer,
                                            reinterpret_cast<void**>(&Batch->Vb));
        if (Batch->Ib == nullptr)
            DALib::Pipeline->QueryInterface(Iids::RPIndexBuffer,
                                            reinterpret_cast<void**>(&Batch->Ib));
        if (Batch->Draw == nullptr)
            DALib::Pipeline->QueryInterface(Iids::RPDraw, reinterpret_cast<void**>(&Batch->Draw));
    }

    // FUN_005a2e80 (thiscall).
    void DestroyImmedBatch(FImmedBatcher* Batch)
    {
        if (Batch->VertBase != nullptr)
        {
            operator delete(Batch->VertBase);
            Batch->VertBase = nullptr;
        }
        if (Batch->IndexBase != nullptr)
        {
            operator delete(Batch->IndexBase);
            Batch->IndexBase = nullptr;
        }
        if (Batch->Vb != nullptr)
        {
            Batch->Vb->Release();
            Batch->Vb = nullptr;
        }
        if (Batch->Ib != nullptr)
        {
            Batch->Ib->Release();
            Batch->Ib = nullptr;
        }
        if (Batch->Draw != nullptr)
        {
            Batch->Draw->Release();
            Batch->Draw = nullptr;
        }
    }

    void FlushImmedBatchers()
    {
        // FUN_005a2a20
        FlushImmedBatch(&G_ImmedBatchA);
        FlushImmedBatch(&G_ImmedBatchB);
    }

    void EnsureButtonMontage()
    {
        // FUN_00559c10
        if (!G_bButtonMontageLoaded)
            LoadButtonMontage();
    }

    void FlushMontageButtons()
    {
        // FUN_00559c20 -- the body lives in NnMontageFlushButtons (nn_montage_draw.cpp).
        NnMontageFlushButtons();
    }

    void PrepUiOverlayPipeline()
    {
        // FUN_00417f10. Retail first calls an empty function (0x00401e90) with 0x1701.
        Win3DPushProjection();
        Win3DSetOrtho2D();

        IRenderPipeline* Pipe = GetPipe();
        if (Pipe != nullptr)
        {
            int SavedTss10 = 0;
            int SavedTss11 = 0;
            Pipe->GetTextureStageState(0, D3DTSS_MAGFILTER, &SavedTss10);
            Pipe->GetTextureStageState(0, D3DTSS_MINFILTER, &SavedTss11);
            if (G_UiOverlayForceTss != 0)
            {
                Pipe->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
                Pipe->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
            }

            int Saved17 = 0;
            int Saved0E = 0;
            Pipe->GetRenderState(D3DRS_ZFUNC, &Saved17);
            Pipe->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            Pipe->GetRenderState(D3DRS_ZWRITEENABLE, &Saved0E);
            Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);

            // The font atlas list (DAT_00616670), found by its key with no material-key call
            // before it.
            ScreenQuadFlush(Pipe, FontAtlasMaterialKey());

            Pipe->SetTextureStageState(0, D3DTSS_MAGFILTER, SavedTss10);
            Pipe->SetTextureStageState(0, D3DTSS_MINFILTER, SavedTss11);
            Win3DPopProjection();
            Pipe->SetRenderState(D3DRS_ZWRITEENABLE, Saved0E);
            Pipe->SetRenderState(D3DRS_ZFUNC, Saved17);
        }
        else
        {
            Win3DPopProjection();
        }
    }
}

void Win3DImmedBatchReset()
{
    ResetImmedBatchDirty();
}

void Win3DImmedBatchFlushAll()
{
    FlushImmedBatchers();
}

void ImmedBatchAddLine(FImmedBatcher* Batch, Vector const* A, Vector const* B,
                       unsigned long Color, unsigned long EdgeColor, float Width)
{
    // FUN_005a2a40. Signed compares; the reset is inlined.
    if (Batch->IndexCount + 12 >= 0x1800 || Batch->VertCount + 8 >= 0xc00)
    {
        FlushImmedBatch(&G_ImmedBatchA);
        FlushImmedBatch(&G_ImmedBatchB);
        ResetImmedBatchDirty();
    }

    // The index count first, then twelve indices on the current vertex count (16-bit adds),
    // then the vertex count.
    Batch->IndexCount += 12;
    static unsigned short const kLineIndexOffsets[12] = {0, 1, 2, 2, 3, 1, 4, 5, 6, 6, 7, 5};
    for (int Index = 0; Index < 12; ++Index)
    {
        *Batch->IndexCursor = static_cast<unsigned short>(
            static_cast<unsigned short>(Batch->VertCount) + kLineIndexOffsets[Index]);
        ++Batch->IndexCursor;
    }
    Batch->VertCount += 8;

    // atan2(dx, dy) -- the arguments in that order -- plus DAT_005d3e88. x87: the cosine is
    // rounded to float before the multiply; the sine * Width stays double.
    double const Dx = static_cast<double>(B->x) - static_cast<double>(A->x);
    double const Dy = static_cast<double>(B->y) - static_cast<double>(A->y);
    double const Angle = atan2(Dx, Dy) + 1.57079632675;
    float const CosAngle = static_cast<float>(cos(Angle));
    double const SinW = sin(Angle) * static_cast<double>(Width);
    double const CosW = static_cast<double>(CosAngle) * static_cast<double>(Width);

    // A, A + off, B, B + off, A, A - off, B, B - off with off = (SinW, CosW, 0).
    FImmedVertex Vertices[8];
    Vertices[0] = {A->x, A->y, A->z, Color};
    Vertices[1] = {static_cast<float>(static_cast<double>(A->x) + SinW),
                   static_cast<float>(static_cast<double>(A->y) + CosW), A->z, EdgeColor};
    Vertices[2] = {B->x, B->y, B->z, Color};
    Vertices[3] = {static_cast<float>(static_cast<double>(B->x) + SinW),
                   static_cast<float>(static_cast<double>(B->y) + CosW), B->z, EdgeColor};
    Vertices[4] = {A->x, A->y, A->z, Color};
    Vertices[5] = {static_cast<float>(static_cast<double>(A->x) - SinW),
                   static_cast<float>(static_cast<double>(A->y) - CosW), A->z, EdgeColor};
    Vertices[6] = {B->x, B->y, B->z, Color};
    Vertices[7] = {static_cast<float>(static_cast<double>(B->x) - SinW),
                   static_cast<float>(static_cast<double>(B->y) - CosW), B->z, EdgeColor};
    for (int Index = 0; Index < 8; ++Index)
    {
        *Batch->VertCursor = Vertices[Index];
        ++Batch->VertCursor;
    }
}

void Win3DImmedBatchCreateAll()
{
    // FUN_005a2970
    CreateImmedBatch(&G_ImmedBatchA);
    CreateImmedBatch(&G_ImmedBatchB);
    G_ImmedBatchB.bOrtho = 1;
}

void Win3DImmedBatchDestroyAll()
{
    // FUN_005a2990
    DestroyImmedBatch(&G_ImmedBatchA);
    DestroyImmedBatch(&G_ImmedBatchB);
}

void Win3DImmedDrawLine(Vector const* A, Vector const* B, unsigned long Color,
                        unsigned long EdgeColor, float Width)
{
    // FUN_005a2940
    ImmedBatchAddLine(&G_ImmedBatchB, A, B, Color, EdgeColor, Width);
}

void ButtonMontageEnsure()
{
    EnsureButtonMontage();
}

void ButtonMontageFlushBatch()
{
    FlushMontageButtons();
}

void UiOverlayPrepPipeline()
{
    PrepUiOverlayPipeline();
}

void Win3DFlushBeforeOverlayDraw()
{
    // FUN_0045a9f0, in retail's call order.
    FlushImmedBatchers();
    ResetImmedBatchDirty();
    FlushMontageButtons();
    EnsureButtonMontage();
    PrepUiOverlayPipeline();
}
