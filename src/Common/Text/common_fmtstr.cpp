// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FmtStr, FmtStr::Val and the mission-log pack helpers.

#include "common_text.h"
#include "dacom.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <deque>
#include <list>
#include <new>
#include <vector>
#include <windows.h>

void __cdecl AppendMissionLogData(FmtStr const* Fmt, unsigned char*& Buffer, int& Offset, int MaxSize)
{
    int Result = Fmt->flatten(Buffer, MaxSize);
    Offset += Result;
    Buffer += Result;
}

void __cdecl ExtractMissionLogData(FmtStr* Fmt, unsigned char*& Buffer, int& Remaining)
{
    int Result = Fmt->unflatten(Buffer, 0);
    Remaining -= Result;
    Buffer += Result;
}

int __cdecl PackMissionLogData(int Type, FmtStr const* Fmt, unsigned char* Buffer, int MaxSize)
{
    *(int*)Buffer = Type;
    if (Type == 0)
    {
        *(int*)(Buffer + 4) = 0;
        return 8;
    }
    if (Type == 1)
    {
        *(int*)(Buffer + 4) = 1;
        int Result = Fmt->flatten(Buffer + 8, MaxSize);
        return Result + 8;
    }
    return 8;
}

// ============================================================================
// FmtStr -- linked-list formatted string.
// ============================================================================

FmtStr::Val::Val(unsigned short InType, void* InData, unsigned char InFlags)
{
    // FUN_0630d570. The exported signature takes the payload as void*; retail stores the dword
    // as-is, whatever the tag.
    Next = nullptr;
    Value = (unsigned int)(std::uintptr_t)InData;
    TypeTag = InType;
    Flags = InFlags;
}

FmtStr::Val::Val(void const* Src, int& OutConsumed)
{
    // FUN_0630d660: from a serialized Val, reporting the bytes consumed.
    Next = nullptr;
    OutConsumed = InitFromVoid(Src);
}

FmtStr::Val::Val(void const* Src)
{
    // FUN_0630d680
    Next = nullptr;
    InitFromVoid(Src);
}

FmtStr::Val::Val(const Val& Other)
{
    // FUN_0630d6a0: forwards to operator=, which deep-copies 0x21/0x4E payloads.
    *this = Other;
}

FmtStr::Val::~Val()
{
    // FUN_0630d510. Tag 0x21: delete each child, clear the subtree's Head, delete the subtree.
    // Tag 0x4E: delete the block.
    if (TypeTag == FmtStr::TagFmtStr)
    {
        if (Subtree != nullptr)
        {
            Val* Child = Subtree->Head;
            while (Child != nullptr)
            {
                Val* NextChild = Child->Next;
                delete Child;
                Child = NextChild;
            }
            Subtree->Head = nullptr;
            // Retail calls operator delete directly; ~FmtStr on the emptied subtree does nothing.
            delete Subtree;
        }
    }
    else if (TypeTag == FmtStr::TagNavMarker)
    {
        // No null test, no store afterwards.
        delete Marker;
    }
}

FmtStr::Val const& FmtStr::Val::operator=(const Val& Other)
{
    // FUN_0630d6c0. Next, TypeTag and Flags are copied shallowly; 0x21/0x4E payloads get a
    // fresh deep copy, as CopyFmtStrChain does. No self-assignment guard, as retail.
    Next = Other.Next;
    Value = Other.Value; // the raw payload dword, whatever the tag
    TypeTag = Other.TypeTag;
    Flags = Other.Flags;

    if (TypeTag == FmtStr::TagFmtStr)
    {
        // VC6 operator new returns null on failure (hence nothrow); a null subtree skips the
        // copy.
        FmtStr* const NewSub = new (std::nothrow) FmtStr();
        if (NewSub != nullptr)
            *NewSub = *Other.Subtree;
        Subtree = NewSub;
    }
    else if (TypeTag == FmtStr::TagNavMarker)
    {
        // The new block is not null-tested, as retail.
        NavMarker* const NewBlock = new (std::nothrow) NavMarker;
        *NewBlock = *Other.Marker;
        Marker = NewBlock;
    }
    return *this;
}

bool FmtStr::Val::operator==(const Val& Other) const
{
    // FUN_0630d770
    if (TypeTag != Other.TypeTag)
    {
        return false;
    }
    if (TypeTag == FmtStr::TagNavMarker)
    {
        // Dwords 0..1 compare as integers, dwords 2..4 (presumably the position) with a float
        // epsilon. Neither payload is null-tested.
        unsigned int const* ThisData = Marker->Data;
        unsigned int const* OtherData = Other.Marker->Data;
        if (ThisData[0] != OtherData[0] || ThisData[1] != OtherData[1])
        {
            return false;
        }
        for (int i = 2; i < 5; i++)
        {
            // x87: the square stays unrounded against 0.01f; NaN is unequal. The dwords hold
            // float bit patterns.
            float FA = *(float const*)&ThisData[i];
            float FB = *(float const*)&OtherData[i];
            double const Delta = (double)FA - (double)FB;
            double const Diff = Delta * Delta;
            if (!(Diff <= 0.01f))
            {
                return false;
            }
        }
        return true;
    }
    if (TypeTag == FmtStr::TagFmtStr)
    {
        // MadLibId first, then both child lists. Neither subtree is null-tested.
        FmtStr const* ThisSub = Subtree;
        FmtStr const* OtherSub = Other.Subtree;
        if (ThisSub->MadLibId != OtherSub->MadLibId)
        {
            return false;
        }
        Val* A = ThisSub->Head;
        Val* B = OtherSub->Head;
        while (A != nullptr && B != nullptr)
        {
            if (!(*A == *B))
            {
                return false;
            }
            A = A->Next;
            B = B->Next;
        }
        return (A == nullptr && B == nullptr);
    }
    // Default: compare the values themselves
    return Value == Other.Value;
}

void* FmtStr::Val::operator new(size_t Size)
{
    // FUN_062fd560
    return malloc(Size);
}

void FmtStr::Val::operator delete(void* Ptr)
{
    // FUN_062fd570
    free(Ptr);
}

int FmtStr::Val::InitFromVoid(const void* Src)
{
    // FUN_0630d5a0: deserialize a Val from raw bytes.
    const unsigned char* P = (const unsigned char*)Src;
    unsigned short Tag = *(const unsigned short*)P;
    TypeTag = Tag;
    Flags = P[2];

    if (Tag == FmtStr::TagFmtStr)
    {
        // A nested subtree: the bytes unflatten consumed plus this node's 3-byte header.
        // A failed allocation is not tested: retail unflattens into the null subtree and faults;
        // so does the port.
        FmtStr* Sub = new (std::nothrow) FmtStr();
        Subtree = Sub;
        int const Consumed = Sub->unflatten((void*)(P + 3), 0);
        return Consumed + 3;
    }
    if (Tag == FmtStr::TagNavMarker)
    {
        // Data block: 5 dwords, filled with no null test.
        NavMarker* Block = new (std::nothrow) NavMarker;
        const unsigned int* SrcBlock = (const unsigned int*)(P + 3);
        for (int i = 0; i < 5; i++)
        {
            Block->Data[i] = SrcBlock[i];
        }
        Marker = Block;
        return 0x17;
    }
    // Simple value: 4 bytes
    Value = *(unsigned int const*)(P + 3);
    return 7;
}

unsigned int FmtStr::Val::flatten(void* Dest, unsigned int MaxSize) const
{
    // FUN_0630d460: serialize this node.
    unsigned char* P = (unsigned char*)Dest;
    *(unsigned short*)P = TypeTag;
    P[2] = Flags;

    if (TypeTag == FmtStr::TagFmtStr)
    {
        // A 9-byte header (tag, flags, the subtree's MadLibId, child count), then the children;
        // the result is the bytes written. The subtree is not null-tested.
        FmtStr const* Sub = Subtree;
        *(unsigned int*)(P + 3) = Sub->MadLibId;
        unsigned int NodeCount = 0;
        for (Val* V = Sub->Head; V != nullptr; V = V->Next)
        {
            NodeCount++;
        }
        unsigned short ChildCount = (unsigned short)NodeCount;
        *(unsigned short*)(P + 7) = ChildCount;
        // Recursively flatten each child starting at P+9
        unsigned char* Cur = P + 9;
        unsigned int Remaining = MaxSize - 6;
        for (Val* V = Sub->Head; V != nullptr; V = V->Next)
        {
            unsigned int Written = V->flatten(Cur, Remaining);
            Cur += Written;
            Remaining -= Written;
        }
        return (unsigned int)(Cur - P);
    }
    if (TypeTag == FmtStr::TagNavMarker)
    {
        // Copy 5 dwords from Data block
        unsigned int const* SrcBlock = Marker->Data;
        unsigned int* DestBlock = (unsigned int*)(P + 3);
        for (int i = 0; i < 5; i++)
        {
            DestBlock[i] = SrcBlock[i];
        }
        return 0x17;
    }
    // Simple value: 4 bytes
    *(unsigned int*)(P + 3) = Value;
    return 7;
}

unsigned int FmtStr::Val::get_flattened_size() const
{
    // FUN_06263a20
    if (TypeTag != FmtStr::TagFmtStr)
    {
        if (TypeTag == FmtStr::TagNavMarker)
        {
            return 0x17;
        }
        return 7;
    }
    // A nested subtree, not null-tested.
    unsigned int Total = 6;
    for (Val* V = Subtree->Head; V != nullptr; V = V->Next)
    {
        Total += V->get_flattened_size();
    }
    return Total + 3;
}

namespace
{
    // The tail link every append_* inlines: walk to the last node and store the new one there,
    // or into Head when the list is empty.
    void LinkFmtStrTail(FmtStr& Str, FmtStr::Val* Node)
    {
        FmtStr::Val** Link = &Str.Head;
        while (*Link != nullptr)
            Link = &(*Link)->Next;
        *Link = Node;
    }

    // The simple append_* bodies (FUN_0630d980 system ... FUN_0630dd50 loot): a malloc'd node;
    // when that fails nothing happens (the counter does not move either). Otherwise Flags = the
    // kind's counter (post-incremented), and the node goes to the tail.
    void AppendValueNode(FmtStr& Str, unsigned short Tag, unsigned int Value, unsigned char& Counter)
    {
        FmtStr::Val* const Node = (FmtStr::Val*)malloc(sizeof(FmtStr::Val));
        if (Node == nullptr)
            return;
        Node->Flags = Counter;
        ++Counter;
        Node->Value = Value;
        Node->Next = nullptr;
        Node->TypeTag = Tag;
        LinkFmtStrTail(Str, Node);
    }

    // FUN_0630de20 (operator= and the (id, list) ctor): free Dest's chain, then copy Src node by
    // node. Tag 0x4E gets a fresh block filled without a null test, tag 0x21 a new FmtStr (null
    // on failure, else operator=); the source payload is not null-tested either. Retail quirk
    // kept: a failed node malloc stores 0 into the current link, and the next node's link is
    // then taken from that null node.
    void CopyFmtStrChain(FmtStr& Dest, FmtStr::Val const* Src)
    {
        Dest.destroy();
        FmtStr::Val** Link = &Dest.Head;
        for (FmtStr::Val const* Cur = Src; Cur != nullptr; Cur = Cur->Next)
        {
            FmtStr::Val* const Node = (FmtStr::Val*)malloc(sizeof(FmtStr::Val));
            if (Node != nullptr)
            {
                Node->Next = Cur->Next;
                Node->Value = Cur->Value; // the raw payload slot, whatever the tag
                Node->TypeTag = Cur->TypeTag;
                Node->Flags = Cur->Flags;
                if (Node->TypeTag == FmtStr::TagFmtStr)
                {
                    // A null subtree (VC6 new, hence nothrow) skips the copy.
                    FmtStr* const Sub = new (std::nothrow) FmtStr();
                    if (Sub != nullptr)
                        *Sub = *Cur->Subtree;
                    Node->Subtree = Sub;
                }
                else if (Node->TypeTag == FmtStr::TagNavMarker)
                {
                    // Filled without a null test.
                    FmtStr::NavMarker* const Block = new (std::nothrow) FmtStr::NavMarker;
                    *Block = *Cur->Marker;
                    Node->Marker = Block;
                }
            }
            *Link = Node;
            Link = (FmtStr::Val**)Node; // &Node->Next (Next is at +0); null after a failed malloc
        }
    }
}

// FmtStr::FmtStr() is inline in common_text.h and not exported -- see the note on the struct.

FmtStr::FmtStr(const FmtStr& Other)
{
    // FUN_0630e1f0. Retail zeroes only the head pointer and then runs operator=, which copies
    // the id and all fourteen counters.
    Head = nullptr;
    MadLibId = 0;
    memset(ValueIndex, 0, sizeof(ValueIndex));
    *this = Other;
}

FmtStr::FmtStr(unsigned int InMadLibId, const Val* ValueList)
{
    // FUN_0630e2d0. The mad-lib id and the head of a node chain to deep-copy.
    Head = nullptr;
    memset(ValueIndex, 0, sizeof(ValueIndex));
    MadLibId = InMadLibId;
    CopyFmtStrChain(*this, ValueList);
}

FmtStr::~FmtStr()
{
    // FUN_0630e210
    destroy();
}

const FmtStr& FmtStr::operator=(const FmtStr& Other)
{
    // FUN_0630dda0: the id, a deep copy of the chain, then all fourteen counters. Retail
    // returns a const reference. Deviation: retail has no self test (self-assignment frees the
    // chain it is about to copy); the port keeps the chain.
    if (this != &Other)
    {
        MadLibId = Other.MadLibId;
        CopyFmtStrChain(*this, Other.Head);
        memcpy(ValueIndex, Other.ValueIndex, sizeof(ValueIndex));
    }
    return *this;
}

bool FmtStr::operator==(const FmtStr& Other) const
{
    // FUN_0630e060: MadLibId first, then the node lists.
    if (MadLibId != Other.MadLibId)
    {
        return false;
    }
    Val* A = Head;
    Val* B = Other.Head;
    while (A != nullptr && B != nullptr)
    {
        if (!(*A == *B))
        {
            return false;
        }
        A = A->Next;
        B = B->Next;
    }
    return (A == nullptr && B == nullptr);
}

void FmtStr::destroy()
{
    // FUN_0630e270. Only the node chain goes; the counters and the mad-lib id stay.
    Val* Current = Head;
    while (Current != nullptr)
    {
        Val* Next = Current->Next;
        if (Current->TypeTag == FmtStr::TagFmtStr && Current->Subtree != nullptr)
        {
            FmtStr* SubTree = Current->Subtree;
            SubTree->destroy();
            delete SubTree;
        }
        else if (Current->TypeTag == FmtStr::TagNavMarker)
        {
            delete Current->Marker; // no null test
        }
        free(Current);
        Current = Next;
    }
    Head = nullptr;
}

void FmtStr::begin_mad_lib(unsigned int Id)
{
    // FUN_06309b30 (ICF-folded with a dozen setters).
    MadLibId = Id;
}

void FmtStr::end_mad_lib()
{
    // FUN_062a8920, the DLL's shared empty function.
}

void FmtStr::append(Val* Node)
{
    // FUN_0630d850. Takes ownership of an already-built node; a null node is ignored.
    // Node->Next is not touched, so a chain is appended whole.
    if (Node == nullptr)
        return;
    LinkFmtStrTail(*this, Node);
}

void FmtStr::append_fmt_str(const FmtStr& Other)
{
    // FUN_0630dc50: tag 0x21, a nested subtree, with its own counter at +0x13
    // (ValueIndex[KindFmtStr]). A failed malloc does nothing; a null subtree (VC6 new) still
    // appends the node with a null payload.

    Val* const Node = (Val*)malloc(sizeof(Val));
    if (Node == nullptr)
        return;
    FmtStr* const Sub = new (std::nothrow) FmtStr();
    if (Sub != nullptr)
        *Sub = Other;
    Node->Flags = ValueIndex[KindFmtStr];
    ++ValueIndex[KindFmtStr];
    Node->Subtree = Sub;
    Node->Next = nullptr;
    Node->TypeTag = FmtStr::TagFmtStr;
    LinkFmtStrTail(*this, Node);
}

void FmtStr::append_nav_marker(const NavMarker& Marker)
{
    // FUN_0630d8e0: tag 0x4E, counter +0x08. A failed malloc does nothing; the payload block is
    // null-tested here (on failure the node keeps a null payload).
    Val* const Node = (Val*)malloc(sizeof(Val));
    if (Node == nullptr)
        return;
    NavMarker* const Block = new (std::nothrow) NavMarker;
    if (Block != nullptr)
        *Block = Marker;
    Node->Flags = ValueIndex[KindNavMarker];
    ++ValueIndex[KindNavMarker];
    Node->Next = nullptr;
    Node->Marker = Block;
    Node->TypeTag = FmtStr::TagNavMarker;
    LinkFmtStrTail(*this, Node);
}

int FmtStr::append_void(void* Data)
{
    // FUN_0630d870: a node straight from a serialized Val, so the flags come from the source
    // and no counter moves. Returns the bytes consumed. Retail quirk kept: a failed malloc
    // returns the Data pointer itself.
    Val* const Node = (Val*)malloc(sizeof(Val));
    if (Node == nullptr)
        return (int)(std::uintptr_t)Data;
    Node->Next = nullptr;
    int const Consumed = Node->InitFromVoid(Data);
    LinkFmtStrTail(*this, Node);
    return Consumed;
}

void FmtStr::append_system(const unsigned int& Id)
{
    // FUN_0630d980: tag 0x53 'S', counter +0x09.
    AppendValueNode(*this, FmtStr::TagSystem, Id, ValueIndex[KindSystem]);
}

void FmtStr::append_base(const unsigned int& Id)
{
    // FUN_0630d9d0: tag 0x42 'B', counter +0x0A. An id by reference, not a char*.
    AppendValueNode(*this, FmtStr::TagBase, Id, ValueIndex[KindBase]);
}

void FmtStr::append_string(unsigned int Id)
{
    // FUN_0630da20: tag 0x73 's', counter +0x0B. An IDS id, not a char*.
    AppendValueNode(*this, FmtStr::TagString, Id, ValueIndex[KindString]);
}

void FmtStr::append_good(const unsigned int& Id)
{
    // FUN_0630da70: tag 0x47 'G', counter +0x0C.
    AppendValueNode(*this, FmtStr::TagGood, Id, ValueIndex[KindGood]);
}

void FmtStr::append_int(unsigned int Value)
{
    // FUN_0630dac0: tag 0x64 'd', counter +0x0E.
    AppendValueNode(*this, FmtStr::TagInt, Value, ValueIndex[KindInt]);
}

void FmtStr::append_rep_instance(const int& Id)
{
    // FUN_0630db10: tag 0x52 'R', counter +0x0F. Signed, unlike its group sibling.
    AppendValueNode(*this, FmtStr::TagRepInstance, Id, ValueIndex[KindRepInstance]);
}

void FmtStr::append_rep_group(const unsigned int& Id)
{
    // FUN_0630db60: tag 0x46 'F', counter +0x10.
    AppendValueNode(*this, FmtStr::TagRepGroup, Id, ValueIndex[KindRepGroup]);
}

void FmtStr::append_zone_id(const unsigned int& Id)
{
    // FUN_0630dbb0: tag 0x5A 'Z', counter +0x11.
    AppendValueNode(*this, FmtStr::TagZoneId, Id, ValueIndex[KindZoneId]);
}

void FmtStr::append_spaceobj_id(const unsigned int& Id)
{
    // FUN_0630dc00: tag 0x4F 'O', counter +0x12.
    AppendValueNode(*this, FmtStr::TagSpaceObjId, Id, ValueIndex[KindSpaceObjId]);
}

void FmtStr::append_installation(int Id)
{
    // FUN_0630dd00: tag 0x49 'I', counter +0x14.
    AppendValueNode(*this, FmtStr::TagInstallation, Id, ValueIndex[KindInstallation]);
}

void FmtStr::append_loot(int Id)
{
    // FUN_0630dd50: tag 0x4C 'L', counter +0x15.
    AppendValueNode(*this, FmtStr::TagLoot, Id, ValueIndex[KindLoot]);
}

int FmtStr::flatten(void* Dest, unsigned int MaxSize) const
{
    // FUN_0630df60. The wire header is {mad-lib id : dword, node count : word}.
    unsigned char* const Base = (unsigned char*)Dest;
    *(unsigned int*)Base = MadLibId;

    unsigned short NodeCount = 0;
    for (Val const* V = Head; V != nullptr; V = V->Next)
        NodeCount += 1;
    *(unsigned short*)(Base + 4) = NodeCount;

    unsigned char* Cursor = Base + 6;
    unsigned int Remaining = MaxSize - 6;
    for (Val const* V = Head; V != nullptr; V = V->Next)
    {
        *(unsigned short*)Cursor = V->TypeTag;
        *(Cursor + 2) = V->Flags;

        unsigned int Written;
        if (V->TypeTag == FmtStr::TagFmtStr)
        {
            // The nested flatten gets the same (not reduced) budget; its byte count plus the
            // 3-byte header is this node's size. The subtree is not null-tested.
            Written = V->Subtree->flatten(Cursor + 3, Remaining) + 3;
        }
        else if (V->TypeTag == FmtStr::TagNavMarker)
        {
            memcpy(Cursor + 3, V->Marker, sizeof(NavMarker));
            Written = 0x17;
        }
        else
        {
            *(unsigned int*)(Cursor + 3) = V->Value;
            Written = 7;
        }
        Cursor += Written;
        Remaining -= Written;
    }
    return (int)(Cursor - Base);
}

int FmtStr::unflatten(void* Src, unsigned int Size)
{
    // FUN_0630e010, the inverse of flatten. Returns the bytes consumed. The whole object (head,
    // id and all fourteen counters) is zeroed before the null test, and the wire format never
    // carries the counters, so they end up zero.
    (void)Size;
    destroy();
    MadLibId = 0;
    memset(ValueIndex, 0, sizeof(ValueIndex));
    if (Src == nullptr)
        return 0;

    unsigned char const* const Base = (unsigned char const*)Src;
    MadLibId = *(unsigned int const*)Base;
    unsigned short const NodeCount = *(unsigned short const*)(Base + 4);

    // Retail quirk kept: a failed malloc stores 0 and advances the cursor by the previous step
    // (initially the Src pointer itself); the next node's link store then goes through null.
    unsigned char const* Cursor = Base + 6;
    Val** Link = &Head;
    std::uintptr_t Step = (std::uintptr_t)Src;
    for (unsigned int Index = 0; Index < NodeCount; ++Index)
    {
        Val* const Node = (Val*)malloc(sizeof(Val));
        if (Node != nullptr)
        {
            Node->Next = nullptr;
            Step = (std::uintptr_t)Node->InitFromVoid(Cursor);
        }
        Cursor += Step;
        *Link = Node;
        Link = (Val**)Node; // &Node->Next (Next is at +0)
    }
    return (int)(Cursor - Base);
}
