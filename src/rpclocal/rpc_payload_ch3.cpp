// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_payload_ch3.cpp -- the client -> server half of rpclocal: the channel-3 packet
// types AND the port of retail FUN_06d86a70, the client-side dispatcher.
//
// FUN_06d86a70 mirrors IServerImpl::HandlePacket: it reads the routing header at
// CDAPacket+0x0c, switches on the channel, and for channels 1 and 3 calls one vtable slot
// of the real IServer Server.dll handed back (.data:DAT_06dbbd24, filled by
// IServerImpl::Startup from DAT_06dbbd5c). Channel 2 never reaches it.
//
// Each layout is read from the sender (the IServerImpl slot body, allocating through
// FUN_06d96300(Serializer, 3, Opcode, &Out)), the receiver (the matching case here) and the
// registry size (payload == operator new() size - 0x20). The 27 channel-1 and 53 channel-3
// call sites reach 80 distinct IServerImpl slots whose arity and argument types match
// common_server.h; the 13 untouched slots are lifecycle/connection 9..17, GetServerID (88),
// GetServerSig (89), DumpPacketStats (90) and PushToServer (92, IServerImpl's own virtual).
//
// ch3 0x0a GFREQUESTSHIPINFO is registered (size 0x20, payload 0) but has no sender and no
// case in retail: a dead retail entry (docs/re/rpclocal.md).
//
// The container types 0x16 REQEQUIPMENT, 0x17 REQCARGO and 0x36 REQCOLLISIONGROUPS use the
// retail std::list wire layout from rpc_containers.{h,cpp}; the real EquipDescList /
// std::list<CollisionGroupDesc> are handed to IServer (docs/re/deviations.md, "former
// bridges").

#include "rpc_payloads.h"
#include "rpc_packet_classes.h"
#include "rpc_containers.h"
#include "common_server.h"
// EquipDesc and its setters (retail imports .rdata:06db3004, 06db3070..06db308c), used by
// the two PLAYER_TRADE equipment sub-types.
#include "common_goods.h"
// CollisionGroupDesc's complete definition, for ch3 0x36 REQCOLLISIONGROUPS: retail passes
// the payload's own list to IServer+0x6c, the port builds a real
// std::list<CollisionGroupDesc>.
#include "common_cobject.h"

#include <cstring>
#include <list>
#include <new>

// ---------------------------------------------------------------------------
// The real interfaces Server.dll handed back. Retail keeps them in .data:0x06dbbd24
// (IServer, aliased from 0x06dbbd5c) and .data:0x06dbbd20 (IChatServer, from
// 0x06dbbd60). IServerImpl::Startup publishes both; Shutdown clears them.
// ---------------------------------------------------------------------------
IServer* GRealServer = nullptr;
IChatServer* GRealChatServer = nullptr;

// ---------------------------------------------------------------------------
// Shared helpers. The block writers FUN_06d9a160, FUN_06d9a5f0, FUN_06d9aab0 and the blob
// writer FUN_06d97b00 (shared with channel 2) all do RpcCopyBlock (rpc_payloads.h) and store
// block and length in two payload fields. The three block writers add
// `if (size == 0 || src == 0) return` (their callers carry that test here); FUN_06d97b00
// has none.
// ---------------------------------------------------------------------------

// Inline wcslen; retail does not test the pointer. (Inline strlen is RpcStringLength.)
static unsigned int RpcCh3WideLength(const wchar_t* Text) {
    unsigned int Length = 0;
    while (Text[Length] != 0) {
        ++Length;
    }
    return Length;
}

// ===========================================================================
// Generic marshallers. The argument index is the position in the retail IServer
// signature (src/include/common_server.h), NOT counting `this`.
// ===========================================================================

// payload+0x00 = Args[Index], by value.
template<unsigned int Index>
static bool MarshalCh3DwordAt(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDword Wire;
    Wire.Field_0x00 = Args[Index];
    return RpcPayloadWrite(Packet, 0, Wire);
}

// payload+0x00 = *(dword*)Args[Index], where the method takes `uint const&` (Dock's first
// argument).
template<unsigned int Index>
static bool MarshalCh3DwordByRefAt(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDword Wire;
    Wire.Field_0x00 = *reinterpret_cast<const unsigned int*>(Args[Index]);
    return RpcPayloadWrite(Packet, 0, Wire);
}

// Nothing is serialized at all. ch3 0x05, 0x10, 0x38, 0x42 -- the whole call is the
// opcode plus the implicit session.
static bool MarshalCh3Empty(FPacketBase*, const unsigned int*, unsigned int) {
    return true;
}

// The first N dwords of the struct Args[0] points at, copied straight through.
template<unsigned int DwordCount>
static bool MarshalCh3StructHead(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const unsigned int* Src = reinterpret_cast<const unsigned int*>(Args[0]);
    for (unsigned int i = 0; i < DwordCount; ++i) {
        RpcPayloadWrite(Packet, i * 4u, Src[i]);
    }
    return true;
}

// ===========================================================================
// ch3 0x01 FLPACKET_CLIENT_LOGIN -- sender IServerImpl::Login 0x06d8cc70,
// receiver caseD_1. IServer slot 18 (+0x48) Login(SLoginInfo const&, uint).
//
// The receiver fills a file-static SLoginInfo at .data:0x06dbba38 (not re-entrant in retail
// either) and passes its address. The wcsncpy caps (0x28 and 0x10 wide characters) and the
// explicit NUL stores at .data:0x06dbba86 / 0x06dbbaf6 pin the second string to +0xa0 and
// the flag to +0xc0.
// ===========================================================================

static SLoginInfo GStaticLoginInfo;   // retail .data:0x06dbba38

static bool MarshalLogin(FPacket_FLPACKET_CLIENT_LOGIN& Packet, const unsigned int* Args,
                         unsigned int ArgCount) {
    const SLoginInfo* Info = reinterpret_cast<const SLoginInfo*>(Args[0]);
    // IServerImpl::Login 0x06d8cc70: two unbounded wcscpy calls into a 0x404-byte stack
    // window (to +0x000 and +0x200), the flag byte to +0x400, then 0x402 bytes copied into
    // the payload. Window bytes neither wcscpy reaches are uninitialised in retail; zeroed
    // here.
    FPayloadClientLogin Wire;
    for (unsigned int i = 0; i < (unsigned int)sizeof(Wire); ++i) {
        reinterpret_cast<unsigned char*>(&Wire)[i] = 0;
    }
    wcscpy(Wire.AccountName, Info->AccountName);
    wcscpy(Wire.Password, Info->Password);
    // Retail reads the single byte at +0xc0.
    Wire.CreateFlag = *reinterpret_cast<const unsigned char*>(&Info->CreateFlag);
    // Exactly 0x402 bytes; +0x402/+0x403 keep what the constructor left.
    memcpy(&Packet.Wire, &Wire, 0x402u);
    return true;
}

static bool DeliverLogin(const FPacket_FLPACKET_CLIENT_LOGIN& Packet, IServer* Server,
                         unsigned int SessionId) {
    const FPayloadClientLogin* Wire =
        &Packet.Wire;
    // wcsncpy pads with NULs up to the count. Retail never clears the static; bytes not
    // overwritten keep their zero .data image.
    wcsncpy(GStaticLoginInfo.AccountName, Wire->AccountName, 0x28);
    wcsncpy(GStaticLoginInfo.Password, Wire->Password, 0x10);
    // A byte store at +0xc0; the flag's upper three bytes stay as they were.
    *reinterpret_cast<unsigned char*>(&GStaticLoginInfo.CreateFlag) = Wire->CreateFlag;
    // Retail's two explicit terminator stores, +0x4e and +0xbe.
    GStaticLoginInfo.AccountName[0x27] = 0;
    GStaticLoginInfo.Password[0x0f] = 0;
    Server->Login(GStaticLoginInfo, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x03 FLPACKET_CLIENT_MUNCOLLISION -- sender 0x06d8d6e0, receiver caseD_3.
// IServer slot 53 (+0xd4) SPMunitionCollision(SSPMunitionCollisionInfo const&, uint).
// Identity mapping; the only narrow field is the word at +0x0c on both sides.
// ===========================================================================
static bool MarshalMunCollision(FPacket_FLPACKET_CLIENT_MUNCOLLISION& Packet,
                                const unsigned int* Args, unsigned int ArgCount) {
    const SSPMunitionCollisionInfo* Info = reinterpret_cast<const SSPMunitionCollisionInfo*>(Args[0]);
    FPayloadClientMunCollision Wire;
    Wire.ProjectileArchId = Info->ProjectileArchId;
    Wire.AttackerId = Info->AttackerId;
    Wire.ObjectId = Info->ObjectId;
    Wire.SubObjId = Info->SubObjId;
    Wire.Pad_0x0e = 0;
    Wire.HitPosX = Info->HitPosX;
    Wire.HitPosY = Info->HitPosY;
    Wire.HitPosZ = Info->HitPosZ;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverMunCollision(const FPacket_FLPACKET_CLIENT_MUNCOLLISION& Packet, IServer* Server,
                                unsigned int SessionId) {
    const FPayloadClientMunCollision& Wire = Packet.Wire;
    SSPMunitionCollisionInfo Info;
    Info.ProjectileArchId = Wire.ProjectileArchId;
    Info.AttackerId = Wire.AttackerId;
    Info.ObjectId = Wire.ObjectId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad_0x0e = 0;
    Info.HitPosX = Wire.HitPosX;
    Info.HitPosY = Wire.HitPosY;
    Info.HitPosZ = Wire.HitPosZ;
    Server->SPMunitionCollision(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x04 REQUESTLAUNCH / 0x07 ENTERBASE / 0x0b SYSTEM_SWITCH_OUT_COMPLETE /
// 0x0d EXITBASE / 0x0e ENTERLOCATION / 0x0f EXITLOCATION / 0x13 GFSELECTOBJECT /
// 0x15 REQSHIPARCH -- one dword, the FIRST argument; the session is the second and is
// dropped. Receivers push (payload+0x00, SessionId) in that order.
// ===========================================================================
static bool DeliverPlayerLaunch(const FPacket_FLPACKET_CLIENT_REQUESTLAUNCH& Packet,
                                IServer* Server, unsigned int SessionId) {
    Server->PlayerLaunch(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverBaseEnter(const FPacket_FLPACKET_CLIENT_ENTERBASE& Packet, IServer* Server,
                             unsigned int SessionId) {
    Server->BaseEnter(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverBaseExit(const FPacket_FLPACKET_CLIENT_EXITBASE& Packet, IServer* Server,
                            unsigned int SessionId) {
    Server->BaseExit(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverLocationEnter(const FPacket_FLPACKET_CLIENT_ENTERLOCATION& Packet,
                                 IServer* Server, unsigned int SessionId) {
    Server->LocationEnter(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverLocationExit(const FPacket_FLPACKET_CLIENT_EXITLOCATION& Packet, IServer* Server,
                                unsigned int SessionId) {
    Server->LocationExit(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverSystemSwitchOutComplete(const FPacket_FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE& Packet,
                                           IServer* Server, unsigned int SessionId) {
    Server->SystemSwitchOutComplete(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverGFObjSelect(const FPacket_FLPACKET_CLIENT_GFSELECTOBJECT& Packet,
                               IServer* Server, unsigned int SessionId) {
    Server->GFObjSelect(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverReqShipArch(const FPacket_FLPACKET_CLIENT_REQSHIPARCH& Packet, IServer* Server,
                               unsigned int SessionId) {
    Server->ReqShipArch(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverReqSetCash(const FPacket_FLPACKET_CLIENT_REQSETCASH& Packet, IServer* Server,
                              unsigned int SessionId) {
    Server->ReqSetCash((int)Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverReqChangeCash(const FPacket_FLPACKET_CLIENT_REQCHANGECASH& Packet,
                                 IServer* Server, unsigned int SessionId) {
    Server->ReqChangeCash((int)Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverReqHullStatus(const FPacket_FLPACKET_CLIENT_REQHULLSTATUS& Packet,
                                 IServer* Server, unsigned int SessionId) {
    Server->ReqHullStatus(Packet.Wire.Field_0x00, SessionId);
    return true;
}
static bool DeliverReqDifficultyScale(const FPacket_FLPACKET_CLIENT_REQDIFFICULTYSCALE& Packet,
                                      IServer* Server, unsigned int SessionId) {
    Server->ReqDifficultyScale(Packet.Wire.Field_0x00, SessionId);
    return true;
}

// ch3 0x35 INTERFACEITEMUSED / 0x41 ABORT_MISSION / 0x45 MISSIONSAVEB / 0x47 RTCDONE --
// these four take the session FIRST, so retail serializes the SECOND argument and the
// receiver pushes (SessionId, payload+0x00).
static bool DeliverInterfaceItemUsed(const FPacket_FLPACKET_CLIENT_INTERFACEITEMUSED& Packet,
                                     IServer* Server, unsigned int SessionId) {
    Server->InterfaceItemUsed(SessionId, Packet.Wire.Field_0x00);
    return true;
}
static bool DeliverAbortMission(const FPacket_FLPACKET_CLIENT_ABORT_MISSION& Packet,
                                IServer* Server, unsigned int SessionId) {
    Server->AbortMission(SessionId, Packet.Wire.Field_0x00);
    return true;
}
static bool DeliverMissionSaveB(const FPacket_FLPACKET_CLIENT_MISSIONSAVEB& Packet, IServer* Server,
                                unsigned int SessionId) {
    Server->MissionSaveB(SessionId, Packet.Wire.Field_0x00);
    return true;
}
static bool DeliverRTCDone(const FPacket_FLPACKET_CLIENT_RTCDONE& Packet, IServer* Server,
                           unsigned int SessionId) {
    Server->RTCDone(SessionId, Packet.Wire.Field_0x00);
    return true;
}

// ===========================================================================
// ch3 0x05 REQUESTCHARINFO -- sender IServerImpl::CharacterInfoReq 0x06d8cd00 writes
// NOTHING, and the receiver pushes a literal 0 for the bool. So the bool argument is
// lost in transit on purpose: retail always asks for the short form over the wire.
// ch3 0x10 REQUESTCREATESHIP / 0x38 REQUESTNEWCHARINFO / 0x42 SKIP_AUTOSAVE take the
// session only.
// ===========================================================================
static bool DeliverCharacterInfoReq(const FPacket_FLPACKET_CLIENT_REQUESTCHARINFO&, IServer* Server,
                                    unsigned int SessionId) {
    Server->CharacterInfoReq(SessionId, false);
    return true;
}
static bool DeliverRequestCreateShip(const FPacket_FLPACKET_CLIENT_REQUESTCREATESHIP&,
                                     IServer* Server, unsigned int SessionId) {
    Server->RequestCreateShip(SessionId);
    return true;
}
static bool DeliverNewCharacterInfoReq(const FPacket_FLPACKET_CLIENT_REQUESTNEWCHARINFO&,
                                       IServer* Server, unsigned int SessionId) {
    Server->NewCharacterInfoReq(SessionId);
    return true;
}
static bool DeliverCharacterSkipAutosave(const FPacket_FLPACKET_CLIENT_SKIP_AUTOSAVE&,
                                         IServer* Server, unsigned int SessionId) {
    Server->CharacterSkipAutosave(SessionId);
    return true;
}

// ===========================================================================
// ch3 0x06 SELECTCHARACTER / 0x3a DESTROYCHAR -- the payload IS the CHARACTER_ID, and
// the receiver hands packet+0x20 straight to the slot.
//
// Senders 0x06d8cd60 / 0x06d8ce90 (and 0x06d8db10 for SAVEGAME) call the imported
// COMMON.DLL CHARACTER_ID::operator= into packet+0x20, no size test in front.
// ===========================================================================
static bool MarshalCharacterId(FRpcCharacterIdPacket& Packet, const unsigned int* Args,
                               unsigned int ArgCount) {
    CHARACTER_ID* Dst = reinterpret_cast<CHARACTER_ID*>(Packet.CharacterId);
    *Dst = *reinterpret_cast<const CHARACTER_ID*>(Args[0]);
    return true;
}

static bool DeliverCharacterSelect(const FPacket_FLPACKET_CLIENT_SELECTCHARACTER& Packet,
                                   IServer* Server, unsigned int SessionId) {
    Server->CharacterSelect(*reinterpret_cast<const CHARACTER_ID*>(Packet.CharacterId), SessionId);
    return true;
}

static bool DeliverDestroyCharacter(const FPacket_FLPACKET_CLIENT_DESTROYCHAR& Packet,
                                    IServer* Server, unsigned int SessionId) {
    Server->DestroyCharacter(*reinterpret_cast<const CHARACTER_ID*>(Packet.CharacterId), SessionId);
    return true;
}

// ===========================================================================
// ch3 0x08 REQUESTBASEINFO / 0x09 REQUESTLOCATIONINFO -- sender 0x06d8d500 / 0x06d8d550.
// IServer slots 39 (+0x9c) and 40 (+0xa0), both (uint, uint, bool).
// ===========================================================================
static bool MarshalInfoRequest(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientInfoRequest Wire;
    Wire.Id = Args[0];
    Wire.bFull = (unsigned char)(Args[2] & 0xffu);
    Wire.Pad_0x05[0] = 0;
    Wire.Pad_0x05[1] = 0;
    Wire.Pad_0x05[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverBaseInfoRequest(const FPacket_FLPACKET_CLIENT_REQUESTBASEINFO& Packet,
                                   IServer* Server, unsigned int SessionId) {
    const FPayloadClientInfoRequest& Wire = Packet.Wire;
    Server->BaseInfoRequest(Wire.Id, SessionId, Wire.bFull != 0);
    return true;
}

static bool DeliverLocationInfoRequest(const FPacket_FLPACKET_CLIENT_REQUESTLOCATIONINFO& Packet,
                                       IServer* Server, unsigned int SessionId) {
    const FPayloadClientInfoRequest& Wire = Packet.Wire;
    Server->LocationInfoRequest(Wire.Id, SessionId, Wire.bFull != 0);
    return true;
}

// ===========================================================================
// ch3 0x0c OBJCOLLISION -- sender 0x06d8d7b0, receiver caseD_c. The halves of
// SSPObjCollisionInfo are swapped on the wire and swapped back on arrival.
// ===========================================================================
static bool MarshalObjCollision(FPacket_FLPACKET_CLIENT_OBJCOLLISION& Packet,
                                const unsigned int* Args, unsigned int ArgCount) {
    const SSPObjCollisionInfo* Info = reinterpret_cast<const SSPObjCollisionInfo*>(Args[0]);
    FPayloadClientObjCollision Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.SubObjId = Info->SubObjId;
    Wire.Pad_0x06 = 0;
    Wire.TargetObjectId = Info->TargetObjectId;
    Wire.TargetSubObjId = Info->TargetSubObjId;
    Wire.Pad_0x0e = 0;
    Wire.DamageBits = Info->DamageBits;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverObjCollision(const FPacket_FLPACKET_CLIENT_OBJCOLLISION& Packet, IServer* Server,
                                unsigned int SessionId) {
    const FPayloadClientObjCollision& Wire = Packet.Wire;
    SSPObjCollisionInfo Info;
    Info.TargetObjectId = Wire.TargetObjectId;
    Info.TargetSubObjId = Wire.TargetSubObjId;
    Info.Pad_0x06 = 0;
    Info.ObjectId = Wire.ObjectId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad_0x0e = 0;
    Info.DamageBits = Wire.DamageBits;
    Server->SPObjCollision(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x3d BADLANDSOBJCOLLISION -- sender 0x06d8d760, receiver caseD_3d. Same idea,
// a three-field rotation.
// ===========================================================================
static bool MarshalBadLandsObjCollision(FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION& Packet,
                                        const unsigned int* Args, unsigned int ArgCount) {
    const SSPBadLandsObjCollisionInfo* Info =
        reinterpret_cast<const SSPBadLandsObjCollisionInfo*>(Args[0]);
    FPayloadClientBadLandsObjCollision Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.SubObjId = Info->SubObjId;
    Wire.Pad_0x06 = 0;
    Wire.BadLandsObjId = Info->BadLandsObjId;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverBadLandsObjCollision(const FPacket_FLPACKET_CLIENT_BADLANDSOBJCOLLISION& Packet,
                                        IServer* Server, unsigned int SessionId) {
    const FPayloadClientBadLandsObjCollision& Wire = Packet.Wire;
    SSPBadLandsObjCollisionInfo Info;
    Info.BadLandsObjId = Wire.BadLandsObjId;
    Info.ObjectId = Wire.ObjectId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad_0x0a = 0;
    Server->SPBadLandsObjCollision(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x11 GFGOODSELL / 0x12 GFGOODBUY / 0x3c GFGOODVAPORIZED -- straight struct-head
// copies of three, four and three dwords. IServer slots 46 (+0xb8), 45 (+0xb4) and
// 42 (+0xa8). Retail never shows the total size of SGFGoodSellInfo / SGFGoodVaporizedInfo
// (hence no static_assert); only these three dwords travel.
// ===========================================================================
static bool DeliverGFGoodSell(const FPacket_FLPACKET_CLIENT_GFGOODSELL& Packet, IServer* Server,
                              unsigned int SessionId) {
    const FPayloadClientDwordTriple& Wire = Packet.Wire;
    SGFGoodSellInfo Info;
    Info.BaseId = Wire.Field_0x00;
    Info.GoodId = Wire.Field_0x04;
    Info.Count = Wire.Field_0x08;
    Server->GFGoodSell(Info, SessionId);
    return true;
}

static bool DeliverGFGoodVaporized(const FPacket_FLPACKET_CLIENT_GFGOODVAPORIZED& Packet,
                                   IServer* Server, unsigned int SessionId) {
    const FPayloadClientDwordTriple& Wire = Packet.Wire;
    SGFGoodVaporizedInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = Wire.Field_0x04;
    Info.Field_0x08 = Wire.Field_0x08;
    Server->GFGoodVaporized(Info, SessionId);
    return true;
}

static bool DeliverGFGoodBuy(const FPacket_FLPACKET_CLIENT_GFGOODBUY& Packet, IServer* Server,
                             unsigned int SessionId) {
    const FPayloadClientGFGoodBuy& Wire = Packet.Wire;
    SGFGoodBuyInfo Info;
    Info.BaseId = Wire.BaseId;
    Info.Null04 = Wire.Null04;
    Info.GoodId = Wire.GoodId;
    Info.Count = Wire.Count;
    Server->GFGoodBuy(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x14 MISSIONRESPONSE -- sender 0x06d8da80, IServer slot 43 (+0xac).
// ===========================================================================
static bool MarshalMissionResponse(FPacket_FLPACKET_CLIENT_MISSIONRESPONSE& Packet,
                                   const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientMissionResponse Wire;
    Wire.OfferId = Args[0];
    Wire.OfferKind = Args[1];
    Wire.bAccepted = (unsigned char)(Args[2] & 0xffu);
    Wire.Pad_0x09[0] = 0;
    Wire.Pad_0x09[1] = 0;
    Wire.Pad_0x09[2] = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverMissionResponse(const FPacket_FLPACKET_CLIENT_MISSIONRESPONSE& Packet,
                                   IServer* Server, unsigned int SessionId) {
    const FPayloadClientMissionResponse& Wire = Packet.Wire;
    Server->MissionResponse(Wire.OfferId, Wire.OfferKind, Wire.bAccepted, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x18 REQADDITEM / 0x1a REQMODIFYITEM -- senders 0x06d8d230 / 0x06d8d300 plus the
// string writers FUN_06d9a160 / FUN_06d9a5f0. IServer slots 30 (+0x78) and 32 (+0x80).
// The packet owns the hardpoint copy, so both are self-contained and get a destructor.
// ===========================================================================
static bool MarshalReqAddItem(FPacket_FLPACKET_CLIENT_REQADDITEM& Packet, const unsigned int* Args,
                              unsigned int ArgCount) {
    const char* Hardpoint = reinterpret_cast<const char*>(Args[1]);
    const unsigned int Length = RpcStringLength(Hardpoint) + 1u;
    FPayloadClientReqAddItem Wire;
    Wire.ArchId = Args[0];
    Wire.Count = (int)Args[2];
    Wire.HardpointSize = 0u;
    Wire.Health = *reinterpret_cast<const float*>(&Args[3]);
    Wire.bMounted = (unsigned char)(Args[4] & 0xffu);
    Wire.Pad_0x11[0] = 0;
    Wire.Pad_0x11[1] = 0;
    Wire.Pad_0x11[2] = 0;
    // FUN_06d9a160(hardpoint, strlen + 1), with its own guard.
    Wire.Hardpoint = nullptr;
    if (Length != 0 && Hardpoint != nullptr) {
        Wire.Hardpoint = static_cast<char*>(RpcCopyBlock(Hardpoint, Length));
        Wire.HardpointSize = Length;
    }
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverReqAddItem(const FPacket_FLPACKET_CLIENT_REQADDITEM& Packet, IServer* Server,
                              unsigned int SessionId) {
    const FPayloadClientReqAddItem& Wire = Packet.Wire;
    Server->ReqAddItem(Wire.ArchId, Wire.Hardpoint, Wire.Count, Wire.Health,
                       Wire.bMounted, SessionId);
    return true;
}

static bool MarshalReqModifyItem(FPacket_FLPACKET_CLIENT_REQMODIFYITEM& Packet,
                                 const unsigned int* Args, unsigned int ArgCount) {
    const char* Hardpoint = reinterpret_cast<const char*>(Args[1]);
    const unsigned int Length = RpcStringLength(Hardpoint) + 1u;
    FPayloadClientReqModifyItem Wire;
    Wire.Count = (int)Args[2];
    Wire.HardpointSize = 0u;
    Wire.EquipId = Args[0] & 0xffffu;
    Wire.Health = *reinterpret_cast<const float*>(&Args[3]);
    Wire.bMounted = (unsigned char)(Args[4] & 0xffu);
    Wire.Pad_0x11[0] = 0;
    Wire.Pad_0x11[1] = 0;
    Wire.Pad_0x11[2] = 0;
    // FUN_06d9a5f0(hardpoint, strlen + 1), with its own guard.
    Wire.Hardpoint = nullptr;
    if (Length != 0 && Hardpoint != nullptr) {
        Wire.Hardpoint = static_cast<char*>(RpcCopyBlock(Hardpoint, Length));
        Wire.HardpointSize = Length;
    }
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverReqModifyItem(const FPacket_FLPACKET_CLIENT_REQMODIFYITEM& Packet,
                                 IServer* Server, unsigned int SessionId) {
    const FPayloadClientReqModifyItem& Wire = Packet.Wire;
    // Retail reads payload+0x08 as a word, not the dword the sender wrote.
    Server->ReqModifyItem((unsigned short)(Wire.EquipId & 0xffffu), Wire.Hardpoint, Wire.Count,
                          Wire.Health, Wire.bMounted, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x19 REQREMOVEITEM -- sender 0x06d8d2b0, IServer slot 31 (+0x7c).
// The sender zero-extends the ushort into a dword; the receiver reads it back as a word.
// ===========================================================================
static bool MarshalReqRemoveItem(FPacket_FLPACKET_CLIENT_REQREMOVEITEM& Packet,
                                 const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientReqRemoveItem Wire;
    Wire.EquipId = Args[0] & 0xffffu;
    Wire.Count = Args[1];
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverReqRemoveItem(const FPacket_FLPACKET_CLIENT_REQREMOVEITEM& Packet,
                                 IServer* Server, unsigned int SessionId) {
    const FPayloadClientReqRemoveItem& Wire = Packet.Wire;
    Server->ReqRemoveItem((unsigned short)(Wire.EquipId & 0xffffu), (int)Wire.Count, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x1e SAVEGAME -- sender 0x06d8db10 plus the wide-string writer FUN_06d9aab0.
// IServer slot 58 (+0xe8). The packet owns the description copy.
// ===========================================================================
static bool MarshalSaveGame(FPacket_FLPACKET_CLIENT_SAVEGAME& Packet, const unsigned int* Args,
                            unsigned int ArgCount) {
    // CHARACTER_ID::operator= into packet+0x20, as MarshalCharacterId does.
    CHARACTER_ID* Dst = reinterpret_cast<CHARACTER_ID*>(Packet.Wire.CharId);
    *Dst = *reinterpret_cast<const CHARACTER_ID*>(Args[0]);
    // wcslen with no pointer test, then FUN_06d9aab0(description, wcslen + 1) with its own
    // guard.
    const wchar_t* Description = reinterpret_cast<const wchar_t*>(Args[1]);
    const unsigned int Count = RpcCh3WideLength(Description) + 1u;
    wchar_t* Block = nullptr;
    unsigned int StoredCount = 0u;
    if (Count != 0 && Description != nullptr) {
        Block = static_cast<wchar_t*>(RpcCopyBlock(Description, Count * 2u));
        StoredCount = Count;
    }
    Packet.Wire.Description = Block;
    Packet.Wire.DescriptionCount = StoredCount;
    return true;
}

static bool DeliverSaveGame(const FPacket_FLPACKET_CLIENT_SAVEGAME& Packet, IServer* Server,
                            unsigned int SessionId) {
    const wchar_t* Description = Packet.Wire.Description;
    Server->SaveGame(*reinterpret_cast<const CHARACTER_ID*>(Packet.Wire.CharId), Description, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x20 MINEASTEROID -- sender 0x06d8dcf0, IServer slot 62 (+0xf8). The receiver
// rebuilds the Vector on its own stack between the first and the last three dwords.
// ===========================================================================
static bool MarshalMineAsteroid(FPacket_FLPACKET_CLIENT_MINEASTEROID& Packet,
                                const unsigned int* Args, unsigned int ArgCount) {
    const Vector* Position = reinterpret_cast<const Vector*>(Args[1]);
    FPayloadClientMineAsteroid Wire;
    Wire.SystemId = Args[0];
    Wire.PositionX = Position->x;
    Wire.PositionY = Position->y;
    Wire.PositionZ = Position->z;
    Wire.LootId = Args[2];
    Wire.SecondaryLoot = Args[3];
    Wire.Quantity = Args[4];
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverMineAsteroid(const FPacket_FLPACKET_CLIENT_MINEASTEROID& Packet, IServer* Server,
                                unsigned int SessionId) {
    const FPayloadClientMineAsteroid& Wire = Packet.Wire;
    Vector Position;
    Position.x = Wire.PositionX;
    Position.y = Wire.PositionY;
    Position.z = Wire.PositionZ;
    Server->MineAsteroid(Wire.SystemId, Position, Wire.LootId, Wire.SecondaryLoot,
                         Wire.Quantity, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x24 DOCK -- sender 0x06d8dbb0 dereferences its FIRST argument (uint const&).
// IServer slot 91 (+0x16c) Dock(uint const&, uint const&); the receiver passes
// packet+0x20 for the first and the ADDRESS of its own session local for the second.
// ===========================================================================
static bool DeliverDock(const FPacket_FLPACKET_CLIENT_DOCK& Packet, IServer* Server,
                        unsigned int SessionId) {
    Server->Dock(Packet.Wire.Field_0x00, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x27 TRADERESPONSE -- sender 0x06d8dad0 via FUN_06d97b00 (length first, block
// second). IServer slot 44 (+0xb0) TradeResponse(uchar const*, int, uint).
// ===========================================================================
static bool MarshalTradeResponse(FPacket_FLPACKET_CLIENT_TRADERESPONSE& Packet,
                                 const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientBlob Wire;
    Wire.Size = Args[1];
    // FUN_06d97b00: bare operator new(len) and the copy, no guard at all.
    Wire.Buffer = RpcCopyBlock(reinterpret_cast<const void*>(Args[0]), Args[1]);
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverTradeResponse(const FPacket_FLPACKET_CLIENT_TRADERESPONSE& Packet,
                                 IServer* Server, unsigned int SessionId) {
    const FPayloadClientBlob& Wire = Packet.Wire;
    Server->TradeResponse(static_cast<const unsigned char*>(Wire.Buffer), (int)Wire.Size, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x2b CARGOSCAN -- sender 0x06d8ebd0 dereferences both `uint const&` arguments.
// IServer slot 65 (+0x104); the receiver passes packet+0x20 and packet+0x24 as the two
// references, so the packet's own storage backs them.
// ===========================================================================
static bool MarshalSPScanCargo(FPacket_FLPACKET_CLIENT_CARGOSCAN& Packet, const unsigned int* Args,
                               unsigned int ArgCount) {
    FPayloadClientScanCargo Wire;
    Wire.ScannerId = *reinterpret_cast<const unsigned int*>(Args[0]);
    Wire.TargetId = *reinterpret_cast<const unsigned int*>(Args[1]);
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverSPScanCargo(const FPacket_FLPACKET_CLIENT_CARGOSCAN& Packet, IServer* Server,
                               unsigned int SessionId) {
    const FPayloadClientScanCargo* Wire =
        &Packet.Wire;
    Server->SPScanCargo(Wire->ScannerId, Wire->TargetId, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x2f SETMANEUVER -- sender 0x06d8e670, IServer slot 66 (+0x108)
// SetManeuver(uint Session, XSetManeuver const&): the session comes FIRST.
// ===========================================================================
static bool MarshalSetManeuver(FPacket_FLPACKET_CLIENT_SETMANEUVER& Packet,
                               const unsigned int* Args, unsigned int ArgCount) {
    const XSetManeuver* Info = reinterpret_cast<const XSetManeuver*>(Args[1]);
    FPayloadClientDwordTriple Wire;
    Wire.Field_0x00 = Info->ShipFrom;
    Wire.Field_0x04 = Info->ObjectId;
    Wire.Field_0x08 = Info->Flag;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverSetManeuver(const FPacket_FLPACKET_CLIENT_SETMANEUVER& Packet, IServer* Server,
                               unsigned int SessionId) {
    const FPayloadClientDwordTriple& Wire = Packet.Wire;
    XSetManeuver Info;
    Info.ShipFrom = Wire.Field_0x00;
    Info.ObjectId = Wire.Field_0x04;
    Info.Flag = Wire.Field_0x08;
    Server->SetManeuver(SessionId, Info);
    return true;
}

// ===========================================================================
// ch3 0x31 REQUEST_EVENT / 0x32 REQUEST_CANCEL -- senders 0x06d8eaf0 / 0x06d8eb60,
// IServer slots 60 (+0xf0) and 61 (+0xf4). Retail truncates the int event type and the
// trailing ulong to one byte each on the way out and extends them back on the way in.
// ===========================================================================
static bool MarshalRequestEvent(FPacket_FLPACKET_CLIENT_REQUEST_EVENT& Packet,
                                const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientRequestEvent Wire;
    Wire.EventType = (unsigned char)(Args[0] & 0xffu);
    Wire.Pad_0x01[0] = 0;
    Wire.Pad_0x01[1] = 0;
    Wire.Pad_0x01[2] = 0;
    Wire.ShipId = Args[1];
    Wire.TargetId = Args[2];
    Wire.SecondId = Args[3];
    Wire.bFlag = (unsigned char)(Args[4] & 0xffu);
    Wire.Pad_0x11[0] = 0;
    Wire.Pad_0x11[1] = 0;
    Wire.Pad_0x11[2] = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverRequestEvent(const FPacket_FLPACKET_CLIENT_REQUEST_EVENT& Packet,
                                IServer* Server, unsigned int SessionId) {
    const FPayloadClientRequestEvent& Wire = Packet.Wire;
    Server->RequestEvent((int)Wire.EventType, Wire.ShipId, Wire.TargetId, Wire.SecondId,
                         (unsigned long)Wire.bFlag, SessionId);
    return true;
}

static bool MarshalRequestCancel(FPacket_FLPACKET_CLIENT_REQUEST_CANCEL& Packet,
                                 const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientRequestCancel Wire;
    Wire.EventType = (unsigned char)(Args[0] & 0xffu);
    Wire.Pad_0x01[0] = 0;
    Wire.Pad_0x01[1] = 0;
    Wire.Pad_0x01[2] = 0;
    Wire.ShipId = Args[1];
    Wire.TargetId = Args[2];
    Wire.Field_0x0c = (unsigned char)(Args[3] & 0xffu);
    Wire.Pad_0x0d[0] = 0;
    Wire.Pad_0x0d[1] = 0;
    Wire.Pad_0x0d[2] = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverRequestCancel(const FPacket_FLPACKET_CLIENT_REQUEST_CANCEL& Packet,
                                 IServer* Server, unsigned int SessionId) {
    const FPayloadClientRequestCancel& Wire = Packet.Wire;
    Server->RequestCancel((int)Wire.EventType, Wire.ShipId, Wire.TargetId,
                          (unsigned long)Wire.Field_0x0c, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x37 COMMCOMPLETE -- sender 0x06d8dd70, IServer slot 63 (+0xfc). The slot takes four
// payload values and no session id.
// ===========================================================================
static bool MarshalCommComplete(FPacket_FLPACKET_CLIENT_COMMCOMPLETE& Packet,
                                const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientCommComplete Wire;
    Wire.SenderId = Args[0];
    Wire.ReceiverId = Args[1];
    Wire.MsgId = Args[2];
    Wire.Result = Args[3];
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverCommComplete(const FPacket_FLPACKET_CLIENT_COMMCOMPLETE& Packet, IServer* Server,
                                unsigned int) {
    const FPayloadClientCommComplete& Wire = Packet.Wire;
    Server->CommComplete(Wire.SenderId, Wire.ReceiverId, Wire.MsgId, (CommResult)Wire.Result);
    return true;
}

// ===========================================================================
// ch3 0x39 CREATENEWCHAR -- sender 0x06d8cdf0, IServer slot 22 (+0x58). As with LOGIN,
// retail's receiver fills a file-static SCreateCharacterInfo (.data:0x06dbbb10) and
// passes its address.
// ===========================================================================
static SCreateCharacterInfo GStaticCreateCharacterInfo;   // retail .data:0x06dbbb10

static bool MarshalCreateNewCharacter(FPacket_FLPACKET_CLIENT_CREATENEWCHAR& Packet,
                                      const unsigned int* Args, unsigned int ArgCount) {
    const SCreateCharacterInfo* Info = reinterpret_cast<const SCreateCharacterInfo*>(Args[0]);
    // wcsncpy of 0x40 wide characters out of the 0x18-character Name, as retail asks (it
    // stops at the first NUL and pads); all 0x90 payload bytes are written.
    FPayloadClientCreateNewChar Wire;
    wcsncpy(Wire.Name, Info->Name, 0x40);
    Wire.FactionId = Info->FactionId;
    Wire.BaseId = Info->BaseId;
    Wire.PackageId = Info->PackageId;
    Wire.PilotId = Info->PilotId;
    Wire.Name[0x3f] = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverCreateNewCharacter(const FPacket_FLPACKET_CLIENT_CREATENEWCHAR& Packet,
                                      IServer* Server, unsigned int SessionId) {
    const FPayloadClientCreateNewChar& Wire = Packet.Wire;
    // Retail quirk: unbounded wcscpy into the static's Name, no clearing; the four dwords
    // stored after it overwrite whatever a long name spilled into them.
    wcscpy(GStaticCreateCharacterInfo.Name, Wire.Name);
    GStaticCreateCharacterInfo.FactionId = Wire.FactionId;
    GStaticCreateCharacterInfo.BaseId = Wire.BaseId;
    GStaticCreateCharacterInfo.PackageId = Wire.PackageId;
    GStaticCreateCharacterInfo.PilotId = Wire.PilotId;
    Server->CreateNewCharacter(GStaticCreateCharacterInfo, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x3e LAUNCHCOMPLETE / 0x43 JUMPINCOMPLETE -- senders 0x06d8d8e0 / 0x06d8d930,
// IServer slots 49 (+0xc4) and 50 (+0xc8). Both arguments travel; no session is appended.
// ===========================================================================
static bool MarshalDwordPair(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientDwordPair Wire;
    Wire.Field_0x00 = Args[0];
    Wire.Field_0x04 = Args[1];
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverLaunchComplete(const FPacket_FLPACKET_CLIENT_LAUNCHCOMPLETE& Packet,
                                  IServer* Server, unsigned int) {
    const FPayloadClientLaunchComplete& Wire = Packet.Wire;
    Server->LaunchComplete(Wire.BaseObjId, Wire.ShipId);
    return true;
}

static bool DeliverJumpInComplete(const FPacket_FLPACKET_CLIENT_JUMPINCOMPLETE& Packet,
                                  IServer* Server, unsigned int) {
    const FPayloadClientJumpInComplete& Wire = Packet.Wire;
    Server->JumpInComplete(Wire.SystemId, Wire.ShipId);
    return true;
}

// ===========================================================================
// ch3 0x3f HAIL -- sender 0x06d8d980, IServer slot 51 (+0xcc). All three arguments
// travel; no session is appended.
// ===========================================================================
static bool MarshalHail(FPacket_FLPACKET_CLIENT_HAIL& Packet, const unsigned int* Args,
                        unsigned int ArgCount) {
    FPayloadClientHail Wire;
    Wire.ShipId = Args[0];
    Wire.TargetId = Args[1];
    Wire.SystemId = Args[2];
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverHail(const FPacket_FLPACKET_CLIENT_HAIL& Packet, IServer* Server, unsigned int) {
    const FPayloadClientHail& Wire = Packet.Wire;
    Server->Hail(Wire.ShipId, Wire.TargetId, Wire.SystemId);
    return true;
}

// ===========================================================================
// ch3 0x40 REQUEST_USE_ITEM -- sender 0x06d8ec20, IServer slot 56 (+0xe0). Identity
// mapping of SSPUseItem.
// ===========================================================================
static bool MarshalSPRequestUseItem(FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM& Packet,
                                    const unsigned int* Args, unsigned int ArgCount) {
    const SSPUseItem* Info = reinterpret_cast<const SSPUseItem*>(Args[0]);
    FPayloadClientUseItem Wire;
    Wire.ObjectId = Info->ObjectId;
    Wire.ItemId = Info->ItemId;
    Wire.Count = Info->Count;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverSPRequestUseItem(const FPacket_FLPACKET_CLIENT_REQUEST_USE_ITEM& Packet,
                                    IServer* Server, unsigned int SessionId) {
    const FPayloadClientUseItem& Wire = Packet.Wire;
    SSPUseItem Info;
    Info.ObjectId = Wire.ObjectId;
    Info.ItemId = Wire.ItemId;
    Info.Count = Wire.Count;
    Server->SPRequestUseItem(Info, SessionId);
    return true;
}

// ===========================================================================
// ch3 0x44 REQINVINCIBILITY -- sender 0x06d8ec80, IServer slot 57 (+0xe4). The sender
// crosses the two byte arguments (reason at +0x04, bool at +0x05) and the receiver
// crosses them back, which is what tells the two bytes apart.
// ===========================================================================
static bool MarshalSPRequestInvincibility(FPacket_FLPACKET_CLIENT_REQINVINCIBILITY& Packet,
                                          const unsigned int* Args, unsigned int ArgCount) {
    FPayloadClientInvincibility Wire;
    Wire.ShipId = Args[0];
    Wire.Reason = (unsigned char)(Args[2] & 0xffu);
    Wire.bInvincible = (unsigned char)(Args[1] & 0xffu);
    Wire.Pad_0x06[0] = 0;
    Wire.Pad_0x06[1] = 0;
    return RpcPayloadWrite(Packet.Wire, Wire);
}

static bool DeliverSPRequestInvincibility(const FPacket_FLPACKET_CLIENT_REQINVINCIBILITY& Packet,
                                          IServer* Server, unsigned int SessionId) {
    const FPayloadClientInvincibility& Wire = Packet.Wire;
    Server->SPRequestInvincibility(Wire.ShipId, Wire.bInvincible,
                                   (InvincibilityReason)Wire.Reason, SessionId);
    return true;
}

// ===========================================================================
// The six debug-only channel-3 types. Retail registers them but release has no sender; the
// dispatcher answers each with an FDUMP naming retail's source file and line.
// ===========================================================================
static const char* const kServerPacketHandlerFile =
    "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp";

// DACOM FDUMP via import .rdata:06db311c, severity 0x100001, format .rdata:06db389c (no
// trailing newline), file string .rdata:06db38b4.
static bool ReportDebugPacket(int Line, const char* Message) {
    FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kServerPacketHandlerFile, Line, Message);
    return false;
}

static bool DeliverDbgCreateShip(const FPacket_FLPACKET_CLIENT_DBGCREATESHIP&, IServer*,
                                 unsigned int) {
    return ReportDebugPacket(0x26a, "FLPACKET_CLIENT_DBGCREATESHIP encountered in release mode");
}
static bool DeliverDbgLoadSystem(const FPacket_FLPACKET_CLIENT_DBGLOADSYSTEM&, IServer*,
                                 unsigned int) {
    return ReportDebugPacket(0x217, "FLPACKET_CLIENT_DBGLOADSYSTEM encountered in release mode");
}
static bool DeliverDbgDestroyObject(const FPacket_FLPACKET_CLIENT_DBGDESTROYOBJECT&, IServer*,
                                    unsigned int) {
    return ReportDebugPacket(0x1e4, "FLPACKET_CLIENT_DBGDESTROYOBJECT encountered in Final Release mode");
}
static bool DeliverDbgConsole(const FPacket_FLPACKET_CLIENT_DBGCONSOLE&, IServer*, unsigned int) {
    return ReportDebugPacket(0x204, "FLPACKET_CLIENT_DBGCONSOLE encountered in release mode");
}
static bool DeliverDbgFreeSystem(const FPacket_FLPACKET_CLIENT_DBGFREESYSTEM&, IServer*,
                                 unsigned int) {
    return ReportDebugPacket(0x227, "FLPACKET_CLIENT_DBGFREESYSTEM encountered in release mode");
}
static bool DeliverDbgRelocateShip(const FPacket_FLPACKET_CLIENT_DBGRELOCATE_SHIP&, IServer*,
                                   unsigned int) {
    return ReportDebugPacket(0x1f5, "FLPACKET_CLIENT_DBGRELOCATE_SHIP encountered in Final Release mode");
}

// ===========================================================================
// The three container types, closed by the serializer in rpc_containers.{h,cpp}.
//
// 0x16 REQEQUIPMENT  -- sender IServerImpl::ReqEquipment       (0x06d8cf90)
//                       receiver FUN_06d87ec0, IServer slot 28 (+0x70)
// 0x17 REQCARGO      -- sender IServerImpl::ReqCargo           (0x06d8d0e0)
//                       receiver FUN_06d880a0, IServer slot 29 (+0x74)
// 0x36 REQCOLLISIONGROUPS
//                    -- sender IServerImpl::ReqCollisionGroups (0x06d8cf50)
//                       receiver FUN_06d8f7d0, IServer slot 27 (+0x6c)
//
// The two REQ*EQUIPMENT senders differ only in the opcode they allocate, their receivers
// only in the IServer slot they call (+0x70 vs +0x74). Registry sizes: REQEQUIPMENT and
// REQCARGO 0x24 (payload = the handle pointer), REQCOLLISIONGROUPS 0x2c (payload 0x0c = the
// std::list header itself). Each case pushes one container reference plus the session id.
//
// Both containers are the real Common types: retail imports the EquipDesc ctor, setters,
// make_internal and is_internal by name (.rdata:06db3004, 06db3070..06db308c, 06db30d8)
// and std::list<EquipDesc>::_Buynode (06db30ac). See docs/re/deviations.md.
// ===========================================================================

// Args[0] is the caller's EquipDescList, whose node chain is walked directly.
static bool MarshalReqEquipmentCommon(FRpcEquipListPacket& Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    const EquipDescList& Source = *reinterpret_cast<const EquipDescList*>(Args[0]);

    // The local handle is filled, deep-copied into the packet (FUN_06d8fa90) and destroyed
    // (FUN_06d91340), also on a throw.
    FRpcEquipWireHandle Handle;
    RpcEquipHandleFromList(&Handle, Source);
    Packet.AssignEquipHandle(&Handle);
    return true;
}

static bool DeliverReqEquipmentCommon(const FRpcEquipListPacket& Packet, IServer* Server,
                                      unsigned int SessionId, bool bCargo) {
    // Retail FUN_06d87ec0: an empty EquipDescList, then the handle (FUN_06d95d20 fills
    // it), the list refilled from it, one IServer call, and both destroyed in reverse order
    // (also on a throw).
    EquipDescList List;
    FRpcEquipWireHandle Handle;
    Packet.ExtractEquipHandle(&Handle);
    RpcEquipListFromHandle(List, &Handle);

    if (bCargo) {
        Server->ReqCargo(List, SessionId);
    } else {
        Server->ReqEquipment(List, SessionId);
    }

    return true;
}

static bool DeliverReqEquipment(const FPacket_FLPACKET_CLIENT_REQEQUIPMENT& Packet, IServer* Server,
                                unsigned int SessionId) {
    return DeliverReqEquipmentCommon(Packet, Server, SessionId, false);
}

static bool DeliverReqCargo(const FPacket_FLPACKET_CLIENT_REQCARGO& Packet, IServer* Server,
                            unsigned int SessionId) {
    return DeliverReqEquipmentCommon(Packet, Server, SessionId, true);
}

// 0x36 REQCOLLISIONGROUPS. Retail's sender splices the caller's list into the payload with
// FUN_06d8f710; its receiver passes packet+0x20, because the payload is the list object.
static bool MarshalReqCollisionGroups(FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS& Packet,
                                      const unsigned int* Args, unsigned int ArgCount) {
    const std::list<CollisionGroupDesc>& Source =
        *reinterpret_cast<const std::list<CollisionGroupDesc>*>(Args[0]);

    // FUN_06d8f710 walks the caller's list directly into the list the packet constructor
    // built; each value travels as 8 raw bytes. Its self-assignment test cannot fire here
    // (different types).
    FRpcCollisionGroupList* List = &Packet.Wire;
    RpcCollisionGroupListAssignRange(List, Source.begin(), Source.end());
    return true;
}

static bool DeliverReqCollisionGroups(const FPacket_FLPACKET_CLIENT_REQCOLLISIONGROUPS& Packet,
                                      IServer* Server, unsigned int SessionId) {
    // Retail passes the payload's address as the list reference. The port's std::list is
    // its own toolchain's container, so the node chain is copied into one first (as the
    // create-object header does, retail's inline copy at 06d8505c).
    std::list<CollisionGroupDesc> Groups;
    RpcCollisionGroupListFromWire(Groups, &Packet.Wire);
    Server->ReqCollisionGroups(Groups, SessionId);
    return true;
}

// ===========================================================================
// The channel-3 table. Ordered by opcode, not by the retail registrator's order.
// ===========================================================================
const FRpcServerCodec GChannel3Codecs[] = {
    { 3, 0x01, "FLPACKET_CLIENT_LOGIN",                      0x404,
      RpcMarshalAs<MarshalLogin>,                  RpcDeliverToServerAs<DeliverLogin> },
    { 3, 0x03, "FLPACKET_CLIENT_MUNCOLLISION",               0x1c,
      RpcMarshalAs<MarshalMunCollision>,           RpcDeliverToServerAs<DeliverMunCollision> },
    { 3, 0x04, "FLPACKET_CLIENT_REQUESTLAUNCH",              0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverPlayerLaunch> },
    { 3, 0x05, "FLPACKET_CLIENT_REQUESTCHARINFO",            0x00,
      MarshalCh3Empty,                             RpcDeliverToServerAs<DeliverCharacterInfoReq> },
    { 3, 0x06, "FLPACKET_CLIENT_SELECTCHARACTER",            0x200,
      RpcMarshalAs<MarshalCharacterId>,            RpcDeliverToServerAs<DeliverCharacterSelect> },
    { 3, 0x07, "FLPACKET_CLIENT_ENTERBASE",                  0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverBaseEnter> },
    { 3, 0x08, "FLPACKET_CLIENT_REQUESTBASEINFO",            0x08,
      MarshalInfoRequest,                          RpcDeliverToServerAs<DeliverBaseInfoRequest> },
    { 3, 0x09, "FLPACKET_CLIENT_REQUESTLOCATIONINFO",        0x08,
      MarshalInfoRequest,
      RpcDeliverToServerAs<DeliverLocationInfoRequest> },
    { 3, 0x0b, "FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE", 0x04,
      MarshalCh3DwordAt<0>,
      RpcDeliverToServerAs<DeliverSystemSwitchOutComplete> },
    { 3, 0x0c, "FLPACKET_CLIENT_OBJCOLLISION",               0x14,
      RpcMarshalAs<MarshalObjCollision>,           RpcDeliverToServerAs<DeliverObjCollision> },
    { 3, 0x0d, "FLPACKET_CLIENT_EXITBASE",                   0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverBaseExit> },
    { 3, 0x0e, "FLPACKET_CLIENT_ENTERLOCATION",              0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverLocationEnter> },
    { 3, 0x0f, "FLPACKET_CLIENT_EXITLOCATION",               0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverLocationExit> },
    { 3, 0x10, "FLPACKET_CLIENT_REQUESTCREATESHIP",          0x00,
      MarshalCh3Empty,                             RpcDeliverToServerAs<DeliverRequestCreateShip> },
    { 3, 0x11, "FLPACKET_CLIENT_GFGOODSELL",                 0x0c,
      MarshalCh3StructHead<3>,                     RpcDeliverToServerAs<DeliverGFGoodSell> },
    { 3, 0x12, "FLPACKET_CLIENT_GFGOODBUY",                  0x10,
      MarshalCh3StructHead<4>,                     RpcDeliverToServerAs<DeliverGFGoodBuy> },
    { 3, 0x13, "FLPACKET_CLIENT_GFSELECTOBJECT",             0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverGFObjSelect> },
    { 3, 0x14, "FLPACKET_CLIENT_MISSIONRESPONSE",            0x0c,
      RpcMarshalAs<MarshalMissionResponse>,        RpcDeliverToServerAs<DeliverMissionResponse> },
    { 3, 0x15, "FLPACKET_CLIENT_REQSHIPARCH",                0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverReqShipArch> },
    { 3, 0x16, "FLPACKET_CLIENT_REQEQUIPMENT",               0x04,
      RpcMarshalAs<MarshalReqEquipmentCommon>,     RpcDeliverToServerAs<DeliverReqEquipment> },
    { 3, 0x17, "FLPACKET_CLIENT_REQCARGO",                   0x04,
      RpcMarshalAs<MarshalReqEquipmentCommon>,     RpcDeliverToServerAs<DeliverReqCargo> },
    { 3, 0x18, "FLPACKET_CLIENT_REQADDITEM",                 0x18,
      RpcMarshalAs<MarshalReqAddItem>,             RpcDeliverToServerAs<DeliverReqAddItem> },
    { 3, 0x19, "FLPACKET_CLIENT_REQREMOVEITEM",              0x08,
      RpcMarshalAs<MarshalReqRemoveItem>,          RpcDeliverToServerAs<DeliverReqRemoveItem> },
    { 3, 0x1a, "FLPACKET_CLIENT_REQMODIFYITEM",              0x18,
      RpcMarshalAs<MarshalReqModifyItem>,          RpcDeliverToServerAs<DeliverReqModifyItem> },
    { 3, 0x1b, "FLPACKET_CLIENT_REQSETCASH",                 0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverReqSetCash> },
    { 3, 0x1c, "FLPACKET_CLIENT_REQCHANGECASH",              0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverReqChangeCash> },
    { 3, 0x1e, "FLPACKET_CLIENT_SAVEGAME",                   0x208,
      RpcMarshalAs<MarshalSaveGame>,               RpcDeliverToServerAs<DeliverSaveGame> },
    { 3, 0x20, "FLPACKET_CLIENT_MINEASTEROID",               0x1c,
      RpcMarshalAs<MarshalMineAsteroid>,           RpcDeliverToServerAs<DeliverMineAsteroid> },
    { 3, 0x22, "FLPACKET_CLIENT_DBGCREATESHIP",              0x70,
      nullptr,                                     RpcDeliverToServerAs<DeliverDbgCreateShip> },
    { 3, 0x23, "FLPACKET_CLIENT_DBGLOADSYSTEM",              0x08,
      nullptr,                                     RpcDeliverToServerAs<DeliverDbgLoadSystem> },
    { 3, 0x24, "FLPACKET_CLIENT_DOCK",                       0x04,
      MarshalCh3DwordByRefAt<0>,                   RpcDeliverToServerAs<DeliverDock> },
    { 3, 0x25, "FLPACKET_CLIENT_DBGDESTROYOBJECT",           0x08,
      nullptr,                                     RpcDeliverToServerAs<DeliverDbgDestroyObject> },
    { 3, 0x27, "FLPACKET_CLIENT_TRADERESPONSE",              0x08,
      RpcMarshalAs<MarshalTradeResponse>,          RpcDeliverToServerAs<DeliverTradeResponse> },
    { 3, 0x2b, "FLPACKET_CLIENT_CARGOSCAN",                  0x08,
      RpcMarshalAs<MarshalSPScanCargo>,            RpcDeliverToServerAs<DeliverSPScanCargo> },
    { 3, 0x2d, "FLPACKET_CLIENT_DBGCONSOLE",                 0x84,
      nullptr,                                     RpcDeliverToServerAs<DeliverDbgConsole> },
    { 3, 0x2e, "FLPACKET_CLIENT_DBGFREESYSTEM",              0x00,
      nullptr,                                     RpcDeliverToServerAs<DeliverDbgFreeSystem> },
    { 3, 0x2f, "FLPACKET_CLIENT_SETMANEUVER",                0x0c,
      RpcMarshalAs<MarshalSetManeuver>,            RpcDeliverToServerAs<DeliverSetManeuver> },
    { 3, 0x30, "FLPACKET_CLIENT_DBGRELOCATE_SHIP",           0x10,
      nullptr,                                     RpcDeliverToServerAs<DeliverDbgRelocateShip> },
    { 3, 0x31, "FLPACKET_CLIENT_REQUEST_EVENT",              0x14,
      RpcMarshalAs<MarshalRequestEvent>,           RpcDeliverToServerAs<DeliverRequestEvent> },
    { 3, 0x32, "FLPACKET_CLIENT_REQUEST_CANCEL",             0x10,
      RpcMarshalAs<MarshalRequestCancel>,          RpcDeliverToServerAs<DeliverRequestCancel> },
    { 3, 0x35, "FLPACKET_CLIENT_INTERFACEITEMUSED",          0x04,
      MarshalCh3DwordAt<1>,                        RpcDeliverToServerAs<DeliverInterfaceItemUsed> },
    { 3, 0x36, "FLPACKET_CLIENT_REQCOLLISIONGROUPS",         0x0c,
      RpcMarshalAs<MarshalReqCollisionGroups>,
      RpcDeliverToServerAs<DeliverReqCollisionGroups> },
    { 3, 0x37, "FLPACKET_CLIENT_COMMCOMPLETE",               0x10,
      RpcMarshalAs<MarshalCommComplete>,           RpcDeliverToServerAs<DeliverCommComplete> },
    { 3, 0x38, "FLPACKET_CLIENT_REQUESTNEWCHARINFO",         0x00,
      MarshalCh3Empty,
      RpcDeliverToServerAs<DeliverNewCharacterInfoReq> },
    { 3, 0x39, "FLPACKET_CLIENT_CREATENEWCHAR",              0x90,
      RpcMarshalAs<MarshalCreateNewCharacter>,
      RpcDeliverToServerAs<DeliverCreateNewCharacter> },
    { 3, 0x3a, "FLPACKET_CLIENT_DESTROYCHAR",                0x200,
      RpcMarshalAs<MarshalCharacterId>,            RpcDeliverToServerAs<DeliverDestroyCharacter> },
    { 3, 0x3b, "FLPACKET_CLIENT_REQHULLSTATUS",              0x04,
      MarshalCh3DwordAt<0>,                        RpcDeliverToServerAs<DeliverReqHullStatus> },
    { 3, 0x3c, "FLPACKET_CLIENT_GFGOODVAPORIZED",            0x0c,
      MarshalCh3StructHead<3>,                     RpcDeliverToServerAs<DeliverGFGoodVaporized> },
    { 3, 0x3d, "FLPACKET_CLIENT_BADLANDSOBJCOLLISION",       0x0c,
      RpcMarshalAs<MarshalBadLandsObjCollision>,
      RpcDeliverToServerAs<DeliverBadLandsObjCollision> },
    { 3, 0x3e, "FLPACKET_CLIENT_LAUNCHCOMPLETE",             0x08,
      MarshalDwordPair,                            RpcDeliverToServerAs<DeliverLaunchComplete> },
    { 3, 0x3f, "FLPACKET_CLIENT_HAIL",                       0x0c,
      RpcMarshalAs<MarshalHail>,                   RpcDeliverToServerAs<DeliverHail> },
    { 3, 0x40, "FLPACKET_CLIENT_REQUEST_USE_ITEM",           0x08,
      RpcMarshalAs<MarshalSPRequestUseItem>,       RpcDeliverToServerAs<DeliverSPRequestUseItem> },
    { 3, 0x41, "FLPACKET_CLIENT_ABORT_MISSION",              0x04,
      MarshalCh3DwordAt<1>,                        RpcDeliverToServerAs<DeliverAbortMission> },
    { 3, 0x42, "FLPACKET_CLIENT_SKIP_AUTOSAVE",              0x00,
      MarshalCh3Empty,
      RpcDeliverToServerAs<DeliverCharacterSkipAutosave> },
    { 3, 0x43, "FLPACKET_CLIENT_JUMPINCOMPLETE",             0x08,
      MarshalDwordPair,                            RpcDeliverToServerAs<DeliverJumpInComplete> },
    { 3, 0x44, "FLPACKET_CLIENT_REQINVINCIBILITY",           0x08,
      RpcMarshalAs<MarshalSPRequestInvincibility>,
      RpcDeliverToServerAs<DeliverSPRequestInvincibility> },
    { 3, 0x45, "FLPACKET_CLIENT_MISSIONSAVEB",               0x04,
      MarshalCh3DwordAt<1>,                        RpcDeliverToServerAs<DeliverMissionSaveB> },
    { 3, 0x46, "FLPACKET_CLIENT_REQDIFFICULTYSCALE",         0x04,
      MarshalCh3DwordAt<0>,
      RpcDeliverToServerAs<DeliverReqDifficultyScale> },
    { 3, 0x47, "FLPACKET_CLIENT_RTCDONE",                    0x04,
      MarshalCh3DwordAt<1>,                        RpcDeliverToServerAs<DeliverRTCDone> },
};

const unsigned int GChannel3CodecCount =
    (unsigned int)(sizeof(GChannel3Codecs) / sizeof(GChannel3Codecs[0]));

const FRpcServerCodec* RpcFindServerCodec(unsigned int Channel, unsigned int Opcode) {
    if (Channel != 3u) {
        return nullptr;
    }
    for (unsigned int i = 0; i < GChannel3CodecCount; ++i) {
        if (GChannel3Codecs[i].Opcode == (unsigned short)Opcode) {
            return &GChannel3Codecs[i];
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// AddTradeEquip / DelTradeEquip (IServer slots 83 and 84, +0x14c / +0x150) take
// `EquipDesc const&`, built with COMMON.DLL's EquipDesc ctor and setters in retail's order.
// ---------------------------------------------------------------------------

// ===========================================================================
// The channel-1 half of FUN_06d86a70: the channel-1 packet types delivered into IServer
// instead of IClient. Wire structs are those of rpc_payload_ch1.cpp; the 27 call sites (19
// plain cases plus the eight PLAYER_TRADE sub-types) match common_server.h's arities.
// IServerImpl serializes all 81 retail senders (rpclocal.cpp); five channel-1 types need an
// IServer-direction marshaller of their own (see the block comment above IServerImpl).
// ===========================================================================
static bool DispatchChannel1ToServer(const FPacketBase* Packet, IServer* Server,
                                     IChatServer* ChatServer, unsigned int SessionId) {
    switch (Packet->GetOpcode()) {
    case 0x01: {   // UPDATEOBJECT -> slot 52 SPObjUpdate
        const FPayloadCommonUpdateObject& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_UPDATEOBJECT*>(Packet)->Wire;
        SSPObjUpdateInfo Info;
        Info.ObjectId = Wire.ObjectId;
        Info.Orientation = Wire.Orientation;
        Info.Position = Wire.Position;
        // Retail widens the wire float into the struct's double with FLD/FSTP.
        Info.Timestamp = (double)Wire.Timestamp;
        // Retail copies packet+0x48 as a plain dword into the struct's +0x28.
        Info.ThrottleBits = *reinterpret_cast<const unsigned int*>(&Wire.Throttle);
        Info.State = Wire.State;
        Info.Pad_0x2d[0] = 0;
        Info.Pad_0x2d[1] = 0;
        Info.Pad_0x2d[2] = 0;
        Server->SPObjUpdate(Info, SessionId);
        return true;
    }
    case 0x02: {   // FIREWEAPON -> slot 0
        const FPayloadCommonFireWeapon& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_FIREWEAPON*>(Packet)->Wire;
        // A local XFireWeaponInfo; the hardpoint vector is a copy of the elements (inlined
        // operator=), never the packet's block, freed after the call.
        XFireWeaponInfo Info;
        Info.ObjectId = Wire.ObjectId;
        Info.TargetPos = Wire.TargetPos;
        const unsigned int Count = Wire.Hardpoints.Count();
        if (Count != 0) {
            Info.Hardpoints.assign(Wire.Hardpoints.Begin, Wire.Hardpoints.Begin + Count);
        }
        Server->FireWeapon(SessionId, Info);
        return true;
    }
    case 0x04: {   // SETTARGET -> slot 4. The wire packs both ids first, XSetTarget interleaves.
        const FPayloadCommonSetTarget& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_SETTARGET*>(Packet)->Wire;
        XSetTarget Info;
        Info.ObjectId = Wire.ObjectId;
        Info.SubObjId = Wire.SubObjId;
        Info.Pad_0x06 = 0;
        Info.TargetObjectId = Wire.TargetObjectId;
        Info.TargetSubObjId = Wire.TargetSubObjId;
        Info.Pad_0x0e = 0;
        Server->SetTarget(SessionId, Info);
        return true;
    }
    case 0x05: {   // CHATMSG -> IChatServer slot 0. Retail: SubmitChat(Session & 0xffff,
                   // payload+0x08, payload+0x0c, payload+0x00, -1).
        if (ChatServer == nullptr) {
            return false;
        }
        const FPayloadCommonChatMsg& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_CHATMSG*>(Packet)->Wire;
        const CHAT_ID Sender(SessionId & 0xffffu);
        const CHAT_ID Target(Wire.TargetId);
        ChatServer->SubmitChat(Sender, Wire.Size, Wire.Buffer, Target, -1);
        return true;
    }
    case 0x08: {   // ACTIVATEEQUIP -> slot 1
        const FPayloadCommonActivateEquip& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_ACTIVATEEQUIP*>(Packet)->Wire;
        XActivateEquip Info;
        Info.ObjectId = Wire.ObjectId;
        Info.EquipId = Wire.EquipId;
        Info.bActivate = (Wire.bActivate != 0);
        Info.Pad_0x07 = 0;
        Server->ActivateEquip(SessionId, Info);
        return true;
    }
    case 0x0e: {   // ACTIVATECRUISE -> slot 2
        const FPayloadCommonActivateCruise& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_ACTIVATECRUISE*>(Packet)->Wire;
        XActivateCruise Info;
        Info.ObjectId = Wire.ObjectId;
        Info.bActivate = (Wire.bActivate != 0);
        Info.bDisruptCruise = (unsigned char)(Wire.bDisruptCruise != 0 ? 1 : 0);
        Info.Pad_0x06[0] = 0;
        Info.Pad_0x06[1] = 0;
        Server->ActivateCruise(SessionId, Info);
        return true;
    }
    case 0x0f: {   // GOTRADELANE -> slot 6
        const FPayloadCommonGoTradelane& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_GOTRADELANE*>(Packet)->Wire;
        XGoTradelane Info;
        Info.ObjectId = Wire.ObjectId;
        Info.TradelaneRing1 = Wire.TradelaneRing1;
        Info.TradelaneRing2 = Wire.TradelaneRing2;
        Info.bMidLane = (Wire.bMidLane != 0);
        Info.Pad_0x0d[0] = 0;
        Info.Pad_0x0d[1] = 0;
        Info.Pad_0x0d[2] = 0;
        Server->GoTradelane(SessionId, Info);
        return true;
    }
    case 0x10: {   // STOPTRADELANE -> slot 7: ship, entrance and exit from the
                   // IServerImpl sender 06d8e770; SessionId comes from this dispatcher.
        const FPayloadCommonStopTradelane& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_STOPTRADELANE*>(Packet)->Wire;
        Server->StopTradelane(SessionId, Wire.ShipId, Wire.TradelaneRing1, Wire.TradelaneRing2);
        return true;
    }
    case 0x11: {   // SET_WEAPON_GROUP -> slot 70
        const FPayloadCommonBuffer& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_SET_WEAPON_GROUP*>(Packet)->Wire;
        Server->SetWeaponGroup(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x12: {   // PLAYER_TRADE -> the eight trade slots, picked by the bitmask at +0x00
        const FPayloadCommonPlayerTrade& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_PLAYER_TRADE*>(Packet)->Wire;
        switch (Wire.SubType) {
        case 0x01:
            Server->InitiateTrade(SessionId, Wire.ShipId);
            return true;
        case 0x02:
            Server->TerminateTrade(SessionId, (int)Wire.Reason);
            return true;
        case 0x04:
            Server->SetTradeMoney(SessionId, Wire.Money);
            return true;
        case 0x08: {
            EquipDesc Desc;
            Desc.set_arch_id(Wire.EquipArchId);
            Desc.set_count((int)Wire.EquipCount);
            Desc.set_status(Wire.EquipStatus);
            Desc.set_id(Wire.EquipId);                       // zero-extended word
            Server->AddTradeEquip(SessionId, Desc);          // IServer+0x14c
            return true;
        }
        case 0x10: {
            // Retail never calls set_status here; health stays at the constructor default.
            EquipDesc Desc;
            Desc.set_arch_id(Wire.EquipArchId);
            Desc.set_count((int)Wire.EquipCount);
            Desc.set_id(Wire.EquipId);
            Server->DelTradeEquip(SessionId, Desc);          // IServer+0x150
            return true;
        }
        case 0x20:
            Server->RequestTrade(SessionId, Wire.ShipId);
            return true;
        case 0x40:
            Server->AcceptTrade(SessionId, Wire.bAccepted != 0);
            return true;
        case 0x80:
            // Retail quirk: the one sub-type that does not forward the session id;
            // StopTradeRequest gets payload+0x08 instead.
            Server->StopTradeRequest(Wire.ShipId);
            return true;
        default:
            return false;
        }
    }
    case 0x13: {   // SET_VISITED_STATE -> slot 71
        const FPayloadCommonBuffer& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_SET_VISITED_STATE*>(Packet)->Wire;
        Server->SetVisitedState(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x14: {   // JETTISONCARGO -> slot 8
        const FPayloadCommonJettisonCargo& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_JETTISONCARGO*>(Packet)->Wire;
        XJettisonCargo Info;
        Info.ObjectId = Wire.ObjectId;
        Info.ItemId = Wire.ItemId;
        Info.Pad_0x06 = 0;
        Info.Count = Wire.Count;
        Server->JettisonCargo(SessionId, Info);
        return true;
    }
    case 0x15: {   // ACTIVATETHRUSTERS -> slot 3
        const FPayloadCommonActivateThrusters& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_ACTIVATETHRUSTERS*>(Packet)->Wire;
        XActivateThrusters Info;
        Info.ObjectId = Wire.ObjectId;
        Info.bActivate = (Wire.bActivate != 0);
        Info.Pad_0x05[0] = 0;
        Info.Pad_0x05[1] = 0;
        Info.Pad_0x05[2] = 0;
        Server->ActivateThrusters(SessionId, Info);
        return true;
    }
    case 0x16: {   // REQUEST_BEST_PATH -> slot 72
        const FPayloadCommonBuffer& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_REQUEST_BEST_PATH*>(Packet)->Wire;
        Server->RequestBestPath(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x17: {   // REQUEST_GROUP_POSITIONS -> slot 75
        const FPayloadCommonBuffer& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_REQUEST_GROUP_POSITIONS*>(Packet)->Wire;
        Server->RequestGroupPositions(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x18: {   // REQUEST_PLAYER_STATS -> slot 73
        const FPayloadCommonBuffer& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_REQUEST_PLAYER_STATS*>(Packet)->Wire;
        Server->RequestPlayerStats(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x19: {   // SET_MISSION_LOG -> slot 76
        const FPayloadCommonBuffer& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_SET_MISSION_LOG*>(Packet)->Wire;
        Server->SetMissionLog(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x1a: {   // REQUEST_RANK_LEVEL -> slot 78
        const FPayloadCommonBuffer& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_REQUEST_RANK_LEVEL*>(Packet)->Wire;
        Server->RequestRankLevel(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x1b: {   // POP_UP_DIALOG -> slot 74. Retail forwards ONLY payload+0x10; the two
                   // flattened FmtStr blocks are dropped on this side of the wire.
        const FPayloadCommonPopUpDialog& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_POP_UP_DIALOG*>(Packet)->Wire;
        Server->PopUpDialog(SessionId, Wire.Buttons);
        return true;
    }
    case 0x1c: {   // SET_INTERFACE_STATE -> slot 77
        const FPayloadCommonBuffer& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_SET_INTERFACE_STATE*>(Packet)->Wire;
        Server->SetInterfaceState(SessionId, static_cast<unsigned char*>(Wire.Buffer), (int)Wire.Size);
        return true;
    }
    case 0x1d: {   // TRACTOROBJECTS -> slot 5
        const FPayloadCommonTractorObjects& Wire =
            static_cast<const FPacket_FLPACKET_COMMON_TRACTOROBJECTS*>(Packet)->Wire;
        XTractorObjects Info;
        Info.ObjectId = Wire.ObjectId;
        Info.TractorEquipId = Wire.TractorEquipId;
        Info.Pad_0x06 = 0;
        const unsigned int Count = Wire.TargetIds.Count();
        if (Count != 0u) {
            Info.TargetIds.assign(Wire.TargetIds.Begin, Wire.TargetIds.End);
        }
        Server->TractorObjects(SessionId, Info);
        return true;
    }
    default:
        // Retail drops the packet silently.
        return false;
    }
}

// ===========================================================================
// RpcDispatchToServer -- the entry point, retail FUN_06d86a70(param_1, packet).
//
// Retail also releases the packet on every path (FUN_06d8f4a0); here the release stays with
// the caller (the pump in rpc_client_proxy.cpp), so this function only routes.
// ===========================================================================
bool RpcDispatchToServer(FPacketBase* Packet, unsigned int SessionId) {
    if (Packet == nullptr) {
        return false;
    }
    IServer* Server = GRealServer;
    if (Server == nullptr) {
        return false;
    }

    const unsigned int Channel = Packet->GetChannel();
    if (Channel == 1u) {
        return DispatchChannel1ToServer(Packet, Server, GRealChatServer, SessionId);
    }
    if (Channel != 3u) {
        // Channel 2 never reaches this dispatcher in retail.
        return false;
    }

    const FRpcServerCodec* Codec = RpcFindServerCodec(3u, Packet->GetOpcode());
    if (Codec == nullptr || Codec->Deliver == nullptr) {
        // ch3 0x0a GFREQUESTSHIPINFO: registered, no case in retail either.
        return false;
    }
    return Codec->Deliver(Packet, Server, SessionId);
}
