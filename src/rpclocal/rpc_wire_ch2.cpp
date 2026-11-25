// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_ch2.cpp -- retail channel-2 packet codec slots (vtable +0x04 Encode,
// +0x08 Decode). The retail bodies are anonymous LAB_ blocks reached only from .rdata
// vtable slots.
//
// Each packet class is identified by the routing header its constructor writes right
// after the vptr (162 such constructors, one per registry entry):
//   ch2 0x28 CREATELOOT      vtable 06db50ac = { dtor FUN_06d9d1d0, Encode LAB_06d928c0,
//                            Decode LAB_06d92d20, dump (IServerImpl::GetServerStats) }
//   ch2 0x29 SETREPUTATION   vtable 06db3d50 = { FUN_06d91eb0, LAB_06d91ed0, LAB_06d92350,
//                            FUN_06d92860 }
//   ch2 0x54 MISCOBJUPDATE   vtable 06db4018 = { FUN_06d98aa0, LAB_06d98d30, LAB_06d990d0,
//                            dump }
//
// Argument conventions:
//   * Encode (RET 0xc): arg1 is a pointer to the buffer pointer (dereferenced), arg2 the
//     capacity, arg3 receives the byte count including the two header bytes.
//   * Decode (RET 0xc): arg1 is the buffer pointer itself, arg2 the size, arg3 the
//     consumed count.
//   * FUN_06da1380 writes the two header bytes; FUN_06da13c0 (`XOR EAX,EAX ; RET 0xc`)
//     does nothing and is called at the end of an encoder and the start of a decoder.
//   * On failure retail returns the sticky error without touching the out-length.
//
// In single player packets are queued by pointer and neither slot is called.

#include "rpc_containers.h"
#include "rpc_payloads.h"

// COMMON.DLL imports used here: [06db3108] Archetype::LargeIDToSmallID,
// [06db3000] Archetype::SmallIDToLargeID, [06db305c] SinglePlayer.
#include "common_goods.h"
#include "common_misc.h" // SinglePlayer
#include "rpc_packet_classes.h"

#include <cstdlib>
#include <cstring>
#include <new>

// ===========================================================================
// ch2 0x28 FLPACKET_SERVER_CREATELOOT -- Encode LAB_06d928c0, Decode LAB_06d92d20
//
// Payload 0x14 (registry 0x34). The constructor stores a dword at +0x20, a byte at +0x24,
// the std::list allocator byte at +0x28 (garbage nobody reads), the dummy of a list with
// 0x50-byte nodes at +0x2c and the count at +0x30; the element is therefore 0x48 (the
// decoder's insert FUN_06d931f0 allocates the same 0x50).
//
// The wire element, in order:
//     +0x00  dword
//     +0x04  Archetype::LargeIDToSmallID -> word
//     +0x08  float (FLD/FSTP on encode, plain dword on decode)
//     +0x0c  word
//     +0x10  Archetype::LargeIDToSmallID -> word
//     +0x14  float
//     +0x18  dword
//     +0x1c  three dwords (FUN_06d8f510)
//   if (SinglePlayer()):
//     +0x28  three dwords (FUN_06d8f510)
//     +0x34  four dwords  (FUN_06d8f4f0)
//     +0x44  byte
//     +0x45  byte
//
// The single-player gate runs the other way round from CREATESHIP's (whose reputation tail
// is skipped in single player): here the two extra vectors travel only in single player.
// Both directions agree, so this is a per-site polarity, not PLAYER_TRADE's
// encoder/decoder inversion.
//
// FUN_06d8f510 / FUN_06d8f4f0 are three- and four-dword copies whose arguments swap between
// the directions.
// ===========================================================================

// ===========================================================================
// ch2 codec slots transcribed in rpc_wire_ch2.cpp -- see the file header there for
// how each packet class was tied to its channel/opcode through its constructor.
// ===========================================================================
unsigned int FPacket_FLPACKET_SERVER_CREATELOOT::Encode(unsigned char** BufferHolder,
                                                        unsigned int Capacity,
                                                        unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    Stream.PutDword(Wire.OwnerId);                          // payload+0x00
    Stream.PutByte(Wire.Kind);                           // payload+0x04

    // Only the low byte of the list's Size travels; zero skips the loop.
    const unsigned char Count = (unsigned char)(Wire.Items.Size & 0xffu);
    Stream.PutByte(Count);
    if (Count != 0) {
        // 06d929bd dereferences the dummy node with no null check. A non-zero count
        // means PushBackLoot has built it.
        for (const FRpcLootWireNode* Node = Wire.Items.Head->Next;
             Node != Wire.Items.Head && !Stream.Failed(); Node = Node->Next) {
            const FRpcLootWireRecord& Record = Node->Value;
            Stream.PutDword(Record.LootId);
            Stream.PutWord(Archetype::LargeIDToSmallID(Record.ContentsNickname));
            Stream.PutDword(Record.ContentsHitPtsBits);                                   // FSTP
            Stream.PutWord(Record.Units);
            Stream.PutWord(Archetype::LargeIDToSmallID(Record.ContainerNickname));
            Stream.PutDword(Record.HitPtsBits);                                   // FSTP
            Stream.PutDword(Record.NameOverride);
            Stream.PutBytes(&Record.PosXBits, 12);
            if (SinglePlayer()) {                                                 // 06d92b7f / 06d92b87
                Stream.PutBytes(&Record.VelXBits, 12);
                Stream.PutBytes(&Record.OrientWBits, 16);
            }
            Stream.PutByte(Record.bMarkedBy);
            Stream.PutByte(Record.bTemporary);
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    // The byte-statistics tail runs on the success path only.
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_CREATELOOT::Decode(const unsigned char* Buffer,
                                                        unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // The dispatcher already consumed the two header bytes; the decoder's length excludes
    // them.

    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire.OwnerId = Scratch;
    unsigned char Small8 = 0;
    if (!Stream.GetByte(Small8)) {
        return Stream.GetError();
    }
    Wire.Kind = Small8;
    unsigned char Count = 0;
    if (!Stream.GetByte(Count)) {                              // 06d92dd7
        return Stream.GetError();
    }
    // Zero elements skip the loop; it counts with a byte.
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        // Retail zeroes a 0x48-byte stack record; FUN_06d931c0 writes the identity
        // quaternion (1, 0, 0, 0) at record+0x34, the only non-zero default.
        FRpcLootWireRecord Fresh;
        unsigned char* Raw = reinterpret_cast<unsigned char*>(&Fresh);
        for (unsigned int b = 0; b < (unsigned int)sizeof(FRpcLootWireRecord); ++b) {
            Raw[b] = 0;
        }
        Fresh.OrientWBits = 0x3f800000u;

        // FUN_06d931f0 == std::list::insert(end(), record): the element is linked first
        // and filled in place, so a short buffer leaves a partially decoded element.
        PushBackLoot(Fresh);
        FRpcLootWireList* List = &Wire.Items;
        FRpcLootWireRecord& Record = List->Head->Prev->Value;

        unsigned short Small = 0;
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Record.LootId = Scratch;
        if (!Stream.GetWord(Small)) { return Stream.GetError(); }
        // Zero-extended, then converted.
        Record.ContentsNickname = Archetype::SmallIDToLargeID(Small);
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Record.ContentsHitPtsBits = Scratch;
        if (!Stream.GetWord(Small)) { return Stream.GetError(); }
        Record.Units = Small;
        if (!Stream.GetWord(Small)) { return Stream.GetError(); }
        Record.ContainerNickname = Archetype::SmallIDToLargeID(Small);
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Record.HitPtsBits = Scratch;
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Record.NameOverride = Scratch;
        if (!Stream.GetBytes(&Record.PosXBits, 12)) { return Stream.GetError(); }// 06d92fe7
        if (SinglePlayer()) {                                                     // 06d92ff1 / 06d92ff9
            if (!Stream.GetBytes(&Record.VelXBits, 12)) { return Stream.GetError(); }
            if (!Stream.GetBytes(&Record.OrientWBits, 16)) { return Stream.GetError(); }
        }
        if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
        Record.bMarkedBy = Small8;
        if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
        Record.bTemporary = Small8;
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    // The byte-statistics tail runs on the success path only.
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x29 FLPACKET_SERVER_SETREPUTATION -- Encode LAB_06d91ed0, Decode LAB_06d92350
//
// Registry 0x68, payload 0x48. The ctor seeds payload+0x14..+0x24 with 1, 2, 4, 8, 0x10 --
// five bit flags -- and clears the byte at payload+0x34, the presence mask built from them.
// Both directions read the flag values out of the payload rather than using literals, and
// so does the port.
//
// ENCODE (LAB_06d91ed0) mutates the packet: payload+0x34 is an output.
//     mask = 0
//     if (Affiliation != 0xffffffff) mask = Flag1
//     if (SinglePlayer()) {
//         if (DisplayNameLength != 0)   mask |= Flag4
//         if (*(int*)NameABuffer || *(int*)NameBBuffer)
//                                       mask |= Flag2
//         RelCount = Relations.Size
//         if (RelCount != 0)
//             mask |= (RelCount > 0xff) ? Flag10 : Flag8    (signed compare)
//     }
//     PutByte(mask)
//     PutDword(VibeHandle)
//     if (mask & Flag1) PutDword(Affiliation)
//     if (mask & Flag4) { PutByte((byte)DisplayNameLength)
//                         PutBytes(DisplayName, len * 2) }
//     if (mask & Flag2) { size+bytes of NameA
//                         size+bytes of NameB }
//     if (IsMPServer()) PutFloat(Attitude)
//     else if (RelCount != 0) {
//         (mask & Flag10) ? PutWord(count) : PutByte(count)
//         for each node: PutFloat(rel+0x04); PutDword(rel+0x00)
//     }
//
// DECODE (LAB_06d92350) is the inverse with three asymmetries, all reproduced:
//   1. The MP/relations fork asks a different function: the encoder IsMPServer(), the
//      decoder SinglePlayer() -- two different globals, not one predicate inverted.
//   2. The display-name length is written as a low byte but read back sign-extended, so
//      0x80..0xff decode negative.
//   3. The name buffer is allocated as len * 4 + 4 bytes while only len * 2 are copied,
//      terminated by a word at [buf + len*2].
// ===========================================================================

unsigned int FPacket_FLPACKET_SERVER_SETREPUTATION::Encode(unsigned char** BufferHolder,
                                                           unsigned int Capacity,
                                                           unsigned int* OutLength) {
    // Not const: the mask is written back into payload+0x34.

    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380

    const unsigned char Flag1 = (unsigned char)(Wire.MaskAffiliation & 0xffu);
    const unsigned char Flag2 = (unsigned char)(Wire.MaskNames & 0xffu);
    const unsigned char Flag4 = (unsigned char)(Wire.MaskDisplayName & 0xffu);
    const unsigned char Flag8 = (unsigned char)(Wire.MaskRelationsByte & 0xffu);
    const unsigned char Flag10 = (unsigned char)(Wire.MaskRelationsWord & 0xffu);

    unsigned char Mask = 0;
    if (Wire.Affiliation != 0xffffffffu) {                      // 06d91f14 CMP ...,-1
        Mask = Flag1;
    }
    unsigned int RelCount = 0;
    if (SinglePlayer()) {                                      // 06d91f28 / 06d91f30
        if (Wire.DisplayNameLength != 0) {                     // 06d91f35
            Mask = (unsigned char)(Mask | Flag4);
        }
        // Both buffers dereferenced untested; the marshaller always fills them.
        const unsigned int* NameAFirst = static_cast<const unsigned int*>(Wire.NameABuffer);
        const unsigned int* NameBFirst = static_cast<const unsigned int*>(Wire.NameBBuffer);
        if (*NameAFirst != 0 || *NameBFirst != 0) {
            Mask = (unsigned char)(Mask | Flag2);
        }
        RelCount = Wire.Relations.Size;
        if (RelCount != 0) {                                   // 06d91f68
            // Retail compares signed on an unsigned count. Kept signed.
            if ((int)RelCount > 0xff) {
                Mask = (unsigned char)(Mask | Flag10);
            } else {
                Mask = (unsigned char)(Mask | Flag8);
            }
        }
    }
    Wire.Flags = Mask;

    Stream.PutByte(Mask);
    Stream.PutDword(Wire.VibeHandle);                          // payload+0x38
    if ((Flag1 & Mask) != 0) {                                 // 06d91fef
        Stream.PutDword(Wire.Affiliation);
    }
    if ((Flag4 & Mask) != 0) {                                 // 06d92028
        Stream.PutByte((unsigned char)(Wire.DisplayNameLength & 0xffu));
        Stream.PutBytes(Wire.DisplayName, Wire.DisplayNameLength * 2u);
    }
    if ((Flag2 & Mask) != 0) {                                 // 06d920b4
        RpcPutSizedBlock(Stream, Wire.NameASize, Wire.NameABuffer);
        RpcPutSizedBlock(Stream, Wire.NameBSize, Wire.NameBBuffer);
    }
    if (IsMPServer()) {                                        // 06d921d0 / 06d921d8
        Stream.PutFloat(Wire.Attitude);                        // FLD/FSTP
    } else if (RelCount != 0) {                                // 06d92216
        if ((Flag10 & Mask) != 0) {                            // 06d92227
            Stream.PutWord((unsigned short)(RelCount & 0xffffu));
        } else {
            Stream.PutByte((unsigned char)(RelCount & 0xffu));
        }
        // The dummy is dereferenced untested; a non-zero count means it was built.
        for (const FRpcRelationNode* Node = Wire.Relations.Head->Next;
             Node != Wire.Relations.Head && !Stream.Failed(); Node = Node->Next) {
            // The value is used untested; the pair travels second field first.
            const FRpcRelation* Relation = Node->Value;
            Stream.PutDword(Relation->FeelingBits);
            Stream.PutDword(Relation->GroupId);
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    // The byte-statistics tail runs on the success path only.
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_SETREPUTATION::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    const unsigned char Flag1 = (unsigned char)(Wire.MaskAffiliation & 0xffu);
    const unsigned char Flag2 = (unsigned char)(Wire.MaskNames & 0xffu);
    const unsigned char Flag4 = (unsigned char)(Wire.MaskDisplayName & 0xffu);
    const unsigned char Flag8 = (unsigned char)(Wire.MaskRelationsByte & 0xffu);
    const unsigned char Flag10 = (unsigned char)(Wire.MaskRelationsWord & 0xffu);

    unsigned char Mask = 0;
    if (!Stream.GetByte(Mask)) {                               // 06d923bd
        return Stream.GetError();
    }
    Wire.Flags = Mask;
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire.VibeHandle = Scratch;
    if ((Flag1 & Mask) != 0) {                                 // 06d923ef
        if (!Stream.GetDword(Scratch)) {
            return Stream.GetError();
        }
        Wire.Affiliation = Scratch;
    } else {
        Wire.Affiliation = 0xffffffffu;
    }

    if (SinglePlayer()) {                                      // 06d9242a / 06d92432
        if ((Flag4 & Mask) != 0) {                             // 06d9243b
            unsigned char Raw = 0;
            if (!Stream.GetByte(Raw)) {                        // 06d92466
                return Stream.GetError();
            }
            // Sign-extended into the dword length.
            const int Signed = (int)(signed char)Raw;
            Wire.DisplayNameLength = (unsigned int)Signed;
            // Retail quirk: a negative length is not refused; it reaches operator new and
            // the cursor check wrapped.
            const unsigned int Chars = (unsigned int)Signed;
            // Four bytes per character plus four, for a buffer used two bytes per
            // character. Bare new in retail.
            void* Block = ::operator new(Chars * 4u + 4u, std::nothrow);
            Wire.DisplayName = static_cast<wchar_t*>(Block);
            if (!Stream.GetBytes(Block, Chars * 2u)) {
                return Stream.GetError();
            }
            static_cast<unsigned short*>(Block)[Chars] = 0;
        } else {
            Wire.DisplayNameLength = 0;
        }
        if ((Flag2 & Mask) != 0) {                             // 06d924e9
            // FUN_06da1620 on payload+0x00 and payload+0x08, each a {size, buffer} pair.
            if (!RpcGetSizedBlock(Stream, Wire.NameASize, Wire.NameABuffer)) {
                return Stream.GetError();
            }
            if (!RpcGetSizedBlock(Stream, Wire.NameBSize, Wire.NameBBuffer)) {
                return Stream.GetError();
            }
        }
    }

    // SinglePlayer() again: the fork the encoder makes on IsMPServer() instead.
    if (SinglePlayer()) {
        const unsigned char Present = (unsigned char)(Flag8 | Flag10);      // OR
        if ((Mask & Present) != 0) {                                        // 06d9251e
            unsigned int Count = 0;
            if ((Mask & Flag10) != 0) {                                     // 06d92528
                unsigned short Wide = 0;
                if (!Stream.GetWord(Wide)) {                                // 06d9254f
                    return Stream.GetError();
                }
                Count = Wide;                                               // zero-extended
            } else {
                unsigned char Narrow = 0;
                if (!Stream.GetByte(Narrow)) {                              // 06d92581
                    return Stream.GetError();
                }
                Count = Narrow;                                             // MOVZX
            }
            for (unsigned int Index = 0; Index < Count; ++Index) {
                // Retail quirk: operator new(8) before the two reads; a failed bounds check
                // leaks the element.
                FRpcRelation* Relation = static_cast<FRpcRelation*>(
                    ::operator new(sizeof(FRpcRelation), std::nothrow));
                if (!Stream.GetDword(Scratch)) {
                    return Stream.GetError();
                }
                Relation->FeelingBits = Scratch;
                if (!Stream.GetDword(Scratch)) {
                    return Stream.GetError();
                }
                Relation->GroupId = Scratch;
                // operator new(0xc) node; bumps the list size.
                RpcRelationListLink(&Wire.Relations, Relation);
            }
        }
    } else {
        if (!Stream.GetFloat(Wire.Attitude)) {                              // 06d92689
            return Stream.GetError();                                       // LAB_06d92668
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    // The byte-statistics tail runs on the success path only.
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x54 FLPACKET_SERVER_MISCOBJUPDATE -- Encode LAB_06d98d30, Decode LAB_06d990d0
//
// Registry 0x6c, payload 0x4c: a 16-bit presence mask and nine optional members. The codec
// confirms payload+0x20 is a wide basic_string (count doubled, _Nullstr fallback
// [06db3190], decoder measures with wcslen) and payload+0x48 is the Ids count.
//
// Field order, identical in both directions, with the gating mask bit:
//     ---     word   the mask itself
//     0x0008  dword  payload+0x08
//     0x0004  dword  payload+0x04
//     0x0001  dword  payload+0x14
//     0x0002  dword  payload+0x18 then dword +0x1c
//     0x0010  word   character count, then count*2 bytes
//     0x0020  dword  payload+0x34
//     0x0040  word   payload+0x0c
//     0x0080  dword  payload+0x10
//     0x0100  byte   the Ids count, then that many dwords
//
// Bit 0x0080 is tested as the sign bit of the mask's low byte, and bit 0x0100 in the high
// byte; both are the same bits.
//
// The decoder takes the stored string length from wcslen of its terminated scratch copy,
// not from the wire count, so an embedded NUL truncates the string. Reproduced.
// ===========================================================================


unsigned int FPacket_FLPACKET_SERVER_MISCOBJUPDATE::Encode(unsigned char** BufferHolder,
                                                           unsigned int Capacity,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    const unsigned short Flags = Wire.Flags;
    Stream.PutWord(Flags);
    if ((Flags & kMiscObjUpdateFlagObjectId) != 0) {         // 06d98daf TEST ...,0x8
        Stream.PutDword(Wire.ObjectId);
    }
    if ((Flags & kMiscObjUpdateFlagPlayerId) != 0) {         // 06d98de4
        Stream.PutDword(Wire.PlayerId);
    }
    if ((Flags & kMiscObjUpdateFlagGroupId) != 0) {         // 06d98e19
        Stream.PutDword(Wire.GroupId);
    }
    if ((Flags & kMiscObjUpdateFlagMission) != 0) {               // 06d98e4e
        Stream.PutDword(Wire.MissionId);
        Stream.PutDword(Wire.MissionSetBy);
    }
    if ((Flags & kMiscObjUpdateFlagText) != 0) {               // 06d98eb2
        // Only the low word of the character count travels, and the same word doubled is
        // the byte run.
        const unsigned short Chars = (unsigned short)(Wire.Text.size() & 0xffffu);
        Stream.PutWord(Chars);
        // Retail's buffer pointer, or _Nullstr when never allocated: c_str().
        Stream.PutBytes(Wire.Text.c_str(), (unsigned int)Chars * 2u);
    }
    if ((Flags & kMiscObjUpdateFlagAffiliation) != 0) {         // 06d98f45
        Stream.PutDword(Wire.Affiliation);
    }
    if ((Flags & kMiscObjUpdateFlagRank) != 0) {         // 06d98f7c
        Stream.PutWord(Wire.Rank);
    }
    if ((Flags & kMiscObjUpdateFlagSystemId) != 0) {         // 06d98fb5 TEST AL,AL / JNS
        Stream.PutDword(Wire.SystemId);
    }
    if ((Flags & kMiscObjUpdateFlagIds) != 0) {                // 06d98fed TEST [+0x21],1
        Stream.PutByte(Wire.IdCount);
        // Walks Begin..End and ships every element whether or not the count byte agrees.
        for (std::vector<unsigned int>::const_iterator Id = Wire.Ids.begin();
             Id != Wire.Ids.end() && !Stream.Failed(); ++Id) {
            Stream.PutDword(*Id);
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    // The byte-statistics tail runs on the success path only.
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_MISCOBJUPDATE::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    unsigned short Flags = 0;
    if (!Stream.GetWord(Flags)) {                              // 06d9913f
        return Stream.GetError();
    }
    Wire.Flags = Flags;
    unsigned int Scratch = 0;
    if ((Flags & kMiscObjUpdateFlagObjectId) != 0) {         // 06d99149 TEST AL,0x8
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.ObjectId = Scratch;
    }
    if ((Flags & kMiscObjUpdateFlagPlayerId) != 0) {         // 06d9917b
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.PlayerId = Scratch;
    }
    if ((Flags & kMiscObjUpdateFlagGroupId) != 0) {         // 06d991ab
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.GroupId = Scratch;
    }
    if ((Flags & kMiscObjUpdateFlagMission) != 0) {               // 06d991db
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.MissionId = Scratch;
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.MissionSetBy = Scratch;
    }
    if ((Flags & kMiscObjUpdateFlagText) != 0) {               // 06d99235
        unsigned short WireChars = 0;
        if (!Stream.GetWord(WireChars)) {                      // 06d9925f
            return Stream.GetError();
        }
        // Bare MSVCRT malloc of count*2 + 2. Retail quirk: a failed bounds check leaks the
        // scratch buffer; only the success path frees it.
        const unsigned int Bytes = (unsigned int)WireChars * 2u;
        wchar_t* Temp = static_cast<wchar_t*>(malloc(Bytes + 2u));
        if (!Stream.GetBytes(Temp, Bytes)) {                   // 06d992b3
            return Stream.GetError();
        }
        Temp[WireChars] = 0;
        // The stored length comes from the terminator (wcslen), not the wire count.
        unsigned int Chars = 0;
        while (Temp[Chars] != 0) {
            ++Chars;
        }
        // Inlined basic_string::assign(Temp, Chars).
        Wire.Text.assign(Temp, (size_t)Chars);
        free(Temp);
    }
    if ((Flags & kMiscObjUpdateFlagAffiliation) != 0) {         // 06d9932e
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.Affiliation = Scratch;
    }
    if ((Flags & kMiscObjUpdateFlagRank) != 0) {         // 06d99362
        unsigned short Word = 0;
        if (!Stream.GetWord(Word)) { return Stream.GetError(); }
        Wire.Rank = Word;
    }
    if ((Flags & kMiscObjUpdateFlagSystemId) != 0) {         // 06d99398 TEST AL,AL / JNS
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.SystemId = Scratch;
    }
    if ((Flags & kMiscObjUpdateFlagIds) != 0) {                // 06d993cd TEST [+0x21],1
        unsigned char Count = 0;
        if (!Stream.GetByte(Count)) {                          // 06d993f8
            return Stream.GetError();
        }
        Wire.IdCount = Count;
        for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
            if (!Stream.GetDword(Scratch)) {                   // 06d9943a
                return Stream.GetError();
            }
            // Inlined vector::insert(End, 1, value).
            Wire.Ids.push_back(Scratch);
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    // The byte-statistics tail runs on the success path only.
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x4f FLPACKET_SERVER_SENDCOMM -- Encode LAB_06dad000, Decode LAB_06dad320
//
// Constructor FUN_06d9eab0, routing header 0x2004f, vtable 06db52dc = { dtor FUN_06d9eaf0,
// Encode LAB_06dad000, Decode LAB_06dad320, dump }. The constructor defaults the
// discriminator at payload+0x54 to 1 and zeroes the id count (+0x3c) and the buffer pointer
// (+0x44), which makes the decoder's free-then-allocate safe on a fresh packet.
//
// Registry 0x78, payload 0x58. The codec agrees field for field with the layout read from
// the sender and receiver (FRpcIdArrayBlock):
//
//     Body.Head        payload+0x0c   dword on the wire
//     Body.Body        payload+0x10   dword on the wire
//     Body.LeftHand    payload+0x14   never travels
//     Body.RightHand   payload+0x18   never travels
//     Body.Ids[0..7]   payload+0x1c   dwords, Count of them
//     Body.Count       payload+0x3c   low byte on the wire
//
// payload+0x54 is unused by the delivery path but on the wire it gates the whole id block:
// when it is zero the encoder skips payload+0x0c..+0x3c and the decoder clears them.
//
// Wire order:
//     payload+0x00  dword
//     payload+0x04  dword
//     payload+0x08  dword
//     payload+0x54  byte, the discriminator
//   if (payload+0x54 != 0): the id block, as above
//     payload+0x40  dword
//     payload+0x48  byte, BufferCount
//     payload+0x44  BufferCount dwords through a pointer
//     payload+0x4c  dword, only its low word travels
//     payload+0x50  float, quantised to one byte
//
// Easy to get backwards:
//  1. Body.Count is a dword tested signed, but only its low byte travels and it comes back
//     zero-extended. BufferCount is a byte read back sign-extended everywhere. Two
//     extensions in one packet.
//  2. The decoder owns the buffer: it frees any pointer already at payload+0x44, then
//     allocates BufferCount * 4. ~FPacket_FLPACKET_SERVER_SENDCOMM is the matching teardown.
//  3. payload+0x50 is quantised in two stages (x 0.1f, x 255.0, ftol) and decoded with the
//     single constant 1/25.5 after zero-extension: it covers [0,10] unsigned -- the
//     opposite of the orientation bytes in rpc_wire_ch1.cpp.
//
// Single player never calls either slot; the packet is queued by pointer.
// ===========================================================================

// payload+0x50 is declared as raw bits (the delivery path moves it as a stack dword); the
// codec needs a float, so the bytes are copied rather than type-punned.
static void RpcSendCommMoveFloatBits(void* Dest, const void* Source) {
    unsigned char* Out = static_cast<unsigned char*>(Dest);
    const unsigned char* In = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < 4u; ++i) {
        Out[i] = In[i];
    }
}

// Retail quirk: the inline array is eight entries wide but both Ids loops are bounded by
// the count alone, re-read from payload+0x3c every pass and compared signed. A count above
// eight walks off the end of Ids (Ids[8] is the count); the port does the same.

unsigned int FPacket_FLPACKET_SERVER_SENDCOMM::Encode(unsigned char** BufferHolder,
                                                      unsigned int Capacity,
                                                      unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380

    Stream.PutDword(Wire.SenderId);
    Stream.PutDword(Wire.ReceiverId);
    Stream.PutDword(Wire.VoiceId);
    Stream.PutByte(Wire.bReceiverIsOwnShip);
    if (Wire.bReceiverIsOwnShip != 0) {                                // 06dad0ea / 06dad0ef JZ
        Stream.PutDword(Wire.Body.Head);
        Stream.PutDword(Wire.Body.Body);
        // The low byte of the dword count travels; the loop bound is the whole dword,
        // signed.
        Stream.PutByte((unsigned char)((unsigned int)Wire.Body.Count & 0xffu));
        const unsigned int* const IdSlots = Wire.Body.Ids;
        for (int Index = 0; Index < Wire.Body.Count && !Stream.Failed(); ++Index) {
            Stream.PutDword(IdSlots[Index]);
        }
    }
    Stream.PutDword(Wire.CaptionIds);
    Stream.PutByte(Wire.BufferCount);
    // The loop bound is the byte, signed.
    const int SignedBufferCount = (int)(signed char)Wire.BufferCount;
    if (SignedBufferCount > 0) {
        // payload+0x44 dereferenced untested.
        for (int Index = 0; Index < SignedBufferCount && !Stream.Failed(); ++Index) {
            Stream.PutDword(Wire.Buffer[Index]);
        }
    }
    // The dword's low half only.
    Stream.PutWord((unsigned short)(Wire.PermutationIds & 0xffffu));
    float Scaled = 0.0f;
    RpcSendCommMoveFloatBits(&Scaled, &Wire.DurationBits);
    Stream.PutQuantizedTwoStage(Scaled, FRpcMarshalStream::kTenthScale,
                                FRpcMarshalStream::kPercentScale);

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_SENDCOMM::Decode(const unsigned char* Buffer,
                                                      unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }

    unsigned int Scratch = 0;
    unsigned short Small16 = 0;
    unsigned char Small8 = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.SenderId = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.ReceiverId = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.VoiceId = Scratch;
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire.bReceiverIsOwnShip = Small8;
    if (Small8 != 0) {                                         // 06dad40d JZ
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.Body.Head = Scratch;
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire.Body.Body = Scratch;
        // Not on the wire; the decoder clears them.
        Wire.Body.LeftHand = 0;
        Wire.Body.RightHand = 0;
        if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
        // Zero-extended into the dword.
        Wire.Body.Count = (int)(unsigned int)Small8;
        unsigned int* const IdSlots = Wire.Body.Ids;
        for (int Index = 0; Index < Wire.Body.Count; ++Index) {
            if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
            IdSlots[Index] = Scratch;
        }
    } else {
        // LAB_06dad4dd clears the whole id block except its Ids array.
        Wire.Body.Head = 0;
        Wire.Body.Body = 0;
        Wire.Body.LeftHand = 0;
        Wire.Body.RightHand = 0;
        Wire.Body.Count = 0;
    }
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.CaptionIds = Scratch;
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire.BufferCount = Small8;

    // The old pointer is freed first, so a packet decoded twice does not leak.
    if (Wire.Buffer != nullptr) {
        ::operator delete(Wire.Buffer);
        Wire.Buffer = nullptr;
    }
    const int SignedBufferCount = (int)(signed char)Wire.BufferCount;
    // Retail quirk: bare operator new(count << 2) with no sign test, so 0x80..0xff reach it
    // as a negative count shifted left; zero allocates zero bytes. Only the loop is gated.
    Wire.Buffer = static_cast<unsigned int*>(
        ::operator new((unsigned int)SignedBufferCount << 2, std::nothrow));
    if (SignedBufferCount > 0) {
        for (int Index = 0; Index < SignedBufferCount; ++Index) {
            if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
            Wire.Buffer[Index] = Scratch;
        }
    }
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    // Zero-extended into the dword.
    Wire.PermutationIds = (unsigned int)Small16;
    float Scaled = 0.0f;
    if (!Stream.GetQuantizedUnsigned(Scaled, FRpcMarshalStream::kTenthInvScale)) {
        return Stream.GetError();
    }
    RpcSendCommMoveFloatBits(&Wire.DurationBits, &Scaled);

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// The orientation fields below are declared as raw dwords (the payload marshallers copy
// argument dwords into them) while the codec does FLD / FMUL / ftol on them; these two
// convert. rpc_containers.cpp has its own static pair.
// ===========================================================================
static float RpcCreateBitsToFloat(unsigned int Bits) {
    float Value = 0.0f;
    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static unsigned int RpcCreateFloatToBits(float Value) {
    unsigned int Bits = 0;
    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

// ===========================================================================
// ch2 0x37 FLPACKET_SERVER_CREATEGUIDED -- Encode LAB_06da84e0, Decode FUN_06da8810
//
// Constructor FUN_06d9c9a0, routing header 0x20037, vtable 06db500c = { dtor FUN_06d9af50,
// Encode LAB_06da84e0, Decode FUN_06da8810, the shared dump body }. Registry 0x58, payload
// 0x38.
//
// The constructor seeds 1.0f at payload+0x28 and zeroes the next three dwords: W first in
// memory, but the codec sends X, Y, Z and W last.
//
// payload+0x00 never crosses the wire: the decoder writes a literal zero there first.
//
// Encode field order:
//     PutWord(LargeIDToSmallID(payload+0x04))
//     PutDword(payload+0x08)
//     PutWord (payload+0x0c)
//     PutDword(payload+0x10)
//     PutWord (payload+0x14)
//     PutDword(payload+0x18)
//     PutBytes(payload+0x1c, 12)                FUN_06d8f510
//     four quantized bytes, scale 127.0 out and 1/127 back, sign-extended on decode
//
// The encoder computes its four ftol results into stack slots (two of them its own spent
// argument slots) long before writing them; reading those stores as consecutive locals
// would permute the quadruple.
// ===========================================================================

unsigned int FPacket_FLPACKET_SERVER_CREATEGUIDED::Encode(unsigned char** BufferHolder,
                                                          unsigned int Capacity,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    Stream.PutWord(Archetype::LargeIDToSmallID(Wire.ArchetypeNickname));     // / 06da8564
    Stream.PutDword(Wire.OwnerId);
    Stream.PutWord(Wire.SubObjId);
    Stream.PutDword(Wire.TargetId);
    Stream.PutWord(Wire.TargetSubObjId);
    Stream.PutDword(Wire.ObjId);
    Stream.PutBytes(&Wire.PosXBits, 12);
    Stream.PutQuantized(RpcCreateBitsToFloat(Wire.OrientXBits),
                        FRpcMarshalStream::kOrientationScale);        // > 06da8709
    Stream.PutQuantized(RpcCreateBitsToFloat(Wire.OrientYBits),
                        FRpcMarshalStream::kOrientationScale);        // > 06da8738
    Stream.PutQuantized(RpcCreateBitsToFloat(Wire.OrientZBits),
                        FRpcMarshalStream::kOrientationScale);        // > 06da8767
    Stream.PutQuantized(RpcCreateBitsToFloat(Wire.OrientWBits),
                        FRpcMarshalStream::kOrientationScale);        // > 06da8792

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_CREATEGUIDED::Decode(const unsigned char* Buffer,
                                                          unsigned int Size,
                                                          unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    Wire.OwnerPlayer = 0;
    unsigned short Small16 = 0;
    unsigned int Scratch = 0;
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    // Zero-extended before the call.
    Wire.ArchetypeNickname = Archetype::SmallIDToLargeID(Small16);
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.OwnerId = Scratch;
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire.SubObjId = Small16;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.TargetId = Scratch;
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire.TargetSubObjId = Small16;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.ObjId = Scratch;
    if (!Stream.GetBytes(&Wire.PosXBits, 12)) {                     // 06da8983 FUN_06d8f510
        return Stream.GetError();
    }

    // In the order the encoder wrote them: X, Y, Z, then W at +0x28.
    float Quantized = 0.0f;
    if (!Stream.GetQuantizedSigned(Quantized, FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    Wire.OrientXBits = RpcCreateFloatToBits(Quantized);
    if (!Stream.GetQuantizedSigned(Quantized, FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    Wire.OrientYBits = RpcCreateFloatToBits(Quantized);
    if (!Stream.GetQuantizedSigned(Quantized, FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    Wire.OrientZBits = RpcCreateFloatToBits(Quantized);
    if (!Stream.GetQuantizedSigned(Quantized, FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    Wire.OrientWBits = RpcCreateFloatToBits(Quantized);

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x2c FLPACKET_SERVER_CREATEMINE   -- Encode LAB_06da95d0, Decode LAB_06da98d0
// ch2 0x2d FLPACKET_SERVER_CREATECOUNTER -- Encode LAB_06da9b50, Decode LAB_06da9e40
//
// Same registry size (0x68, payload 0x48) but different codecs:
//   * CREATEMINE translates its archetype (LargeIDToSmallID / SmallIDToLargeID);
//     CREATECOUNTER calls no COMMON.DLL import at all.
//   * CREATEMINE puts most of its payload behind `if (SinglePlayer())`; CREATECOUNTER has
//     no conditional.
//   * The constructors seed the unit quaternion one field apart: CREATEMINE's word at
//     payload+0x0c is zero, CREATECOUNTER carries an extra dword at +0x0c and its word at
//     +0x10 is seeded from COMMON.DLL SUBOBJ_ID_NONE.
//
// CREATEMINE: constructor FUN_06d9ce00, vtable 06db506c, header 0x2002c. CREATECOUNTER:
// constructor FUN_06d9cea0, vtable 06db507c, header 0x2002d.
//
// CREATEMINE's payload+0x44 does not cross the wire; the decoder zeroes it on both paths.
//
// Neither codec quantizes anything: position and orientation move through FUN_06d8f510
// (three dwords) and FUN_06d8f4f0 (four dwords) as raw bytes.
// ===========================================================================

unsigned int FPacket_FLPACKET_SERVER_CREATEMINE::Encode(unsigned char** BufferHolder,
                                                        unsigned int Capacity,
                                                        unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    Stream.PutDword(Wire.ObjId);
    Stream.PutWord(Archetype::LargeIDToSmallID(Wire.ArchetypeNickname));     // / 06da9689
    Stream.PutDword(Wire.OwnerId);
    Stream.PutWord(Wire.SubObjId);
    // payload+0x20..+0x2b -- not payload+0x10, which travels later and only in SP.
    Stream.PutBytes(&Wire.PosXBits, 12);
    if (SinglePlayer()) {                                             // 06da9745 / 06da974d
        Stream.PutBytes(&Wire.OrientWBits, 16);                        // FUN_06d8f4f0
        Stream.PutBytes(&Wire.VelXBits, 12);                        // FUN_06d8f510
        Stream.PutBytes(&Wire.AngVelXBits, 12);                        // FUN_06d8f510
    }

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_CREATEMINE::Decode(const unsigned char* Buffer,
                                                        unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // 06da98fc FUN_06da13c0 -- consumes nothing.

    unsigned int Scratch = 0;
    unsigned short Small16 = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.ObjId = Scratch;
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire.ArchetypeNickname = Archetype::SmallIDToLargeID(Small16);           // / 06da9974
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.OwnerId = Scratch;
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire.SubObjId = Small16;
    if (!Stream.GetBytes(&Wire.PosXBits, 12)) {                     // 06da99f4
        return Stream.GetError();
    }
    if (SinglePlayer()) {                                             // 06da99fe / 06da9a06
        if (!Stream.GetBytes(&Wire.OrientWBits, 16)) {                 // 06da9a34
            return Stream.GetError();
        }
        if (!Stream.GetBytes(&Wire.VelXBits, 12)) {                 // 06da9a66
            return Stream.GetError();
        }
        if (!Stream.GetBytes(&Wire.AngVelXBits, 12)) {                 // 06da9a98
            return Stream.GetError();
        }
    }
    Wire.OwnerPlayer = 0;

    // 06da9ab2 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06da9b14
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_CREATECOUNTER::Encode(unsigned char** BufferHolder,
                                                           unsigned int Capacity,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    Stream.PutDword(Wire.ArchetypeNickname);
    Stream.PutDword(Wire.ObjId);
    Stream.PutDword(Wire.OwnerPlayer);
    Stream.PutDword(Wire.OwnerId);
    Stream.PutWord(Wire.SubObjId);
    Stream.PutBytes(&Wire.OrientWBits, 16);                            // FUN_06d8f4f0
    Stream.PutBytes(&Wire.PosXBits, 12);                            // FUN_06d8f510
    Stream.PutBytes(&Wire.VelXBits, 12);                            // FUN_06d8f510
    Stream.PutBytes(&Wire.AngVelXBits, 12);                            // FUN_06d8f510

    // 06da9dce FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06da9e28
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_CREATECOUNTER::Decode(const unsigned char* Buffer,
                                                           unsigned int Size,
                                                           unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // 06da9e6c FUN_06da13c0 -- consumes nothing.

    unsigned int Scratch = 0;
    unsigned short Small16 = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.ArchetypeNickname = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.ObjId = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.OwnerPlayer = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.OwnerId = Scratch;
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire.SubObjId = Small16;
    if (!Stream.GetBytes(&Wire.OrientWBits, 16)) {                     // 06da9f81
        return Stream.GetError();
    }
    if (!Stream.GetBytes(&Wire.PosXBits, 12)) {                     // 06da9fb3
        return Stream.GetError();
    }
    if (!Stream.GetBytes(&Wire.VelXBits, 12)) {                     // 06da9fe5
        return Stream.GetError();
    }
    if (!Stream.GetBytes(&Wire.AngVelXBits, 12)) {                     // 06daa013
        return Stream.GetError();
    }

    // 06daa026 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());         // onwards
    return 0;
}

// ===========================================================================
// ch2 0x2e FLPACKET_SERVER_SETADDITEM -- Encode LAB_06d96f90, Decode LAB_06d97210
//
// Constructor FUN_06d9c440, vtable 06db3ee4, header 0x2002e. Registry 0x40, payload 0x20
// (the ctor's byte store to payload+0x19 confirms bMission's width).
//
// Wire order, neither struct nor argument order; the decoder is the strict inverse:
//     PutDword(ArchId)          payload+0x00
//     PutDword(Id)              payload+0x10
//     PutDword(Count)           payload+0x04
//     PutFloat(Health)          payload+0x14   raw bytes, not quantized
//     PutByte (bEquipped)       payload+0x18
//     PutByte (bMission)        payload+0x19
//     PutDword(AddResult)       payload+0x0c
//     PutDword(HardpointSize)   payload+0x08   strlen + 1
//     the hardpoint bytes, only when HardpointSize != 0
//
// Retail quirks reproduced:
//   * the decoder allocates before the bounds check, so a truncated packet leaves the
//     block for the packet's destructor (as FUN_06da1620);
//   * it does not free a previous Hardpoint first (SENDCOMM's decoder does), so a packet
//     decoded twice leaks the first buffer (docs/re/deviations.md).
// ===========================================================================

unsigned int FPacket_FLPACKET_SERVER_SETADDITEM::Encode(unsigned char** BufferHolder,
                                                        unsigned int Capacity,
                                                        unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    Stream.PutDword(Wire.ArchId);
    Stream.PutDword(Wire.Id);
    Stream.PutDword((unsigned int)Wire.Count);
    Stream.PutFloat(Wire.Health);
    Stream.PutByte(Wire.bEquipped);
    Stream.PutByte(Wire.bMission);
    Stream.PutDword(Wire.AddResult);
    Stream.PutDword(Wire.HardpointSize);
    if (Wire.HardpointSize != 0) {                                    // 06d9714f / 06d97154
        Stream.PutBytes(Wire.Hardpoint, Wire.HardpointSize);
    }

    // 06d971ad FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_SETADDITEM::Decode(const unsigned char* Buffer,
                                                        unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // 06d9723a FUN_06da13c0 -- consumes nothing.

    unsigned int Scratch = 0;
    unsigned char Small8 = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.ArchId = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.Id = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.Count = (int)Scratch;
    if (!Stream.GetFloat(Wire.Health)) { return Stream.GetError(); }  // 06d972f6
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire.bEquipped = Small8;
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire.bMission = Small8;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.AddResult = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.HardpointSize = Scratch;
    if (Wire.HardpointSize != 0) {                                    // 06d97390 / 06d97395
        // Allocated before the bounds check, exactly as retail does; the pointer is
        // stored whether or not the copy then fits.
        char* Block = static_cast<char*>(
            ::operator new(Wire.HardpointSize, std::nothrow));        // bare
        Wire.Hardpoint = Block;
        if (!Stream.GetBytes(Wire.Hardpoint, Wire.HardpointSize)) {   // 06d973c7..06d973e1
            return Stream.GetError();
        }
    }

    // 06d973f8 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06d9745a
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());         // onwards
    return 0;
}

// ===========================================================================
// ch2 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES -- Encode LAB_06d8fc90,
//                                                  Decode FUN_06d8ffd0
//
// Constructor FUN_06d9da40, vtable 06db3c90, header 0x20035. Registry 0x48, payload 0x28.
// The constructor builds the objective list's dummy itself (0xc-byte nodes, header at
// payload+0x1c = {allocator byte, Head, Size}); the allocator byte is copied from an
// uninitialised temporary and is not a field.
//
// Encode order, not struct order:
//     PutDword(payload+0x00)
//     PutDword(payload+0x18)  the count, ahead of everything it counts
//     PutDword(payload+0x04)
//     {dword size, bytes} payload+0x08/+0x0c
//     {dword size, bytes} payload+0x10/+0x14
//     per node: PutByte(record+0x00)
//               {dword size, bytes} record+0x04/+0x08
//
// The list walk is guarded twice (count == 0, list empty); both guards are reproduced.
//
// Decode (FUN_06d8ffd0): RpcGetSizedBlock (FUN_06da1620) three times; each record is
// operator new(0xc) with +0x04/+0x08 zeroed and byte 0 from the wire, pushed before the
// sentinel. Only the list's own Size is bumped; the payload Count comes off the wire.
// ===========================================================================

unsigned int FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES::Encode(unsigned char** BufferHolder,
                                                                  unsigned int Capacity,
                                                                  unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    Stream.PutDword(Wire.MissionId);
    Stream.PutDword(Wire.Count);
    Stream.PutDword(Wire.MissionType);
    RpcPutSizedBlock(Stream, Wire.TextASize, Wire.TextABuffer);       // / 06d8fdcc
    RpcPutSizedBlock(Stream, Wire.TextBSize, Wire.TextBBuffer);       // / 06d8fe62

    // The sentinel is dereferenced untested: count and list size move together
    // (FUN_06d8fc10), so a non-zero count means the list was built.
    if (Wire.Count != 0) {
        const FRpcMissionObjectiveNode* Sentinel = Wire.Objectives.Head;
        for (const FRpcMissionObjectiveNode* Node = Sentinel->Next;
             Node != Sentinel && !Stream.Failed(); Node = Node->Next) {
            // The value is used untested.
            const FRpcMissionObjectiveRecord* Record = Node->Value;
            Stream.PutByte(Record->State);
            RpcPutSizedBlock(Stream, Record->TextSize, Record->TextBuffer);
        }
    }

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_SETMISSIONOBJECTIVES::Decode(const unsigned char* Buffer,
                                                                  unsigned int Size,
                                                                  unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // 06d8fffb FUN_06da13c0 -- consumes nothing.

    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.MissionId = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.Count = Scratch;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire.MissionType = Scratch;
    if (!RpcGetSizedBlock(Stream, Wire.TextASize, Wire.TextABuffer)) {
        return Stream.GetError();                                     // FUN_06da1620
    }
    if (!RpcGetSizedBlock(Stream, Wire.TextBSize, Wire.TextBBuffer)) {
        return Stream.GetError();                                     // FUN_06da1620
    }

    for (unsigned int Index = 0; Index < Wire.Count; ++Index) {       // 06d900a9 / 06d9015f
        FRpcMissionObjectiveRecord* Record =
            static_cast<FRpcMissionObjectiveRecord*>(
                ::operator new(sizeof(FRpcMissionObjectiveRecord), std::nothrow));
                                                                      // 06d900c0 PUSH 0xc
        // The null test skips only the zeroing; the reads below store through the result
        // either way. Bytes 0..3 are uninitialised heap in retail; zeroed here.
        if (Record != nullptr) {
            Record->State = 0;
            Record->Pad_0x01[0] = 0;
            Record->Pad_0x01[1] = 0;
            Record->Pad_0x01[2] = 0;
            Record->TextSize = 0;
            Record->TextBuffer = nullptr;
        }

        // Retail quirk: both error exits abandon the unlinked record (and any text block
        // already allocated) without freeing it; reproduced.
        unsigned char Small8 = 0;
        if (!Stream.GetByte(Small8)) {                                // 06d900e0 / 06d90101
            return Stream.GetError();
        }
        Record->State = Small8;
        if (!RpcGetSizedBlock(Stream, Record->TextSize, Record->TextBuffer)) {
            return Stream.GetError();
        }
        // Insert before the sentinel, then bump the list's Size.
        Wire.Objectives.PushBack(Record);
    }

    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x41 FLPACKET_SERVER_BURNFUSE -- LAB_06dac3c0 / LAB_06dac5c0
//
// Constructor FUN_06d9e5a0, header 0x20041, vtable 06db528c = { dtor FUN_06d9af50 (the
// base's -- BURNFUSE owns nothing), Encode LAB_06dac3c0, Decode LAB_06dac5c0,
// IServerImpl::GetServerStats }.
//
// Wire layout, one field per payload slot in payload order:
//     dword  payload+0x00  SourceId
//     dword  payload+0x04  ObjectId
//     dword  payload+0x08  FuseId
//     word   payload+0x0c  SubObjId (ctor seeds COMMON.DLL SUBOBJ_ID_NONE)
//     float  payload+0x10  AtT
//     float  payload+0x14  Lifetime (ctor seeds -1.0f)
//     byte   payload+0x18  bLit
// 0x1c == 0x3c - 0x20, the registry size.
//
// The two floats go out through FLD/FSTP and come back as plain dwords, no FMUL: four raw
// bytes both ways, so PutDword is faithful.
//
// In single player packets are queued by pointer and neither slot is called.
// ===========================================================================

// 0x06dac3c0 / 0x06dac5c0 -- ch2 0x41 BURNFUSE, seven flat fields. In rpc_wire_ch2.cpp.
unsigned int FPacket_FLPACKET_SERVER_BURNFUSE::Encode(unsigned char** BufferHolder,
                                                      unsigned int Capacity,
                                                      unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(PacketId));   // FUN_06da1380
    if (!Stream.PutDword(Wire.SourceId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire.ObjectId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire.FuseId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutWord(Wire.SubObjId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire.AtTBits)) {                       // FLD/FSTP in retail
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire.LifetimeBits)) {                       // FLD/FSTP in retail
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutByte(Wire.bLit)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(PacketId, Stream.Length());
    return 0;
}

unsigned int FPacket_FLPACKET_SERVER_BURNFUSE::Decode(const unsigned char* Buffer,
                                                      unsigned int Size, unsigned int* OutLength) {
    FRpcMarshalStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);

    if (!Stream.GetDword(Wire.SourceId)) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire.ObjectId)) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire.FuseId)) {
        return Stream.GetError();
    }
    if (!Stream.GetWord(Wire.SubObjId)) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire.AtTBits)) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire.LifetimeBits)) {
        return Stream.GetError();
    }
    if (!Stream.GetByte(Wire.bLit)) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(PacketId, Stream.Length());
    return 0;
}
