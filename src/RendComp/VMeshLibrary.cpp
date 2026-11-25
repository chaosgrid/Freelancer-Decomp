// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RendComp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <new>

// VMeshData.cpp / VMeshLibrary.cpp of retail rendcomp.dll.

// Mesh data indexed by CRC.
static std::map<unsigned long, FVmeshData*> G_VmeshDataMap;

// FUN_06c2e780: map[data->Crc] = data (an existing node keeps its key and gets the new value).
static bool VMeshDataMapInsert(FVmeshData* Data)
{
    if (Data == nullptr)
        return false;
    G_VmeshDataMap[Data->Crc] = Data;
    return true;
}

// FUN_06c2e8b0: erase every node keyed by data->Crc.
static void VMeshDataMapErase(FVmeshData* Data)
{
    if (Data == nullptr)
        return;
    G_VmeshDataMap.erase(Data->Crc);
}

FVmeshData* VMeshDataFind(const FVmeshKey* Key)
{
    std::map<unsigned long, FVmeshData*>::iterator It = G_VmeshDataMap.find(Key->Crc);
    if (It == G_VmeshDataMap.end())
        return nullptr;
    return It->second;
}

// Channel sizes and FVF masks.
struct FVmeshFvfChannel
{
    unsigned int ChannelId;
    unsigned int FvfMask;
    unsigned int ElemBytes;
};

static const FVmeshFvfChannel G_FvfChannelTable[] = {
    {1, 0xE, 0xC},
    {0x800, 0xC, 0x30},
    {8, 0x10, 0xC},
    {2, 0x100, 8},
    {4, 0x200, 8},
    {0x10, 0x40, 4},
};

static const unsigned int G_FvfChannelCount =
    (unsigned int)(sizeof(G_FvfChannelTable) / sizeof(G_FvfChannelTable[0]));

bool VMeshChannelPresent(unsigned long Fvf, unsigned int ChannelId)
{
    bool bPresent = false;
    for (unsigned int Index = 0; Index < G_FvfChannelCount; ++Index)
    {
        const FVmeshFvfChannel* Entry = &G_FvfChannelTable[Index];
        if (ChannelId != Entry->ChannelId)
            continue;
        if ((Entry->FvfMask & 0xF00) == 0)
            bPresent = (Fvf & Entry->FvfMask) != 0;
        else if ((Fvf & 0xF00) == 0)
            bPresent = false;
        else
            bPresent = ((Fvf & 0xF00) >> 8) >= (ChannelId - 2);
        if (bPresent)
            return true;
    }
    return bPresent;
}

// FUN_06c31280 / the offset loop of FUN_06c2e020: absent channel -> 0; otherwise the element
// sizes of every present channel that precedes it in the table.
int VMeshChannelByteOffset(unsigned long Fvf, unsigned int ChannelId)
{
    int Offset = 0;
    if (!VMeshChannelPresent(Fvf, ChannelId))
        return Offset;
    for (unsigned int Index = 0; Index < G_FvfChannelCount; ++Index)
    {
        const FVmeshFvfChannel* Entry = &G_FvfChannelTable[Index];
        if (ChannelId == Entry->ChannelId)
            break;
        if (VMeshChannelPresent(Fvf, Entry->ChannelId))
            Offset += (int)Entry->ElemBytes;
    }
    return Offset;
}

// FUN_06c2e290 (texture part) plus the position/normal/colour terms inlined in FUN_06c2e020 /
// FUN_06c31500. Texture formats (jump table 06c2e2e0): 0 -> 8, 1 -> 0xC, 2 -> 0x10, 3 -> 4.
unsigned int VMeshFvfStride(unsigned long Fvf)
{
    unsigned int Position = 0;
    if ((Fvf & 0xC) == 0xC)
        Position = 0x1C;
    else if ((Fvf & 4) == 4)
        Position = 0x10;
    else if ((Fvf & 2) == 2)
        Position = 0xC;

    unsigned int Normal = 0;
    if ((Fvf & 0x10) != 0)
        Normal = 0xC;

    unsigned int Colours = 0;
    if ((Fvf & 0x40) != 0)
        Colours = 4;
    if ((Fvf & 0x80) != 0)
        Colours += 4;

    unsigned int Textures = 0;
    const int TexCount = (int)((Fvf >> 8) & 0xF);
    unsigned int Shift = 0x10;
    for (int TexIndex = 0; TexIndex < TexCount; ++TexIndex)
    {
        switch ((Fvf >> Shift) & 3)
        {
        case 0:
            Textures += 8;
            break;
        case 1:
            Textures += 0xC;
            break;
        case 2:
            Textures += 0x10;
            break;
        case 3:
            Textures += 4;
            break;
        }
        Shift += 2;
    }
    return Textures + Colours + Normal + Position;
}

static int VMeshChannelElemBytes(unsigned int ChannelId)
{
    for (unsigned int Index = 0; Index < G_FvfChannelCount; ++Index)
    {
        if (G_FvfChannelTable[Index].ChannelId == ChannelId)
            return (int)(G_FvfChannelTable[Index].ElemBytes);
    }
    return 0;
}

// FUN_06c2e020 / FUN_06c31500. Vertices start after the facegroups and the index array:
// header + 0x10 + 12 * FaceGroups + 2 * Indices; the VMeshRef variant adds VertexStart*stride.
// +0x0C of the record is never written.
bool VMeshFillChannelRecord(const FVmeshDataHeader* Header, unsigned long VertexStart,
                            unsigned long VertexCount, unsigned int ChannelId,
                            FVmeshChannelRecord* Out)
{
    const unsigned long Fvf = Header->Fvf;
    if (!VMeshChannelPresent(Fvf, ChannelId))
        return false;
    const unsigned int Stride = VMeshFvfStride(Fvf);
    const unsigned char* Vertices = (const unsigned char*)Header + 0x10 +
                                    (unsigned long)Header->FaceGroups * 0xC +
                                    (unsigned long)Header->Indices * 2;
    Out->Pointer = (unsigned char*)(Vertices + VertexStart * Stride +
                                              VMeshChannelByteOffset(Fvf, ChannelId));
    Out->ElemBytes = VMeshChannelElemBytes(ChannelId);
    Out->Stride = (int)Stride;
    Out->VertexCount = (int)VertexCount;
    return true;
}

// FUN_06c2e510 / FUN_06c31730: zero 0x70 bytes, channel records 1 @+4, 8 @+0x18, 2 @+0x40,
// 4 @+0x54, 0x10 @+0x2C, [0] = 2, +0x68 = FVF, +0x6C = vertex count.
static void VMeshFillVertexStream(const FVmeshDataHeader* Header, unsigned long VertexStart,
                                  unsigned long VertexCount, unsigned int ChannelId,
                                  FRpVertexStream* Out)
{
    FVmeshChannelRecord Record;
    if (VMeshFillChannelRecord(Header, VertexStart, VertexCount, ChannelId, &Record))
    {
        Out->Base = (const unsigned char*)Record.Pointer;
        Out->ByteSize = Record.ElemBytes;
        Out->Stride = Record.Stride;
        Out->Count = Record.VertexCount;
    }
}

void VMeshBuildVertexDesc(const FVmeshDataHeader* Header, unsigned long VertexStart,
                          unsigned long VertexCount, FRpVertexSrcDesc* Out)
{
    *Out = FRpVertexSrcDesc{};
    VMeshFillVertexStream(Header, VertexStart, VertexCount, 1, &Out->Position);
    VMeshFillVertexStream(Header, VertexStart, VertexCount, 8, &Out->Normal);
    VMeshFillVertexStream(Header, VertexStart, VertexCount, 2, &Out->Tex[0]);
    VMeshFillVertexStream(Header, VertexStart, VertexCount, 4, &Out->Tex[1]);
    VMeshFillVertexStream(Header, VertexStart, VertexCount, 0x10, &Out->Diffuse);
    Out->Flags = 2;
    Out->SrcFvf = Header->Fvf;
    Out->SrcCount = VertexCount;
}

// ---------------------------------------------------------------------------------------
// Shared vertex/index buffer group (0x28).
// ---------------------------------------------------------------------------------------
static int VMeshGroupRemove(FVmeshGroup* Group, FVmeshData* Data);

// FUN_06c2d480: DestroyVb(&group->VertexBuffer).
void FVmeshGroup::DestroyVb()
{
    G_VertexBuffer->DestroyVb(&VertexBuffer);
}

// FUN_06c2d490: DestroyIb(&group->IndexBuffer).
void FVmeshGroup::DestroyIb()
{
    G_IndexBuffer->DestroyIb(&IndexBuffer);
}

// FUN_06c2d460: Fvf = 0, then both buffers.
void FVmeshGroup::ReleaseBuffers()
{
    Fvf = 0;
    DestroyVb();
    DestroyIb();
}

// FUN_06c2d330 (thiscall on the group). Already a member -> true. The first member
// fixes the FVF; a different FVF -> false. Totals grow before the old group lets go.
bool FVmeshGroup::Add(FVmeshData* Data)
{
    if (Data->Group == this)
        return true;
    const FVmeshDataHeader* Header = Data->Header;
    if (Fvf == 0)
        Fvf = Header->Fvf;
    const bool bSameFvf = Fvf == Header->Fvf;
    if (!bSameFvf)
        return bSameFvf;
    VertexTotal += Header->Vertices;
    IndexTotal += Header->Indices;
    if (Data->Group != nullptr)
        VMeshGroupRemove(Data->Group, Data);
    Data->Group = this;
    FVmeshData* Last = this->Last;
    this->Last = Data;
    if (Last == nullptr)
        First = Data;
    else
        Last->Next = Data;
    Data->Prev = Last;
    Data->Next = nullptr;
    Count += 1;
    return bSameFvf;
}

static int VMeshGroupRemove(FVmeshGroup* Group, FVmeshData* Data)
{
    int Result = -1;
    if (Data->Group != Group)
        return Result;
    const FVmeshDataHeader* Header = Data->Header;
    Group->VertexTotal -= Header->Vertices;
    Group->IndexTotal -= Header->Indices;
    FVmeshData* Node = nullptr;
    while (true)
    {
        if (Node == nullptr)
            Node = Group->First;
        else
            Node = Node->Next;
        if (Node == nullptr)
            break;
        if (Node != Data)
            continue;
        Node->Group = nullptr;
        if (Node->Prev == nullptr)
            Group->First = Node->Next;
        else
            Node->Prev->Next = Node->Next;
        if (Node->Next == nullptr)
            Group->Last = Node->Prev;
        else
            Node->Next->Prev = Node->Prev;
        Group->Count -= 1;
        const int Count = Group->Count;
        if (Count == 0)
        {
            Group->ReleaseBuffers();
            delete Group;
        }
        Result = Count;
    }
    return Result;
}

// FUN_06c2d4a0 (ECX = group). A VB that still selects is kept. Otherwise it is rebuilt for
// VertexTotal vertices and every member copies its vertices in at a running base; a failed
// copy stops the walk. Returns the final select_vb result (or the create failure).
int FVmeshGroup::EnsureVb()
{
    int Result = G_VertexBuffer->SelectVb(VertexBuffer);
    if (Result >= 0)
        return Result;
    G_VertexBuffer->DestroyVb(&VertexBuffer);
    Result = G_VertexBuffer->CreateVb(Fvf, VertexTotal,
                                      (unsigned int**)(&VertexBuffer), 0);
    if (Result < 0)
        return Result;
    unsigned int LockOffset = 0;
    void* LockBits = nullptr;
    if (G_VertexBuffer->LockVb(VertexBuffer, &LockOffset, &LockBits, VertexTotal) >= 0)
    {
        unsigned long Base = 0;
        FVmeshData* Data = nullptr;
        while (true)
        {
            if (Data == nullptr)
                Data = First;
            else
                Data = Data->Next;
            if (Data == nullptr)
                break;
            FRpVertexSrcDesc Desc;
            VMeshBuildVertexDesc(Data->Header, 0, Data->Header->Vertices, &Desc);
            unsigned int OutBase = Base;
            if (G_VertexBuffer->CopyVertices(VertexBuffer, &OutBase, &Desc, 0,
                                             Desc.SrcCount) < 0)
                break;
            Data->VertexBuffer = VertexBuffer;
            Data->Fvf = Fvf;
            Data->BaseVertex = OutBase;
            Base += Data->Header->Vertices;
        }
        G_VertexBuffer->UnlockVb(VertexBuffer);
    }
    return G_VertexBuffer->SelectVb(VertexBuffer);
}

int FVmeshGroup::EnsureIb(unsigned long BaseVertex)
{
    int Result = G_IndexBuffer->SelectIb(IndexBuffer, BaseVertex, 0, 0);
    if (Result >= 0)
        return Result;
    G_IndexBuffer->DestroyIb(&IndexBuffer);
    Result = G_IndexBuffer->CreateIb(IndexTotal,
                                     (unsigned int**)(&IndexBuffer), 0);
    if (Result < 0)
        return Result;
    unsigned int LockOffset = 0;
    void* LockBits = nullptr;
    if (G_IndexBuffer->LockIb(IndexBuffer, &LockOffset, &LockBits, IndexTotal) >= 0)
    {
        unsigned short* Dest = (unsigned short*)LockBits;
        unsigned long Start = 0;
        FVmeshData* Data = nullptr;
        while (true)
        {
            if (Data == nullptr)
                Data = First;
            else
                Data = Data->Next;
            if (Data == nullptr)
                break;
            const FVmeshDataHeader* Header = Data->Header;
            const FVmeshFacegroup* Facegroups = (const FVmeshFacegroup*)(
                (const unsigned char*)Header + 0x10);
            const unsigned short* Source = (const unsigned short*)(
                Facegroups + Header->FaceGroups);
            if ((Header->Flags & 1) != 0)
            {
                for (int GroupIndex = 0; GroupIndex < (int)Header->FaceGroups;
                     ++GroupIndex)
                {
                    const FVmeshFacegroup* Facegroup = &Facegroups[GroupIndex];
                    for (int Index = 0; Index < (int)Facegroup->IndexCount; ++Index)
                    {
                        *Dest = (unsigned short)(Facegroup->VertexBegin + *Source);
                        Dest += 1;
                        Source += 1;
                    }
                }
            }
            else
            {
                std::memcpy(Dest, Source, (size_t)Header->Indices * 2);
                Dest += Header->Indices;
            }
            Data->IndexBuffer = IndexBuffer;
            Data->StartIndex = Start;
            Start += Header->Indices;
        }
        G_IndexBuffer->UnlockIb(IndexBuffer);
    }
    return G_IndexBuffer->SelectIb(IndexBuffer, BaseVertex, 0, 0);
}

// ---------------------------------------------------------------------------------------
// VMeshData.
// ---------------------------------------------------------------------------------------

// FUN_06c2df60 (thiscall): SizeKib = (int)Size * (double)1/1024 (DAT_06c36490) stored as float.
FVmeshData* FVmeshData::Construct(const char* Name, unsigned long Size)
{
    SizeKib = (float)((double)((int)Size) * 0.0009765625);
    Crc = 0;
    Next = nullptr;
    Prev = nullptr;
    Group = nullptr;
    VertexBuffer = nullptr;
    Fvf = 0;
    BaseVertex = 0;
    IndexBuffer = nullptr;
    StartIndex = 0;
    Materials = nullptr;
    Header = nullptr;
    RefCount = 1;
    this->Name = _strdup(Name);
    Crc = DACOM_CRC::GetCRC32(this->Name);
    VMeshDataMapInsert(this);
    LogStream::LogEvent("Memory_VMeshData", SizeKib, 0x610);
    return this;
}

// FUN_06c2df00: map entry, group membership, name, leaf bytes and the material array go (the
// materials themselves are released by the parts, FUN_06c30980), then the negative event.
void FVmeshData::Destruct()
{
    VMeshDataMapErase(this);
    if (Group != nullptr)
        VMeshGroupRemove(Group, this);
    if (Name != nullptr)
        std::free(Name);
    delete[] (const unsigned char*)Header;
    delete[] Materials;
    LogStream::LogEvent("Memory_VMeshData", -SizeKib, 0x610);
}

int VMeshDataAddRef(FVmeshData* Data)
{
    Data->RefCount += 1;
    return Data->RefCount;
}

// FUN_06c2dff0: at <= 0 the object is destroyed; returns the decremented count.
int FVmeshData::Release()
{
    RefCount -= 1;
    const int Count = RefCount;
    if (Count <= 0)
    {
        Destruct();
        delete this;
    }
    return Count;
}

// FUN_06c2dd20 (cdecl File, Name). A CRC hit is AddRef'd and returned. Otherwise the
// "VMeshData" leaf of the given (per-.vms) file system is read whole; a failed open or read
// is an ERROR and 0. Old-format leaves (flag bit 0 clear) are refused with a WARNING.
FVmeshData* VMeshDataLoad(IFileSystem* File, const char* Name)
{
    FVmeshKey Key;
    Key.Name = Name;
    Key.Crc = DACOM_CRC::GetCRC32(Name);
    FVmeshData* Found = VMeshDataFind(&Key);
    if (Found != nullptr)
    {
        VMeshDataAddRef(Found);
        return Found;
    }

    unsigned long Size = 0;
    unsigned char* Bytes = nullptr;
    int bRead = 0;
    FFileSystemDesc Desc;
    Desc.Init("VMeshData", "FileSystem");
    const int Handle = File->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        const unsigned long LeafSize = File->GetSize((unsigned long)Handle, 0);
        unsigned char* Buffer = new unsigned char[LeafSize];
        unsigned long ReadCount = 0;
        bRead = File->Read((unsigned long)Handle, Buffer, LeafSize, &ReadCount, 0);
        if (bRead != 0)
        {
            Size = LeafSize;
            Bytes = Buffer;
        }
        else
            delete[] Buffer;
        File->CloseFile((unsigned long)Handle);
    }
    if (bRead == 0)
    {
        RendCompFdumpAt(kFdumpError, G_FileVMeshData, 0x2A,
                        "couldnt load vmesh persistant data; bad version?");
        return nullptr;
    }

    FVmeshData* Data = (new FVmeshData)->Construct(Name, Size);
    Data->Header = (FVmeshDataHeader*)Bytes;
    if ((Data->Header->Flags & 1) == 0)
    {
        char Message[0x2000];
        std::sprintf(Message, "mesh in old format: %s", Name);
        RendCompFdumpAt(kFdumpWarning, G_FileVMeshData, 0x3A, Message);
        Data->Destruct();
        delete Data;
        return nullptr;
    }

    const unsigned int FaceGroups = Data->Header->FaceGroups;
    IMaterial** Materials =
        new IMaterial*[FaceGroups];
    for (unsigned int Index = 0; Index < FaceGroups; ++Index)
        Materials[Index] = nullptr;
    Data->Materials = Materials;
    return Data;
}

// ---------------------------------------------------------------------------------------
// VMeshLibrary aggregate (0x18).
// ---------------------------------------------------------------------------------------

// The handle Load returns: retail news a 0xC VC6 std::list<smart ptr to VMeshData>
// (FUN_06c2f2b0 head, FUN_06c2f250 push_back with AddRef, FUN_06c2f2e0 / FUN_06c2f230 erase
// with Release, FUN_06c2f330 iterator step).
typedef std::list<FVmeshData*> FVmeshDataList;

class FVMeshLibrary : public IAggregateComponent, public IVMeshLibrary
{
public:
    // +0xC: non-delegating unknown (vtable PTR_LAB_06c364f8).
    struct FInner : public IDAComponent
    {
        unsigned long RefCount; // +0x10
        FVMeshLibrary* Owner;   // +0x14

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
    };

    IDAComponent* Outer;        // +0x08 AGGDESC+8, or &Inner when not aggregated
    FInner Inner;

    // LAB_06c2f430 / 440 / 450 (and the -4 thunks 06c2f460 / 470 / 480 for the IVMeshLibrary
    // face): everything goes to the controlling unknown.
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        return Outer->QueryInterface(Iid, Out);
    }

    unsigned long DACOM_API AddRef() override
    {
        return Outer->AddRef();
    }

    unsigned long DACOM_API Release() override
    {
        return Outer->Release();
    }

    // FUN_06c2f5e0.
    GENRESULT DACOM_API Initialize() override
    {
        return 0;
    }

    void* DACOM_API Load(void* File, int LodFilter) override;
    void DACOM_API Unload(void* Handle) override;
};

// LAB_06c2f340: table {"1.11_IVMeshLibrary", 4}, {"1.11_IAggregateComponent", 0}; a hit hands
// out owner + offset AddRef'd through that face; a miss writes 0 and returns -3.
GENRESULT DACOM_API FVMeshLibrary::FInner::QueryInterface(const char* Iid, void** Out)
{
    IDAComponent* Face = nullptr;
    if (std::strcmp(Iids::VMeshLibrary, Iid) == 0)
        Face = (IVMeshLibrary*)Owner;
    else if (std::strcmp(Iids::AggregateComponent, Iid) == 0)
        Face = (IAggregateComponent*)Owner;
    if (Face == nullptr)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

// LAB_06c306b0.
unsigned long DACOM_API FVMeshLibrary::FInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// LAB_06c2f400: decrement when above 0; at 0 pin the count at 1 and free the owner (the
// object has no destructor), return 0.
unsigned long DACOM_API FVMeshLibrary::FInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    delete Owner;
    return 0;
}

void* DACOM_API FVMeshLibrary::Load(void* FileArg, int LodFilter)
{
    IFileSystem* File = (IFileSystem*)FileArg;
    FVmeshDataList* List = nullptr;
    FVmeshGroup* Group = nullptr;
    if (File->OpenNode("VMeshLibrary") == 0)
        return List;

    WIN32_FIND_DATAA FindData;
    const int Find = File->FindFirst("*.*", &FindData);
    if (Find != -1)
    {
        IFileSystem* Child = nullptr;
        do
        {
            if ((FindData.dwFileAttributes & 0x10) == 0 || FindData.cFileName[0] == '.')
                continue;
            FFileSystemDesc Desc;
            std::memset(&Desc, 0, sizeof(Desc));
            Desc.ComponentName = "FileSystem";
            Child = nullptr;
            Desc.Access = 0x80000000;
            Desc.ShareMode = 1;
            Desc.Creation = 3;
            Desc.Flags = 0x8000080;
            Desc.Path = FindData.cFileName;
            Desc.Size = 0x34;
            Desc.FindHandle = (unsigned long)Find;
            if (LodFilter >= 0)
            {
                const char* Lod = std::strstr(FindData.cFileName, ".lod");
                if (Lod != nullptr && !((Lod[4] - '0') >= LodFilter))
                    goto NextEntry;
                if (Child != nullptr)
                {
                    Child->Release();
                    Child = nullptr;
                }
            }
            if (File->CreateInstance((AGGDESC*)(&Desc),
                                     (void**)(&Child)) < 0)
                goto NextEntry;
            {
                FVmeshData* Data = VMeshDataLoad(Child, FindData.cFileName);
                if (Data == nullptr)
                {
                    char Message[0x2000];
                    std::sprintf(Message, "couldnt load %s", FindData.cFileName);
                    RendCompFdumpAt(kFdumpWarning, G_FileVMeshLibrary, 0x8B, Message);
                    goto NextEntry;
                }
                if (List == nullptr)
                    List = new FVmeshDataList();
                VMeshDataAddRef(Data);
                List->push_back(Data);
                if (Group == nullptr)
                    Group = new FVmeshGroup();
                if (!Group->Add(Data))
                {
                    if (Group->Count == 0)
                    {
                        Group->ReleaseBuffers();
                        delete Group;
                    }
                    Group = new FVmeshGroup();
                    if (!Group->Add(Data))
                    {
                        char Message[0x2000];
                        std::sprintf(Message, "couldnt add vmesh to vmeshlibrary? %s",
                                     FindData.cFileName);
                        RendCompFdumpAt(kFdumpWarning, G_FileVMeshLibrary, 0xA5, Message);
                    }
                }
                Data->Release();
            }
        NextEntry:
            if (Child != nullptr)
            {
                Child->Release();
                Child = nullptr;
            }
        } while (File->FindNext((unsigned long)Find, &FindData) != 0);
    }

    File->OpenNode(".."); // DAT_06c37250
    if (List != nullptr && List->empty())
    {
        delete List;
        List = nullptr;
    }
    if (Group != nullptr && Group->Count == 0)
    {
        Group->ReleaseBuffers();
        delete Group;
    }
    return List;
}

// FUN_06c2f1c0 (thiscall).
void DACOM_API FVMeshLibrary::Unload(void* Handle)
{
    FVmeshDataList* List = (FVmeshDataList*)Handle;
    if (List == nullptr)
        return;
    while (!List->empty())
    {
        FVmeshData* Data = List->front();
        List->pop_front();
        if (Data != nullptr)
            Data->Release();
    }
    delete List;
}

// LAB_06c2ed50 (factory vtable slot 3).
struct FVMeshAggDesc
{
    unsigned long Size;
    const char* Name;
    IDAComponent* Outer;
    IDAComponent** InnerOut;
    const char* Extra;
};

GENRESULT DACOM_API VMeshLibraryFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    FVMeshAggDesc* Extended = (FVMeshAggDesc*)Desc;
    if (Extended->Size != 0x14 || std::strcmp(Extended->Name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    FVMeshLibrary* Object = new (std::nothrow) FVMeshLibrary();
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    Object->Inner.RefCount = 1;
    Object->Inner.Owner = Object;
    Object->Outer = Extended->Outer;
    if (Object->Outer == nullptr)
        Object->Outer = &Object->Inner;
    else
        *Extended->InnerOut = &Object->Inner;
    *Out = (IAggregateComponent*)Object;
    return GR_OK;
}
