// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "x86math.h"
#include "EngBase.h"
#include "readfile.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>

static FEngineInstance* EngineInstanceInit(FEngineInstance* This, FArchetypeHandle* Handle);
static void ReplaceSearchPath(FEngineObject* This, IDAComponent* NewPath);
static void EngineComposeAncestors(FEngineInstance* Instance);

static FEngineInstance* EngineCreateInstanceTree(FEngineObject* This, FArchetypeHandle* Handle,
                                                IInstanceTransform* Transform);

static void EraseChildNodes(FEngineChildNode* First, FEngineChildNode* Last, unsigned long* Count);
static void InsertRootInstance(FEngineObject* Engine, FEngineInstance* Instance);
static void EraseRootInstance(FEngineObject* Engine, FEngineInstance* Instance);
static void DestroyChildNodeValue(FEngineChildNode* Node);
static unsigned long JointUtfRecordSize(unsigned long Type);
static unsigned long ApplyJointToLocal(FEngineInstance* Instance, unsigned char bForce);
static void ApplyRevJoint(float* Dest, const float* Rel, const float* JointState);
static void ApplyPrisJoint(float* Dest, const float* Rel, const float* JointState);
static void ApplyCylJoint(float* Dest, const float* Rel, const float* JointState);
static void ApplySphereJoint(float* Dest, const float* Rel, const float* JointState);
static void ApplyLooseJoint(float* Dest, const float* Rel, const float* JointState);

// FEngineObject holds a real std::map (the instance-root map), so only the facet offset is
// asserted; the retail byte layout is documented on the struct.
static_assert(offsetof(FEngineObject, SearchPath) == 0x04, "Engine.SearchPath");
static_assert(sizeof(FConstraintRecord) == 0x64, "constraint record 0x64");
static_assert(offsetof(FConstraintRecord, ChildPart) == 0x00, "constraint.ChildPart");
static_assert(offsetof(FConstraintRecord, ParentPart) == 0x04, "constraint.ParentPart");
static_assert(offsetof(FConstraintRecord, Payload) == 0x08, "constraint.Payload");
static_assert(sizeof(FArchetypeChild) == 0x48, "archetype child size");
static_assert(offsetof(FArchetypeChild, Crc) == 0x40, "child.Crc");
static_assert(offsetof(FArchetypeChild, Handle) == 0x44, "child.Handle");
static_assert(offsetof(FEngineChildNode, Name) == 0x08, "child node Name");
static_assert(offsetof(FEngineChildNode, Object) == 0x38, "child node Object");
static_assert(sizeof(FEngineChildNode) == 0x3C, "child node size");
static_assert(sizeof(FEngineInstance) == G_EngineInstanceSize, "instance size");
static_assert(offsetof(FEngineInstance, Archetype) == 0x08, "instance.Archetype");
static_assert(offsetof(FEngineInstance, ParentArchetype) == 0x0C, "instance.ParentArchetype");
static_assert(offsetof(FEngineInstance, Parent) == 0x10, "instance.Parent");
static_assert(offsetof(FEngineInstance, NextSibling) == 0x14, "instance.NextSibling");
static_assert(offsetof(FEngineInstance, FirstChild) == 0x1C, "instance.FirstChild");
static_assert(offsetof(FEngineInstance, LastChild) == 0x20, "instance.LastChild");
static_assert(offsetof(FEngineInstance, UserData) == 0x28, "instance.UserData");
static_assert(offsetof(FEngineInstance, Matrix) == 0x2C, "instance.Matrix");
static_assert(offsetof(FEngineInstance, Relative) == 0x5C, "instance.Relative");
static_assert(offsetof(FEngineInstance, JointState) == 0xB8, "instance.JointState");
static_assert(offsetof(FEngineInstance, bWorldDirty) == 0xD4, "instance.bWorldDirty");
static_assert(offsetof(FEngineInstance, PartChild) == 0xE8, "instance.PartChild");
static_assert(sizeof(FInstanceTransform) == G_TransformObjectSize, "transform size");
static_assert(offsetof(FInstanceTransform, Matrix) == 0x04, "transform.Matrix");
static_assert(offsetof(FInstanceTransform, Extra) == 0x34, "transform.Extra");
static_assert(offsetof(FInstanceTransform, User) == 0x44, "transform.User");

static void EngineUpdateInstanceTree(FEngineObject* This, FEngineInstance* Instance,
                                    unsigned long Flags, void* Arg);

static FArchetypeHandle* DACOM_API EngineCreateArchetypeHandle(FEngineObject* This,
                                                              FArchetypeName* Name,
                                                              void* FileSystem);

static FEngineInstance* DACOM_API EngineCreateInstance(FEngineObject* This,
                                                      FArchetypeHandle* Handle, void* Transform);

// FUN_0661ea00, on a block from FUN_0661e9e0 (free list DAT_0662b168).
FArchetypeHandle::FArchetypeHandle(const FArchetypeName* Source)
    : Name(Source->Text != nullptr ? Source->Text : "")
    , Crc(Source->Crc)
    , RefCount(0)
{
}

// FUN_0661eab0: ref at +0x14; every child of the vector at +0x1C (stride 0x48) goes through
// IEngine slot 14 with its +0x44 handle. No tests.
unsigned long FArchetypeHandle::AddRef(FEngineObject* Engine)
{
    RefCount += 1;
    for (std::vector<FArchetypeChild>::iterator Child = Children.begin();
         Child != Children.end(); ++Child)
        Engine->AddRefHandle(Child->Handle);
    return RefCount;
}

// FUN_0661eaf0: no floor on the decrement; children go through IEngine slot 15.
unsigned long FArchetypeHandle::Release(FEngineObject* Engine)
{
    RefCount -= 1;
    for (std::vector<FArchetypeChild>::iterator Child = Children.begin();
         Child != Children.end(); ++Child)
        Engine->ReleaseHandle(Child->Handle);
    return RefCount;
}

// FUN_0661eb30: the first child whose +0x40 equals the CRC. No tests.
FArchetypeChild* FArchetypeHandle::FindChildByCrc(unsigned long Crc)
{
    for (std::vector<FArchetypeChild>::iterator Child = Children.begin();
         Child != Children.end(); ++Child)
    {
        if (Child->Crc == Crc)
            return &*Child;
    }
    return nullptr;
}

static FEngBaseFreeList G_InstancePool = {sizeof(FEngineInstance), nullptr, 0x40, 0};         // DAT_0662b158

// FUN_0661eb60 (pool DAT_0662b158, 0xEC) + FUN_0661eb80 (ctor). A null allocation yields 0,
// which retail then dereferences -- no failure branch.
static FEngineInstance* AllocEngineInstance(FArchetypeHandle* Handle)
{
    FEngineInstance* Instance = (FEngineInstance*)(G_InstancePool.Alloc());
    if (Instance != nullptr)
        EngineInstanceInit(Instance, Handle);
    return Instance;
}

// Packed 3x3 orientation followed by translation (12 floats).
static float G_IdentityMatrix[12] = {
    1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};

static void InitIdentity3x4(float* Matrix)
{
    std::memcpy(Matrix, G_IdentityMatrix, sizeof(G_IdentityMatrix));
}

static void InitInstanceMatrix(FEngineInstance* Instance)
{
    InitIdentity3x4(Instance->Matrix);
}

// FUN_0661eb80.
static FEngineInstance* EngineInstanceInit(FEngineInstance* This, FArchetypeHandle* Handle)
{
    if (This == nullptr)
        return nullptr;
    std::memset(This, 0, sizeof(FEngineInstance));
    This->Archetype = Handle;
    This->Extent = -1.0f;
    This->bWorldDirty = 1;
    This->JointState[3] = 1.0f;
    This->Relative[0] = 0xFFFFFFFF;
    InitInstanceMatrix(This);
    return This;
}

// FUN_0661ec30 (thiscall, this = PARENT).
unsigned char FEngineInstance::UnlinkChild(FEngineInstance* Node)
{
    if (Node->PrevSibling == nullptr)
        FirstChild = Node->NextSibling;
    else
        Node->PrevSibling->NextSibling = Node->NextSibling;
    if (Node->NextSibling == nullptr)
        LastChild = Node->PrevSibling;
    else
        Node->NextSibling->PrevSibling = Node->PrevSibling;
    ChildCount -= 1;
    Node->Parent = nullptr;
    Node->NextSibling = nullptr;
    Node->PrevSibling = nullptr;
    return 1;
}

// FUN_0661ec90: copy 0x17 dwords from constraint+8 to inst+0x5C, then link as the last child.
unsigned char FEngineInstance::ConnectChild(FEngineInstance* Child, const unsigned long* Payload)
{
    Child->bWorldDirty = 1;
    std::memcpy(Child->Relative, Payload, sizeof(Child->Relative));
    FEngineInstance* Last = LastChild;
    LastChild = Child;
    if (Last == nullptr)
        FirstChild = Child;
    else
        Last->NextSibling = Child;
    Child->NextSibling = nullptr;
    Child->PrevSibling = Last;
    ChildCount += 1;
    Child->Parent = this;
    InvalidateExtentChain(1);
    return 1;
}

// FUN_0661ece0 (only caller IEngine slot 33).
void FEngineInstance::SetExtentAndInvalidate(float Extent, const float* Vec, unsigned char bXform)
{
    if (bXform != 0)
    {
        Transform->SetExtra(this, Extent, Vec);
        InvalidateExtentChain(bXform);
        return;
    }
    this->Extent = Extent;
    ExtentVec[0] = Vec[0];
    ExtentVec[1] = Vec[1];
    ExtentVec[2] = Vec[2];
    InvalidateExtentChain(0);
}

// FUN_0661ed40: the transform at +4, slot 9. No checks.
void FEngineInstance::TransformExtra(float* OutExtent, float* OutVec)
{
    Transform->GetExtra(this, OutExtent, OutVec);
}

// FUN_0661ed60: walk up the parents (+0x10), writing -1 to the extent (+0xD8) until one already
// holds -1 (a dword compare). No test of this.
unsigned char FEngineInstance::InvalidateExtentChain(unsigned char bFromThis)
{
    FEngineInstance* Cursor = this;
    if (bFromThis == 0)
        Cursor = Cursor->Parent;
    while (Cursor != nullptr)
    {
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Cursor->Extent, sizeof(Bits));
        if (Bits == 0xBF800000)
            break;
        Cursor->Extent = -1.0f;
        Cursor = Cursor->Parent;
    }
    return 1;
}

// Distance on the FPU as FUN_0661ed90 does it: the three differences stay unrounded and the
// squares are summed z, y, x.
static double ExtentDistance(const float* A, const float* B)
{
    const double Dx = (double)(A[0]) - (double)(B[0]);
    const double Dy = (double)(A[1]) - (double)(B[1]);
    const double Dz = (double)(A[2]) - (double)(B[2]);
    return std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx);
}

// FUN_0661ed90: the bounding sphere of the instance and its children, cached in +0xD8.
void FEngineInstance::ComputeInstanceExtent(unsigned char bRecursive, float* OutExtent,
                                            float* OutVec)
{
    if (bRecursive == 0)
    {
        TransformExtra(OutExtent, OutVec);
        return;
    }
    if (Extent > 0.0f)
    {
        *OutExtent = Extent;
        OutVec[0] = ExtentVec[0];
        OutVec[1] = ExtentVec[1];
        OutVec[2] = ExtentVec[2];
        return;
    }

    float* WorldXf = Transform->GetMatrix(this);
    float OwnR = 0.0f;
    float OwnLocal[3];
    TransformExtra(&OwnR, OwnLocal);
    float OwnWorld[3];
    Acquire3DMathEngine()->TransformXform(*(FVector*)OwnWorld,
                                                  *(const FTransform*)WorldXf,
                                                  *(const FVector*)OwnLocal);

    float MinV[3];
    float MaxV[3];
    for (unsigned long Axis = 0; Axis < 3; ++Axis)
    {
        MinV[Axis] = OwnWorld[Axis] - OwnR;
        MaxV[Axis] = OwnWorld[Axis] + OwnR;
    }

    FEngineInstance* Child = FirstChild;
    while (Child != nullptr)
    {
        float* ChildXf = Child->Transform->GetMatrix(Child);
        float ChildR = 0.0f;
        float ChildLocal[3];
        Child->ComputeInstanceExtent(1, &ChildR, ChildLocal);
        float ChildWorld[3];
        Acquire3DMathEngine()->TransformXform(*(FVector*)ChildWorld,
                                                      *(const FTransform*)ChildXf,
                                                      *(const FVector*)ChildLocal);
        for (unsigned long Axis = 0; Axis < 3; ++Axis)
        {
            const float Lo = ChildWorld[Axis] - ChildR;
            if (Lo < MinV[Axis])
                MinV[Axis] = Lo;
            const float Hi = ChildWorld[Axis] + ChildR;
            if (MaxV[Axis] < Hi)
                MaxV[Axis] = Hi;
        }
        Child = Child->NextSibling;
    }

    float CenterW[3];
    CenterW[0] = (MaxV[0] + MinV[0]) * 0.5f;
    CenterW[1] = (MaxV[1] + MinV[1]) * 0.5f;
    CenterW[2] = (MaxV[2] + MinV[2]) * 0.5f;
    const float SpanZ = MaxV[2] - MinV[2];
    const float SpanY = MaxV[1] - MinV[1];
    const float SpanX = MaxV[0] - MinV[0];
    float Widest = SpanX < SpanY ? SpanY : SpanX;
    if (Widest < SpanZ)
        Widest = SpanZ;
    float Radius = Widest * 0.5f;
    const double OwnSphere = ExtentDistance(CenterW, OwnWorld) + (double)OwnR;
    if (OwnSphere > (double)Radius)
        Radius = (float)OwnSphere;

    Child = FirstChild;
    while (Child != nullptr)
    {
        float* ChildXf = Child->Transform->GetMatrix(Child);
        float ChildR = 0.0f;
        float ChildLocal[3];
        Child->ComputeInstanceExtent(1, &ChildR, ChildLocal);
        float ChildWorld[3];
        Acquire3DMathEngine()->TransformXform(*(FVector*)ChildWorld,
                                                      *(const FTransform*)ChildXf,
                                                      *(const FVector*)ChildLocal);
        const float ChildSphere =
            (float)(ExtentDistance(ChildWorld, CenterW) + (double)ChildR);
        if (ChildSphere > Radius)
            Radius = ChildSphere;
        Child = Child->NextSibling;
    }

    float LocalCenter[3];
    Acquire3DMathEngine()->InverseTransform(*(FVector*)LocalCenter,
                                                    *(const FTransform*)WorldXf,
                                                    *(const FVector*)CenterW);
    Extent = Radius;
    ExtentVec[0] = LocalCenter[0];
    ExtentVec[1] = LocalCenter[1];
    ExtentVec[2] = LocalCenter[2];
    *OutExtent = Radius;
    OutVec[0] = LocalCenter[0];
    OutVec[1] = LocalCenter[1];
    OutVec[2] = LocalCenter[2];
}

// The transform's vtable +0x34 with the instance; its AL decides the FUN_0661f210 recursion.
static unsigned char MarkInstanceWorldDirty(FEngineInstance* Instance)
{
    return (unsigned char)Instance->Transform->MarkWorldDirty(Instance);
}

// FUN_0661f210. No tests.
static void EngineWalkDirtyChildren(FEngineInstance* Instance)
{
    for (FEngineInstance* Child = Instance->FirstChild; Child != nullptr; Child = Child->NextSibling)
    {
        if (MarkInstanceWorldDirty(Child) != 0)
            EngineWalkDirtyChildren(Child);
    }
}

// DAT_0662b178: FUN_0661f250's own lazily created 3DMathEngine.
static I3DMathEngine* G_ComposeMath = nullptr;

// FUN_0661f250, in retail order.
unsigned char FEngineInstance::ComposeWorld()
{
    if (Parent == nullptr)
        return 0;
    const unsigned long bApplied = ApplyJointToLocal(this, 0);
    const float* ParentWorld = Parent->Transform->GetMatrix(Parent);
    if (G_ComposeMath == nullptr)
        G_ComposeMath = Create3DMathEngine();
    float Composed[12];
    G_ComposeMath->MulTransform(*(FTransform*)Composed, // +0x1C
                                        *(const FTransform*)ParentWorld,
                                        *(const FTransform*)(Matrix));
    float Copy[12];
    std::memcpy(Copy, Composed, sizeof(Copy));
    Transform->SetMatrix(this, Copy);
    if ((bApplied & 0xFF) != 0)
    {
        InvalidateExtentChain(1);
        if (MarkInstanceWorldDirty(this) != 0)
            EngineWalkDirtyChildren(this);
    }
    return 0;
}

static FEngBaseFreeList G_TransformPool = {sizeof(FInstanceTransform), nullptr, 0x40, 0};     // DAT_0662b114

// FUN_0661f2f0 (free list DAT_0662b114) + FUN_0661f310 (ctor: +0x44 = 0, vtable 066295d0).
// The rest of the block is left as the allocator hands it out (slot 0 Init fills it).
static FInstanceTransform* AllocInstanceTransform()
{
    void* Block = G_TransformPool.Alloc();
    if (Block == nullptr)
        return nullptr;
    return new (Block) FInstanceTransform;
}

// LAB_0661f320 (slot 13): returns 0.
unsigned long DACOM_API FInstanceTransform::MarkWorldDirty(void* Instance)
{
    (void)Instance;
    return 0;
}

// FUN_0661f330 (dtor: vtable back to 066295d0) + FUN_0661f300 (block back to DAT_0662b114).
void FInstanceTransform::Free()
{
    this->~FInstanceTransform();
    G_TransformPool.Free(this);
}

// Engine-owned transform.

// LAB_0661f340 (slot 0). +0x44 (user) is NOT touched.
GENRESULT DACOM_API FInstanceTransform::Init(void* Instance)
{
    (void)Instance;
    InitIdentity3x4(Matrix);
    Extra = 0.0f;
    ExtraVec[0] = 0.0f;
    ExtraVec[1] = 0.0f;
    ExtraVec[2] = 0.0f;
    return GR_OK;
}

// LAB_06612160 (slots 1 and 2): no body.
GENRESULT DACOM_API FInstanceTransform::AddRef(void* Instance)
{
    (void)Instance;
    return GR_OK;
}

GENRESULT DACOM_API FInstanceTransform::Release(void* Instance)
{
    (void)Instance;
    return GR_OK;
}

// LAB_0661f380 (slot 3): +0x28.
GENRESULT DACOM_API FInstanceTransform::SetPosition(void* Instance, const float* Position)
{
    (void)Instance;
    Matrix[9] = Position[0];
    Matrix[10] = Position[1];
    Matrix[11] = Position[2];
    return GR_OK;
}

// LAB_0661f3a0 (slot 4): +0x28.
float* DACOM_API FInstanceTransform::GetPosition(void* Instance)
{
    (void)Instance;
    return Matrix + 9;
}

// LAB_0661f3b0 (slot 5): nine dwords to +4.
GENRESULT DACOM_API FInstanceTransform::SetOrientation(void* Instance, const float* Rows)
{
    (void)Instance;
    std::memcpy(Matrix, Rows, 9 * sizeof(float));
    return GR_OK;
}

// LAB_0661f3d0 (slots 6 and 8): +4.
float* DACOM_API FInstanceTransform::GetOrientation(void* Instance)
{
    (void)Instance;
    return Matrix;
}

float* DACOM_API FInstanceTransform::GetMatrix(void* Instance)
{
    (void)Instance;
    return Matrix;
}

// LAB_0661f3e0 (slot 7): twelve dwords to +4.
GENRESULT DACOM_API FInstanceTransform::SetMatrix(void* Instance, const float* NewMatrix)
{
    (void)Instance;
    std::memcpy(Matrix, NewMatrix, 12 * sizeof(float));
    return GR_OK;
}

// LAB_0661f400 (slot 9): +0x34, +0x38..+0x40.
GENRESULT DACOM_API FInstanceTransform::GetExtra(void* Instance, float* OutExtra, float* OutVec)
{
    (void)Instance;
    *OutExtra = Extra;
    OutVec[0] = ExtraVec[0];
    OutVec[1] = ExtraVec[1];
    OutVec[2] = ExtraVec[2];
    return GR_OK;
}

// LAB_0661f430 (slot 10).
GENRESULT DACOM_API FInstanceTransform::SetExtra(void* Instance, float NewExtra, const float* Vec)
{
    (void)Instance;
    Extra = NewExtra;
    ExtraVec[0] = Vec[0];
    ExtraVec[1] = Vec[1];
    ExtraVec[2] = Vec[2];
    return GR_OK;
}

// LAB_0661f460 (slot 11).
GENRESULT DACOM_API FInstanceTransform::SetUser(void* Instance, unsigned long NewUser)
{
    (void)Instance;
    User = NewUser;
    return GR_OK;
}

// LAB_0661f470 (slot 12).
unsigned long DACOM_API FInstanceTransform::GetUser(void* Instance)
{
    (void)Instance;
    return User;
}

// The list sentinel the ctor FUN_0661f550 builds for +0x48 and +0x54 (operator new(0x3c)).
// No null test.
static FEngineChildNode* CreateListSentinel()
{
    FEngineChildNode* Sentinel =
        new FEngineChildNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    return Sentinel;
}

// FUN_0661f550, in retail order.
void FEngineObject::Construct()
{
    new (this) FEngineObject();
    SearchPath = nullptr;
    InstanceLookupCount = 0;
    InstanceCacheHits = 0;
    bInstanceCacheValid = 0;
    ComponentList = CreateListSentinel();
    ComponentCount = 0;
    EngineComponentList = CreateListSentinel();
    EngineComponentCount = 0;
    bInitialized = 0;
}

// ---- Fixed-size free lists (see FEngBaseFreeList in EngBase.h) ----
// Element sizes 0x38 / 0xEC / 0x48 (static_asserts above), limit 0x40 and the empty list are
// the values LAB_0661f6a0 / LAB_0661f6e0 / LAB_0661f720 store.
static FEngBaseFreeList G_ArchetypeHandlePool = {sizeof(FArchetypeHandle), nullptr, 0x40, 0}; // DAT_0662b168

// FUN_0661f750, in retail order (including the repeated erase of the engine-component list).
void FEngineObject::Destruct()
{
    G_ArchetypeHandlePool.Drain();
    G_InstancePool.Drain();
    G_TransformPool.Drain();
    if (SearchPath != nullptr)
    {
        SearchPath->Release();
        SearchPath = nullptr;
    }
    bInstanceCacheValid = 0;
    Instances.clear();
    Archetypes.clear();

    FEngineChildNode* Sentinel = ComponentList;
    FEngineChildNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        Node->Object->Release();
        FEngineChildNode* Next = Node->Next;
        Node->Prev->Next = Next;
        Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        ComponentCount -= 1;
        Node = Next;
    }

    EraseChildNodes(EngineComponentList->Next, EngineComponentList,
                    &EngineComponentCount);
    bInitialized = 0;
    EraseChildNodes(EngineComponentList->Next, EngineComponentList,
                    &EngineComponentCount);
    delete EngineComponentList;
    EngineComponentList = nullptr;
    EngineComponentCount = 0;
    EraseChildNodes(ComponentList->Next, ComponentList, &ComponentCount);
    delete ComponentList;
    ComponentList = nullptr;
    ComponentCount = 0;
    this->~FEngineObject();
}

FArchetypeHandle* const G_InvalidArchetype =
    (FArchetypeHandle*)G_InvalidEngineHandle;

// FUN_0661f8b0: find(Crc) in the map at +0x30 (lower_bound FUN_0661a260).
FArchetypeHandle* FEngineObject::LookupArchetype(FArchetypeName* Name)
{
    if (Name->Crc == 0)
        return G_InvalidArchetype;
    FArchetypeMap::iterator It = Archetypes.find(Name->Crc);
    if (It == Archetypes.end())
        return G_InvalidArchetype;
    return It->second;
}

// .data:0662a5c8 (from the PE bytes): the __FILE__ of every FDUMP site in engine.cpp
// (0661f96a..06622a5d).
static const char* const G_FdumpFileEngine =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Engine\\engine.cpp";

// .data:0662a51c / .data:0662a510 (PE bytes): the status word of the load_engine_components
// aggregation NOTICE (0661fb18 / 0661fb6c).
static const char* const G_StatusOk = "[OK]";
static const char* const G_StatusFailed = "[FAILED]";

// Node names occupy 0x30 bytes; preserve the existing bounded-copy policy.
static void BuildNodeName(char* Buffer, const char* Source)
{
    if (Source == nullptr)
    {
        Buffer[0] = '\0';
        return;
    }
    const size_t Length = std::strlen(Source) + 1;
    std::memcpy(Buffer, Source, Length < 0x30 ? Length : 0x30);
}

// Retail strtok on DAT_0662a524 = " \t". First token = name; token starting with '=' then next = value.
static void ParseEngineProfileLine(char* Line, char** NameOut, const char** ValueOut)
{
    *NameOut = nullptr;
    *ValueOut = nullptr;
    char* Name = nullptr;
    const char* Value = nullptr;
    unsigned char bSawEquals = 0;
    char* Token = std::strtok(Line, " \t");
    while (Token != nullptr)
    {
        if (Token[0] == ';')
            break;
        if (Name == nullptr)
            Name = Token;
        else if (bSawEquals != 0)
            Value = Token;
        else if (Token[0] == '=')
            bSawEquals = 1;
        Token = std::strtok(nullptr, " \t");
    }
    if (Name != nullptr)
    {
        char* Space = std::strchr(Name, ' ');
        if (Space != nullptr)
            *Space = '\0';
        char* Tab = std::strchr(Name, '\t');
        if (Tab != nullptr)
            *Tab = '\0';
    }
    *NameOut = Name;
    *ValueOut = Value;
}

// LAB_0661f930 / IEngine +0x0C (RET 8). The engine's own refcount is never touched here.
GENRESULT DACOM_API FEngineObject::LoadEngineComponents(IDAComponent* SystemContainer)
{
    if (bInitialized != 0)
        return GR_OK;

    if (SystemContainer == nullptr)
    {
        EngBaseFdump(kFdumpNotice, G_FdumpFileEngine, 0x1A3, "NOTICE",
                     "Engine: load_engine_components: system parameter is NULL, continuing with "
                     "complete faith in the client...");
    }

    ICOManager* Dacom = DACOM_Acquire();
    IProfileParser* Parser = nullptr;
    if (Dacom->QueryInterface(Iids::ProfileParser, (void**)(&Parser)) < 0)
    {
        EngBaseFdump(kFdumpError, G_FdumpFileEngine, 0x1AF, "ERROR",
                     "Engine: load_engine_components: unable to acquire IID_IProfileParser");
        if (Parser != nullptr)
            Parser->Release();
        return GR_GENERIC;
    }

    void* Section = Parser->OpenSection(G_NameEngine, 3);
    if (Section == nullptr)
    {
        bInitialized = 1;
        if (Parser != nullptr)
            Parser->Release();
        return GR_OK;
    }

    char Line[0x100];
    char NodeName[0x30];
    int LineIndex = 0;
    while (Parser->ReadLine(Section, LineIndex++, Line, sizeof(Line)) != 0)
    {
        char* Name = nullptr;
        const char* Value = nullptr;
        ParseEngineProfileLine(Line, &Name, &Value);
        if (Name == nullptr)
            continue;

        void* OuterInner = nullptr;
        void* Created = nullptr;
        FAggDesc18 Desc;
        Desc.Size = G_AggDescSizeAnimation;
        Desc.Name = Name;
        Desc.Inner = this;
        Desc.Outer = &OuterInner;
        Desc.Value = Value;
        Desc.SystemContainer = SystemContainer;

        const char* Status = G_StatusFailed;
        if (Dacom->CreateInstance((AGGDESC*)(&Desc), &Created) >= 0)
        {
            Status = G_StatusOk;
            BuildNodeName(NodeName, Name);
            ComponentList->Insert(&ComponentCount, NodeName,
                                        (IDAComponent*)OuterInner);
        }

        char Notice[0x1FF0];
        std::sprintf(Notice, "ENGINE::load_engine_components: aggregation of '%s' [%s] returned %s\n",
                     Name, Value != nullptr ? Value : "", Status);
        EngBaseFdump(kFdumpNotice, G_FdumpFileEngine, 0x1FC, "NOTICE", Notice);
    }

    // Pass 2. Aggregate is a smart pointer: released before every QI and once after the walk.
    FEngineChildNode* Sentinel = ComponentList;
    IAggregateComponent* Aggregate = nullptr;
    FEngineChildNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        if (Aggregate != nullptr)
        {
            Aggregate->Release();
            Aggregate = nullptr;
        }
        if (Node->Object->QueryInterface(Iids::AggregateComponent,
                                         (void**)(&Aggregate)) >= 0 &&
            Aggregate->Initialize() >= 0)
        {
            Node = Node->Next;
            continue;
        }

        char Warning[0x1FF0];
        std::sprintf(Warning,
                     "ENGINE::load_engine_components: initialization of '%s' failed, removing "
                     "from aggregate\n",
                     Node->Name);
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x224, "WARNING", Warning);
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        ComponentCount -= 1;
        Node = ComponentList->Next;
    }
    if (Aggregate != nullptr)
    {
        Aggregate->Release();
        Aggregate = nullptr;
    }

    // Pass 3.
    Sentinel = ComponentList;
    for (Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        IDAComponent* EngineComponent = nullptr;
        if (Node->Object->QueryInterface(Iids::EngineComponent,
                                         (void**)(&EngineComponent)) < 0)
            continue;
        EngineComponent->Release();
        BuildNodeName(NodeName, Node->Name);
        EngineComponentList->Insert(&EngineComponentCount, NodeName,
                                          EngineComponent);
    }

    bInitialized = 1;
    if (Parser != nullptr)
        Parser->Release();
    return GR_OK;
}

// LAB_0661fda0 (IEngine +0x10): only the callback's low byte is tested.
unsigned long DACOM_API FEngineObject::EnumerateEngineComponents(
    int(__cdecl* Callback)(const char*, void*))
{
    if (Callback != nullptr)
    {
        FEngineChildNode* Sentinel = EngineComponentList;
        for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            if ((unsigned char)(Callback(Node->Name, Node->Object)) == 0)
                break;
        }
    }
    return EngineComponentCount;
}

// LAB_0661fdf0 (IEngine +0x18).
GENRESULT DACOM_API FEngineObject::SetSearchPath(const char* Path)
{
    IDAComponent* Created = nullptr;
    if (Path == nullptr)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x270, "WARNING",
                     "Engine: set_search_path: path is NULL");
    }
    else
    {
        FAggDesc8 Desc;
        Desc.Size = G_AggDescSizeSearchPath;
        Desc.Name = G_NameSearchPath;
        if (DACOM_Acquire()->CreateInstance((AGGDESC*)(&Desc),
                                            (void**)(&Created)) >= 0)
        {
            ((ISearchPath*)((void*)Created))->SetPath(Path);
            ReplaceSearchPath(this, Created);
            if (Created != nullptr)
                return (GENRESULT)(Created->Release());
            return GR_OK;
        }
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x275, "WARNING",
                     "Engine: set_search_path: unable to create search path");
    }
    if (Created != nullptr)
        return (GENRESULT)(Created->Release());
    return GR_OK;
}

// The smart-pointer assignment into +4 that FUN_0661fee0, LAB_0661fdf0 and LAB_0661ffc0 inline.
static void ReplaceSearchPath(FEngineObject* This, IDAComponent* NewPath)
{
    if (This->SearchPath == NewPath)
        return;
    if (This->SearchPath != nullptr)
    {
        This->SearchPath->Release();
        This->SearchPath = nullptr;
    }
    This->SearchPath = NewPath;
    if (NewPath != nullptr)
        NewPath->AddRef();
}

// FUN_0661fee0 (IEngine +0x20). Retail returns whatever EAX the last AddRef / Release left (or
// the old pointer); the port returns 0.
GENRESULT DACOM_API FEngineObject::SetFilesystem(IDAComponent* NewPath)
{
    ReplaceSearchPath(this, NewPath);
    return GR_OK;
}

// FUN_0661ff20 (IEngine +0x1C).
GENRESULT DACOM_API FEngineObject::QuerySearchPath(char* Dest, unsigned long MaxChars)
{
    ISearchPath* SearchPathIface = nullptr;
    if (SearchPath != nullptr &&
        SearchPath->QueryInterface(Iids::SearchPath,
                                         (void**)(&SearchPathIface)) >= 0)
    {
        const GENRESULT Result = SearchPathIface->GetPath(Dest, MaxChars);
        if (SearchPathIface != nullptr)
            SearchPathIface->Release();
        return Result;
    }
    *Dest = 0;
    if (SearchPathIface != nullptr)
        SearchPathIface->Release();
    return GR_OK;
}

// LAB_0661ff90 (IEngine+0x24). No checks.
GENRESULT DACOM_API FEngineObject::GetSearchPath(void** Out)
{
    *Out = SearchPath;
    if (*Out == nullptr)
        *Out = DACOM_Acquire();
    return (GENRESULT)(((IDAComponent*)(*Out))->AddRef());
}

// The 0x34 FileSystem descriptor every caller builds inline (LAB_0661ffc0, FUN_066201b0,
// FUN_06611840, ...).
void FillFileSystemDesc(FEngBaseFileSystemDesc* Desc, const char* Path, const char* Backend)
{
    std::memset(Desc, 0, sizeof(*Desc));
    Desc->Size = 0x34;
    Desc->ComponentName = G_NameFileSystem;
    Desc->BackendName = Backend;
    Desc->DataPath = Path;
    Desc->Access = 0x80000000;
    Desc->ShareMode = 1;
    Desc->Creation = 3;
    Desc->Flags = 0x8000080;
    Desc->FindHandle = 0xFFFFFFFF;
}

// Retail tests only these two forms.
static bool IsAbsoluteFilePath(const char* Path)
{
    if (Path[1] == ':')
        return true;
    if (Path[0] == '\\' && Path[1] == '\\')
        return true;
    return false;
}

// LAB_0661ffc0 (IEngine +0x28).
GENRESULT DACOM_API FEngineObject::CreateFileSystem(const char* Path, void** Out, void* Factory)
{
    if (Path == nullptr || Out == nullptr)
        return GR_INVALID_PARMS;

    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Path, nullptr);

    // Every candidate is called through vtable +0xC (CreateInstance): a component factory,
    // the COManager, or whatever +4 holds.
    IComponentFactory* Creator = (IComponentFactory*)Factory;
    if (Creator == nullptr)
    {
        if (IsAbsoluteFilePath(Path))
        {
            Creator = (IComponentFactory*)((void*)(DACOM_Acquire()));
        }
        else
        {
            if (SearchPath == nullptr)
                ReplaceSearchPath(this, DACOM_Acquire());
            Creator = (IComponentFactory*)((void*)(SearchPath));
        }
    }
    return Creator->CreateInstance((AGGDESC*)(&Desc), Out);
}

static const char* const G_AnonymousFormat = "anonymous-%d";
static unsigned long G_AnonymousIndex = 0;

// LAB_066200c0 (IEngine +0x2C).
void* DACOM_API FEngineObject::CreateCompoundArchetype(FEngineName* InName)
{
    FArchetypeName* Name = InName;
    DACOM_CRC::GetCRC32(nullptr);
    FArchetypeName Local;
    char Anonymous[0x100];
    if (Name->Crc != 0)
    {
        Local.Text = Name->Text;
        Local.Crc = Name->Crc;
    }
    else
    {
        Local.Text = Anonymous;
        do
        {
            wsprintfA(Anonymous, G_AnonymousFormat, G_AnonymousIndex++);
            Local.Crc = DACOM_CRC::GetCRC32(Anonymous);
        } while (LookupArchetype(&Local) != G_InvalidArchetype);
        Local.Crc = DACOM_CRC::GetCRC32(Anonymous);
    }

    void* Block = G_ArchetypeHandlePool.Alloc();
    FArchetypeHandle* Handle = nullptr;
    if (Block != nullptr)
        Handle = new (Block) FArchetypeHandle(&Local);
    Archetypes.insert(FArchetypeMap::value_type(Local.Crc, Handle));
    AddRefHandle(Handle);
    return Handle;
}

// The resize(n) back to the size before a failed FUN_066201b0 (erase, FUN_066230f0).
static void ShrinkArchetypeChildren(FArchetypeHandle* Handle, std::size_t Count)
{
    Handle->Children.erase(Handle->Children.begin() + Count, Handle->Children.end());
}

// The "Object name" / "File name" reads of FUN_066201b0, capped at 0x3F bytes.
static bool ReadPartNameField(IFileSystem* Fs, const char* Field, char* Buffer)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Field, nullptr);
    const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (Handle == -1)
        return false;
    unsigned long Size = Fs->GetSize((unsigned long)Handle, 0); // +0x28
    if (!(Size < 0x3F))
        Size = 0x3F;
    bool bRead = false;
    unsigned long BytesRead = 0;
    if (Fs->Read((unsigned long)Handle, Buffer, Size, &BytesRead, 0) != 0 && // +0x14
        BytesRead == Size)
    {
        bRead = true;
        Buffer[BytesRead] = '\0';
    }
    Fs->CloseFile((unsigned long)Handle); // +0x10
    return bRead;
}

// FUN_066201b0. Retries CreateFileSystem without the parent FS when the first attempt fails.
static GENRESULT LinkCmpndPartHandle(FEngineObject* Engine, const char* Name, void* CmpndFs,
                                    unsigned long FindHandle, void* ParentFs, FArchetypeChild* Child)
{
    FEngBaseFileSystemDesc PartDesc;
    FillFileSystemDesc(&PartDesc, Name, nullptr);
    PartDesc.FindHandle = FindHandle;
    void* FileFs = nullptr;
    void* PartFs = nullptr;
    GENRESULT ObjectResult = (GENRESULT)0xFFFFFFFF;
    GENRESULT FileResult = (GENRESULT)0xFFFFFFFF;
    char FileName[0x40];
    if (AsFileSystem(CmpndFs)->CreateInstance((AGGDESC*)(&PartDesc), &PartFs) == 0)
    {
        IFileSystem* Fs = AsFileSystem(PartFs);
        ObjectResult = ReadPartNameField(Fs, G_NameObjectName, Child->Name) ? GR_OK : GR_E_FAIL;
        FileResult = ReadPartNameField(Fs, G_NameFileName, FileName) ? GR_OK : GR_E_FAIL;
    }
    if (PartFs != nullptr)
    {
        ((IDAComponent*)PartFs)->Release();
        PartFs = nullptr;
    }
    if (ObjectResult < 0 || FileResult < 0)
        return GR_E_FAIL;

    if (Engine->CreateFileSystem(FileName, &FileFs, ParentFs) != 0) // +0x28
    {
        if (FileFs != nullptr)
        {
            ((IDAComponent*)FileFs)->Release();
            FileFs = nullptr;
        }
        if (Engine->CreateFileSystem(FileName, &FileFs, nullptr) != 0) // +0x28
        {
            if (FileFs != nullptr)
                ((IDAComponent*)FileFs)->Release();
            return GR_E_FAIL;
        }
    }

    FArchetypeName Nested;
    Nested.Text = FileName;
    Nested.Crc = DACOM_CRC::GetCRC32(FileName);
    void* PartHandle = Engine->CreateArchetypeHandle(&Nested, FileFs); // +0x30
    if (PartHandle == G_InvalidArchetype)
    {
        if (FileFs != nullptr)
            ((IDAComponent*)FileFs)->Release();
        return GR_E_FAIL;
    }
    Child->Handle = (FArchetypeHandle*)PartHandle;
    Child->Crc = DACOM_CRC::GetCRC32(Child->Name);
    if (FileFs != nullptr)
        ((IDAComponent*)FileFs)->Release();
    return GR_OK;
}

// The child vector's resize(size + 1, element) in FUN_06620500.
static FArchetypeChild* AppendArchetypeChild(FArchetypeHandle* Handle)
{
    const std::size_t Count = Handle->Children.size();
    Handle->Children.resize(Count + 1, FArchetypeChild());
    return Handle->Children.data() + Count;
}

static const unsigned long G_JointUtfNameBytes = 0x40;
static const unsigned long G_JointUtfHeaderBytes = 0x80;
static const unsigned long G_JointUtfMaxBytes = 0xD8;

static const unsigned long G_JointTypeFix = 0;
static const unsigned long G_JointTypePris = 1;
static const unsigned long G_JointTypeRev = 2;
static const unsigned long G_JointTypeCyl = 3;
static const unsigned long G_JointTypeSphere = 4;
static const unsigned long G_JointTypeTrans = 5;
static const unsigned long G_JointTypeLoose = 6;

static void CopyPayloadDwords(unsigned long* Dest, const unsigned long* Source,
                              unsigned long Count)
{
    unsigned long Index = 0;
    while (Index < Count)
    {
        Dest[Index] = Source[Index];
        Index += 1;
    }
}

// Decode the type-specific joint payload from the UTF record.
static void RemapJointPayload(unsigned long Type, const unsigned char* UtfExtra,
                              unsigned long* Payload)
{
    const unsigned long* Source = (const unsigned long*)UtfExtra;
    switch (Type)
    {
    case G_JointTypeFix:
    case G_JointTypeTrans:
    case G_JointTypeLoose:
        CopyPayloadDwords(Payload + 4, Source, 12);
        break;
    case G_JointTypePris:
    case G_JointTypeRev:
        CopyPayloadDwords(Payload + 1, Source, 20);
        break;
    case G_JointTypeCyl:
        CopyPayloadDwords(Payload + 1, Source, 22);
        break;
    case G_JointTypeSphere:
        CopyPayloadDwords(Payload + 1, Source, 15);
        Payload[16] = Source[19];
        Payload[17] = Source[20];
        CopyPayloadDwords(Payload + 19, Source + 15, 4);
        break;
    default:
        break;
    }
}

// The record loop of FUN_06620500 for one open Cons file.
static void ApplyConsFileRecords(FArchetypeHandle* Handle, IFileSystem* Fs, int File,
                                 unsigned long Type, unsigned long RecordSize, unsigned long FileSize)
{
    unsigned long Left = FileSize / RecordSize;
    while (Left != 0)
    {
        unsigned char Buffer[G_JointUtfMaxBytes];
        unsigned long BytesRead = 0;
        if (Fs->Read((unsigned long)File, Buffer, RecordSize, &BytesRead, 0) != 0 && // +0x14
            BytesRead == RecordSize)
        {
            const char* FirstName = (const char*)Buffer;
            const char* SecondName = (const char*)(Buffer + G_JointUtfNameBytes);
            FArchetypeChild* FirstPart =
                Handle->FindChildByCrc(DACOM_CRC::GetCRC32(FirstName));
            FArchetypeChild* SecondPart =
                Handle->FindChildByCrc(DACOM_CRC::GetCRC32(SecondName));
            if (FirstPart == nullptr || SecondPart == nullptr)
            {
                char Message[0x2000];
                _snprintf(Message, sizeof(Message), "Joint %s<->%s not found!", FirstName,
                          SecondName);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x389, "WARNING", Message);
            }
            else
            {
                // A zeroed temporary appended with insert(end, 1, record) (FUN_066237a0).
                FConstraintRecord Record = {};
                Record.ChildPart = SecondPart;
                Record.ParentPart = FirstPart;
                Record.Payload[0] = Type;
                RemapJointPayload(Type, Buffer + G_JointUtfHeaderBytes, Record.Payload);
                Handle->Constraints.push_back(Record);
            }
        }
        Left -= 1;
    }
}

static unsigned long JointTypeFromName(const char* Name)
{
    if (Name == nullptr)
        return 0xFFFFFFFF;
    if (DACOM_CRC::CompareStringsI(Name, G_NameFix) == 0)
        return G_JointTypeFix;
    if (DACOM_CRC::CompareStringsI(Name, G_NamePris) == 0)
        return G_JointTypePris;
    if (DACOM_CRC::CompareStringsI(Name, G_NameRev) == 0)
        return G_JointTypeRev;
    if (DACOM_CRC::CompareStringsI(Name, G_NameCyl) == 0)
        return G_JointTypeCyl;
    if (DACOM_CRC::CompareStringsI(Name, G_NameSphere) == 0)
        return G_JointTypeSphere;
    if (DACOM_CRC::CompareStringsI(Name, G_NameTrans) == 0)
        return G_JointTypeTrans;
    if (DACOM_CRC::CompareStringsI(Name, G_NameLoose) == 0)
        return G_JointTypeLoose;
    return 0xFFFFFFFF;
}

// The Cons pass of FUN_06620500 ("Cons" is DAT_0662a788).
static void ParseConsConstraints(FArchetypeHandle* Handle, IFileSystem* Fs)
{
    if (Fs->OpenNode(G_NameCons) == 0) // +0x60
        return;
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar, // +0x48
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            if (FindData.cFileName[0] == '.')
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.FindHandle = (unsigned long)FindHandle;
            const int File = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
            if (File == -1)
                continue;
            const unsigned long FileSize = Fs->GetSize((unsigned long)File, 0); // +0x28
            const unsigned long Type = JointTypeFromName(FindData.cFileName);
            const unsigned long RecordSize = JointUtfRecordSize(Type);
            if (Type == 0xFFFFFFFF)
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x376, "WARNING",
                             "Engine: unknown joint type, probably random garbage");
            // Retail: RecordSize is then 0 and the record count divides by it (06620a79 DIV EBP).
            ApplyConsFileRecords(Handle, Fs, File, Type, RecordSize, FileSize);
            Fs->CloseFile((unsigned long)File); // +0x10
        } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                            &FindData) != 0);
        Fs->FindClose((unsigned long)FindHandle); // +0x50
    }
    Fs->OpenNode(G_NameParentDot); // +0x60
}

// .data:0662a798: the text both FUN_06620500 part failures push.
static const char* const G_CmpndPartFailText =
    "Engine: create_compound_archetype: unable to create part archetype, ignoring...";

// FUN_06620500.
static GENRESULT ParseCmpndChildren(FEngineObject* Engine, FArchetypeHandle* Handle, void* CmpndFs,
                                   void* ParentFs)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return GR_E_FAIL;
    IFileSystem* Fs = AsFileSystem(CmpndFs);

    std::size_t Count = Handle->Children.size();
    FArchetypeChild* Child = AppendArchetypeChild(Handle);
    if (LinkCmpndPartHandle(Engine, G_NameRoot, CmpndFs, 0xFFFFFFFF, ParentFs, Child) < 0)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x331, "WARNING", G_CmpndPartFailText);
        ShrinkArchetypeChildren(Handle, Count);
    }

    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NamePartStar, // +0x48
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            Count = Handle->Children.size();
            Child = AppendArchetypeChild(Handle);
            if (LinkCmpndPartHandle(Engine, FindData.cFileName, CmpndFs,
                                    (unsigned long)FindHandle, ParentFs, Child) < 0)
            {
                ShrinkArchetypeChildren(Handle, Count);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x345, "WARNING",
                             G_CmpndPartFailText);
            }
        } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                            &FindData) != 0);
        Fs->FindClose((unsigned long)FindHandle); // +0x50
    }

    ParseConsConstraints(Handle, Fs);
    return GR_OK;
}

// The Engine +0x58 walk of LAB_06621150: CreateArchetype on every engine component. No checks.
static void FanoutIecCreateArchetype(FEngineObject* This, FArchetypeHandle* Handle, void* FileSystem)
{
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->CreateArchetype(Handle, FileSystem);
}

// LAB_06621150 (IEngine +0x30).
static FArchetypeHandle* DACOM_API EngineCreateArchetypeHandle(FEngineObject* This,
                                                              FArchetypeName* Name,
                                                              void* FileSystem)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, G_NameCmpnd, nullptr);
    void* CmpndFs = nullptr;
    if (Name->Crc == 0)
        return G_InvalidArchetype;

    FArchetypeHandle* Existing = This->LookupArchetype(Name);
    if (Existing != G_InvalidArchetype)
    {
        This->AddRefHandle(Existing); // +0x38
        return Existing;
    }
    if (FileSystem == nullptr)
        return G_InvalidArchetype;

    FArchetypeHandle* Handle = (FArchetypeHandle*)(This->CreateCompoundArchetype(Name)); // +0x2C
    if (Handle == G_InvalidArchetype)
        return G_InvalidArchetype;

    if (AsFileSystem(FileSystem)->CreateInstance((AGGDESC*)(&Desc), &CmpndFs) < 0 ||
        ParseCmpndChildren(This, Handle, CmpndFs, FileSystem) < 0)
        FanoutIecCreateArchetype(This, Handle, FileSystem);

    if (CmpndFs != nullptr)
        ((IDAComponent*)CmpndFs)->Release();
    return Handle;
}

static void FanoutIecDuplicateArchetype(FEngineObject* This, FArchetypeHandle* NewHandle,
                                       FArchetypeHandle* OldHandle)
{
    // IEC +0x14 on every node of +0x54, no tests.
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DuplicateArchetype(NewHandle, OldHandle);
}

// LAB_066212c0 (IEngine+0x34).
void* DACOM_API FEngineObject::DuplicateArchetype(void* InArchIn, FEngineName* InName)
{
    FArchetypeHandle* ArchIn = (FArchetypeHandle*)InArchIn;
    FArchetypeName* Name = InName;
    if (ArchIn == G_InvalidArchetype)
        return G_InvalidArchetype;
    if (ArchIn != nullptr && ArchIn->Children.size() != 0)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x42A, "WARNING",
                     "Engine: duplicate_archetype: arch_index is a compound archetype (not valid).");
        return G_InvalidArchetype;
    }
    if (LookupArchetype(Name) != G_InvalidArchetype)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x42F, "WARNING",
                     "Engine: duplicate_archetype: archetype name already exists in duplicate_archetype().");
        return G_InvalidArchetype;
    }

    FArchetypeHandle* NewHandle =
        (FArchetypeHandle*)(CreateCompoundArchetype(Name)); // vtable +0x2C
    if (NewHandle == G_InvalidArchetype)
        return G_InvalidArchetype;
    FanoutIecDuplicateArchetype(this, NewHandle, ArchIn);
    return NewHandle;
}

// LAB_066213b0 (IEngine +0x38). Retail leaves EAX unwritten for an invalid handle; the port
// returns 0.
static unsigned long DACOM_API EngineAddRefHandle(FEngineObject* This, FArchetypeHandle* Handle)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    return Handle->AddRef(This);
}

// LAB_066213d0 (IEngine +0x3C).
static unsigned long DACOM_API EngineReleaseHandle(FEngineObject* This, FArchetypeHandle* Handle)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    const unsigned long Count = Handle->Release(This);
    if (Count != 0)
        return Count;

    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DestroyArchetype(Handle);

    if (This->Archetypes.erase(Handle->Crc) == 0)
    {
        char Message[0x2000];
        std::sprintf(Message, "Failed to find %s in archetype name map\n", Handle->Name.c_str());
        EngBaseFdump(kFdumpError, G_FdumpFileEngine, 0x47E, "ERROR", Message);
    }

    // Retail inlines ~FArchetypeHandle; FUN_0661e9f0 returns the block.
    Handle->~FArchetypeHandle();
    G_ArchetypeHandlePool.Free(Handle);
    return 0;
}

static void CallEngineSlot14(FEngineObject* This, FArchetypeHandle* Handle)
{
    This->AddRefHandle(Handle);
}

// LAB_06621530 (IEngine+0x44): lookup + slot 14.
void* DACOM_API FEngineObject::FindArchetype(FEngineName* InName)
{
    FArchetypeName* Name = InName;
    FArchetypeHandle* Handle = LookupArchetype(Name);
    if (Handle != G_InvalidArchetype)
        CallEngineSlot14(this, Handle);
    return Handle;
}

// FUN_06621560 (IEngine+0x48): IEC+0x1C on every engine component; the first hr >= 0 wins.
// Out is not tested.
GENRESULT DACOM_API FEngineObject::QueryEngineComponent(int Id, void* Name, void** Out)
{
    *Out = nullptr;
    if (Id == -1 || Name == nullptr)
        return GR_INVALID_PARMS;

    FEngineChildNode* Sentinel = EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        const GENRESULT Result = AsEngineComponent(Node->Object)->QueryArchetype(
            (void*)((ptrdiff_t)Id), (const char*)Name, Out);
        if (Result >= 0)
            return GR_OK;
    }
    return (GENRESULT)(-1);
}

static FEngineInstance* const G_InvalidInstance =
    (FEngineInstance*)G_InvalidEngineHandle;

// LAB_066215d0 (IEngine+0x58): slot 12, slot 23, slot 15.
void* DACOM_API FEngineObject::CreateInstanceFromName(FEngineName* InName, void* FileSystem,
                                                      void* Transform)
{
    FArchetypeName* Name = InName;
    FArchetypeHandle* Handle = EngineCreateArchetypeHandle(this, Name, FileSystem);
    if (Handle == G_InvalidArchetype)
        return G_InvalidInstance;
    FEngineInstance* Instance = EngineCreateInstance(this, Handle, Transform);
    EngineReleaseHandle(this, Handle);
    return Instance;
}


// FUN_06621630's leaf path. No failure branch and no instance-map insert.
static FEngineInstance* EngineCreateInstanceSimple(FEngineObject* This, FArchetypeHandle* Handle,
                                                  IInstanceTransform* Transform)
{
    This->AddRefHandle(Handle);
    FEngineInstance* Instance = AllocEngineInstance(Handle);
    This->SetInstanceTransform(Instance, Transform);
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->CreateInstance(Instance, Handle);
    Instance->Transform->AddRef(Instance);
    return Instance;
}

// FUN_06621630 (IEngine+0x5C): compound archetypes go through FUN_06622860; otherwise
// allocate, slot 28 and IEC+0x20.
static FEngineInstance* DACOM_API EngineCreateInstance(FEngineObject* This,
                                                      FArchetypeHandle* Handle, void* Transform)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return G_InvalidInstance;
    IInstanceTransform* Xf = (IInstanceTransform*)Transform;
    if (Handle->Children.size() != 0)
    {
        FEngineInstance* Root = EngineCreateInstanceTree(This, Handle, Xf);
        if (Root == G_InvalidInstance)
            return Root;
        Root->Transform->AddRef(Root);
        return Root;
    }
    return EngineCreateInstanceSimple(This, Handle, Xf);
}

// FUN_06621710, in retail order.
void FEngineObject::DestroyInstanceBody(FEngineInstance* Instance)
{
    while (Instance->FirstChild != nullptr)
        DestroyInstanceBody(Instance->FirstChild);
    Instance->Transform->Release(Instance);
    if (Instance->Parent != nullptr)
        Instance->Parent->UnlinkChild(Instance);
    FEngineChildNode* Sentinel = EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DestroyInstance(Instance);
    if ((Instance->Flags & G_InstanceFlagOwnedTransform) != 0 && Instance->Transform != nullptr)
        ((FInstanceTransform*)(Instance->Transform))->Free(); // flag 2: ours
    Instance->Transform = nullptr;
    EraseRootInstance(this, Instance);
    ReleaseHandle(Instance->Archetype);
    if (Instance->ParentArchetype != nullptr)
        ReleaseHandle(Instance->ParentArchetype);
    G_InstancePool.Free(Instance);
}

// LAB_06621830 (IEngine+0x60): an invalid instance is returned as the result, as retail leaves
// the argument in EAX on that path.
GENRESULT DACOM_API FEngineObject::DestroyInstance(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return (GENRESULT)((intptr_t)Instance);
    DestroyInstanceBody(Instance);
    return GR_OK;
}

// LAB_06621850 (IEngine+0x40). No null test on Out.
FEngineName* DACOM_API FEngineObject::GetArchetypeName(FEngineName* Out, void* InHandle)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    if (Handle == G_InvalidArchetype || Handle == nullptr)
    {
        Out->Text = nullptr;
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);
        return Out;
    }
    // Retail returns the string's _Ptr (+4), or MSVCP60's _Nullstr when it has none.
    Out->Text = Handle->Name.c_str();
    Out->Crc = Handle->Crc;                                  // +0x10
    return Out;
}

// FUN_066218a0 (IEngine+0x64): inst+8, slot 14.
void* DACOM_API FEngineObject::GetInstanceArchetype(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return G_InvalidArchetype;
    EngineAddRefHandle(this, Instance->Archetype);
    return Instance->Archetype;
}

static void CopyTransformState(IInstanceTransform* Dest, IInstanceTransform* Source,
                               FEngineInstance* Instance)
{
    // Always through the interface on both sides. No tests. The two locals of the extra pair
    // are uninitialised in retail; the port zeroes them.
    Dest->SetMatrix(Instance, Source->GetMatrix(Instance));
    float Extra = 0.0f;
    float ExtraVec[3] = {0.0f, 0.0f, 0.0f};
    Source->GetExtra(Instance, &Extra, ExtraVec);
    Dest->SetExtra(Instance, Extra, ExtraVec);
    const unsigned long User = Source->GetUser(Instance);
    Dest->SetUser(Instance, User);
}

// LAB_066218d0 (IEngine +0x70), in retail order.
GENRESULT DACOM_API FEngineObject::SetInstanceTransform(void* InInstance, void* Transform)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    IInstanceTransform* NewTransform = (IInstanceTransform*)Transform;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return GR_OK;
    const unsigned long OldFlags = Instance->Flags;
    IInstanceTransform* Old = Instance->Transform;
    IInstanceTransform* Attached = NewTransform;
    if (Attached == nullptr)
    {
        Instance->Flags = OldFlags | G_InstanceFlagOwnedTransform;
        Attached = AllocInstanceTransform();
    }
    else
    {
        Instance->Flags = OldFlags & ~G_InstanceFlagOwnedTransform;
    }

    if (Attached == Old)
        return GR_OK;

    Attached->Init(Instance);
    if (Old != nullptr)
    {
        CopyTransformState(Attached, Old, Instance);
        Old->Release(Instance);
    }
    if ((OldFlags & G_InstanceFlagOwnedTransform) != 0 && Old != nullptr)
        ((FInstanceTransform*)Old)->Free(); // flag 2: ours
    Instance->Transform = Attached;
    return GR_OK;
}

// LAB_066219b0 (IEngine +0x74): only -1 is tested. Retail returns Out in EAX; the port
// returns 0.
GENRESULT DACOM_API FEngineObject::GetInstanceTransformObject(void* InInstance, void** InOut)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    IInstanceTransform** Out = (IInstanceTransform**)InOut;
    *Out = nullptr;
    if (Instance != G_InvalidInstance)
        *Out = Instance->Transform;
    return GR_OK;
}

// FUN_066219d0 (IEngine+0x78): IEC+0x2C on every engine component. Out is not tested; only
// instance -1 is rejected.
GENRESULT DACOM_API FEngineObject::QueryInstance(void* InInstance, const char* Iid, void** Out)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    *Out = nullptr;
    if (Instance == G_InvalidInstance || Iid == nullptr)
        return GR_INVALID_PARMS;
    FEngineChildNode* Sentinel = EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        const GENRESULT Result = AsEngineComponent(Node->Object)->QueryInstance(Instance, Iid, Out);
        if (Result >= 0)
            return GR_OK;
    }
    return (GENRESULT)(-1);
}

// LAB_06621a40 (IEngine +0x7C): inst+0x28. Retail returns the instance argument; the port
// returns 0.
GENRESULT DACOM_API FEngineObject::SetInstanceUserData(void* InInstance, void* Value)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance != G_InvalidInstance && Instance != nullptr)
        Instance->UserData = Value;
    return GR_OK;
}

// LAB_06621a60 (IEngine +0x80): inst+0x28.
void* DACOM_API FEngineObject::GetInstanceUserData(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return nullptr;
    return Instance->UserData;
}

// LAB_06621a80 (IEngine +0x84): FUN_0661ece0. Retail leaves the callee's EAX; the port
// returns 0.
GENRESULT DACOM_API FEngineObject::SetInstanceExtent(void* InInstance, unsigned long Flags, float Extent,
                                                      const float* Vec)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return GR_OK;
    Instance->SetExtentAndInvalidate(Extent, Vec, (unsigned char)(Flags & 1));
    return GR_OK;
}

static bool IsValidInstance(FEngineInstance* Instance)
{
    return Instance != G_InvalidInstance && Instance != nullptr;
}

// LAB_06621ab0, IEngine slot 34 (+0x88).
GENRESULT DACOM_API FEngineObject::GetInstanceExtent(void* InInstance, unsigned long Flags,
                                                      float* OutExtent, float* OutVec)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
    {
        *OutExtent = -1.0f;
        OutVec[2] = 0.0f;
        OutVec[1] = 0.0f;
        OutVec[0] = 0.0f;
        return GR_OK;
    }
    if ((Flags & 1) != 0)
    {
        Instance->ComputeInstanceExtent(0, OutExtent, OutVec);
        return GR_OK;
    }
    Instance->ComputeInstanceExtent(1, OutExtent, OutVec);
    return GR_OK;
}

GENRESULT DACOM_API FEngineObject::SetInstancePosition(void* InInstance, void* Position)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetPosition(Instance, (const float*)Position);
}

// LAB_06621b40 (IEngine +0x90) -> transform +0x10; fallback [DAT_0662b124].
float* DACOM_API FEngineObject::GetInstancePosition(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix + 9;
    return Instance->Transform->GetPosition(Instance);
}

// LAB_06621b70 (IEngine +0x94) -> transform +0x14.
GENRESULT DACOM_API FEngineObject::SetInstanceOrientation(void* InInstance, void* Rows)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetOrientation(Instance, (const float*)Rows);
}

// LAB_06621ba0 (IEngine +0x98) -> transform +0x18; fallback [DAT_0662b110].
float* DACOM_API FEngineObject::GetInstanceOrientation(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix;
    return Instance->Transform->GetOrientation(Instance);
}

// LAB_06621bd0 (IEngine +0x9C) -> transform +0x1C.
GENRESULT DACOM_API FEngineObject::SetInstanceMatrix(void* InInstance, void* Matrix)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetMatrix(Instance, (const float*)Matrix);
}

// LAB_06621c00 (IEngine +0xA0) -> transform +0x20; fallback DAT_0662b128 itself.
float* DACOM_API FEngineObject::GetInstanceTransform(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix;
    return Instance->Transform->GetMatrix(Instance);
}

// LAB_06621c30 (IEngine +0xA8 and +0xB0): return [DAT_0662b124] -- the zero
// translation of the identity, not the matrix.
static float* DACOM_API EngineGetIdentityMatrix(FEngineObject* This, void* Unused)
{
    (void)This;
    (void)Unused;
    return G_IdentityMatrix + 9;
}

// LAB_06621c40 (IEngine +0xB4) -> transform +0x2C.
GENRESULT DACOM_API FEngineObject::SetTransformUser(void* InInstance, unsigned long InUser)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    void* User = (void*)InUser;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetUser(Instance, (unsigned long)User);
}

// LAB_06621c70 (IEngine +0xB8) -> transform +0x30.
unsigned long DACOM_API FEngineObject::GetTransformUser(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return 0;
    return Instance->Transform->GetUser(Instance);
}

// LAB_06621ca0: the {iid, offset} table at 06629608, then each aggregated component.
GENRESULT DACOM_API FEngineObject::QueryInterface(const char* Iid, void** Out)
{
    if (std::strcmp(Iids::Engine, Iid) == 0 || std::strcmp(Iids::Engine2, Iid) == 0)
    {
        AddRef();
        *Out = this;
        return GR_OK;
    }

    *Out = nullptr;
    FEngineChildNode* Sentinel = ComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Node->Object->QueryInterface(Iid, Out) >= 0)
            return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// IEC +0x28 UpdateInstance on every node of +0x54, no tests.
static void FanoutIecUpdateInstance(FEngineObject* This, FEngineInstance* Instance, void* Arg)
{
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->UpdateInstance(Instance, Arg);
}


static void EngineUpdateInstanceTree(FEngineObject* This, FEngineInstance* Instance,
                                    unsigned long Flags, void* Arg)
{
    if ((Flags & 2) == 2)
        FanoutIecUpdateInstance(This, Instance, Arg);
    if ((Flags & 1) == 1)
        return;
    FEngineInstance* Child = Instance->FirstChild;
    while (Child != nullptr)
    {
        // FUN_06621ea0 always composes each child; flag 8 only gates the ancestor pass in
        // FUN_06621e60.
        Child->ComposeWorld();
        EngineUpdateInstanceTree(This, Child, Flags, Arg);
        Child = Child->NextSibling;
    }
}

// FUN_06621de0 (IEngine+0x14). No checks.
GENRESULT DACOM_API FEngineObject::NotifyEngineComponents(void* Arg)
{
    FEngineChildNode* Sentinel = EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->Notify(Arg);
    for (FEngineInstanceMap::iterator It = Instances.begin(); It != Instances.end();
         ++It)
        EngineUpdateInstanceTree(this, It->second, 0, nullptr);
    return GR_OK;
}

// FUN_06621e60 (IEngine+0x6C): compose ancestors, IEC+0x28, walk children.
GENRESULT DACOM_API FEngineObject::UpdateInstance(void* InInstance, unsigned long Flags, void* Arg)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return GR_OK;
    if ((Flags & 8) == 0)
        EngineComposeAncestors(Instance);
    EngineUpdateInstanceTree(this, Instance, Flags, Arg);
    return GR_OK;
}

// FUN_06621f20.
static void EngineComposeAncestors(FEngineInstance* Instance)
{
    if (Instance == nullptr || Instance->Parent == nullptr)
        return;
    EngineComposeAncestors(Instance->Parent);
    Instance->ComposeWorld();
}

// LAB_06621f90 (IEngine+0xBC).
unsigned long DACOM_API FEngineObject::CreateJoint(void* InParent, void* InChild,
                                                     const unsigned long* Payload)
{
    FEngineInstance* Parent = (FEngineInstance*)InParent;
    FEngineInstance* Child = (FEngineInstance*)InChild;
    if (Parent == G_InvalidInstance || Parent == nullptr)
        return 0;
    if (Child == G_InvalidInstance || Child == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x741, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: create_joint: Failing to connect %d <- %d, "
                                        "invalid instances.",
                                        Parent, Child));
        return 0;
    }
    if (Child->Parent != nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x746, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: create_joint: Failing to connect %d <- %d, "
                                        "child already has a parent.\n",
                                        Parent, Child));
        return 0;
    }
    if ((long)(Child->ChildCount) > 0)
        EraseRootInstance(this, Child);
    Parent->ConnectChild(Child, Payload);
    if (Parent->ChildCount == 1 && Parent->Parent == nullptr)
        InsertRootInstance(this, Parent);
    UpdateInstance(Child, 0, nullptr);
    return 1;
}

// LAB_06622140 (IEngine+0xC0). The instance acted on is Child; Parent only feeds the messages.
unsigned long DACOM_API FEngineObject::DestroyJoint(void* InParent, void* InChild)
{
    FEngineInstance* Parent = (FEngineInstance*)InParent;
    FEngineInstance* Child = (FEngineInstance*)InChild;
    if (Child == G_InvalidInstance || Child == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x763, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: destroy_joint: Failing to disconnect %d <- %d, "
                                        "child not found.",
                                        Parent, Child));
        return 0;
    }
    if (Child->Parent == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x769, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: destroy_joint: Failing to disconnect %d <- %d, "
                                        "not connected.",
                                        Parent, Child));
        return 0;
    }
    FEngineInstance* OldParent = Child->Parent;
    OldParent->UnlinkChild(Child);
    if (OldParent->ChildCount == 0 && OldParent->Parent == nullptr)
        EraseRootInstance(this, OldParent);
    if ((long)(Child->ChildCount) > 0)
        InsertRootInstance(this, Child);
    return 1;
}

// LAB_06612df0 (IEngine+0xE4): always -1.
unsigned long DACOM_API FEngineObject::RejectOperation(void* A, void* B)
{
    (void)A;
    (void)B;
    return 0xFFFFFFFF;
}

// The instance-root map key: the instance pointer as a signed int (FUN_06612e70 compares signed).
static long EngineInstanceKey(FEngineInstance* Instance)
{
    return (long)((intptr_t)Instance);
}

// LAB_066222a0 (IEngine+0xD0): iterate the instance-root map. No checks.
void* DACOM_API FEngineObject::FindMappedInstance(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance)
    {
        if (Instances.empty())
            return G_InvalidInstance;
        return (FEngineInstance*)(
            (intptr_t)(Instances.begin()->first));
    }
    const long Key = EngineInstanceKey(Instance);
    InstanceLookupCount += 1;
    FEngineInstanceMap::iterator It;
    if (bInstanceCacheValid != 0 && CachedInstance->first == Key)
    {
        InstanceCacheHits += 1;
        It = CachedInstance;
    }
    else
    {
        It = Instances.find(Key);
        if (It != Instances.end())
        {
            CachedInstance = It;
            bInstanceCacheValid = 1;
        }
    }
    if (It == Instances.end())
        return G_InvalidInstance;
    ++It;
    if (It == Instances.end())
        return G_InvalidInstance;
    return (FEngineInstance*)((intptr_t)(It->first));
}

// LAB_06622360 (IEngine +0x50): the argument is an archetype handle; only -1 is tested.
GENRESULT DACOM_API FEngineObject::EnumerateArchetypeConstraints(
    void* InHandle, int(__cdecl* Callback)(void*, void*, void*, void*), void* Context)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    if (Handle == G_InvalidArchetype)
        return (GENRESULT)(-1);
    FConstraintRecord* Record = Handle->Constraints.data();
    FConstraintRecord* const End = Record + Handle->Constraints.size();
    while (Record != End)
    {
        if ((unsigned char)(Callback(Record->ParentPart->Handle,
                                                Record->ChildPart->Handle, &Record->Payload[0],
                                                Context)) == 0)
            break;
        Record += 1;
    }
    return GR_OK;
}

// FUN_066223b0 (IEngine +0x54): only -1 is tested.
GENRESULT DACOM_API FEngineObject::EnumerateArchetypeChildren(
    void* InHandle, int(__cdecl* Callback)(void*, void*, int, void*), void* Context)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    if (Handle == G_InvalidArchetype)
        return (GENRESULT)(-1);
    FArchetypeChild* Child = Handle->Children.data();
    FArchetypeChild* const End = Child + Handle->Children.size();
    while (Child != End)
    {
        if ((unsigned char)(Callback(Handle, Child->Handle, 0, Context)) == 0)
            break;
        Child += 1;
    }
    return GR_OK;
}

// LAB_06622400 (IEngine+0x4C).
unsigned long DACOM_API FEngineObject::IsCompoundArchetype(void* InHandle)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    return Handle->Children.size() != 0 ? 1 : 0;
}

// LAB_06622450 (IEngine+0xE0): find the archetype child record by nested handle.
void* DACOM_API FEngineObject::FindArchetypeChild(void* InHandle, void* InChildHandle)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    FArchetypeHandle* ChildHandle = (FArchetypeHandle*)InChildHandle;
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return nullptr;
    FArchetypeChild* Child = Handle->Children.data();          // end read once
    FArchetypeChild* const End = Child + Handle->Children.size();
    while (Child != End)
    {
        if (Child->Handle == ChildHandle)
            return Child;
        Child += 1;
    }
    return nullptr;
}

// LAB_06622480 (IEngine+0xDC): walk +0x10 to root.
void* DACOM_API FEngineObject::GetInstanceRoot(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return G_InvalidInstance;
    while (Instance->Parent != nullptr)
        Instance = Instance->Parent;
    return Instance;
}

// LAB_066224b0 (IEngine+0xD4): inst+0x10.
void* DACOM_API FEngineObject::GetInstanceParent(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr)
        return G_InvalidInstance;
    return Instance->Parent;
}

// LAB_066224d0 (IEngine+0xD8): first child / next sibling / parent-walk.
void* DACOM_API FEngineObject::GetRelatedInstance(void* InFrom, unsigned long Flags, void* InCursor)
{
    FEngineInstance* From = (FEngineInstance*)InFrom;
    FEngineInstance* Cursor = (FEngineInstance*)InCursor;
    FEngineInstance* Root = IsValidInstance(From) ? From : nullptr;
    FEngineInstance* Node = IsValidInstance(Cursor) ? Cursor : nullptr;
    if (Root == nullptr)
        return G_InvalidInstance;
    if (Node == nullptr)
        return Root->FirstChild != nullptr ? Root->FirstChild : G_InvalidInstance;
    if ((Flags & 1) != 0)
        return Node->NextSibling != nullptr ? Node->NextSibling : G_InvalidInstance;
    if (Node->FirstChild != nullptr)
        return Node->FirstChild;
    while (Node != nullptr)
    {
        if (Node->NextSibling != nullptr)
            return Node->NextSibling;
        Node = Node->Parent;
        if (Node == Root)
            break;
    }
    return G_InvalidInstance;
}

// LAB_06622530 (IEngine+0x68): Text is the part child (+0xE8, whose name leads the record),
// Crc its +0x40. A root instance (+0xC == 0) gets the empty name.
FEngineName* DACOM_API FEngineObject::GetInstanceName(FEngineName* Out, void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Out == nullptr)
        return nullptr;
    if (!IsValidInstance(Instance) || Instance->ParentArchetype == nullptr)
    {
        Out->Text = nullptr;
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);
        return Out;
    }
    Out->Text = (char*)(Instance->PartChild);
    if (Instance->PartChild != nullptr)
        Out->Crc = Instance->PartChild->Crc;
    else
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);
    return Out;
}

// LAB_06622580 (IEngine+0xC4): inst+0x5C, or 0 for a root.
unsigned long* DACOM_API FEngineObject::GetInstanceRelative(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr)
        return nullptr;
    return Instance->Relative;
}

static void CopyJointFloats(float* Dest, const float* Source, unsigned long Count)
{
    if (Dest == nullptr || Source == nullptr)
        return;
    unsigned long Index = 0;
    while (Index < Count)
    {
        Dest[Index] = Source[Index];
        Index += 1;
    }
}

// LAB_066225b0 (IEngine+0xCC): get stored joint state. No solver.
unsigned long DACOM_API FEngineObject::GetJointParams(void* InInstance, unsigned long Index, float* Out)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr || Out == nullptr)
        return 0;
    if (Index != 0)
    {
        CopyJointFloats(Out, Instance->JointState, 7);
        return 1;
    }

    const unsigned long Type = Instance->Relative[0];
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
    {
        CopyJointFloats(Out, Instance->JointState, 1);
        return 1;
    }
    if (Type == G_JointTypeCyl)
    {
        CopyJointFloats(Out, Instance->JointState, 2);
        return 1;
    }
    if (Type == G_JointTypeSphere)
    {
        CopyJointFloats(Out, Instance->JointState + 3, 4);
        return 1;
    }
    if (Type == G_JointTypeTrans)
    {
        CopyJointFloats(Out, Instance->JointState, 3);
        return 1;
    }
    if (Type == G_JointTypeLoose)
    {
        CopyJointFloats(Out, Instance->JointState, 7);
        return 1;
    }
    return 1;
}

// LAB_066226e0 (IEngine+0xC8): set stored joint state. No solver.
unsigned long DACOM_API FEngineObject::SetJointParams(void* InInstance, unsigned long Index,
                                                      const float* Values)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr || Values == nullptr)
        return 0;
    if (Index != 0)
    {
        // All 7 floats (the Loose case); only the Index == 0 path sets +0xD4.
        CopyJointFloats(Instance->JointState, Values, 7);
        return 1;
    }

    Instance->bWorldDirty = 1;
    const unsigned long Type = Instance->Relative[0];
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
    {
        const float Input = Values[0];
        const float MinLimit = *(const float*)(&Instance->Relative[0x13]);
        const float MaxLimit = *(const float*)(&Instance->Relative[0x14]);
        if (MinLimit > Input)
            Instance->JointState[0] = MinLimit;
        else if (MaxLimit < Input)
            Instance->JointState[0] = MaxLimit;
        else
            Instance->JointState[0] = Input;
        return 1;
    }
    if (Type == G_JointTypeCyl)
    {
        CopyJointFloats(Instance->JointState, Values, 2);
        return 1;
    }
    if (Type == G_JointTypeSphere)
    {
        CopyJointFloats(Instance->JointState + 3, Values, 4);
        return 1;
    }
    if (Type == G_JointTypeTrans)
    {
        CopyJointFloats(Instance->JointState, Values, 3);
        return 1;
    }
    if (Type == G_JointTypeLoose)
    {
        CopyJointFloats(Instance->JointState, Values, 7);
        return 1;
    }
    return 1;
}

// FUN_06622860, called only from FUN_06621630 for compound archetypes.
static FEngineInstance* EngineCreateInstanceTree(FEngineObject* This, FArchetypeHandle* Handle,
                                                IInstanceTransform* Transform)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return G_InvalidInstance;

    std::map<unsigned long, FEngineInstance*> Parts; // FUN_06623730

    // The caller's transform goes to every child until the first one is created
    // successfully; every later part then gets its own engine-owned transform.
    IInstanceTransform* ChildTransform = Transform;
    FArchetypeChild* Child = Handle->Children.data();
    while (Child != Handle->Children.data() + Handle->Children.size())
    {
        // No filtering: slot 23 itself answers -1 for a -1/null handle.
        FEngineInstance* ChildInst =
            (FEngineInstance*)(This->CreateInstance(Child->Handle, ChildTransform));
        if (ChildInst != G_InvalidInstance)
        {
            ChildInst->ParentArchetype = Handle; // +0xC
            ChildInst->PartChild = Child;        // +0xE8
            // Overwrites an existing entry with the same CRC, as operator[] does.
            Parts[Child->Crc] = ChildInst;
            This->AddRefHandle(Handle);
            ChildTransform = nullptr;
        }
        Child += 1;
    }

    FEngineInstance* Root = nullptr;
    FConstraintRecord* Constraint = Handle->Constraints.data();
    while (Constraint != Handle->Constraints.data() + Handle->Constraints.size())
    {
        // A record with a null child or parent part is skipped silently.
        if (Constraint->ChildPart != nullptr && Constraint->ParentPart != nullptr)
        {
            std::map<unsigned long, FEngineInstance*>::iterator ChildIt =
                Parts.find(Constraint->ChildPart->Crc);
            if (ChildIt == Parts.end())
            {
                FEngineName ArchName;
                const char* ArchText = This->GetArchetypeName(&ArchName, Handle)->Text;
                if (ArchText == nullptr)
                    ArchText = "";               // DAT_0662ace4
                char Message[0x2000];
                _snprintf(Message, sizeof(Message),
                          "Unable to find child part '%s' in archetype '%s'",
                          Constraint->ChildPart->Name, ArchText);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8D7, "WARNING", Message);
            }
            else
            {
                FEngineInstance* ChildInst = ChildIt->second;
                std::map<unsigned long, FEngineInstance*>::iterator ParentIt =
                    Parts.find(Constraint->ParentPart->Crc);
                if (ParentIt == Parts.end())
                {
                    FEngineName ArchName;
                    const char* ArchText = This->GetArchetypeName(&ArchName, Handle)->Text;
                    if (ArchText == nullptr)
                        ArchText = "";           // DAT_0662ace4
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message),
                              "Unable to find parent part '%s' in archetype '%s'",
                              Constraint->ParentPart->Name, ArchText);
                    EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8E0, "WARNING", Message);
                }
                else
                {
                    // The parent is kept as Root even when the connect fails.
                    Root = ParentIt->second;
                    if (Root->ConnectChild(ChildInst, Constraint->Payload) == 0)
                        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8EB, "WARNING",
                                     "Unable to connect child to parent");
                }
            }
        }
        Constraint += 1;
    }

    if (Root != nullptr)
    {
        // Climb to the top of the last connected parent's tree.
        while (Root->Parent != nullptr)
            Root = Root->Parent;
    }
    else
    {
        // No constraint connected anything: the part with the lowest CRC.
        if (Parts.empty())
            return G_InvalidInstance;
        Root = Parts.begin()->second;
    }

    // Instance-root map at +0x10, cache at +0x28 / byte +0x2C.
    InsertRootInstance(This, Root);
    return Root;
}

// FUN_066235d0 (list insert(where, value)): operator new(0x3c), 0x34-byte value copy.
void FEngineChildNode::Insert(unsigned long* Count, const char* Name, IDAComponent* Object)
{
    FEngineChildNode* Node = new FEngineChildNode;
    Node->Next = this;
    Node->Prev = Prev != nullptr ? Prev : Node;
    Prev = Node;
    Node->Prev->Next = Node;
    std::memcpy(Node->Name, Name, sizeof(Node->Name));
    Node->Object = Object;
    *Count += 1;
}

// FUN_06623620 (list erase(first, last)).
static void EraseChildNodes(FEngineChildNode* First, FEngineChildNode* Last, unsigned long* Count)
{
    FEngineChildNode* Node = First;
    while (Node != Last)
    {
        FEngineChildNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        *Count -= 1;
        Node = Next;
    }
}

// FUN_06623ac0 with the pair {Instance, Instance} and the cache update every caller does
// after it.
static void InsertRootInstance(FEngineObject* Engine, FEngineInstance* Instance)
{
    FEngineInstanceMap::iterator It =
        Engine->Instances.insert(FEngineInstanceMap::value_type(EngineInstanceKey(Instance), Instance))
            .first;
    if (It != Engine->Instances.end())
    {
        Engine->CachedInstance = It;
        Engine->bInstanceCacheValid = 1;
    }
}

// The erase every caller inlines (FUN_06625220 / FUN_06624ca0 / FUN_06625500 / FUN_06623200).
static void EraseRootInstance(FEngineObject* Engine, FEngineInstance* Instance)
{
    const long Key = EngineInstanceKey(Instance);
    if (Engine->bInstanceCacheValid != 0 && Engine->CachedInstance->first == Key)
        Engine->bInstanceCacheValid = 0;
    Engine->Instances.erase(Key);
}

// LAB_06625380.
GENRESULT DACOM_API EngineFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeEngine || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FEngineObject* Object = (FEngineObject*)(std::calloc(1, sizeof(FEngineObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    Object->Construct();
    Object->RefCount = 1;
    const GENRESULT Hook = EngBaseAggregationHook(Desc);
    if (Hook != 0)
    {
        Object->Destruct();
        std::free(Object);
        *Out = nullptr;
        return Hook;
    }
    *Out = Object;
    return GR_OK;
}

// LAB_06625460: count at +0x60.
unsigned long DACOM_API FEngineObject::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// FUN_06625470: at zero the count is pinned to 1 before the dtor runs.
unsigned long DACOM_API FEngineObject::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    Destruct();
    std::free(this);
    return 0;
}

// FUN_066254d0 with the flag clear: the node value's dtor zeroes name[0] and the +0x38 pointer.
static void DestroyChildNodeValue(FEngineChildNode* Node)
{
    Node->Name[0] = '\0';
    Node->Object = nullptr;
}

// PTR_LAB_06621118: joint type to UTF record size (not the FUN_06625560 apply).
static unsigned long JointUtfRecordSize(unsigned long Type)
{
    if (Type == G_JointTypeFix || Type == G_JointTypeTrans || Type == G_JointTypeLoose)
        return 0xB0;
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
        return 0xD0;
    if (Type == G_JointTypeCyl)
        return 0xD8;
    if (Type == G_JointTypeSphere)
        return 0xD4;
    return 0;
}

static void CopyRelative3x3(float* Dest, const float* Rel)
{
    std::memcpy(Dest, Rel + 7, 9 * sizeof(float));
}

static void CopyRelativeTranslation(float* Dest, const float* Rel)
{
    Dest[9] = Rel[4];
    Dest[10] = Rel[5];
    Dest[11] = Rel[6];
}

static void ApplyFixJoint(float* Dest, const float* Rel)
{
    CopyRelative3x3(Dest, Rel);
    CopyRelativeTranslation(Dest, Rel);
}

static void AddVec3(float* Dest, const float* A, const float* B)
{
    Dest[0] = A[0] + B[0];
    Dest[1] = A[1] + B[1];
    Dest[2] = A[2] + B[2];
}

static void ApplyTransJoint(float* Dest, const float* Rel, const float* JointState)
{
    CopyRelative3x3(Dest, Rel);
    AddVec3(Dest + 9, JointState, Rel + 4);
}

// FUN_06625560 (thiscall on inst+0x5C); only caller FUN_0661f250.
static unsigned long ApplyJointToLocal(FEngineInstance* Instance, unsigned char bForce)
{
    if (Instance->bWorldDirty == 0 && bForce == 0)
        return 0;
    Instance->bWorldDirty = 0;
    const unsigned long Type = Instance->Relative[0];
    if (Type == 0xFFFFFFFF)
        return 0;

    float* Dest = Instance->Matrix;
    const float* Rel = (const float*)(Instance->Relative);
    const float* State = Instance->JointState;
    if (Type == G_JointTypeFix)
        ApplyFixJoint(Dest, Rel);
    else if (Type == G_JointTypePris)
        ApplyPrisJoint(Dest, Rel, State);
    else if (Type == G_JointTypeRev)
        ApplyRevJoint(Dest, Rel, State);
    else if (Type == G_JointTypeCyl)
        ApplyCylJoint(Dest, Rel, State);
    else if (Type == G_JointTypeSphere)
        ApplySphereJoint(Dest, Rel, State);
    else if (Type == G_JointTypeTrans)
        ApplyTransJoint(Dest, Rel, State);
    else if (Type == G_JointTypeLoose)
        ApplyLooseJoint(Dest, Rel, State);
    else
        return 0;
    return 1;
}


// FUN_06625560: this = inst+0x5C, dest = inst+0x2C. Jump table PTR_LAB_0662aa84.
// Relative: [0] type, [1..3] rest pos, [4..6] T @ +0x10, [7..15] 3x3 @ +0x1C, [16..18] axis @ +0x40.
// DAT_06629510 = 0.5f (half-angle). Only 3DMath +0x20/+0x2C/+0x54 are used; there is no IK.
static const float G_JointHalfAngle = 0.5f;

// Quaternion order is W, X, Y, Z; preserve the float spill sites.
static void AxisAngleToQuat(float* OutQuat, const float* Axis, float Angle)
{
    const double Half = (double)Angle * (double)G_JointHalfAngle;
    const double Sin = std::sin(Half);
    const double X = Axis[0];
    const double Y = Axis[1];
    const double Z = Axis[2];
    const double Len = std::sqrt(X * X + Y * Y + Z * Z);
    const float K = (float)(Sin / Len);
    OutQuat[0] = (float)(std::cos(Half));
    OutQuat[1] = (float)((double)K * X);
    OutQuat[2] = (float)((double)K * Y);
    OutQuat[3] = (float)((double)K * Z);
}

static void SubVec3(float* Dest, const float* A, const float* B)
{
    Dest[0] = A[0] - B[0];
    Dest[1] = A[1] - B[1];
    Dest[2] = A[2] - B[2];
}

// DAT_0662b1b0: the 3DMathEngine every joint helper re-checks before each call
// (FUN_06628190 creates it).
static I3DMathEngine* G_JointMath = nullptr;

static I3DMathEngine* JointMath()
{
    if (G_JointMath == nullptr)
        G_JointMath = Create3DMathEngine();
    return G_JointMath;
}

// Rev / Sphere tail: T = Rel[1..3] - Dest 3x3 * Rel[4..6], float subtractions.
static void ApplyRestTranslation(float* Dest, const float* Rel)
{
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SubVec3(Dest + 9, Rel + 1, Rotated);
}

// LAB_066255e0 (Rev): the joint rotation multiplies on the LEFT of the rest 3x3.
static void ApplyRevJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    float Rot[9];
    AxisAngleToQuat(Quat, Rel + 16, JointState[0]);
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    ApplyRestTranslation(Dest, Rel);
}

// Preserve separate float rounding for Y/Z translation intermediates.
static void SlideTranslation(float* OutT, const float* Rel, float Slide, const float* V)
{
    const double S = Slide;
    const double Ax = (double)(Rel[16]) * S;
    const float Ty = (float)(S * (double)(Rel[17]));
    const float Tz = (float)(S * (double)(Rel[18]));
    const float Pz = (float)((double)Tz + (double)(Rel[3]));
    OutT[0] = (float)((Ax + (double)(Rel[1])) - (double)(V[0]));
    OutT[1] = (float)(((double)Ty + (double)(Rel[2])) -
                                 (double)(V[1]));
    OutT[2] = (float)((double)Pz - (double)(V[2]));
}

// LAB_06625730 (Pris).
static void ApplyPrisJoint(float* Dest, const float* Rel, const float* JointState)
{
    CopyRelative3x3(Dest, Rel);
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SlideTranslation(Dest + 9, Rel, JointState[0], Rotated);
}

// LAB_066257f0 (Cyl): rotation from State[1] multiplies on the RIGHT; slide from State[0].
static void ApplyCylJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    float Rot[9];
    AxisAngleToQuat(Quat, Rel + 16, JointState[1]);
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rel + 7, Rot);
    std::memcpy(Dest, Product, sizeof(Product));
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SlideTranslation(Dest + 9, Rel, JointState[0], Rotated);
}

// LAB_06625980 (Sphere): quaternion State[3..6].
static void ApplySphereJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    std::memcpy(Quat, JointState + 3, sizeof(Quat));
    float Rot[9];
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    ApplyRestTranslation(Dest, Rel);
}

// LAB_06625af0 (Loose): quaternion State[3..6], translation State[0..2] + Rel[4..6].
static void ApplyLooseJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    std::memcpy(Quat, JointState + 3, sizeof(Quat));
    float Rot[9];
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    AddVec3(Dest + 9, JointState, Rel + 4);
}

// FUN_06625bd0: an element's first dword is the next link. The memory is not cleared.
void* FEngBaseFreeList::Alloc()
{
    void* Element = FreeHead;
    if (Element != nullptr)
    {
        FreeHead = *(void**)Element;
        FreeCount -= 1;
        return Element;
    }
    return std::malloc(ElementSize);
}

// FUN_06625c00: a negative limit means unbounded.
void FEngBaseFreeList::Free(void* Element)
{
    if (FreeLimit > FreeCount || FreeLimit < 0)
    {
        FreeCount += 1;
        *(void**)Element = FreeHead;
        FreeHead = Element;
        return;
    }
    std::free(Element);
}

// FUN_06625c40. Live elements are not tracked and are not touched.
void FEngBaseFreeList::Drain()
{
    void* Element = FreeHead;
    while (Element != nullptr)
    {
        void* Next = *(void**)Element;
        std::free(Element);
        Element = Next;
    }
    FreeCount = 0;
    FreeHead = nullptr;
}

void* DACOM_API FEngineObject::CreateArchetypeHandle(FEngineName* Name, void* FileSystem)
{
    return EngineCreateArchetypeHandle(this, Name, FileSystem);
}

unsigned long DACOM_API FEngineObject::AddRefHandle(void* Handle)
{
    return EngineAddRefHandle(this, (FArchetypeHandle*)Handle);
}

unsigned long DACOM_API FEngineObject::ReleaseHandle(void* Handle)
{
    return EngineReleaseHandle(this, (FArchetypeHandle*)Handle);
}

void* DACOM_API FEngineObject::CreateInstance(void* Handle, void* Transform)
{
    return EngineCreateInstance(this, (FArchetypeHandle*)Handle, Transform);
}

GENRESULT DACOM_API FEngineObject::NoOp41(void* A, void* B)
{
    return StubRetC(this, A, B);
}

float* DACOM_API FEngineObject::GetIdentityMatrix(void* Unused)
{
    return EngineGetIdentityMatrix(this, Unused);
}

GENRESULT DACOM_API FEngineObject::NoOp43(void* A, void* B)
{
    return StubRetC(this, A, B);
}

float* DACOM_API FEngineObject::GetIdentityMatrix2(void* Unused)
{
    return EngineGetIdentityMatrix(this, Unused);
}
