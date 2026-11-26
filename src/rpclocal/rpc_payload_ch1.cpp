// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The 21 channel-1 packet types, marshalled and delivered the way retail does it.
//
// Channel 1 is the "common" channel: the same 21 packet types travel in BOTH directions,
// so every layout has two independent readings -- rpclocal's IClient slot (server ->
// client, the direction this file implements) and its IServerImpl method (client ->
// server). Where a documented struct exists the two agree field for field.
//
// Every Deliver() below reproduces its retail HandlePacket case, including the literal 1
// retail pushes in place of the session id. Case labels are those inside the channel-1
// switch at 0x06d83256.

#include "rpc_payloads.h"
#include "rpc_packet_classes.h"
#include "rpc_iclient.h"
#include "rpc_containers.h"
#include "common_goods.h"

#include <new>

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Copies InSize bytes into a block the packet then owns. Retail does the same with an
// untested operator new (e.g. 0x06d8c4aa for CHATMSG) -- a zero size included, which still
// hands back a non-null zero-byte block. Where retail tests the source or the size first
// (CHATMSG), the caller carries that test.
static void* RpcCopyIntoPacketBlock(const void* Source, unsigned int InSize) {
    unsigned char* Block = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < InSize; ++i) {
        Block[i] = Src[i];
    }
    return Block;
}

static void RpcFreePacketBlock(void* Block) {
    if (Block != nullptr) {
        ::operator delete(Block);
    }
}

// ===========================================================================
// ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT
// Sender 0x06d897c0 (IClient+0x0d8, RET 8), receiver caseD_1 at 0x06d83a4a.
//   payload+0x00 = byte  arg+0x28        payload+0x04 = dword arg+0x00
//   payload+0x08 = Vector arg+0x14 (FUN_06d8f510)
//   payload+0x14 = Quaternion arg+0x04 (FUN_06d8f4f0)
//   payload+0x24 = dword arg+0x20        payload+0x28 = dword arg+0x24
// ===========================================================================
static bool MarshalUpdateObject(FPacket_FLPACKET_COMMON_UPDATEOBJECT& Packet,
                                const unsigned int* Args, unsigned int ArgCount) {
    const FCommonUpdateObjectInfo* Info =
        reinterpret_cast<const FCommonUpdateObjectInfo*>(Args[1]);
    FPayloadCommonUpdateObject Wire;
    Wire.State = Info->State;
    Wire.Pad_0x01[0] = 0;
    Wire.Pad_0x01[1] = 0;
    Wire.Pad_0x01[2] = 0;
    Wire.ObjectId = Info->ObjectId;
    Wire.Position = Info->Position;
    Wire.Orientation = Info->Orientation;
    Wire.Timestamp = Info->Timestamp;
    Wire.Throttle = Info->Throttle;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverUpdateObject(const FPacket_FLPACKET_COMMON_UPDATEOBJECT& Packet, IClient* Client,
                                IChatClient*) {
    const FPayloadCommonUpdateObject& Wire = Packet.Wire;
    FCommonUpdateObjectInfo Info;
    Info.ObjectId = Wire.ObjectId;
    Info.Orientation = Wire.Orientation;
    Info.Position = Wire.Position;
    Info.Timestamp = Wire.Timestamp;
    Info.Throttle = Wire.Throttle;
    Info.State = Wire.State;
    Info.Pad_0x29[0] = 0;
    Info.Pad_0x29[1] = 0;
    Info.Pad_0x29[2] = 0;
    Client->SendCommonUpdateObject(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x02 FLPACKET_COMMON_FIREWEAPON
// Sender 0x06d8b310 (IClient+0x000, RET 8), receiver case 2.
// Argument struct = XFireWeaponInfo (see common_server_types.h).
// ===========================================================================
static bool MarshalFireWeapon(FPacket_FLPACKET_COMMON_FIREWEAPON& Packet, const unsigned int* Args,
                              unsigned int ArgCount) {
    const XFireWeaponInfo* Info = reinterpret_cast<const XFireWeaponInfo*>(Args[1]);
    FPayloadCommonFireWeapon Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.TargetPos = Info->TargetPos;
    Wire.Hardpoints.AllocPad = 0;
    Wire.Hardpoints.Pad_0x01[0] = 0;
    Wire.Hardpoints.Pad_0x01[1] = 0;
    Wire.Hardpoints.Pad_0x01[2] = 0;
    Wire.Hardpoints.Begin = nullptr;
    Wire.Hardpoints.End = nullptr;
    Wire.Hardpoints.Cap = nullptr;

    // Retail's `(last - first) >> 1` element count, then a word-by-word copy into a block
    // the packet keeps. std::vector::size() is the same arithmetic, data() its _First.
    const unsigned int Count = static_cast<unsigned int>(Info->Hardpoints.size());
    if (Count != 0) {
        // The inlined vector operator=: an empty destination gets an untested allocation
        // and the elements copied in.
        unsigned short* Block = static_cast<unsigned short*>(
            RpcCopyIntoPacketBlock(Info->Hardpoints.data(), Count * 2u));
        Wire.Hardpoints.Begin = Block;
        Wire.Hardpoints.End = Block + Count;
        Wire.Hardpoints.Cap = Block + Count;
    }
    return RpcPayloadWrite(Packet.Wire, Wire);
}

// caseD_2 0x06d833c9: a local XFireWeaponInfo; +0x00..+0x0c copied from the payload, then
// the inlined vector operator= from payload+0x10 (an empty source leaves the local empty,
// otherwise one block of Count*2 bytes holds a COPY). IClient slot 0 gets (1, &local);
// the local vector's destructor frees the copy.
static bool DeliverFireWeapon(const FPacket_FLPACKET_COMMON_FIREWEAPON& Packet, IClient* Client,
                              IChatClient*) {
    const FPayloadCommonFireWeapon& Wire = Packet.Wire;
    XFireWeaponInfo Info;
    Info.ObjectId = Wire.ObjectId;
    Info.TargetPos = Wire.TargetPos;
    const unsigned int Count = Wire.Hardpoints.Count();
    if (Count != 0) {
        Info.Hardpoints.assign(Wire.Hardpoints.Begin, Wire.Hardpoints.Begin + Count);
    }
    Client->SendCommonFireWeapon(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x04 FLPACKET_COMMON_SETTARGET
// Sender 0x06d8bd50 (IClient+0x010, RET 8), receiver caseD_4 at 0x06d838c9.
// Argument struct = XSetTarget (§3.5).
// ===========================================================================
static bool MarshalSetTarget(FPacket_FLPACKET_COMMON_SETTARGET& Packet, const unsigned int* Args,
                             unsigned int ArgCount) {
    const XSetTarget* Info = reinterpret_cast<const XSetTarget*>(Args[1]);
    FPayloadCommonSetTarget Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.TargetObjectId = Info->TargetObjectId;
    Wire.SubObjId = Info->SubObjId;
    Wire.TargetSubObjId = Info->TargetSubObjId;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverSetTarget(const FPacket_FLPACKET_COMMON_SETTARGET& Packet, IClient* Client,
                             IChatClient*) {
    const FPayloadCommonSetTarget& Wire = Packet.Wire;
    XSetTarget Info;
    Info.ObjectId = Wire.ObjectId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad_0x06 = 0;
    Info.TargetObjectId = Wire.TargetObjectId;
    Info.TargetSubObjId = Wire.TargetSubObjId;
    Info.Pad_0x0e = 0;
    Client->SendCommonSetTarget(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x05 FLPACKET_COMMON_CHATMSG
// Sender 0x06d8c470 (IChatClient+0x000, RET 0x10), receiver caseD_5 at 0x06d83900.
// The only channel-1 type that goes to the chat client rather than to IClient.
// ===========================================================================
static bool MarshalChatMsg(FPacket_FLPACKET_COMMON_CHATMSG& Packet, const unsigned int* Args,
                           unsigned int ArgCount) {
    FPayloadCommonChatMsg Wire;
    Wire.TargetId = Args[0];
    Wire.SenderId = Args[1];
    // Retail: `if (Buffer != 0 && Size != 0) { new(Size); memcpy } else { Size = 0; Buffer = 0 }`.
    Wire.Size = 0;
    Wire.Buffer = nullptr;
    const unsigned int Size = Args[2];
    void* Source = reinterpret_cast<void*>(Args[3]);
    if (Source != nullptr && Size != 0) {
        Wire.Size = Size;
        Wire.Buffer = RpcCopyIntoPacketBlock(Source, Size);
    }
    return RpcPayloadWrite(Packet.Wire, Wire);
}

// caseD_5 0x06d83900: DAT_06dbbcd4 is read untested -- no null test here either. The
// first argument is a 4-byte stack struct, word 1 then two zero bytes, i.e. the dword 1.
static bool DeliverChatMsg(const FPacket_FLPACKET_COMMON_CHATMSG& Packet, IClient*,
                           IChatClient* ChatClient) {
    const FPayloadCommonChatMsg& Wire = Packet.Wire;
    // Retail pushes the payload's +0x04, +0x08 and +0x0c and a literal 1 -- payload+0x00
    // is written by the sender and then never read.
    ChatClient->SendCommonChatMsg(CHAT_ID(1u), CHAT_ID(Wire.SenderId), Wire.Size, Wire.Buffer);
    return true;
}

// ===========================================================================
// ch1 op 0x08 FLPACKET_COMMON_ACTIVATEEQUIP -- sender 0x06d8bbb0, receiver case 8.
// Argument struct = XActivateEquip (§3.2).
// ===========================================================================
static bool MarshalActivateEquip(FPacket_FLPACKET_COMMON_ACTIVATEEQUIP& Packet,
                                 const unsigned int* Args, unsigned int ArgCount) {
    const XActivateEquip* Info = reinterpret_cast<const XActivateEquip*>(Args[1]);
    FPayloadCommonActivateEquip Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.bActivate = Info->bActivate ? 1u : 0u;
    Wire.Pad_0x05 = 0;
    Wire.EquipId = Info->EquipId;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverActivateEquip(const FPacket_FLPACKET_COMMON_ACTIVATEEQUIP& Packet,
                                 IClient* Client, IChatClient*) {
    const FPayloadCommonActivateEquip& Wire = Packet.Wire;
    XActivateEquip Info;
    Info.ObjectId = Wire.ObjectId;
    Info.EquipId = Wire.EquipId;
    Info.bActivate = (Wire.bActivate != 0);   // retail: `param_1[0x24] != 0`
    Info.Pad_0x07 = 0;
    Client->SendCommonActivateEquip(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x0e FLPACKET_COMMON_ACTIVATECRUISE -- sender 0x06d8bc10, receiver case 0xe.
// Argument struct = XActivateCruise (§3.3).
// ===========================================================================
static bool MarshalActivateCruise(FPacket_FLPACKET_COMMON_ACTIVATECRUISE& Packet,
                                  const unsigned int* Args, unsigned int ArgCount) {
    const XActivateCruise* Info = reinterpret_cast<const XActivateCruise*>(Args[1]);
    FPayloadCommonActivateCruise Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.bActivate = Info->bActivate ? 1u : 0u;
    Wire.bDisruptCruise = Info->bDisruptCruise;
    Wire.Pad_0x06[0] = 0;
    Wire.Pad_0x06[1] = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverActivateCruise(const FPacket_FLPACKET_COMMON_ACTIVATECRUISE& Packet,
                                  IClient* Client, IChatClient*) {
    const FPayloadCommonActivateCruise& Wire = Packet.Wire;
    XActivateCruise Info;
    Info.ObjectId = Wire.ObjectId;
    Info.bActivate = (Wire.bActivate != 0);
    Info.bDisruptCruise = (Wire.bDisruptCruise != 0) ? 1u : 0u;   // retail: `param_1[0x25] != 0`
    Info.Pad_0x06[0] = 0;
    Info.Pad_0x06[1] = 0;
    Client->SendCommonActivateCruise(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x0f FLPACKET_COMMON_GOTRADELANE -- sender 0x06d8bcb0, receiver case 0xf.
// Argument struct = XGoTradelane (§3.7).
// ===========================================================================
static bool MarshalGoTradelane(FPacket_FLPACKET_COMMON_GOTRADELANE& Packet,
                               const unsigned int* Args, unsigned int ArgCount) {
    const XGoTradelane* Info = reinterpret_cast<const XGoTradelane*>(Args[1]);
    FPayloadCommonGoTradelane Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.TradelaneRing1 = Info->TradelaneRing1;
    Wire.TradelaneRing2 = Info->TradelaneRing2;
    Wire.bMidLane = Info->bMidLane ? 1u : 0u;
    Wire.Pad_0x0d[0] = 0;
    Wire.Pad_0x0d[1] = 0;
    Wire.Pad_0x0d[2] = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverGoTradelane(const FPacket_FLPACKET_COMMON_GOTRADELANE& Packet, IClient* Client,
                               IChatClient*) {
    const FPayloadCommonGoTradelane& Wire = Packet.Wire;
    XGoTradelane Info;
    Info.ObjectId = Wire.ObjectId;
    Info.TradelaneRing1 = Wire.TradelaneRing1;
    Info.TradelaneRing2 = Wire.TradelaneRing2;
    Info.bMidLane = (Wire.bMidLane != 0);
    Info.Pad_0x0d[0] = 0;
    Info.Pad_0x0d[1] = 0;
    Info.Pad_0x0d[2] = 0;
    Client->SendCommonGoTradeLane(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x10 FLPACKET_COMMON_STOPTRADELANE -- sender 0x06d8bd10, receiver caseD_10.
// Four arguments in, one dword out: see the note on FPayloadCommonStopTradelane.
// ===========================================================================
static bool MarshalStopTradelane(FPacket_FLPACKET_COMMON_STOPTRADELANE& Packet,
                                 const unsigned int* Args, unsigned int ArgCount) {
    FPayloadCommonStopTradelane Wire;
    Wire.ShipId = Args[1];
    Wire.TradelaneRing1 = 0;   // retail never writes these two
    Wire.TradelaneRing2 = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverStopTradelane(const FPacket_FLPACKET_COMMON_STOPTRADELANE& Packet,
                                 IClient* Client, IChatClient*) {
    const FPayloadCommonStopTradelane& Wire = Packet.Wire;
    Client->SendCommonStopTradeLane(1u, Wire.ShipId, Wire.TradelaneRing1, Wire.TradelaneRing2);
    return true;
}

// ===========================================================================
// The buffer family. One marshaller pair, parameterised by whether retail copies and
// whether the packet takes ownership (payload+0x08, see FPayloadCommonBuffer).
// ===========================================================================
static bool MarshalBufferCommon(FPayloadCommonBuffer& Payload, const unsigned int* Args,
                                unsigned int ArgCount, bool bCopy, unsigned char bOwns) {
    FPayloadCommonBuffer Wire;
    Wire.Buffer = reinterpret_cast<void*>(Args[1]);
    Wire.Size = Args[2];
    Wire.bOwnsBuffer = bOwns;
    Wire.Pad_0x09[0] = 0;
    Wire.Pad_0x09[1] = 0;
    Wire.Pad_0x09[2] = 0;
    if (bCopy) {
        Wire.Buffer = RpcCopyIntoPacketBlock(reinterpret_cast<const void*>(Args[1]), Args[2]);
    }
    return RpcPayloadWrite(Payload, Wire);
}

// op 0x11 FLPACKET_COMMON_SET_WEAPON_GROUP -- sender 0x06d8b4f0, receiver caseD_11.
// The one sender in the family that leaves payload+0x08 at zero.
static bool MarshalSetWeaponGroup(FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP& P,
                                  const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P.Wire, A, N, false, 0);
}
static bool DeliverSetWeaponGroup(const FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP& Packet,
                                  IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer& Wire = Packet.Wire;
    Client->SendCommonSetWeaponGroup(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x13 FLPACKET_COMMON_SET_VISITED_STATE -- sender 0x06d8b540, receiver caseD_13.
// Stores the caller's pointer and sets the flag: the packet takes the caller's
// operator-new block over. Same for 0x17, 0x19, 0x1c and 0x1a.
static bool MarshalSetVisitedState(FPacket_FLPACKET_COMMON_SET_VISITED_STATE& P,
                                   const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P.Wire, A, N, false, 1);
}
static bool DeliverSetVisitedState(const FPacket_FLPACKET_COMMON_SET_VISITED_STATE& Packet,
                                   IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer& Wire = Packet.Wire;
    Client->SendCommonSetVisitedState(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x16 FLPACKET_COMMON_REQUEST_BEST_PATH -- sender 0x06d8b590, receiver caseD_16.
// operator new(Size) + copy: the packet owns the bytes.
static bool MarshalRequestBestPath(FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH& P,
                                   const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P.Wire, A, N, true, 1);
}
static bool DeliverRequestBestPath(const FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH& Packet,
                                   IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer& Wire = Packet.Wire;
    Client->SendCommonRequestBestPath(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x17 FLPACKET_COMMON_REQUEST_GROUP_POSITIONS -- sender 0x06d8b7a0, receiver caseD_17.
static bool MarshalRequestGroupPositions(FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS& P,
                                         const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P.Wire, A, N, false, 1);
}
static bool DeliverRequestGroupPositions(const FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS& Packet,
                                         IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer& Wire = Packet.Wire;
    Client->SendCommonRequestGroupPositions(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x18 FLPACKET_COMMON_REQUEST_PLAYER_STATS -- sender 0x06d8b610, receiver caseD_18.
// Same operator_new + copy as 0x16.
static bool MarshalRequestPlayerStats(FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS& P,
                                      const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P.Wire, A, N, true, 1);
}
static bool DeliverRequestPlayerStats(const FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS& Packet,
                                      IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer& Wire = Packet.Wire;
    Client->SendCommonRequestPlayerStats(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x19 FLPACKET_COMMON_SET_MISSION_LOG -- sender 0x06d8b7f0, receiver caseD_19.
static bool MarshalSetMissionLog(FPacket_FLPACKET_COMMON_SET_MISSION_LOG& P, const unsigned int* A,
                                 unsigned int N) {
    return MarshalBufferCommon(P.Wire, A, N, false, 1);
}
static bool DeliverSetMissionLog(const FPacket_FLPACKET_COMMON_SET_MISSION_LOG& Packet,
                                 IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer& Wire = Packet.Wire;
    Client->SendCommonSetMissionLog(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x1a FLPACKET_COMMON_REQUEST_RANK_LEVEL -- sender 0x06d8b890, receiver caseD_1a.
static bool MarshalRequestRankLevel(FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL& P,
                                    const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P.Wire, A, N, false, 1);
}
static bool DeliverRequestRankLevel(const FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL& Packet,
                                    IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer& Wire = Packet.Wire;
    Client->SendCommonRequestRankLevel(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// op 0x1c FLPACKET_COMMON_SET_INTERFACE_STATE -- sender 0x06d8b840, receiver caseD_1c.
static bool MarshalSetInterfaceState(FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE& P,
                                     const unsigned int* A, unsigned int N) {
    return MarshalBufferCommon(P.Wire, A, N, false, 1);
}
static bool DeliverSetInterfaceState(const FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE& Packet,
                                     IClient* Client, IChatClient*) {
    const FPayloadCommonBuffer& Wire = Packet.Wire;
    Client->SendCommonSetInterfaceState(1u, static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    return true;
}

// ===========================================================================
// ch1 op 0x12 FLPACKET_COMMON_PLAYER_TRADE -- eight senders, one sub-switch.
//
// The proxy hands us Args[0] = the sub-type mask it is sending under, then the slot's own
// arguments -- see FLocalClientProxy::SendTradePacket. That mirrors retail, where each of
// the eight slots writes its own literal into payload+0x00.
// ===========================================================================
static bool MarshalPlayerTrade(FPacket_FLPACKET_COMMON_PLAYER_TRADE& Packet,
                               const unsigned int* Args, unsigned int ArgCount) {
    FPayloadCommonPlayerTrade Wire;
    Wire.SubType = Args[0];
    Wire.Reason = 0;
    Wire.ShipId = 0;
    Wire.Money = 0;
    Wire.bAccepted = 0;
    Wire.Pad_0x11[0] = 0;
    Wire.Pad_0x11[1] = 0;
    Wire.Pad_0x11[2] = 0;
    // The rest is what the packet constructor FUN_06d9bad0 leaves for the fields a sub-type
    // does not write: +0x14 = 0xffffffff, +0x18 / +0x1c = 0. So sub-types
    // 1/2/4/0x20/0x40/0x80 ship EquipArchId -1, not 0. The word at +0x20 is not seeded at all
    // (operator new residue in retail); zero here.
    Wire.EquipArchId = 0xffffffffu;
    Wire.EquipCount = 0;
    Wire.EquipStatus = 0.0f;
    Wire.EquipId = 0;
    Wire.Pad_0x22 = 0;

    // Args[1] is every sender's 2nd explicit argument, Args[2] the 3rd where there is one.
    // Both senders always hand over a zero-filled array of at least three
    // (SendTradePacket's Marshalled[4], RpcSendTradeToServer's Args[3]).
    const unsigned int A1 = Args[1];
    const unsigned int A2 = Args[2];

    switch (Wire.SubType) {
    case kPlayerTradeInitiateTrade:   // 0x06d8b8e0 InitiateTrade-shaped
    case kPlayerTradeRequestTrade:   // 0x06d8bb10
    case kPlayerTradeStopTradeRequest:   // 0x06d8bb60
        Wire.ShipId = A1;
        break;
    case kPlayerTradeTerminateTrade:   // 0x06d8b980
        Wire.Reason = A1;
        break;
    case kPlayerTradeSetTradeMoney:   // 0x06d8b9d0
        Wire.ShipId = A1;
        Wire.Money = A2;
        break;
    case kPlayerTradeAcceptTrade:   // 0x06d8b930
        Wire.ShipId = A1;
        Wire.bAccepted = (unsigned char)(A2 & 0xffu);
        break;
    case kPlayerTradeAddTradeEquip:   // 0x06d8ba20 -- EquipDesc, status included
    case kPlayerTradeDelTradeEquip: { // 0x06d8baa0 -- EquipDesc, status NOT written
        // Direct loads off the caller's EquipDesc (+0x04, +0x14, float +0x10, word +0x02);
        // 0x10 skips the float.
        Wire.ShipId = A1;
        const EquipDesc* Desc = reinterpret_cast<const EquipDesc*>(A2);
        Wire.EquipArchId = Desc->ArchId;
        Wire.EquipCount = (unsigned int)Desc->Count;
        Wire.EquipId = Desc->Id;
        if (Wire.SubType == kPlayerTradeAddTradeEquip) {
            Wire.EquipStatus = Desc->Health;
        }
        break;
    }
    default:
        return false;
    }
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverPlayerTrade(const FPacket_FLPACKET_COMMON_PLAYER_TRADE& Packet, IClient* Client,
                               IChatClient*) {
    const FPayloadCommonPlayerTrade& Wire = Packet.Wire;
    switch (Wire.SubType) {
    case kPlayerTradeInitiateTrade:
        Client->SendCommonPlayerTradeInitiateTrade(1u, Wire.ShipId);
        return true;
    case kPlayerTradeTerminateTrade:
        Client->SendCommonPlayerTradeTerminateTrade(1u, static_cast<int>(Wire.Reason));
        return true;
    case kPlayerTradeSetTradeMoney:
        Client->SendCommonPlayerTradeSetTradeMoney(1u, Wire.ShipId, Wire.Money);
        return true;
    case kPlayerTradeAddTradeEquip: {
        // caseD_8 0x06d837df: EquipDesc::EquipDesc(), then set_arch_id(+0x34),
        // set_count(+0x38), set_status(+0x3c), set_id(word +0x40), then IClient+0x1a4.
        EquipDesc Desc;
        Desc.set_arch_id(Wire.EquipArchId);
        Desc.set_count((int)Wire.EquipCount);
        Desc.set_status(Wire.EquipStatus);
        Desc.set_id(Wire.EquipId);
        Client->SendCommonPlayerTradeAddTradeEquip(1u, Wire.ShipId, Desc);
        return true;
    }
    case kPlayerTradeDelTradeEquip: {
        // caseD_10 0x06d83842: the same without set_status, so Health keeps the ctor's
        // default; then IClient+0x1a8.
        EquipDesc Desc;
        Desc.set_arch_id(Wire.EquipArchId);
        Desc.set_count((int)Wire.EquipCount);
        Desc.set_id(Wire.EquipId);
        Client->SendCommonPlayerTradeDelTradeEquip(1u, Wire.ShipId, Desc);
        return true;
    }
    case kPlayerTradeRequestTrade:
        Client->SendCommonPlayerTradeRequestTrade(1u, Wire.ShipId);
        return true;
    case kPlayerTradeAcceptTrade:
        // The byte goes out zero-extended; the sender only ever stored a bool's 0/1.
        Client->SendCommonPlayerTradeAcceptTrade(1u, Wire.ShipId, Wire.bAccepted != 0);
        return true;
    case kPlayerTradeStopTradeRequest:
        Client->SendCommonPlayerTradeStopTradeRequest(1u, Wire.ShipId);
        return true;
    default:
        // Retail's sub-switch has no default: anything else falls out and the packet dies.
        return false;
    }
}

// ===========================================================================
// ch1 op 0x14 FLPACKET_COMMON_JETTISONCARGO -- sender 0x06d8bdb0, receiver caseD_14.
// Argument struct = XJettisonCargo (§3.8).
// ===========================================================================
static bool MarshalJettisonCargo(FPacket_FLPACKET_COMMON_JETTISONCARGO& Packet,
                                 const unsigned int* Args, unsigned int ArgCount) {
    const XJettisonCargo* Info = reinterpret_cast<const XJettisonCargo*>(Args[1]);
    FPayloadCommonJettisonCargo Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.ItemId = Info->ItemId;
    Wire.Pad_0x06 = 0;
    Wire.Count = Info->Count;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverJettisonCargo(const FPacket_FLPACKET_COMMON_JETTISONCARGO& Packet,
                                 IClient* Client, IChatClient*) {
    const FPayloadCommonJettisonCargo& Wire = Packet.Wire;
    XJettisonCargo Info;
    Info.ObjectId = Wire.ObjectId;
    Info.ItemId = Wire.ItemId;
    Info.Pad_0x06 = 0;
    Info.Count = Wire.Count;
    Client->SendCommonJettisonCargo(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x15 FLPACKET_COMMON_ACTIVATETHRUSTERS -- sender 0x06d8bc60, receiver caseD_15.
// Argument struct = XActivateThrusters (§3.4).
// ===========================================================================
static bool MarshalActivateThrusters(FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS& Packet,
                                     const unsigned int* Args, unsigned int ArgCount) {
    const XActivateThrusters* Info = reinterpret_cast<const XActivateThrusters*>(Args[1]);
    FPayloadCommonActivateThrusters Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.bActivate = Info->bActivate ? 1u : 0u;
    Wire.Pad_0x05[0] = 0;
    Wire.Pad_0x05[1] = 0;
    Wire.Pad_0x05[2] = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverActivateThrusters(const FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS& Packet,
                                     IClient* Client, IChatClient*) {
    const FPayloadCommonActivateThrusters& Wire = Packet.Wire;
    XActivateThrusters Info;
    Info.ObjectId = Wire.ObjectId;
    Info.bActivate = (Wire.bActivate != 0);   // retail: SETNZ on packet+0x24
    Info.Pad_0x05[0] = 0;
    Info.Pad_0x05[1] = 0;
    Info.Pad_0x05[2] = 0;
    Client->SendCommonActivateThrusters(1u, Info);
    return true;
}

// ===========================================================================
// ch1 op 0x1b FLPACKET_COMMON_POP_UP_DIALOG -- sender 0x06d8b690 (IClient+0x17c,
// RET 0x10), receiver caseD_1b at 0x06d835e3 -> IClient vtable +0x17c.
//
// Both FmtStrs are flattened through the COMMON.DLL exports (see the flattened-FmtStr
// section in rpc_payloads.h). The inlined size loop at 0x06d8b6d0 costs 0x17 for tag 0x4e
// and 7 for every other tag.
//
// Arguments: Args[0] session id (never serialized), Args[1]/Args[2] FmtStr*, Args[3] the
// dword at packet+0x30.
// ===========================================================================
static bool MarshalPopUpDialog(FPacket_FLPACKET_COMMON_POP_UP_DIALOG& Packet,
                               const unsigned int* Args, unsigned int ArgCount) {
    const FmtStr* SourceA = reinterpret_cast<const FmtStr*>(Args[1]);
    const FmtStr* SourceB = reinterpret_cast<const FmtStr*>(Args[2]);
    FPayloadCommonPopUpDialog Wire;
    Wire.SizeA = 0;
    Wire.SizeB = 0;
    Wire.BufferA = RpcFlattenFmtStr(*SourceA, Wire.SizeA);
    Wire.BufferB = RpcFlattenFmtStr(*SourceB, Wire.SizeB);
    Wire.Buttons = Args[3];
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverPopUpDialog(const FPacket_FLPACKET_COMMON_POP_UP_DIALOG& Packet, IClient* Client,
                               IChatClient*) {
    const FPayloadCommonPopUpDialog& Wire = Packet.Wire;
    FmtStr TextA;
    FmtStr TextB;
    RpcUnflattenFmtStr(TextA, Wire.BufferA, Wire.SizeA);
    RpcUnflattenFmtStr(TextB, Wire.BufferB, Wire.SizeB);
    Client->SendCommonPopUpDialog(1u, TextA, TextB, Wire.Buttons);
    return true;
}

// ===========================================================================
// ch1 op 0x1d FLPACKET_COMMON_TRACTOROBJECTS -- sender 0x06d8be10, receiver caseD_1d.
// Argument struct = XTractorObjects (§3.6).
// ===========================================================================
static bool MarshalTractorObjects(FPacket_FLPACKET_COMMON_TRACTOROBJECTS& Packet,
                                  const unsigned int* Args, unsigned int ArgCount) {
    const XTractorObjects* Info = reinterpret_cast<const XTractorObjects*>(Args[1]);
    FPayloadCommonTractorObjects Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.TractorEquipId = Info->TractorEquipId;
    Wire.Pad_0x06 = 0;
    Wire.TargetIds.AllocPad = 0;
    Wire.TargetIds.Pad_0x01[0] = 0;
    Wire.TargetIds.Pad_0x01[1] = 0;
    Wire.TargetIds.Pad_0x01[2] = 0;
    Wire.TargetIds.Begin = nullptr;
    Wire.TargetIds.End = nullptr;
    Wire.TargetIds.Cap = nullptr;

    const unsigned int Count = static_cast<unsigned int>(Info->TargetIds.size());
    if (Count != 0) {
        // The inlined vector operator=: an untested allocation.
        unsigned int* Block = static_cast<unsigned int*>(
            RpcCopyIntoPacketBlock(Info->TargetIds.data(), Count * 4u));
        Wire.TargetIds.Begin = Block;
        Wire.TargetIds.End = Block + Count;
        Wire.TargetIds.Cap = Block + Count;
    }
    return RpcPayloadWrite(Packet.Wire, Wire);
}

// caseD_1d 0x06d8326a: a local XTractorObjects; +0x00 and the word +0x04 from the payload,
// then the inlined vector<uint> operator= from payload+0x08 (an empty source leaves it
// empty, otherwise one block of Count*4 bytes holds a COPY). IClient slot 5 gets
// (1, &local); FUN_06d851d0, the vector destructor, frees the copy.
static bool DeliverTractorObjects(const FPacket_FLPACKET_COMMON_TRACTOROBJECTS& Packet,
                                  IClient* Client, IChatClient*) {
    const FPayloadCommonTractorObjects& Wire = Packet.Wire;
    XTractorObjects Info;
    Info.ObjectId = Wire.ObjectId;
    Info.TractorEquipId = Wire.TractorEquipId;
    Info.Pad_0x06 = 0;
    const unsigned int Count = Wire.TargetIds.Count();
    if (Count != 0) {
        Info.TargetIds.assign(Wire.TargetIds.Begin, Wire.TargetIds.End);
    }
    Client->SendCommonTractorObjects(1u, Info);
    return true;
}

// ===========================================================================
// The table. Order follows the opcode, not the retail registrator's order.
// ===========================================================================
const FRpcPayloadCodec GChannel1Codecs[] = {
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_UPDATEOBJECT), "FLPACKET_COMMON_UPDATEOBJECT",             0x2c,
      RpcMarshalAs<MarshalUpdateObject>,          RpcDeliverAs<DeliverUpdateObject> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_FIREWEAPON), "FLPACKET_COMMON_FIREWEAPON",               0x20,
      RpcMarshalAs<MarshalFireWeapon>,            RpcDeliverAs<DeliverFireWeapon> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SETTARGET), "FLPACKET_COMMON_SETTARGET",                0x0c,
      RpcMarshalAs<MarshalSetTarget>,             RpcDeliverAs<DeliverSetTarget> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_CHATMSG), "FLPACKET_COMMON_CHATMSG",                  0x10,
      RpcMarshalAs<MarshalChatMsg>,               RpcDeliverAs<DeliverChatMsg> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATEEQUIP), "FLPACKET_COMMON_ACTIVATEEQUIP",            0x08,
      RpcMarshalAs<MarshalActivateEquip>,         RpcDeliverAs<DeliverActivateEquip> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATECRUISE), "FLPACKET_COMMON_ACTIVATECRUISE",           0x08,
      RpcMarshalAs<MarshalActivateCruise>,        RpcDeliverAs<DeliverActivateCruise> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_GOTRADELANE), "FLPACKET_COMMON_GOTRADELANE",              0x10,
      RpcMarshalAs<MarshalGoTradelane>,           RpcDeliverAs<DeliverGoTradelane> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_STOPTRADELANE), "FLPACKET_COMMON_STOPTRADELANE",            0x0c,
      RpcMarshalAs<MarshalStopTradelane>,         RpcDeliverAs<DeliverStopTradelane> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_WEAPON_GROUP), "FLPACKET_COMMON_SET_WEAPON_GROUP",         0x0c,
      RpcMarshalAs<MarshalSetWeaponGroup>,        RpcDeliverAs<DeliverSetWeaponGroup> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), "FLPACKET_COMMON_PLAYER_TRADE",             0x24,
      RpcMarshalAs<MarshalPlayerTrade>,           RpcDeliverAs<DeliverPlayerTrade> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_VISITED_STATE), "FLPACKET_COMMON_SET_VISITED_STATE",        0x0c,
      RpcMarshalAs<MarshalSetVisitedState>,       RpcDeliverAs<DeliverSetVisitedState> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_JETTISONCARGO), "FLPACKET_COMMON_JETTISONCARGO",            0x0c,
      RpcMarshalAs<MarshalJettisonCargo>,         RpcDeliverAs<DeliverJettisonCargo> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATETHRUSTERS), "FLPACKET_COMMON_ACTIVATETHRUSTERS",        0x08,
      RpcMarshalAs<MarshalActivateThrusters>,     RpcDeliverAs<DeliverActivateThrusters> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_BEST_PATH), "FLPACKET_COMMON_REQUEST_BEST_PATH",        0x0c,
      RpcMarshalAs<MarshalRequestBestPath>,       RpcDeliverAs<DeliverRequestBestPath> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_GROUP_POSITIONS), "FLPACKET_COMMON_REQUEST_GROUP_POSITIONS",  0x0c,
      RpcMarshalAs<MarshalRequestGroupPositions>, RpcDeliverAs<DeliverRequestGroupPositions> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_PLAYER_STATS), "FLPACKET_COMMON_REQUEST_PLAYER_STATS",     0x0c,
      RpcMarshalAs<MarshalRequestPlayerStats>,    RpcDeliverAs<DeliverRequestPlayerStats> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_MISSION_LOG), "FLPACKET_COMMON_SET_MISSION_LOG",          0x0c,
      RpcMarshalAs<MarshalSetMissionLog>,         RpcDeliverAs<DeliverSetMissionLog> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_RANK_LEVEL), "FLPACKET_COMMON_REQUEST_RANK_LEVEL",       0x0c,
      RpcMarshalAs<MarshalRequestRankLevel>,      RpcDeliverAs<DeliverRequestRankLevel> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_POP_UP_DIALOG), "FLPACKET_COMMON_POP_UP_DIALOG",            0x14,
      RpcMarshalAs<MarshalPopUpDialog>,           RpcDeliverAs<DeliverPopUpDialog> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_INTERFACE_STATE), "FLPACKET_COMMON_SET_INTERFACE_STATE",      0x0c,
      RpcMarshalAs<MarshalSetInterfaceState>,     RpcDeliverAs<DeliverSetInterfaceState> },
    { kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_TRACTOROBJECTS), "FLPACKET_COMMON_TRACTOROBJECTS",           0x18,
      RpcMarshalAs<MarshalTractorObjects>,        RpcDeliverAs<DeliverTractorObjects> },
};

const unsigned int GChannel1CodecCount =
    (unsigned int)(sizeof(GChannel1Codecs) / sizeof(GChannel1Codecs[0]));

extern "C" __declspec(dllexport) const FRpcPayloadCodec* RpcFindPayloadCodec(unsigned int Channel, unsigned int Opcode) {
    if (Channel == kPacketChannelCommon) {
        for (unsigned int i = 0; i < GChannel1CodecCount; ++i) {
            if (GChannel1Codecs[i].Opcode == (unsigned short)Opcode) {
                return &GChannel1Codecs[i];
            }
        }
        return nullptr;
    }
    if (Channel == kPacketChannelServer) {
        // GChannel2Codecs lives in src/rpclocal/rpc_payload_ch2.cpp.
        for (unsigned int i = 0; i < GChannel2CodecCount; ++i) {
            if (GChannel2Codecs[i].Opcode == (unsigned short)Opcode) {
                return &GChannel2Codecs[i];
            }
        }
        return nullptr;
    }
    // Channel 3 (client -> server) delivers to IServer, not IClient: see RpcFindServerCodec
    // in src/rpclocal/rpc_payload_ch3.cpp.
    return nullptr;
}
