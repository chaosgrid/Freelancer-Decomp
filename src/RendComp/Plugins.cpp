// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RendComp.h"
#include "TriMesh.h"         // the TriMesh archetype (FTriMeshArch)
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "common_misc.h"     // ICamera (the sphere draw reads the camera)
#include "render/shading.h" // IMaterialLibrary, FShadingPropertyValue (the sphere archetype)

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <string>
#include <vector>

static int IrcHandleOk(void* Handle)
{
    if (Handle == nullptr || Handle == (void*)G_HandleFail)
        return 0;
    return 1;
}

// LAB_06c215a0's QueryGeometry test: the mode is argument A, the flags B, the out pointer D.
static int QueryGeometryCanWrite(int QueryMode, unsigned long Flags, void* Out)
{
    if (QueryMode != 0 && QueryMode != 2 && QueryMode != 3)
        return 0;
    if ((Flags & 0xFFF) != 3)
        return 0;
    if ((Flags & 0x4000) == 0)
        return 0;
    if (Out == nullptr)
        return 0;
    return 1;
}

class FTriMeshAnim;

// DAT_06c37cac (five dwords): the archetype's
// texcoord-0 stream (+0x5C) saved while a TriMesh instance with a UV0 track draws.
static FRpVertexStream G_TriMeshSavedUv0;

static GENRESULT TriMeshAnimQueryInterface(FTriMeshAnim* Anim, const char* Iid, void** Out);

class FRenderPlugin : public IRenderComponent
{
public:
    unsigned long RefCount;
    std::map<void*, FTriMeshAnim*> InstanceMap;
    void* LastHitKey;
    FTriMeshAnim* LastHitAnim;
    unsigned char bLastHit;
    unsigned long LookupCount;
    unsigned long HitCount;

    FRenderPlugin()
        : RefCount(1)
        , LastHitKey(nullptr)
        , LastHitAnim(nullptr)
        , bLastHit(0)
        , LookupCount(0)
        , HitCount(0)
    {
    }

    virtual ~FRenderPlugin();

    FTriMeshAnim* FindInstance(void* InstId);
    void ClearLastHit(void* InstId);
    void DropInstance(void* InstId);
    void TickInstance(void* InstId, void* Arg);
    void TickAllInstances(void* Arg);
    void DestroyInstanceMap();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        if (std::strcmp(Iid, Iids::RenderComponent) != 0)
        {
            *Out = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }
        IRenderComponent* Interface = (IRenderComponent*)this;
        Interface->AddRef();
        *Out = Interface;
        return GR_OK;
    }

    unsigned long DACOM_API AddRef() override
    {
        RefCount += 1;
        return RefCount;
    }

    unsigned long DACOM_API Release() override
    {
        if (RefCount != 0)
            RefCount -= 1;
        const unsigned long Count = RefCount;
        if (Count == 0)
        {
            delete this;
            return 0;
        }
        return Count;
    }

    int DACOM_API CloneHandle(void* /*Handle*/) override
    {
        return (int)G_HandleFail;
    }

    int DACOM_API QueryHandle(void* Handle, const char* Iid, void** Out) override
    {
        if (Handle == nullptr || Handle == (void*)G_HandleFail)
            return -1;
        IDAComponent* Object = (IDAComponent*)Handle;
        return Object->QueryInterface(Iid, Out);
    }

    int DACOM_API CreateMeshInstance(void* /*InstId*/, void* Handle) override
    {
        if (Handle == nullptr || Handle == (void*)G_HandleFail)
            return 0;
        return 1;
    }

    int DACOM_API DestroyMeshInstance(void* InstId, void* /*Handle*/) override
    {
        DropInstance(InstId);
        return 0;
    }

    int DACOM_API UpdateMeshInstance(void* /*InstId*/, void* /*Arg*/) override
    {
        return 0;
    }

    int DACOM_API QueryMeshInstance(void* InstId, void* /*Handle*/, const char* Iid, void** Out) override
    {
        LookupCount += 1;
        FTriMeshAnim* Anim = FindInstance(InstId);
        if (Anim == nullptr)
            return -1;
        return (TriMeshAnimQueryInterface(Anim, Iid, Out) >= 0) ? 0 : -1;
    }

    int DACOM_API UpdateMeshInstances(void* /*InstId*/) override
    {
        return 0;
    }

    int DACOM_API QueryGeometry(void* Handle, int /*Lod*/, void* A, void* B, void* /*C*/, void* Out,
                         void* /*E*/) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        const int QueryMode = (int)((intptr_t)A);
        const unsigned long Flags = (unsigned long)((uintptr_t)B);
        if (QueryGeometryCanWrite(QueryMode, Flags, Out) == 0)
            return 0;
        return 1;
    }

    int DACOM_API GetBounds(void* Handle, float /*Lod*/, void* /*Out*/) override
    {
        if (Handle == nullptr || Handle == (void*)G_HandleFail)
            return 0;
        return 1;
    }

    int DACOM_API GetCentroid(void* Handle, float /*Lod*/, void* /*Out*/) override
    {
        if (Handle == nullptr || Handle == (void*)G_HandleFail)
            return 0;
        return 1;
    }

    // As SphereMesh LAB_06c29bc0, whose FUN_06c2aef0 always fails: 0 either way.
    int DACOM_API SplitByPlane(void* Handle, void* /*A*/, void* /*B*/, void* /*C*/, void* /*D*/, void* /*E*/,
                         void* /*F*/) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        return 0;
    }

    // FUN_06c21a00 (TriMesh table 06c36114; SphereMesh overrides it). Defined after
    // FTriMeshAnim.
    int DACOM_API BindMeshInstance(void* InstId, void* Handle) override;
};

// DAT_06c37d80: the 0x400-byte buffer FUN_06c211a0 reads the "Tristrips" value into.
static char G_TriMeshProfileValue[0x400];

static GENRESULT TriMeshPluginReadProfile(int* Flag, const FRenderPluginDesc* Desc)
{
    if (Desc->Parent == nullptr || Desc->Engine == nullptr)
        return (GENRESULT)0xFFFFFFFE;
    ICOManager* Dacom = DACOM_Acquire();
    IProfileParser* First = nullptr;
    if (Dacom->QueryInterface(Iids::ProfileParser, (void**)(&First)) < 0)
    {
        if (First != nullptr)
            First->Release();
        return GR_OK;
    }
    IProfileParser* Parser = nullptr;
    if (First != nullptr)
    {
        Parser = First;
        Parser->AddRef();
    }
    if (Parser != nullptr ||
        Dacom->QueryInterface(Iids::ProfileParser, (void**)(&Parser)) == 0)
    {
        *Flag = 1;
        void* Section = Parser->OpenSection(G_NameTriMesh, 3);
        if (Section != nullptr)
        {
            if (Parser->GetString(Section, "Tristrips", G_TriMeshProfileValue,
                                  sizeof(G_TriMeshProfileValue)) != 0)
            {
                const char TokenChars[] = "0123456789truefalsynoxTRUEFALSYNOX+-";
                const size_t Span = std::strspn(G_TriMeshProfileValue, TokenChars);
                if (Span != 0)
                    G_TriMeshProfileValue[Span] = '\0';
                if (_stricmp(G_TriMeshProfileValue, "true") == 0)
                    *Flag = 1;
                else if (_stricmp(G_TriMeshProfileValue, "yes") == 0)
                    *Flag = 1;
                else if (_stricmp(G_TriMeshProfileValue, "false") == 0)
                    *Flag = 0;
                else if (_stricmp(G_TriMeshProfileValue, "no") == 0)
                    *Flag = 0;
                else if (std::strchr(G_TriMeshProfileValue, 'x') != nullptr ||
                         std::strchr(G_TriMeshProfileValue, 'X') != nullptr)
                    *Flag = (int)(std::strtoul(G_TriMeshProfileValue, nullptr, 16));
                else
                    *Flag = MsvcrtAtoi(G_TriMeshProfileValue);
            }
            Parser->CloseSection(Section);
        }
    }
    if (Parser != nullptr)
        Parser->Release();
    if (First != nullptr)
        First->Release();
    return GR_OK;
}

// MSVCRT _ftol: truncation towards zero into a 64-bit integer; the callers keep EAX.
static unsigned long TriMeshFtol(double Value)
{
    return (unsigned long)((long long)Value);
}

// Animated TriMesh instance with material and UV-animation facets.
class FTriMeshAnim : public IMesh
{
public:
    class FHookFacet : public IMatHookUp
    {
    public:
        FTriMeshAnim* Owner;

        explicit FHookFacet(FTriMeshAnim* InOwner)
            : Owner(InOwner)
        {
        }

        // LAB_06c29ac0 (this - 4, then LAB_06c29a00).
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            return Owner->QueryInterface(Iid, Out);
        }

        // LAB_06c27670 -> FUN_06c2b100, which returns 1.
        unsigned long DACOM_API AddRef() override
        {
            return 1;
        }

        unsigned long DACOM_API Release() override
        {
            return 1;
        }

        // LAB_06c29830: tail-jumps to the live archetype's (+0x10) IMatHookUp facet, slot 3.
        int DACOM_API HookUpMaterials() override
        {
            return Owner->MeshLive->HookFacet.HookUpMaterials();
        }
    };

    class FAnimFacet : public ITriMeshAnim
    {
    public:
        FTriMeshAnim* Owner;

        explicit FAnimFacet(FTriMeshAnim* InOwner)
            : Owner(InOwner)
        {
        }

        // LAB_06c29ad0 (this - 8, then LAB_06c29a00).
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            return Owner->QueryInterface(Iid, Out);
        }

        // LAB_06c27690 -> FUN_06c2b100, which returns 1.
        unsigned long DACOM_API AddRef() override
        {
            return 1;
        }

        unsigned long DACOM_API Release() override
        {
            return 1;
        }

        void AdvanceUvAnimation(float Dt) override
        {
            FTriMeshAnim* Anim = Owner;
            if (Anim->UvTrack == nullptr)
                return;
            const FTriMeshUvAnim* Uv = &Anim->MeshLive->UvAnim;
            float* Dest = (float*)((unsigned char*)Anim->UvTrack->Base);
            if (Uv->LookupCount == 0)
                return;
            const double Sum = (double)Dt + (double)Anim->Clock;
            Anim->Clock = (float)Sum;
            if (Sum >= (double)Uv->Period)
                Anim->Clock = (float)(Sum - (double)Uv->Period);
            const double Position = (double)Anim->Clock * (double)Uv->Fps;
            const unsigned long Frame = TriMeshFtol(Position) % (Uv->FrameCount - 1);
            const float T = (float)(Position - (double)((long)Frame));
            const unsigned long Count = Uv->LookupCount;
            if (Uv->Interpolate != 0)
            {
                const float* A = Uv->Frames + Count * Frame * 2;
                const float* B = Uv->Frames + (Count * Frame + Count) * 2;
                for (unsigned long Index = 0; Index < Count; ++Index)
                {
                    const long Slot = Uv->Lookup[Index];
                    if (Slot == -1)
                        continue;
                    const float U = (float)(
                        ((double)B[Index * 2] - A[Index * 2]) * T + A[Index * 2]);
                    const float V = (float)(
                        ((double)B[Index * 2 + 1] - A[Index * 2 + 1]) * T + A[Index * 2 + 1]);
                    Dest[Slot * 2] = U;
                    Dest[Slot * 2 + 1] = V;
                }
                return;
            }
            const unsigned long Previous = TriMeshFtol(
                ((double)Anim->Clock - (double)Dt) * (double)Uv->Fps);
            if (Frame == Previous)
                return;
            const float* A = Uv->Frames + Count * Frame * 2;
            for (unsigned long Index = 0; Index < Count; ++Index)
            {
                const long Slot = Uv->Lookup[Index];
                if (Slot == -1)
                    continue;
                Dest[Slot * 2] = A[Index * 2];
                Dest[Slot * 2 + 1] = A[Index * 2 + 1];
            }
        }
    };

    FHookFacet HookFacet;         // +0x04
    FAnimFacet AnimFacet;         // +0x08
    FTriMeshArch* Mesh;           // +0x0C
    FTriMeshArch* MeshLive;       // +0x10
    FRpVertexStream* UvTrack;     // +0x14 operator new(0x14), FUN_06c24c60 copy of the UV0 stream
    float Clock;                  // +0x18 UV0_anim clock (LAB_06c293a0)
    void* InstId;                 // +0x1C

    // FUN_06c292b0.
    FTriMeshAnim()
        : HookFacet(this)
        , AnimFacet(this)
        , Mesh(nullptr)
        , MeshLive(nullptr)
        , UvTrack(nullptr)
        , Clock(0.0f)
        , InstId((void*)((intptr_t)-1))
    {
    }

    // FUN_06c29310: an archetype with UV0_anim lookup entries (+0x8C) also gets a 0x14 track
    // copied from its texcoord-0 stream (+0x5C) with SrcCount (+0x88) indices (FUN_06c24c60).
    int Init(FTriMeshArch* InMesh, void* InInstId)
    {
        MeshLive = InMesh;
        Mesh = InMesh;
        InstId = InInstId;
        if (InMesh->UvAnim.LookupCount > 0)
        {
            UvTrack = new FRpVertexStream;
            TriMeshStreamCopy(&InMesh->Vertices.Tex[0], UvTrack, InMesh->Vertices.SrcCount);
        }
        return 0;
    }

    // FUN_06c29360; a track is freed with FUN_06c23dc0.
    int Reset()
    {
        MeshLive = nullptr;
        Mesh = nullptr;
        InstId = (void*)((intptr_t)-1);
        if (UvTrack != nullptr)
        {
            TriMeshStreamFree(UvTrack);
            delete UvTrack;
            UvTrack = nullptr;
        }
        return 0;
    }

    int Draw(ICamera* Camera, unsigned long Flags, const void* Transform, IMaterial* Extra)
    {
        if (MeshLive == nullptr || InstId == (void*)((intptr_t)-1))
            return 0;
        const FTransform* CameraView = nullptr;
        const FTransform* World = nullptr;
        PipelineApplySubmit(Camera, InstId, Transform, &CameraView, &World);

        FTriMeshArch* Arch = MeshLive;
        FRpVertexSrcDesc* Desc = &Arch->Vertices;
        unsigned long DrawFlags = Flags | 0xC;
        if (UvTrack != nullptr)
        {
            G_TriMeshSavedUv0 = Desc->Tex[0];
            Desc->Tex[0] = *UvTrack;
            DrawFlags &= ~0x4ul;
        }

        FVertexMemoryLock Lock;
        std::memset(&Lock, 0, sizeof(Lock));
        if (Desc->Flags != 0 && (DrawFlags & 0x80) == 0)
        {
            if (!Arch->GetPrebuiltListing(&Lock.Listing.VertexBuffer))
            {
                IVertexBufferManager* Manager = G_VbManager;
                if (Manager->LockVertexMemory(Desc->SrcFvf, Desc->SrcCount, &Lock) >= 0)
                {
                    Manager->CopyVertexData(Lock.Vertices, Lock.Listing.Fvf, Desc, 0, Desc->SrcCount);
                    Manager->UnlockVertexMemory(&Lock);
                }
            }
        }

        FMaterialSubmitContext Context;
        std::memset(&Context, 0, sizeof(Context));
        Context.Inst = InstId;
        Context.World = (FTransform*)World;
        Context.View = (FTransform*)CameraView;
        Context.VbListing = &Lock.Listing;
        for (size_t Index = 0; Index < Arch->Groups.size(); ++Index)
        {
            FTriMeshGroup* Group = Arch->Groups[Index];
            if (Group->Vertices != Group->SharedVertices)
                Context.VbListing = nullptr;
            Context.SourceVertices = Group->Vertices;
            Context.Indices = Group->Indices;
            IMaterial* Material = Group->Material;
            Context.MaterialContext = Group->MaterialContext;
            Context.ReplacedMaterial = nullptr;
            if (Extra != nullptr)
            {
                Context.ReplacedMaterial = Material;
                Context.MaterialContext = Group->MaterialContext;
                Material = Extra;
            }
            if (Group->IndexCount == 0 || Material == nullptr)
                continue;
            Material->Bind(&Context);
            Material->Submit(&Context, Group->Type, Group->MinIndex, Group->MaxIndex - Group->MinIndex + 1, 0,
                             Group->IndexCount, DrawFlags);
            Material->Teardown(&Context);
            if ((long)Group->Bucket >= 2)
                Context.VbListing = nullptr;
        }

        if (UvTrack != nullptr)
            Desc->Tex[0] = G_TriMeshSavedUv0;
        return 1;
    }

    // FUN_06c292f0: the vtables reset, then FUN_06c29360.
    virtual ~FTriMeshAnim()
    {
        Reset();
    }

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        if (RendCompIidEqual(Iid, Iids::Mesh))
        {
            *Out = (IMesh*)this;
            return GR_OK;
        }
        if (RendCompIidEqual(Iid, Iids::MatHookUp))
        {
            *Out = (IMatHookUp*)(&HookFacet);
            return GR_OK;
        }
        if (RendCompIidEqual(Iid, Iids::DAComponent))
        {
            *Out = (IMesh*)this;
            return GR_OK;
        }
        if (RendCompIidEqual(Iid, Iids::TriMeshAnim))
        {
            *Out = (ITriMeshAnim*)(&AnimFacet);
            return GR_OK;
        }
        return (GENRESULT)-1;
    }

    // FUN_06c2b100: returns 1.
    unsigned long DACOM_API AddRef() override
    {
        return 1;
    }

    unsigned long DACOM_API Release() override
    {
        return 1;
    }

    // LAB_06c29850..LAB_06c299f0: each tail-jumps to the same IMesh slot of the live archetype
    // (+0x10).
    int DACOM_API ResetMesh() override
    {
        return MeshLive->ResetMesh();
    }
    int DACOM_API NoOp() override
    {
        return MeshLive->NoOp();
    }
    int DACOM_API RefreshMaterials(unsigned long Flags) override
    {
        return MeshLive->RefreshMaterials(Flags);
    }
    // LAB_06c29880.
    int DACOM_API RejectOperation(void*) override
    {
        return -7;
    }
    // LAB_06c29890.
    int DACOM_API RejectOperation() override
    {
        return -7;
    }
    int DACOM_API GetFaceGroupCount(int* Out) override
    {
        return MeshLive->GetFaceGroupCount(Out);
    }
    int DACOM_API OpenFaceGroup(int Index, int Mode) override
    {
        return MeshLive->OpenFaceGroup(Index, Mode);
    }
    int DACOM_API CloseFaceGroup() override
    {
        return MeshLive->CloseFaceGroup();
    }
    int DACOM_API AddFaceGroup(void* A) override
    {
        return MeshLive->AddFaceGroup(A);
    }
    int DACOM_API RemoveFaceGroup(int Index) override
    {
        return MeshLive->RemoveFaceGroup(Index);
    }
    int DACOM_API GetFaceGroupPrimitiveType(unsigned int* Out) override
    {
        return MeshLive->GetFaceGroupPrimitiveType(Out);
    }
    int DACOM_API GetFaceGroupMaterial(IMaterial** Out) override
    {
        return MeshLive->GetFaceGroupMaterial(Out);
    }
    int DACOM_API SetFaceGroupMaterial(IMaterial* Material) override
    {
        return MeshLive->SetFaceGroupMaterial(Material);
    }
    int DACOM_API GetFaceGroupMaterialContext(void** Out) override
    {
        return MeshLive->GetFaceGroupMaterialContext(Out);
    }
    int DACOM_API SetFaceGroupMaterialContext(void* A) override
    {
        return MeshLive->SetFaceGroupMaterialContext(A);
    }
    int DACOM_API OpenFaceGroupChannels(unsigned long Flags) override
    {
        return MeshLive->OpenFaceGroupChannels(Flags);
    }
    int DACOM_API CloseFaceGroupChannels() override
    {
        return MeshLive->CloseFaceGroupChannels();
    }
    int DACOM_API LockFaceGroupChannels(unsigned long Mask) override
    {
        return MeshLive->LockFaceGroupChannels(Mask);
    }
    int DACOM_API UnlockFaceGroupChannels() override
    {
        return MeshLive->UnlockFaceGroupChannels();
    }
    int DACOM_API GetFaceGroupData(int Channel, void** A, void** B, void** C, void** D) override
    {
        return MeshLive->GetFaceGroupData(Channel, A, B, C, D);
    }
    int DACOM_API SetFaceGroupData(int Channel, void* A, void* B, void* C, void* D) override
    {
        return MeshLive->SetFaceGroupData(Channel, A, B, C, D);
    }
    int DACOM_API GetFaceGroupIndices(int Channel, void** A, void** B) override
    {
        return MeshLive->GetFaceGroupIndices(Channel, A, B);
    }
    int DACOM_API SetFaceGroupIndices(int Channel, void* Src, int Count) override
    {
        return MeshLive->SetFaceGroupIndices(Channel, Src, Count);
    }
    int DACOM_API GetFaceGroupIndexCount(int Selector, void** Out) override
    {
        return MeshLive->GetFaceGroupIndexCount(Selector, Out);
    }
    int DACOM_API SetFaceGroupIndexCount(int Selector, int Count) override
    {
        return MeshLive->SetFaceGroupIndexCount(Selector, Count);
    }
    int DACOM_API AddFaceGroupChannels(unsigned long Flags) override
    {
        return MeshLive->AddFaceGroupChannels(Flags);
    }
    int DACOM_API RemoveFaceGroupChannels(unsigned long Flags) override
    {
        return MeshLive->RemoveFaceGroupChannels(Flags);
    }
};

static GENRESULT TriMeshAnimQueryInterface(FTriMeshAnim* Anim, const char* Iid, void** Out)
{
    return Anim->QueryInterface(Iid, Out);
}

// The IRenderComponent tick argument is a float passed in a dword stack slot.
static float TickArgAsFloat(void* Arg)
{
    const unsigned long Bits =
        (unsigned long)((uintptr_t)Arg);
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

// FUN_06c25730 (thiscall on the archetype): operator new(0x20). A failed init returns -1
// without freeing the instance.
int FTriMeshArch::CreateInstance(void* InstId, FTriMeshAnim** Out)
{
    *Out = nullptr;
    FTriMeshAnim* Anim = new FTriMeshAnim();
    if (Anim->Init(this, InstId) < 0)
        return -1;
    *Out = Anim;
    return 0;
}

FTriMeshAnim* FRenderPlugin::FindInstance(void* InstId)
{
    if (bLastHit != 0 && LastHitKey == InstId)
    {
        HitCount += 1;
        return LastHitAnim;
    }
    std::map<void*, FTriMeshAnim*>::iterator Found = InstanceMap.find(InstId);
    if (Found == InstanceMap.end())
        return nullptr;
    LastHitKey = InstId;
    LastHitAnim = Found->second;
    bLastHit = 1;
    return LastHitAnim;
}

void FRenderPlugin::ClearLastHit(void* InstId)
{
    if (bLastHit != 0 && LastHitKey == InstId)
    {
        bLastHit = 0;
        LastHitKey = nullptr;
        LastHitAnim = nullptr;
    }
}

void FRenderPlugin::DropInstance(void* InstId)
{
    LookupCount += 1;
    FTriMeshAnim* Anim = FindInstance(InstId);
    if (Anim == nullptr)
        return;
    Anim->Reset();
    delete Anim;
    ClearLastHit(InstId);
    InstanceMap.erase(InstId);
}

void FRenderPlugin::TickInstance(void* InstId, void* Arg)
{
    LookupCount += 1;
    FTriMeshAnim* Anim = FindInstance(InstId);
    if (Anim == nullptr)
        return;
    // FUN_06c21940: the instance's ITriMeshAnim facet (+8), slot 3.
    Anim->AnimFacet.AdvanceUvAnimation(TickArgAsFloat(Arg));
}

void FRenderPlugin::TickAllInstances(void* Arg)
{
    std::map<void*, FTriMeshAnim*>::iterator Walk = InstanceMap.begin();
    for (; Walk != InstanceMap.end(); ++Walk)
    {
        FTriMeshAnim* Anim = Walk->second;
        if (Anim != nullptr)
            Anim->AnimFacet.AdvanceUvAnimation(TickArgAsFloat(Arg));
    }
}

void FRenderPlugin::DestroyInstanceMap()
{
    InstanceMap.clear();
}

FRenderPlugin::~FRenderPlugin()
{
    DestroyInstanceMap();
}

int DACOM_API FRenderPlugin::BindMeshInstance(void* InstId, void* Handle)
{
    if (Handle == (void*)((intptr_t)-1) || Handle == nullptr)
        return -1;
    LookupCount += 1;
    FTriMeshAnim* Anim = FindInstance(InstId);
    if (Anim == nullptr)
        return -1;
    if (Anim->MeshLive != Anim->Mesh)
        return -1;
    Anim->Mesh = (FTriMeshArch*)Handle;
    Anim->MeshLive = (FTriMeshArch*)Handle;
    return 0;
}

class FTriMeshPlugin : public FRenderPlugin
{
public:
    int Tristrips; // retail +0x30: FUN_06c211a0 fills it, Claim LAB_06c21450 passes it on

    FTriMeshPlugin()
        : Tristrips(1)
    {
    }

    ~FTriMeshPlugin()
    {
        std::map<void*, FTriMeshAnim*>::iterator Walk = InstanceMap.begin();
        for (; Walk != InstanceMap.end(); ++Walk)
        {
            char Line[0x2000];
            _snprintf(Line, sizeof(Line), "TriMesh: dtor: instance %d dangling\n",
                      (int)((intptr_t)Walk->first));
            RendCompFdumpAt(kFdumpNotice, G_FileTriMesh, 0x86, Line);
        }
    }

    // FUN_06c211a0, see TriMeshPluginReadProfile.
    GENRESULT Initialize(const FRenderPluginDesc* Desc)
    {
        return TriMeshPluginReadProfile(&Tristrips, Desc);
    }

    // LAB_06c214d0; the clone is FUN_06c24aa0.
    int DACOM_API CloneHandle(void* Handle) override
    {
        if (Handle == (void*)G_HandleFail || Handle == nullptr)
            return (int)G_HandleFail;
        FTriMeshArch* Clone = nullptr;
        if (((FTriMeshArch*)Handle)->Clone(&Clone) < 0 || Clone == nullptr)
            return (int)G_HandleFail;
        return (int)((intptr_t)Clone);
    }

    int DACOM_API CreateMeshInstance(void* InstId, void* Handle) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        LookupCount += 1;
        if (FindInstance(InstId) != nullptr)
            return 1;
        FTriMeshArch* Arch = (FTriMeshArch*)Handle;
        FTriMeshAnim* Anim = nullptr;
        if (Arch->CreateInstance(InstId, &Anim) < 0)
            return 0;
        InstanceMap[InstId] = Anim;
        LastHitKey = InstId;
        LastHitAnim = Anim;
        bLastHit = 1;
        float Center[3];
        Center[0] = 0.0f;
        Center[1] = 0.0f;
        Center[2] = 0.0f;
        float Radius = 0.0f;
        Arch->GetSphere(Center, &Radius);
        G_Engine->SetInstanceExtent(InstId, 1, Radius, Center);
        return 1;
    }

    int DACOM_API UpdateMeshInstance(void* InstId, void* Arg) override
    {
        TickInstance(InstId, Arg);
        return 0;
    }

    int DACOM_API UpdateMeshInstances(void* InstId) override
    {
        TickAllInstances(InstId);
        return 0;
    }

    int DACOM_API Claim(void* File) override
    {
        FTriMeshArch* Arch = new FTriMeshArch();
        if (Arch->ResetMesh() >= 0 &&
            Arch->Load((IFileSystem*)File, G_Pipeline, G_VbManager,
                       G_MaterialLibrary, Tristrips) >= 0)
            return (int)((intptr_t)Arch);
        delete Arch;
        return (int)G_HandleFail;
    }

    // FUN_06c21510: the dtor FUN_06c22550 and free -- no reference count. Retail leaves EAX
    // unset; the port returns 0.
    int DACOM_API DestroyHandle(void* Handle) override
    {
        if (Handle == (void*)G_HandleFail || Handle == nullptr)
            return 0;
        delete (FTriMeshArch*)Handle;
        return 0;
    }

    // LAB_06c215a0: mode 0 / 2 = triangle count (FUN_06c26220), 3 = vertex count (FUN_06c26260).
    int DACOM_API QueryGeometry(void* Handle, int /*Lod*/, void* A, void* B, void* /*C*/, void* Out,
                         void* /*E*/) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        const int QueryMode = (int)((intptr_t)A);
        const unsigned long Flags = (unsigned long)((uintptr_t)B);
        if (QueryGeometryCanWrite(QueryMode, Flags, Out) == 0)
            return 0;
        FTriMeshArch* Mesh = (FTriMeshArch*)Handle;
        int* CountOut = (int*)Out;
        if (QueryMode == 3)
            *CountOut = Mesh->VertexCount();
        else
            *CountOut = Mesh->TriangleCount();
        return 1;
    }

    // LAB_06c21620; the FUN_06c25790 result is not checked.
    int DACOM_API GetBounds(void* Handle, float /*Lod*/, void* Out) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        ((FTriMeshArch*)Handle)->GetBox((float*)Out);
        return 1;
    }

    // LAB_06c219a0; the FUN_06c25d40 result is not checked.
    int DACOM_API GetCentroid(void* Handle, float /*Lod*/, void* Out) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        ((FTriMeshArch*)Handle)->GetCenter((float*)Out);
        return 1;
    }

    int DACOM_API SplitByPlane(void* Handle, void* A, void* B, void* C, void* D, void* E, void* /*F*/) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        FTriMeshArch* Mesh = (FTriMeshArch*)Handle;
        float PlaneD = 0.0f;
        std::memcpy(&PlaneD, &B, sizeof(PlaneD));
        const unsigned char SplitFlags = (unsigned char)((uintptr_t)E);
        FTriMeshArch* Front = nullptr;
        FTriMeshArch* Back = nullptr;
        if (Mesh->SplitByPlane((const float*)A, PlaneD, SplitFlags, &Front, &Back) < 0)
            return 0;
        void* FrontValue = Front;
        if (FrontValue == nullptr)
            FrontValue = (void*)((intptr_t)-1);
        *(void**)C = FrontValue;
        void* BackValue = Back;
        if (BackValue == nullptr)
            BackValue = (void*)((intptr_t)-1);
        *(void**)D = BackValue;
        return 1;
    }

    int DACOM_API RenderInstance(ICamera* Camera, void* /*Handle*/, void* Inst, unsigned long Flags,
                         void* Transform, void* Extra) override
    {
        LookupCount += 1;
        FTriMeshAnim* Anim = FindInstance(Inst);
        if (Anim == nullptr)
            return 0;
        unsigned long Remapped = 0;
        if ((Flags & 0x4000) == 0)
            Remapped = 0x40;
        if ((Flags & 0x1000) != 0)
            Remapped |= 0x2;
        if ((Flags & 0x8000) != 0)
            Remapped |= 0x80;
        return Anim->Draw(Camera, Remapped, Transform, (IMaterial*)Extra);
    }
};

// ---------------------------------------------------------------------------------------
// SphereMesh (retail FDUMP path Src\RendComp\SphereMesh\SphereMeshArchetype.cpp).
// ---------------------------------------------------------------------------------------

// Sphere archetype and material hooks.
class FSphereHandle : public IMatHookUp
{
public:
    IMaterial** Materials;        // +0x04 operator new(Sides * 4), zeroed; filled by HookUpMaterials
    unsigned long Sides;          // +0x08 "Sides": the number of materials
    float Radius;                 // +0x0C "Radius"; -1.0f after a reset
    float Scale;                  // +0x10 not written by the ctor; the load seeds 1.0f and an
                                  //       AtmosphereMaterial's parameter 8 overwrites it
    char** MaterialNames;         // +0x14 operator new(Sides * 4): _strdup'd, lowercased "M%d"
                                  //       names, 0 where the name did not load
    IMaterialLibrary* Library;    // +0x18 the load's library argument, AddRef'd

    // FUN_06c2a950, then the reset FUN_06c2a9a0.
    FSphereHandle()
        : Materials(nullptr)
        , Sides(0)
        , MaterialNames(nullptr)
        , Library(nullptr)
    {
        ResetChecked();
    }

    // FUN_06c2a970: the reset, then the library Released and cleared.
    ~FSphereHandle()
    {
        Reset();
        if (Library != nullptr)
        {
            Library->Release();
            Library = nullptr;
        }
    }

    // FUN_06c2a9b0.
    int Reset()
    {
        Radius = -1.0f;
        if (MaterialNames != nullptr)
        {
            for (unsigned long Index = 0; Index < Sides; ++Index)
                std::free(MaterialNames[Index]);
            delete[] (const unsigned char*)MaterialNames;
            MaterialNames = nullptr;
        }
        for (unsigned long Index = 0; Index < Sides; ++Index)
        {
            if (Materials[Index] != nullptr)
            {
                Materials[Index]->Release();
                Materials[Index] = nullptr;
            }
        }
        delete[] (const unsigned char*)Materials;
        Materials = nullptr;
        Sides = 0;
        return 0;
    }

    // FUN_06c2a9a0: the reset's result as 0 (>= 0) or -1.
    int ResetChecked()
    {
        return (Reset() >= 0) ? 0 : -1;
    }

    int Load(IFileSystem* File, IRenderPipeline* /*Pipeline*/, IVertexBufferManager* /*VbManager*/,
             IMaterialLibrary* InLibrary)
    {
        Scale = 1.0f;
        if (File->OpenNode("Sphere") == 0)
            return -1;

        FFileSystemDesc Desc;
        unsigned long ReadCount = 0;
        Desc.Init("Radius", "FileSystem");
        bool bRead = false;
        int Handle = File->OpenByDesc(&Desc);
        if (Handle != -1)
        {
            float Value = 0.0f;
            if (File->Read((unsigned long)Handle, &Value, 4, &ReadCount, 0) != 0 &&
                ReadCount == 4)
            {
                bRead = true;
                Radius = Value;
            }
            File->CloseFile((unsigned long)Handle);
        }
        if (!bRead)
            RendCompFdumpAt(kFdumpWarning, G_FileSphereMeshArchetype, 0x5F,
                            "SphereMeshArchetype: load: no radius! invalid sphere file?");

        Desc.Init("Sides", "FileSystem");
        bRead = false;
        Handle = File->OpenByDesc(&Desc);
        if (Handle != -1)
        {
            unsigned long Value = 0;
            if (File->Read((unsigned long)Handle, &Value, 4, &ReadCount, 0) != 0 &&
                ReadCount == 4)
            {
                bRead = true;
                Sides = Value;
            }
            File->CloseFile((unsigned long)Handle);
        }
        if (!bRead)
        {
            RendCompFdumpAt(kFdumpWarning, G_FileSphereMeshArchetype, 0x63,
                            "SphereMeshArchetype: load: no sides! invalid sphere file?");
            Sides = 0;
        }

        if (Sides != 0)
        {
            Materials = (IMaterial**)new unsigned char[Sides * 4];
            std::memset(Materials, 0, Sides * 4);
            MaterialNames = (char**)new unsigned char[Sides * 4];
            std::memset(MaterialNames, 0, Sides * 4);

            char Buffer[0x80];
            for (unsigned long Index = 0; Index < Sides; ++Index)
            {
                char NodeName[0x80];
                std::sprintf(NodeName, "M%d", (int)Index);
                Desc.Init(NodeName, "FileSystem");
                bRead = false;
                Handle = File->OpenByDesc(&Desc);
                if (Handle != -1)
                {
                    unsigned long Size = File->GetSize((unsigned long)Handle, 0);
                    if (Size >= 0x3F)
                        Size = 0x3F;
                    if (File->Read((unsigned long)Handle, Buffer, Size, &ReadCount, 0) !=
                            0 &&
                        ReadCount == Size)
                    {
                        bRead = true;
                        Buffer[ReadCount] = '\0';
                    }
                    File->CloseFile((unsigned long)Handle);
                }
                if (!bRead)
                {
                    RendCompFdumpAt(
                        kFdumpWarning, G_FileSphereMeshArchetype, 0x78,
                        "SphereMeshArchetype: load: material name not found! invalid sphere file?");
                    MaterialNames[Index] = nullptr;
                }
                else
                {
                    MaterialNames[Index] = _strdup(Buffer);
                    _strlwr(MaterialNames[Index]);
                }
            }

            if (Library != InLibrary)
            {
                if (Library != nullptr)
                {
                    Library->Release();
                    Library = nullptr;
                }
                Library = InLibrary;
                if (InLibrary != nullptr)
                    InLibrary->AddRef();
            }
            HookUpMaterials();

            if (Sides > 6)
            {
                IMaterial* Candidate = Materials[6];
                if (Candidate != nullptr)
                {
                    Candidate->CopyClassName(Buffer, 0x80);
                    if (std::strcmp(Buffer, "AtmosphereMaterial") == 0)
                    {
                        unsigned long Count = 0;
                        Candidate->GetParameterCount(&Count);
                        if (Count > 8)
                        {
                            FShadingPropertyValue Value;
                            Value.Type = 0;
                            Value.Payload0 = 0;
                            Candidate->GetParameterValue(8, &Value);
                            std::memcpy(&Scale, &Value.Payload0, 4);
                            if (Value.Type == 5 || Value.Type == 7)
                            {
                                if (Value.Payload0 != 0)
                                    HeapFree(GetProcessHeap(), 0,
                                             (void*)Value.Payload0);
                            }
                            else if (Value.Type == 6)
                            {
                                IDAComponent* Component =
                                    (IDAComponent*)Value.Payload0;
                                Value.Payload0 = 0;
                                if (Component != nullptr)
                                    Component->Release();
                            }
                        }
                    }
                }
            }
        }
        File->OpenNode(".."); // DAT_06c37250
        return 0;
    }

    // FUN_06c2b020: no AddRef; *Out is left untouched on a miss.
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        if (RendCompIidEqual(Iid, Iids::MatHookUp))
        {
            *Out = (IMatHookUp*)this;
            return GR_OK;
        }
        return (GENRESULT)-1;
    }

    // FUN_06c2b100: returns 1.
    unsigned long DACOM_API AddRef() override
    {
        return 1;
    }

    unsigned long DACOM_API Release() override
    {
        return 1;
    }

    int DACOM_API HookUpMaterials() override
    {
        struct FMaterialQuery
        {
            const char* Name;
            unsigned long Crc;
        };
        if (Library == nullptr)
            return -1;
        unsigned char bFailed = 0;
        for (unsigned long Index = 0; Index < Sides; ++Index)
        {
            IMaterial* Previous = Materials[Index];
            FMaterialQuery Query;
            Query.Name = MaterialNames[Index];
            Query.Crc = DACOM_CRC::GetCRC32(Query.Name);
            if (Library->FindMaterialByCrc(&Query, &Materials[Index]) != 0)
            {
                Materials[Index] = nullptr;
                bFailed = 1;
            }
            if (Previous != nullptr)
                Previous->Release();
        }
        if (bFailed != 0)
            return -1;
        return 0;
    }
};

// DAT_06c37ccc: a byte the sphere draw reads to skip the atmosphere pass. Nothing in
// rendcomp.dll writes it (one XREF, the read) and it is not exported, so it stays 0.
static unsigned char G_SphereNoAtmosphere = 0;

static void SphereFaceRotation(FTransform& Out, int Face, float HalfPi)
{
    double Cos = 1.0;
    double Sin = 0.0;
    bool bAboutX = false;
    switch (Face)
    {
    case 1:
        Cos = std::cos((double)HalfPi);
        Sin = std::sin((double)HalfPi);
        break;
    case 2:
        Cos = std::cos(3.141592653589793);
        Sin = std::sin((double)((float)3.141592653589793));
        break;
    case 3:
        Cos = std::cos((double)-HalfPi);
        Sin = std::sin((double)-HalfPi);
        break;
    case 4:
        Cos = std::cos((double)-HalfPi);
        Sin = std::sin((double)-HalfPi);
        bAboutX = true;
        break;
    case 5:
        Cos = std::cos((double)HalfPi);
        Sin = std::sin((double)HalfPi);
        bAboutX = true;
        break;
    default:
        Out.Rotation.D[0][0] = 1.0f;
        Out.Rotation.D[0][1] = 0.0f;
        Out.Rotation.D[0][2] = 0.0f;
        Out.Rotation.D[1][0] = 0.0f;
        Out.Rotation.D[1][1] = 1.0f;
        Out.Rotation.D[1][2] = 0.0f;
        Out.Rotation.D[2][0] = 0.0f;
        Out.Rotation.D[2][1] = 0.0f;
        Out.Rotation.D[2][2] = 1.0f;
        Out.Translation.X = 0.0f;
        Out.Translation.Y = 0.0f;
        Out.Translation.Z = 0.0f;
        return;
    }
    const float C = (float)Cos;
    const float S = (float)Sin;
    if (!bAboutX)
    {
        // [c 0 s; 0 1 0; -s 0 c], translation untouched (it stays 0).
        Out.Rotation.D[0][0] = C;
        Out.Rotation.D[0][1] = 0.0f;
        Out.Rotation.D[0][2] = S;
        Out.Rotation.D[1][0] = 0.0f;
        Out.Rotation.D[1][1] = 1.0f;
        Out.Rotation.D[1][2] = 0.0f;
        Out.Rotation.D[2][0] = -S;
        Out.Rotation.D[2][1] = 0.0f;
        Out.Rotation.D[2][2] = C;
        return;
    }
    // [1 0 0; 0 c -s; 0 s c], translation zeroed.
    Out.Translation.X = 0.0f;
    Out.Translation.Y = 0.0f;
    Out.Translation.Z = 0.0f;
    Out.Rotation.D[0][0] = 1.0f;
    Out.Rotation.D[0][1] = 0.0f;
    Out.Rotation.D[0][2] = 0.0f;
    Out.Rotation.D[1][0] = 0.0f;
    Out.Rotation.D[1][1] = C;
    Out.Rotation.D[1][2] = -S;
    Out.Rotation.D[2][0] = 0.0f;
    Out.Rotation.D[2][1] = S;
    Out.Rotation.D[2][2] = C;
}

// A material slot; the first slot also owns the shared sphere geometry.
struct FSphereMaterialSlot
{
    IMaterial* Material;        // +0x00
    unsigned long BaseVertex;   // +0x04 FUN_06c2c690 argument 4; IMaterial::Submit Start
    unsigned long VertexCount;  // +0x08 vertices written by FUN_06c2c690; Submit Count
    unsigned short* Indices;    // +0x0C realloc'd by FUN_06c2c690, freed by FUN_06c2c660;
                                //       the submit context's CPU indices (+0x10)
    unsigned long IndexCount;   // +0x10 indices written by FUN_06c2c690; Submit IndexSpan

    FSphereMaterialSlot()
        : Material(nullptr)
        , BaseVertex(0)
        , VertexCount(0)
        , Indices(nullptr)
        , IndexCount(0)
    {
    }

    // FUN_06c2c660.
    void ReleaseContents()
    {
        if (Material != nullptr)
        {
            Material->Release();
            Material = nullptr;
        }
        std::free(Indices);
        Indices = nullptr;
    }

    // One index store of FUN_06c2c690: the value (ftol of a float index) as a word at
    // Indices[IndexCount], then IndexCount + 1.
    void PushIndex(int Value)
    {
        Indices[IndexCount] = (unsigned short)Value;
        IndexCount += 1;
    }

    // One vertex store of FUN_06c2c690 at BaseVertex + VertexCount: position = normal * Radius
    // (each product rounded to float), the unit normal, texcoord (S, T) rounded to float.
    // Streams: Desc+4 position (12 bytes), Desc+0x18 normal (12), Desc+0x40 texcoord 0 (8).
    void WriteVertex(FRpVertexSrcDesc* Desc, float Nx, float Ny, float Nz, float Radius, double S,
                     double T)
    {
        const unsigned long Vertex = VertexCount + BaseVertex;
        float* Position = (float*)(
            (unsigned char*)(Desc->Position.Base) + Vertex * 0xC);
        Position[0] = Nx * Radius;
        Position[1] = Ny * Radius;
        Position[2] = Nz * Radius;
        float* Normal =
            (float*)((unsigned char*)(Desc->Normal.Base) + Vertex * 0xC);
        Normal[0] = Nx;
        Normal[1] = Ny;
        Normal[2] = Nz;
        float* Tex = (float*)((unsigned char*)(Desc->Tex[0].Base));
        Tex[Vertex * 2] = (float)S;
        Tex[Vertex * 2 + 1] = (float)T;
    }

    void Tessellate(float Radius, int Face, int Level, unsigned long InBaseVertex,
                    unsigned long /*Count*/, FRpVertexSrcDesc* Desc)
    {
        const float K = (float)(1.0 / std::sqrt(3.0));
        BaseVertex = InBaseVertex;
        VertexCount = 0;
        IndexCount = 0;

        float Corner[4][3];
        for (int Row = 0; Row < 4; ++Row)
        {
            Corner[Row][0] = K;
            Corner[Row][1] = K;
            Corner[Row][2] = K;
        }
        switch (Face)
        {
        case 0:
            Corner[0][0] *= -1.0f;
            Corner[0][1] *= -1.0f;
            Corner[1][1] *= -1.0f;
            Corner[2][0] *= -1.0f;
            break;
        case 1:
            Corner[0][1] *= -1.0f;
            Corner[1][1] *= -1.0f;
            Corner[1][2] *= -1.0f;
            Corner[3][2] *= -1.0f;
            break;
        case 2:
            Corner[0][1] *= -1.0f;
            Corner[0][2] *= -1.0f;
            Corner[1][0] *= -1.0f;
            Corner[1][1] *= -1.0f;
            Corner[1][2] *= -1.0f;
            Corner[2][2] *= -1.0f;
            Corner[3][0] *= -1.0f;
            Corner[3][2] *= -1.0f;
            break;
        case 3:
            Corner[0][0] *= -1.0f;
            Corner[0][1] *= -1.0f;
            Corner[0][2] *= -1.0f;
            Corner[1][0] *= -1.0f;
            Corner[1][1] *= -1.0f;
            Corner[2][0] *= -1.0f;
            Corner[2][2] *= -1.0f;
            Corner[3][0] *= -1.0f;
            break;
        case 4:
            Corner[0][0] *= -1.0f;
            Corner[2][2] *= -1.0f;
            Corner[2][0] *= -1.0f;
            Corner[3][2] *= -1.0f;
            break;
        case 5:
            Corner[0][0] *= -1.0f;
            Corner[0][1] *= -1.0f;
            Corner[0][2] *= -1.0f;
            Corner[1][1] *= -1.0f;
            Corner[1][2] *= -1.0f;
            Corner[2][0] *= -1.0f;
            Corner[2][1] *= -1.0f;
            Corner[3][1] *= -1.0f;
            break;
        default:
            break;
        }

        Indices = (unsigned short*)(
            std::realloc(Indices, (size_t)((1u << Level) * 0xCu)));
        const int Half = (int)((float)Level * 0.5f);
        const int N = 1 << Half;
        const float Step = (float)(1.0 / (double)N);
        const int Width = N + 1;
        double T = 0.0;

        if ((Level % 2) != 0)
        {
            const float HalfStep = (float)((double)Step * 0.5);
            const int Rows = Width + Width - 1;
            for (int Row = 0; Row < Rows; ++Row)
            {
                const double OneMinusT = 1.0 - T;
                const float C2x = (float)(Corner[2][0] * T);
                const float C2y = (float)(Corner[2][1] * T);
                const float C2z = (float)(Corner[2][2] * T);
                const float C0y = (float)(Corner[0][1] * OneMinusT);
                const float C0z = (float)(Corner[0][2] * OneMinusT);
                const double Ax = Corner[0][0] * OneMinusT + C2x;
                const float Ay = (float)((double)C0y + C2y);
                const float Az = (float)((double)C0z + C2z);
                const double C3x = T * Corner[3][0];
                const double C3y = T * Corner[3][1];
                const float C3z = (float)(T * Corner[3][2]);
                const double C1x = Corner[1][0] * OneMinusT;
                const float C1y = (float)(OneMinusT * Corner[1][1]);
                const float C1z = (float)(OneMinusT * Corner[1][2]);
                float Bx = (float)(C1x + C3x);
                float By = (float)(C1y + C3y);
                float Bz = (float)((double)C1z + C3z);

                const float InvA = (float)(
                    1.0 / std::sqrt(Ax * Ax + (double)Az * Az +
                                    (double)Ay * Ay));
                const float Anx = (float)(Ax * InvA);
                const float Any = (float)((double)Ay * InvA);
                const float Anz = (float)((double)Az * InvA);
                const double InvB = 1.0 / std::sqrt((double)Bz * Bz +
                                                    (double)By * By +
                                                    (double)Bx * Bx);
                Bx = (float)(Bx * InvB);
                By = (float)(By * InvB);
                Bz = (float)(Bz * InvB);

                double S = 0.0;
                int Columns = Width;
                if ((Row % 2) != 0)
                {
                    S = HalfStep;
                    Columns = Width - 1;
                }
                for (int Column = 0; Column < Columns; ++Column)
                {
                    const double BsX = Bx * S;
                    const double BsY = By * S;
                    const float BsZ = (float)(Bz * S);
                    const double OneMinusS = 1.0 - S;
                    const float AsY = (float)(Any * OneMinusS);
                    const float AsZ = (float)(Anz * OneMinusS);
                    const float Px = (float)(Anx * OneMinusS + BsX);
                    const double Py = AsY + BsY;
                    const double Pz = (double)AsZ + BsZ;
                    const double Inv =
                        1.0 / std::sqrt(Pz * Pz + Py * Py + (double)Px * Px);
                    WriteVertex(Desc, (float)(Px * Inv), (float)(Py * Inv),
                                (float)(Pz * Inv), Radius, S, T);
                    S += Step;
                    if (S > 1.0)
                        S = 1.0;
                    VertexCount += 1;
                }
                T += HalfStep;
                if (T > 1.0)
                    T = 1.0;
            }

            if (N <= 0)
                return;
            // Per cell of the even rows, four triangles round the odd-row centre M:
            // (A0, A1, M), (A1, C+1, M), (C+1, C, M), (C, A0, M).
            int Base = 0;
            for (int Outer = N; Outer != 0; --Outer)
            {
                for (int Column = 0; Column < N; ++Column)
                {
                    const int A0 = Base + Column;
                    const int A1 = A0 + 1;
                    const int M = Base + Column + Width;
                    const int C = Base + Column + Rows;
                    PushIndex(A0);
                    PushIndex(A1);
                    PushIndex(M);
                    PushIndex(A1);
                    PushIndex(C + 1);
                    PushIndex(M);
                    PushIndex(C + 1);
                    PushIndex(C);
                    PushIndex(M);
                    PushIndex(C);
                    PushIndex(A0);
                    PushIndex(M);
                }
                Base += Rows;
            }
            return;
        }

        for (int Row = 0; Row < Width; ++Row)
        {
            const double OneMinusT = 1.0 - T;
            const float C2x = (float)(Corner[2][0] * T);
            const float C2y = (float)(Corner[2][1] * T);
            const float C2z = (float)(Corner[2][2] * T);
            const float C0y = (float)(Corner[0][1] * OneMinusT);
            const float C0z = (float)(Corner[0][2] * OneMinusT);
            const double Ax = Corner[0][0] * OneMinusT + C2x;
            const float Ay = (float)((double)C0y + C2y);
            const float Az = (float)((double)C0z + C2z);
            const double C3x = T * Corner[3][0];
            const double C3y = T * Corner[3][1];
            const float C3z = (float)(T * Corner[3][2]);
            const double C1x = Corner[1][0] * OneMinusT;
            const float C1y = (float)(OneMinusT * Corner[1][1]);
            const float C1z = (float)(OneMinusT * Corner[1][2]);
            float Bx = (float)(C1x + C3x);
            float By = (float)(C1y + C3y);
            float Bz = (float)((double)C1z + C3z);

            // The even branch sums the squares z, y, x and keeps the normalised A.x on the
            // x87 stack.
            const float InvA = (float)(
                1.0 / std::sqrt((double)Az * Az + (double)Ay * Ay +
                                Ax * Ax));
            const double Anx = Ax * InvA;
            const float Any = (float)((double)Ay * InvA);
            const float Anz = (float)((double)Az * InvA);
            const double InvB = 1.0 / std::sqrt((double)Bz * Bz +
                                                (double)By * By +
                                                (double)Bx * Bx);
            Bx = (float)(Bx * InvB);
            By = (float)(By * InvB);
            Bz = (float)(Bz * InvB);

            double S = 0.0;
            for (int Column = 0; Column < Width; ++Column)
            {
                const double BsX = Bx * S;
                const double BsY = By * S;
                const float BsZ = (float)(Bz * S);
                const double OneMinusS = 1.0 - S;
                const float AsY = (float)(Any * OneMinusS);
                const float AsZ = (float)(Anz * OneMinusS);
                const float Px = (float)(Anx * OneMinusS + BsX);
                const double Py = AsY + BsY;
                const double Pz = (double)AsZ + BsZ;
                const double Inv =
                    1.0 / std::sqrt(Pz * Pz + Py * Py + (double)Px * Px);
                WriteVertex(Desc, (float)(Px * Inv), (float)(Py * Inv),
                            (float)(Pz * Inv), Radius, S, T);
                S += Step;
                if (S > 1.0)
                    S = 1.0;
                VertexCount += 1;
            }
            T += Step;
            if (T > 1.0)
                T = 1.0;
        }

        if (N <= 0)
            return;
        // Per cell P (row, column) with Q = P + Width below it, two triangles:
        // odd (row + column) -> (P, P+1, Q), (P+1, Q+1, Q); even -> (P, Q+1, Q), (P, P+1, Q+1).
        int Base = 0;
        for (int Row = 0; Row < N; ++Row)
        {
            for (int Column = 0; Column < N; ++Column)
            {
                const int P = Base + Column;
                const int Q = Width + Column + Base;
                if (((Row + Column) % 2) != 0)
                {
                    PushIndex(P);
                    PushIndex(P + 1);
                    PushIndex(Q);
                    PushIndex(P + 1);
                    PushIndex(Q + 1);
                    PushIndex(Q);
                }
                else
                {
                    PushIndex(P);
                    PushIndex(Q + 1);
                    PushIndex(Q);
                    PushIndex(P);
                    PushIndex(P + 1);
                    PushIndex(Q + 1);
                }
            }
            Base += Width;
        }
    }
};

// Sphere instance with a material-hook facet.
class FSphereAnim : public ISphereMesh
{
public:
    class FHook : public IMatHookUp
    {
    public:
        FSphereAnim* Owner;

        explicit FHook(FSphereAnim* InOwner)
            : Owner(InOwner)
        {
        }

        // LAB_06c2c600 (this - 4, then FUN_06c2c230).
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            return Owner->QueryInterface(Iid, Out);
        }

        // LAB_06c27670 -> FUN_06c2b100: returns 1.
        unsigned long DACOM_API AddRef() override
        {
            return 1;
        }

        unsigned long DACOM_API Release() override
        {
            return 1;
        }

        // FUN_06c2b350.
        int DACOM_API HookUpMaterials() override
        {
            return Owner->HookUpMaterials();
        }
    };

    FHook Hook;                   // +0x04
    unsigned long Flags;          // +0x08 bit 1: rebuild requested (SetMaxTessellationLevel /
                                  //       SetRadius / RequestRebuild); FUN_06c2c430 clears it
                                  //       and sets bit 0 after a rebuild
    float Radius;                 // +0x0C archetype radius (Set/GetRadius); the tessellation
                                  //       radius and the level formula's scale
    FSphereHandle* Arch;          // +0x10
    void* InstId;                 // +0x14
    float LodDistanceScale;             // +0x18 1.5f (Set/GetLodDistanceScale). The draw multiplies
                                  //       the camera distance by it before the level update; the
                                  //       exe's level_of_detail option writes it
    int LevelMin;                 // +0x1C 3, lower clamp of FUN_06c2c430
    int LevelMax;                 // +0x20 9, upper clamp of FUN_06c2c430 (Set/GetMaxTessellationLevel)
    int Field_0x24;               // +0x24 0. TODO: meaning (FUN_06c2b230 writes 0; no other
                                  //       access in the sphere code)
    int CachedLevel;              // +0x28 -1, level of the current tessellation (FUN_06c2c430)
    std::list<FSphereMaterialSlot*> MaterialSlots; // +0x2C..+0x34 (VC6 list: allocator, head, size)
    std::list<void*> List_0x38;   // +0x38..+0x40: built by the ctor and freed by the dtor; no
                                  //       other access in the sphere code
    FRpVertexSrcDesc Desc;        // +0x44..+0xB3: 0x1C dwords zeroed by FUN_06c2b110
    unsigned long VertexCount;    // +0xB4 vertices the streams are sized for (FUN_06c2c430)
    unsigned char bHasAtmosphere; // +0xB8 more than 6 archetype materials

    // FUN_06c2b110, then FUN_06c2b230(0, -1).
    FSphereAnim()
        : Hook(this)
        , Flags(0)
        , Radius(1.0f)
        , Arch(nullptr)
        , InstId((void*)((intptr_t)-1))
        , LodDistanceScale(1.5f)
        , LevelMin(3)
        , LevelMax(9)
        , Field_0x24(0)
        , CachedLevel(-1)
        , VertexCount(0)
        , bHasAtmosphere(0)
    {
        std::memset(&Desc, 0, sizeof(Desc));
        Init(nullptr, (void*)((intptr_t)-1));
    }

    // FUN_06c2b190: FUN_06c2b3c0, then both lists are emptied and their heads freed.
    ~FSphereAnim()
    {
        Reset();
    }

    // FUN_06c2b3c0.
    int Reset()
    {
        Flags = 0;
        Arch = nullptr;
        InstId = (void*)((intptr_t)-1);
        for (std::list<FSphereMaterialSlot*>::iterator Walk = MaterialSlots.begin();
             Walk != MaterialSlots.end(); ++Walk)
        {
            FSphereMaterialSlot* Slot = *Walk;
            if (Slot != nullptr)
            {
                Slot->ReleaseContents();
                delete Slot;
            }
        }
        MaterialSlots.clear();
        VertexCount = 0;
        if (Desc.Position.Base != nullptr)
        {
            std::free((unsigned char*)Desc.Position.Base);
            Desc.Position.Base = nullptr;
        }
        if (Desc.Normal.Base != nullptr)
        {
            std::free((unsigned char*)Desc.Normal.Base);
            Desc.Normal.Base = nullptr;
        }
        if (Desc.Tex[0].Base != nullptr)
        {
            std::free((unsigned char*)(Desc.Tex[0].Base));
            Desc.Tex[0].Base = nullptr;
        }
        return 0;
    }

    int Init(FSphereHandle* InArch, void* InInstId)
    {
        Reset();
        Arch = InArch;
        InstId = InInstId;
        bHasAtmosphere = 0;
        Flags = 0;
        LodDistanceScale = 1.5f;
        CachedLevel = -1;
        Field_0x24 = 0;
        LevelMin = 3;
        LevelMax = 9;
        VertexCount = 0;
        std::memset(&Desc, 0, sizeof(Desc));
        Desc.SrcFvf = 0x112;
        Desc.Flags = 0;
        Radius = 1.0f;
        if (Arch == nullptr)
            return 0;
        Radius = Arch->Radius;
        unsigned long Index = 0;
        if (Arch->Sides == 0)
            return 0;
        for (; Index < Arch->Sides; ++Index)
        {
            FSphereMaterialSlot* Slot = new FSphereMaterialSlot();
            IMaterial* Material = Arch->Materials[Index];
            Slot->Material = Material;
            if (Material != nullptr)
                Material->AddRef();
            MaterialSlots.push_back(Slot);
        }
        if (Index > 6)
            bHasAtmosphere = 1;
        return 0;
    }

    int HookUpMaterials()
    {
        if (Arch == nullptr)
            return -1;
        Arch->HookUpMaterials();
        std::list<FSphereMaterialSlot*>::iterator Walk = MaterialSlots.begin();
        for (unsigned long Index = 0; Index < Arch->Sides; ++Index)
        {
            FSphereMaterialSlot* Slot = *Walk;
            IMaterial* Previous = Slot->Material;
            Slot->Material = Arch->Materials[Index];
            if (Slot->Material != nullptr)
                Slot->Material->AddRef();
            if (Previous != nullptr)
                Previous->Release();
            ++Walk;
        }
        return 0;
    }

    void UpdateLevel(float Distance)
    {
        const double HalfPi = 3.141592653589793 * 0.5;
        const double Ratio =
            HalfPi / std::acos(1.0 - (double)Distance / (double)Radius);
        const double Log2 = std::log10(Ratio) / std::log10(2.0);
        int Level = (int)(Log2 + Log2 + 0.5);
        if (Level < LevelMin)
            Level = LevelMin;
        if (LevelMax < Level)
            Level = LevelMax;
        if (CachedLevel == Level && (Flags & 2) == 0)
            return;

        CachedLevel = Level;
        const int Half = (int)((float)Level * 0.5f);
        const unsigned long N = 1u << Half;
        unsigned long Count = (N + 1) * (N + 1);
        if ((Level % 2) != 0)
            Count += N * N;
        VertexCount = Count;
        Desc.Position.Base = (unsigned char*)(
            std::realloc((unsigned char*)Desc.Position.Base, VertexCount * 0xC));
        Desc.Normal.Base = (unsigned char*)(
            std::realloc((unsigned char*)Desc.Normal.Base, VertexCount * 0xC));
        Desc.Tex[0].Base = (unsigned char*)(
            std::realloc((unsigned char*)(Desc.Tex[0].Base), VertexCount * 0xC));
        Desc.SrcCount = 0;
        FSphereMaterialSlot* Geometry = *MaterialSlots.begin();
        Geometry->Tessellate(Radius, 0, Level, 0, VertexCount, &Desc);
        Desc.SrcCount = Geometry->VertexCount;
        Desc.Position.Count = Desc.SrcCount;
        Desc.Position.Stride = 0xC;
        Desc.Position.ByteSize = 0xC;
        Desc.Normal.Stride = 0xC;
        Desc.Normal.ByteSize = 0xC;
        Desc.Tex[0].Stride = 8;
        Desc.Tex[0].ByteSize = 8;
        Flags = (Flags & ~2ul) | 1ul;
        Desc.Normal.Count = Desc.SrcCount;
        Desc.Tex[0].Count = Desc.SrcCount;
    }

    bool Draw(ICamera* Camera, const FTransform* Matrix, IMaterial* Override)
    {
        if (Arch == nullptr || InstId == (void*)((intptr_t)-1))
            return false;
        const FTransform* View = (const FTransform*)(Camera->GetViewTransform());
        const FTransform* World =
            (const FTransform*)(G_Engine->GetInstanceTransform(InstId));

        FTransform Modelview;
        if (Matrix != nullptr)
        {
            FTransform Product;
            MathEngineCached(G_MathSphere)->MulTransform(Product, *Matrix, *World);
            const FTransform Local = Product; // FUN_06c2c610
            MathEngineCached(G_MathSphere)->MulTransform(Modelview, *View, Local);
        }
        else
            MathEngineCached(G_MathSphere)->MulTransform(Modelview, *View, *World);
        FTransform Oriented = Modelview;  // FUN_06c2c610
        FTransform Pipelined = Oriented;
        const float ScaledRadius = Arch->Scale * Arch->Radius;
        const float ArchRadius = Arch->Radius;

        // The corner indices of each cube face.
        const int FaceCorners[6][4] = {
            {0, 3, 4, 7}, {0, 1, 4, 5}, {1, 2, 5, 6}, {2, 3, 6, 7}, {0, 1, 2, 3}, {4, 5, 6, 7}};
        // The face normals.
        FVector Normals[6] = {{0.0f, 0.0f, 1.0f},  {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
                              {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
        // The cube corners, +-k with k = 0x3f13cd3a.
        const float K = 0.57735026f;
        FVector Corners[8] = {{K, K, K},   {K, K, -K},   {-K, K, -K},  {-K, K, K},
                              {K, -K, K},  {K, -K, -K},  {-K, -K, -K}, {-K, -K, K}};
        const FMatrix WorldRotation = World->Rotation;
        unsigned char bVisible[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int Face = 0; Face < 6; ++Face)
        {
            FVector Turned;
            MathEngineCached(G_MathSphere)->TransformMatrix(Turned, WorldRotation, Normals[Face]);
            Normals[Face] = Turned;
        }

        const float* CameraPos = Camera->GetPosition();
        FVector ToCamera; // FUN_06c25cc0
        ToCamera.X = CameraPos[0] - World->Translation.X;
        ToCamera.Y = CameraPos[1] - World->Translation.Y;
        ToCamera.Z = CameraPos[2] - World->Translation.Z;
        float Best = -1.0f;
        int BestFace = -1;
        const float Distance = (float)(std::sqrt(
            (double)ToCamera.X * ToCamera.X + (double)ToCamera.Z * ToCamera.Z +
            (double)ToCamera.Y * ToCamera.Y));
        const float InvDistance = 1.0f / Distance;
        ToCamera.X *= InvDistance;
        ToCamera.Y *= InvDistance;
        ToCamera.Z *= InvDistance;
        for (int Face = 0; Face < 6; ++Face)
        {
            const float Dot = ToCamera.X * Normals[Face].X + ToCamera.Z * Normals[Face].Z +
                              ToCamera.Y * Normals[Face].Y;
            if (Dot > Best)
            {
                Best = Dot;
                BestFace = Face;
            }
        }
        const double Limit = (double)InvDistance * ArchRadius;
        float Threshold = (float)Limit;
        if (Limit > 0.57735026918962573)
            Threshold = K;
        const int* BestCorners = FaceCorners[BestFace];
        for (int Index = 0; Index < 4; ++Index)
        {
            const int Corner = BestCorners[Index];
            FVector Turned;
            MathEngineCached(G_MathSphere)->TransformMatrix(Turned, WorldRotation, Corners[Corner]);
            Corners[Corner] = Turned;
            const float Dot = ToCamera.Z * Corners[Corner].Z + ToCamera.Y * Corners[Corner].Y +
                              ToCamera.X * Corners[Corner].X;
            bVisible[Corner] = (Dot > Threshold) ? 1 : 0;
        }

        // The modelview origin into a local nothing reads afterwards (as retail).
        FVector Origin;
        Origin.X = 0.0f;
        Origin.Y = 0.0f;
        Origin.Z = 0.0f;
        FVector ViewCentre;
        MathEngineCached(G_MathSphere)->TransformXform(ViewCentre, Pipelined, Origin);

        const float Biased = Distance * LodDistanceScale;
        const float XScale = (float)(Camera->GetXScaleOverNear()); // rounded to float
        UpdateLevel(Biased / (Camera->GetZNear() * XScale));

        IVertexBufferManager* VbManager = G_VbManager;
        FVertexMemoryLock Lock;
        if (VbManager->LockVertexMemory(Desc.SrcFvf, Desc.SrcCount, &Lock) >= 0)
        {
            VbManager->CopyVertexData(Lock.Vertices, Lock.Listing.Fvf, &Desc, 0, Desc.SrcCount);
            VbManager->UnlockVertexMemory(&Lock);
        }
        else
            Lock.Listing.VertexBuffer = nullptr;

        const float HalfPi = (float)(3.141592653589793 * 0.5);
        // Context +0x18 is never written by retail (a stack leftover); the port passes 0.
        FMaterialSubmitContext Context;
        Context.Inst = InstId;
        Context.World = nullptr;
        Context.View = (FTransform*)View;
        Context.SourceVertices = &Desc;
        Context.Indices = nullptr;
        Context.VbListing = &Lock.Listing;
        Context.IbScratch = nullptr;
        Context.ReplacedMaterial = nullptr;
        Context.MaterialContext = nullptr;

        FTransform Rotation;
        SphereFaceRotation(Rotation, 0, HalfPi);
        FTransform FaceWorld;
        const std::list<FSphereMaterialSlot*>::iterator First = MaterialSlots.begin();
        std::list<FSphereMaterialSlot*>::iterator Walk = First;
        for (int Face = 0; Face < 6; ++Face)
        {
            FSphereMaterialSlot* Geometry = *First;
            IMaterial* Material = (*Walk)->Material;
            Context.ReplacedMaterial = nullptr;
            if (Override != nullptr)
            {
                Context.ReplacedMaterial = Material;
                Material = Override;
            }
            Context.Indices = Geometry->Indices;
            unsigned char bDraw = 1;
            if (Face != BestFace)
            {
                bDraw = 0;
                for (int Index = 0; Index < 4; ++Index)
                {
                    if (bVisible[FaceCorners[Face][Index]] != 0)
                        bDraw = 1;
                }
            }
            if (bDraw != 0)
            {
                if (Face != 0)
                    SphereFaceRotation(Rotation, Face, HalfPi);
                MathEngineCached(G_MathSphere)->MulTransform(Oriented, *World, Rotation);
                FaceWorld = Oriented;
                int State = 1;
                G_Pipeline->GetPipelineState(7, &State);
                if (State != 0)
                {
                    FTransform Product;
                    MathEngineCached(G_MathSphere)->MulTransform(Product, *View, FaceWorld);
                    Modelview = Product;
                    Pipelined = Modelview;
                    G_Pipeline->SetModelview((const float*)(&Pipelined));
                }
                else
                    G_Pipeline->SetWorld((const float*)(&FaceWorld));
                Context.World = &FaceWorld;
                if (Material != nullptr)
                {
                    Material->Bind(&Context);
                    Material->Submit(&Context, 4, Geometry->BaseVertex, Geometry->VertexCount, 0,
                                     Geometry->IndexCount, 0);
                    Material->Teardown(&Context);
                }
            }
            if (Walk == MaterialSlots.end())
                Walk = First;
            else
            {
                ++Walk;
                if (Walk == MaterialSlots.end())
                    Walk = First;
            }
        }

        if (bHasAtmosphere == 0 || Override != nullptr || G_SphereNoAtmosphere != 0)
            return true;

        IMaterial* Atmosphere = (*Walk)->Material;
        Context.ReplacedMaterial = nullptr;
        const double AtmosphereLimit = (double)InvDistance * ScaledRadius;
        double AtmosphereThreshold = AtmosphereLimit;
        if (AtmosphereLimit > 0.57735026918962573)
            AtmosphereThreshold = K;
        for (int Index = 0; Index < 4; ++Index)
        {
            const int Corner = BestCorners[Index];
            const double Dot = (double)ToCamera.Z * Corners[Corner].Z +
                               (double)ToCamera.Y * Corners[Corner].Y +
                               (double)ToCamera.X * Corners[Corner].X;
            bVisible[Corner] = (Dot > AtmosphereThreshold) ? 1 : 0;
        }
        Context.MaterialContext = G_Engine; // DAT_06c37d4c
        FTransform AtmosphereWorld;
        for (int Face = 0; Face < 6; ++Face)
        {
            FSphereMaterialSlot* Geometry = *First;
            if (Face != BestFace)
            {
                unsigned char bAny = 0;
                for (int Index = 0; Index < 4; ++Index)
                {
                    if (bVisible[FaceCorners[Face][Index]] != 0)
                        bAny = 1;
                }
                if (bAny == 0)
                    continue;
            }
            SphereFaceRotation(Rotation, Face, HalfPi);
            MathEngineCached(G_MathSphere)->MulTransform(Oriented, *World, Rotation);
            AtmosphereWorld = Oriented;
            Context.World = &AtmosphereWorld;
            if (Atmosphere != nullptr)
            {
                Atmosphere->Bind(&Context);
                Atmosphere->Submit(&Context, 4, Geometry->BaseVertex, Geometry->VertexCount, 0,
                                   Geometry->IndexCount, 0);
                Atmosphere->Teardown(&Context);
            }
        }
        return true;
    }

    // FUN_06c2c230: no AddRef; *Out is left untouched on a miss.
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        if (RendCompIidEqual(Iid, Iids::SphereMesh))
        {
            *Out = (ISphereMesh*)this;
            return GR_OK;
        }
        if (RendCompIidEqual(Iid, Iids::MatHookUp))
        {
            *Out = (IMatHookUp*)(&Hook);
            return GR_OK;
        }
        return (GENRESULT)-1;
    }

    // FUN_06c2b100: returns 1 -- the plugin's instance map owns the object.
    unsigned long DACOM_API AddRef() override
    {
        return 1;
    }

    unsigned long DACOM_API Release() override
    {
        return 1;
    }

    // LAB_06c2c2a0.
    int DACOM_API SetLodDistanceScale(float Value) override
    {
        LodDistanceScale = Value;
        return 0;
    }

    // LAB_06c2c2b0.
    int DACOM_API GetLodDistanceScale(float* Out) override
    {
        *Out = LodDistanceScale;
        return 0;
    }

    // LAB_06c2c2d0.
    int DACOM_API NoOp05(void* /*Arg*/) override
    {
        return 0;
    }

    // LAB_06c2c2d0 again (06c36360).
    int DACOM_API NoOp06(void* /*Arg*/) override
    {
        return 0;
    }

    // LAB_06c2c2e0.
    int DACOM_API SetMaxTessellationLevel(int Value) override
    {
        LevelMax = Value;
        Flags |= 2;
        return 0;
    }

    // LAB_06c2c300.
    int DACOM_API GetMaxTessellationLevel(int* Out) override
    {
        *Out = LevelMax;
        return 0;
    }

    // LAB_06c2c320.
    int DACOM_API SetRadius(float Value) override
    {
        Radius = Value;
        Flags |= 2;
        return 0;
    }

    // LAB_06c2c340.
    int DACOM_API GetRadius(float* Out) override
    {
        *Out = Radius;
        return 0;
    }

    // LAB_06c2c360: returns -1; the argument is not read.
    int DACOM_API RequestRebuild(void* /*Arg*/) override
    {
        Flags |= 2;
        return -1;
    }

    // LAB_06c2c370.
    int DACOM_API GetMaterialCount(int* OutCount) override
    {
        *OutCount = (int)(MaterialSlots.size());
        return 0;
    }

    // LAB_06c2c390.
    int DACOM_API SetMaterial(int Index, IMaterial* Material) override
    {
        int Position = 0;
        for (std::list<FSphereMaterialSlot*>::iterator Walk = MaterialSlots.begin();
             Walk != MaterialSlots.end(); ++Walk)
        {
            if (Position == Index)
            {
                FSphereMaterialSlot* Slot = *Walk;
                IMaterial* Previous = Slot->Material;
                Slot->Material = Material;
                if (Material != nullptr)
                    Material->AddRef();
                if (Previous != nullptr)
                    Previous->Release();
                return 0;
            }
            Position += 1;
        }
        return -1;
    }

    // LAB_06c2c3e0.
    int DACOM_API GetMaterial(int Index, IMaterial** Out) override
    {
        *Out = nullptr;
        int Position = 0;
        for (std::list<FSphereMaterialSlot*>::iterator Walk = MaterialSlots.begin();
             Walk != MaterialSlots.end(); ++Walk)
        {
            if (Position == Index)
            {
                IMaterial* Material = (*Walk)->Material;
                *Out = Material;
                if (Material != nullptr)
                    Material->AddRef();
                return 0;
            }
            Position += 1;
        }
        return -1;
    }
};

class FSphereMeshPlugin : public FRenderPlugin
{
public:
    std::map<void*, FSphereAnim*> SphereMap;
    // Retail keeps one last-hit cache (+0x24 node / +0x28 flag) per plugin. The port's base
    // cache is typed for FTriMeshAnim, so the sphere plugin keeps its own pointer and shares
    // bLastHit / LastHitKey / LookupCount / HitCount with the base.
    FSphereAnim* LastHitSphere;

    FSphereMeshPlugin()
        : LastHitSphere(nullptr)
    {
    }

    // The FUN_06c29f70 lookup over the sphere map: cached key -> HitCount++, else map find and
    // on a hit the cache is set. The caller bumps LookupCount (+4), as for the TriMesh map.
    FSphereAnim* FindSphereCached(void* InstId)
    {
        if (bLastHit != 0 && LastHitKey == InstId)
        {
            HitCount += 1;
            return LastHitSphere;
        }
        std::map<void*, FSphereAnim*>::iterator Found = SphereMap.find(InstId);
        if (Found == SphereMap.end())
            return nullptr;
        LastHitKey = InstId;
        LastHitSphere = Found->second;
        bLastHit = 1;
        return LastHitSphere;
    }

    // FUN_06c29f70 (the SphereMesh table entry 06c362e8).
    int DACOM_API QueryMeshInstance(void* InstId, void* /*Handle*/, const char* Iid, void** Out) override
    {
        LookupCount += 1;
        FSphereAnim* Anim = FindSphereCached(InstId);
        if (Anim == nullptr)
            return -1;
        return (Anim->QueryInterface(Iid, Out) >= 0) ? 0 : -1;
    }

    // FUN_06c2a6c0 at refcount 0 only erases the map nodes (FUN_06c2a2e0 / FUN_06c21b10) and
    // frees the head/nil nodes and the plugin; the instances in the map are not released or
    // freed, and nothing is reported. SphereMap's own destructor does exactly the node part.
    ~FSphereMeshPlugin()
    {
    }

    FSphereAnim* FindSphere(void* InstId)
    {
        std::map<void*, FSphereAnim*>::iterator Found = SphereMap.find(InstId);
        if (Found == SphereMap.end())
            return nullptr;
        return Found->second;
    }

    int DACOM_API CreateMeshInstance(void* InstId, void* Handle) override
    {
        if (Handle == nullptr || Handle == (void*)G_HandleFail)
            return 0;
        FSphereHandle* Sphere = (FSphereHandle*)Handle;
        // FUN_06c2af00: operator new(0xBC) (null -> -1) + FUN_06c2b110, then FUN_06c2b230 Init;
        // a failed Init returns -1 WITHOUT freeing the instance (retail leaks it, so does the port).
        FSphereAnim* Anim = new (std::nothrow) FSphereAnim();
        if (Anim == nullptr)
            return 0;
        if (Anim->Init(Sphere, InstId) < 0)
            return 0;
        // FUN_06c2afb0: radius < 0.0f (unordered included) -> extent = radius, else
        // Scale * Radius.
        float Center[3];
        Center[0] = 0.0f;
        Center[1] = 0.0f;
        Center[2] = 0.0f;
        float Extent = Sphere->Radius;
        if (0.0f <= Sphere->Radius)
            Extent = Sphere->Scale * Sphere->Radius;
        G_Engine->SetInstanceExtent(InstId, 1, Extent, Center);
        // FUN_06c2a060: a non-multi insert. An InstId already mapped keeps its old node and the
        // new instance leaks; the returned iterator is never end(), so retail's == end branch
        // (FUN_06c2b3c0 on the instance, return 0) cannot run.
        std::pair<std::map<void*, FSphereAnim*>::iterator, bool> Inserted =
            SphereMap.insert(std::map<void*, FSphereAnim*>::value_type(InstId, Anim));
        LastHitKey = Inserted.first->first;
        LastHitSphere = Inserted.first->second;
        bLastHit = 1;
        return 1;
    }

    int DACOM_API DestroyMeshInstance(void* InstId, void* /*Handle*/) override
    {
        LookupCount += 1;
        FSphereAnim* Anim = FindSphereCached(InstId);
        if (Anim == nullptr)
            return 0;
        Anim->Reset();
        delete Anim;
        if (bLastHit != 0 && LastHitKey == InstId)
            bLastHit = 0;
        SphereMap.erase(InstId);
        return 0;
    }

    int DACOM_API Claim(void* File) override
    {
        FSphereHandle* Handle = new (std::nothrow) FSphereHandle();
        if (Handle == nullptr)
            return -1;
        if (Handle->ResetChecked() >= 0 &&
            Handle->Load((IFileSystem*)File, G_Pipeline, G_VbManager,
                         G_MaterialLibrary) >= 0)
            return (int)((intptr_t)Handle);
        delete Handle;
        return -1;
    }

    // FUN_06c29b90: the archetype dtor FUN_06c2a970 and free -- no reference count. Retail
    // leaves EAX unset; the port returns 0.
    int DACOM_API DestroyHandle(void* Handle) override
    {
        if (Handle == nullptr || Handle == (void*)G_HandleFail)
            return 0;
        delete (FSphereHandle*)Handle;
        return 0;
    }

    int DACOM_API QueryGeometry(void* Handle, int /*Lod*/, void* A, void* B, void* /*C*/, void* Out,
                         void* /*E*/) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        const int QueryMode = (int)((intptr_t)A);
        const unsigned long Flags = (unsigned long)((uintptr_t)B);
        if (QueryGeometryCanWrite(QueryMode, Flags, Out) == 0)
            return 0;
        *(int*)Out = 0;
        return 1;
    }

    int DACOM_API GetBounds(void* Handle, float /*Lod*/, void* Out) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        FSphereHandle* Sphere = (FSphereHandle*)Handle;
        float* Box = (float*)Out;
        if (Sphere->Radius >= 0.0f)
        {
            const float Extent = Sphere->Scale * Sphere->Radius;
            const float Neg = -Extent;
            Box[0] = Extent;
            Box[1] = Neg;
            Box[2] = Extent;
            Box[3] = Neg;
            Box[4] = Extent;
            Box[5] = Neg;
        }
        else
        {
            for (int Index = 0; Index < 6; ++Index)
                Box[Index] = 0.0f;
        }
        return 1;
    }

    // LAB_06c29f40; FUN_06c2aff0 zeroes the three floats.
    int DACOM_API GetCentroid(void* Handle, float /*Lod*/, void* Out) override
    {
        if (IrcHandleOk(Handle) == 0)
            return 0;
        float* CenterOut = (float*)Out;
        CenterOut[2] = 0.0f;
        CenterOut[1] = 0.0f;
        CenterOut[0] = 0.0f;
        return 1;
    }

    int DACOM_API RenderInstance(ICamera* Camera, void* /*Handle*/, void* Inst, unsigned long /*Flags*/,
                         void* Matrix, void* Extra) override
    {
        LookupCount += 1;
        FSphereAnim* Anim = FindSphereCached(Inst);
        if (Anim == nullptr)
            return 0;
        return Anim->Draw(Camera, (const FTransform*)Matrix,
                          (IMaterial*)Extra)
                   ? 1
                   : 0;
    }

    // LAB_06c29ff0. The sphere plugin binds nothing.
    int DACOM_API BindMeshInstance(void* /*Inst*/, void* /*Handle*/) override
    {
        return -1;
    }
};

// LAB_06c22210 after its AGGDESC checks: calloc(1, 0x38), the ctor, then FUN_06c211a0; a
// failure runs the dtor FUN_06c21080 and frees the plugin.
GENRESULT FRenderPluginDesc::CreateTriMesh(void** Out) const
{
    FTriMeshPlugin* Plugin = new (std::nothrow) FTriMeshPlugin();
    if (Plugin == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    const GENRESULT Result = Plugin->Initialize(this);
    if (Result != GR_OK)
    {
        delete Plugin;
        *Out = nullptr;
        return Result;
    }
    *Out = (IRenderComponent*)Plugin;
    return Result;
}

// LAB_06c2a3d0 after its AGGDESC checks: calloc(1, 0x34), the ctor, then FUN_06c2d720 (AGGDESC
// +8 and +0xC must be set, else -2).
GENRESULT CreateSphereMeshPlugin(const FRenderPluginDesc* Desc, void** Out)
{
    FSphereMeshPlugin* Plugin = new (std::nothrow) FSphereMeshPlugin();
    if (Plugin == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    GENRESULT Result = GR_OK;
    if (Desc->Parent == nullptr || Desc->Engine == nullptr)
        Result = (GENRESULT)0xFFFFFFFE;
    if (Result != GR_OK)
    {
        delete Plugin;
        *Out = nullptr;
        return Result;
    }
    *Out = (IRenderComponent*)Plugin;
    return Result;
}
