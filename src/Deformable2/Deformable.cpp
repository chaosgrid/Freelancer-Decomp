// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Deformable2.h"
#include "EyeMaterial.h"
#include "common_misc.h"
#include "msvcrt_compat.h"

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

static void WriteIdentity3x4(float* Matrix);
static void ReleaseRenderCaches();

namespace
{

    void LogDeformError(int Line, const char* Message)
    {
        if (FDUMP != nullptr)
        {
            // .data:06602070: the __FILE__ of the three Initialize sites (lines 0xB5, 0xCC, 0xF4).
            FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s",
                  "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\Deformable.cpp",
                  Line, Message);
        }
    }

    void ReleaseInterface(IDAComponent*& Pointer)
    {
        if (Pointer == nullptr)
            return;
        Pointer->Release();
        Pointer = nullptr;
    }

    template <typename T>
    void ReleaseInterface(T*& Pointer)
    {
        IDAComponent* Component = Pointer;
        ReleaseInterface(Component);
        Pointer = nullptr;
    }

}

static GENRESULT DACOM_API StubRet8(void* /*This*/, void* /*A*/);

// FUN_065f1030 (thiscall: InverseRoot).
void FBoneRuntime::ComposeWorldMatrix(const float* InverseRoot)
{
    float Local[12];
    AcquireMathEngine()->MulTransform(
        *(FTransform*)Local,
        *(const FTransform*)LocalTransform,
        *(const FTransform*)Source->BoneToRoot); // +0x1C
    float LocalCopy[12];
    std::memcpy(LocalCopy, Local, sizeof(LocalCopy));
    float World[12];
    AcquireMathEngine()->MulTransform(*(FTransform*)World,
                                      *(const FTransform*)InverseRoot,
                                      *(const FTransform*)LocalCopy); // +0x1C
    std::memcpy(Matrix, World, sizeof(World));
}

// EngBase's IEngine, acquired by QI. Every slot this module calls (+0x20, +0x24,
// +0x30, +0x3C, +0x48, +0x60) is IEngine's.
IEngine* G_Engine = nullptr;

// FUN_065f10c0: IEngine +0x3C on each loaded bone's +0 handle, then on +0x1e80 --
// unconditionally, -1 included (a skeleton whose FUN_065f1100 failed still passes its -1 here).
void FSkeleton::DestroyHandles()
{
    int Index = 0;
    while (Index < BoneCount)
    {
        G_Engine->ReleaseHandle(
            (void*)((ptrdiff_t)(Bones[Index].Parent)));
        Index += 1;
    }
    G_Engine->ReleaseHandle(EngineHandle);
}

// DAT_06603150 / DAT_06603154, set by FUN_065f1d90.
static bool (__cdecl* G_BoneLinkFn)(void*, int, FLodHolder*) = nullptr;

// FUN_065f1100 (thiscall on the skeleton: Name, Fs).
bool FSkeleton::Load(const char* Name, IFileSystem* Fs)
{
    WIN32_FIND_DATAA FindData;
    const int Finder = FsFindFirst(Fs, "*.3db", &FindData);
    if (Finder != -1)
    {
        do
        {
            FFileSystemDesc ChildDesc;
            FillFileDesc(&ChildDesc, FindData.cFileName);
            ChildDesc.FindHandle = (unsigned long)Finder;
            IFileSystem* Child = nullptr;
            if (Fs->CreateInstance((AGGDESC*)(&ChildDesc),
                                   (void**)(&Child)) >= 0)
            {
                const int bSkeletonNode = Child->OpenNode("DFMSkeleton");
                FBoneSlot* Bone = &Bones[BoneCount];
                float Buffer[12];
                WriteIdentity3x4(Buffer);
                FFileSystemDesc BoneDesc;
                FillFileDesc(&BoneDesc, "Bone to root");
                bool bRead = false;
                const int Handle = Child->OpenByDesc(&BoneDesc);
                if (Handle != -1)
                {
                    unsigned long Got = 0;
                    if (Child->Read((unsigned long)Handle, Buffer, 0x30, &Got, 0) != 0 &&
                        Got == 0x30)
                    {
                        std::memcpy(Bone->BoneToRoot, Buffer, 0x30);
                        bRead = true;
                    }
                    Child->CloseFile((unsigned long)Handle);
                }
                const GENRESULT Result = bRead ? 0 : (GENRESULT)0x80004005;
                // Retail: the ".." goes to the parent Fs, not to Child (065F12BB, [esp+214h]).
                if (bSkeletonNode != 0)
                    Fs->OpenNode(G_ParentNode);
                if (Result >= 0)
                {
                    FEngineName BoneName;
                    BoneName.Text = FindData.cFileName;
                    BoneName.Crc = DACOM_CRC::GetCRC32(FindData.cFileName);
                    Bone->Parent = (int)((ptrdiff_t)(
                        G_Engine->CreateArchetypeHandle(&BoneName, Child)));
                    BoneCount += 1;
                }
            }
            if (Child != nullptr)
                Child->Release();
        } while (FsFindNext(Fs, Finder, &FindData) != 0);
        FsFindClose(Fs, Finder);
    }

    IDAComponent* Saved = nullptr;
    G_Engine->GetSearchPath((void**)(&Saved));
    G_Engine->SetFilesystem(Fs);
    FEngineName SkelName;
    SkelName.Text = Name;
    SkelName.Crc = DACOM_CRC::GetCRC32(Name);
    EngineHandle = G_Engine->CreateArchetypeHandle(&SkelName, Fs);
    G_Engine->SetFilesystem(Saved);
    Saved->Release();
    return EngineHandle != G_InvalidEngineHandle;
}

// FUN_065f13e0 (thiscall).
static void WriteIdentity3x4(float* Matrix)
{
    std::memset(Matrix, 0, 12 * sizeof(float));
    Matrix[0] = 1.0f;
    Matrix[4] = 1.0f;
    Matrix[8] = 1.0f;
}

// FUN_065f1410 (thiscall).
static void AddRefSkeleton(FSkeleton* Skel)
{
    if (Skel == nullptr)
        return;
    Skel->RefCount += 1;
}

// FUN_065f1420 (thiscall).
int ReleaseSkeleton(FSkeleton* Skel)
{
    if (Skel == nullptr)
        return 0;
    Skel->RefCount -= 1;
    const int Remaining = Skel->RefCount;
    if (Remaining <= 0)
    {
        Skel->DestroyHandles();
        delete Skel;
    }
    return Remaining;
}

// LAB_065f1450 (cdecl): forwards to the thiscall FUN_065f14f0 with the holder as this.
static bool __cdecl BoneLinkCallbackThunk(void* BoneInst, int Parent, FLodHolder* Holder)
{
    return Holder->LinkBoneByParent(BoneInst, Parent);
}

// FUN_065f1470 (thiscall).
static void DestroyLodHolderBody(FLodHolder* Holder)
{
    if (Holder == nullptr)
        return;
    if (Holder->BoneSlots != nullptr)
    {
        delete[] (unsigned char*)Holder->BoneSlots;
        Holder->BoneSlots = nullptr;
    }
    if (Holder->Skeleton != nullptr)
    {
        ReleaseSkeleton(Holder->Skeleton);
        Holder->Skeleton = nullptr;
    }
}

// FUN_065f14a0 (thiscall on the holder: Index, BoneInst).
FBoneRuntime* FLodHolder::BindOneBoneRuntime(int Index, void* BoneInst)
{
    FBoneRuntime* Slot = &BoneSlots[Index];
    unsigned char Flags = Slot->Flags;
    Flags = (unsigned char)(((Index == 0) ^ Flags) & 1 ^ Flags);
    Slot->Flags = Flags;
    Slot->Source = &Skeleton->Bones[Index];
    G_Engine->SetInstanceTransform(BoneInst, Slot);
    return Slot;
}

// FUN_065f14f0: parent-link walk from BindCursor.
bool FLodHolder::LinkBoneByParent(void* BoneInst, int Parent)
{
    FSkeleton* Skel = Skeleton;
    int Cursor = BindCursor;
    if (Cursor >= Skel->BoneCount)
        return false;

    while (Skel->Bones[Cursor].Parent != Parent)
    {
        Cursor += 1;
        if (Cursor >= Skel->BoneCount)
            return false;
    }

    BindCursor = Cursor;
    FBoneRuntime* Slot = BindOneBoneRuntime(Cursor, BoneInst);
    BindCursor = BindCursor + 1;
    return Slot != nullptr;
}

// The per-slot body of the FUN_065f1550 construct loop.
FBoneRuntime::FBoneRuntime()
{
    Instance = G_InvalidEngineHandle;
    Source = nullptr;
    Flags = 0;
    std::memset(FlagPadding, 0, sizeof(FlagPadding));
    WriteIdentity3x3(Matrix);
    Matrix[9] = 0.0f;
    Matrix[10] = 0.0f;
    Matrix[11] = 0.0f;
}

// The FUN_065f1550 construct loop body for one slot. The memset stands in for the operator
// new garbage in the fields retail leaves unwritten (+0x0C..+0x3B). No test of the slot.
static void InitBoneRuntimeSlot(FBoneRuntime* Slot)
{
    std::memset((void*)Slot, 0, sizeof(FBoneRuntime));
    new (Slot) FBoneRuntime();
}

static FLodHolder* G_BoneLinkHolder = nullptr;

static void RegisterBoneLinkCallback(bool (__cdecl* Fn)(void*, int, FLodHolder*), FLodHolder* Holder)
{
    G_BoneLinkFn = Fn;
    G_BoneLinkHolder = Holder;
}

// FUN_065f1550 (thiscall on the holder: Skel). Always returns 1.
unsigned char FLodHolder::AttachSkeleton(FSkeleton* Skel)
{
    if (Skeleton != Skel)
    {
        if (Skeleton != nullptr)
        {
            ReleaseSkeleton(Skeleton);
            Skeleton = nullptr;
        }
        Skeleton = Skel;
        if (Skel != nullptr)
            AddRefSkeleton(Skel);
    }

    Skel = Skeleton;
    const int Count = Skel->BoneCount;
    FBoneRuntime* Slots =
        (FBoneRuntime*)new unsigned char[(size_t)Count * sizeof(FBoneRuntime)];
    int Index = 0;
    while (Index < Count)
    {
        InitBoneRuntimeSlot(&Slots[Index]);
        Index += 1;
    }
    BoneSlots = Slots;
    RegisterBoneLinkCallback(&BoneLinkCallbackThunk, this);
    void* RootInst = G_Engine->CreateInstance(Skel->EngineHandle, nullptr);
    RegisterBoneLinkCallback(nullptr, nullptr);
    if (RootInst == G_InvalidEngineHandle)
        return 1;

    int BoneIndex = 0;
    while (BoneIndex < Skeleton->BoneCount)
    {
        if (BoneSlots[BoneIndex].Instance == G_InvalidEngineHandle)
        {
            void* ArchHandle = (void*)(
                (ptrdiff_t)(Skeleton->Bones[BoneIndex].Parent));
            void* BoneInst = G_Engine->CreateInstance(ArchHandle, nullptr);
            FBoneRuntime* Slot = BindOneBoneRuntime(BoneIndex, BoneInst);
            Slot->Flags = (unsigned char)(Slot->Flags | 1u);
        }
        BoneIndex += 1;
    }
    return 1;
}

// FUN_065f1660 (thiscall). Retail does not test for null.
void WriteIdentity3x3(float* Matrix)
{
    std::memset(Matrix, 0, 9 * sizeof(float));
    Matrix[0] = 1.0f;
    Matrix[4] = 1.0f;
    Matrix[8] = 1.0f;
}

// LAB_065f1690 (RET 8), slot 0: [this+4] = the instance.
GENRESULT DACOM_API FBoneRuntime::Init(void* InInstance)
{
    Instance = InInstance;
    return GR_OK; // retail returns this; nobody reads it
}

// --- FBoneRuntime : IInstanceTransform, vtable PTR_LAB_066010a0 (14 slots) ---
// Every slot is stdcall with the engine instance as its first argument (EngBase pushes it);
// only slot 0 uses it. None of them tests `this` or a pointer argument.

// FUN_065f16a0: slots 1 and 2, and the ICF-shared empty body of several IEngineComponent
// slots. Retail leaves EAX unset; the port answers 0.
static unsigned long DACOM_API BoneRuntimeRet8(void* /*This*/, void* /*A*/)
{
    return 0;
}

// FUN_065f16a0, slot 1.
GENRESULT DACOM_API FBoneRuntime::AddRef(void* InInstance)
{
    return (GENRESULT)(BoneRuntimeRet8(this, InInstance));
}

// FUN_065f16a0, slot 2.
GENRESULT DACOM_API FBoneRuntime::Release(void* InInstance)
{
    return (GENRESULT)(BoneRuntimeRet8(this, InInstance));
}

// LAB_065f16b0, slot 3.
GENRESULT DACOM_API FBoneRuntime::SetPosition(void* /*InInstance*/, const float* Position)
{
    float* const Dest = LocalTransform + 9;
    Dest[0] = Position[0];
    Dest[1] = Position[1];
    Dest[2] = Position[2];
    if ((this->Flags & 1u) != 0)
        this->Flags = (unsigned char)(this->Flags | 2u);
    return GR_OK; // retail returns the flag byte; nobody reads it
}

// LAB_065f16e0, slot 4.
float* DACOM_API FBoneRuntime::GetPosition(void* /*InInstance*/)
{
    return LocalTransform + 9;
}

// LAB_065f16f0, slot 5. The copy is unconditional; the bit 0 test made before it only decides
// the |= 2 after it.
GENRESULT DACOM_API FBoneRuntime::SetOrientation(void* /*InInstance*/, const float* Rows)
{
    const unsigned char Flags = this->Flags;
    std::memcpy(LocalTransform, Rows, 9 * sizeof(float));
    if ((Flags & 1u) != 0)
        this->Flags = (unsigned char)(Flags | 2u);
    return GR_OK;
}

// LAB_065f1720, slots 6 and 8.
float* DACOM_API FBoneRuntime::GetOrientation(void* /*InInstance*/)
{
    return LocalTransform;
}

// LAB_065f1720, slot 8 (the same body as slot 6).
float* DACOM_API FBoneRuntime::GetMatrix(void* /*InInstance*/)
{
    return LocalTransform;
}

// LAB_065f1730, slot 7: as slot 5, with rotation + position.
GENRESULT DACOM_API FBoneRuntime::SetMatrix(void* /*InInstance*/, const float* InMatrix)
{
    const unsigned char Flags = this->Flags;
    std::memcpy(LocalTransform, InMatrix, 12 * sizeof(float));
    if ((Flags & 1u) != 0)
        this->Flags = (unsigned char)(Flags | 2u);
    return GR_OK;
}

// LAB_065f1760, slot 9. The instance is not read.
GENRESULT DACOM_API FBoneRuntime::GetExtra(void* /*InInstance*/, float* OutExtra, float* OutVec)
{
    *(unsigned long*)OutExtra = 0;
    ((unsigned long*)OutVec)[0] = 0;
    ((unsigned long*)OutVec)[1] = 0;
    ((unsigned long*)OutVec)[2] = 0;
    return GR_OK; // retail returns OutVec; nobody reads it
}

// LAB_065f1780, slot 10.
GENRESULT DACOM_API FBoneRuntime::SetExtra(void* /*InInstance*/, float /*Extra*/,
                                           const float* /*Vec*/)
{
    return GR_OK;
}

// LAB_065f1790: slot 11 SetUser, shared with IEngineComponent slots 5/10.
static void DACOM_API BoneRuntimeRetC(void* /*This*/, void* /*A*/, void* /*B*/)
{
}

// LAB_065f1790, slot 11.
GENRESULT DACOM_API FBoneRuntime::SetUser(void* InInstance, unsigned long User)
{
    BoneRuntimeRetC(this, InInstance, (void*)((ptrdiff_t)User));
    return GR_OK;
}

// LAB_065f17a0, slot 12.
unsigned long DACOM_API FBoneRuntime::GetUser(void* /*InInstance*/)
{
    return 0;
}

// LAB_065f17b0, slot 13.
unsigned long DACOM_API FBoneRuntime::MarkWorldDirty(void* /*InInstance*/)
{
    unsigned char Flags = this->Flags;
    if ((Flags & 2u) != 0)
        return 0;
    Flags = (unsigned char)(Flags | 2u);
    this->Flags = Flags;
    return (unsigned long)((~Flags) & 1u);
}

// FUN_065f17d0 (thiscall).
void AddRefLodHolder(FLodHolder* Holder)
{
    if (Holder == nullptr)
        return;
    Holder->RefCount += 1;
}

// FUN_065f17e0 (thiscall).
int ReleaseLodHolder(FLodHolder* Holder)
{
    if (Holder == nullptr)
        return 0;
    Holder->RefCount -= 1;
    const int Remaining = Holder->RefCount;
    if (Remaining <= 0)
    {
        DestroyLodHolderBody(Holder);
        delete Holder;
    }
    return Remaining;
}

// DAT_06603128 .. DAT_0660314c — module cache written by FUN_065f1810 / FUN_065f1900 / LAB_065f1bf0.
static IDAComponent* G_System = nullptr;
IMaterialLibrary* G_MaterialLibrary = nullptr;
IRenderPipeline* G_Pipeline = nullptr;
// EngBase's IAnimation2, acquired by QI (Iids::Animation2).
IAnimation2* G_Animation = nullptr;
// EngBase's IChannel, the facet at Animation+8. All five slots this module drives
// (3, 5, 6, 7, 15) match engbase.h's declaration argument for argument.
IChannel* G_Channel = nullptr;
IRenderer2* G_Renderer = nullptr;

// FUN_065f1810: stores 0 without Release.
static void ZeroModuleInterfaceCaches()
{
    G_System = nullptr;
    G_MaterialLibrary = nullptr;
    G_Pipeline = nullptr;
    G_Engine = nullptr;
    G_Animation = nullptr;
    G_Channel = nullptr;
    G_Renderer = nullptr;
}


// FUN_065f1810; vtables 06601110 / 066010dc (066011ac is IEngineComponent's own).
FDeformable::FDeformable()
{
    ZeroModuleInterfaceCaches();
}

unsigned long DACOM_API FDeformableObject::AddRef()
{
    return Controlling->AddRef();
}

unsigned long DACOM_API FDeformableObject::Release()
{
    return Controlling->Release();
}

static GENRESULT DACOM_API DeformableCreate(FDeformable* /*This*/, void** OutInstance,
                                            int* Parts, void* Arg3, void* Arg4, void* /*Arg5*/)
{
    // LAB_065f1e50: an empty parts table is rejected before an instance is built (retail).
    if (Parts[0] <= 0)
        return GR_INVALID_PARMS;

    // `new FDeformInstance` sets the vtable FUN_065f3650 stores first (PTR_FUN_06601308 --
    // EngBase calls it back as the playback's script); FDeformInstance::ConstructBody does the rest.
    FDeformInstance* Instance = new FDeformInstance;
    Instance->ConstructBody();
    if (Instance->ConnectParts(Parts, Arg3, Arg4))
    {
        *OutInstance = Instance;
        return GR_OK;
    }
    Instance->Teardown();
    delete Instance;
    *OutInstance = nullptr;
    return GR_GENERIC;
}

GENRESULT DACOM_API FDeformable::CreateAlias(void** OutInstance, int* Parts, void* Arg3,
                                                   void* Arg4, void* Arg5)
{
    // LAB_065f1860: tail-jumps to vtable+0x0C.
    return DeformableCreate(this, OutInstance, Parts, Arg3, Arg4, Arg5);
}

GENRESULT DACOM_API FDeformable::CreateUnused(void* /*A*/, void* /*B*/, void* /*C*/, void* /*D*/,
                                              void* /*E*/, void* /*F*/, void* /*G*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRetC(void* /*This*/, void* /*A*/, void* /*B*/)
{
    return GR_GENERIC;
}

GENRESULT DACOM_API FDeformable::Unused21(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused22(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused23(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused28(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused30(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused31(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused32(void* A, void* B)
{
    return StubRetC(this, A, B);
}

// Shared fail stubs returning -1; the suffix is the retail RET size.
static GENRESULT DACOM_API StubRet8(void* /*This*/, void* /*A*/)
{
    return GR_GENERIC;
}

GENRESULT DACOM_API FDeformable::Unused13(void* A)
{
    return StubRet8(this, A);
}

GENRESULT DACOM_API FDeformable::Unused14(void* A)
{
    return StubRet8(this, A);
}

static GENRESULT DACOM_API StubRet14(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/,
                                     void* /*D*/)
{
    return GR_GENERIC;
}

GENRESULT DACOM_API FDeformable::Unused17(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
}

GENRESULT DACOM_API FDeformable::Unused25(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
}

GENRESULT DACOM_API FDeformable::Unused34(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
}

GENRESULT DACOM_API FDeformable::Unused29(void* /*A*/, void* /*B*/, void* /*C*/, void* /*D*/,
                                          void* /*E*/, void* /*F*/, void* /*G*/, void* /*H*/,
                                          void* /*I*/)
{
    return GR_GENERIC;
}

GENRESULT DACOM_API FDeformable::Unused33(void* /*A*/, void* /*B*/, void* /*C*/)
{
    return GR_GENERIC;
}

// FUN_065f18d0. Anim/Engine/Channel/Renderer are non-owning after QI+Release.
static void ReleaseComponentCaches()
{
    ReleaseRenderCaches();
    ReleaseInterface(G_System);
}

// FUN_065f18d0. Called by FUN_065f22f0's failure path and the inner Release, always right
// before operator delete.
FDeformable::~FDeformable()
{
    ReleaseComponentCaches();
}

static const char* const G_RateCharset = "0123456789truefalsynoxTRUEFALSYNOX+-";

// FUN_065f1900's value parse; G_RateCharset is .data:06602138.
static unsigned long ParseDeformRateValue(char* Text)
{
    const size_t Span = strspn(Text, G_RateCharset);
    if (Span != 0)
        Text[Span] = '\0';

    if (_stricmp(Text, "true") == 0) // DACOM's stricmp is _stricmp (DacomStricmp)
        return 1;
    if (_stricmp(Text, "yes") == 0)
        return 1;
    if (_stricmp(Text, "false") == 0)
        return 0;
    if (_stricmp(Text, "no") == 0)
        return 0;

    if (strchr(Text, 'x') != nullptr || strchr(Text, 'X') != nullptr)
        return strtoul(Text, nullptr, 16);

    strspn(Text, "0123456789+-");
    return (unsigned long)(MsvcrtAtoi(Text));
}

// Reused parser buffer; a failed read leaves its contents unchanged.
static char G_DeformRateValue[0x400];
float G_DeformRate = 0.0f;

// Widen the unsigned rate before dividing, then round the result to float.
static void ApplyParsedRate(unsigned long Rate)
{
    if (Rate == 0)
        G_DeformRate = 0.0f;
    else
        G_DeformRate = (float)(1.0 / (double)Rate);
}

// FUN_065f1900 (thiscall on the component: Desc).
static GENRESULT ApplyCreateInstanceInit(FDeformableObject* Object, FAggDesc18* Desc)
{
    G_System = Desc->SystemAt14;
    if (G_System == nullptr)
        return GR_GENERIC;

    G_System->AddRef();
    ICOManager* Dacom = DACOM_Acquire();
    IProfileParser* First = nullptr;
    if (Dacom->QueryInterface(Iids::ProfileParser, (void**)(&First)) >= 0)
    {
        IProfileParser* Parser = nullptr; // the holder at entry+4
        if (First != nullptr)
        {
            Parser = First;
            Parser->AddRef();
        }
        unsigned long Rate = 0;
        bool bParse = true;
        if (Parser == nullptr &&
            Dacom->QueryInterface(Iids::ProfileParser, (void**)(&Parser)) != 0)
        {
            // Retail: the out pointer's bits become the rate (065F1B23 mov esi,[esp+54h]).
            Rate = (unsigned long)((ptrdiff_t)Parser);
            bParse = false;
        }
        if (bParse)
        {
            void* const Section = Parser->OpenSection(G_DeformableSection, 3);
            if (Section != nullptr)
            {
                if (Parser->GetString(Section, G_DeformRateKey, G_DeformRateValue, 0x400) != 0)
                    Rate = ParseDeformRateValue(G_DeformRateValue);
                Parser->CloseSection(Section);
            }
        }
        if (Parser != nullptr) // FUN_065f9170
        {
            Parser->Release();
            Parser = nullptr;
        }
        ApplyParsedRate(Rate);
    }

    if (Object->QueryInterface(Iids::Engine, (void**)(&G_Engine)) < 0)
    {
        LogDeformError(0xB5, "Deform: Initialize: Unable to acquire IID_IEngine, unable to continue.");
        ReleaseInterface(G_System);
        if (First != nullptr)
            First->Release();
        return GR_GENERIC;
    }
    G_Engine->Release();
    if (First != nullptr)
        First->Release();
    return GR_OK;
}

// One QI of LAB_065f1bf0, straight into the module cache (no pre-clear, no test of Source or
// of the out pointer). Failure logs ERROR at Line and runs FUN_065f1df0.
static bool QueryRequired(IDAComponent* Source, const char* Iid, void** Out, int Line,
                          const char* Error)
{
    if (Source->QueryInterface(Iid, Out) >= 0)
        return true;
    LogDeformError(Line, Error);
    ReleaseRenderCaches();
    return false;
}

IRPVertexBuffer* G_VertexBuffer = nullptr;
IRPIndexBuffer* G_IndexBuffer = nullptr;

// --- IEngineComponent helpers (compiler builds IEC vtable + this-4 thunks) ---

// LAB_065f1bf0 (IEngineComponent slot 3).
static GENRESULT DACOM_API EngineInitialize(void* /*EngineThis*/)
{
    ReleaseRenderCaches();

    if (!QueryRequired(G_System, Iids::RenderPipeline8B, (void**)(&G_Pipeline), 0xCC,
                       "Deform: Initialize: Unable to acquire IID_IRenderPipeline, unable to continue."))
        return GR_GENERIC;
    if (!QueryRequired(G_System, Iids::MaterialLibrary, (void**)(&G_MaterialLibrary),
                       0xD2,
                       "Deform: Initialize: Unable to acquire IID_IMaterialLibrary, unable to continue."))
        return GR_GENERIC;
    if (!QueryRequired(G_System, Iids::RPVertexBuffer, (void**)(&G_VertexBuffer),
                       0xD8,
                       "Deform: Initialize: Unable to acquire IID_IVertexBufferManager, unable to continue."))
        return GR_GENERIC;
    if (!QueryRequired(G_System, Iids::RPIndexBuffer, (void**)(&G_IndexBuffer), 0xDE,
                       "Deform: Initialize: Unable to acquire IID_IIndexBufferManager, unable to continue."))
        return GR_GENERIC;

    if (!QueryRequired(G_Engine, Iids::Animation2, (void**)(&G_Animation), 0xE6,
                       "Deform: Initialize: Unable to acquire IID_IAnimation2, unable to continue"))
        return GR_GENERIC;
    G_Animation->Release();
    if (!QueryRequired(G_Engine, Iids::Channel, (void**)(&G_Channel), 0xED,
                       "Deform: Initialize: Unable to acquire IID_IChannel, unable to continue"))
        return GR_GENERIC;
    G_Channel->Release();
    if (!QueryRequired(G_Engine, Iids::Renderer, (void**)(&G_Renderer), 0xF4,
                       "Deform: Initialize: Unable to acquire IID_IRenderer, unable to continue"))
        return GR_GENERIC;
    G_Renderer->Release();

    BindEyeMaterials(G_MaterialLibrary);
    return GR_OK;
}

// LAB_065f1dc0: calls DAT_06603150 (cdecl) with DAT_06603154 as the third argument.
static GENRESULT DACOM_API EngineCallbackTrampoline(void* /*This*/, void* Arg1, void* Arg2)
{
    if (G_BoneLinkFn == nullptr)
        return GR_OK;
    return (GENRESULT)(
        G_BoneLinkFn(Arg1, (int)((ptrdiff_t)Arg2), G_BoneLinkHolder));
}

// FUN_065f1df0: only these four, in retail order.
static void ReleaseRenderCaches()
{
    ReleaseInterface(G_Pipeline);
    ReleaseInterface(G_MaterialLibrary);
    ReleaseInterface(G_IndexBuffer);
    ReleaseInterface(G_VertexBuffer);
}

GENRESULT DACOM_API FDeformable::Create(void** OutInstance, int* Parts, void* Arg3, void* Arg4,
                                               void* Arg5)
{
    return DeformableCreate(this, OutInstance, Parts, Arg3, Arg4, Arg5);
}

GENRESULT DACOM_API FDeformable::Destroy(void* InInstance)
{
    FDeformInstance* Instance = (FDeformInstance*)InInstance;
    // FUN_065f1ed0. FUN_065f3ed0, then the dtor FUN_065f3640 runs it again (a no-op on the
    // emptied instance).
    if (Instance == nullptr)
        return GR_GENERIC;
    Instance->Teardown();
    Instance->Teardown();
    delete Instance;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::Play(void* Instance, int* Out, void* Name, void* Extra,
                                               void* ArgC, void* ArgD, void* ArgE, void* ArgF,
                                               void* ArgG, void* ArgH, void* Script)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1f00 -> FUN_065f41d0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    const int Handle =
        DeformInstance->PlayAnim(Name, Extra, ArgC, ArgD, ArgE, ArgF, ArgG, ArgH, Script);
    *Out = Handle;
    if (Handle == -1)
        return GR_GENERIC;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::PlayAlt(void* Instance, int* Out, void* Name, void* Extra,
                                                void* ArgC, void* ArgD, void* ArgE, void* ArgF,
                                                void* ArgG, void* ArgH, void* Script)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1f50 -> FUN_065f4660.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    const int Handle =
        DeformInstance->PlayAnimChain(Name, Extra, ArgC, ArgD, ArgE, ArgF, ArgG, ArgH, Script);
    *Out = Handle;
    if (Handle == -1)
        return GR_GENERIC;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::DestroyAll(void* Instance)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1fa0 → FUN_065f4c80.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    DeformInstance->DestroyAllList();
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::DestroyHandle(void* Instance, int Handle)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1fc0 → FUN_065f4d30.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    DeformInstance->DestroyListByHandle(Handle);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::StartAim(void* Instance, void** Out, unsigned char* AimDesc,
                                                 void* Arg3)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f1fe0 -> FUN_065f5400 start_aim. Only DeformInstance is tested.
    if (DeformInstance == nullptr)
        return GR_GENERIC;

    FIkListNode* Node = DeformInstance->AllocAimEvent(AimDesc, Arg3);
    *Out = Node;
    if (Node == (FIkListNode*)((ptrdiff_t)-1))
        return GR_GENERIC;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::StopAim(void* Instance, void* Node)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    FIkListNode* AimNode = (FIkListNode*)Node;
    // LAB_065f2010 → FUN_065f5950 stop_aim (node, not Event). Both null tests are retail.
    if (DeformInstance == nullptr || AimNode == nullptr)
        return GR_GENERIC;
    StopAimNode(AimNode);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::SetLod(void* Instance, float Dt, int* LodIds)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2040 -> FUN_065f3fd0(LodIds, Dt).
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    unsigned char bChanged = 0;
    int Index = 0;
    while (Index < DeformInstance->PartCount)
    {
        bChanged = (unsigned char)(
            bChanged | DeformInstance->PartArray[Index].SelectLod(LodIds[Index]));
        Index += 1;
    }
    if (bChanged != 0)
        DeformInstance->RateCookie = -1.0f;
    const double Remaining = (double)DeformInstance->RateCookie - (double)Dt;
    DeformInstance->RateCookie = (float)Remaining;
    if (!(Remaining < 0.0) && Remaining != 0.0)
        return GR_OK;
    DeformInstance->RateCookie = G_DeformRate;
    int Part = 0;
    while (Part < DeformInstance->PartCount)
    {
        DeformInstance->PartArray[Part].MarkSkinDirty();
        Part += 1;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::Draw(void* Instance, void* Camera, void* UnusedB,
                                              void* RenderArg, int* OutLod, unsigned int Flags)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2070 -> FUN_065f4070(inst, B, A, D, C, E). A null DeformInstance still stores 0
    // to *OutLod (no test).
    if (DeformInstance == nullptr)
    {
        *OutLod = 0;
        return GR_GENERIC;
    }
    DeformInstance->DrawParts(UnusedB, (ICamera*)Camera, OutLod, RenderArg, Flags);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::GetFirstBone(void* Instance, int* Out)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f20b0. Out is never tested.
    if (DeformInstance == nullptr)
    {
        *Out = -1;
        return GR_GENERIC;
    }
    if (DeformInstance->PartArray == nullptr)
    {
        *Out = -1;
        return GR_OK;
    }
    *Out = (int)(
        (ptrdiff_t)(DeformInstance->PartArray[0].Lod->BoneSlots[0].Instance));
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::SetPosePos(void* Instance, float* Pos3)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f20f0 -> FUN_065f4f00(Pos3).
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    DeformInstance->SweepExpiredList();
    float Vec[3];
    Vec[0] = Pos3[0];
    Vec[1] = Pos3[1];
    Vec[2] = Pos3[2];
    unsigned char bHit = 0;
    FIkListNode* Node = DeformInstance->ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
        {
            const unsigned long Handle =
                (unsigned long)((ptrdiff_t)Node->Event);
            float Duration;
            float MappedA[3];
            float MappedB[3];
            MappedA[0] = 0.0f;
            MappedA[1] = 0.0f;
            MappedA[2] = 0.0f;
            MappedB[0] = 0.0f;
            MappedB[1] = 0.0f;
            MappedB[2] = 0.0f;
            double Sum = 0.0;
            if (G_Animation->GetDurationIfNamed(Handle, &Duration) != 0)
            {
                G_Animation->TouchMapped(Handle, MappedB, MappedA);
                Sum = (double)Duration + (double)MappedA[1];
            }
            else
                Sum = (double)DeformInstance->RootHeightOffset + 0.0;
            const float Y = (float)(Sum + (double)DeformInstance->BaseHeight);
            Vec[1] = Y;
            float OverlayPos[3];
            OverlayPos[0] = Pos3[0];
            OverlayPos[1] = Y;
            OverlayPos[2] = Pos3[2];
            G_Animation->SetOverlayPos(Handle, OverlayPos);
            bHit = 1;
        }
        Node = Node->Next;
    }
    if (bHit == 0)
        Vec[1] = DeformInstance->RootHeightOffset + DeformInstance->BaseHeight;
    G_Engine->SetInstancePosition((void*)((ptrdiff_t)(
                                      FirstBoneInstance(DeformInstance))),
                                  Vec);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::SetPoseOri(void* Instance, void* Ori)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2110 -> FUN_065f5050(Ori).
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    FIkListNode* Node = DeformInstance->ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
            G_Animation->SetOverlayExtra(
                (unsigned long)((ptrdiff_t)Node->Event),
                (float*)Ori);
        Node = Node->Next;
    }
    G_Engine->SetInstanceOrientation(
        (void*)((ptrdiff_t)(FirstBoneInstance(DeformInstance))), Ori);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::GetOffset(void* Instance, float* Out)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2130. Out is not tested.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    *Out = DeformInstance->BaseHeight;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::SetHeight(void* Instance, float Height)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2150 -> FUN_065f50b0(Height).
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    DeformInstance->BaseHeight = Height;
    DeformInstance->SweepExpiredList();
    FIkListNode* Node = DeformInstance->ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
        {
            const unsigned long Handle =
                (unsigned long)((ptrdiff_t)Node->Event);
            float Duration = 0.0f;
            float Base = 0.0f;
            float Add = 0.0f;
            float Ref = 0.0f;
            if (G_Animation->GetDurationIfNamed(Handle, &Duration) != 0)
            {
                float MappedA[3];
                float MappedB[3];
                MappedA[0] = 0.0f;
                MappedA[1] = 0.0f;
                MappedA[2] = 0.0f;
                MappedB[0] = 0.0f;
                MappedB[1] = 0.0f;
                MappedB[2] = 0.0f;
                G_Animation->TouchMapped(Handle, MappedA, MappedB);
                Base = Duration;
                Add = MappedB[1];
                Ref = MappedA[1];
            }
            else
            {
                Base = DeformInstance->RootHeightOffset;
                Add = 0.0f;
                Ref = DeformInstance->PartArray[0].Lod->BoneSlots[0].LocalTransform[10];
            }
            const double Delta = (((double)Base + (double)Add) +
                                  (double)DeformInstance->BaseHeight) -
                                 (double)Ref;
            if (std::fabs(Delta) > 1.1920928955078125e-07)
            {
                float Vec[3];
                Vec[0] = 0.0f;
                Vec[1] = (float)Delta;
                Vec[2] = 0.0f;
                G_Animation->AddOverlayPos(Handle, Vec);
            }
        }
        Node = Node->Next;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::GetListCount(void* Instance, int* Out)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2170 -> FUN_065f51d0. Out is never tested.
    if (DeformInstance == nullptr)
    {
        *Out = 0;
        return GR_GENERIC;
    }
    DeformInstance->SweepExpiredList();
    *Out = DeformInstance->ListCount;
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::CopyList(void* Instance, int* Out)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f21a0 -> FUN_065f51e0. Both null tests are retail.
    if (DeformInstance == nullptr || Out == nullptr)
        return GR_GENERIC;
    FIkListNode* Node = DeformInstance->ListHead;
    int* Cursor = Out;
    while (Node != nullptr)
    {
        *Cursor = (int)((ptrdiff_t)Node->Event);
        Cursor += 1;
        Node = Node->Next;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::WalkHardpoints(void* Instance, void* UserFn, void* UserData)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f21d0 -> FUN_065f59d0.
    if (DeformInstance == nullptr)
        return GR_GENERIC;
    DeformInstance->WalkHardpoints(UserFn, UserData);
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::GetMeshStats(void* Instance, unsigned int* Out)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f21f0 tests the instance, FUN_065f5ae0 Out (retail).
    if (DeformInstance == nullptr || Out == nullptr)
        return GR_GENERIC;
    if (DeformInstance->PartCount <= 0)
        return GR_GENERIC;
    Out[0] = (unsigned int)DeformInstance->PartCount;
    Out[1] = 0xFFFFFFFFu;
    Out[2] = 0;
    Out[3] = 0;
    Out[4] = 0;
    Out[5] = 0;
    Out[6] = 0;
    Out[7] = 0;
    Out[8] = 0;
    int Index = 0;
    while (Index < DeformInstance->PartCount)
    {
        FDeformPart* Part = &DeformInstance->PartArray[Index];
        const unsigned int Lod = (unsigned int)Part->LodIndex;
        if (Lod < Out[1])
            Out[1] = Lod;
        Out[6] += (unsigned int)Part->Lod->BindCursor;
        FDeformMesh* Mesh = Part->Current;
        if (Mesh != nullptr)
        {
            // The raw reads go past the end of a VMesh wrapper (0x14 bytes), as they do in
            // retail.
            Out[2] += (unsigned int)(Mesh->GetVertexCapacity());
            Out[3] += (unsigned int)(Mesh->GetIndexCount());
            const unsigned int* Raw = (const unsigned int*)Mesh;
            Out[4] += Raw[0x24 / 4];
            Out[5] += Raw[0x40 / 4];
            Out[7] += Raw[0x44 / 4];
            Out[8] += Raw[0x48 / 4];
        }
        Index += 1;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDeformable::GetBoundSphere(void* Instance, float* OutRadius,
                                                      float* OutCenter)
{
    FDeformInstance* DeformInstance = (FDeformInstance*)Instance;
    // LAB_065f2220 -> FUN_065f5bc0.
    if (DeformInstance == nullptr || DeformInstance->PartCount <= 0)
        return GR_GENERIC;

    float BestRadius = 0.0f;
    float FirstCenter[3];
    FirstCenter[0] = 0.0f;
    FirstCenter[1] = 0.0f;
    FirstCenter[2] = 0.0f;
    DeformInstance->PartArray[0].BoundSphere(&BestRadius, FirstCenter);

    int Index = 0;
    while (Index < DeformInstance->PartCount)
    {
        float Radius = 0.0f;
        float Center[3];
        Center[0] = 0.0f;
        Center[1] = 0.0f;
        Center[2] = 0.0f;
        DeformInstance->PartArray[Index].BoundSphere(&Radius, Center);
        const double Dx = (double)Center[0] - (double)FirstCenter[0];
        const double Dy = (double)Center[1] - (double)FirstCenter[1];
        const double Dz = (double)Center[2] - (double)FirstCenter[2];
        const double Reach = std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx) + (double)Radius;
        Radius = (float)Reach;
        if (Reach > (double)BestRadius)
            BestRadius = Radius;
        Index += 1;
    }
    if (!(BestRadius > 0.0f))
        return GR_GENERIC;
    OutCenter[0] = FirstCenter[0];
    OutCenter[1] = FirstCenter[1];
    OutCenter[2] = FirstCenter[2];
    *OutRadius = BestRadius;
    return GR_OK;
}

struct FDeformableComponentFactory : FDeformableFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        // FUN_065f2250.
        DisableThreadLibraryCalls(Instance);
        ICOManager* Dacom = DACOM_Acquire();

        FDeformableComponentFactory* Factory = new FDeformableComponentFactory();
        Factory->RefCount = 1;
        Factory->Name = G_DeformableComponentName;
        Dacom->RegisterComponent((IDAComponent*)Factory, G_DeformableComponentName,
                                 (void*)0x80000000);
        Factory->Release();
        Dacom->Release();
    }
    return TRUE;
}

int DACOM_API FDeformable::Initialize()
{
    return (int)(EngineInitialize(this));
}

static GENRESULT DACOM_API EngineUpdateOk(void* /*This*/, void* /*A*/, void* /*B*/)
{
    return GR_OK;
}

int DACOM_API FDeformable::CreateArchetype(void* Arg1, void* Arg2)
{
    return (int)(EngineUpdateOk(this, Arg1, Arg2));
}

int DACOM_API FDeformable::DuplicateArchetype(void* Arg1, void* Arg2)
{
    BoneRuntimeRetC(this, Arg1, Arg2);
    return 0;
}

int DACOM_API FDeformable::DestroyArchetype(void* Arg1)
{
    return (int)(BoneRuntimeRet8(this, Arg1));
}

static GENRESULT DACOM_API EngineStub10(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/)
{
    return GR_GENERIC;
}

int DACOM_API FDeformable::QueryArchetype(void* Arg1, const char* Iid, void** Out)
{
    // The callback ABI carries the const IID through an opaque mutable token.
    return (int)(EngineStub10(this, Arg1, const_cast<char*>(Iid), Out));
}

int DACOM_API FDeformable::CreateInstance(void* Arg1, void* Arg2)
{
    return (int)(EngineCallbackTrampoline(this, Arg1, Arg2));
}

int DACOM_API FDeformable::DestroyInstance(void* Arg1)
{
    return (int)(BoneRuntimeRet8(this, Arg1));
}

int DACOM_API FDeformable::UpdateInstance(void* Arg1, void* Arg2)
{
    BoneRuntimeRetC(this, Arg1, Arg2);
    return 0;
}

int DACOM_API FDeformable::QueryInstance(void* Arg1, const char* Iid, void** Out)
{
    // The callback ABI carries the const IID through an opaque mutable token.
    return (int)(EngineStub10(this, Arg1, const_cast<char*>(Iid), Out));
}

int DACOM_API FDeformable::Notify(void* Arg1)
{
    return (int)(BoneRuntimeRet8(this, Arg1));
}

// --- factory ---

GENRESULT DACOM_API FDeformableFactory::QueryInterface(const char* Iid, void** Out)
{
    if (Out != nullptr)
        *Out = nullptr;
    if (Iid == nullptr || Out == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    if (!Iids::Equal(Iid, Iids::ComponentFactory))
        return GR_INTERFACE_UNSUPPORTED;
    *Out = this;
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FDeformableFactory::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FDeformableFactory::Release()
{
    if (RefCount != 0)
        RefCount -= 1;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        RefCount = 1;
        // Shared factory base has no virtual destructor; derived tails are trivial.
        operator delete(this);
        return 0;
    }
    return Remaining;
}

GENRESULT DACOM_API FDeformableComponentFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    FDeformableFactory* This = this;
    // FUN_065f22f0. operator new(0x18) returns null on failure (VC6 MSVCRT), which retail tests.
    // No test of This, Desc, Out or either name.
    FAggDesc18* Extended = (FAggDesc18*)Desc;
    if (Extended->Size != 0x18 || std::strcmp(Extended->NameAt4, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FDeformableObject* Object =
        new (std::nothrow) FDeformableObject();
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    // FUN_065f1810 is FDeformable's constructor (it zeroes the module caches).
    Object->Inner.RefCount = 1;
    Object->Inner.Owner = Object;
    IDAComponent* const InnerUnknown = &Object->Inner;

    if (Extended->Outer == nullptr)
        Object->Controlling = InnerUnknown;
    else
    {
        // InnerOut is not tested here (only the failure path tests it).
        Object->Controlling = (IDAComponent*)Extended->Outer;
        *Extended->InnerOut = InnerUnknown;
    }

    const GENRESULT InitResult = ApplyCreateInstanceInit(Object, Extended);
    if (InitResult != GR_OK)
    {
        if (Extended->InnerOut != nullptr)
            *Extended->InnerOut = nullptr;
        delete Object;
        *Out = nullptr;
        return InitResult;
    }

    *Out = Object;
    return GR_OK;
}

// --- inner IDAComponent @ +0x0C (FDeformableInner: LAB_065f2400 / 065fe060 / 065f24c0) ---

// .rdata:066012d0: {IID string, offset from the owner}, null-terminated. An offset
// with bit 31 set would name a pointer member instead of an embedded facet; none here does.
struct FInnerIidEntry
{
    const char* Iid;
    int Offset;
};

static const FInnerIidEntry G_InnerIidTable[] = {
    {G_IidDeformableShort, 0},
    {G_IidEngineComponentShort, 4},
    {Iids::Deformable, 0},
    {Iids::EngineComponent, 4},
    {Iids::AggregateComponent, 4},
    {nullptr, 0},
};

// LAB_065f2400: walks the table with an inlined strcmp.
GENRESULT DACOM_API FDeformableInner::QueryInterface(const char* Iid, void** Out)
{
    int Index = 0;
    while (G_InnerIidTable[Index].Iid != nullptr)
    {
        if (std::strcmp(G_InnerIidTable[Index].Iid, Iid) == 0)
        {
            unsigned char* OwnerBytes = (unsigned char*)Owner;
            const int Offset = G_InnerIidTable[Index].Offset;
            IDAComponent* Facet = nullptr;
            if (Offset < 0)
                Facet = *(IDAComponent**)(OwnerBytes + (Offset & 0x7fffffff));
            else
                Facet = (IDAComponent*)(OwnerBytes + Offset);
            Facet->AddRef();
            *Out = Facet;
            return GR_OK;
        }
        Index += 1;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// LAB_065fe060 (ICF-shared).
unsigned long DACOM_API FDeformableInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// LAB_065f24c0; the owner goes through FUN_065f18d0 + operator delete.
unsigned long DACOM_API FDeformableInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    FDeformableObject* Object = Owner;
    if (Object != nullptr)
    {
        delete Object;
    }
    return 0;
}

// --- IDeformable @ +0: QI/AddRef/Release delegate to [this+8] (LAB_065f2500) ---

// LAB_065f2500/2510/2520: tail-jump to Controlling ([this+8]); no tests.
GENRESULT DACOM_API FDeformableObject::QueryInterface(const char* Iid, void** Out)
{
    IDAComponent* Controlling = this->Controlling;
    return Controlling->QueryInterface(Iid, Out);
}
