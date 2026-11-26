// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Deformable2.h"
#include "EyeMaterial.h"     // RegisterEyeMaterialFactory (FUN_065fdfd0)
#include "common_misc.h"   // ICamera (GetViewTransform +0x40, ClassifySphere +0x3C)
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include <cstddef>
#include <cstdarg>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <vector>

static I3DMathEngine* G_MathEngine = nullptr; // DAT_066033a8 FUN_06600de0
static const int G_PartDescStride = 0x18;
static const int G_MaxLodCount = 8;

// Enumerate FileSystem children using the published ReadFile contract.
int FsFindFirst(IFileSystem* Fs, const char* Pattern, WIN32_FIND_DATAA* FindData)
{
    return Fs->FindFirst(Pattern,
                      FindData);
}

int FsFindNext(IFileSystem* Fs, int Finder, WIN32_FIND_DATAA* FindData)
{
    return Fs->FindNext((unsigned long)Finder, FindData);
}

void FsFindClose(IFileSystem* Fs, int Finder)
{
    Fs->FindClose((unsigned long)Finder);
}

// FUN_065fdc60 (via FUN_065fdca0, vtable slot 6).
FVmeshWrapper::~FVmeshWrapper()
{
    if (EngineHandle != InvalidArchetypeIndex)
        G_Engine->ReleaseHandle(EngineHandle);
    if (VMeshPart != nullptr)
    {
        VMeshPart->Release();
        VMeshPart = nullptr;
    }
}

// FUN_065fdcc0; vtable PTR_LAB_066013dc.
FVmeshWrapper::FVmeshWrapper(const char* Name, ArchetypeIndex Handle, IVMeshPart* Part)
    : FDeformMesh(Name)
    , EngineHandle(Handle)
    , VMeshPart(Part)
{
    if (Part != nullptr)
        Part->AddRef();
}

// FUN_065fdf80 (thiscall on Dest). Retail does not test either pointer for null.
static void VmeshCopy12(void* Dest, const void* Src)
{
    std::memcpy(Dest, Src, 0x30);
}

// FUN_06600de0: CreateInstance "3DMathEngine" (the decompiler's ICOManager return type is wrong).
// Retail keeps one lazily-filled cache per call site (DAT_06603170, DAT_066031a4, ...); the port
// shares one.
I3DMathEngine* AcquireMathEngine()
{
    if (G_MathEngine != nullptr)
        return G_MathEngine;
    ICOManager* Manager = DACOM_Acquire();
    FIkMathDesc Desc;
    Desc.Size = 0xC;
    Desc.Name = G_Name3DMathEngine;
    Desc.Extra = nullptr;
    void* Instance = nullptr;
    Manager->CreateInstance((AGGDESC*)(&Desc), &Instance);
    G_MathEngine = (I3DMathEngine*)Instance;
    return G_MathEngine;
}

// Saves one render state on construction and puts it back on destruction: LAB_065fdd10 keeps
// three {pipeline, state, old value} triples and restores them in reverse order on return.
struct FSavedRenderState
{
    IRenderPipeline* Pipeline;
    D3DRENDERSTATETYPE State;
    int Old;

    FSavedRenderState(IRenderPipeline* InPipeline, D3DRENDERSTATETYPE InState)
        : Pipeline(InPipeline)
        , State(InState)
        , Old(0)
    {
        Pipeline->GetRenderState(State, &Old);
    }
    ~FSavedRenderState()
    {
        Pipeline->SetRenderState(State, Old);
    }
    FSavedRenderState(const FSavedRenderState&) = delete;
    FSavedRenderState& operator=(const FSavedRenderState&) = delete;
};

// DAT_06602a60: 1 in .data and never written (four reads, all in LAB_065fdd10). It gates the
// bone palette and feeds D3DRS_SOFTWAREVERTEXPROCESSING / D3DRS_INDEXEDVERTEXBLENDENABLE and,
// as 3 or 0, D3DRS_VERTEXBLEND.
static unsigned long G_VmeshPaletteSkinning = 1;

// LAB_065fdd10, the VMesh wrapper's slot 5 (Camera, Part).
unsigned char FVmeshWrapper::Draw(ICamera* Camera, FDeformPart* Part)
{
    FLodHolder* const Holder = Part->Lod;
    const Transform* const View = Camera->GetViewTransform();
    int bHaveView = 1;
    G_Pipeline->GetPipelineState(7, &bHaveView);
    if (VMeshPart == nullptr)
        return 1;
    if (G_VmeshPaletteSkinning != 0 && Holder->BindCursor > 0)
    {
        int WorldIndex = 1;
        do
        {
            FBoneRuntime* const Bone = &Holder->BoneSlots[WorldIndex - 1];
            if (bHaveView != 0)
            {
                Transform Composed;
                AcquireMathEngine()->MulTransform(Composed,
                                                  *View,
                                                  Bone->WorldMatrix); // +0x1C
                Transform Copy;
                VmeshCopy12(&Copy, &Composed);
                G_Pipeline->SetWorldN(WorldIndex, &Copy);
            }
            else
                G_Pipeline->SetWorldN(WorldIndex, &Bone->WorldMatrix);
            WorldIndex += 1;
        } while (WorldIndex - 1 < Holder->BindCursor);
    }

    FSavedRenderState SoftwareVertexProcessing(G_Pipeline, D3DRS_SOFTWAREVERTEXPROCESSING);
    G_Pipeline->SetRenderState(D3DRS_SOFTWAREVERTEXPROCESSING, (int)G_VmeshPaletteSkinning);
    FSavedRenderState IndexedVertexBlend(G_Pipeline, D3DRS_INDEXEDVERTEXBLENDENABLE);
    G_Pipeline->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, (int)G_VmeshPaletteSkinning);
    FSavedRenderState VertexBlend(G_Pipeline, D3DRS_VERTEXBLEND);
    G_Pipeline->SetRenderState(D3DRS_VERTEXBLEND,
                              G_VmeshPaletteSkinning != 0 ? D3DVBF_3WEIGHTS : D3DVBF_DISABLE);
    VMeshPart->Draw(Camera, Holder->BoneSlots[0].Instance, 0, nullptr, nullptr);
    return 1;
}
