// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_net.h"
#include "../include/common_physys.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_savefile.h" // Server_AccountDirName / Server_GetAccountsRoot (FUN_06d49f60)
#include "../include/common_ini.h"
#include "../include/common_misc.h"
#include "../include/server_sysdb.h"
#include "../include/server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "../include/server_basedb.h"
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FServerSetReputationInfo / FPayloadServerActivateObject (add_player MP walk)
#include "../include/md5hash.h"      // MD5HASH_API: Server exports its implicit members like retail
#include "server_internal.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <list>
#include <set>
#include <io.h>
#include <wchar.h>
#include <stdarg.h>

#pragma comment(lib, "winmm.lib")

// Forward declaration for IClient (defined in rpc_iclient.h)
class IClient;

// Retail FUN_06d43650(msgId, payload) is a controller BROADCAST (retail 0x06d44080);
// Server_ControllerDispatch (server_dlls.cpp, common_server_controller.h) is that walk.

// Retail reaches IServerImpl members through the exported `Server` object; this file takes
// the same object from GetServerInterface() / GetChatServerInterface() (Server + 4).

// The IClient / IChatClient halves of rpclocal's Client object (.data:DAT_06d8fb14 /
// DAT_06d8fb18), defined in IServerImpl.cpp.
extern IClient* g_pClientInterface;
extern IChatClient* g_pChatClientInterface;

// .data:DAT_06d8f5dc -- the group-control chat channel, 0x00010004 (CRT initializer
// 0x06d189c0 also sets the channel byte). Defined in server_chat.cpp.
extern const unsigned int GGroupChatSourceId;
// .data:DAT_06d9042c -- the channel CPlayerGroup::DeliverChat stamps on every copy,
// 0x00010003 (CRT initializer 0x06d5b190), the group chat channel.
static const unsigned int GDeliverChatSourceId = 0x00010003u;

// The three lookup helpers the rest of Server.dll calls. Retail has no equivalents -- its
// call sites inline the `[.data:Players]` walk -- so they keep port-native names.
// PlayerDB_GetPlayer is null for an out-of-range session AND for a slot that is not logged
// in (PlayerDB::is_valid, 0x06ce1a00: client_id != 0).
PlayerSlot* __cdecl PlayerDB_GetPlayer(unsigned int session_id) {
    if (!Players.is_valid(session_id)) {
        return nullptr;
    }
    return &Players[session_id];
}

unsigned int __cdecl PlayerDB_GetMaxPlayers() {
    return Players.GetMaxPlayerCount();
}

bool __cdecl PlayerDB_AllowsNewPlayers() {
    return Players.m_bAllowNewPlayers;   // friend of PlayerDB; [Players + 0x0c]
}

// PlayerDB::PlayerDB(const PlayerDB&) -- retail RVA 0x1AE0, ordinal 14; exported but never
// called inside Server.dll. A SHALLOW copy of the slot array; the CPlayerAccount's hash
// pointers are shared (its own copy ctor does that); MOTD and both containers deep-copied.
PlayerDB::PlayerDB(const PlayerDB& Other)
    : m_ServerAccount(Other.m_ServerAccount) {
    m_PlayerArray = Other.m_PlayerArray;
    m_MaxPlayers = Other.m_MaxPlayers;
    m_ServerID = Other.m_ServerID;
    m_bAllowNewPlayers = Other.m_bAllowNewPlayers;
    m_PlayerArrayEnd = Other.m_PlayerArrayEnd;
    m_MOTD = Other.m_MOTD;
    m_CharacterIDMap = Other.m_CharacterIDMap;
    m_AccountNameMap = Other.m_AccountNameMap;
}

// PlayerDB::operator= -- retail RVA 0x1C50, ordinal 31; exported but never called
// internally. Retail does NOT free the existing slot array; it overwrites the pointers,
// then assigns the rest under a self-assignment check.
PlayerDB& PlayerDB::operator=(const PlayerDB& Other) {
    m_PlayerArray = Other.m_PlayerArray;
    m_MaxPlayers = Other.m_MaxPlayers;
    m_ServerID = Other.m_ServerID;
    m_bAllowNewPlayers = Other.m_bAllowNewPlayers;
    m_PlayerArrayEnd = Other.m_PlayerArrayEnd;
    if (this != &Other) {
        // Copies the two hash pointers, never the vptr.
        m_ServerAccount = Other.m_ServerAccount;
        m_MOTD = Other.m_MOTD;
        m_CharacterIDMap = Other.m_CharacterIDMap;
        m_AccountNameMap = Other.m_AccountNameMap;
    }
    return *this;
}

// FUN_06d18880 -- the UNDOCK: BaseDB::exit_location / unload_base, then current_base and
// current_location zeroed on both paths.
void PlayerDB_LeaveBase(PlayerData& Slot, unsigned int SessionId) {
    if ((Slot.current_base & 0x3fffffff) != 0) {
        if ((Slot.current_location & 0x3fffffff) != 0) {
            GetBaseDB()->exit_location(SessionId, Slot.current_location);
        }
        GetBaseDB()->unload_base(SessionId, Slot.current_base);
    }
    Slot.current_base = 0;
    Slot.current_location = 0;
    // Retail does NOT touch +0x3F4 (entered_base_id) here.
}

unsigned int PlayerDB::GetMaxPlayerCount() {
    return m_MaxPlayers;
}

unsigned int PlayerDB::GetServerID() {
    return m_ServerID;
}

// PlayerDB::GetServerSig -- retail RVA 0x1AC0, ordinal 264: the signature-hash string of the
// CPlayerAccount at this+0x10.
const char* PlayerDB::GetServerSig() {
    return m_ServerAccount.GetAccountNameSigChar();
}

bool PlayerDB::is_valid(const unsigned int& Id) {
    // Retail (0x06ce1a00) has no test on the array pointer itself.
    if (Id == 0 || Id - 1 >= m_MaxPlayers) return false;
    // The client-id field doubles as the slot-occupied flag.
    return m_PlayerArray[Id - 1].client_id != 0;
}

// PlayerDB::is_valid_ship_owner -- retail RVA 0x1A40, ordinal 496: the FIRST argument is
// the player id, the SECOND the ship flid.
bool PlayerDB::is_valid_ship_owner(const unsigned int& PlayerId, const unsigned int& ShipFlid) {
    if (!is_valid(PlayerId)) return false;
    return m_PlayerArray[PlayerId - 1].ship_object_id == ShipFlid;
}

PlayerData& PlayerDB::operator[](const unsigned int& Id) {
    return m_PlayerArray[Id - 1];
}

unsigned int PlayerDB::to_index(unsigned int SessionId) {
    return (SessionId > 0) ? SessionId - 1 : 0;
}

std::wstring& PlayerDB::GetMOTD() {
    return m_MOTD;
}

// ===========================================================================
// CPlayerGroup class implementation
// ===========================================================================

// .data:06d90400, the protected static CPlayerGroup::s_GroupIDToGroupPtrMap (export ordinal
// 508).
CPlayerGroup::GroupMapType CPlayerGroup::s_GroupIDToGroupPtrMap;

// .data:06d90450: the ctor pre-increments and the dtor gives the id back when it was the
// last one handed out.
unsigned int CPlayerGroup::s_uiGroupID = 0;

// CPlayerGroup::CPlayerGroup -- retail 0x06d5b320, ordinal 7.
CPlayerGroup::CPlayerGroup() {
    m_GroupID = ++s_uiGroupID;
    s_GroupIDToGroupPtrMap[m_GroupID] = this;
}

// Copy ctor (0x06ce1100, ordinal 6) and operator= (0x06ce1280, ordinal 26): exported, never
// called inside Server.dll. The copy is NOT registered in s_GroupIDToGroupPtrMap.
CPlayerGroup::CPlayerGroup(const CPlayerGroup& Other) {
    m_GroupID = Other.m_GroupID;
    m_Members = Other.m_Members;
    m_Invites = Other.m_Invites;
}

CPlayerGroup& CPlayerGroup::operator=(const CPlayerGroup& Other) {
    if (this != &Other) {
        m_GroupID = Other.m_GroupID;
        m_Members = Other.m_Members;
        m_Invites = Other.m_Invites;
    }
    return *this;
}

// CPlayerGroup::~CPlayerGroup -- retail 0x06d5b440, ordinal 20.
CPlayerGroup::~CPlayerGroup() {
    if (m_GroupID == s_uiGroupID) {
        --s_uiGroupID;
    }
    s_GroupIDToGroupPtrMap.erase(m_GroupID);
}

// CPlayerGroup::FromGroupID -- retail 0x06d5b5c0, ordinal 138.
CPlayerGroup* CPlayerGroup::FromGroupID(unsigned int GroupId) {
    GroupMapType::iterator It = s_GroupIDToGroupPtrMap.find(GroupId);
    if (It != s_GroupIDToGroupPtrMap.end()) {
        return It->second;
    }
    return nullptr;
}

// CPlayerGroup::AddMember -- retail .text:06d5b610, ordinal 53. Every member and the joiner
// learn about each other (group message 6). The group's mission is the first member's
// non-zero +0x408 (and, in a separate scan, +0x40c); a joiner without a mission, or joining
// a group with one, takes the group's, otherwise the joiner's mission goes to every member.
// Then the member is added, the group id sent and stamped on the joiner's CShip, and the
// system told (StarSystem::send_group_change, FUN_06d0f1c0). No IsFull test (the callers do
// it) and no interface null tests. FUN_06d006c0 is Server_LookupGameObject here.
bool CPlayerGroup::AddMember(unsigned int PlayerId) {
    PlayerData& Joiner = Players[PlayerId];                                 // raw, no guard

    std::set<unsigned int>::const_iterator It;
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberJoined;    // 6
        Payload.Param = MakeChatId(PlayerId);
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberJoined;    // 6
        Payload.Param = MakeChatId(*It);
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(PlayerId)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }

    unsigned int GroupMission = 0;
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_id != 0) {
            GroupMission = Players[*It].group_mission_id;
            break;
        }
    }
    unsigned int GroupSetBy = 0;
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_set_by != 0) {
            GroupSetBy = Players[*It].group_mission_set_by;
            break;
        }
    }

    unsigned int const JoinerMission = Joiner.group_mission_id;
    if (JoinerMission == 0 || GroupMission != 0) {
        Joiner.group_mission_id = GroupMission;
        Joiner.group_mission_set_by = GroupSetBy;
        g_pClientInterface->SendServerMiscObjUpdateMission(PlayerId, GroupMission, GroupSetBy);
    } else {
        unsigned int const JoinerSetBy = Joiner.group_mission_set_by;
        for (It = m_Members.begin(); It != m_Members.end(); ++It) {
            PlayerData& Member = Players[*It];
            Member.group_mission_id = JoinerMission;
            Member.group_mission_set_by = JoinerSetBy;
            g_pClientInterface->SendServerMiscObjUpdateMission(*It, JoinerMission, JoinerSetBy);
        }
    }

    m_Members.insert(PlayerId);
    Players.SendGroupID(PlayerId, m_GroupID);
    Joiner.group_ptr = this;
    unsigned int JoinEvent = PlayerId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::GroupMemberJoined, &JoinEvent);

    // Stamp the group on the joiner's ship.
    GameObject* const ShipObj = Server_LookupGameObject(Joiner.ship_object_id);
    if (ShipObj == nullptr)
        return true;
    CObject const* const Cobj = ShipObj->cobject();
    if (Cobj == nullptr)
        return true;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return true;
    const_cast<CShip*>(static_cast<CShip const*>(Cobj))->GroupId = m_GroupID;
    SysDB::SysMapType::iterator System = SysDB::SysMap.find(Joiner.system_id);
    if (System != SysDB::SysMap.end())
        System->second.send_group_change(Joiner.ship_object_id);
    return true;
}

// CPlayerGroup::DelMember -- retail .text:06d5b920, ordinal 96. The controller event goes out
// BEFORE the erase; the remaining members get message 8, the leaver 7; the leaver's mission
// is cleared and an empty objectives record sent (the id BY ADDRESS); then the group id is
// cleared and unstamped from the leaver's CShip as in AddMember. No interface null tests.
bool CPlayerGroup::DelMember(unsigned int PlayerId) {
    std::set<unsigned int>::iterator Found = m_Members.find(PlayerId);
    if (Found == m_Members.end())
        return false;

    unsigned int LeaveEvent = PlayerId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::GroupMemberLeft, &LeaveEvent);
    m_Members.erase(Found);

    PlayerData& Leaver = Players[PlayerId];                                // raw, no guard

    std::set<unsigned int>::const_iterator It;
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberLeft;      // 8
        Payload.Param = MakeChatId(PlayerId);
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
    {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::RemovedFromGroup; // 7
        Payload.Param = MakeChatId(PlayerId);
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(PlayerId)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }

    Leaver.group_mission_id = 0;
    Leaver.group_mission_set_by = 0;
    g_pClientInterface->SendServerMiscObjUpdateMission(PlayerId, 0, 0);

    FServerSetMissionObjectivesInfo NoObjectives;
    NoObjectives.MissionId = 0;
    NoObjectives.Entries = nullptr;
    NoObjectives.Count = 0;
    NoObjectives.MissionType = 0;
    g_pClientInterface->SendServerSetMissionObjectives(PlayerId, NoObjectives);

    Players.SendGroupID(PlayerId, 0);
    Leaver.group_ptr = nullptr;

    // Clear the group on the leaver's ship.
    GameObject* const ShipObj = Server_LookupGameObject(Leaver.ship_object_id);
    if (ShipObj == nullptr)
        return true;
    CObject const* const Cobj = ShipObj->cobject();
    if (Cobj == nullptr)
        return true;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return true;
    const_cast<CShip*>(static_cast<CShip const*>(Cobj))->GroupId = 0;
    SysDB::SysMapType::iterator System = SysDB::SysMap.find(Leaver.system_id);
    if (System != SysDB::SysMap.end())
        System->second.send_group_change(Leaver.ship_object_id);
    return true;
}

// CPlayerGroup::IsMember -- retail 0x06d5bb90, ordinal 316.
bool CPlayerGroup::IsMember(unsigned int PlayerId) {
    return m_Members.find(PlayerId) != m_Members.end();
}

// CPlayerGroup::AddInvite -- retail 0x06d5bbf0, ordinal 52: always true, no invite cap.
bool CPlayerGroup::AddInvite(unsigned int PlayerId) {
    m_Invites.insert(PlayerId);
    return true;
}

// CPlayerGroup::DelInvite -- retail 0x06d5bc90, ordinal 95.
bool CPlayerGroup::DelInvite(unsigned int PlayerId) {
    std::set<unsigned int>::iterator It = m_Invites.find(PlayerId);
    if (It == m_Invites.end()) {
        return false;
    }
    m_Invites.erase(It);
    return true;
}

// CPlayerGroup::IsInvited -- retail 0x06d5bd00, ordinal 315.
bool CPlayerGroup::IsInvited(unsigned int PlayerId) {
    return m_Invites.find(PlayerId) != m_Invites.end();
}

// GetMemberCount (RVA 0x7bb80), GetInviteCount (0x7bcf0), GetID (0x10f0), IsFull (0x10e0).
// All four are QAE, i.e. NOT const (const would change the exported mangled name).
unsigned int CPlayerGroup::GetMemberCount() { return (unsigned int)m_Members.size(); }
unsigned int CPlayerGroup::GetInviteCount() { return (unsigned int)m_Invites.size(); }
unsigned int CPlayerGroup::GetID() { return m_GroupID; }
bool CPlayerGroup::IsFull() { return m_Members.size() >= 8; }

// CPlayerGroup::HandleClientLogout -- retail 0x06d5bd60, ordinal 299. An empty group deletes
// itself (scalar deleting destructor).
void CPlayerGroup::HandleClientLogout(unsigned int PlayerId) {
    std::set<unsigned int>::iterator It = m_Invites.find(PlayerId);
    if (It != m_Invites.end()) {
        m_Invites.erase(It);
    }
    DelMember(PlayerId);
    if (m_Members.empty()) {
        delete this;
    }
}

// CPlayerGroup::SendGroup -- retail 0x06d5bdc0, ordinal 402: FUN_06d190d0 per member, the
// 8-byte control block on the group-control channel. No null test on the IChatClient.
void CPlayerGroup::SendGroup(BaseGroupMessage::Type MsgType, unsigned int Param) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)MsgType;
        Payload.Param = Param;
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
}

// CPlayerGroup::SendChat -- retail .text:06d5be60 (cdecl, `this` on the stack,
// ?SendChat@CPlayerGroup@@QAAXHPBGZZ). The vswprintf is unbounded, as in retail. The first
// int is the StyleNode style FUN_06d18ea0 renders with, not a sender id.
void CPlayerGroup::SendChat(int Style, const wchar_t* Msg, ...) {
    wchar_t FormattedMsg[0x400];
    va_list Args;
    va_start(Args, Msg);
    _vswprintf(FormattedMsg, Msg, Args);
    va_end(Args);

    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        Server_SendStyledChat(static_cast<unsigned short>(Style), MakeChatId(*It),
                              L"%s", FormattedMsg);
    }
}

// CPlayerGroup::DeliverChat -- retail 0x06d5bf20. MsgType is the BYTE SIZE and MsgData the
// encoded RDL buffer (the one caller is SubmitChat's group branch). No null check on the
// IChatClient, which retail reloads per member.
void CPlayerGroup::DeliverChat(unsigned long MsgType, const void* MsgData) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GDeliverChatSourceId),
                                                  MsgType, MsgData);
    }
}

// CPlayerGroup::SetMissionObjectives -- retail 0x06d5bfd0: IClient slot 72
// (FLPACKET_SERVER_SETMISSIONOBJECTIVES) per member, the member id passed by reference to
// the set node's own value; no IClient null test. CMissionObjectives IS the 0x40-byte
// FServerSetMissionObjectivesInfo (rpc_iclient_args.h); common_server.h still declares it
// as a one-pointer placeholder, so the reference is reinterpreted.
void CPlayerGroup::SetMissionObjectives(CMissionObjectives& Objs) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pClientInterface->SendServerSetMissionObjectives(
            *It, reinterpret_cast<const FServerSetMissionObjectivesInfo&>(Objs));
    }
}

// CPlayerGroup::SetMissionMessage -- retail 0x06d5c050: IClient slot 70
// (FLPACKET_SERVER_SET_MISSION_MESSAGE), same shape as SetMissionObjectives.
void CPlayerGroup::SetMissionMessage(CSetMissionMessage& Msg) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pClientInterface->SendServerSetMissionMessage(*It, Msg);
    }
}

// CPlayerGroup::StoreMemberList -- retail 0x06d5c0d0, ordinal 455. Does NOT clear the vector
// first.
void CPlayerGroup::StoreMemberList(std::vector<unsigned int>& OutMembers) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        OutMembers.push_back(*It);
    }
}

// CPlayerGroup::RewardMembers -- retail .text:06d5c2b0, ordinal 389: PlayerData::adjust_cash
// (bCountEarned = true) on every member, indexed raw.
void CPlayerGroup::RewardMembers(int Amount) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        Players[*It].adjust_cash(Amount, true);
    }
}

// CPlayerGroup::GetMissionID (0x06d5c330, ordinal 216) / GetMissionSetBy (0x06d5c3b0,
// ordinal 217): the mission is not stored on the group; both return the first member's
// non-zero +0x408 / +0x40c, indexing Players raw.
unsigned int CPlayerGroup::GetMissionID() {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_id != 0) {
            return Players[*It].group_mission_id;
        }
    }
    return 0;
}

unsigned int CPlayerGroup::GetMissionSetBy() {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_set_by != 0) {
            return Players[*It].group_mission_set_by;
        }
    }
    return 0;
}

// CPlayerGroup::SetMissionID -- retail 0x06d5c430, ordinal 423: every member's mission, then
// IClient slot 114. Raw Players index, no interface null test.
void CPlayerGroup::SetMissionID(unsigned int MissionId, unsigned int SetBy) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        PlayerData& Player = Players[*It];
        Player.group_mission_id = MissionId;
        Player.group_mission_set_by = SetBy;
        g_pClientInterface->SendServerMiscObjUpdateMission(*It, MissionId, SetBy);
    }
}

// CPlayerGroup::GetMember -- retail 0x06d5c4c0, ordinal 213; the one accessor retail declares
// const (QBE).
unsigned int CPlayerGroup::GetMember(int Index) const {
    if (Index < 0 || (unsigned int)Index >= m_Members.size()) {
        return 0;
    }
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (int i = 0; i < Index; ++i) {
        ++It;
    }
    return *It;
}

// ===========================================================================
// CAccount class implementation
// ===========================================================================

// CAccount::CAccount -- retail .text:06d567d0, ordinal 4.
CAccount::CAccount()
    : m_Name()
    , m_DirectoryPath()
    , m_Characters()
    , m_bLocked(false)
    , m_bNotBanned(true)
    , m_bAdmin(false)
    , m_ClientID(0) {
}

// Retail .text:06ce1620: member-wise copy in declaration order.
CAccount::CAccount(const CAccount& Other)
    : m_Name(Other.m_Name)
    , m_DirectoryPath(Other.m_DirectoryPath)
    , m_Characters(Other.m_Characters)
    , m_bLocked(Other.m_bLocked)
    , m_bNotBanned(Other.m_bNotBanned)
    , m_bAdmin(Other.m_bAdmin)
    , m_ClientID(Other.m_ClientID) {
}

CAccount::~CAccount() {
}

// Retail .text:06ce1740. No whole-object `this != &Other` test: each member's own
// assignment handles aliasing, then the scalars are copied unconditionally.
CAccount& CAccount::operator=(const CAccount& Other) {
    m_Name = Other.m_Name;
    m_DirectoryPath = Other.m_DirectoryPath;
    m_Characters = Other.m_Characters;
    m_bLocked = Other.m_bLocked;
    m_bNotBanned = Other.m_bNotBanned;
    m_bAdmin = Other.m_bAdmin;
    m_ClientID = Other.m_ClientID;
    return *this;
}

// CAccount::InitFromFolder -- retail .text:06d56940 (thiscall): the name from the XOR-coded
// "name" file (constant key), then the "banned" / "admin" marker files; an absent marker
// leaves the ctor value. Retail does not store the folder: +0x14 is written only at login
// (FUN_06d48e80).
void CAccount::InitFromFolder(const char* FolderPath) {
    char Path[260];
    _makepath(Path, nullptr, FolderPath, "name", nullptr);
    FILE* File = fopen(Path, "rb");
    if (File != nullptr) {
        unsigned short Raw[0x400];
        wchar_t Decoded[0x400 + 1];
        size_t const Count = fread(Raw, 2, 0x400, File);
        // Deviation: retail's decoded buffer sits directly below the raw one, so a full
        // 0x400-char read puts this terminator on raw[0]; the spare slot here avoids that
        // overrun.
        Decoded[Count] = L'\0';
        fclose(File);
        unsigned short const XorKey = AccountNameFileXorKey();
        for (size_t I = 0; I < Count; ++I)
            Decoded[I] = static_cast<wchar_t>(Raw[I] ^ XorKey);
        m_Name.assign(Decoded, wcslen(Decoded));
    }

    _makepath(Path, nullptr, FolderPath, "banned", nullptr);
    File = fopen(Path, "rb");
    if (File != nullptr) {
        m_bNotBanned = false;
        fclose(File);
    }

    _makepath(Path, nullptr, FolderPath, "admin", nullptr);
    File = fopen(Path, "rb");
    if (File != nullptr) {
        m_bAdmin = true;
        fclose(File);
    }
}

// CAccount::ForceLogout -- retail .text:06d56dd0, ordinal 134: IClient slot 17 with no null
// test on the interface; the stored id is not cleared.
void CAccount::ForceLogout() {
    if (m_ClientID == 0) {
        return;
    }
    extern IClient* g_pClientInterface;
    g_pClientInterface->DisconnectClient(m_ClientID);
}

// CAccount::AppendCharacterNames -- retail .text:06ce1050, ordinal 60: empty (ICF-folded).
void CAccount::AppendCharacterNames(std::list<CiWString>& Names) {
    (void)Names;
}

// CAccount::DeleteCharacterFromID -- retail .text:06d56bd0 (thiscall). The first list entry
// whose "<hash>.fl" matches CharId (ci_char_traits compare) has its save file unlinked
// and is erased.
void CAccount::DeleteCharacterFromID(CiString& CharId) {
    std::list<CiWString>::iterator It = m_Characters.begin();
    for (; It != m_Characters.end(); ++It) {
        char Hash[260];
        char FileName[260];
        // Retail's dir argument is FileName itself with its first byte zeroed, i.e. empty.
        Server_AccountDirName(Hash, It->c_str());
        _makepath(FileName, nullptr, "", Hash, "fl");
        _strlwr(Hash);
        if (CharId.compare(0, CharId.size(), FileName, strlen(FileName)) != 0)
            continue;

        char DirName[260];
        if (IsMPServer())
            Server_AccountDirName(DirName, m_Name.c_str());
        else
            wcstombs(DirName, m_Name.c_str(), 0x104);
        char AccountPath[260];
        _makepath(AccountPath, nullptr, Server_GetAccountsRoot(), DirName, nullptr);
        Server_AccountDirName(Hash, It->c_str());
        // Retail writes this into the same buffer it passes as dir; a second buffer gives
        // the same string without relying on _makepath's overlap behaviour.
        char FilePath[260];
        _makepath(FilePath, nullptr, AccountPath, Hash, "fl");
        _strlwr(Hash);
        _unlink(FilePath);
        m_Characters.erase(It);
        return;
    }
}

// CDeadReckonedVector -- declared once, in common_cobject.h. Server.dll's own copies of its
// members (copy ctor .text:06ce2020, operator= 06ce2180, CalcError 06ce1f40, GetSample
// 06ce1f70, NormalizeProjectedPosition 06ce1f80, NormalizeTrackedPosition 06ce1fd0, ??_7
// .rdata:06d64f10) are the header's inline bodies; this module defines nothing for it.

// CRemotePhysicsSimulation -- declared once in common_server.h; the class is Common.dll's.
// The eight members retail emits into every module (copy ctor ord 8, operator= ord 27,
// CalcPositionError 70, GetLatestUpdate 200, GetSample 258, GetSampleCount 259,
// GetSimulationTime 273, SetLogging 420, Server's own ??_7 ord 38) are inline in that header
// (SHARED_INLINE_API; Server's copies .text:06ce2380 .. 06ce29b0). The seven Common-only
// members resolve through Common.lib, as retail Server.dll imports them; their only bodies
// are in src/Common/common_physys.cpp. Do not add a body for any of them to this file.

// OwnerList<pub::SpaceObj::CargoDesc> -- the template's members are defined in
// common_server.h. This explicit instantiation makes Server.dll export the members retail's
// Server.dll exports: ??0 (ord 2), copy ??0 (ord 1), ??1 (ord 18), ??4 (ord 23), ??_7 (ord
// 34, .rdata:06d64f24) and free (ord 483). ??_7 is the one retail export this cannot emit.
OWNERLIST_CARGODESC_EXPORTS

// MD5Hash, IClient, IServerImpl, CDeadReckonedVector, pub::AI::ContentCallback: their
// Server.dll exports come from the declarations themselves (MD5HASH_API, RPC_ICLIENT_API,
// SERVER_API, SHARED_INLINE_API), the way retail's headers produced them. No linker
// /EXPORT aliases.

// ===========================================================================
// Retail Server.dll data exports
// ===========================================================================

// SysDB::SysMap -- RVA 0xADA2C, export ordinal 460: THE system table.
namespace SysDB {
    SysMapType SysMap;
}

// ObjectList::count_type -- FUN_06cf5130: the objects whose type bits intersect Mask (not a
// size read, not an equality test). Every StarSystem caller passes 0xffffffff, so in
// practice it counts nodes with ANY type bit. node->Data is dereferenced unguarded.
unsigned int ObjectList::count_type(unsigned int Mask) const
{
    unsigned int Count = 0;
    for (MetaNode<GameObject>* Node = Head; Node != nullptr; Node = Node->Next) {
        if ((Mask & Node->Data->get_type_bits()) != 0)
            ++Count;
    }
    return Count;
}

// ObjectList::collect_in_sphere -- FUN_06cf5020. A negative radius takes every matching
// object without a distance test. x87: the squared radius is stored as a float, the
// distance (dx*dx + dz*dz) + dy*dy stays at extended precision.
unsigned int ObjectList::collect_in_sphere(unsigned int* OutIds, unsigned int MaxCount,
                                           Vector const* Pos, float Radius,
                                           unsigned int TypeMask) const
{
    float const RadiusSq = static_cast<float>(static_cast<double>(Radius) * static_cast<double>(Radius));
    unsigned int Count = 0;
    if (MaxCount == 0u)
        return 0u;

    MetaNode<GameObject>* Node = nullptr;
    do
    {
        Node = (Node == nullptr) ? Head : Node->Next;
        if (Node == nullptr)
            break;

        GameObject* const Wrapper = Node->Data;
        if (Wrapper == nullptr)
            continue;
        CObject* const Raw = Wrapper->Object;
        if (Raw == nullptr || (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
            continue;
        CSimple* const Simple = static_cast<CSimple*>(Raw);
        if ((Simple->TypeBits & TypeMask) == 0u)
            continue;
        if ((TypeMask & OBJ_SHIP_MASK) != 0u && Wrapper->is_destroyed())
            continue;

        if (static_cast<double>(Radius) < 0.0)
        {
            OutIds[Count] = Simple->Id;
            ++Count;
            continue;
        }

        double const Dx = static_cast<double>(Pos->x) - static_cast<double>(Simple->WorldPosition.x);
        double const Dy = static_cast<double>(Pos->y) - static_cast<double>(Simple->WorldPosition.y);
        double const Dz = static_cast<double>(Pos->z) - static_cast<double>(Simple->WorldPosition.z);
        double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
        if (DistSq <= static_cast<double>(RadiusSq))
        {
            OutIds[Count] = Simple->Id;
            ++Count;
        }
    } while (Count < MaxCount);
    return Count;
}

// ObjectList::find_by_id -- FUN_06cf4f00: the first node whose get_id() matches, or null.
MetaNode<GameObject>* ObjectList::find_by_id(unsigned int Id) const
{
    MetaNode<GameObject>* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data->get_id() == Id)
            break;
    }
    return Node;
}
