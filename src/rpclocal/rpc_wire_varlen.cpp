// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_varlen.cpp -- the VARIABLE-LENGTH packet codec slots.
//
// Retail Encode/Decode pairs whose wire length is read out of the packet (a strlen, a list
// size, a block size) and written as a leading count. Two pairs of types share a body:
//
//   .rdata:06db5c88  addr .text:LAB_06daf830   <- ch3 0x3a DESTROYCHAR   (ctor 06da0bcd)
//   .rdata:06db5c8c  addr .text:LAB_06daf970
//   .rdata:06db5c98  addr .text:LAB_06daf830   <- ch3 0x06 SELECTCHARACTER (ctor 06da0bfd)
//   .rdata:06db5c9c  addr .text:LAB_06daf970
//
//   .rdata:06db52b0  addr .text:LAB_06dacca0   <- ch2 0x48 SETCOLLISIONGROUPS (ctor 06d9e915)
//   .rdata:06db52b4  addr .text:LAB_06dacd70
//   .rdata:06db5df8  addr .text:LAB_06dacca0   <- ch3 0x36 REQCOLLISIONGROUPS (ctor 06da1115)
//   .rdata:06db5dfc  addr .text:LAB_06dacd70
//
// Constructors with an SEH frame (owning a std::list or std::string: ch2 0x2b, ch2 0x48,
// ch3 0x36) keep `this` in ESI, so their routing-id store reads [ESI + 0xc] rather than
// [EAX + 0xc]; all 162 types store a distinct id.
//
// The routing dword at packet+0x0c is the byte image of opcode word (+0x0c), channel byte
// (+0x0e) and a spare byte (+0x0f) nothing reads: 0x30005 is `05 00 03 00`.
//
// Single-player: in SP a packet is queued by pointer and neither slot is ever called.

#include "rpc_containers.h"
#include "rpc_payloads.h"

#include <new>

// ===========================================================================
// ch3 0x06 FLPACKET_CLIENT_SELECTCHARACTER and ch3 0x3a FLPACKET_CLIENT_DESTROYCHAR --
// LAB_06daf830 (133 instructions) / LAB_06daf970 (118).
//
// Both types are operator new(0x220) (factories 06da00c0, 06da01c0), so the payload is the
// 0x200-byte CHARACTER_ID; the dump slot (LAB_06dafab0) prints it as "   Character: ".
//
// Wire format { word Length; char Text[Length] }: strlen of the payload, no terminator and
// no fixed run (unlike ch3 0x2d DBGCONSOLE's 128-byte block). The length is truncated to
// 16 bits before both the cursor bump and the copy (unreachable for a 0x200-byte field).
// ===========================================================================
unsigned int RpcEncodeWireCharacterId(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength) {
    const char* Name = reinterpret_cast<const char*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));

    // Unbounded strlen over the payload, as retail's.
    unsigned int Length = 0;
    while (Name[Length] != '\0') {
        ++Length;
    }
    const unsigned short WireLength = (unsigned short)(Length & 0xffffu);

    Stream.PutWord(WireLength);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutBytes(Name, (unsigned int)WireLength);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// LAB_06daf970 -- Decode (kDecodeOverrun 0x17).
//
// Retail bug, reproduced: the wire length is checked only against the input buffer, never
// against the 0x200-byte payload, so a datagram longer than 0x220 bytes writes past the
// packet (copy and terminator).
unsigned int RpcDecodeWireCharacterId(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    char* Name = reinterpret_cast<char*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned short WireLength = 0;
    if (!Stream.GetWord(WireLength)) {                                 // 06daf9bb / 06daf9db
        return Stream.GetError();
    }
    const unsigned int Length = (unsigned int)WireLength;
    if (!Stream.GetBytes(Name, Length)) {                              // 06daf9ee / 06dafa1b
        return Stream.GetError();
    }
    Name[Length] = '\0';
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x48 FLPACKET_SERVER_SETCOLLISIONGROUPS and ch3 0x36 FLPACKET_CLIENT_
// REQCOLLISIONGROUPS -- LAB_06dacca0 (83 instructions) / LAB_06dacd70 (82).
//
// Both are operator new(0x2c): the 0x0c payload is one retail std::list header
// (FRpcCollisionGroupList, rpc_containers.h), built by the packet constructor.
//
// Each slot is one call, FUN_06d8f530 (encode) / FUN_06d8f5e0 (decode); both receive the
// slot's three arguments but read none of them (everything goes through the packet).
//
// Wire format { word Count; { word Id; float Health; } Count times }:
//   * Count is the list header's size (low word), but the loop is driven by the list
//     itself, not by that count; both kept as retail wrote them;
//   * per node: the low word of the value's +0x00 only, and the health as a raw float
//     (FLD/FSTP). No quantizer on this pair, unlike the
//     CREATESHIP/CREATESOLAR header codec which sends the same node type through a
//     255.0 scale.
// ===========================================================================
unsigned int RpcEncodeWireCollisionGroups(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FRpcCollisionGroupList* List = RpcPacketCollisionGroups(&Packet);

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));

    // FUN_06d8f530 begins here.
    const unsigned short Count = (unsigned short)(List->Size & 0xffffu);
    Stream.PutWord(Count);                                             // / 06d8f55b
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // A zero count skips the walk, as does an empty list. The dummy is dereferenced
    // untested; a non-zero count means it was built.
    if (Count != 0) {
        const FRpcCollisionGroupNode* Node = List->Head->Next;
        while (Node != List->Head) {
            Stream.PutWord((unsigned short)(Node->Id & 0xffffu));
            if (Stream.Failed()) {
                return Stream.GetError();
            }
            // FLD/FSTP of a single-precision slot is a bit-exact 4-byte move.
            Stream.PutDword(Node->HealthBits);                         // / 06d8f5bc
            if (Stream.Failed()) {
                return Stream.GetError();
            }
            Node = Node->Next;
        }
    }

    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// LAB_06dacd70 / FUN_06d8f5e0 -- Decode.
//
// The payload's own list is erased first (COMMON.DLL std::list::erase), so a decode starts
// from an empty list; each node is spliced in by retail's own inline code.
//
// Deviation: retail copy-constructs every node value (FUN_06d85500) from eight
// uninitialised stack bytes, then overwrites +0x04 fully but only the low word of +0x00, so
// the high half of every decoded CollisionGroupDesc::Id is stack residue. The port zeroes
// instead (docs/re/deviations.md).
unsigned int RpcDecodeWireCollisionGroups(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FRpcCollisionGroupList* List = RpcPacketCollisionGroups(&Packet);

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // FUN_06d8f5e0 begins here: erase(begin, end).
    RpcCollisionGroupListAssign(List, nullptr, 0u);
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {                                      // 06d8f606 / 06d8f629
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        // COMMON.DLL list _Buynode; the node is used untested.
        FRpcCollisionGroupNode* New =
            static_cast<FRpcCollisionGroupNode*>(::operator new(sizeof(FRpcCollisionGroupNode),
                                                               std::nothrow));
        FRpcCollisionGroupNode* Dummy = List->Head;
        FRpcCollisionGroupNode* Tail = Dummy->Prev;
        New->Next = Dummy;
        New->Prev = Tail;
        Dummy->Prev = New;
        Tail->Next = New;
        // Retail's FUN_06d85500 copies eight uninitialised bytes here.
        New->Id = 0;
        New->HealthBits = 0;
        ++List->Size;

        unsigned short Id = 0;
        if (!Stream.GetWord(Id)) {                                     // 06d8f68c / 06d8f6ac
            return Stream.GetError();
        }
        New->Id = (unsigned int)Id;                            // low word only
        unsigned int Health = 0;
        if (!Stream.GetDword(Health)) {                                // 06d8f6be / 06d8f6db
            return Stream.GetError();
        }
        New->HealthBits = Health;
    }

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE -- LAB_06dabbb0 / LAB_06dabd00.
// Vtable 06db516c (ctor 06d9d9b0, header 0x20045). Payload 0x0c {TextSize, TextBuffer,
// Type}.
//
// The wire order is not the struct order: Type (payload+0x08) goes out first, then the
// size and that many bytes of the text. The decoder reads the size/bytes pair with
// FUN_06da1620 (RpcGetSizedBlock, allocation before the second bounds check included).
// ===========================================================================
unsigned int RpcEncodeServerSetMissionMessage(FPacketBase& Packet,
                                              unsigned char** BufferHolder,
                                              unsigned int Capacity,
                                              unsigned int* OutLength) {
    const FPayloadServerSetMissionMessage* Wire =
        reinterpret_cast<const FPayloadServerSetMissionMessage*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Type);                                 // / 06dabc21
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    RpcPutSizedBlock(Stream, Wire->TextSize, Wire->TextBuffer);        // / 06dabc5a
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06dabc6a
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerSetMissionMessage(FPacketBase& Packet,
                                              const unsigned char* Buffer,
                                              unsigned int Size,
                                              unsigned int* OutLength) {
    FPayloadServerSetMissionMessage* Wire =
        reinterpret_cast<FPayloadServerSetMissionMessage*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {                                   // 06dabd47 / 06dabd62
        return Stream.GetError();
    }
    Wire->Type = Scratch;
    // Retail quirk: the pointer is overwritten without freeing what the payload held (only
    // the destructor FUN_06d9d9e0 frees it), so a second decode leaks.
    if (!RpcGetSizedBlock(Stream, Wire->TextSize, Wire->TextBuffer)) {
        return Stream.GetError();                                      // FUN_06da1620
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x3d FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE -- LAB_06d8f840 / LAB_06d8f970.
// Vtable 06db3c70 (ctor 06d9ecfd, header 0x2003d). Payload 0x0c.
//
// Carries the same equipment/cargo list as ch2 0x24 / 0x25 and ch3 0x16 / 0x17, through
// the same out-of-line bodies FUN_06da2240 / FUN_06da2480 (RpcPutEquipList /
// RpcGetEquipList in rpc_wire_generic.cpp).
//
// Wire order: ObjectId (payload+0x08), then the byte at payload+0x04 (one byte both ways;
// its padding never travels), then the list at payload+0x00.
// ===========================================================================
unsigned int RpcEncodeServerRemoteObjectCargoUpdate(FPacketBase& Packet,
                                                    unsigned char** BufferHolder,
                                                    unsigned int Capacity,
                                                    unsigned int* OutLength) {
    const FPayloadServerRemoteObjectCargoUpdate* Wire =
        reinterpret_cast<const FPayloadServerRemoteObjectCargoUpdate*>(Packet.Payload());
    // payload+0x00 is the operator new(4) handle the list hangs off (declared as a dword).
    FRpcEquipWireHandle* const* Slot =
        reinterpret_cast<FRpcEquipWireHandle* const*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);                                 // / 06d8f8b8
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->Field_0x04);                                  // / 06d8f8f4
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!RpcPutEquipList(Stream, Slot)) {                              // 06d8f902 FUN_06da2240
        return FRpcMarshalStream::kEncodeOverflow;
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerRemoteObjectCargoUpdate(FPacketBase& Packet,
                                                    const unsigned char* Buffer,
                                                    unsigned int Size,
                                                    unsigned int* OutLength) {
    FPayloadServerRemoteObjectCargoUpdate* Wire =
        reinterpret_cast<FPayloadServerRemoteObjectCargoUpdate*>(Packet.Payload());
    FRpcEquipWireHandle** Slot =
        reinterpret_cast<FRpcEquipWireHandle**>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {                                   // 06d8f9b7 / 06d8f9d6
        return Stream.GetError();
    }
    Wire->ObjectId = Scratch;
    unsigned char Flag = 0;
    if (!Stream.GetByte(Flag)) {                                       // 06d8f9e5 / 06d8fa02
        return Stream.GetError();
    }
    Wire->Field_0x04 = Flag;
    if (!RpcGetEquipList(Stream, Slot)) {                              // 06d8fa17 FUN_06da2480
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}
