// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The retail container serializer of rpclocal.dll.
//
// Seven packet types (channel 2: 0x24 SETEQUIPMENT, 0x25 SETCARGO, 0x3d
// REMOTEOBJECTCARGOUPDATE, 0x48 SETCOLLISIONGROUPS; channel 3: 0x16 REQEQUIPMENT,
// 0x17 REQCARGO, 0x36 REQCOLLISIONGROUPS) carry a CONTAINER in their payload rather
// than a flat record. All seven go through the same two mechanisms below.
//
// ===========================================================================
// 1. The retail std::list layout (from FUN_06d8f710, the list assignment into the packet)
// ===========================================================================
//
//   header = { allocator (1 byte, padded to 4) @+0x00, Node* Head @+0x04,
//              size_t Size @+0x08 }  -- 0x0c bytes
//   node   = { Node* Next @+0x00, Node* Prev @+0x04, T Value @+0x08 }
//
// The allocator byte is a copy of an uninitialised temporary (FUN_06d913f0 at 0x06d9143e);
// nothing reads it back. SETCOLLISIONGROUPS / REQCOLLISIONGROUPS allocate 0x2c bytes (0x20
// base + exactly the 0x0c header), and their accessor FUN_06d8f7d0 returns packet+0x20: the
// list header IS the payload.
//
// Node sizes from the operator new() argument: 0x10 for CollisionGroupDesc (8 bytes of
// value); 0x0c for the equipment list, whose value is a POINTER -- a list OF POINTERS to
// heap records.
//
// ===========================================================================
// 2. The 0x18-byte equipment wire record
// ===========================================================================
//
// Built identically by FUN_06d8c750 (channel 2) and inlined in IServerImpl::ReqEquipment
// (0x06d8cf90) and ::ReqCargo (0x06d8d0e0): new(0x18), all fields zeroed, then from a
// Common.dll EquipDesc:
//
//   rec+0x00 <- EquipDesc+0x04      ArchId
//   rec+0x04 <- EquipDesc+0x14      Count
//   rec+0x08 <- EquipDesc+0x10      Health (a float, x87)
//   rec+0x0c <- EquipDesc+0x02      Id, 16 bits
//   rec+0x0e <- EquipDesc+0x0c      bEquipped, 8 bits
//   rec+0x0f <- EquipDesc+0x18      bMission -- only the CHARACTERINFO sender fills it;
//                                   the three above zero it and FUN_06d913f0 copies it
//   rec+0x10 <- strlen(Hardpoint)+1 as a 16-bit count, 0 when the item is internal
//   rec+0x14 <- operator new(that count) + strcpy of EquipDesc+0x08
//
// The hardpoint is copied only when EquipDesc::is_internal() says no: internal equipment
// ships WITHOUT its hardpoint name, and the receivers use that absence as the predicate for
// make_internal(). The receivers (FUN_06d87ec0/FUN_06d880a0 into an EquipDescList,
// FUN_06d84d60 into a std::vector<EquipDesc>) apply the fields back through Common.dll's
// set_arch_id, set_count, set_status, set_id, set_equipped, then set_hardpoint(StringAlloc)
// or make_internal(), which is what names the fields.
//
// Retail rpclocal.dll imports COMMON.DLL, DACOM.DLL, KERNEL32.DLL, MSVCP60.DLL and MSVCRT.DLL,
// and takes the EquipDesc accessors by name (.rdata:06db3070..06db308c, is_internal at
// 06db30d8); the port links Common.lib the same way. The module-private structs below are
// the WIRE layouts (the 0x18 record, the 0x0c/0x10/0x14 nodes, the 4-byte handle), which are
// rpclocal's own and not Common.dll types; the real Common types cross the interface.
#pragma once

#include "rpc_transport.h"
#include "rpc_payloads.h"
// EquipDesc / EquipDescList. Retail imports EquipDesc::EquipDesc (.rdata:06db3004), the
// accessors and std::list<EquipDesc>::_Buynode (06db30ac) by name.
#include "common_goods.h"
// The CREATESHIP / CREATESOLAR source record, shared with Server.dll and freelancer.exe.
#include "server_createinfo.h"
#include "server_characterset.h"   // CCharacterSet::CharacterInfo (ch2 0x03)

#include <list>
#include <new>
#include <string>
#include <vector>

// ===========================================================================
// VC6 std::vector<T>::insert(_Last, 1, Value) on a retail-layout vector
//
// Inlined at 06da5945 (ch1 FIREWEAPON decode, T = unsigned short) and 06da7ced (ch1
// TRACTOROBJECTS decode, T = unsigned int), and called out of line through COMMON.DLL's copy
// (IAT .rdata:06db3114). Without room the new capacity is `Size + max(Size, 1)` (a null
// _First counts as size 0), clamped at zero, with an untested allocation.
// ===========================================================================
template <typename T>
void RpcRetailVectorAppend(FRetailVector<T>& Vector, T Value) {
    if ((unsigned int)(Vector.Cap - Vector.End) >= 1u) {
        *Vector.End = Value;
        ++Vector.End;
        return;
    }
    const unsigned int Size =
        (Vector.Begin != nullptr) ? (unsigned int)(Vector.End - Vector.Begin) : 0u;
    unsigned int Growth = 1u;
    if (Vector.Begin != nullptr && Size > 1u) {
        Growth = Size;
    }
    int NewCapacity = (int)(Size + Growth);
    if (NewCapacity < 0) {
        NewCapacity = 0;
    }
    T* Block = static_cast<T*>(
        ::operator new((unsigned int)NewCapacity * (unsigned int)sizeof(T), std::nothrow));
    for (unsigned int i = 0; i < Size; ++i) {
        Block[i] = Vector.Begin[i];
    }
    Block[Size] = Value;
    ::operator delete(Vector.Begin);
    Vector.Cap = Block + NewCapacity;
    Vector.End = Block + Size + 1u;
    Vector.Begin = Block;
}

// ===========================================================================
// The retail std::list, laid out by hand
// ===========================================================================

// One node of a retail list whose value is a CollisionGroupDesc. operator new(0x10).
struct FRpcCollisionGroupNode {
    FRpcCollisionGroupNode* Next;   // +0x00
    FRpcCollisionGroupNode* Prev;   // +0x04
    // CollisionGroupDesc, 8 bytes: { ushort Id; ushort Pad; float Health; }. The
    // CREATESHIP/CREATESOLAR codec sends only the id's LOW BYTE and the health as a 255-scale
    // byte. The fields stay `unsigned int` because every other user moves the pair as raw
    // dwords; the codecs bit-cast. TODO: retype +0x04 to float.
    unsigned int Id;        // +0x08  low byte == CollisionGroupDesc::Id
    unsigned int HealthBits;        // +0x0c  float health fraction, bit pattern
};

static_assert(sizeof(FRpcCollisionGroupNode) == 0x10,
              "retail CollisionGroupDesc list node is operator new(0x10)");

// The 0x0c-byte list header. For the two COLLISIONGROUPS types this struct IS the
// payload, at packet+0x20, exactly as retail keeps it.
struct FRpcCollisionGroupList {
    unsigned char Allocator;        // +0x00  VC6 allocator<T>: one byte, never read back
    unsigned char Pad_0x01[3];      // +0x01
    FRpcCollisionGroupNode* Head;   // +0x04  dummy node; Next == Prev == itself when empty
    unsigned int Size;              // +0x08
};

static_assert(sizeof(FRpcCollisionGroupList) == 0x0c,
              "retail std::list header is 0x0c: allocator, head, size");

// The 8-byte element on its own, for callers that only want the value.
struct FRpcCollisionGroupDescMirror {
    unsigned int Id;
    unsigned int HealthBits;
};

static_assert(sizeof(FRpcCollisionGroupDescMirror) == 8,
              "CollisionGroupDesc mirror must be 8 like the real one");

// ===========================================================================
// The equipment wire record and its list
// ===========================================================================

// operator new(0x18); see the field derivation at the top of this file.
struct FRpcEquipWireRecord {
    unsigned int ArchId;            // +0x00 -> EquipDesc::set_arch_id
    int Count;                      // +0x04 -> EquipDesc::set_count
    float Status;                   // +0x08 -> EquipDesc::set_status (EquipDesc::Health)
    unsigned short Id;              // +0x0c -> EquipDesc::set_id
    unsigned char bEquipped;        // +0x0e -> EquipDesc::set_equipped
    // +0x0f: EquipDesc+0x18 (bMission), set and read back raw only by ch2 0x03 CHARACTERINFO
    // (sender 06d89d72, receiver 06d848fa); every other user zeroes or copies it.
    unsigned char bMission;       // +0x0f  EquipDesc::bMission
    unsigned short HardpointSize;   // +0x10  strlen + 1, or 0 for internal equipment
    unsigned char Pad_0x12[2];      // +0x12
    char* Hardpoint;                // +0x14  owned by the record
};

static_assert(sizeof(FRpcEquipWireRecord) == 0x18,
              "retail equipment wire record is operator new(0x18)");

// One node of the equipment list. operator new(0x0c) -- the value is a POINTER.
struct FRpcEquipWireNode {
    FRpcEquipWireNode* Next;        // +0x00
    FRpcEquipWireNode* Prev;        // +0x04
    FRpcEquipWireRecord* Value;     // +0x08
};

static_assert(sizeof(FRpcEquipWireNode) == 0x0c,
              "retail equipment list node is operator new(0x0c)");

// The header of the equipment list. Same 0x0c shape as the collision-group one.
struct FRpcEquipWireListHeader {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    FRpcEquipWireNode* Head;        // +0x04
    unsigned int Size;              // +0x08
};

static_assert(sizeof(FRpcEquipWireListHeader) == 0x0c,
              "retail std::list header is 0x0c: allocator, head, size");

// The 4-byte handle behind the equipment payload. FUN_06d8fa90 allocates it with
// operator new(4) and stores its ADDRESS at packet+0x20 -- so the payload's four bytes
// are a pointer to this struct, not the struct itself. The five senders keep an
// identical handle on their own stack while they build the list, then deep-copy it in.
// SETEQUIPMENT, SETCARGO, REQEQUIPMENT and REQCARGO all allocate 0x24 bytes, i.e. exactly
// four bytes of payload.
//
// A class with a destructor in retail: FUN_06d91340 (RpcEquipHandleDestroy) is what every
// EH unwind funclet runs on a live handle (receiver stack handles, sender handles, the
// handle inside a record at +0x264 or a create-object header at +0x70); the constructor is
// an inline null store. The explicit RpcEquipHandleDestroy calls in destructor bodies leave
// it null, so the member destructor is a no-op after them. The handles the payload slot
// points at are raw operator new(4) blocks, torn down by hand (RpcEquipHandleDestroy +
// operator delete, 06da2205).
struct FRpcEquipWireHandle {
    FRpcEquipWireListHeader* List;  // +0x00, null until the first push_back

    FRpcEquipWireHandle() : List(nullptr) {}
    __declspec(dllexport) ~FRpcEquipWireHandle();         // FUN_06d91340
    FRpcEquipWireHandle(const FRpcEquipWireHandle&) = delete;
    FRpcEquipWireHandle& operator=(const FRpcEquipWireHandle&) = delete;
};

static_assert(sizeof(FRpcEquipWireHandle) == 4,
              "retail equipment payload handle is operator new(4)");

// ===========================================================================
// The two Common.dll containers the senders and receivers speak
// ===========================================================================

// retail std::vector<EquipDesc> is Common's EquipDescVector (common_goods.h): VC6 keeps
// the allocator as a REAL member, so the three pointers start at +0x04, and the element
// stride is 0x20 (sizeof EquipDesc).

// retail EquipDescList == std::list<EquipDesc>, with the same 0x0c header and
// { Next, Prev, EquipDesc } nodes, grown through the imported
// `COMMON.DLL::std::list<struct EquipDesc,...>::_Buynode` (.rdata:0x06db30ac). The port
// uses the real EquipDescList and its own std::list rather than rebuilding the node chain
// by hand; what stays retail-exact is the WIRE format -- the handle, the 0x0c nodes and
// the 0x18 records above.

// ===========================================================================
// The ported helpers. Every one names the retail function it reproduces.
// ===========================================================================

// FUN_06d913e0 -- the handle's only accessor.
FRpcEquipWireListHeader* RpcEquipHandleGetList(const FRpcEquipWireHandle* Handle);

// FUN_06d91580 -- push_back(Record). Creates the header and its dummy node on the
// first call (operator new(0xc) twice), then links one operator new(0xc) node at the
// tail and bumps Size. The list takes ownership of Record.
void RpcEquipHandlePushBack(FRpcEquipWireHandle* Handle, FRpcEquipWireRecord* Record);

// FUN_06d91340 -- the handle's destructor. For every node: free Record->Hardpoint,
// then the record; then unlink and free every node, the dummy node and the header.
// Retail leaves the handle's own pointer dangling afterwards; the port nulls it.
void RpcEquipHandleDestroy(FRpcEquipWireHandle* Handle);

// FUN_06d913f0 -- deep copy of one handle into another. Allocates a fresh 0x18 record
// per element, including a fresh copy of the hardpoint string.
void RpcEquipHandleCopy(FRpcEquipWireHandle* Dest, const FRpcEquipWireHandle* Source);

// FUN_06d8fa90 -- `if (*(Handle**)payload is null) allocate the handle; deep-copy
// Source into it`.
void RpcPacketEquipAssign(FPacketBase* Packet, const FRpcEquipWireHandle* Source);

// FUN_06d95d20 -- the inverse: `if (payload handle is non-null) copy it into Dest`.
__declspec(dllexport) void RpcPacketEquipExtract(const FPacketBase* Packet, FRpcEquipWireHandle* Dest);

// FUN_06d8c750 -- fills Handle from a retail std::vector<EquipDesc>. bAll false keeps
// only the items whose EquipDesc::bEquipped is 0 (the cargo filter at 06d8c770).
void RpcEquipHandleFromVector(FRpcEquipWireHandle* Handle,
                              const EquipDescVector* Source,
                              bool bAll);

// FUN_06d84d60 -- the inverse: rebuilds a std::vector<EquipDesc> out of a handle. The
// storage behind Begin comes from one operator new; release it with
// RpcEquipDescVectorRelease.
__declspec(dllexport) void RpcEquipVectorFromHandle(EquipDescVector* Dest,
                              const FRpcEquipWireHandle* Source);

// retail 06d88(0)0d / 06d84dca: `std::vector<EquipDesc>::_Destroy(first, last)` then
// operator delete(first).
void RpcEquipDescVectorRelease(EquipDescVector* Vector);

// FUN_06d87ec0 / FUN_06d880a0 -- rebuilds the equipment list out of a handle. Both
// retail functions are the same 153 instructions; the only difference is the IServer
// slot they finish on (+0x70 ReqEquipment vs +0x74 ReqCargo), so the rebuild is
// factored out here and the slot choice stays in the channel-3 codec.
__declspec(dllexport) void RpcEquipListFromHandle(EquipDescList& Dest, const FRpcEquipWireHandle* Source);

// The marshal direction of the same pair: one 0x18 wire record per element
// (IServerImpl::ReqEquipment's own loop at 0x06d8cff0).
void RpcEquipHandleFromList(FRpcEquipWireHandle* Handle, const EquipDescList& Source);

// ===========================================================================
// Collision groups
// ===========================================================================

// The payload's own list header, i.e. retail FUN_06d8f7d0 (packet+0x20).
FRpcCollisionGroupList* RpcPacketCollisionGroups(FPacketBase* Packet);
__declspec(dllexport) const FRpcCollisionGroupList* RpcPacketCollisionGroups(const FPacketBase* Packet);

// The payload's list header is built by the concrete packet constructor
// (FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS / _CLIENT_REQCOLLISIONGROUPS in
// rpc_packet_registry.cpp, retail 0x06d9e8b0 / 0x06da10b0).

// FUN_06d8f710 -- list assignment into the payload: RpcCollisionGroupListAssignRange
// (below, with the create-object primitives) on RpcPacketCollisionGroups(Packet).

// Walks a retail-layout node chain front to back, for RpcCollisionGroupListAssignRange
// when the SOURCE is itself a retail-layout list (the create-object record copy in
// rpc_containers.cpp).
struct FRpcCollisionGroupNodeIterator {
    const FRpcCollisionGroupNode* Node;

    explicit FRpcCollisionGroupNodeIterator(const FRpcCollisionGroupNode* InNode)
        : Node(InNode) {}
    const FRpcCollisionGroupDescMirror& operator*() const {
        return *reinterpret_cast<const FRpcCollisionGroupDescMirror*>(&Node->Id);
    }
    FRpcCollisionGroupNodeIterator& operator++() {
        Node = Node->Next;
        return *this;
    }
    bool operator!=(const FRpcCollisionGroupNodeIterator& Other) const {
        return Node != Other.Node;
    }
};

// ===========================================================================
// The CREATELOOT list (ch2 0x28)
//
// Header at payload+0x08 (retail accessor FUN_06d931b0), nodes
// of operator new(0x50) built by FUN_06d93160, values copied field by field by
// FUN_06d93280. Only the sender ever grows it and only the receiver ever walks it, so
// push_back plus a walk is the whole API -- retail has no assign for this one.
// ===========================================================================

FRpcLootWireList* RpcPacketLootList(FPacketBase* Packet);
__declspec(dllexport) const FRpcLootWireList* RpcPacketLootList(const FPacketBase* Packet);

// The packet constructor's share: dummy node pointing at itself, Size 0.
void RpcPacketLootListInit(FPacketBase* Packet);

// FUN_06d93160 -- one operator new(0x50) node at the tail, value copied by
// FUN_06d93280, then ++Size (packet+0x30).
void RpcPacketLootPushBack(FPacketBase* Packet, const FRpcLootWireRecord& Record);

// Frees the node chain and the dummy node. Retail does it from the packet destructor.
void RpcPacketLootListRelease(FPacketBase* Packet);

// ===========================================================================
// The reputation relation list (ch2 0x29 SETREPUTATION, 0x2b SETGROUPFEELINGS)
//
// std::list<Relation*>: the node is operator new(0xc) and its value is a POINTER to a
// separately operator new(8)'d pair. Retail builds it twice per packet -- the sender
// fills a stack-local list (FUN_06d8cb20 makes the dummy, FUN_06d8cad0 push_backs) and
// then transfers it node by node into the packet (FUN_06d92760 for 0x29, FUN_06d91ca0
// for 0x2b), each transfer allocating a FRESH 8-byte value. The reverse pair
// (FUN_06d927e0 / FUN_06d91d20) does the same on the way out. Because every hop
// re-allocates the value, this port collapses the two hops into one push_back per
// element, which produces byte-identical packet contents.
//
// The list header sits INSIDE the payload (payload+0x28 for 0x29, payload+0x08 for
// 0x2b), so these take the header, not the packet.
// ===========================================================================

// Every list these reach was built by its packet's constructor (FUN_06d9d290 for 0x29,
// FUN_06d9c540 for 0x2b), so none of them builds or tests the dummy; the packet
// destructors free the lists.

// FUN_06d8cad0 / the tail of FUN_06d92760: operator new(8) for the value, operator
// new(0xc) for the node, link at the tail, ++Size.
void RpcRelationListPushBack(FRpcRelationList* List, const FRpcRelation& Value);

// The node half alone: FUN_06d8cad0's insert of a value the caller already allocated --
// the decoders (06d91b5a / 06d925a2) new(8) the element before reading into it, so a
// failed read leaks it, as retail's does.
void RpcRelationListLink(FRpcRelationList* List, FRpcRelation* Block);

// ===========================================================================
// The mission-objective record list (ch2 0x35 SETMISSIONOBJECTIVES)
//
// Header at payload+0x1c. Again std::list<T*>: the node is operator new(0xc) and holds a
// pointer to an operator new(0xc) record. Retail's push_back is FUN_06d8fc10, which also
// increments payload+0x18 -- the caller here does that separately, because payload+0x18
// belongs to the payload struct and not to the list header.
//
// The accessor is FUN_06d8fc60: a LINEAR walk from the head, returning the Index'th
// node's value or null when the walk reaches the dummy first. Retail really does re-walk
// the list from the start for every element; the receiver's loop is O(n^2) and this port
// keeps the same accessor so the null-on-overrun behaviour is identical.
// ===========================================================================

// The list is the one the packet constructor built (FUN_06d9da40, 06d9daad).

// Takes ownership of Record (retail hands FUN_06d8fc10 the operator new(0xc) block it
// just filled, and the packet destructor frees it).
void RpcMissionObjectiveListPushBack(FRpcMissionObjectiveList* List,
                                     FRpcMissionObjectiveRecord* Record);

// FUN_06d8fc60.
__declspec(dllexport) const FRpcMissionObjectiveRecord* RpcMissionObjectiveListAt(
    const FRpcMissionObjectiveList* List, unsigned int Index);

// ===========================================================================
// The create-object header (ch2 0x04 CREATESHIP, 0x42 CREATESOLAR)
//
// These two types are the only users of retail's FUN_06d8c830 (marshal) and
// FUN_06d84ed0 (deliver), a 0x7c-byte prefix that both payloads start with. It is the
// largest structure on channel 2 and the only one carrying three containers at once.
//
// FUN_06d8c830(this, Source, Packet) copies, in this order (packet offsets; payload ==
// packet - 0x20):
//
//   pkt+0x20..0x2c <- src+0x04..0x10    pkt+0x40 <- src+0x44 (count N)
//   pkt+0x48 <- src+0x48                pkt+0x44 <- operator new(N << 2)
//   pkt+0x68 <- src+0x68                block[i] <- src+0x24 + 4*i
//   pkt+0x6c <- src+0x6c                pkt+0x4c..0x64 <- src+0x4c..0x64
//   pkt+0x30..0x3c <- src+0x14..0x20
//
// then the three containers:
//
//   * src+0x70 is a std::vector<EquipDesc> (allocator byte, then _First/_Last at +0x74/+0x78,
//     stride 0x20). Each element becomes one operator new(0x18) wire record (the same
//     record as the SETEQUIPMENT family above, hardpoint gated on EquipDesc::is_internal)
//     pushed onto the handle at pkt+0x70 by FUN_06d91580.
//   * src+0x80 is a std::list<CollisionGroupDesc> assigned into the one at pkt+0x74
//     (two dwords per node).
//   * src+0x8c is a std::list<DamageEntry> assigned into the one at pkt+0x80 (three
//     dwords per node). The decorated imports name both element types
//     (.rdata:06db309c / 06db3060, list<...>::erase).
//
// and finally the four-field tail:
//   pkt+0x8c (BYTE) <- src+0x98,  pkt+0x90 <- src+0x9c,
//   pkt+0x94 <- src+0xa0,         pkt+0x98 <- src+0xa4.
//
// FUN_06d84ed0(Packet, Dest) is the strict inverse, applying the equipment through the
// Common.dll setters (set_arch_id, set_count, set_id, set_equipped, set_status, then
// set_hardpoint(StringAlloc(...)) or make_internal).
//
// Not reproduced: 06d84f5e writes 1.0f to Dest+0x58 and zeroes Dest+0x5c..+0x64
// immediately before overwriting all four from the packet (an inlined constructor
// default with no observable effect).
// ===========================================================================

// The source/destination record is NOT declared here. It is Server.dll's own
// CEqObjCreateInfo / CShipCreateInfo / CSolarCreateInfo, published once in
// src/include/server_createinfo.h: the sender (Server.dll) builds one and hands its
// address to IClient::SendServerCreateSolar / SendServerCreateShip, the marshallers below read it field by field, and
// the receivers deliver their own static copy of the same type. Retail compiled that
// struct into rpclocal from the same header -- rpclocal carries its own copies of the
// constructor (06d83df0), destructor (06d843b0) and both clear()s (06d83ed0 /
// 06d83fa0), reproduced in src/include with inline members. The byte at +0xa8 belongs to
// the BASE (base ctor 06d83eb5, base clears 06d83ee5 / Server 06cf0275).

// The create-object part of both packets, packet+0x20 onwards. Retail builds it in the
// abstract base's constructor inlined into FUN_06d9cc60 (CREATESHIP) and FUN_06d9e630
// (CREATESOLAR), and tears it down in the base part of FUN_06d97b90 / FUN_06d9e740. Its
// EH map, FuncInfo_06db70a4 (handler 06db288a, pushed by both constructors), names the
// sub-objects that have destructors, in construction order:
//   state 0  CDAPacket base            -> Unwind@06db2860  FUN_06da1370
//   state 1  +0x30 (Wire+0x10..+0x27)  -> Unwind@06db2868  FUN_06d95b60, frees +0x44 (Ids)
//   state 2  +0x70 (Wire+0x50)         -> Unwind@06db2873  FUN_06d91340, the handle
//   state 3  +0x74 (Wire+0x54)         -> Unwind@06db287e  std::list<CollisionGroupDesc>::~list
// and the DamageList at +0x80 is the last member built (06d9ccf3 / 06d9e6c8).
//
// The port splits it the same way: FRpcCreateObjectHead is everything up to and including
// the handle, and its destructor is states 1 and 2; FRpcCreateObjectWire adds the two
// Common.dll containers as REAL members, so the compiler's own unwind follows the retail
// map in the retail order. The normal destructor matches the base part of FUN_06d97b90:
// DamageList, list, handle, id block.
//
// No retail ABI: the two containers are this toolchain's std::list<CollisionGroupDesc>
// and Common.dll's DamageList (common_misc.h), both imported from Common.dll as retail
// imports them (.rdata:06db30b4, .rdata:06db310c). Their sizes follow the build, so the
// wire header is not 0x7c bytes and each payload's tail follows it at
// sizeof(FRpcCreateObjectWire); the retail tail offsets are kept as RELATIVE asserts
// (rpc_containers.cpp).
struct FRpcCreateObjectHead {
    // 06d9cca2 (CREATESHIP) == 06d9e66b (CREATESOLAR).
    FRpcCreateObjectHead();
    // 06d97c94 == 06d9e81c.
    ~FRpcCreateObjectHead();
    // The id block and the handle are owned; retail never copies a packet.
    FRpcCreateObjectHead(const FRpcCreateObjectHead&) = delete;
    FRpcCreateObjectHead& operator=(const FRpcCreateObjectHead&) = delete;

    unsigned int ObjId;                // +0x00 <- src+0x04
    unsigned int ArchetypeNickname;                // +0x04 <- src+0x08
    unsigned int DockingBaseId;                // +0x08 <- src+0x0c
    int VibeGroupId;                        // +0x0c <- src+0x10
    unsigned int CostumeHead;                // +0x10 <- src+0x14
    unsigned int CostumeBody;                // +0x14 <- src+0x18
    unsigned int CostumeLeftHand;                // +0x18 <- src+0x1c
    unsigned int CostumeRightHand;                // +0x1c <- src+0x20
    int IdCount;                            // +0x20 <- src+0x44
    unsigned int* Ids;                      // +0x24    operator new(IdCount << 2)
    unsigned int VoiceId;                // +0x28 <- src+0x48
    float PosX;                       // +0x2c <- src+0x4c
    float PosY;                       // +0x30 <- src+0x50
    float PosZ;                       // +0x34 <- src+0x54
    // CEqObjCreateInfo::Orientation (src+0x58..+0x64), a unit float quaternion. It
    // ships as four quantized bytes in the order x,y,z,w, scaled by 127.0 and
    // sign-extended back through 1/127.
    float OrientW;                       // +0x38 <- src+0x58  quaternion w
    float OrientX;                       // +0x3c <- src+0x5c  quaternion x
    float OrientY;                       // +0x40 <- src+0x60  quaternion y
    float OrientZ;                       // +0x44 <- src+0x64  quaternion z
    // +0x48: an INTEGER hit-point count, not a float (FILD out, ftol back in). It ships
    // as one byte of percentage against Archetype::GetArchMaxHitPts(ArchetypeNickname).
    unsigned int HitPts;                // +0x48 <- src+0x68  current hit points
    unsigned int LoadoutId;                // +0x4c <- src+0x6c
    FRpcEquipWireHandle Equipment;          // +0x50    packet+0x70, FUN_06d91580's this
};

static_assert(sizeof(FRpcCreateObjectHead) == 0x54,
              "the create-object head ends at packet+0x74, where the collision list starts");

struct FRpcCreateObjectWire : FRpcCreateObjectHead {
    // packet+0x74. Retail constructs and destroys this one through the Common.dll import
    // (.rdata:06db30b4) rather than inline.
    std::list<CollisionGroupDesc> CollisionGroups;
    // packet+0x80, ONE Common.dll DamageList (CEqObjCreateInfo::Damage on the source side),
    // constructed through .rdata:06db310c, `??0DamageList@@QAE@XZ`.
    DamageList Damage;
};

// The header-level primitives behind RpcPacketCollisionGroups*, for the retail-image
// collision lists that still exist (SETCOLLISIONGROUPS / REQCOLLISIONGROUPS payloads and
// the CHARACTERINFO record).
void RpcCollisionGroupListInit(FRpcCollisionGroupList* List);
void RpcCollisionGroupListAssign(FRpcCollisionGroupList* Dest,
                                 const FRpcCollisionGroupDescMirror* Items,
                                 unsigned int Count);

// FUN_06d8f710's three phases, generic over the SOURCE: the port's callers hand either a
// std::list<CollisionGroupDesc> or an array of 8-byte mirrors. The value travels as 8 raw
// bytes in two dword moves, whatever type the iterator yields.
//
// Retail's self-assignment guard (`payload == source`) has no counterpart: the port's
// source and destination are never the same object. The destination's dummy node exists
// already (RpcCollisionGroupListInit, the stand-in for the packet constructor).
template<typename TIterator>
void RpcCollisionGroupListAssignRange(FRpcCollisionGroupList* List, TIterator First,
                                      TIterator Last) {
    static_assert(sizeof(*First) == 8, "a CollisionGroupDesc value is 8 bytes");
    FRpcCollisionGroupNode* Dummy = List->Head;

    // Phase 1: overwrite the values of the nodes already there, stopping at whichever of
    // the two lists runs out first.
    FRpcCollisionGroupNode* Node = Dummy->Next;
    while (Node != Dummy && First != Last) {
        const unsigned int* Value = reinterpret_cast<const unsigned int*>(&(*First));
        Node->Id = Value[0];
        Node->HealthBits = Value[1];
        Node = Node->Next;
        ++First;
    }

    // Phase 2: erase the surplus, decrementing Size per node.
    while (Node != Dummy) {
        FRpcCollisionGroupNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        ::operator delete(Node);
        --List->Size;
        Node = Next;
    }

    // Phase 3: append the rest. operator new(0x10) is untested.
    while (First != Last) {
        FRpcCollisionGroupNode* New =
            static_cast<FRpcCollisionGroupNode*>(::operator new(sizeof(FRpcCollisionGroupNode),
                                                                std::nothrow));
        FRpcCollisionGroupNode* Tail = Dummy->Prev;
        New->Next = Dummy;
        if (Tail == nullptr) {
            Tail = New;
        }
        New->Prev = Tail;
        Dummy->Prev = New;
        New->Prev->Next = New;
        const unsigned int* Value = reinterpret_cast<const unsigned int*>(&(*First));
        New->Id = Value[0];
        New->HealthBits = Value[1];
        ++List->Size;
        ++First;
    }
}

// FUN_06d8c830 and FUN_06d84ed0, on the shared CEqObjCreateInfo. Both sides' containers
// are already built by their constructors -- the packet's by FRpcCreateObjectWire's, the
// record's by its own -- so both directions assign into them.
void RpcCreateObjectHeaderWrite(FRpcCreateObjectWire* Wire,
                                const CEqObjCreateInfo& Source);
__declspec(dllexport) void RpcCreateObjectHeaderRead(CEqObjCreateInfo& Dest,
                               const FRpcCreateObjectWire* Wire);

// The one conversion left between a retail-image collision list (SETCOLLISIONGROUPS /
// REQCOLLISIONGROUPS keep the VC6 node image in their payload) and the std::list the
// callers hold: 06d8505c-style three-phase assignment, two dwords per node.
__declspec(dllexport) void RpcCollisionGroupListFromWire(std::list<CollisionGroupDesc>& Dest,
                                   const FRpcCollisionGroupList* Source);

// ---------------------------------------------------------------------------
// ch2 0x42 FLPACKET_SERVER_CREATESOLAR -- registry 0xb8, payload 0x98.
//
// Sender 0x06d89190 (IClient+0x0b8), receiver caseD_42 at 0x06d82c62. After the shared
// header the type carries two bytes, two flattened FmtStrs and the reputation pair. The
// whole tail is inside an "if (!SinglePlayer())" on BOTH sides (06d891fb / 06d82dab),
// so in single player the six fields stay zero and the receiver never looks at them.
//
//   pkt+0x9c (BYTE) <- src+0xad              src+0xad <- pkt+0x9c
//   pkt+0x9d (BYTE) <- src+0xa8              src+0xa8 <- pkt+0x9d
//   GetAffiliation(&pkt+0x2c, pkt+0xb0, false)
//   AttitudeTowards(&pkt+0x2c, &src+0xb0, pkt+0xb4)
//   GetName(&pkt+0x2c, NameA, NameB, &wide)  -- the wide name is DISCARDED here,
//            unlike CREATESHIP which ships it
//   then    pkt+0xa0/0xa4 = flatten(NameA), pkt+0xa8/0xac = flatten(NameB)
//
// and the receiver runs the exact inverse: unflatten into NameA and NameB, then
// EnsureExists, SetName(&pkt+0x2c, NameA, NameB, 0), SetAffiliation(&pkt+0x2c, pkt+0xb0,
// false) and SetAttitudeTowardsPlayer(*(&pkt+0x2c), pkt+0xb4).
// ---------------------------------------------------------------------------
struct FPayloadServerCreateSolar {
    FRpcCreateObjectWire Common;    // retail +0x00 .. +0x7b; the tail follows at sizeof
    unsigned char bDestructible;       // +0x7c <- src+0xad
    unsigned char bMarkedBy;       // +0x7d <- src+0xa8
    unsigned char Pad_0x7e[2];      // +0x7e
    unsigned int NameASize;         // +0x80
    void* NameABuffer;              // +0x84
    unsigned int NameBSize;         // +0x88
    void* NameBBuffer;              // +0x8c
    unsigned int Affiliation;       // +0x90    Reputation::Vibe::GetAffiliation out
    float Attitude;                 // +0x94    Reputation::Vibe::AttitudeTowards out
};
// Retail's payload is 0x98 (operator new(0xb8)); the port's is sizeof(FRpcCreateObjectWire)
// + 0x1c, the tail offsets being asserted relative to Field_0x7c in rpc_containers.cpp.

// The caller-side record is CSolarCreateInfo (src/include/server_createinfo.h). The
// receiver static is .data:0x06dbb838; its one-time init (06d82c83) is that
// type's constructor, and the marshaller reads +0xad, +0xa8 and +0xb0 of it.

// ---------------------------------------------------------------------------
// ch2 0x04 FLPACKET_SERVER_CREATESHIP -- registry 0xec, payload 0xcc.
//
// Sender 0x06d89410 (IClient+0x0bc), receiver caseD_4 at 0x06d81fc5. Same shape as
// CREATESOLAR with a longer tail, and one extra branch: GetName's fourth out-parameter
// is a wide display name, and when it comes back non-empty (wcslen) the sender ships
// THAT (FUN_06d97ce0 -> pkt+0xd8 length, pkt+0xdc operator new + wcscpy) and leaves both
// flattened FmtStr slots zero. The receiver unflattens both regardless and passes the
// wide pointer as SetName's fourth argument, so an empty pair plus a name, or a name pair
// plus a null, are the two legal states.
//
// Three fields are NARROWED on the way out and zero-extended back on the way in; the high
// halves never travel:
//   pkt+0xc0 <- WORD  src+0xb0
//   pkt+0xc2 <- BYTE  src+0xb4
//   pkt+0xc4 <- WORD  src+0xd8
// ---------------------------------------------------------------------------
struct FPayloadServerCreateShip {
    FRpcCreateObjectWire Common;    // retail +0x00 .. +0x7b; the tail follows at sizeof
    unsigned char bUsingTradelane;       // +0x7c <- src+0xdc
    unsigned char Pad_0x7d[3];      // +0x7d
    unsigned int VelXBits;        // +0x80 <- src+0xb8
    float VelY;               // +0x84 <- src+0xbc
    float VelZ;               // +0x88 <- src+0xc0
    unsigned int AngVelXBits;        // +0x8c <- src+0xc4
    float AngVelY;               // +0x90 <- src+0xc8
    float AngVelZ;               // +0x94 <- src+0xcc
    // A FLOAT: the encoder quantizes it with the 127.0 scale and the decoder sign-extends
    // the byte back through 1/127. Sender and receiver only ever move it as a dword.
    float Throttle;               // +0x98 <- src+0xd0, [-1,+1], one quantized byte
    // +0x9c is the wire's own discriminator as well as a value: the encoder sets flag
    // bit 0x04 from `OwnerPlayer != 0` and only then ships its LOW BYTE plus GroupId --
    // and the display name instead of the two FmtStrs.
    unsigned int OwnerPlayer;        // +0x9c <- src+0xac
    unsigned short GroupNameId;      // +0xa0 <- LOW WORD of src+0xb0
    unsigned char ReputationRank;       // +0xa2 <- LOW BYTE of src+0xb4
    unsigned char Pad_0xa3;         // +0xa3
    unsigned short GroupId;      // +0xa4 <- LOW WORD of src+0xd8
    unsigned char Pad_0xa6[2];      // +0xa6
    unsigned int NameASize;         // +0xa8
    void* NameABuffer;              // +0xac
    unsigned int NameBSize;         // +0xb0
    void* NameBBuffer;              // +0xb4
    unsigned int DisplayNameLength; // +0xb8    wcslen, written but never read back
    wchar_t* DisplayName;           // +0xbc    operator new((len*2)+2) + wcscpy
    unsigned int Affiliation;       // +0xc0
    float Attitude;                 // +0xc4
    unsigned char bMarkedBy;       // +0xc8 <- src+0xa8
    unsigned char Pad_0xc9[3];      // +0xc9
};
// Retail's payload is 0xcc (operator new(0xec)); the port's is sizeof(FRpcCreateObjectWire)
// + 0x50, the tail offsets being asserted relative to Field_0x7c in rpc_containers.cpp.

// The caller-side record is CShipCreateInfo (src/include/server_createinfo.h). The
// receiver static is .data:0x06dbb940; its one-time init (06d81fe3) is that type's
// constructor. The three fields the wire narrows (+0xb0, +0xb4, +0xd8) are DWORDS in the
// record -- the receiver writes all four bytes of each.

// ===========================================================================
// ch2 0x03 FLPACKET_SERVER_CHARACTERINFO -- registry 0x30, payload 0x10
//
// Sender 0x06d89890 (IClient+0x034), receiver IServerImpl::HandleCharacterInfo 0x06d84490
// (tail-called by caseD_3), decorated
// "?HandleCharacterInfo@IServerImpl@@QAE_NPAVCDAPCharacterInfo@@PAVIClient@@I@Z" -- so the
// packet class's retail name is CDAPCharacterInfo, the only channel-2 packet class rpclocal
// names anywhere. This is the character-select list:
//
//   payload+0x00  a dword, argument struct +0x14
//   payload+0x04  std::list<Record*>, header at packet+0x24 -- FUN_06d955c0 pushes into
//                 it and FUN_06d94460 reads it back
//
// Two DIFFERENT record layouts exist:
//   * the CALLER's record is 0x2dc and holds its two id blocks INLINE at +0x274 and
//     +0x2a8, a real EquipDescList at +0x248 and a std::list<CollisionGroupDesc> at +0x254;
//   * the WIRE record is 0x27c and holds POINTERS to two operator new(0x18) id blocks at
//     +0x274/+0x278, the 4-byte equipment handle at +0x264 and the collision list at +0x268.
// Fifteen scalar fields are also PERMUTED between the two; the receiver's permutation is
// the strict inverse of the sender's. The pairs are listed on FRpcCharacterInfoWireRecord.
//
// The 0x18 id-block-on-the-wire is the same compaction FUN_06d8c830 uses for the
// create-object array: four dwords, a count, and one heap block of count dwords.
//
// NOT BYTE-EXACT: the two strings at record+0x204 and +0x214 are MSVCP60 basic_string,
// 0x10 bytes each in retail; the port uses std::wstring, so neither record is 0x2dc or
// 0x27c. Both records are heap objects reached only through a pointer, and the only size
// that has to match retail is the payload's 0x10, which is asserted.
// ===========================================================================

// The id block as it travels: operator new(0x18), the five fields and the array.
struct FRpcIdArrayBlockWire {
    unsigned int Head;        // +0x00
    unsigned int Body;        // +0x04
    unsigned int LeftHand;        // +0x08
    unsigned int RightHand;        // +0x0c
    int Count;                      // +0x10
    unsigned int* Ids;              // +0x14  operator new(Count << 2)
};
static_assert(sizeof(FRpcIdArrayBlockWire) == 0x18,
              "the wire id block is operator new(0x18)");

// The wire record, 0x27c in retail. Field order below is retail's field order; the arrow
// names the CALLER-side offset each one comes from; the receiver (06d84630) re-inverts
// every pair.
struct FRpcCharacterInfoWireRecord {
    CHARACTER_ID Name;              // +0x000 CHARACTER_ID::operator=
    unsigned char bIsRestart;      // +0x200 <- src+0x200; the receiver re-normalises it
                                    //          to 0/1
    unsigned char bIsAutoSave;      // +0x201 <- src+0x201, same re-normalisation
    // +0x204 / +0x214: retail MSVCP60 basic_string<unsigned short> (decorated
    // `?$basic_string@GU?$char_traits@G@std@@...`), 0x10 bytes each -- VC6's wide string.
    // The codec writes the length as a CHARACTER count then length*2 bytes; the decoder
    // pads with a 16-bit fill (FUN_06d94420) and a 16-bit terminator.
    std::wstring TextA;             // +0x204
    std::wstring TextB;             // +0x214
    unsigned int DescripStrId;       // +0x224 <- src+0x224
    unsigned int TimestampLow;       // +0x228 <- src+0x228
    unsigned int TimestampHigh;       // +0x22c <- src+0x22c
    unsigned int ShipArchetype;       // +0x230 <- src+0x230
    unsigned int Money;       // +0x234 <- src+0x234
    unsigned int SystemId;       // +0x238 <- src+0x260
    unsigned int BaseId;       // +0x23c <- src+0x264
    unsigned int LocationId;       // +0x240 <- src+0x268
    unsigned int VoiceId;       // +0x244 <- src+0x26c
    unsigned int Rank;       // +0x248 <- src+0x270
    // +0x24c: a DEAD field that nevertheless TRAVELS. Both record constructors zero it,
    // the sender never fills it, the receiver never reads it, but the codec pair and the
    // packet-bound copy carry it like any other dword. Same class as
    // FPayloadServerSendComm::Field_0x54.
    unsigned int Field_0x24c;       // +0x24c
    // +0x250: the encoder reads it as a float (x87) but the decoder and sender move it as
    // a raw dword, so the port keeps the bit pattern.
    unsigned int HullStatusBits;       // +0x250 <- src+0x244
    unsigned int NumKills;       // +0x254 <- src+0x238
    unsigned int NumMissionFailures;       // +0x258 <- src+0x240
    unsigned int NumMissionSuccesses;       // +0x25c <- src+0x23c
    // +0x260: the EQUIPMENT ELEMENT COUNT, and it does travel. The first copy hop only
    // zeroes it; the packet-bound hop FUN_06d955c0 fills it with the low byte of the
    // equipment list's Size (0 when the handle is still null). The decoder uses it as the
    // loop bound.
    unsigned char EquipmentCount;      // +0x260
    FRpcEquipWireHandle Equipment;  // +0x264 built from the caller's EquipDescList
    FRpcCollisionGroupList CollisionGroups; // +0x268 <- the caller's list at src+0x254
    FRpcIdArrayBlockWire* BlockA;   // +0x274 <- the inline block at src+0x274
    FRpcIdArrayBlockWire* BlockB;   // +0x278 <- the inline block at src+0x2a8
};

// The caller's record, 0x2dc in retail, and the 0x18 argument IClient slot 13 takes:
// CCharacterSet::CharacterInfo and CCharacterSet, declared ONCE for Server.dll, this
// module and freelancer.exe in src/include/server_characterset.h (retail's own names,
// from the OwnerList<CCharacterSet::CharacterInfo> / MetaList<> RTTI in all three).
// This module's FUN_06d852c0 / FUN_06d852e0 / FUN_06d85340 (vtables .rdata:06db3758 /
// 06db3754) are that list's MetaList(bool) / ~MetaList / ~OwnerList; the record's
// destructor pass in ~OwnerList (06d85380 .. 06d853d3) is OwnerListDeleteData's
// specialisation there. The chain is NULL-terminated (MetaList), not circular: the
// sender walks it as "p = p ? p->Next : Head" and stops on null.

// The wire list header, at packet+0x24. Same retail 0x0c shape as everywhere else, with
// a pointer value.
struct FRpcCharacterInfoWireNode {
    FRpcCharacterInfoWireNode* Next;        // +0x00
    FRpcCharacterInfoWireNode* Prev;        // +0x04
    FRpcCharacterInfoWireRecord* Value;     // +0x08
};
struct FRpcCharacterInfoWireList {
    unsigned char Allocator;                // +0x00
    unsigned char Pad_0x01[3];              // +0x01
    FRpcCharacterInfoWireNode* Head;        // +0x04
    unsigned int Size;                      // +0x08
};
static_assert(sizeof(FRpcCharacterInfoWireList) == 0x0c, "retail std::list header is 0x0c");

struct FPayloadServerCharacterInfo {
    unsigned int SaveFailureCount;                // +0x00 <- argument struct +0x14
    FRpcCharacterInfoWireList Records;      // +0x04 .. +0x0f
};
static_assert(sizeof(FPayloadServerCharacterInfo) == 0x10, "CHARACTERINFO payload 0x10");

// FUN_06d955c0 -- the sender's transfer of its temporary list into the packet. Retail
// builds the deep copy twice (once into a stack list, then again into the packet, each
// hop allocating fresh records); every hop re-allocates, so one copy here produces
// identical packet contents. The dummy node already stands: the packet constructor built
// it (FPacket_FLPACKET_SERVER_CHARACTERINFO's constructor, rpc_packet_registry.cpp).
void RpcCharacterInfoWireListPushBack(FRpcCharacterInfoWireList* List,
                                      FRpcCharacterInfoWireRecord* Record);
// Frees every record (its equipment handle, its collision list, its two id blocks and
// their arrays), every node and the dummy. Retail's packet destructor does this.
void RpcCharacterInfoWireListRelease(FRpcCharacterInfoWireList* List);

// FUN_06d94460 -- the RECEIVER's deep copy of the packet's list (header at packet+0x24)
// into a list of its own: one fresh 0x27c record
// per non-null source record, every owned member re-allocated, pushed at Dest's tail.
// Dest's dummy must already stand.
void RpcCharacterInfoWireListCopy(FRpcCharacterInfoWireList* Dest,
                                  const FRpcCharacterInfoWireList* Source);

// The receiver's inline teardown of ONE copied record, right after it is converted
// (IServerImpl::HandleCharacterInfo 06d849fe): both id blocks, the collision
// list, the equipment handle, the two strings, the record. The node keeps its (now
// dangling) value pointer, as in retail.
void RpcCharacterInfoWireRecordFree(FRpcCharacterInfoWireRecord* Record);

// The two record conversions, i.e. the body of the sender's loop (06d8991a) and the
// body of the receiver's (06d84517).
void RpcCharacterInfoRecordToWire(FRpcCharacterInfoWireRecord* Wire,
                                  const CCharacterSet::CharacterInfo* Source);
void RpcCharacterInfoRecordFromWire(CCharacterSet::CharacterInfo* Dest,
                                    const FRpcCharacterInfoWireRecord* Wire);

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
unsigned int RpcEncodeServerCharacterInfo(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCharacterInfo(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// The {dword size, that many raw bytes} pair, retail's only variable-length primitive
// outside the strings. The encode side is inlined at every call site; the decode side
// is the real function FUN_06da1620, which allocates the buffer
// with operator new BEFORE its second bounds check -- so the pointer is stored even
// when the byte run then overruns, and the packet's destructor is what frees it.
//
// Shared out of rpc_containers.cpp because ch2 0x29 SETREPUTATION calls FUN_06da1620
// twice (payload+0x00 and payload+0x08) and inlines the encode side the same way the
// create-object pair does.
// ===========================================================================
bool RpcPutSizedBlock(FRpcMarshalStream& Stream, unsigned int Size, const void* Buffer);
bool RpcGetSizedBlock(FRpcMarshalStream& Stream, unsigned int& OutSize, void*& OutBuffer);

// ===========================================================================
// FUN_06da2240 / FUN_06da2480 -- the equipment/cargo list on the wire, { word Count;
// Count * { dword Count, float Status, dword ArchId, word Id, byte bEquipped, byte
// Field_0x0f, word HardpointSize, HardpointSize bytes } }.
//
// Retail calls both out of line from TWO slot families, not one: 06d916f6 / 06d95c3f are
// the SETEQUIPMENT / SETCARGO / REQEQUIPMENT / REQCARGO slots, and 06d8f902 / 06d8fa17
// are the ch2 0x3d REMOTEOBJECTCARGOUPDATE slots. Those four are every call site in the
// module. The bodies live in rpc_wire_generic.cpp, where the field derivation is.
// ===========================================================================
bool RpcPutEquipList(FRpcMarshalStream& Stream, FRpcEquipWireHandle* const* Slot);
bool RpcGetEquipList(FRpcMarshalStream& Stream, FRpcEquipWireHandle** Slot);

// ===========================================================================
// ch2 codec slots transcribed in rpc_wire_ch2.cpp -- see the file header there for
// how each packet class was tied to its channel/opcode through its constructor.
// ===========================================================================
unsigned int RpcEncodeServerCreateLoot(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateLoot(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetReputation(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetReputation(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerMiscObjUpdate(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerMiscObjUpdate(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSendComm(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSendComm(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerCreateGuided(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateGuided(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerCreateMine(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateMine(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerCreateCounter(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateCounter(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetAddItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetAddItem(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetMissionObjectives(FPacketBase& Packet,
                                                 unsigned char** BufferHolder,
                                                 unsigned int Capacity,
                                                 unsigned int* OutLength);
unsigned int RpcDecodeServerSetMissionObjectives(FPacketBase& Packet,
                                                 const unsigned char* Buffer,
                                                 unsigned int Size,
                                                 unsigned int* OutLength);

// ===========================================================================
// ch1 codec slots transcribed in rpc_wire_ch1.cpp.
// ===========================================================================
unsigned int RpcEncodeCommonUpdateObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonUpdateObject(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
// 0x06da7a00 / 0x06da7b90 -- ch1 0x1d TRACTOROBJECTS: dword, word, word count, then that
// many dwords appended into the payload's own std::vector<unsigned int>.
unsigned int RpcEncodeCommonTractorObjects(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonTractorObjects(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// The shape-only codec slots transcribed in rpc_wire_generic.cpp.
//
// These are NOT per-type codecs: retail folds the small packet classes onto twenty-four
// shared Encode/Decode bodies, and one body serves up to thirty-three vtables. The
// retail address is on each declaration; the type lists and the four pairs of
// near-identical bodies that are NOT interchangeable are documented in the .cpp.
// Between them they cover 86 of the 162 registered types.
// ===========================================================================

// 0x06da13d0 / 0x06da1470 -- no payload, header inlined, cannot fail.
unsigned int RpcEncodeWireEmptyInline(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireEmptyInline(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// 0x06dae0d0 / 0x06dabad0 -- no payload, out-of-line header, CAN return 0x16.
unsigned int RpcEncodeWireEmpty(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireEmpty(FPacketBase& Packet, const unsigned char* Buffer,
                                unsigned int Size, unsigned int* OutLength);
// 0x06db0310 / 0x06db03f0 -- one dword at payload+0x00.
unsigned int RpcEncodeWireDword(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDword(FPacketBase& Packet, const unsigned char* Buffer,
                                unsigned int Size, unsigned int* OutLength);
// 0x06db1cd0 -- the same four bytes moved through x87; decodes with RpcDecodeWireDword.
unsigned int RpcEncodeWireFloat(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength);
// 0x06da82e0 / 0x06da83c0 -- one byte at payload+0x00.
unsigned int RpcEncodeWireByte(FPacketBase& Packet, unsigned char** BufferHolder,
                               unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireByte(FPacketBase& Packet, const unsigned char* Buffer,
                               unsigned int Size, unsigned int* OutLength);
// 0x06da9110 / 0x06d95d40 -- two dwords.
unsigned int RpcEncodeWireDwordPair(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordPair(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength);
// 0x06dab0e0 / 0x06daf230 -- a dword then a byte, five bytes on the wire.
unsigned int RpcEncodeWireDwordByte(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordByte(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength);
// 0x06db0160 / 0x06da6e40 -- three dwords.
unsigned int RpcEncodeWireThreeDwords(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireThreeDwords(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// ===========================================================================
// The plain field-list slots, transcribed in rpc_wire_fields.cpp. Seventeen more retail
// bodies covering twenty-three types; the retail Encode/Decode addresses head each one.
// Attribution is the constructor -> vtable -> slot scan described at the top of that file.
// ===========================================================================
// 0x06dae620 / 0x06da78d0 -- dword, word, dword. ch1 0x14, ch3 0x3d.
unsigned int RpcEncodeWireDwordWordDword(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordWordDword(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
// 0x06daaf90 / 0x06dacea0 -- dword, dword, word. ch2 0x33, ch2 0x4a.
unsigned int RpcEncodeWireDwordDwordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordDwordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
// 0x06db19b0 / 0x06dad690 -- dword, word, word. ch2 0x51, ch3 0x40.
unsigned int RpcEncodeWireDwordWordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordWordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06da8b50 / 0x06da8cd0 -- dword, dword, word, word. ch1 0x04, ch2 0x38.
unsigned int RpcEncodeWireDwordDwordWordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordDwordWordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength);
// 0x06daffc0 / 0x06d9ab00 -- four flat dwords. ch3 0x12, ch3 0x37.
unsigned int RpcEncodeWireFourDwords(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireFourDwords(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
// 0x06db1b60 / 0x06da6a50 -- dword, byte, byte. ch1 0x0e, ch3 0x44.
unsigned int RpcEncodeWireDwordTwoBytes(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordTwoBytes(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06da67a0 / 0x06da68f0 -- ch1 0x08, and the wire reorders the last two fields.
unsigned int RpcEncodeCommonActivateEquip(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonActivateEquip(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// 0x06da6b80 / 0x06da6cf0 -- ch1 0x0f, three dwords then a byte.
unsigned int RpcEncodeCommonGoTradelane(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonGoTradelane(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06da8e30 / 0x06da8f80 -- ch2 0x0a, dword, byte, dword.
unsigned int RpcEncodeServerActivateObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerActivateObject(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength);
// 0x06dab200 / 0x06dab350 -- ch2 0x47, and the wire reorders the last two fields the
// OTHER way round from ch1 0x08's.
unsigned int RpcEncodeServerCfgInterfaceNotification(FPacketBase& Packet,
                                                     unsigned char** BufferHolder,
                                                     unsigned int Capacity,
                                                     unsigned int* OutLength);
unsigned int RpcDecodeServerCfgInterfaceNotification(FPacketBase& Packet,
                                                     const unsigned char* Buffer,
                                                     unsigned int Size,
                                                     unsigned int* OutLength);
// 0x06daeec0 / 0x06daf010 -- ch3 0x14, dword, dword, byte.
unsigned int RpcEncodeClientMissionResponse(FPacketBase& Packet, unsigned char** BufferHolder,
                                            unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientMissionResponse(FPacketBase& Packet, const unsigned char* Buffer,
                                            unsigned int Size, unsigned int* OutLength);
// 0x06daa820 / 0x06daa990 -- ch2 0x44, dword then three consecutive bytes.
unsigned int RpcEncodeServerRequestReturned(FPacketBase& Packet, unsigned char** BufferHolder,
                                            unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerRequestReturned(FPacketBase& Packet, const unsigned char* Buffer,
                                            unsigned int Size, unsigned int* OutLength);
// 0x06daa560 / 0x06daa6d0 -- ch3 0x32, byte, dword, dword, byte.
unsigned int RpcEncodeClientRequestCancel(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientRequestCancel(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// 0x06da7fe0 / 0x06da8100 -- ch2 0x27, byte then dword.
unsigned int RpcEncodeServerRequestCreateShipResp(FPacketBase& Packet,
                                                  unsigned char** BufferHolder,
                                                  unsigned int Capacity,
                                                  unsigned int* OutLength);
unsigned int RpcDecodeServerRequestCreateShipResp(FPacketBase& Packet,
                                                  const unsigned char* Buffer,
                                                  unsigned int Size,
                                                  unsigned int* OutLength);
// 0x06db12c0 / 0x06db13e0 -- ch3 0x25, dword then word.
unsigned int RpcEncodeClientDbgDestroyObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientDbgDestroyObject(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength);
// 0x06daa0c0 -- ch2 0x2a, dword then FLOAT. Its DECODE slot is 0x06d95d40, the
// dword-pair family's decoder above; only the encoder is its own.
unsigned int RpcEncodeServerAdjustAttitude(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength);
// 0x06dabdf0 / 0x06dabf10 -- ch2 0x08, a dword and then eight bytes, skipping the hole
// at payload+0x04.
unsigned int RpcEncodeServerCharSelectVerified(FPacketBase& Packet,
                                               unsigned char** BufferHolder,
                                               unsigned int Capacity,
                                               unsigned int* OutLength);
unsigned int RpcDecodeServerCharSelectVerified(FPacketBase& Packet,
                                               const unsigned char* Buffer,
                                               unsigned int Size,
                                               unsigned int* OutLength);
// 0x06db1670 / 0x06db17a0 -- a dword then one 128-byte run. ch3 0x2d only.
unsigned int RpcEncodeClientDbgConsole(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientDbgConsole(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);

// --- rpc_wire_varlen.cpp: the two codec pairs whose length is read out of the packet ---
// 0x06daf830 / 0x06daf970 -- { word Length; char Text[Length] }, the strlen of the
// CHARACTER_ID at payload+0x00. ch3 0x06 SELECTCHARACTER and ch3 0x3a DESTROYCHAR share
// the pair outright (.rdata:06db5c88/06db5c98 and 06db5c8c/06db5c9c).
unsigned int RpcEncodeWireCharacterId(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireCharacterId(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// 0x06dacca0 / 0x06dacd70, bodies FUN_06d8f530 / FUN_06d8f5e0 -- { word Count; { word
// Id; float Health; } * Count } over the payload's own FRpcCollisionGroupList. ch2 0x48
// SETCOLLISIONGROUPS and ch3 0x36 REQCOLLISIONGROUPS share the pair outright
// (.rdata:06db52b0/06db5df8 and 06db52b4/06db5dfc).
unsigned int RpcEncodeWireCollisionGroups(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireCollisionGroups(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// 0x06dabbb0 / 0x06dabd00 -- ch2 0x45 SET_MISSION_MESSAGE, { dword Field_0x08; dword
// TextSize; bytes }. The payload's LAST field leads on the wire; see the .cpp.
unsigned int RpcEncodeServerSetMissionMessage(FPacketBase& Packet,
                                              unsigned char** BufferHolder,
                                              unsigned int Capacity,
                                              unsigned int* OutLength);
unsigned int RpcDecodeServerSetMissionMessage(FPacketBase& Packet,
                                              const unsigned char* Buffer,
                                              unsigned int Size, unsigned int* OutLength);
// 0x06d8f840 / 0x06d8f970 -- ch2 0x3d REMOTEOBJECTCARGOUPDATE, { dword; byte; the
// equipment/cargo list }. Reaches FUN_06da2240 / FUN_06da2480 through the two helpers
// above, the same way retail's slots do.
unsigned int RpcEncodeServerRemoteObjectCargoUpdate(FPacketBase& Packet,
                                                    unsigned char** BufferHolder,
                                                    unsigned int Capacity,
                                                    unsigned int* OutLength);
unsigned int RpcDecodeServerRemoteObjectCargoUpdate(FPacketBase& Packet,
                                                    const unsigned char* Buffer,
                                                    unsigned int Size,
                                                    unsigned int* OutLength);

// 0x06db14f0 / 0x06dad820 -- a dword then one twelve-byte run (FUN_06d8f510).
unsigned int RpcEncodeWireDwordBlock12(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordBlock12(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
// 0x06d97570 / 0x06d978f0 -- {count, bytes}, payload {Size, Buffer}.
unsigned int RpcEncodeWireBlobSizeFirst(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireBlobSizeFirst(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06da6190 -- {count, bytes}, payload {Buffer, Size}. Four different decoders pair
// with this one encoder; the differences between them are real, see the .cpp.
unsigned int RpcEncodeWireBlobBufferFirst(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
// 0x06d97730 -- allocates unconditionally.
unsigned int RpcDecodeWireBlobBufferFirst(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// 0x06db10e0 -- skips the allocation and the run when the count is zero.
unsigned int RpcDecodeWireBlobBufferFirstSkipEmpty(FPacketBase& Packet,
                                                   const unsigned char* Buffer,
                                                   unsigned int Size,
                                                   unsigned int* OutLength);
// 0x06da62d0 -- the 0x0c channel-1 buffer family: reuses a buffer already in the payload.
unsigned int RpcDecodeWireOwnedBuffer(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// 0x06da5bc0 -- the same, but allocates Size + 1 and NUL-terminates. ch1 0x11 only.
unsigned int RpcDecodeWireOwnedString(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// 0x06d9a660 -- ch3 0x27 TRADERESPONSE's own decoder. Its ENCODE side is the shared
// RpcEncodeWireDwordPair (0x06da9110); the pair does not round-trip, see rpc_wire_generic.cpp.
unsigned int RpcDecodeWireTradeResponse(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06d90480 / 0x06d906c0 -- ch2 0x05 DAMAGEOBJECT: three scalars, then the list's count
// and one { word, byte, float } per DamageEntry. Lives in rpc_containers.cpp, next to the
// damage-list helpers it needs.
unsigned int RpcEncodeServerDamageObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerDamageObject(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
// 0x06dac3c0 / 0x06dac5c0 -- ch2 0x41 BURNFUSE, seven flat fields. In rpc_wire_ch2.cpp.
unsigned int RpcEncodeServerBurnFuse(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerBurnFuse(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
// 0x06d916a0 + 0x06da2240 / 0x06d95bf0 + 0x06da2480 -- the equipment/cargo list. Four
// types: ch2 0x24 SETEQUIPMENT, ch2 0x25 SETCARGO, ch3 0x16 REQEQUIPMENT, ch3 0x17
// REQCARGO. A word count then one { dword, float, dword, word, byte, byte, word, run }
// record per element; the payload's four bytes are the handle POINTER, not the handle.
unsigned int RpcEncodeWireEquipList(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireEquipList(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// The hardpoint-name dictionary (FUN_06da2820 / FUN_06da2980 / FUN_06da2a80)
//
// A fixed 42-entry table in rpclocal's own .data (06dbb770..06dbb814), used by the
// CREATESHIP / CREATESOLAR equipment codec to ship a common hardpoint name as one byte.
// Codes run 1..42; 0 means "not in the table", and both directions fall back to the
// literal string for it. The derivation and the whole table are in rpc_containers.cpp.
// RpcBuildHardpointDictionary (FUN_06da2820, declared in rpc_transport.h) builds it from
// the serializer's construction (retail 06d95f07); both lookups read what it built.
// ===========================================================================
unsigned int RpcHardpointDictionaryCount();
unsigned int RpcHardpointNameToCode(const char* Name);
const char* RpcHardpointCodeToName(unsigned int Code);

// ===========================================================================
// ch2 0x04 CREATESHIP / 0x42 CREATESOLAR -- the shared 0x7c-byte header codec
//
// FUN_06da3720 (encode) and FUN_06da4020 (decode). NOT whole-packet codecs: the
// concrete type's Encode/Decode slot sets the marshalling cursor up, deals with the
// routing header, calls one of these for the shared header, and then writes its own
// tail. Both stop at Wire+0x6c -- the four-field wire tail belongs to the slot.
//
// Retail passes three stack dwords on top of the `this` in ECX and reads none of them:
// the state is entirely the packet's own four marshalling dwords, which is what the
// FRpcMarshalStream view is. Return value is 0, or the direction's sticky error --
// FRpcMarshalStream::kEncodeOverflow / ::kDecodeOverrun.
// ===========================================================================
unsigned int RpcEncodeCreateObjectCommon(FRpcMarshalStream& Stream,
                                         const FRpcCreateObjectWire* Wire);
unsigned int RpcDecodeCreateObjectCommon(FRpcMarshalStream& Stream,
                                         FRpcCreateObjectWire* Wire);

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
unsigned int RpcEncodeServerCreateShip(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateShip(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerCreateSolar(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateSolar(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// ch1 0x02 FLPACKET_COMMON_FIREWEAPON -- the adaptive-precision direction codec.
//
// Packet vtable PTR_FUN_06db4388 = { FUN_06d9b000, LAB_06da4f60, LAB_06da5550,
// IServerImpl::GetServerStats }, published by the type's constructor FUN_06d9afb0
// together with routing header 0x10002 (channel 1, opcode 2).
// ===========================================================================
unsigned int RpcEncodeCommonFireWeapon(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonFireWeapon(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// ch3 0x22 FLPACKET_CLIENT_DBGCREATESHIP -- LAB_06db0560 / LAB_06db0b30, slots +0x04
// and +0x08 of PTR_FUN_06db5d54 (ctor FUN_06da0e20, routing header 0x30022). The payload
// layout is on FPayloadClientDbgCreateShip in rpc_payloads.h.
//
// This type has no sender in the release image, so the codecs are reachable only from
// the network Encode/Decode path, never from the single-player queue.
// ===========================================================================
unsigned int RpcEncodeClientDbgCreateShip(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientDbgCreateShip(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// The type's constructor FUN_06da0e20 is FPacket_FLPACKET_CLIENT_DBGCREATESHIP's, with
// its identity quaternion, in rpc_packet_registry.cpp.

// ===========================================================================
// ch1 0x12 FLPACKET_COMMON_PLAYER_TRADE -- LAB_06da7030 / LAB_06da7430, slots +0x04 and
// +0x08 of PTR_FUN_06db4498 (ctor FUN_06d9bad0, routing header 0x10012). Eight sub-types
// behind one jump table; the inverted IsMPServer test and the decoder's one stray 0x16
// are described in rpc_containers.cpp.
// ===========================================================================
unsigned int RpcEncodeCommonPlayerTrade(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonPlayerTrade(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// FUN_06d9bad0's one non-zero default, EquipArchId = 0xffffffff, is stored by
// FPacket_FLPACKET_COMMON_PLAYER_TRADE's constructor in rpc_packet_registry.cpp.

// ===========================================================================
// The flattened FmtStr (ch1 0x1b, ch2 0x29, 0x34, 0x35, 0x45)
//
// Retail rpclocal imports COMMON.DLL::FmtStr::flatten, ::unflatten and
// ::Val::get_flattened_size (IAT .rdata:06db30dc / 06db3008 / 06db30e0). The senders
// that inline the size walk compute exactly what get_flattened_size computes (five
// copies, listed in rpc_payloads.h).
// ===========================================================================

// Base 6 (the mad-lib id dword plus the ushort node count) plus one term per top node.
unsigned int RpcFlattenedFmtStrSize(const FmtStr& Source);

// Stores the size, allocates it and calls FmtStr::flatten. The packet owns the returned
// block; its class destructor frees it, as the retail per-type packet destructor does.
// Returns null (and OutSize 0) on failure.
void* RpcFlattenFmtStr(const FmtStr& Source, unsigned int& OutSize);

// FmtStr::unflatten(buffer, size). A null buffer is fine: unflatten clears the object and
// returns 0.
__declspec(dllexport) void RpcUnflattenFmtStr(FmtStr& Dest, void* Buffer, unsigned int Size);

// ===========================================================================
// The remaining 22 codec slots, and the twelve per-type dump slots.
//
// rpc_wire_closeout.cpp holds the thirteen channel-1 / channel-2 pairs, rpc_wire_ch3.cpp
// the nine channel-3 pairs and rpc_packet_dump.cpp the dumps. Every one of the 162
// registered types has its real vtable +0x04 / +0x08 body. The three retail asymmetries they reproduce -- ch1 0x1b's second
// block length, ch2 0x2b's missing relation count and ch2 0x32's unallocated destination
// -- are written up in docs/re/deviations.md.
// ===========================================================================
unsigned int RpcEncodeCommonChatMsg(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonChatMsg(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeCommonPopUpDialog(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonPopUpDialog(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerLaunch(FPacketBase& Packet, unsigned char** BufferHolder,
                                   unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerLaunch(FPacketBase& Packet, const unsigned char* Buffer,
                                   unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSystemSwitchIn(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSystemSwitchIn(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetGroupFeelings(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetGroupFeelings(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerRequestSpaceScript(FPacketBase& Packet, unsigned char** BufferHolder,
                                               unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerRequestSpaceScript(FPacketBase& Packet, const unsigned char* Buffer,
                                               unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerReplaceMissionObjective(FPacketBase& Packet, unsigned char** BufferHolder,
                                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerReplaceMissionObjective(FPacketBase& Packet, const unsigned char* Buffer,
                                                    unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetCamera(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetCamera(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerLoadHint(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerLoadHint(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetDirective(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetDirective(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerPlayerList(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerPlayerList(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerObjectCargoUpdate(FPacketBase& Packet, unsigned char** BufferHolder,
                                              unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerObjectCargoUpdate(FPacketBase& Packet, const unsigned char* Buffer,
                                              unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetMusic(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetMusic(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientLogin(FPacketBase& Packet, unsigned char** BufferHolder,
                                  unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientLogin(FPacketBase& Packet, const unsigned char* Buffer,
                                  unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientMunCollision(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientMunCollision(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientObjCollision(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientObjCollision(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientReqAddItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientReqAddItem(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientReqModifyItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientReqModifyItem(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientSaveGame(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientSaveGame(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientMineAsteroid(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientMineAsteroid(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientRequestEvent(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientRequestEvent(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientCreateNewChar(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientCreateNewChar(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);

// Packet vtable slot +0x0c: eleven bodies serving twelve types. See rpc_packet_dump.cpp.
void RpcDumpServerConnectResponse(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerLoginResponse(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerCharacterInfo(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerLaunch(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerCharSelectVerified(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerSetReputation(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerRepairObject(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpClientLogin(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpClientRequestLaunch(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpWireCharacterIdDump(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpClientCreateNewChar(const FPacketBase& Packet, std::ostream& Out);

// The two CHARACTERINFO list destructors (FUN_06d852e0 / FUN_06d85340) are ~MetaList /
// ~OwnerList of OwnerList<CCharacterSet::CharacterInfo> -- the template in
// src/include/common_metalist.h, instantiated by src/include/server_characterset.h.
