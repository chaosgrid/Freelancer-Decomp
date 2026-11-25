// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpclocal's container serializer. The layouts the code walks are documented at the top of
// rpc_containers.h.
//
// Ownership, as retail has it:
//   * the equipment handle (payload+0x00 for the four EQUIPMENT/CARGO types) owns its
//     list header, its dummy node, every node, every 0x18 record and every hardpoint
//     string that hangs off a record;
//   * the collision-group list header IS the payload, and owns its dummy node and every
//     node;
//   * the EquipDescList and std::vector<EquipDesc> the receivers build are stack
//     temporaries that die with the call.
//
// Retail's per-type packet classes do the allocation and the teardown in their own
// constructor/destructor, and so does this port (rpc_packet_registry.cpp); what is left
// here are the list and handle primitives those bodies and the codecs share.

#include "rpc_containers.h"
#include "rpc_payloads.h"

// Retail imports COMMON.DLL EquipDesc::is_internal (.rdata:06db30d8) and ::make_internal
// (.rdata:06db3070) and uses them as the hardpoint gate on both sides of the wire.
#include "common_goods.h"
// The CREATESHIP / CREATESOLAR codec's other COMMON imports (IAT slots from the call bytes):
// [06db3108] Archetype::LargeIDToSmallID, [06db3000] SmallIDToLargeID, [06db3110]
// GetArchMaxHitPts, [06db305c] SinglePlayer, and the data import [06db3100] SUBOBJ_ID_NONE
// (the slot holds the ADDRESS of the ushort).
#include "common_misc.h"
#include "rpc_packet_classes.h"

#include <cstddef>
#include <cstring>
#include <map>
#include <new>
#include <string>

// ===========================================================================
// Small helpers. Retail calls MSVCRT's VC6 operator new, which returns NULL on failure, and
// FUN_06db1dc2 (operator delete) directly. The port uses std::nothrow and tests the result
// only where the retail site does: a new-expression with an initializer is tested, a bare
// operator new call is not (a failure faults on the next store, in retail and here alike).
// ===========================================================================

// Retail's inline strlen (06d8d04f), with no null test in front.
static unsigned int RpcContainerStringLength(const char* Text) {
    unsigned int Length = 0;
    while (Text[Length] != '\0') {
        ++Length;
    }
    return Length;
}

// Untested new(SizeWithNul), then retail's byte loop (06d8d070, 06d91503), bounded by the
// terminator, not by the size it allocated.
static char* RpcContainerCopyString(const char* Text, unsigned int SizeWithNul) {
    char* Block = static_cast<char*>(::operator new(SizeWithNul, std::nothrow));
    char* Out = Block;
    char Byte;
    do {
        Byte = *Text;
        ++Text;
        *Out = Byte;
        ++Out;
    } while (Byte != '\0');
    return Block;
}

static void RpcContainerInitRecord(FRpcEquipWireRecord* Record) {
    // Retail (06d8c78d, 06d8cffe, 06d9149f) zeroes all fields first, in this order.
    Record->ArchId = 0;
    Record->Count = 0;
    Record->Status = 0.0f;
    Record->Id = 0;
    Record->bEquipped = 0;
    Record->bMission = 0;
    Record->HardpointSize = 0;
    Record->Pad_0x12[0] = 0;
    Record->Pad_0x12[1] = 0;
    Record->Hardpoint = nullptr;
}

static void RpcContainerFreeRecord(FRpcEquipWireRecord* Record) {
    // FUN_06d91340 at 06d9135b. The HardpointSize field is NOT consulted.
    if (Record == nullptr) {
        return;
    }
    if (Record->Hardpoint != nullptr) {
        ::operator delete(Record->Hardpoint);
    }
    ::operator delete(Record);
}

// ===========================================================================
// FUN_06d913e0
// ===========================================================================
FRpcEquipWireListHeader* FRpcEquipWireHandle::GetList() const {
    return List;
}

// ===========================================================================
// FUN_06d91580 -- push_back. When the handle is still null it first builds the list (0xc
// header, self-linked 0xc dummy node), then links a new 0xc node in front of the dummy.
// ===========================================================================
// The "build the list" half. Retail inlines it twice, identically: push_back 06d9159f and the
// deep copy 06d91426. The header allocation is tested and a null header is stored into the
// handle all the same before the caller bails out on it; the dummy's allocation is untested.
static FRpcEquipWireListHeader* RpcEquipHandleBuildList(FRpcEquipWireHandle* Handle) {
    FRpcEquipWireListHeader* Header =
        static_cast<FRpcEquipWireListHeader*>(::operator new(sizeof(FRpcEquipWireListHeader),
                                                             std::nothrow));
    if (Header != nullptr) {
        // +0x00 is the list's (empty) allocator object: retail copies one byte of an
        // allocator temporary that holds no value. +0x01..+0x03 are never written.
        Header->Allocator = 0;
        Header->Pad_0x01[0] = 0;
        Header->Pad_0x01[1] = 0;
        Header->Pad_0x01[2] = 0;
        FRpcEquipWireNode* Dummy =
            static_cast<FRpcEquipWireNode*>(::operator new(sizeof(FRpcEquipWireNode),
                                                           std::nothrow));
        Dummy->Next = Dummy;
        Dummy->Prev = Dummy;
        Dummy->Value = nullptr;   // never written by retail; the dummy's value is never read
        Header->Head = Dummy;
        Header->Size = 0;
    }
    Handle->List = Header;
    return Header;
}

// The common half of push_back (06d915ec), which the deep copy also inlines (06d9150d). The
// node's allocation is untested.
static void RpcEquipHandleLinkBack(FRpcEquipWireListHeader* Header, FRpcEquipWireRecord* Record) {
    FRpcEquipWireNode* Dummy = Header->Head;
    FRpcEquipWireNode* Tail = Dummy->Prev;
    FRpcEquipWireNode* Node =
        static_cast<FRpcEquipWireNode*>(::operator new(sizeof(FRpcEquipWireNode), std::nothrow));
    Node->Next = Dummy;
    // Retail 06d915ff: when the dummy's Prev is still null the new node becomes its own
    // predecessor. The dummy is always self-linked, so this never fires; reproduced anyway.
    if (Tail == nullptr) {
        Tail = Node;
    }
    Node->Prev = Tail;
    Dummy->Prev = Node;
    Node->Prev->Next = Node;
    Node->Value = Record;
    ++Header->Size;
}

void FRpcEquipWireHandle::PushBack(FRpcEquipWireRecord* Record) {
    if (List == nullptr && RpcEquipHandleBuildList(this) == nullptr) {
        return;
    }
    RpcEquipHandleLinkBack(List, Record);
}

// ===========================================================================
// FUN_06d91340 -- the handle's destructor
// ===========================================================================
void FRpcEquipWireHandle::Destroy() {
    // The handle itself is not tested.
    if (List == nullptr) {
        return;
    }
    // Phase 1 (06d91358): free the records, leaving the chain intact. The loop bound re-reads
    // the handle's dummy every round.
    for (FRpcEquipWireNode* Node = List->Head->Next; Node != List->Head;
         Node = Node->Next) {
        RpcContainerFreeRecord(Node->Value);
    }
    // Phase 2 (06d91381): the handle is re-read and re-tested, then every node is
    // unlinked and freed with the size decremented as retail does, then the dummy.
    FRpcEquipWireListHeader* Header = List;
    if (Header == nullptr) {
        return;
    }
    FRpcEquipWireNode* Dummy = Header->Head;
    FRpcEquipWireNode* Node = Dummy->Next;
    while (Node != Dummy) {
        FRpcEquipWireNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        ::operator delete(Node);
        --Header->Size;
        Node = Next;
    }
    ::operator delete(Dummy);
    Header->Head = nullptr;
    Header->Size = 0;
    ::operator delete(Header);
    // Deviation: retail leaves the caller's handle pointing at the freed header. The port
    // nulls it because RpcCreateObjectHeaderRelease runs on the receiver statics, whose handle
    // is filled again by the next packet. Not a guard: nothing is skipped.
    List = nullptr;
}

// Retail runs FUN_06d91340 on every handle object on both the normal and the unwind path.
__declspec(dllexport) FRpcEquipWireHandle::~FRpcEquipWireHandle() {
    Destroy();
}

// ===========================================================================
// FUN_06d913f0 -- deep copy, `this` = the destination handle, argument = the source.
//
// Retail bails out at once when the SOURCE has no list, lazily builds the destination list,
// then per source node allocates a fresh 0x18 record (a null one skips the node), copies the
// scalar fields -- including the byte at +0x0f -- and, when HardpointSize is non-zero, copies
// the name up to its NUL.
// ===========================================================================
void FRpcEquipWireHandle::Copy(const FRpcEquipWireHandle* Source) {
    if (Source->List == nullptr) {
        return;
    }
    if (List == nullptr && RpcEquipHandleBuildList(this) == nullptr) {
        return;
    }
    // The loop bound re-reads the source's dummy every round.
    for (const FRpcEquipWireNode* Node = Source->List->Head->Next; Node != Source->List->Head;
         Node = Node->Next) {
        const FRpcEquipWireRecord* From = Node->Value;
        FRpcEquipWireRecord* To =
            static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                             std::nothrow));
        if (To == nullptr) {
            continue;
        }
        RpcContainerInitRecord(To);
        To->HardpointSize = From->HardpointSize;
        To->ArchId = From->ArchId;
        To->Status = From->Status;
        To->Count = From->Count;
        To->Id = From->Id;
        To->bEquipped = From->bEquipped;
        To->bMission = From->bMission;
        if (To->HardpointSize != 0) {
            To->Hardpoint = RpcContainerCopyString(From->Hardpoint, To->HardpointSize);
        }
        RpcEquipHandleLinkBack(List, To);
    }
}

// ===========================================================================
// FUN_06d8fa90
// ===========================================================================
// The payload's four bytes hold a POINTER to the handle, not the handle itself: retail
// allocates the handle with operator new(4), stores its address at packet+0x20 and hands THAT
// address to FUN_06d913f0 as the destination. The receiver reads the same dword back and
// passes it as the SOURCE (06d95d20). Both indirections are reproduced.
void FRpcEquipListPacket::AssignEquipHandle(const FRpcEquipWireHandle* Source) {
    if (EquipHandle == nullptr) {
        FRpcEquipWireHandle* Handle =
            static_cast<FRpcEquipWireHandle*>(::operator new(sizeof(FRpcEquipWireHandle),
                                                             std::nothrow));
        if (Handle != nullptr) {
            Handle->List = nullptr;
        }
        EquipHandle = Handle;
    }
    if (EquipHandle != nullptr) {
        EquipHandle->Copy(Source);
    }
}

// ===========================================================================
// FUN_06d95d20 -- the inverse.
// ===========================================================================
void FRpcEquipListPacket::ExtractEquipHandle(FRpcEquipWireHandle* Dest) const {
    if (EquipHandle != nullptr) {
        Dest->Copy(EquipHandle);
    }
}

__declspec(dllexport) void RpcPacketEquipExtract(const FPacketBase* Packet, FRpcEquipWireHandle* Dest) {
    static_cast<const FRpcEquipListPacket*>(Packet)->ExtractEquipHandle(Dest);
}

// ===========================================================================
// FUN_06d8c750 -- std::vector<EquipDesc> -> handle. With bAll clear only the UNequipped items
// travel (the cargo half).
// ===========================================================================
void RpcEquipHandleFromVector(FRpcEquipWireHandle* Handle,
                              const EquipDescVector* Source,
                              bool bAll) {
    const EquipDesc* Item = Source->Begin;
    const EquipDesc* End = Source->End;

    for (; Item != End; ++Item) {
        if (!bAll && Item->bEquipped) {
            continue;
        }
        // The null test skips only the zeroing; the stores below then go through the null
        // pointer exactly as retail's do.
        FRpcEquipWireRecord* Record =
            static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                             std::nothrow));
        if (Record != nullptr) {
            RpcContainerInitRecord(Record);
        }
        Record->ArchId = Item->ArchId;
        Record->Count = Item->Count;
        Record->Status = Item->Health;
        Record->Id = Item->Id;
        Record->bEquipped = Item->bEquipped;

        // EquipDesc::is_internal() (06d8c7cd) is the ONLY gate: a non-internal hardpoint
        // travels even when it is the empty string, as size 1 and a lone NUL. The size is
        // stored before the allocation it sizes.
        if (!Item->is_internal()) {
            Record->HardpointSize =
                (unsigned short)(RpcContainerStringLength(Item->Hardpoint) + 1u);
            Record->Hardpoint = RpcContainerCopyString(Item->Hardpoint, Record->HardpointSize);
        }
        Handle->PushBack(Record);
    }
}

// ===========================================================================
// FUN_06d84d60 -- handle -> std::vector<EquipDesc>
// ===========================================================================
void RpcEquipDescVectorRelease(EquipDescVector* Vector) {
    // Retail 06d84dca: _Destroy(first, last) then operator delete(first). EquipDesc owns no
    // heap (its hardpoint is an interned StringAlloc string).
    for (EquipDesc* It = Vector->Begin; It != Vector->End; ++It) {
        It->~EquipDesc();
    }
    ::operator delete(Vector->Begin);
    Vector->Begin = nullptr;
    Vector->End = nullptr;
    Vector->Cap = nullptr;
}

__declspec(dllexport) void RpcEquipVectorFromHandle(EquipDescVector* Dest,
                              const FRpcEquipWireHandle* Source) {
    const FRpcEquipWireListHeader* Header = Source->GetList();
    if (Header == nullptr) {
        return;
    }

    // Retail reserves Size elements (moving any existing ones in). Every caller hands in a
    // fresh, empty vector, so the port just replaces the block; on an empty vector the two
    // are the same. The allocation is untested.
    const unsigned int Wanted = Header->Size;
    if (Wanted != 0) {
        RpcEquipDescVectorRelease(Dest);
        EquipDesc* Block = static_cast<EquipDesc*>(
            ::operator new(sizeof(EquipDesc) * Wanted, std::nothrow));
        Dest->Begin = Block;
        Dest->End = Block;
        Dest->Cap = Block + Wanted;
    }

    const FRpcEquipWireNode* Dummy = Header->Head;
    for (const FRpcEquipWireNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
        const FRpcEquipWireRecord* Record = Node->Value;
        if (Record == nullptr) {
            continue;
        }
        // Retail inserts a default EquipDesc at end and runs the setters on it in this order;
        // the reserved block never reallocates, so constructing in place is the same object.
        EquipDesc* Slot = new (Dest->End) EquipDesc();
        ++Dest->End;
        Slot->set_arch_id(Record->ArchId);
        Slot->set_count(Record->Count);
        Slot->set_status(Record->Status);
        Slot->set_id(Record->Id);
        Slot->set_equipped(Record->bEquipped);
        // Interning is not cosmetic: set_hardpoint just stores the pointer, and the RECORD's
        // copy is freed with the packet.
        if (Record->HardpointSize != 0) {
            CacheString Hp;
            Hp.Value = StringAlloc(Record->Hardpoint, false);
            Slot->set_hardpoint(Hp);
        } else {
            Slot->make_internal();
        }
    }
}

// ===========================================================================
// FUN_06d87ec0 / FUN_06d880a0 -- handle -> equipment list
//
// Retail builds its EquipDescList with COMMON.DLL std::list<EquipDesc>::_Buynode; the element
// is a default EquipDesc filled through the five setters in this order, then set_hardpoint
// (interned) or make_internal. Appending is a raw tail insert (06d87f56), NOT
// add_equipment_item -- retail does no combinable merging here and never touches NextId.
// ===========================================================================
__declspec(dllexport) void RpcEquipListFromHandle(EquipDescList& Dest, const FRpcEquipWireHandle* Source) {
    const FRpcEquipWireListHeader* Header = Source->GetList();
    if (Header == nullptr) {
        // A null list means an empty EquipDescList, not a failure.
        return;
    }

    const FRpcEquipWireNode* Dummy = Header->Head;
    for (const FRpcEquipWireNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
        // The record is used with no test.
        const FRpcEquipWireRecord* Record = Node->Value;
        EquipDesc Desc;
        Desc.set_arch_id(Record->ArchId);
        Desc.set_count(Record->Count);
        Desc.set_status(Record->Status);
        Desc.set_id(Record->Id);
        Desc.set_equipped(Record->bEquipped);
        // A zero length is the internal case. Interning is not cosmetic: set_hardpoint only
        // stores the pointer, and the RECORD's copy dies with the packet.
        if (Record->HardpointSize != 0) {
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
            Desc.set_hardpoint(Hardpoint);
        } else {
            Desc.make_internal();
        }
        Dest.Items.push_back(Desc);
    }
}

// ===========================================================================
// IServerImpl::ReqEquipment 0x06d8cff0 -- equipment list -> handle
//
// Field map (EquipDesc -> wire): +0x04 ArchId -> +0x00, +0x14 Count -> +0x04, +0x10 Health ->
// +0x08, +0x02 Id -> +0x0c, +0x0c bEquipped -> +0x0e; internal equipment (is_internal) sends no
// hardpoint, else strlen + 1 into +0x10 and the copy into +0x14.
//
// Retail leaves wire+0x0f (EquipDesc::bMission) at zero: REQEQUIPMENT/REQCARGO do not carry it;
// only ch2 0x03 CHARACTERINFO does. Reproduced as found.
// ===========================================================================
void RpcEquipHandleFromList(FRpcEquipWireHandle* Handle, const EquipDescList& Source) {
    for (std::list<EquipDesc>::const_iterator It = Source.Items.begin();
         It != Source.Items.end(); ++It) {
        // The null test skips only the zeroing; the stores below then go through the null
        // pointer exactly as retail's do.
        FRpcEquipWireRecord* Record =
            static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                             std::nothrow));
        if (Record != nullptr) {
            RpcContainerInitRecord(Record);
        }
        Record->ArchId = It->ArchId;
        Record->Count = It->Count;
        Record->Status = It->Health;
        Record->Id = It->Id;
        Record->bEquipped = It->bEquipped;

        // Retail 06d8d040: the ONLY gate is is_internal(), so an empty but non-internal
        // hardpoint travels as size 1 and a lone terminator. No null test on the hardpoint
        // (a non-internal one always points at an interned string). The size is written
        // BEFORE the allocation it sizes.
        if (!It->is_internal()) {
            Record->HardpointSize =
                (unsigned short)(RpcContainerStringLength(It->Hardpoint) + 1u);
            Record->Hardpoint = RpcContainerCopyString(It->Hardpoint, Record->HardpointSize);
        }
        Handle->PushBack(Record);
    }
}

// ===========================================================================
// Collision groups
// ===========================================================================

// FUN_06d8f7d0 (payload+0x00).
__declspec(dllexport) const FRpcCollisionGroupList* RpcPacketCollisionGroups(const FPacketBase* Packet) {
    return &static_cast<const FRpcCollisionGroupPacket*>(Packet)->Wire;
}

// Stand-in for the create-object record constructors' std::list ctor, which builds the dummy
// node (the packet classes build theirs in rpc_packet_registry.cpp, as 0x06d9e8b0 / 0x06da10b0
// do). The dummy's allocation is untested.
void FRpcCollisionGroupList::Init() {
    if (Head != nullptr) {
        return;   // already constructed
    }
    FRpcCollisionGroupNode* Dummy =
        static_cast<FRpcCollisionGroupNode*>(::operator new(sizeof(FRpcCollisionGroupNode),
                                                            std::nothrow));
    Dummy->Next = Dummy;
    Dummy->Prev = Dummy;
    Dummy->Id = 0;   // the dummy's value is never read
    Dummy->HealthBits = 0;
    Allocator = 0;
    Pad_0x01[0] = 0;
    Pad_0x01[1] = 0;
    Pad_0x01[2] = 0;
    Head = Dummy;
    Size = 0;
}

// FUN_06d8f710 -- list assignment, in retail's three phases, from an array of 8-byte
// values. The phases live in RpcCollisionGroupListAssignRange (rpc_containers.h).
void RpcCollisionGroupListAssign(FRpcCollisionGroupList* List,
                                 const FRpcCollisionGroupDescMirror* Items,
                                 unsigned int Count) {
    List->Init();
    RpcCollisionGroupListAssignRange(List, Items, Items + Count);
}

// ===========================================================================
// The CREATELOOT list (ch2 0x28)
// ===========================================================================

// FUN_06d931b0 (payload+0x08).
__declspec(dllexport) const FRpcLootWireList* RpcPacketLootList(const FPacketBase* Packet) {
    return &static_cast<const FPacket_FLPACKET_SERVER_CREATELOOT*>(Packet)->Wire.Items;
}

// Stand-in for the CREATELOOT packet constructor's list constructor; untested allocation.
void FPacket_FLPACKET_SERVER_CREATELOOT::InitLootList() {
    FRpcLootWireList* List = &Wire.Items;
    if (List->Head != nullptr) {
        return;   // already constructed
    }
    FRpcLootWireNode* Dummy =
        static_cast<FRpcLootWireNode*>(::operator new(sizeof(FRpcLootWireNode),
                                                      std::nothrow));
    Dummy->Next = Dummy;
    Dummy->Prev = Dummy;
    unsigned char* Value = reinterpret_cast<unsigned char*>(&Dummy->Value);
    for (unsigned int i = 0; i < (unsigned int)sizeof(FRpcLootWireRecord); ++i) {
        Value[i] = 0;   // the dummy's value is never read
    }
    List->Allocator = 0;
    List->Pad_0x01[0] = 0;
    List->Pad_0x01[1] = 0;
    List->Pad_0x01[2] = 0;
    List->Head = Dummy;
    List->Size = 0;
}

// FUN_06d93160 -- push_back: dummy at packet+0x2c, untested new(0x50), value copied with
// FUN_06d93280, size at packet+0x30.
void FPacket_FLPACKET_SERVER_CREATELOOT::PushBackLoot(const FRpcLootWireRecord& Record) {
    InitLootList();
    FRpcLootWireList* List = &Wire.Items;
    FRpcLootWireNode* Dummy = List->Head;
    FRpcLootWireNode* Tail = Dummy->Prev;
    FRpcLootWireNode* New =
        static_cast<FRpcLootWireNode*>(::operator new(sizeof(FRpcLootWireNode),
                                                      std::nothrow));
    New->Next = Dummy;
    if (Tail == nullptr) {
        // Retail 06d93175 (never fires on a self-linked dummy).
        Tail = New;
    }
    New->Prev = Tail;
    Dummy->Prev = New;
    New->Prev->Next = New;
    New->Value = Record;
    ++List->Size;
}

void FPacket_FLPACKET_SERVER_CREATELOOT::ReleaseLootList() {
    FRpcLootWireList* List = &Wire.Items;
    if (List->Head == nullptr) {
        return;   // never constructed
    }
    FRpcLootWireNode* Dummy = List->Head;
    FRpcLootWireNode* Node = Dummy->Next;
    while (Node != Dummy) {
        FRpcLootWireNode* Next = Node->Next;
        ::operator delete(Node);
        Node = Next;
    }
    ::operator delete(Dummy);
    List->Head = nullptr;
    List->Size = 0;
}

// ===========================================================================
// The reputation relation list (ch2 0x29, 0x2b)
// ===========================================================================

void RpcRelationListPushBack(FRpcRelationList* List, const FRpcRelation& Value) {
    // 06d88e81: the value lives in its own new(8) block, stored through with no test.
    FRpcRelation* Block =
        static_cast<FRpcRelation*>(::operator new(sizeof(FRpcRelation), std::nothrow));
    Block->GroupId = Value.GroupId;
    Block->FeelingBits = Value.FeelingBits;
    RpcRelationListLink(List, Block);
}

void RpcRelationListLink(FRpcRelationList* List, FRpcRelation* Block) {
    // FUN_06d8cad0. The dummy is read untested: every list this reaches was built by its
    // packet's constructor (06d9d30c for 0x29, 06d9c59f for 0x2b). The node's allocation is
    // untested as well.
    FRpcRelationNode* Dummy = List->Head;
    FRpcRelationNode* Tail = Dummy->Prev;
    FRpcRelationNode* New =
        static_cast<FRpcRelationNode*>(::operator new(sizeof(FRpcRelationNode),
                                                      std::nothrow));
    New->Next = Dummy;
    if (Tail == nullptr) {
        Tail = New;
    }
    New->Prev = Tail;
    Dummy->Prev = New;
    New->Prev->Next = New;
    New->Value = Block;
    ++List->Size;
}

// ===========================================================================
// The mission-objective record list (ch2 0x35)
// ===========================================================================

void FRpcMissionObjectiveList::PushBack(FRpcMissionObjectiveRecord* Record) {
    // FUN_06d8fc10: a null record is silently dropped and neither counter moves (retail's
    // own test).
    if (Record == nullptr) {
        return;
    }
    // The dummy is read untested: the packet constructor built it (06d9daad).
    FRpcMissionObjectiveNode* Dummy = Head;
    FRpcMissionObjectiveNode* Tail = Dummy->Prev;
    // Untested allocation.
    FRpcMissionObjectiveNode* New =
        static_cast<FRpcMissionObjectiveNode*>(
            ::operator new(sizeof(FRpcMissionObjectiveNode), std::nothrow));
    New->Next = Dummy;
    if (Tail == nullptr) {
        Tail = New;
    }
    New->Prev = Tail;
    Dummy->Prev = New;
    New->Prev->Next = New;
    New->Value = Record;
    ++Size;
}

__declspec(dllexport) const FRpcMissionObjectiveRecord* RpcMissionObjectiveListAt(
    const FRpcMissionObjectiveList* List, unsigned int Index) {
    // 06d8fc60: the dummy (packet+0x40) is read untested.
    const FRpcMissionObjectiveNode* Dummy = List->Head;
    const FRpcMissionObjectiveNode* Node = Dummy->Next;
    unsigned int At = 0;
    while (Node != Dummy) {
        if (At == Index) {
            return Node->Value;
        }
        Node = Node->Next;
        ++At;
    }
    return nullptr;
}

// ===========================================================================
// The seam between the retail-shaped containers some payloads still keep (the equipment
// handle, the SETCOLLISIONGROUPS / REQCOLLISIONGROUPS collision image) and the Common.dll
// types the shared CEqObjCreateInfo (src/include/server_createinfo.h) really holds. See
// the declarations in rpc_containers.h.
// ===========================================================================

// One CollisionGroupDesc as retail's node copy moves it: two dwords, the first holding the
// ushort id and the struct's two padding bytes.
static void RpcCollisionGroupDescDecode(CollisionGroupDesc& Desc, unsigned int Word0,
                                        unsigned int Word1) {
    Desc.Id = static_cast<unsigned short>(Word0 & 0xffffu);
    Desc.Pad02 = static_cast<unsigned short>(Word0 >> 16);
    std::memcpy(&Desc.Health, &Word1, sizeof(Word1));
}

// 06d8505c: std::list assignment. The receiver's static has been cleared since the previous
// packet, so in practice only the append runs.
__declspec(dllexport) void RpcCollisionGroupListFromWire(std::list<CollisionGroupDesc>& Dest,
                                   const FRpcCollisionGroupList* Source) {
    const FRpcCollisionGroupNode* Dummy = Source->Head;
    const FRpcCollisionGroupNode* Node = Dummy->Next;
    std::list<CollisionGroupDesc>::iterator It = Dest.begin();
    while (It != Dest.end() && Node != Dummy) {
        RpcCollisionGroupDescDecode(*It, Node->Id, Node->HealthBits);
        ++It;
        Node = Node->Next;
    }
    Dest.erase(It, Dest.end());
    while (Node != Dummy) {
        CollisionGroupDesc Desc;
        RpcCollisionGroupDescDecode(Desc, Node->Id, Node->HealthBits);
        Dest.push_back(Desc);
        Node = Node->Next;
    }
}

// 06d8c8e2, inlined in FUN_06d8c830: one 0x18 wire record per EquipDesc, with NO bEquipped
// filter (this is not FUN_06d8c750's cargo path). Status goes through x87; the same field map
// and is_internal() gate as RpcEquipHandleFromList.
static void RpcEquipHandleFromEquipDescVector(FRpcEquipWireHandle* Handle,
                                              const EquipDescVector& Source) {
    for (const EquipDesc* Item = Source.Begin; Item != Source.End; ++Item) {
        // The null test skips only the zeroing; the stores go through the result either way.
        FRpcEquipWireRecord* Record =
            static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                             std::nothrow));
        if (Record != nullptr) {
            RpcContainerInitRecord(Record);
        }
        Record->ArchId = Item->ArchId;
        Record->Count = Item->Count;
        Record->Status = Item->Health;
        Record->Id = Item->Id;
        Record->bEquipped = Item->bEquipped;
        // The size is stored before the allocation it sizes.
        if (!Item->is_internal()) {
            Record->HardpointSize =
                static_cast<unsigned short>(RpcContainerStringLength(Item->Hardpoint) + 1u);
            Record->Hardpoint = RpcContainerCopyString(Item->Hardpoint, Record->HardpointSize);
        }
        Handle->PushBack(Record);
    }
}

// Retail appends through COMMON.DLL::std::vector<EquipDesc>::insert(end, 1, value)
// (06d84fc8); VC6 grows that by doubling. The block comes from ::operator new because
// ~EquipDescVector (Common 0x06269110) releases it with ::operator delete.
static void RpcEquipDescVectorPushBack(EquipDescVector& Vector, const EquipDesc& Item) {
    if (Vector.End == Vector.Cap) {
        const unsigned int Count = static_cast<unsigned int>(Vector.End - Vector.Begin);
        const unsigned int NewCap = (Count == 0u) ? 1u : Count * 2u;
        // VC6 allocator<EquipDesc>::allocate: a bare operator new, no test.
        EquipDesc* Block = static_cast<EquipDesc*>(
            ::operator new(sizeof(EquipDesc) * NewCap, std::nothrow));
        for (unsigned int Index = 0; Index < Count; ++Index) {
            new (&Block[Index]) EquipDesc(Vector.Begin[Index]);
        }
        ::operator delete(Vector.Begin);
        Vector.Begin = Block;
        Vector.End = Block + Count;
        Vector.Cap = Block + NewCap;
    }
    new (Vector.End) EquipDesc(Item);
    ++Vector.End;
}

// 06d84f86, inlined in FUN_06d84ed0. Per node a default EquipDesc is APPENDED to the record's
// vector and the setters run on it in this order (note set_status last), then set_hardpoint
// (interned) or make_internal. Unlike FUN_06d84d60 there is no null-record test here.
static void RpcEquipDescVectorAppendFromHandle(EquipDescVector& Dest,
                                               const FRpcEquipWireHandle* Source) {
    const FRpcEquipWireListHeader* Header = Source->GetList();
    if (Header == nullptr) {
        return;
    }
    const FRpcEquipWireNode* Dummy = Header->Head;
    for (const FRpcEquipWireNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
        const FRpcEquipWireRecord* Record = Node->Value;
        EquipDesc Blank;
        RpcEquipDescVectorPushBack(Dest, Blank);
        EquipDesc& Slot = *(Dest.End - 1);
        Slot.set_arch_id(Record->ArchId);
        Slot.set_count(Record->Count);
        Slot.set_id(Record->Id);
        Slot.set_equipped(Record->bEquipped);
        Slot.set_status(Record->Status);
        if (Record->HardpointSize != 0) {
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
            Slot.set_hardpoint(Hardpoint);
        } else {
            Slot.make_internal();
        }
    }
}

// ===========================================================================
// The create-object header (ch2 0x04 CREATESHIP, 0x42 CREATESOLAR)
//
// FUN_06d8c830 and FUN_06d84ed0 on the shared CEqObjCreateInfo; the wire side is laid
// out in rpc_containers.h.
// ===========================================================================

// The abstract base's constructor part, inlined into both packet constructors in the same
// order (CREATESHIP 06d9cca2, CREATESOLAR 06d9e66b): packet+0x20..+0x2c, then the six dwords
// at +0x30..+0x44 zeroed LAST first (FUN_06d9cda0), +0x48..+0x54, the identity quaternion
// at +0x58 (FUN_06d931c0), +0x68..+0x70. Nothing is tested. The two Common.dll containers that
// follow are the derived FRpcCreateObjectWire's members.
FRpcCreateObjectHead::FRpcCreateObjectHead() {
    ObjId = 0;
    ArchetypeNickname = 0;
    DockingBaseId = 0;
    VibeGroupId = 0;
    Ids = nullptr;                                      // +0x44
    IdCount = 0;                                        // +0x40
    CostumeRightHand = 0;                                     // +0x3c
    CostumeLeftHand = 0;                                     // +0x38
    CostumeBody = 0;                                     // +0x34
    CostumeHead = 0;                                     // +0x30
    VoiceId = 0;
    PosX = 0.0f;
    PosY = 0.0f;
    PosZ = 0.0f;
    OrientW = 1.0f;
    OrientX = 0.0f;
    OrientY = 0.0f;
    OrientZ = 0.0f;
    HitPts = 0;
    LoadoutId = 0;
    Equipment.List = nullptr;
}

// The tail of the base part of FUN_06d97b90 (== FUN_06d9e740 from 06d9e81c): the handle at
// packet+0x70 through FUN_06d91340, then the id block at +0x44 freed (no nulling). The two
// containers above it in memory were destroyed first by the derived FRpcCreateObjectWire's
// member destructors, which is also retail's EH order (FuncInfo_06db70a4).
FRpcCreateObjectHead::~FRpcCreateObjectHead() {
    // The handle goes first; its own member destructor then finds it null.
    Equipment.Destroy();
    if (Ids != nullptr) {
        ::operator delete(Ids);
    }
}

// FUN_06d8c830.
void RpcCreateObjectHeaderWrite(FRpcCreateObjectWire* Wire,
                                const CEqObjCreateInfo& Source) {
    Wire->ObjId = Source.ObjId;
    Wire->ArchetypeNickname = Source.ArchetypeNickname;
    Wire->DockingBaseId = Source.DockingBaseId;
    Wire->VibeGroupId = Source.VibeGroupId;
    Wire->CostumeHead = Source.CostumeHead;
    Wire->CostumeBody = Source.CostumeBody;
    Wire->CostumeLeftHand = Source.CostumeLeftHand;
    Wire->CostumeRightHand = Source.CostumeRightHand;

    // An untested new(count << 2), made unconditionally (a count of 0 included), then a
    // signed-guarded copy with no bound. A Costume holds at most eight accessories, so a
    // CEqObjCreateInfo built from one never carries a larger count.
    const int Count = Source.CostumeAccessoryCount;
    Wire->IdCount = Count;
    unsigned int* Block = static_cast<unsigned int*>(
        ::operator new(static_cast<unsigned int>(Count) << 2, std::nothrow));
    Wire->Ids = Block;
    const unsigned int* Accessory = &Source.CostumeAccessory[0];
    for (int i = 0; i < Count; ++i) {
        Block[i] = Accessory[i];
    }

    Wire->VoiceId = Source.VoiceId;
    Wire->PosX = Source.Position.x;
    Wire->PosY = Source.Position.y;
    Wire->PosZ = Source.Position.z;
    Wire->OrientW = Source.Orientation.w;
    Wire->OrientX = Source.Orientation.x;
    Wire->OrientY = Source.Orientation.y;
    Wire->OrientZ = Source.Orientation.z;
    Wire->HitPts = static_cast<unsigned int>(Source.HitPts);
    Wire->LoadoutId = Source.LoadoutId;

    RpcEquipHandleFromEquipDescVector(&Wire->Equipment, Source.Equipment);
    // Into the lists the packet constructor built (06d9cce2 / 06d9ccf3).
    Wire->CollisionGroups = Source.CollisionGroups;
    Wire->Damage = Source.Damage;
}

// FUN_06d84ed0.
__declspec(dllexport) void RpcCreateObjectHeaderRead(CEqObjCreateInfo& Dest,
                               const FRpcCreateObjectWire* Wire) {
    Dest.ObjId = Wire->ObjId;
    Dest.ArchetypeNickname = Wire->ArchetypeNickname;
    Dest.DockingBaseId = Wire->DockingBaseId;
    Dest.VibeGroupId = Wire->VibeGroupId;
    Dest.CostumeHead = Wire->CostumeHead;
    Dest.CostumeBody = Wire->CostumeBody;
    Dest.CostumeLeftHand = Wire->CostumeLeftHand;
    Dest.CostumeRightHand = Wire->CostumeRightHand;

    // An unsigned loop after a signed guard, so a negative count copies nothing. No bound on
    // the destination and no test on the id block: the count and the block were produced
    // together by RpcCreateObjectHeaderWrite (at most eight).
    const int Count = Wire->IdCount;
    Dest.CostumeAccessoryCount = Count;
    if (Count > 0) {
        unsigned int* Accessory = &Dest.CostumeAccessory[0];
        for (unsigned int i = 0; i < static_cast<unsigned int>(Wire->IdCount); ++i) {
            Accessory[i] = Wire->Ids[i];
        }
    }

    Dest.VoiceId = Wire->VoiceId;
    Dest.Position.x = Wire->PosX;
    Dest.Position.y = Wire->PosY;
    Dest.Position.z = Wire->PosZ;
    Dest.Orientation.w = Wire->OrientW;
    Dest.Orientation.x = Wire->OrientX;
    Dest.Orientation.y = Wire->OrientY;
    Dest.Orientation.z = Wire->OrientZ;
    Dest.HitPts = static_cast<int>(Wire->HitPts);
    Dest.LoadoutId = Wire->LoadoutId;

    RpcEquipDescVectorAppendFromHandle(Dest.Equipment, &Wire->Equipment);
    // Into the record's own list (emptied by clear() since the last packet).
    Dest.CollisionGroups = Wire->CollisionGroups;
    Dest.Damage = Wire->Damage;
}

// The receiver statics' constructor (06d83df0), per-packet clear (06d83ed0 / 06d83fa0) and
// destructor (06d843b0) are the shared type's inline members in src/include/server_createinfo.h.

// ===========================================================================
// ch2 0x03 CHARACTERINFO -- the record conversions and the wire list
//
// Sender 0x06d89890 / FUN_06d955c0, receiver IServerImpl::HandleCharacterInfo
// 0x06d84490. The layout derivation is in rpc_containers.h.
// ===========================================================================

// 06d89b30 (BlockA; BlockB is the same run at 06d89c25); 06d84782 back the other way. The
// inline block is a Costume (server_characterset.h).
static void RpcIdBlockToWire(FRpcIdArrayBlockWire** Out, const Costume& Source) {
    // new(0x18); the null test only skips the zero stores -- the pointer is stored and
    // written through either way.
    FRpcIdArrayBlockWire* Block = static_cast<FRpcIdArrayBlockWire*>(
        ::operator new(sizeof(FRpcIdArrayBlockWire), std::nothrow));
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->RightHand = 0;
        Block->LeftHand = 0;
        Block->Body = 0;
        Block->Head = 0;
    }
    *Out = Block;
    Block->Body = Source.PartIds[1];            // <- src+0x278
    Block->Head = Source.PartIds[0];            // <- src+0x274
    Block->LeftHand = Source.PartIds[2];
    Block->RightHand = Source.PartIds[3];
    Block->Count = static_cast<int>(Source.AccessoryCount);   // <- src+0x2a4
    // The count is UNSIGNED and the copy is not bounded by the eight-slot array -- it walks
    // on from src+0x284.
    if ((unsigned int)Block->Count != 0) {
        Block->Ids = static_cast<unsigned int*>(
            ::operator new((unsigned int)Block->Count << 2, std::nothrow));
        const unsigned int* SourceIds = &Source.AccessoryIds[0];
        for (unsigned int i = 0; i < (unsigned int)Block->Count; ++i) {
            Block->Ids[i] = SourceIds[i];
        }
    }
}

static void RpcIdBlockFromWire(Costume& Dest, const FRpcIdArrayBlockWire* Block) {
    // FUN_06d84bf0, the record constructor's block init: +0x00..+0x0c and the count at +0x30.
    // Deviation: retail leaves the eight ids as heap garbage; the port zeroes them.
    Dest.PartIds[0] = 0;
    Dest.PartIds[1] = 0;
    Dest.PartIds[2] = 0;
    Dest.PartIds[3] = 0;
    Dest.AccessoryCount = 0;
    for (unsigned int i = 0; i < 8u; ++i) {
        Dest.AccessoryIds[i] = 0;
    }
    if (Block == nullptr) {
        // A null block pointer skips the whole group.
        return;
    }
    Dest.PartIds[1] = Block->Body;
    Dest.PartIds[0] = Block->Head;
    Dest.PartIds[3] = Block->RightHand;
    Dest.PartIds[2] = Block->LeftHand;
    Dest.AccessoryCount = static_cast<unsigned int>(Block->Count);
    // Unsigned count, no bound against the eight slots, and the wire array is not tested --
    // the sender's loop mirrored.
    unsigned int* DestIds = &Dest.AccessoryIds[0];
    for (unsigned int i = 0; i < (unsigned int)Block->Count; ++i) {
        DestIds[i] = Block->Ids[i];
    }
}

// FUN_06d95b60 -- the wire id block's destructor. Called by the CHARACTERINFO packet
// destructor FUN_06d93350; retail also reaches it from three EH unwind funclets, which the
// port's non-throwing constructors do not need.
void FRpcIdArrayBlockWire::Destroy() {
    if (Ids != nullptr) {
        ::operator delete(Ids);
    }
}

// FUN_06d84c10 -- the scalar deleting destructor: FUN_06d95b60's body, then the block.
static void RpcIdBlockWireFree(FRpcIdArrayBlockWire* Block) {
    if (Block == nullptr) {
        return;
    }
    Block->Destroy();
    ::operator delete(Block);
}

void RpcCharacterInfoRecordToWire(FRpcCharacterInfoWireRecord* Wire,
                                  const CCharacterSet::CharacterInfo* Source) {
    // Retail's record constructor (06d89944) puts every owning field in a releasable state
    // before a single value is copied; the packet destructor can run after a half-built record.
    Wire->BlockA = nullptr;
    Wire->BlockB = nullptr;
    Wire->Equipment.List = nullptr;
    Wire->CollisionGroups.Allocator = 0;
    Wire->CollisionGroups.Pad_0x01[0] = 0;
    Wire->CollisionGroups.Pad_0x01[1] = 0;
    Wire->CollisionGroups.Pad_0x01[2] = 0;
    Wire->CollisionGroups.Head = nullptr;
    Wire->CollisionGroups.Size = 0;
    // 06d899c1: the record constructor builds the std::list at +0x268 (the dummy node).
    Wire->CollisionGroups.Init();

    Wire->Name = Source->Id;
    Wire->bIsRestart = Source->bIsRestart;
    Wire->bIsAutoSave = Source->bIsAutoSave;
    Wire->TextA = Source->Name;
    Wire->TextB = Source->Description;
    Wire->DescripStrId = Source->DescripStrId;
    Wire->TimestampLow = Source->TimestampLow;
    Wire->TimestampHigh = Source->TimestampHigh;
    Wire->ShipArchetype = Source->ShipArchetype;
    Wire->Money = static_cast<unsigned int>(Source->Money);
    // The permuted nine; the sender/receiver pairs are listed on the struct in rpc_containers.h.
    Wire->SystemId = Source->SystemId;            // <- src+0x260
    Wire->VoiceId = Source->VoiceId;             // <- src+0x26c
    Wire->Rank = Source->Rank;                // <- src+0x270
    Wire->BaseId = Source->BaseId;              // <- src+0x264
    Wire->LocationId = Source->LocationId;          // <- src+0x268
    // 06d89986: the record ctor's zero is the ONLY write retail ever makes to +0x24c. It still
    // travels -- see the note on the struct.
    Wire->Field_0x24c = 0;
    // src+0x244 is the float HullStatus; the sender moves its four bytes as a dword.
    std::memcpy(&Wire->HullStatusBits, &Source->HullStatus, sizeof(Wire->HullStatusBits));
    Wire->NumKills = static_cast<unsigned int>(Source->NumKills);             // <- src+0x238
    Wire->NumMissionFailures = static_cast<unsigned int>(Source->NumMissionFailures);   // <- src+0x240
    Wire->NumMissionSuccesses = static_cast<unsigned int>(Source->NumMissionSuccesses);  // <- src+0x23c
    // Hop one only zeroes it; the packet-bound hop FUN_06d955c0 writes the real value, folded
    // in below once the equipment list exists.
    Wire->EquipmentCount = 0;

    RpcIdBlockToWire(&Wire->BlockA, Source->ComCostume);   // <- src+0x274
    RpcIdBlockToWire(&Wire->BlockB, Source->BaseCostume);  // <- src+0x2a8

    // 06d89d28: one 0x18 wire record per EquipDesc. Unlike FUN_06d8c830 this loop has NO
    // is_internal gate -- it always copies the hardpoint.
    for (std::list<EquipDesc>::const_iterator It = Source->Equipment.Items.begin();
         It != Source->Equipment.Items.end(); ++It) {
        // The null test skips only the zero stores.
        FRpcEquipWireRecord* Record = static_cast<FRpcEquipWireRecord*>(
            ::operator new(sizeof(FRpcEquipWireRecord), std::nothrow));
        if (Record != nullptr) {
            RpcContainerInitRecord(Record);
        }
        Record->ArchId = It->ArchId;
        Record->Count = It->Count;
        Record->Status = It->Health;
        Record->Id = It->Id;
        Record->bEquipped = It->bEquipped;
        // EquipDesc+0x18 (bMission) -> record +0x0f (06d89d72); the receiver puts it straight
        // back (06d848fd). Every other user of the record leaves +0x0f at zero.
        Record->bMission = (unsigned char)(It->bMission ? 1 : 0);
        const unsigned int Length = RpcContainerStringLength(It->Hardpoint);
        Record->Hardpoint = RpcContainerCopyString(It->Hardpoint, Length + 1u);
        Record->HardpointSize = (unsigned short)(Length + 1u);
        Wire->Equipment.PushBack(Record);
    }

    // FUN_06d955c0's postscript (06d959c6), the only thing the packet-bound hop adds:
    // +0x260 = the list's size truncated to a byte, or 0. +0x260 is the count the wire codec
    // emits and the decoder loops on, so it must not be left zero.
    {
        const FRpcEquipWireListHeader* Header = Wire->Equipment.GetList();
        Wire->EquipmentCount = (Header != nullptr) ? (unsigned char)(Header->Size & 0xffu) : 0;
    }

    // 06d89dd2: list assignment of src+0x254 onto rec+0x268, each value moved as two raw
    // dwords.
    RpcCollisionGroupListAssignRange(&Wire->CollisionGroups,
                                     Source->CollisionGroups.begin(),
                                     Source->CollisionGroups.end());
}

void RpcCharacterInfoRecordFromWire(CCharacterSet::CharacterInfo* Dest,
                                    const FRpcCharacterInfoWireRecord* Wire) {
    Dest->Id = Wire->Name;
    // The receiver re-normalises both flags to 0/1 instead of copying the byte back verbatim.
    Dest->bIsRestart = (unsigned char)(Wire->bIsRestart != 0 ? 1 : 0);
    Dest->bIsAutoSave = (unsigned char)(Wire->bIsAutoSave != 0 ? 1 : 0);
    Dest->Name = Wire->TextA;
    Dest->Description = Wire->TextB;
    Dest->DescripStrId = Wire->DescripStrId;
    Dest->TimestampLow = Wire->TimestampLow;
    Dest->TimestampHigh = Wire->TimestampHigh;
    Dest->ShipArchetype = Wire->ShipArchetype;
    Dest->Money = static_cast<int>(Wire->Money);
    Dest->VoiceId = Wire->VoiceId;               // -> dst+0x26c
    Dest->Rank = Wire->Rank;                  // -> dst+0x270
    Dest->BaseId = Wire->BaseId;                // -> dst+0x264
    Dest->LocationId = Wire->LocationId;            // -> dst+0x268
    Dest->SystemId = Wire->SystemId;              // -> dst+0x260
    std::memcpy(&Dest->HullStatus, &Wire->HullStatusBits, sizeof(Dest->HullStatus));  // -> dst+0x244
    Dest->NumKills = static_cast<int>(Wire->NumKills);              // -> dst+0x238
    Dest->NumMissionFailures = static_cast<int>(Wire->NumMissionFailures);    // -> dst+0x240
    Dest->NumMissionSuccesses = static_cast<int>(Wire->NumMissionSuccesses);   // -> dst+0x23c

    RpcIdBlockFromWire(Dest->ComCostume, Wire->BlockA);   // -> dst+0x274
    RpcIdBlockFromWire(Dest->BaseCostume, Wire->BlockB);  // -> dst+0x2a8

    // 06d84839: one EquipDesc per wire record. Only the list HEADER is tested; the dummy and
    // each node's record are used as they are.
    Dest->Equipment.Items.clear();
    const FRpcEquipWireListHeader* Header = Wire->Equipment.GetList();
    if (Header != nullptr) {
        for (const FRpcEquipWireNode* Node = Header->Head->Next; Node != Header->Head;
             Node = Node->Next) {
            const FRpcEquipWireRecord* Record = Node->Value;
            EquipDesc Item;
            Item.set_arch_id(Record->ArchId);
            Item.set_count(Record->Count);
            Item.set_id(Record->Id);
            Item.set_equipped(Record->bEquipped);
            Item.set_status(Record->Status);
            Item.bMission = (Record->bMission != 0);        // a direct store at +0x18
            // Unlike the other two equipment receivers this one has NO conditional and no
            // make_internal branch -- it interns and stores whatever the record holds.
            // Interning is required: set_hardpoint only stores the pointer.
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
            Item.set_hardpoint(Hardpoint);
            Dest->Equipment.Items.push_back(Item);
        }
    }

    // 06d84934: list assignment from rec+0x268 onto the fresh record's empty +0x254 list,
    // which reduces to one append per node.
    Dest->CollisionGroups.clear();
    for (const FRpcCollisionGroupNode* Node = Wire->CollisionGroups.Head->Next;
         Node != Wire->CollisionGroups.Head; Node = Node->Next) {
        // The value moves as two raw dwords, pad included.
        CollisionGroupDesc Item;
        std::memcpy(&Item, &Node->Id, sizeof(Item));
        Dest->CollisionGroups.push_back(Item);
    }
}

// 06d94305 (decoder) and 06d95ad4 (FUN_06d955c0): untested new(0xc), spliced before the
// dummy. The dummy is the one the packet constructor built (06d9d52f), so it is read without a
// test, as retail reads packet+0x28.
void FRpcCharacterInfoWireList::PushBack(FRpcCharacterInfoWireRecord* Record) {
    FRpcCharacterInfoWireNode* Dummy = Head;
    FRpcCharacterInfoWireNode* Tail = Dummy->Prev;
    FRpcCharacterInfoWireNode* Node = static_cast<FRpcCharacterInfoWireNode*>(
        ::operator new(sizeof(FRpcCharacterInfoWireNode), std::nothrow));
    Node->Next = Dummy;
    if (Tail == nullptr) {
        Tail = Node;
    }
    Node->Prev = Tail;
    Dummy->Prev = Node;
    Node->Prev->Next = Node;
    Node->Value = Record;
    ++Size;
}

// FUN_06d93350, the CHARACTERINFO destructor's body, in retail's two passes. Pass 1: for every
// node whose value is non-null, the record's owned members in retail's order, then the record;
// the nodes are left alone. Pass 2: every node unlinked and freed (FUN_06d901f0), then the
// sentinel.
void FRpcCharacterInfoWireList::Release() {
    FRpcCharacterInfoWireNode* Node = Head->Next;
    while (Node != Head) {
        FRpcCharacterInfoWireRecord* Record = Node->Value;
        if (Record != nullptr) {
            // FUN_06d95b60 (the id array at +0x14) and the block, for +0x274 then +0x278.
            if (Record->BlockA != nullptr) {
                Record->BlockA->Destroy();
                ::operator delete(Record->BlockA);
            }
            if (Record->BlockB != nullptr) {
                Record->BlockB->Destroy();
                ::operator delete(Record->BlockB);
            }
            // COMMON.DLL's list<CollisionGroupDesc>::erase(begin, end), then the sentinel. No
            // test: the record's constructor always built the sentinel.
            FRpcCollisionGroupList& Groups = Record->CollisionGroups;
            FRpcCollisionGroupNode* Group = Groups.Head->Next;
            while (Group != Groups.Head) {
                FRpcCollisionGroupNode* Erased = Group;
                Group = Group->Next;
                Erased->Prev->Next = Erased->Next;
                Erased->Next->Prev = Erased->Prev;
                ::operator delete(Erased);
                --Groups.Size;
            }
            ::operator delete(Groups.Head);
            Groups.Head = nullptr;
            Groups.Size = 0;
            Record->Equipment.Destroy();
            // The two basic_string destructors (+0x214 then +0x204) run as the record's
            // member destructors, in the same order.
            delete Record;
        }
        Node = Node->Next;
    }
    FRpcCharacterInfoWireNode* Sentinel = Head;
    FRpcCharacterInfoWireNode* Erase = Sentinel->Next;
    while (Erase != Sentinel) {
        FRpcCharacterInfoWireNode* Erased = Erase;
        Erase = Erase->Next;
        Erased->Prev->Next = Erased->Next;
        Erased->Next->Prev = Erased->Prev;
        ::operator delete(Erased);
        --Size;
    }
    ::operator delete(Head);
    Head = nullptr;
    Size = 0;
}

// FUN_06d94460's id-block copy (06d94692 for +0x274, 06d94770 for +0x278). The null test only
// skips the zero stores; for an UNSIGNED non-zero count an untested new(count * 4) and a copy
// whose bound re-reads the DESTINATION's count.
static void RpcIdBlockWireCopy(FRpcIdArrayBlockWire** Out, const FRpcIdArrayBlockWire* Source) {
    if (Source == nullptr) {
        return;
    }
    FRpcIdArrayBlockWire* Block = static_cast<FRpcIdArrayBlockWire*>(
        ::operator new(sizeof(FRpcIdArrayBlockWire), std::nothrow));
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->RightHand = 0;
        Block->LeftHand = 0;
        Block->Body = 0;
        Block->Head = 0;
    }
    *Out = Block;
    Block->Body = Source->Body;
    Block->Head = Source->Head;
    Block->RightHand = Source->RightHand;
    Block->LeftHand = Source->LeftHand;
    Block->Count = Source->Count;
    if ((unsigned int)Block->Count != 0) {
        Block->Ids = static_cast<unsigned int*>(
            ::operator new((unsigned int)Block->Count << 2, std::nothrow));
        for (unsigned int i = 0; i < (unsigned int)Block->Count; ++i) {
            Block->Ids[i] = Source->Ids[i];
        }
    }
}

// IServerImpl::HandleCharacterInfo 06d849fe. Same members, same order as the packet
// destructor's pass 1, but with the id blocks going through FUN_06d84c10 (destroy + free).
void RpcCharacterInfoWireRecordFree(FRpcCharacterInfoWireRecord* Record) {
    if (Record->BlockA != nullptr) {
        RpcIdBlockWireFree(Record->BlockA);
    }
    if (Record->BlockB != nullptr) {
        RpcIdBlockWireFree(Record->BlockB);
    }
    // 06d84a30: the std::list<CollisionGroupDesc> destructor on +0x268 (the dummy was built by
    // the record constructor, so never null).
    FRpcCollisionGroupList& Groups = Record->CollisionGroups;
    FRpcCollisionGroupNode* Group = Groups.Head->Next;
    while (Group != Groups.Head) {
        FRpcCollisionGroupNode* Erased = Group;
        Group = Group->Next;
        Erased->Prev->Next = Erased->Next;
        Erased->Next->Prev = Erased->Prev;
        ::operator delete(Erased);
        --Groups.Size;
    }
    ::operator delete(Groups.Head);
    Groups.Head = nullptr;
    Groups.Size = 0;
    Record->Equipment.Destroy();
    // The two basic_string destructors (+0x214 then +0x204) run as the record's member
    // destructors, in the same order.
    delete Record;
}

// FUN_06d94460 (`this` = the packet, argument = the destination header).
void FRpcCharacterInfoWireList::Copy(const FRpcCharacterInfoWireList* Source) {
    // The source dummy is read untested; the loop bound re-reads it every round.
    for (const FRpcCharacterInfoWireNode* Node = Source->Head->Next; Node != Source->Head;
         Node = Node->Next) {
        const FRpcCharacterInfoWireRecord* From = Node->Value;
        if (From == nullptr) {
            continue;
        }
        // new(0x27c); the null test skips only the constructor -- the copy below runs on the
        // result either way, as in the sender. Retail's constructor leaves +0x230..+0x240
        // alone; every field it does not set is overwritten below.
        FRpcCharacterInfoWireRecord* To = new (std::nothrow) FRpcCharacterInfoWireRecord();
        To->CollisionGroups.Init();

        To->Name = From->Name;
        To->bIsRestart = From->bIsRestart;
        To->bIsAutoSave = From->bIsAutoSave;
        To->TextA = From->TextA;
        To->TextB = From->TextB;
        To->DescripStrId = From->DescripStrId;
        To->TimestampLow = From->TimestampLow;
        To->TimestampHigh = From->TimestampHigh;
        To->ShipArchetype = From->ShipArchetype;
        To->Money = From->Money;
        To->SystemId = From->SystemId;
        To->BaseId = From->BaseId;
        To->LocationId = From->LocationId;
        To->VoiceId = From->VoiceId;
        To->Rank = From->Rank;
        To->Field_0x24c = From->Field_0x24c;
        To->HullStatusBits = From->HullStatusBits;
        To->NumKills = From->NumKills;
        To->NumMissionFailures = From->NumMissionFailures;
        To->NumMissionSuccesses = From->NumMissionSuccesses;

        RpcIdBlockWireCopy(&To->BlockA, From->BlockA);
        RpcIdBlockWireCopy(&To->BlockB, From->BlockB);

        To->Equipment.Copy(&From->Equipment);
        // +0x260 is DERIVED from the copy, not copied (same postscript as FUN_06d955c0).
        {
            const FRpcEquipWireListHeader* Header = To->Equipment.GetList();
            To->EquipmentCount = (Header != nullptr) ? (unsigned char)(Header->Size & 0xffu) : 0;
        }

        // List assignment inlined (06d94880), each value moved as two raw dwords.
        RpcCollisionGroupListAssignRange(&To->CollisionGroups,
                                         FRpcCollisionGroupNodeIterator(From->CollisionGroups.Head->Next),
                                         FRpcCollisionGroupNodeIterator(From->CollisionGroups.Head));

        PushBack(To);
    }
}

// ===========================================================================
// ch2 0x03 CHARACTERINFO -- the wire codec, packet vtable slots +0x04 / +0x08.
//
// Retail LAB_06d949d0 (encode) and FUN_06d934d0 (decode). Both walk the payload with the
// FRpcMarshalStream idiom (Cursor += Width; if (Cursor > Limit) Error = code; if (Error) bail;
// access; Ptr += Width). Error is sticky; 0x16 out, 0x17 back.
//
// FIELD ORDER, which is NOT the struct's order; the decoder mirrors it exactly:
//
//   header    u16   FUN_06da1380, cursor starts at 2
//   count     u8    (byte)Records.Size, read from packet+0x2c
//   per record, walking packet+0x28's dummy-node ring:
//     NameLen u16 + NameLen bytes of the CHARACTER_ID at rec+0x000
//     u8    rec+0x200
//     u8    rec+0x201
//     u16   TextA length in CHARACTERS + 2*length bytes
//     u16   TextB length in CHARACTERS + 2*length bytes
//     u32   rec+0x224, +0x22c, +0x228  (note the 22c/228 SWAP)
//     u32   rec+0x230, +0x234, +0x238, +0x23c, +0x240, +0x244
//     u32   rec+0x248, +0x24c, +0x250, +0x254, +0x258, +0x25c
//     BlockA: u8 present; then A+0x04, A+0x00, A+0x08, A+0x0c,
//             A+0x10 = Count, then Count*4 bytes from A+0x14
//     BlockB: the same, from rec+0x278
//     u8    rec+0x260 = the equipment element count
//     per equipment record E (the whole list at *(rec+0x264)):
//       u32 E+0x04, float E+0x08, u32 E+0x00, u16 E+0x0c,
//       u8 E+0x0e, u8 E+0x0f, u16 E+0x10 = byte count,
//       then that many bytes from E+0x14
//     u32   rec+0x270 = the collision list's Size
//     per collision node N (the ring at rec+0x26c):
//       u16 (low half of N.Field_0x00), float N.Field_0x04
//   tail      u32   packet+0x20
//   trailer         FUN_06da13c0, a no-op
//
// The header has no limit test (PutHeaderUnchecked). The name is memcpy'd into the 0x200-byte
// CHARACTER_ID with the full wire length and terminated at rec[NameLen], unbounded, as retail.
// ===========================================================================

// 06d94230 -- the decoder's own splice into rec+0x26c (untested new(0x10)). Returns the node:
// both decoders splice it in BEFORE reading the fields they then write into it.
static FRpcCollisionGroupNode* RpcCollisionGroupListPushBack(FRpcCollisionGroupList* List,
                                                             unsigned int Field_0x00,
                                                             unsigned int Field_0x04) {
    List->Init();
    FRpcCollisionGroupNode* Dummy = List->Head;
    FRpcCollisionGroupNode* Tail = Dummy->Prev;
    FRpcCollisionGroupNode* New = static_cast<FRpcCollisionGroupNode*>(
        ::operator new(sizeof(FRpcCollisionGroupNode), std::nothrow));
    New->Next = Dummy;
    if (Tail == nullptr) {
        Tail = New;
    }
    New->Prev = Tail;
    Dummy->Prev = New;
    New->Prev->Next = New;
    New->Id = Field_0x00;
    New->HealthBits = Field_0x04;
    ++List->Size;
    return New;
}

// 06d94a92: strlen, bounded here by the buffer retail scans, which is always NUL-terminated.
static unsigned int RpcCharacterIdLength(const CHARACTER_ID& Id) {
    unsigned int Length = 0;
    while (Length < (unsigned int)sizeof(Id.Buffer) && Id.Buffer[Length] != '\0') {
        ++Length;
    }
    return Length;
}

// The id block's five scalars and its array, in the encoder's order.
static void RpcEncodeIdBlock(FRpcMarshalStream& Stream, const FRpcIdArrayBlockWire* Block) {
    if (Block == nullptr) {
        Stream.PutByte(0);
        return;
    }
    Stream.PutByte(1);
    Stream.PutDword(Block->Body);
    Stream.PutDword(Block->Head);
    Stream.PutDword(Block->LeftHand);
    Stream.PutDword(Block->RightHand);
    Stream.PutDword((unsigned int)Block->Count);
    // Count DWORDS as Count*4 bytes from +0x14; no zero test.
    Stream.PutBytes(Block->Ids, (unsigned int)Block->Count * 4u);
}

static bool RpcDecodeIdBlock(FRpcMarshalStream& Stream, FRpcIdArrayBlockWire** Out) {
    *Out = nullptr;
    unsigned char bPresent = 0;
    if (!Stream.GetByte(bPresent)) {
        return false;
    }
    if (bPresent == 0) {
        return true;
    }
    // 06d93be5 new(0x18): the null test only skips the zero stores; a null block is stored and
    // written through, exactly as retail does.
    FRpcIdArrayBlockWire* Block = static_cast<FRpcIdArrayBlockWire*>(
        ::operator new(sizeof(FRpcIdArrayBlockWire), std::nothrow));
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->RightHand = 0;
        Block->LeftHand = 0;
        Block->Body = 0;
        Block->Head = 0;
    }
    *Out = Block;
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->Body = Scratch;
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->Head = Scratch;
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->LeftHand = Scratch;
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->RightHand = Scratch;
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->Count = (int)Scratch;
    if ((unsigned int)Block->Count != 0) {            // unsigned
        const unsigned int Bytes = (unsigned int)Block->Count << 2;
        // Untested allocation, stored at once; the stream bound is checked only AFTER it.
        Block->Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
        if (!Stream.GetBytes(Block->Ids, Bytes)) {
            return false;
        }
    }
    return true;
}

// ===========================================================================
// The ch2 0x03 WIRE CODEC -- packet vtable slots +0x04 and +0x08.
//
// Retail: LAB_06d949d0 (encode) and FUN_06d934d0 (decode), slots +0x04 / +0x08 of
// PTR_FUN_06db3dc4 (CDAPCharacterInfo ctor 0x06d9d54f). Both are __thiscall with THREE
// stack arguments.
//
// The two argument shapes are NOT symmetric, and that asymmetry is retail's:
//   * encode takes a pointer TO the buffer pointer and a plain capacity, like the shared
//     payload-less encoder LAB_06da13d0. Retail never writes the caller's pointer back --
//     the moving cursor lives in packet+0x18.
//   * decode takes the buffer pointer DIRECTLY, and again the capacity.
// Both report the byte count through arg3 and return 0, or FRpcMarshalStream's 0x16 / 0x17.
//
// Encode's count INCLUDES the two header bytes FUN_06da1380 writes; decode's does NOT,
// because its header hook FUN_06da13c0 is a no-op -- the dispatcher has already consumed
// the header.
// ===========================================================================
unsigned int FPacket_FLPACKET_SERVER_CHARACTERINFO::Encode(unsigned char** BufferHolder,
                                                           unsigned int Capacity,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380

    // The LOW BYTE of the list's Size.
    const unsigned char Count = (unsigned char)(Wire.Records.Size & 0xffu);
    Stream.PutByte(Count);
    // An empty count OR an empty ring skips straight to the tail.
    if (Count != 0 && Wire.Records.Head != nullptr) {
        for (const FRpcCharacterInfoWireNode* Node = Wire.Records.Head->Next;
             Node != Wire.Records.Head && !Stream.Failed(); Node = Node->Next) {
            const FRpcCharacterInfoWireRecord* Record = Node->Value;
            if (Record == nullptr) {
                continue;
            }
            const unsigned int NameLen = RpcCharacterIdLength(Record->Name);
            Stream.PutWord((unsigned short)NameLen);
            Stream.PutBytes(Record->Name.Buffer, NameLen);
            Stream.PutByte(Record->bIsRestart);
            Stream.PutByte(Record->bIsAutoSave);
            // The length is in CHARACTERS and the run is 2*that.
            const unsigned short LenA = (unsigned short)(Record->TextA.size() & 0xffffu);
            Stream.PutWord(LenA);
            Stream.PutBytes(LenA != 0 ? Record->TextA.data() : nullptr,
                            (unsigned int)LenA * 2u);
            const unsigned short LenB = (unsigned short)(Record->TextB.size() & 0xffffu);
            Stream.PutWord(LenB);
            Stream.PutBytes(LenB != 0 ? Record->TextB.data() : nullptr,
                            (unsigned int)LenB * 2u);
            Stream.PutDword(Record->DescripStrId);
            Stream.PutDword(Record->TimestampHigh);
            Stream.PutDword(Record->TimestampLow);
            Stream.PutDword(Record->ShipArchetype);
            Stream.PutDword(Record->Money);
            Stream.PutDword(Record->SystemId);
            Stream.PutDword(Record->BaseId);
            Stream.PutDword(Record->LocationId);
            Stream.PutDword(Record->VoiceId);
            Stream.PutDword(Record->Rank);
            Stream.PutDword(Record->Field_0x24c);
            Stream.PutDword(Record->HullStatusBits);                   // x87 in retail
            Stream.PutDword(Record->NumKills);
            Stream.PutDword(Record->NumMissionFailures);
            Stream.PutDword(Record->NumMissionSuccesses);
            RpcEncodeIdBlock(Stream, Record->BlockA);
            RpcEncodeIdBlock(Stream, Record->BlockB);
            // The stored count byte, then the WHOLE list. Retail trusts the two to agree
            // (FUN_06d955c0 derives the byte from the list's Size); reproduced rather than
            // recomputed.
            Stream.PutByte(Record->EquipmentCount);
            // The header is tested; the dummy and each node's record are not.
            const FRpcEquipWireListHeader* Equip = Record->Equipment.GetList();
            if (Equip != nullptr) {
                for (const FRpcEquipWireNode* Item = Equip->Head->Next;
                     Item != Equip->Head && !Stream.Failed(); Item = Item->Next) {
                    const FRpcEquipWireRecord* E = Item->Value;
                    Stream.PutDword((unsigned int)E->Count);        // E+0x04
                    Stream.PutFloat(E->Status);                     // E+0x08
                    Stream.PutDword(E->ArchId);                     // E+0x00
                    Stream.PutWord(E->Id);                          // E+0x0c
                    Stream.PutByte(E->bEquipped);                   // E+0x0e
                    Stream.PutByte(E->bMission);                  // E+0x0f
                    Stream.PutWord(E->HardpointSize);               // E+0x10
                    // A BYTE count, unlike the two strings above.
                    Stream.PutBytes(E->Hardpoint, E->HardpointSize);
                }
            }
            Stream.PutDword(Record->CollisionGroups.Size);          // rec+0x270
            // The dummy of the record ctor's list (+0x26c), used untested.
            for (const FRpcCollisionGroupNode* Group = Record->CollisionGroups.Head->Next;
                 Group != Record->CollisionGroups.Head && !Stream.Failed();
                 Group = Group->Next) {
                // Only the LOW HALF of the first dword travels, which fits CollisionGroupDesc
                // as { ushort Id; ushort Pad; float Health; } (common_cobject.h).
                Stream.PutWord((unsigned short)(Group->Id & 0xffffu));
                Stream.PutDword(Group->HealthBits);                 // x87 in retail
            }
        }
    }
    Stream.PutDword(Wire.SaveFailureCount);                               // packet+0x20
    // The trailer FUN_06da13c0 emits nothing.
    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    if (!Stream.Failed()) {
        RpcRecordEncodedPacket(PacketId, Stream.Length());
    }
    return Stream.GetError();
}

unsigned int FPacket_FLPACKET_SERVER_CHARACTERINFO::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    // The buffer pointer is taken DIRECTLY, and the decoder reads no header.
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    unsigned char Count = 0;
    if (!Stream.GetByte(Count)) {
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        // new(0x27c): a failed allocation skips the record's whole body -- no bytes are read
        // for it -- and the loop goes on with the next index.
        FRpcCharacterInfoWireRecord* Record = new (std::nothrow) FRpcCharacterInfoWireRecord();
        if (Record == nullptr) {
            continue;
        }
        // The record constructor's std::list at +0x268 (the dummy node).
        Record->CollisionGroups.Init();

        // Every failed read below returns the error at once: the record is not in the packet
        // list yet, and retail neither frees it nor reports a length. The port leaks it the
        // same way.
        unsigned short Length = 0;
        unsigned int Scratch = 0;

        // The length word, then memcpy of the FULL wire length into the 0x200-byte
        // CHARACTER_ID and `rec[len] = 0` -- no bound, like retail.
        if (!Stream.GetWord(Length)) {
            return Stream.GetError();
        }
        char* NameBytes = Record->Name.Buffer;
        if (!Stream.GetBytes(NameBytes, (unsigned int)Length)) {
            return Stream.GetError();
        }
        NameBytes[Length] = '\0';
        if (!Stream.GetByte(Record->bIsRestart)) { return Stream.GetError(); }
        if (!Stream.GetByte(Record->bIsAutoSave)) { return Stream.GetError(); }
        // The CHARACTER count, append(count, ' ') (FUN_06d94420), then 2*count bytes over it.
        if (!Stream.GetWord(Length)) {
            return Stream.GetError();
        }
        Record->TextA.append((size_t)Length, L' ');
        if (!Stream.GetBytes(Length != 0 ? &Record->TextA[0] : nullptr,
                             (unsigned int)Length * 2u)) {
            return Stream.GetError();
        }
        if (!Stream.GetWord(Length)) {
            return Stream.GetError();
        }
        Record->TextB.append((size_t)Length, L' ');
        if (!Stream.GetBytes(Length != 0 ? &Record->TextB[0] : nullptr,
                             (unsigned int)Length * 2u)) {
            return Stream.GetError();
        }
        if (!Stream.GetDword(Record->DescripStrId)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->TimestampHigh)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->TimestampLow)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->ShipArchetype)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->Money)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->SystemId)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->BaseId)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->LocationId)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->VoiceId)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->Rank)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->Field_0x24c)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->HullStatusBits)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->NumKills)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->NumMissionFailures)) { return Stream.GetError(); }
        if (!Stream.GetDword(Record->NumMissionSuccesses)) { return Stream.GetError(); }
        if (!RpcDecodeIdBlock(Stream, &Record->BlockA)) {
            return Stream.GetError();
        }
        if (!RpcDecodeIdBlock(Stream, &Record->BlockB)) {
            return Stream.GetError();
        }
        if (!Stream.GetByte(Record->EquipmentCount)) {
            return Stream.GetError();
        }
        for (unsigned int i = 0; i < (unsigned int)Record->EquipmentCount; ++i) {
            // A failed allocation skips this element's reads and goes to the next index.
            FRpcEquipWireRecord* E = static_cast<FRpcEquipWireRecord*>(
                ::operator new(sizeof(FRpcEquipWireRecord), std::nothrow));
            if (E == nullptr) {
                continue;
            }
            RpcContainerInitRecord(E);
            if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }   // E+0x04
            E->Count = (int)Scratch;
            if (!Stream.GetFloat(E->Status)) { return Stream.GetError(); } // E+0x08
            if (!Stream.GetDword(E->ArchId)) { return Stream.GetError(); } // E+0x00
            if (!Stream.GetWord(E->Id)) { return Stream.GetError(); }      // E+0x0c
            if (!Stream.GetByte(E->bEquipped)) { return Stream.GetError(); }
            if (!Stream.GetByte(E->bMission)) { return Stream.GetError(); }
            if (!Stream.GetWord(E->HardpointSize)) { return Stream.GetError(); }
            // new(HardpointSize) UNCONDITIONALLY (a zero length gives a zero-byte block),
            // untested, and stored before the stream bound check.
            E->Hardpoint = static_cast<char*>(
                ::operator new((unsigned int)E->HardpointSize, std::nothrow));
            if (!Stream.GetBytes(E->Hardpoint, E->HardpointSize)) {
                return Stream.GetError();
            }
            Record->Equipment.PushBack(E);
        }
        if (!Stream.GetDword(Scratch)) {                             // rec+0x270
            return Stream.GetError();
        }
        for (unsigned int i = 0; i < Scratch; ++i) {
            // The node is spliced in FIRST, its value initialised from the stack pair {count
            // just read, residue}; the residue is overwritten by the dword read (0 here). The
            // word read overwrites only the low half of the first dword, so its high half
            // keeps the count's high half.
            FRpcCollisionGroupNode* Group =
                RpcCollisionGroupListPushBack(&Record->CollisionGroups, Scratch, 0u);
            unsigned short GroupId = 0;
            if (!Stream.GetWord(GroupId)) {
                return Stream.GetError();
            }
            Group->Id = (Group->Id & 0xffff0000u) | (unsigned int)GroupId;
            if (!Stream.GetDword(Group->HealthBits)) {
                return Stream.GetError();
            }
        }
        Wire.Records.PushBack(Record);
    }
    Stream.GetDword(Wire.SaveFailureCount);                               // packet+0x20
    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    if (!Stream.Failed()) {
        RpcRecordDecodedPacket(PacketId, Stream.Length());
    }
    return Stream.GetError();
}

// ===========================================================================
// The hardpoint-name dictionary -- FUN_06da2820 (build), FUN_06da2980 (name -> code),
// FUN_06da2a80 (code -> name).
//
// Only the CREATESHIP/CREATESOLAR equipment codec uses it, as a name compressor: a known
// hardpoint ships as one byte instead of a length byte plus the string.
//
// The dictionary is a fixed, null-terminated array of char* in rpclocal's .data
// (06dbb770..06dbb814, terminator 06dbb818); it is not read from a file and cannot grow.
// FUN_06da2820 runs once from the serializer's construction (06d95f07), filling the
// std::map at .data:06dbbf64 and the count at .data:06dbbf78. Codes start at 1:
// FUN_06da2a80 indexes DAT_06dbb76c[code], so code 0 means "not in the dictionary".
//
// The gap is retail's, not a transcription slip: there is no HpRunningLight08 or 09.
// ===========================================================================
static const char* const GRpcHardpointNames[] = {
    nullptr,               // code 0 -- DAT_06dbb76c itself, never a valid hardpoint
    "HpCM01",              // 06dbb770
    "HpCargo01",
    "HpCargo02",
    "HpContrail01",
    "HpContrail02",
    "HpDockLight01",
    "HpDockLight02",
    "HpDockLight03",
    "HpHeadlight",
    "HpMine01",
    "HpRunningLight01",
    "HpRunningLight02",
    "HpRunningLight03",
    "HpRunningLight04",
    "HpRunningLight05",
    "HpRunningLight06",
    "HpRunningLight07",
    "HpRunningLight10",    // no 08 / 09 in retail
    "HpRunningLight11",
    "HpRunningLight12",
    "HpRunningLight13",
    "HpShield01",
    "HpThruster01",
    "HpTurret01",
    "HpTurret02",
    "HpTurret03",
    "HpTurret04",
    "HpTurret05",
    "HpTurret06",
    "HpTurret07",
    "HpTurret08",
    "HpTurret09",
    "HpTurret_U1_01",
    "HpTurret_U1_02",
    "HpTurret_U1_03",
    "HpTurret_U1_04",
    "HpTurret_U1_05",
    "HpTurret_U3_01",
    "HpWeapon01",
    "HpWeapon02",
    "HpWeapon03",
    "HpWeapon04",          // 06dbb814
    nullptr                // 06dbb818, the terminator
};

// .data:06dbbf64, a CRT-initialised global as in retail. Its builder runs from the
// serializer's constructor (FUN_06d95e50 / RpcGetPacketSerializer, reached only from
// IServerImpl::Startup, IClient slot 10 or DumpPacketStats), long after static initialisation.
static std::map<std::string, int> GRpcHardpointMap;

// .data:06dbbf78, the count FUN_06da2820 leaves: 42.
static unsigned int GRpcHardpointCount = 0;

// FUN_06da2820. Codes start at 1.
void RpcBuildHardpointDictionary() {
    GRpcHardpointCount = 0;
    int Code = 1;
    for (const char* const* Entry = &GRpcHardpointNames[1]; *Entry != nullptr; ++Entry) {
        GRpcHardpointMap[std::string(*Entry)] = Code;
        ++Code;
        ++GRpcHardpointCount;
    }
}

unsigned int RpcHardpointDictionaryCount() {
    return GRpcHardpointCount;
}

// FUN_06da2980: the map's find on a std::string of the argument; 0 on a miss.
unsigned int RpcHardpointNameToCode(const char* Name) {
    std::map<std::string, int>::const_iterator It = GRpcHardpointMap.find(std::string(Name));
    if (It == GRpcHardpointMap.end()) {
        return 0;
    }
    return static_cast<unsigned int>(It->second);
}

// FUN_06da2a80. The upper bound is an UNSIGNED compare against the BUILT count, so the valid
// range is [1, count].
const char* RpcHardpointCodeToName(unsigned int Code) {
    if (Code == 0) {
        return nullptr;
    }
    if (Code > GRpcHardpointCount) {
        return nullptr;
    }
    return GRpcHardpointNames[Code];
}

// ===========================================================================
// ch2 0x04 CREATESHIP / 0x42 CREATESOLAR -- the shared 0x7c-byte header codec
//
// Encoder FUN_06da3720, decoder FUN_06da4020. Neither is a whole packet codec: both are
// called from the concrete type's Encode/Decode slot (06d97d20 / 06d98250 for CREATESHIP,
// 06dac7c0 / 06daca80 for CREATESOLAR) after the routing header, and each type's own tail
// follows on return. Every byte below belongs to FRpcCreateObjectWire (Wire+0xNN ==
// packet+0x20+0xNN) and stops at Wire+0x6c: the DamageList's four tail members
// (packet+0x8c..0x98) are NOT touched; only the node chain and the Size at packet+0x88 travel.
//
// Both take three stack arguments that they never read; they run entirely off the packet's
// four marshalling dwords, so the port passes only the FRpcMarshalStream view.
//
// RETURN: 0, or the direction's sticky error (0x16 encode, 0x17 decode).
// ===========================================================================

// The per-equipment-record flag byte (built at 06da3ba3, taken apart at 06da4479). Every bit
// is a wire-width or presence switch.
static const unsigned char kEquipFlagEquipped      = 0x01;
static const unsigned char kEquipFlagMission       = 0x02;
static const unsigned char kEquipFlagWideCount     = 0x04;
static const unsigned char kEquipFlagWideId        = 0x08;
static const unsigned char kEquipFlagHardpointText = 0x10;
static const unsigned char kEquipFlagNoHardpoint   = 0x20;
static const unsigned char kEquipFlagUnitStatus    = 0x40;
static const unsigned char kEquipFlagUnitCount     = 0x80;

// 06da3ba0 -- one equipment record.
static void RpcEncodeCreateObjectEquip(FRpcMarshalStream& Stream,
                                       const FRpcEquipWireRecord* Record) {
    unsigned char Flags = 0;
    if (Record->bEquipped != 0) {
        Flags |= kEquipFlagEquipped;
    }
    if (Record->bMission != 0) {
        Flags |= kEquipFlagMission;
    }
    // A SIGNED compare on the int Count.
    if (Record->Count > 255) {
        Flags |= kEquipFlagWideCount;
    }
    // UNSIGNED, on the ushort Id.
    if (Record->Id > 255) {
        Flags |= kEquipFlagWideId;
    }
    // No hardpoint at all, a dictionary hit, or a literal string.
    unsigned char HardpointCode = 0;
    if (Record->Hardpoint == nullptr) {
        Flags |= kEquipFlagNoHardpoint;
    } else {
        HardpointCode =
            static_cast<unsigned char>(RpcHardpointNameToCode(Record->Hardpoint) & 0xffu);
        if (HardpointCode == 0) {
            Flags |= kEquipFlagHardpointText;
        }
    }
    if (Record->Count == 1) {
        Flags |= kEquipFlagUnitCount;
    }
    // float vs double 1.0 (.rdata:06db3c58): the bit is set only on equality, so an
    // undamaged item ships no status byte.
    if (static_cast<double>(Record->Status) == 1.0) {
        Flags |= kEquipFlagUnitStatus;
    }

    Stream.PutByte(Flags);
    if ((Flags & kEquipFlagUnitCount) == 0) {
        if ((Flags & kEquipFlagWideCount) != 0) {
            Stream.PutDword(static_cast<unsigned int>(Record->Count));
        } else {
            Stream.PutByte(static_cast<unsigned char>(
                static_cast<unsigned int>(Record->Count) & 0xffu));
        }
    }
    if ((Flags & kEquipFlagUnitStatus) == 0) {
        // Status * 255.0 (DAT_06db60f0), ftol, one byte.
        Stream.PutQuantized(Record->Status, FRpcMarshalStream::kPercentScale);
    }
    Stream.PutWord(Archetype::LargeIDToSmallID(Record->ArchId));
    if ((Flags & kEquipFlagWideId) != 0) {
        Stream.PutWord(Record->Id);
    } else {
        Stream.PutByte(static_cast<unsigned char>(Record->Id & 0xffu));
    }
    if ((Flags & kEquipFlagNoHardpoint) != 0) {
        return;
    }
    if ((Flags & kEquipFlagHardpointText) != 0) {
        // Retail sends the LOW BYTE of HardpointSize as the length but the whole WORD's worth
        // of bytes, so a name of 256 bytes or more would ship a truncated length with a
        // full-length body. Retail's own names top out at fifteen characters; reproduced.
        Stream.PutByte(static_cast<unsigned char>(Record->HardpointSize & 0xffu));
        Stream.PutBytes(Record->Hardpoint, Record->HardpointSize);
    } else {
        Stream.PutByte(HardpointCode);
    }
}

// 06da4421 -- the same record, read back.
static bool RpcDecodeCreateObjectEquip(FRpcMarshalStream& Stream,
                                       FRpcEquipWireRecord* Record) {
    unsigned char Flags = 0;
    if (!Stream.GetByte(Flags)) {
        return false;
    }
    // Both bits land as a plain 0/1, not as the mask.
    Record->bMission = static_cast<unsigned char>((Flags >> 1) & 0x01u);
    Record->bEquipped = static_cast<unsigned char>(Flags & 0x01u);

    unsigned int Scratch = 0;
    if ((Flags & kEquipFlagUnitCount) != 0) {
        Record->Count = 1;
    } else if ((Flags & kEquipFlagWideCount) != 0) {
        if (!Stream.GetDword(Scratch)) {
            return false;
        }
        Record->Count = static_cast<int>(Scratch);
    } else {
        unsigned char Narrow = 0;
        if (!Stream.GetByte(Narrow)) {
            return false;
        }
        Record->Count = static_cast<int>(Narrow);                        // zero-extended
    }

    if ((Flags & kEquipFlagUnitStatus) != 0) {
        // Exactly 1.0f, the constant the encoder compared against.
        Record->Status = 1.0f;
    } else if (!Stream.GetQuantizedUnsigned(Record->Status,
                                            FRpcMarshalStream::kPercentInvScale)) {
        return false;
    }

    unsigned short Small = 0;
    if (!Stream.GetWord(Small)) {
        return false;
    }
    Record->ArchId = Archetype::SmallIDToLargeID(Small);

    if ((Flags & kEquipFlagWideId) != 0) {
        if (!Stream.GetWord(Record->Id)) {
            return false;
        }
    } else {
        unsigned char Narrow = 0;
        if (!Stream.GetByte(Narrow)) {
            return false;
        }
        Record->Id = static_cast<unsigned short>(Narrow);                // zero-extended
    }

    if ((Flags & kEquipFlagNoHardpoint) != 0) {
        // Both fields cleared again, though the record had already been zeroed.
        Record->HardpointSize = 0;
        Record->Hardpoint = nullptr;
        return true;
    }
    unsigned char Raw = 0;
    if ((Flags & kEquipFlagHardpointText) != 0) {
        if (!Stream.GetByte(Raw)) {
            return false;
        }
        Record->HardpointSize = static_cast<unsigned short>(Raw);
        if (Record->HardpointSize == 0) {
            return true;
        }
        // Untested allocation, stored at once, bound checked after.
        Record->Hardpoint = static_cast<char*>(
            ::operator new(Record->HardpointSize, std::nothrow));
        return Stream.GetBytes(Record->Hardpoint, Record->HardpointSize);
    }
    if (!Stream.GetByte(Raw)) {
        return false;
    }
    // An out-of-range code simply leaves the record hardpointless, which is how retail
    // survives a dictionary mismatch between the two ends of the wire.
    const char* Name = RpcHardpointCodeToName(static_cast<unsigned int>(Raw));
    if (Name == nullptr) {
        return true;
    }
    // strlen+1, stored WITH the terminator; the copy includes the terminator too.
    unsigned int Length = 0;
    while (Name[Length] != '\0') {
        ++Length;
    }
    ++Length;
    Record->HardpointSize = static_cast<unsigned short>(Length & 0xffffu);
    // Untested allocation of the stored WORD.
    Record->Hardpoint = static_cast<char*>(
        ::operator new(static_cast<unsigned int>(Record->HardpointSize), std::nothrow));
    unsigned int Index = 0;
    do {
        Record->Hardpoint[Index] = Name[Index];
    } while (Name[Index++] != '\0');
    return true;
}

// The decoder's collision-group node splice (06da4764) is RpcCollisionGroupListPushBack above.

unsigned int RpcEncodeCreateObjectCommon(FRpcMarshalStream& Stream,
                                         const FRpcCreateObjectWire* Wire) {
    Stream.PutDword(Wire->ObjId);
    // The archetype id goes out narrowed to 16 bits and comes back through SmallIDToLargeID.
    Stream.PutWord(Archetype::LargeIDToSmallID(Wire->ArchetypeNickname));
    Stream.PutDword(Wire->DockingBaseId);
    Stream.PutDword(static_cast<unsigned int>(Wire->VibeGroupId));
    // The swap is retail's: packet+0x34 (Wire+0x14) goes out BEFORE packet+0x30.
    Stream.PutDword(Wire->CostumeBody);
    Stream.PutDword(Wire->CostumeHead);
    // The count goes as a BYTE off the int field.
    Stream.PutByte(static_cast<unsigned char>(
        static_cast<unsigned int>(Wire->IdCount) & 0xffu));
    // IdCount DWORDS, checked and copied as IdCount*4 bytes.
    Stream.PutBytes(Wire->Ids, static_cast<unsigned int>(Wire->IdCount) * 4u);
    Stream.PutDword(Wire->VoiceId);
    // One 12-byte check, then a plain three-dword copy (FUN_06d8f510), not a quantizer.
    Stream.PutBytes(&Wire->PosX, 12u);
    // Four ftol(f * 127.0) bytes. Memory order is w,x,y,z but the wire order is x,y,z,w --
    // OrientW goes LAST.
    Stream.PutQuantized(Wire->OrientX, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->OrientY, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->OrientZ, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->OrientW, FRpcMarshalStream::kOrientationScale);
    {
        // 06da3a9c, the hull fraction: HitPts read as a SIGNED int and rounded to float
        // before the divide by GetArchMaxHitPts(ArchetypeNickname), then * 255.0, ftol, one
        // byte.
        const float HullPoints = static_cast<float>(static_cast<int>(Wire->HitPts));
        const int MaxHitPts = Archetype::GetArchMaxHitPts(Wire->ArchetypeNickname);
        unsigned char Percent = 0;
        if (MaxHitPts != 0) {
            const double Scaled = (static_cast<double>(HullPoints) /
                                   static_cast<double>(MaxHitPts)) *
                                  FRpcMarshalStream::kPercentScale;
            Percent = static_cast<unsigned char>(
                static_cast<unsigned long>(static_cast<long>(Scaled)) & 0xffu);
        }
        // MaxHitPts == 0 cannot be reached through Common.dll (GetArchMaxHitPts defaults
        // to 1 on a miss). If it ever were, retail's FDIVR would produce an infinity and
        // its ftol would return 0x80000000, whose low byte is 0 -- which is the byte the
        // branch above emits. So this is retail's result without retail's undefined cast.
        Stream.PutByte(Percent);
    }
    if (SinglePlayer()) {
        Stream.PutDword(Wire->LoadoutId);
    }
    // The handle's list pointer is null until the first push_back. A null list ships a zero
    // COUNT and no records, which is not the same code path as a present-but-empty list.
    const FRpcEquipWireListHeader* Equip = Wire->Equipment.GetList();
    if (Equip == nullptr) {
        Stream.PutWord(static_cast<unsigned short>(0));
    } else {
        // The LOW WORD of the list's Size.
        Stream.PutWord(static_cast<unsigned short>(Equip->Size & 0xffffu));
        // The dummy is used untested, and so is each node's record (unlike CHARACTERINFO's
        // encoder, which tests it). RpcCreateObjectHeaderWrite builds every list, so every
        // dummy exists here.
        for (const FRpcEquipWireNode* Node = Equip->Head->Next;
             Node != Equip->Head && !Stream.Failed(); Node = Node->Next) {
            RpcEncodeCreateObjectEquip(Stream, Node->Value);
        }
    }
    // The LOW BYTE of the list's size (packet+0x7c): the collision-group count is ONE BYTE
    // here, unlike CHARACTERINFO's dword.
    Stream.PutByte(static_cast<unsigned char>(Wire->CollisionGroups.size() & 0xffu));
    for (std::list<CollisionGroupDesc>::const_iterator Group = Wire->CollisionGroups.begin();
         Group != Wire->CollisionGroups.end() && !Stream.Failed(); ++Group) {
        // One byte of the Id, where CHARACTERINFO sends a word.
        Stream.PutByte(static_cast<unsigned char>(Group->Id & 0xffu));
        // Health, a FLOAT in [0,1], quantized * 255 (CHARACTERINFO sends it raw).
        Stream.PutQuantized(Group->Health, FRpcMarshalStream::kPercentScale);
    }
    if (!SinglePlayer()) {
        return Stream.GetError();
    }
    // The DamageList's size (packet+0x88) as a whole dword.
    Stream.PutDword(static_cast<unsigned int>(Wire->Damage.Entries.size()));
    for (std::list<DamageEntry>::const_iterator Entry = Wire->Damage.Entries.begin();
         Entry != Wire->Damage.Entries.end() && !Stream.Failed(); ++Entry) {
        // SubObjId (value+0x00), Fate (value+0x08), HitPts (value+0x04) -- the middle field
        // goes out LAST.
        Stream.PutWord(Entry->SubObjId);
        Stream.PutDword(static_cast<unsigned int>(Entry->Fate));
        Stream.PutFloat(Entry->HitPts);
    }
    return Stream.GetError();
}

unsigned int RpcDecodeCreateObjectCommon(FRpcMarshalStream& Stream,
                                         FRpcCreateObjectWire* Wire) {
    if (!Stream.GetDword(Wire->ObjId)) {
        return Stream.GetError();
    }
    unsigned short Small = 0;
    if (!Stream.GetWord(Small)) {
        return Stream.GetError();
    }
    Wire->ArchetypeNickname = Archetype::SmallIDToLargeID(Small);
    if (!Stream.GetDword(Wire->DockingBaseId)) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->VibeGroupId = static_cast<int>(Scratch);
    if (!Stream.GetDword(Wire->CostumeBody)) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->CostumeHead)) {
        return Stream.GetError();
    }
    // The two fields the encoder never sends are cleared here, the only thing in either
    // direction that touches packet+0x38 / +0x3c.
    Wire->CostumeLeftHand = 0;
    Wire->CostumeRightHand = 0;

    unsigned char Narrow = 0;
    if (!Stream.GetByte(Narrow)) {
        return Stream.GetError();
    }
    Wire->IdCount = static_cast<int>(Narrow);                            // zero-extended
    if (Wire->IdCount != 0) {
        // Untested allocation, stored at once.
        Wire->Ids = static_cast<unsigned int*>(
            ::operator new(static_cast<unsigned int>(Wire->IdCount) * 4u, std::nothrow));
        if (!Stream.GetBytes(Wire->Ids, static_cast<unsigned int>(Wire->IdCount) * 4u)) {
            return Stream.GetError();
        }
    }
    if (!Stream.GetDword(Wire->VoiceId)) {
        return Stream.GetError();
    }
    // One 12-byte check, then the plain three-dword copy (FUN_06d8f510), mirrored.
    if (!Stream.GetBytes(&Wire->PosX, 12u)) {
        return Stream.GetError();
    }
    {
        // Retail reads all FOUR bytes first and only then converts, so a short stream leaves
        // none of the four fields written.
        unsigned char Raw[4] = { 0, 0, 0, 0 };
        for (unsigned int Index = 0; Index < 4u; ++Index) {
            if (!Stream.GetByte(Raw[Index])) {
                return Stream.GetError();
            }
        }
        // Sign-extended, then * (1/127).
        Wire->OrientX = static_cast<float>(
            static_cast<double>(static_cast<signed char>(Raw[0])) *
            FRpcMarshalStream::kOrientationInvScale);
        Wire->OrientY = static_cast<float>(
            static_cast<double>(static_cast<signed char>(Raw[1])) *
            FRpcMarshalStream::kOrientationInvScale);
        Wire->OrientZ = static_cast<float>(
            static_cast<double>(static_cast<signed char>(Raw[2])) *
            FRpcMarshalStream::kOrientationInvScale);
        Wire->OrientW = static_cast<float>(
            static_cast<double>(static_cast<signed char>(Raw[3])) *
            FRpcMarshalStream::kOrientationInvScale);
    }
    {
        // 06da435d, the hull fraction back to absolute hit points: GetArchMaxHitPts *
        // (ZERO-extended byte * 1/255), ftol. The decoder multiplies by the reciprocal where
        // the encoder divided, and widens WITHOUT sign where the orientation bytes widened
        // WITH it.
        unsigned char Percent = 0;
        if (!Stream.GetByte(Percent)) {
            return Stream.GetError();
        }
        const double MaxHitPts =
            static_cast<double>(Archetype::GetArchMaxHitPts(Wire->ArchetypeNickname));
        const double Fraction = static_cast<double>(static_cast<unsigned int>(Percent)) *
                                FRpcMarshalStream::kPercentInvScale;
        Wire->HitPts = static_cast<unsigned int>(
            static_cast<long>(MaxHitPts * Fraction));
    }
    if (SinglePlayer()) {
        if (!Stream.GetDword(Wire->LoadoutId)) {
            return Stream.GetError();
        }
    }

    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < static_cast<unsigned int>(Count); ++Index) {
        FRpcEquipWireRecord* Record = static_cast<FRpcEquipWireRecord*>(
            ::operator new(sizeof(FRpcEquipWireRecord), std::nothrow));
        if (Record == nullptr) {
            // Retail SKIPS the record and keeps looping, leaving the stream pointing at a
            // record body it never reads.
            continue;
        }
        RpcContainerInitRecord(Record);
        if (!RpcDecodeCreateObjectEquip(Stream, Record)) {
            // A stream error: retail returns with the record not yet in the list -- leaked,
            // and so it is here.
            return Stream.GetError();
        }
        Wire->Equipment.PushBack(Record);
    }

    unsigned char GroupCount = 0;
    if (!Stream.GetByte(GroupCount)) {
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < static_cast<unsigned int>(GroupCount); ++Index) {
        // The node is spliced in FIRST, its value copied from an unwritten stack pair
        // (residue in retail, 0 here); the reads then fill it in place.
        CollisionGroupDesc Blank = {};
        Wire->CollisionGroups.push_back(Blank);
        CollisionGroupDesc& Group = Wire->CollisionGroups.back();
        unsigned char GroupId = 0;
        if (!Stream.GetByte(GroupId)) {
            return Stream.GetError();
        }
        // Only the Id WORD is written (a zero-extended byte).
        Group.Id = static_cast<unsigned short>(GroupId);
        float Health = 0.0f;
        if (!Stream.GetQuantizedUnsigned(Health,
                                         FRpcMarshalStream::kPercentInvScale)) {
            return Stream.GetError();
        }
        Group.Health = Health;
    }

    if (!SinglePlayer()) {
        return Stream.GetError();
    }
    unsigned int DamageCount = 0;
    if (!Stream.GetDword(DamageCount)) {
        return Stream.GetError();
    }
    // A zero count is simply the end of the packet.
    if (DamageCount == 0) {
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < DamageCount; ++Index) {
        // Retail appends a default DamageEntry { SUBOBJ_ID_NONE, -1.0f, 0 } (untested 0x14
        // node) and bumps the list's size (packet+0x88, the port's Count) first; the reads
        // then fill the node in place, so a short stream leaves the defaults in it.
        Wire->Damage.Entries.push_back(DamageEntry());
        ++Wire->Damage.Count;
        DamageEntry& Entry = Wire->Damage.Entries.back();
        if (!Stream.GetWord(Entry.SubObjId)) {
            return Stream.GetError();
        }
        unsigned int Fate = 0;
        if (!Stream.GetDword(Fate)) {
            return Stream.GetError();
        }
        Entry.Fate = static_cast<DamageEntry::SubObjFate>(Fate);
        // Four raw bytes into value+0x04, the float HitPts.
        if (!Stream.GetFloat(Entry.HitPts)) {
            return Stream.GetError();
        }
    }
    return Stream.GetError();
}

// ===========================================================================
// The four OUTER wire slots -- ch2 0x04 CREATESHIP and ch2 0x42 CREATESOLAR
//
//   0x06d97d20 CREATESHIP  encode   0x06d98250 CREATESHIP  decode
//   0x06dac7c0 CREATESOLAR encode   0x06daca80 CREATESOLAR decode
//
// Each is the packet vtable's own +0x04 / +0x08 entry. Shape, identical in all four:
//   1. fill packet+0x10..0x1c -- Cursor = 0, Limit = arg2, Ptr = the buffer, Error = 0;
//   2. the routing-header hook -- FUN_06da1380 out (two bytes, cursor := 2),
//      FUN_06da13c0 back (a no-op, the dispatcher already ate them);
//   3. the shared 0x7c header, FUN_06da3720 / FUN_06da4020;
//   4. this type's own tail, below;
//   5. FUN_06da13c0 again as the trailer hook, in BOTH directions;
//   6. *arg3 = the cursor, then the per-type statistics record.
//
// The DamageList's four tail members (packet+0x8c..0x98: bDestroyed, cause, inflictor id and
// inflictor owner) do NOT travel: no slot touches payload below +0x7c, and the shared codec
// stops at packet+0x88. They exist only on the in-process FUN_06d8c830 / FUN_06d84ed0 path.
//
// Argument asymmetry: encode's arg1 is a pointer TO the buffer pointer (06d97d37 / 06dac7d4
// dereference it); decode's arg1 IS the buffer (06d9826b / 06daca9a).
//
// The epilogue's statistics record (FUN_06da16b0 -> FUN_06da1730 on .data:DAT_06dbbf4c) is
// RpcRecordEncodedPacket / RpcRecordDecodedPacket (rpc_transport.cpp). The decode side's
// maximum really is `if (Cursor > Max + 2) Max = Cursor + 2` (06d985ea), NOT
// `if (Cursor + 2 > Max)`; both decoders emit it identically, so it is retail's.
// ===========================================================================

// Both create-object slots move packet+0xa0..0xa8 as ONE twelve-byte run, so the three
// members must be adjacent with no padding.
//
// The port's shared header is not retail's 0x7c bytes (its two containers are this
// toolchain's std::list and DamageList), so each tail is pinned RELATIVE to its first field:
// retail offset minus 0x7c on the right-hand side.
static_assert(offsetof(FPayloadServerCreateShip, bUsingTradelane) == sizeof(FRpcCreateObjectWire),
              "CREATESHIP's tail follows the shared header directly");
static_assert(offsetof(FPayloadServerCreateSolar, bDestructible) == sizeof(FRpcCreateObjectWire),
              "CREATESOLAR's tail follows the shared header directly");
static_assert(offsetof(FPayloadServerCreateShip, VelXBits) -
                  offsetof(FPayloadServerCreateShip, bUsingTradelane) == 0x80 - 0x7c,
              "the twelve-byte run starts at CREATESHIP payload+0x80");
static_assert(offsetof(FPayloadServerCreateShip, VelY) -
                  offsetof(FPayloadServerCreateShip, bUsingTradelane) == 0x84 - 0x7c,
              "FUN_06d8f510 moves packet+0xa0, +0xa4 and +0xa8 back to back");
static_assert(offsetof(FPayloadServerCreateShip, VelZ) -
                  offsetof(FPayloadServerCreateShip, bUsingTradelane) == 0x88 - 0x7c,
              "FUN_06d8f510 moves packet+0xa0, +0xa4 and +0xa8 back to back");
static_assert(offsetof(FPayloadServerCreateShip, Throttle) -
                  offsetof(FPayloadServerCreateShip, bUsingTradelane) == 0x98 - 0x7c,
              "the 127.0-quantized float is CREATESHIP payload+0x98");
static_assert(offsetof(FPayloadServerCreateShip, DisplayNameLength) -
                  offsetof(FPayloadServerCreateShip, bUsingTradelane) == 0xb8 - 0x7c,
              "the wide display name is length at payload+0xb8, buffer at +0xbc");
static_assert(sizeof(FPayloadServerCreateShip) -
                  offsetof(FPayloadServerCreateShip, bUsingTradelane) == 0xcc - 0x7c,
              "CREATESHIP's tail is 0x50 bytes (retail payload 0xcc)");
static_assert(offsetof(FPayloadServerCreateSolar, NameASize) -
                  offsetof(FPayloadServerCreateSolar, bDestructible) == 0x80 - 0x7c,
              "FUN_06da1620 is handed &payload+0x80 for CREATESOLAR's first name block");
static_assert(offsetof(FPayloadServerCreateSolar, NameBSize) -
                  offsetof(FPayloadServerCreateSolar, bDestructible) == 0x88 - 0x7c,
              "FUN_06da1620 is handed &payload+0x88 for CREATESOLAR's second name block");
static_assert(sizeof(FPayloadServerCreateSolar) -
                  offsetof(FPayloadServerCreateSolar, bDestructible) == 0x98 - 0x7c,
              "CREATESOLAR's tail is 0x1c bytes (retail payload 0x98)");

// The create-object encoders inline this pair twice each (CREATESHIP 06d9803b / 06d980d8,
// CREATESOLAR 06dac923 / 06dac9aa).
bool RpcPutSizedBlock(FRpcMarshalStream& Stream, unsigned int Size,
                      const void* Buffer) {
    if (!Stream.PutDword(Size)) {
        return false;
    }
    return Stream.PutBytes(Buffer, Size);
}

// FUN_06da1620, the decode-side counterpart, called with a pointer to the CONTIGUOUS
// {Size, Buffer} pair. Retail allocates BEFORE the second bounds check, so the pointer is
// stored even when the read then fails -- the packet's destructor frees it either way.
bool RpcGetSizedBlock(FRpcMarshalStream& Stream, unsigned int& OutSize,
                      void*& OutBuffer) {
    if (!Stream.GetDword(OutSize)) {
        return false;
    }
    OutBuffer = ::operator new(OutSize, std::nothrow);                   // untested
    return Stream.GetBytes(OutBuffer, OutSize);
}

// The CREATESHIP tail's one flag byte (built at 06d97d82, taken apart at 06d982d0). Bit 0x04
// is a presence switch as well as a value bit: it decides between the wide display name and
// the two flattened FmtStrs.
static const unsigned char kCreateShipFlagUsingTradelane = 0x01;  // payload +0x7c
static const unsigned char kCreateShipFlagMarkedBy = 0x02;        // payload +0xc8
static const unsigned char kCreateShipFlagOwnerPlayer = 0x04;     // payload +0x9c != 0

// ===========================================================================
// The four OUTER wire slots of the create-object pair
//
//   ch2 0x04 CREATESHIP   encode 0x06d97d20 (vtable .rdata:06db3fc8)
//                         decode 0x06d98250 (vtable .rdata:06db3fcc)
//   ch2 0x42 CREATESOLAR  encode 0x06dac7c0
//                         decode 0x06daca80 (vtable .rdata:06db52a4)
//
// These are the packet vtable's +0x04 / +0x08 entries themselves. Each one sets the
// four marshalling dwords up, runs the two-byte routing header hook, calls the shared
// 0x7c header codec above, ships its own tail and reports the byte count. They pass their
// three stack arguments straight through to FUN_06da3720 / FUN_06da4020, which read none.
//
// Same per-direction asymmetry as CHARACTERINFO: encode's first argument is a pointer TO
// the buffer pointer (never written back), decode's IS the buffer; encode's count
// includes the two routing bytes FUN_06da1380 writes, decode's (no-op hook FUN_06da13c0)
// excludes them.
// ===========================================================================
// 0x06d97d20 -- CREATESHIP, packet vtable +0x04.
unsigned int FPacket_FLPACKET_SERVER_CREATESHIP::Encode(unsigned char** BufferHolder,
                                                        unsigned int Capacity,
                                                        unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    // The two routing bytes; the cursor starts at 2.
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    // Retail DISCARDS the shared header's return value and lets the sticky error stop the
    // first field below.
    RpcEncodeCreateObjectCommon(Stream, &Wire.Common);

    unsigned char Flags = 0;
    if (Wire.bUsingTradelane != 0) {                                          // byte test
        Flags |= kCreateShipFlagUsingTradelane;
    }
    if (Wire.OwnerPlayer != 0) {                                          // DWORD test
        Flags |= kCreateShipFlagOwnerPlayer;
    }
    if (Wire.bMarkedBy != 0) {                                          // byte test
        Flags |= kCreateShipFlagMarkedBy;
    }
    if (!Stream.PutByte(Flags)) {
        return Stream.GetError();
    }

    // One 12-byte check, then a plain three-dword move (FUN_06d8f510) of a stack copy of
    // packet+0xa0..0xa8, in memory order.
    unsigned char VectorBytes[12];
    memcpy(VectorBytes, &Wire.VelXBits, sizeof(VectorBytes));
    if (!Stream.PutBytes(VectorBytes, sizeof(VectorBytes))) {
        return Stream.GetError();
    }
    // Throttle * 127.0 (DAT_06db3fd8), ftol, one byte.
    if (!Stream.PutQuantized(Wire.Throttle, FRpcMarshalStream::kOrientationScale)) {
        return Stream.GetError();
    }
    if (!Stream.PutWord(Wire.GroupNameId)) {
        return Stream.GetError();
    }
    if (!Stream.PutByte(Wire.ReputationRank)) {
        return Stream.GetError();
    }

    if ((Flags & kCreateShipFlagOwnerPlayer) != 0) {
        // Only the LOW BYTE of the dword travels; the decoder zero-extends it back.
        if (!Stream.PutByte((unsigned char)(Wire.OwnerPlayer & 0xffu))) {
            return Stream.GetError();
        }
        // GroupId rides on this branch ONLY; the decoder leaves the packet's zero in place
        // when the bit is clear.
        if (!Stream.PutWord(Wire.GroupId)) {
            return Stream.GetError();
        }
        if (!SinglePlayer()) {
            // Retail writes the LOW BYTE of the dword length but copies `length * 2` bytes off
            // the full dword: its own desynchronisation for a name of 128 characters or more;
            // reproduced.
            if (!Stream.PutByte((unsigned char)(Wire.DisplayNameLength & 0xffu))) {
                return Stream.GetError();
            }
            if (!Stream.PutBytes(Wire.DisplayName, Wire.DisplayNameLength * 2u)) {
                return Stream.GetError();
            }
        }
    } else if (!SinglePlayer()) {
        if (!RpcPutSizedBlock(Stream, Wire.NameASize, Wire.NameABuffer)) {
            return Stream.GetError();
        }
        if (!RpcPutSizedBlock(Stream, Wire.NameBSize, Wire.NameBBuffer)) {
            return Stream.GetError();
        }
    }

    // Retail asks SinglePlayer() a second time here (a pure read, so the same answer).
    if (!SinglePlayer()) {
        if (!Stream.PutDword(Wire.Affiliation)) {
            return Stream.GetError();
        }
        // The attitude, [-1,+1], * 127.0.
        if (!Stream.PutQuantized(Wire.Attitude, FRpcMarshalStream::kOrientationScale)) {
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

// 0x06d98250 -- CREATESHIP, packet vtable +0x08.
unsigned int FPacket_FLPACKET_SERVER_CREATESHIP::Decode(const unsigned char* Buffer,
                                                        unsigned int Size, unsigned int* OutLength) {
    // The buffer pointer is taken with no dereference; the header hook reads nothing.
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // The shared header's return value is discarded, as on the encode side.
    RpcDecodeCreateObjectCommon(Stream, &Wire.Common);

    unsigned char Flags = 0;
    if (!Stream.GetByte(Flags)) {
        return Stream.GetError();
    }
    // Both bits store the literal 1, and a clear bit leaves the field alone rather than
    // zeroing it (the packet arrives zeroed).
    if ((Flags & kCreateShipFlagUsingTradelane) != 0) {
        Wire.bUsingTradelane = 1;
    }
    if ((Flags & kCreateShipFlagMarkedBy) != 0) {
        Wire.bMarkedBy = 1;
    }

    // The same three dwords, read back in place.
    if (!Stream.GetBytes(&Wire.VelXBits, 12u)) {
        return Stream.GetError();
    }
    // SIGNED * 1/127 (DAT_06db3fe0), which is what makes the byte cover [-1,+1].
    if (!Stream.GetQuantizedSigned(Wire.Throttle,
                                   FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    if (!Stream.GetWord(Wire.GroupNameId)) {
        return Stream.GetError();
    }
    if (!Stream.GetByte(Wire.ReputationRank)) {
        return Stream.GetError();
    }

    if ((Flags & kCreateShipFlagOwnerPlayer) != 0) {
        unsigned char Narrow = 0;
        if (!Stream.GetByte(Narrow)) {
            return Stream.GetError();
        }
        // ZERO-extended into the whole dword.
        Wire.OwnerPlayer = (unsigned int)Narrow;
        if (!Stream.GetWord(Wire.GroupId)) {
            return Stream.GetError();
        }
        if (!SinglePlayer()) {
            unsigned char LengthByte = 0;
            if (!Stream.GetByte(LengthByte)) {
                return Stream.GetError();
            }
            // 06d98461: retail SIGN-extends this count and uses the signed value for the
            // stored length, the untested new(2*n + 2) and the byte run. A byte of 0x80..0xff
            // therefore wraps the cursor backwards past the bound check and copies a wrapped
            // count -- reproduced as it is.
            const unsigned int Length = (unsigned int)(int)(signed char)LengthByte;
            Wire.DisplayNameLength = Length;
            Wire.DisplayName = static_cast<wchar_t*>(
                ::operator new((Length * 2u) + 2u, std::nothrow));
            if (!Stream.GetBytes(Wire.DisplayName, Length * 2u)) {
                return Stream.GetError();
            }
            Wire.DisplayName[Length] = L'\0';
        }
    } else if (!SinglePlayer()) {
        if (!RpcGetSizedBlock(Stream, Wire.NameASize, Wire.NameABuffer)) {
            return Stream.GetError();
        }
        if (!RpcGetSizedBlock(Stream, Wire.NameBSize, Wire.NameBBuffer)) {
            return Stream.GetError();
        }
    }

    if (!SinglePlayer()) {
        if (!Stream.GetDword(Wire.Affiliation)) {
            return Stream.GetError();
        }
        // Signed * 1/127, same as Throttle.
        if (!Stream.GetQuantizedSigned(Wire.Attitude,
                                       FRpcMarshalStream::kOrientationInvScale)) {
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// 0x06dac7c0 -- CREATESOLAR, packet vtable +0x04. Shorter than CREATESHIP and with NO flag
// byte: its two leading bytes go out unconditionally, and the reputation pair comes BEFORE
// the two name blocks rather than after them.
unsigned int FPacket_FLPACKET_SERVER_CREATESOLAR::Encode(unsigned char** BufferHolder,
                                                         unsigned int Capacity,
                                                         unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    RpcEncodeCreateObjectCommon(Stream, &Wire.Common);
    if (!Stream.PutByte(Wire.bDestructible)) {
        return Stream.GetError();
    }
    if (!Stream.PutByte(Wire.bMarkedBy)) {
        return Stream.GetError();
    }
    if (!SinglePlayer()) {
        if (!Stream.PutDword(Wire.Affiliation)) {
            return Stream.GetError();
        }
        // Attitude * 127.0.
        if (!Stream.PutQuantized(Wire.Attitude, FRpcMarshalStream::kOrientationScale)) {
            return Stream.GetError();
        }
        if (!RpcPutSizedBlock(Stream, Wire.NameASize, Wire.NameABuffer)) {
            return Stream.GetError();
        }
        if (!RpcPutSizedBlock(Stream, Wire.NameBSize, Wire.NameBBuffer)) {
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

// 0x06daca80 -- CREATESOLAR, packet vtable +0x08.
unsigned int FPacket_FLPACKET_SERVER_CREATESOLAR::Decode(const unsigned char* Buffer,
                                                         unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // The shared header's result is discarded.
    RpcDecodeCreateObjectCommon(Stream, &Wire.Common);
    if (!Stream.GetByte(Wire.bDestructible)) {
        return Stream.GetError();
    }
    if (!Stream.GetByte(Wire.bMarkedBy)) {
        return Stream.GetError();
    }
    if (!SinglePlayer()) {
        if (!Stream.GetDword(Wire.Affiliation)) {
            return Stream.GetError();
        }
        // Signed * 1/127, like every quantized field in these tails (the 255.0 pair belongs
        // to the shared header codec).
        if (!Stream.GetQuantizedSigned(Wire.Attitude,
                                       FRpcMarshalStream::kOrientationInvScale)) {
            return Stream.GetError();
        }
        if (!RpcGetSizedBlock(Stream, Wire.NameASize, Wire.NameABuffer)) {
            return Stream.GetError();
        }
        if (!RpcGetSizedBlock(Stream, Wire.NameBSize, Wire.NameBBuffer)) {
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch1 0x02 FLPACKET_COMMON_FIREWEAPON -- encoder LAB_06da4f60, decoder LAB_06da5550, the
// FIREWEAPON class's +0x04 / +0x08 slots (vtable PTR_FUN_06db4388, set by ctor FUN_06d9afb0
// with routing header 0x10002). The only codec that changes a field's WIDTH by its value.
//
// Payload (from the ctor): +0x00 ObjectId, +0x04..+0x0c TargetPos (FLT_MAX each), +0x10 the
// hardpoint vector's allocator byte (never read or serialised), _First/_Last/_End at
// +0x14/+0x18/+0x1c -- reproduced by FRetailVector<unsigned short>.
//
// WIRE FORMAT: the routing word, the object id as a dword, then by COMMON.DLL::SinglePlayer
// (both directions consult it):
//   single player -- 12 raw bytes of TargetPos (FUN_06d8f510, a plain three-dword move), one
//       count byte, then one word per hardpoint id. No flag byte.
//   multiplayer -- one flag byte, then each of x, y, z in the width its flag bits chose, then
//       the count byte (unless bit 0x40 says there is exactly one id), then the ids.
//
// TIERS (.rdata): thresholds DAT_06db6130 64.0 and DAT_06db6128 8192.0; small tier
// v * 2^-6 (DAT_06db6124) * 127.0 (DAT_06db3fd8) to a signed byte; medium tier v * 2^-13
// (DAT_06db6120) * 32767.0 (DAT_06db6118) to a signed word; inverses DAT_06db6140 = 64/127 and
// DAT_06db6138 = 8192/32767 (the mantissas are exactly those repeating patterns). The port
// folds retail's two FMULs into one constant; the first factor is an exact power of two, so
// the rounding is identical.
//
// FOUR THINGS TO LEAVE ALONE, all of which read as mistakes and are not:
//  1. Z HAS NO MEDIUM TIER: x and y are tested against both thresholds, z against 64.0 only,
//     so bit 0x20 is never set by any encoder. Both the encoder's writer and the decoder still
//     handle it, so the branch is transcribed and left unreachable, as retail leaves it.
//     Filling in the tier would put bytes on the wire retail's encoder never produces.
//  2. THE TWO ONE-HARDPOINT TESTS DIFFER: bit 0x40 compares the whole int count (06da516d);
//     leaving the count byte off compares only its low byte (06da5466). They disagree at
//     exactly 257 hardpoints.
//  3. THE SINGLE-PLAYER BRANCH HAS NO 0x40 SHORTCUT: it writes the count byte unconditionally.
//  4. THE COUNT IS RECOMPUTED for the flag and again for the write (no caching).
//
// The decoder reads the count as an UNSIGNED byte and loops on a byte counter in both
// branches, so 0 reads nothing and 0xff reads 255 ids.
// ===========================================================================

// |v|, without <cmath>. Retail's FABS + FCOM agree on NaN: an unordered compare falls through
// to the raw-float tier, which is what `!(NaN < limit)` does here too.
static double RpcFireWeaponAbs(double Value) {
    return Value < 0.0 ? -Value : Value;
}

static const double kFireWeaponSmallLimit = 64.0;      // DAT_06db6130
static const double kFireWeaponMediumLimit = 8192.0;   // DAT_06db6128
// DAT_06db6124 * DAT_06db3fd8 and DAT_06db6120 * DAT_06db6118, folded (see above).
static const double kFireWeaponSmallScale = 127.0 / 64.0;
static const double kFireWeaponMediumScale = 32767.0 / 8192.0;
static const double kFireWeaponSmallInvScale = 64.0 / 127.0;      // DAT_06db6140
static const double kFireWeaponMediumInvScale = 8192.0 / 32767.0; // DAT_06db6138

static const unsigned char kFireWeaponXSmall = 0x01;
static const unsigned char kFireWeaponXMedium = 0x02;
static const unsigned char kFireWeaponYSmall = 0x04;
static const unsigned char kFireWeaponYMedium = 0x08;
static const unsigned char kFireWeaponZSmall = 0x10;
// Read by both codecs, written nowhere. See note 1 above.
static const unsigned char kFireWeaponZMedium = 0x20;
static const unsigned char kFireWeaponOneHardpoint = 0x40;

// The per-component width decision (06da50e8). bHasMediumTier is false for z.
static unsigned char RpcFireWeaponPickTier(float Value, unsigned char SmallBit,
                                           unsigned char MediumBit, bool bHasMediumTier) {
    const double Abs = RpcFireWeaponAbs(static_cast<double>(Value));
    if (Abs < kFireWeaponSmallLimit) {
        return SmallBit;
    }
    if (bHasMediumTier && Abs < kFireWeaponMediumLimit) {
        return MediumBit;
    }
    return 0;
}

// 06da51b2 and its y and z copies (retail's three copies differ only in field and bits).
static bool RpcFireWeaponPutComponent(FRpcMarshalStream& Stream, float Value,
                                      unsigned char Flags, unsigned char SmallBit,
                                      unsigned char MediumBit) {
    if ((Flags & SmallBit) != 0) {
        return Stream.PutQuantized(Value, kFireWeaponSmallScale);
    }
    if ((Flags & MediumBit) != 0) {
        return Stream.PutQuantizedWord(Value, kFireWeaponMediumScale);
    }
    // The raw float through x87.
    return Stream.PutFloat(Value);
}

// 06da5702 and its y and z copies.
static bool RpcFireWeaponGetComponent(FRpcMarshalStream& Stream, float& OutValue,
                                      unsigned char Flags, unsigned char SmallBit,
                                      unsigned char MediumBit) {
    if ((Flags & SmallBit) != 0) {
        return Stream.GetQuantizedSigned(OutValue, kFireWeaponSmallInvScale);
    }
    if ((Flags & MediumBit) != 0) {
        return Stream.GetQuantizedWordSigned(OutValue, kFireWeaponMediumInvScale);
    }
    // A dword move, not a conversion.
    return Stream.GetFloat(OutValue);
}

// ===========================================================================
// ch1 0x02 FLPACKET_COMMON_FIREWEAPON -- the adaptive-precision direction codec.
//
// Packet vtable PTR_FUN_06db4388 = { FUN_06d9b000, LAB_06da4f60, LAB_06da5550,
// IServerImpl::GetServerStats }, published by the type's constructor FUN_06d9afb0
// together with routing header 0x10002 (channel 1, opcode 2).
// ===========================================================================
// LAB_06da4f60 -- FIREWEAPON, packet vtable +0x04.
unsigned int FPacket_FLPACKET_COMMON_FIREWEAPON::Encode(unsigned char** BufferHolder,
                                                        unsigned int Capacity,
                                                        unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    if (!Stream.PutDword(Wire.ObjectId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    if (SinglePlayer()) {
        // A stack copy of the three floats through the plain three-dword move, in memory
        // order.
        unsigned char VectorBytes[12];
        memcpy(VectorBytes, &Wire.TargetPos, sizeof(VectorBytes));
        if (!Stream.PutBytes(VectorBytes, sizeof(VectorBytes))) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        // The count byte, unconditionally on this branch.
        const unsigned int SpCount = Wire.Hardpoints.Count();
        if (!Stream.PutByte((unsigned char)(SpCount & 0xffu))) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        for (unsigned int i = 0; i < SpCount; ++i) {
            if (!Stream.PutWord(Wire.Hardpoints.Begin[i])) {
                return FRpcMarshalStream::kEncodeOverflow;
            }
        }
    } else {
        unsigned char Flags = 0;
        Flags |= RpcFireWeaponPickTier(Wire.TargetPos.x, kFireWeaponXSmall,
                                       kFireWeaponXMedium, true);
        Flags |= RpcFireWeaponPickTier(Wire.TargetPos.y, kFireWeaponYSmall,
                                       kFireWeaponYMedium, true);
        // false: z really is tested against 64.0 only. See note 1 at the top.
        Flags |= RpcFireWeaponPickTier(Wire.TargetPos.z, kFireWeaponZSmall,
                                       kFireWeaponZMedium, false);
        // The full int is compared here, unlike the count-byte test below.
        if (Wire.Hardpoints.Begin != nullptr && Wire.Hardpoints.Count() == 1u) {
            Flags |= kFireWeaponOneHardpoint;
        }
        if (!Stream.PutByte(Flags)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }

        if (!RpcFireWeaponPutComponent(Stream, Wire.TargetPos.x, Flags,
                                       kFireWeaponXSmall, kFireWeaponXMedium)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!RpcFireWeaponPutComponent(Stream, Wire.TargetPos.y, Flags,
                                       kFireWeaponYSmall, kFireWeaponYMedium)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!RpcFireWeaponPutComponent(Stream, Wire.TargetPos.z, Flags,
                                       kFireWeaponZSmall, kFireWeaponZMedium)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }

        // The count is recomputed, and the byte is SKIPPED when its low byte is 1 -- the
        // decoder recovers that case from bit 0x40 instead.
        const unsigned int MpCount = Wire.Hardpoints.Count();
        const unsigned char CountByte = (unsigned char)(MpCount & 0xffu);
        if (!(Wire.Hardpoints.Begin != nullptr && CountByte == 1u)) {
            if (!Stream.PutByte(CountByte)) {
                return FRpcMarshalStream::kEncodeOverflow;
            }
        }
        for (unsigned int i = 0; i < MpCount; ++i) {
            if (!Stream.PutWord(Wire.Hardpoints.Begin[i])) {
                return FRpcMarshalStream::kEncodeOverflow;
            }
        }
    }

    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

// LAB_06da5550 -- FIREWEAPON, packet vtable +0x08.
unsigned int FPacket_FLPACKET_COMMON_FIREWEAPON::Decode(const unsigned char* Buffer,
                                                        unsigned int Size, unsigned int* OutLength) {
    // The decode side is handed the bytes, not a holder; the routing word is already gone,
    // so the cursor starts at the object id.
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.GetDword(Wire.ObjectId)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }

    // Retail rebuilds the hardpoint vector one element at a time at _Last, with
    // COMMON.DLL std::vector<unsigned short>::insert(_Last, 1, v) (IAT .rdata:06db3114) on the
    // single-player branch and the same routine inlined on the multiplayer branch.
    // RpcRetailVectorAppend is that insert, capacity policy and all. It APPENDS: a fresh
    // packet holds the three nulls FUN_06d9afb0 wrote.
    unsigned char Count = 0;
    if (SinglePlayer()) {
        // Twelve bytes straight into TargetPos.
        unsigned char VectorBytes[12];
        if (!Stream.GetBytes(VectorBytes, sizeof(VectorBytes))) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        memcpy(&Wire.TargetPos, VectorBytes, sizeof(VectorBytes));
        if (!Stream.GetByte(Count)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    } else {
        unsigned char Flags = 0;
        if (!Stream.GetByte(Flags)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        if (!RpcFireWeaponGetComponent(Stream, Wire.TargetPos.x, Flags,
                                       kFireWeaponXSmall, kFireWeaponXMedium)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        if (!RpcFireWeaponGetComponent(Stream, Wire.TargetPos.y, Flags,
                                       kFireWeaponYSmall, kFireWeaponYMedium)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        if (!RpcFireWeaponGetComponent(Stream, Wire.TargetPos.z, Flags,
                                       kFireWeaponZSmall, kFireWeaponZMedium)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        // Bit 0x40 stands in for a count byte of 1.
        if ((Flags & kFireWeaponOneHardpoint) != 0) {
            Count = 1;
        } else if (!Stream.GetByte(Count)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }

    // An unsigned byte count and a byte counter, so 0 reads nothing.
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        unsigned short Id = 0;
        if (!Stream.GetWord(Id)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        RpcRetailVectorAppend(Wire.Hardpoints, Id);
    }

    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x22 FLPACKET_CLIENT_DBGCREATESHIP -- LAB_06db0560 (+0x04) and LAB_06db0b30 (+0x08), the
// slots of vtable PTR_FUN_06db5d54 (set by ctor FUN_06da0e20 with routing header 0x30022;
// dtor FUN_06db0500). The layout is set out on FPayloadClientDbgCreateShip in rpc_payloads.h.
//
// Encode's arg1 is a pointer TO the buffer pointer; decode's arg1 IS the buffer.
//
// No quantizer: every field travels at its natural width; the two Vectors and the Quaternion
// go out as raw dwords through the plain 3- and 4-dword copiers.
//
// The zero-count shortcut is in both directions: each of the four blocks is skipped on a zero
// COUNT, so it consumes no bytes and, on the decode side, allocates nothing and leaves the
// pointer as the constructor left it.
// ===========================================================================

// ===========================================================================
// ch3 0x22 FLPACKET_CLIENT_DBGCREATESHIP -- LAB_06db0560 / LAB_06db0b30, slots +0x04
// and +0x08 of PTR_FUN_06db5d54 (ctor FUN_06da0e20, routing header 0x30022). The payload
// layout is on FPayloadClientDbgCreateShip in rpc_payloads.h.
//
// This type has no sender in the release image, so the codecs are reachable only from
// the network Encode/Decode path, never from the single-player queue.
// ===========================================================================
// LAB_06db0560 -- DBGCREATESHIP, packet vtable +0x04.
unsigned int FPacket_FLPACKET_CLIENT_DBGCREATESHIP::Encode(unsigned char** BufferHolder,
                                                           unsigned int Capacity,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    // The two routing bytes; the cursor starts at 2.
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));

    // Five plain dwords.
    if (!Stream.PutDword(Wire.Field_0x00) ||
        !Stream.PutDword(Wire.Field_0x04) ||
        !Stream.PutDword(Wire.Field_0x08) ||
        !Stream.PutDword(Wire.Field_0x0c) ||
        !Stream.PutDword(Wire.Field_0x10)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // Two twelve-byte runs through FUN_06d8f510 (a plain three-dword move), in memory order.
    if (!Stream.PutBytes(&Wire.Field_0x14, (unsigned int)sizeof(Vector)) ||
        !Stream.PutBytes(&Wire.Field_0x20, (unsigned int)sizeof(Vector))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // Sixteen bytes through FUN_06d8f4f0, the four-dword move.
    if (!Stream.PutBytes(&Wire.Orientation, (unsigned int)sizeof(Quaternion))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire.Field_0x3c)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // The wide name -- the CHARACTER count, then count*2 bytes. Retail does not null-check
    // the pointer.
    if (!Stream.PutDword(Wire.NameCharCount)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (Wire.NameCharCount != 0 &&
        !Stream.PutBytes(Wire.NameChars, Wire.NameCharCount * 2u)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // Two BYTE blocks, count then bytes.
    if (!Stream.PutDword(Wire.BlockASize)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (Wire.BlockASize != 0 && !Stream.PutBytes(Wire.BlockA, Wire.BlockASize)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire.BlockBSize)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (Wire.BlockBSize != 0 && !Stream.PutBytes(Wire.BlockB, Wire.BlockBSize)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // Four more plain dwords.
    if (!Stream.PutDword(Wire.Field_0x50) ||
        !Stream.PutDword(Wire.Field_0x54) ||
        !Stream.PutDword(Wire.Field_0x58) ||
        !Stream.PutDword(Wire.Field_0x5c)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // The dword array -- the ELEMENT count, then count*4 bytes.
    if (!Stream.PutDword(Wire.DwordArrayCount)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (Wire.DwordArrayCount != 0 &&
        !Stream.PutBytes(Wire.DwordArray, Wire.DwordArrayCount * 4u)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

// LAB_06db0b30 -- DBGCREATESHIP, packet vtable +0x08.
//
// Retail allocates each block BEFORE the bounds check that guards the copy and stores the
// pointer at once; the packet's destructor FUN_06db0500 frees it, so a half-decoded packet
// still releases cleanly.
unsigned int FPacket_FLPACKET_CLIENT_DBGCREATESHIP::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    // The buffer is taken WITHOUT a dereference; the routing word is already gone.
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    if (!Stream.GetDword(Wire.Field_0x00) ||
        !Stream.GetDword(Wire.Field_0x04) ||
        !Stream.GetDword(Wire.Field_0x08) ||
        !Stream.GetDword(Wire.Field_0x0c) ||
        !Stream.GetDword(Wire.Field_0x10)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }

    if (!Stream.GetBytes(&Wire.Field_0x14, (unsigned int)sizeof(Vector)) ||
        !Stream.GetBytes(&Wire.Field_0x20, (unsigned int)sizeof(Vector))) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (!Stream.GetBytes(&Wire.Orientation, (unsigned int)sizeof(Quaternion))) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (!Stream.GetDword(Wire.Field_0x3c)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }

    // The wide name.
    if (!Stream.GetDword(Wire.NameCharCount)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (Wire.NameCharCount != 0) {
        const unsigned int NameBytes = Wire.NameCharCount * 2u;
        Wire.NameChars = static_cast<wchar_t*>(
            ::operator new(NameBytes, std::nothrow));              // untested
        if (!Stream.GetBytes(Wire.NameChars, NameBytes)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }

    // The two byte blocks.
    if (!Stream.GetDword(Wire.BlockASize)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (Wire.BlockASize != 0) {
        Wire.BlockA = ::operator new(Wire.BlockASize, std::nothrow);    // untested
        if (!Stream.GetBytes(Wire.BlockA, Wire.BlockASize)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }
    if (!Stream.GetDword(Wire.BlockBSize)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (Wire.BlockBSize != 0) {
        Wire.BlockB = ::operator new(Wire.BlockBSize, std::nothrow);    // untested
        if (!Stream.GetBytes(Wire.BlockB, Wire.BlockBSize)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }

    if (!Stream.GetDword(Wire.Field_0x50) ||
        !Stream.GetDword(Wire.Field_0x54) ||
        !Stream.GetDword(Wire.Field_0x58) ||
        !Stream.GetDword(Wire.Field_0x5c)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }

    // The dword array.
    if (!Stream.GetDword(Wire.DwordArrayCount)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (Wire.DwordArrayCount != 0) {
        const unsigned int ArrayBytes = Wire.DwordArrayCount * 4u;
        Wire.DwordArray = static_cast<unsigned int*>(
            ::operator new(ArrayBytes, std::nothrow));               // untested
        if (!Stream.GetBytes(Wire.DwordArray, ArrayBytes)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }

    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch1 0x12 FLPACKET_COMMON_PLAYER_TRADE -- LAB_06da7030 (+0x04) and LAB_06da7430 (+0x08), the
// slots of vtable PTR_FUN_06db4498 (ctor FUN_06d9bad0, routing header 0x10012, registry size
// 0x44, payload 0x24). One packet type carrying eight logical messages, picked by SubType.
//
// THE SWITCH IS A JUMP TABLE, NOT A BITMASK (encoder 06da70b5, decoder 06da74b0): only the
// eight powers of two map to a case; anything else, including any COMBINATION of bits, goes
// to the default, which writes nothing beyond the sub-type byte. The two index tables differ
// only in which case body the compiler folded 0x80 / 0x20 onto; the bodies are identical.
//
// THE MP FLAG IS READ THE OPPOSITE WAY ROUND IN THE TWO DIRECTIONS (not a transcription slip):
// the encoder's flag is IsMPServer() (06da70ad), the decoder's !IsMPServer() (06da74a8). So
// the trailing ShipId dword is counted in the cursor only when the ENCODING end is the MP
// server and the DECODING end is not -- the same condition between a server and a client.
// The dword is always written by Encode and normally read by Decode (except Decode sub-type
// 0x40, which writes the packet's value into the input); only the cursor bump (bounds check,
// reported length) is conditional, hence the split Reserve / PutDwordUncounted /
// GetDwordUncounted.
//
// RETAIL SLIP, KEPT: the decoder's sub-type 0x40 bounds check stores the ENCODER's error
// code 0x16 (06da7541) where all its other checks store 0x17.
//
// FIELD WIDTHS:
//   payload+0x00 SubType       one BYTE out, read back ZERO-extended.
//   payload+0x04 Reason        one BYTE out, read back SIGN-extended. Sub-type 2 only, and
//                              sub-type 2 sends nothing else -- not even payload+0x08.
//   payload+0x08 ShipId        dword, every sub-type except 2.
//   payload+0x0c Money         dword, sub-type 4 only.
//   payload+0x10 bAccepted     one BYTE out, read back NORMALISED to 0/1. Sub-type 0x40 only.
//   payload+0x14 EquipArchId   dword, sub-types 8 and 0x10.
//   payload+0x18 EquipCount    dword, sub-types 8 and 0x10.
//   payload+0x1c EquipStatus   FLOAT out (x87), a plain dword move back. Sub-type 8 ONLY --
//                              sub-type 0x10's record is four bytes shorter.
//   payload+0x20 EquipId       WORD, sub-types 8 and 0x10.
// No quantizer appears anywhere in either body.
// ===========================================================================

// COMMON.DLL IsMPServer (IAT .rdata:06db3104). Already declared via common_goods.h ->
// common_net.h; restated so the codec's import is visible here (identical signature).
COMMON_API bool __cdecl IsMPServer();

// Sub-types, named after the IClient::SendCommonPlayerTrade* sender that sets each one.
static const unsigned int kPlayerTradeInitiateTrade = 0x01;
static const unsigned int kPlayerTradeTerminateTrade = 0x02;
static const unsigned int kPlayerTradeSetTradeMoney = 0x04;
static const unsigned int kPlayerTradeAddTradeEquip = 0x08;
static const unsigned int kPlayerTradeDelTradeEquip = 0x10;
static const unsigned int kPlayerTradeRequestTrade = 0x20;
static const unsigned int kPlayerTradeAcceptTrade = 0x40;
static const unsigned int kPlayerTradeStopTradeRequest = 0x80;

// ===========================================================================
// ch1 0x12 FLPACKET_COMMON_PLAYER_TRADE -- LAB_06da7030 / LAB_06da7430, slots +0x04 and
// +0x08 of PTR_FUN_06db4498 (ctor FUN_06d9bad0, routing header 0x10012). Eight sub-types
// behind one jump table; the inverted IsMPServer test and the decoder's one stray 0x16
// are described in rpc_containers.cpp.
// ===========================================================================
// LAB_06da7030 -- PLAYER_TRADE, packet vtable +0x04.
unsigned int FPacket_FLPACKET_COMMON_PLAYER_TRADE::Encode(unsigned char** BufferHolder,
                                                          unsigned int Capacity,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    // The sub-type goes out as ONE byte.
    if (!Stream.PutByte((unsigned char)(Wire.SubType & 0xffu))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    const bool bAccountTrailer = IsMPServer();

    // The trailing dword, shared by every sub-type except 2.
    bool bNeedTrailer = true;

    switch (Wire.SubType) {
    case kPlayerTradeAcceptTrade:                        // 0x40
        if (!Stream.PutByte(Wire.bAccepted)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        break;
    case kPlayerTradeTerminateTrade:                        // 0x02
        // The LOW BYTE of payload+0x04, and nothing more.
        if (!Stream.PutByte((unsigned char)(Wire.Reason & 0xffu))) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        bNeedTrailer = false;
        break;
    case kPlayerTradeSetTradeMoney:                       // 0x04
        if (!Stream.PutDword(Wire.Money)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        break;
    case kPlayerTradeAddTradeEquip:                    // 0x08
        if (!Stream.PutDword(Wire.EquipArchId) ||
            !Stream.PutDword(Wire.EquipCount) ||
            !Stream.PutFloat(Wire.EquipStatus) ||
            !Stream.PutWord(Wire.EquipId)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        break;
    case kPlayerTradeDelTradeEquip:                 // 0x10
        // No status float on this branch.
        if (!Stream.PutDword(Wire.EquipArchId) ||
            !Stream.PutDword(Wire.EquipCount) ||
            !Stream.PutWord(Wire.EquipId)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        break;
    case kPlayerTradeInitiateTrade:                      // 0x01 / 0x80
    case kPlayerTradeStopTradeRequest:
    case kPlayerTradeRequestTrade:                       // 0x20
        // These three reserve the trailer unconditionally, with no IsMPServer test.
        Stream.Reserve(4u, FRpcMarshalStream::kEncodeOverflow);
        if (Stream.Failed()) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!Stream.PutDwordUncounted(Wire.ShipId)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        bNeedTrailer = false;
        break;
    default:
        // Anything that is not one of the eight, including any combination of bits.
        bNeedTrailer = false;
        break;
    }

    if (bNeedTrailer) {
        if (bAccountTrailer) {
            Stream.Reserve(4u, FRpcMarshalStream::kEncodeOverflow);
        }
        if (Stream.Failed()) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!Stream.PutDwordUncounted(Wire.ShipId)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
    }

    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

// LAB_06da7430 -- PLAYER_TRADE, packet vtable +0x08.
unsigned int FPacket_FLPACKET_COMMON_PLAYER_TRADE::Decode(const unsigned char* Buffer,
                                                          unsigned int Size,
                                                          unsigned int* OutLength) {
    // The buffer is taken WITHOUT a dereference.
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    unsigned char SubTypeByte = 0;
    if (!Stream.GetByte(SubTypeByte)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    // Zero-extended, unlike the sub-type 2 field.
    Wire.SubType = (unsigned int)SubTypeByte;
    const bool bAccountTrailer = !IsMPServer();      // INVERTED relative to the encoder

    bool bNeedTrailer = true;

    switch (Wire.SubType) {
    case kPlayerTradeTerminateTrade: {                      // 0x02
        unsigned char Value = 0;
        if (!Stream.GetByte(Value)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        // Signed, where the sub-type byte above was not.
        Wire.Reason = (unsigned int)(int)(signed char)Value;
        bNeedTrailer = false;
        break;
    }
    case kPlayerTradeAcceptTrade: {                      // 0x40
        unsigned char Value = 0;
        if (!Stream.GetByte(Value)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        // Any non-zero byte comes back as 1.
        Wire.bAccepted = (unsigned char)(Value != 0 ? 1 : 0);
        // The ONE place in this decoder that uses the encoder's error code. Reproduced.
        if (bAccountTrailer) {
            Stream.Reserve(4u, FRpcMarshalStream::kEncodeOverflow);
        }
        if (Stream.Failed()) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        // Retail writes the EXISTING packet target into the input (06da755b); the pointer
        // advances four bytes even when MP omitted the Reserve.
        if (!Stream.PutDwordUncounted(Wire.ShipId)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        bNeedTrailer = false;
        break;
    }
    case kPlayerTradeSetTradeMoney:                       // 0x04
        if (!Stream.GetDword(Wire.Money)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        break;
    case kPlayerTradeAddTradeEquip: {                  // 0x08
        unsigned int Status = 0;
        if (!Stream.GetDword(Wire.EquipArchId) ||
            !Stream.GetDword(Wire.EquipCount) ||
            // A dword move, so the float's bits come back untouched.
            !Stream.GetDword(Status) ||
            !Stream.GetWord(Wire.EquipId)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        memcpy(&Wire.EquipStatus, &Status, sizeof(float));
        break;
    }
    case kPlayerTradeDelTradeEquip:                 // 0x10
        if (!Stream.GetDword(Wire.EquipArchId) ||
            !Stream.GetDword(Wire.EquipCount) ||
            !Stream.GetWord(Wire.EquipId)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        break;
    case kPlayerTradeInitiateTrade:                      // 0x01 / 0x20
    case kPlayerTradeRequestTrade:
    case kPlayerTradeStopTradeRequest:                         // 0x80
        Stream.Reserve(4u, FRpcMarshalStream::kDecodeOverrun);
        if (Stream.Failed()) {
            return Stream.GetError();
        }
        if (!Stream.GetDwordUncounted(Wire.ShipId)) {
            return Stream.GetError();
        }
        bNeedTrailer = false;
        break;
    default:
        bNeedTrailer = false;
        break;
    }

    if (bNeedTrailer) {
        if (bAccountTrailer) {
            Stream.Reserve(4u, FRpcMarshalStream::kDecodeOverrun);
        }
        if (Stream.Failed()) {
            return Stream.GetError();
        }
        if (!Stream.GetDwordUncounted(Wire.ShipId)) {
            return Stream.GetError();
        }
    }

    *OutLength = Stream.Length();
    // Retail runs the byte-statistics tail on the SUCCESS PATH ONLY. See FPacketWireStats.
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// FUN_06d9bad0, the PLAYER_TRADE constructor, zeroes packet+0x20..+0x3c except EquipArchId
// (payload+0x14) = 0xffffffff ("none"); that store lives in
// FPacket_FLPACKET_COMMON_PLAYER_TRADE's constructor in rpc_packet_registry.cpp.

// ===========================================================================
// ch2 0x05 FLPACKET_SERVER_DAMAGEOBJECT -- LAB_06d90480 / LAB_06d906c0, the slots of vtable
// 06db3cb0 (ctor FUN_06d9c8d0, routing header 0x20005, dtor FUN_06d909b0).
//
// Payload: +0x00 ObjectId, then a DamageList in place at +0x04 (allocator byte, dummy at
// +0x08, Size +0x0c, bDestroyed +0x10, Cause +0x14, InflictorId +0x18, InflictorOwner +0x1c).
//
// Wire layout -- THE ORDER IS NOT THE STRUCT ORDER; the list's count travels FOURTH:
//     dword  payload+0x00  ObjectId
//     byte   payload+0x10  bDestroyed
//     dword  payload+0x14  Cause
//     dword  payload+0x18  InflictorId
//     word   payload+0x0c  Entries.Size (low word)
//     per entry: word SubObjId, byte Fate (low byte), float HitPts
// payload+0x1c (InflictorOwner) never travels.
//
// The encoder LOADS all three entry fields before writing any, so the float is read FIRST and
// written LAST (its error exit pops the float still on the x87 stack). The decoder reads word,
// byte, float into a stack temporary and only then allocates the node, so it leaks nothing.
//
// Retail seeds that temporary with SUBOBJ_ID_NONE, -1.0f, 0 using a WORD store into the first
// DWORD slot, and copies the whole dword into the node: the upper two bytes (padding behind
// SubObjId) carry stack residue. The port's DamageEntry ctor builds the node; nothing reads
// that half.
//
// THE DECODER APPENDS: it never clears the list, the count is only the (unsigned) loop bound,
// and the size is incremented per element.
// ===========================================================================

// 0x06d90480 / 0x06d906c0 -- ch2 0x05 DAMAGEOBJECT: three scalars, then the list's count
// and one { word, byte, float } per DamageEntry. Lives in rpc_containers.cpp, next to the
// damage-list helpers it needs.
unsigned int FPacket_FLPACKET_SERVER_DAMAGEOBJECT::Encode(unsigned char** BufferHolder,
                                                          unsigned int Capacity,
                                                          unsigned int* OutLength) {
    const DamageList& Damage = Wire.Damage;

    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    if (!Stream.PutDword(Wire.ObjectId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // DamageList+0x0c, moved as a byte.
    if (!Stream.PutByte(Damage.bDestroyed ? 1u : 0u)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(static_cast<unsigned int>(Damage.Cause))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Damage.InflictorId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // Only the LOW WORD of the list's size.
    const unsigned int Count = static_cast<unsigned int>(Damage.Entries.size());
    if (!Stream.PutWord((unsigned short)(Count & 0xffffu))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // The list the packet ctor built, walked untested.
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It) {
        if (!Stream.PutWord(It->SubObjId)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        // The low byte of the fate.
        if (!Stream.PutByte((unsigned char)(static_cast<unsigned int>(It->Fate) & 0xffu))) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!Stream.PutFloat(It->HitPts)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_DAMAGEOBJECT::Decode(const unsigned char* Buffer,
                                                          unsigned int Size,
                                                          unsigned int* OutLength) {
    DamageList& Damage = Wire.Damage;

    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    if (!Stream.GetDword(Wire.ObjectId)) {
        return Stream.GetError();
    }
    unsigned char Destroyed = 0;
    if (!Stream.GetByte(Destroyed)) {
        return Stream.GetError();
    }
    Damage.bDestroyed = (Destroyed != 0);
    unsigned int Cause = 0;
    if (!Stream.GetDword(Cause)) {
        return Stream.GetError();
    }
    Damage.Cause = static_cast<DamageCause>(Cause);
    if (!Stream.GetDword(Damage.InflictorId)) {
        return Stream.GetError();
    }
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        // Retail seeds the temporary with SUBOBJ_ID_NONE, -1.0f and 0; all three are
        // overwritten below.
        DamageEntry Entry;
        if (!Stream.GetWord(Entry.SubObjId)) {
            return Stream.GetError();
        }
        unsigned char Fate = 0;
        if (!Stream.GetByte(Fate)) {
            return Stream.GetError();
        }
        Entry.Fate = static_cast<DamageEntry::SubObjFate>(Fate);
        if (!Stream.GetFloat(Entry.HitPts)) {
            return Stream.GetError();
        }
        // The node is made only AFTER all three fields are in hand; the list is APPENDED to
        // and its size (the port's DamageList::Count) incremented per element.
        Damage.Entries.push_back(Entry);
        ++Damage.Count;
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// The flattened FmtStr (ch1 0x1b, ch2 0x29, 0x34, 0x35, 0x45)
// ===========================================================================

unsigned int RpcFlattenedFmtStrSize(const FmtStr& Source) {
    unsigned int Total = 6;
    for (const FmtStr::Val* Node = Source.Head; Node != nullptr; Node = Node->Next) {
        Total += Node->get_flattened_size();
    }
    return Total;
}

// Every retail site (e.g. 06d88de7) stores the size and hands an untested new(size) straight
// to FmtStr::flatten.
void* RpcFlattenFmtStr(const FmtStr& Source, unsigned int& OutSize) {
    const unsigned int Size = RpcFlattenedFmtStrSize(Source);
    OutSize = Size;
    void* Block = ::operator new(Size, std::nothrow);
    Source.flatten(Block, Size);
    return Block;
}

__declspec(dllexport) void RpcUnflattenFmtStr(FmtStr& Dest, void* Buffer, unsigned int Size) {
    Dest.unflatten(Buffer, Size);
}

// The two CHARACTERINFO list destructors (FUN_06d852e0 / FUN_06d85340) are
// ~MetaList / ~OwnerList of OwnerList<CCharacterSet::CharacterInfo>
// (src/include/common_metalist.h + server_characterset.h).
