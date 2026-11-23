// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_closeout.cpp -- thirteen channel-1 / channel-2 codec slots.
//
//   ch1 0x05 CHATMSG                 vtable 06db4428  enc LAB_06da6420  dec LAB_06da65d0
//   ch1 0x1b POP_UP_DIALOG           vtable 06db43d8  enc LAB_06da5de0  dec LAB_06da5fe0
//   ch2 0x07 LAUNCH                  vtable 06db50ec  enc LAB_06daab40  dec LAB_06daad40
//   ch2 0x22 SYSTEM_SWITCH_IN        vtable 06db505c  enc LAB_06da9260  dec LAB_06da9430
//   ch2 0x2b SETGROUPFEELINGS        vtable 06db3d30  enc LAB_06d91890  dec LAB_06d91a40
//   ch2 0x32 REQUESTSPACESCRIPT      vtable 06db3ff8  enc LAB_06d986e0  dec LAB_06d98850
//   ch2 0x34 REPLACEMISSIONOBJECTIVE vtable 06db512c  enc LAB_06dab4b0  dec LAB_06dab630
//   ch2 0x4b SET_CAMERA              vtable 06db514c  enc LAB_06dab780  dec LAB_06dab960
//   ch2 0x4d LOADHINT                vtable 06db3cd0  enc LAB_06d90a50  dec LAB_06d90ba0
//   ch2 0x4e SETDIRECTIVE            vtable 06db509c  enc LAB_06d90d10  dec LAB_06d90f70
//   ch2 0x52 PLAYERLIST              vtable 06db4038  enc LAB_06d99990  dec LAB_06d99b60
//   ch2 0x55 OBJECTCARGOUPDATE       vtable 06db531c  enc LAB_06dad9b0  dec LAB_06dadb80
//   ch2 0x57 SET_MUSIC               vtable 06db533c  enc LAB_06dadd60  dec LAB_06dadf30
//
// ===========================================================================
// ch2 0x55 and ch2 0x57 have equal-length bodies but different field lists:
// ch2 0x55 is { dword, dword, word, word, float, byte } == 17 wire bytes; ch2 0x57 is
// { dword, dword, dword, dword, byte, float } == 21.
// ===========================================================================

#include "rpc_containers.h"
#include "rpc_payloads.h"

#include <new>
#include <cstring>
#include <cstdlib>

// ===========================================================================
// ch1 0x05 FLPACKET_COMMON_CHATMSG -- LAB_06da6420 (174) / LAB_06da65d0 (154).
//
// { dword Size, Size bytes, dword TargetId, dword SenderId } -- the block first, the
// reverse of struct order. Neither side guards the zero-length case (the decoder calls
// operator new(Size) unconditionally): RpcPutSizedBlock / RpcGetSizedBlock.
// ===========================================================================
unsigned int RpcEncodeCommonChatMsg(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonChatMsg* Wire =
        reinterpret_cast<const FPayloadCommonChatMsg*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    if (!RpcPutSizedBlock(Stream, Wire->Size, Wire->Buffer)) {            // 06da6495/06da64de
        return Stream.GetError();
    }
    Stream.PutDword(Wire->TargetId);
    Stream.PutDword(Wire->SenderId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeCommonChatMsg(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonChatMsg* Wire =
        reinterpret_cast<FPayloadCommonChatMsg*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!RpcGetSizedBlock(Stream, Wire->Size, Wire->Buffer)) {            // 06da6636/06da6686
        return Stream.Failed() ? Stream.GetError()
                               : FRpcMarshalStream::kDecodeOverrun;
    }
    Stream.GetDword(Wire->TargetId);
    Stream.GetDword(Wire->SenderId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch1 0x1b FLPACKET_COMMON_POP_UP_DIALOG -- LAB_06da5de0 (206) / LAB_06da5fe0 (101).
//
// The decoder is two RpcGetSizedBlock calls (FUN_06da1620) plus a dword.
//
// Retail bug, reproduced: the encoder's second block announces SizeB (06da5ede) but
// reserves and copies SizeA bytes out of BufferB (06da5ee8, 06da5f14 / 06da5f19). The two
// sizes are independent FmtStr flattenings, so the stream desynchronises whenever they
// differ, and the copy over-reads BufferB when SizeA > SizeB.
// ===========================================================================
unsigned int RpcEncodeCommonPopUpDialog(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonPopUpDialog* Wire =
        reinterpret_cast<const FPayloadCommonPopUpDialog*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    // First pair, consistent: length SizeA, SizeA bytes of BufferA.
    if (!RpcPutSizedBlock(Stream, Wire->SizeA, Wire->BufferA)) {          // 06da5e55/06da5e9e
        return Stream.GetError();
    }
    // Second pair, retail's mismatch.
    if (!Stream.PutDword(Wire->SizeB)) {                                  // 06da5ede
        return Stream.GetError();
    }
    // Reserves and copies SizeA bytes out of BufferB.
    if (!Stream.PutBytes(Wire->BufferB, Wire->SizeA)) {                   // 06da5f29
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Buttons);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeCommonPopUpDialog(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonPopUpDialog* Wire =
        reinterpret_cast<FPayloadCommonPopUpDialog*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!RpcGetSizedBlock(Stream, Wire->SizeA, Wire->BufferA)) {          // 06da6028
        return Stream.Failed() ? Stream.GetError()
                               : FRpcMarshalStream::kDecodeOverrun;
    }
    if (!RpcGetSizedBlock(Stream, Wire->SizeB, Wire->BufferB)) {          // 06da6033
        return Stream.Failed() ? Stream.GetError()
                               : FRpcMarshalStream::kDecodeOverrun;
    }
    Stream.GetDword(Wire->Buttons);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x07 FLPACKET_SERVER_LAUNCH -- LAB_06daab40 (213) / LAB_06daad40 (159).
//
// dword, dword, dword, Vector, Quaternion. The aggregates move through FUN_06d8f510 (three
// dwords) and FUN_06d8f4f0 (four dwords) in index order -- the same bytes as the field list
// below. (FUN_06d91160 is a third copy of the three-dword copier.)
// ===========================================================================
unsigned int RpcEncodeServerLaunch(FPacketBase& Packet, unsigned char** BufferHolder,
                                   unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerLaunch* Wire =
        reinterpret_cast<const FPayloadServerLaunch*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ShipId);
    Stream.PutDword(Wire->LaunchObjId);
    Stream.PutDword(Wire->LaunchPosIndex);
    Stream.PutFloat(Wire->Position.x);                                    // /06daac6e
    Stream.PutFloat(Wire->Position.y);
    Stream.PutFloat(Wire->Position.z);
    Stream.PutFloat(Wire->Orientation.w);                                 // /06daaccc
    Stream.PutFloat(Wire->Orientation.x);
    Stream.PutFloat(Wire->Orientation.y);
    Stream.PutFloat(Wire->Orientation.z);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerLaunch(FPacketBase& Packet, const unsigned char* Buffer,
                                   unsigned int Size, unsigned int* OutLength) {
    FPayloadServerLaunch* Wire =
        reinterpret_cast<FPayloadServerLaunch*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->ShipId);
    Stream.GetDword(Wire->LaunchObjId);
    Stream.GetDword(Wire->LaunchPosIndex);
    // The aggregates are copied in dword by dword.
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.z));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.w));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.z));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN -- LAB_06da9260 (190) / LAB_06da9430 (143).
// The same shape as LAUNCH minus one leading dword.
// ===========================================================================
unsigned int RpcEncodeServerSystemSwitchIn(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSystemSwitchIn* Wire =
        reinterpret_cast<const FPayloadServerSystemSwitchIn*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ShipId);
    Stream.PutDword(Wire->JumpObjTypeBits);
    Stream.PutFloat(Wire->Position.x);                                    // /06da935c
    Stream.PutFloat(Wire->Position.y);
    Stream.PutFloat(Wire->Position.z);
    Stream.PutFloat(Wire->Orientation.w);                                 // /06da93ba
    Stream.PutFloat(Wire->Orientation.x);
    Stream.PutFloat(Wire->Orientation.y);
    Stream.PutFloat(Wire->Orientation.z);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerSystemSwitchIn(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSystemSwitchIn* Wire =
        reinterpret_cast<FPayloadServerSystemSwitchIn*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->ShipId);
    Stream.GetDword(Wire->JumpObjTypeBits);
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.z));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.w));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.z));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x2b FLPACKET_SERVER_SETGROUPFEELINGS -- LAB_06d91890 (180) / LAB_06d91a40 (223).
//
// The vtable is 06db3d30 (the constructor's second store), not the intermediate base
// 06db4fbc it writes first.
//
// Retail bug, reproduced: the encoder never writes the relation count its own decoder
// reads. It emits NameSize, the name bytes (skipped when NameSize == 0) and, if the list's
// low size word is non-zero, each element's Feeling then GroupId -- but no count. The
// decoder reads a word count after the name. The decoder's bounds checks stop a bogus
// count; "fixing" it would put bytes on the wire retail does not (docs/re/deviations.md).
// ===========================================================================
unsigned int RpcEncodeServerSetGroupFeelings(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSetGroupFeelings* Wire =
        reinterpret_cast<const FPayloadServerSetGroupFeelings*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    if (!Stream.PutDword(Wire->NameSize)) {                               // 06d91903
        return Stream.GetError();
    }
    if (Wire->NameSize != 0) {                                            // 06d91912
        if (!Stream.PutBytes(Wire->Name, Wire->NameSize)) {                // 06d91949
            return Stream.GetError();
        }
    }
    // Only the low word of the size is tested, so a list of exactly 0x10000 elements is
    // treated as empty. Kept. The dummy is dereferenced untested.
    if ((unsigned short)(Wire->Relations.Size & 0xffffu) != 0) {
        for (const FRpcRelationNode* Node = Wire->Relations.Head->Next;
             Node != Wire->Relations.Head && !Stream.Failed(); Node = Node->Next) {
            const FRpcRelation* Relation = Node->Value;
            if (Relation == nullptr) {
                continue;
            }
            Stream.PutDword(Relation->FeelingBits);                        // FLD/FSTP
            Stream.PutDword(Relation->GroupId);
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerSetGroupFeelings(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetGroupFeelings* Wire =
        reinterpret_cast<FPayloadServerSetGroupFeelings*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->NameSize)) {                               // 06d91aba
        return Stream.GetError();
    }
    // Unconditional bare operator new, before the byte run's check.
    Wire->Name = static_cast<char*>(
        ::operator new(Wire->NameSize, std::nothrow));
    if (!Stream.GetBytes(Wire->Name, Wire->NameSize)) {                   // 06d91afe
        return Stream.GetError();
    }
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {                                         // 06d91b3b
        return Stream.GetError();
    }
    for (unsigned short Index = 0; Index < Count; ++Index) {              // 06d91b4a
        // Retail quirk: operator new(8) before the two reads; a failed bounds check leaks
        // the element.
        FRpcRelation* Relation = static_cast<FRpcRelation*>(
            ::operator new(sizeof(FRpcRelation), std::nothrow));
        if (!Stream.GetDword(Relation->FeelingBits)) {                     // 06d91b86
            return Stream.GetError();
        }
        if (!Stream.GetDword(Relation->GroupId)) {                     // 06d91bae
            return Stream.GetError();
        }
        // operator new(0xc) node; bumps the size at packet+0x30.
        RpcRelationListLink(&Wire->Relations, Relation);
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT -- LAB_06d986e0 (148) / LAB_06d98850 (136).
//
// { dword ObjectId, dword NameSize, NameSize bytes }. Position and camera mode
// (payload+0x08..+0x14) never reach the wire.
//
// Retail bug, reproduced: the decoder copies into Name (packet+0x38) without allocating
// it, and the constructor zeroes it -- on a fresh packet a copy to address zero. Retail
// gets away with it because this codec never runs in single player.
// ===========================================================================
unsigned int RpcEncodeServerRequestSpaceScript(FPacketBase& Packet,
                                               unsigned char** BufferHolder,
                                               unsigned int Capacity,
                                               unsigned int* OutLength) {
    const FPayloadServerRequestSpaceScript* Wire =
        reinterpret_cast<const FPayloadServerRequestSpaceScript*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    if (!Stream.PutDword(Wire->NameSize)) {                               // 06d98787
        return Stream.GetError();
    }
    if (Wire->NameSize != 0) {                                            // 06d98796 JBE
        if (!Stream.PutBytes(Wire->Name, Wire->NameSize)) {                // 06d987d4
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerRequestSpaceScript(FPacketBase& Packet,
                                               const unsigned char* Buffer,
                                               unsigned int Size,
                                               unsigned int* OutLength) {
    FPayloadServerRequestSpaceScript* Wire =
        reinterpret_cast<FPayloadServerRequestSpaceScript*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->ObjectId);
    if (!Stream.GetDword(Wire->NameSize)) {                               // 06d988ea
        return Stream.GetError();
    }
    if (Wire->NameSize != 0) {                                            // 06d988ed
        // Bounds check, then an untested copy into packet+0x38: GetBytes.
        if (!Stream.GetBytes(Wire->Name, Wire->NameSize)) {
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE -- LAB_06dab4b0 (150) /
// LAB_06dab630 (110).
//
// { dword, dword, dword TextSize, TextSize bytes }; the decoder reads the contiguous
// {TextSize, TextBuffer} pair at payload+0x08 with FUN_06da1620.
// ===========================================================================
unsigned int RpcEncodeServerReplaceMissionObjective(FPacketBase& Packet,
                                                    unsigned char** BufferHolder,
                                                    unsigned int Capacity,
                                                    unsigned int* OutLength) {
    const FPayloadServerReplaceMissionObjective* Wire =
        reinterpret_cast<const FPayloadServerReplaceMissionObjective*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->MissionId);
    Stream.PutDword(Wire->State);
    if (!RpcPutSizedBlock(Stream, Wire->TextSize, Wire->TextBuffer)) {    // 06dab572/06dab5af
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerReplaceMissionObjective(FPacketBase& Packet,
                                                    const unsigned char* Buffer,
                                                    unsigned int Size,
                                                    unsigned int* OutLength) {
    FPayloadServerReplaceMissionObjective* Wire =
        reinterpret_cast<FPayloadServerReplaceMissionObjective*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->MissionId);
    Stream.GetDword(Wire->State);
    if (!RpcGetSizedBlock(Stream, Wire->TextSize, Wire->TextBuffer)) {    // 06dab6d0
        return Stream.Failed() ? Stream.GetError()
                               : FRpcMarshalStream::kDecodeOverrun;
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x4b FLPACKET_SERVER_SET_CAMERA -- LAB_06dab780 (190) / LAB_06dab960 (141).
// Vector, Quaternion, float, float -- no leading scalars.
// ===========================================================================
unsigned int RpcEncodeServerSetCamera(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSetCamera* Wire =
        reinterpret_cast<const FPayloadServerSetCamera*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Position.x));  // /06dab81a
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Position.y));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Position.z));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Orient.w));    // /06dab878
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Orient.x));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Orient.y));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Orient.z));
    // These two go through the x87 stack; the seven above are plain dword moves.
    Stream.PutFloat(Wire->Fov);
    Stream.PutFloat(Wire->Aspect);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerSetCamera(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetCamera* Wire =
        reinterpret_cast<FPayloadServerSetCamera*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.z));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orient.w));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orient.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orient.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orient.z));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Fov));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Aspect));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x4d FLPACKET_SERVER_LOADHINT -- LAB_06d90a50 (134) / LAB_06d90ba0 (140).
//
// { dword Size, dword Kind, Size bytes } -- not the FUN_06da1620 pair: the block follows
// both scalars. The decoder allocates by the size read first.
// ===========================================================================
unsigned int RpcEncodeServerLoadHint(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerLoadHint* Wire =
        reinterpret_cast<const FPayloadServerLoadHint*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Size);
    if (!Stream.PutDword(Wire->Kind)) {                                   // 06d90ae8
        return Stream.GetError();
    }
    // No zero-length guard on either side of this type.
    if (!Stream.PutBytes(Wire->Buffer, Wire->Size)) {                      // 06d90b25
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerLoadHint(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength) {
    FPayloadServerLoadHint* Wire =
        reinterpret_cast<FPayloadServerLoadHint*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Size);
    if (!Stream.GetDword(Wire->Kind)) {                                   // 06d90c3c
        return Stream.GetError();
    }
    // Unconditional, before the run's own bounds check.
    Wire->Buffer = ::operator new(Wire->Size, std::nothrow);              // bare
    if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {                     // 06d90c7f
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x4e FLPACKET_SERVER_SETDIRECTIVE -- LAB_06d90d10 (176) / LAB_06d90f70 (156).
//
// The whole 0x94-byte payload, no permutation. The encoder reserves 0x94 once, rebuilds
// the struct on its stack field by field and copies it out with FUN_06d911b0 (a 0x94-byte
// member-wise assignment); the decoder writes wire+n to payload+n.
//
// The three bytes after the byte fields at wire+0x08 and wire+0x90 are never touched:
// retail's stack temporary ships stack residue there. PutGap / GetGap leave the
// destination alone, the closest model.
// ===========================================================================
unsigned int RpcEncodeServerSetDirective(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const unsigned char* Wire = Packet.Payload();

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    // The four runs add up to exactly 0x94.
    Stream.PutBytes(Wire + 0x00, 0x09u);
    Stream.PutGap(0x03u);                                                 // wire+0x09..0x0b
    Stream.PutBytes(Wire + 0x0c, 0x85u);
    Stream.PutGap(0x03u);                                                 // wire+0x91..0x93
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerSetDirective(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    unsigned char* Wire = Packet.Payload();

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetBytes(Wire + 0x00, 0x09u);
    Stream.GetGap(0x03u);
    Stream.GetBytes(Wire + 0x0c, 0x85u);
    Stream.GetGap(0x03u);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x52 FLPACKET_SERVER_PLAYERLIST -- LAB_06d99990 (205) / LAB_06d99b60 (170).
//
// { dword Kind, dword PlayerId, byte bAlreadyPresent, byte N, 2*N bytes } where
//
//     N = (Name == nullptr) ? 0 : (unsigned char)(wcslen(Name) + 1)
//
// with the + 1 applied after truncation to a byte: the terminator is counted, and a name
// of 255 wide characters wraps to N == 0 and travels as no name.
//
// The decoder uses MSVCRT malloc(2*N), untested; ~FPacket_FLPACKET_SERVER_PLAYERLIST frees
// with ::free. When N is zero Name is left as it was.
// ===========================================================================
unsigned int RpcEncodeServerPlayerList(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerPlayerList* Wire =
        reinterpret_cast<const FPayloadServerPlayerList*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Kind);
    Stream.PutDword(Wire->PlayerId);
    if (!Stream.PutByte(Wire->bAlreadyPresent)) {                              // 06d99a5f
        return Stream.GetError();
    }
    unsigned char Count = 0;
    if (Wire->Name != nullptr) {
        unsigned int Length = 0;
        while (Wire->Name[Length] != 0) {
            ++Length;
        }
        Count = (unsigned char)(((unsigned char)(Length & 0xffu)) + 1u);
    }
    if (!Stream.PutByte(Count)) {                                         // 06d99a9f
        return Stream.GetError();
    }
    if (Count != 0) {
        if (!Stream.PutBytes(Wire->Name, (unsigned int)Count * 2u)) {      // 06d99add
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerPlayerList(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadServerPlayerList* Wire =
        reinterpret_cast<FPayloadServerPlayerList*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Kind);
    Stream.GetDword(Wire->PlayerId);
    Stream.GetByte(Wire->bAlreadyPresent);
    unsigned char Count = 0;
    if (!Stream.GetByte(Count)) {                                         // 06d99c39
        return Stream.GetError();
    }
    if (Count != 0) {                                                     // 06d99c40
        const unsigned int Bytes = (unsigned int)Count * 2u;
        // Never tested.
        Wire->Name = static_cast<wchar_t*>(::malloc(Bytes));
        if (!Stream.GetBytes(Wire->Name, Bytes)) {                        // 06d99c87
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x55 FLPACKET_SERVER_OBJECTCARGOUPDATE -- LAB_06dad9b0 (207) / LAB_06dadb80 (166).
// dword, dword, word, word, float, byte. See the twin note at the head of this file.
// ===========================================================================
unsigned int RpcEncodeServerObjectCargoUpdate(FPacketBase& Packet,
                                              unsigned char** BufferHolder,
                                              unsigned int Capacity,
                                              unsigned int* OutLength) {
    const FPayloadServerObjectCargoUpdate* Wire =
        reinterpret_cast<const FPayloadServerObjectCargoUpdate*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    Stream.PutDword(Wire->ArchId);
    Stream.PutWord(Wire->EquipId);
    Stream.PutWord(Wire->Count);
    Stream.PutFloat(Wire->Status);                                        // FLD/FSTP
    Stream.PutByte(Wire->bMission);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerObjectCargoUpdate(FPacketBase& Packet,
                                              const unsigned char* Buffer,
                                              unsigned int Size,
                                              unsigned int* OutLength) {
    FPayloadServerObjectCargoUpdate* Wire =
        reinterpret_cast<FPayloadServerObjectCargoUpdate*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->ObjectId);
    Stream.GetDword(Wire->ArchId);
    Stream.GetWord(Wire->EquipId);
    Stream.GetWord(Wire->Count);
    // Read back as a plain dword, not through the x87 stack.
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Status));
    Stream.GetByte(Wire->bMission);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x57 FLPACKET_SERVER_SET_MUSIC -- LAB_06dadd60 (207) / LAB_06dadf30 (166).
// dword, dword, dword, dword, byte, float. Not the same list as ch2 0x55.
// ===========================================================================
unsigned int RpcEncodeServerSetMusic(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSetMusic* Wire =
        reinterpret_cast<const FPayloadServerSetMusic*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->SpaceMusic);
    Stream.PutDword(Wire->DangerMusic);
    Stream.PutDword(Wire->BattleMusic);
    Stream.PutDword(Wire->OverrideMusic);
    Stream.PutByte(Wire->bNoLoop);
    Stream.PutFloat(Wire->Fade);                                    // FLD/FSTP
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerSetMusic(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetMusic* Wire =
        reinterpret_cast<FPayloadServerSetMusic*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->SpaceMusic);
    Stream.GetDword(Wire->DangerMusic);
    Stream.GetDword(Wire->BattleMusic);
    Stream.GetDword(Wire->OverrideMusic);
    Stream.GetByte(Wire->bNoLoop);
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Fade));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}
