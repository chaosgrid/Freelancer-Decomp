// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_payload_ch2.cpp -- the channel-2 packet types (server -> client), marshalled and
// delivered the way retail does it.
//
// Channel 2 is one-way: rpclocal's IClient (called by Server.dll) serializes, and
// IServerImpl::HandlePacket deserializes into the real IClient in freelancer.exe. Each layout
// is read from the sender (the IClient slot body), the receiver (the channel-2 case of
// IServerImpl::HandlePacket, jump table .text:0x06d83b44) and the registry size
// (payload == operator new() size - 0x20). Where the sender permutes the argument struct
// (LAUNCH, CREATEMINE, SETREMOVEITEM) the receiver's permutation is its strict inverse.
//
// Two retail conventions channel 1 does not use (see the channel-2 section of rpc_payloads.h):
//   * the "1" that replaces the session id is passed by reference where the slot takes
//     uint const&;
//   * four types deliver out of file-static storage instead of a stack temporary.

#include "rpc_payloads.h"
#include "rpc_containers.h"
#include "rpc_iclient.h"
// ch2 0x4d LOADHINT goes through COMMON.DLL CostumeHint::{Serialize,UnSerialize,CleanUp}.
#include "common_goods.h"
// CollisionGroupDesc (ch2 0x48 SETCOLLISIONGROUPS).
#include "common_archetype.h"
// ch2 0x04/0x42 use the COMMON.DLL reputation imports (.rdata:06db3050..06db30ec, 06db305c);
// SinglePlayer comes from common_misc.h.
#include "common_misc.h"
// FMineExplosion (IClient::SendServerExplodeAsteroidMine).
#include "server_sysdb.h"

#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

// ---------------------------------------------------------------------------
// Shared helpers -- the two block writers retail uses on this channel.
// ---------------------------------------------------------------------------

// FUN_06d97b00 / FUN_06d90a00 and the body of FUN_06d97470 / FUN_06d989b0: bare
// operator new(N) -- a size of 0 included -- and copy the caller's bytes in; the packet's
// destructor frees the block. FUN_06d97470 / FUN_06d989b0 add `if (len == 0 || src == 0)
// return` in front; their callers here carry that test themselves.
static void* RpcCh2CopyBlock(const void* Source, unsigned int InSize) {
    unsigned char* Block = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < InSize; ++i) {
        Block[i] = Src[i];
    }
    return Block;
}

// Inline strlen as retail computes it; no null test (retail faults on null).
static unsigned int RpcCh2StringLength(const char* Text) {
    unsigned int Length = 0;
    while (Text[Length] != '\0') {
        ++Length;
    }
    return Length;
}

// Float fields are moved as bit patterns: retail copies them with dword MOVs, not FLD/FSTP.
static unsigned int RpcCh2FloatBits(const float& Value) {
    unsigned int Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

static void RpcCh2SetFloatBits(float& Out, unsigned int Bits) {
    std::memcpy(&Out, &Bits, sizeof(Bits));
}

// The payload at packet+0x20, written with no size test. Senders store field by field rather
// than via RpcPayloadWrite, which would also write the pad bytes retail leaves untouched.
template<typename T>
static T* Ch2Payload(FPacketBase* Packet) {
    return reinterpret_cast<T*>(Packet->Payload());
}

// ===========================================================================
// Generic marshallers. Retail repeats these shapes across dozens of types; the codec
// table below names the sender address of every type that uses each one.
// ===========================================================================

// payload+0x00 = the 2nd argument, by value.
static bool MarshalCh2Dword(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerDword* Wire = Ch2Payload<FPayloadServerDword>(Packet);
    Wire->Field_0x00 = Args[1];
    return true;
}

// payload+0x00 = *(dword*)the 2nd argument. Used where the slot takes a const reference.
static bool MarshalCh2DwordByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerDword* Wire = Ch2Payload<FPayloadServerDword>(Packet);
    Wire->Field_0x00 = *reinterpret_cast<const unsigned int*>(Args[1]);
    return true;
}

// payload+0x00 = the low byte of the 2nd argument; the three bytes after it are not touched.
static bool MarshalCh2Byte(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerByte* Wire = Ch2Payload<FPayloadServerByte>(Packet);
    Wire->Field_0x00 = (unsigned char)(Args[1] & 0xffu);
    return true;
}

// payload+0x00 = *(byte*)the 2nd argument; the three bytes after it are not touched.
static bool MarshalCh2ByteByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerByte* Wire = Ch2Payload<FPayloadServerByte>(Packet);
    Wire->Field_0x00 = *reinterpret_cast<const unsigned char*>(Args[1]);
    return true;
}

// payload+0x00 = 2nd argument, payload+0x04 = 3rd argument.
static bool MarshalCh2DwordPair(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // Some senders (0x06d88930, 0x06d889c0) store the two in the other order; same bytes.
    FPayloadServerDwordPair* Wire = Ch2Payload<FPayloadServerDwordPair>(Packet);
    Wire->Field_0x00 = Args[1];
    Wire->Field_0x04 = Args[2];
    return true;
}

// FUN_06d97b00: payload+0x00 = size (3rd argument), payload+0x04 = an owned copy of the
// buffer at the 2nd argument.
static bool MarshalCh2Blob(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerBlob* Wire = Ch2Payload<FPayloadServerBlob>(Packet);
    Wire->Buffer = RpcCh2CopyBlock(reinterpret_cast<const void*>(Args[1]), Args[2]);
    Wire->Size = Args[2];
    return true;
}

// FUN_06d90a00: the same, fields the other way round.
static bool MarshalCh2BlobReversed(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerBlobReversed* Wire = Ch2Payload<FPayloadServerBlobReversed>(Packet);
    Wire->Buffer = RpcCh2CopyBlock(reinterpret_cast<const void*>(Args[1]), Args[2]);
    Wire->Size = Args[2];
    return true;
}

// Empty payload (REVERT_CAMERA 0x06d8af20, CANCEL_MUSIC 0x06d8c6e0): registry size 0x20,
// the bare CDAPacket base.
static bool MarshalCh2Empty(FPacketBase*, const unsigned int*, unsigned int) {
    return true;
}

// ===========================================================================
// Generic delivery shapes.
//
// Retail passes the literal 1 in place of the session id on every channel-2 case; where the
// slot takes uint const& it passes the address of a stack local holding 1 (...ByRef).
// ===========================================================================

// int / unsigned long parameters get the same 32 bits, converted explicitly.
#define RPC_CH2_DELIVER_DWORD(FnName, SlotCall, ParamType)                                 \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerDword Wire = RpcPayloadRead<FPayloadServerDword>(Packet, 0);    \
        Client->SlotCall(1u, static_cast<ParamType>(Wire.Field_0x00));                      \
        return true;                                                                        \
    }

#define RPC_CH2_DELIVER_DWORD_BYREF(FnName, SlotCall, ParamType)                           \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerDword Wire = RpcPayloadRead<FPayloadServerDword>(Packet, 0);    \
        unsigned int One = 1u;                                                              \
        Client->SlotCall(One, static_cast<ParamType>(Wire.Field_0x00));                     \
        return true;                                                                        \
    }

// (1, payload+0x00, payload+0x04)
#define RPC_CH2_DELIVER_PAIR(FnName, SlotCall)                                             \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerDwordPair Wire =                                                \
            RpcPayloadRead<FPayloadServerDwordPair>(Packet, 0);                             \
        Client->SlotCall(1u, Wire.Field_0x00, Wire.Field_0x04);                             \
        return true;                                                                        \
    }

// (1, payload+0x04, payload+0x00) -- the blob shape, buffer first then length.
#define RPC_CH2_DELIVER_BLOB(FnName, SlotCall)                                             \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerBlob Wire = RpcPayloadRead<FPayloadServerBlob>(Packet, 0);      \
        Client->SlotCall(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);    \
        return true;                                                                        \
    }

#define RPC_CH2_DELIVER_BLOB_REVERSED(FnName, SlotCall)                                    \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerBlobReversed Wire =                                             \
            RpcPayloadRead<FPayloadServerBlobReversed>(Packet, 0);                          \
        Client->SlotCall(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);    \
        return true;                                                                        \
    }

// ===========================================================================
// ch2 0x02 FLPACKET_SERVER_LOGINRESPONSE
// Sender 0x06d883a0 (IClient+0x030). Receiver caseD_2 at 0x06d816ec copies the byte into
// .data:0x06dbb904 and passes the address of that static, not of a temporary.
// ===========================================================================
static unsigned char GLoginResponseStatic = 0;   // retail .data:0x06dbb904

static bool DeliverLoginResponse(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerByte Wire = RpcPayloadRead<FPayloadServerByte>(Packet, 0);
    GLoginResponseStatic = Wire.Field_0x00;
    Client->SendServerLoginResponse(1u, GLoginResponseStatic);
    return true;
}

// ===========================================================================
// ch2 0x06 FLPACKET_SERVER_DESTROYOBJECT
// Sender 0x06d89140 (IClient+0x0dc): payload+0x00 = [A1+0x00], payload+0x04 = byte [A1+0x04].
// Receiver caseD_6 at 0x06d81f6a delivers through the static pair .data:0x06dbb824 /
// 0x06dbb828 (lazily initialised, atexit-registered).
// ===========================================================================
static FServerDestroyObjectInfo GDestroyObjectStatic = { 0, 0, { 0, 0, 0 } };

static bool MarshalDestroyObject(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FServerDestroyObjectInfo* Info =
        reinterpret_cast<const FServerDestroyObjectInfo*>(Args[1]);
    FPayloadServerDestroyObject* Wire = Ch2Payload<FPayloadServerDestroyObject>(Packet);
    Wire->ObjectId = Info->ObjectId;
    Wire->DestroyType = Info->DestroyType;
    return true;
}

static bool DeliverDestroyObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDestroyObject Wire =
        RpcPayloadRead<FPayloadServerDestroyObject>(Packet, 0);
    GDestroyObjectStatic.ObjectId = Wire.ObjectId;
    // Retail stores a zero-extended dword: the byte plus three cleared pad bytes.
    GDestroyObjectStatic.DestroyType = Wire.DestroyType;
    GDestroyObjectStatic.Pad_0x05[0] = 0;
    GDestroyObjectStatic.Pad_0x05[1] = 0;
    GDestroyObjectStatic.Pad_0x05[2] = 0;
    Client->SendServerDestroyObject(1u, GDestroyObjectStatic);
    return true;
}

// ===========================================================================
// ch2 0x07 FLPACKET_SERVER_LAUNCH
// Sender 0x06d8a080 (IClient+0x0f0), receiver caseD_7 at 0x06d81951.
// ===========================================================================
static bool MarshalLaunch(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FServerLaunchInfo* Info = reinterpret_cast<const FServerLaunchInfo*>(Args[1]);
    FPayloadServerLaunch Wire;
    Wire.ShipId = Info->ShipId;
    Wire.LaunchObjId = Info->LaunchObjId;
    Wire.LaunchPosIndex = Info->LaunchPosIndex;
    Wire.Position = Info->Position;
    Wire.Orientation = Info->Orientation;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverLaunch(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerLaunch Wire = RpcPayloadRead<FPayloadServerLaunch>(Packet, 0);
    FServerLaunchInfo Info;
    Info.ShipId = Wire.ShipId;
    Info.LaunchObjId = Wire.LaunchObjId;
    Info.LaunchPosIndex = Wire.LaunchPosIndex;
    Info.Orientation = Wire.Orientation;
    Info.Position = Wire.Position;
    Client->SendServerLaunch(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x0a FLPACKET_SERVER_ACTIVATEOBJECT
// Sender 0x06d89770 (IClient+0x0e0), receiver caseD_a at 0x06d823ab (three statics).
// ===========================================================================
static FPayloadServerActivateObject GActivateObjectStatic = { 0, 0, { 0, 0, 0 }, 0 };

static bool MarshalActivateObject(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FPayloadServerActivateObject* Info =
        reinterpret_cast<const FPayloadServerActivateObject*>(Args[1]);
    FPayloadServerActivateObject* Wire = Ch2Payload<FPayloadServerActivateObject>(Packet);
    Wire->ObjectId = Info->ObjectId;
    Wire->ActivateParam = Info->ActivateParam;
    Wire->bActivate = Info->bActivate;
    return true;
}

static bool DeliverActivateObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerActivateObject Wire =
        RpcPayloadRead<FPayloadServerActivateObject>(Packet, 0);
    GActivateObjectStatic.ObjectId = Wire.ObjectId;
    GActivateObjectStatic.bActivate = Wire.bActivate;
    GActivateObjectStatic.Pad_0x05[0] = 0;
    GActivateObjectStatic.Pad_0x05[1] = 0;
    GActivateObjectStatic.Pad_0x05[2] = 0;
    GActivateObjectStatic.ActivateParam = Wire.ActivateParam;
    Client->SendServerActivateObject(1u, GActivateObjectStatic);
    return true;
}

// ===========================================================================
// ch2 0x0b FLPACKET_SERVER_LAND
// Sender 0x06d8a990 (IClient+0x0ec), receiver caseD_b at 0x06d8237c.
// ===========================================================================
// The wire keeps the generic three-dword shape, in FServerLandInfo order.
static bool MarshalThreeDwordsByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FServerLandInfo* Info = reinterpret_cast<const FServerLandInfo*>(Args[1]);
    FPayloadServerThreeDwords* Wire = Ch2Payload<FPayloadServerThreeDwords>(Packet);
    Wire->Field_0x00 = Info->ShipId;
    Wire->Field_0x08 = Info->BaseId;
    Wire->Field_0x04 = Info->DockId;
    return true;
}

static bool DeliverLand(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerThreeDwords Wire =
        RpcPayloadRead<FPayloadServerThreeDwords>(Packet, 0);
    FServerLandInfo Info;
    Info.ShipId = Wire.Field_0x00;
    Info.DockId = Wire.Field_0x04;
    Info.BaseId = Wire.Field_0x08;
    Client->SendServerLand(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x21 FLPACKET_SERVER_SYSTEM_SWITCH_OUT
// Sender 0x06d8a860 (IClient+0x0e4). Receiver caseD_21 at 0x06d82351 delivers through the
// statics .data:0x06dbb82c / 0x06dbb830. The wire keeps the generic dword-pair shape.
// ===========================================================================
static FServerSystemSwitchOutInfo GSystemSwitchOutStatic = { 0, 0 };

static bool MarshalDwordPairByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FServerSystemSwitchOutInfo* Info =
        reinterpret_cast<const FServerSystemSwitchOutInfo*>(Args[1]);
    FPayloadServerDwordPair* Wire = Ch2Payload<FPayloadServerDwordPair>(Packet);
    Wire->Field_0x00 = Info->ShipId;
    Wire->Field_0x04 = Info->GateId;
    return true;
}

static bool DeliverSystemSwitchOut(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDwordPair Wire = RpcPayloadRead<FPayloadServerDwordPair>(Packet, 0);
    GSystemSwitchOutStatic.ShipId = Wire.Field_0x00;
    GSystemSwitchOutStatic.GateId = Wire.Field_0x04;
    Client->SendServerSystemSwitchOut(1u, GSystemSwitchOutStatic);
    return true;
}

// ===========================================================================
// ch2 0x27 FLPACKET_SERVER_REQUESTCREATESHIPRESP
// Sender 0x06d8a130 (IClient+0x0f4), receiver caseD_27 at 0x06d819b1.
// ===========================================================================
static bool MarshalRequestCreateShipResp(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerRequestCreateShipResp* Wire = Ch2Payload<FPayloadServerRequestCreateShipResp>(Packet);
    Wire->ShipId = Args[2];
    Wire->bSuccess = (unsigned char)(Args[1] & 0xffu);
    return true;
}

static bool DeliverRequestCreateShipResp(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRequestCreateShipResp Wire =
        RpcPayloadRead<FPayloadServerRequestCreateShipResp>(Packet, 0);
    // The receiver pushes the byte zero-extended; the sender only ever stored a bool's 0/1.
    Client->SendServerRequestCreateShipResp(1u, Wire.bSuccess != 0, Wire.ShipId);
    return true;
}

// ===========================================================================
// ch2 0x2c FLPACKET_SERVER_CREATEMINE
// Sender 0x06d8a180 (IClient+0x0c4), receiver caseD_2c at 0x06d81ac1. The two field
// groups swap places on the wire and swap back on arrival.
// ===========================================================================
// The argument is Server.dll's FServerCreateProjectileInfo (built by FUN_06d07c80, the same
// record CREATECOUNTER sends); all eighteen fields are copied with plain MOVs.
static bool MarshalCreateMine(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FServerCreateProjectileInfo* Info =
        reinterpret_cast<const FServerCreateProjectileInfo*>(Args[1]);
    FPayloadServerCreateMine Wire;
    Wire.ObjId = Info->ObjId;                                    // arg +0x00
    Wire.ArchetypeNickname = Info->ArchetypeNickname;                        // arg +0x04
    Wire.OwnerId = Info->OwnerId;                                  // arg +0x08
    Wire.SubObjId = Info->SubObjId;                                 // arg +0x0c, a word
    Wire.Pad_0x0e = 0;
    Wire.OrientWBits = RpcCh2FloatBits(Info->Orientation.w);           // arg +0x1c
    Wire.OrientXBits = RpcCh2FloatBits(Info->Orientation.x);           // arg +0x20
    Wire.OrientYBits = RpcCh2FloatBits(Info->Orientation.y);           // arg +0x24
    Wire.OrientZBits = RpcCh2FloatBits(Info->Orientation.z);           // arg +0x28
    Wire.PosXBits = RpcCh2FloatBits(Info->Position.x);              // arg +0x10
    Wire.PosYBits = RpcCh2FloatBits(Info->Position.y);              // arg +0x14
    Wire.PosZBits = RpcCh2FloatBits(Info->Position.z);              // arg +0x18
    Wire.VelXBits = RpcCh2FloatBits(Info->Velocity.x);              // arg +0x2c
    Wire.VelYBits = RpcCh2FloatBits(Info->Velocity.y);              // arg +0x30
    Wire.VelZBits = RpcCh2FloatBits(Info->Velocity.z);              // arg +0x34
    Wire.AngVelXBits = RpcCh2FloatBits(Info->AngularVelocity.x);            // arg +0x38
    Wire.AngVelYBits = RpcCh2FloatBits(Info->AngularVelocity.y);            // arg +0x3c
    Wire.AngVelZBits = RpcCh2FloatBits(Info->AngularVelocity.z);            // arg +0x40
    Wire.OwnerPlayer = Info->OwnerPlayer;                               // arg +0x44
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverCreateMine(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateMine Wire = RpcPayloadRead<FPayloadServerCreateMine>(Packet, 0);
    FServerCreateProjectileInfo Info;
    Info.ObjId = Wire.ObjId;
    Info.ArchetypeNickname = Wire.ArchetypeNickname;
    Info.OwnerId = Wire.OwnerId;
    Info.SubObjId = Wire.SubObjId;
    // Deviation: retail leaves this half of the local as stack garbage; the port zeroes it
    // (docs/re/deviations.md).
    Info.Pad0e = 0;
    RpcCh2SetFloatBits(Info.Position.x, Wire.PosXBits);
    RpcCh2SetFloatBits(Info.Position.y, Wire.PosYBits);
    RpcCh2SetFloatBits(Info.Position.z, Wire.PosZBits);
    RpcCh2SetFloatBits(Info.Orientation.w, Wire.OrientWBits);
    RpcCh2SetFloatBits(Info.Orientation.x, Wire.OrientXBits);
    RpcCh2SetFloatBits(Info.Orientation.y, Wire.OrientYBits);
    RpcCh2SetFloatBits(Info.Orientation.z, Wire.OrientZBits);
    RpcCh2SetFloatBits(Info.Velocity.x, Wire.VelXBits);
    RpcCh2SetFloatBits(Info.Velocity.y, Wire.VelYBits);
    RpcCh2SetFloatBits(Info.Velocity.z, Wire.VelZBits);
    RpcCh2SetFloatBits(Info.AngularVelocity.x, Wire.AngVelXBits);
    RpcCh2SetFloatBits(Info.AngularVelocity.y, Wire.AngVelYBits);
    RpcCh2SetFloatBits(Info.AngularVelocity.z, Wire.AngVelZBits);
    Info.OwnerPlayer = Wire.OwnerPlayer;
    Client->SendServerCreateMine(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x2e FLPACKET_SERVER_SETADDITEM
// Sender 0x06d885c0 (IClient+0x068), receiver caseD_2e at 0x06d819ed.
// ===========================================================================
static bool MarshalSetAddItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const EquipDesc* Desc = reinterpret_cast<const EquipDesc*>(Args[2]);
    FPayloadServerSetAddItem Wire;
    Wire.ArchId = Desc->ArchId;
    Wire.Count = Desc->Count;
    Wire.HardpointSize = 0;
    Wire.AddResult = Args[1];
    Wire.Id = (unsigned int)Desc->Id;
    Wire.Health = Desc->Health;
    Wire.bEquipped = Desc->bEquipped;
    Wire.bMission = (unsigned char)(Desc->bMission ? 1 : 0);
    Wire.Pad_0x1a[0] = 0;
    Wire.Pad_0x1a[1] = 0;
    Wire.Hardpoint = nullptr;

    // The hardpoint travels only when EquipDesc::is_internal() is false (retail imports it,
    // IAT .rdata:0003826c). Not the same as an empty-string test: an internal item carries a
    // non-empty interned cargo-bay name that retail still omits.
    if (!Desc->is_internal()) {
        // FUN_06d97470(packet, str, len), which no-ops when len == 0.
        const unsigned int Length = RpcCh2StringLength(Desc->Hardpoint);
        if (Length != 0) {
            Wire.Hardpoint = static_cast<char*>(RpcCh2CopyBlock(Desc->Hardpoint, Length + 1u));
            Wire.HardpointSize = Length + 1u;
        }
    }

    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSetAddItem(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetAddItem Wire = RpcPayloadRead<FPayloadServerSetAddItem>(Packet, 0);
    // Retail fills a Common.dll EquipDesc through the exported setters, in this order.
    EquipDesc Desc;
    Desc.set_arch_id(Wire.ArchId);                      // payload+0x00
    Desc.set_id((unsigned short)(Wire.Id & 0xffffu));   // payload+0x10 as a word
    Desc.set_count(Wire.Count);                         // payload+0x04
    Desc.set_status(Wire.Health);                       // payload+0x14
    Desc.set_equipped(Wire.bEquipped);             // payload+0x18
    // No setter for bMission; retail stores it straight into the struct.
    Desc.bMission = (Wire.bMission != 0);
    if (Wire.HardpointSize != 0) {
        // StringAlloc interns the name in Common.dll's string cache so the EquipDesc keeps a
        // permanent pointer; retail's CacheString temporary is one pointer wide.
        CacheString Interned;
        Interned.Value = StringAlloc(Wire.Hardpoint, false);
        Desc.set_hardpoint(Interned);
    } else {
        Desc.make_internal();
    }
    Client->SendServerSetAddItem(1u, static_cast<int>(Wire.AddResult), Desc);
    return true;
}

// ===========================================================================
// ch2 0x2f FLPACKET_SERVER_SETREMOVEITEM
// Sender 0x06d88670 (IClient+0x06c), receiver caseD_2f at 0x06d81aa0.
// ===========================================================================
static bool MarshalSetRemoveItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerSetRemoveItem* Wire = Ch2Payload<FPayloadServerSetRemoveItem>(Packet);
    Wire->RemoveResult = Args[1];
    Wire->ItemId = Args[2] & 0xffffu;
    Wire->Count = Args[3];
    return true;
}

static bool DeliverSetRemoveItem(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetRemoveItem Wire =
        RpcPayloadRead<FPayloadServerSetRemoveItem>(Packet, 0);
    Client->SendServerSetRemoveItem(1u, static_cast<int>(Wire.RemoveResult),
                                    static_cast<unsigned short>(Wire.ItemId & 0xffffu),
                                    static_cast<int>(Wire.Count));
    return true;
}

// ===========================================================================
// ch2 0x31 FLPACKET_SERVER_EXPLODEASTEROIDMINE
// Sender 0x06d8a360 (IClient+0x0d0), receiver caseD_31 at 0x06d81b6d.
// ===========================================================================
// The argument is Server.dll's FMineExplosion (built by MedMine::update); the wire keeps the
// generic four-dword shape, floats as bits.
static bool MarshalFourDwordsByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FMineExplosion* Info = reinterpret_cast<const FMineExplosion*>(Args[1]);
    FPayloadServerFourDwords* Wire = Ch2Payload<FPayloadServerFourDwords>(Packet);
    Wire->Field_0x00 = Info->MineId;
    Wire->Field_0x04 = RpcCh2FloatBits(Info->Offset.x);
    Wire->Field_0x08 = RpcCh2FloatBits(Info->Offset.y);
    Wire->Field_0x0c = RpcCh2FloatBits(Info->Offset.z);
    return true;
}

static bool DeliverExplodeAsteroidMine(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerFourDwords Wire = RpcPayloadRead<FPayloadServerFourDwords>(Packet, 0);
    FMineExplosion Info;
    Info.MineId = Wire.Field_0x00;
    RpcCh2SetFloatBits(Info.Offset.x, Wire.Field_0x04);
    RpcCh2SetFloatBits(Info.Offset.y, Wire.Field_0x08);
    RpcCh2SetFloatBits(Info.Offset.z, Wire.Field_0x0c);
    Client->SendServerExplodeAsteroidMine(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x33 FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE
// Sender 0x06d8ac40 (IClient+0x124), receiver caseD_33 at 0x06d823df. The "1" travels by
// reference here.
// ===========================================================================
static bool MarshalSetMissionObjectiveState(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // The byte index widens to a dword; payload+0x0a is not touched.
    const FServerMissionObjectiveStateInfo* Info =
        reinterpret_cast<const FServerMissionObjectiveStateInfo*>(Args[1]);
    FPayloadServerSetMissionObjectiveState* Wire =
        Ch2Payload<FPayloadServerSetMissionObjectiveState>(Packet);
    Wire->MissionId = Info->MissionId;
    Wire->ObjectiveIndex = (unsigned int)Info->ObjectiveIndex;
    Wire->State = (unsigned short)(Info->State & 0xffffu);
    return true;
}

static bool DeliverSetMissionObjectiveState(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetMissionObjectiveState Wire =
        RpcPayloadRead<FPayloadServerSetMissionObjectiveState>(Packet, 0);
    FServerMissionObjectiveStateInfo Info;
    Info.MissionId = Wire.MissionId;
    Info.ObjectiveIndex = (unsigned char)(Wire.ObjectiveIndex & 0xffu);
    Info.Pad_0x05[0] = 0;
    Info.Pad_0x05[1] = 0;
    Info.Pad_0x05[2] = 0;
    Info.State = (unsigned int)Wire.State;
    unsigned int One = 1u;
    Client->SendServerSetMissionObjectiveState(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x38 FLPACKET_SERVER_ITEMTRACTORED
// Sender 0x06d88c60 (IClient+0x100), receiver caseD_38 at 0x06d81e3d.
// ===========================================================================
static bool MarshalItemTractored(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FPayloadServerItemTractored* Info =
        reinterpret_cast<const FPayloadServerItemTractored*>(Args[1]);
    FPayloadServerItemTractored* Wire = Ch2Payload<FPayloadServerItemTractored>(Packet);
    Wire->LootId = Info->LootId;
    Wire->ShipId = Info->ShipId;
    Wire->TractorEquipId = Info->TractorEquipId;
    Wire->UnitsTaken = Info->UnitsTaken;
    return true;
}

static bool DeliverItemTractored(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerItemTractored Wire =
        RpcPayloadRead<FPayloadServerItemTractored>(Packet, 0);
    FPayloadServerItemTractored Info = Wire;
    Client->SendServerItemTractored(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x3c FLPACKET_SERVER_REPAIROBJECT
// Sender 0x06d8bfe0 (IClient+0x150), receiver caseD_3c at 0x06d82b55. The literal 1 is
// passed by value here, unlike its neighbours in the same slot range.
// ===========================================================================
static bool MarshalRepairObject(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FServerRepairObjectInfo* Info =
        reinterpret_cast<const FServerRepairObjectInfo*>(Args[1]);
    FPayloadServerRepairObject* Wire = Ch2Payload<FPayloadServerRepairObject>(Packet);
    Wire->EquipId = (unsigned int)Info->EquipId;
    Wire->ObjectId = Info->ObjectId;
    Wire->Points = Info->Points;
    return true;
}

static bool DeliverRepairObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRepairObject Wire =
        RpcPayloadRead<FPayloadServerRepairObject>(Packet, 0);
    FServerRepairObjectInfo Info;
    Info.ObjectId = Wire.ObjectId;
    Info.EquipId = (unsigned short)(Wire.EquipId & 0xffffu);
    Info.Pad_0x06 = 0;   // retail leaves this half untouched; see deviations.md
    Info.Points = Wire.Points;
    Client->SendServerRepairObject(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x41 FLPACKET_SERVER_BURNFUSE
// Sender 0x06d8c170 (IClient+0x15c), receiver caseD_41 at 0x06d82c04.
// ===========================================================================
static bool MarshalBurnFuse(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // Built by EqObj::light_fuse / unlight_fuse. Floats travel as bits; pad bytes are not
    // touched.
    const FServerBurnFuseInfo* Info = reinterpret_cast<const FServerBurnFuseInfo*>(Args[1]);
    FPayloadServerBurnFuse* Wire = Ch2Payload<FPayloadServerBurnFuse>(Packet);
    Wire->SourceId = Info->SourceId;
    Wire->ObjectId = Info->ObjectId;
    Wire->FuseId = Info->FuseId;
    Wire->SubObjId = Info->SubObjId;
    std::memcpy(&Wire->AtTBits, &Info->AtT, sizeof(unsigned int));
    std::memcpy(&Wire->LifetimeBits, &Info->Lifetime, sizeof(unsigned int));
    Wire->bLit = Info->bLit;
    return true;
}

static bool DeliverBurnFuse(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerBurnFuse Wire = RpcPayloadRead<FPayloadServerBurnFuse>(Packet, 0);
    // Pads are copied as the wire carried them.
    static_assert(sizeof(FServerBurnFuseInfo) == sizeof(FPayloadServerBurnFuse),
                  "BURNFUSE argument and payload are the same 0x1c bytes");
    FServerBurnFuseInfo Info;
    Info.SourceId = Wire.SourceId;
    Info.ObjectId = Wire.ObjectId;
    Info.FuseId = Wire.FuseId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad_0x0e = Wire.Pad_0x0e;
    RpcCh2SetFloatBits(Info.AtT, Wire.AtTBits);
    RpcCh2SetFloatBits(Info.Lifetime, Wire.LifetimeBits);
    Info.bLit = Wire.bLit;
    Info.Pad_0x19[0] = Wire.Pad_0x19[0];
    Info.Pad_0x19[1] = Wire.Pad_0x19[1];
    Info.Pad_0x19[2] = Wire.Pad_0x19[2];
    unsigned int One = 1u;
    Client->SendServerBurnFuse(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x43 FLPACKET_SERVER_SET_STORY_CUE
// Sender 0x06d8ae50 (IClient+0x168), receiver caseD_43 at 0x06d82541. Both the session id
// and the payload dword travel by reference.
// ===========================================================================
static bool DeliverSetStoryCue(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDword Wire = RpcPayloadRead<FPayloadServerDword>(Packet, 0);
    FPayloadServerDword Info = Wire;
    unsigned int One = 1u;
    Client->SendServerSetStoryCue(One, Info.Field_0x00);
    return true;
}

// ===========================================================================
// ch2 0x44 FLPACKET_SERVER_REQUEST_RETURNED
// Sender 0x06d8a9e0 (IClient+0x14c), receiver caseD_44 at 0x06d81918.
// ===========================================================================
static bool MarshalRequestReturned(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerRequestReturned Wire;
    Wire.ShipId = Args[1];
    Wire.RequestType = (unsigned char)(Args[2] & 0xffu);
    Wire.Response = (signed char)(Args[3] & 0xffu);
    Wire.Extra = (signed char)(Args[4] & 0xffu);
    Wire.Pad_0x07 = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverRequestReturned(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRequestReturned Wire =
        RpcPayloadRead<FPayloadServerRequestReturned>(Packet, 0);
    unsigned int One = 1u;
    Client->SendServerRequestReturned(One,
                   Wire.ShipId,
                   static_cast<int>(Wire.RequestType),       // MOVZX
                   static_cast<int>(Wire.Response),       // MOVSX
                   static_cast<int>(Wire.Extra));      // MOVSX
    return true;
}

// ===========================================================================
// ch2 0x46 FLPACKET_SERVER_MARKOBJ
// Sender 0x06d8ace0 (IClient+0x12c), receiver caseD_46 at 0x06d82420.
// ===========================================================================
static bool MarshalMarkObj(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerDwordByte* Wire = Ch2Payload<FPayloadServerDwordByte>(Packet);
    Wire->Field_0x00 = Args[1];
    Wire->Field_0x04 = (unsigned char)(Args[2] & 0xffu);
    return true;
}

static bool DeliverMarkObj(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDwordByte Wire = RpcPayloadRead<FPayloadServerDwordByte>(Packet, 0);
    unsigned int One = 1u;
    Client->SendServerMarkObj(One, Wire.Field_0x00, static_cast<int>(Wire.Field_0x04));
    return true;
}

// ===========================================================================
// ch2 0x47 FLPACKET_SERVER_CFGINTERFACENOTIFICATION
// Sender 0x06d8ad30 (IClient+0x130), receiver caseD_47 at 0x06d8244f.
// ===========================================================================
static bool MarshalCfgInterfaceNotification(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FPayloadServerCfgInterfaceNotification* Info =
        reinterpret_cast<const FPayloadServerCfgInterfaceNotification*>(Args[1]);
    FPayloadServerCfgInterfaceNotification* Wire =
        Ch2Payload<FPayloadServerCfgInterfaceNotification>(Packet);
    Wire->NotifType = Info->NotifType;
    Wire->Param = Info->Param;
    Wire->bEnable = Info->bEnable;
    return true;
}

static bool DeliverCfgInterfaceNotification(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCfgInterfaceNotification Wire =
        RpcPayloadRead<FPayloadServerCfgInterfaceNotification>(Packet, 0);
    FPayloadServerCfgInterfaceNotification Info;
    Info.NotifType = Wire.NotifType;
    Info.Param = Wire.Param;
    Info.bEnable = (unsigned char)((Wire.bEnable != 0) ? 1 : 0);   // retail TEST/SETNZ
    Info.Pad_0x09[0] = 0;
    Info.Pad_0x09[1] = 0;
    Info.Pad_0x09[2] = 0;
    unsigned int One = 1u;
    Client->SendServerCfgInterfaceNotification(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x49 FLPACKET_SERVER_SETHULLSTATUS
// Sender 0x06d88420 (IClient+0x058): the 2nd argument is the float itself, by value.
// Receiver caseD_49 at 0x06d83032 pushes the raw dword straight back out.
// ===========================================================================
static bool MarshalSetHullStatus(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // Retail moves it through FLD/FSTP, which would also quiet a signalling NaN; the port
    // copies the bits.
    FPayloadServerDword* Wire = Ch2Payload<FPayloadServerDword>(Packet);
    Wire->Field_0x00 = Args[1];
    return true;
}

// ===========================================================================
// ch2 0x4a FLPACKET_SERVER_SETGUIDEDTARGET
// Sender 0x06d8a800 (IClient+0x0d4), receiver caseD_4a at 0x06d83048.
// ===========================================================================
static bool MarshalSetGuidedTarget(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerSetGuidedTarget* Wire = Ch2Payload<FPayloadServerSetGuidedTarget>(Packet);
    Wire->GuidedId = Args[1];
    Wire->TargetId = Args[2];
    Wire->TargetSubObjId = (unsigned short)(Args[3] & 0xffffu);
    return true;
}

static bool DeliverSetGuidedTarget(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetGuidedTarget Wire =
        RpcPayloadRead<FPayloadServerSetGuidedTarget>(Packet, 0);
    Client->SendServerSetGuidedTarget(1u, Wire.GuidedId, Wire.TargetId, Wire.TargetSubObjId);
    return true;
}

// ===========================================================================
// ch2 0x4b FLPACKET_SERVER_SET_CAMERA
// Sender 0x06d8ae90 (IClient+0x160), receiver caseD_4b at 0x06d82573.
// ===========================================================================
static bool MarshalSetCamera(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FPayloadServerSetCamera* Info =
        reinterpret_cast<const FPayloadServerSetCamera*>(Args[1]);
    FPayloadServerSetCamera Wire = *Info;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSetCamera(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetCamera Wire = RpcPayloadRead<FPayloadServerSetCamera>(Packet, 0);
    FPayloadServerSetCamera Info = Wire;
    unsigned int One = 1u;
    Client->SendServerSetCamera(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x4c FLPACKET_SERVER_REVERT_CAMERA / 0x58 FLPACKET_SERVER_CANCEL_MUSIC
// Empty payloads. 0x4c takes the "1" by reference (caseD_4c at 0x06d825fd), 0x58 takes it
// by value (caseD_58 at 0x06d813a1).
// ===========================================================================
static bool DeliverRevertCamera(const FPacketBase*, IClient* Client, IChatClient*) {
    unsigned int One = 1u;
    Client->SendServerRevertCamera(One);
    return true;
}

static bool DeliverCancelMusic(const FPacketBase*, IClient* Client, IChatClient*) {
    Client->SendServerCancelMusic(1u);
    return true;
}

// ===========================================================================
// ch2 0x51 FLPACKET_SERVER_USE_ITEM
// Sender 0x06d88cc0 (IClient+0x104), receiver caseD_51 at 0x06d81e77.
// ===========================================================================
static bool MarshalUseItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FPayloadServerUseItem* Info = reinterpret_cast<const FPayloadServerUseItem*>(Args[1]);
    FPayloadServerUseItem* Wire = Ch2Payload<FPayloadServerUseItem>(Packet);
    Wire->ObjectId = Info->ObjectId;
    Wire->EquipId = Info->EquipId;
    Wire->Count = Info->Count;
    return true;
}

static bool DeliverUseItem(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerUseItem Wire = RpcPayloadRead<FPayloadServerUseItem>(Packet, 0);
    FPayloadServerUseItem Info = Wire;
    Client->SendServerUseItem(1u, Info);
    return true;
}

// ===========================================================================
// The generic deliverers, one per slot.
// ===========================================================================

RPC_CH2_DELIVER_DWORD(DeliverSetShipArch, SendServerSetShipArch, unsigned int)                    // 0x23 -> +0x054
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteCharList, SendServerGFCompleteCharList, unsigned int)       // 0x0f -> +0x07c
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteScriptBehaviorList, SendServerGFCompleteScriptBehaviorList, unsigned int)   // 0x11 -> +0x088
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteAmbientScriptList, SendServerGFCompleteAmbientScriptList, unsigned int)     // 0x13 -> +0x094
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteMissionComputerList, SendServerGFCompleteMissionComputerList, unsigned int) // 0x10 -> +0x0a0
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteNewsBroadcastList, SendServerGFCompleteNewsBroadcastList, unsigned int)     // 0x0e -> +0x0b4
RPC_CH2_DELIVER_DWORD(DeliverSendNnMessage, SendServerSendNNMessage, unsigned int)                 // 0x56 -> +0x114
RPC_CH2_DELIVER_DWORD(DeliverMissionSaveA, SendServerMissionSaveA, unsigned long)                  // 0x5b -> +0x128
RPC_CH2_DELIVER_DWORD(DeliverPlaySoundEffect, SendServerPlaySoundEffect, unsigned int)             // 0x59 -> +0x1f0

RPC_CH2_DELIVER_DWORD_BYREF(DeliverSetCash, SendServerSetCash, int)                                // 0x30 -> +0x134
RPC_CH2_DELIVER_DWORD_BYREF(DeliverSetNumKills, SendServerSetNumKills, int)                        // 0x3e -> +0x138
RPC_CH2_DELIVER_DWORD_BYREF(DeliverSetMissionSuccesses, SendServerSetMissionSuccesses, int)        // 0x3f -> +0x13c
RPC_CH2_DELIVER_DWORD_BYREF(DeliverSetMissionFailures, SendServerSetMissionFailures, int)          // 0x40 -> +0x140

RPC_CH2_DELIVER_PAIR(DeliverSetStartRoom, SendServerSetStartRoom)                       // 0x0d -> +0x070
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyCharacter, SendServerGFDestroyCharacter)           // 0x15 -> +0x074
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyScriptBehavior, SendServerGFDestroyScriptBehavior) // 0x17 -> +0x084
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyAmbientScript, SendServerGFDestroyAmbientScript)   // 0x19 -> +0x090
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyMissionComputer, SendServerGFDestroyMissionComputer) // 0x16 -> +0x098
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyNewsBroadcast, SendServerGFDestroyNewsBroadcast)   // 0x14 -> +0x0ac
RPC_CH2_DELIVER_PAIR(DeliverScanNotify, SendServerScanNotify)                           // 0x39 -> +0x1b4

RPC_CH2_DELIVER_BLOB(DeliverGfUpdateChar, SendServerGFUpdateChar)                       // 0x26 -> +0x078
RPC_CH2_DELIVER_BLOB(DeliverGfScriptBehavior, SendServerGFScriptBehavior)               // 0x1a -> +0x080
RPC_CH2_DELIVER_BLOB(DeliverGfUpdateMissionComputer, SendServerGFUpdateMissionComputer) // 0x1d -> +0x09c
RPC_CH2_DELIVER_BLOB(DeliverGfMissionVendorAcceptance, SendServerGFMissionVendorAcceptance) // 0x20 -> +0x0a4
RPC_CH2_DELIVER_BLOB(DeliverGfUpdateNewsBroadcast, SendServerGFUpdateNewsBroadcast)     // 0x1e -> +0x0b0

RPC_CH2_DELIVER_BLOB_REVERSED(DeliverGfUpdateAmbientScript, SendServerGFUpdateAmbientScript) // 0x1f -> +0x08c

// ch2 0x49 FLPACKET_SERVER_SETHULLSTATUS -> +0x058; the bits are moved, not the value.
static bool DeliverSetHullStatus(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDword Wire = RpcPayloadRead<FPayloadServerDword>(Packet, 0);
    float HullStatus = 0.0f;
    RpcCh2SetFloatBits(HullStatus, Wire.Field_0x00);
    Client->SendServerSetHullStatus(1u, HullStatus);
    return true;
}

// ch2 0x5a FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY, sender 0x06d88af0, receiver caseD_5a
// at 0x06d829fa: the payload byte, zero-extended.
static bool DeliverGfMissionVendorWhyEmpty(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerByte Wire = RpcPayloadRead<FPayloadServerByte>(Packet, 0);
    Client->SendServerGFMissionVendorWhyEmpty(1u, (unsigned int)Wire.Field_0x00);
    return true;
}

// ===========================================================================
// The four container types of channel 2, closed by the serializer in
// rpc_containers.{h,cpp}.
//
// 0x24 SETEQUIPMENT   sender 0x06d884a0  receiver caseD_24 @0x06d817be  SendServerSetEquipment (+0x060)
// 0x25 SETCARGO       sender 0x06d88530  receiver caseD_25 @0x06d8171f  SendServerSetCargo (+0x064)
// 0x3d REMOTEOBJECTCARGOUPDATE
//                     sender 0x06d8c040  receiver caseD_3d @0x06d82a14  SendServerRemoteObjectCargoUpdate (+0x154)
// 0x48 SETCOLLISIONGROUPS
//                     sender 0x06d88460  receiver caseD_48 @0x06d83012  SendServerSetCollisionGroups (+0x05c)
//
// The first three share one marshaller, FUN_06d8c750 (one 0x18 wire record per EquipDesc,
// pushed onto a handle that FUN_06d8fa90 hangs off the payload), and one demarshaller,
// FUN_06d95d20 + FUN_06d84d60. Registry sizes: SETEQUIPMENT and SETCARGO 0x24 (payload =
// the handle pointer), REMOTEOBJECTCARGOUPDATE 0x2c (handle + byte +0x04 + dword +0x08),
// SETCOLLISIONGROUPS 0x2c (payload 0x0c = the list header itself).
//
// FUN_06d8c750's third argument is the only difference: SETEQUIPMENT and SETCARGO pass 1
// and ship the whole vector; REMOTEOBJECTCARGOUPDATE passes 0, which drops every element
// whose bEquipped byte is set. So "cargo" is a matter of which vector the caller hands over,
// except for 0x3d.
// ===========================================================================

// --- the shared equipment marshaller (retail FUN_06d8c750 + FUN_06d8fa90) ---
// The stack handle has a destructor in retail (FUN_06d91340), run on the way out and from
// the unwind funclets on a throw.
static bool MarshalEquipVectorCommon(FPacketBase* Packet, unsigned int VectorArg, bool bAll) {
    const EquipDescVector* Source = reinterpret_cast<const EquipDescVector*>(VectorArg);
    FRpcEquipWireHandle Handle;
    RpcEquipHandleFromVector(&Handle, Source, bAll);
    RpcPacketEquipAssign(Packet, &Handle);
    return true;
}

// --- the shared equipment demarshaller (retail FUN_06d95d20 + FUN_06d84d60) ---
//
// The wire handle and the rebuilt vector live across the IClient call and are torn down
// after it, the handle first, on the normal path and on a throw alike; hence vector declared
// first, handle second. The rebuilt EquipDescs hold interned StringAlloc hardpoints, so
// nothing in the vector points into the handle.
namespace {

struct FEquipVectorScope {
    EquipDescVector Vector;         // ctor zeroes the four dwords (Common 0x06266310)
    FRpcEquipWireHandle Handle;     // ~FRpcEquipWireHandle = FUN_06d91340

    explicit FEquipVectorScope(const FPacketBase* Packet) {
        RpcPacketEquipExtract(Packet, &Handle);
        RpcEquipVectorFromHandle(&Vector, &Handle);
    }

private:
    FEquipVectorScope(const FEquipVectorScope&);
    FEquipVectorScope& operator=(const FEquipVectorScope&);
};

}  // namespace

// ch2 0x24 SETEQUIPMENT. Args: [0] the by-value session id retail drops, [1] the vector.
static bool MarshalSetEquipment(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    return MarshalEquipVectorCommon(Packet, Args[1], true);
}

static bool DeliverSetEquipment(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    FEquipVectorScope Scope(Packet);
    Client->SendServerSetEquipment(1u, Scope.Vector);
    return true;
}

// ch2 0x25 SETCARGO -- same sender and receiver as SETEQUIPMENT, one IClient slot apart.
static bool MarshalSetCargo(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    return MarshalEquipVectorCommon(Packet, Args[1], true);
}

static bool DeliverSetCargo(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    FEquipVectorScope Scope(Packet);
    Client->SendServerSetCargo(1u, Scope.Vector);
    return true;
}

// ch2 0x3d REMOTEOBJECTCARGOUPDATE. Sender 0x06d8c040, four arguments:
//   Args[0] a pointer to the session id (the by-reference family);
//   Args[1] -> payload+0x08;
//   Args[2] the std::vector<EquipDesc>, filtered to the unequipped items;
//   Args[3] -> payload+0x04 as a single byte.
static bool MarshalRemoteObjectCargoUpdate(FPacketBase* Packet, const unsigned int* Args,
                                           unsigned int ArgCount) {
    // The three pad bytes at +0x25 are written neither here nor by the constructor
    // (FUN_06d9ece0).
    FPayloadServerRemoteObjectCargoUpdate* Wire =
        Ch2Payload<FPayloadServerRemoteObjectCargoUpdate>(Packet);
    Wire->Field_0x04 = (unsigned char)(Args[3] & 0xffu);
    Wire->ObjectId = Args[1];
    // bAll is false only here: keeps the items whose bEquipped byte is clear.
    return MarshalEquipVectorCommon(Packet, Args[2], false);
}

static bool DeliverRemoteObjectCargoUpdate(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRemoteObjectCargoUpdate Wire =
        RpcPayloadRead<FPayloadServerRemoteObjectCargoUpdate>(Packet, 0);
    FEquipVectorScope Scope(Packet);
    unsigned int One = 1u;
    Client->SendServerRemoteObjectCargoUpdate(One, Wire.ObjectId, Scope.Vector, Wire.Field_0x04);
    return true;
}

// ch2 0x48 SETCOLLISIONGROUPS. Sender 0x06d88460; receiver caseD_48 passes packet+0x20
// (FUN_06d8f7d0) to SendServerSetCollisionGroups: in retail the payload is the list itself.
//
// The caller's list is Server.dll's std::list<CollisionGroupDesc>
// (&PlayerData::collision_groups, 06d4f400). As with ch3 0x36 REQCOLLISIONGROUPS
// (rpc_payload_ch3.cpp), the caller's iterators fill the packet list and the packet list is
// rebuilt into a real std::list for the IClient call.
static bool MarshalSetCollisionGroups(FPacketBase* Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    const std::list<CollisionGroupDesc>& Source =
        *reinterpret_cast<const std::list<CollisionGroupDesc>*>(Args[1]);
    // FUN_06d8f710(packet, Source): walks the caller's nodes into the list the packet
    // constructor built. Its self-assignment test cannot fire here (different types).
    FRpcCollisionGroupList* List = RpcPacketCollisionGroups(Packet);
    RpcCollisionGroupListAssignRange(List, Source.begin(), Source.end());
    return true;
}

static bool DeliverSetCollisionGroups(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    // Rebuilt into a real std::list first (retail's inline copy at 06d8505c), as
    // DeliverReqCollisionGroups does.
    std::list<CollisionGroupDesc> Groups;
    RpcCollisionGroupListFromWire(Groups, RpcPacketCollisionGroups(Packet));
    Client->SendServerSetCollisionGroups(1u, Groups);
    return true;
}

// ===========================================================================
// ch2 0x01 FLPACKET_SERVER_CONNECTRESPONSE. Retail's sender lives inside the pump
// FUN_06d8efc0, not in an IClient slot, and the payload is a literal.
// ===========================================================================
static bool MarshalConnectResponse(FPacketBase* Packet, const unsigned int*, unsigned int) {
    FPayloadServerConnectResponse Wire;
    Wire.ClientId = 1u;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverConnectResponse(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerConnectResponse Wire =
        RpcPayloadRead<FPayloadServerConnectResponse>(Packet, 0);
    // The one channel-2 case with no leading 1 -- retail pushes the payload dword alone.
    Client->OnConnect(Wire.ClientId);
    return true;
}

// ===========================================================================
// ch2 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED.
// ===========================================================================
static bool MarshalCharSelectVerified(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // payload+0x04 is left alone, as retail leaves it.
    const unsigned int* Source = reinterpret_cast<const unsigned int*>(Args[1]);
    FPayloadServerCharSelectVerified* Wire = Ch2Payload<FPayloadServerCharSelectVerified>(Packet);
    Wire->VibeHandle = Source[0];
    Wire->ServerTimeLo = Source[2];
    Wire->ServerTimeHi = Source[3];
    return true;
}

static bool DeliverCharSelectVerified(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCharSelectVerified Wire =
        RpcPayloadRead<FPayloadServerCharSelectVerified>(Packet, 0);
    // The hole at +0x04 is zeroed (see FPayloadServerCharSelectVerified).
    FServerCharSelectVerifiedInfo Info;
    Info.VibeHandle = static_cast<int>(Wire.VibeHandle);
    Info.Unused_0x04 = 0;
    unsigned char* const TimeBytes = reinterpret_cast<unsigned char*>(&Info.ServerTime);
    std::memcpy(TimeBytes, &Wire.ServerTimeLo, sizeof(unsigned int));
    std::memcpy(TimeBytes + sizeof(unsigned int), &Wire.ServerTimeHi, sizeof(unsigned int));
    Client->SendServerCharSelectVerified(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN. Delivered out of a file-static block,
// .data:0x06dbb91c, not re-entrant in retail either.
// ===========================================================================
static FPayloadServerSystemSwitchIn GSystemSwitchInStatic_06dbb91c;

static bool MarshalSystemSwitchIn(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FPayloadServerSystemSwitchIn* Source =
        reinterpret_cast<const FPayloadServerSystemSwitchIn*>(Args[1]);
    return RpcPayloadWrite(Packet, 0, *Source);
}

static bool DeliverSystemSwitchIn(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    GSystemSwitchInStatic_06dbb91c = RpcPayloadRead<FPayloadServerSystemSwitchIn>(Packet, 0);
    Client->SendServerSystemSwitchIn(1u, GSystemSwitchInStatic_06dbb91c);
    return true;
}

// ===========================================================================
// ch2 0x2a FLPACKET_SERVER_ADJUSTATTITUDE.
// ===========================================================================
static bool MarshalAdjustAttitude(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // Retail moves the float through FLD/FSTP; the port copies the bits (x87 would quiet an
    // sNaN).
    const FPayloadServerAdjustAttitude* Source =
        reinterpret_cast<const FPayloadServerAdjustAttitude*>(Args[1]);
    FPayloadServerAdjustAttitude* Wire = Ch2Payload<FPayloadServerAdjustAttitude>(Packet);
    Wire->VibeHandle = Source->VibeHandle;
    ::memcpy(&Wire->Attitude, &Source->Attitude, sizeof(float));
    return true;
}

static bool DeliverAdjustAttitude(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    FPayloadServerAdjustAttitude Local = RpcPayloadRead<FPayloadServerAdjustAttitude>(Packet, 0);
    Client->SendServerAdjustAttitude(1u, Local);
    return true;
}

// ===========================================================================
// ch2 0x53 FLPACKET_SERVER_FORMATION_UPDATE.
// ===========================================================================
static bool MarshalFormationUpdate(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // The vector is copied with integer moves -- a bit copy.
    FPayloadServerFormationUpdate* Wire = Ch2Payload<FPayloadServerFormationUpdate>(Packet);
    Wire->LeaderId = Args[1];
    ::memcpy(&Wire->Offset, reinterpret_cast<const void*>(Args[2]), sizeof(Vector));
    return true;
}

static bool DeliverFormationUpdate(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerFormationUpdate Wire =
        RpcPayloadRead<FPayloadServerFormationUpdate>(Packet, 0);
    Vector Local = Wire.Offset;
    Client->SendServerFormationUpdate(1u, Wire.LeaderId, Local);
    return true;
}

// ===========================================================================
// ch2 0x57 FLPACKET_SERVER_SET_MUSIC.
// ===========================================================================
static bool MarshalSetMusic(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // Fade arrives as its argument slot's bits and travels as such (retail FLD/FSTP would
    // quiet an sNaN). Pad bytes untouched.
    FPayloadServerSetMusic* Wire = Ch2Payload<FPayloadServerSetMusic>(Packet);
    Wire->SpaceMusic = Args[1];
    Wire->DangerMusic = Args[2];
    Wire->BattleMusic = Args[3];
    Wire->OverrideMusic = Args[4];
    Wire->bNoLoop = (unsigned char)(Args[5] & 0xffu);
    ::memcpy(&Wire->Fade, &Args[6], sizeof(float));
    return true;
}

static bool DeliverSetMusic(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetMusic Wire = RpcPayloadRead<FPayloadServerSetMusic>(Packet, 0);
    // The byte is pushed zero-extended; the sender only ever stores 0/1 there.
    Client->SendServerSetMusic(1u, Wire.SpaceMusic, Wire.DangerMusic, Wire.BattleMusic, Wire.OverrideMusic,
                               Wire.bNoLoop != 0, Wire.Fade);
    return true;
}

// ===========================================================================
// ch2 0x52 FLPACKET_SERVER_PLAYERLIST -- three senders, three slots, one dword of
// discriminator at payload+0x00.
//
// Senders and their kind: 0x06d8c5b0 (IClient+0x1c0) kind 0, 0x06d8c500 (+0x1b8) kind 1,
// 0x06d8c560 (+0x1bc) kind 2. All three call the setter FUN_06d99950(packet, Name, Value),
// which frees the previous wide string, _wcsdup's the new one into payload+0x04 and stores
// Value at payload+0x08. Receiver at .text:0x06d83102.
// ===========================================================================

// FUN_06d99950's copy half: null test, then _wcsdup, result stored untested. The packet's
// destructor frees with the matching allocator.
static wchar_t* RpcCh2DupWide(const wchar_t* Text) {
    if (Text == nullptr) {
        return nullptr;
    }
    return ::_wcsdup(Text);
}

// Args[0] is the kind literal the sender slot writes; Args[1..] are that slot's own
// arguments minus the session id, which retail never serializes.
static bool MarshalPlayerList(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerPlayerList Wire;
    Wire.Kind = Args[0];
    Wire.Name = nullptr;
    Wire.PlayerId = 0;
    Wire.bAlreadyPresent = 0;
    Wire.Pad_0x0d[0] = 0;
    Wire.Pad_0x0d[1] = 0;
    Wire.Pad_0x0d[2] = 0;

    if (Wire.Kind == kPlayerListKindEntry) {
        Wire.Name = RpcCh2DupWide(reinterpret_cast<const wchar_t*>(Args[1]));
        Wire.PlayerId = Args[2];
        Wire.bAlreadyPresent = (unsigned char)(Args[3] & 0xffu);
    } else if (Wire.Kind == kPlayerListKindDrop) {
        // Retail pushes the zero left in EAX by the allocation's success test as the
        // string, so kind 2 never carries a name; its one argument is the value.
        Wire.PlayerId = Args[1];
    }

    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverPlayerList(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerPlayerList Wire = RpcPayloadRead<FPayloadServerPlayerList>(Packet, 0);
    if (Wire.Kind == kPlayerListKindClear) {
        Client->SendServerPlayerListClear(1u);
        return true;
    }
    if (Wire.Kind == kPlayerListKindEntry) {
        Client->SendServerPlayerListAdd(1u, Wire.Name, Wire.PlayerId, Wire.bAlreadyPresent != 0);
        return true;
    }
    if (Wire.Kind == kPlayerListKindDrop) {
        Client->SendServerPlayerListRemove(1u, Wire.PlayerId);
        return true;
    }
    // Any other kind (the constructor's 3 included) falls to caseD_9: discarded, no call.
    return false;
}

// ===========================================================================
// ch2 0x54 FLPACKET_SERVER_MISCOBJUPDATE -- eight senders, eight slots, a two-byte
// bitmask at payload+0x00. See the flag/setter table in rpc_payloads.h.
// ===========================================================================

// Unchecked, as retail's setters FUN_06d98ac0..FUN_06d98cf0 are; the block is the class's
// own size (rpc_packet_registry.cpp).
static FPayloadServerMiscObjUpdate* Ch2MiscObjUpdate(FPacketBase* Packet) {
    return reinterpret_cast<FPayloadServerMiscObjUpdate*>(Packet->Payload());
}

static const FPayloadServerMiscObjUpdate* Ch2MiscObjUpdate(const FPacketBase* Packet) {
    return reinterpret_cast<const FPayloadServerMiscObjUpdate*>(Packet->Payload());
}

// FUN_06d98b20: basic_string<unsigned short>::assign(source, 0, npos) into packet+0x40.
// The string is wide: the MSVCP60 imports are the basic_string<G> instantiation.
static void Ch2MiscObjUpdateSetText(FPayloadServerMiscObjUpdate* Wire,
                                    const std::wstring& Source) {
    Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagText);
    Wire->Text.assign(Source, 0, std::wstring::npos);
}

// FUN_06d98cf0, called once per element; an empty vector leaves the flag clear and the
// receiver never calls slot 120.
static void Ch2MiscObjUpdateAddId(FPayloadServerMiscObjUpdate* Wire, unsigned int Value) {
    Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagIds);
    Wire->Ids.push_back(Value);
    // Every push_back stores the low byte of the new size at packet+0x68, the wire count.
    Wire->IdCount = (unsigned char)(Wire->Ids.size() & 0xffu);
}

// Args[0] is the sender variant; Args[1..] are that slot's arguments minus the session id.
static bool MarshalMiscObjUpdate(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // No re-initialisation: the factory already ran the ctor (0x06d9edc0).
    FPayloadServerMiscObjUpdate* Wire = Ch2MiscObjUpdate(Packet);

    switch (Args[0]) {
    case kMiscObjUpdateVariant_ObjectGroup:   // 0x06d8c1f0: FUN_06d98ac0 + FUN_06d98af0
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x08);
        Wire->ObjectId = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x14);
        Wire->GroupId = Args[2];
        return true;

    case kMiscObjUpdateVariant_Mission:   // 0x06d8c290: FUN_06d98b00, the only pair setter
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagPair);
        Wire->MissionId = Args[1];
        Wire->MissionSetBy = Args[2];
        return true;

    case kMiscObjUpdateVariant_Motd: { // 0x06d8c430: FUN_06d98b20
        // The wide string's address (Server's PlayerDB::SetMOTD, 0x746e0), read untested.
        const std::wstring* Source = reinterpret_cast<const std::wstring*>(Args[1]);
        Ch2MiscObjUpdateSetText(Wire, *Source);
        return true;
    }

    case kMiscObjUpdateVariant_Affiliation:   // 0x06d8c240: FUN_06d98ac0 + FUN_06d98ae0
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x08);
        Wire->ObjectId = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x34);
        Wire->Affiliation = Args[2];
        return true;

    case kMiscObjUpdateVariant_Rank:   // 0x06d8c2e0: FUN_06d98ad0 + FUN_06d98ce0 (word)
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x04);
        Wire->PlayerId = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x0c);
        Wire->Rank = (unsigned short)(Args[2] & 0xffffu);
        return true;

    case kMiscObjUpdateVariant_PlayerGroup:   // 0x06d8c390: FUN_06d98ad0 + FUN_06d98af0
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x04);
        Wire->PlayerId = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x14);
        Wire->GroupId = Args[2];
        return true;

    case kMiscObjUpdateVariant_System:   // 0x06d8c3e0: FUN_06d98ad0 + FUN_06d98cd0
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x04);
        Wire->PlayerId = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x10);
        Wire->SystemId = Args[2];
        return true;

    case kMiscObjUpdateVariant_Formation: { // 0x06d8c330: FUN_06d98cf0 per element
        // The vector's address, read untested.
        const std::vector<unsigned int>* Source =
            reinterpret_cast<const std::vector<unsigned int>*>(Args[1]);
        for (std::vector<unsigned int>::const_iterator It = Source->begin();
             It != Source->end(); ++It) {
            Ch2MiscObjUpdateAddId(Wire, *It);
        }
        return true;
    }

    default:
        return false;
    }
}

// The receiver's seven tests, in retail's order; more than one can fire from one packet.
static bool DeliverMiscObjUpdate(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerMiscObjUpdate* Wire = Ch2MiscObjUpdate(Packet);
    bool bDelivered = false;

    if ((Wire->Flags & kMiscObjUpdateFlagField_0x34) != 0) {
        Client->SendServerMiscObjUpdateAffiliation(1u, Wire->ObjectId, Wire->Affiliation);
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagField_0x14) != 0) {
        if ((Wire->Flags & kMiscObjUpdateFlagField_0x04) != 0) {
            Client->SendServerMiscObjUpdatePlayerGroup(1u, Wire->PlayerId, Wire->GroupId);
        } else {
            Client->SendServerMiscObjUpdateObjectGroup(1u, Wire->ObjectId, Wire->GroupId);
        }
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagPair) != 0) {
        Client->SendServerMiscObjUpdateMission(1u, Wire->MissionId, Wire->MissionSetBy);
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagText) != 0) {
        // The string inside the packet is passed by address, not copied out.
        Client->SendServerMiscObjUpdateMotd(1u, Wire->Text);
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagField_0x0c) != 0) {
        // Zero-extended.
        Client->SendServerMiscObjUpdateRank(1u, Wire->PlayerId, static_cast<int>(Wire->Rank));
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagField_0x10) != 0) {
        Client->SendServerMiscObjUpdateSystem(1u, Wire->PlayerId, Wire->SystemId);
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagIds) != 0) {
        // Passed by address (packet+0x58).
        Client->SendServerMiscObjUpdateFormation(1u, Wire->Ids);
        bDelivered = true;
    }
    return bDelivered;
}

// ===========================================================================
// The channel-2 payloads that carry a container, a permutation or a verbatim struct. The
// per-type layout notes are next to the structs in rpc_payloads.h.
// ===========================================================================

// Typed views on a payload that holds a live container (RpcPayloadWrite would overwrite
// the list header). Unchecked: the block is always the class's own size.
static FPayloadServerDamageObject* RpcDamageObjectPayload(FPacketBase* Packet) {
    return reinterpret_cast<FPayloadServerDamageObject*>(Packet->Payload());
}

static const FPayloadServerDamageObject* RpcDamageObjectPayload(const FPacketBase* Packet) {
    return reinterpret_cast<const FPayloadServerDamageObject*>(Packet->Payload());
}

static FPayloadServerCreateLoot* RpcCreateLootPayload(FPacketBase* Packet) {
    return reinterpret_cast<FPayloadServerCreateLoot*>(Packet->Payload());
}

static const FPayloadServerCreateLoot* RpcCreateLootPayload(const FPacketBase* Packet) {
    return reinterpret_cast<const FPayloadServerCreateLoot*>(Packet->Payload());
}

// ===========================================================================
// ch2 0x05 FLPACKET_SERVER_DAMAGEOBJECT
// Sender 0x06d88c10 (IClient+0x0fc), receiver caseD_5 at 0x06d81ed4. The 1 travels by value
// on both sides.
//
// IServerImpl::HandleDamageObject (0x06d84c60, the other caller of FUN_06d90290) is not on
// the HandlePacket path: caseD_5 inlines the same logic.
// ===========================================================================
static bool MarshalDamageObject(FPacketBase* Packet, const unsigned int* Args,
                                unsigned int ArgCount) {
    // The third argument is Common's DamageList, as both Server.dll senders push it.
    const DamageList* Source = reinterpret_cast<const DamageList*>(Args[2]);
    FPayloadServerDamageObject* Wire = RpcDamageObjectPayload(Packet);
    Wire->ObjectId = Args[1];
    // FUN_06d90290: list plus the four tail fields, i.e. DamageList::operator=.
    Wire->Damage = *Source;
    return true;
}

static bool DeliverDamageObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDamageObject* Wire = RpcDamageObjectPayload(Packet);
    // Retail builds the destination DamageList on its own stack and tears it down after
    // the call.
    DamageList Local;
    Local = Wire->Damage;                             // FUN_06d90290(packet, &local)
    Client->SendServerDamageObject(1u, Wire->ObjectId, Local);
    return true;
}

// IServerImpl::HandleDamageObject (0x06d84c60), the exported member rpclocal.cpp forwards
// to. Same body as caseD_5 except the slot's first argument is the third parameter instead
// of the literal 1, and a null packet returns false.
__declspec(dllexport) bool RpcHandleDamageObject(const FPacketBase* Packet, IClient* Client, unsigned int Arg) {
    if (Packet == nullptr) {
        return false;
    }
    const FPayloadServerDamageObject* Wire = RpcDamageObjectPayload(Packet);
    DamageList Local;
    Local = Wire->Damage;
    Client->SendServerDamageObject(Arg, Wire->ObjectId, Local);
    return true;
}

// ===========================================================================
// ch2 0x28 FLPACKET_SERVER_CREATELOOT
// Sender 0x06d8aae0 (IClient+0x0c0), receiver caseD_28 at 0x06d811c8. The literal 1
// travels by value on both sides.
// ===========================================================================
// The argument is Server.dll's FServerCreateLootRequest with its array of FServerLootDesc
// (server_createinfo.h). Floats move as bits: both directions use plain dword MOVs.
static_assert(sizeof(FServerLootDesc) == 0x48, "CREATELOOT record is 0x48 (the array stride)");
static_assert(offsetof(FServerLootDesc, Orientation) == 0x28, "CREATELOOT record +0x28");
static_assert(offsetof(FServerLootDesc, Velocity) == 0x38, "CREATELOOT record +0x38");
static_assert(offsetof(FServerLootDesc, Field_0x44) == 0x44, "CREATELOOT record +0x44");
static_assert(sizeof(FServerCreateLootRequest) == 0x14, "CREATELOOT argument struct 0x14");
static_assert(offsetof(FServerCreateLootRequest, Items) == 0x0c, "CREATELOOT argument +0x0c");

static bool MarshalCreateLoot(FPacketBase* Packet, const unsigned int* Args,
                              unsigned int ArgCount) {
    const FServerCreateLootRequest* Source =
        reinterpret_cast<const FServerCreateLootRequest*>(Args[1]);
    FPayloadServerCreateLoot* Wire = RpcCreateLootPayload(Packet);
    RpcPacketLootListInit(Packet);                                        // the packet ctor's dummy
    Wire->OwnerId = Source->OwnerId;
    Wire->Kind = (unsigned char)(Source->Kind & 0xffu);
    Wire->Pad_0x05[0] = 0;
    Wire->Pad_0x05[1] = 0;
    Wire->Pad_0x05[2] = 0;

    const unsigned int Count = Source->Count;                             // a byte, +0x10
    if (Count == 0) {
        return true;
    }
    // The array at src+0x0c is walked untested.
    for (unsigned int i = 0; i < Count; ++i) {
        const FServerLootDesc& In = Source->Items[i];
        // Retail zeroes the temporary once and seeds wire+0x34 with 1.0f; every field is
        // overwritten on each iteration, so the seed never ships.
        FRpcLootWireRecord Out;
        Out.LootId = In.LootId;                                        // record +0x00
        Out.ContentsNickname = In.ContentsNickname;                              // record +0x04
        Out.ContentsHitPtsBits = RpcCh2FloatBits(In.ContentsHitPts);               // record +0x08
        Out.Units = (unsigned short)(In.Units & 0xffffu);             // record +0x0c, a word
        Out.Pad_0x0e = 0;
        Out.ContainerNickname = In.ContainerNickname;                             // record +0x10
        Out.HitPtsBits = RpcCh2FloatBits(In.HitPts);                       // record +0x14
        Out.NameOverride = In.NameOverride;                                  // record +0x18
        Out.PosXBits = RpcCh2FloatBits(In.Position.x);                   // record +0x1c
        Out.PosYBits = RpcCh2FloatBits(In.Position.y);                   // record +0x20
        Out.PosZBits = RpcCh2FloatBits(In.Position.z);                   // record +0x24
        Out.VelXBits = RpcCh2FloatBits(In.Velocity.x);                   // record +0x38
        Out.VelYBits = RpcCh2FloatBits(In.Velocity.y);                   // record +0x3c
        Out.VelZBits = RpcCh2FloatBits(In.Velocity.z);                   // record +0x40
        Out.OrientWBits = RpcCh2FloatBits(In.Orientation.w);                // record +0x28
        Out.OrientYBits = RpcCh2FloatBits(In.Orientation.y);   // record +0x30 -- the transposed pair, see the header
        Out.OrientXBits = RpcCh2FloatBits(In.Orientation.x);   // record +0x2c
        Out.OrientZBits = RpcCh2FloatBits(In.Orientation.z);                // record +0x34
        Out.bMarkedBy = In.bMarkedBy;                                     // record +0x46
        Out.bTemporary = In.bTemporary;                                    // record +0x45
        Out.Pad_0x46[0] = 0;
        Out.Pad_0x46[1] = 0;
        RpcPacketLootPushBack(Packet, Out);                               // FUN_06d93160
    }
    return true;
}

static bool DeliverCreateLoot(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateLoot* Wire = RpcCreateLootPayload(Packet);
    // FUN_06d931b0: the list header's address, used untested.
    const FRpcLootWireList* List = RpcPacketLootList(Packet);
    if (List->Size == 0) {
        // An empty list drops the packet; the slot is never called.
        return true;
    }
    // Retail quirk: only the low byte of the list size is kept, as array length and as the
    // count handed on, yet the whole list is walked into the array with a byte index. A list
    // of 256 or more allocates short and writes past the end; reproduced.
    const unsigned int Count = List->Size & 0xffu;
    // The array new-expression's null test only skips the element constructors; the loop
    // below writes through the pointer anyway.
    FServerLootDesc* Items = static_cast<FServerLootDesc*>(
        ::operator new(sizeof(FServerLootDesc) * Count, std::nothrow));
    if (Items != nullptr) {
        // FUN_06d840d0 per element: zero, then +0x44 = 1, +0x45 = +0x46 = 0.
        for (unsigned int i = 0; i < Count; ++i) {
            unsigned char* Raw = reinterpret_cast<unsigned char*>(&Items[i]);
            for (unsigned int b = 0; b < (unsigned int)sizeof(FServerLootDesc); ++b) {
                Raw[b] = 0;
            }
            Items[i].Field_0x44 = 1;
            Items[i].bTemporary = 0;
            Items[i].bMarkedBy = 0;
        }
    }
    unsigned char Index = 0;
    for (const FRpcLootWireNode* Node = List->Head->Next; Node != List->Head;
         Node = Node->Next, ++Index) {
        const FRpcLootWireRecord& In = Node->Value;
        FServerLootDesc& Out = Items[Index];
        Out.LootId = In.LootId;                                        // record +0x00
        Out.ContentsNickname = In.ContentsNickname;                              // record +0x04
        RpcCh2SetFloatBits(Out.ContentsHitPts, In.ContentsHitPtsBits);             // record +0x08
        // Retail writes the word only; the element's upper half is the ctor's zero.
        Out.Units = (unsigned int)In.Units;                           // record +0x0c
        Out.ContainerNickname = In.ContainerNickname;                             // record +0x10
        RpcCh2SetFloatBits(Out.HitPts, In.HitPtsBits);                     // record +0x14
        Out.NameOverride = In.NameOverride;                                  // record +0x18
        RpcCh2SetFloatBits(Out.Position.x, In.PosXBits);                 // record +0x1c
        RpcCh2SetFloatBits(Out.Position.y, In.PosYBits);                 // record +0x20
        RpcCh2SetFloatBits(Out.Position.z, In.PosZBits);                 // record +0x24
        RpcCh2SetFloatBits(Out.Velocity.x, In.VelXBits);                 // record +0x38
        RpcCh2SetFloatBits(Out.Velocity.y, In.VelYBits);                 // record +0x3c
        RpcCh2SetFloatBits(Out.Velocity.z, In.VelZBits);                 // record +0x40
        RpcCh2SetFloatBits(Out.Orientation.w, In.OrientWBits);              // record +0x28
        RpcCh2SetFloatBits(Out.Orientation.x, In.OrientYBits);   // record +0x2c -- the transposed pair
        RpcCh2SetFloatBits(Out.Orientation.y, In.OrientXBits);   // record +0x30
        RpcCh2SetFloatBits(Out.Orientation.z, In.OrientZBits);              // record +0x34
        Out.bMarkedBy = In.bMarkedBy;                                     // record +0x46
        Out.bTemporary = In.bTemporary;                                    // record +0x45
    }

    FServerCreateLootRequest Info;
    Info.OwnerId = Wire->OwnerId;
    Info.Kind = Wire->Kind;                // zero-extended to a dword
    Info.bMarkedBy = 0;
    Info.bTemporary = 0;
    Info.Pad0a[0] = 0;
    Info.Pad0a[1] = 0;
    Info.Items = Items;
    Info.Count = (unsigned char)Count;
    Info.Pad11[0] = 0;
    Info.Pad11[1] = 0;
    Info.Pad11[2] = 0;
    Client->SendServerCreateLoot(1u, Info);
    ::operator delete(Items);
    return true;
}

// ===========================================================================
// ch2 0x2d FLPACKET_SERVER_CREATECOUNTER
// Sender 0x06d8a270 (IClient+0x0cc), receiver caseD_2d at 0x06d81ce9. Eighteen fields,
// permuted; the receiver is the strict inverse. The literal 1 is by reference.
// ===========================================================================
static bool MarshalCreateCounter(FPacketBase* Packet, const unsigned int* Args,
                                 unsigned int ArgCount) {
    // Server.dll's FServerCreateProjectileInfo, the record CREATEMINE sends too.
    const FServerCreateProjectileInfo* Source =
        reinterpret_cast<const FServerCreateProjectileInfo*>(Args[1]);
    FPayloadServerCreateCounter Wire;
    Wire.ArchetypeNickname = Source->ArchetypeNickname;                      // arg +0x04
    Wire.ObjId = Source->ObjId;                                  // arg +0x00
    Wire.OwnerPlayer = Source->OwnerPlayer;                             // arg +0x44
    Wire.OwnerId = Source->OwnerId;                                // arg +0x08
    Wire.SubObjId = Source->SubObjId;                               // arg +0x0c, a word
    Wire.Pad_0x12 = 0;
    Wire.OrientWBits = RpcCh2FloatBits(Source->Orientation.w);         // arg +0x1c
    Wire.OrientXBits = RpcCh2FloatBits(Source->Orientation.x);         // arg +0x20
    Wire.OrientYBits = RpcCh2FloatBits(Source->Orientation.y);         // arg +0x24
    Wire.OrientZBits = RpcCh2FloatBits(Source->Orientation.z);         // arg +0x28
    Wire.PosXBits = RpcCh2FloatBits(Source->Position.x);            // arg +0x10
    Wire.PosYBits = RpcCh2FloatBits(Source->Position.y);            // arg +0x14
    Wire.PosZBits = RpcCh2FloatBits(Source->Position.z);            // arg +0x18
    Wire.VelXBits = RpcCh2FloatBits(Source->Velocity.x);            // arg +0x2c
    Wire.VelYBits = RpcCh2FloatBits(Source->Velocity.y);            // arg +0x30
    Wire.VelZBits = RpcCh2FloatBits(Source->Velocity.z);            // arg +0x34
    Wire.AngVelXBits = RpcCh2FloatBits(Source->AngularVelocity.x);          // arg +0x38
    Wire.AngVelYBits = RpcCh2FloatBits(Source->AngularVelocity.y);          // arg +0x3c
    Wire.AngVelZBits = RpcCh2FloatBits(Source->AngularVelocity.z);          // arg +0x40
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverCreateCounter(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateCounter Wire =
        RpcPayloadRead<FPayloadServerCreateCounter>(Packet, 0);
    FServerCreateProjectileInfo Info;
    Info.ObjId = Wire.ObjId;
    Info.ArchetypeNickname = Wire.ArchetypeNickname;
    Info.OwnerId = Wire.OwnerId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad0e = 0;
    RpcCh2SetFloatBits(Info.Position.x, Wire.PosXBits);
    RpcCh2SetFloatBits(Info.Position.y, Wire.PosYBits);
    RpcCh2SetFloatBits(Info.Position.z, Wire.PosZBits);
    RpcCh2SetFloatBits(Info.Orientation.w, Wire.OrientWBits);
    RpcCh2SetFloatBits(Info.Orientation.x, Wire.OrientXBits);
    RpcCh2SetFloatBits(Info.Orientation.y, Wire.OrientYBits);
    RpcCh2SetFloatBits(Info.Orientation.z, Wire.OrientZBits);
    RpcCh2SetFloatBits(Info.Velocity.x, Wire.VelXBits);
    RpcCh2SetFloatBits(Info.Velocity.y, Wire.VelYBits);
    RpcCh2SetFloatBits(Info.Velocity.z, Wire.VelZBits);
    RpcCh2SetFloatBits(Info.AngularVelocity.x, Wire.AngVelXBits);
    RpcCh2SetFloatBits(Info.AngularVelocity.y, Wire.AngVelYBits);
    RpcCh2SetFloatBits(Info.AngularVelocity.z, Wire.AngVelZBits);
    Info.OwnerPlayer = Wire.OwnerPlayer;
    unsigned int One = 1u;
    Client->SendServerCreateCounter(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x37 FLPACKET_SERVER_CREATEGUIDED
// Sender 0x06d8a730 (IClient+0x0c8), receiver caseD_37 at 0x06d81da6. Fourteen fields,
// rotated; the receiver is the strict inverse. The literal 1 is by reference.
// ===========================================================================
static bool MarshalCreateGuided(FPacketBase* Packet, const unsigned int* Args,
                                unsigned int ArgCount) {
    const FServerCreateGuidedInfo* Source =
        reinterpret_cast<const FServerCreateGuidedInfo*>(Args[1]);
    // The first eight dwords (ObjId, Position, Orientation) are copied as plain dwords.
    const unsigned int* Head = reinterpret_cast<const unsigned int*>(Source);
    FPayloadServerCreateGuided Wire;
    Wire.OwnerPlayer = Source->OwnerPlayer;
    Wire.ArchetypeNickname = Source->ArchetypeNickname;
    Wire.OwnerId = Source->OwnerId;
    Wire.SubObjId = Source->SubObjId;               // a word
    Wire.Pad_0x0e = 0;
    Wire.TargetId = Source->TargetId;
    Wire.TargetSubObjId = Source->TargetSubObjId;         // a word
    Wire.Pad_0x16 = 0;
    Wire.ObjId = Head[0];                        // ObjId
    Wire.PosXBits = Head[1];                        // Position
    Wire.PosYBits = Head[2];
    Wire.PosZBits = Head[3];
    Wire.OrientWBits = Head[4];                        // Orientation
    Wire.OrientXBits = Head[5];
    Wire.OrientYBits = Head[6];
    Wire.OrientZBits = Head[7];
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverCreateGuided(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateGuided Wire =
        RpcPayloadRead<FPayloadServerCreateGuided>(Packet, 0);
    // Deviation: retail never writes +0x26 / +0x36 of its stack local; the port zeroes them.
    FServerCreateGuidedInfo Info;
    unsigned int* Head = reinterpret_cast<unsigned int*>(&Info);
    Head[0] = Wire.ObjId;                        // ObjId
    Head[1] = Wire.PosXBits;                        // Position
    Head[2] = Wire.PosYBits;
    Head[3] = Wire.PosZBits;
    Head[4] = Wire.OrientWBits;                        // Orientation
    Head[5] = Wire.OrientXBits;
    Head[6] = Wire.OrientYBits;
    Head[7] = Wire.OrientZBits;
    Info.TargetId = Wire.TargetId;
    Info.TargetSubObjId = Wire.TargetSubObjId;
    Info.Pad26 = 0;
    Info.ArchetypeNickname = Wire.ArchetypeNickname;
    Info.OwnerPlayer = Wire.OwnerPlayer;
    Info.OwnerId = Wire.OwnerId;
    Info.SubObjId = Wire.SubObjId;
    Info.Pad36 = 0;
    unsigned int One = 1u;
    Client->SendServerCreateGuided(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT
// Sender 0x06d8aa40 (IClient+0x0f8), receiver caseD_32 at 0x06d81c7d. The literal 1 is
// by value here.
// ===========================================================================
static bool MarshalRequestSpaceScript(FPacketBase* Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    const FServerRequestSpaceScriptInfo* Source =
        reinterpret_cast<const FServerRequestSpaceScriptInfo*>(Args[1]);
    FPayloadServerRequestSpaceScript Wire;
    Wire.ObjectId = Source->ObjectId;
    Wire.PosXBits = Source->PosXBits;
    Wire.PosYBits = Source->PosYBits;
    Wire.PosZBits = Source->PosZBits;
    // Only 1 and 2 are mapped; any other value keeps the constructor's seed (FUN_06d9db10
    // stores byte 1 at packet+0x34), so it ships 1.
    if (Source->CameraMode == 1u) {
        Wire.CameraModeWire = 1;
    } else if (Source->CameraMode == 2u) {
        Wire.CameraModeWire = 0;
    } else {
        Wire.CameraModeWire = 1;
    }
    Wire.Pad_0x15[0] = 0;
    Wire.Pad_0x15[1] = 0;
    Wire.Pad_0x15[2] = 0;

    // FUN_06d989b0: pointer at payload+0x18, count at payload+0x04. A zero-length name
    // ships nothing (the name is an inline array, so the null half of the guard never
    // fires).
    const unsigned int Length = RpcCh2StringLength(Source->Name);
    if (Length == 0) {
        Wire.NameSize = 0;
        Wire.Name = nullptr;
    } else {
        Wire.Name = static_cast<char*>(RpcCh2CopyBlock(Source->Name, Length + 1));
        Wire.NameSize = Length + 1;
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverRequestSpaceScript(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRequestSpaceScript Wire =
        RpcPayloadRead<FPayloadServerRequestSpaceScript>(Packet, 0);
    FServerRequestSpaceScriptInfo Info;
    Info.ObjectId = Wire.ObjectId;
    Info.PosXBits = Wire.PosXBits;
    Info.PosYBits = Wire.PosYBits;
    Info.PosZBits = Wire.PosZBits;
    Info.CameraMode = (Wire.CameraModeWire == 0) ? 2u : 1u;
    // Retail copies up to and including the NUL with no bound, skipped on null. The rest of
    // the 0x104-byte field is stack garbage in retail; the port zeroes it.
    for (unsigned int i = 0; i < kRequestSpaceScriptNameSize; ++i) {
        Info.Name[i] = '\0';
    }
    if (Wire.Name != nullptr) {
        char* Dest = Info.Name;
        unsigned int i = 0;
        do {
            Dest[i] = Wire.Name[i];
        } while (Wire.Name[i++] != '\0');
    }
    Client->SendServerRequestSpaceScript(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x4e FLPACKET_SERVER_SETDIRECTIVE
// Sender 0x06d8a480 (IClient+0x190), receiver caseD_4e at 0x06d81c3b. The payload is the
// argument struct: the receiver copies 0x94 bytes, the sender writes the same offsets one
// at a time. The literal 1 is by reference.
// ===========================================================================
static bool MarshalSetDirective(FPacketBase* Packet, const unsigned int* Args,
                                unsigned int ArgCount) {
    const FPayloadServerSetDirective* Source =
        reinterpret_cast<const FPayloadServerSetDirective*>(Args[1]);
    FPayloadServerSetDirective Wire = *Source;
    // Deviation: retail never writes the pads; the port zeroes them.
    Wire.Pad_0x09[0] = 0;
    Wire.Pad_0x09[1] = 0;
    Wire.Pad_0x09[2] = 0;
    Wire.Pad_0x91[0] = 0;
    Wire.Pad_0x91[1] = 0;
    Wire.Pad_0x91[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSetDirective(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetDirective Wire =
        RpcPayloadRead<FPayloadServerSetDirective>(Packet, 0);
    FPayloadServerSetDirective Info = Wire;
    unsigned int One = 1u;
    Client->SendServerSetDirective(One, Info);
    return true;
}

// ===========================================================================
// The flattened-FmtStr family: ch2 0x29, 0x2b, 0x34, 0x35, 0x45.
//
// The COMMON.DLL entry points retail imports for this (flatten, unflatten, ~FmtStr,
// Val::get_flattened_size) are declared in common_text.h; see the family section in
// rpc_payloads.h. RpcFlattenFmtStr / RpcUnflattenFmtStr live in rpc_containers.cpp (ch1
// POP_UP_DIALOG uses the same pair).
// ===========================================================================

// ===========================================================================
// ch2 0x29 FLPACKET_SERVER_SETREPUTATION
// Sender 0x06d88d70 (IClient+0x108), receiver caseD_29 at 0x06d813cf, which calls the same
// slot on the real IClient, so the argument struct round-trips: +0x00 <-> payload+0x38,
// +0x04 <-> payload+0x10, +0x08 <-> payload+0x3c, the relation array via the packet's list,
// both FmtStrs and the wide name. The literal 1 is by value.
//
// Retail drops arg+0x0c (filled by Reputation::Vibe::Get, read by no sender field).
// Reproduced.
// ===========================================================================
static bool MarshalSetReputation(FPacketBase* Packet, const unsigned int* Args,
                                 unsigned int ArgCount) {
    const FServerSetReputationInfo* Source =
        reinterpret_cast<const FServerSetReputationInfo*>(Args[1]);
    // Retail stores field by field into the freshly built packet; everything else keeps the
    // constructor's seed (FUN_06d9d290): +0x14..+0x24 = 1, 2, 4, 8, 0x10, mask byte +0x34 =
    // 0, display name null, relation list dummy built. A whole-payload write would clobber
    // those and leak the dummy.
    FPayloadServerSetReputation* Wire = Ch2Payload<FPayloadServerSetReputation>(Packet);
    Wire->VibeHandle = (unsigned int)Source->VibeHandle;     // arg+0x00
    Wire->Affiliation = Source->Affiliation;                   // arg+0x08
    // Size (6 + the Val sizes), bare operator new, FmtStr::flatten.
    Wire->NameABuffer = RpcFlattenFmtStr(Source->NameA, Wire->NameASize);
    Wire->NameBBuffer = RpcFlattenFmtStr(Source->NameB, Wire->NameBSize);

    // Unbounded wcslen, then FUN_06d92710: an empty name ships no block. Retail quirk: the
    // block is operator new((len+1)*4) -- four bytes per wchar_t -- but only (len+1)*2 bytes
    // are copied. Reproduced.
    const wchar_t* NameSource = Source->DisplayName;
    unsigned int NameChars = 0;
    while (NameSource[NameChars] != L'\0') {
        ++NameChars;
    }
    if (NameChars != 0) {
        const unsigned int Total = NameChars + 1;
        wchar_t* NameBlock = static_cast<wchar_t*>(::operator new(Total * 4, std::nothrow));
        for (unsigned int i = 0; i < Total; ++i) {
            NameBlock[i] = NameSource[i];
        }
        Wire->DisplayName = NameBlock;
        Wire->DisplayNameLength = Total;
    }

    // Retail builds a stack list (16-bit index against the byte count at arg+0x14, array at
    // arg+0x10 read untested), then FUN_06d92760 copies it into the packet's list and frees
    // the stack list. The two hops collapse into one push_back per element; the packet ends
    // up the same (see the relation-list note in rpc_containers.h).
    FRpcRelationList* List = &Wire->Relations;
    const unsigned int Count = Source->RelationCount;
    for (unsigned int i = 0; i < Count; ++i) {
        // Reputation::Relation -> its wire view FRpcRelation (same two dwords).
        RpcRelationListPushBack(List,
            reinterpret_cast<const FRpcRelation&>(Source->Relations[i]));
    }
    Wire->Attitude = Source->Attitude;                       // arg+0x04
    return true;
}

static bool DeliverSetReputation(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetReputation Wire =
        RpcPayloadRead<FPayloadServerSetReputation>(Packet, 0);

    FServerSetReputationInfo Info;
    Info.VibeHandle = (int)Wire.VibeHandle;
    Info.Attitude = Wire.Attitude;
    Info.Affiliation = Wire.Affiliation;
    Info.Rank = 0;                                     // retail leaves it as garbage
    Info.Relations = nullptr;
    Info.RelationCount = 0;
    Info.Pad_0x15[0] = 0;
    Info.Pad_0x15[1] = 0;
    Info.Pad_0x15[2] = 0;
    for (unsigned int i = 0; i < 24; ++i) {
        Info.DisplayName[i] = L'\0';                         // retail writes only [0]
    }

    RpcUnflattenFmtStr(Info.NameA, Wire.NameABuffer, Wire.NameASize);
    RpcUnflattenFmtStr(Info.NameB, Wire.NameBBuffer, Wire.NameBSize);

    // Unbounded wcscpy, as retail's.
    if (Wire.DisplayName != nullptr) {
        wcscpy(Info.DisplayName, Wire.DisplayName);
    }

    const FRpcRelationList* List =
        reinterpret_cast<const FRpcRelationList*>(Packet->Payload() + 0x28);
    // FUN_06d927e0 copies the packet's list into a stack list, skipping null values, so its
    // size counts only the non-null ones. The port reads the packet's list in place and
    // counts explicitly.
    unsigned int Size = 0;
    for (const FRpcRelationNode* Node = List->Head->Next; Node != List->Head;
         Node = Node->Next) {
        if (Node->Value != nullptr) {
            ++Size;
        }
    }
    // The size is tested as a dword but ships as a byte count.
    Info.RelationCount = (unsigned char)(Size & 0xffu);
    FRpcRelation* Items = nullptr;
    if (Size != 0) {
        // The walk covers the whole list with a 16-bit index, no bound against Size.
        Items = static_cast<FRpcRelation*>(
            ::operator new(sizeof(FRpcRelation) * Size, std::nothrow));
        const FRpcRelationNode* Dummy = List->Head;
        unsigned short At = 0;
        for (const FRpcRelationNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
            if (Node->Value != nullptr) {
                Items[At] = *Node->Value;
                ++At;
            }
        }
        // The wire view back to Reputation::Relation (same two dwords).
        Info.Relations = reinterpret_cast< ::Reputation::Relation*>(Items);
    }

    Client->SendServerSetReputation(1u, Info);

    // Retail quirk: freed only when the byte count is non-zero, so a list of exactly 256*k
    // relations leaks the array. Count and pointer are read back from the local.
    if (Info.RelationCount > 0) {
        ::operator delete(Info.Relations);
    }
    return true;
}

// ===========================================================================
// ch2 0x2b FLPACKET_SERVER_SETGROUPFEELINGS
// Sender 0x06d8a5c0 (IClient+0x148), receiver caseD_2b at 0x06d815bb -> the same slot. No
// FmtStr: the name is a narrow string written by FUN_06d91850 (operator new + strncpy,
// count at packet+0x24) and read back by strncpy. The relation list is the same
// std::list<Relation*> as 0x29.
// ===========================================================================
static bool MarshalSetGroupFeelings(FPacketBase* Packet, const unsigned int* Args,
                                    unsigned int ArgCount) {
    const FServerSetGroupFeelingsInfo* Source =
        reinterpret_cast<const FServerSetGroupFeelingsInfo*>(Args[1]);
    // The constructor (FUN_06d9c540) zeroed the name pair and built the relation list's
    // dummy, so no whole-payload write (it would leak the dummy).
    FPayloadServerSetGroupFeelings* Wire = Ch2Payload<FPayloadServerSetGroupFeelings>(Packet);

    // Unbounded strlen+1 is both block size and count; no zero-length test, so an empty
    // name still ships one byte.
    unsigned int Length = 0;
    while (Source->Name[Length] != '\0') {
        ++Length;
    }
    const unsigned int Total = Length + 1;
    char* Block = static_cast<char*>(::operator new(Total, std::nothrow));
    for (unsigned int i = 0; i < Total; ++i) {
        Block[i] = Source->Name[i];
    }
    Wire->Name = Block;
    Wire->NameSize = Total;

    // As in SETREPUTATION, retail's stack list plus FUN_06d91ca0 copy collapse into one
    // push_back per element (rpc_containers.h, relation list).
    FRpcRelationList* List = &Wire->Relations;
    const unsigned int Count = Source->RelationCount;
    for (unsigned int i = 0; i < Count; ++i) {
        RpcRelationListPushBack(List,
            reinterpret_cast<const FRpcRelation&>(Source->Relations[i]));
    }
    return true;
}

static bool DeliverSetGroupFeelings(const FPacketBase* Packet, IClient* Client,
                                    IChatClient*) {
    const FPayloadServerSetGroupFeelings Wire =
        RpcPayloadRead<FPayloadServerSetGroupFeelings>(Packet, 0);

    FServerSetGroupFeelingsInfo Info;
    for (unsigned int i = 0; i < 0x10; ++i) {
        Info.Name[i] = '\0';
    }
    Info.Relations = nullptr;
    Info.RelationCount = 0;
    Info.Pad_0x15[0] = 0;
    Info.Pad_0x15[1] = 0;
    Info.Pad_0x15[2] = 0;

    // No null test, no bound against the 0x10-byte field: retail trusts the sender's count.
    strncpy(Info.Name, Wire.Name, Wire.NameSize);

    const FRpcRelationList* List =
        reinterpret_cast<const FRpcRelationList*>(Packet->Payload() + 0x08);
    // FUN_06d91d20 copies the list skipping null values; the port counts explicitly.
    unsigned int Size = 0;
    for (const FRpcRelationNode* Node = List->Head->Next; Node != List->Head;
         Node = Node->Next) {
        if (Node->Value != nullptr) {
            ++Size;
        }
    }
    Info.RelationCount = (unsigned char)(Size & 0xffu);
    FRpcRelation* Items = nullptr;
    if (Size != 0) {                                         // tested as a dword
        // Whole list, 16-bit index, null values skipped, no bound against Size.
        Items = static_cast<FRpcRelation*>(
            ::operator new(sizeof(FRpcRelation) * Size, std::nothrow));
        const FRpcRelationNode* Dummy = List->Head;
        unsigned short At = 0;
        for (const FRpcRelationNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
            if (Node->Value != nullptr) {
                Items[At] = *Node->Value;
                ++At;
            }
        }
        Info.Relations = reinterpret_cast< ::Reputation::Relation*>(Items);
    }

    Client->SendServerSetGroupFeelings(1u, Info);

    // Retail quirk: the relation array is never freed (only the local list is), so every
    // SETGROUPFEELINGS leaks it; reproduced.
    return true;
}

// ===========================================================================
// ch2 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE
// Sender 0x06d8ad90 (IClient+0x11c), receiver caseD_34 at 0x06d82494 -> the same slot.
// The index byte is zero-extended to a dword going out and read back as a byte. The
// literal 1 travels by reference.
// ===========================================================================
static bool MarshalReplaceMissionObjective(FPacketBase* Packet, const unsigned int* Args,
                                           unsigned int ArgCount) {
    const FServerReplaceMissionObjectiveInfo* Source =
        reinterpret_cast<const FServerReplaceMissionObjectiveInfo*>(Args[1]);
    FPayloadServerReplaceMissionObjective* Wire =
        Ch2Payload<FPayloadServerReplaceMissionObjective>(Packet);
    Wire->MissionId = Source->MissionId;
    Wire->State = Source->ObjectiveIndex;
    // FmtStr at arg+0x04: size into +0x08, block into +0x0c.
    Wire->TextBuffer = RpcFlattenFmtStr(Source->Text, Wire->TextSize);
    return true;
}

static bool DeliverReplaceMissionObjective(const FPacketBase* Packet, IClient* Client,
                                           IChatClient*) {
    FPayloadServerReplaceMissionObjective Wire =
        RpcPayloadRead<FPayloadServerReplaceMissionObjective>(Packet, 0);

    FServerReplaceMissionObjectiveInfo Info;
    Info.MissionId = Wire.MissionId;
    Info.ObjectiveType = 0;
    Info.ObjectiveIndex = (unsigned char)(Wire.State & 0xffu);
    Info.Pad_0x21[0] = 0;
    Info.Pad_0x21[1] = 0;
    Info.Pad_0x21[2] = 0;
    RpcUnflattenFmtStr(Info.Text, Wire.TextBuffer, Wire.TextSize);

    unsigned int One = 1u;
    Client->SendServerReplaceMissionObjective(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE
// Sender 0x06d8af60 (IClient+0x118), receiver caseD_45 at 0x06d82623 -> the same slot. One
// FmtStr and one dword. The literal 1 is by reference.
// ===========================================================================
static bool MarshalSetMissionMessage(FPacketBase* Packet, const unsigned int* Args,
                                     unsigned int ArgCount) {
    const FServerSetMissionMessageInfo* Source =
        reinterpret_cast<const FServerSetMissionMessageInfo*>(Args[1]);
    FPayloadServerSetMissionMessage* Wire = Ch2Payload<FPayloadServerSetMissionMessage>(Packet);
    // FmtStr at arg+0x00: size into +0x00, block into +0x04.
    Wire->TextBuffer = RpcFlattenFmtStr(Source->Text, Wire->TextSize);
    Wire->Type = Source->Type;
    return true;
}

static bool DeliverSetMissionMessage(const FPacketBase* Packet, IClient* Client,
                                     IChatClient*) {
    FPayloadServerSetMissionMessage Wire =
        RpcPayloadRead<FPayloadServerSetMissionMessage>(Packet, 0);

    FServerSetMissionMessageInfo Info;
    Info.Type = Wire.Type;
    RpcUnflattenFmtStr(Info.Text, Wire.TextBuffer, Wire.TextSize);

    unsigned int One = 1u;
    Client->SendServerSetMissionMessage(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES
// Sender 0x06d8b020 (IClient+0x120), receiver caseD_35 at 0x06d826c9 -> the same slot. Two
// top-level FmtStrs, a count, and a list of per-objective records.
//
// The receiver rebuilds the element array with a VC6 vector-new (count cookie, FmtStr ctor
// per element) and walks the list with the linear accessor FUN_06d8fc60 (element i costs i
// steps; null once the walk runs off the end). The literal 1 is by reference.
// ===========================================================================
static bool MarshalSetMissionObjectives(FPacketBase* Packet, const unsigned int* Args,
                                        unsigned int ArgCount) {
    const FServerSetMissionObjectivesInfo* Source =
        reinterpret_cast<const FServerSetMissionObjectivesInfo*>(Args[1]);
    // The constructor (FUN_06d9da40) zeroed +0x08..+0x18 and built the record list's dummy,
    // so no whole-payload write (it would leak the dummy).
    FPayloadServerSetMissionObjectives* Live =
        reinterpret_cast<FPayloadServerSetMissionObjectives*>(Packet->Payload());
    Live->MissionId = Source->MissionId;
    Live->MissionType = Source->MissionType;                   // zero-extended
    // Size at +0x08/+0x10, block at +0x0c/+0x14.
    Live->TextABuffer = RpcFlattenFmtStr(Source->TextA, Live->TextASize);
    Live->TextBBuffer = RpcFlattenFmtStr(Source->TextB, Live->TextBSize);
    // The dword count at arg+0x08 is the only bound; the entry array is read untested.
    const unsigned int Count = Source->Count;
    for (unsigned int i = 0; i < Count; ++i) {
        // The new-expression's null test skips only the zeroing; the stores below go
        // through the pointer either way.
        FRpcMissionObjectiveRecord* Record =
            static_cast<FRpcMissionObjectiveRecord*>(
                ::operator new(sizeof(FRpcMissionObjectiveRecord), std::nothrow));
        if (Record != nullptr) {
            Record->TextSize = 0;
            Record->TextBuffer = nullptr;
        }
        Record->State = (unsigned char)(Source->Entries[i].State & 0xffu);
        Record->Pad_0x01[0] = 0;
        Record->Pad_0x01[1] = 0;
        Record->Pad_0x01[2] = 0;
        Record->TextBuffer = RpcFlattenFmtStr(Source->Entries[i].Text, Record->TextSize);
        // FUN_06d8fc10 pushes and bumps packet+0x38 and packet+0x44 together.
        RpcMissionObjectiveListPushBack(&Live->Objectives, Record);
        ++Live->Count;
    }
    return true;
}

static bool DeliverSetMissionObjectives(const FPacketBase* Packet, IClient* Client,
                                        IChatClient*) {
    const FPayloadServerSetMissionObjectives* Wire =
        reinterpret_cast<const FPayloadServerSetMissionObjectives*>(Packet->Payload());

    FServerSetMissionObjectivesInfo Info;
    Info.MissionId = Wire->MissionId;
    Info.Entries = nullptr;
    Info.Count = Wire->Count;
    Info.MissionType = (unsigned char)(Wire->MissionType & 0xffu);
    Info.Pad_0x3d[0] = 0;
    Info.Pad_0x3d[1] = 0;
    Info.Pad_0x3d[2] = 0;

    RpcUnflattenFmtStr(Info.TextA, Wire->TextABuffer, Wire->TextASize);
    RpcUnflattenFmtStr(Info.TextB, Wire->TextBBuffer, Wire->TextBSize);

    FServerMissionObjectiveEntry* Entries = nullptr;
    const unsigned int Count = Wire->Count;
    if (Count != 0) {
        // The vector-new's null test skips only the element constructors; the loop writes
        // through the result either way.
        Entries = new (std::nothrow) FServerMissionObjectiveEntry[Count];
        for (unsigned int i = 0; i < Count; ++i) {
            // Used untested: the sender bumps count and list size together, so retail
            // never walks past the end.
            const FRpcMissionObjectiveRecord* Record =
                RpcMissionObjectiveListAt(&Wire->Objectives, i);
            Entries[i].State = Record->State;
            RpcUnflattenFmtStr(Entries[i].Text, Record->TextBuffer, Record->TextSize);
        }
        Info.Entries = Entries;
    }

    unsigned int One = 1u;
    Client->SendServerSetMissionObjectives(One, Info);

    // Count and pointer are re-read from the local the slot was handed.
    if (Info.Count != 0 && Info.Entries != nullptr) {
        delete[] Info.Entries;
    }
    return true;
}

// ===========================================================================
// ch2 0x4d FLPACKET_SERVER_LOADHINT
// Sender 0x06d8a3c0 (IClient+0x144), receiver caseD_4d at 0x06d81ba3 -> the same slot. The
// object is a CostumeHint (see rpc_payloads.h).
//
// The sender dispatches Serialize (vtable +0x4) and CleanUp (+0xc) through the object's own
// vtable, so it takes any BaseHint (the three virtuals are pure on BaseHint,
// .rdata:063a3700), and IServer::LoadHint passes a BaseHint*.
// ===========================================================================

// After COMMON's CostumeHint ctor, the receiver stores rpclocal's own vftable
// (.rdata:06db3740), the VC6 local copy for a dllimport class: slot 0 is rpclocal's scalar
// deleting dtor FUN_06d83dd0 (COMMON's dtor, then rpclocal's operator delete), slots 1..3
// thunk to COMMON's Serialize / UnSerialize / CleanUp. A modern compiler does not re-stamp
// a dllimport vptr, so a module-local derived class gives the same object shape; no members
// are added, so it stays 0x3c bytes.
struct FRpcLocalCostumeHint final : public CostumeHint
{
};

static_assert(sizeof(FRpcLocalCostumeHint) == 0x3C, "operator new(0x3c) at 06d81bad");

static bool MarshalLoadHint(FPacketBase* Packet, const unsigned int* Args,
                            unsigned int ArgCount) {
    BaseHint* Source = reinterpret_cast<BaseHint*>(Args[1]);
    FPayloadServerLoadHint* Wire = Ch2Payload<FPayloadServerLoadHint>(Packet);

    // Serialize(&OutBuf) returns the byte count.
    unsigned char* Serialized = nullptr;
    const unsigned int Size = Source->Serialize(&Serialized);
    // FUN_06d90a00, with no test of either value.
    {
        void* Block = ::operator new(Size, std::nothrow);
        unsigned char* Dest = static_cast<unsigned char*>(Block);
        for (unsigned int i = 0; i < Size; ++i) {
            Dest[i] = Serialized[i];
        }
        Wire->Buffer = Block;
        Wire->Size = Size;
    }
    Wire->Kind = Source->Kind;

    Source->CleanUp(Serialized);
    return true;
}

static bool DeliverLoadHint(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerLoadHint Wire = RpcPayloadRead<FPayloadServerLoadHint>(Packet, 0);

    CostumeHint* Hint = nullptr;
    if (Wire.Kind == 1) {
        // The new-expression's null test skips only the constructor; UnSerialize is then
        // called through the vtable untested.
        Hint = new (std::nothrow) FRpcLocalCostumeHint;
        Hint->UnSerialize(static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    }

    // Retail quirk: the hint pointer goes out even when null, and the exe's LAB_0053de70
    // reads Kind (+4) through it untested. The literal 1 travels by reference.
    unsigned int One = 1u;
    Client->SendServerLoadHint(One, Hint);

    if (Hint != nullptr) {
        delete Hint;                                         // scalar deleting dtor, arg 1
    }
    return true;
}

// ===========================================================================
// ch2 0x55 FLPACKET_SERVER_OBJECTCARGOUPDATE
// Sender 0x06d8c0f0 (IClient+0x158), receiver caseD_55 at 0x06d82abd -> the same slot. The
// receiver rebuilds a real EquipDesc through seven COMMON.DLL entry points.
// ===========================================================================
static bool MarshalObjectCargoUpdate(FPacketBase* Packet, const unsigned int* Args,
                                     unsigned int ArgCount) {
    // The pad +0x11..+0x13 is written neither here nor by the constructor FUN_06d9ee70.
    const EquipDesc* Source = reinterpret_cast<const EquipDesc*>(Args[2]);
    FPayloadServerObjectCargoUpdate* Wire = Ch2Payload<FPayloadServerObjectCargoUpdate>(Packet);
    Wire->ObjectId = Args[1];
    Wire->EquipId = Source->Id;                              // word
    Wire->ArchId = Source->ArchId;
    Wire->Count = (unsigned short)(Source->Count & 0xffff);  // truncated to a word
    Wire->Status = Source->Health;                           // FLD/FSTP in retail
    // The byte at EquipDesc+0x18, not normalised.
    Wire->bMission = *reinterpret_cast<const unsigned char*>(&Source->bMission);
    return true;
}

static bool DeliverObjectCargoUpdate(const FPacketBase* Packet, IClient* Client,
                                     IChatClient*) {
    const FPayloadServerObjectCargoUpdate Wire =
        RpcPayloadRead<FPayloadServerObjectCargoUpdate>(Packet, 0);

    EquipDesc Desc;
    Desc.set_id(Wire.EquipId);
    Desc.set_arch_id(Wire.ArchId);
    Desc.set_count((int)Wire.Count);
    Desc.set_status(Wire.Status);
    Desc.make_internal();
    Desc.set_equipped(false);
    // No setter: retail writes EquipDesc+0x18 directly, normalised to 0/1.
    Desc.bMission = (Wire.bMission != 0);

    unsigned int One = 1u;
    Client->SendServerObjectCargoUpdate(One, Wire.ObjectId, Desc);
    return true;
}

// ===========================================================================
// ch2 0x4f FLPACKET_SERVER_SENDCOMM
// Sender 0x06d88fc0 (IClient+0x110, 23 args), receiver caseD_4f at 0x06d8306c. Layout and
// the per-field accessor list are on FPayloadServerSendComm.
// ===========================================================================
// The dword the sender's Ids loop reads for index I. Retail reads its own frame at
// ESP0-0x24+4*I (ESP0 = the return-address slot on entry), so
//   I 0..8   the scratch copy of A8..A16
//   I 9      the return address into the caller
//   I 10..32 the arguments themselves again, A0..A22
//   I >= 33  the caller's frame -- not determinable.
// Args carries the 23 arguments plus the return address at [23] (the proxy puts it there
// with _ReturnAddress()). For I >= 33 the port reads past Args into its own stack where
// retail reads Server.dll's; both have already written past the 0x78-byte packet by then.
static unsigned int RpcSendCommFrameDword(const unsigned int* Args, int Index) {
    if (Index <= 8) {
        return Args[8 + Index];
    }
    if (Index == 9) {
        return Args[23];
    }
    return Args[Index - 10];
}

static bool MarshalSendComm(FPacketBase* Packet, const unsigned int* Args,
                            unsigned int ArgCount) {
    // Stores go straight into the constructed packet: the Ids loop below can run past the
    // array into the fields after it, and the old-buffer test reads whatever that left.
    FPayloadServerSendComm* Wire =
        reinterpret_cast<FPayloadServerSendComm*>(Packet->Payload());
    Wire->SenderId = Args[1];
    Wire->ReceiverId = Args[2];
    Wire->VoiceId = Args[3];

    Wire->Body.Head = Args[4];
    Wire->Body.Body = Args[5];
    Wire->Body.LeftHand = Args[6];
    Wire->Body.RightHand = Args[7];
    Wire->Body.Count = (int)Args[16];
    // Retail quirk: Ids[i] = A8+i while i < Count, the bound re-read from payload+0x3c every
    // pass and compared signed, with no clamp to the eight-entry array. Ids[8] is Count
    // itself, Ids[9..14] are the fields stored below, and beyond that the writes leave the
    // payload. Reads past i = 8 come from the rest of the frame (RpcSendCommFrameDword).
    unsigned int* const IdSlots = Wire->Body.Ids;
    for (int i = 0; i < Wire->Body.Count; ++i) {
        IdSlots[i] = RpcSendCommFrameDword(Args, i);
    }

    Wire->CommType = Args[17];
    // The old pointer at payload+0x44 is tested and freed.
    if (Wire->Buffer != nullptr) {
        ::operator delete(Wire->Buffer);
    }
    // operator new(A19 << 2) whatever A19's sign, then A19 dwords copied out of A18,
    // untested, while A19 > 0.
    const int BufferCount = (int)Args[19];
    Wire->Buffer = static_cast<unsigned int*>(
        ::operator new((unsigned int)BufferCount << 2, std::nothrow));
    // Truncated to a byte here and sign-extended back by the receiver; both reproduced.
    Wire->BufferCount = (unsigned char)((unsigned int)BufferCount & 0xffu);
    const unsigned int* Source = reinterpret_cast<const unsigned int*>(Args[18]);
    for (int i = 0; i < BufferCount; ++i) {
        Wire->Buffer[i] = Source[i];
    }

    Wire->Mood = Args[20];
    Wire->DurationBits = Args[21];                         // FLD/FSTP in retail
    Wire->bReceiverIsOwnShip = (unsigned char)(Args[22] & 0xffu);
    return true;
}

static bool DeliverSendComm(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSendComm Wire = RpcPayloadRead<FPayloadServerSendComm>(Packet, 0);
    // Signed (MOVSX at 06d84200), so 0x80..0xff arrive negative.
    const int BufferCount = (int)(signed char)Wire.BufferCount;
    // The thirteen Body dwords are the by-value Costume, in its own order: PartIds[4],
    // AccessoryIds[8], AccessoryCount.
    Costume CommCostume;
    CommCostume.PartIds[0] = Wire.Body.Head;
    CommCostume.PartIds[1] = Wire.Body.Body;
    CommCostume.PartIds[2] = Wire.Body.LeftHand;
    CommCostume.PartIds[3] = Wire.Body.RightHand;
    for (unsigned int i = 0; i < 8; ++i) {
        CommCostume.AccessoryIds[i] = Wire.Body.Ids[i];
    }
    CommCostume.AccessoryCount = (unsigned int)Wire.Body.Count;
    float Duration = 0.0f;
    RpcCh2SetFloatBits(Duration, Wire.DurationBits);
    Client->SendServerSendComm(1u,
                               Wire.SenderId, Wire.ReceiverId, Wire.VoiceId,
                               CommCostume,
                               Wire.CommType,
                               Wire.Buffer,
                               BufferCount,
                               Wire.Mood,
                               Duration,
                               // A literal 0, not payload+0x54, which retail writes and
                               // never reads.
                               false);
    return true;
}

// ===========================================================================
// ch2 0x42 FLPACKET_SERVER_CREATESOLAR and 0x04 FLPACKET_SERVER_CREATESHIP
//
// Both start with the 0x7c-byte create-object header (rpc_containers.h) and both gate
// their reputation tail on SinglePlayer().
// ===========================================================================
// Both records are the types Server.dll builds (CSolarCreateInfo / CShipCreateInfo,
// server_createinfo.h). Retail keeps each receiver's destination in a file static,
// .data:0x06dbb838 (CREATESOLAR) and .data:0x06dbb940 (CREATESHIP), lazily constructed
// behind bits of .data:0x06dbb914, destroyed via atexit, and cleared (not rebuilt) after
// every slot call. A function-local static gives the same lifetime.

static bool MarshalCreateSolar(FPacketBase* Packet, const unsigned int* Args,
                               unsigned int ArgCount) {
    const CSolarCreateInfo* Source = reinterpret_cast<const CSolarCreateInfo*>(Args[1]);
    FPayloadServerCreateSolar* Wire =
        reinterpret_cast<FPayloadServerCreateSolar*>(Packet->Payload());
    RpcCreateObjectHeaderWrite(&Wire->Common, *Source);
    Wire->bDestructible = Source->bDestructible;
    Wire->bMarkedBy = Source->bMarkedBy;                    // src+0xa8
    // The name block +0x80..+0x8c keeps the constructor's zeroes (FUN_06d9e630); the pad
    // at +0x7e and Affiliation / Attitude (+0x90 / +0x94) are not seeded, so in single
    // player they ship uninitialised, in retail and here (the receiver reads them only when
    // !SinglePlayer()).
    if (SinglePlayer()) {
        return true;
    }
    Reputation::Vibe::GetAffiliation(Wire->Common.VibeGroupId,
                                     Wire->Affiliation, false);
    Reputation::Vibe::AttitudeTowards(Wire->Common.VibeGroupId,
                                      Source->VibeOtherGroupId,
                                      Wire->Attitude);                   // src+0xb0
    FmtStr NameA;
    FmtStr NameB;
    wchar_t const* DisplayName = nullptr;
    Reputation::Vibe::GetName(Wire->Common.VibeGroupId, NameA, NameB, DisplayName);
    // CREATESOLAR drops the wide name; only CREATESHIP ships it.
    Wire->NameABuffer = RpcFlattenFmtStr(NameA, Wire->NameASize);
    Wire->NameBBuffer = RpcFlattenFmtStr(NameB, Wire->NameBSize);
    return true;
}

static bool DeliverCreateSolar(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateSolar* Wire =
        reinterpret_cast<const FPayloadServerCreateSolar*>(Packet->Payload());
    // caseD_42's one-time init is CSolarCreateInfo's constructor (+0xac/+0xad = 0,
    // +0xb0 = -1, vftable .rdata:06db3738).
    static CSolarCreateInfo Static;
    RpcCreateObjectHeaderRead(Static, &Wire->Common);
    Static.bDestructible = Wire->bDestructible;
    Static.bMarkedBy = Wire->bMarkedBy;                     // +0xa8
    if (!SinglePlayer()) {
        FmtStr NameA;
        FmtStr NameB;
        RpcUnflattenFmtStr(NameA, Wire->NameABuffer, Wire->NameASize);
        RpcUnflattenFmtStr(NameB, Wire->NameBBuffer, Wire->NameBSize);
        Reputation::Vibe::EnsureExists(Wire->Common.VibeGroupId);
        Reputation::Vibe::SetName(Wire->Common.VibeGroupId, NameA, NameB, nullptr);
        Reputation::Vibe::SetAffiliation(Wire->Common.VibeGroupId,
                                         Wire->Affiliation, false);
        Reputation::Vibe::SetAttitudeTowardsPlayer(Wire->Common.VibeGroupId,
                                                   Wire->Attitude);
    }
    Client->SendServerCreateSolar(1u, Static);
    // The base clear(), inlined in retail (no override for the solar type).
    Static.clear();
    return true;
}

static bool MarshalCreateShip(FPacketBase* Packet, const unsigned int* Args,
                              unsigned int ArgCount) {
    const CShipCreateInfo* Source = reinterpret_cast<const CShipCreateInfo*>(Args[1]);
    FPayloadServerCreateShip* Wire =
        reinterpret_cast<FPayloadServerCreateShip*>(Packet->Payload());
    RpcCreateObjectHeaderWrite(&Wire->Common, *Source);
    Wire->bUsingTradelane = Source->bUsingTradelane;              // src+0xdc
    // src+0xb8..+0xcc, the two Vectors: plain MOV pairs, no FLD/FSTP, so bit copies.
    std::memcpy(&Wire->VelXBits, &Source->Velocity.x, sizeof(Wire->VelXBits));
    std::memcpy(&Wire->VelY, &Source->Velocity.y, sizeof(Wire->VelY));
    std::memcpy(&Wire->VelZ, &Source->Velocity.z, sizeof(Wire->VelZ));
    std::memcpy(&Wire->AngVelXBits, &Source->AngularVelocity.x, sizeof(Wire->AngVelXBits));
    std::memcpy(&Wire->AngVelY, &Source->AngularVelocity.y, sizeof(Wire->AngVelY));
    std::memcpy(&Wire->AngVelZ, &Source->AngularVelocity.z, sizeof(Wire->AngVelZ));
    std::memcpy(&Wire->Throttle, &Source->Throttle, sizeof(Wire->Throttle));   // src+0xd0
    Wire->OwnerPlayer = Source->OwnerPlayer;                  // src+0xac
    Wire->GroupNameId = (unsigned short)(Source->GroupNameId & 0xffffu);
    Wire->ReputationRank = (unsigned char)(Source->ReputationRank & 0xffu);
    Wire->GroupId = (unsigned short)(Source->GroupId & 0xffffu);
    Wire->bMarkedBy = Source->bMarkedBy;                    // src+0xa8
    // The name block, display name and Affiliation / Attitude (+0xa8..+0xc4) keep the
    // constructor's zeroes (FUN_06d9cc60); the pads +0x7d, +0xa3, +0xa6 and +0xc9 are
    // seeded by nobody, in retail or here.
    if (SinglePlayer()) {
        return true;
    }
    FmtStr NameA;
    FmtStr NameB;
    wchar_t const* DisplayName = nullptr;
    Reputation::Vibe::GetName(Wire->Common.VibeGroupId, NameA, NameB, DisplayName);
    // A non-empty display name is shipped instead of the two flattened FmtStrs, which then
    // stay null/zero.
    unsigned int DisplayLength = 0;
    if (DisplayName != nullptr) {
        while (DisplayName[DisplayLength] != L'\0') {
            ++DisplayLength;
        }
    }
    if (DisplayLength == 0) {
        Wire->NameABuffer = RpcFlattenFmtStr(NameA, Wire->NameASize);
        Wire->NameBBuffer = RpcFlattenFmtStr(NameB, Wire->NameBSize);
    } else {
        // FUN_06d97ce0: pkt+0xd8 = wcslen, pkt+0xdc = operator new((len*2)+2) + wcscpy.
        wchar_t* Copy = static_cast<wchar_t*>(
            ::operator new((DisplayLength * 2u) + 2u, std::nothrow));   // bare new in retail
        for (unsigned int i = 0; i <= DisplayLength; ++i) {
            Copy[i] = DisplayName[i];
        }
        Wire->DisplayNameLength = DisplayLength;
        Wire->DisplayName = Copy;
    }
    Reputation::Vibe::GetAffiliation(Wire->Common.VibeGroupId,
                                     Wire->Affiliation, false);
    Reputation::Vibe::AttitudeTowards(Wire->Common.VibeGroupId,
                                      Source->VibeOtherGroupId,
                                      Wire->Attitude);                   // src+0xd4
    return true;
}

static bool DeliverCreateShip(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateShip* Wire =
        reinterpret_cast<const FPayloadServerCreateShip*>(Packet->Payload());
    // caseD_4's one-time init is CShipCreateInfo's constructor (vftable .rdata:06db373c).
    static CShipCreateInfo Static;
    RpcCreateObjectHeaderRead(Static, &Wire->Common);
    Static.bUsingTradelane = Wire->bUsingTradelane;               // +0xdc
    Static.OwnerPlayer = Wire->OwnerPlayer;                   // +0xac
    Static.GroupId = (unsigned int)Wire->GroupId;      // zero-extended, +0xd8
    Static.GroupNameId = (unsigned int)Wire->GroupNameId;      // zero-extended, +0xb0
    Static.ReputationRank = (unsigned int)Wire->ReputationRank;  // zero-extended, +0xb4
    Static.Throttle = Wire->Throttle;                      // +0xd0
    std::memcpy(&Static.AngularVelocity.x, &Wire->AngVelXBits,
                sizeof(Wire->AngVelXBits));                   // +0xc4
    Static.AngularVelocity.y = Wire->AngVelY;             // +0xc8
    Static.AngularVelocity.z = Wire->AngVelZ;             // +0xcc
    std::memcpy(&Static.Velocity.x, &Wire->VelXBits,
                sizeof(Wire->VelXBits));                   // +0xb8
    Static.Velocity.y = Wire->VelY;                    // +0xbc
    Static.Velocity.z = Wire->VelZ;                    // +0xc0
    Static.bMarkedBy = Wire->bMarkedBy;                     // +0xa8
    if (!SinglePlayer()) {
        FmtStr NameA;
        FmtStr NameB;
        RpcUnflattenFmtStr(NameA, Wire->NameABuffer, Wire->NameASize);
        RpcUnflattenFmtStr(NameB, Wire->NameBBuffer, Wire->NameBSize);
        Reputation::Vibe::EnsureExists(Wire->Common.VibeGroupId);
        // Unlike CREATESOLAR, the wide name is passed on.
        Reputation::Vibe::SetName(Wire->Common.VibeGroupId, NameA, NameB,
                                  Wire->DisplayName);
        Reputation::Vibe::SetAffiliation(Wire->Common.VibeGroupId,
                                         Wire->Affiliation, false);
        Reputation::Vibe::SetAttitudeTowardsPlayer(Wire->Common.VibeGroupId,
                                                   Wire->Attitude);
    }
    Client->SendServerCreateShip(1u, Static);
    // CShipCreateInfo::clear() (06d83fa0), inlined in retail.
    Static.clear();
    return true;
}

// ===========================================================================
// ch2 0x03 FLPACKET_SERVER_CHARACTERINFO
// Sender 0x06d89890 (IClient+0x034) plus FUN_06d955c0, receiver
// IServerImpl::HandleCharacterInfo 0x06d84490 reached from caseD_3 at 06d81715.
// ===========================================================================
static bool MarshalCharacterInfo(FPacketBase* Packet, const unsigned int* Args,
                                 unsigned int ArgCount) {
    // The caller's CCharacterSet (Server.dll's IServerImpl::CharacterInfoReq local).
    const CCharacterSet* Source = reinterpret_cast<const CCharacterSet*>(Args[1]);
    // The packet ctor already built the list's dummy.
    FPayloadServerCharacterInfo* Wire =
        reinterpret_cast<FPayloadServerCharacterInfo*>(Packet->Payload());
    // Retail copies twice (into a stack list, then into the packet via FUN_06d955c0); one
    // hop here gives the same bytes.
    //
    // Retail quirk: a null source value re-appends the copy made for the previous entry, so
    // a null entry duplicates the record before it on the wire; the port converts the
    // previous source again. Before any record retail's copy pointer is uninitialised; the
    // port treats it as null, which FUN_06d955c0 skips. (Retail then double-frees the shared
    // copy from its stack list; the port has no stack list.)
    const CCharacterSet::CharacterInfo* Previous = nullptr;
    for (const MetaNode<CCharacterSet::CharacterInfo>* Node = Source->Characters.get_head();
         Node != nullptr; Node = Node->Next) {
        const CCharacterSet::CharacterInfo* Record = Node->Data;
        if (Record == nullptr) {
            Record = Previous;
            if (Record == nullptr) {
                continue;
            }
        }
        // operator new(0x27c); the null test skips only the constructor, the conversion
        // runs on the result either way.
        FRpcCharacterInfoWireRecord* Copy = new (std::nothrow) FRpcCharacterInfoWireRecord();
        RpcCharacterInfoRecordToWire(Copy, Record);
        RpcCharacterInfoWireListPushBack(&Wire->Records, Copy);
        Previous = Record;
    }
    Wire->SaveFailureCount = Source->SaveFailureCount;
    return true;
}

namespace {

// HandleCharacterInfo's own stack std::list<record*>: a bare operator new(0xc) dummy linked
// to itself; torn down after the slot call node by node (FUN_06d901f0), then the dummy.
// Nodes only: each record was freed inside the loop right after its conversion. Retail's
// unwind covers the list too.
struct FCharacterInfoLocalList {
    FRpcCharacterInfoWireList List;

    FCharacterInfoLocalList() {
        List.Allocator = 0;          // retail stores an uninitialised stack byte here
        List.Pad_0x01[0] = 0;
        List.Pad_0x01[1] = 0;
        List.Pad_0x01[2] = 0;
        FRpcCharacterInfoWireNode* Dummy = static_cast<FRpcCharacterInfoWireNode*>(
            ::operator new(sizeof(FRpcCharacterInfoWireNode)));
        Dummy->Next = Dummy;
        Dummy->Prev = Dummy;
        Dummy->Value = nullptr;      // the dummy's value is never read
        List.Head = Dummy;
        List.Size = 0;
    }

    ~FCharacterInfoLocalList() {
        FRpcCharacterInfoWireNode* Node = List.Head->Next;
        while (Node != List.Head) {
            FRpcCharacterInfoWireNode* Erased = Node;
            Node = Node->Next;
            Erased->Prev->Next = Erased->Next;                       // FUN_06d901f0
            Erased->Next->Prev = Erased->Prev;
            ::operator delete(Erased);
            --List.Size;
        }
        ::operator delete(List.Head);
        List.Head = nullptr;
        List.Size = 0;
    }

private:
    FCharacterInfoLocalList(const FCharacterInfoLocalList&);
    FCharacterInfoLocalList& operator=(const FCharacterInfoLocalList&);
};

}  // namespace

// IServerImpl::HandleCharacterInfo (0x06d84490); rpclocal.cpp's exported member forwards
// here. Arg is slot 13's first argument (caseD_3 passes 1). Retail dereferences the packet
// untested, unlike HandleDamageObject.
__declspec(dllexport) bool RpcHandleCharacterInfo(const FPacketBase* Packet, IClient* Client, unsigned int Arg) {
    const FPayloadServerCharacterInfo* Wire =
        reinterpret_cast<const FPayloadServerCharacterInfo*>(Packet->Payload());
    // The local CCharacterSet: MetaList(bUseHeap = false) with the
    // OwnerList<CCharacterSet::CharacterInfo> vtable; its destructor frees every record and
    // node after the client has consumed it.
    CCharacterSet Set;

    // FUN_06d94460 deep-copies the packet's records into the stack list; everything below
    // reads the copy, not the packet.
    FCharacterInfoLocalList Local;
    RpcCharacterInfoWireListCopy(&Local.List, &Wire->Records);

    for (const FRpcCharacterInfoWireNode* Node = Local.List.Head->Next;
         Node != Local.List.Head; Node = Node->Next) {
        // Used untested (FUN_06d94460 never appends a null value).
        const FRpcCharacterInfoWireRecord* Source = Node->Value;
        // operator new(0x2dc): null skips the constructor and the conversion, and the node
        // is still appended with a null value.
        CCharacterSet::CharacterInfo* Record =
            new (std::nothrow) CCharacterSet::CharacterInfo();
        if (Record != nullptr) {
            RpcCharacterInfoRecordFromWire(Record, Source);
        }
        // MetaList::append, inlined in retail.
        Set.Characters.append(Record);
        // The copied wire record is torn down on the spot; the node keeps the pointer.
        if (Node->Value != nullptr) {
            RpcCharacterInfoWireRecordFree(Node->Value);
        }
    }
    Set.SaveFailureCount = Wire->SaveFailureCount;
    Client->SendServerCharacterInfo(Arg, Set);
    return true;
}

// HandlePacket caseD_3: HandleCharacterInfo(packet, DAT_06dbbcd8, 1).
static bool DeliverCharacterInfo(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    return RpcHandleCharacterInfo(Packet, Client, 1u);
}

// ===========================================================================
// The table. Order follows the opcode.
//
// No payload here stores a caller-owned pointer, so all these types queue and are drained by
// IServerImpl::Update, as in retail.
// ===========================================================================
const FRpcPayloadCodec GChannel2Codecs[] = {
    { 2, 0x01, "FLPACKET_SERVER_CONNECTRESPONSE",              0x04,
      MarshalConnectResponse,        DeliverConnectResponse },
    { 2, 0x02, "FLPACKET_SERVER_LOGINRESPONSE",                0x04,
      MarshalCh2ByteByRef,           DeliverLoginResponse },
    { 2, 0x03, "FLPACKET_SERVER_CHARACTERINFO",               0x10,
      MarshalCharacterInfo,          DeliverCharacterInfo },
    { 2, 0x04, "FLPACKET_SERVER_CREATESHIP",                  0xcc,
      MarshalCreateShip,             DeliverCreateShip },
    { 2, 0x05, "FLPACKET_SERVER_DAMAGEOBJECT",                 (unsigned short)sizeof(FPayloadServerDamageObject),
      MarshalDamageObject,           DeliverDamageObject },
    { 2, 0x06, "FLPACKET_SERVER_DESTROYOBJECT",                0x08,
      MarshalDestroyObject,          DeliverDestroyObject },
    { 2, 0x07, "FLPACKET_SERVER_LAUNCH",                       0x28,
      MarshalLaunch,                 DeliverLaunch },
    { 2, 0x08, "FLPACKET_SERVER_CHARSELECTVERIFIED",           0x10,
      MarshalCharSelectVerified,     DeliverCharSelectVerified },
    { 2, 0x0a, "FLPACKET_SERVER_ACTIVATEOBJECT",               0x0c,
      MarshalActivateObject,         DeliverActivateObject },
    { 2, 0x0b, "FLPACKET_SERVER_LAND",                         0x0c,
      MarshalThreeDwordsByRef,       DeliverLand },
    { 2, 0x0d, "FLPACKET_SERVER_SETSTARTROOM",                 0x08,
      MarshalCh2DwordPair,           DeliverSetStartRoom },
    { 2, 0x0e, "FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST",  0x04,
      MarshalCh2Dword,               DeliverGfCompleteNewsBroadcastList },
    { 2, 0x0f, "FLPACKET_SERVER_GFCOMPLETECHARLIST",           0x04,
      MarshalCh2Dword,               DeliverGfCompleteCharList },
    { 2, 0x10, "FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST",0x04,
      MarshalCh2Dword,               DeliverGfCompleteMissionComputerList },
    { 2, 0x11, "FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST", 0x04,
      MarshalCh2Dword,               DeliverGfCompleteScriptBehaviorList },
    { 2, 0x13, "FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST",  0x04,
      MarshalCh2Dword,               DeliverGfCompleteAmbientScriptList },
    { 2, 0x14, "FLPACKET_SERVER_GFDESTROYNEWSBROADCAST",       0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyNewsBroadcast },
    { 2, 0x15, "FLPACKET_SERVER_GFDESTROYCHARACTER",           0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyCharacter },
    { 2, 0x16, "FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER",     0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyMissionComputer },
    { 2, 0x17, "FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR",      0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyScriptBehavior },
    { 2, 0x19, "FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT",       0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyAmbientScript },
    { 2, 0x1a, "FLPACKET_SERVER_GFSCRIPTBEHAVIOR",             0x08,
      MarshalCh2Blob,                DeliverGfScriptBehavior },
    { 2, 0x1d, "FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER",      0x08,
      MarshalCh2Blob,                DeliverGfUpdateMissionComputer },
    { 2, 0x1e, "FLPACKET_SERVER_GFUPDATENEWSBROADCAST",        0x08,
      MarshalCh2Blob,                DeliverGfUpdateNewsBroadcast },
    { 2, 0x1f, "FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT",        0x08,
      MarshalCh2BlobReversed,        DeliverGfUpdateAmbientScript },
    { 2, 0x20, "FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE",    0x08,
      MarshalCh2Blob,                DeliverGfMissionVendorAcceptance },
    { 2, 0x21, "FLPACKET_SERVER_SYSTEM_SWITCH_OUT",            0x08,
      MarshalDwordPairByRef,         DeliverSystemSwitchOut },
    { 2, 0x22, "FLPACKET_SERVER_SYSTEM_SWITCH_IN",             0x24,
      MarshalSystemSwitchIn,         DeliverSystemSwitchIn },
    { 2, 0x23, "FLPACKET_SERVER_SETSHIPARCH",                  0x04,
      MarshalCh2Dword,               DeliverSetShipArch },
    { 2, 0x24, "FLPACKET_SERVER_SETEQUIPMENT",                 0x04,
      MarshalSetEquipment,           DeliverSetEquipment },
    { 2, 0x25, "FLPACKET_SERVER_SETCARGO",                     0x04,
      MarshalSetCargo,               DeliverSetCargo },
    { 2, 0x26, "FLPACKET_SERVER_GFUPDATECHAR",                 0x08,
      MarshalCh2Blob,                DeliverGfUpdateChar },
    { 2, 0x27, "FLPACKET_SERVER_REQUESTCREATESHIPRESP",        0x08,
      MarshalRequestCreateShipResp,  DeliverRequestCreateShipResp },
    { 2, 0x28, "FLPACKET_SERVER_CREATELOOT",                   0x14,
      MarshalCreateLoot,             DeliverCreateLoot },
    { 2, 0x29, "FLPACKET_SERVER_SETREPUTATION",                0x48,
      MarshalSetReputation,          DeliverSetReputation },
    { 2, 0x2a, "FLPACKET_SERVER_ADJUSTATTITUDE",               0x08,
      MarshalAdjustAttitude,         DeliverAdjustAttitude },
    { 2, 0x2b, "FLPACKET_SERVER_SETGROUPFEELINGS",             0x14,
      MarshalSetGroupFeelings,       DeliverSetGroupFeelings },
    { 2, 0x2c, "FLPACKET_SERVER_CREATEMINE",                   0x48,
      MarshalCreateMine,             DeliverCreateMine },
    { 2, 0x2d, "FLPACKET_SERVER_CREATECOUNTER",                0x48,
      MarshalCreateCounter,          DeliverCreateCounter },
    { 2, 0x2e, "FLPACKET_SERVER_SETADDITEM",                   0x20,
      MarshalSetAddItem,             DeliverSetAddItem },
    { 2, 0x2f, "FLPACKET_SERVER_SETREMOVEITEM",                0x0c,
      MarshalSetRemoveItem,          DeliverSetRemoveItem },
    { 2, 0x30, "FLPACKET_SERVER_SETCASH",                      0x04,
      MarshalCh2Dword,               DeliverSetCash },
    { 2, 0x31, "FLPACKET_SERVER_EXPLODEASTEROIDMINE",          0x10,
      MarshalFourDwordsByRef,        DeliverExplodeAsteroidMine },
    { 2, 0x32, "FLPACKET_SERVER_REQUESTSPACESCRIPT",           0x1c,
      MarshalRequestSpaceScript,     DeliverRequestSpaceScript },
    { 2, 0x33, "FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE",     0x0c,
      MarshalSetMissionObjectiveState, DeliverSetMissionObjectiveState },
    { 2, 0x34, "FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE",      0x10,
      MarshalReplaceMissionObjective, DeliverReplaceMissionObjective },
    { 2, 0x35, "FLPACKET_SERVER_SETMISSIONOBJECTIVES",         0x28,
      MarshalSetMissionObjectives,   DeliverSetMissionObjectives },
    { 2, 0x37, "FLPACKET_SERVER_CREATEGUIDED",                 0x38,
      MarshalCreateGuided,           DeliverCreateGuided },
    { 2, 0x38, "FLPACKET_SERVER_ITEMTRACTORED",                0x0c,
      MarshalItemTractored,          DeliverItemTractored },
    { 2, 0x39, "FLPACKET_SERVER_SCANNOTIFY",                   0x08,
      MarshalCh2DwordPair,           DeliverScanNotify },
    { 2, 0x3c, "FLPACKET_SERVER_REPAIROBJECT",                 0x0c,
      MarshalRepairObject,           DeliverRepairObject },
    { 2, 0x3d, "FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE",      0x0c,
      MarshalRemoteObjectCargoUpdate, DeliverRemoteObjectCargoUpdate },
    { 2, 0x3e, "FLPACKET_SERVER_SETNUMKILLS",                  0x04,
      MarshalCh2Dword,               DeliverSetNumKills },
    { 2, 0x3f, "FLPACKET_SERVER_SETMISSIONSUCCESSES",          0x04,
      MarshalCh2Dword,               DeliverSetMissionSuccesses },
    { 2, 0x40, "FLPACKET_SERVER_SETMISSIONFAILURES",           0x04,
      MarshalCh2Dword,               DeliverSetMissionFailures },
    { 2, 0x41, "FLPACKET_SERVER_BURNFUSE",                     0x1c,
      MarshalBurnFuse,               DeliverBurnFuse },
    { 2, 0x42, "FLPACKET_SERVER_CREATESOLAR",                 0x98,
      MarshalCreateSolar,            DeliverCreateSolar },
    { 2, 0x43, "FLPACKET_SERVER_SET_STORY_CUE",                0x04,
      MarshalCh2DwordByRef,          DeliverSetStoryCue },
    { 2, 0x44, "FLPACKET_SERVER_REQUEST_RETURNED",             0x08,
      MarshalRequestReturned,        DeliverRequestReturned },
    { 2, 0x45, "FLPACKET_SERVER_SET_MISSION_MESSAGE",          0x0c,
      MarshalSetMissionMessage,      DeliverSetMissionMessage },
    { 2, 0x46, "FLPACKET_SERVER_MARKOBJ",                      0x08,
      MarshalMarkObj,                DeliverMarkObj },
    { 2, 0x47, "FLPACKET_SERVER_CFGINTERFACENOTIFICATION",     0x0c,
      MarshalCfgInterfaceNotification, DeliverCfgInterfaceNotification },
    { 2, 0x48, "FLPACKET_SERVER_SETCOLLISIONGROUPS",           0x0c,
      MarshalSetCollisionGroups,     DeliverSetCollisionGroups },
    { 2, 0x49, "FLPACKET_SERVER_SETHULLSTATUS",                0x04,
      MarshalSetHullStatus,          DeliverSetHullStatus },
    { 2, 0x4a, "FLPACKET_SERVER_SETGUIDEDTARGET",              0x0c,
      MarshalSetGuidedTarget,        DeliverSetGuidedTarget },
    { 2, 0x4b, "FLPACKET_SERVER_SET_CAMERA",                   0x24,
      MarshalSetCamera,              DeliverSetCamera },
    { 2, 0x4c, "FLPACKET_SERVER_REVERT_CAMERA",                0x00,
      MarshalCh2Empty,               DeliverRevertCamera },
    { 2, 0x4d, "FLPACKET_SERVER_LOADHINT",                     0x0c,
      MarshalLoadHint,               DeliverLoadHint },
    { 2, 0x4e, "FLPACKET_SERVER_SETDIRECTIVE",                 0x94,
      MarshalSetDirective,           DeliverSetDirective },
    { 2, 0x4f, "FLPACKET_SERVER_SENDCOMM",                     0x58,
      MarshalSendComm,               DeliverSendComm },
    { 2, 0x51, "FLPACKET_SERVER_USE_ITEM",                     0x08,
      MarshalUseItem,                DeliverUseItem },
    { 2, 0x52, "FLPACKET_SERVER_PLAYERLIST",                   0x10,
      MarshalPlayerList,             DeliverPlayerList },
    { 2, 0x53, "FLPACKET_SERVER_FORMATION_UPDATE",             0x10,
      MarshalFormationUpdate,        DeliverFormationUpdate },
    { 2, 0x54, "FLPACKET_SERVER_MISCOBJUPDATE",                (unsigned short)sizeof(FPayloadServerMiscObjUpdate),
      MarshalMiscObjUpdate,          DeliverMiscObjUpdate },
    { 2, 0x55, "FLPACKET_SERVER_OBJECTCARGOUPDATE",            0x14,
      MarshalObjectCargoUpdate,      DeliverObjectCargoUpdate },
    { 2, 0x56, "FLPACKET_SERVER_SENDNNMESSAGE",                0x04,
      MarshalCh2Dword,               DeliverSendNnMessage },
    { 2, 0x57, "FLPACKET_SERVER_SET_MUSIC",                    0x18,
      MarshalSetMusic,               DeliverSetMusic },
    { 2, 0x58, "FLPACKET_SERVER_CANCEL_MUSIC",                 0x00,
      MarshalCh2Empty,               DeliverCancelMusic },
    { 2, 0x59, "FLPACKET_SERVER_PLAY_SOUND_EFFECT",            0x04,
      MarshalCh2Dword,               DeliverPlaySoundEffect },
    { 2, 0x5a, "FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY",      0x04,
      MarshalCh2Byte,                DeliverGfMissionVendorWhyEmpty },
    { 2, 0x5b, "FLPACKET_SERVER_MISSIONSAVEA",                 0x04,
      MarshalCh2Dword,               DeliverMissionSaveA },
};

const unsigned int GChannel2CodecCount =
    (unsigned int)(sizeof(GChannel2Codecs) / sizeof(GChannel2Codecs[0]));
