// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_nodelibrary.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "alchemy_common.h"
#include "alchemy_genericfx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

// Independent node-library instances share registry code. The map owns
// prototypes; their actual Next/Prev sibling links form the borrowed insertion
// list used by enumeration, saving and teardown.
namespace AlchemyImpl
{
    namespace
    {
        template <typename TInterface>
        struct FLoadedInterfaceReference
        {
            FLoadedInterfaceReference() : Value(nullptr) {}
            ~FLoadedInterfaceReference()
            {
                if (Value != nullptr)
                    Value->Release();
            }
            FLoadedInterfaceReference(const FLoadedInterfaceReference&) = delete;
            FLoadedInterfaceReference& operator=(const FLoadedInterfaceReference&) = delete;

            TInterface* Value;
        };
    }

    IDAComponent* FNodePrototypeRegistry::Find(unsigned long Hash)
    {
        FPrototypeMap::iterator Entry = Entries_->find(Hash);
        return Entry == Entries_->end() ? nullptr : Entry->second.Component;
    }

    GENRESULT FNodePrototypeRegistry::FindWithRef(unsigned long Hash, IDAComponent** Out)
    {
        FPrototypeMap::iterator Entry = Entries_->find(Hash);
        if (Entry == Entries_->end())
            return GR_ALE_CREATE_FAIL;
        *Out = Entry->second.Component;
        Entry->second.Component->AddRef();
        return GR_OK;
    }

    GENRESULT FNodePrototypeRegistry::Insert(IFxNodeLibrary* Library, const char* Name,
                                             IDAComponent* Node, unsigned long* OutHash)
    {
        const unsigned long Hash = Library->HashNodeName(Name);
        FAlchemyComponentReference Existing;
        Library->FindNodeByHash(Hash, &Existing.Component);
        if (Existing.Component != nullptr)
        {
            const char* ExistingName;
            AsFxNode(Existing.Component)->GetInstanceName(&ExistingName);
            if (std::strcmp(ExistingName, Name) != 0)
            {
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                      0x43, "Different name generates same Hash");
            }
            return GR_OK;
        }

        AsFxNode(Node)->SetInstanceName(Name);
        if (OutHash != nullptr)
            *OutHash = Hash;
        bool bInserted = false;
        {
            FAlchemyComponentReference Owner(Node);
            {
                FAlchemyComponentReference Pair(Owner);
                FPrototypeMap::iterator Position = Entries_->lower_bound(Hash);
                if (Position == Entries_->end() || Position->first != Hash)
                {
                    Entries_->emplace_hint(Position, Hash, Pair);
                    bInserted = true;
                }
            }
        }
        if (!bInserted)
            return GR_GENERIC;

        AsFxNode(Node)->SetNextSibling(nullptr);
        if (Tail_ != nullptr)
        {
            AsFxNode(Tail_)->SetNextSibling(Node);
            AsFxNode(Node)->SetPrevSibling(Tail_);
        }
        else
        {
            AsFxNode(Node)->SetPrevSibling(nullptr);
            Head_ = Node;
        }
        Tail_ = Node;
        ++Count_;
        return GR_OK;
    }

    IDAComponent* FNodePrototypeRegistry::UnlinkPrototype(IDAComponent* Node)
    {
        IDAComponent* Prev = AsFxNode(Node)->GetPrevSibling();
        IDAComponent* Next = AsFxNode(Node)->GetNextSibling();
        if (Prev != nullptr)
            AsFxNode(Prev)->SetNextSibling(Next);
        else
            Head_ = Next;
        if (Next != nullptr)
            AsFxNode(Next)->SetPrevSibling(Prev);
        else
            Tail_ = Prev;
        AsFxNode(Node)->SetPrevSibling(nullptr);
        AsFxNode(Node)->SetNextSibling(nullptr);
        return Next;
    }

    GENRESULT FNodePrototypeRegistry::Remove(unsigned long Hash)
    {
        FPrototypeMap::iterator Entry = Entries_->find(Hash);
        if (Entry == Entries_->end() || Entry->first == 0)
            return GR_GENERIC;
        UnlinkPrototype(Entry->second.Component);
        {
            FPrototypeMap::node_type Removed = Entries_->extract(Entry);
            Removed.mapped().Reset();
        }
        --Count_;
        return GR_OK;
    }

    GENRESULT FNodePrototypeRegistry::Enumerate(FNodeLibraryEnumCallback Callback, void* Context)
    {
        IDAComponent* Node = Head_;
        while (Node != nullptr)
        {
            if (Callback(Node, Context) != 0)
                break;
            Node = AsFxNode(Node)->GetNextSibling();
        }
        return GR_OK;
    }

    FNodePrototypeRegistry::FMapStorageOwner::~FMapStorageOwner() noexcept(false)
    {
        if (Storage != nullptr)
        {
            FPrototypeMap* Current = Storage;
            Storage = nullptr;
            Owner->DestroyMapStorage(Current);
        }
    }

    FNodePrototypeRegistry::~FNodePrototypeRegistry() noexcept(false)
    {
        if (MapOwner_.Storage != nullptr)
            Destroy();
    }

    void FNodePrototypeRegistry::Destroy()
    {
        // Map-only ownership is consumed before the detachment callbacks.
        // Entries_ remains published until all releases and deletion succeed.
        FMapStorageOwner Storage(this, MapOwner_.Storage);
        MapOwner_.Storage = nullptr;
        DetachPrototypes();
    }

    void FNodePrototypeRegistry::DetachPrototypes()
    {
        while (Head_ != nullptr)
        {
            IDAComponent* Node = Head_;
            Head_ = AsFxNode(Node)->GetNextSibling();
            AsFxNode(Node)->SetNextSibling(nullptr);
            AsFxNode(Node)->SetPrevSibling(nullptr);
        }
        Head_ = nullptr;
        Tail_ = nullptr;
    }

    void FNodePrototypeRegistry::ClearMapEntries(FPrototypeMap* Storage)
    {
        for (FPrototypeMap::reverse_iterator Entry = Storage->rbegin();
             Entry != Storage->rend(); ++Entry)
        {
            Entry->second.Reset();
        }
        Storage->clear();
    }

    void FNodePrototypeRegistry::DestroyMapStorage(FPrototypeMap* Storage)
    {
        ClearMapEntries(Storage);
        delete Storage;
        Entries_ = nullptr;
    }

    void FNodePrototypeRegistry::Clear()
    {
        DetachPrototypes();
        ClearMapEntries(Entries_);
        Count_ = 0;
    }

    // 06228b90: the shared embedded/standalone loader retains one IFxNode and
    // its property set until insertion. Header failures close without leaving;
    // record failures leave then close. EH only closes, after releasing owners.
    GENRESULT FNodePrototypeRegistry::Load(IFxNodeLibrary* Library, IFileSystem* Filesys)
    {
        if (Filesys->OpenNode(kNodeLibName) == 0)
            return GR_ALE_LOAD_FAIL;

        FAleChunk Chunk;
        Chunk.OpenChunk(Filesys, kNodeLibName, 0);
        unsigned long VersionBits = 0;
        int Count = 0;
        if (Chunk.ReadBytes(&VersionBits, 4) < 0)
        {
            Chunk.CloseChunk();
            return GR_ALE_LOAD_FAIL;
        }
        GENRESULT Result = Chunk.ReadBytes(&Count, 4);
        if (Result < 0)
        {
            Chunk.CloseChunk();
            return GR_ALE_LOAD_FAIL;
        }

        for (int Index = 0; Index < Count; ++Index)
        {
            char Name[256];
            Result = Chunk.ReadString(Name, kAleNameMax);
            if (Result < 0)
                break;
            FixColideFieldAlias(Name);

            FLoadedInterfaceReference<IDAComponent> Created;
            FLoadedInterfaceReference<IGenericFx> Fx;
            AGGDESC Desc;
            Desc.size = sizeof(Desc);
            Desc.interface_name = Name;
            Result = DACOM_Acquire()->CreateInstance(
                &Desc, reinterpret_cast<void**>(&Created.Value));
            if (Result < 0)
            {
                const std::string Message = std::string("Unable to create instance of node \"") +
                    Name + "\". Aborting node library load.\n";
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                      0x3bb, Message.c_str());
                break;
            }

            Result = Created.Value->QueryInterface(
                Iids::FxNode, reinterpret_cast<void**>(&Fx.Value));
            if (Result < 0)
            {
                const std::string Message = std::string("DACOM class \"") + Name +
                    "\" does not implement IFxNode. Aborting node library load.\n";
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                      0x3c8, Message.c_str());
                break;
            }

            FLoadedInterfaceReference<IFxNodePropertySet> Set;
            Fx.Value->GetPropertySet(&Set.Value);
            ReadNodeLibProperties(&Chunk, Set.Value, VersionBits);
            const char* CurrentName = nullptr;
            Set.Value->GetPropName(0xefa8ce01, &CurrentName);
            if (CurrentName == nullptr || CurrentName[0] == '\0')
            {
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                      0x3d8, "This node has empty name. Alchemy give new name for this node.");
                CurrentName = "unnamed";
                unsigned long Suffix = 0;
                while (Entries_->find(Library->HashNodeName(CurrentName)) != Entries_->end())
                {
                    // The inline loader starts from the fixed literal "unnamed":
                    // every collision retains this base and an empty extension.
                    // Unsigned increment preserves the retail 32-bit counter wrap.
                    ++Suffix;
                    std::sprintf(G_ResolvedName, "unnamed#%d", static_cast<int>(Suffix));
                    CurrentName = G_ResolvedName;
                }
                Set.Value->SetPropName(0xefa8ce01, CurrentName);
                Set.Value->GetPropName(0xefa8ce01, &CurrentName);
            }
            Result = Library->InsertNode(CurrentName, Fx.Value, nullptr);
        }
        Filesys->OpenNode(kAleLeaveNode);
        Chunk.CloseChunk();
        return Result;
    }

    FFxNodeLibrary::~FFxNodeLibrary() noexcept(false) {}

    GENRESULT DACOM_API FFxNodeLibrary::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_0622afe0: a 2-entry IID table (0624e114) of "1.11_IFxNodeLibrary" and
        // "1.11_IDAComponent", both at offset 0.
        if (IidEqual(Iid, Iids::FxNodeLibrary) || IidEqual(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IFxNodeLibrary*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FFxNodeLibrary::Release()
    {
        // FUN_0622b0a0 (vtable PTR_LAB_0624e0a4 slot 2): the decrement is guarded against
        // underflow, and the count (+0x28) is re-armed to 1 before the teardown
        // (FUN_0622b140 / FUN_062460e0).
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ != 0)
            return RefCount_;

        RefCount_ = 1;
        this->~FFxNodeLibrary();
        ::operator delete(this);
        return 0;
    }

    GENRESULT DACOM_API FFxNodeLibrary::LoadNodeLibrary(IFileSystem* Filesys)
    {
        return NodeRegistry_.Load(this, Filesys);
    }

    GENRESULT DACOM_API FFxNodeLibrary::SaveNodeLibrary(IFileSystem* Filesys)
    {
        FAleChunk Chunk;
        Chunk.Filesys = Filesys;
        Chunk.DeleteNode(kNodeLibName);
        Chunk.OpenNode(kNodeLibName);
        Chunk.OpenChunk(Filesys, kNodeLibName, 1);
        const unsigned long VersionBits = kAleVersion11Bits;
        Chunk.WriteBytes(&VersionBits, 4);
        Chunk.WriteBytes(NodeRegistry_.CountAddress(), 4);
        IDAComponent* Node = NodeRegistry_.First();
        while (Node != nullptr)
        {
            WriteNodeLibProperties(&Chunk, Node);
            Node = AsFxNode(Node)->GetNextSibling();
        }
        Chunk.OpenNode(kAleLeaveNode);
        Chunk.CloseChunk();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxNodeLibrary::InsertNode(const char* Name, IDAComponent* Node,
                                                       unsigned long* OutHash)
    {
        return InsertNodeByName(Name, Node, OutHash);
    }

    GENRESULT DACOM_API FFxNodeLibrary::RemoveNodeByHash(unsigned long Hash)
    {
        return NodeRegistry_.Remove(Hash);
    }

    GENRESULT DACOM_API FFxNodeLibrary::EnumerateNodes(FNodeLibraryEnumCallback Callback, void* Context)
    {
        return NodeRegistry_.Enumerate(Callback, Context);
    }

    GENRESULT DACOM_API FFxNodeLibrary::ClearNodeLibrary()
    {
        ClearNodeList();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxNodeLibrary::MakeUniqueName(void* NameRaw, void* OutRaw)
    {
        // 062277e0: borrowed original name or the independent DAT_06257d80 buffer.
        const char* CurrentName = static_cast<const char*>(NameRaw);
        const char** Out = static_cast<const char**>(OutRaw);
        char Base[256];
        char Ext[32];
        int Suffix = 0;
        while (FindNodeEntry(HashNodeName(CurrentName)) != nullptr)
        {
            if (Suffix == 0)
            {
                std::strcpy(Base, CurrentName);
                char* HashMark = std::strrchr(Base, '#');
                if (HashMark != nullptr)
                {
                    *HashMark = '\0';
                    Suffix = MsvcrtAtoi(HashMark + 1);
                }
                const char* Dot = std::strrchr(CurrentName, '.');
                if (Dot != nullptr)
                    std::strcpy(Ext, Dot);
                else
                    Ext[0] = '\0';
            }
            unsigned int SuffixBits;
            std::memcpy(&SuffixBits, &Suffix, sizeof(SuffixBits));
            ++SuffixBits;
            std::memcpy(&Suffix, &SuffixBits, sizeof(Suffix));
            std::sprintf(G_NodeLibraryResolvedName, "%s#%d%s", Base, Suffix, Ext);
            CurrentName = G_NodeLibraryResolvedName;
        }
        *Out = CurrentName;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxNodeLibrary::CreateNodeByHash(unsigned long Hash, IDAComponent** Out)
    {
        IDAComponent* Prototype = FindNodeEntry(Hash);
        if (Prototype == nullptr)
            return GR_ALE_CREATE_FAIL;
        return InstantiateFromNodeLibPrototype(Prototype, Out);
    }

    GENRESULT DACOM_API FFxNodeLibrary::FindNodeByHash(unsigned long Hash, IDAComponent** Out)
    {
        return NodeRegistry_.FindWithRef(Hash, Out);
    }

    IDAComponent* FFxNodeLibrary::FindNodeEntry(unsigned long Hash)
    {
        return NodeRegistry_.Find(Hash);
    }

    GENRESULT FFxNodeLibrary::InsertNodeByName(const char* Name, IDAComponent* Node,
                                             unsigned long* OutHash)
    {
        return NodeRegistry_.Insert(this, Name, Node, OutHash);
    }

    void FFxNodeLibrary::ClearNodeList()
    {
        NodeRegistry_.Clear();
    }

} // namespace AlchemyImpl
