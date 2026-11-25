// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_generic.cpp -- the SHARED packet codec slots.
//
// Most small packet types share one of a dozen shape-only Encode/Decode bodies that every
// vtable of that shape points at. This file transcribes twenty-four such bodies, carrying
// 86 of the 162 registered types. Bodies are tied to types by the vtable each constructor
// stores before its routing header (e.g. ch2 0x02 LOGINRESPONSE, ctor FUN_06d9c860,
// vtable 06db4ffc = { FUN_06d9af50, LAB_06da82e0, LAB_06da83c0, GetServerStats }).
//
// Ghidra's XREF headers under-report (LAB_06db0310 prints 21 referents, .rdata holds 30;
// LAB_06db03f0 33), so the type lists below come from the .rdata scan.
//
// A shared body proves the wire format and payload offsets its types have in common,
// nothing beyond. It matches the per-type structs, e.g. FPayloadServerBlob {Size, Buffer}
// vs FPayloadServerBlobReversed {Buffer, Size} (decoders LAB_06d978f0 / LAB_06d97730).
//
// Differences between otherwise identical bodies, all reproduced:
//   * LAB_06da13d0 inlines the header write, LAB_06dae0d0 calls FUN_06da1380; neither can
//     fail (see PutHeaderUnchecked).
//   * LAB_06da5bc0 allocates Size + 1 and NUL-terminates; LAB_06da62d0 does neither
//     (SET_WEAPON_GROUP's blob is a C string).
//   * LAB_06db10e0 skips allocation and copy when the count is zero; LAB_06d97730 does
//     not.
//   * LAB_06da62d0 / LAB_06da5bc0 reuse a buffer the packet already holds; LAB_06d978f0
//     and LAB_06d97730 allocate unconditionally and leak the old one.
//
// The blob encoder LAB_06da6190 spills its destination pointer into its own second
// argument slot; reading the stack slots as consecutive locals would permute the fields.
//
// Every body ends in the statistics tail (see RpcRecordDecodedPacket for the Max + 2
// quirk).
//
// In single player packets are queued by pointer and neither slot is called.

#include "rpc_containers.h"
#include "rpc_payloads.h"
#include "rpc_packet_classes.h"

#include <cstddef>
#include <cstring>
#include <new>

// ===========================================================================
// Shape 0 -- no payload at all. Registry size 0x20, payload 0x00.
//
// LAB_06da13d0 / LAB_06da1470, 8 .rdata slots each, 5 of them registered types:
//   ch3 0x05 REQUESTCHARINFO, 0x0a GFREQUESTSHIPINFO, 0x10 REQUESTCREATESHIP,
//   ch3 0x38 REQUESTNEWCHARINFO, 0x42 SKIP_AUTOSAVE.
// The other three slots belong to base-class vtables, not registry types:
//   * 06db4fbc, the root packet base {dtor, Encode, Decode, Dump}: 13 constructors store it
//     before their own vtable and routing header (e.g. SETGROUPFEELINGS, 06d9c575 then
//     06d9c5a5), and FUN_06da1370 restores it before chaining to FUN_06d8f4c0, which
//     installs the one-slot grandparent 06db3c54 (scalar deleting dtor LAB_06d8f4d0).
//   * 06db3fb4, an intermediate base of the create-object family (stored only by the
//     destructors FUN_06d97b90, FUN_06d9e740).
//   * 06db5e54, the base of the five equipment-carrying classes (ch2 0x24 / 0x25 / 0x3d,
//     ch3 0x16 / 0x17), stored by FUN_06da21d0, which frees the equipment handle.
// Two further vtables first taken for bases are registry types: 06db408c is ch3 0x27
// TRADERESPONSE (ctor FUN_06da08e0, codec at the end of this file) and 06db5d64 is ch3 0x23
// DBGLOADSYSTEM (ctor FUN_06da0ec0).
// ===========================================================================

// 0x06da13d0 -- Encode. Header only, and it cannot fail; see PutHeaderUnchecked.
unsigned int RpcEncodeWireEmptyInline(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength) {
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    *OutLength = Stream.Length();                                  // always 2
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06da1470 -- Decode. Consumes nothing.
unsigned int RpcDecodeWireEmptyInline(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    *OutLength = Stream.Length();                                  // always 0
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 0b -- no payload, the out-of-line form. Registry size 0x20.
//
// LAB_06dae0d0 / LAB_06dabad0, 3 .rdata slots each, all three registered:
//   ch2 0x4c REVERT_CAMERA, ch2 0x58 CANCEL_MUSIC, ch3 0x2e DBGFREESYSTEM.
// Same result as shape 0; it differs only in calling FUN_06da1380 rather than inlining
// the header write (neither can fail).
// ===========================================================================

// 0x06dae0d0 -- Encode.
unsigned int RpcEncodeWireEmpty(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength) {
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06dabad0 -- Decode.
unsigned int RpcDecodeWireEmpty(FPacketBase& Packet, const unsigned char* Buffer,
                                unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 0x17
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 1 -- one dword. Registry size 0x24, payload 0x04.
//
// LAB_06db0310 (30 .rdata slots) / LAB_06db03f0 (33). The decoder serves three more
// vtables than the encoder because the float encoder below shares it. The registered
// types, from the .rdata scan:
//   ch2 0x01 CONNECTRESPONSE, 0x0e GFCOMPLETENEWSBROADCASTLIST,
//   ch2 0x0f GFCOMPLETECHARLIST, 0x10 GFCOMPLETEMISSIONCOMPUTERLIST,
//   ch2 0x11 GFCOMPLETESCRIPTBEHAVIORLIST, 0x13 GFCOMPLETEAMBIENTSCRIPTLIST,
//   ch2 0x23 SETSHIPARCH, 0x30 SETCASH, 0x3e SETNUMKILLS, 0x3f SETMISSIONSUCCESSES,
//   ch2 0x40 SETMISSIONFAILURES, 0x43 SET_STORY_CUE, 0x56 SENDNNMESSAGE,
//   ch2 0x59 PLAY_SOUND_EFFECT, 0x5b MISSIONSAVEA,
//   ch3 0x04 REQUESTLAUNCH, 0x07 ENTERBASE, 0x0b SYSTEM_SWITCH_OUT_COMPLETE,
//   ch3 0x0d EXITBASE, 0x0e ENTERLOCATION, 0x0f EXITLOCATION, 0x13 GFSELECTOBJECT,
//   ch3 0x15 REQSHIPARCH, 0x1b REQSETCASH, 0x1c REQCHANGECASH, 0x24 DOCK,
//   ch3 0x35 INTERFACEITEMUSED, 0x41 ABORT_MISSION, 0x45 MISSIONSAVEB, 0x47 RTCDONE.
// ===========================================================================

// 0x06db0310 -- Encode.
unsigned int RpcEncodeWireDword(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerDword* Wire =
        reinterpret_cast<const FPayloadServerDword*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    // 06db0357..06db0385: advance 4 and check, then `MOV ECX,[EDI+0x20] ; MOV [EAX],ECX`.
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06db03a2
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06db03f0 -- Decode.
unsigned int RpcDecodeWireDword(FPacketBase& Packet, const unsigned char* Buffer,
                                unsigned int Size, unsigned int* OutLength) {
    FPayloadServerDword* Wire = reinterpret_cast<FPayloadServerDword*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 1f -- one float. Registry size 0x24, payload 0x04.
//
// LAB_06db1cd0, 3 .rdata slots, all registered: ch2 0x49 SETHULLSTATUS,
// ch3 0x3b REQHULLSTATUS, ch3 0x46 REQDIFFICULTYSCALE.
// Differs from shape 1's encoder only in moving the field through FLD/FSTP; the decoder
// is shape 1's LAB_06db03f0, so these types read the field back as a dword.
// ===========================================================================

// 0x06db1cd0 -- Encode.
unsigned int RpcEncodeWireFloat(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerFloat* Wire =
        reinterpret_cast<const FPayloadServerFloat*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutFloat(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06db1d62
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 2 -- one byte. Registry size 0x24, payload 0x04, but only the first byte travels.
//
// LAB_06da82e0 / LAB_06da83c0, 2 .rdata slots each, both registered:
//   ch2 0x02 LOGINRESPONSE, ch2 0x5a GFMISSIONVENDORWHYEMPTY.
// ===========================================================================

// 0x06da82e0 -- Encode.
unsigned int RpcEncodeWireByte(FPacketBase& Packet, unsigned char** BufferHolder,
                               unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerByte* Wire =
        reinterpret_cast<const FPayloadServerByte*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutByte(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06da836e
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06da83c0 -- Decode.
unsigned int RpcDecodeWireByte(FPacketBase& Packet, const unsigned char* Buffer,
                               unsigned int Size, unsigned int* OutLength) {
    FPayloadServerByte* Wire = reinterpret_cast<FPayloadServerByte*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned char Small8 = 0;
    if (!Stream.GetByte(Small8)) {                                     // 06da8407 / 06da8420
        return Stream.GetError();
    }
    Wire->Field_0x00 = Small8;
    // The other three payload bytes keep the constructor's zeroes.
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 3 -- two dwords. Registry size 0x28, payload 0x08.
//
// LAB_06da9110 / LAB_06d95d40, 13 .rdata slots each, all registered:
//   ch2 0x0d SETSTARTROOM, 0x14 GFDESTROYNEWSBROADCAST, 0x15 GFDESTROYCHARACTER,
//   ch2 0x16 GFDESTROYMISSIONCOMPUTER, 0x17 GFDESTROYSCRIPTBEHAVIOR,
//   ch2 0x19 GFDESTROYAMBIENTSCRIPT, 0x21 SYSTEM_SWITCH_OUT, 0x39 SCANNOTIFY,
//   ch3 0x19 REQREMOVEITEM, 0x2b CARGOSCAN, 0x3e LAUNCHCOMPLETE, 0x43 JUMPINCOMPLETE.
// (The thirteenth slot is vtable 06db408c, ch3 0x27 TRADERESPONSE's.)
// ===========================================================================

// 0x06da9110 -- Encode.
unsigned int RpcEncodeWireDwordPair(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerDwordPair* Wire =
        reinterpret_cast<const FPayloadServerDwordPair*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Field_0x04);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06da91d7
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06d95d40 -- Decode.
unsigned int RpcDecodeWireDwordPair(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength) {
    FPayloadServerDwordPair* Wire =
        reinterpret_cast<FPayloadServerDwordPair*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x04 = Scratch;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 4 -- a dword then a byte. Registry size 0x28, payload 0x08, five bytes on the
// wire.
//
// LAB_06dab0e0 / LAB_06daf230, 5 .rdata slots each, all registered:
//   ch1 0x15 ACTIVATETHRUSTERS, ch2 0x06 DESTROYOBJECT, ch2 0x46 MARKOBJ,
//   ch3 0x08 REQUESTBASEINFO, ch3 0x09 REQUESTLOCATIONINFO.
// The {dword, byte, 3 pad} shape matches DESTROYOBJECT's and MARKOBJ's sender layouts.
// ===========================================================================

// 0x06dab0e0 -- Encode.
unsigned int RpcEncodeWireDwordByte(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerDwordByte* Wire =
        reinterpret_cast<const FPayloadServerDwordByte*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06dab17a
    }
    Stream.PutByte(Wire->Field_0x04);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06daf230 -- Decode.
unsigned int RpcDecodeWireDwordByte(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength) {
    FPayloadServerDwordByte* Wire =
        reinterpret_cast<FPayloadServerDwordByte*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;
    unsigned char Small8 = 0;
    if (!Stream.GetByte(Small8)) {
        return Stream.GetError();
    }
    Wire->Field_0x04 = Small8;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 5 -- three dwords. Registry size 0x2c, payload 0x0c.
//
// LAB_06db0160 / LAB_06da6e40, 8 .rdata slots each, all registered:
//   ch1 0x10 STOPTRADELANE, ch2 0x0b LAND, ch2 0x2f SETREMOVEITEM,
//   ch2 0x3c REPAIROBJECT, ch3 0x11 GFGOODSELL, ch3 0x2f SETMANEUVER,
//   ch3 0x3c GFGOODVAPORIZED, ch3 0x3f HAIL.
// Payload order both ways. Types whose struct calls a field a narrower type (e.g. ch2 0x2f
// SETREMOVEITEM) still move a whole dword: the padding travels.
// ===========================================================================

// 0x06db0160 -- Encode.
unsigned int RpcEncodeWireThreeDwords(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerThreeDwords* Wire =
        reinterpret_cast<const FPayloadServerThreeDwords*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Field_0x04);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Field_0x08);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06db025c
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06da6e40 -- Decode.
unsigned int RpcDecodeWireThreeDwords(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    FPayloadServerThreeDwords* Wire =
        reinterpret_cast<FPayloadServerThreeDwords*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x04 = Scratch;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x08 = Scratch;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 6 -- a dword then a twelve-byte block. Registry size 0x30, payload 0x10.
//
// LAB_06db14f0 / LAB_06dad820, 3 .rdata slots each, all registered:
//   ch2 0x31 EXPLODEASTEROIDMINE, ch2 0x53 FORMATION_UPDATE, ch3 0x30 DBGRELOCATE_SHIP.
// The twelve bytes move through FUN_06d8f510 (the three-dword Vector copier) under one
// 0xc cursor bump: one run, not three fields. FORMATION_UPDATE's {dword, Vector} and
// EXPLODEASTEROIDMINE's {four dwords} are the same sixteen bytes; the neutral form is used.
// ===========================================================================

// 0x06db14f0 -- Encode.
unsigned int RpcEncodeWireDwordBlock12(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerFourDwords* Wire =
        reinterpret_cast<const FPayloadServerFourDwords*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // One bounds check guards the whole run.
    Stream.PutBytes(&Wire->Field_0x04, 12u);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06db15e4
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06dad820 -- Decode.
unsigned int RpcDecodeWireDwordBlock12(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadServerFourDwords* Wire =
        reinterpret_cast<FPayloadServerFourDwords*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;
    if (!Stream.GetBytes(&Wire->Field_0x04, 12u)) {                    // 06dad897 / 06dad8b6
        return Stream.GetError();
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 7 -- {dword count, that many raw bytes}, count FIRST in the payload.
// Registry size 0x28, payload 0x08 == FPayloadServerBlob {Size, Buffer}.
//
// LAB_06d97570 / LAB_06d978f0, 5 .rdata slots each, all registered:
//   ch2 0x1a GFSCRIPTBEHAVIOR, 0x1d GFUPDATEMISSIONCOMPUTER,
//   ch2 0x1e GFUPDATENEWSBROADCAST, 0x20 GFMISSIONVENDORACCEPTANCE, 0x26 GFUPDATECHAR.
//
// Retail quirk: the decoder allocates unconditionally between the count's bounds check and
// the run's, without testing the existing pointer, so a packet decoded twice leaks its
// first block. The packet class's destructor owns the block (rpc_packet_registry.cpp).
// ===========================================================================

// 0x06d97570 -- Encode.
unsigned int RpcEncodeWireBlobSizeFirst(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerBlob* Wire =
        reinterpret_cast<const FPayloadServerBlob*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06d97612
    }
    // The cursor is advanced by the whole run before the copy.
    Stream.PutBytes(Wire->Buffer, Wire->Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06d978f0 -- Decode.
unsigned int RpcDecodeWireBlobSizeFirst(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadServerBlob* Wire = reinterpret_cast<FPayloadServerBlob*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Size = Scratch;
    Wire->Buffer = ::operator new(Wire->Size, std::nothrow);
    if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {                  // 06d9796e / 06d979a6
        return Stream.GetError();
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 8 -- the same {dword count, raw bytes} pair, but the payload holds the POINTER
// first and the count second. Encoder LAB_06da6190, 10 .rdata slots.
//
// The count comes from packet+0x24 and the bytes from packet+0x20, the mirror of shape 7.
// Its ten types do not all share a decoder:
//   ch1 0x11 SET_WEAPON_GROUP, 0x13 SET_VISITED_STATE, 0x16 REQUEST_BEST_PATH,
//   ch1 0x17 REQUEST_GROUP_POSITIONS, 0x18 REQUEST_PLAYER_STATS,
//   ch1 0x19 SET_MISSION_LOG, 0x1a REQUEST_RANK_LEVEL, 0x1c SET_INTERFACE_STATE,
//   ch2 0x1f GFUPDATEAMBIENTSCRIPT, ch3 0x23 DBGLOADSYSTEM.
// The channel-1 ones carry the 0x0c FPayloadCommonBuffer, the last two the 0x08
// FPayloadServerBlobReversed; both start {Buffer, Size}, all this encoder touches.
// ===========================================================================

// 0x06da6190 -- Encode.
unsigned int RpcEncodeWireBlobBufferFirst(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerBlobReversed* Wire =
        reinterpret_cast<const FPayloadServerBlobReversed*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // / 06da6232
    }
    // See the file header on the spilled destination pointer.
    Stream.PutBytes(Wire->Buffer, Wire->Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06d97730 -- allocates unconditionally.
// 0x06d97730 -- Decode, the 0x28 form. ch2 0x1f GFUPDATEAMBIENTSCRIPT only.
// Allocates unconditionally, exactly like shape 7's decoder.
unsigned int FPacket_FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT::Decode(const unsigned char* Buffer,
                                                                   unsigned int Size,
                                                                   unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire.Size = Scratch;
    Wire.Buffer = ::operator new(Wire.Size, std::nothrow);
    if (!Stream.GetBytes(Wire.Buffer, Wire.Size)) {                    // 06d977ae onward
        return Stream.GetError();
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// 0x06db10e0 -- skips the allocation and the run when the count is zero.
// 0x06db10e0 -- Decode, the 0x28 form again, ch3 0x23 DBGLOADSYSTEM only.
//
// The same body as 0x06d97730 except that a zero count skips both the allocation and the
// byte run, leaving the constructor's null buffer pointer.
unsigned int FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire.Size = Scratch;
    if (Wire.Size != 0) {                                              // 06db114f / 06db1154
        Wire.Buffer = ::operator new(Wire.Size, std::nothrow);
        if (!Stream.GetBytes(Wire.Buffer, Wire.Size)) {                // 06db1162 onward
            return Stream.GetError();
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// 0x06da62d0 -- Decode, the 0x0c channel-1 buffer family. 7 .rdata slots:
//   ch1 0x13 SET_VISITED_STATE, 0x16 REQUEST_BEST_PATH, 0x17 REQUEST_GROUP_POSITIONS,
//   ch1 0x18 REQUEST_PLAYER_STATS, 0x19 SET_MISSION_LOG, 0x1a REQUEST_RANK_LEVEL,
//   ch1 0x1c SET_INTERFACE_STATE.
//
// Two differences from the 0x28 forms:
//   * the allocation is skipped when the packet already holds a pointer;
//   * a fresh allocation sets bOwnsBuffer = 1, as the channel-1 senders do, so the class
//     destructor frees it (FPayloadCommonBuffer).
unsigned int RpcDecodeWireOwnedBuffer(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonBuffer* Wire =
        reinterpret_cast<FPayloadCommonBuffer*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Size = Scratch;
    if (Wire->Buffer == nullptr) {                                     // 06da6340
        Wire->Buffer = ::operator new(Wire->Size, std::nothrow);
        Wire->bOwnsBuffer = 1;
    }
    if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {                  // 06da635c / 06da6382
        return Stream.GetError();
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06da5bc0 -- the same, but allocates Size + 1 and NUL-terminates. ch1 0x11 only.
// 0x06da5bc0 -- Decode, ch1 0x11 SET_WEAPON_GROUP only (1 .rdata slot, 06db439c).
//
// 0x06da62d0 plus two instructions: the block is Size + 1 bytes and Buffer[Size] = 0, so
// SET_WEAPON_GROUP's blob is a NUL-terminated string where the family's others are not.
unsigned int FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP::Decode(const unsigned char* Buffer,
                                                              unsigned int Size,
                                                              unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire.Size = Scratch;
    if (Wire.Buffer == nullptr) {                                      // 06da5c30 / 06da5c38
        Wire.Buffer = ::operator new(Wire.Size + 1u, std::nothrow);    // / 06da5c3e
        Wire.bOwnsBuffer = 1;
    }
    if (!Stream.GetBytes(Wire.Buffer, Wire.Size)) {                    // 06da5c4d / 06da5c87
        return Stream.GetError();
    }
    // Retail quirk: written unconditionally, even into a pre-existing buffer it did not
    // size itself.
    reinterpret_cast<unsigned char*>(Wire.Buffer)[Wire.Size] = 0;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// Shape 25 -- the EQUIPMENT/CARGO LIST. Registry size 0x24, payload 0x04.
//
// Slots LAB_06d916a0 / LAB_06d95bf0, bodies FUN_06da2240 (encode) and FUN_06da2480
// (decode). Four types share the pair, each from its constructor's vtable and routing
// header: ch2 0x24 SETEQUIPMENT (06db3cf0), ch2 0x25 SETCARGO (06db3d10), ch3 0x16
// REQEQUIPMENT (06db3de0), ch3 0x17 REQCARGO (06db3dfc) -- so cargo and equipment have the
// same wire format.
//
// The payload is a pointer to a 4-byte handle (operator new(4)) pointing at a 0x0c list
// header { allocator, dummy node, Size } whose 0x0c nodes point at 0x18 records
// (rpc_containers.h).
//
// Wire order:
//   word  Count            the list's Size, only its low word
//   per element:
//   dword Count            record+0x04
//   float Status           record+0x08, FLD/FSTP, not quantised
//   dword ArchId           record+0x00
//   word  Id               record+0x0c
//   byte  bEquipped        record+0x0e
//   byte  bMission         record+0x0f
//   word  HardpointSize    record+0x10
//   HardpointSize bytes    (bytes, not characters)
//
// Status here is a raw float, unlike CREATESHIP's 255.0-scaled equipment status byte. The
// decoder moves it back with a plain dword copy (bit-exact). ch2 0x03 CHARACTERINFO embeds
// the same seven fields in the same order.
//
// Decoder quirks, reproduced:
//   * operator new(HardpointSize) even for a zero size, stored untested;
//   * a failed operator new(0x18) skips the element but advances the counter;
//   * an error mid-record leaks the half-built record and its hardpoint buffer;
//   * it appends, never clearing a list the packet already holds;
//   * the loop counter is compared as a word.
// ===========================================================================

// ===========================================================================
// FUN_06da2240 / FUN_06da2480, factored out of the slots that call them. Retail calls each
// from exactly two places: the slots above and the ch2 0x3d REMOTEOBJECTCARGOUPDATE codec
// (LAB_06d8f840). Their three stack arguments are dead (every access goes through the
// packet), hence the stream-and-slot signature.
// ===========================================================================
bool RpcPutEquipList(FRpcMarshalStream& Stream, FRpcEquipWireHandle* const* Slot) {
    // The count is zero when the payload slot is null or the handle has no list yet.
    unsigned short Count = 0;
    const FRpcEquipWireListHeader* Header = (*Slot)->GetList();
    if (Header != nullptr) {
        Count = (unsigned short)(Header->Size & 0xffffu);
    }
    if (!Stream.PutWord(Count)) {                                      // 06da2261..06da2295
        return false;
    }
    if (Count != 0) {
        // The handle is dereferenced and tested a second time.
        const FRpcEquipWireListHeader* Walk = (*Slot)->GetList();
        if (Walk != nullptr && Walk->Head != nullptr) {
            const FRpcEquipWireNode* Node = Walk->Head->Next;
            while (Node != Walk->Head) {
                // The node's value is not null-tested in retail.
                const FRpcEquipWireRecord* Item = Node->Value;
                if (!Stream.PutDword((unsigned int)Item->Count)) {      // 06da2304
                    return false;
                }
                if (!Stream.PutFloat(Item->Status)) {                   // 06da232c FLD/FSTP
                    return false;
                }
                if (!Stream.PutDword(Item->ArchId)) {                   // 06da235c
                    return false;
                }
                if (!Stream.PutWord(Item->Id)) {                        // 06da2388
                    return false;
                }
                if (!Stream.PutByte(Item->bEquipped)) {                 // 06da23b6
                    return false;
                }
                if (!Stream.PutByte(Item->bMission)) {                // 06da23e0
                    return false;
                }
                if (!Stream.PutWord(Item->HardpointSize)) {             // 06da240b
                    return false;
                }
                // Bytes, not characters: no doubling.
                if (!Stream.PutBytes(Item->Hardpoint, Item->HardpointSize)) {
                    return false;
                }
                Node = Node->Next;
            }                                                           // 06da2466/06da246d
        }
    }
    return true;
}

bool RpcGetEquipList(FRpcMarshalStream& Stream, FRpcEquipWireHandle** Slot) {
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {                                       // 06da2487..06da24b2
        return false;
    }
    // An empty list is a complete packet; no handle is allocated for it.
    if (Count != 0) {
        if (*Slot == nullptr) {                                         // 06da24c8
            FRpcEquipWireHandle* Handle =
                static_cast<FRpcEquipWireHandle*>(::operator new(sizeof(FRpcEquipWireHandle),
                                                                 std::nothrow));
            if (Handle != nullptr) {
                Handle->List = nullptr;
            }
            *Slot = Handle;
        }
        // Retail tests the count a second time before the loop.
        for (unsigned short Index = 0; Index < Count; ++Index) {        // 06da26bf..06da26cb
            FRpcEquipWireRecord* Item =
                static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                                 std::nothrow));
            if (Item == nullptr) {
                // Silently dropped; not an error in retail.
                continue;
            }
            // All eight fields zeroed, in retail's order.
            Item->ArchId = 0;
            Item->Count = 0;
            Item->Status = 0.0f;
            Item->Id = 0;
            Item->bEquipped = 0;
            Item->bMission = 0;
            Item->HardpointSize = 0;
            Item->Pad_0x12[0] = 0;
            Item->Pad_0x12[1] = 0;
            Item->Hardpoint = nullptr;

            unsigned int Scratch = 0;
            if (!Stream.GetDword(Scratch)) {                            // 06da2551
                return false;
            }
            Item->Count = (int)Scratch;                                 // record+0x04
            // A raw dword copy on this side.
            if (!Stream.GetFloat(Item->Status)) {
                return false;
            }
            if (!Stream.GetDword(Item->ArchId)) {                       // 06da25a3
                return false;
            }
            if (!Stream.GetWord(Item->Id)) {                            // 06da25d9
                return false;
            }
            if (!Stream.GetByte(Item->bEquipped)) {                     // 06da2600
                return false;
            }
            if (!Stream.GetByte(Item->bMission)) {                    // 06da2620
                return false;
            }
            if (!Stream.GetWord(Item->HardpointSize)) {                 // 06da2647
                return false;
            }
            // Allocated before the run's bounds check, untested; a zero size still
            // allocates.
            Item->Hardpoint =
                static_cast<char*>(::operator new(Item->HardpointSize, std::nothrow));
            if (!Stream.GetBytes(Item->Hardpoint, Item->HardpointSize)) {
                return false;
            }
            // 06da26b1 FUN_06d91580 -- the list takes ownership of the record.
            (*Slot)->PushBack(Item);
        }
    }
    return true;
}

// 0x06d916a0 (slot) + 0x06da2240 (body) -- Encode.
unsigned int RpcEncodeWireEquipList(FRpcEquipListPacket& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength) {
    FRpcEquipWireHandle* const* Slot = &Packet.EquipHandle;

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));

    if (!RpcPutEquipList(Stream, Slot)) {                                // FUN_06da2240
        return FRpcMarshalStream::kEncodeOverflow;
    }

    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// 0x06d95bf0 (slot) + 0x06da2480 (body) -- Decode.
unsigned int RpcDecodeWireEquipList(FRpcEquipListPacket& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength) {
    FRpcEquipWireHandle** Slot = &Packet.EquipHandle;

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);

    if (!RpcGetEquipList(Stream, Slot)) {                                // FUN_06da2480
        return Stream.GetError();
    }

    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x27 FLPACKET_CLIENT_TRADERESPONSE -- LAB_06da9110 / LAB_06d9a660
//
// Constructor FUN_06da08e0, header 0x30027, vtable 06db408c = { dtor FUN_06d9a7b0, Encode
// LAB_06da9110, Decode LAB_06d9a660, GetServerStats }. The encoder is the shared
// dword-pair body (RpcEncodeWireDwordPair); the decoder is its own.
//
// Retail bug, reproduced: the pair does not round-trip. The payload is FPayloadClientBlob
// { Size, Buffer } with Buffer a packet-owned copy (sender IServerImpl::TradeResponse
// 0x06d8dad0 via FUN_06d97b00; the destructor frees it). The shared encoder ships the
// pointer itself -- four bytes of address, not Size bytes of data. The decoder allocates
// Size bytes into Buffer and then overwrites Buffer with the second wire dword, leaking the
// block and leaving the sender's address. It only works locally because the packet is
// handed over by pointer; the single-player path does not reach here.
//
// Both bounds checks store the proper decoder error 0x17 (unlike PLAYER_TRADE).
// ===========================================================================

// 0x06d9a660 -- ch3 0x27 TRADERESPONSE's own decoder. Its ENCODE side is the shared
// RpcEncodeWireDwordPair (0x06da9110); the pair does not round-trip, see rpc_wire_generic.cpp.
// 0x06d9a660 -- Decode. The encode side is RpcEncodeWireDwordPair (0x06da9110).
unsigned int FPacket_FLPACKET_CLIENT_TRADERESPONSE::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    if (!Stream.GetDword(Wire.Size)) {
        return Stream.GetError();
    }
    // Allocated, stored, never written to: the next field overwrites the pointer.
    Wire.Buffer = ::operator new(Wire.Size, std::nothrow);
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    // The second wire dword lands on top of the pointer.
    Wire.Buffer = reinterpret_cast<void*>(static_cast<size_t>(Scratch));

    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}
