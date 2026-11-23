// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_pub.h"
#include "common_text.h"
#include "common_csys.h"
#include "common_server_types.h"
#include "common_server_controller.h"
// common_net.h: CPlayerAccount by value (PlayerDB embeds one at +0x10, ctor 0x06d50b10).
#include "common_metalist.h"
#include "common_net.h"
#include "common_exports.h"
#include "server_exports.h"
// common_cobject.h: CDeadReckonedVector complete, for CRemotePhysicsSimulation's inline
// copy ctor / operator= below.
#include "common_cobject.h"
#include "dacom.h"
#include "common_ivp_controller.h"  // IVP_Controller_Golem, CRemotePhysicsController's first base
#include <cmath>     // sqrt (CRemotePhysicsSimulation::CalcPositionError)
#include <new>
#include <string>
#include <cstring>   // _strnicmp (ci_char_traits)
#include <cwchar>    // _wcsnicmp (ci_wchar_traits)
#include <vector>
#include <map>
#include <list>
#include <set>

// ===========================================================================
// Forward declarations and type definitions for Server.dll export signatures
// These types must match retail mangled names exactly.
// ===========================================================================

// Global types used in Server.dll exports
struct FmtStr;
struct BaseHint;
struct AmbientScriptDescription;
struct CharacterBehaviorDescription; // common_iface.h (pub::GF::CharacterBehaviorCreate)
struct NewsBroadcastDescription; // common_goods.h (pub::GF::NewsBroadcastCreate)
struct MissionVendorOfferDescription; // common_goods.h (pub::GF::MissionVendorOfferCreate)
struct EquipDescVector; // common_goods.h (pub::GetLoadout / pub::Player::SetShipAndLoadout)
class Transform;
// AmbientScriptDescription / AmbientScriptProperties are defined in common_goods.h.

// MissionMessageType — used in Player::DisplayMissionMessage
enum MissionMessageType {
    MISSION_MSG_TYPE_0 = 0,
    MISSION_MSG_TYPE_1 = 1,
    MISSION_MSG_TYPE_2 = 2,
    MISSION_MSG_TYPE_3 = 3,
};

// DOCK_HOST_RESPONSE — used in SpaceObj::Dock
enum DOCK_HOST_RESPONSE {
    DOCK_HOST_RESPONSE_0 = 0,
    DOCK_HOST_RESPONSE_1 = 1,
    DOCK_HOST_RESPONSE_2 = 2,
    DOCK_HOST_RESPONSE_3 = 3,
    // Content.dll passes 4 after a granted dock (FUN_06f6e9d0) and 5 when a ship's docking
    // is cancelled (FUN_06f6e810).
    DOCK_HOST_RESPONSE_4 = 4,
    DOCK_HOST_RESPONSE_5 = 5,
};

// EQInfo and TerminalInfo are declared in server_cobject.h under pub::SpaceObj namespace

// ZoneShape — used in Zone::GetShape
enum ZoneShape {
    ZONE_SHAPE_SPHERE = 0,
    ZONE_SHAPE_BOX = 1,
    ZONE_SHAPE_CYLINDER = 2,
    ZONE_SHAPE_RING = 3,
};

// MVEmptyReason — used in GF::ReportWhyMissionVendorEmpty
enum MVEmptyReason {
    MV_EMPTY_REASON_0 = 0,
    MV_EMPTY_REASON_1 = 1,
    MV_EMPTY_REASON_2 = 2,
};

// Defined in common_weighted_vector.h.
template <typename T>
class weighted_vector;

// ---------------------------------------------------------------------------
// pub::CargoEnumerator — the sink pub::SpaceObj::EnumerateCargo (.text:06cf95f0) and
// pub::Player::EnumerateCargo (.text:06d54d20) call through slot 0 with (&GoodId, Count,
// Id): the good id by reference to a stack local. Not the same type as
// IObjInspect::CargoEnumerator (common_iface.h), whose order is (Id, ArchId, Count).
// ---------------------------------------------------------------------------
namespace pub {
struct CargoEnumerator {
    virtual void notify_cargo(unsigned int const& GoodId, int Count, unsigned short Id) = 0;
};
} // namespace pub

struct Costume; // common_reputation.h
struct CharacterDescription; // common_goods.h

// pub::Reputation::Enumerator
namespace pub {
namespace Reputation {
// One virtual in slot 0: retail's adapter thunk (0x06d5f940) dereferences the group id and
// tail-calls the sink's slot 0 with it by value.
struct Enumerator {
    virtual char Execute(unsigned int GroupId) = 0;
};

SERVER_API int __cdecl EnumerateGroups(Enumerator& GroupEnumerator);

// pubGlobalReputation::SetRank; Content passes the table rank as float.
SERVER_API int __cdecl SetRank(int const& VibeHandle, float Rank);

// Server.dll 0x7f0f0.
SERVER_API int __cdecl Alloc(int& VibeHandle, FmtStr const& Name, FmtStr const& Nickname);

// Server.dll 0x7f730.
SERVER_API int __cdecl GetName(int const& VibeHandle, FmtStr& OutName, FmtStr& OutNickname);

// Server.dll 0x7f400.
SERVER_API int __cdecl GetAffiliation(int const& VibeHandle, unsigned int& OutAffiliation);

// Server.dll 0x7f430.
SERVER_API int __cdecl SetAffiliation(int const& VibeHandle, unsigned int const& Affiliation);

SERVER_API int __cdecl SetAttitude(int const& VibeA, int const& VibeB, float Attitude);

// Server.dll 0x7f6a0.
SERVER_API int __cdecl GetReputationGroup(unsigned int& OutId, char const* Name);

// Server.dll 0x7f810.
SERVER_API int __cdecl GetGroupName(unsigned int const& GroupId, unsigned int& OutNameId);

// Server.dll 0x7f840.
SERVER_API int __cdecl GetShortGroupName(unsigned int const& GroupId, unsigned int& OutNameId);

// Server.dll 0x7f8a0.
SERVER_API int __cdecl GetReputation(int& OutRep, char const* Name);

// Server.dll 0x7f470 (vibe overload).
SERVER_API int __cdecl SetReputation(int const& VibeHandle, unsigned int const& GroupId,
                                     float Value);

// Server.dll 0x7f590.
SERVER_API int __cdecl GetRank(int const& VibeHandle, float& OutRank);

// Server.dll 0x7f870.
SERVER_API int __cdecl GetGroupFeelingsTowards(int const& VibeHandle, unsigned int const& GroupId,
                                               float& OutFeeling);

// Server.dll 0x7f4b0 (vibe/group overload): OutValue = 0, then Vibe::GetGroupFeelingsTowards.
SERVER_API int __cdecl GetReputation(int const& VibeHandle, unsigned int const& GroupId,
                                     float& OutValue);

// Frees a vibe handle.
SERVER_API int __cdecl Free(int const& VibeHandle);

// Server.dll 0x7f760 (group/group overload): -4 for an invalid id without writing
// OutValue, else get_feelings_towards.
SERVER_API int __cdecl GetReputation(unsigned int const& GroupA, unsigned int const& GroupB,
                                     float& OutValue);

// Server.dll 0x7f4e0.
SERVER_API int __cdecl GetAttitude(int const& VibeA, int const& VibeB, float& OutAttitude);
} // namespace Reputation

namespace GF {
// MakeLocationID(base, room).
SERVER_API unsigned int __cdecl FindLocation(unsigned int BaseId, char const* Name);

// Server.dll 0x37ce0: Common's SpaceflightLocationID.
SERVER_API unsigned int __cdecl GetSpaceflightLocation();

// Server.dll 0x37d20: position of the solar that docks with base_id in system_id; 0 on
// success, -2 when none.
SERVER_API int __cdecl GetBasePosition(const unsigned int& system_id, const unsigned int& base_id,
                                       Vector& out_pos);

// Server.dll 0x38100: index of the named character placement of a room, 0 on success.
SERVER_API int __cdecl GetCharacterPlacementByName(unsigned int LocationId, char const* Name,
                                                   int& OutIndex);

// Costume tables.
SERVER_API int __cdecl GetBodyPart(char const* Name, int TypeId);
SERVER_API int __cdecl GetAccessory(char const* Name);

// Server.dll 0x37e70.
SERVER_API void __cdecl GetCostumeByID(int Id, Costume& OutCostume);

// BaseDB destroy.
SERVER_API void __cdecl AmbientScriptDestroy(unsigned long* Handle);

// Server.dll 0x384e0.
SERVER_API unsigned long __cdecl AmbientScriptCreate(AmbientScriptDescription const& Description);

// Bar characters.
// Server.dll .text:06d180c0.
SERVER_API int __cdecl GetCostumeSkeletonGender(Costume const& Desc, int& OutGender);
// Server.dll .text:06d17f30.
SERVER_API unsigned int __cdecl CharacterCreate(CharacterDescription const& Description);
// Server.dll .text:06d17fb0.
SERVER_API void __cdecl CharacterDestroy(unsigned int* Handle);
// Server.dll .text:06d17fe0.
SERVER_API void __cdecl CharacterSetBehavior(unsigned int CharacterId, unsigned long BehaviorHandle);
// Server.dll .text:06d18000 (struct in common_iface.h).
SERVER_API unsigned long __cdecl CharacterBehaviorCreate(CharacterBehaviorDescription const& Description);
// Server.dll .text:06d18090.
SERVER_API void __cdecl CharacterBehaviorDestroy(unsigned long* Handle);
// Server.dll 0x380e0.
SERVER_API int __cdecl GetNumCharacterPlacements(unsigned int LocationId);
// Server.dll 0x38140.
SERVER_API char const* __cdecl GetCharacterPlacementName(unsigned int LocationId, int Index);
// Server.dll 0x381a0.
SERVER_API int __cdecl GetCharacterPlacementPosture(unsigned int LocationId, int Index,
                                                    unsigned long& OutPosture);
// News and mission vendor.
SERVER_API unsigned long __cdecl NewsBroadcastCreate(NewsBroadcastDescription const& Description);
SERVER_API void __cdecl NewsBroadcastDestroy(unsigned long* Handle);
SERVER_API unsigned long __cdecl MissionVendorOfferCreate(MissionVendorOfferDescription const& Description);
SERVER_API void __cdecl MissionVendorOfferDestroy(unsigned long* Handle);
// Content.dll (FUN_06f047e0) passes the player id and the reason of FUN_06f04450 (0 offers
// made, 1 no faction to offer for, 2 no difficulty, 3 no objective zone, 4 other
// CreateMission failure); Server only forwards both to IClient slot 42.
SERVER_API int __cdecl ReportWhyMissionVendorEmpty(unsigned int ClientId, MVEmptyReason Reason);
SERVER_API void __cdecl MissionVendorAcceptance(unsigned long Handle, bool bAccepted,
                                                FmtStr const& Reason, unsigned int PlayerId);
} // namespace GF
} // namespace pub

// pub::System enumerators and ConnectionType enum
enum ConnectionType {
    CONN_TYPE_ALL = 0,
    CONN_TYPE_JUMP_GATE = 1,
    CONN_TYPE_JUMP_HOLE = 2,
    CONN_TYPE_TRADELANE = 3,
};

namespace pub {
namespace System {
// Not an interface: pub::System::EnumerateConnections (.text:06cfb170) writes straight into
// the caller's arrays at index Count, with no bound check on the count.
struct ConnectionEnumerator {
    unsigned int GotoSystemId[10]; // +0x00  FSystemConnection +0x00
    ID_String Object[10];          // +0x28  FSystemConnection +0x04
    unsigned int ObjectId[10];     // +0x50  FSystemConnection +0x08
    Vector Pos[10];                // +0x78  FSystemConnection +0x0c
    unsigned int Count;            // +0xf0
};
static_assert(sizeof(ConnectionEnumerator) == 0xf4, "ConnectionEnumerator size is 0xf4");

// Appends from enumerator.Count on (the caller zeroes Count and clears Object[] first).
SERVER_API int __cdecl EnumerateConnections(const unsigned int& system_id,
                                            ConnectionEnumerator& enumerator,
                                            ConnectionType connection_type);

// The 0x64-byte record pub::System::EnumerateObjects builds from each
// Universe::FSystemObject before handing it to the sink. The type name is the port's.
struct FSysObj {
    char Nickname[0x20];        // +0x00 strncpy(FSystemObject::Nickname.Data, 0x20)
    Vector Pos;                 // +0x20
    unsigned int ArchetypeId;   // +0x2c
    int IdsName;                // +0x30
    int IdsInfo;                // +0x34
    char Reputation[0x20];      // +0x38 strncpy(FSystemObject::Reputation.Data, 0x20)
    unsigned int DockWith;      // +0x58 FSystemObject +0x80
    unsigned int Goto;          // +0x5c FSystemObject +0x84
    unsigned int SystemId;      // +0x60 the enumerated system
};
static_assert(sizeof(FSysObj) == 0x64, "FSysObj size is 0x64");

// One virtual in slot 0; the enumeration stops when it returns false.
struct SysObjEnumerator {
    virtual bool OnObject(FSysObj const* Object) = 0;
};

// One virtual in slot 0, called with the reader already set_state'd to the zone's [Zone]
// section. The return value is not read; retail's name/return type are not exported.
struct ZoneEnumerator {
    virtual void OnZone(unsigned int const* ZoneId, INI_Reader* Reader) = 0;
};

// Server.dll .text:06cfb2e0.
SERVER_API int __cdecl EnumerateObjects(const unsigned int& SystemId, SysObjEnumerator& Enumerator);

// Walks the Universe zone INI sections.
SERVER_API int __cdecl EnumerateZones(unsigned int const& SystemId, ZoneEnumerator& Enumerator);

// ISystem zone-property query.
SERVER_API int __cdecl GetNestedProperties(unsigned int const& SystemId, Vector const& Position,
                                           unsigned long& OutProps);

// Server.dll .text:06cfb8e0: the system's name string id.
SERVER_API int __cdecl GetName(unsigned int SystemId, unsigned int& OutNameId);

// Server.dll .text:06cfb640 (Vector overload).
SERVER_API int __cdecl InZones(unsigned int system_id, const Vector& position, float radius,
                               unsigned int* const out_zones, unsigned int max_zones,
                               unsigned int& out_count);
// Server.dll .text:06cfb760 (view frustum overload: angle x, angle y, distance).
SERVER_API int __cdecl InZones(unsigned int system_id, const Transform& transform,
                               float angle_x, float angle_y, float distance,
                               unsigned int* const out_zones, unsigned int max_zones,
                               unsigned int& out_count);

// Server.dll .text:06cfad10.
SERVER_API int __cdecl ScanObjects(const unsigned int& system_id, unsigned int* const buffer,
                                   unsigned int max_results, const Vector& position, float radius,
                                   unsigned int type_mask, unsigned int& out_count);
} // namespace System

namespace Zone {
// Server.dll .text:06cfba60.
SERVER_API float __cdecl GetRadius(unsigned int zone_id);
// Server.dll .text:06cfbb70 (signed, negative inside).
SERVER_API float __cdecl GetDistance(unsigned int zone_id, const Vector& point);
// Server.dll .text:06cfba80.
SERVER_API int __cdecl GetSize(unsigned int zone_id, Vector& out_size);
// Server.dll .text:06cfbad0.
SERVER_API int __cdecl GetShape(unsigned int zone_id, ZoneShape& out_shape);
// Server.dll .text:06cfb9d0.
SERVER_API Vector __cdecl GetPos(unsigned int zone_id);
// Server.dll .text:06cfba20.
SERVER_API int __cdecl GetOrientation(const unsigned int& zone_id, Matrix& out_matrix);
// Server.dll .text:06cfbbe0.
SERVER_API int __cdecl GetProperties(unsigned int zone_id, unsigned long& out_props);
// Server.dll .text:06cfb970 (0 for an unknown zone).
SERVER_API unsigned int __cdecl GetSystem(unsigned int zone_id);
// Server.dll .text:06cfb990: the zone's faction population; null and -2 for an unknown zone.
SERVER_API int __cdecl GetPopulation(unsigned int zone_id,
                                     weighted_vector<unsigned int> const*& out_population);
// Content's wander guide takes a point when the result is < 1.0.
SERVER_API float __cdecl ClassifyPoint(unsigned int zone_id, const Vector& point);
// Server.dll .text:06cfbca0: the zone's name string id, -2 for an unknown zone.
SERVER_API int __cdecl GetName(unsigned int zone_id, unsigned int& out_name_id);
} // namespace Zone
} // namespace pub

// IServer (92 slots), IChatServer, GetServerInterface / GetChatServerInterface and the three
// chat hooks live in server_iserver.h, whose closure avoids common_cobject.h so FLServer.exe's
// MFC TUs can include it.
#include "server_iserver.h"

// FUN_06d18ea0 (Server.dll internal, cdecl): format, wrap as StyleNode(Style) +
// TextNode(text) rich text, encode into the chat command buffer and send it to Target from
// the console id. Defined in src/Server/server_chat.cpp.
void __cdecl Server_SendStyledChat(unsigned short Style, unsigned int Target,
                                   const wchar_t* Fmt, ...);

// ci traits, CiString / CiWString, CAccount, PlayerDB and `Players` live in
// server_playerdb.h (its closure avoids common_cobject.h, so MFC TUs of FLServer.exe
// can include it).
#include "server_playerdb.h"

// ============================================================================
// SrvAsteroid
// ============================================================================
namespace CmnAsteroid {
    class CAsteroidSystem;
    class CAsteroidField;
}

namespace SrvAsteroid {
    // One server-side record per CmnAsteroid::CAsteroidField: retail's 0x20-byte
    // "field entry" (operator new(0x20)). Defined in src/Server/server_classes.cpp.
    struct FSrvAsteroidField;

    // Retail layout (.text:06d03710) in member order; the port does not reproduce the
    // VC6 std::list layout (no retail ABI here).
    class SERVER_API SrvAsteroidSystem {
    public:
        SrvAsteroidSystem();
        SrvAsteroidSystem(const SrvAsteroidSystem& Other);
        ~SrvAsteroidSystem();
        SrvAsteroidSystem& operator=(const SrvAsteroidSystem& Other);

        void load(const char* Path);
        void update();
        void set_sys_id(unsigned int SystemId);
        void set_cmn_system(CmnAsteroid::CAsteroidSystem* Sys);
        void map_asteroid_fields();
        int AddRef();
        int Release();

    protected:
        CmnAsteroid::CAsteroidSystem* m_CmnSystem;      // +0x00
        std::list<FSrvAsteroidField*> m_Fields;         // +0x04..+0x0c
        unsigned int m_SystemId;                        // +0x10
        int m_RefCount;                                 // +0x14

        // The module-internal free functions below operate on the members
        // directly, as retail's inline copies of AddRef/set_cmn_system do.
        friend int SrvAsteroid_LoadSystem(unsigned int SystemId, const char* Path);
        friend int SrvAsteroid_UnloadSystem(unsigned int SystemId);
        friend void SrvAsteroid_UpdateSystem(IObjDB* Db);
        friend void SrvAsteroid_MapSystem(unsigned int SystemId);
    };

    // -- Server.dll-internal free functions over the per-system map DAT_06d8d980
    //    (std::map<unsigned int, SrvAsteroidSystem>).

    // FUN_06d032b0. Multiplayer: StarSystem::load, right after CmnAsteroid::Load.
    // Existing entry -> ++RefCount, return 2; otherwise insert a default entry,
    // ++RefCount, SystemId = id, load(Path), return 1.
    int SrvAsteroid_LoadSystem(unsigned int SystemId, const char* Path);

    // FUN_06d03410. From StarSystem::unload. Missing -> 0; --RefCount != 0 -> 2;
    // else erase -> 1.
    int SrvAsteroid_UnloadSystem(unsigned int SystemId);

    // FUN_06d03480. StarSystem's step: the id from Db->GetZoneDb()'s vtable[0], then
    // FUN_06d04480 on every field entry.
    void SrvAsteroid_UpdateSystem(IObjDB* Db);

    // FUN_06d03500. Single player: StarSystem::load. Mirrors the fields the client
    // already built (CmnAsteroid::Find).
    void SrvAsteroid_MapSystem(unsigned int SystemId);
}

// ============================================================================
// CPlayerGroup
// ============================================================================
// BaseGroupMessage::Type is the first dword of the 8-byte payload FUN_06d190d0 hands to
// IChatClient::SendCommonChatMsg(CHAT_ID{target}, DAT_06d8f5dc, 8, &{ MsgType, Param }).
// Retail fires 6 from CPlayerGroup::AddMember, 7 from DelMember to the removed player and
// 8 from DelMember to every remaining member.
namespace BaseGroupMessage {
    enum Type {
        MemberJoined = 6,
        RemovedFromGroup = 7,
        MemberLeft = 8,
    };
}

// FUN_06d19110 (cdecl): the same 8-byte control payload { MsgType, Param & 0xffff } sent
// to every active player, each addressed by CHAT_ID{slot client id}. Only caller: EqObj's
// destroy override FUN_06ce8080 (MsgType 0x14 with the dead player's id).
// Defined in server_classes.cpp.
void Server_BroadcastGroupControl(unsigned int MsgType, unsigned int Param);

struct CMissionObjectives {
    void* Data;
};

// Layout ({FmtStr Text; uint Type;}, 0x1c bytes) in rpc_iclient_args.h.
struct CSetMissionMessage;

// Retail layout (0x30 bytes), from the default ctor at 0x06d5b320:
//   +0x00  vftable (one slot, the scalar deleting dtor at 0x06ce13c0)
//   +0x04  m_GroupID
//   +0x08  m_Members  std::set<unsigned int>, size at +0x18
//   +0x1c  m_Invites  std::set<unsigned int>, size at +0x2c
// IsFull (0x06ce10e0) is m_Members.size() >= 8. Only GetMember is const (QBE).
class SERVER_API CPlayerGroup {
public:
    CPlayerGroup();
    CPlayerGroup(const CPlayerGroup& Other);
    virtual ~CPlayerGroup();
    CPlayerGroup& operator=(const CPlayerGroup& Other);
    static CPlayerGroup* __cdecl FromGroupID(unsigned int GroupId);
    bool AddMember(unsigned int PlayerId);
    bool DelMember(unsigned int PlayerId);
    bool IsMember(unsigned int PlayerId);
    bool AddInvite(unsigned int PlayerId);
    bool DelInvite(unsigned int PlayerId);
    bool IsInvited(unsigned int PlayerId);
    unsigned int GetMemberCount();
    unsigned int GetMember(int Index) const;
    unsigned int GetInviteCount();
    unsigned int GetID();
    bool IsFull();
    void HandleClientLogout(unsigned int PlayerId);
    void SendGroup(BaseGroupMessage::Type MsgType, unsigned int Param);
    void SendChat(int Style, const wchar_t* Msg, ...);
    void DeliverChat(unsigned long MsgType, const void* MsgData);
    void SetMissionObjectives(CMissionObjectives& Objs);
    void SetMissionMessage(CSetMissionMessage& Msg);
    void StoreMemberList(std::vector<unsigned int>& OutMembers);
    void RewardMembers(int Amount);
    unsigned int GetMissionID();
    unsigned int GetMissionSetBy();
    void SetMissionID(unsigned int MissionId, unsigned int SetBy);
protected:
    unsigned int m_GroupID;
    std::set<unsigned int> m_Members;
    std::set<unsigned int> m_Invites;

    // Retail exports both protected statics: s_GroupIDToGroupPtrMap (ordinal 508,
    // .data:06d90400) and s_uiGroupID (.data:06d90450). Deviation: retail's VC6 map spelled
    // its allocator with the mapped type; the port uses a plain std::map, so the decorated
    // name differs on purpose.
    typedef std::map<unsigned int, CPlayerGroup*> GroupMapType;
    static GroupMapType s_GroupIDToGroupPtrMap;
    static unsigned int s_uiGroupID;
};

// ============================================================================
// pub namespaces
// ============================================================================
namespace pub {

SERVER_API int __cdecl IsCommodity(const unsigned int& GoodId, bool& OutCommodity);
SERVER_API int __cdecl GetCargoHoldSize(const unsigned int& ShipArchId, unsigned int& OutSize);
// Server.dll 0x7e3f0: Equipment volume, Root mass.
SERVER_API int __cdecl GetGoodProperties(const unsigned int& GoodId, float& OutVolume, float& OutMass);
SERVER_API int __cdecl GetLoadoutName(const unsigned int& LoadoutId, char* OutBuffer, int MaxLen);
SERVER_API int __cdecl GetLoadoutID(unsigned int& OutId, const char* Nickname);
// Server.dll ordinal 201.
SERVER_API int __cdecl GetLoadout(EquipDescVector& OutLoadout, const unsigned int& LoadoutId);
SERVER_API int __cdecl GetEquipmentID(unsigned int& OutId, const char* Nickname);
SERVER_API int __cdecl GetGoodID(unsigned int& OutId, const char* Nickname);
// Server.dll 0x7E2E0.
SERVER_API int __cdecl GetShipID(unsigned int& OutShipId, const char* ShipName);
SERVER_API int __cdecl TranslateArchToGood(const unsigned int& ArchId, unsigned int& OutGoodId);
SERVER_API int __cdecl GetVoiceID(unsigned int& OutId, const char* Nickname);
SERVER_API int __cdecl GetCostumeID(int& OutId, const char* Nickname);
SERVER_API struct IFileSystem* __cdecl GetDataPath();
SERVER_API int __cdecl GetTime(double& OutTime);
SERVER_API int __cdecl GetRwTime(double& OutTime);
// Server.dll 0x7e1e0 (forwards to Common ::SinglePlayer).
SERVER_API bool __cdecl SinglePlayer();
SERVER_API bool __cdecl NextSystemID(unsigned int& InOutId);
SERVER_API bool __cdecl NextBaseID(unsigned int& InOutId);
SERVER_API int __cdecl GetSystemID(unsigned int& OutId, const char* Nickname);
SERVER_API int __cdecl GetSystemGateConnection(const unsigned int& GateId, unsigned int& OutConnectedSystemId);
SERVER_API int __cdecl GetBaseID(unsigned int& OutId, const char* Nickname);
// Server.dll 0x7efa0.
SERVER_API int __cdecl Save(unsigned int PlayerId, unsigned int SaveType);
SERVER_API bool __cdecl SinglePlayer();
SERVER_API int __cdecl BuildSystemReader(INI_Reader& Reader, const unsigned int& SystemId);
SERVER_API int __cdecl BuildBaseReader(INI_Reader& Reader, const unsigned int& BaseId);
SERVER_API unsigned int __cdecl GetSystemNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& SystemId);
SERVER_API unsigned int __cdecl GetBaseNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& BaseId);
SERVER_API int __cdecl GetBaseStridName(unsigned int& OutStrid, const unsigned int& BaseId);
SERVER_API int __cdecl GetLocations(const unsigned int& BaseId, unsigned int* const OutLocations, unsigned int MaxCount, unsigned int& OutCount);
SERVER_API unsigned int __cdecl GetLocationNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& RoomId);
SERVER_API int __cdecl GetBases(const unsigned int& SystemId, unsigned int* const OutBases, unsigned int MaxCount, unsigned int& OutCount);
SERVER_API int __cdecl GetSystem(unsigned int& OutSystemId, const unsigned int& BaseId);
SERVER_API unsigned int __cdecl GetNicknameId(const char* Nickname);
// Server.dll 0x7e160.
SERVER_API int __cdecl TranslateSystemToMsgIdPrefix(unsigned int SystemId, TString<64>& OutPrefix);
// Server.dll 0x7e0d0: the ship archetype's msg_id_prefix.
SERVER_API int __cdecl TranslateShipToMsgIdPrefix(unsigned int ShipArchId, TString<64>& OutPrefix);
// Server.dll 0x7e050: the good's msg_id_prefix.
SERVER_API int __cdecl TranslateGoodToMsgIdPrefix(unsigned int GoodId, TString<64>& OutPrefix);
// Server.dll 0x7e330: the ship archetype's Root::ObjectType.
SERVER_API int __cdecl GetType(const unsigned int& ShipArchId, unsigned int& OutType);
// Server.dll 0x7e230.
SERVER_API int __cdecl GetShipArchSTRID(const unsigned int& ShipArchId, unsigned int& OutStrid);
// Server.dll ordinal 211. 0 success, -2 solar missing, -4 bad id.
SERVER_API int __cdecl GetMaxHitPoints(unsigned int const& ObjId, int& OutHP);
SERVER_API unsigned short __cdecl MakeId(const char* Nickname);
SERVER_API void __cdecl DebugPrint(const char* Msg, int Level);
SERVER_API int __cdecl GetNavMapScale(unsigned int SystemId, float& OutScale);
SERVER_API int __cdecl ReportFreeTerminal(unsigned int P1, int P2);
SERVER_API HINSTANCE __cdecl DLL_LoadLibrary(const char* DllPath);

namespace Market {
    SERVER_API int __cdecl GetMinInventory(unsigned int BaseId, unsigned int GoodId, int& OutInv);
    SERVER_API int __cdecl GetPrice(unsigned int BaseId, unsigned int GoodId, float& OutPrice);
    SERVER_API int __cdecl GetNominalPrice(unsigned int GoodId, float& OutPrice);
    SERVER_API int __cdecl GetGoodJumpDist(unsigned int GoodId, unsigned int& OutDist);
    SERVER_API int __cdecl IsGoodInDemand(unsigned int BaseId, unsigned int GoodId, bool& OutDemand);
    SERVER_API int __cdecl GetNumCommoditiesForSale(unsigned int BaseId, int* OutCount);
    SERVER_API int __cdecl GetCommoditiesForSale(unsigned int BaseId, unsigned int* const OutGoods, int* InOutCount);
    SERVER_API int __cdecl GetNumCommoditiesInDemand(unsigned int BaseId, int* OutCount);
    SERVER_API int __cdecl GetCommoditiesInDemand(unsigned int BaseId, unsigned int* const OutGoods, int* InOutCount);
}

namespace Audio {
    struct Tryptich {
        unsigned int Field0;
        unsigned int Field4;
        unsigned int Field8;
        unsigned int FieldC;
        unsigned char Field10;
        unsigned int Field14;
    };

    SERVER_API int __cdecl SetMusic(unsigned int PlayerId, const Tryptich& Track);
    SERVER_API int __cdecl CancelMusic(unsigned int PlayerId);
    SERVER_API int __cdecl PlaySoundEffect(unsigned int PlayerId, unsigned int SoundId);
}

namespace Phantom {
    SERVER_API int __cdecl Create(unsigned int SystemId, const Vector& MinPos, const Vector& MaxPos, const Matrix& Orient, unsigned int Flags, void*& OutHandle);
    SERVER_API int __cdecl Create(unsigned int SystemId, float Radius, const Vector& Center, unsigned int Flags, void*& OutHandle);
    SERVER_API int __cdecl Destroy(void* Handle);
    SERVER_API int __cdecl SetActive(void* Handle, bool bActive);
    SERVER_API int __cdecl Attach(const unsigned int& ObjId, void* Handle);
    SERVER_API int __cdecl Detach(void* Handle);
}

namespace AI {
    SERVER_API OP_RTYPE __cdecl SubmitState(unsigned int Flid, BaseOp* Op);
    SERVER_API OP_RTYPE __cdecl SubmitDirective(unsigned int Flid, BaseOp* Op);
    SERVER_API OP_RTYPE __cdecl set_directive_priority(unsigned int Flid, DirectivePriority Priority);
    SERVER_API int __cdecl get_behavior_id(unsigned int Flid);
    SERVER_API int __cdecl get_state_graph_id(unsigned int Flid);
    SERVER_API bool __cdecl get_personality(unsigned int Flid, Personality& OutPersonality);
    SERVER_API bool __cdecl enable_maneuver(unsigned int Flid, int ManeuverId, bool bEnable);
    SERVER_API bool __cdecl enable_all_maneuvers(unsigned int Flid);
    SERVER_API bool __cdecl lock_maneuvers(unsigned int Flid, bool bLock);
    SERVER_API ScanResponse __cdecl get_scan_response(unsigned int ScannerFlid, unsigned int TargetFlid, unsigned int GoodId);
    SERVER_API int __cdecl submit_forced_target(unsigned int Flid, unsigned int TargetFlid);
    SERVER_API int __cdecl remove_forced_target(unsigned int Flid, unsigned int TargetFlid);
    SERVER_API bool __cdecl set_player_enemy_clamp(unsigned int Flid, int MinClamp, int MaxClamp);
    // .text:06d5d7a0: ShipFlid is the ship whose IObjAffect +0x10/+0x14 get the new
    // leader/offset.
    SERVER_API FORMATION_RTYPE __cdecl update_formation_state(unsigned int ShipFlid, unsigned int LeaderFlid, const Vector& Offset);
    SERVER_API void __cdecl refresh_state_graph(unsigned int Flid);
}

namespace Player {
    struct MissionObjective {
        unsigned int Type;
        FmtStr Description;
    };

    SERVER_API int __cdecl GetRep(const unsigned int& PlayerId, int& OutRep);
    SERVER_API int __cdecl SendNNMessage(unsigned int PlayerId, unsigned int MessageId);
    SERVER_API int __cdecl ForceLand(unsigned int PlayerId, unsigned int BaseId);
    SERVER_API int __cdecl RevertCamera(unsigned int PlayerId);
    // Server.dll .text:06d55e60.
    SERVER_API int __cdecl SetCamera(unsigned int PlayerId, const Transform& Camera, float Fov,
                                     float Aspect);
    // Server.dll .text:06d54d20 / .text:06d54fb0.
    SERVER_API int __cdecl EnumerateCargo(const unsigned int& PlayerId,
                                          CargoEnumerator& Enumerator);
    SERVER_API int __cdecl RemoveCargo(const unsigned int& PlayerId, unsigned short CargoSlot,
                                       unsigned int Quantity);
    // Server.dll .text:06d550a0.
    SERVER_API int __cdecl SetShipAndLoadout(const unsigned int& PlayerId, unsigned int ShipArchId,
                                             const EquipDescVector& Loadout);
    SERVER_API int __cdecl InspectCash(const unsigned int& PlayerId, int& OutCash);
    SERVER_API int __cdecl SetInitialPos(const unsigned int& PlayerId, const Vector& Position);
    SERVER_API int __cdecl SetInitialOrnt(const unsigned int& PlayerId, const Matrix& Orientation);
    SERVER_API int __cdecl AdjustCash(const unsigned int& PlayerId, int Amount);
    SERVER_API int __cdecl GetShipID(const unsigned int& PlayerId, unsigned int& OutShipId);
    // The ship's space object id.
    SERVER_API int __cdecl GetShip(const unsigned int& PlayerId, unsigned int& OutShipObjId);
    SERVER_API int __cdecl GetAssetValue(const unsigned int& PlayerId, float& OutValue);
    SERVER_API int __cdecl GetRank(const unsigned int& PlayerId, int& OutRank);
    SERVER_API int __cdecl SetRank(unsigned int PlayerId, int Rank);
    // Server.dll 0x06d56460; Content's loader calls (player, controller, player, false, 0).
    SERVER_API int __cdecl SetMsnID(unsigned int PlayerId, unsigned int MissionId,
                                   unsigned int SubId, bool bNotify, unsigned int NotifyArg);
    // Server.dll .text:06d55830.
    SERVER_API int __cdecl DisplayMissionMessage(unsigned int const& PlayerId, FmtStr const& Text,
                                                MissionMessageType MsgType, bool bBroadcast);
    // Server.dll .text:06d55df0.
    SERVER_API int __cdecl PopUpDialog(unsigned int PlayerId, FmtStr const& Title,
                                      FmtStr const& Text, unsigned int Buttons);
    // Server.dll .text:06d55f80.
    SERVER_API int __cdecl LoadHint(unsigned int PlayerId, BaseHint* Hint);
    // Server.dll .text:06d549d5.
    SERVER_API int __cdecl MarkObj(unsigned int PlayerId, unsigned int ObjectId, int MarkType);
    SERVER_API int __cdecl GetGender(const unsigned int& PlayerId, int& OutGender);
    SERVER_API int __cdecl SetMoneyNeededToNextRank(unsigned int PlayerId, int Money);
    // Server.dll 0x75280.
    SERVER_API int __cdecl GetMoneyNeededToNextRank(const unsigned int& PlayerId, int& OutMoney);
    // Server.dll 0x75e40.
    SERVER_API int __cdecl ReturnPlayerStats(unsigned int PlayerId, unsigned char* Data, int DataLen);
    // Server.dll 0x75e20.
    SERVER_API int __cdecl ReturnBestPath(unsigned int PlayerId, unsigned char* Data, int DataLen);
    SERVER_API int __cdecl GetGroupMembers(unsigned int PlayerId, std::vector<unsigned int>& OutMembers);
    // Server.dll 0x75fa0.
    SERVER_API int __cdecl GetGroupSize(unsigned int PlayerId, unsigned int& OutSize);
    // Server.dll 0x763a0.
    SERVER_API int __cdecl RewardGroup(unsigned int PlayerId, int Amount);
    // Server.dll 0x75fe0; retail takes VC6's wstring.
    SERVER_API int __cdecl GetName(unsigned int PlayerId, std::wstring& OutName);
    // Server.dll 0x76170.
    SERVER_API int __cdecl IsGroupMember(unsigned int PlayerId, unsigned int OtherId, bool& OutResult);
    // Server.dll 0x761c0.
    SERVER_API int __cdecl RemoveFromGroup(unsigned int PlayerId);
    SERVER_API int __cdecl SetMissionObjectives(const unsigned int& PlayerId, const unsigned int& MissionId, const MissionObjective* Objs, unsigned int Count, const FmtStr& Title, unsigned char Type, const FmtStr& Desc);
    SERVER_API int __cdecl ReplaceMissionObjective(const unsigned int& PlayerId, const unsigned int& MissionId, unsigned int Index, const MissionObjective& Obj);
    SERVER_API int __cdecl SetMissionObjectiveState(const unsigned int& PlayerId, const unsigned int& MissionId, int Index, unsigned int State);
}

} // namespace pub

// ============================================================================
// CRemotePhysicsSimulation — network physics interpolation for remote objects.
//
// A Common.dll class (vftable ordinal 1023, .rdata:0639b994); Server.dll imports it.
//
// Exports differ per member. Retail Common.dll exports all sixteen; Server.dll
// and FreeLancer.exe each export their own copy of eight inline members plus their own
// vftable (Common / Server / exe):
//   GetSimulationTime  0x23500 / 0x2380 / 0053fcd0   CalcPositionError 0x23510 / 0x2390 / 0053fce0
//   GetSampleCount     0x23540 / 0x23c0 / 0053fd10   GetLatestUpdate   0x23550 / 0x23d0 / 0053fd20
//   GetSample          0x23570 / 0x23f0 / 0053fd40   SetLogging        0x235a0 / 0x2420 / 0053fd70
//   copy ctor          0x235b0 / 0x2430 / 0053fd80   operator=         0x23b30 / 0x29b0 / 00540300
//   vftable            0x13b994 / 0x84f18 / .rdata:005df89c
// Common only: default ctor 0x31c70, dtor 0x31d50, Reset 0x31d70, RemoteUpdate 0x31e10,
// Doit 0x31fe0, CheckForSync 0x325c0, tracef 0x31bb0. Server's own vftable slots still
// resolve into Common (slot 1 JMPs to the imported RemoteUpdate, slot 0's deleting dtor to
// the imported dtor). The three modules' copies of the eight are identical.
//
// The port mirrors this with a class-level SHARED_INLINE_API (dllexport in Common, Server
// and the exe): the eight shared members are inline below, so each module emits its own
// copy and vftable; the seven Common-only members have their only bodies in
// src/Common/common_remotephysics.cpp and the other modules bind to Common.lib's import
// thunks. Do not give those seven bodies to Server.dll or the exe: duplicate bodies drift.
//
// Size 0x2E8 (stride pushed by the vector deleting dtor at .text:0628417a): the trailing
// bool at +0x2E0 is padded to the class's 8-byte alignment.
// Vtable: two slots, the vector deleting dtor (.text:06284160) and RemoteUpdate
// (.text:06291e10).
// ============================================================================
class SHARED_INLINE_API CRemotePhysicsSimulation {
public:
    CRemotePhysicsSimulation();                                                  // Common only

    // Copy constructor -- Common .text:062835b0. Retail's compiler-generated memberwise
    // copy: each embedded CDeadReckonedVector via its own (implicit) copy ctor, which
    // stamps the module's own ??_7CDeadReckonedVector, then the trailing members, then
    // this's vptr last. Only padding is left unwritten (+0x2bc, +0x2e1..+0x2e7).
    CRemotePhysicsSimulation(const CRemotePhysicsSimulation& Other)
        : m_PositionDRV(Other.m_PositionDRV)
        , m_OrientAxisXDRV(Other.m_OrientAxisXDRV)
        , m_OrientAxisYDRV(Other.m_OrientAxisYDRV)
        , m_Velocity(Other.m_Velocity)                          // +0x278
        , m_BlendFromVelocity(Other.m_BlendFromVelocity)        // +0x284
        , m_BlendToVelocity(Other.m_BlendToVelocity)            // +0x290
        , m_Orientation(Other.m_Orientation)                    // +0x29c
        , m_ProjectedPosition(Other.m_ProjectedPosition)        // +0x2ac
        , m_SampleCount(Other.m_SampleCount)                    // +0x2b8
        , m_SimulationTime(Other.m_SimulationTime)              // +0x2c0
        , m_ConvergeWindow(Other.m_ConvergeWindow)              // +0x2c8
        , m_Unread2D0{Other.m_Unread2D0[0], Other.m_Unread2D0[1]}  // +0x2d0
        , m_SyncBackoff(Other.m_SyncBackoff)                    // +0x2d8
        , m_bLogging(Other.m_bLogging)                          // +0x2e0
    {
    }

    virtual ~CRemotePhysicsSimulation();                                         // vtable slot 0, Common only
    // vtable slot 1 (.text:06291e10). Feeds one network sample to the three
    // dead-reckoning sub-objects: the position verbatim, then the orientation's first two
    // basis columns from I3DMathEngine::QuaternionToMatrix. Common only.
    virtual void RemoteUpdate(Vector const& Position, Quaternion const& Orientation,
                              double Timestamp);

    // operator= -- Common .text:06283b30. Compiler-generated memberwise assignment; no
    // vptr writes and no self-assignment test, as in retail.
    CRemotePhysicsSimulation& operator=(const CRemotePhysicsSimulation& Other)
    {
        m_PositionDRV = Other.m_PositionDRV;
        m_OrientAxisXDRV = Other.m_OrientAxisXDRV;
        m_OrientAxisYDRV = Other.m_OrientAxisYDRV;
        m_Velocity = Other.m_Velocity;                                // +0x278
        m_BlendFromVelocity = Other.m_BlendFromVelocity;              // +0x284
        m_BlendToVelocity = Other.m_BlendToVelocity;                  // +0x290
        m_Orientation = Other.m_Orientation;                          // +0x29c
        m_ProjectedPosition = Other.m_ProjectedPosition;              // +0x2ac
        m_SampleCount = Other.m_SampleCount;                          // +0x2b8
        m_SimulationTime = Other.m_SimulationTime;                    // +0x2c0
        m_ConvergeWindow = Other.m_ConvergeWindow;                    // +0x2c8
        m_Unread2D0[0] = Other.m_Unread2D0[0];                        // +0x2d0
        m_Unread2D0[1] = Other.m_Unread2D0[1];                        // +0x2d4
        m_SyncBackoff = Other.m_SyncBackoff;                          // +0x2d8
        m_bLogging = Other.m_bLogging;                                // +0x2e0
        return *this;
    }

    // The accessors below read the position sub-object's fields directly, as retail does
    // (e.g. this+0x74 = m_PositionDRV(+0x08) + TrackedPosition(+0x6c)).

    // CalcPositionError -- Common .text:06283510. Every term stays on the x87 stack and
    // the FSQRT result is returned in ST0, so the port keeps every intermediate in double.
    // Summation order is retail's: (Dx^2 + Dz^2) + Dy^2.
    double CalcPositionError(Vector const& Point)
    {
        Vector const& Tracked = m_PositionDRV.TrackedPosition;      // this+0x74
        double const Dx = (double)Point.x - Tracked.x;
        double const Dy = (double)Point.y - Tracked.y;
        double const Dz = (double)Point.z - Tracked.z;
        return sqrt((Dx * Dx + Dz * Dz) + Dy * Dy);
    }

    // Common .text:06283550: the position DRV's Samples[0].
    Vector GetLatestUpdate()
    {
        return m_PositionDRV.Samples[0];                             // this+0x10
    }

    // Common .text:06283570. No bounds check.
    Vector GetSample(int Index)
    {
        return m_PositionDRV.Samples[Index];                         // this+0x10+Index*0xc
    }

    // Common .text:06283540.
    unsigned long GetSampleCount()
    {
        return m_SampleCount;
    }

    // Common .text:06283500.
    double GetSimulationTime()
    {
        return m_SimulationTime;
    }

    // Common .text:062835a0.
    void SetLogging(bool bEnable)
    {
        m_bLogging = bEnable;
    }

    // Common only from here to the end of the public section.
    // Doit — advance simulation by Dt seconds (Server.dll's Observer update).
    void Doit(double Dt);

    // Common.dll.c 0x325c0: whether the caller should force a resync of this object's
    // dead-reckoned state. Param1/Param2 are two independently sampled positions (their
    // separation scales the tolerance); Param3 is the orientation checked against the
    // tracked basis vectors. Details on the implementation.
    bool CheckForSync(Vector const& Param1, Vector const& Param2, Quaternion const& Param3);

    // Common.dll.c 0x31d70: re-primes the trailing scratch/sync fields (sample count,
    // simulation time, sync backoff) without touching the DRV sub-objects or m_bLogging.
    void Reset();

    // Common.dll.c 0x31bb0: gated on m_bLogging. Writes to c:\flservertrace.txt (MP server,
    // handle kept open across calls) or appends to c:\client.updates (single-player/client).
    void tracef(char const* Format, ...);

public:
    // Data members are public: PhySys::GetVelocity (.text:06285823) reads m_Velocity
    // of a CRemotePhysicsController's simulation base directly.
    //
    // No explicit padding after the vptr: the class is 8-aligned (doubles), so MSVC
    // already starts the first DRV at +0x08.
    // Three CDeadReckonedVector sub-objects (common_cobject.h, 0xD0 each); every method the
    // simulation calls on them is non-virtual. None is a velocity: RemoteUpdate feeds the
    // position sample to the first and the first and second columns of the orientation's
    // rotation matrix (the world-space X and Y axes; Z follows from them) to the other
    // two. CheckForSync compares the same two columns.
    CDeadReckonedVector m_PositionDRV;        // +0x08   position
    CDeadReckonedVector m_OrientAxisXDRV;     // +0xD8   orientation basis column 0
    CDeadReckonedVector m_OrientAxisYDRV;     // +0x1A8  orientation basis column 1
    // The velocity Doit() publishes this tick: a lerp from m_BlendFromVelocity to
    // m_BlendToVelocity inside the blend window, the position DRV's ProjectVelocity after
    // it. Read by PhySys::GetVelocity, copied into the golem's TargetSpeed by
    // do_simulation_controller, seeded by the controller's ctor.
    Vector m_Velocity;                        // +0x278
    // m_Velocity as of the last RemoteUpdate: the lerp's "from".
    Vector m_BlendFromVelocity;               // +0x284
    // Velocity + 0.5*Acceleration*Window^2 of the new fit (RemoteUpdate): the lerp's "to".
    Vector m_BlendToVelocity;                 // +0x290
    // The orthonormal basis Doit() rebuilds each tick, written by
    // I3DMathEngine::MatrixToQuaternion in (W,X,Y,Z) order.
    Quaternion m_Orientation;                 // +0x29C
    // Mirror of m_PositionDRV.ProjectedPosition.
    Vector m_ProjectedPosition;               // +0x2AC
    unsigned long m_SampleCount;              // +0x2B8
    // (+0x2BC: alignment padding, not copied by the copy ctor / operator=)
    double m_SimulationTime;                  // +0x2C0
    // The blend window: RemoteUpdate copies m_PositionDRV.ConvergeWindow here, Reset
    // zeroes it, Doit compares the clock with it.
    double m_ConvergeWindow;                  // +0x2C8
    // Eight bytes the copy ctor and operator= copy as two dwords; no other Common.dll code
    // reads or writes them (the ctor and Reset leave them alone). Their type is unknown.
    unsigned long m_Unread2D0[2];             // +0x2D0
    // CheckForSync's resync backoff timer: 5.0 from the ctor/Reset, doubled each time
    // m_SimulationTime outgrows it, back to 5.0 once a resync is forced.
    double m_SyncBackoff;                     // +0x2D8
    bool m_bLogging;                          // +0x2E0
};

static_assert(offsetof(CRemotePhysicsSimulation, m_PositionDRV) == 0x08, "CRPS +0x08");
static_assert(offsetof(CRemotePhysicsSimulation, m_OrientAxisXDRV) == 0xD8, "CRPS +0xd8");
static_assert(offsetof(CRemotePhysicsSimulation, m_OrientAxisYDRV) == 0x1A8, "CRPS +0x1a8");
static_assert(offsetof(CRemotePhysicsSimulation, m_Velocity) == 0x278, "CRPS +0x278");
static_assert(offsetof(CRemotePhysicsSimulation, m_Orientation) == 0x29C, "CRPS +0x29c");
static_assert(offsetof(CRemotePhysicsSimulation, m_ProjectedPosition) == 0x2AC, "CRPS +0x2ac");
static_assert(offsetof(CRemotePhysicsSimulation, m_SampleCount) == 0x2B8, "CRPS +0x2b8");
static_assert(offsetof(CRemotePhysicsSimulation, m_SimulationTime) == 0x2C0, "CRPS +0x2c0");
static_assert(offsetof(CRemotePhysicsSimulation, m_ConvergeWindow) == 0x2C8, "CRPS +0x2c8");
static_assert(offsetof(CRemotePhysicsSimulation, m_SyncBackoff) == 0x2D8, "CRPS +0x2d8");
static_assert(offsetof(CRemotePhysicsSimulation, m_bLogging) == 0x2E0, "CRPS +0x2e0");

// Retail's element stride from the vector deleting destructor; no STL member, so safe.
static_assert(sizeof(CRemotePhysicsSimulation) == 0x2E8, "CRemotePhysicsSimulation sizeof 0x2e8");

// ============================================================================
// CRemotePhysicsController -- the object PhySys::RCBegin hangs off CObject+0x60 for a
// remotely simulated object. It has a CRemotePhysicsSimulation as its second base.
//
// RTTI: TypeDescriptor .data:063eadf4, hierarchy descriptor .rdata:063a6af8 (multiple
// inheritance; CRemotePhysicsSimulation at mdisp 0xd0).
// Vftables:
//   .rdata:0639ba38 (IVP_Controller_Golem base, +0x00), eight slots: [0..3] inherited,
//     [4] 062923e0 do_simulation_controller, [5] 06285a70 get_controller_priority
//     (IVP_Controller_Motion's, 500), [6] 06285a80 scalar deleting dtor, [7] 06292570
//     beam_to_target.
//   .rdata:0639ba2c (CRemotePhysicsSimulation base, +0xd0): [0] 06285de0 this-adjusting
//     dtor thunk, [1] 062923a0 RemoteUpdate.
// Layout (operator new(0x3c0), .text:06285962): +0x000 IVP_Controller_Golem (0xd0),
// +0x0d0 CRemotePhysicsSimulation (0x2e8), +0x3b8 RealObject (the CObject's +0x54 body;
// stored but never read by Common).
//
// Not exported by retail. Server.dll only names it in an __RTDynamicCast (FUN_06d4ccd0)
// and then reads the simulation base's sample count and latest sample through the inline
// accessors. Defined in src/Common/common_remotephysics.cpp.
// ============================================================================
class CRemotePhysicsController : public IVP_Controller_Golem, public CRemotePhysicsSimulation
{
public:
    // Inlined by retail into PhySys::RCBegin: both bases, the two vptrs,
    // RemoteUpdate(Pos, Orient, 0.0) through this class's override (called directly), then
    // the velocity into +0x348, the body into +0x3b8 and the sample count back to zero.
    CRemotePhysicsController(IVP_Real_Object* InBody, FLControllerGolemParms const& Parms,
                             Vector const& Position, Quaternion const& Orientation,
                             Vector const& Velocity);

    // FUN_06285aa0 (body) / FUN_06285a80 (scalar deleting) / LAB_06285de0 (thunk).
    virtual ~CRemotePhysicsController();

    // LAB_062923e0
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* AffectedCores) override;

    // FUN_06292570
    virtual int beam_to_target(IVP_Event_Sim const* EventSim, int Reason) override;

    // FUN_062923a0
    virtual void RemoteUpdate(Vector const& Position, Quaternion const& Orientation,
                              double Timestamp) override;

public:
    IVP_Real_Object* RealObject;  // +0x3b8
};

static_assert(sizeof(CRemotePhysicsController) == 0x3C0,
              "CRemotePhysicsController is operator new(0x3c0) at .text:06285962");

// ============================================================================
// OwnerList<pub::SpaceObj::CargoDesc> (templates in common_metalist.h). Retail exports
// the six OwnerList<CargoDesc> members (default and copy ctor, dtor, operator=, vftable,
// free) from Common.dll, Server.dll and freelancer.exe with identical bodies, so each
// module expands OWNERLIST_CARGODESC_EXPORTS once (common_goodinfo.cpp,
// server_classes.cpp, client_objdb.cpp).
// ============================================================================
namespace pub {
namespace SpaceObj {
    // Full definition in server_cobject.h; OwnerList frees Data with operator delete.
    struct CargoDesc;

    // Server.dll .text:06cf8700. Content passes a duration stored in its list node.
    SERVER_API int __cdecl SetInvincible(unsigned int ObjId, bool bInvincible, bool Param3,
                                        float Duration);
    // Content calls (id, false, true, 1.0f) for a critical loot.
    SERVER_API int __cdecl SetInvincible2(unsigned int ObjId, unsigned char bInvincible,
                                         unsigned char Param3, float Duration);
    // 06cf88a0: writes the two raw getter bytes into its output references.
    SERVER_API int __cdecl GetInvincible(unsigned int ObjId, unsigned char& OutFromPlayers,
                                         unsigned char& OutProtect, float& OutMaxHpLoss);

    // Server.dll 0x18da0.
    SERVER_API int __cdecl GetType(unsigned int ObjId, unsigned int& OutType);
    // Server.dll 0x19a40.
    SERVER_API int __cdecl GetJumpTarget(const unsigned int& ObjId, unsigned int& OutSystemId,
                                         unsigned int& OutObjectId);

    // Server.dll 0x19880.
    SERVER_API int __cdecl GetDockingTarget(const unsigned int& ObjId, unsigned int& OutTarget);

    // Server.dll 0x18ba0.
    SERVER_API int __cdecl GetRep(unsigned int ObjId, int& OutRep);

    // Server.dll 0x20551.
    SERVER_API int __cdecl GetRelativeHealth(unsigned int const& ObjId, float& OutRatio);

    SERVER_API int __cdecl SetRelativeHealth(const unsigned int& ObjId, float Ratio);

    SERVER_API int __cdecl LightFuse(const unsigned int& ObjId, const char* FuseName, float Delay);

    // ClientId is the client the request goes to (IClient::SendServerRequestSpaceScript).
    SERVER_API int __cdecl RequestSpaceScript(const unsigned int& ObjId, const Vector& Pos,
                                              const int& Priority, unsigned int ClientId,
                                              const char* ScriptName);

    // Server.dll 0x18732.
    SERVER_API int __cdecl GetLocation(unsigned int ObjId, Vector& OutPos, Matrix& OutOrient);

    // Server.dll .text:06cf8e00.
    SERVER_API int __cdecl GetSystem(unsigned int ObjId, unsigned int& OutSystemId);

    SERVER_API int __cdecl GetRadius(unsigned int const& ObjId, float& OutRadius,
                                     Vector& OutCenter);

    SERVER_API int __cdecl GetTarget(unsigned int const& ObjId, unsigned int& OutTargetId);

    SERVER_API int __cdecl GetSolarRep(unsigned int SolarId, int& OutRep);

    // Server.dll 0x18d00.
    SERVER_API int __cdecl GetSolarParent(unsigned int const& ObjId, unsigned int& OutParentId);

    // Server.dll 0x1ac00.
    SERVER_API int __cdecl GetTradelaneNextAndPrev(const unsigned int& obj_id_ref,
                                                   unsigned int& out_next, unsigned int& out_prev);

    // Server.dll 0x18b20.
    SERVER_API int __cdecl GetMotion(unsigned int ObjId, Vector& OutLinear, Vector& OutAngular);

    // OutArchId = Good2Arch(GoodId).
    SERVER_API int __cdecl GetArchetypeID(unsigned int const& GoodId, unsigned int& OutArchId);

    // Server.dll 0x196d0.
    SERVER_API int __cdecl GetCargoSpaceOccupied(unsigned int const& ObjId, unsigned int& OutOccupied);

    // Server.dll .text:06cf9230.
    SERVER_API int __cdecl SendComm(unsigned int SenderId, unsigned int TargetId,
                                     unsigned int VoiceId, const Costume* CommCostume,
                                     unsigned int CommType, unsigned int* TextIds,
                                     int TextCount, unsigned int Mood, float Duration,
                                     bool bInterrupt);

    SERVER_API int __cdecl DrainShields(unsigned int ObjId);

    // Server.dll 0x18870. 0 when the object exists and is alive, -2 otherwise.
    SERVER_API int __cdecl ExistsAndAlive(unsigned int ObjId);

    SERVER_API int __cdecl GetVoiceID(unsigned int const& ObjId, unsigned int& OutVoiceId);

    SERVER_API int __cdecl DockRequest(const unsigned int& ObjId, const unsigned int& TargetId);

    // Server.dll 0x1a6e0. DockPort is -1 for any port.
    SERVER_API int __cdecl Dock(const unsigned int& ShipId, const unsigned int& TargetId,
                                int DockPort, DOCK_HOST_RESPONSE Response);

    // Server.dll 0x1a370.
    SERVER_API int __cdecl Relocate(const unsigned int& ObjId, const unsigned int& SystemId,
                                    const Vector& Position, const Matrix& Orientation);

    // Server.dll .text:06cfa220.
    SERVER_API int __cdecl JumpIn(const unsigned int& ShipId, const unsigned int& GateId);

    // Server.dll 0x18e70.
    SERVER_API int __cdecl GetHardpoint(const unsigned int& ObjId, const char* HardpointName,
                                        Vector* OutPosition, Matrix* OutOrientation);

    // Server.dll .text:06cfa520.
    SERVER_API int __cdecl Launch(const unsigned int& LaunchObjId, const unsigned int& DockObjId,
                                  int LaunchType);

    // Server.dll .text:06cf9ac0. Content passes (dock target, occupied, terminal).
    SERVER_API int __cdecl Activate(const unsigned int& ObjId, bool bActivate, int Param);

    // Server.dll .text:06cfa9f0.
    SERVER_API int __cdecl FormationResponse(const unsigned int& ObjId, FORMATION_RTYPE Response);

    // Server.dll .text:06cfaa50. Content passes responses 1/4/5.
    SERVER_API int __cdecl LaneResponse(const unsigned int& ObjId, int Response);

    SERVER_API int __cdecl GetScannerRange(unsigned int ObjId, int& OutRadarRange,
                                           int& OutCargoScanRange);

    SERVER_API int __cdecl JettisonEquipment(const unsigned int& ObjId,
                                             const unsigned short& EquipId, const int& Quantity);
} // namespace SpaceObj

// Server.dll .text:06cffd10.
SERVER_API int __cdecl ReportFreeTerminal(unsigned int DockTargetId, int TerminalIndex);

// Server.dll 0x7e270. 0 found, -2 the archetype failed to load, -3 no such hardpoint.
SERVER_API int __cdecl FindHardpoint(char const* Name, unsigned int ShipArchId, Vector& OutPos,
                                     Matrix& OutRot);
} // namespace pub

static_assert(sizeof(OwnerList<pub::SpaceObj::CargoDesc>) == 0x14, "OwnerList sizeof 0x14");

// Member-level export on purpose: class-level dllexport (or an extern-template base) also
// exports all of MetaList<CargoDesc>, which retail never exports. Known gap: MSVC
// member-level export emits no ??_7.
#define OWNERLIST_CARGODESC_EXPORTS                                                               \
    template SHARED_INLINE_API OwnerList<pub::SpaceObj::CargoDesc>::OwnerList();                  \
    template SHARED_INLINE_API OwnerList<pub::SpaceObj::CargoDesc>::OwnerList(                    \
        OwnerList<pub::SpaceObj::CargoDesc> const&);                                              \
    template SHARED_INLINE_API OwnerList<pub::SpaceObj::CargoDesc>::~OwnerList();                 \
    template SHARED_INLINE_API OwnerList<pub::SpaceObj::CargoDesc>&                               \
        OwnerList<pub::SpaceObj::CargoDesc>::operator=(OwnerList<pub::SpaceObj::CargoDesc> const&); \
    template SHARED_INLINE_API void OwnerList<pub::SpaceObj::CargoDesc>::free();
