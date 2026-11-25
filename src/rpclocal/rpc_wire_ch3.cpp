// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_ch3.cpp -- nine channel-3 packet codec slots, including the two near-twin
// payloads REQADDITEM / REQMODIFYITEM.
//
// Slot addresses come from the vtable the constructor publishes; its last vtable store is
// the one that counts (13 of the 162 constructors first store the base vtable, e.g. ch3
// 0x03 MUNCOLLISION writes PTR_FUN_06db4fbc before PTR_FUN_06db5b54).
//
//   ch3 0x01 LOGIN            vtable 06db5ca4  enc LAB_06dafb10  dec FUN_06dafd40
//   ch3 0x03 MUNCOLLISION     vtable 06db5b54  enc LAB_06dae280  dec LAB_06dae430
//   ch3 0x0c OBJCOLLISION     vtable 06db5b74  enc LAB_06dae770  dec LAB_06dae910
//   ch3 0x18 REQADDITEM       vtable 06db4054  enc LAB_06d99d90  dec LAB_06d99f80
//   ch3 0x1a REQMODIFYITEM    vtable 06db4070  enc LAB_06d9a220  dec LAB_06d9a410
//   ch3 0x1e SAVEGAME         vtable 06db40a8  enc LAB_06d9a800  dec LAB_06d9a950
//   ch3 0x20 MINEASTEROID     vtable 06db5b84  enc LAB_06daeac0  dec LAB_06daeca0
//   ch3 0x31 REQUEST_EVENT    vtable 06db5dd4  enc LAB_06daa240  dec LAB_06daa3e0
//   ch3 0x39 CREATENEWCHAR    vtable 06db5c74  enc LAB_06daf410  dec LAB_06daf5b0
//
// ===========================================================================
// REQADDITEM and REQMODIFYITEM have opcode-identical bodies that differ only in which
// payload dword they load: the three dwords at packet+0x20 / +0x24 / +0x28 go out as
// (0x20, 0x24, 0x28) for REQADDITEM and (0x28, 0x20, 0x24) for REQMODIFYITEM, and the
// trailing block's length is packet+0x28 in one and packet+0x24 in the other. The decoders
// carry the same permutation, matching the senders' layouts (HardpointSize at payload+0x08
// vs +0x04).
//
// ch2 0x55 OBJECTCARGOUPDATE and ch2 0x57 SET_MUSIC are another equal-length pair, but
// genuinely different field lists (rpc_wire_closeout.cpp).
// ===========================================================================

#include "rpc_containers.h"
#include "rpc_payloads.h"
#include "rpc_packet_classes.h"

#include <new>
#include <cstring>
#include <cwchar>

// Inline wcslen (retail calls MSVCRT), avoiding _CRT_SECURE_NO_WARNINGS.
static unsigned int RpcWideLength(const wchar_t* Text) {
    unsigned int Length = 0;
    if (Text == nullptr) {
        return 0;
    }
    while (Text[Length] != 0) {
        ++Length;
    }
    return Length;
}


// ===========================================================================
// ch3 0x01 FLPACKET_CLIENT_LOGIN -- LAB_06dafb10 (220) / FUN_06dafd40 (197).
//
// Wire: { byte CreateFlag, byte LenA, byte LenB, LenA wide chars, LenB wide chars }.
//
// Retail quirks, reproduced:
//  * the two lengths are wcslen truncated to a byte, NUL not counted: a 256-character
//    name travels as no name;
//  * the encoder lowercases the account name in the packet itself (_wcslwr), so a second
//    Encode sees the lowered text; the password is not lowered.
//
// The decoder zeroes both buffers first (0x402 bytes from packet+0x20) and NUL-terminates
// each string after the copy.
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_LOGIN::Encode(unsigned char** BufferHolder,
                                                   unsigned int Capacity, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    if (!Stream.PutByte(Wire.CreateFlag)) {                               // 06dafb86/06dafb92
        return Stream.GetError();
    }
    // Both lengths kept as bytes.
    const unsigned char LenA =
        (unsigned char)(RpcWideLength(Wire.AccountName) & 0xffu);
    const unsigned char LenB =
        (unsigned char)(RpcWideLength(Wire.Password) & 0xffu);
    if (!Stream.PutByte(LenA)) {                                          // 06dafbe1
        return Stream.GetError();
    }
    if (!Stream.PutByte(LenB)) {                                          // 06dafc10
        return Stream.GetError();
    }
    if (LenA != 0) {                                                      // 06dafc17
        // MSVCRT _wcslwr (IAT .rdata:06db320c), on the packet itself.
        _wcslwr(Wire.AccountName);
        if (!Stream.PutBytes(Wire.AccountName, (unsigned int)LenA * 2u)) {   // 06dafc63
            return Stream.GetError();
        }
    }
    if (LenB != 0) {                                                      // 06dafc7b
        if (!Stream.PutBytes(Wire.Password, (unsigned int)LenB * 2u)) {      // 06dafcc1
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_LOGIN::Decode(const unsigned char* Buffer, unsigned int Size,
                                                   unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // 0x402 bytes from packet+0x20: both wide buffers and CreateFlag, which the read below
    // overwrites.
    ::memset(Wire.AccountName, 0, 0x402u);
    if (!Stream.GetByte(Wire.CreateFlag)) {                               // 06dafdba
        return Stream.GetError();
    }
    unsigned char LenA = 0;
    if (!Stream.GetByte(LenA)) {                                          // 06dafde5
        return Stream.GetError();
    }
    unsigned char LenB = 0;
    if (!Stream.GetByte(LenB)) {                                          // 06dafe08
        return Stream.GetError();
    }
    if (LenA != 0) {                                                      // 06dafe13
        if (!Stream.GetBytes(Wire.AccountName, (unsigned int)LenA * 2u)) {   // 06dafe49
            return Stream.GetError();
        }
        Wire.AccountName[LenA] = L'\0';
    }
    if (LenB != 0) {                                                      // 06dafe6e
        if (!Stream.GetBytes(Wire.Password, (unsigned int)LenB * 2u)) {      // 06dafeae
            return Stream.GetError();
        }
        Wire.Password[LenB] = L'\0';
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x03 FLPACKET_CLIENT_MUNCOLLISION -- LAB_06dae280 (168) / LAB_06dae430 (158).
// Flat: dword, dword, dword, WORD, Vector. The word is the only 2-byte field and it
// leaves payload+0x0e as a hole neither side touches.
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_MUNCOLLISION::Encode(unsigned char** BufferHolder,
                                                          unsigned int Capacity,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    Stream.PutDword(Wire.ProjectileArchId);
    Stream.PutDword(Wire.AttackerId);
    Stream.PutDword(Wire.ObjectId);
    Stream.PutWord(Wire.SubObjId);
    // The three-dword copier FUN_06d8f510, via a stack temp.
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire.HitPosX));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire.HitPosY));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire.HitPosZ));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_MUNCOLLISION::Decode(const unsigned char* Buffer,
                                                          unsigned int Size,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire.ProjectileArchId);
    Stream.GetDword(Wire.AttackerId);
    Stream.GetDword(Wire.ObjectId);
    Stream.GetWord(Wire.SubObjId);
    // FUN_06d8f510 into packet+0x30.
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.HitPosX));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.HitPosY));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.HitPosZ));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x0c FLPACKET_CLIENT_OBJCOLLISION -- LAB_06dae770 (186) / LAB_06dae910 (151).
// dword, WORD, dword, WORD, float. Both 2-byte fields leave a 2-byte hole behind them
// in the struct and neither hole reaches the wire.
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_OBJCOLLISION::Encode(unsigned char** BufferHolder,
                                                          unsigned int Capacity,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    Stream.PutDword(Wire.ObjectId);
    Stream.PutWord(Wire.SubObjId);
    Stream.PutDword(Wire.TargetObjectId);
    Stream.PutWord(Wire.TargetSubObjId);
    // FLD/FSTP in retail.
    Stream.PutFloat(*reinterpret_cast<const float*>(&Wire.DamageBits));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_OBJCOLLISION::Decode(const unsigned char* Buffer,
                                                          unsigned int Size,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire.ObjectId);
    Stream.GetWord(Wire.SubObjId);
    Stream.GetDword(Wire.TargetObjectId);
    Stream.GetWord(Wire.TargetSubObjId);
    // Read back as a plain dword, not through the x87 stack.
    Stream.GetDword(Wire.DamageBits);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x18 FLPACKET_CLIENT_REQADDITEM -- LAB_06d99d90 (221) / LAB_06d99f80 (186).
// Wire order: ArchId, Count, Health, bMounted, HardpointSize, HardpointSize bytes.
// See the permutation note at the head of this file before touching the order.
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_REQADDITEM::Encode(unsigned char** BufferHolder,
                                                        unsigned int Capacity,
                                                        unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    Stream.PutDword(Wire.ArchId);                                         // packet+0x20
    Stream.PutDword((unsigned int)Wire.Count);                            // packet+0x24
    Stream.PutFloat(Wire.Health);                                         // packet+0x2c
    Stream.PutByte(Wire.bMounted);                                        // packet+0x30
    Stream.PutDword(Wire.HardpointSize);                                  // packet+0x28
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // A zero length skips the block.
    if (Wire.HardpointSize != 0) {
        if (!Stream.PutBytes(Wire.Hardpoint, Wire.HardpointSize)) {       // 06d99f02
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_REQADDITEM::Decode(const unsigned char* Buffer,
                                                        unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire.ArchId);
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.Count));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.Health));
    Stream.GetByte(Wire.bMounted);
    Stream.GetDword(Wire.HardpointSize);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // Retail allocates unconditionally, before the byte run's bounds check; the packet's
    // destructor frees it.
    Wire.Hardpoint = static_cast<char*>(
        ::operator new(Wire.HardpointSize, std::nothrow));                // bare
    if (!Stream.GetBytes(Wire.Hardpoint, Wire.HardpointSize)) {           // 06d9a0d6
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x1a FLPACKET_CLIENT_REQMODIFYITEM -- LAB_06d9a220 (221) / LAB_06d9a410 (186).
// Same shape, different order: EquipId, Count, Health, bMounted, HardpointSize, block.
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_REQMODIFYITEM::Encode(unsigned char** BufferHolder,
                                                           unsigned int Capacity,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    Stream.PutDword(Wire.EquipId);                                        // packet+0x28
    Stream.PutDword((unsigned int)Wire.Count);                            // packet+0x20
    Stream.PutFloat(Wire.Health);                                         // packet+0x2c
    Stream.PutByte(Wire.bMounted);                                        // packet+0x30
    Stream.PutDword(Wire.HardpointSize);                                  // packet+0x24
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (Wire.HardpointSize != 0) {                                        // 06d9a35b
        if (!Stream.PutBytes(Wire.Hardpoint, Wire.HardpointSize)) {       // 06d9a392
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_REQMODIFYITEM::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire.EquipId);
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.Count));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.Health));
    Stream.GetByte(Wire.bMounted);
    Stream.GetDword(Wire.HardpointSize);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Wire.Hardpoint = static_cast<char*>(
        ::operator new(Wire.HardpointSize, std::nothrow));                // bare
    if (!Stream.GetBytes(Wire.Hardpoint, Wire.HardpointSize)) {           // 06d9a566
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x1e FLPACKET_CLIENT_SAVEGAME -- LAB_06d9a800 (133) / LAB_06d9a950 (128).
//
// { dword DescriptionCount, 2*DescriptionCount bytes }. The 0x200-byte CHARACTER_ID at
// payload+0x000 is not serialized by either side -- retail's wire format. The count is in
// wide characters.
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_SAVEGAME::Encode(unsigned char** BufferHolder,
                                                      unsigned int Capacity,
                                                      unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    if (!Stream.PutDword(Wire.DescriptionCount)) {                        // 06d9a875
        return Stream.GetError();
    }
    if (Wire.DescriptionCount != 0) {                                     // 06d9a88a
        if (!Stream.PutBytes(Wire.Description,
                             Wire.DescriptionCount * 2u)) {               // 06d9a8d3
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_SAVEGAME::Decode(const unsigned char* Buffer,
                                                      unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire.DescriptionCount)) {                        // 06d9a9c1
        return Stream.GetError();
    }
    if (Wire.DescriptionCount != 0) {                                     // 06d9a9c7
        Wire.Description = static_cast<wchar_t*>(
            ::operator new(Wire.DescriptionCount * 2u, std::nothrow));    // bare
        if (!Stream.GetBytes(Wire.Description,
                             Wire.DescriptionCount * 2u)) {               // 06d9aa1f
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x20 FLPACKET_CLIENT_MINEASTEROID -- LAB_06daeac0 (202) / LAB_06daeca0 (160).
// dword, Vector, dword, dword, dword.
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_MINEASTEROID::Encode(unsigned char** BufferHolder,
                                                          unsigned int Capacity,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    Stream.PutDword(Wire.SystemId);
    // FUN_06d8f510 copies a stack temp out in index order (the temp's load order is the
    // compiler's, not the wire order).
    Stream.PutFloat(Wire.PositionX);
    Stream.PutFloat(Wire.PositionY);
    Stream.PutFloat(Wire.PositionZ);
    Stream.PutDword(Wire.LootId);
    Stream.PutDword(Wire.SecondaryLoot);
    Stream.PutDword(Wire.Quantity);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_MINEASTEROID::Decode(const unsigned char* Buffer,
                                                          unsigned int Size,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire.SystemId);
    // FUN_06d8f510 into packet+0x24.
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.PositionX));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.PositionY));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire.PositionZ));
    Stream.GetDword(Wire.LootId);
    Stream.GetDword(Wire.SecondaryLoot);
    Stream.GetDword(Wire.Quantity);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x31 FLPACKET_CLIENT_REQUEST_EVENT -- LAB_06daa240 (185) / LAB_06daa3e0 (151).
// byte, dword, dword, dword, byte.
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_REQUEST_EVENT::Encode(unsigned char** BufferHolder,
                                                           unsigned int Capacity,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    Stream.PutByte(Wire.EventType);
    Stream.PutDword(Wire.ShipId);
    Stream.PutDword(Wire.TargetId);
    Stream.PutDword(Wire.SecondId);
    Stream.PutByte(Wire.bFlag);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_REQUEST_EVENT::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetByte(Wire.EventType);
    Stream.GetDword(Wire.ShipId);
    Stream.GetDword(Wire.TargetId);
    Stream.GetDword(Wire.SecondId);
    Stream.GetByte(Wire.bFlag);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x39 FLPACKET_CLIENT_CREATENEWCHAR -- LAB_06daf410 (180) / LAB_06daf5b0 (163).
//
// { word NameLength, NameLength wide chars, dword, dword, dword, dword }. The name goes
// through FUN_06da14f0 / FUN_06da1580 (PutWideString / GetWideString), the module's only
// users: word length without the NUL, a JC bounds test that rejects a string exactly
// filling the buffer, and -- for a zero length -- a buffer pointer left un-advanced (see
// rpc_transport.h and docs/re/deviations.md).
// ===========================================================================
unsigned int FPacket_FLPACKET_CLIENT_CREATENEWCHAR::Encode(unsigned char** BufferHolder,
                                                           unsigned int Capacity,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));
    if (!Stream.PutWideString(Wire.Name)) {                               // 06daf465 FUN_06da14f0
        return FRpcMarshalStream::kEncodeOverflow;
    }
    Stream.PutDword(Wire.FactionId);
    Stream.PutDword(Wire.BaseId);
    Stream.PutDword(Wire.PackageId);
    Stream.PutDword(Wire.PilotId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_CLIENT_CREATENEWCHAR::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    wchar_t* Name = nullptr;
    if (!Stream.GetWideString(Name)) {                                    // 06daf5ff FUN_06da1580
        return FRpcMarshalStream::kDecodeOverrun;
    }
    // Retail guards the wcscpy against the null an empty string leaves; the destination
    // then keeps whatever it held.
    if (Name != nullptr) {
        // Retail quirk: unbounded wcscpy. The wire length is a word, so a name longer than
        // the 0x40-wchar_t field runs over the four dwords (which the reads below then
        // overwrite) and on past the end of the packet.
        wcscpy(Wire.Name, Name);
        ::operator delete(Name);
    }
    Stream.GetDword(Wire.FactionId);
    Stream.GetDword(Wire.BaseId);
    Stream.GetDword(Wire.PackageId);
    Stream.GetDword(Wire.PilotId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}
