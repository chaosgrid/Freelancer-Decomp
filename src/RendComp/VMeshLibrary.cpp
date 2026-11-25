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

// ---------------------------------------------------------------------------------------
// Shared vertex/index buffer group (0x28).
// ---------------------------------------------------------------------------------------

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

int VMeshGroupRemove(FVmeshGroup* Group, FVmeshData* Data)
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
