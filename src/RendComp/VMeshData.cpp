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

static bool VMeshDataMapInsert(FVmeshData* Data);
static void VMeshDataMapErase(FVmeshData* Data);

// ---------------------------------------------------------------------------------------
// VMeshData.
// ---------------------------------------------------------------------------------------

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

// Mesh data indexed by CRC.
static std::map<unsigned long, FVmeshData*> G_VmeshDataMap;

FVmeshData* VMeshDataFind(const FVmeshKey* Key)
{
    std::map<unsigned long, FVmeshData*>::iterator It = G_VmeshDataMap.find(Key->Crc);
    if (It == G_VmeshDataMap.end())
        return nullptr;
    return It->second;
}

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
