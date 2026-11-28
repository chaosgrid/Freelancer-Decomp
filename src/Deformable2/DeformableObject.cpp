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

static void AxisAngleToQuat(float* Quat, const float* Axis, float Angle);

// .data:06602570 and .data:066026dc: the __FILE__ and message FUN_065f5400 pushes at line 0x44D.
static const char* const G_FdumpFileDeformableObject =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformableObject.cpp";

// Pair bone instances by hardpoint-name CRC for connection.
struct FHardpointInstancePair
{
    InstanceIndex First;  // node +0x10
    InstanceIndex Second; // node +0x14
};

struct FHardpointConnectContext
{
    std::map<unsigned long, FHardpointInstancePair> Pairs;
    IHardpoint* Hardpoint;
};

// LAB_065f3540 (cdecl: BoneInst, Name, Context).
static void __cdecl HardpointConnectCallback(InstanceIndex BoneInst, char const* Name, void* Context)
{
    FHardpointConnectContext* Ctx = static_cast<FHardpointConnectContext*>(Context);
    FHardpointInstancePair Unset;
    Unset.First = InvalidInstanceIndex;
    Unset.Second = InvalidInstanceIndex;
    std::pair<std::map<unsigned long, FHardpointInstancePair>::iterator, bool> Inserted =
        Ctx->Pairs.insert(std::make_pair(DACOM_CRC::GetCRC32(Name), Unset));
    FHardpointInstancePair* Pair = &Inserted.first->second;
    if (Pair->First == InvalidInstanceIndex)
    {
        Pair->First = BoneInst;
        return;
    }
    Pair->Second = BoneInst;
    FHardpointKey KeyA;
    KeyA.Name = Name;
    KeyA.Crc = DACOM_CRC::GetCRC32(Name);
    FHardpointKey KeyB;
    KeyB.Name = Name;
    KeyB.Crc = DACOM_CRC::GetCRC32(Name);
    if (Ctx->Hardpoint->ConnectHardpoints(Pair->First, &KeyA, Pair->Second, &KeyB) == 0)
        return;

    char Message[0x2000];
    FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s", G_FdumpFileDeformableObject, 0x53,
          FormatDeformMessage(Message, "Deformable: create: cannot connect parts %s<->%s",
                              Name, Name));
}

static const float G_RandToUnit = 3.051851e-05f;

// FUN_065f3650
void FDeformInstance::ConstructBody()
{
    PartCount = 0;
    PartArray = nullptr;
    ListHead = nullptr;
    ListTail = nullptr;
    ListCount = 0;
    ForwardSink = nullptr;
    DefaultScript = nullptr;
    BaseHeight = 0.0f;
    RootHeightOffset = 0.0f;
    if (G_DeformRate > 0.0f)
        RateCookie = (float)(rand()) * G_DeformRate * G_RandToUnit;
    else
        RateCookie = G_DeformRate;
}

// Inlined in the skeleton ctor FUN_065f36c0: operator new(0x1e88). The per-bone ctor
// FUN_065f3e70 is empty, so the matrices stay uninitialised until FUN_065f1100 copies them.
static FSkeleton* ConstructSkeleton()
{
    FSkeleton* Skel = new FSkeleton;
    Skel->Scale = 1.0f;
    Skel->BoneCount = 0;
    int Index = 0;
    while (Index < G_MaxBoneCount)
    {
        Skel->Bones[Index].Parent = -1;
        Index += 1;
    }
    Skel->EngineHandle = InvalidArchetypeIndex;
    Skel->RefCount = 1;
    return Skel;
}

// FUN_065f36c0 (thiscall on the instance: Parts, Arg3, Arg4). Returns 1.
bool FDeformInstance::ConnectParts(FDeformPartTable* Parts, void* Arg3, void* Arg4)
{
    ForwardSink = static_cast<IChannelSink*>(Arg3);
    DefaultScript = Arg4;
    PartCount = Parts->Count;
    int* Block = (int*)new unsigned char[(size_t)PartCount * sizeof(FDeformPart) + 4];
    *Block = PartCount;
    FDeformPart* Array = reinterpret_cast<FDeformPart*>((Block + 1));
    int Index = 0;
    while (Index < PartCount)
    {
        Array[Index].Construct();
        Index += 1;
    }
    PartArray = Array;

    FDeformPartDesc* Descs = Parts->Entries;
    int PartIndex = 0;
    while (PartIndex < PartCount)
    {
        FDeformPartDesc* Entry = &Descs[PartIndex];
        const char* Path = Entry->Path;
        IComponentFactory* Factory = nullptr;
        if (Path == nullptr)
        {
            Factory = Entry->Lods[0].FileSystem;
            Path = Entry->Lods[0].Name;
        }
        FFileSystemDesc RootDesc;
        FillFileDesc(&RootDesc, Path);
        IFileSystem* RootFs = nullptr;
        GENRESULT Created = 0;
        if (Factory != nullptr)
            Created = Factory->CreateInstance(reinterpret_cast<AGGDESC*>(&RootDesc),
                                              (void**)(&RootFs));
        else
            Created = DACOM_Acquire()->CreateInstance(reinterpret_cast<AGGDESC*>(&RootDesc),
                                                      (void**)(&RootFs));
        if (Created < 0)
        {
            if (RootFs != nullptr)
            {
                RootFs->Release();
                RootFs = nullptr;
            }
            PartIndex += 1;
            continue;
        }

        char BaseName[0x104];
        std::strncpy(BaseName, std::strrchr(Path, '\\') + 1, 0x104);
        char* Dot = std::strchr(BaseName, '.');
        Dot[0] = '\0';
        Dot[1] = '\0';
        char SkelName[0x100];
        _snprintf(SkelName, 0x100, "skel_%s", BaseName);

        FSkeleton* Skel = ConstructSkeleton();
        if (!Skel->Load(SkelName, RootFs))
        {
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0xB0,
                  "couldnt load skeleton");
        }
        else
        {
            FLodHolder* Holder = new FLodHolder;
            Holder->Skeleton = nullptr;
            Holder->BoneSlots = nullptr;
            Holder->BindCursor = 0;
            Holder->RefCount = 1;
            if (!Holder->AttachSkeleton(Skel))
            {
                FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0xB8,
                      "couldnt create skeleton");
            }
            else
            {
                float Fractions[8];
                int LodCap = 0;
                if (RootFs->OpenNode("MultiLevel") != 0)
                {
                    unsigned int Wanted = 8;
                    FFileSystemDesc FractionDesc;
                    FillFileDesc(&FractionDesc, "Fractions");
                    const int Handle = RootFs->OpenByDesc(&FractionDesc);
                    if (Handle != -1)
                    {
                        const unsigned int Available =
                            RootFs->GetSize((unsigned long)Handle, 0) >> 2;
                        if (Available < Wanted)
                            Wanted = Available;
                        unsigned long Got = 0;
                        RootFs->Read((unsigned long)Handle, Fractions, Wanted * 4, &Got,
                                     0);
                        LodCap = (int)Wanted;
                        RootFs->CloseFile((unsigned long)Handle);
                    }
                }
                else
                {
                    LodCap = 1;
                    Fractions[0] = 1.0f;
                }
                if (LodCap > Entry->LodCount)
                    LodCap = Entry->LodCount;

                if (LodCap <= 0)
                {
                    Entry->LodCount = 0;
                    FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject,
                          0xD7, "no meshes found");
                }
                else
                {
                    FDeformMesh* Loaded[8];
                    int LoadedCount = 0;
                    WIN32_FIND_DATAA FindData;
                    const int Finder = FsFindFirst(RootFs, "Mesh*", &FindData);
                    if (Finder != -1)
                    {
                        unsigned int LodIndex = 0;
                        for (;;)
                        {
                            bool bSkip = false;
                            if (LodIndex == 0)
                                bSkip = (Entry->Flags & 2) == 2;
                            else if ((Entry->Flags & 1) == 1)
                                break;
                            if (!bSkip)
                            {
                                FFileSystemDesc MeshDesc;
                                FillFileDesc(&MeshDesc, FindData.cFileName);
                                MeshDesc.FindHandle = (unsigned long)Finder;
                                IFileSystem* MeshFs = nullptr;
                                bool bFull = false;
                                if (RootFs->CreateInstance(reinterpret_cast<AGGDESC*>(&MeshDesc),
                                                           (void**)(&MeshFs)) >= 0)
                                {
                                    Dot[0] = (char)('0' + LodIndex);
                                    const unsigned char bSkinned =
                                        (LodIndex < Entry->LodLimit) ? 1 : 0;
                                    FDeformMesh* Mesh = LoadDeformMesh(Holder, MeshFs, BaseName, bSkinned);
                                    if (Mesh != nullptr)
                                    {
                                        Loaded[LoadedCount] = Mesh;
                                        Entry->Lods[LoadedCount].Fraction = Fractions[LodIndex];
                                        LoadedCount += 1;
                                        bFull = LoadedCount >= LodCap;
                                    }
                                }
                                if (MeshFs != nullptr)
                                    MeshFs->Release();
                                if (bFull)
                                    break;
                            }
                            LodIndex += 1;
                            if (LodIndex == 0)
                                break;
                            if (FsFindNext(RootFs, Finder, &FindData) == 0)
                                break;
                        }
                        FsFindClose(RootFs, Finder);
                    }
                    Entry->LodCount = LoadedCount;
                    if (!PartArray[PartIndex].BindMeshes(Holder, Loaded, LoadedCount))
                    {
                        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                              G_FdumpFileDeformableObject, 0x11F, "warning: couldnt create part.");
                    }
                }
            }
        }
        // The skeleton reference taken by the ctor, then RootFs.
        Skel->Release();
        if (RootFs != nullptr)
        {
            RootFs->Release();
            RootFs = nullptr;
        }
        PartIndex += 1;
    }

    FHardpointConnectContext Context;
    Context.Hardpoint = nullptr;
    if (G_Engine->QueryInterface(Iids::Hardpoint, (void**)(&Context.Hardpoint)) < 0)
    {
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x2B,
              "Deform: Unable to acquire IID_IHardpoint, unable to continue");
    }
    WalkHardpoints(&HardpointConnectCallback, &Context);

    PartIndex = 0;
    while (PartIndex < PartCount)
    {
        FLodHolder* Lod = PartArray[PartIndex].Lod;
        int Bone = 1;
        while (Bone < Lod->BindCursor)
        {
            FBoneRuntime* Slot = &Lod->BoneSlots[Bone];
            if (G_Engine->GetInstanceParent(Slot->Instance) == InvalidInstanceIndex)
            {
                G_Engine->DestroyInstance(Slot->Instance);
                Slot->Instance = InvalidInstanceIndex;
            }
            Bone += 1;
        }
        PartIndex += 1;
    }

    if (Context.Hardpoint != nullptr)
    {
        Context.Hardpoint->Release();
        Context.Hardpoint = nullptr;
    }
    return true;
}

static void UnlinkListNode(FDeformInstance* Instance, FIkListNode* Node)
{
    if (Node->Prev == nullptr)
        Instance->ListHead = Node->Next;
    else
        Node->Prev->Next = Node->Next;
    if (Node->Next == nullptr)
        Instance->ListTail = Node->Prev;
    else
        Node->Next->Prev = Node->Prev;
    Instance->ListCount -= 1;
}

void FillFileDesc(FFileSystemDesc* Desc, const char* Path)
{
    std::memset(Desc, 0, sizeof(FFileSystemDesc));
    Desc->Size = offsetof(FFileSystemDesc, MemBuffer);
    Desc->ComponentName = G_FileSystemName;
    Desc->Path = Path;
    Desc->Access = GENERIC_READ;
    Desc->ShareMode = FILE_SHARE_READ;
    Desc->Creation = OPEN_EXISTING;
    Desc->Flags = FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL;
    Desc->FindHandle = 0xFFFFFFFF;
}

// FUN_065f3ed0 (thiscall on the instance).
void FDeformInstance::Teardown()
{
    FIkListNode* Node = ListHead;
    while (Node != nullptr)
    {
        if (Node->Active != 0)
        {
            if (Node->Event != nullptr)
                delete Node->Event;                              // FUN_065fbf40 + operator delete
            Node->Event = nullptr;
        }
        else
        {
            G_Animation->DestroyScript(Node->Event);
            Node->Event = reinterpret_cast<FIkEvent*>(((ptrdiff_t)-1));
        }
        Node = Node->Next;
    }
    while (ListCount != 0)
    {
        FIkListNode* Head = ListHead;
        UnlinkListNode(this, Head);
        delete Head;
    }

    if (PartArray != nullptr)
    {
        InstanceIndex Root = PartArray[0].Lod->BoneSlots[0].Instance;
        if (Root != InvalidInstanceIndex)
            G_Engine->DestroyInstance(Root);
        int* Header = (int*)PartArray - 1;
        int Index = *Header;
        while (Index > 0)
        {
            Index -= 1;
            PartArray[Index].Reset();
        }
        delete[] (unsigned char*)Header;
        PartArray = nullptr;
    }
    PartCount = 0;
}

// The root-bone read retail inlines at several sites; PartArray (+8) is the only test.
InstanceIndex FirstBoneInstance(FDeformInstance* Instance)
{
    if (Instance->PartArray == nullptr)
        return InvalidInstanceIndex;
    return Instance->PartArray[0].Lod->BoneSlots[0].Instance;
}


// FUN_065f4070 (thiscall on the instance: Unused, Camera, OutLod, RenderArg, Flags).
void FDeformInstance::DrawParts(void* /*Unused*/, ICamera* Camera, int* OutLod, void* RenderArg,
                                unsigned long Flags)
{
    *OutLod = 0;
    if (PartCount <= 0)
        return;
    static_assert(sizeof(ICamera*) == sizeof(float), "the radius shares the camera's slot");
    float Radius = 0.0f;
    std::memcpy(&Radius, &Camera, sizeof(Radius));
    Vector Center;
    Center.x = 0.0f;
    Center.y = 0.0f;
    Center.z = 0.0f;
    int Index = 0;
    do
    {
        FDeformPart* const Part = &PartArray[Index];
        Part->BoundSphere(&Radius, &Center);
        const Transform* const View = Camera->GetViewTransform();
        Vector ViewCenter;
        AcquireMathEngine()->TransformXform(ViewCenter,
                                            *View,
                                            Center); // +0x28
        Vector Probe;
        Probe.x = ViewCenter.x;
        Probe.y = ViewCenter.y;
        Probe.z = ViewCenter.z;
        const int Rank = Camera->ClassifySphere(&Probe, Radius);
        if (Rank > 2)
            Part->DrawLive(Camera, RenderArg);
        if (Index == 0)
            *OutLod = Rank;
        else if (*OutLod != Rank)
        {
            if (*OutLod < Rank)
                *OutLod = Rank;
            if (*OutLod >= 3)
                *OutLod = 3;
        }
        Index += 1;
    } while (Index < PartCount);

    if (*OutLod < 3)
        return;
    const InstanceIndex Bone = FirstBoneInstance(this);
    // Submit the root instance and merge its renderer rank into the LOD result.
    const int Rank = G_Renderer->RenderInstance(
        Camera, Bone, 1.0f, Flags | kDrawNoCull | kDrawPickSwitch, nullptr, RenderArg);
    if (*OutLod != Rank)
    {
        if (*OutLod < Rank)
            *OutLod = Rank;
        if (*OutLod >= 3)
            *OutLod = 3;
    }
}


static void LinkListNode(FDeformInstance* Instance, FIkListNode* Node)
{
    FIkListNode* OldTail = Instance->ListTail;
    Instance->ListTail = Node;
    if (OldTail == nullptr)
        Instance->ListHead = Node;
    else
        OldTail->Next = Node;
    Node->Prev = OldTail;
    Node->Next = nullptr;
    Instance->ListCount += 1;
}

// FUN_065f41d0 (thiscall on the instance, nine stack arguments).
int FDeformInstance::PlayAnim(void* Name, void* Extra, void* ArgC, void* ArgD, void* ArgE,
                              void* ArgF, void* ArgG, void* ArgH, void* ScriptOrNull)
{
    SweepExpiredList();
    int BoneHandle = -1;
    if (PartArray != nullptr)
        BoneHandle = (int)(
            (ptrdiff_t)(PartArray[0].Lod->BoneSlots[0].Instance));
    void* Script = ScriptOrNull;
    if (Script == nullptr)
        Script = DefaultScript;
    const int Handle = (int)(G_Animation->PlayScript(
        Name, BoneHandle, (const char*)Extra,
        this, Script));
    if (Handle == -1)
        return -1;

    FBoneRuntime* Bone = PartArray[0].Lod->BoneSlots;
    const Vector* BonePos =
        &Bone->LocalTransform.Translation;
    Vector Vec;
    Vec.x = 0.0f;
    Vec.y = 0.0f;
    Vec.z = 0.0f;
    const float ArgCFloat = FloatFromOpaqueDword(ArgC);
    if (ArgCFloat > 0.0f)
    {
        IRootData* Root = nullptr;
        if (G_Animation->QueryInterface(Iids::RootData, (void**)&Root) == 0)
        {
            // Retail pushes, last first: 1.0f, ArgF, ArgD, ArgE, ArgC, ArgG, &Vec, Handle.
            const unsigned char bTouched = Root->Eval(
                (unsigned long)Handle, &Vec,
                (unsigned long)((ptrdiff_t)ArgG), ArgCFloat,
                FloatFromOpaqueDword(ArgE), ArgD, ArgF, 1.0f);
            if (bTouched != 0)
            {
                Vec.z = 0.0f;
                Vec.x = 0.0f;
            }
        }
        if (Root != nullptr)
            Root->Release();
    }
    float DurationOut = 0.0f;
    float Y = 0.0f;
    if (G_Animation->GetNamedDurationOut(Name, (const char*)Extra, &DurationOut) != 0)
        Y = DurationOut + BaseHeight;
    else
        Y = RootHeightOffset + BaseHeight;
    Vector Pos;
    Pos.x = BonePos->x + Vec.x;
    Pos.y = Vec.y + Y;
    Pos.z = BonePos->z + Vec.z;

    float Quat[4];
    float UpAxis[3];
    const float Twist = FloatFromOpaqueDword(ArgH);
    if (Twist < 0.0f)
    {
        float Row[3];
        WriteVec3(Row, Bone->LocalTransform.Rotation.E[0][2], Bone->LocalTransform.Rotation.E[1][2], Bone->LocalTransform.Rotation.E[2][2]);
        const float Angle = (float)(
            std::atan2((double)Row[0], (double)Row[2]));
        WriteVec3(UpAxis, 0.0f, 1.0f, 0.0f);
        AxisAngleToQuat(Quat, UpAxis, Angle);
    }
    else
    {
        const double Half = (double)Twist * 0.5;
        WriteVec3(UpAxis, 0.0f, 1.0f, 0.0f);
        Quat[0] = (float)(std::cos(Half));
        const float Scale = (float)(std::sin(Half) / std::sqrt(1.0));
        ScaleVec3(&Quat[1], UpAxis, Scale);
    }

    Transform Xform;
    I3DMathEngine* Math = AcquireMathEngine();
    Math->QuaternionToMatrix(Xform.Rotation,
                             *reinterpret_cast<const Quaternion*>(Quat)); // +0x54
    Xform.Translation.x = Pos.x;
    Xform.Translation.y = Pos.y;
    Xform.Translation.z = Pos.z;
    const long Applied = (long)(G_Animation->EvalPlayback(
        (unsigned long)Handle,
        (unsigned long)((ptrdiff_t)ArgG), ArgCFloat,
        FloatFromOpaqueDword(ArgE), ArgD, ArgF, AsOpaqueDword(1.0f), &Xform));
    if (Applied < 0)
    {
        G_Animation->DestroyScript((void*)((ptrdiff_t)Handle));
        return -1;
    }

    FIkListNode* Node = new FIkListNode;
    Node->Active = 0;
    LinkListNode(this, Node);
    Node->Event = reinterpret_cast<FIkEvent*>(((ptrdiff_t)Handle));
    std::memcpy(&Node->StartHeight, &Y, sizeof(float));
    return Handle;
}

// FUN_065f4580 (thiscall).
void WriteVec3(float* Dest, float X, float Y, float Z)
{
    Dest[0] = X;
    Dest[1] = Y;
    Dest[2] = Z;
}

// FUN_065f45a0.
void ScaleVec3(float* Out, const float* In, float Scale)
{
    Out[0] = In[0] * Scale;
    Out[1] = In[1] * Scale;
    Out[2] = In[2] * Scale;
}

// FUN_065f45d0 (thiscall on the quaternion {w,x,y,z}: Axis, Angle).
static void AxisAngleToQuat(float* Quat, const float* Axis, float Angle)
{
    const double Half = (double)Angle * 0.5;
    const double Length = std::sqrt((double)Axis[0] * Axis[0] +
                                    (double)Axis[1] * Axis[1] +
                                    (double)Axis[2] * Axis[2]);
    const float Scale = (float)(std::sin(Half) / Length);
    Quat[0] = (float)(std::cos(Half));
    Quat[1] = Scale * Axis[0];
    Quat[2] = Scale * Axis[1];
    Quat[3] = Scale * Axis[2];
}

// FUN_065f4660 (thiscall: Name, Extra, Time, ArgD, ArgE, ArgF, ArgG, ArgH, Joint).
int FDeformInstance::PlayAnimChain(void* Name, void* Extra, void* ArgC, void* ArgD, void* ArgE,
                                   void* ArgF, void* ArgG, void* ArgH, InstanceIndex Joint)
{
    const Vector* JointPos = G_Engine->GetInstancePosition(Joint);
    float StartPos[3];
    StartPos[0] = JointPos->x;
    StartPos[1] = JointPos->y;
    StartPos[2] = JointPos->z;

    const int PlayHandle =
        PlayAnim(Name, Extra, ArgC, ArgD, ArgE, ArgF, ArgG, ArgH, nullptr);
    const unsigned long Play = (unsigned long)PlayHandle;
    const float Time = FloatFromOpaqueDword(ArgC);

    const InstanceIndex Stop = FirstBoneInstance(this);

    // EngBase FAnimBindOut {Kind, Sink, SinkContext, Instance}; +0x10 of the frame is outside it.
    unsigned long Query[4];
    Query[0] = 0;
    Query[1] = 0;
    Query[2] = 0;
    Query[3] = 0xFFFFFFFF;

    std::vector<const unsigned char*> Relatives;
    std::vector<Quaternion> ChainQuats;
    InstanceIndex Cursor = Joint;
    while (Cursor != Stop)
    {
        const InstanceIndex Parent = G_Engine->GetInstanceParent(Cursor);
        Relatives.push_back(
            (const unsigned char*)(G_Engine->GetInstanceRelative(Cursor)));
        Query[3] = (unsigned long)((ptrdiff_t)Cursor);
        const unsigned long Channel = G_Animation->FindObject(Play, Query);
        IChannelArchetype* Archetype = nullptr;
        G_Channel->GetObject(Channel, (void**)(&Archetype));
        unsigned long Flags = 0;
        Archetype->GetFlags(&Flags);
        const unsigned char* KeyA = nullptr;
        const unsigned char* KeyB = nullptr;
        float Alpha = 0.0f;
        Archetype->Sample(Time, &KeyA, &KeyB, &Alpha);
        Archetype->Release();
        Quaternion Quat;
        AcquireMathEngine()->QuatSlerp(Quat, *reinterpret_cast<const Quaternion*>(KeyA),
                                       *reinterpret_cast<const Quaternion*>(KeyB), Alpha);
        ChainQuats.push_back(Quat);
        Cursor = Parent;
    }
    const int ChainCount = (int)(ChainQuats.size());

    Query[3] = (unsigned long)((ptrdiff_t)Stop);
    Query[0] = 1;
    const unsigned long RootChannel = G_Animation->FindObject(Play, Query);
    IChannelArchetype* RootArchetype = nullptr;
    G_Channel->GetObject(RootChannel, (void**)(&RootArchetype));
    unsigned long RootFlags = 0;
    RootArchetype->GetFlags(&RootFlags);
    const unsigned char* RootA = nullptr;
    const unsigned char* RootB = nullptr;
    float RootAlpha = 0.0f;
    RootArchetype->Sample(Time, &RootA, &RootB, &RootAlpha);
    const float* const PosA = (const float*)RootA;
    const float* const PosB = (const float*)RootB;

    float Diff[3];
    SubVec3(Diff, PosB, PosA);
    float Tail[3];
    ScaleVec3(Tail, Diff, RootAlpha);
    float Accum[3];
    WriteVec3(Accum, Tail[0] + PosA[0], Tail[1] + PosA[1], Tail[2] + PosA[2]);

    Quaternion RootQuat;
    AcquireMathEngine()->QuatSlerp(RootQuat, reinterpret_cast<const FPosQuatSample*>(RootA)->Quat,
                                   reinterpret_cast<const FPosQuatSample*>(RootB)->Quat, RootAlpha);
    RootArchetype->Release();

    Matrix Rot;
    AcquireMathEngine()->QuaternionToMatrix(Rot, RootQuat);
    Matrix Running = Rot;
    int Index = ChainCount - 1;
    while (Index >= 0)
    {
        Matrix Step;
        AcquireMathEngine()->QuaternionToMatrix(Step, ChainQuats[Index]);
        const FEngineJointBlock* const Relative =
            reinterpret_cast<const FEngineJointBlock*>(Relatives[Index]);
        Matrix Mul;
        AcquireMathEngine()->MulMatrix(Mul, Running, Step);
        AcquireMathEngine()->MulMatrix(Rot, Mul,
                                       Relative->Orient);
        Vector XfA;
        AcquireMathEngine()->TransformMatrix(XfA, Rot,
                                             Relative->ChildPos);
        Vector XfB;
        AcquireMathEngine()->TransformMatrix(XfB, Running,
                                             Relative->ParentPos);
        const float SumZ =
            (float)((double)XfB.z + (double)Accum[2]);
        const float X = (float)(
            (double)XfB.x + (double)Accum[0] - (double)XfA.x);
        const float Y = (float)(
            (double)XfB.y + (double)Accum[1] - (double)XfA.y);
        const float Z = (float)((double)SumZ - (double)XfA.z);
        Accum[0] = X;
        Accum[1] = Y;
        Accum[2] = Z;
        Tail[0] = X;
        Tail[1] = Y;
        Tail[2] = Z;
        Running = Rot;
        Index -= 1;
    }

    const float* const BonePos = &PartArray[0].Lod->BoneSlots[0].LocalTransform.Translation.x;
    const float DiffZ = (float)((double)StartPos[2] - BonePos[2]);
    Vector Delta;
    Delta.x = (float)((double)StartPos[0] - BonePos[0] -
                                  (double)Tail[0]);
    Delta.y = (float)((double)StartPos[1] - BonePos[1] -
                                  (double)Tail[1]);
    Delta.z = (float)((double)DiffZ - (double)Tail[2]);
    G_Animation->AddOverlayPos(Play, &Delta);
    return PlayHandle;
}

// FUN_065f4c50.
void SubVec3(float* Out, const float* A, const float* B)
{
    Out[0] = A[0] - B[0];
    Out[1] = A[1] - B[1];
    Out[2] = A[2] - B[2];
}

// The payload teardown every list walker inlines. An inactive node holds a script handle, which
// goes to IAnimation2 +0x28 (DestroyScript) with no test.
static void DestroyNodePayload(FIkListNode* Node)
{
    if (Node->Active != 0)
    {
        FIkEvent* Event = Node->Event;
        if (Event != nullptr)
            delete Event;                                        // FUN_065fbf40 + operator delete
        Node->Event = nullptr;
        return;
    }
    G_Animation->DestroyScript(Node->Event);
    Node->Event = reinterpret_cast<FIkEvent*>(((ptrdiff_t)-1));
}

// The payload teardown, then the inlined unlink and operator delete.
static void DeleteListNode(FDeformInstance* Instance, FIkListNode* Node)
{
    DestroyNodePayload(Node);
    UnlinkListNode(Instance, Node);
    delete Node;
}

// FUN_065f4c80. No test of the instance.
void FDeformInstance::DestroyAllList()
{
    FIkListNode* Node = ListHead;
    while (Node != nullptr)
    {
        FIkListNode* Next = Node->Next;
        DeleteListNode(this, Node);
        Node = Next;
    }
}

// FUN_065f4d30: only the first node whose handle matches goes.
void FDeformInstance::DestroyListByHandle(int Handle)
{
    FIkListNode* Node = ListHead;
    while (Node != nullptr)
    {
        FIkListNode* Next = Node->Next;
        const int Stored = (int)((ptrdiff_t)Node->Event);
        if (Stored == Handle)
        {
            DeleteListNode(this, Node);
            return;
        }
        Node = Next;
    }
}

// FUN_065f4de0.
void FDeformInstance::SweepExpiredList()
{
    FIkListNode* Node = ListHead;
    while (Node != nullptr)
    {
        FIkListNode* Next = Node->Next;
        bool bDrop = false;
        if (Node->Active != 0)
        {
            FIkEvent* Event = Node->Event;
            if ((Event->StateFlags & kIkStateStopped) != 0 &&
                (unsigned char)(G_Channel->InMixer((unsigned long)(
                    (ptrdiff_t)Event->Channels[0]))) == 0)
                bDrop = true;
        }
        else if (G_Animation->FilterObjects(Node->Event) != 0)
            bDrop = true;
        if (bDrop)
            DeleteListNode(this, Node);
        Node = Next;
    }
    if (ListCount == 0)
    {
        RootHeightOffset = PartArray[0].Lod->BoneSlots[0].LocalTransform.Translation.y - BaseHeight;
    }
}

// IChannelSink slot 0 (called by EngBase FUN_0661b420), FUN_065f5210.
void DACOM_API FDeformInstance::OnKeys(unsigned long ChannelKey, void* Context,
                                       FChannelKeyBatch* Batch)
{
    if (ForwardSink == nullptr)
        return;
    unsigned long Cookie = 0;
    FIkListNode* Node = ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
        {
            const unsigned long Handle =
                (unsigned long)((ptrdiff_t)Node->Event);
            if (G_Animation->HasObject(Handle, AsOpaqueDword(ChannelKey)) != 0)
            {
                G_Animation->GetFirstInstanceField(Handle, &Cookie);
                break;
            }
        }
        Node = Node->Next;
    }
    ForwardSink->OnKeys(Cookie, Context, Batch);
}

// IChannelSink slot 1, LAB_065f5290: tail-jumps to ForwardSink's slot 1.
void DACOM_API FDeformInstance::OnOpaqueNotification(void* A, void* B)
{
    if (ForwardSink == nullptr)
        return;
    ForwardSink->OnOpaqueNotification(A, B);
}

// IChannelSink slot 2 (called by EngBase FUN_0661b420), LAB_065f52b0.
void DACOM_API FDeformInstance::OnLoop(unsigned long ChannelKey, Transform* Xform, void* Context)
{
    int Handle = -1;
    FIkListNode* Node = ListHead;
    while (Node != nullptr)
    {
        if (Node->Active == 0)
        {
            const unsigned long Stored =
                (unsigned long)((ptrdiff_t)Node->Event);
            if (G_Animation->HasObject(Stored, AsOpaqueDword(ChannelKey)) != 0)
            {
                Handle = (int)Stored;
                break;
            }
        }
        Node = Node->Next;
    }
    if (Node == nullptr)
        return;

    float Duration = 0.0f;
    float Height = 0.0f;
    if (G_Animation->GetDurationIfNamed((unsigned long)Handle, &Duration) != 0)
        Height = Duration + BaseHeight;
    else
        Height = RootHeightOffset + BaseHeight;
    Xform->Translation.y = Height;
    const double Half =
        std::atan2((double)Xform->Rotation.E[0][2], (double)Xform->Rotation.E[2][2]) *
        0.5;
    const float SinScaled = (float)(std::sin(Half) / std::sqrt(1.0));
    Quaternion Quat;
    Quat.w = (float)(std::cos(Half));
    const float Side = SinScaled * 0.0f;
    WriteVec3(&Quat.x, Side, SinScaled, Side);
    Matrix Mat;
    I3DMathEngine* Math = AcquireMathEngine();
    Math->QuaternionToMatrix(Mat,
                             Quat); // +0x54
    std::memcpy(&Xform->Rotation, &Mat, sizeof(Mat));
    if (ForwardSink == nullptr)
        return;
    ForwardSink->OnLoop(ChannelKey, Xform, Context);
}

// The Euler rebuild FUN_065f5400 and FUN_065fb900 inline.
void IkEulerRebuild(Matrix* Out, float Yaw, float Pitch, float Roll)
{
    const double SinY = std::sin((double)Yaw);
    const double CosY = std::cos((double)Yaw);
    const double SinP = std::sin((double)Pitch);
    const double CosP = std::cos((double)Pitch);
    const double SinR = std::sin((double)Roll);
    const double CosR = std::cos((double)Roll);
    const double SinPSinY = SinP * SinY;
    const float SinRCosY = (float)(SinR * CosY);
    const float CosRCosY = (float)(CosR * CosY);
    Out->E[0][0] = (float)(CosR * CosP);
    Out->E[0][1] = (float)(SinPSinY * CosR - (double)SinRCosY);
    Out->E[0][2] = (float)((double)CosRCosY * SinP + SinR * SinY);
    Out->E[1][0] = (float)(SinR * CosP);
    Out->E[1][1] = (float)(SinPSinY * SinR + (double)CosRCosY);
    Out->E[1][2] = (float)((double)SinRCosY * SinP - CosR * SinY);
    Out->E[2][0] = (float)-SinP;
    Out->E[2][1] = (float)(CosP * SinY);
    Out->E[2][2] = (float)(CosP * CosY);
}

static const char* const G_StartAimBlocked = "deform::start_aim(): instance blocked -- exiting";

void IkMathQuatToMatrix(Matrix* Out, const Quaternion* Quat)
{
    AcquireMathEngine()->QuaternionToMatrix(*Out,
                                            *Quat); // +0x54
}

// The StartAim descriptor FUN_065f5400 reads (+0x00 is not read).
struct FAimDesc
{
    unsigned long Unread00;  // +0x00
    int StartJoint;          // +0x04 the chain root; the walk stops there
    int EndJoint;            // +0x08 the end joint; the walk starts there
    unsigned long Flags;     // +0x0C FIkEvent::AimFlags
    Vector* TargetA;         // +0x10 point or direction
    Matrix* TargetB;         // +0x14 orientation target
    float Damping;           // +0x18
    int LockCount;           // +0x1C
    int LockIds[1];          // +0x20 LockCount joint ids
};

static_assert(offsetof(FAimDesc, LockIds) == 0x20, "aim desc lock ids +20");

// FUN_065f5400 (thiscall on the instance: Desc, PlayExtra).
FIkListNode* FDeformInstance::AllocAimEvent(const unsigned char* Desc, void* PlayExtra)
{
    const FAimDesc* const Aim = reinterpret_cast<const FAimDesc*>(Desc);
    int BindB = -1;
    if (PartArray != nullptr)
        BindB = (int)((ptrdiff_t)(PartArray[0].Lod->BoneSlots[0].Instance));

    std::vector<int> Chain;
    unsigned char bSphereAxis = 0;
    int Cursor = Aim->EndJoint;
    if (Cursor != Aim->StartJoint)
    {
        int Parent = 0;
        do
        {
            if (G_Animation != nullptr)
            {
                unsigned char bBlocked = 0;
                G_Animation->HasHandle((unsigned long)Cursor, &bBlocked);
                if (bBlocked != 0)
                {
                    int WalkBack = Aim->EndJoint;
                    while (WalkBack != Cursor)
                    {
                        G_Animation->HasAnyHandle((unsigned long)WalkBack);
                        WalkBack = (int)((ptrdiff_t)(
                            G_Engine->GetInstanceParent(WalkBack)));
                    }
                    FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x44D,
                          G_StartAimBlocked);
                    return reinterpret_cast<FIkListNode*>(((ptrdiff_t)-1));
                }
                G_Animation->RegisterHandle((unsigned long)Cursor);
            }
            Chain.push_back(Cursor);
            const InstanceIndex CursorInstance = Cursor;
            Parent = (int)((ptrdiff_t)(G_Engine->GetInstanceParent(CursorInstance)));
            const FEngineJointBlock* Relative = G_Engine->GetInstanceRelative(CursorInstance);
            if (Relative->Type == kJointTypeSphere)
            {
                const Vector* Axis = &Relative->ChildPos;
                bSphereAxis = 1;
                const double X = (double)Axis->x;
                const double Y = (double)Axis->y;
                const double Z = (double)Axis->z;
                const double Length = std::sqrt(X * X + Y * Y + Z * Z);
                if (!(Length > 1.0e-5))
                    bSphereAxis = 0;
            }
            Cursor = Parent;
        } while (Parent != Aim->StartJoint);
    }
    const int Count = (int)(Chain.size());
    std::vector<int> JointIds(Chain.size());
    for (int Index = 0; Index < Count; ++Index)
        JointIds[Index] = Chain[Count - 1 - Index];

    FIkListNode* Node = new FIkListNode;
    std::memset(Node, 0, sizeof(FIkListNode));
    Node->Active = 0;
    Node->Event = reinterpret_cast<FIkEvent*>(((ptrdiff_t)-1));
    LinkListNode(this, Node);
    Node->Active = 1;

    // operator new 0x44 (not cleared), then the inlined constructor (vtable 06601340).
    FIkEvent* Event = new FIkEvent;
    Event->TargetB = Aim->TargetB;
    Event->StateFlags = (unsigned char)(Event->StateFlags & (unsigned char)~kIkStateStopped);
    Event->TargetA = Aim->TargetA;
    Event->ApplyTick = 0;
    Node->Event = Event;
    Event->Owner = Node;

    const size_t Count4 = (size_t)Count * 4;
    Event->JointIds = (int*)new unsigned char[Count4];
    if (Count > 0)
        std::memcpy(Event->JointIds, &JointIds[0], Count4);
    Event->Channels = (void**)new unsigned char[Count4];
    Event->Quats = reinterpret_cast<Quaternion*>(new unsigned char[(size_t)Count * 0x10]);
    Event->AimFlags = Aim->Flags;
    Event->StartJoint = Aim->StartJoint;
    Event->EndJoint = Aim->EndJoint;
    Event->BindMats = reinterpret_cast<Matrix*>(new unsigned char[(size_t)Count * 0x24]);
    Event->LockBytes = new unsigned char[(size_t)Count];
    std::memset(Event->LockBytes, 0, (size_t)Count);
    Event->Damping = Aim->Damping;
    Event->LockCount = Aim->LockCount;

    if (!Event->SetJointCount((unsigned int)Count))
    {
        StopAimNode(Node);
        return reinterpret_cast<FIkListNode*>(((ptrdiff_t)-1));
    }

    unsigned long PlayBlob[7];
    PlayBlob[0] = 0;
    PlayBlob[1] = (Aim->Flags >> 1) & 0x80u;
    const float One = 1.0f;
    std::memcpy(&PlayBlob[2], &One, sizeof(One));
    PlayBlob[3] = (unsigned long)((ptrdiff_t)PlayExtra);
    std::memcpy(&PlayBlob[4], &One, sizeof(One));
    std::memcpy(&PlayBlob[5], &One, sizeof(One));
    PlayBlob[6] = 0;
    unsigned long CreateCookie[4];
    CreateCookie[0] = 0;
    CreateCookie[1] = 0;
    CreateCookie[2] = 0;
    CreateCookie[3] = (unsigned long)-1;
    for (int Index = 0; Index < Count; ++Index)
    {
        const int JointId = JointIds[Index];
        const InstanceIndex JointInstance = JointId;
        CreateCookie[3] = (unsigned long)JointId;
        const unsigned long Key = G_Channel->CreateTyped(
            Event, 4, CreateCookie, (void*)((ptrdiff_t)BindB));
        Event->Channels[Index] = (void*)((ptrdiff_t)Key);
        G_Channel->Play(Key, (float*)PlayBlob);

        const float* Relative = (const float*)(G_Engine->GetInstanceRelative(JointInstance));
        // (hi + lo) * 0.5 (DAT_06601338) in double, rounded to float once.
        const float Yaw = (float)(
            ((double)Relative[0x50 / 4] + (double)Relative[0x4C / 4]) * 0.5);
        const float Pitch = (float)(
            ((double)Relative[0x58 / 4] + (double)Relative[0x54 / 4]) * 0.5);
        const float Roll = (float)(
            ((double)Relative[0x44 / 4] + (double)Relative[0x40 / 4]) * 0.5);
        // Written into BindMats[i], then overwritten whole below before anything reads it;
        // retail does both.
        IkEulerRebuild(&Event->BindMats[Index], Yaw, Pitch, Roll);

        // Retail's target is [E+0x78] with the matrix at [E+0x88]: a loose joint (seven
        // floats from EngBase's GetJointParams) runs into the matrix slot before
        // QuaternionToMatrix rewrites it, so the buffer here holds seven.
        float JointQuat[7];
        G_Engine->GetJointParams(JointInstance, 0, JointQuat);
        Matrix LiveMat;
        IkMathQuatToMatrix(&LiveMat, reinterpret_cast<const Quaternion*>(JointQuat));
        std::memcpy(&Event->BindMats[Index], &LiveMat, sizeof(LiveMat));

        const int LockCount = Aim->LockCount;
        const int* LockIds = Aim->LockIds;
        for (int Lock = 0; Lock < LockCount; ++Lock)
        {
            if (LockIds[Lock] == JointId)
            {
                Event->LockBytes[Index] = 1;
                break;
            }
        }
    }
    Event->StateFlags = (unsigned char)((((bSphereAxis << 1) ^ Event->StateFlags) & 2) ^
                                                Event->StateFlags);
    return Node;
}

// FUN_065f5950 (thiscall; the instance in ECX is never read).
void StopAimNode(FIkListNode* Node)
{
    if (Node == reinterpret_cast<FIkListNode*>(((ptrdiff_t)-1)))
        return;
    Node->Event->StateFlags = (unsigned char)(Node->Event->StateFlags | 1);
    if (G_Animation == nullptr)
        return;
    int Cursor = Node->Event->EndJoint;
    while (Cursor != Node->Event->StartJoint)
    {
        G_Animation->HasAnyHandle((unsigned long)Cursor);
        Cursor = (int)((ptrdiff_t)(
            G_Engine->GetInstanceParent(Cursor)));
    }
}

// Context for the LAB_065f59b0 thunk.
struct FHardpointWalkCtx
{
    FHardpointUserFn UserFn;
    void* UserData;
    InstanceIndex BoneInst;
};

// LAB_065f59b0 (cdecl): calls UserFn(bone instance, name, user data). The first argument is the
// hardpoint's NAME, not a blob (FHardpointEnumProc).
static void __cdecl HardpointWalkThunk(char const* Name, void* Context)
{
    FHardpointWalkCtx* Ctx = static_cast<FHardpointWalkCtx*>(Context);
    Ctx->UserFn(Ctx->BoneInst, Name, Ctx->UserData);
}

// FUN_065f59d0 (thiscall on the instance: UserFn, UserData).
unsigned char FDeformInstance::WalkHardpoints(FHardpointUserFn UserFn, void* UserData)
{
    IHardpoint* Hardpoint = nullptr;
    if (G_Engine->QueryInterface(Iids::Hardpoint, (void**)(&Hardpoint)) < 0)
    {
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x4EF,
              "Deform: Unable to acquire IID_IHardpoint, unable to continue");
        if (Hardpoint != nullptr)
            Hardpoint->Release();
        return 0;
    }

    int PartIndex = 0;
    while (PartIndex < PartCount)
    {
        FLodHolder* Lod = PartArray[PartIndex].Lod;
        int Bone = 0;
        while (Bone < Lod->BindCursor)
        {
            FHardpointWalkCtx Ctx;
            Ctx.UserFn = UserFn;
            Ctx.UserData = UserData;
            Ctx.BoneInst = Lod->BoneSlots[Bone].Instance;
            Hardpoint->EnumerateHardpoints(
                &HardpointWalkThunk,
                (ptrdiff_t)(Lod->Skeleton->Bones[Bone].Parent),
                &Ctx);
            Bone += 1;
        }
        PartIndex += 1;
    }
    if (Hardpoint != nullptr)
        Hardpoint->Release();
    return 1;
}
