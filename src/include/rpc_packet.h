// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_packet.h -- the RPC packet base and the packet-type registry, shared by
// rpclocal.dll (src/rpclocal/) and remoteserver.dll (src/RemoteServer/).
//
// Retail compiles this code into both DLLs: remoteserver's IServerImpl::FireWeapon
// (.text:06bd8140) allocates through its own serializer at IServerImpl+0x08
// (`MOV ECX,[EBP+0x8]` / CALL FUN_06be1750), whose body is the same as rpclocal's
// FUN_06d96300 (null out -> 4, (Channel << 16 | Opcode) map lookup, miss -> 0x19,
// else the factory's answer). Member bodies for rpclocal are in
// src/rpclocal/rpc_transport.cpp; nothing here is exported -- each module links its
// own definitions.
//
// Ground truth: docs/re/rpclocal.md, EXE/Disasm/rpclocal.dll.txt.
#pragma once

#include <iosfwd>
#include <map>
#include <string>

class CDAPacket;

// ===========================================================================
// FRpcRefObject -- the refcounted root every packet (and the serializer) derives from.
//
// Vtable .rdata:06db3c54 has ONE slot, the scalar deleting dtor LAB_06d8f4d0, whose dtor
// FUN_06d8f4c0 only stores that vptr back. The use count sits at +0x04; every retail
// constructor that reaches this base writes 1 there (e.g. 06d9afb3 in FLPACKET_COMMON_
// FIREWEAPON's, 06d95e92 in the serializer's).
// ===========================================================================
class FRpcRefObject {
public:
    FRpcRefObject() : UsageCount(1) {}
    virtual ~FRpcRefObject();                   // FUN_06d8f4c0 / scalar LAB_06d8f4d0

    // FUN_06d8f4a0: a zero count is returned untouched; otherwise decrement, and at zero
    // call the scalar deleting dtor (delete flag) and return 0, else re-read the count.
    unsigned int Release();

    unsigned int UsageCount;                    // +0x04
};

// ===========================================================================
// FPacketBase -- retail CDAPacket, exactly 0x20 bytes.
//
// Vtable .rdata:06db4fbc = { 06d9c5d0 scalar dtor, 06da13d0 Encode, 06da1470 Decode,
// the shared no-op dump }. Its dtor FUN_06da1370 is `MOV [ECX],06db4fbc ; JMP
// FUN_06d8f4c0`. Every retail packet constructor makes the same five base stores
// (e.g. FUN_06d9c860, FLPACKET_SERVER_LOGINRESPONSE):
//     [+0x04] = 1 ; [+0x08] = 0 ; [+0x10] = 0 ; [+0x1c] = 0 ; [+0x0c] = PacketId
// and leaves +0x14 / +0x18 alone.
//
// The 162 concrete classes derive from this one -- see src/rpclocal/rpc_packet_registry.cpp. The
// payload is theirs and starts at +0x20.
//
// class CDAPacket in src/include/common_server_types.h is the plain-struct mirror of
// this layout, so a FPacketBase* and a CDAPacket* are interchangeable by cast.
// ===========================================================================
class FPacketBase : public FRpcRefObject {
public:
    explicit FPacketBase(unsigned int InPacketId);

    // --- packet vtable, 4 slots, retail order ---
    ~FPacketBase() override;                                                   // +0x00 FUN_06da1370
    // +0x04 -- marshal the payload out for the network transport. The first argument is a
    // pointer to the buffer pointer, which retail never writes back (the moving cursor is
    // packet+0x18). OutLength includes the two header bytes. CDAPacket's own body,
    // 06da13d0, writes the header and nothing else.
    virtual unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                                unsigned int* OutLength);
    // +0x08 -- the inverse. Takes the buffer pointer directly, and OutLength excludes the
    // header, which the dispatcher has already consumed. CDAPacket's own body is 06da1470.
    virtual unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                                unsigned int* OutLength);
    // +0x0c dump/trace slot taking a std::ostream& (the twelve real bodies stream into
    // it). The other 150 types share one no-op body, ICF-folded onto
    // IServerImpl::GetServerStats; see src/rpclocal/rpc_packet_dump.cpp.
    virtual void Dump(std::ostream& Out) const;

    unsigned int GetChannel() const { return PacketId >> 16; }
    unsigned int GetOpcode() const { return PacketId & 0xfff; }

    // Payload starts at +0x20 and belongs to the concrete packet class.
    unsigned char* Payload() { return reinterpret_cast<unsigned char*>(this) + 0x20; }
    const unsigned char* Payload() const { return reinterpret_cast<const unsigned char*>(this) + 0x20; }
    unsigned int* PayloadDwords() { return reinterpret_cast<unsigned int*>(Payload()); }
    const unsigned int* PayloadDwords() const { return reinterpret_cast<const unsigned int*>(Payload()); }

    void AddRef();

    CDAPacket* AsRaw() { return reinterpret_cast<CDAPacket*>(this); }

    // --- layout, mirroring CDAPacket exactly (UsageCount +0x04 is FRpcRefObject's) ---
    // +0x08 -- every retail packet constructor zeroes it, but the connection queues (VC6
    // std::deque<CDAPacket*>, FPacketQueue in src/rpclocal/rpc_transport.h) do not use it.
    // TODO: no reader of this field has been found anywhere in rpclocal; semantics unknown.
    FPacketBase* Field_0x08;    // +0x08  purpose unproven, retail only ever writes 0
    unsigned int PacketId;      // +0x0c  Channel << 16 | Opcode
    // The marshalling scratch the codec slots run on. Encode's entry (e.g. 0x06d97d20) sets
    // {0, capacity, *buffer pointer, 0}, then FUN_06da3720 walks the payload, per field:
    //     Cursor += width; if (Cursor > Limit) Error = code; if (Error) return code;
    //     *Ptr = value; Ptr += width;
    // The decoder FUN_06da4020 is the same with reads and writes swapped and error 0x17
    // instead of 0x16. The constructor zeroes +0x10 and +0x1c and leaves +0x14 / +0x18 alone.
    unsigned int MarshalCursor;   // +0x10  bytes emitted/consumed so far
    unsigned int MarshalLimit;    // +0x14  capacity of the caller's buffer
    unsigned int MarshalPtr;      // +0x18  moving read/write pointer into that buffer
    unsigned int MarshalError;    // +0x1c  0 = ok, 0x16 = encode overflow, 0x17 = decode overrun
};

// The compiler must place the vptr first and pack the seven dwords behind it, so the
// payload really does begin at +0x20 exactly as retail's does.
static_assert(sizeof(FPacketBase) == 0x20, "FPacketBase must match the retail CDAPacket base");

// ===========================================================================
// The retail packet factory -- one per registered class.
//
// FLPACKET_COMMON_FIREWEAPON's, FUN_06d9af70, is the model for all 162 (__cdecl, one
// CDAPacket** argument): *out = 0; operator new(size) -- the size lives here, not in the
// registry; null -> 5; ctor; *out = packet; return 0. The generated body is
// RpcPacketFactory<T> in src/rpclocal/rpc_packet_registry.cpp.
typedef int (*FPacketFactoryFn)(FPacketBase** OutPacket);

// Retail's two factory return codes.
const int kPacketFactoryOk = 0;
const int kPacketFactoryNoMemory = 5;
// FUN_06d960a0's duplicate-id answer.
const int kPacketRegisterDuplicate = 0x42;
// FUN_06d96300's two failures: a null out pointer, and an id not in the map.
const int kPacketAllocateNullOut = 4;
const int kPacketAllocateUnknownType = 0x19;

// ===========================================================================
// FPacketTypeInfo / FPacketSerializer -- the retail packet-type registry.
//
// Retail: FUN_06d960a0(Serializer, Channel << 16 | Opcode, "FLPACKET_...", Factory),
// 162 registrations spread over three registrators (FUN_06d9ace0 channel 1,
// FUN_06d9bc80 channel 2, FUN_06d9f100 channel 3). The serializer object itself lives
// at IServerImpl+0x08 (built by FUN_06d95e50) and at IClient+0x08.
//
// The table is a std::map keyed on the packet id, at serializer+0x08. The mapped value is
// a pointer to a 0x14-byte record: the factory at +0x00 and a MSVCP60 std::string name at
// +0x04. No size and no counter: the counters live in GRpcPacketStats
// (src/rpclocal/rpc_transport.h).
// ===========================================================================
struct FPacketTypeInfo {
    FPacketFactoryFn Factory;    // +0x00
    std::string Name;            // +0x04
};

// An FRpcRefObject of 0x1c bytes (FUN_06d95e50): use count 1, the map at +0x08, its own
// vptr 06db3e18 whose one slot is the scalar deleting destructor FUN_06d964c0, reached
// only through FRpcRefObject::Release from IServerImpl::Shutdown.
class FPacketSerializer : public FRpcRefObject {
public:
    FPacketSerializer();
    ~FPacketSerializer() override;                  // FUN_06d95f30 / scalar FUN_06d964c0

    // FUN_06d960a0, three stack arguments (no size): 0x42 for an id already in the map,
    // otherwise a fresh record inserted and 0.
    int RegisterType(unsigned int PacketId, const char* Name, FPacketFactoryFn Factory);

    // Retail FUN_06d96300(Serializer, Channel, Opcode, &OutPacket): 4 for a null out
    // pointer, 0x19 for an id not in the map, otherwise whatever the factory returns.
    int Allocate(unsigned int Channel, unsigned int Opcode, FPacketBase** OutPacket);

    // Retail FUN_06d961b0, reached from IServerImpl::DumpPacketStats and IClient slot 125.
    void DumpStats(const char* Name) const;

private:
    std::map<unsigned int, FPacketTypeInfo*> Types;   // serializer+0x08
};

// Compiled into each Remote DLL: raw lazy cache, no registration or AddRef.
FPacketSerializer* RpcGetRemotePacketSerializer();
