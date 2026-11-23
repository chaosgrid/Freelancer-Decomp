// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IRenderPipeline.h"

// Pipeline / library slot numbers from docs/re/render.md and docs/re/shading.md.
// Call through the live vtable. Do not invent C++ method names for unnamed slots.

static const int kPipelineQueryDeviceAbility = 8;     // +0x20
static const int kPipelineSetModelview = 31;          // +0x7C
static const int kPipelineSetPerspective = 37;        // +0x94
static const int kPipelineSetMaterial = 43;           // +0xAC
static const int kPipelineGetMaterial = 44;           // +0xB0
static const int kPipelineGetRenderState = 61;        // +0xF4
static const int kPipelineSetRenderState = 60;        // +0xF0
static const int kPipelineSetTextureStageState = 62;  // +0xF8
static const int kPipelineGetTextureStageState = 63;  // +0xFC (EnvMap: unnamed counterpart to
                                                       // SetTextureStageState +0xF8, used by
                                                       // LAB_066F5AF0 to probe which of stage
                                                       // 0/1 already carries TSS type 1 == 1
                                                       // before picking the stage to apply the
                                                       // dynamic envmap TSS pair at)
static const int kPipelineSetTextureStageTransform = 64; // +0x100
static const int kPipelineSetTextureStageTexture = 66;   // +0x108
static const int kPipelineVerifyState = 68;           // +0x110
static const int kPipelineDrawPrimitiveVb = 71;       // +0x11C
static const int kPipelineDrawIndexedPrimitiveVb = 72; // +0x120
static const int kVbMgrLockVertexMemory = 5;          // +0x14
static const int kVbMgrUnlockVertexMemory = 6;        // +0x18
static const int kVbMgrFillVertexMemory = 8;          // +0x20 (IVertexBufferManager's own
                                                       // slot 8, not the pipeline's: takes
                                                       // (VbMgr, DestBase, Fvf, Mesh, Start, Count)
                                                       // and walks the Mesh's vertex arrays into
                                                       // the locked buffer; FlmFillMeshAndDraw)
static const int kEngineGetInstanceTransform = 40;    // +0xA0
static const int kEngineGetInstanceHandle = 25;       // +0x64
static const int kEngineReleaseHandle = 15;           // +0x3C
// IEngine::GetInstanceRelative (GetInstanceParent is slot 53).
static const int kEngineGetInstanceRelative = 49;     // +0xC4 (parent ? inst+0x5C : 0)
static const int kEngineGetRoot = 55;                 // +0xDC (root via +0x10)
static const int kEngineQueryInstance = 30;           // +0x78 IEngine->QueryInstance(inst, iid,
                                                       // &out); flmaterials Wire's Slot10 fetches
                                                       // IVMeshWire/IMesh off G_FlmEngineOuter
static const int kRendererGetExtents = 4;             // +0x10

static const int kMaterialLibraryFindOrCreate = 15; // +0x3C
static const int kMaterialLibrarySlot18 = 18;       // +0x48
static const int kMaterialLibrarySlot23 = 23;       // +0x5C
static const int kMaterialLibrarySlot25 = 25;       // +0x64

static const int kMatAnimLibrarySlot5 = 5; // +0x14

static const int kBatchedSlot6 = 6;  // +0x18
static const int kBatchedSlot7 = 7;  // +0x1C (Shadow's Slot9, LAB_066fa530, calls it on the
                                      // QI'd IBatchedMaterial; Backdrop/Cloak use slot 6)
static const int kBatchedSlot8 = 8;  // +0x20
static const int kBatchedSlot17 = 17; // +0x44
static const int kBatchedSlot18 = 18; // +0x48

// Ordinary virtual calls on IRenderPipeline; the kPipeline* numbers above are the offset map,
// the arity is IRenderPipeline.h's.
inline GENRESULT PipelineQueryDeviceAbility(IRenderPipeline* Pipeline, unsigned long Index,
                                            unsigned long* Out)
{
    return Pipeline->QueryDeviceAbility(static_cast<unsigned int>(Index), Out);
}

inline GENRESULT PipelineSetModelview(IRenderPipeline* Pipeline, const float* Matrix12)
{
    return Pipeline->SetModelview(Matrix12);
}

inline GENRESULT PipelineDrawPrimitiveVb(IRenderPipeline* Pipeline, unsigned long Prim, void* Vb,
                                          unsigned long StartVertex, unsigned long Count,
                                          void* Unused)
{
    return Pipeline->DrawPrimitiveVb(Prim, Vb, static_cast<unsigned int>(StartVertex),
                                     static_cast<unsigned int>(Count), Unused);
}

// Slot 72 takes seven arguments; retail callers push 0 for the unread last one.
inline GENRESULT PipelineDrawIndexedPrimitiveVb(IRenderPipeline* Pipeline, unsigned long Prim,
                                                 void* Vb, unsigned long StartVertex,
                                                 unsigned long VertCount, void* Indices,
                                                 unsigned long IndexCount)
{
    return Pipeline->DrawIndexedPrimitiveVb(Prim, Vb, static_cast<unsigned int>(StartVertex),
                                            static_cast<unsigned int>(VertCount), Indices,
                                            static_cast<unsigned int>(IndexCount), 0);
}

inline GENRESULT PipelineSetPerspective(IRenderPipeline* Pipeline, float FovDeg, float Aspect,
                                         float NearPlane, float FarPlane)
{
    return Pipeline->SetPerspective(FovDeg, Aspect, NearPlane, FarPlane);
}

inline GENRESULT PipelineSetMaterial(IRenderPipeline* Pipeline, const D3DMATERIAL8* Block)
{
    return Pipeline->SetMaterial(Block);
}

inline GENRESULT PipelineGetMaterial(IRenderPipeline* Pipeline, D3DMATERIAL8* Block)
{
    return Pipeline->GetMaterial(Block);
}

inline GENRESULT PipelineSetRenderState(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State,
                                         unsigned long Value)
{
    return Pipeline->SetRenderState(State, (int)Value);
}

// The int / unsigned long bridge is the port's (the declared slot takes int*); retail passes
// the caller's out pointer straight through.
inline GENRESULT PipelineGetRenderState(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State,
                                         unsigned long* Out)
{
    int Value = 0;
    const GENRESULT Result = Pipeline->GetRenderState(State, &Value);
    *Out = static_cast<unsigned long>(Value);
    return Result;
}

// No null test: every retail SetTextureStageState site calls [vtable+0xF8] straight through
// the pipeline pointer (Freelancer.exe 0x0050b31c/0x0050b358/0x0050b37a, flmaterials.dll's
// bind paths). Callers own the pipeline's validity, exactly as in retail.
inline GENRESULT PipelineSetTextureStageState(IRenderPipeline* Pipeline, unsigned long Stage,
                                               unsigned long Type, unsigned long Value)
{
    return Pipeline->SetTextureStageState(static_cast<int>(Stage), static_cast<int>(Type),
                                         static_cast<int>(Value));
}

inline GENRESULT PipelineGetTextureStageState(IRenderPipeline* Pipeline, unsigned long Stage,
                                               unsigned long Type, unsigned long* Out)
{
    int Value = 0;
    const GENRESULT Result =
        Pipeline->GetTextureStageState(static_cast<int>(Stage), static_cast<int>(Type), &Value);
    *Out = static_cast<unsigned long>(Value);
    return Result;
}

inline GENRESULT PipelineSetTextureStageTexture(IRenderPipeline* Pipeline, unsigned long Stage,
                                                 unsigned long Handle)
{
    return Pipeline->SetTextureStageTexture(static_cast<int>(Stage),
                                           reinterpret_cast<void*>(Handle));
}

// Slot 64's second argument is the 4x4 UV matrix, not a flags dword: flmaterials.dll
// 0x066f9e3e and 0x067001e3 both push the address of a 0x40-byte stack buffer that the
// preceding MatAnim slot 6 call just filled.
inline GENRESULT PipelineSetTextureStageTransform(IRenderPipeline* Pipeline, unsigned long Stage,
                                                   const float* Matrix16)
{
    return Pipeline->SetTextureStageTransform(static_cast<int>(Stage), Matrix16);
}

inline GENRESULT PipelineVerifyState(IRenderPipeline* Pipeline)
{
    return Pipeline->VerifyState();
}

// RS/TSS tables: first dword is the start state/type (not consumed as a value).
// Then [+4]=value, [+8]=next, stride 8, stop when next==0.
// Retail inlines this loop at every site (Freelancer.exe 0x0050b366..0x0050b388) with no test
// of the pipeline or the table pointer; the tables are constants in .rdata.
inline void PipelineApplyRsTable(IRenderPipeline* Pipeline, const unsigned long* Table)
{
    unsigned long State = Table[0];
    const unsigned long* Cursor = Table;
    while (State != 0)
    {
        PipelineSetRenderState(Pipeline, (D3DRENDERSTATETYPE)State, Cursor[1]);
        State = Cursor[2];
        Cursor += 2;
    }
}

inline void PipelineApplyTssTable(IRenderPipeline* Pipeline, unsigned long Stage,
                                  const unsigned long* Table)
{
    unsigned long Type = Table[0];
    const unsigned long* Cursor = Table;
    while (Type != 0)
    {
        PipelineSetTextureStageState(Pipeline, Stage, Type, Cursor[1]);
        Type = Cursor[2];
        Cursor += 2;
    }
}
