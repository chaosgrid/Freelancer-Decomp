// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_payloads.h -- per-packet-type payload layouts (the retail wire format).
//
// Each layout is read from both ends of the packet type: the sender (the IClient slot body,
// allocating through FUN_06d96300(Serializer, Channel, Opcode, &OutPacket) and writing from
// packet+0x20) and the receiver (the matching case of IServerImpl::HandlePacket, 0x06d81170,
// which rebuilds the argument struct and calls one slot of the real IClient,
// .data:0x06dbbcd8, or chat client, .data:0x06dbbcd4). The registry's operator new() size
// (rpc_packet_registry.cpp) minus the 0x20-byte CDAPacket base is the payload size; the
// static_asserts below encode it.
//
// rpclocal decorates none of the 126 IClient methods; rpc_iclient.h names and types them
// from the sender, receiver and caller bodies. Marshal functions read the sender's arguments
// as the retail stack image -- one dword per argument slot, a reference or pointer as its
// address -- which FLocalClientProxy (rpc_client_proxy.cpp) builds from the typed parameters.
//
// Retail never forwards its first argument (a session/client id): every receive case pushes
// the literal 1 in its place.
#pragma once

#include "rpc_transport.h"
#include "common_server_types.h"
// FmtStr::flatten / unflatten / ~FmtStr / Val::get_flattened_size are COMMON.DLL imports in
// retail (.rdata:06db3008, 06db3094, 06db30dc, 06db30e0).
#include "common_text.h"
#include "rpc_iclient_args.h"   // FServerSetReputationInfo, Reputation::Relation
#include "common_misc.h"        // DamageList, the DAMAGEOBJECT payload member

#include <cstddef>
#include <new>
#include <string>
#include <vector>

class IClient;
class IChatClient;

// ===========================================================================
// Codec table entry
// ===========================================================================

// Packs the sender's raw argument dwords into the retail payload. Returns false when the
// arguments cannot be marshalled (e.g. an unknown sender variant).
typedef bool (*FRpcMarshalFn)(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount);

// Rebuilds the retail argument list out of the payload and makes the one call retail's
// HandlePacket case makes. Returns false when the packet cannot be delivered.
typedef bool (*FRpcDeliverFn)(const FPacketBase* Packet, IClient* Client, IChatClient* ChatClient);

struct FRpcPayloadCodec {
    unsigned short Channel;
    unsigned short Opcode;
    const char* Name;              // the retail FLPACKET_* string
    unsigned short PayloadSize;    // registry size minus the 0x20 base
    FRpcMarshalFn Marshal;
    FRpcDeliverFn Deliver;
};

// nullptr for a channel/opcode with no codec.
extern "C" __declspec(dllexport) const FRpcPayloadCodec* RpcFindPayloadCodec(unsigned int Channel, unsigned int Opcode);

// ===========================================================================
// Typed payload access. Unchecked, as retail's own field stores are: the packet is the
// concrete class the registry built, whose size is retail's operator new argument.
// ===========================================================================
template<typename T>
inline bool RpcPayloadWrite(FPacketBase* Packet, unsigned int Offset, const T& Value) {
    unsigned char* Base = Packet->Payload() + Offset;
    const unsigned char* Src = reinterpret_cast<const unsigned char*>(&Value);
    for (unsigned int i = 0; i < (unsigned int)sizeof(T); ++i) {
        Base[i] = Src[i];
    }
    return true;
}

template<typename T>
inline T RpcPayloadRead(const FPacketBase* Packet, unsigned int Offset) {
    T Value;
    unsigned char* Dst = reinterpret_cast<unsigned char*>(&Value);
    const unsigned char* Base = Packet->Payload() + Offset;
    for (unsigned int i = 0; i < (unsigned int)sizeof(T); ++i) {
        Dst[i] = Base[i];
    }
    return Value;
}

// Bare operator new(N) -- a size of 0 included -- and a byte copy; the packet owns the
// block. The block writers of channels 2 and 3 (FUN_06d97b00, FUN_06d90a00, FUN_06d97470,
// FUN_06d989b0, FUN_06d9a160, FUN_06d9a5f0, FUN_06d9aab0) and the IServerImpl senders do
// this; the writers that test `size == 0 || src == 0` first have that test in their callers.
inline void* RpcCopyBlock(const void* Source, unsigned int InSize) {
    unsigned char* Block = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < InSize; ++i) {
        Block[i] = Src[i];
    }
    return Block;
}

// Retail's inline strlen; no null test (retail faults on null).
inline unsigned int RpcStringLength(const char* Text) {
    unsigned int Length = 0;
    while (Text[Length] != '\0') {
        ++Length;
    }
    return Length;
}

// ===========================================================================
// Channel-1 payload structures.
//
// The bytes on the wire, not the argument structs; for several types the two differ
// (UPDATEOBJECT reorders its fields, SETTARGET packs its 16-bit ids behind the 32-bit ids).
// ===========================================================================

// ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT, packet size 0x4c -> payload 0x2c.
// Sender 0x06d897c0 (IClient+0x0d8), receiver caseD_1 at 0x06d83a4a.
struct FPayloadCommonUpdateObject {
    // <- argument struct +0x28 (byte). freelancer.exe stores it as the ship's
    // BehaviorIndex; the IServer direction sends SSPObjUpdateInfo::State.
    unsigned char State;
    unsigned char Pad_0x01[3];
    unsigned int ObjectId;       // +0x04  <- argument struct +0x00
    Vector Position;             // +0x08  <- FUN_06d8f510 (3-dword copier), struct +0x14
    Quaternion Orientation;      // +0x14  <- FUN_06d8f4f0 (4-dword copier), struct +0x04
    // Both floats (the receiver uses FLD/FST). freelancer.exe's handler passes the first to
    // PhySys::RCSet as the update time and the second to CShip::set_throttle; the IServer
    // direction narrows SSPObjUpdateInfo's double Timestamp into the first and copies its
    // ThrottleBits into the second.
    float Timestamp;             // +0x24  <- argument struct +0x20
    float Throttle;              // +0x28  <- argument struct +0x24
};
static_assert(sizeof(FPayloadCommonUpdateObject) == 0x2c, "UPDATEOBJECT payload 0x2c");

// The argument struct (0x2c bytes, same fields in a different order) is
// FCommonUpdateObjectInfo in rpc_iclient_args.h, built by Server.dll's
// StarSystem::sp_obj_update (FUN_06d0bbc0).

// ch1 op 0x02 FLPACKET_COMMON_FIREWEAPON, packet size 0x40 -> payload 0x20.
// Sender 0x06d8b310 (IClient+0x000), receiver case 2 of the channel-1 switch.
// Retail's XFireWeaponInfo layout, with the VC6 vector triple at +0x10. The port's
// XFireWeaponInfo holds a std::vector, so the wire keeps the retail triple; the elements are
// copied into a block the packet owns (freed by the dtor FUN_06d9b020) and copied back out
// on receipt.
struct FPayloadCommonFireWeapon {
    unsigned int ObjectId;                    // +0x00
    Vector TargetPos;                         // +0x04
    FRetailVector<unsigned short> Hardpoints; // +0x10  element stride 2
};
static_assert(sizeof(FPayloadCommonFireWeapon) == 0x20, "FIREWEAPON payload 0x20");

// ch1 op 0x04 FLPACKET_COMMON_SETTARGET, packet size 0x2c -> payload 0x0c.
// Sender 0x06d8bd50 (IClient+0x010), receiver caseD_4 at 0x06d838c9.
// Reordered: both 32-bit ids first, the 16-bit sub-object ids behind; XSetTarget
// interleaves them.
struct FPayloadCommonSetTarget {
    unsigned int ObjectId;          // +0x00  <- XSetTarget +0x00
    unsigned int TargetObjectId;    // +0x04  <- XSetTarget +0x08
    unsigned short SubObjId;        // +0x08  <- XSetTarget +0x04
    unsigned short TargetSubObjId;  // +0x0a  <- XSetTarget +0x0c
};
static_assert(sizeof(FPayloadCommonSetTarget) == 0x0c, "SETTARGET payload 0x0c");

// ch1 op 0x05 FLPACKET_COMMON_CHATMSG, packet size 0x30 -> payload 0x10.
// Sender 0x06d8c470 (IChatClient+0x000), receiver caseD_5 at 0x06d83900.
// The packet owns a copy of the message body.
struct FPayloadCommonChatMsg {
    // TODO: semantics unproven. The sender's 1st explicit argument; the IClient-direction
    // receiver does not forward it (it pushes the literal 1).
    unsigned int TargetId;   // +0x00
    // TODO: semantics unproven. Forwarded as the chat slot's 2nd argument.
    unsigned int SenderId;   // +0x04
    unsigned int Size;         // +0x08  operator new() argument and memcpy length
    void* Buffer;              // +0x0c  owned by the packet
};
static_assert(sizeof(FPayloadCommonChatMsg) == 0x10, "CHATMSG payload 0x10");

// ch1 op 0x08 FLPACKET_COMMON_ACTIVATEEQUIP, packet size 0x28 -> payload 0x08.
// Sender 0x06d8bbb0 (IClient+0x004), receiver case 8. Mirrors XActivateEquip with the
// bool and the 16-bit id swapped round.
struct FPayloadCommonActivateEquip {
    unsigned int ObjectId;     // +0x00  <- XActivateEquip +0x00
    unsigned char bActivate;   // +0x04  <- XActivateEquip +0x06
    unsigned char Pad_0x05;    // +0x05
    unsigned short EquipId;    // +0x06  <- XActivateEquip +0x04
};
static_assert(sizeof(FPayloadCommonActivateEquip) == 0x08, "ACTIVATEEQUIP payload 0x08");

// ch1 op 0x0e FLPACKET_COMMON_ACTIVATECRUISE, packet size 0x28 -> payload 0x08.
// Sender 0x06d8bc10 (IClient+0x008), receiver case 0xe. Same order as XActivateCruise.
struct FPayloadCommonActivateCruise {
    unsigned int ObjectId;     // +0x00
    unsigned char bActivate;   // +0x04
    unsigned char bDisruptCruise;  // +0x05  TODO: semantics unproven (XActivateCruise +0x05)
    unsigned char Pad_0x06[2]; // +0x06
};
static_assert(sizeof(FPayloadCommonActivateCruise) == 0x08, "ACTIVATECRUISE payload 0x08");

// ch1 op 0x0f FLPACKET_COMMON_GOTRADELANE, packet size 0x30 -> payload 0x10.
// Sender 0x06d8bcb0 (IClient+0x018), receiver case 0xf. Same order as XGoTradelane.
struct FPayloadCommonGoTradelane {
    unsigned int ObjectId;       // +0x00
    unsigned int TradelaneRing1; // +0x04
    unsigned int TradelaneRing2; // +0x08
    unsigned char bMidLane;    // +0x0c  XGoTradelane +0x0c; both builders pass go_tradelane's bFastExit
    unsigned char Pad_0x0d[3];   // +0x0d
};
static_assert(sizeof(FPayloadCommonGoTradelane) == 0x10, "GOTRADELANE payload 0x10");

// ch1 op 0x10 FLPACKET_COMMON_STOPTRADELANE, packet size 0x2c -> payload 0x0c.
// Sender 0x06d8bd10 (IClient+0x01c), receiver caseD_10 at 0x06d839fe.
// IClient's four-argument slot serializes only the ship id (the rings stay zero); the
// IServerImpl sender 06d8e770 writes all three. Both receivers forward all three.
struct FPayloadCommonStopTradelane {
    unsigned int ShipId;   // +0x00  <- the sender's 2nd argument
    unsigned int TradelaneRing1;   // +0x04  entrance ring (IServer); zero (IClient)
    unsigned int TradelaneRing2;   // +0x08  exit ring (IServer); zero (IClient)
};
static_assert(sizeof(FPayloadCommonStopTradelane) == 0x0c, "STOPTRADELANE payload 0x0c");

// The buffer family -- ch1 ops 0x11, 0x13, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1c, packet
// size 0x2c -> payload 0x0c. Senders 0x06d8b4f0 / 0x06d8b540 / 0x06d8b590 / 0x06d8b7a0 /
// 0x06d8b610 / 0x06d8b7f0 / 0x06d8b890 / 0x06d8b840. Every receiver is
// RealClient->vt[slot](1, payload+0x00, payload+0x04).
//
// payload+0x08 is the ownership flag; its one reader is the class destructor (0x13's is
// FUN_06d9b1a0, siblings 06d9b0e0 / 06d9b260 / 06d9b320 / 06d9b4c0 / 06d9b580 / 06d9b640 /
// 06d9b700), which frees the block only when it is set. The constructors zero buffer and
// flag. The IClient senders:
//   * 0x16 REQUEST_BEST_PATH and 0x18 REQUEST_PLAYER_STATS copy the caller's bytes and set
//     the flag -- the packet owns its copy;
//   * 0x13 / 0x17 / 0x19 / 0x1a / 0x1c store the caller's pointer and set the flag -- the
//     packet takes the caller's block over (every Server.dll caller hands a fresh block);
//   * 0x11 SET_WEAPON_GROUP stores the caller's pointer with the flag clear -- the one
//     borrower, so it must be delivered before the call returns.
// The wire decoders (0x06da62d0, 0x06da5bc0) set the flag on the block they allocate.
struct FPayloadCommonBuffer {
    void* Buffer;              // +0x00  sender's 2nd argument
    unsigned int Size;         // +0x04  sender's 3rd argument
    unsigned char bOwnsBuffer; // +0x08  the destructor frees Buffer when set
    unsigned char Pad_0x09[3]; // +0x09
};
static_assert(sizeof(FPayloadCommonBuffer) == 0x0c, "buffer-family payload 0x0c");

// ch1 op 0x12 FLPACKET_COMMON_PLAYER_TRADE, packet size 0x44 -> payload 0x24.
// Eight senders, 0x06d8b8e0 / 0x06d8b980 / 0x06d8b930 / 0x06d8b9d0 / 0x06d8ba20 /
// 0x06d8baa0 / 0x06d8bb10 / 0x06d8bb60, all writing the same packet type with a different
// bitmask in payload+0x00; receiver sub-switch at 0x06d8375d. Each sub-type writes only
// the fields it needs and leaves the rest at the constructor's zeros.
struct FPayloadCommonPlayerTrade {
    unsigned int SubType;       // +0x00  1, 2, 4, 8, 0x10, 0x20, 0x40 or 0x80
    unsigned int Reason;    // +0x04  sub-type 2 only. TODO: semantics unproven.
    unsigned int ShipId;    // +0x08  every sub-type except 2. TODO: semantics unproven.
    unsigned int Money;         // +0x0c  sub-type 4 only (SetTradeMoney)
    unsigned char bAccepted;    // +0x10  sub-type 0x40 only (AcceptTrade)
    unsigned char Pad_0x11[3];  // +0x11
    unsigned int EquipArchId;   // +0x14  sub-types 8 / 0x10  <- EquipDesc +0x04
    unsigned int EquipCount;    // +0x18  sub-types 8 / 0x10  <- EquipDesc +0x14
    float EquipStatus;          // +0x1c  sub-type 8 only     <- EquipDesc +0x10
    unsigned short EquipId;     // +0x20  sub-types 8 / 0x10  <- EquipDesc +0x02
    unsigned short Pad_0x22;    // +0x22
};
static_assert(sizeof(FPayloadCommonPlayerTrade) == 0x24, "PLAYER_TRADE payload 0x24");

// ch1 op 0x14 FLPACKET_COMMON_JETTISONCARGO, packet size 0x2c -> payload 0x0c.
// Sender 0x06d8bdb0 (IClient+0x020), receiver caseD_14 at 0x06d83a1c. Same order as
// XJettisonCargo.
struct FPayloadCommonJettisonCargo {
    unsigned int ObjectId;    // +0x00
    unsigned short ItemId;    // +0x04
    unsigned short Pad_0x06;  // +0x06
    unsigned int Count;       // +0x08
};
static_assert(sizeof(FPayloadCommonJettisonCargo) == 0x0c, "JETTISONCARGO payload 0x0c");

// ch1 op 0x15 FLPACKET_COMMON_ACTIVATETHRUSTERS, packet size 0x28 -> payload 0x08.
// Sender 0x06d8bc60 (IClient+0x00c), receiver caseD_15 at 0x06d8399c.
struct FPayloadCommonActivateThrusters {
    unsigned int ObjectId;      // +0x00
    unsigned char bActivate;    // +0x04
    unsigned char Pad_0x05[3];  // +0x05
};
static_assert(sizeof(FPayloadCommonActivateThrusters) == 0x08, "ACTIVATETHRUSTERS payload 0x08");

// ch1 op 0x1b FLPACKET_COMMON_POP_UP_DIALOG, packet size 0x34 -> payload 0x14.
// Sender 0x06d8b690 (IClient+0x17c), receiver caseD_1b at 0x06d835e3.
// Each FmtStr is sized, allocated and FmtStr::flatten'ed (COMMON.DLL); the receiver
// unflattens it.
struct FPayloadCommonPopUpDialog {
    unsigned int SizeA;     // +0x00  flattened size of the 1st FmtStr
    void* BufferA;          // +0x04  flattened bytes of the 1st FmtStr
    unsigned int SizeB;     // +0x08  flattened size of the 2nd FmtStr
    void* BufferB;          // +0x0c  flattened bytes of the 2nd FmtStr
    // +0x10  button bits (1 yes, 2 no, 4 later, 8 ok/close): server -> client the buttons
    // to show (freelancer.exe NnMissionMessagePopUp), client -> server the one clicked
    // (IServerImpl::PopUpDialog's only field).
    unsigned int Buttons;
};
static_assert(sizeof(FPayloadCommonPopUpDialog) == 0x14, "POP_UP_DIALOG payload 0x14");

// ch1 op 0x1d FLPACKET_COMMON_TRACTOROBJECTS, packet size 0x38 -> payload 0x18.
// Sender 0x06d8be10 (IClient+0x014), receiver caseD_1d at 0x06d8326a. Same shape as
// XTractorObjects; the id vector's ELEMENTS are copied into the packet.
struct FPayloadCommonTractorObjects {
    unsigned int ObjectId;                  // +0x00
    unsigned short TractorEquipId;          // +0x04
    unsigned short Pad_0x06;                // +0x06
    FRetailVector<unsigned int> TargetIds;  // +0x08  element stride 4
};
static_assert(sizeof(FPayloadCommonTractorObjects) == 0x18, "TRACTOROBJECTS payload 0x18");

// payload 0x0c: ch2 0x3d FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE. The first dword is the
// equipment handle FUN_06d8fa90 hangs off packet+0x20 (see rpc_containers.h); the byte at
// +0x04 and dword at +0x08 are written by the sender directly. Registry size 0x2c.
struct FPayloadServerRemoteObjectCargoUpdate {
    unsigned int EquipHandle;    // +0x00, owned by the packet
    // +0x04  IClient::SendServerRemoteObjectCargoUpdate's Flag: Server.dll always passes 1,
    // freelancer.exe's FUN_0053deb0 never reads it. TODO: meaning.
    unsigned char Flag;
    unsigned char Pad_0x05[3];   // +0x05
    unsigned int ObjectId;       // +0x08  the ship whose cargo is synced (Server passes Ship->Id)
};
static_assert(sizeof(FPayloadServerRemoteObjectCargoUpdate) == 0x0c,
              "ch2 0x3d payload 0x0c");

// The three channel-1 codecs live in src/rpclocal/rpc_payload_ch1.cpp.
extern const FRpcPayloadCodec GChannel1Codecs[];
extern const unsigned int GChannel1CodecCount;

// ===========================================================================
// Channel-2 payload structures (server -> client).
//
// Read like channel 1: sender (IClient slot body, FUN_06d96300(Serializer, 2, Opcode,
// &OutPacket)), receiver (the channel-2 switch of IServerImpl::HandlePacket, jump table
// .text:0x06d83b44) and registry size.
//
// Two conventions differ from channel 1:
//   1. For one family of types the literal 1 that replaces the session id is passed by
//      reference, because the slot takes uint const&; those senders also dereference their
//      own first argument. Each codec notes which convention its type uses.
//   2. Four types (0x02, 0x06, 0x0a, 0x21) deliver out of file-static .data storage
//      instead of a stack struct (not re-entrant in retail either).
// ===========================================================================

// --- shared shapes -------------------------------------------------------

// payload 0x04: one dword written by value. Registry size 0x24.
struct FPayloadServerDword {
    unsigned int Field_0x00;
};
static_assert(sizeof(FPayloadServerDword) == 0x04, "ch2 dword payload 0x04");

// payload 0x04: one float (sender FLD/FSTP). Registry size 0x24.
struct FPayloadServerFloat {
    float Field_0x00;
};
static_assert(sizeof(FPayloadServerFloat) == 0x04, "ch2 float payload 0x04");

// payload 0x04: one byte; the packet constructor zeroes packet+0x20 and nothing writes the
// other three. Registry size 0x24.
struct FPayloadServerByte {
    unsigned char Field_0x00;
    unsigned char Pad_0x01[3];
};
static_assert(sizeof(FPayloadServerByte) == 0x04, "ch2 byte payload 0x04");

// payload 0x08: two dwords by value. Registry size 0x28.
struct FPayloadServerDwordPair {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
};
static_assert(sizeof(FPayloadServerDwordPair) == 0x08, "ch2 dword-pair payload 0x08");

// payload 0x08: a sized blob the packet owns, written by FUN_06d97b00 (block at
// packet+0x24, length at packet+0x20); the destructor (e.g. FUN_06d974c0) frees it.
struct FPayloadServerBlob {
    unsigned int Size;   // +0x00
    void* Buffer;        // +0x04  owned by the packet
};
static_assert(sizeof(FPayloadServerBlob) == 0x08, "ch2 blob payload 0x08");

// payload 0x08: the same blob, fields reversed, written by FUN_06d90a00. Only
// GFUPDATEAMBIENTSCRIPT uses it; its receiver reads the reversed order.
struct FPayloadServerBlobReversed {
    void* Buffer;        // +0x00  owned by the packet
    unsigned int Size;   // +0x04
};
static_assert(sizeof(FPayloadServerBlobReversed) == 0x08, "ch2 reversed-blob payload 0x08");

// --- per-type shapes -----------------------------------------------------

// ch2 0x06 FLPACKET_SERVER_DESTROYOBJECT, size 0x28 -> payload 0x08.
// Sender 0x06d89140 (IClient+0x0dc), receiver caseD_6 at 0x06d81f6a.
// The receiver delivers through the statics .data:0x06dbb824/0x06dbb828, writing the
// second field as a zero-extended dword. FServerDestroyObjectInfo is in rpc_iclient_args.h.
struct FPayloadServerDestroyObject {
    unsigned int ObjectId;      // +0x00  <- arg struct +0x00
    unsigned char DestroyType;   // +0x04  <- arg struct +0x04 (0 vanish, 1 destroyed)
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FPayloadServerDestroyObject) == 0x08, "DESTROYOBJECT payload 0x08");

// ch2 0x07 FLPACKET_SERVER_LAUNCH, size 0x48 -> payload 0x28.
// Sender 0x06d8a080 (IClient+0x0f0), receiver caseD_7 at 0x06d81951.
// Reordered: three leading dwords, the Vector from arg+0x1c (FUN_06d8f510, 3-dword copier),
// then the Quaternion from arg+0x0c (FUN_06d8f4f0, 4-dword copier); the receiver is the
// strict inverse.
struct FPayloadServerLaunch {
    unsigned int ShipId;            // +0x00  <- arg +0x00
    unsigned int LaunchObjId;       // +0x04  <- arg +0x04, the object launched from
    unsigned int LaunchPosIndex;    // +0x08  <- arg +0x08, its launch_pos index (-1: none)
    Vector Position;            // +0x0c  <- arg +0x1c (FUN_06d8f510)
    Quaternion Orientation;     // +0x18  <- arg +0x0c (FUN_06d8f4f0)
};
static_assert(sizeof(FPayloadServerLaunch) == 0x28, "LAUNCH payload 0x28");

// FServerLaunchInfo (the argument struct, built by Server.dll's IServerImpl::PlayerLaunch)
// is in rpc_iclient_args.h.

// ch2 0x0a FLPACKET_SERVER_ACTIVATEOBJECT, size 0x2c -> payload 0x0c.
// Sender 0x06d89770 (IClient+0x0e0), receiver caseD_a at 0x06d823ab (statics
// .data:0x06dbb8f4 / 0x06dbb8f8 / 0x06dbb8fc). The payload is the argument struct, so
// FPayloadServerActivateObject is declared in rpc_iclient_args.h.

// payload 0x0c: three dwords, the view of the shared shape-5 codec (rpc_wire_generic.cpp).
struct FPayloadServerThreeDwords {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
};
static_assert(sizeof(FPayloadServerThreeDwords) == 0x0c, "three-dword payload 0x0c");

// ch2 0x0b FLPACKET_SERVER_LAND, size 0x2c -> payload 0x0c. Sender 0x06d8a990
// (IClient+0x0ec), receiver caseD_b at 0x06d8237c. Three dwords, order preserved: the
// payload is FServerLandInfo (rpc_iclient_args.h) as it stands.
static_assert(sizeof(FServerLandInfo) == sizeof(FPayloadServerThreeDwords),
              "LAND payload is FServerLandInfo");

// ch2 0x21 FLPACKET_SERVER_SYSTEM_SWITCH_OUT, size 0x28 -> payload 0x08. The payload is
// FServerSystemSwitchOutInfo (rpc_iclient_args.h) as it stands.
static_assert(sizeof(FServerSystemSwitchOutInfo) == sizeof(FPayloadServerDwordPair),
              "SYSTEM_SWITCH_OUT payload is FServerSystemSwitchOutInfo");

// ch2 0x27 FLPACKET_SERVER_REQUESTCREATESHIPRESP, size 0x28 -> payload 0x08.
// Sender 0x06d8a130 (IClient+0x0f4, three arguments), receiver caseD_27 at 0x06d819b1.
struct FPayloadServerRequestCreateShipResp {
    unsigned char bSuccess;   // +0x00  <- 2nd argument, truncated to a byte
    unsigned char Pad_0x01[3];
    unsigned int ShipId;    // +0x04  <- 3rd argument
};
static_assert(sizeof(FPayloadServerRequestCreateShipResp) == 0x08, "REQUESTCREATESHIPRESP payload 0x08");

// ch2 0x2c FLPACKET_SERVER_CREATEMINE, size 0x68 -> payload 0x48.
// Sender 0x06d8a180 (IClient+0x0c4), receiver caseD_2c at 0x06d81ac1.
// Eighteen fields; the wire swaps the groups at arg+0x10 and arg+0x1c and carries arg+0x0c
// as a word (the receiver leaves the upper half of its local untouched).
struct FPayloadServerCreateMine {
    unsigned int ObjId;    // +0x00  <- arg +0x00
    unsigned int ArchetypeNickname;    // +0x04  <- arg +0x04
    unsigned int OwnerId;    // +0x08  <- arg +0x08
    unsigned short SubObjId;  // +0x0c  <- arg +0x0c (word only)
    unsigned short Pad_0x0e;
    unsigned int OrientWBits;    // +0x10  <- arg +0x1c
    unsigned int OrientXBits;    // +0x14  <- arg +0x20
    unsigned int OrientYBits;    // +0x18  <- arg +0x24
    unsigned int OrientZBits;    // +0x1c  <- arg +0x28
    unsigned int PosXBits;    // +0x20  <- arg +0x10
    unsigned int PosYBits;    // +0x24  <- arg +0x14
    unsigned int PosZBits;    // +0x28  <- arg +0x18
    unsigned int VelXBits;    // +0x2c  <- arg +0x2c
    unsigned int VelYBits;    // +0x30  <- arg +0x30
    unsigned int VelZBits;    // +0x34  <- arg +0x34
    unsigned int AngVelXBits;    // +0x38  <- arg +0x38
    unsigned int AngVelYBits;    // +0x3c  <- arg +0x3c
    unsigned int AngVelZBits;    // +0x40  <- arg +0x40
    unsigned int OwnerPlayer;    // +0x44  <- arg +0x44
};
static_assert(sizeof(FPayloadServerCreateMine) == 0x48, "CREATEMINE payload 0x48");

// The argument struct is Server.dll's FServerCreateProjectileInfo (server_createinfo.h,
// built by FUN_06d07c80). Its +0x0e half never crosses the wire.

// ch2 0x2e FLPACKET_SERVER_SETADDITEM, size 0x40 -> payload 0x20.
// Sender 0x06d885c0 (IClient+0x068), receiver caseD_2e at 0x06d819ed. The sender fans an
// EquipDesc out into scalars and, unless is_internal(), copies the hardpoint string into a
// block the packet owns (FUN_06d97470). The receiver rebuilds the EquipDesc with the
// setters and interns the hardpoint with StringAlloc before set_hardpoint.
struct FPayloadServerSetAddItem {
    unsigned int ArchId;            // +0x00  <- EquipDesc +0x04
    int Count;                      // +0x04  <- EquipDesc +0x14
    unsigned int HardpointSize;     // +0x08  <- strlen(EquipDesc.Hardpoint) + 1, or 0
    unsigned int AddResult;        // +0x0c  <- the slot's 2nd argument. TODO: unproven.
    unsigned int Id;                // +0x10  <- EquipDesc +0x02, zero-extended from a word
    float Health;                   // +0x14  <- EquipDesc +0x10
    unsigned char bEquipped;        // +0x18  <- EquipDesc +0x0c
    unsigned char bMission;         // +0x19  <- EquipDesc +0x18
    unsigned char Pad_0x1a[2];
    char* Hardpoint;                // +0x1c  owned by the packet, nullptr when internal
};
static_assert(sizeof(FPayloadServerSetAddItem) == 0x20, "SETADDITEM payload 0x20");

// ch2 0x2f FLPACKET_SERVER_SETREMOVEITEM, size 0x2c -> payload 0x0c.
// Sender 0x06d88670 (IClient+0x06c, four arguments), receiver caseD_2f at 0x06d81aa0.
// Wire order is not argument order: A2 (a word) first, then A3, then A1.
struct FPayloadServerSetRemoveItem {
    unsigned int ItemId;    // +0x00  <- 3rd argument, zero-extended from a word
    unsigned int Count;    // +0x04  <- 4th argument
    unsigned int RemoveResult;    // +0x08  <- 2nd argument
};
static_assert(sizeof(FPayloadServerSetRemoveItem) == 0x0c, "SETREMOVEITEM payload 0x0c");

// payload 0x10: four dwords, the view of the shared shape-6 codec (rpc_wire_generic.cpp).
struct FPayloadServerFourDwords {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned int Field_0x0c;
};
static_assert(sizeof(FPayloadServerFourDwords) == 0x10, "four-dword payload 0x10");

// ch2 0x31 FLPACKET_SERVER_EXPLODEASTEROIDMINE, size 0x30 -> payload 0x10.
// Sender 0x06d8a360 (IClient+0x0d0), receiver caseD_31 at 0x06d81b6d. Server.dll's
// FMineExplosion (server_sysdb.h, built by MedMine::update), order preserved both ways,
// the Vector as raw bits.
struct FPayloadServerExplodeAsteroidMine {
    unsigned int MineId;            // +0x00
    unsigned int OffsetXBits;       // +0x04
    unsigned int OffsetYBits;       // +0x08
    unsigned int OffsetZBits;       // +0x0c
};
static_assert(sizeof(FPayloadServerExplodeAsteroidMine) == sizeof(FPayloadServerFourDwords),
              "EXPLODEASTEROIDMINE payload 0x10");

// ch2 0x33 FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE, size 0x2c -> payload 0x0c.
// Sender 0x06d8ac40 (IClient+0x124), receiver caseD_33 at 0x06d823df.
// Width asymmetry: the sender widens arg+0x04 (a byte) to a dword and carries arg+0x08 as a
// word; the receiver writes back a byte and a zero-extended dword.
struct FPayloadServerSetMissionObjectiveState {
    unsigned int MissionId;    // +0x00  <- arg +0x00
    unsigned int ObjectiveIndex;    // +0x04  <- arg +0x04, zero-extended from a byte
    unsigned short State;  // +0x08  <- arg +0x08 (word)
    unsigned short Pad_0x0a;
};
static_assert(sizeof(FPayloadServerSetMissionObjectiveState) == 0x0c, "SETMISSIONOBJECTIVESTATE payload 0x0c");

// FServerMissionObjectiveStateInfo is in rpc_iclient_args.h (built by Server.dll's
// pub::Player::SetMissionObjectiveState).

// ch2 0x38 FLPACKET_SERVER_ITEMTRACTORED, size 0x2c -> payload 0x0c:
// FPayloadServerItemTractored is in rpc_iclient_args.h (built by StarSystem::tractor_loot,
// FUN_06d0ed40).

// ch2 0x3c FLPACKET_SERVER_REPAIROBJECT, size 0x2c -> payload 0x0c.
// Sender 0x06d8bfe0 (IClient+0x150), receiver caseD_3c at 0x06d82b55.
// The middle field travels as a zero-extended dword and comes back as a word.
struct FPayloadServerRepairObject {
    unsigned int ObjectId;    // +0x00  <- arg +0x00
    unsigned int EquipId;    // +0x04  <- arg +0x04, zero-extended from a word
    unsigned int Points;    // +0x08  <- arg +0x08
};
static_assert(sizeof(FPayloadServerRepairObject) == 0x0c, "REPAIROBJECT payload 0x0c");

// FServerRepairObjectInfo (the argument struct, also read by freelancer.exe /
// RemoteClient.dll) is in rpc_iclient_args.h.

// ch2 0x41 FLPACKET_SERVER_BURNFUSE, size 0x3c -> payload 0x1c.
// Sender 0x06d8c170 (IClient+0x15c), receiver caseD_41 at 0x06d82c04. Straight copy both
// ways, mixed widths.
struct FPayloadServerBurnFuse {
    unsigned int SourceId;
    unsigned int ObjectId;
    unsigned int FuseId;
    unsigned short SubObjId;
    unsigned short Pad_0x0e;
    unsigned int AtTBits;
    unsigned int LifetimeBits;
    unsigned char bLit;
    unsigned char Pad_0x19[3];
};
static_assert(sizeof(FPayloadServerBurnFuse) == 0x1c, "BURNFUSE payload 0x1c");

// ch2 0x44 FLPACKET_SERVER_REQUEST_RETURNED, size 0x28 -> payload 0x08.
// Sender 0x06d8a9e0 (IClient+0x14c, five arguments), receiver caseD_44 at 0x06d81918.
// The receiver zero-extends the first byte and sign-extends the other two.
struct FPayloadServerRequestReturned {
    unsigned int ShipId;    // +0x00  <- 2nd argument
    unsigned char RequestType;   // +0x04  <- 3rd argument, delivered zero-extended
    signed char Response;     // +0x05  <- 4th argument, delivered sign-extended
    signed char Extra;     // +0x06  <- 5th argument, delivered sign-extended
    unsigned char Pad_0x07;
};
static_assert(sizeof(FPayloadServerRequestReturned) == 0x08, "REQUEST_RETURNED payload 0x08");

// payload 0x08: a dword and a byte, the view of the shared shape-4 codec
// (rpc_wire_generic.cpp).
struct FPayloadServerDwordByte {
    unsigned int Field_0x00;
    unsigned char Field_0x04;
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FPayloadServerDwordByte) == 0x08, "dword+byte payload 0x08");

// ch2 0x46 FLPACKET_SERVER_MARKOBJ, size 0x28 -> payload 0x08.
// Sender 0x06d8ace0 (IClient+0x12c), receiver caseD_46 at 0x06d82420, which zero-extends
// the byte into IClient::SendServerMarkObj's MarkType.
struct FPayloadServerMarkObj {
    unsigned int ObjectId;          // +0x00  <- 2nd argument
    unsigned char MarkType;         // +0x04  <- 3rd argument, truncated to a byte
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FPayloadServerMarkObj) == sizeof(FPayloadServerDwordByte),
              "MARKOBJ payload 0x08");

// ch2 0x47 FLPACKET_SERVER_CFGINTERFACENOTIFICATION, size 0x2c -> payload 0x0c.
// Sender 0x06d8ad30 (IClient+0x130), receiver caseD_47 at 0x06d8244f. The payload is the
// argument struct; FPayloadServerCfgInterfaceNotification is in rpc_iclient_args.h.

// ch2 0x4a FLPACKET_SERVER_SETGUIDEDTARGET, size 0x2c -> payload 0x0c.
// Sender 0x06d8a800 (IClient+0x0d4, four arguments), receiver caseD_4a at 0x06d83048.
struct FPayloadServerSetGuidedTarget {
    unsigned int GuidedId;    // +0x00  <- 2nd argument
    unsigned int TargetId;    // +0x04  <- 3rd argument
    unsigned short TargetSubObjId;  // +0x08  <- 4th argument, truncated to a word
    unsigned short Pad_0x0a;
};
static_assert(sizeof(FPayloadServerSetGuidedTarget) == 0x0c, "SETGUIDEDTARGET payload 0x0c");

// ch2 0x4b FLPACKET_SERVER_SET_CAMERA, size 0x44 -> payload 0x24.
// Sender 0x06d8ae90 (IClient+0x160), receiver caseD_4b at 0x06d82573. The payload is the
// argument struct built by pub::Player::SetCamera; FPayloadServerSetCamera is in
// rpc_iclient_args.h.

// ch2 0x51 FLPACKET_SERVER_USE_ITEM, size 0x28 -> payload 0x08:
// FPayloadServerUseItem is in rpc_iclient_args.h (built by StarSystem::use_item,
// FUN_06d0efe0).

// ch2 0x01 FLPACKET_SERVER_CONNECTRESPONSE, size 0x24 -> payload 0x04.
// Sender 0x06d8eff0 is not an IClient slot: the pump FUN_06d8efc0 sends it on its first run
// with a literal 1 in the only field. Receiver caseD_1 at 0x06d816d8 is the one channel-2
// case without a leading 1: RealClient->vt[+0x2c](payload+0x00).
struct FPayloadServerConnectResponse {
    unsigned int ClientId;    // +0x00  always the literal 1
};
static_assert(sizeof(FPayloadServerConnectResponse) == 0x04, "CONNECTRESPONSE payload 0x04");

// ch2 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED, size 0x30 -> payload 0x10.
// Sender 0x06d8a030 (IClient+0x38), receiver caseD_8 at 0x06d81828. A straight copy with one
// hole: neither side touches +0x04, so retail delivers stack garbage there; the port zeroes
// it (as for CREATEMINE and REPAIROBJECT).
struct FPayloadServerCharSelectVerified {
    unsigned int VibeHandle;    // +0x00
    unsigned int Unused_0x04;   // +0x04  never written, never read
    unsigned int ServerTimeLo;    // +0x08
    unsigned int ServerTimeHi;    // +0x0c
};
static_assert(sizeof(FPayloadServerCharSelectVerified) == 0x10, "CHARSELECTVERIFIED payload 0x10");

// ch2 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN, size 0x44 -> payload 0x24.
// Sender 0x06d8a8b0 (IClient+0xe8), receiver caseD_22 at 0x06d822c6 (file-static
// .data:0x06dbb91c). The payload is the argument struct (built by FUN_06d0cc00);
// FPayloadServerSystemSwitchIn is in rpc_iclient_args.h.

// ch2 0x2a FLPACKET_SERVER_ADJUSTATTITUDE, size 0x28 -> payload 0x08.
// Sender 0x06d88f70 (IClient+0x10c), receiver caseD_2a at 0x06d81317. The payload is the
// argument struct; FPayloadServerAdjustAttitude is in rpc_iclient_args.h.

// ch2 0x53 FLPACKET_SERVER_FORMATION_UPDATE, size 0x30 -> payload 0x10.
// Sender 0x06d8c600 (IClient+0x1e4); receiver caseD_53 at 0x06d8133f reads the three
// dwords back with FLD/FSTP -- a Vector rebuilt on its stack and passed by address.
struct FPayloadServerFormationUpdate {
    unsigned int LeaderId;    // +0x00  <- 2nd argument, delivered by value
    Vector Offset;              // +0x04  <- the 3rd argument's first three floats
};
static_assert(sizeof(FPayloadServerFormationUpdate) == 0x10, "FORMATION_UPDATE payload 0x10");

// ch2 0x57 FLPACKET_SERVER_SET_MUSIC, size 0x38 -> payload 0x18.
// Sender 0x06d8c660 (IClient+0x1e8) serializes six of its seven arguments (not the session
// id). Receiver caseD_57 at 0x06d81372 zero-extends the byte and passes the float by value.
struct FPayloadServerSetMusic {
    unsigned int SpaceMusic;    // +0x00
    unsigned int DangerMusic;    // +0x04
    unsigned int BattleMusic;    // +0x08
    unsigned int OverrideMusic;    // +0x0c
    unsigned char bNoLoop;   // +0x10  delivered zero-extended
    unsigned char Pad_0x11[3];
    float Fade;           // +0x14  FLD/FSTP in the sender
};
static_assert(sizeof(FPayloadServerSetMusic) == 0x18, "SET_MUSIC payload 0x18");

// ===========================================================================
// The two multi-slot channel-2 opcodes: like ch1 PLAYER_TRADE, the IClient slot is picked
// by a field of the payload, not by the routing header.
// ===========================================================================

// ch2 0x52 FLPACKET_SERVER_PLAYERLIST, size 0x30 -> payload 0x10.
//
// Discriminator: the dword at payload+0x00, compared against 0, 1 and 2 by the receiver
// (.text:0x06d83102). The constructor (0x06d9ec60) seeds 3, which falls to the default
// branch and delivers nothing -- "no message", not a fourth kind.
//
//   Kind 0 (sender 0x06d8c5b0, IClient+0x1c0): slot 112, no payload at all.
//   Kind 1 (sender 0x06d8c500, IClient+0x1b8): slot 110, (1, Name, PlayerId, bool).
//   Kind 2 (sender 0x06d8c560, IClient+0x1bc): slot 111, (1, PlayerId).
//
// Name is a wide string the packet owns, set by FUN_06d99950 (free old, _wcsdup new into
// packet+0x24, value into packet+0x28). Kind 2 passes a null string, kind 0 two nulls, so
// only kind 1 carries one.
struct FPayloadServerPlayerList {
    unsigned int Kind;          // +0x00  0, 1 or 2; the ctor's 3 means "nothing to send"
    wchar_t* Name;              // +0x04  malloc'd copy, freed by the packet
    unsigned int PlayerId;    // +0x08
    unsigned char bAlreadyPresent;   // +0x0c  delivered as bool: SETNZ against zero
    unsigned char Pad_0x0d[3];
};
static_assert(sizeof(FPayloadServerPlayerList) == 0x10, "PLAYERLIST payload 0x10");

// The kind values (kPlayerListKind*) are public, in rpc_iclient_args.h, because
// RemoteClient / RemoteServer encode and decode the same discriminator.

// MISCOBJUPDATE's two containers are the standard ones:
//   * Text is retail's VC6 basic_string<unsigned short> -- wide: the MSVCP60 imports are
//     the `@G` instantiation (_Split [06db315c], _Grow [06db316c], _Eos [06db3158], _Nullstr
//     [06db3190]), the encoder doubles the character count, and the decoder allocates
//     len*2+2 and measures with wcslen. The port holds std::wstring.
//   * Ids is retail's VC6 std::vector<unsigned int> at packet+0x58; FUN_06d98cf0 is
//     vector::insert(_Last, 1, value), i.e. push_back.
// Both are members of FPacket_FLPACKET_SERVER_MISCOBJUPDATE (rpc_packet_registry.cpp), built
// by its constructor (retail 0x06d9edc0).

// ch2 0x54 FLPACKET_SERVER_MISCOBJUPDATE, size 0x6c -> payload 0x4c.
//
// Discriminator: a two-byte bitmask at payload+0x00. The receiver (caseD_54 at
// .text:0x06d8316d) tests it seven times in a fixed order and may fire more than one slot
// from one packet; each sender sets the bits its fields need through nine one-line setters
// (OR the bit, one store):
//
//   FUN_06d98af0  0x0001 -> +0x14 dword        FUN_06d98b20  0x0010 -> +0x20 wide string
//   FUN_06d98b00  0x0002 -> +0x18/+0x1c pair   FUN_06d98ae0  0x0020 -> +0x34 dword
//   FUN_06d98ad0  0x0004 -> +0x04 dword        FUN_06d98ce0  0x0040 -> +0x0c word
//   FUN_06d98ac0  0x0008 -> +0x08 dword        FUN_06d98cd0  0x0080 -> +0x10 dword
//   FUN_06d98cf0  0x0100 -> +0x38 vector push_back (it ORs byte [this+0x21], 0x01)
//
// Bit 0x0008 is never tested by the receiver. The slot for bit 0x0001 depends on bit
// 0x0004:
//
//   0x0009 (sender 0x06d8c1f0) -> slot 113 (1, ObjectId, GroupId)
//   0x0005 (sender 0x06d8c390) -> slot 118 (1, PlayerId, GroupId)
//
// Sender -> mask -> slot:
//   0x06d8c1f0 IClient+0x1c4  mask 0x0009  slot 113
//   0x06d8c290 IClient+0x1c8  mask 0x0002  slot 114
//   0x06d8c430 IClient+0x1cc  mask 0x0010  slot 115 (const basic_string<G>&)
//   0x06d8c240 IClient+0x1d0  mask 0x0028  slot 116
//   0x06d8c2e0 IClient+0x1d4  mask 0x0044  slot 117 (the word is zero-extended)
//   0x06d8c390 IClient+0x1d8  mask 0x0005  slot 118
//   0x06d8c3e0 IClient+0x1dc  mask 0x0084  slot 119
//   0x06d8c330 IClient+0x1e0  mask 0x0100  slot 120 (const std::vector<uint>&)
struct FPayloadServerMiscObjUpdate {
    unsigned short Flags;             // +0x00  ctor zeroes this word and nothing more
    unsigned short Pad_0x02;          // +0x02  retail leaves these two bytes uninitialised
    unsigned int PlayerId;          // +0x04  flag 0x0004
    unsigned int ObjectId;          // +0x08  flag 0x0008
    unsigned short Rank;        // +0x0c  flag 0x0040
    unsigned short Pad_0x0e;          // +0x0e
    unsigned int SystemId;          // +0x10  flag 0x0080
    unsigned int GroupId;          // +0x14  flag 0x0001
    unsigned int MissionId;          // +0x18  flag 0x0002
    unsigned int MissionSetBy;          // +0x1c  flag 0x0002
    // Offsets from here on are retail's; the port's members after Text sit wherever
    // std::wstring's size puts them.
    std::wstring Text;                // +0x20  flag 0x0010

    unsigned int Field_0x30;          // +0x30  zeroed by the ctor, no sender ever writes it
    unsigned int Affiliation;          // +0x34  flag 0x0020; the ctor seeds it with -1
    std::vector<unsigned int> Ids;    // +0x38  flag 0x0100
    // Low byte of the Ids element count, stored by every FUN_06d98cf0 push_back (0 when
    // Begin is null). The encoder writes it before the elements and the decoder uses it as
    // the loop bound, so a zero here drops the whole array on the wire.
    unsigned char IdCount;         // +0x48
    unsigned char Pad_0x49[3];        // +0x49
};
// Retail payload 0x4c (registry 0x6c). The port's is larger (std::wstring / std::vector
// are not VC6's 0x10 bytes each); FPacket_FLPACKET_SERVER_MISCOBJUPDATE holds it as a
// typed member, so the class is 0x20 + sizeof(this struct), not operator new(0x6c).

const unsigned short kMiscObjUpdateFlagGroupId     = 0x0001;
const unsigned short kMiscObjUpdateFlagMission     = 0x0002;
const unsigned short kMiscObjUpdateFlagPlayerId    = 0x0004;
const unsigned short kMiscObjUpdateFlagObjectId    = 0x0008;
const unsigned short kMiscObjUpdateFlagText        = 0x0010;
const unsigned short kMiscObjUpdateFlagAffiliation = 0x0020;
const unsigned short kMiscObjUpdateFlagRank        = 0x0040;
const unsigned short kMiscObjUpdateFlagSystemId    = 0x0080;
const unsigned short kMiscObjUpdateFlagIds         = 0x0100;

// The sender variant, prepended to the argument dwords the way SendTradePacket prepends
// PLAYER_TRADE's sub-type. One value per retail sender slot (113..120), in slot order,
// named after the IClient::SendServerMiscObjUpdate* suffix.
const unsigned int kMiscObjUpdateVariant_ObjectGroup = 0u;   // slot 113
const unsigned int kMiscObjUpdateVariant_Mission     = 1u;   // slot 114
const unsigned int kMiscObjUpdateVariant_Motd        = 2u;   // slot 115
const unsigned int kMiscObjUpdateVariant_Affiliation = 3u;   // slot 116
const unsigned int kMiscObjUpdateVariant_Rank        = 4u;   // slot 117
const unsigned int kMiscObjUpdateVariant_PlayerGroup = 5u;   // slot 118
const unsigned int kMiscObjUpdateVariant_System      = 6u;   // slot 119
const unsigned int kMiscObjUpdateVariant_Formation   = 7u;   // slot 120

// ===========================================================================
// ch2 0x05 FLPACKET_SERVER_DAMAGEOBJECT -- retail registry 0x40, payload 0x20 (the port
// sizes it from FPayloadServerDamageObject, see there).
//
// Sender 0x06d88c10 (IClient+0x0fc), receiver caseD_5 at 0x06d81ed4. The payload is
// { dword, a std::list, a four-field tail }: payload+0x00 = A1, then FUN_06d90390 assigns
// A2 into payload+0x04 -- the three-phase std::list assignment with 0x14-byte nodes (8 bytes
// of links + a 0x0c value), then a tail of one byte (+0x0c) and three dwords (+0x10..+0x18).
// The receiver FUN_06d90290 is the mirror. 0x04 + 0x0c + 0x10 == 0x40 - 0x20.
//
// The tail is copied shallow both ways (never deep-copied or freed): four plain fields, not
// a std::vector, whatever its { byte, ptr, ptr, ptr } shape suggests.

// Retail's 0x1c-byte DamageList image: a 0x0c std::list header whose 0x14-byte nodes
// (operator new(0x14) at 06d90413 / 06d90324) hold a 0x0c DamageEntry, then a byte at +0x0c
// (bDestroyed, hence the byte copy) and three dwords +0x10 / +0x14 / +0x18 (DamageCause,
// InflictorId, InflictorOwner). The create-object header codec FUN_06da4020 builds the
// default entry: +0x00 = COMMON.DLL SUBOBJ_ID_NONE stored as a word, +0x04 = -1.0f
// (DamageEntry's untouched health), +0x08 = 0; the encoder FUN_06da3720 ships
// word(+0x00), dword(+0x08), float(+0x04).

// ch2 0x05 DAMAGEOBJECT holds the real Common.dll DamageList (common_misc.h). Retail's packet
// ctor 06d9c8d0 builds it in place at packet+0x24; the sender assigns into it
// (FUN_06d90290) and the receiver assigns it into a stack DamageList. The port's DamageList
// follows the build's std::list size (0x1c Release, 0x20 Debug / DebugASan) rather than
// retail's payload 0x20, so the class is sized as the 0x20 header plus this struct
// (rpc_packet_registry.cpp) and holds it as a typed member.
struct FPayloadServerDamageObject {
    unsigned int ObjectId;        // +0x00  the 2nd argument, by value
    DamageList Damage;              // +0x04
};
// The offset contract, not retail's byte count: the payload starts right after the
// 0x20-byte packet header, and the list object follows the one argument dword.
static_assert(sizeof(FPacketBase) == 0x20, "payload starts at packet+0x20");
static_assert(offsetof(FPayloadServerDamageObject, Damage) == sizeof(unsigned int),
              "DAMAGEOBJECT: the DamageList follows the by-value argument dword");

// ===========================================================================
// ch2 0x28 FLPACKET_SERVER_CREATELOOT -- registry 0x34, payload 0x14.
//
// Sender 0x06d8aae0 (IClient+0x0c0), receiver caseD_28 at 0x06d811c8. The payload is
// { dword, byte, a std::list of 0x48-byte records }; the list header is at payload+0x08
// (accessor FUN_06d931b0), push_back FUN_06d93160 allocates 0x50-byte nodes.
// 0x04 + 0x04 + 0x0c == 0x34 - 0x20. Both sides hold an array of records on the caller's
// side and a std::list on the wire.
//
// Retail transposition, reproduced (docs/re/deviations.md): the sender moves record+0x30 to
// wire+0x38 and record+0x2c to wire+0x3c; the receiver moves wire+0x38 to record+0x2c and
// wire+0x3c to record+0x30. The other sixteen moves are strict inverses.

// The caller's record is Server.dll's FServerLootDesc (server_createinfo.h), 0x48 bytes,
// default-constructed on the receiving side by FUN_06d840d0 (zeroes +0x00..+0x1b, +0x44 = 1,
// +0x45 = +0x46 = 0). Its +0x0c moves as a word; its +0x44 never travels.

// The record as it travels: FUN_06d93280 copies 0x46 bytes; the 0x50 node pads it to 0x48.
struct FRpcLootWireRecord {
    unsigned int LootId;        // +0x00 <- record+0x00
    unsigned int ContentsNickname;        // +0x04 <- record+0x04
    unsigned int ContentsHitPtsBits;        // +0x08 <- record+0x08
    unsigned short Units;      // +0x0c <- record+0x0c
    unsigned short Pad_0x0e;        // +0x0e
    unsigned int ContainerNickname;        // +0x10 <- record+0x10
    unsigned int HitPtsBits;        // +0x14 <- record+0x14
    unsigned int NameOverride;        // +0x18 <- record+0x18
    unsigned int PosXBits;        // +0x1c <- record+0x1c
    unsigned int PosYBits;        // +0x20 <- record+0x20
    unsigned int PosZBits;        // +0x24 <- record+0x24
    unsigned int VelXBits;        // +0x28 <- record+0x38
    unsigned int VelYBits;        // +0x2c <- record+0x3c
    unsigned int VelZBits;        // +0x30 <- record+0x40
    unsigned int OrientWBits;        // +0x34 <- record+0x28  (sender seeds it with 1.0f)
    unsigned int OrientYBits;        // +0x38 <- record+0x30  (receiver puts it back at +0x2c)
    unsigned int OrientXBits;        // +0x3c <- record+0x2c  (receiver puts it back at +0x30)
    unsigned int OrientZBits;        // +0x40 <- record+0x34
    unsigned char bMarkedBy;       // +0x44 <- record+0x46
    unsigned char bTemporary;       // +0x45 <- record+0x45
    unsigned char Pad_0x46[2];      // +0x46  inside the 0x50 node, never written
};
static_assert(sizeof(FRpcLootWireRecord) == 0x48, "CREATELOOT wire record is 0x50 - 8 links");

struct FRpcLootWireNode {
    FRpcLootWireNode* Next;         // +0x00
    FRpcLootWireNode* Prev;         // +0x04
    FRpcLootWireRecord Value;       // +0x08
};
static_assert(sizeof(FRpcLootWireNode) == 0x50, "retail loot list node is operator new(0x50)");

struct FRpcLootWireList {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    FRpcLootWireNode* Head;         // +0x04
    unsigned int Size;              // +0x08
};
static_assert(sizeof(FRpcLootWireList) == 0x0c, "retail std::list header is 0x0c");

struct FPayloadServerCreateLoot {
    unsigned int OwnerId;        // +0x00 <- argument+0x00
    unsigned char Kind;       // +0x04 <- low byte of argument+0x04
    unsigned char Pad_0x05[3];      // +0x05
    FRpcLootWireList Items;         // +0x08
};
static_assert(sizeof(FPayloadServerCreateLoot) == 0x14, "CREATELOOT payload 0x14");

// The argument struct is Server.dll's FServerCreateLootRequest (server_createinfo.h). The
// receiver stores the payload byte back as a dword at +0x04 (Kind is four bytes wide, only
// its low byte ships), zeroes +0x08 / +0x09, and both sides read the count at +0x10 as a
// byte.

// ===========================================================================
// ch2 0x2d FLPACKET_SERVER_CREATECOUNTER -- registry 0x68, payload 0x48.
//
// Sender 0x06d8a270 (IClient+0x0cc), receiver caseD_2d at 0x06d81ce9. Eighteen field moves
// each way, strict inverses; the argument struct is 0x48 too, only the order differs. The
// literal 1 travels by reference on both sides.
struct FPayloadServerCreateCounter {
    unsigned int ArchetypeNickname;        // +0x00 <- argument+0x04
    unsigned int ObjId;        // +0x04 <- argument+0x00
    unsigned int OwnerPlayer;        // +0x08 <- argument+0x44
    unsigned int OwnerId;        // +0x0c <- argument+0x08
    unsigned short SubObjId;      // +0x10 <- argument+0x0c, a word on both sides
    unsigned short Pad_0x12;        // +0x12
    unsigned int OrientWBits;        // +0x14 <- argument+0x1c
    unsigned int OrientXBits;        // +0x18 <- argument+0x20
    unsigned int OrientYBits;        // +0x1c <- argument+0x24
    unsigned int OrientZBits;        // +0x20 <- argument+0x28
    unsigned int PosXBits;        // +0x24 <- argument+0x10
    unsigned int PosYBits;        // +0x28 <- argument+0x14
    unsigned int PosZBits;        // +0x2c <- argument+0x18
    unsigned int VelXBits;        // +0x30 <- argument+0x2c
    unsigned int VelYBits;        // +0x34 <- argument+0x30
    unsigned int VelZBits;        // +0x38 <- argument+0x34
    unsigned int AngVelXBits;        // +0x3c <- argument+0x38
    unsigned int AngVelYBits;        // +0x40 <- argument+0x3c
    unsigned int AngVelZBits;        // +0x44 <- argument+0x40
};
static_assert(sizeof(FPayloadServerCreateCounter) == 0x48, "CREATECOUNTER payload 0x48");

// The argument struct is Server.dll's FServerCreateProjectileInfo (server_createinfo.h),
// the record CREATEMINE sends too.

// ===========================================================================
// ch2 0x37 FLPACKET_SERVER_CREATEGUIDED -- registry 0x58, payload 0x38.
//
// Sender 0x06d8a730 (IClient+0x0c8), receiver caseD_37 at 0x06d81da6. Fourteen moves each
// way, strict inverses, a rotation: the argument's first eight dwords go to payload+0x18..,
// its tail +0x20..+0x34 to payload+0x00..+0x14. The literal 1 travels by reference.
struct FPayloadServerCreateGuided {
    unsigned int OwnerPlayer;        // +0x00 <- argument+0x2c
    unsigned int ArchetypeNickname;        // +0x04 <- argument+0x28
    unsigned int OwnerId;        // +0x08 <- argument+0x30
    unsigned short SubObjId;      // +0x0c <- argument+0x34, a word on both sides
    unsigned short Pad_0x0e;        // +0x0e
    unsigned int TargetId;        // +0x10 <- argument+0x20
    unsigned short TargetSubObjId;      // +0x14 <- argument+0x24, a word on both sides
    unsigned short Pad_0x16;        // +0x16
    unsigned int ObjId;        // +0x18 <- argument+0x00
    unsigned int PosXBits;        // +0x1c <- argument+0x04
    unsigned int PosYBits;        // +0x20 <- argument+0x08
    unsigned int PosZBits;        // +0x24 <- argument+0x0c
    unsigned int OrientWBits;        // +0x28 <- argument+0x10
    unsigned int OrientXBits;        // +0x2c <- argument+0x14
    unsigned int OrientYBits;        // +0x30 <- argument+0x18
    unsigned int OrientZBits;        // +0x34 <- argument+0x1c
};
static_assert(sizeof(FPayloadServerCreateGuided) == 0x38, "CREATEGUIDED payload 0x38");

// The argument struct is FServerCreateGuidedInfo (server_createinfo.h), built by Server.dll's
// FUN_06d07d40.

// ===========================================================================
// ch2 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT -- registry 0x3c, payload 0x1c.
//
// Sender 0x06d8aa40 (IClient+0x0f8), receiver caseD_32 at 0x06d81c7d. The literal 1 travels
// by value here, unlike its neighbours.
//
// The name at argument+0x04 goes through FUN_06d989b0 (operator new(len+1), pointer at
// payload+0x18, count len+1 at payload+0x04); the receiver copies it back into a 0x104-byte
// field, stopping on NUL.
//
// payload+0x14 is a two-value enum remap:
//   sender   argument+0x114 == 1 -> 1, == 2 -> 0 (anything else keeps the ctor's seed, 1)
//   receiver 0 -> argument+0x114 = 2, else 1
struct FPayloadServerRequestSpaceScript {
    unsigned int ObjectId;        // +0x00 <- argument+0x000
    unsigned int NameSize;          // +0x04 = strlen(argument+0x004) + 1
    unsigned int PosXBits;        // +0x08 <- argument+0x108
    unsigned int PosYBits;        // +0x0c <- argument+0x10c
    unsigned int PosZBits;        // +0x10 <- argument+0x110
    unsigned char CameraModeWire;       // +0x14 <- the enum remap above
    unsigned char Pad_0x15[3];      // +0x15
    char* Name;                     // +0x18  owned by the packet
};
static_assert(sizeof(FPayloadServerRequestSpaceScript) == 0x1c, "REQUESTSPACESCRIPT payload 0x1c");

// The argument record (FServerRequestSpaceScriptInfo, 0x118) and kRequestSpaceScriptNameSize
// are in rpc_iclient_args.h; Server.dll builds the record and Freelancer.exe's LAB_0053d9d0
// reads it.

// ===========================================================================
// ch2 0x4e FLPACKET_SERVER_SETDIRECTIVE -- registry 0xb4, payload 0x94.
//
// The payload is IClient slot 100's argument struct verbatim (receiver caseD_4e at
// 0x06d81c3b copies 0x94 bytes). Built by pub::AI::SubmitDirective;
// FPayloadServerSetDirective is in rpc_iclient_args.h.

// ===========================================================================
// The flattened-FmtStr family: ch2 0x29, 0x2b (no FmtStr, but the same relation list),
// 0x34, 0x35 and 0x45.
//
// Retail imports exactly four FmtStr entry points from COMMON.DLL, all declared in
// common_text.h:
//   .rdata:06db30dc  ?flatten@FmtStr@@QBEHPAXI@Z
//   .rdata:06db3008  ?unflatten@FmtStr@@QAEHPAXI@Z
//   .rdata:06db3094  ??1FmtStr@@QAE@XZ
//   .rdata:06db30e0  ?get_flattened_size@Val@FmtStr@@QBEIXZ
//
// Senders that do not call get_flattened_size inline it (0x06d8ade2 for 0x34, 0x06d8afa0 for
// 0x45, 0x06d8b070 and 0x06d8b170 for 0x35, 0x06d8b6d0 for ch1 0x1b): tag 0x4e -> 0x17,
// tag 0x21 -> the recursive walk plus 3, anything else -> 7 -- the same as
// FmtStr::Val::get_flattened_size in common_fmtstr.cpp. Per-FmtStr base is 6: the mad-lib
// id dword plus the ushort node count.

// The 8-byte element the reputation types carry: the wire view of Common.dll's
// Reputation::Relation {GroupId, Feeling}, both dwords moved as raw bits. Server.dll
// allocates the array as operator new(Reputation::group_count() * 8) and fills it through
// Reputation::Vibe::Get.
struct FRpcRelation {
    unsigned int GroupId;        // +0x00  Reputation::Relation::GroupId
    unsigned int FeelingBits;        // +0x04  Reputation::Relation::Feeling, as float bits
};
static_assert(sizeof(FRpcRelation) == 0x08, "reputation relation element is 8 bytes");
static_assert(sizeof(FRpcRelation) == sizeof(::Reputation::Relation),
              "FRpcRelation is the wire view of Reputation::Relation");

// Retail's list node is operator new(0xc) and its value is a pointer to a separately
// allocated 8-byte element -- std::list<Relation*>, not std::list<Relation>. The receiver
// copies the pair out and frees the value straight away.
struct FRpcRelationNode {
    FRpcRelationNode* Next;         // +0x00
    FRpcRelationNode* Prev;         // +0x04
    FRpcRelation* Value;            // +0x08
};
static_assert(sizeof(FRpcRelationNode) == 0x0c,
              "retail relation list node is operator new(0xc)");

struct FRpcRelationList {
    unsigned char Allocator;        // +0x00  VC6 allocator<T>: one byte, never read back
    unsigned char Pad_0x01[3];      // +0x01
    FRpcRelationNode* Head;         // +0x04  dummy node; Next == Prev == itself when empty
    unsigned int Size;              // +0x08
};
static_assert(sizeof(FRpcRelationList) == 0x0c,
              "retail std::list header is 0x0c: allocator, head, size");

// ===========================================================================
// ch2 0x29 FLPACKET_SERVER_SETREPUTATION -- registry 0x68, payload 0x48.
//
// Sender 0x06d88d70 (IClient+0x108), receiver caseD_29 at 0x06d813cf, which calls the same
// slot: a straight relay, the argument struct identical on both sides.
// ===========================================================================
struct FPayloadServerSetReputation {
    unsigned int NameASize;         // +0x00  flattened size of arg+0x18
    void* NameABuffer;              // +0x04
    unsigned int NameBSize;         // +0x08  flattened size of arg+0x30
    void* NameBBuffer;              // +0x0c
    float Attitude;                 // +0x10  <- arg+0x04 ; ctor seeds 0
    // +0x14..+0x24 are the codec's five presence-flag constants, not payload fields: the
    // ctor seeds 1, 2, 4, 8, 0x10, and both codec slots (LAB_06d91ed0 / LAB_06d92350,
    // rpc_wire_ch2.cpp) read these values to test against the mask byte at +0x34.
    unsigned int MaskAffiliation;        // +0x14  ctor seeds 1
    unsigned int MaskNames;        // +0x18  ctor seeds 2
    unsigned int MaskDisplayName;        // +0x1c  ctor seeds 4
    unsigned int MaskRelationsByte;        // +0x20  ctor seeds 8
    unsigned int MaskRelationsWord;        // +0x24  ctor seeds 0x10
    FRpcRelationList Relations;     // +0x28  (FUN_06d92760 / FUN_06d927e0)
    unsigned char Flags;       // +0x34  the presence mask; ctor clears it, the encoder
                                    //        rebuilds it
    unsigned char Pad_0x35[3];      // +0x35
    unsigned int VibeHandle;        // +0x38  <- arg+0x00
    unsigned int Affiliation;        // +0x3c  <- arg+0x08 ; ctor seeds 0xffffffff
    unsigned int DisplayNameLength; // +0x40  FUN_06d92710: wcslen(arg+0x48) + 1, in wchar_t
    wchar_t* DisplayName;           // +0x44  operator new((len+1)*4) -- see the note below
};
static_assert(sizeof(FPayloadServerSetReputation) == 0x48, "SETREPUTATION payload 0x48");

// The argument struct (FServerSetReputationInfo, rpc_iclient_args.h) is built on the stack by
// Server.dll's two call sites, whose trailing wide name is 0x30 bytes in both frames.
//
// Retail quirks, reproduced: the single-player branch never fills +0x04 and the other never
// fills +0x08/+0x0c (each sets only what Reputation::Vibe::Get or ::AttitudeTowards returns),
// yet the sender serializes +0x00, +0x04 and +0x08 regardless. +0x0c is never serialized.

// ===========================================================================
// ch2 0x2b FLPACKET_SERVER_SETGROUPFEELINGS -- registry 0x34, payload 0x14.
//
// Sender 0x06d8a5c0 (IClient+0x148), receiver caseD_2b at 0x06d815bb -> the same slot, a
// straight relay. The name travels as a narrow string through FUN_06d91850; the relation
// list is the same std::list<Relation*>.
// ===========================================================================
struct FPayloadServerSetGroupFeelings {
    char* Name;                     // +0x00  FUN_06d91850: operator new(strlen+1) + strncpy
    unsigned int NameSize;          // +0x04  strlen(arg+0x00) + 1
    FRpcRelationList Relations;     // +0x08  (FUN_06d91ca0 / FUN_06d91d20)
};
static_assert(sizeof(FPayloadServerSetGroupFeelings) == 0x14,
              "SETGROUPFEELINGS payload 0x14");

// FServerSetGroupFeelingsInfo (the slot 82 argument) is in rpc_iclient_args.h.

// ===========================================================================
// ch2 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE -- registry 0x30, payload 0x10.
//
// Sender 0x06d8ad90 (IClient+0x11c), receiver caseD_34 at 0x06d82494 -> the same slot. The
// packet destructor (FUN_06d9d7d0) frees packet+0x2c and nothing else.
// ===========================================================================
struct FPayloadServerReplaceMissionObjective {
    unsigned int MissionId;        // +0x00  <- arg+0x00
    unsigned int State;             // +0x04  zero-extended from the byte at arg+0x20
    unsigned int TextSize;          // +0x08
    void* TextBuffer;               // +0x0c
};
static_assert(sizeof(FPayloadServerReplaceMissionObjective) == 0x10,
              "REPLACEMISSIONOBJECTIVE payload 0x10");

// FServerReplaceMissionObjectiveInfo is in rpc_iclient_args.h (built by
// pub::Player::ReplaceMissionObjective).

// ===========================================================================
// ch2 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE -- registry 0x2c, payload 0x0c.
//
// Sender 0x06d8af60 (IClient+0x118), receiver caseD_45 at 0x06d82623 -> the same slot. The
// packet destructor (FUN_06d9d9e0) frees packet+0x24.
// ===========================================================================
struct FPayloadServerSetMissionMessage {
    unsigned int TextSize;          // +0x00
    void* TextBuffer;               // +0x04
    unsigned int Type;        // +0x08  <- arg+0x18
};
static_assert(sizeof(FPayloadServerSetMissionMessage) == 0x0c,
              "SET_MISSION_MESSAGE payload 0x0c");

// FServerSetMissionMessageInfo == CSetMissionMessage is in rpc_iclient_args.h (built by
// DisplayMissionMessage).

// ===========================================================================
// ch2 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES -- registry 0x48, payload 0x28.
//
// Sender 0x06d8b020 (IClient+0x120), receiver caseD_35 at 0x06d826c9 -> the same slot. Two
// top-level FmtStrs plus a list of per-objective records, each its own operator new(0xc)
// holding one more flattened FmtStr and a state byte.
// ===========================================================================
struct FRpcMissionObjectiveRecord {
    unsigned char State;            // +0x00
    unsigned char Pad_0x01[3];      // +0x01  never written by retail
    unsigned int TextSize;          // +0x04
    void* TextBuffer;               // +0x08
};
static_assert(sizeof(FRpcMissionObjectiveRecord) == 0x0c,
              "per-objective record is operator new(0xc)");

struct FRpcMissionObjectiveNode {
    FRpcMissionObjectiveNode* Next; // +0x00
    FRpcMissionObjectiveNode* Prev; // +0x04
    FRpcMissionObjectiveRecord* Value;  // +0x08  a pointer, not an inline value
};
static_assert(sizeof(FRpcMissionObjectiveNode) == 0x0c,
              "retail objective list node is operator new(0xc)");

struct FRpcMissionObjectiveList {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    FRpcMissionObjectiveNode* Head; // +0x04
    unsigned int Size;              // +0x08
};
static_assert(sizeof(FRpcMissionObjectiveList) == 0x0c,
              "retail std::list header is 0x0c: allocator, head, size");

struct FPayloadServerSetMissionObjectives {
    unsigned int MissionId;        // +0x00  <- arg+0x00
    unsigned int MissionType;        // +0x04  zero-extended from the byte at arg+0x3c
    unsigned int TextASize;         // +0x08  flattened size of arg+0x0c
    void* TextABuffer;              // +0x0c
    unsigned int TextBSize;         // +0x10  flattened size of arg+0x24
    void* TextBBuffer;              // +0x14
    unsigned int Count;             // +0x18  not written directly: FUN_06d8fc10 bumps it on
                                    //        every push_back, in lockstep with the list size
    FRpcMissionObjectiveList Objectives;  // +0x1c
};
static_assert(sizeof(FPayloadServerSetMissionObjectives) == 0x28,
              "SETMISSIONOBJECTIVES payload 0x28");

// FServerMissionObjectiveEntry / FServerSetMissionObjectivesInfo (the slot 72 argument) are
// in rpc_iclient_args.h (built by CPlayerGroup::DelMember / SetMissionObjectives,
// pub::Player::SetMissionObjectives).

// ===========================================================================
// ch2 0x4d FLPACKET_SERVER_LOADHINT -- registry 0x2c, payload 0x0c.
//
// The hint is a CostumeHint (common_goods.h: BaseHint 0x08 with Kind at +0x04, CostumeHint
// 0x3c with Data[13] at +0x08). rpclocal's own vtable at .rdata:06db3740 thunks +0x04 /
// +0x08 / +0x0c to CostumeHint::Serialize / UnSerialize / CleanUp, the order common_goods.h
// declares.
//
// Sender 0x06d8a3c0 (IClient+0x144): Serialize into a stack pointer, FUN_06d90a00 copies the
// bytes into a packet-owned block, packet+0x28 takes BaseHint::Kind, CleanUp releases the
// serializer's buffer.
//
// Receiver caseD_4d at 0x06d81ba3: only Kind == 1 builds a hint (operator new(0x3c), ctor,
// vtable swap, UnSerialize); the slot is called with the literal 1 by reference, then the
// deleting destructor. Kind != 1 still calls the slot, with a null hint. The vtable swap is
// FRpcLocalCostumeHint in rpc_payload_ch2.cpp.
struct FPayloadServerLoadHint {
    void* Buffer;                   // +0x00  FUN_06d90a00: operator new(Size) + the copy
    unsigned int Size;              // +0x04
    unsigned int Kind;              // +0x08  <- BaseHint::Kind at hint+0x04
};
static_assert(sizeof(FPayloadServerLoadHint) == 0x0c, "LOADHINT payload 0x0c");

// ===========================================================================
// ch2 0x55 FLPACKET_SERVER_OBJECTCARGOUPDATE -- registry 0x34, payload 0x14.
//
// The receiver (caseD_55, 0x06d82abd) uses seven EquipDesc entry points, imported by name:
//   .rdata:06db3004 ??0EquipDesc@@QAE@XZ
//   .rdata:06db3088 ?set_id@EquipDesc@@QAEXG@Z
//   .rdata:06db308c ?set_arch_id@...QAEXI@Z
//   .rdata:06db3084 ?set_count@...QAEXH@Z         (retail RVA 0xdbad0)
//   .rdata:06db3080 ?set_status@...QAEXM@Z
//   .rdata:06db3070 ?make_internal@...QAEXXZ
//   .rdata:06db307c ?set_equipped@...QAEX_N@Z
//
// Sender 0x06d8c0f0 (IClient+0x158) reads EquipDesc at Id +0x02, ArchId +0x04, Count +0x14
// (truncated to a word), Health +0x10, bMission +0x18 -- matching common_goods.h. The
// session id is by reference on both sides.
//
// The hardpoint and bEquipped never travel: the receiver uses make_internal() and
// set_equipped(false).
struct FPayloadServerObjectCargoUpdate {
    unsigned int ObjectId;          // +0x00  <- the sender's 2nd dword argument
    unsigned int ArchId;            // +0x04  <- EquipDesc+0x04
    unsigned short EquipId;         // +0x08  <- EquipDesc+0x02
    unsigned short Count;           // +0x0a  <- EquipDesc+0x14, word-truncated
    float Status;                   // +0x0c  <- EquipDesc+0x10
    unsigned char bMission;         // +0x10  <- EquipDesc+0x18
    unsigned char Pad_0x11[3];      // +0x11  neither end touches these
};
static_assert(sizeof(FPayloadServerObjectCargoUpdate) == 0x14,
              "OBJECTCARGOUPDATE payload 0x14");

// ===========================================================================
// The 0x34 "four dwords, eight ids, a count" block
//
// The same 13-dword struct (a Costume) appears in two places:
//   * ch2 0x4f SENDCOMM passes it by value: the sender copies 13 dwords off its argument
//     list and lays them at packet+0x2c; the receiver's accessor FUN_06d841c0 copies 13
//     dwords back out.
//   * ch2 0x03 CHARACTERINFO holds two inline in its 0x2dc record, at +0x274 and +0x2a8
//     (the record ends exactly on the second).
// The array capacity is 8 in both: (count offset - array offset) / 4.
// ===========================================================================
struct FRpcIdArrayBlock {
    unsigned int Head;        // +0x00
    unsigned int Body;        // +0x04
    unsigned int LeftHand;        // +0x08
    unsigned int RightHand;        // +0x0c
    unsigned int Ids[8];            // +0x10 .. +0x2f
    int Count;                      // +0x30
};
static_assert(sizeof(FRpcIdArrayBlock) == 0x34,
              "the id block is 13 dwords: four fields, eight ids and a count");

// ch2 0x4f FLPACKET_SERVER_SENDCOMM -- registry 0x78, payload 0x58.
//
// Sender 0x06d88fc0 (IClient+0x110), 23 dword arguments A0..A22 (A0 the session id, which
// never travels); receiver caseD_4f at 0x06d8306c reaches every field through a
// one-instruction accessor:
//
//   A1  packet+0x20  FUN_06d841a0
//   A2  packet+0x24  FUN_06d90a40
//   A3  packet+0x28  FUN_06d841b0
//   A4..A16          FUN_06d841c0  the 13-dword block at packet+0x2c
//   A17 packet+0x60  FUN_06d841e0
//   A18 packet+0x64  FUN_06d841f0
//   A19 packet+0x68  FUN_06d84200  signed byte
//   A20 packet+0x6c  FUN_06d84210
//   A21 packet+0x70  FUN_06d84220  float
//   A22              not read -- caseD_4f pushes 0
//
// Retail facts reproduced:
//   * BufferCount is a byte on the wire though the sender takes a dword, and the reader
//     sign-extends it: a count of 0x80..0xff arrives negative at the slot.
//   * bReceiverIsOwnShip (+0x54) is never read by the delivery path, but the wire codec
//     pair (rpc_wire_ch2.cpp) uses it as the discriminator for the whole id block: when it
//     is zero the encoder skips payload+0x0c..+0x3c and the decoder zeroes it. The
//     constructor FUN_06d9eab0 defaults it to 1.
struct FPayloadServerSendComm {
    unsigned int SenderId;        // +0x00 <- A1
    unsigned int ReceiverId;        // +0x04 <- A2
    unsigned int VoiceId;        // +0x08 <- A3
    FRpcIdArrayBlock Body;          // +0x0c .. +0x3f <- A4..A16
    unsigned int CaptionIds;        // +0x40 <- A17
    unsigned int* Buffer;           // +0x44    operator new(A19 << 2), copied from A18
    unsigned char BufferCount;      // +0x48 <- LOW BYTE of A19
    unsigned char Pad_0x49[3];      // +0x49
    unsigned int PermutationIds;        // +0x4c <- A20
    // A float, moved as its raw bits (FLD/FSTP in retail).
    unsigned int DurationBits;    // +0x50 <- A21
    unsigned char bReceiverIsOwnShip;       // +0x54 <- LOW BYTE of A22; never read back
    unsigned char Pad_0x55[3];      // +0x55
};
static_assert(sizeof(FPayloadServerSendComm) == 0x58, "SENDCOMM payload 0x58");

// The channel-2 codecs live in rpc_payload_ch2.cpp. The payloads of 0x03 CHARACTERINFO,
// 0x04 CREATESHIP and 0x42 CREATESOLAR are declared in rpc_containers.h, next to the
// container mirrors they are built from.
extern const FRpcPayloadCodec GChannel2Codecs[];
extern const unsigned int GChannel2CodecCount;

// ===========================================================================
// Channel-3 payload structures (client -> server, the IServer direction).
//
// Read from the sender (IServerImpl slot body, FUN_06d96300(Serializer, 3, Opcode,
// &OutPacket)), the receiver (the channel-3 case of FUN_06d86a70, which calls one slot of
// the real IServer, .data:DAT_06dbbd24) and the registry size; the IServer signatures in
// common_server.h agree with every recovered field order. Where the sender permutes its
// argument struct (OBJCOLLISION, BADLANDSOBJCOLLISION, REQMODIFYITEM, REQINVINCIBILITY,
// CREATENEWCHAR) the receiver is the strict inverse.
//
// Argument order: the session id is usually the last IServer argument, never serialized;
// the receiver supplies the connection id the pump passes in. A few methods take the session
// first (InterfaceItemUsed, RTCDone, AbortMission, MissionSaveB, SetManeuver); for those
// retail serializes the second argument.
//
// Marshal() argument convention for channel 3: Args[i] is the i-th dword argument
// after this, in the order src/include/common_server.h declares it.
// ===========================================================================

// --- generic shapes, reused by many opcodes -------------------------------

// payload 0x04. ch3 0x04, 0x07, 0x0b, 0x0d, 0x0e, 0x0f, 0x13, 0x15, 0x1b, 0x1c, 0x24,
// 0x35, 0x3b, 0x41, 0x45, 0x46, 0x47.
struct FPayloadClientDword {
    unsigned int Field_0x00;
};
static_assert(sizeof(FPayloadClientDword) == 0x04, "ch3 single-dword payload 0x04");

// payload 0x08. ch3 0x19, 0x2b, 0x3e, 0x43.
struct FPayloadClientDwordPair {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
};
static_assert(sizeof(FPayloadClientDwordPair) == 0x08, "ch3 dword-pair payload 0x08");

// payload 0x0c. ch3 0x11 GFGOODSELL, 0x3c GFGOODVAPORIZED, 0x2f SETMANEUVER, 0x3f HAIL.
struct FPayloadClientDwordTriple {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
};
static_assert(sizeof(FPayloadClientDwordTriple) == 0x0c, "ch3 dword-triple payload 0x0c");

// payload 0x10. ch3 0x12 GFGOODBUY, 0x37 COMMCOMPLETE.
struct FPayloadClientDwordQuad {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned int Field_0x0c;
};
static_assert(sizeof(FPayloadClientDwordQuad) == 0x10, "ch3 dword-quad payload 0x10");

// --- per-opcode views of the generic shapes --------------------------------
// The wire codecs keep the shapes above; the marshallers and receivers use these. Names
// come from freelancer.exe's callers.

// ch3 0x19 REQREMOVEITEM: IServer::ReqRemoveItem(ushort, int, uint), the equip id
// zero-extended into the first dword.
struct FPayloadClientReqRemoveItem {
    unsigned int EquipId;         // +0x00  read back as a word
    unsigned int Count;           // +0x04
};
static_assert(sizeof(FPayloadClientReqRemoveItem) == sizeof(FPayloadClientDwordPair),
              "REQREMOVEITEM payload 0x08");

// ch3 0x2b CARGOSCAN: IServer::SPScanCargo(uint const&, uint const&, uint).
struct FPayloadClientScanCargo {
    unsigned int ScannerId;       // +0x00
    unsigned int TargetId;        // +0x04
};
static_assert(sizeof(FPayloadClientScanCargo) == sizeof(FPayloadClientDwordPair),
              "CARGOSCAN payload 0x08");

// ch3 0x3e LAUNCHCOMPLETE: Common's LaunchAction hands the ship IObjAffect slot 28 the base
// object's id; freelancer.exe's LAB_005471f0 forwards it with the ship's own id.
struct FPayloadClientLaunchComplete {
    unsigned int BaseObjId;       // +0x00
    unsigned int ShipId;          // +0x04
};
static_assert(sizeof(FPayloadClientLaunchComplete) == sizeof(FPayloadClientDwordPair),
              "LAUNCHCOMPLETE payload 0x08");

// ch3 0x43 JUMPINCOMPLETE: Common's JumpInAction hands IObjAffect slot 29 the arrival system's
// id; freelancer.exe's LAB_00547220 forwards it with the ship's own id.
struct FPayloadClientJumpInComplete {
    unsigned int SystemId;        // +0x00
    unsigned int ShipId;          // +0x04
};
static_assert(sizeof(FPayloadClientJumpInComplete) == sizeof(FPayloadClientDwordPair),
              "JUMPINCOMPLETE payload 0x08");

// ch3 0x3f HAIL: the target panel's comm button sends (own ship, hailed object, own system).
struct FPayloadClientHail {
    unsigned int ShipId;          // +0x00
    unsigned int TargetId;        // +0x04
    unsigned int SystemId;        // +0x08
};
static_assert(sizeof(FPayloadClientHail) == sizeof(FPayloadClientDwordTriple),
              "HAIL payload 0x0c");

// ch3 0x37 COMMCOMPLETE: the comm HUD reports (sender, receiver, last line's id, result).
struct FPayloadClientCommComplete {
    unsigned int SenderId;        // +0x00
    unsigned int ReceiverId;      // +0x04
    unsigned int MsgId;           // +0x08
    unsigned int Result;          // +0x0c  CommResult
};
static_assert(sizeof(FPayloadClientCommComplete) == sizeof(FPayloadClientDwordQuad),
              "COMMCOMPLETE payload 0x10");

// --- per-opcode shapes ----------------------------------------------------

// ch3 0x01 FLPACKET_CLIENT_LOGIN, packet size 0x424 -> payload 0x404.
// Sender IServerImpl::Login 0x06d8cc70, receiver caseD_1 of FUN_06d86a70.
//
// The sender wcscpy's SLoginInfo+0x00 and SLoginInfo+0xa0 into two wchar_t[256] stack
// buffers, puts the byte at SLoginInfo+0xc0 behind them and copies 0x402 bytes into the
// packet. The receiver wcsncpy's them into a file-static SLoginInfo at .data:0x06dbba38
// (caps 0x28 and 0x10 wide characters), NUL-terminates both (+0x4e, +0xbe) and stores the
// flag byte at +0xc0.
//
// Deviation: retail copies the whole stack window, so bytes past each string's NUL are
// uninitialised; the marshal zeroes the payload first.
struct FPayloadClientLogin {
    wchar_t AccountName[0x100];   // +0x000  wcscpy'd, receiver re-caps it at 0x28 wchars
    wchar_t Password[0x100];      // +0x200  wcscpy'd from SLoginInfo+0xa0, re-capped at 0x10
    unsigned char CreateFlag;     // +0x400  <- SLoginInfo+0xc0
    unsigned char Pad_0x401[3];
};
static_assert(sizeof(FPayloadClientLogin) == 0x404, "LOGIN payload 0x404");

// ch3 0x03 FLPACKET_CLIENT_MUNCOLLISION, packet size 0x3c -> payload 0x1c.
// Sender IServerImpl::SPMunitionCollision 0x06d8d6e0, receiver caseD_3.
// Identity mapping of SSPMunitionCollisionInfo; the receiver rebuilds the struct
// field by field on its own stack and calls IServer+0xd4 (slot 53) with it.
struct FPayloadClientMunCollision {
    unsigned int ProjectileArchId;
    unsigned int AttackerId;
    unsigned int ObjectId;        // +0x08
    unsigned short SubObjId;    // +0x0c  the only 2-byte write on either side
    unsigned short Pad_0x0e;
    unsigned int HitPosX;
    unsigned int HitPosY;
    unsigned int HitPosZ;
};
static_assert(sizeof(FPayloadClientMunCollision) == 0x1c, "MUNCOLLISION payload 0x1c");

// ch3 0x06 FLPACKET_CLIENT_SELECTCHARACTER, size 0x220 -> payload 0x200, and
// ch3 0x3a FLPACKET_CLIENT_DESTROYCHAR, size 0x220 -> payload 0x200.
// Senders 0x06d8cd60 / 0x06d8ce90 do CHARACTER_ID::operator=(packet+0x20, arg); the
// receiver passes packet+0x20 to IServer+0x50 / +0x5c. The payload is a CHARACTER_ID (0x200,
// static_assert in common_server_types.h), so no wire struct is declared.

// ch3 0x08 FLPACKET_CLIENT_REQUESTBASEINFO and 0x09 REQUESTLOCATIONINFO,
// size 0x28 -> payload 0x08. Senders 0x06d8d500 / 0x06d8d550.
// The session id (2nd argument) is dropped; the bool 3rd argument travels as a byte.
struct FPayloadClientInfoRequest {
    unsigned int Id;              // +0x00  base / location id
    unsigned char bFull;          // +0x04
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FPayloadClientInfoRequest) == 0x08, "REQUEST*INFO payload 0x08");

// ch3 0x0c FLPACKET_CLIENT_OBJCOLLISION, size 0x34 -> payload 0x14.
// Sender IServerImpl::SPObjCollision 0x06d8d7b0, receiver caseD_c.
// The sender swaps the two halves of SSPObjCollisionInfo, so the object's own id leads on
// the wire; the receiver swaps them back.
struct FPayloadClientObjCollision {
    unsigned int ObjectId;          // +0x00  <- SSPObjCollisionInfo+0x08
    unsigned short SubObjId;        // +0x04  <- +0x0c
    unsigned short Pad_0x06;
    unsigned int TargetObjectId;    // +0x08  <- +0x00
    unsigned short TargetSubObjId;  // +0x0c  <- +0x04
    unsigned short Pad_0x0e;
    unsigned int DamageBits;        // +0x10  <- +0x10
};
static_assert(sizeof(FPayloadClientObjCollision) == 0x14, "OBJCOLLISION payload 0x14");

// ch3 0x14 FLPACKET_CLIENT_MISSIONRESPONSE, size 0x2c -> payload 0x0c.
// Sender IServerImpl::MissionResponse 0x06d8da80, receiver caseD_14.
struct FPayloadClientMissionResponse {
    unsigned int OfferId;         // +0x00  <- 1st argument
    unsigned int OfferKind;       // +0x04  <- 2nd argument (ulong)
    unsigned char bAccepted;      // +0x08  <- 3rd argument
    unsigned char Pad_0x09[3];
};
static_assert(sizeof(FPayloadClientMissionResponse) == 0x0c, "MISSIONRESPONSE payload 0x0c");

// ch3 0x18 FLPACKET_CLIENT_REQADDITEM, size 0x38 -> payload 0x18.
// Sender IServerImpl::ReqAddItem 0x06d8d230. The hardpoint string goes through the block
// writer FUN_06d9a160 (block at payload+0x14, length at payload+0x08); the packet owns the
// copy. Receiver caseD_18 calls IServer+0x78 (slot 30) as
//   ReqAddItem(payload+0x00, payload+0x14, payload+0x04, payload+0x0c, payload+0x10, Session).
struct FPayloadClientReqAddItem {
    unsigned int ArchId;          // +0x00
    int Count;                    // +0x04
    unsigned int HardpointSize;   // +0x08  written by FUN_06d9a160
    float Health;                 // +0x0c
    unsigned char bMounted;       // +0x10
    unsigned char Pad_0x11[3];
    char* Hardpoint;              // +0x14  packet-owned copy
};
static_assert(sizeof(FPayloadClientReqAddItem) == 0x18, "REQADDITEM payload 0x18");

// ch3 0x1a FLPACKET_CLIENT_REQMODIFYITEM, size 0x38 -> payload 0x18.
// Sender IServerImpl::ReqModifyItem 0x06d8d300, block writer FUN_06d9a5f0 (length at
// payload+0x04). The first three fields are in a different order from REQADDITEM: Count
// leads, the equip id sits at +0x08.
struct FPayloadClientReqModifyItem {
    int Count;                    // +0x00
    unsigned int HardpointSize;   // +0x04  written by FUN_06d9a5f0
    unsigned int EquipId;         // +0x08  zero-extended from a ushort, read back as a word
    float Health;                 // +0x0c
    unsigned char bMounted;       // +0x10
    unsigned char Pad_0x11[3];
    char* Hardpoint;              // +0x14  packet-owned copy
};
static_assert(sizeof(FPayloadClientReqModifyItem) == 0x18, "REQMODIFYITEM payload 0x18");

// ch3 0x1e FLPACKET_CLIENT_SAVEGAME, size 0x228 -> payload 0x208.
// Sender IServerImpl::SaveGame 0x06d8db10: CHARACTER_ID::operator= into packet+0x20, then
// FUN_06d9aab0 copies count*2 bytes, block at payload+0x200, wide-character count at
// payload+0x204. Receiver caseD_1e calls IServer+0xe8 (slot 58) and never reads the count.
struct FPayloadClientSaveGame {
    unsigned char CharId[0x200];      // +0x000  a CHARACTER_ID
    wchar_t* Description;             // +0x200  packet-owned wide string copy
    unsigned int DescriptionCount;    // +0x204  in wide characters, including the NUL
};
static_assert(sizeof(FPayloadClientSaveGame) == 0x208, "SAVEGAME payload 0x208");

// ch3 0x20 FLPACKET_CLIENT_MINEASTEROID, size 0x3c -> payload 0x1c.
// Sender IServerImpl::MineAsteroid 0x06d8dcf0 flattens the Vector inline; receiver
// caseD_20 rebuilds it on its own stack and calls IServer+0xf8 (slot 62).
struct FPayloadClientMineAsteroid {
    unsigned int SystemId;        // +0x00
    float PositionX;              // +0x04  <- Vector+0x00
    float PositionY;              // +0x08
    float PositionZ;              // +0x0c
    unsigned int LootId;          // +0x10
    unsigned int SecondaryLoot;   // +0x14
    unsigned int Quantity;        // +0x18
};
static_assert(sizeof(FPayloadClientMineAsteroid) == 0x1c, "MINEASTEROID payload 0x1c");

// ===========================================================================
// ch3 0x22 FLPACKET_CLIENT_DBGCREATESHIP, size 0x90 -> payload 0x70.
//
// Size from the factory FUN_06d9f680 (operator new(0x90)); the highest offset either codec
// touches is packet+0x8c, a dword. Layout from the encoder LAB_06db0560 (vtable +0x04), the
// decoder LAB_06db0b30 (+0x08, its strict inverse), the ctor FUN_06da0e20 and the dtor
// FUN_06db0500, which frees exactly the four packet-owned blocks (packet+0x60, +0x84, +0x88,
// +0x8c).
//
// Ctor defaults: payload+0x14..+0x28 = 0, +0x2c = 1.0f (quaternion w, identity),
// +0x30..+0x6c = 0; payload+0x00..+0x10 are left uninitialised by retail.
// FPacket_FLPACKET_CLIENT_DBGCREATESHIP in rpc_packet_registry.cpp reproduces it.
//
// +0x14 and +0x20 go through the 3-dword copier FUN_06d8f510 (Vector), +0x2c through the
// 4-dword copier FUN_06d8f4f0 (Quaternion). Four variable-length blocks, element widths from
// the codec arithmetic:
//   * count +0x44 -> block +0x40, 2 bytes per element (a wide string, count in characters)
//   * count +0x48 -> block +0x68, 1 byte
//   * count +0x4c -> block +0x6c, 1 byte
//   * count +0x60 -> block +0x64, 4 bytes (a dword array)
// Counts and pointers are not adjacent for three of the four (retail's field order).
//
// The type is registered but never built in release (the ch3 dispatcher answers it with an
// FDUMP), so the remaining dwords have only codec-proven widths and keep Field_0xNN names.
struct FPayloadClientDbgCreateShip {
    unsigned int Field_0x00;        // +0x00  dword
    unsigned int Field_0x04;        // +0x04  dword
    unsigned int Field_0x08;        // +0x08  dword
    unsigned int Field_0x0c;        // +0x0c  dword
    unsigned int Field_0x10;        // +0x10  dword
    Vector Field_0x14;              // +0x14  FUN_06d8f510
    Vector Field_0x20;              // +0x20  FUN_06d8f510
    Quaternion Orientation;         // +0x2c  FUN_06d8f4f0, ctor default {1,0,0,0}
    unsigned int Field_0x3c;        // +0x3c  dword
    wchar_t* NameChars;             // +0x40  packet-owned
    unsigned int NameCharCount;     // +0x44  in wide characters
    unsigned int BlockASize;        // +0x48  in bytes
    unsigned int BlockBSize;        // +0x4c  in bytes
    unsigned int Field_0x50;        // +0x50  dword
    unsigned int Field_0x54;        // +0x54  dword
    unsigned int Field_0x58;        // +0x58  dword
    unsigned int Field_0x5c;        // +0x5c  dword
    unsigned int DwordArrayCount;   // +0x60  element count, not a byte count
    unsigned int* DwordArray;       // +0x64  packet-owned
    void* BlockA;                   // +0x68  packet-owned
    void* BlockB;                   // +0x6c  packet-owned
};
static_assert(sizeof(FPayloadClientDbgCreateShip) == 0x70, "DBGCREATESHIP payload 0x70");

// ch3 0x25 FLPACKET_CLIENT_DBGDESTROYOBJECT, size 0x28 -> payload 0x08.
// No sender in release (only the ctor FUN_06da0f90 writes 0x30025; the dispatcher answers
// with an FDUMP). The codec pair LAB_06db12c0 / LAB_06db13e0 touches a dword at +0x00 and a
// word at +0x04 and nothing else; meanings are unknown.
struct FPayloadClientDbgDestroyObject {
    unsigned int Field_0x00;      // +0x00  dword
    unsigned short Field_0x04;    // +0x04  word
    unsigned short Pad_0x06;      // +0x06  neither direction touches it
};
static_assert(sizeof(FPayloadClientDbgDestroyObject) == 0x08, "DBGDESTROYOBJECT payload 0x08");

// ch3 0x27 FLPACKET_CLIENT_TRADERESPONSE, size 0x28 -> payload 0x08.
// Sender IServerImpl::TradeResponse 0x06d8dad0 uses FUN_06d97b00, channel 2's blob writer:
// length at payload+0x00, owned copy at payload+0x04. Receiver caseD_27 calls IServer+0xb0
// (slot 44) as
// TradeResponse(payload+0x04, payload+0x00, Session).
struct FPayloadClientBlob {
    unsigned int Size;            // +0x00
    void* Buffer;                 // +0x04  packet-owned copy
};
static_assert(sizeof(FPayloadClientBlob) == 0x08, "TRADERESPONSE payload 0x08");

// ch3 0x2d FLPACKET_CLIENT_DBGCONSOLE, size 0xa4 -> payload 0x84.
// Encoder LAB_06db1670, decoder LAB_06db17a0 (rpc_wire_fields.cpp). The encoder reserves 4
// bytes for the leading dword and 0x80 for the run at payload+0x04; 4 + 128 == 0x84.
//
// The 128-byte run is copied whole, not as a NUL-terminated string (no length prefix either
// way), so bytes past the terminator go out as they sit in the packet.
//
// Debug-only type with no sender in rpclocal, so nothing fixes the dword's meaning.
struct FPayloadClientDbgConsole {
    unsigned int Field_0x00;      // +0x00  TODO: name unknown, no sender in rpclocal
    char Text_0x04[128];          // +0x04  fixed 128-byte run, sent in full
};
static_assert(sizeof(FPayloadClientDbgConsole) == 0x84, "DBGCONSOLE payload 0x84");

// ch3 0x31 FLPACKET_CLIENT_REQUEST_EVENT, size 0x34 -> payload 0x14.
// Sender IServerImpl::RequestEvent 0x06d8eaf0. Retail truncates the int first argument and
// the ulong fifth argument to a byte each on the wire (a retail information loss). The
// fields are IObjAffect::request_event's arguments: freelancer.exe's LAB_00546df0 sends the
// ship's own id, *TargetId, *SecondId and bFlag; Server.dll passes them on to the ship.
struct FPayloadClientRequestEvent {
    unsigned char EventType;      // +0x00  <- int, truncated
    unsigned char Pad_0x01[3];
    unsigned int ShipId;
    unsigned int TargetId;
    unsigned int SecondId;
    unsigned char bFlag;          // +0x10  <- ulong, truncated
    unsigned char Pad_0x11[3];
};
static_assert(sizeof(FPayloadClientRequestEvent) == 0x14, "REQUEST_EVENT payload 0x14");

// ch3 0x32 FLPACKET_CLIENT_REQUEST_CANCEL, size 0x30 -> payload 0x10.
// Sender IServerImpl::RequestCancel 0x06d8eb60, same truncation habit.
struct FPayloadClientRequestCancel {
    unsigned char EventType;      // +0x00  <- int, truncated
    unsigned char Pad_0x01[3];
    unsigned int ShipId;
    unsigned int TargetId;
    unsigned char Field_0x0c;     // +0x0c  <- ulong, truncated
    unsigned char Pad_0x0d[3];
};
static_assert(sizeof(FPayloadClientRequestCancel) == 0x10, "REQUEST_CANCEL payload 0x10");

// ch3 0x39 FLPACKET_CLIENT_CREATENEWCHAR, size 0xb0 -> payload 0x90.
// Sender IServerImpl::CreateNewCharacter 0x06d8cdf0: wcsncpy(scratch, Info->Name, 0x40),
// zero the last wide character, the four dwords of SCreateCharacterInfo+0x30..+0x3c at
// scratch+0x80, then 0x24 dwords into the packet. Receiver caseD_39 wcscpy's the name into a
// file-static SCreateCharacterInfo at .data:0x06dbbb10 and restores the four dwords.
//
// The wire name window is 0x80 bytes while SCreateCharacterInfo::Name is 0x30 (retail's
// wcsncpy count exceeds the field; it stops at the NUL and pads).
struct FPayloadClientCreateNewChar {
    wchar_t Name[0x40];           // +0x00 .. +0x7f
    unsigned int FactionId;      // +0x80  <- SCreateCharacterInfo+0x30
    unsigned int BaseId;      // +0x84  <- +0x34
    unsigned int PackageId;      // +0x88  <- +0x38
    unsigned int PilotId;      // +0x8c  <- +0x3c
};
static_assert(sizeof(FPayloadClientCreateNewChar) == 0x90, "CREATENEWCHAR payload 0x90");

// ch3 0x3d FLPACKET_CLIENT_BADLANDSOBJCOLLISION, size 0x2c -> payload 0x0c.
// Sender IServerImpl::SPBadLandsObjCollision 0x06d8d760 rotates the three fields of
// SSPBadLandsObjCollisionInfo; receiver caseD_3d rotates them back.
struct FPayloadClientBadLandsObjCollision {
    unsigned int ObjectId;        // +0x00  <- SSPBadLandsObjCollisionInfo+0x04
    unsigned short SubObjId;    // +0x04  <- +0x08
    unsigned short Pad_0x06;
    unsigned int BadLandsObjId;      // +0x08  <- +0x00
};
static_assert(sizeof(FPayloadClientBadLandsObjCollision) == 0x0c, "BADLANDSOBJCOLLISION payload 0x0c");

// ch3 0x40 FLPACKET_CLIENT_REQUEST_USE_ITEM, size 0x28 -> payload 0x08.
// Sender IServerImpl::SPRequestUseItem 0x06d8ec20, identity mapping of SSPUseItem.
struct FPayloadClientUseItem {
    unsigned int ObjectId;        // +0x00
    unsigned short ItemId;        // +0x04
    unsigned short Count;         // +0x06
};
static_assert(sizeof(FPayloadClientUseItem) == 0x08, "REQUEST_USE_ITEM payload 0x08");

// ch3 0x44 FLPACKET_CLIENT_REQINVINCIBILITY, size 0x28 -> payload 0x08.
// Sender IServerImpl::SPRequestInvincibility 0x06d8ec80 writes the reason at +0x04 and the
// bool at +0x05, the reverse of the argument order; receiver caseD_44 crosses them back.
struct FPayloadClientInvincibility {
    unsigned int ShipId;          // +0x00
    unsigned char Reason;         // +0x04  InvincibilityReason, one byte on the wire
    unsigned char bInvincible;    // +0x05
    unsigned char Pad_0x06[2];
};
static_assert(sizeof(FPayloadClientInvincibility) == 0x08, "REQINVINCIBILITY payload 0x08");

// ===========================================================================
// Channel-3 codec table. The deliver side calls the real IServer inside Server.dll, not
// IClient, so it has its own signature.
// ===========================================================================
struct IServer;
class IChatServer;

// The interfaces Server.dll handed back: .data:0x06dbbd24 (IServer, an alias of 0x06dbbd5c)
// and .data:0x06dbbd20 (IChatServer, from 0x06dbbd60). IServerImpl::Startup publishes both,
// Shutdown clears them, FUN_06d86a70 is their only reader.
extern IServer* GRealServer;
extern IChatServer* GRealChatServer;

// Rebuilds the retail argument list out of the payload and makes the single IServer
// call that FUN_06d86a70's matching case makes. SessionId is the dispatcher's own
// param_1, which retail substitutes wherever the session id was dropped.
typedef bool (*FRpcServerDeliverFn)(const FPacketBase* Packet, IServer* Server, unsigned int SessionId);

struct FRpcServerCodec {
    unsigned short Channel;
    unsigned short Opcode;
    const char* Name;              // the retail FLPACKET_* string
    unsigned short PayloadSize;    // registry size minus the 0x20 base
    FRpcMarshalFn Marshal;         // nullptr for the debug types retail never sends
    FRpcServerDeliverFn Deliver;
};

// The channel-3 codecs live in src/rpclocal/rpc_payload_ch3.cpp.
extern const FRpcServerCodec GChannel3Codecs[];
extern const unsigned int GChannel3CodecCount;

// nullptr for a channel/opcode with no channel-3 codec (only 0x0a GFREQUESTSHIPINFO).
const FRpcServerCodec* RpcFindServerCodec(unsigned int Channel, unsigned int Opcode);

// The port of retail FUN_06d86a70: the client-side dispatcher for channels 1 and 3.
// Returns true when the packet was routed into a real IServer slot.
bool RpcDispatchToServer(FPacketBase* Packet, unsigned int SessionId);

// Bodies of the exported non-virtual members IServerImpl::HandleCharacterInfo
// (0x06d84490, ch2 0x03) and IServerImpl::HandleDamageObject (0x06d84c60, ch2 0x05).
// Arg is the value both pass as the IClient slot's first argument. Defined in
// rpc_payload_ch2.cpp beside their codecs.
__declspec(dllexport) bool RpcHandleCharacterInfo(const FPacketBase* Packet, IClient* Client, unsigned int Arg);
__declspec(dllexport) bool RpcHandleDamageObject(const FPacketBase* Packet, IClient* Client, unsigned int Arg);
