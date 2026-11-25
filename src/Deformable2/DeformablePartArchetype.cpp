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

static void ExpandTristrip(const unsigned short* Strip, int StripCount, unsigned short* Out,
                           int* OutCount);

static void RebaseFaceIndices(FFaceGroup* Group);
static void HoldFileSystem(IFileSystem** Slot, IFileSystem* Fs);
static void ReleaseFaceGroupMaterialSlot(IMaterial** MaterialSlot);
static void DropFileSystem(IFileSystem** Slot);
static void BoneListPush(FBoneVertexList* List, unsigned short VertexIndex);
static void DestroyVertexRemapList(FVertexRemapList* List);

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

static const char* const G_FdumpFileDeformablePartArchetype =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\DeformablePartArchetype.cpp";

// Return to the parent UTF node after loading a child.
const char* const G_ParentNode = "..";

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
                FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformablePartArchetype,
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
                        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
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

static const unsigned long G_ParseTagUtf = 0x112;

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

// FUN_065f8b00, on the calloc(1, 0x84) block of FUN_065f26f0.
FMeshArchetype::FMeshArchetype(const char* Name)
    : FDeformMesh(Name)
{
    std::memset(&ParseTag, 0, sizeof(FMeshArchetype) - offsetof(FMeshArchetype, ParseTag));
    RemapAllocator = (unsigned char)((uintptr_t)Name);
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

// FUN_065f9150.
static void HoldFileSystem(IFileSystem** Slot, IFileSystem* Fs)
{
    *Slot = Fs;
    if (Fs != nullptr)
        Fs->AddRef();
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

// FUN_065f9170.
static void DropFileSystem(IFileSystem** Slot)
{
    if (*Slot != nullptr)
    {
        (*Slot)->Release();
        *Slot = nullptr;
    }
}

// FUN_065f9430. Retail does not test the list pointer for null.
void FBoneVertexList::Destroy()
{
    delete[] (unsigned char*)Start;
    Start = nullptr;
    Finish = nullptr;
    CapacityEnd = nullptr;
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

// FUN_065f9ba0 with flags 0 (bit 0 would also delete the list). Retail does not test the list
// pointer for null.
static void DestroyVertexRemapList(FVertexRemapList* List)
{
    delete[] (unsigned char*)List->Start;
    List->Start = nullptr;
    List->Finish = nullptr;
    List->CapacityEnd = nullptr;
}
