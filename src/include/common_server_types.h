// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_pub.h"
#include "common_physys.h"   // Quaternion (SSPObjUpdateInfo::Orientation)

#include <cfloat>
#include <cstring>
#include <list>
#include <vector>

// ---------------------------------------------------------------------------
// Legacy representation retained by unmigrated records and rpclocal's internal
// packet storage: allocator padding followed by three pointers. New public
// owning payloads, including XFireWeaponInfo and XTractorObjects, use std::vector.
// ---------------------------------------------------------------------------
template<typename T>
struct FRetailVector {
    // +0x00: the VC6 vector's allocator<T> member, an empty class: one byte, then three
    // alignment bytes. Retail's FIREWEAPON / TRACTOROBJECTS packet ctors (rpclocal) store
    // this one byte; nothing reads it.
    unsigned char AllocPad;
    unsigned char Pad_0x01[3]; // +0x01
    T* Begin;                // +0x04  _First
    T* End;                  // +0x08  _Last
    T* Cap;                  // +0x0c  _End

    unsigned int Count() const {
        return Begin != nullptr ? (unsigned int)(End - Begin) : 0u;
    }
};
static_assert(sizeof(FRetailVector<unsigned int>) == 0x10, "FRetailVector: VC6 vector, 0x10 bytes");

// ---------------------------------------------------------------------------
// SStartupInfo — 0x214 bytes. Layout from Server.dll IServerImpl::Startup (0x64b70: PlayerDB
// ::init, LoadLibrary(ModulePath), DataConfig::startup(IniPath), IClient slot 10) and
// rpclocal's IServerImpl::Startup (0x06d8f1e0), which strncpy's the first 0x104 bytes.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct SStartupInfo {
    // +0x000: path of the module that owns GetClientInterface (the caller's own
    //         module; rpclocal overwrites this with its own path before forwarding).
    char ModulePath[0x104];

    // +0x104: path of freelancer.ini (DataConfig::startup / initial_world reader).
    char IniPath[0x104];

    // +0x208: max player count (PlayerDB::init, IClient slot 10 arg 0).
    unsigned int MaxPlayers;

    // +0x20c: the DirectPlay host port. Forwarded to IClient slot 10; RemoteClient passes it
    //         to CDPServer::BeginHosting, which adds it as the address's "port" component
    //         when non-zero. FLServer's -P switch sets it.
    unsigned int Port;

    // +0x210: players may damage other players. Stored in GbAllowPvp by FUN_06cfffd0; the
    //         damage paths drop player-on-player hits while it is 0. FLServer: "Allow
    //         Players to Harm Other Players"; single player sets 1.
    unsigned char bAllowPvp;

    // +0x211: new accounts may be created (PlayerDB::init -> PlayerDB+0x0c, which gates
    //         account creation at login). FLServer: "Allow New Players"; single player 1.
    unsigned char bAllowNewPlayers;

    // +0x212..0x213: padding
    unsigned char Pad_0x212[2];
};
#pragma pack(pop)

static_assert(sizeof(SStartupInfo) == 0x214, "SStartupInfo size mismatch");

// SLoginInfo — passed to IServer::Login and PlayerDB::login; 0xC8 bytes. The flag at +0xC0
// is checked in load_user_data before create_account.
#pragma pack(push, 1)
struct SLoginInfo {
    // rpclocal's IServerImpl::Login (0x06d8cc70) copies a wide string from +0x00 and one
    // from +0xA0, then reads +0xC0; the receiver caps them at 0x28 and 0x10 characters.
    wchar_t AccountName[40];    // +0x00: account/character name (wide), 0x50 bytes
    // +0x50: not written by rpclocal's Login sender (0x06d8cc70, which copies only +0x00,
    // +0xA0 and +0xC0); no reader identified.
    unsigned char Field_0x50[0x50];
    // +0xA0: the second wide string; only its 16-character width is confirmed (the name is
    // the port's).
    wchar_t Password[16];     // +0xA0: second wide string, 0x20 bytes
    unsigned int CreateFlag;    // +0xC0: nonzero = allow account creation
    unsigned int Pad_C4;        // +0xC4: padding
};
#pragma pack(pop)
static_assert(sizeof(SLoginInfo) == 0xC8, "SLoginInfo size mismatch");

// ===========================================================================
// IServer payload structures.
//
// Each layout is read off both the rpclocal.dll serializer (writes the fields into a
// CDAPacket from +0x20 on) and the Server.dll consumer. A static_assert is present only
// where the total size is proven. Fields whose semantics are not proven keep a Field_0xNN
// name and a TODO; their primitive type is proven.
// ===========================================================================

// XFireWeaponInfo — retail 0x20 bytes. Slot 0.
// Retail +0x10 is a VC6 std::vector<unsigned short> owned by the record; the port holds a
// modern std::vector (no retail ABI here), so no size assert. Retail inlines the special
// members everywhere: the ctor sets ObjectId 0, TargetPos to FLT_MAX x3 and an empty
// vector; copy is a deep element copy; the dtor frees the vector -- what the implicit
// members do.
struct XFireWeaponInfo {
    XFireWeaponInfo()
        : ObjectId(0u)
    {
        TargetPos.x = FLT_MAX;
        TargetPos.y = FLT_MAX;
        TargetPos.z = FLT_MAX;
    }

    unsigned int ObjectId;                     // +0x00
    Vector TargetPos;                          // +0x04  3 floats (rpclocal FUN_06d8f510)
    std::vector<unsigned short> Hardpoints;    // +0x10  element stride 2 ((last-first) >> 1)
};

// XActivateEquip — 8 bytes. Slot 1.
struct XActivateEquip {
    unsigned int ObjectId;   // +0x00
    unsigned short EquipId;  // +0x04
    bool bActivate;          // +0x06
    unsigned char Pad_0x07;  // +0x07
};
static_assert(sizeof(XActivateEquip) == 0x8, "XActivateEquip sizeof 0x8");

// XActivateCruise — 8 bytes. Slot 2.
struct XActivateCruise {
    unsigned int ObjectId;      // +0x00
    bool bActivate;             // +0x04
    unsigned char bDisruptCruise; // +0x05  2nd arg of IObjAffect::slot_50 (toggle_cruise)
    unsigned char Pad_0x06[2];  // +0x06
};
static_assert(sizeof(XActivateCruise) == 0x8, "XActivateCruise sizeof 0x8");

// XActivateThrusters — 8 bytes. Slot 3.
struct XActivateThrusters {
    unsigned int ObjectId;      // +0x00
    bool bActivate;             // +0x04
    unsigned char Pad_0x05[3];  // +0x05
};
static_assert(sizeof(XActivateThrusters) == 0x8, "XActivateThrusters sizeof 0x8");

// XSetTarget — 0x10 bytes. Slot 4.
struct XSetTarget {
    unsigned int ObjectId;          // +0x00
    unsigned short SubObjId;        // +0x04  compared against the SUBOBJ_ID_NONE export
    unsigned short Pad_0x06;        // +0x06
    unsigned int TargetObjectId;    // +0x08
    unsigned short TargetSubObjId;  // +0x0c
    unsigned short Pad_0x0e;        // +0x0e
};
static_assert(sizeof(XSetTarget) == 0x10, "XSetTarget sizeof 0x10");

// XTractorObjects — IServer slot 5. Retail's VC6 vector at +0x08 owns its ids; the port
// uses a modern std::vector.
struct XTractorObjects {
    unsigned int ObjectId;                  // +0x00
    unsigned short TractorEquipId;          // +0x04
    unsigned short Pad_0x06;                // +0x06
    std::vector<unsigned int> TargetIds;
};

// XGoTradelane — 0x10 bytes. Slot 6.
struct XGoTradelane {
    unsigned int ObjectId;        // +0x00  compared against the player's ship id (player+0x3e4)
    unsigned int TradelaneRing1;  // +0x04
    unsigned int TradelaneRing2;  // +0x08
    // One byte, Server.dll FUN_06d0df40's 5th argument: copied unread into the type-4 message
    // it hands the other ships' IBehaviorManager and into the IClient +0x18 notification to
    // the observing players. Nothing in Server.dll tests it; the name is the port's.
    bool bMidLane;              // +0x0c
    unsigned char Pad_0x0d[3];    // +0x0d
};
static_assert(sizeof(XGoTradelane) == 0x10, "XGoTradelane sizeof 0x10");

// XJettisonCargo — 0x0c bytes. Slot 8.
struct XJettisonCargo {
    unsigned int ObjectId;    // +0x00
    unsigned short ItemId;    // +0x04
    unsigned short Pad_0x06;  // +0x06
    unsigned int Count;       // +0x08
};
static_assert(sizeof(XJettisonCargo) == 0xc, "XJettisonCargo sizeof 0xc");

// SSPUseItem — 8 bytes. Slot 56.
struct SSPUseItem {
    unsigned int ObjectId;   // +0x00
    unsigned short ItemId;   // +0x04
    unsigned short Count;    // +0x06
};
static_assert(sizeof(SSPUseItem) == 0x8, "SSPUseItem sizeof 0x8");

// SGFGoodBuyInfo — 0x10 bytes (rpclocal 0x06d8d5a0, Server 0x06d17bb0, ClientMarketRequestBuy).
struct SGFGoodBuyInfo {
    unsigned int BaseId;   // +0x00
    unsigned int Null04;   // +0x04  always 0 from client
    unsigned int GoodId;   // +0x08
    unsigned int Count;    // +0x0c
};
static_assert(sizeof(SGFGoodBuyInfo) == 0x10, "SGFGoodBuyInfo sizeof 0x10");

// SGFGoodSellInfo — size unproven. Three dwords are serialized (rpclocal 0x06d8d600 /
// Server 0x06d17bf0 / ClientMarketRequestSell); no reader touches past +0x08.
struct SGFGoodSellInfo {
    unsigned int BaseId;   // +0x00
    unsigned int GoodId;   // +0x04
    unsigned int Count;    // +0x08
};

// SGFGoodVaporizedInfo — as SGFGoodSellInfo (rpclocal 0x06d8d650 / Server 0x06d17c30).
// Size not proven.
// Server.dll copies the three dwords unread, with the session id, into controller message
// 0x1e (GFGoodVaporized); no reader of them identified.
struct SGFGoodVaporizedInfo {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
};

// SSPObjUpdateInfo — 0x30 bytes. Slot 52.
struct SSPObjUpdateInfo {
    unsigned int ObjectId;       // +0x00
    Quaternion Orientation;      // +0x04  4 floats (rpclocal FUN_06d8f4f0)
    Vector Position;             // +0x14  3 floats (rpclocal FUN_06d8f510)
    double Timestamp;            // +0x20  read as an 8-byte double, narrowed to float on the wire
    unsigned int ThrottleBits;   // +0x28  throttle float bits (serialized to packet+0x48)
    unsigned char State;         // +0x2c  ship state/behavior byte (serialized to packet+0x20)
    unsigned char Pad_0x2d[3];   // +0x2d
};
static_assert(sizeof(SSPObjUpdateInfo) == 0x30, "SSPObjUpdateInfo sizeof 0x30");

// SSPMunitionCollisionInfo — 0x1c bytes. Slot 53. Semantics from the consumer, Server.dll
// StarSystem::munition_collision (FUN_06d0d500); the hit position stays raw float bits
// because rpclocal's wire marshalling copies dwords.
struct SSPMunitionCollisionInfo {
    unsigned int ProjectileArchId; // +0x00  munition archetype id (Archetype::GetEquipment)
    unsigned int AttackerId;       // +0x04  shooter object id (FMunitionImpact::ShooterId, DamageList inflictor)
    unsigned int ObjectId;         // +0x08  Server.dll looks the object up by THIS field
    unsigned short SubObjId;       // +0x0c  sub-object hit (FMunitionImpact +0x08)
    unsigned short Pad_0x0e;       // +0x0e
    unsigned int HitPosX;          // +0x10  hit position x (float bits, FMunitionImpact +0x0c)
    unsigned int HitPosY;          // +0x14  hit position y (float bits)
    unsigned int HitPosZ;          // +0x18  hit position z (float bits)
};
static_assert(sizeof(SSPMunitionCollisionInfo) == 0x1c, "SSPMunitionCollisionInfo sizeof 0x1c");

// SSPObjCollisionInfo — 0x14 bytes. Slot 55.
struct SSPObjCollisionInfo {
    unsigned int TargetObjectId;    // +0x00
    unsigned short TargetSubObjId;  // +0x04
    unsigned short Pad_0x06;        // +0x06
    unsigned int ObjectId;          // +0x08  Server.dll looks the object up by THIS field
    unsigned short SubObjId;        // +0x0c
    unsigned short Pad_0x0e;        // +0x0e
    unsigned int DamageBits;        // +0x10  collision damage float bits (FCollisionDamage::Damage; packet+0x30)
};
static_assert(sizeof(SSPObjCollisionInfo) == 0x14, "SSPObjCollisionInfo sizeof 0x14");

// SSPBadLandsObjCollisionInfo — 0x0c bytes. Slot 54.
struct SSPBadLandsObjCollisionInfo {
    // +0x00: StarSystem::badlands_collision (FUN_06d0d6e0) passes it as the killer of
    // destroy(1, Id) on the object; the name is the port's.
    unsigned int BadLandsObjId;
    unsigned int ObjectId;      // +0x04  the object destroyed (find_object)
    unsigned short SubObjId;  // +0x08  serialized by rpclocal; Server.dll does not read it
    unsigned short Pad_0x0a;    // +0x0a
};
static_assert(sizeof(SSPBadLandsObjCollisionInfo) == 0xc, "SSPBadLandsObjCollisionInfo sizeof 0xc");

// CollisionGroupDesc — 8 bytes ({ushort Id; ushort Pad; float Health}); ReqCollisionGroups
// takes a std::list of them. Defined once, in common_archetype.h.
struct CollisionGroupDesc;

// The status byte PlayerDB::login returns and IServerImpl::Login sends in
// FLPACKET_SERVER_LOGINRESPONSE (SendServerLoginResponse's Status); the client's LoginMgr
// keeps it. The others the login path produces (2, 6) are not named.
enum ELoginStatus : unsigned char
{
    kLoginBanned = 0,           // PlayerData::login_account: the account is banned (FUN_06d48e80)
    kLoginAlreadyLoggedIn = 1,  // PlayerDB::login: the account name is already in a slot
    kLoginAccepted = 3,
    kLoginAccountLocked = 7,    // PlayerData::login_account: CAccount::m_bLocked
};

// InvincibilityReason — SPRequestInvincibility's 3rd argument. rpclocal writes it as one
// byte; the ABI passes a dword. Server.dll's SPRequestInvincibility (0x11380) honours
// turning it on for 2 (the ship is in a dockable solar's dock queue: the client sends it while
// a docking is pending) and 5 (the client's script / static camera scenes) only. The other
// values are unrecovered. The fixed underlying type admits every byte value decoded from the
// wire.
enum InvincibilityReason : int {
    INVINCIBILITY_DOCKING = 2,
    INVINCIBILITY_CAMERA = 5,
};

// CommResult enum
enum class CommResult : int {
    SUCCESS = 0,
    FAILED = 1,
    CANCELLED = 2,
    TIMEOUT = 3
};

// EFLConnection enum
enum class EFLConnection : int {
    NONE = 0,
    LOCAL = 1,
    REMOTE = 2
};

// XSetManeuver — 0x0c bytes. HUD_Maneuvers::RequestManeuver fills
// {ShipFrom (watched player ship), ObjectId (target), Flag (behavior id)}.
struct XSetManeuver {
    unsigned int ShipFrom;    // +0x00  player ship id (packet+0x20)
    unsigned int ObjectId;    // +0x04  target object id (FUN_06d006c0)
    unsigned int Flag;        // +0x08  behavior/maneuver id (packet+0x28)
};
static_assert(sizeof(XSetManeuver) == 0xc, "XSetManeuver sizeof 0xc");

// ---------------------------------------------------------------------------
// CDAPacket — the RPC packet base, 0x20 bytes, from the packet constructors the rpclocal
// registry installs (e.g. FUN_06d9c860). The vtable has 4 slots { dtor, codec A, codec B,
// dump }. Both dispatchers route on +0x0c; a concrete packet's payload starts at +0x20 and
// its size is its operator new argument (0x24..0xec). Registry:
// src/rpclocal/rpc_packet_registry.cpp (protocol: docs/re/rpclocal.md).
// A class in retail (`PAV` in ?PushToServer@IServerImpl@@UAEXPAVCDAPacket@@@Z).
// ---------------------------------------------------------------------------
class CDAPacket {
public:
    void* VTable;             // +0x00  packet-class vtable (4 slots)
    unsigned int UsageCount;   // +0x04  ctor sets 1
    CDAPacket* QueueLink;      // +0x08  CDAConnection links queued packets here
    unsigned int PacketId;     // +0x0c  Channel << 16 | Opcode
    unsigned int ProcessedBytes;      // +0x10  zeroed by ctor (FLSDK: processedBytes)
    unsigned int BufferCapacity;      // +0x14  marshal buffer capacity (FLSDK: bufferCapacity)
    unsigned char* BufferCursor;      // +0x18  marshal cursor pointer (FLSDK: bufferCursor)
    unsigned int SerializationError;  // +0x1c  zeroed by ctor (FLSDK: serializationError)
    // Payload starts at +0x20 and belongs to the derived packet class.

    unsigned int GetChannel() const { return PacketId >> 16; }
    unsigned int GetOpcode() const { return PacketId & 0xfff; }
};
static_assert(sizeof(CDAPacket) == 0x20, "CDAPacket base sizeof 0x20");

// Defined in common_goods.h; EquipDescList is a class in retail.
class EquipDescList;
struct EquipDesc;

// CHARACTER_ID — a 0x200-byte NUL-terminated, case-insensitive character/account identifier
// (freelancer.exe FUN_005aa9c0 places flag bytes right after it at +0x200). Retail Common.dll
// exports exactly seven members (no dtor, copy ctor or operator>):
//   ctor 0x1030 (writes Buffer[0] = 0), is_valid 0x1040 (Buffer[0] != 0), invalidate 0x1050,
//   operator= 0xce720 (strcpy), operator< / == / != 0xce740 / 0xce760 / 0xce780 (stricmp).
// Server.dll imports operator= by name, so all seven are COMMON_API and defined out of line
// in src/Common/common_id.cpp.
struct CHARACTER_ID {
    char Buffer[0x200];

    // Deviation: retail only zeroes Buffer[0]; the port zeroes the whole buffer.
    COMMON_API CHARACTER_ID();

    COMMON_API bool is_valid() const;
    COMMON_API void invalidate();

    // Retail returns a const reference.
    COMMON_API CHARACTER_ID const& operator=(CHARACTER_ID const& other);
};

static_assert(sizeof(CHARACTER_ID) == 0x200, "CHARACTER_ID must match the retail 512-byte buffer");

COMMON_API bool operator<(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs);
COMMON_API bool operator==(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs);
COMMON_API bool operator!=(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs);

// ---------------------------------------------------------------------------
// SCreateCharacterInfo — CreateNewCharacter's first argument; size unproven.
//   +0x00  wide, NUL-terminated character name (rpclocal 0x06d8cdf0 wcsncpy's up to 0x40
//          characters; PlayerDB::create_new_character 0x06d53fd0 wcslen's it).
//   +0x30..+0x3f  four dwords, read individually by rpclocal's serializer.
// The dword read at +0x30 bounds Name to 24 characters, against rpclocal's wcsncpy count of
// 0x40 (safe only because of the terminator). TODO: the exact Name capacity is unproven, so
// there is no size assert. (The serialized payload is 0x90 bytes, the packet's size.)
// ---------------------------------------------------------------------------
struct SCreateCharacterInfo {
    wchar_t Name[24];         // +0x00 .. +0x2f
    // Server.dll FUN_06d4b790 looks each one up in NewChar::TheDB:
    unsigned int FactionId;  // [Faction] nickname id -- slot 0, faction tree +0x04
    unsigned int BaseId;  // home base id (a [Faction] `base`) -- slot 3, tree +0x40
    unsigned int PackageId;  // [Package] nickname id -- slot 1, tree +0x18
    unsigned int PilotId;  // [Pilot] nickname id -- slot 2, tree +0x2c
};

// ServerStats — 0x2c bytes (11 dwords). GetServerStats copies Server.dll's global
// GServerStats (.data:06d8f998), overriding +0x00 with the active player count and
// +0x20..+0x28 with 0. rpclocal's body is a bare RET 4. Startup zero-fills it; the
// counters below are the only other writers.
struct ServerStats {
    unsigned int PlayerCount;           // +0x00: active player count (overridden)
    unsigned int LoginSuccessCount;     // +0x04: Login, result 3
    unsigned int LogoutCount;           // +0x08: PlayerDB::logout
    unsigned int LoginFailCount;        // +0x0c: Login, other results
    // +0x10: multiplayer player-ship deaths, incremented by EqObj's destroy override
    // (FUN_06ce8080) for a player-owned object with destroy kind 1, after the 0x14
    // group-control broadcast.
    unsigned int PlayerDeathCount;
    unsigned int Field_0x14;            // +0x14: only the Startup zero-fill writes it
    unsigned int Field_0x18;            // +0x18: only the Startup zero-fill writes it
    unsigned int AbortMissionCount;     // +0x1c: AbortMission
    unsigned int Reserved1;             // +0x20: always 0
    unsigned int Reserved2;             // +0x24: always 0
    unsigned int Reserved3;             // +0x28: always 0
};
static_assert(sizeof(ServerStats) == 0x2c, "ServerStats sizeof 0x2c");

// CHAT_ID — 4-byte chat identifier, passed by value (?SubmitChat@IServerImpl@@UAEXUCHAT_ID@@...).
// Low 16 bits = player/session ID, upper bits = flags/channel info.
struct CHAT_ID {
    unsigned int Id;

    CHAT_ID() : Id(0) {}
    CHAT_ID(unsigned int InId) : Id(InId) {}
    unsigned short GetPlayerId() const { return (unsigned short)(Id & 0xFFFF); }
};

// DamageEntry is defined in common_misc.h.
