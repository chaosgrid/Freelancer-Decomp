// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_ch1.cpp -- retail channel-1 packet codec slots (vtable +0x04 Encode,
// +0x08 Decode).
//
// Slot ownership follows the constructor recipe at the head of rpc_wire_ch2.cpp:
//   ch1 0x01 FLPACKET_COMMON_UPDATEOBJECT, constructor FUN_06d9af10, routing header 0x10001,
//   vtable 06db4378 = { dtor FUN_06d9af50, Encode LAB_06da49e0, Decode FUN_06da4ca0,
//   dump (IServerImpl::GetServerStats) }.
//
// The constructor seeds 1.0f at packet+0x34 and zeroes +0x38..+0x40: the identity
// quaternion W-first, so payload+0x14 is W and +0x18..+0x20 are X, Y, Z.
//
// The hottest packet in the game (one per moving object per tick): the 0x2c-byte payload
// leaves as 26 bytes of body plus the 2 header bytes, the quaternion and Throttle quantised
// to a byte each. Wire order:
//
//     payload+0x00  byte   State
//     payload+0x04  dword  ObjectId
//     payload+0x08  three dwords (FUN_06d8f510) Position
//     payload+0x18  Orientation.x  quantised byte
//     payload+0x1c  Orientation.y  quantised byte
//     payload+0x20  Orientation.z  quantised byte
//     payload+0x14  Orientation.w  quantised byte
//     payload+0x28  Throttle       quantised byte
//     payload+0x24  Timestamp      raw float
//
// The quaternion travels x,y,z,w -- w last -- on both ends (the encoder computes the four
// ftol results in a different order than it writes them; the writes are what count).
//
// Scale 127.0 (DAT_06db3fd8) out, 1/127 (DAT_06db3fe0) back, and all five bytes are read
// back sign-extended: the [-1,+1] tier, not the [0,1] percentage tier.
//
// Timestamp is not quantised: FLD/FSTP out, a plain dword back, no FMUL near it.
//
// Both slots end in the shared byte-statistics tail (see FPacketWireStats). In single
// player packets are queued by pointer and neither slot is called.

#include "rpc_containers.h"
#include "rpc_payloads.h"
#include "rpc_packet_classes.h"

#include <new>

// ===========================================================================
// ch1 0x01 FLPACKET_COMMON_UPDATEOBJECT -- Encode LAB_06da49e0, Decode FUN_06da4ca0
// ===========================================================================

// ===========================================================================
// ch1 codec slots transcribed in rpc_wire_ch1.cpp.
// ===========================================================================
unsigned int FPacket_FLPACKET_COMMON_UPDATEOBJECT::Encode(unsigned char** BufferHolder,
                                                          unsigned int Capacity,
                                                          unsigned int* OutLength) {
    const FPayloadCommonUpdateObject* Wire =
        reinterpret_cast<const FPayloadCommonUpdateObject*>(Payload());

    // The encoder's buffer argument is a pointer to the buffer pointer.
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380

    Stream.PutByte(Wire->State);
    Stream.PutDword(Wire->ObjectId);
    Stream.PutBytes(&Wire->Position, 12u);                         // FUN_06d8f510

    Stream.PutQuantized(Wire->Orientation.x, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->Orientation.y, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->Orientation.z, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->Orientation.w, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->Throttle, FRpcMarshalStream::kOrientationScale);
    Stream.PutFloat(Wire->Timestamp);

    // FUN_06da13c0 (`XOR EAX,EAX ; RET 0xc`) emits nothing.
    if (Stream.Failed()) {
        // Returns 0x16 without writing the out-length.
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_COMMON_UPDATEOBJECT::Decode(const unsigned char* Buffer,
                                                          unsigned int Size,
                                                          unsigned int* OutLength) {
    FPayloadCommonUpdateObject* Wire =
        reinterpret_cast<FPayloadCommonUpdateObject*>(Payload());

    // The decoder's buffer argument is the buffer pointer itself (no second dereference).
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // FUN_06da13c0 at the start consumes nothing: the dispatcher already ate the two header
    // bytes, which is why this side's length excludes them.
    if (Stream.Failed()) {
        return Stream.GetError();
    }

    unsigned char Small8 = 0;
    unsigned int Scratch = 0;
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire->State = Small8;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->ObjectId = Scratch;
    if (!Stream.GetBytes(&Wire->Position, 12u)) { return Stream.GetError(); }

    if (!Stream.GetQuantizedSigned(Wire->Orientation.x,
                                   FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    if (!Stream.GetQuantizedSigned(Wire->Orientation.y,
                                   FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    if (!Stream.GetQuantizedSigned(Wire->Orientation.z,
                                   FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    if (!Stream.GetQuantizedSigned(Wire->Orientation.w,
                                   FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    if (!Stream.GetQuantizedSigned(Wire->Throttle,
                                   FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    // A plain dword, no scale.
    if (!Stream.GetFloat(Wire->Timestamp)) { return Stream.GetError(); }

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch1 0x1d FLPACKET_COMMON_TRACTOROBJECTS -- LAB_06da7a00 / LAB_06da7b90
//
// Constructor FUN_06d9bbc0, routing header 0x1001d, vtable 06db44b8 = { dtor FUN_06d9bc00,
// Encode LAB_06da7a00, Decode LAB_06da7b90, IServerImpl::GetServerStats }.
//
// The constructor zeroes the dword at +0x20 and the word at +0x24, copies an uninitialised
// allocator byte to +0x28 (not a field), and zeroes First/Last/End at +0x2c..+0x34:
// { dword, word, pad, { allocator, First, Last, End } }, i.e. FPayloadCommonTractorObjects.
//
// Wire layout:
//     dword ObjectId          packet+0x20
//     word  TractorEquipId    packet+0x24
//     word  Count
//     Count * dword
//
// There is no size field: the count is (Last - First) >> 2 (0 when First is null), but the
// encoder's element loop walks First..Last, not that count.
//
// The decoder's middle is an inlined VC6 std::vector::insert(end, 1, v) with growth to
// Size + max(Size, 1) (RpcRetailVectorAppend, rpc_containers.h, shared with the FIREWEAPON
// decoder; bare operator new). The retail capacity policy is kept because End feeds the
// next encode. The decoder appends: it never clears the vector the packet already holds.
//
// In single player packets are queued by pointer and neither slot is called.
// ===========================================================================

// 0x06da7a00 / 0x06da7b90 -- ch1 0x1d TRACTOROBJECTS: dword, word, word count, then that
// many dwords appended into the payload's own std::vector<unsigned int>.
// 0x06da7a00 -- Encode.
unsigned int FPacket_FLPACKET_COMMON_TRACTOROBJECTS::Encode(unsigned char** BufferHolder,
                                                            unsigned int Capacity,
                                                            unsigned int* OutLength) {
    const FPayloadCommonTractorObjects* Wire =
        reinterpret_cast<const FPayloadCommonTractorObjects*>(Payload());

    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    if (!Stream.PutDword(Wire->ObjectId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutWord(Wire->TractorEquipId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    const unsigned int Count = Wire->TargetIds.Count();
    if (!Stream.PutWord((unsigned short)(Count & 0xffffu))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // Walks First..Last, not the count just written.
    for (const unsigned int* It = Wire->TargetIds.Begin;
         It != Wire->TargetIds.End; ++It) {
        if (!Stream.PutDword(*It)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

// 0x06da7b90 -- Decode.
unsigned int FPacket_FLPACKET_COMMON_TRACTOROBJECTS::Decode(const unsigned char* Buffer,
                                                            unsigned int Size,
                                                            unsigned int* OutLength) {
    FPayloadCommonTractorObjects* Wire =
        reinterpret_cast<FPayloadCommonTractorObjects*>(Payload());

    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    if (!Stream.GetDword(Wire->ObjectId)) {
        return Stream.GetError();
    }
    if (!Stream.GetWord(Wire->TractorEquipId)) {
        return Stream.GetError();
    }
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {
        return Stream.GetError();
    }
    // The zero-extended word is compared signed against zero; 0 skips to the tail.
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        unsigned int Value = 0;
        if (!Stream.GetDword(Value)) {
            return Stream.GetError();
        }
        RpcRetailVectorAppend(Wire->TargetIds, Value);
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}
