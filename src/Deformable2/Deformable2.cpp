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

namespace
{
    void LogDeformError(int Line, const char* Message)
    {
        if (FDUMP != nullptr)
        {
            // .data:06602070: the __FILE__ of the three Initialize sites (lines 0xB5, 0xCC, 0xF4).
            FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                  "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\Deformable.cpp",
                  Line, Message);
        }
    }

    bool IidEquals(const char* Left, const char* Right)
    {
        if (Left == nullptr || Right == nullptr)
            return false;
        return std::strcmp(Left, Right) == 0;
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

// DAT_06603128 .. DAT_0660314c — module cache written by FUN_065f1810 / FUN_065f1900 / LAB_065f1bf0.
static IDAComponent* G_System = nullptr;
static IMaterialLibrary* G_MaterialLibrary = nullptr;
static IRenderPipeline* G_Pipeline = nullptr;
// EngBase's IEngine, acquired by QI. Every slot this module calls (+0x20, +0x24,
// +0x30, +0x3C, +0x48, +0x60) is IEngine's.
IEngine* G_Engine = nullptr;
// EngBase's IAnimation2, acquired by QI (Iids::Animation2).
static IAnimation2* G_Animation = nullptr;
// EngBase's IChannel, the facet at Animation+8. All five slots this module drives
// (3, 5, 6, 7, 15) match engbase.h's declaration argument for argument.
static IChannel* G_Channel = nullptr;
static IRenderer2* G_Renderer = nullptr;
static IRPVertexBuffer* G_VertexBuffer = nullptr;
static IRPIndexBuffer* G_IndexBuffer = nullptr;
static I3DMathEngine* G_MathEngine = nullptr; // DAT_066033a8 FUN_06600de0
static float G_DeformRate = 0.0f;


// IK current and trial poses; row zero holds the start joint.
static float G_IkCurRot[G_IkMaxJoints + 1][9];
static float G_IkCurPos[G_IkMaxJoints + 1][3];
static float G_IkNewPos[G_IkMaxJoints + 1][3];
static float G_IkNewRot[G_IkMaxJoints + 1][9];

// Source paths used by diagnostics.
static const char* const G_FdumpFileDeformableMesh =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformableMesh.cpp";
static const char* const G_FdumpFileDeformablePart =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformablePart.cpp";
static const char* const G_FdumpFileDeformablePartArchetype =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformablePartArchetype.cpp";
static const char* const G_FdumpFileMat =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\Mat.h";
// .data:06602570 and .data:066026dc: the __FILE__ and message FUN_065f5400 pushes at line 0x44D.
static const char* const G_FdumpFileDeformableObject =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformableObject.cpp";
static const char* const G_StartAimBlocked = "deform::start_aim(): instance blocked -- exiting";
// .data:066029e8: the __FILE__ FUN_065fb030 pushes at line 0x15.
static const char* const G_FdumpFileDeformIk =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\deformIK.cpp";
static const char* const G_RateCharset = "0123456789truefalsynoxTRUEFALSYNOX+-";
static const float G_RandToUnit = 3.051851e-05f;
static const int G_PartDescStride = 0x18;
static const int G_MaxLodCount = 8;
static const unsigned long G_ParseTagUtf = 0x112;
// Return to the parent UTF node after loading a child.
static const char* const G_ParentNode = "..";


// DAT_06603158: the mesh cache -- an STL map from CRC32(name) to the mesh (FUN_065f2a40 find,
// FUN_065f2ab0 insert, FUN_065f3440/3320 + FUN_065f2cc0 erase). Both kinds of FDeformMesh
// live in it. The port uses the current std::map; no VC6 layout is kept.
static std::map<unsigned long, FDeformMesh*> G_MeshCache;


// FUN_065f28e0.
void FDeformMesh::AddRef()
{
    RefCount += 1;
}

// FUN_065f28f0.
int FDeformMesh::Release()
{
    RefCount -= 1;
    const int Remaining = RefCount;
    if (Remaining > 0)
        return Remaining;
    G_MeshCache.erase(NameCrc);
    delete this;
    return Remaining;
}

// PTR_LAB_06601378 -- the base's slots.
FDeformMesh::FDeformMesh(const char* Name)
    : RefCount(1)
    , NameCrc(DACOM_CRC::GetCRC32(Name))
{
}

// LAB_065f8ca0.
int FDeformMesh::GetIndexCount()
{
    return 0;
}

// LAB_065f8ca0 (the same body in slot 1).
int FDeformMesh::GetVertexCapacity()
{
    return 0;
}

// LAB_065f8cb0.
unsigned char FDeformMesh::FillIndexBuffer(FDeformIbDesc* /*Ib*/)
{
    return 0;
}

// LAB_065f8cc0: three arguments (RET 0xC), as FUN_065f69d0 pushes them.
unsigned char FDeformMesh::Skin(FLodHolder* /*Holder*/, FDeformVbDesc* /*Vb*/,
                                unsigned char /*bForce*/)
{
    return 0;
}

// LAB_065f8cd0.
unsigned char FDeformMesh::GetBoundSphere(float* /*OutRadius*/, float* /*OutCenter*/)
{
    return 0;
}

// FUN_065f8ce0: no fields to free.
FDeformMesh::~FDeformMesh()
{
}

// FUN_065f9170. Retail does not test the slot pointer itself for null.
static void ReleaseFaceGroupMaterialSlot(IMaterial** MaterialSlot)
{
    if (*MaterialSlot == nullptr)
        return;
    IMaterial* Material = *MaterialSlot;
    Material->Release();
    *MaterialSlot = nullptr;
}

// FUN_065f9430. Retail does not test the list pointer for null.
void FBoneVertexList::Destroy()
{
    delete[] (unsigned char*)Start;
    Start = nullptr;
    Finish = nullptr;
    CapacityEnd = nullptr;
}

// FUN_065f9ba0 with flags 0 (bit 0 would also delete the list). Retail does not test the list
// pointer for null.
static void DestroyVertexRemapList(FVertexRemapList* List)
{
    delete[] (unsigned char*)List->Start;
    List->Start = nullptr;
    List->Finish = nullptr;
    List->CapacityEnd = nullptr;
}

// FUN_065f8b90 (via FUN_065f8b70, vtable slot 6). The stream buffers are deleted unconditionally.
FMeshArchetype::~FMeshArchetype()
{
    if (FaceGroups != nullptr)
    {
        int* Header = (int*)FaceGroups - 1;
        int Count = *Header;
        int Index = Count - 1;
        while (Index >= 0)
        {
            FFaceGroup* Group = &FaceGroups[Index];
            delete[] (unsigned char*)Group->Indices;
            ReleaseFaceGroupMaterialSlot(&Group->Material);
            Index -= 1;
        }
        delete[] (unsigned char*)Header;
        FaceGroups = nullptr;
        FaceGroupCount = 0;
    }

    delete[] (unsigned char*)PointIndices;
    delete[] (unsigned char*)Vertices;
    delete[] (unsigned char*)SkinPairs;
    delete[] (unsigned char*)SkinTable;
    delete[] (unsigned char*)UvIndices;
    delete[] (unsigned char*)UvData;
    delete[] (unsigned char*)UvDeform;

    if (BoneVerts != nullptr)
    {
        int* Header = (int*)BoneVerts - 1;
        int Count = *Header;
        int Index = Count - 1;
        while (Index >= 0)
        {
            BoneVerts[Index].Destroy();
            Index -= 1;
        }
        delete[] (unsigned char*)Header;
        BoneVerts = nullptr;
    }

    FVertexRemapList* RemapCursor = RemapStart;
    FVertexRemapList* RemapEnd = RemapFinish;
    while (RemapCursor != RemapEnd)
    {
        DestroyVertexRemapList(RemapCursor);
        RemapCursor += 1;
    }
    delete[] (unsigned char*)RemapStart;
    RemapStart = nullptr;
    RemapFinish = nullptr;
    RemapCapacity = nullptr;

    delete[] FaceFlags;
    FaceFlags = nullptr;
    PointIndices = nullptr;
    Vertices = nullptr;
    SkinPairs = nullptr;
    SkinTable = nullptr;
    UvIndices = nullptr;
    UvData = nullptr;
    UvDeform = nullptr;
}

void FillFileDesc(FFileSystemDesc* Desc, const char* Path)
{
    std::memset(Desc, 0, sizeof(FFileSystemDesc));
    Desc->Size = 0x34;
    Desc->ComponentName = G_FileSystemName;
    Desc->Path = Path;
    Desc->Access = GENERIC_READ;
    Desc->ShareMode = FILE_SHARE_READ;
    Desc->Creation = OPEN_EXISTING;
    Desc->Flags = FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL;
    Desc->FindHandle = 0xFFFFFFFF;
}

// Enumerate FileSystem children using the published ReadFile contract.
static int FsFindFirst(IFileSystem* Fs, const char* Pattern, WIN32_FIND_DATAA* FindData)
{
    return Fs->FindFirst(Pattern,
                      FindData);
}

static int FsFindNext(IFileSystem* Fs, int Finder, WIN32_FIND_DATAA* FindData)
{
    return Fs->FindNext((unsigned long)Finder, FindData);
}

static void FsFindClose(IFileSystem* Fs, int Finder)
{
    Fs->FindClose((unsigned long)Finder);
}

// FUN_065f71c0: texcoord extra from FVF bits 8–11 count, sizes at bit 16+.
static int FvfTexcoordBytes(unsigned long Fvf)
{
    int Bytes = 0;
    unsigned int SetCount = (Fvf >> 8) & 0xF;
    unsigned int Bit = 0x10;
    while (SetCount != 0)
    {
        switch ((Fvf >> (Bit & 31)) & 3)
        {
        case 0:
            Bytes += 8;
            break;
        case 1:
            Bytes += 0xC;
            break;
        case 2:
            Bytes += 0x10;
            break;
        default:
            Bytes += 4;
            break;
        }
        Bit += 2;
        SetCount -= 1;
    }
    return Bytes;
}

// FUN_065f7220: D3DFVF_DIFFUSE (0x40) / D3DFVF_SPECULAR (0x80).
static int FvfColorBytes(unsigned long Fvf)
{
    int Bytes = 0;
    if ((Fvf & 0x40) != 0)
        Bytes += 4;
    if ((Fvf & 0x80) != 0)
        Bytes += 4;
    return Bytes;
}

// LAB_065f6bf0 FVF stride. ParseTag 0x112 → xyz 0xC + nrm 0xC + uv0 8 = 0x20.
static int ComputeFvfStride(unsigned long Fvf)
{
    int Position = 0;
    if ((Fvf & 0xC) == 0xC)
        Position = 0x1C;
    else if ((Fvf & 4) == 4)
        Position = 0x10;
    else if ((Fvf & 2) == 2)
        Position = 0xC;
    int Normal = ((Fvf & 0x10) != 0) ? 0xC : 0;
    return Position + Normal + FvfColorBytes(Fvf) + FvfTexcoordBytes(Fvf);
}

// FUN_065f96b0 call-site in FUN_065f89e0: insert 1 ushort at vector finish (push_back).
static void BoneListPush(FBoneVertexList* List, unsigned short VertexIndex)
{
    if (List == nullptr)
        return;
    int Used = 0;
    int Cap = 0;
    if (List->Start != nullptr && List->Finish != nullptr)
        Used = (int)(List->Finish - List->Start);
    if (List->Start != nullptr && List->CapacityEnd != nullptr)
        Cap = (int)(List->CapacityEnd - List->Start);
    if (Used >= Cap)
    {
        int NewCap = (Cap <= 0) ? 4 : Cap * 2;
        unsigned short* NewBuf = (unsigned short*)new (std::nothrow) unsigned char[(size_t)NewCap * sizeof(unsigned short)];
        if (NewBuf == nullptr)
            return;
        if (List->Start != nullptr && Used > 0)
            std::memcpy(NewBuf, List->Start, (size_t)Used * sizeof(unsigned short));
        if (List->Start != nullptr)
            delete[] (unsigned char*)List->Start;
        List->Start = NewBuf;
        List->Finish = NewBuf + Used;
        List->CapacityEnd = NewBuf + NewCap;
    }
    if (List->Finish == nullptr)
        return;
    *List->Finish = VertexIndex;
    List->Finish += 1;
}

// FUN_065f89e0 tail: a counted array of Holder+8 (the bound bone count) vertex lists.
static void BuildBoneVertexLists(FMeshArchetype* Arch, FLodHolder* Holder, IFileSystem* Fs)
{
    const int BoneCount = Holder->BindCursor;
    const unsigned char AllocatorByte = (unsigned char)((uintptr_t)Fs);
    unsigned char* Block = new unsigned char[(size_t)BoneCount * sizeof(FBoneVertexList) + sizeof(int)];
    *(int*)Block = BoneCount;
    FBoneVertexList* Lists = (FBoneVertexList*)(Block + sizeof(int));
    int ListIndex = 0;
    while (ListIndex < BoneCount)
    {
        Lists[ListIndex].Allocator = AllocatorByte;
        Lists[ListIndex].Start = nullptr;
        Lists[ListIndex].Finish = nullptr;
        Lists[ListIndex].CapacityEnd = nullptr;
        ListIndex += 1;
    }
    Arch->BoneVerts = Lists;

    unsigned int Vertex = 0;
    while (Vertex < (unsigned int)Arch->VertexCount)
    {
        const FSkinVertex* Skin = &Arch->SkinTable[Vertex];
        int Index = 0;
        while (Index < Skin->BoneCount)
        {
            BoneListPush(&Lists[Skin->Pairs[Index].BoneId], (unsigned short)Vertex);
            Index += 1;
        }
        Vertex += 1;
    }
}

static void RemapListPush(FVertexRemapList* List, unsigned int PointIndex)
{
    if (List == nullptr)
        return;
    int Used = 0;
    int Cap = 0;
    if (List->Start != nullptr && List->Finish != nullptr)
        Used = (int)(List->Finish - List->Start);
    if (List->Start != nullptr && List->CapacityEnd != nullptr)
        Cap = (int)(List->CapacityEnd - List->Start);
    if (Used >= Cap)
    {
        int NewCap = (Cap <= 0) ? 4 : Cap * 2;
        unsigned int* NewBuf = (unsigned int*)new (std::nothrow) unsigned char[(size_t)NewCap * sizeof(unsigned int)];
        if (NewBuf == nullptr)
            return;
        if (List->Start != nullptr && Used > 0)
            std::memcpy(NewBuf, List->Start, (size_t)Used * sizeof(unsigned int));
        if (List->Start != nullptr)
            delete[] (unsigned char*)List->Start;
        List->Start = NewBuf;
        List->Finish = NewBuf + Used;
        List->CapacityEnd = NewBuf + NewCap;
    }
    if (List->Finish == nullptr)
        return;
    *List->Finish = PointIndex;
    List->Finish += 1;
}

// FUN_065f75f0: remap[PointIndices[i]].push(i). vector<uint> @ mesh+0x64.
static void BuildVertexRemap(FMeshArchetype* Arch)
{
    if (Arch->VertexCount == 0)
        return;

    const size_t VertexCount = (size_t)((unsigned int)Arch->VertexCount);
    FVertexRemapList* Lists =
        (FVertexRemapList*)new unsigned char[VertexCount * sizeof(FVertexRemapList)];
    std::memset(Lists, 0, VertexCount * sizeof(FVertexRemapList));
    Arch->RemapStart = Lists;
    Arch->RemapFinish = Lists + VertexCount;
    Arch->RemapCapacity = Arch->RemapFinish;

    const unsigned long* Indices = Arch->PointIndices;
    const unsigned int Count = (unsigned int)Arch->PointIndexCount;
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        const unsigned int Vertex = Indices[Index];
        if (Vertex < (unsigned int)Arch->VertexCount)
            RemapListPush(&Lists[Vertex], Index);
    }
}

// FUN_065f7590: a face group whose material's ClassName (slot 20) is "EyeMaterial"
// (.data:06602790) gets UvBoneIndex (+0x20) = 0. Retail returns AL = 1.
static void BindUvBoneIdsToFaceGroups(FMeshArchetype* Arch)
{
    for (int Index = 0; Index < Arch->FaceGroupCount; ++Index)
    {
        FFaceGroup* Group = &Arch->FaceGroups[Index];
        if (Group->Material == nullptr)
            continue;
        const char* Name = Group->Material->ClassName();
        // strncmp over 12 == REPE CMPSB over 12 here: the 12th byte of "EyeMaterial" is its
        // NUL, so both stop at the same first difference.
        if (std::strncmp(Name, G_EyeMaterialName, 12) == 0)
            Group->UvBoneIndex = 0;
    }
}

// PTR_LAB_0660135c slot 0 (LAB_065f8b40) and slot 1 (LAB_065f8b60).
// PTR_LAB_0660135c slot 0 LAB_065f8b40. Retail does not test the mesh or the group array for
// null.
int FMeshArchetype::GetIndexCount()
{
    int Total = 0;
    for (int Index = 0; Index < FaceGroupCount; ++Index)
        Total += (int)(FaceGroups[Index].IndexCount);
    return Total;
}

// PTR_LAB_0660135c slot 1 LAB_065f8b60: [this+0x20] + [this+0x10] (see Field_0x20). Retail
// does not test the mesh for null.
int FMeshArchetype::GetVertexCapacity()
{
    return (int)Field_0x20 + PointIndexCount;
}

static void WriteVec3(float* Dest, float X, float Y, float Z);
static void ScaleVec3(float* Out, const float* In, float Scale);
static void SubVec3(float* Out, const float* A, const float* B);

// Slot 4 (LAB_065f8d00).
// LAB_065f8d00, the DFM mesh's slot 4.
unsigned char FMeshArchetype::GetBoundSphere(float* OutRadius, float* OutCenter)
{
    if (SphereReady == 0)
    {
        const unsigned int Count = (unsigned int)VertexCount;
        const float* const Points = (const float*)Vertices;
        SphereCenter[2] = 0.0f;
        SphereCenter[1] = 0.0f;
        SphereCenter[0] = 0.0f;
        SphereRadius = 0.0f;
        const unsigned char bNonEmpty = (Count != 0) ? 1 : 0;
        if (bNonEmpty != 0)
        {
            unsigned int MinX = 0;
            unsigned int MaxX = 0;
            unsigned int MinY = 0;
            unsigned int MaxY = 0;
            unsigned int MinZ = 0;
            unsigned int MaxZ = 0;
            unsigned int Index = 1;
            while (Index < Count)
            {
                const float* const P = Points + Index * 3;
                if ((double)P[0] < Points[MinX * 3])
                    MinX = Index;
                else if ((double)P[0] > Points[MaxX * 3])
                    MaxX = Index;
                if ((double)P[1] < Points[MinY * 3 + 1])
                    MinY = Index;
                else if ((double)P[1] > Points[MaxY * 3 + 1])
                    MaxY = Index;
                if ((double)P[2] < Points[MinZ * 3 + 2])
                    MinZ = Index;
                else if ((double)P[2] > Points[MaxZ * 3 + 2])
                    MaxZ = Index;
                Index += 1;
            }
            const float Dx = Points[MaxX * 3] - Points[MinX * 3];
            const float Dy = Points[MaxY * 3 + 1] - Points[MinY * 3 + 1];
            const float Dz = Points[MaxZ * 3 + 2] - Points[MinZ * 3 + 2];
            float Sum[3];
            Sum[0] = Points[MinX * 3] + Points[MaxX * 3];
            Sum[1] = Points[MaxY * 3 + 1] + Points[MinY * 3 + 1];
            Sum[2] = Points[MaxZ * 3 + 2] + Points[MinZ * 3 + 2];
            float Center[3];
            ScaleVec3(Center, Sum, 0.5f);
            SphereCenter[0] = Center[0];
            SphereCenter[1] = Center[1];
            SphereCenter[2] = Center[2];
            float Extent = (Dx > Dy) ? Dx : Dy;
            if (!(Extent > Dz))
                Extent = Dz;
            const double Radius = (double)Extent * 0.5;
            SphereRadius = (float)Radius;
            float RadiusSq = (float)(Radius * Radius);

            Index = 0;
            while (Index < Count)
            {
                const float* const P = Points + Index * 3;
                float D[3];
                SubVec3(D, P, SphereCenter);
                const double DistSq = ((double)D[1] * D[1] + (double)D[2] * D[2]) +
                                      (double)D[0] * D[0];
                if (DistSq > (double)RadiusSq)
                {
                    const double Dist = std::sqrt(DistSq);
                    const float DistF = (float)Dist;
                    const double NewRadius =
                        (Dist + (double)SphereRadius) * 0.5;
                    SphereRadius = (float)NewRadius;
                    RadiusSq = (float)(NewRadius * NewRadius);
                    const float Shift = (float)((double)DistF - NewRadius);
                    float Toward[3];
                    ScaleVec3(Toward, P, Shift);
                    float Kept[3];
                    ScaleVec3(Kept, SphereCenter, SphereRadius);
                    float Blend[3];
                    WriteVec3(Blend, Toward[0] + Kept[0], Toward[1] + Kept[1], Toward[2] + Kept[2]);
                    const double Inv = 1.0 / (double)DistF;
                    float NewCenter[3];
                    WriteVec3(NewCenter, (float)((double)Blend[0] * Inv),
                              (float)((double)Blend[1] * Inv),
                              (float)((double)Blend[2] * Inv));
                    SphereCenter[0] = NewCenter[0];
                    SphereCenter[1] = NewCenter[1];
                    SphereCenter[2] = NewCenter[2];
                }
                Index += 1;
            }
        }
        SphereReady = bNonEmpty;
    }
    OutCenter[0] = SphereCenter[0];
    OutCenter[1] = SphereCenter[1];
    OutCenter[2] = SphereCenter[2];
    *OutRadius = SphereRadius;
    return 1;
}

// FUN_065f6ad0: out[i] = weight * matrix[i] (12 floats).
static void ScaleBoneMatrix(float* Out, const float* Matrix, float Weight)
{
    int Index = 0;
    while (Index < 12)
    {
        Out[Index] = Weight * Matrix[Index];
        Index += 1;
    }
}

// FUN_065f6b00: out[i] += weight * matrix[i], one float rounding per element.
static void AccumulateBoneMatrix(float* Out, const float* Matrix, float Weight)
{
    int Index = 0;
    while (Index < 12)
    {
        Out[Index] = (float)((double)Weight * Matrix[Index] +
                                        (double)Out[Index]);
        Index += 1;
    }
}

// FUN_065f6b30: out = M3x4 * point (translation m[9..11]).
static void TransformPoint(float* Out, const float* Matrix, const float* Point)
{
    // Point is re-read per row, as retail does (matters only if Out aliases it).
    Out[0] = (float)((((double)Matrix[2] * Point[2] +
                                  (double)Matrix[1] * Point[1]) +
                                 (double)Matrix[0] * Point[0]) +
                                Matrix[9]);
    Out[1] = (float)((((double)Matrix[5] * Point[2] +
                                  (double)Matrix[3] * Point[0]) +
                                 (double)Matrix[4] * Point[1]) +
                                Matrix[10]);
    Out[2] = (float)((((double)Matrix[8] * Point[2] +
                                  (double)Matrix[6] * Point[0]) +
                                 (double)Matrix[7] * Point[1]) +
                                Matrix[11]);
}

// FUN_065f6b90: out = M3x3 * direction; FUN_065f6b30 without the translation, same summation
// order.
static void TransformDirection(float* Out, const float* Matrix, const float* Direction)
{
    Out[0] = (float)(((double)Matrix[2] * Direction[2] +
                                 (double)Matrix[1] * Direction[1]) +
                                (double)Matrix[0] * Direction[0]);
    Out[1] = (float)(((double)Matrix[5] * Direction[2] +
                                 (double)Matrix[3] * Direction[0]) +
                                (double)Matrix[4] * Direction[1]);
    Out[2] = (float)(((double)Matrix[8] * Direction[2] +
                                 (double)Matrix[6] * Direction[0]) +
                                (double)Matrix[7] * Direction[1]);
}

// FUN_065f70b0 (thiscall): dest = -this[0..2].
static void NegateVec3(const float* Source, float* Dest)
{
    Dest[0] = -Source[0];
    Dest[1] = -Source[1];
    Dest[2] = -Source[2];
}

// FUN_065f70d0 (thiscall): 3x3 transpose. Retail loads every element before the first store,
// so an in-place transpose works; the copy keeps that.
static void Transpose3x3(const float* Source, float* Dest)
{
    float Copy[9];
    std::memcpy(Copy, Source, sizeof(Copy));
    Dest[0] = Copy[0];
    Dest[1] = Copy[3];
    Dest[2] = Copy[6];
    Dest[3] = Copy[1];
    Dest[4] = Copy[4];
    Dest[5] = Copy[7];
    Dest[6] = Copy[2];
    Dest[7] = Copy[5];
    Dest[8] = Copy[8];
}

// FUN_065f90c0. No tests.
void FMeshArchetype::FillStreamDesc(FRpVertexSrcDesc* Out)
{
    std::memset(Out, 0, sizeof(FRpVertexSrcDesc));
    Out->SrcFvf = ParseTag;
    Out->Flags = 6;
    Out->SrcCount = PointIndexCount;
    Out->Position.Base = (const unsigned char*)Vertices;
    Out->Position.ByteSize = 0xC;
    Out->Position.Stride = 0x18;
    Out->Position.IndexArray = PointIndices;
    Out->Position.Count = VertexCount;
    Out->Normal.Base = (unsigned char*)Vertices + 0xC;
    Out->Normal.ByteSize = 0xC;
    Out->Normal.Stride = 0x18;
    Out->Normal.IndexArray = PointIndices;
    Out->Normal.Count = VertexCount;
    if (UvData != nullptr)
    {
        Out->Tex[0].Base = (const unsigned char*)UvData;
        Out->Tex[0].ByteSize = 8;
        Out->Tex[0].Stride = 8;
        Out->Tex[0].IndexArray = UvIndices;
        Out->Tex[0].Count = UvCount;
    }
}

static void WriteIdentity3x3(float* Matrix);
static I3DMathEngine* AcquireMathEngine();

// Reuse a valid VB or recreate it at the descriptor capacity.
static unsigned char EnsurePartVertexBuffer(FDeformVbDesc* Vb, IRPVertexBuffer* VertexBuffer,
                                            unsigned long Fvf, int /*Count*/)
{
    Vb->Listing.Fvf = Fvf;
    Vb->Listing.BaseVertex = 0;
    if (VertexBuffer->IsVbValid(Vb->Listing.VertexBuffer) != 0)
        return 1;
    VertexBuffer->DestroyVb(&Vb->Listing.VertexBuffer);
    return VertexBuffer->CreateVb((unsigned int)(Vb->Listing.Fvf),
                                  (unsigned int)Vb->Capacity,
                                  (unsigned int**)(&Vb->Listing.VertexBuffer),
                                  0) >= 0
               ? 1
               : 0;
}

// LAB_065f6bf0, the DFM mesh's slot 3 (Holder, &part+0x14, bForce).
unsigned char FMeshArchetype::Skin(FLodHolder* Holder, FDeformVbDesc* Vb, unsigned char bForce)
{
    const float* const Root =
        Holder->BoneSlots[0].LocalTransform;
    float RootPos[3];
    MathTransformVec3(RootPos, Root, Root + 9);
    float InverseRoot[12];
    NegateVec3(RootPos, InverseRoot + 9);
    Transpose3x3(Root, InverseRoot);

    SkinnedVertexCount = 0;
    SkinnedBoneCount = 0;
    int BoneIndex = 0;
    while (BoneIndex < Holder->BindCursor)
    {
        FBoneRuntime* const Bone = &Holder->BoneSlots[BoneIndex];
        const FBoneVertexList* const List = &BoneVerts[BoneIndex];
        const unsigned char BoneFlags = Bone->Flags;
        if ((BoneFlags & 2) != 0 || bForce != 0)
        {
            Bone->Flags = (unsigned char)(BoneFlags & 0xFD);
            if (List->Start != nullptr && (List->Finish - List->Start) != 0)
            {
                SkinnedBoneCount += 1;
                if (bSkinned == 0)
                {
                    WriteIdentity3x3(Bone->Matrix);
                    Bone->Matrix[11] = 0.0f;
                    Bone->Matrix[10] = 0.0f;
                    Bone->Matrix[9] = 0.0f;
                }
                else
                {
                    Bone->ComposeWorldMatrix(InverseRoot);
                    unsigned char* const Dirty = FaceFlags;
                    const unsigned short* Cursor = List->Start;
                    while (List->Finish != Cursor)
                    {
                        Dirty[*Cursor] = 1;
                        Cursor += 1;
                    }
                }
            }
        }
        BoneIndex += 1;
    }

    const int Stride = ComputeFvfStride(ParseTag);
    const FVertexRemapList* const Remap = RemapStart;
    unsigned char* Locked = nullptr;
    const int Capacity = GetVertexCapacity();
    IRPVertexBuffer* const VertexBuffer = G_VertexBuffer;
    if (EnsurePartVertexBuffer(Vb, VertexBuffer, ParseTag, Capacity) == 0)
        return 0;
    Vb->Listing.BaseVertex = 0;
    if (VertexBuffer->LockVb(Vb->Listing.VertexBuffer,
                             (unsigned int*)(&Vb->Listing.BaseVertex),
                             (void**)(&Locked),
                             (unsigned int)Capacity) < 0)
        return 0;

    if (bForce != 0)
    {
        FRpVertexSrcDesc StreamDesc;
        FillStreamDesc(&StreamDesc);
        if (G_VertexBuffer->CopyVertices(
                Vb->Listing.VertexBuffer, (unsigned int*)&Vb->Listing.BaseVertex, &StreamDesc, 0,
                (unsigned int)PointIndexCount) < 0)
        {
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformablePart, 0x134,
                  "failed to copy deformable verts during update_verts");
            G_VertexBuffer->UnlockVb(Vb->Listing.VertexBuffer);
            return 0;
        }
    }

    unsigned char* const Dirty = FaceFlags;
    const unsigned char* const Positions = (const unsigned char*)Vertices;
    unsigned int Vertex = 0;
    while (Vertex < (unsigned int)VertexCount)
    {
        if (Dirty[Vertex] != 0)
        {
            Dirty[Vertex] = 0;
            SkinnedVertexCount += 1;
            const FSkinVertex* const SkinVertex = &SkinTable[Vertex];
            const FSkinPair* const Pairs = SkinVertex->Pairs;
            const float* const Source =
                (const float*)(Positions + (size_t)Vertex * 0x18);
            const float* const First = Holder->BoneSlots[Pairs[0].BoneId].Matrix;
            float Out[6];
            const int Count = SkinVertex->BoneCount;
            if (Count == 1)
            {
                TransformPoint(Out, First, Source);
                TransformDirection(Out + 3, First, Source + 3);
            }
            else
            {
                float Blend[12];
                ScaleBoneMatrix(Blend, First, Pairs[0].Weight);
                if (Count == 4)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[3].BoneId].Matrix,
                                         Pairs[3].Weight);
                if (Count == 4 || Count == 3)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[2].BoneId].Matrix,
                                         Pairs[2].Weight);
                if (Count == 4 || Count == 3 || Count == 2)
                    AccumulateBoneMatrix(Blend, Holder->BoneSlots[Pairs[1].BoneId].Matrix,
                                         Pairs[1].Weight);
                TransformPoint(Out, Blend, Source);
                TransformDirection(Out + 3, Blend, Source + 3);
                const float LenSq = (float)(
                    ((double)Out[4] * Out[4] + (double)Out[5] * Out[5]) +
                    (double)Out[3] * Out[3]);
                int Bits = 0;
                std::memcpy(&Bits, &LenSq, sizeof(Bits));
                Bits = (int)(0x5F3759DFu - (unsigned int)(Bits >> 1));
                float Estimate = 0.0f;
                std::memcpy(&Estimate, &Bits, sizeof(Estimate));
                const double Y0 = (double)Estimate;
                const double InvLen =
                    (1.5 - Y0 * Y0 * (double)LenSq * 0.5) * Y0;
                Out[3] = (float)((double)Out[3] * InvLen);
                Out[4] = (float)((double)Out[4] * InvLen);
                Out[5] = (float)((double)Out[5] * InvLen);
            }
            const unsigned int* Cursor = Remap[Vertex].Start;
            const unsigned int* const End = Remap[Vertex].Finish;
            while (Cursor != End)
            {
                std::memcpy(Locked + (size_t)(*Cursor) * (size_t)Stride,
                            Out, 6 * sizeof(float));
                Cursor += 1;
            }
        }
        Vertex += 1;
    }
    G_VertexBuffer->UnlockVb(Vb->Listing.VertexBuffer);
    return 1;
}

// FUN_065f7340 (thiscall on the face group: context, accumulated IB start). No tests of the
// context or the material.
int FFaceGroup::Draw(FMaterialSubmitContext* Context, int StartIndex)
{
    if (IndexCount == 0)
        return 0;
    const int NumVerts =
        (int)IndexMax - (int)IndexMin + 1;
    Context->MaterialContext = UvBinding;
    IMaterial* Material = this->Material;
    Material->Bind(Context);
    Material->Submit(Context, Kind, IndexMin, (unsigned long)NumVerts,
                     (unsigned long)StartIndex, IndexCount, 0x100ul);
    Material->Teardown(Context);
    return NumVerts;
}

// LAB_065f73a0, the DFM mesh's slot 5 (Camera, Part).
unsigned char FMeshArchetype::Draw(ICamera* Camera, FDeformPart* Part)
{
    FBoneRuntime* const BoneSlots = Part->Lod->BoneSlots;
    const float* const Root =
        BoneSlots[0].LocalTransform;
    const float* const View = Camera->GetViewTransform();
    int bHaveView = 1;
    G_Pipeline->GetPipelineState(7, &bHaveView);
    if (bHaveView != 0)
    {
        float Composed[12];
        AcquireMathEngine()->MulTransform(*(FTransform*)Composed,
                                          *(const FTransform*)View,
                                          *(const FTransform*)Root); // +0x1C
        float Modelview[12];
        std::memcpy(Modelview, Composed, sizeof(Modelview));
        G_Pipeline->SetModelview(Modelview);
    }
    else
    {
        float Identity[12];
        Identity[0] = 1.0f;
        Identity[1] = 0.0f;
        Identity[2] = 0.0f;
        Identity[3] = 0.0f;
        Identity[4] = 1.0f;
        Identity[5] = 0.0f;
        Identity[6] = 0.0f;
        Identity[7] = 0.0f;
        Identity[8] = 1.0f;
        Identity[9] = 0.0f;
        Identity[10] = 0.0f;
        Identity[11] = 0.0f;
        G_Pipeline->SetWorld(Identity);
    }

    FMaterialSubmitContext Context;
    Context.Inst = BoneSlots[0].Instance;
    // The mutable context ABI borrows these const transform views.
    Context.World = const_cast<float*>(Root);
    Context.View = const_cast<float*>(View);
    Context.SourceVertices = nullptr;
    Context.Indices = nullptr;
    Context.VbListing = &Part->Vb.Listing;
    Context.IbScratch = &Part->Ib.Scratch;
    // Retail leaves +0x1C as stack garbage; the port cannot reproduce that and stores 0.
    Context.ReplacedMaterial = nullptr;
    Context.MaterialContext = nullptr;

    FUvDeformBinding Binding;
    Binding.Record = nullptr;
    Binding.Bone = nullptr;
    Binding.EyeUvTransformCount = 0;
    unsigned long Start = 0;
    int GroupIndex = 0;
    while (GroupIndex < FaceGroupCount)
    {
        FFaceGroup* const Group = &FaceGroups[GroupIndex];
        if (UvDeformCount != 0 && (long)Group->UvBoneIndex >= 0)
        {
            FUvDeformRecord* const Record = &UvDeform[Group->UvBoneIndex];
            Binding.Record = Record;
            Binding.Bone = &Part->Lod->BoneSlots[Record->BoneId];
            Group->UvBinding = &Binding;
        }
        Group->Draw(&Context, (int)Start);
        Start += Group->IndexCount;
        GroupIndex += 1;
    }
    EyeUvTransformCount = Binding.EyeUvTransformCount;
    return 1;
}

// LAB_065f7240, the DFM mesh's slot 2 (&part+0x24).
unsigned char FMeshArchetype::FillIndexBuffer(FDeformIbDesc* Ib)
{
    const int IndexCount = GetIndexCount();
    IRPIndexBuffer* const IndexBuffer = G_IndexBuffer;
    unsigned short* Locked = nullptr;
    if (IndexBuffer->IsIbValid(Ib->Scratch.IndexBuffer) == 0)
    {
        IndexBuffer->DestroyIb(&Ib->Scratch.IndexBuffer);
        if (IndexBuffer->CreateIb((unsigned int)Ib->Capacity,
                                  (unsigned int**)(&Ib->Scratch.IndexBuffer),
                                  0) < 0)
            return 0;
    }
    Ib->Scratch.Bias = 0;
    if (IndexBuffer->LockIb(Ib->Scratch.IndexBuffer,
                            (unsigned int*)(&Ib->Scratch.Bias),
                            (void**)(&Locked),
                            (unsigned int)IndexCount) < 0)
        return 0;
    int GroupIndex = 0;
    while (GroupIndex < FaceGroupCount)
    {
        const FFaceGroup* const Group = &FaceGroups[GroupIndex];
        unsigned long Index = 0;
        while (Index < Group->IndexCount)
        {
            *Locked = (unsigned short)(Group->Indices[Index] +
                                                  (unsigned short)Group->IndexMin);
            Locked += 1;
            Index += 1;
        }
        GroupIndex += 1;
    }
    G_IndexBuffer->UnlockIb(Ib->Scratch.IndexBuffer);
    return 1;
}

static int ReleaseLodHolder(FLodHolder* Holder);

// FUN_065f6760, the part dtor.
void FDeformPart::Reset()
{
    G_VertexBuffer->DestroyVb(&Vb.Listing.VertexBuffer);
    Vb.Capacity = 0;
    G_IndexBuffer->DestroyIb(&Ib.Scratch.IndexBuffer);
    Ib.Capacity = 0;

    // FUN_065f17e0 on +0 with no null test.
    ReleaseLodHolder(Lod);
    // The counted mesh array (header at -4), last to first.
    if (Meshes != nullptr)
    {
        int* Header = (int*)Meshes - 1;
        int Index = *Header - 1;
        while (Index >= 0)
        {
            if (Meshes[Index] != nullptr)
            {
                Meshes[Index]->Release();
                Meshes[Index] = nullptr;
            }
            Index -= 1;
        }
        delete[] (unsigned char*)Header;
    }
    // +0 is released a second time when still set. The holder carries two references for the
    // part -- FUN_065f36c0 creates it at 1 and FUN_065f67f0 AddRefs it -- so this is not a
    // double free.
    if (Lod != nullptr)
    {
        ReleaseLodHolder(Lod);
        Lod = nullptr;
    }
}

// FUN_065f7c70. No tests.
void FFaceGroup::Construct()
{
    Kind = 4;
    MaterialCrc = 0;
    Material = nullptr;
    UvBinding = nullptr;
    IndexMin = 0;
    IndexMax = 0;
    Indices = nullptr;
    IndexCount = 0;
    UvBoneIndex = 0xFFFFFFFF;
}

// FUN_065f7d70: tristrip -> triangle list. Count - 2 is tested unsigned, so only 0 skips and a
// Count below 2 wraps, as in retail; no pointer tests.
static void ExpandTristrip(const unsigned short* Strip, int StripCount, unsigned short* Out,
                           int* OutCount)
{
    *OutCount = 0;
    const unsigned int Limit = (unsigned int)(StripCount - 2);
    if (Limit == 0)
        return;
    unsigned int Walk = 0;
    const unsigned short* Cursor = Strip + 2;
    do
    {
        const unsigned short A = Cursor[-2];
        const unsigned short B = Cursor[-1];
        const unsigned short C = Cursor[0];
        if (A != B && B != C && C != A)
        {
            Out[*OutCount] = A;
            if ((Walk & 1) == 0)
            {
                Out[*OutCount + 1] = B;
                Out[*OutCount + 2] = C;
            }
            else
            {
                Out[*OutCount + 1] = C;
                Out[*OutCount + 2] = B;
            }
            *OutCount += 3;
        }
        Walk += 1;
        Cursor += 1;
    } while (Walk < (unsigned int)(StripCount - 2));
}

// FUN_065f7e00: index min/max, then rebase to the min.
static void RebaseFaceIndices(FFaceGroup* Group)
{
    unsigned short* Indices = Group->Indices;
    const unsigned long Count = Group->IndexCount;
    const unsigned short Last = Indices[Count - 1];
    const unsigned short First = Indices[0];
    Group->IndexMin = (First < Last) ? First : Last;
    Group->IndexMax = (First >= Last) ? First : Last;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        const unsigned long Value = Indices[Index];
        if (Value < Group->IndexMin)
            Group->IndexMin = Value;
        else if (Value > Group->IndexMax)
            Group->IndexMax = Value;
    }
    const unsigned short Base = (unsigned short)Group->IndexMin;
    for (unsigned long Index = 0; Index < Count; ++Index)
        Indices[Index] = (unsigned short)(Indices[Index] - Base);
}

// FUN_065f8b00, on the calloc(1, 0x84) block of FUN_065f26f0.
FMeshArchetype::FMeshArchetype(const char* Name)
    : FDeformMesh(Name)
{
    std::memset(&ParseTag, 0, sizeof(FMeshArchetype) - offsetof(FMeshArchetype, ParseTag));
    RemapAllocator = (unsigned char)((uintptr_t)Name);
}

// The 0x14-byte mapped node, two of which FUN_065f7760 keeps on its stack (Material_name,
// Tristrip_indices): an open handle, its size, a file mapping and a view of it, and the
// FileSystem they came from, AddRef'd (FUN_065f9150).
struct FMappedFileNode
{
    int Handle;         // +0x00 IFileSystem +0x7C, -1 none
    unsigned long Size; // +0x04 IFileSystem +0x28
    int Mapping;        // +0x08 IFileSystem +0x3C (h, 0, PAGE_READONLY, 0, 0, 0), -1 none
    void* View;         // +0x0C IFileSystem +0x40 (mapping, FILE_MAP_READ, 0, 0, 0)
    IFileSystem* Fs;    // +0x10

    // FUN_065f7d10.
    void Close();

    // FUN_065f8360.
    void Init(IFileSystem* Fs);

    // FUN_065f8390.
    int Map(const char* NodeName, unsigned long FindHandle);
};

// FUN_065f9150.
static void HoldFileSystem(IFileSystem** Slot, IFileSystem* Fs)
{
    *Slot = Fs;
    if (Fs != nullptr)
        Fs->AddRef();
}

// FUN_065f9170.
static void DropFileSystem(IFileSystem** Slot)
{
    if (*Slot != nullptr)
    {
        (*Slot)->Release();
        *Slot = nullptr;
    }
}

// FUN_065f7d10 / FUN_065f7ca0, the node's dtor (7ca0 repeats the final release, a no-op after
// the first).
void FMappedFileNode::Close()
{
    if (Handle != -1)
    {
        if (Mapping != -1)
        {
            if (View != nullptr)
            {
                Fs->UnmapFileView((unsigned long)((uintptr_t)View));
                View = nullptr;
            }
            Fs->CloseFile((unsigned long)Mapping);
        }
        Fs->CloseFile((unsigned long)Handle);
        Handle = -1;
    }
    DropFileSystem(&Fs);
}

// The open-and-map sequence FUN_065f7760 inlines twice.
static void OpenMappedFileNode(FMappedFileNode* Node, const char* NodeName)
{
    FFileSystemDesc Desc;
    FillFileDesc(&Desc, NodeName);
    Node->Handle = Node->Fs->OpenByDesc(&Desc);
    if (Node->Handle == -1)
        return;
    Node->Size = Node->Fs->GetSize((unsigned long)Node->Handle, 0);
    Node->Mapping = Node->Fs->CreateMapping((unsigned long)Node->Handle, 0, 2, 0, 0, 0);
    if (Node->Mapping == -1)
        return;
    Node->View = (void*)((uintptr_t)((unsigned int)(
        Node->Fs->MapFileView((unsigned long)Node->Mapping, 4, 0, 0, 0))));
}

// FUN_065f8360. Mapping is not touched (every reader tests Handle first).
void FMappedFileNode::Init(IFileSystem* Fs)
{
    HoldFileSystem(&this->Fs, Fs);
    Handle = -1;
    Size = 0;
    View = nullptr;
}

// FUN_065f8390 (name, find handle).
int FMappedFileNode::Map(const char* NodeName, unsigned long FindHandle)
{
    FFileSystemDesc Desc;
    FillFileDesc(&Desc, NodeName);
    Desc.FindHandle = FindHandle;
    Handle = Fs->OpenByDesc(&Desc);
    if (Handle == -1)
        return -8;
    Size = Fs->GetSize((unsigned long)Handle, 0);
    Mapping = Fs->CreateMapping((unsigned long)Handle, 0, 2, 0, 0, 0);
    if (Mapping == -1)
        return -6;
    View = (void*)((uintptr_t)((unsigned int)(
        Fs->MapFileView((unsigned long)Mapping, 4, 0, 0, 0))));
    if (View != nullptr)
        return 0;
    return -1;
}

// FUN_065f7760 (thiscall on the mesh: Fs).
static bool LoadFaceGroups(FMeshArchetype* Arch, IFileSystem* Fs)
{
    if (Fs->OpenNode("Face_groups") == 0)
        return false;

    int Count = 0;
    FFileSystemDesc CountDesc;
    FillFileDesc(&CountDesc, "Count");
    const int CountHandle = Fs->OpenByDesc(&CountDesc);
    if (CountHandle != -1)
    {
        int Value = 0;
        unsigned long Got = 0;
        if (Fs->Read((unsigned long)CountHandle, &Value, 4, &Got, 0) != 0 && Got == 4)
            Count = Value;
        Fs->CloseFile((unsigned long)CountHandle);
    }
    int* Block = (int*)new unsigned char[(size_t)Count * sizeof(FFaceGroup) + 4];
    *Block = Count;
    FFaceGroup* Groups = (FFaceGroup*)(Block + 1);
    int GroupIndex = 0;
    while (GroupIndex < Count)
    {
        Groups[GroupIndex].Construct();
        GroupIndex += 1;
    }
    Arch->FaceGroups = Groups;

    WIN32_FIND_DATAA FindData;
    const int Finder = FsFindFirst(Fs, "Group*", &FindData);
    if (Finder != -1)
    {
        for (;;)
        {
            FFaceGroup* Group = &Arch->FaceGroups[Arch->FaceGroupCount];
            IFileSystem* Child = nullptr;
            DropFileSystem(&Child);
            FFileSystemDesc ChildDesc;
            FillFileDesc(&ChildDesc, FindData.cFileName);
            ChildDesc.FindHandle = (unsigned long)Finder;
            if (Fs->CreateInstance((AGGDESC*)(&ChildDesc),
                                   (void**)(&Child)) < 0)
            {
                char Message[0x2000];
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformablePartArchetype,
                      0x37, FormatDeformMessage(Message, "couldnt open dfm face group"));
                DropFileSystem(&Child);
            }
            else
            {
                FMappedFileNode MaterialNode;
                HoldFileSystem(&MaterialNode.Fs, Child);
                MaterialNode.Handle = -1;
                MaterialNode.Size = 0;
                MaterialNode.View = nullptr;
                OpenMappedFileNode(&MaterialNode, "Material_name");
                const char* MaterialName = (const char*)MaterialNode.View;
                if (MaterialName == nullptr)
                {
                    MaterialNode.Close();
                    DropFileSystem(&MaterialNode.Fs);
                    DropFileSystem(&Child);
                }
                else
                {
                    Group->MaterialCrc = DACOM_CRC::GetCRC32(MaterialName);
                    FEngineName Query;
                    Query.Text = MaterialName;
                    Query.Crc = DACOM_CRC::GetCRC32(MaterialName);
                    if (G_MaterialLibrary
                            ->FindMaterialByCrc(&Query, &Group->Material) < 0)
                    {
                        char Message[0x2000];
                        // Retail: the arguments are the literal "???" (066027E0) and an empty
                        // static (066031AC), not the material and part names.
                        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                              G_FdumpFileDeformablePartArchetype, 0x45,
                              FormatDeformMessage(Message, "couldnt find material %s for %s", "???",
                                                  ""));
                        MaterialNode.Close();
                        DropFileSystem(&MaterialNode.Fs);
                        DropFileSystem(&Child);
                    }
                    else
                    {
                        FMappedFileNode IndexNode;
                        HoldFileSystem(&IndexNode.Fs, Child);
                        IndexNode.Handle = -1;
                        IndexNode.Size = 0;
                        IndexNode.View = nullptr;
                        OpenMappedFileNode(&IndexNode, "Tristrip_indices");
                        if (IndexNode.Size != 0)
                        {
                            const int StripCount = (int)(IndexNode.Size >> 1);
                            Group->Indices = (unsigned short*)new unsigned char[(size_t)(StripCount * 6 - 12)];
                            ExpandTristrip((const unsigned short*)IndexNode.View,
                                           StripCount, Group->Indices,
                                           (int*)(&Group->IndexCount));
                        }
                        else
                        {
                            FFileSystemDesc FaceDesc;
                            FillFileDesc(&FaceDesc, "Face_indices");
                            const int FaceHandle = Child->OpenByDesc(&FaceDesc);
                            if (FaceHandle != -1)
                            {
                                const unsigned long FaceBytes =
                                    Child->GetSize((unsigned long)FaceHandle, 0);
                                const unsigned long FaceCount = FaceBytes >> 1;
                                unsigned short* Buffer = (unsigned short*)new unsigned char[(size_t)FaceCount * 2];
                                unsigned long Got = 0;
                                if (Child->Read((unsigned long)FaceHandle, Buffer,
                                                FaceBytes, &Got, 0) != 0)
                                {
                                    Group->IndexCount = FaceCount;
                                    Group->Indices = Buffer;
                                }
                                else
                                {
                                    delete[] (unsigned char*)Buffer;
                                }
                                Child->CloseFile((unsigned long)FaceHandle);
                            }
                        }
                        Group->Kind = 4;
                        RebaseFaceIndices(Group);
                        IndexNode.Close();
                        MaterialNode.Close();
                        DropFileSystem(&Child);
                    }
                }
            }

            Arch->FaceGroupCount += 1;
            if (Arch->FaceGroupCount >= Count)
                break;
            if (FsFindNext(Fs, Finder, &FindData) == 0)
                break;
        }
        FsFindClose(Fs, Finder);
    }
    Fs->OpenNode(G_ParentNode);
    return true;
}

// FUN_065f7e80 (thiscall on the mesh: Fs).
bool FMeshArchetype::LoadGeometry(IFileSystem* Fs)
{
    if (Fs->OpenNode("Geometry") == 0)
        return false;

    FFileSystemDesc Desc;
    FillFileDesc(&Desc, "Point_indices");
    const int IndexHandle = Fs->OpenByDesc(&Desc);
    if (IndexHandle != -1)
    {
        const unsigned long IndexBytes = Fs->GetSize((unsigned long)IndexHandle, 0);
        const unsigned long IndexCount = IndexBytes >> 2;
        unsigned char* Indices = new unsigned char[(size_t)IndexCount * 4];
        unsigned long Got = 0;
        if (Fs->Read((unsigned long)IndexHandle, Indices, IndexBytes, &Got, 0) != 0)
        {
            PointIndexCount = (int)IndexCount;
            PointIndices = (unsigned long*)Indices;
        }
        else
        {
            delete[] Indices;
        }
        Fs->CloseFile((unsigned long)IndexHandle);
    }

    FMappedFileNode PointNode;
    HoldFileSystem(&PointNode.Fs, Fs);
    PointNode.Handle = -1;
    PointNode.Size = 0;
    PointNode.View = nullptr;
    OpenMappedFileNode(&PointNode, "Points");
    const unsigned long* PointView = (const unsigned long*)PointNode.View;

    FMappedFileNode NormalNode;
    HoldFileSystem(&NormalNode.Fs, Fs);
    NormalNode.Handle = -1;
    NormalNode.Size = 0;
    NormalNode.View = nullptr;
    OpenMappedFileNode(&NormalNode, "Vertex_normals");
    const unsigned long* NormalView = (const unsigned long*)NormalNode.View;

    const unsigned int VertexCount = (unsigned int)(PointNode.Size / 12);
    this->VertexCount = (int)VertexCount;
    unsigned long* Vertices =
        (unsigned long*)new unsigned char[(size_t)VertexCount * 0x18];
    this->Vertices = Vertices;
    for (unsigned int Index = 0; Index < (unsigned int)this->VertexCount; ++Index)
    {
        unsigned long* Out = Vertices + Index * 6;
        Out[0] = PointView[Index * 3 + 0];
        Out[1] = PointView[Index * 3 + 1];
        Out[2] = PointView[Index * 3 + 2];
        Out[3] = NormalView[Index * 3 + 0];
        Out[4] = NormalView[Index * 3 + 1];
        Out[5] = NormalView[Index * 3 + 2];
    }
    NormalNode.Close();
    DropFileSystem(&NormalNode.Fs);
    PointNode.Close();
    DropFileSystem(&PointNode.Fs);

    FillFileDesc(&Desc, "UV0_indices");
    const int UvIndexHandle = Fs->OpenByDesc(&Desc);
    if (UvIndexHandle != -1)
    {
        const unsigned long UvIndexBytes =
            Fs->GetSize((unsigned long)UvIndexHandle, 0);
        unsigned char* UvIndices = new unsigned char[(size_t)((UvIndexBytes >> 2) << 2)];
        unsigned long Got = 0;
        if (Fs->Read((unsigned long)UvIndexHandle, UvIndices, UvIndexBytes, &Got,
                     0) != 0)
            this->UvIndices = (unsigned long*)UvIndices;
        else
            delete[] UvIndices;
        Fs->CloseFile((unsigned long)UvIndexHandle);
    }

    FillFileDesc(&Desc, "UV0");
    const int UvHandle = Fs->OpenByDesc(&Desc);
    if (UvHandle != -1)
    {
        const unsigned long UvBytes = Fs->GetSize((unsigned long)UvHandle, 0);
        const unsigned long UvCount = UvBytes >> 3;
        unsigned char* UvData = new unsigned char[(size_t)UvCount * 8];
        unsigned long Got = 0;
        if (Fs->Read((unsigned long)UvHandle, UvData, UvBytes, &Got, 0) != 0)
        {
            this->UvCount = (int)UvCount;
            this->UvData = UvData;
        }
        else
        {
            delete[] UvData;
        }
        Fs->CloseFile((unsigned long)UvHandle);
    }

    FMappedFileNode CountNode;
    CountNode.Init(Fs);
    CountNode.Map("Point_bone_count", 0xFFFFFFFF);
    const unsigned long* BoneCounts = (const unsigned long*)CountNode.View;

    FMappedFileNode IdNode;
    IdNode.Init(Fs);
    IdNode.Map("Bone_id_chain", 0xFFFFFFFF);
    const unsigned long* BoneIds = (const unsigned long*)IdNode.View;

    FMappedFileNode WeightNode;
    WeightNode.Init(Fs);
    WeightNode.Map("Bone_weight_chain", 0xFFFFFFFF);
    const unsigned long* Weights = (const unsigned long*)WeightNode.View;

    const unsigned int PairCount = (unsigned int)(IdNode.Size >> 2);
    SkinPairs = (FSkinPair*)new unsigned char[(size_t)PairCount * 8];
    for (unsigned int Index = 0; Index < PairCount; ++Index)
    {
        unsigned long* Pair = (unsigned long*)(&SkinPairs[Index]);
        Pair[0] = BoneIds[Index];
        Pair[1] = Weights[Index];
    }

    FSkinPair* Cursor = SkinPairs;
    SkinTable = (FSkinVertex*)new unsigned char[(size_t)(unsigned int)this->VertexCount * 8];
    for (unsigned int Index = 0; Index < (unsigned int)this->VertexCount; ++Index)
    {
        const unsigned long Influence = BoneCounts[Index];
        SkinTable[Index].BoneCount = (int)Influence;
        SkinTable[Index].Pairs = Cursor;
        Cursor += Influence;
    }

    WeightNode.Close();
    IdNode.Close();
    CountNode.Close();
    Fs->OpenNode(G_ParentNode);
    return true;
}

// FUN_065f8480 (thiscall on the mesh: Fs).
bool FMeshArchetype::LoadUvDeformTables(IFileSystem* Fs)
{
    if (Fs->OpenNode("Geometry") == 0)
        return false;

    FMappedFileNode BoneIdNode;
    HoldFileSystem(&BoneIdNode.Fs, Fs);
    BoneIdNode.Size = 0;
    BoneIdNode.View = nullptr;
    OpenMappedFileNode(&BoneIdNode, "UV_bone_id");

    FMappedFileNode VertexCountNode;
    HoldFileSystem(&VertexCountNode.Fs, Fs);
    VertexCountNode.Handle = -1;
    VertexCountNode.Size = 0;
    VertexCountNode.View = nullptr;
    OpenMappedFileNode(&VertexCountNode, "UV_vertex_count");

    FMappedFileNode PlaneNode;
    HoldFileSystem(&PlaneNode.Fs, Fs);
    PlaneNode.Handle = -1;
    PlaneNode.Size = 0;
    PlaneNode.View = nullptr;
    OpenMappedFileNode(&PlaneNode, "UV_plane_distance");

    FMappedFileNode XToUNode;
    HoldFileSystem(&XToUNode.Fs, Fs);
    XToUNode.Handle = -1;
    XToUNode.Size = 0;
    XToUNode.View = nullptr;
    OpenMappedFileNode(&XToUNode, "Bone_X_to_U_scale");

    FMappedFileNode YToVNode;
    HoldFileSystem(&YToVNode.Fs, Fs);
    YToVNode.Handle = -1;
    YToVNode.Size = 0;
    YToVNode.View = nullptr;
    YToVNode.Map("Bone_Y_to_V_scale", 0xFFFFFFFF);

    FMappedFileNode MinDuNode;
    HoldFileSystem(&MinDuNode.Fs, Fs);
    MinDuNode.Handle = -1;
    MinDuNode.Size = 0;
    MinDuNode.View = nullptr;
    MinDuNode.Map("Min_du", 0xFFFFFFFF);

    FMappedFileNode MaxDuNode;
    HoldFileSystem(&MaxDuNode.Fs, Fs);
    MaxDuNode.Handle = -1;
    MaxDuNode.Size = 0;
    MaxDuNode.View = nullptr;
    MaxDuNode.Map("Max_du", 0xFFFFFFFF);

    FMappedFileNode MinDvNode;
    HoldFileSystem(&MinDvNode.Fs, Fs);
    MinDvNode.Handle = -1;
    MinDvNode.Size = 0;
    MinDvNode.View = nullptr;
    MinDvNode.Map("Min_dv", 0xFFFFFFFF);

    FMappedFileNode MaxDvNode;
    MaxDvNode.Init(Fs);
    MaxDvNode.Map("Max_dv", 0xFFFFFFFF);

    const unsigned long* const Columns[9] = {
        (const unsigned long*)BoneIdNode.View,
        (const unsigned long*)VertexCountNode.View,
        (const unsigned long*)PlaneNode.View,
        (const unsigned long*)XToUNode.View,
        (const unsigned long*)YToVNode.View,
        (const unsigned long*)MinDuNode.View,
        (const unsigned long*)MaxDuNode.View,
        (const unsigned long*)MinDvNode.View,
        (const unsigned long*)MaxDvNode.View,
    };
    UvDeformCount = (int)(BoneIdNode.Size >> 2);
    if (UvDeformCount != 0)
    {
        UvDeform = (FUvDeformRecord*)new unsigned char[(size_t)(unsigned int)UvDeformCount * 0x24];
        for (unsigned int Index = 0; Index < (unsigned int)UvDeformCount; ++Index)
        {
            unsigned long* Row = (unsigned long*)(&UvDeform[Index]);
            for (int Column = 0; Column < 9; ++Column)
                Row[Column] = Columns[Column][Index];
        }
    }

    MaxDvNode.Close();
    MinDvNode.Close();
    MaxDuNode.Close();
    MinDuNode.Close();
    YToVNode.Close();
    XToUNode.Close();
    PlaneNode.Close();
    VertexCountNode.Close();
    BoneIdNode.Close();
    Fs->OpenNode(G_ParentNode);
    return true;
}

// FUN_065f89e0 (thiscall on the mesh: Holder, Fs).
bool FMeshArchetype::LoadTables(FLodHolder* Holder, IFileSystem* Fs)
{
    if (!LoadFaceGroups(this, Fs))
        return false;
    if (!LoadGeometry(Fs))
        return false;
    if (!LoadUvDeformTables(Fs))
        return false;
    BindUvBoneIdsToFaceGroups(this);
    BuildVertexRemap(this);
    BuildBoneVertexLists(this, Holder, Fs);

    FaceFlagCount = VertexCount;
    FaceFlags = new unsigned char[(size_t)FaceFlagCount];
    std::memset(FaceFlags, 0, (size_t)FaceFlagCount);
    ParseTag = G_ParseTagUtf;
    return true;
}

// FUN_065fdc60 (via FUN_065fdca0, vtable slot 6).
FVmeshWrapper::~FVmeshWrapper()
{
    if (EngineHandle != G_InvalidEngineHandle)
        G_Engine->ReleaseHandle(EngineHandle);
    if (VMeshPart != nullptr)
    {
        VMeshPart->Release();
        VMeshPart = nullptr;
    }
}

// FUN_065fdcc0; vtable PTR_LAB_066013dc.
FVmeshWrapper::FVmeshWrapper(const char* Name, void* Handle, IVMeshPart* Part)
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

// FUN_065f28b0 (cdecl): _vsnprintf(Buffer, 0x2000, Format, ...) and return Buffer. Every
// caller hands it a 0x2000-byte stack buffer.
char* FormatDeformMessage(char* Buffer, const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, 0x2000, Format, Args);
    va_end(Args);
    return Buffer;
}

// FUN_065f26f0.
static FDeformMesh* LoadDeformMesh(FLodHolder* Holder, IFileSystem* MeshFs, const char* Name,
                                   unsigned char bSkinned)
{
    const unsigned long Key = DACOM_CRC::GetCRC32(Name);
    std::map<unsigned long, FDeformMesh*>::iterator Found = G_MeshCache.find(Key);
    if (Found != G_MeshCache.end())
    {
        FDeformMesh* Cached = Found->second;
        Cached->AddRef();
        return Cached;
    }

    FDeformMesh* Result = nullptr;
    FMeshArchetype* Mesh = new FMeshArchetype(Name);
    if (Mesh->LoadTables(Holder, MeshFs))
    {
        Mesh->bSkinned = bSkinned;
        Result = Mesh;
    }
    else
    {
        Mesh->Release();
        FEngineName ArchName;
        ArchName.Text = Name;
        ArchName.Crc = DACOM_CRC::GetCRC32(Name);
        void* Handle = G_Engine->CreateArchetypeHandle(&ArchName, MeshFs);
        IVMeshPart* Library = nullptr;
        // This query ABI accepts the const IID as an opaque mutable token.
        if (G_Engine->QueryEngineComponent((int)((intptr_t)Handle),
                                           const_cast<char*>(Iids::VMeshLibrary),
                                           (void**)(&Library)) < 0)
        {
            G_Engine->ReleaseHandle(Handle);
        }
        else
        {
            Result = new FVmeshWrapper(Name, Handle, Library);
        }
        if (Library != nullptr)
            Library->Release();
    }

    if (Result == nullptr)
    {
        char Message[0x2000];
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableMesh, 0x39,
              FormatDeformMessage(Message, "Deformable: Could not load mesh %s", Name));
        return nullptr;
    }
    G_MeshCache[Key] = Result;
    return Result;
}

// FUN_065f13e0 (thiscall).
static void WriteIdentity3x4(float* Matrix)
{
    std::memset(Matrix, 0, 12 * sizeof(float));
    Matrix[0] = 1.0f;
    Matrix[4] = 1.0f;
    Matrix[8] = 1.0f;
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
    Skel->EngineHandle = G_InvalidEngineHandle;
    Skel->RefCount = 1;
    return Skel;
}

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

// FUN_065f1410 (thiscall).
static void AddRefSkeleton(FSkeleton* Skel)
{
    if (Skel == nullptr)
        return;
    Skel->RefCount += 1;
}

// FUN_065f1420 (thiscall).
static int ReleaseSkeleton(FSkeleton* Skel)
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

// FUN_065f17d0 (thiscall).
static void AddRefLodHolder(FLodHolder* Holder)
{
    if (Holder == nullptr)
        return;
    Holder->RefCount += 1;
}

// FUN_065f17e0 (thiscall).
static int ReleaseLodHolder(FLodHolder* Holder)
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

// --- FBoneRuntime : IInstanceTransform, vtable PTR_LAB_066010a0 (14 slots) ---
// Every slot is stdcall with the engine instance as its first argument (EngBase pushes it);
// only slot 0 uses it. None of them tests `this` or a pointer argument.

// FUN_065f16a0: slots 1 and 2, and the ICF-shared empty body of several IEngineComponent
// slots. Retail leaves EAX unset; the port answers 0.
static unsigned long DACOM_API BoneRuntimeRet8(void* /*This*/, void* /*A*/)
{
    return 0;
}

// LAB_065f1790: slot 11 SetUser, shared with IEngineComponent slots 5/10.
static void DACOM_API BoneRuntimeRetC(void* /*This*/, void* /*A*/, void* /*B*/)
{
}

static void WriteIdentity3x3(float* Matrix);

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

// LAB_065f1690 (RET 8), slot 0: [this+4] = the instance.
GENRESULT DACOM_API FBoneRuntime::Init(void* InInstance)
{
    Instance = InInstance;
    return GR_OK; // retail returns this; nobody reads it
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

// LAB_065f1730, slot 7: as slot 5, with rotation + position.
GENRESULT DACOM_API FBoneRuntime::SetMatrix(void* /*InInstance*/, const float* InMatrix)
{
    const unsigned char Flags = this->Flags;
    std::memcpy(LocalTransform, InMatrix, 12 * sizeof(float));
    if ((Flags & 1u) != 0)
        this->Flags = (unsigned char)(Flags | 2u);
    return GR_OK;
}

// LAB_065f1720, slot 8 (the same body as slot 6).
float* DACOM_API FBoneRuntime::GetMatrix(void* /*InInstance*/)
{
    return LocalTransform;
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

static I3DMathEngine* AcquireMathEngine();

// The FUN_065f1550 construct loop body for one slot. The memset stands in for the operator
// new garbage in the fields retail leaves unwritten (+0x0C..+0x3B). No test of the slot.
static void InitBoneRuntimeSlot(FBoneRuntime* Slot)
{
    std::memset((void*)Slot, 0, sizeof(FBoneRuntime));
    new (Slot) FBoneRuntime();
}

// FUN_065f1660 (thiscall). Retail does not test for null.
static void WriteIdentity3x3(float* Matrix)
{
    std::memset(Matrix, 0, 9 * sizeof(float));
    Matrix[0] = 1.0f;
    Matrix[4] = 1.0f;
    Matrix[8] = 1.0f;
}

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

static float SvdAbs(float Value)
{
    return (Value < 0.0f) ? -Value : Value;
}

// Shared scratch used by the SVD scalar operations.
static float G_SvdSqrArg = 0.0f;
static float G_SvdMaxArg1 = 0.0f;
static float G_SvdMaxArg2 = 0.0f;
static int G_SvdMinArg1 = 0;
static int G_SvdMinArg2 = 0;

// FUN_065f9e00 (two floats; ECX unused).
static double SvdPythag(float Left, float Right)
{
    const double AbsLeft = std::fabs((double)Left);
    const float AbsRight = std::fabs(Right);
    if (AbsLeft > (double)AbsRight)
    {
        const double Ratio = (double)AbsRight / AbsLeft;
        G_SvdSqrArg = (float)Ratio;
        if (Ratio == 0.0)
            return AbsLeft * std::sqrt(0.0 + 1.0);
        const double RatioF = (double)G_SvdSqrArg;
        return AbsLeft * std::sqrt(RatioF * RatioF + 1.0);
    }
    if ((double)AbsRight == 0.0)
        return 0.0;
    const double Ratio = AbsLeft / (double)AbsRight;
    G_SvdSqrArg = (float)Ratio;
    if (Ratio == 0.0)
        return std::sqrt(0.0 + 1.0) * (double)AbsRight;
    const double RatioF = (double)G_SvdSqrArg;
    return std::sqrt(RatioF * RatioF + 1.0) * (double)AbsRight;
}

// FUN_065f9ed0. Retail rounds the sum to float once before the compare, as SSE float addition
// does.
static bool SvdIsNegligible(float Value, float Anorm)
{
    return (Value + Anorm) == Anorm;
}

static float& SvdCell(float* Base, int Row, int Col)
{
    return Base[Row * G_SvdMaxDim + Col];
}

// Retail's sign test before each FSQRT / FABS sets C0 when below zero OR unordered (a NaN
// counts as negative).
static bool SvdSignC0(float Value)
{
    return !((double)Value >= 0.0);
}

// |Value| negated unless the sign test set C0: the FABS / (C0 ? FCHS) / FCHS tail both
// Householder halves use.
static double SvdNegSignOf(double Value, bool bSignNegative)
{
    double Magnitude = std::fabs(Value);
    if (bSignNegative)
        Magnitude = -Magnitude;
    return -Magnitude;
}

// FUN_065f9f00 (thiscall on the source matrix: A, W, V): Golub-Reinsch SVD.
unsigned char FSvdMatrix::SvdCmp(float* A, float* W, float* V)
{
    float Rv1[12];
    float G = 0.0f;
    float Anorm = 0.0f;
    double Scale = 0.0;
    int L = 0;
    int I = 0;
    while (I < ColCount)
    {
        L = I + 1;
        Rv1[I] = (float)(Scale * (double)G);
        G = 0.0f;
        Scale = 0.0;
        double Sum = 0.0;
        if (I < RowCount)
        {
            for (int K = I; K < RowCount; ++K)
                Scale += std::fabs((double)(SvdCell(A, K, I)));
            if (Scale != 0.0)
            {
                const double Inv = 1.0 / Scale;
                for (int K = I; K < RowCount; ++K)
                {
                    const double Scaled = Inv * (double)(SvdCell(A, K, I));
                    SvdCell(A, K, I) = (float)Scaled;
                    Sum += Scaled * Scaled;
                }
                const float F = SvdCell(A, I, I);
                G = (float)(SvdNegSignOf(std::sqrt(Sum), SvdSignC0(F)));
                const double H = (double)F * (double)G - Sum;
                SvdCell(A, I, I) = (float)((double)F - (double)G);
                if (L < ColCount)
                {
                    const double HInv = 1.0 / H;
                    for (int J = L; J < ColCount; ++J)
                    {
                        double Dot = 0.0;
                        if (I < RowCount)
                        {
                            for (int K = I; K < RowCount; ++K)
                                Dot += (double)(SvdCell(A, K, J)) *
                                       (double)(SvdCell(A, K, I));
                        }
                        const float Factor = (float)(HInv * Dot);
                        if (I < RowCount)
                        {
                            for (int K = I; K < RowCount; ++K)
                                SvdCell(A, K, J) = (float)(
                                    (double)Factor * SvdCell(A, K, I) +
                                    (double)(SvdCell(A, K, J)));
                        }
                    }
                }
                for (int K = I; K < RowCount; ++K)
                    SvdCell(A, K, I) = (float)(Scale * (double)(SvdCell(A, K, I)));
            }
        }
        W[I] = (float)(Scale * (double)G);

        G = 0.0f;
        Scale = 0.0;
        Sum = 0.0;
        if (I < RowCount && I != ColCount && L < ColCount)
        {
            for (int K = L; K < ColCount; ++K)
                Scale += std::fabs((double)(SvdCell(A, I, K)));
            if (Scale != 0.0)
            {
                const double Inv = 1.0 / Scale;
                for (int K = L; K < ColCount; ++K)
                {
                    const double Scaled = Inv * (double)(SvdCell(A, I, K));
                    SvdCell(A, I, K) = (float)Scaled;
                    Sum += Scaled * Scaled;
                }
                const float SumF = (float)Sum;
                const float F = SvdCell(A, I, L);
                const double Gd = SvdNegSignOf(std::sqrt((double)SumF), SvdSignC0(F));
                G = (float)Gd;
                const float H = (float)(Gd * (double)F -
                                                   (double)SumF);
                SvdCell(A, I, L) = (float)((double)F - (double)G);
                if (L < ColCount)
                {
                    const double HInv = 1.0 / (double)H;
                    for (int K = L; K < ColCount; ++K)
                        Rv1[K] = (float)(HInv * (double)(SvdCell(A, I, K)));
                }
                if (L < RowCount)
                {
                    for (int J = L; J < RowCount; ++J)
                    {
                        double Dot = 0.0;
                        if (L < ColCount)
                        {
                            for (int K = L; K < ColCount; ++K)
                                Dot += (double)(SvdCell(A, I, K)) *
                                       (double)(SvdCell(A, J, K));
                        }
                        if (L < ColCount)
                        {
                            for (int K = L; K < ColCount; ++K)
                                SvdCell(A, J, K) = (float)(
                                    Dot * (double)Rv1[K] +
                                    (double)(SvdCell(A, J, K)));
                        }
                    }
                }
                if (L < ColCount)
                {
                    for (int K = L; K < ColCount; ++K)
                        SvdCell(A, I, K) = (float)(Scale * (double)(SvdCell(A, I, K)));
                }
            }
        }
        G_SvdMaxArg1 = Anorm;
        G_SvdMaxArg2 = (float)(std::fabs((double)W[I]) +
                                          std::fabs((double)Rv1[I]));
        if (!(Anorm > G_SvdMaxArg2))
            Anorm = G_SvdMaxArg2;
        I = L;
    }

    // Right-hand transforms into V, i from n-1 down; g (float) and l carry.
    I = ColCount - 1;
    while (I >= 0)
    {
        if (I < ColCount)
        {
            if (G != 0.0f)
            {
                if (L < ColCount)
                {
                    const double GInv = 1.0 / (double)G;
                    for (int J = L; J < ColCount; ++J)
                        SvdCell(V, J, I) = (float)(
                            (double)(SvdCell(A, I, J)) /
                            (double)(SvdCell(A, I, L)) * GInv);
                }
                if (L < ColCount)
                {
                    for (int J = L; J < ColCount; ++J)
                    {
                        double Dot = 0.0;
                        if (L < ColCount)
                        {
                            for (int K = L; K < ColCount; ++K)
                                Dot += (double)(SvdCell(V, K, J)) *
                                       (double)(SvdCell(A, I, K));
                        }
                        if (L < ColCount)
                        {
                            for (int K = L; K < ColCount; ++K)
                                SvdCell(V, K, J) = (float)(
                                    Dot * (double)(SvdCell(V, K, I)) +
                                    (double)(SvdCell(V, K, J)));
                        }
                    }
                }
            }
            if (L < ColCount)
            {
                for (int J = L; J < ColCount; ++J)
                {
                    SvdCell(V, J, I) = 0.0f;
                    SvdCell(V, I, J) = 0.0f;
                }
            }
        }
        SvdCell(V, I, I) = 1.0f;
        G = Rv1[I];
        L = I;
        I -= 1;
    }

    // Left-hand transforms into A, i from min(m, n)-1 down (IMIN through DAT_066032a4 /
    // DAT_066032a0).
    G_SvdMinArg1 = RowCount;
    G_SvdMinArg2 = ColCount;
    const int MinDim = (G_SvdMinArg1 < G_SvdMinArg2) ? G_SvdMinArg1 : G_SvdMinArg2;
    I = MinDim - 1;
    while (I >= 0)
    {
        L = I + 1;
        const float WVal = W[I];
        if (L < ColCount)
        {
            for (int J = L; J < ColCount; ++J)
                SvdCell(A, I, J) = 0.0f;
        }
        if (WVal != 0.0f)
        {
            const float GInv = (float)(1.0 / (double)WVal);
            if (L < ColCount)
            {
                for (int J = L; J < ColCount; ++J)
                {
                    double Dot = 0.0;
                    if (L < RowCount)
                    {
                        for (int K = L; K < RowCount; ++K)
                            Dot += (double)(SvdCell(A, K, I)) *
                                   (double)(SvdCell(A, K, J));
                    }
                    const float Factor = (float)(
                        Dot / (double)(SvdCell(A, I, I)) * (double)GInv);
                    if (I < RowCount)
                    {
                        for (int K = I; K < RowCount; ++K)
                            SvdCell(A, K, J) = (float)(
                                (double)Factor * SvdCell(A, K, I) +
                                (double)(SvdCell(A, K, J)));
                    }
                }
            }
            if (I < RowCount)
            {
                for (int K = I; K < RowCount; ++K)
                    SvdCell(A, K, I) = (float)((double)GInv *
                                                          (double)(SvdCell(A, K, I)));
            }
        }
        else
        {
            if (I < RowCount)
            {
                for (int K = I; K < RowCount; ++K)
                    SvdCell(A, K, I) = 0.0f;
            }
        }
        SvdCell(A, I, I) = (float)((double)(SvdCell(A, I, I)) + 1.0);
        I -= 1;
    }

    // Diagonalisation, k from n-1 down, its from 1.
    int K = ColCount - 1;
    while (K >= 0)
    {
        int Its = 1;
        for (;;)
        {
            // l from k down. |rv1[l]| negligible -> no cancellation;
            // |W[l-1]| negligible (or l ran below 0) -> cancellation with this l.
            bool bCancel = true;
            int Nm = K - 1;
            L = K;
            while (L >= 0)
            {
                Nm = L - 1;
                if (SvdIsNegligible(SvdAbs(Rv1[L]), Anorm))
                {
                    bCancel = false;
                    break;
                }
                if (SvdIsNegligible(SvdAbs(W[Nm]), Anorm))
                    break;
                L = Nm;
            }
            if (bCancel && L <= K)
            {
                // c is stored as float, s stays double on the FPU.
                float Cos = 0.0f;
                double Sin = 1.0;
                for (int Row = L; Row <= K; ++Row)
                {
                    const float F = (float)(Sin * (double)Rv1[Row]);
                    Rv1[Row] = (float)((double)Cos * Rv1[Row]);
                    if (SvdIsNegligible(SvdAbs(F), Anorm))
                        break;
                    const float Gw = W[Row];
                    const double Hyp = SvdPythag(F, Gw);
                    W[Row] = (float)Hyp;
                    const double HInv = 1.0 / Hyp;
                    Cos = (float)((double)Gw * HInv);
                    Sin = -(HInv * (double)F);
                    for (int J = 0; J < RowCount; ++J)
                    {
                        const double Y = (double)(SvdCell(A, J, Nm));
                        const double Z = (double)(SvdCell(A, J, Row));
                        SvdCell(A, J, Nm) = (float)(Y * Cos + Z * Sin);
                        SvdCell(A, J, Row) = (float)(Z * Cos - Y * Sin);
                    }
                }
            }

            const float Z = W[K];
            if (L == K)
            {
                // Only "below zero" flips; an unordered compare (NaN) does not.
                if ((double)Z < 0.0)
                {
                    W[K] = -Z;
                    for (int J = 0; J < ColCount; ++J)
                        SvdCell(V, J, K) = -SvdCell(V, J, K);
                }
                break;
            }
            if (Its == G_SvdMaxIter)
                return 0;

            // Shift from the bottom 2x2 minor.
            float X = W[L];
            Nm = K - 1;
            float Y = W[Nm];
            float Gv = Rv1[Nm];
            float H = Rv1[K];
            float F = (float)(
                (((double)Y - Z) * ((double)Z + Y) +
                 ((double)Gv - H) * ((double)H + Gv)) /
                ((double)Y * H + (double)Y * H));
            const double GSigned = SvdSignC0(F) ? -std::fabs(SvdPythag(F, 1.0f))
                                                : std::fabs(SvdPythag(F, 1.0f));
            F = (float)(
                (((double)Y / ((double)F + GSigned) - H) * H +
                 ((double)X - Z) * ((double)X + Z)) /
                (double)X);
            float Cos = 1.0f;
            float Sin = 1.0f;

            // The QR sweep; every quantity is stored as float between steps.
            for (int J = L; J <= Nm; ++J)
            {
                const int Next = J + 1;
                const float GNext = Rv1[Next];
                Y = W[Next];
                H = (float)((double)Sin * GNext);
                Gv = (float)((double)Cos * GNext);
                double Zd = SvdPythag(F, H);
                Rv1[J] = (float)Zd;
                double ZInv = 1.0 / Zd;
                Cos = (float)((double)F * ZInv);
                Sin = (float)(ZInv * (double)H);
                F = (float)((double)X * Cos + (double)Gv * Sin);
                Gv = (float)((double)Cos * Gv - (double)X * Sin);
                H = (float)((double)Y * Sin);
                Y = (float)((double)Y * Cos);
                for (int Jj = 0; Jj < ColCount; ++Jj)
                {
                    const double Vx = (double)(SvdCell(V, Jj, J));
                    const double Vz = (double)(SvdCell(V, Jj, Next));
                    SvdCell(V, Jj, J) = (float)(Vx * Cos + Vz * Sin);
                    SvdCell(V, Jj, Next) = (float)(Vz * Cos - Vx * Sin);
                }
                Zd = SvdPythag(F, H);
                W[J] = (float)Zd;
                if (Zd != 0.0)
                {
                    ZInv = 1.0 / Zd;
                    Cos = (float)((double)F * ZInv);
                    Sin = (float)(ZInv * (double)H);
                }
                F = (float)((double)Y * Sin + (double)Cos * Gv);
                X = (float)((double)Y * Cos - (double)Sin * Gv);
                for (int Jj = 0; Jj < RowCount; ++Jj)
                {
                    const double Ay = (double)(SvdCell(A, Jj, J));
                    const double Az = (double)(SvdCell(A, Jj, Next));
                    SvdCell(A, Jj, J) = (float)(Ay * Cos + Az * Sin);
                    SvdCell(A, Jj, Next) = (float)(Az * Cos - Ay * Sin);
                }
            }
            W[K] = X;
            Rv1[L] = 0.0f;
            Rv1[K] = F;
            Its += 1;
            if (Its > G_SvdMaxIter)
                break;
        }
        K -= 1;
    }
    return 1;
}

static void SvdScaleColumns(const FSvdMatrix* V, const float* W, FSvdMatrix* Out)
{
    Out->RowCount = V->RowCount;
    Out->ColCount = V->ColCount;
    const int Rows = V->RowCount;
    const int Cols = V->ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
            Out->Cell[Row][Col] = W[Col] * V->Cell[Row][Col];
    }
}

static void SvdMulABt(const FSvdMatrix* Left, const FSvdMatrix* Right, FSvdMatrix* Out)
{
    Out->RowCount = Left->RowCount;
    Out->ColCount = Right->RowCount;
    const int Rows = Left->RowCount;
    // Retail quirk: Out.n = Right.m, but the loop bound is Right.n (+0x244).
    const int OutCols = Right->ColCount;
    const int Inner = Left->ColCount;
    // The cell is accumulated in place, so each step rounds to float once (the product never
    // does on its own).
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < OutCols; ++Col)
        {
            float Sum = 0.0f;
            for (int K = 0; K < Inner; ++K)
                Sum = (float)((double)Right->Cell[Col][K] *
                                             (double)Left->Cell[Row][K] +
                                         (double)Sum);
            Out->Cell[Row][Col] = Sum;
        }
    }
}

// FUN_065fadb0 (thiscall on the source matrix: Out).
void FSvdMatrix::PseudoInverse(FSvdMatrix* Out)
{
    Out->RowCount = ColCount;
    Out->ColCount = RowCount;
    const int N = ColCount;
    float W[12];
    int Index = 0;
    while (Index < N)
    {
        W[Index] = 0.0f;
        Index += 1;
    }
    FSvdMatrix V;
    V.RowCount = ColCount;
    V.ColCount = ColCount;
    FSvdMatrix Work;
    std::memcpy(&Work, this, sizeof(FSvdMatrix));

    if (SvdCmp(&Work.Cell[0][0], W, &V.Cell[0][0]) == 0)
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileMat, 0x2E8,
              "svdcmp: didnt approach a finite soln.");

    Index = 0;
    while (Index < N)
    {
        if ((double)W[Index] < 1.0e-6)
            W[Index] = 0.0f;
        else
            W[Index] = (float)(1.0 / (double)W[Index]);
        Index += 1;
    }

    FSvdMatrix Scaled;
    Scaled.RowCount = V.RowCount;
    Scaled.ColCount = V.ColCount;
    SvdScaleColumns(&V, W, &Scaled);
    SvdMulABt(&Scaled, &Work, Out);
}

// FUN_065f9d70 transpose; FUN_065faef0 A*B; FUN_065fafc0 A*x.
void FSvdMatrix::Transpose(FSvdMatrix* Out) const
{
    Out->RowCount = ColCount;
    Out->ColCount = RowCount;
    const int Rows = RowCount;
    const int Cols = ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
            Out->Cell[Col][Row] = Cell[Row][Col];
    }
}

static void SvdMulAB(const FSvdMatrix* Left, const FSvdMatrix* Right, FSvdMatrix* Out)
{
    Out->RowCount = Left->RowCount;
    Out->ColCount = Right->ColCount;
    const int Rows = Left->RowCount;
    const int Cols = Right->ColCount;
    const int Inner = Left->ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
        {
            // Each step rounds to float once.
            float Sum = 0.0f;
            for (int K = 0; K < Inner; ++K)
                Sum = (float)((double)Left->Cell[Row][K] *
                                             (double)Right->Cell[K][Col] +
                                         (double)Sum);
            Out->Cell[Row][Col] = Sum;
        }
    }
}

static void SvdMulVec(const FSvdMatrix* Left, const float* Vec, float* Out)
{
    const int Rows = Left->RowCount;
    const int Cols = Left->ColCount;
    // Retail stores the integer m bits, not float(m).
    *(int*)(Out + 12) = Rows;
    for (int Row = 0; Row < Rows; ++Row)
    {
        // Each step rounds to float once.
        float Sum = 0.0f;
        for (int Col = 0; Col < Cols; ++Col)
            Sum = (float)((double)Vec[Col] *
                                         (double)Left->Cell[Row][Col] +
                                     (double)Sum);
        Out[Row] = Sum;
    }
}

// FUN_06600de0: CreateInstance "3DMathEngine" (the decompiler's ICOManager return type is wrong).
// Retail keeps one lazily-filled cache per call site (DAT_06603170, DAT_066031a4, ...); the port
// shares one.
static I3DMathEngine* AcquireMathEngine()
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

// Float-array views of the math engine slots the IK solvers call (retail: DAT_066033a8, filled
// by FUN_06600de0 on first use). They add no checks.
static void IkMathMulMatrix(float* Out9, const float* Left9, const float* Right9)
{
    AcquireMathEngine()->MulMatrix(*(FMatrix*)Out9,
                                   *(const FMatrix*)Left9,
                                   *(const FMatrix*)Right9); // +0x20
}

static void IkMathTransform(float* Out3, const float* Mat9, const float* Vec3)
{
    AcquireMathEngine()->TransformMatrix(*(FVector*)Out3,
                                         *(const FMatrix*)Mat9,
                                         *(const FVector*)Vec3); // +0x2C
}

static void IkMathMatrixToQuat(float* Out4, const float* Mat9)
{
    AcquireMathEngine()->MatrixToQuaternion(*(FQuaternion*)Out4,
                                            *(const FMatrix*)Mat9); // +0x50
}

static void IkMathQuatToMatrix(float* Out9, const float* Quat4)
{
    AcquireMathEngine()->QuaternionToMatrix(*(FMatrix*)Out9,
                                            *(const FQuaternion*)Quat4); // +0x54
}

// FUN_065f7120: math (DAT_066031a4) +0x38 into a temp, copied to Dest. No tests.
void MathTransformVec3(float* Dest, const float* A, const float* B)
{
    I3DMathEngine* Math = AcquireMathEngine();
    float Tmp[3];
    Math->TransposeTransform(*(FVector*)Tmp,
                                                          *(const FMatrix*)A,
                                                          *(const FVector*)B); // +0x38
    Dest[0] = Tmp[0];
    Dest[1] = Tmp[1];
    Dest[2] = Tmp[2];
}

// An IK joint id (JointIds[i], StartJoint) is an IEngine instance handle.
static void* IkJointInstance(int JointId)
{
    return (void*)((ptrdiff_t)JointId);
}

static void IkCopy3(float* Dest, const float* Src)
{
    Dest[0] = Src[0];
    Dest[1] = Src[1];
    Dest[2] = Src[2];
}

static void IkCopy9(float* Dest, const float* Src)
{
    int Index = 0;
    while (Index < 9)
    {
        Dest[Index] = Src[Index];
        Index += 1;
    }
}

static void IkCopy4(float* Dest, const float* Src)
{
    Dest[0] = Src[0];
    Dest[1] = Src[1];
    Dest[2] = Src[2];
    Dest[3] = Src[3];
}

// FUN_065fb480 (ECX = vec): returned in ST0 as a double, never rounded to float.
static double IkLen3(const float* Vec)
{
    const double X = (double)Vec[0];
    const double Y = (double)Vec[1];
    const double Z = (double)Vec[2];
    return std::sqrt(X * X + Y * Y + Z * Z);
}

// FUN_065fb4b0: each component is a difference of two double products, stored once as float.
static void IkCross3(float* Dest, const float* Left, const float* Right)
{
    const double Z = (double)Left[0] * Right[1] - (double)Left[1] * Right[0];
    const double Y = (double)Left[2] * Right[0] - (double)Right[2] * Left[0];
    const double X = (double)Left[1] * Right[2] - (double)Left[2] * Right[1];
    Dest[0] = (float)X;
    Dest[1] = (float)Y;
    Dest[2] = (float)Z;
}

// FUN_065fb4f0: columns → 3×3.
static void IkColsToMat(float* Out9, const float* Col0, const float* Col1, const float* Col2)
{
    Out9[0] = Col0[0];
    Out9[3] = Col0[1];
    Out9[6] = Col0[2];
    Out9[1] = Col1[0];
    Out9[4] = Col1[1];
    Out9[7] = Col1[2];
    Out9[2] = Col2[0];
    Out9[5] = Col2[1];
    Out9[8] = Col2[2];
}

// FUN_065fb100 (Out, Dir, Parent 3x3, flags byte).
static void IkLookAt(float* Out9, const float* Dir, const float* Parent9, unsigned char Flags)
{
    float Col0[3] = {0.0f, 0.0f, 0.0f};
    float Col1[3] = {0.0f, 0.0f, 0.0f};
    float Col2[3] = {0.0f, 0.0f, 0.0f};
    if ((Flags & 4) != 0)
    {
        IkCopy3(Col0, Dir);
        const double P2 = (double)Parent9[2];
        const double P5 = (double)Parent9[5];
        const double P8 = (double)Parent9[8];
        Col2[0] = (float)((double)Col0[2] * P5 - (double)Col0[1] * P8);
        Col2[1] = (float)(P8 * (double)Col0[0] - (double)Col0[2] * P2);
        Col2[2] = (float)((double)Col0[1] * P2 - P5 * (double)Col0[0]);
        const double Y0 = (double)Col2[0];
        const double Y1 = (double)Col2[1];
        const double Y2 = (double)Col2[2];
        const double Inv = 1.0 / std::sqrt(Y2 * Y2 + Y1 * Y1 + Y0 * Y0);
        Col1[0] = (float)(Y0 * Inv);
        Col1[1] = (float)(Y1 * Inv);
        Col1[2] = (float)(Y2 * Inv);
        IkCross3(Col2, Col0, Col1);
    }
    else if ((Flags & 8) != 0)
    {
        IkCopy3(Col1, Dir);
        float Temp[3];
        Temp[0] = Parent9[0];
        Temp[1] = Parent9[3];
        Temp[2] = Parent9[6];
        IkCross3(Col2, Temp, Col1);
        const double Inv = 1.0 / IkLen3(Col2);
        Col2[0] = (float)((double)Col2[0] * Inv);
        Col2[1] = (float)((double)Col2[1] * Inv);
        Col2[2] = (float)((double)Col2[2] * Inv);
        IkCross3(Col0, Col1, Col2);
    }
    else if ((Flags & 0x10) != 0)
    {
        IkCopy3(Col2, Dir);
        Col1[0] = Parent9[1];
        Col1[1] = Parent9[4];
        Col1[2] = Parent9[7];
        IkCross3(Col0, Col1, Col2);
        const double Inv = 1.0 / IkLen3(Col0);
        Col0[0] = (float)((double)Col0[0] * Inv);
        Col0[1] = (float)((double)Col0[1] * Inv);
        Col0[2] = (float)((double)Col0[2] * Inv);
        IkCross3(Col1, Col2, Col0);
    }
    IkColsToMat(Out9, Col0, Col1, Col2);
}

// DAT_066013b0: the double widening of 1e-5f (bytes 00000080b5f8e43e).
static const double G_IkLogEps = (double)1.0e-5f;
// DAT_06601348: the double 1e-5 (bytes f168e388b5f8e43e).
static const double G_IkExpEps = 1.0e-5;

// FUN_065fbc90 (Out3, Quat wxyz): quat -> log axis (acos / sin).
static void IkQuatLogVec(float* Out3, const float* Quat)
{
    const double W = (double)Quat[0];
    if (std::fabs(1.0 - W) < G_IkLogEps)
    {
        Out3[0] = 0.0f;
        Out3[1] = 0.0f;
        Out3[2] = 0.0f;
        return;
    }
    const double Scale = std::acos(W) / std::sqrt(1.0 - W * W);
    Out3[0] = (float)(Scale * (double)Quat[1]);
    Out3[1] = (float)(Scale * (double)Quat[2]);
    Out3[2] = (float)(Scale * (double)Quat[3]);
}

// FUN_065fbd30 (Out4 wxyz, Axis).
static void IkQuatExp(float* Out4, const float* Axis)
{
    const double Len = IkLen3(Axis);
    if (!(Len > G_IkExpEps))
    {
        Out4[0] = 1.0f;
        Out4[1] = 0.0f;
        Out4[2] = 0.0f;
        Out4[3] = 0.0f;
        return;
    }
    const float W = (float)(std::cos(Len));
    const double Scale = std::sin(Len) / Len;
    Out4[0] = W;
    Out4[1] = (float)(Scale * (double)Axis[0]);
    Out4[2] = (float)(Scale * (double)Axis[1]);
    Out4[3] = (float)(Scale * (double)Axis[2]);
}

// FUN_065fbdf0 (Out3, A, B -- wxyz).
static void IkQuatLog(float* Out3, const float* QuatA, const float* QuatB)
{
    const double Dot = (double)QuatA[3] * QuatB[3] +
                       (double)QuatA[2] * QuatB[2] +
                       (double)QuatA[1] * QuatB[1] +
                       (double)QuatA[0] * QuatB[0];
    float UseB[4];
    if (!(Dot >= 0.0))
    {
        UseB[0] = -QuatB[0];
        UseB[1] = -QuatB[1];
        UseB[2] = -QuatB[2];
        UseB[3] = -QuatB[3];
    }
    else
    {
        IkCopy4(UseB, QuatB);
    }
    float ConjA[4];
    ConjA[0] = QuatA[0];
    ConjA[1] = -QuatA[1];
    ConjA[2] = -QuatA[2];
    ConjA[3] = -QuatA[3];
    float Prod[4];
    AcquireMathEngine()->MulQuat(*(FQuaternion*)Prod,
                                 *(const FQuaternion*)UseB,
                                 *(const FQuaternion*)ConjA); // +0x18
    float Log3[3];
    IkQuatLogVec(Log3, Prod);
    Out3[0] = Log3[0] + Log3[0];
    Out3[1] = Log3[1] + Log3[1];
    Out3[2] = Log3[2] + Log3[2];
}

// The root-bone read retail inlines at several sites; PartArray (+8) is the only test.
static int FirstBoneInstance(FDeformInstance* Instance)
{
    if (Instance->PartArray == nullptr)
        return -1;
    return (int)(
        (ptrdiff_t)(Instance->PartArray[0].Lod->BoneSlots[0].Instance));
}

// FUN_065f5950 (thiscall; the instance in ECX is never read).
static void StopAimNode(FIkListNode* Node)
{
    if (Node == (FIkListNode*)((ptrdiff_t)-1))
        return;
    Node->Event->StateFlags = (unsigned char)(Node->Event->StateFlags | 1);
    if (G_Animation == nullptr)
        return;
    int Cursor = Node->Event->EndJoint;
    while (Cursor != Node->Event->StartJoint)
    {
        G_Animation->HasAnyHandle((unsigned long)Cursor);
        Cursor = (int)((ptrdiff_t)(
            G_Engine->GetInstanceParent((void*)((ptrdiff_t)Cursor))));
    }
}

// DAT_066013a8: the float 0x36000000 = 2^-19, written out exactly.
static const float G_IkGimbalEps = 1.9073486328125e-06f;

// The Euler rebuild FUN_065f5400 and FUN_065fb900 inline.
static void IkEulerRebuild(float* Out9, float Yaw, float Pitch, float Roll)
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
    Out9[0] = (float)(CosR * CosP);
    Out9[1] = (float)(SinPSinY * CosR - (double)SinRCosY);
    Out9[2] = (float)((double)CosRCosY * SinP + SinR * SinY);
    Out9[3] = (float)(SinR * CosP);
    Out9[4] = (float)(SinPSinY * SinR + (double)CosRCosY);
    Out9[5] = (float)((double)SinRCosY * SinP - CosR * SinY);
    Out9[6] = (float)-SinP;
    Out9[7] = (float)(CosP * SinY);
    Out9[8] = (float)(CosP * CosY);
}

// FUN_065fb090: count of yaw/pitch/roll outside [min,max] on Relative.
static int IkEulerOutOfRange(const float* Bone, float Yaw, float Pitch, float Roll)
{
    int Count = 0;
    if (Yaw < Bone[0x4C / 4] || Bone[0x50 / 4] < Yaw)
        Count += 1;
    if (Pitch < Bone[0x54 / 4] || Bone[0x58 / 4] < Pitch)
        Count += 1;
    if (Roll < Bone[0x40 / 4] || Bone[0x44 / 4] < Roll)
        Count += 1;
    return Count;
}

// FUN_065fbb90 (OutYaw, OutPitch, OutRoll, Mat9, bAlt byte).
static void IkEulerExtract(float* OutYaw, float* OutPitch, float* OutRoll, const float* Mat9,
                           bool bAlt)
{
    const double M0 = (double)Mat9[0];
    const double M3 = (double)Mat9[3];
    const double M6 = (double)Mat9[6];
    const double Hyp = std::sqrt(M0 * M0 + M3 * M3);
    if (!(Hyp > (double)G_IkGimbalEps))
    {
        if (bAlt)
        {
            *OutYaw = (float)(std::atan2((double)Mat9[5], -(double)Mat9[4]));
            *OutPitch = (float)(std::atan2(M6, -Hyp));
        }
        else
        {
            *OutYaw = (float)(std::atan2(-(double)Mat9[5], (double)Mat9[4]));
            *OutPitch = (float)(std::atan2(-M6, Hyp));
        }
        *OutRoll = 0.0f;
        return;
    }
    if (bAlt)
    {
        *OutYaw = (float)(std::atan2((double)Mat9[7], -(double)Mat9[8]));
        *OutPitch = (float)(std::atan2(-M6, -Hyp));
        *OutRoll = (float)(std::atan2(M3, -M0));
        return;
    }
    *OutYaw = (float)(std::atan2((double)Mat9[7], (double)Mat9[8]));
    *OutPitch = (float)(std::atan2(-M6, Hyp));
    *OutRoll = (float)(std::atan2(M3, M0));
}

// FUN_065fb900 (Out9, Bone, Mat9). Returns Out9.
static float* IkEulerClamp(float* Out9, const float* Bone, const float* Mat9)
{
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float Roll = 0.0f;
    IkEulerExtract(&Yaw, &Pitch, &Roll, Mat9, false);
    const int Score = IkEulerOutOfRange(Bone, Yaw, Pitch, Roll);
    if (Score != 0)
    {
        float AltYaw = 0.0f;
        float AltPitch = 0.0f;
        float AltRoll = 0.0f;
        IkEulerExtract(&AltYaw, &AltPitch, &AltRoll, Mat9, true);
        if (!(IkEulerOutOfRange(Bone, AltYaw, AltPitch, AltRoll) > Score))
        {
            Yaw = AltYaw;
            Pitch = AltPitch;
            Roll = AltRoll;
        }
    }

    bool bChanged = false;
    if (Yaw < Bone[0x4C / 4])
    {
        Yaw = Bone[0x4C / 4];
        bChanged = true;
    }
    if (Yaw > Bone[0x50 / 4])
    {
        Yaw = Bone[0x50 / 4];
        bChanged = true;
    }
    if (Pitch < Bone[0x54 / 4])
    {
        Pitch = Bone[0x54 / 4];
        bChanged = true;
    }
    if (Pitch > Bone[0x58 / 4])
    {
        Pitch = Bone[0x58 / 4];
        bChanged = true;
    }
    if (Roll < Bone[0x40 / 4])
    {
        Roll = Bone[0x40 / 4];
        bChanged = true;
    }
    if (Roll > Bone[0x44 / 4])
    {
        Roll = Bone[0x44 / 4];
        bChanged = true;
    }

    float Local[9];
    if (!bChanged)
        IkCopy9(Local, Mat9);
    else
        IkEulerRebuild(Local, Yaw, Pitch, Roll);
    IkCopy9(Out9, Local);
    return Out9;
}

// Shared helpers for the two damped-least-squares solvers.

// FSvdMatrix-shaped vectors (FUN_065fafc0 writes its row count into the thirteenth dword).
struct FIkVec
{
    float Cell[12];
    int Count;
};

static_assert(sizeof(FIkVec) == 0x34, "IK vector is 13 dwords");

// Keep Rest9 adjacent: a locked first joint writes into the preceding cells.
struct FIkDlsJacobian
{
    float Rest9[9];
    FSvdMatrix J;
};

static_assert(offsetof(FIkDlsJacobian, J) == 0x24, "the Jacobian sits right after the scratch");

// The Jacobian's cells as the listing addresses them: flat, 12 per row, from Cell[0][0].
static float* IkDlsCells(FIkDlsJacobian* Block)
{
    return (float*)Block + 9;
}

// An unlocked joint's position rows: the skew matrix of the joint's lever arm (X, Y, Z) in columns Col..Col+2 of rows 0..2.
static void IkDlsWriteSkew(float* Cells, int Col, float AxisX, float AxisY, float AxisZ)
{
    Cells[Col] = 0.0f;
    Cells[Col + 1] = AxisZ;
    Cells[Col + 2] = -AxisY;
    Cells[Col + 12] = -AxisZ;
    Cells[Col + 13] = 0.0f;
    Cells[Col + 14] = AxisX;
    Cells[Col + 24] = AxisY;
    Cells[Col + 25] = -AxisX;
    Cells[Col + 26] = 0.0f;
}

// A locked joint's position rows: its skew terms are ADDED into the previous three columns (the
// joint moves with the last unlocked one). Each add rounds to float once. No test of Col.
static void IkDlsAddSkew(float* Cells, int Col, float AxisX, float AxisY, float AxisZ)
{
    Cells[Col - 2] = AxisZ + Cells[Col - 2];
    Cells[Col - 1] = Cells[Col - 1] - AxisY;
    Cells[Col + 9] = Cells[Col + 9] - AxisZ;
    Cells[Col + 11] = AxisX + Cells[Col + 11];
    Cells[Col + 21] = AxisY + Cells[Col + 21];
    Cells[Col + 22] = Cells[Col + 22] - AxisX;
}

// An unlocked joint's orientation rows: identity in the 3x3
// block at row PosDim, column Col (First = Col + 12 * PosDim).
static void IkDlsWriteIdentity(float* Cells, int First)
{
    Cells[First] = 1.0f;
    Cells[First + 1] = 0.0f;
    Cells[First + 2] = 0.0f;
    Cells[First + 12] = 0.0f;
    Cells[First + 13] = 1.0f;
    Cells[First + 14] = 0.0f;
    Cells[First + 24] = 0.0f;
    Cells[First + 25] = 0.0f;
    Cells[First + 26] = 1.0f;
}

// The damping: Lam2 = float(Damping * Damping); every diagonal cell of JtJ becomes
// float(Lam2 + cell).
static void IkDlsDamp(FSvdMatrix* JtJ, float Damp)
{
    const float Lam2 = (float)((double)Damp * (double)Damp);
    int Row = 0;
    while (Row < JtJ->RowCount)
    {
        int Col = 0;
        while (Col < JtJ->ColCount)
        {
            if (Row == Col)
                JtJ->Cell[Row][Col] = (float)((double)Lam2 +
                                                         (double)JtJ->Cell[Row][Col]);
            Col += 1;
        }
        Row += 1;
    }
}

// The rest pull, run when AimFlags bit 7 is clear.
static void IkDlsFillRestPull(FIkEvent* Event, int JointIters, float* Rest9, float* Pull)
{
    float* Cursor = Pull + 1;
    int Joint = 0;
    while (Joint < JointIters)
    {
        if (Event->LockBytes[Joint] == 0)
        {
            float Product[9];
            IkMathMulMatrix(Product, G_IkCurRot[Joint], Event->BindMats + Joint * 9);
            IkCopy9(Rest9, Product);
            float CurQ[4];
            IkMathMatrixToQuat(CurQ, G_IkCurRot[Joint + 1]);
            float RestQ[4];
            IkMathMatrixToQuat(RestQ, Rest9);
            float Log3[3];
            IkQuatLog(Log3, CurQ, RestQ);
            Cursor[-1] = Log3[0];
            Cursor[0] = Log3[1];
            Cursor[1] = Log3[2];
            Cursor += 3;
        }
        Joint += 1;
    }
}

// The trial step over the first JointIters joints.
static void IkDlsStepJoints(FIkEvent* Event, int JointIters, const float* Delta, float* Held3)
{
    const float* Cursor = Delta + 2;
    int Joint = 0;
    while (Joint < JointIters)
    {
        const float* Bone = (const float*)(
            G_Engine->GetInstanceRelative(IkJointInstance(Event->JointIds[Joint])));
        if (Event->LockBytes[Joint] == 0)
        {
            Held3[0] = Cursor[-2];
            Held3[1] = Cursor[-1];
            Held3[2] = Cursor[0];
            Cursor += 3;
        }
        float Axis[3];
        Axis[0] = (float)((double)Held3[0] * 0.5);
        Axis[1] = (float)((double)Held3[1] * 0.5);
        Axis[2] = (float)((double)Held3[2] * 0.5);
        float StepQ[4];
        IkQuatExp(StepQ, Axis);
        float StepM[9];
        IkMathQuatToMatrix(StepM, StepQ);
        float Rotated[9];
        IkMathMulMatrix(Rotated, StepM, G_IkCurRot[Joint + 1]);
        IkCopy9(G_IkNewRot[Joint + 1], Rotated);
        float Arm[3];
        IkMathTransform(Arm, G_IkNewRot[Joint], Bone + 1);
        G_IkNewPos[Joint + 1][0] = (float)((double)Arm[0] +
                                                      (double)G_IkNewPos[Joint][0]);
        G_IkNewPos[Joint + 1][1] = (float)((double)Arm[1] +
                                                      (double)G_IkNewPos[Joint][1]);
        G_IkNewPos[Joint + 1][2] = (float)((double)Arm[2] +
                                                      (double)G_IkNewPos[Joint][2]);
        Joint += 1;
    }
}

// The squared residual, the difference and its square in double (0.0 is DAT_06601318).
static double IkDlsResidual(const FIkVec* Error, const FIkVec* Achieved, int TaskDim)
{
    double Sum = 0.0;
    int Index = 0;
    while (Index < TaskDim)
    {
        const double Diff = (double)Error->Cell[Index] -
                            (double)Achieved->Cell[Index];
        Sum = Sum + Diff * Diff;
        Index += 1;
    }
    return Sum;
}

// The write-back: for each unlocked joint among the first JointIters, the local rotation
// transpose(NewRot[i]) x NewRot[i + 1] goes to Quats[i].
static void IkDlsWriteQuats(FIkEvent* Event, int JointIters)
{
    int Joint = 0;
    while (Joint < JointIters)
    {
        if (Event->LockBytes[Joint] == 0)
        {
            const float* Parent = G_IkNewRot[Joint];
            float ParentT[9];
            ParentT[0] = Parent[0];
            ParentT[1] = Parent[3];
            ParentT[2] = Parent[6];
            ParentT[3] = Parent[1];
            ParentT[4] = Parent[4];
            ParentT[5] = Parent[7];
            ParentT[6] = Parent[2];
            ParentT[7] = Parent[5];
            ParentT[8] = Parent[8];
            float Product[9];
            IkMathMulMatrix(Product, ParentT, G_IkNewRot[Joint + 1]);
            float Local[9];
            IkCopy9(Local, Product);
            float Q[4];
            IkMathMatrixToQuat(Q, Local);
            IkCopy4(Event->Quats + Joint * 4, Q);
        }
        Joint += 1;
    }
}

// FUN_065fb030 (thiscall on the event: JointCount).
static bool SetIkJointCount(FIkEvent* Event, unsigned int JointCount)
{
    if (JointCount > (unsigned int)G_IkMaxJoints)
    {
        char Message[0x2000];
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformIk, 0x15,
              FormatDeformMessage(Message, "too many joints specified for ik event. %d / %d",
                                  (int)JointCount, G_IkMaxJoints));
        return false;
    }
    Event->JointCount = (int)JointCount;
    return true;
}


// FUN_065fbf40; vtable 06601340.
FIkEvent::~FIkEvent()
{
    int Index = 0;
    while (Index < JointCount)
    {
        G_Channel->Destroy(
            (unsigned long)((ptrdiff_t)Channels[Index]));
        Index += 1;
    }
    JointCount = 0;
    delete[] (unsigned char*)JointIds;
    JointIds = nullptr;
    delete[] (unsigned char*)Channels;
    Channels = nullptr;
    delete[] (unsigned char*)Quats;
    Quats = nullptr;
    delete[] (unsigned char*)BindMats;
    BindMats = nullptr;
    delete[] LockBytes;
    LockBytes = nullptr;
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
    Node->Event = (FIkEvent*)((ptrdiff_t)-1);
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
            if ((Event->StateFlags & 1) != 0 &&
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
        RootHeightOffset = PartArray[0].Lod->BoneSlots[0].LocalTransform[10] - BaseHeight;
    }
}

// FUN_065f6930.
bool FDeformPart::SelectLod(int LodIndex)
{
    FDeformMesh* Old = Current;
    if (LodIndex < 0 || MeshCount <= LodIndex)
    {
        Current = nullptr;
        this->LodIndex = LodIndex;
        return Old != Current;
    }
    FDeformMesh* Next = Meshes[LodIndex];
    Current = Next;
    if ((Flags & 1) == 0 && Old == Next)
    {
        Flags = (unsigned char)(Flags & (unsigned char)(~1));
        this->LodIndex = LodIndex;
        return Old != Current;
    }
    // Retail's XOR/AND/XOR sets bit 0 whatever it was (it does not toggle).
    Flags = (unsigned char)(Flags | 1);
    this->LodIndex = LodIndex;
    return Old != Current;
}

// FUN_065f69c0 (thiscall; its one argument is never read). Retail returns AL = 1.
void FDeformPart::MarkSkinDirty()
{
    Flags = (unsigned char)(Flags | 2);
}

// FUN_065f69d0 (thiscall on the part: Camera, Unused).
unsigned char FDeformPart::DrawLive(ICamera* Camera, void* /*Unused*/)
{
    if (Current == nullptr)
        return 1;
    const bool bVbSelected = G_VertexBuffer->SelectVb(Vb.Listing.VertexBuffer) >= 0;
    const unsigned char Flags = this->Flags;
    const unsigned char bRebuild = ((Flags & 1) != 0 || !bVbSelected) ? 1 : 0;
    if ((Flags & 2) != 0 || bRebuild != 0)
    {
        if (Current->Skin(Lod, &Vb, bRebuild) == 0)
            return 0;
        if (G_VertexBuffer->SelectVb(Vb.Listing.VertexBuffer) < 0)
            return 0;
    }
    const bool bIbSelected =
        G_IndexBuffer->SelectIb(Ib.Scratch.IndexBuffer,
                                (unsigned int)(Vb.Listing.BaseVertex), 0, 0) >= 0;
    if ((this->Flags & 1) != 0 || !bIbSelected)
    {
        if (Current->FillIndexBuffer(&Ib) == 0)
            return 0;
        if (G_IndexBuffer->SelectIb(Ib.Scratch.IndexBuffer,
                                    (unsigned int)(Vb.Listing.BaseVertex), 0,
                                    0) < 0)
            return 0;
    }
    Current->Draw(Camera, this);
    this->Flags = (unsigned char)(this->Flags & 0xFC);
    return 1;
}

static void WriteVec3(float* Dest, float X, float Y, float Z);
static void ScaleVec3(float* Out, const float* In, float Scale);

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
        Name, AsOpaqueDword(BoneHandle), (const char*)Extra,
        (IChannelSink*)this, Script));
    if (Handle == -1)
        return -1;

    FBoneRuntime* Bone = PartArray[0].Lod->BoneSlots;
    const float* BonePos =
        Bone->LocalTransform + 9;
    float Vec[3];
    Vec[0] = 0.0f;
    Vec[1] = 0.0f;
    Vec[2] = 0.0f;
    const float ArgCFloat = FloatFromOpaqueDword(ArgC);
    if (ArgCFloat > 0.0f)
    {
        IRootData* Root = nullptr;
        if (G_Animation->QueryInterface(Iids::RootData, (void**)&Root) == 0)
        {
            // Retail pushes, last first: 1.0f, ArgF, ArgD, ArgE, ArgC, ArgG, &Vec, Handle.
            const unsigned char bTouched = Root->Eval(
                (unsigned long)Handle, Vec,
                (unsigned long)((ptrdiff_t)ArgG), ArgCFloat,
                FloatFromOpaqueDword(ArgE), ArgD, ArgF, 1.0f);
            if (bTouched != 0)
            {
                Vec[2] = 0.0f;
                Vec[0] = 0.0f;
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
    float Pos[3];
    Pos[0] = BonePos[0] + Vec[0];
    Pos[1] = Vec[1] + Y;
    Pos[2] = BonePos[2] + Vec[2];

    float Quat[4];
    float UpAxis[3];
    const float Twist = FloatFromOpaqueDword(ArgH);
    if (Twist < 0.0f)
    {
        float Row[3];
        WriteVec3(Row, Bone->LocalTransform[2], Bone->LocalTransform[5], Bone->LocalTransform[8]);
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

    float Xform[12];
    I3DMathEngine* Math = AcquireMathEngine();
    Math->QuaternionToMatrix(*(FMatrix*)Xform,
                             *(const FQuaternion*)Quat); // +0x54
    Xform[9] = Pos[0];
    Xform[10] = Pos[1];
    Xform[11] = Pos[2];
    const long Applied = (long)(G_Animation->EvalPlayback(
        (unsigned long)Handle,
        (unsigned long)((ptrdiff_t)ArgG), ArgCFloat,
        FloatFromOpaqueDword(ArgE), ArgD, ArgF, AsOpaqueDword(1.0f), (void*)Xform));
    if (Applied < 0)
    {
        G_Animation->DestroyScript((void*)((ptrdiff_t)Handle));
        return -1;
    }

    FIkListNode* Node = new FIkListNode;
    Node->Active = 0;
    LinkListNode(this, Node);
    Node->Event = (FIkEvent*)((ptrdiff_t)Handle);
    std::memcpy(&Node->StartHeight, &Y, sizeof(float));
    return Handle;
}

// FUN_065f4580 (thiscall).
static void WriteVec3(float* Dest, float X, float Y, float Z)
{
    Dest[0] = X;
    Dest[1] = Y;
    Dest[2] = Z;
}

// FUN_065f45a0.
static void ScaleVec3(float* Out, const float* In, float Scale)
{
    Out[0] = In[0] * Scale;
    Out[1] = In[1] * Scale;
    Out[2] = In[2] * Scale;
}

// FUN_065f4c50.
static void SubVec3(float* Out, const float* A, const float* B)
{
    Out[0] = A[0] - B[0];
    Out[1] = A[1] - B[1];
    Out[2] = A[2] - B[2];
}

// FUN_065f4660 (thiscall: Name, Extra, Time, ArgD, ArgE, ArgF, ArgG, ArgH, Joint).
int FDeformInstance::PlayAnimChain(void* Name, void* Extra, void* ArgC, void* ArgD, void* ArgE,
                                   void* ArgF, void* ArgG, void* ArgH, void* Joint)
{
    const float* const JointPos = G_Engine->GetInstancePosition(Joint);
    float StartPos[3];
    StartPos[0] = JointPos[0];
    StartPos[1] = JointPos[1];
    StartPos[2] = JointPos[2];

    const int PlayHandle =
        PlayAnim(Name, Extra, ArgC, ArgD, ArgE, ArgF, ArgG, ArgH, nullptr);
    const unsigned long Play = (unsigned long)PlayHandle;
    const float Time = FloatFromOpaqueDword(ArgC);

    void* const Stop = (void*)((ptrdiff_t)(FirstBoneInstance(this)));

    // EngBase FAnimBindOut {Kind, Sink, SinkContext, Instance}; +0x10 of the frame is outside it.
    unsigned long Query[4];
    Query[0] = 0;
    Query[1] = 0;
    Query[2] = 0;
    Query[3] = 0xFFFFFFFF;

    std::vector<const unsigned char*> Relatives;
    std::vector<FQuaternion> ChainQuats;
    void* Cursor = Joint;
    while (Cursor != Stop)
    {
        void* const Parent = G_Engine->GetInstanceParent(Cursor);
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
        FQuaternion Quat;
        AcquireMathEngine()->QuatSlerp(Quat, *(const FQuaternion*)KeyA,
                                       *(const FQuaternion*)KeyB, Alpha);
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

    FQuaternion RootQuat;
    AcquireMathEngine()->QuatSlerp(RootQuat, *(const FQuaternion*)(RootA + 0xC),
                                   *(const FQuaternion*)(RootB + 0xC), RootAlpha);
    RootArchetype->Release();

    FMatrix Rot;
    AcquireMathEngine()->QuaternionToMatrix(Rot, RootQuat);
    FMatrix Running = Rot;
    int Index = ChainCount - 1;
    while (Index >= 0)
    {
        FMatrix Step;
        AcquireMathEngine()->QuaternionToMatrix(Step, ChainQuats[Index]);
        const unsigned char* const Relative = Relatives[Index];
        FMatrix Mul;
        AcquireMathEngine()->MulMatrix(Mul, Running, Step);
        AcquireMathEngine()->MulMatrix(Rot, Mul,
                                       *(const FMatrix*)(Relative + 0x1C));
        FVector XfA;
        AcquireMathEngine()->TransformMatrix(XfA, Rot,
                                             *(const FVector*)(Relative + 0x10));
        FVector XfB;
        AcquireMathEngine()->TransformMatrix(XfB, Running,
                                             *(const FVector*)(Relative + 4));
        const float SumZ =
            (float)((double)XfB.Z + (double)Accum[2]);
        const float X = (float)(
            (double)XfB.X + (double)Accum[0] - (double)XfA.X);
        const float Y = (float)(
            (double)XfB.Y + (double)Accum[1] - (double)XfA.Y);
        const float Z = (float)((double)SumZ - (double)XfA.Z);
        Accum[0] = X;
        Accum[1] = Y;
        Accum[2] = Z;
        Tail[0] = X;
        Tail[1] = Y;
        Tail[2] = Z;
        Running = Rot;
        Index -= 1;
    }

    const float* const BonePos = PartArray[0].Lod->BoneSlots[0].LocalTransform + 9;
    const float DiffZ = (float)((double)StartPos[2] - BonePos[2]);
    float Delta[3];
    Delta[0] = (float)((double)StartPos[0] - BonePos[0] -
                                  (double)Tail[0]);
    Delta[1] = (float)((double)StartPos[1] - BonePos[1] -
                                  (double)Tail[1]);
    Delta[2] = (float)((double)DiffZ - (double)Tail[2]);
    G_Animation->AddOverlayPos(Play, Delta);
    return PlayHandle;
}

// The locals both solvers share, by their frame slots (FUN_065fc000 / FUN_065fce60). Nothing
// in the frame is cleared: J, the vectors, TargetPos / TargetMat when their flag is clear and
// Held3 before the first unlocked joint hold whatever was there, as in retail.
struct FIkDlsFrame
{
    bool bPos;               // [0x13] / [0x13]  AimFlags bit 5
    bool bOri;               // [0x11] / [0x12]  AimFlags bit 6
    int PosDim;              // [0x48] / [0x54]  bPos ? 3 : 0
    int TaskDim;             // [0x58] / [0x58]  PosDim + (bOri ? 3 : 0)
    int Dof;                 // [0x38] / [0x40]
    int JointIters;          // [0x30] / [0x44]
    int EndIndex;            // [0x64] / [0x70]  JointCount, the chain end's scratch row
    int Counter;             // [0x24] / [0x20]
    int CounterFloor;        // [0xa8] / [0x9c]
    float Alpha;             // [0x20] / [0x28]
    float Accum;             // [0x1c] / [0x24]
    float TargetPos[3];      // [0x68] / [0x5c]  TargetA, copied when bPos
    float TargetMat[9];      // [0x2d0] / [0x2c4] TargetB, copied when bOri
    float TargetQ[4];        // [0x124] / [0x118]
    float Held3[3];          // [0xac] / [0xac]
    FIkDlsJacobian Block;    // [0x2f4] / [0x2e8]
    FSvdMatrix JtJ;          // [0x560] / [0x554]
    FSvdMatrix Jt;           // [0x7a8] / [0x79c]
    FSvdMatrix Pinv;         // [0x9f0] / [0x9e4]
    FIkVec Error;            // [0x134] / [0x128]
    FIkVec Achieved;         // [0x168] / [0x15c]
    FIkVec JtE;              // [0x19c] / [0x190]
    FIkVec Delta;            // [0x268] / [0x25c]
    FIkVec Pull;             // [0x29c] / [0x290]
};

// The entry reads of the chain's start: IEngine +0x90 (position) and +0x98 (orientation).
static void IkDlsReadStart(FIkEvent* Event)
{
    const float* StartPos = G_Engine->GetInstancePosition(IkJointInstance(Event->StartJoint));
    IkCopy3(G_IkNewPos[0], StartPos);
    IkCopy3(G_IkCurPos[0], StartPos);
    IkCopy9(G_IkNewRot[0], G_Engine->GetInstanceOrientation(IkJointInstance(Event->StartJoint)));
    IkCopy9(G_IkCurRot[0], G_IkNewRot[0]);
}

// Whether the chain end's orientation is off target.
static bool IkDlsOriOff(const FIkDlsFrame* Frame)
{
    float CurQ[4];
    IkMathMatrixToQuat(CurQ, G_IkCurRot[Frame->EndIndex]);
    float Log3[3];
    IkQuatLog(Log3, CurQ, Frame->TargetQ);
    const double X = (double)Log3[0];
    const double Y = (double)Log3[1];
    const double Z = (double)Log3[2];
    return std::sqrt((Y * Y + Z * Z) + X * X) > 0.01;
}

// The dimension stores before the first round. The J counts are the only ones read as stored;
// FUN_065f9d70, FUN_065faef0 and FUN_065fafc0 rewrite the others.
static void IkDlsSetup(FIkDlsFrame* Frame)
{
    Frame->Block.J.RowCount = Frame->TaskDim;
    Frame->Block.J.ColCount = Frame->Dof;
    Frame->Jt.RowCount = Frame->Dof;
    Frame->Jt.ColCount = Frame->TaskDim;
    Frame->JtJ.RowCount = Frame->TaskDim + Frame->Dof;
    Frame->JtJ.ColCount = Frame->Dof;
    Frame->Delta.Count = Frame->Dof;
    Frame->Achieved.Count = Frame->TaskDim;
    Frame->Counter = 1;
    Frame->CounterFloor = 1;
    Frame->Alpha = 1.0f;
    Frame->Accum = 0.0f;
}

// One round after the Jacobian is built; returns whether to run another.
static bool IkDlsRound(FIkDlsFrame* Frame, FIkEvent* Event)
{
    Frame->Block.J.Transpose(&Frame->Jt);
    SvdMulAB(&Frame->Jt, &Frame->Block.J, &Frame->JtJ);
    IkDlsDamp(&Frame->JtJ, Event->Damping);
    Frame->Pinv.RowCount = Frame->JtJ.ColCount;
    Frame->Pinv.ColCount = Frame->JtJ.RowCount;
    Frame->JtJ.PseudoInverse(&Frame->Pinv);

    const bool bPull = ((Event->AimFlags >> 7) & 1u) == 0;
    Frame->Pull.Count = Frame->Dof;
    if (bPull)
        IkDlsFillRestPull(Event, Frame->JointIters, Frame->Block.Rest9, Frame->Pull.Cell);

    float Step = 2.0f;
    float PrevErr = FLT_MAX;
    float ErrDelta = 0.0f;
    for (;;)
    {
        Step = (float)((double)Step * 0.5);
        const float* const End = G_IkCurPos[Frame->EndIndex];
        float Diff[3];
        Diff[0] = (float)((double)Frame->TargetPos[0] - (double)End[0]);
        Diff[1] = (float)((double)Frame->TargetPos[1] - (double)End[1]);
        Diff[2] = (float)((double)Frame->TargetPos[2] - (double)End[2]);
        const int Divisor = Frame->Counter >= 1 ? Frame->Counter : Frame->CounterFloor;
        const double Scale = 1.0 / (double)Divisor;
        Frame->Counter -= 1;
        Frame->Error.Count = Frame->TaskDim;
        const float ScaledX = (float)((double)Diff[0] * Scale);
        const float ScaledY = (float)((double)Diff[1] * Scale);
        const double ScaledZ = Scale * (double)Diff[2];
        if (Frame->bPos)
        {
            Frame->Error.Cell[0] = ScaledX;
            Frame->Error.Cell[1] = ScaledY;
            Frame->Error.Cell[2] = (float)ScaledZ;
        }
        if (Frame->bOri)
        {
            float CurQ[4];
            IkMathMatrixToQuat(CurQ, G_IkCurRot[Frame->EndIndex]);
            float Log3[3];
            IkQuatLog(Log3, CurQ, Frame->TargetQ);
            Frame->Error.Cell[Frame->PosDim] = Log3[0];
            Frame->Error.Cell[Frame->PosDim + 1] = Log3[1];
            Frame->Error.Cell[Frame->PosDim + 2] = Log3[2];
        }

        Frame->JtE.Count = Frame->Error.Count;
        SvdMulVec(&Frame->Jt, Frame->Error.Cell, Frame->JtE.Cell);
        if (bPull)
        {
            const float Gain = (float)((double)Event->Damping * 0.25);
            int Index = 0;
            while (Index < Frame->JtE.Count)
            {
                Frame->JtE.Cell[Index] = (float)(
                    (double)Gain * (double)(Frame->Pull.Cell[Index]) +
                    (double)(Frame->JtE.Cell[Index]));
                Index += 1;
            }
        }
        SvdMulVec(&Frame->Pinv, Frame->JtE.Cell, Frame->Delta.Cell);

        float OldEnd[3];
        IkCopy3(OldEnd, End);
        IkDlsStepJoints(Event, Frame->JointIters, Frame->Delta.Cell, Frame->Held3);
        if (Frame->bPos)
        {
            const float* const NewEnd = G_IkNewPos[Frame->EndIndex];
            Frame->Achieved.Cell[0] = (float)((double)NewEnd[0] - (double)OldEnd[0]);
            Frame->Achieved.Cell[1] = (float)((double)NewEnd[1] - (double)OldEnd[1]);
            Frame->Achieved.Cell[2] = (float)((double)NewEnd[2] - (double)OldEnd[2]);
        }
        if (Frame->bOri)
        {
            float NewQ[4];
            IkMathMatrixToQuat(NewQ, G_IkNewRot[Frame->EndIndex]);
            float Log3[3];
            IkQuatLog(Log3, NewQ, Frame->TargetQ);
            Frame->Achieved.Cell[Frame->PosDim] = Log3[0];
            Frame->Achieved.Cell[Frame->PosDim + 1] = Log3[1];
            Frame->Achieved.Cell[Frame->PosDim + 2] = Log3[2];
        }

        const double Sum = IkDlsResidual(&Frame->Error, &Frame->Achieved, Frame->TaskDim);
        ErrDelta = (float)(Sum - (double)PrevErr);
        PrevErr = (float)Sum;
        if (!(Sum > 0.01))
            break;
        if (!((double)ErrDelta < -1.0e-5))
            break;
    }

    Frame->Accum = (float)((double)Step * (double)Frame->Alpha +
                                      (double)Frame->Accum);
    const double Remain = 1.0 - (double)Frame->Accum;
    const float RemainF = (float)Remain;
    if (Remain < (double)Frame->Alpha)
        Frame->Alpha = RemainF;
    if (std::fabs((double)Frame->Accum - 1.0) <= 0.01)
        return false;
    return !(ErrDelta >= 0.0f);
}

// FUN_065fc000 (thiscall on the event): LAB_065fb540 calls it for a chain of two or more joints
// when StateFlags bit 1 is clear.
void FIkEvent::SolveDls()
{
    FIkDlsFrame Frame;
    const unsigned long Flags = AimFlags;
    Frame.bPos = ((Flags >> 5) & 1u) != 0;
    Frame.bOri = ((Flags >> 6) & 1u) != 0;
    const int Count = JointCount;
    Frame.Dof = (Frame.bOri ? Count : Count - 1) * 3;
    Frame.PosDim = Frame.bPos ? 3 : 0;
    Frame.TaskDim = Frame.PosDim + (Frame.bOri ? 3 : 0);
    if (Frame.bOri)
        Frame.Dof += LockCount * -3;
    else if (Frame.bPos)
    {
        int Locked = LockCount;
        if (Locked > 0 && LockBytes[Count - 1] != 0)
            Locked -= 1;
        Frame.Dof += Locked * -3;
    }

    IkDlsReadStart(this);
    int Joint = 0;
    while (Joint < JointCount)
    {
        G_Engine->GetJointParams(IkJointInstance(JointIds[Joint]), 0, Quats + Joint * 4);
        G_Engine->GetInstanceRelative(IkJointInstance(JointIds[Joint]));
        IkCopy3(G_IkCurPos[Joint + 1],
                G_Engine->GetInstancePosition(IkJointInstance(JointIds[Joint])));
        IkCopy9(G_IkCurRot[Joint + 1],
                G_Engine->GetInstanceOrientation(IkJointInstance(JointIds[Joint])));
        Joint += 1;
    }

    Frame.EndIndex = JointCount;
    if (Frame.bPos)
        IkCopy3(Frame.TargetPos, (const float*)TargetA);
    if (Frame.bOri)
        IkCopy9(Frame.TargetMat, (const float*)TargetB);
    bool bNeed = false;
    if (Frame.bPos)
    {
        const float* const End = G_IkCurPos[Frame.EndIndex];
        const double Dx = (double)Frame.TargetPos[0] - (double)End[0];
        const double Dy = (double)Frame.TargetPos[1] - (double)End[1];
        const double Dz = (double)Frame.TargetPos[2] - (double)End[2];
        bNeed = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) > 0.001;
    }
    IkMathMatrixToQuat(Frame.TargetQ, Frame.TargetMat);
    if (!bNeed)
    {
        if (!Frame.bOri)
            return;
        if (!IkDlsOriOff(&Frame))
            return;
    }

    IkDlsSetup(&Frame);
    Frame.JointIters = Frame.bOri ? JointCount : JointCount - 1;
    do
    {
        float* const Cells = IkDlsCells(&Frame.Block);
        int Limit = JointCount;
        if (Frame.bOri)
            Limit += 1;
        int Col = 0;
        int Link = 1;
        while (Link < Limit)
        {
            if (Frame.bPos)
            {
                float Arm[3];
                if (Link == JointCount)
                {
                    Arm[0] = 0.0f;
                    Arm[1] = 0.0f;
                    Arm[2] = 0.0f;
                }
                else
                {
                    const float* Bone = (const float*)(
                        G_Engine->GetInstanceRelative(IkJointInstance(JointIds[Link])));
                    IkMathTransform(Arm, G_IkCurRot[Link], Bone + 1);
                }
                if (LockBytes[Link - 1] != 0)
                    IkDlsAddSkew(Cells, Col, Arm[0], Arm[1], Arm[2]);
                else
                    IkDlsWriteSkew(Cells, Col, Arm[0], Arm[1], Arm[2]);
            }
            if (Frame.bOri && LockBytes[Link - 1] == 0)
                IkDlsWriteIdentity(Cells, Col + 12 * Frame.PosDim);
            if (LockBytes[Link - 1] == 0)
                Col += 3;
            Link += 1;
        }
    } while (IkDlsRound(&Frame, this));
    IkDlsWriteQuats(this, Frame.JointIters);
}

// FUN_065fce60 (thiscall on the event): LAB_065fb540 calls it for a chain of two or more joints
// when StateFlags bit 1 is set.
void FIkEvent::SolveDlsAlt()
{
    FIkDlsFrame Frame;
    const unsigned long Flags = AimFlags;
    Frame.bPos = ((Flags >> 5) & 1u) != 0;
    Frame.bOri = ((Flags >> 6) & 1u) != 0;
    Frame.Dof = JointCount * 3;
    Frame.PosDim = Frame.bPos ? 3 : 0;
    Frame.TaskDim = Frame.PosDim + (Frame.bOri ? 3 : 0);
    if (Frame.bOri)
        Frame.Dof += LockCount * -3;
    else if (Frame.bPos)
        Frame.Dof += LockCount * -3;

    IkDlsReadStart(this);
    int Joint = 0;
    while (Joint < JointCount)
    {
        G_Engine->GetJointParams(IkJointInstance(JointIds[Joint]), 0, Quats + Joint * 4);
        IkCopy3(G_IkCurPos[Joint + 1],
                G_Engine->GetInstancePosition(IkJointInstance(JointIds[Joint])));
        IkCopy9(G_IkCurRot[Joint + 1],
                G_Engine->GetInstanceOrientation(IkJointInstance(JointIds[Joint])));
        Joint += 1;
    }

    Frame.EndIndex = JointCount;
    if (Frame.bPos)
        IkCopy3(Frame.TargetPos, (const float*)TargetA);
    if (Frame.bOri)
        IkCopy9(Frame.TargetMat, (const float*)TargetB);
    bool bNeed = false;
    if (Frame.bPos)
    {
        const float* const End = G_IkCurPos[Frame.EndIndex];
        const float Fx = (float)((double)Frame.TargetPos[0] - (double)End[0]);
        const float Fy = (float)((double)Frame.TargetPos[1] - (double)End[1]);
        const float Fz = (float)((double)Frame.TargetPos[2] - (double)End[2]);
        const double X = (double)Fx;
        const double Y = (double)Fy;
        const double Z = (double)Fz;
        bNeed = std::sqrt((Y * Y + Z * Z) + X * X) > 0.001;
    }
    IkMathMatrixToQuat(Frame.TargetQ, Frame.TargetMat);
    if (!bNeed)
    {
        if (!Frame.bOri)
            return;
        if (!IkDlsOriOff(&Frame))
            return;
    }

    IkDlsSetup(&Frame);
    Frame.JointIters = JointCount;
    do
    {
        float* const Cells = IkDlsCells(&Frame.Block);
        const int Limit = JointCount + 1;
        int Col = 0;
        int Link = 1;
        while (Link < Limit)
        {
            if (Frame.bPos)
            {
                const float* Bone = (const float*)(
                    G_Engine->GetInstanceRelative(IkJointInstance(JointIds[Link])));
                float Arm[3];
                IkMathTransform(Arm, G_IkCurRot[Link], Bone + 1);
                if (LockBytes[Link - 1] != 0)
                    IkDlsAddSkew(Cells, Col, Arm[0], Arm[1], Arm[2]);
                else
                    IkDlsWriteSkew(Cells, Col, Arm[0], Arm[1], Arm[2]);
            }
            if (Frame.bOri && LockBytes[Link - 1] == 0)
                IkDlsWriteIdentity(Cells, Col + 12 * Frame.PosDim);
            if (LockBytes[Link - 1] == 0)
                Col += 3;
            Link += 1;
        }
    } while (IkDlsRound(&Frame, this));
    IkDlsWriteQuats(this, Frame.JointIters);
}

// The single-joint body LAB_065fb540 inlines; Bone is the IEngine +0xC4 result.
static void IkSolveSingleJoint(FIkEvent* Event, FIkApplyGate* Gate, const float* Bone)
{
    void* const Joint = (void*)((ptrdiff_t)Gate->Joint);
    const float* ParentRot = G_Engine->GetInstanceOrientation(G_Engine->GetInstanceParent(Joint));
    float Local[9];
    IkCopy9(Local, ParentRot);
    float Trans[9];
    if ((Event->AimFlags & 0x40) != 0)
    {
        Trans[0] = Local[0];
        Trans[1] = Local[3];
        Trans[2] = Local[6];
        Trans[3] = Local[1];
        Trans[4] = Local[4];
        Trans[5] = Local[7];
        Trans[6] = Local[2];
        Trans[7] = Local[5];
        Trans[8] = Local[8];
        AcquireMathEngine()->MulMatrix(*(FMatrix*)Local,
                                       *(const FMatrix*)Trans,
                                       *(const FMatrix*)Event->TargetB);
    }
    else
    {
        const float* Pos = G_Engine->GetInstancePosition(Joint);
        float P[3];
        IkCopy3(P, Pos);
        const float* Target = (const float*)Event->TargetA;
        float Diff[3];
        const float* Src = Target;
        if ((Event->AimFlags & 1) != 0)
        {
            Diff[0] = (float)((double)Target[0] - (double)P[0]);
            Diff[1] = (float)((double)Target[1] - (double)P[1]);
            Diff[2] = (float)((double)Target[2] - (double)P[2]);
            Src = Diff;
        }
        float Dir[3];
        IkCopy3(Dir, Src);
        const double X = (double)Dir[0];
        const double Y = (double)Dir[1];
        const double Z = (double)Dir[2];
        const double Inv = 1.0 / std::sqrt(Y * Y + Z * Z + X * X);
        Dir[0] = (float)(X * Inv);
        Dir[1] = (float)(Y * Inv);
        Dir[2] = (float)(Z * Inv);
        float Look[9];
        IkLookAt(Look, Dir, Local, (unsigned char)Event->AimFlags);
        Trans[0] = Local[0];
        Trans[1] = Local[3];
        Trans[2] = Local[6];
        Trans[3] = Local[1];
        Trans[4] = Local[4];
        Trans[5] = Local[7];
        Trans[6] = Local[2];
        Trans[7] = Local[5];
        Trans[8] = Local[8];
        AcquireMathEngine()->MulMatrix(*(FMatrix*)Local,
                                       *(const FMatrix*)Trans,
                                       *(const FMatrix*)Look);
    }
    float Result[9];
    IkCopy9(Result, Local);
    if ((Event->AimFlags & 0x80) == 0)
    {
        float Clamp[9];
        const float* Clamped = IkEulerClamp(Clamp, Bone, Result);
        IkCopy9(Result, Clamped);
    }
    float Q[4];
    AcquireMathEngine()->MatrixToQuaternion(*(FQuaternion*)Q,
                                            *(const FMatrix*)Result); // +0x50
    IkCopy4(Event->Quats, Q);
}

// LAB_065fb540, the IK event's only table slot (06601340): EngBase's FUN_0661b420 calls it on
// the channel owner with (sampled value, channel key, bind descriptor, time).
// LAB_065fb540 (IK event vtable slot 0: OutQuat, MatchId, Gate, Time).
int DACOM_API FIkEvent::Apply(float* OutQuat, unsigned long ChannelKey, void* BindDesc, float Time)
{
    int MatchId = (int)ChannelKey;
    FIkApplyGate* Gate = (FIkApplyGate*)BindDesc;
    (void)Time;
    if ((StateFlags & 1) != 0)
    {
        for (int Index = 0; Index < JointCount; ++Index)
        {
            if ((int)Channels[Index] == MatchId)
            {
                G_Engine->GetJointParams(
                    (void*)((ptrdiff_t)JointIds[Index]), 0,
                    OutQuat);
                return 1;
            }
        }
        return 1;
    }

    const int OldTick = ApplyTick;
    ApplyTick = OldTick + 1;
    if (OldTick == 0 && Gate->SkipFlag == 0)
    {
        const float* Bone = (const float*)(G_Engine->GetInstanceRelative(
            (void*)((ptrdiff_t)Gate->Joint)));
        if (JointCount == 1)
            IkSolveSingleJoint(this, Gate, Bone);
        else if ((StateFlags & 2) != 0)
            SolveDlsAlt();
        else
            SolveDls();
    }

    if (ApplyTick == JointCount)
        ApplyTick = 0;

    for (int Index = 0; Index < JointCount; ++Index)
    {
        if ((int)Channels[Index] == MatchId)
        {
            IkCopy4(OutQuat, Quats + Index * 4);
            return 0;
        }
    }
    return 0;
}

// The StartAim descriptor FUN_065f5400 reads (+0x00 is not read).
struct FAimDesc
{
    unsigned long Unread00;  // +0x00
    int StartJoint;          // +0x04 the chain root; the walk stops there
    int EndJoint;            // +0x08 the end joint; the walk starts there
    unsigned long Flags;     // +0x0C FIkEvent::AimFlags
    void* TargetA;           // +0x10 point or direction
    void* TargetB;           // +0x14 orientation target
    float Damping;           // +0x18
    int LockCount;           // +0x1C
    int LockIds[1];          // +0x20 LockCount joint ids
};

static_assert(offsetof(FAimDesc, LockIds) == 0x20, "aim desc lock ids +20");

// FUN_065f5400 (thiscall on the instance: Desc, PlayExtra).
FIkListNode* FDeformInstance::AllocAimEvent(const unsigned char* Desc, void* PlayExtra)
{
    const FAimDesc* const Aim = (const FAimDesc*)Desc;
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
                            G_Engine->GetInstanceParent((void*)((ptrdiff_t)WalkBack))));
                    }
                    FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x44D,
                          G_StartAimBlocked);
                    return (FIkListNode*)((ptrdiff_t)-1);
                }
                G_Animation->RegisterHandle((unsigned long)Cursor);
            }
            Chain.push_back(Cursor);
            void* const CursorInstance = (void*)((ptrdiff_t)Cursor);
            Parent = (int)((ptrdiff_t)(G_Engine->GetInstanceParent(CursorInstance)));
            const unsigned long* Relative = G_Engine->GetInstanceRelative(CursorInstance);
            if (Relative[0] == 4)
            {
                const float* Axis = (const float*)(Relative + 4);
                bSphereAxis = 1;
                const double X = (double)Axis[0];
                const double Y = (double)Axis[1];
                const double Z = (double)Axis[2];
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
    Node->Event = (FIkEvent*)((ptrdiff_t)-1);
    LinkListNode(this, Node);
    Node->Active = 1;

    // operator new 0x44 (not cleared), then the inlined constructor (vtable 06601340).
    FIkEvent* Event = new FIkEvent;
    Event->TargetB = Aim->TargetB;
    Event->StateFlags = (unsigned char)(Event->StateFlags & 0xFE);
    Event->TargetA = Aim->TargetA;
    Event->ApplyTick = 0;
    Node->Event = Event;
    Event->Owner = Node;

    const size_t Count4 = (size_t)Count * 4;
    Event->JointIds = (int*)new unsigned char[Count4];
    if (Count > 0)
        std::memcpy(Event->JointIds, &JointIds[0], Count4);
    Event->Channels = (void**)new unsigned char[Count4];
    Event->Quats = (float*)new unsigned char[(size_t)Count * 0x10];
    Event->AimFlags = Aim->Flags;
    Event->StartJoint = Aim->StartJoint;
    Event->EndJoint = Aim->EndJoint;
    Event->BindMats = (float*)new unsigned char[(size_t)Count * 0x24];
    Event->LockBytes = new unsigned char[(size_t)Count];
    std::memset(Event->LockBytes, 0, (size_t)Count);
    Event->Damping = Aim->Damping;
    Event->LockCount = Aim->LockCount;

    if (!SetIkJointCount(Event, (unsigned int)Count))
    {
        StopAimNode(Node);
        return (FIkListNode*)((ptrdiff_t)-1);
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
        void* const JointInstance = (void*)((ptrdiff_t)JointId);
        CreateCookie[3] = (unsigned long)JointId;
        const unsigned long Key = G_Channel->CreateTyped(
            (IChannelOwner*)Event, 4, CreateCookie, (void*)((ptrdiff_t)BindB));
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
        IkEulerRebuild(Event->BindMats + Index * 9, Yaw, Pitch, Roll);

        // Retail's target is [E+0x78] with the matrix at [E+0x88]: a loose joint (seven
        // floats from EngBase's GetJointParams) runs into the matrix slot before
        // QuaternionToMatrix rewrites it, so the buffer here holds seven.
        float JointQuat[7];
        G_Engine->GetJointParams(JointInstance, 0, JointQuat);
        float LiveMat[9];
        IkMathQuatToMatrix(LiveMat, JointQuat);
        std::memcpy(Event->BindMats + Index * 9, LiveMat, sizeof(LiveMat));

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

// LAB_065f1450 (cdecl): forwards to the thiscall FUN_065f14f0 with the holder as this.
static bool __cdecl BoneLinkCallbackThunk(void* BoneInst, int Parent, FLodHolder* Holder)
{
    return Holder->LinkBoneByParent(BoneInst, Parent);
}

// DAT_06603150 / DAT_06603154, set by FUN_065f1d90.
static bool (__cdecl* G_BoneLinkFn)(void*, int, FLodHolder*) = nullptr;
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

// FUN_065f67f0 (thiscall on the part: Holder, Meshes, Count). Returns 1.
unsigned char FDeformPart::BindMeshes(FLodHolder* Holder, FDeformMesh** InMeshes, int Count)
{
    if (Lod != Holder)
    {
        if (Lod != nullptr)
        {
            ReleaseLodHolder(Lod);
            Lod = nullptr;
        }
        Lod = Holder;
        if (Holder != nullptr)
            AddRefLodHolder(Holder);
    }

    MeshCount = Count;
    int* Block = (int*)new unsigned char[(size_t)Count * 4 + 4];
    *Block = Count;
    FDeformMesh** Array = (FDeformMesh**)(Block + 1);
    int Index = 0;
    while (Index < Count)
    {
        Array[Index] = nullptr;
        Index += 1;
    }
    Meshes = Array;

    int MaxVertexCapacity = 0;
    int MaxIndexCount = 0;
    Index = 0;
    while (Index < MeshCount)
    {
        Meshes[Index] = InMeshes[Index];
        const int VertexCapacity = Meshes[Index]->GetVertexCapacity();
        if (VertexCapacity > MaxVertexCapacity)
            MaxVertexCapacity = VertexCapacity;
        const int IndexCount = Meshes[Index]->GetIndexCount();
        if (IndexCount > MaxIndexCount)
            MaxIndexCount = IndexCount;
        Index += 1;
    }
    if (MaxVertexCapacity > 0)
        Vb.Capacity = MaxVertexCapacity;
    if (MaxIndexCount > 0)
        Ib.Capacity = MaxIndexCount;
    return 1;
}

// FUN_065fdfb0. The FUN_065fdfd0 result is dropped; no test.
static void BindEyeMaterials(IMaterialLibrary* MaterialLib)
{
    RegisterEyeMaterialFactory();
    MaterialLib->AddMapRule(0, G_EyeMaterialPattern,
                                                       G_EyeMaterialName);
}

// FUN_065f1df0: only these four, in retail order.
static void ReleaseRenderCaches()
{
    ReleaseInterface(G_Pipeline);
    ReleaseInterface(G_MaterialLibrary);
    ReleaseInterface(G_IndexBuffer);
    ReleaseInterface(G_VertexBuffer);
}

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

// FUN_065f18d0. Anim/Engine/Channel/Renderer are non-owning after QI+Release.
static void ReleaseComponentCaches()
{
    ReleaseRenderCaches();
    ReleaseInterface(G_System);
}

// Reused parser buffer; a failed read leaves its contents unchanged.
static char G_DeformRateValue[0x400];

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

// FUN_065f6720. No test.
void FDeformPart::Construct()
{
    std::memset(this, 0, sizeof(FDeformPart));
    Flags = 1;
}

static GENRESULT DACOM_API StubRet8(void* /*This*/, void* /*A*/);

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
void DACOM_API FDeformInstance::OnLoop(unsigned long ChannelKey, float* Transform, void* Context)
{
    float* const Xform = Transform;
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
    Xform[0x28 / 4] = Height;
    const double Half =
        std::atan2((double)Xform[0x8 / 4], (double)Xform[0x20 / 4]) *
        0.5;
    const float SinScaled = (float)(std::sin(Half) / std::sqrt(1.0));
    float Quat[4];
    Quat[0] = (float)(std::cos(Half));
    const float Side = SinScaled * 0.0f;
    WriteVec3(&Quat[1], Side, SinScaled, Side);
    float Matrix[9];
    I3DMathEngine* Math = AcquireMathEngine();
    Math->QuaternionToMatrix(*(FMatrix*)Matrix,
                             *(const FQuaternion*)Quat); // +0x54
    std::memcpy(Xform, Matrix, sizeof(Matrix));
    if (ForwardSink == nullptr)
        return;
    ForwardSink->OnLoop(ChannelKey, Xform, Context);
}

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

// Context for the LAB_065f59b0 thunk.
struct FHardpointWalkCtx
{
    void* UserFn;
    void* UserData;
    void* BoneInst;
};

// LAB_065f59b0 (cdecl): calls UserFn(bone instance, name, user data). The first argument is the
// hardpoint's NAME, not a blob (FHardpointEnumProc).
static void __cdecl HardpointWalkThunk(char const* Name, void* Context)
{
    FHardpointWalkCtx* Ctx = (FHardpointWalkCtx*)Context;
    using FnUser = void(__cdecl*)(void*, char const*, void*);
    ((FnUser)Ctx->UserFn)(Ctx->BoneInst, Name, Ctx->UserData);
}

// Pair bone instances by hardpoint-name CRC for connection.
struct FHardpointInstancePair
{
    void* First;  // node +0x10
    void* Second; // node +0x14
};

struct FHardpointConnectContext
{
    std::map<unsigned long, FHardpointInstancePair> Pairs;
    IHardpoint* Hardpoint;
};

// LAB_065f3540 (cdecl: BoneInst, Name, Context).
static void __cdecl HardpointConnectCallback(void* BoneInst, char const* Name, void* Context)
{
    FHardpointConnectContext* Ctx = (FHardpointConnectContext*)Context;
    FHardpointInstancePair Unset;
    Unset.First = G_InvalidEngineHandle;
    Unset.Second = G_InvalidEngineHandle;
    std::pair<std::map<unsigned long, FHardpointInstancePair>::iterator, bool> Inserted =
        Ctx->Pairs.insert(std::make_pair(DACOM_CRC::GetCRC32(Name), Unset));
    FHardpointInstancePair* Pair = &Inserted.first->second;
    if (Pair->First == G_InvalidEngineHandle)
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
    if (Ctx->Hardpoint->ConnectHardpoints(Pair->First, &KeyA, Pair->Second, &KeyB) != 0)
    {
        char Message[0x2000];
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s", G_FdumpFileDeformableObject, 0x53,
              FormatDeformMessage(Message, "Deformable: create: cannot connect parts %s<->%s",
                                  Name, Name));
    }
}

// FUN_065f59d0 (thiscall on the instance: UserFn, UserData).
unsigned char FDeformInstance::WalkHardpoints(void* UserFn, void* UserData)
{
    IHardpoint* Hardpoint = nullptr;
    if (G_Engine->QueryInterface(Iids::Hardpoint, (void**)(&Hardpoint)) < 0)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x4EF,
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
                (void*)((ptrdiff_t)(Lod->Skeleton->Bones[Bone].Parent)),
                &Ctx);
            Bone += 1;
        }
        PartIndex += 1;
    }
    if (Hardpoint != nullptr)
        Hardpoint->Release();
    return 1;
}

// FUN_065f36c0 (thiscall on the instance: Parts, Arg3, Arg4). Returns 1.
bool FDeformInstance::ConnectParts(int* Parts, void* Arg3, void* Arg4)
{
    ForwardSink = (IChannelSink*)Arg3;
    DefaultScript = Arg4;
    PartCount = Parts[0];
    int* Block = (int*)new unsigned char[(size_t)PartCount * sizeof(FDeformPart) + 4];
    *Block = PartCount;
    FDeformPart* Array = (FDeformPart*)(Block + 1);
    int Index = 0;
    while (Index < PartCount)
    {
        Array[Index].Construct();
        Index += 1;
    }
    PartArray = Array;

    FDeformPartDesc* Descs = *(FDeformPartDesc**)(Parts + 1);
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
            Created = Factory->CreateInstance((AGGDESC*)(&RootDesc),
                                              (void**)(&RootFs));
        else
            Created = DACOM_Acquire()->CreateInstance((AGGDESC*)(&RootDesc),
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
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0xB0,
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
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0xB8,
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
                    FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject,
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
                                if (RootFs->CreateInstance((AGGDESC*)(&MeshDesc),
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
                        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                              G_FdumpFileDeformableObject, 0x11F, "warning: couldnt create part.");
                    }
                }
            }
        }
        // The skeleton reference taken by the ctor, then RootFs.
        ReleaseSkeleton(Skel);
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
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformableObject, 0x2B,
              "Deform: Unable to acquire IID_IHardpoint, unable to continue");
    }
    WalkHardpoints((void*)(&HardpointConnectCallback), &Context);

    PartIndex = 0;
    while (PartIndex < PartCount)
    {
        FLodHolder* Lod = PartArray[PartIndex].Lod;
        int Bone = 1;
        while (Bone < Lod->BindCursor)
        {
            FBoneRuntime* Slot = &Lod->BoneSlots[Bone];
            if (G_Engine->GetInstanceParent(Slot->Instance) == G_InvalidEngineHandle)
            {
                G_Engine->DestroyInstance(Slot->Instance);
                Slot->Instance = G_InvalidEngineHandle;
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
            Node->Event = (FIkEvent*)((ptrdiff_t)-1);
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
        void* Root = PartArray[0].Lod->BoneSlots[0].Instance;
        if (Root != G_InvalidEngineHandle)
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

unsigned long DACOM_API FDeformableObject::AddRef()
{
    return Controlling->AddRef();
}

unsigned long DACOM_API FDeformableObject::Release()
{
    return Controlling->Release();
}

// Shared fail stubs returning -1; the suffix is the retail RET size.
static GENRESULT DACOM_API StubRet8(void* /*This*/, void* /*A*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRetC(void* /*This*/, void* /*A*/, void* /*B*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet10(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet14(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/,
                                     void* /*D*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet20(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/,
                                     void* /*D*/, void* /*E*/, void* /*F*/, void* /*G*/)
{
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRet28(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/,
                                     void* /*D*/, void* /*E*/, void* /*F*/, void* /*G*/, void* /*H*/,
                                     void* /*I*/)
{
    return GR_GENERIC;
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

static GENRESULT DACOM_API DeformableDestroy(FDeformable* /*This*/, FDeformInstance* Instance)
{
    // FUN_065f1ed0. FUN_065f3ed0, then the dtor FUN_065f3640 runs it again (a no-op on the
    // emptied instance).
    if (Instance == nullptr)
        return GR_GENERIC;
    Instance->Teardown();
    Instance->Teardown();
    delete Instance;
    return GR_OK;
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
    float Center[3];
    Center[0] = 0.0f;
    Center[1] = 0.0f;
    Center[2] = 0.0f;
    int Index = 0;
    do
    {
        FDeformPart* const Part = &PartArray[Index];
        Part->BoundSphere(&Radius, Center);
        const float* const View = Camera->GetViewTransform();
        float ViewCenter[3];
        AcquireMathEngine()->TransformXform(*(FVector*)ViewCenter,
                                            *(const FTransform*)View,
                                            *(const FVector*)Center); // +0x28
        float Probe[3];
        Probe[0] = ViewCenter[0];
        Probe[1] = ViewCenter[1];
        Probe[2] = ViewCenter[2];
        const int Rank = Camera->ClassifySphere(Probe, Radius);
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
    void* const Bone = (void*)((ptrdiff_t)(FirstBoneInstance(this)));
    // Submit the root instance and merge its renderer rank into the LOD result.
    const int Rank = G_Renderer->RenderInstance(
        Camera, Bone, 1.0f, Flags | 0x108ul, nullptr, RenderArg);
    if (*OutLod != Rank)
    {
        if (*OutLod < Rank)
            *OutLod = Rank;
        if (*OutLod >= 3)
            *OutLod = 3;
    }
}

// FUN_065f68c0 (thiscall on the part: OutRadius, OutCenter): the first mesh's slot 4.
unsigned char FDeformPart::BoundSphere(float* OutRadius, float* OutCenter)
{
    const unsigned char bOk = Meshes[0]->GetBoundSphere(OutRadius, OutCenter);
    if (bOk != 0)
    {
        I3DMathEngine* Math = AcquireMathEngine();
        float* BoneAtC = Lod->BoneSlots[0].LocalTransform;
        float Transformed[3];
        Math->TransformXform(*(FVector*)Transformed,
                             *(const FTransform*)BoneAtC,
                             *(const FVector*)OutCenter); // +0x28
        OutCenter[0] = Transformed[0];
        OutCenter[1] = Transformed[1];
        OutCenter[2] = Transformed[2];
    }
    return bOk;
}

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

static GENRESULT DACOM_API EngineUpdateOk(void* /*This*/, void* /*A*/, void* /*B*/)
{
    return GR_OK;
}

static GENRESULT DACOM_API EngineStub10(void* /*This*/, void* /*A*/, void* /*B*/, void* /*C*/)
{
    return GR_GENERIC;
}

// LAB_065f1dc0: calls DAT_06603150 (cdecl) with DAT_06603154 as the third argument.
static GENRESULT DACOM_API EngineCallbackTrampoline(void* /*This*/, void* Arg1, void* Arg2)
{
    if (G_BoneLinkFn == nullptr)
        return GR_OK;
    return (GENRESULT)(
        G_BoneLinkFn(Arg1, (int)((ptrdiff_t)Arg2), G_BoneLinkHolder));
}


// FUN_065f1810; vtables 06601110 / 066010dc (066011ac is IEngineComponent's own).
FDeformable::FDeformable()
{
    ZeroModuleInterfaceCaches();
}

// FUN_065f18d0. Called by FUN_065f22f0's failure path and the inner Release, always right
// before operator delete.
FDeformable::~FDeformable()
{
    ReleaseComponentCaches();
}

GENRESULT DACOM_API FDeformable::Create(void** OutInstance, int* Parts, void* Arg3, void* Arg4,
                                               void* Arg5)
{
    return DeformableCreate(this, OutInstance, Parts, Arg3, Arg4, Arg5);
}

GENRESULT DACOM_API FDeformable::CreateUnused(void* A, void* B, void* C, void* D, void* E,
                                                      void* F, void* G)
{
    return StubRet20(this, A, B, C, D, E, F, G);
}

GENRESULT DACOM_API FDeformable::Destroy(void* Instance)
{
    return DeformableDestroy(this, (FDeformInstance*)Instance);
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

GENRESULT DACOM_API FDeformable::Unused13(void* A)
{
    return StubRet8(this, A);
}

GENRESULT DACOM_API FDeformable::Unused14(void* A)
{
    return StubRet8(this, A);
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

GENRESULT DACOM_API FDeformable::Unused17(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
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

GENRESULT DACOM_API FDeformable::Unused25(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
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

GENRESULT DACOM_API FDeformable::Unused28(void* A, void* B)
{
    return StubRetC(this, A, B);
}

GENRESULT DACOM_API FDeformable::Unused29(void* A, void* B, void* C, void* D, void* E,
                                                 void* F, void* G, void* H, void* I)
{
    return StubRet28(this, A, B, C, D, E, F, G, H, I);
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

GENRESULT DACOM_API FDeformable::Unused33(void* A, void* B, void* C)
{
    return StubRet10(this, A, B, C);
}

GENRESULT DACOM_API FDeformable::Unused34(void* A, void* B, void* C, void* D)
{
    return StubRet14(this, A, B, C, D);
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

int DACOM_API FDeformable::Initialize()
{
    return (int)(EngineInitialize(this));
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
    if (!IidEquals(Iid, Iids::ComponentFactory))
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

struct FDeformableComponentFactory : FDeformableFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};

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
    const float* const View = Camera->GetViewTransform();
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
                float Composed[12];
                AcquireMathEngine()->MulTransform(*(FTransform*)Composed,
                                                  *(const FTransform*)View,
                                                  *(const FTransform*)
                                                      Bone->Matrix); // +0x1C
                float Copy[12];
                VmeshCopy12(Copy, Composed);
                G_Pipeline->SetWorldN(WorldIndex, Copy);
            }
            else
                G_Pipeline->SetWorldN(WorldIndex, Bone->Matrix);
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
