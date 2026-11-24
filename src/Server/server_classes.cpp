// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_net.h"
#include "../include/common_physys.h"
#include "../include/common_ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
#include "../include/common_cequip.h" // TractorArm
#include "../include/common_casteroid.h" // CmnAsteroid::CAsteroidSystem (SrvAsteroidSystem AddRef/Release/get_sys_id)
#include "../include/common_id.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_savegame.h" // PlayerData_CreateNewCharacter (retail FUN_06d4b790)
#include "../include/server_savefile.h" // Server_AccountDirName / Server_GetAccountsRoot (FUN_06d49f60)
#include "../include/common_ini.h"
#include "../include/common_misc.h"
#include "../include/x86math.h" // I3DMathEngine
#include "../include/common_universe.h"
#include "../include/common_reputation.h"
#include "../include/server_sysdb.h"
#include "../include/server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "../include/common_cobjdb.h"  // IObjDB::GetZoneDb (SrvAsteroid_UpdateSystem, FUN_06d03480)
#include "../include/DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "../include/readfile.h"       // IFileSystem::GetFileSysPath (+0x84), create_restart_file
#include "../include/server_createinfo.h"
#include "../include/server_characterset.h" // CCharacterSet (FUN_06d499c0)
#include "../include/common_goods.h"    // AmbientScriptProperties (server_basedb.h uses it by value)
#include "../include/server_basedb.h"
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FServerSetReputationInfo / FPayloadServerActivateObject (add_player MP walk)
#include "../include/md5hash.h"      // MD5HASH_API: Server exports its implicit members like retail
#include <windows.h>
#include <mmsystem.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <new>          // placement new: Observer is constructed into a raw
                        // player-list node, as std::list's _Construct does
#include <io.h>
#include <direct.h>
#include <wchar.h>
#include <stdarg.h>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

// Forward declaration for IClient (defined in rpc_iclient.h)
class IClient;

// Retail FUN_06d43650(msgId, payload) is a controller BROADCAST (retail 0x06d44080);
// Server_ControllerDispatch (IServerImpl.cpp, common_server_controller.h) is that walk.

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

// FUN_06d19180: a CHAT_ID from a player/session id, bytes 2 and 3 zero.
static unsigned int MakeChatId(unsigned int Id) { return Id & 0xffff; }

// The 8-byte block FUN_06d190d0 sends through IChatClient::SendCommonChatMsg: the message
// type and its ushort argument zero-extended.
struct FGroupMsgPayload {
    unsigned int MsgType;
    unsigned int Param;
};

// FUN_06d19110 (cdecl): the control block to every active slot, through the IChatClient
// with no null test. The ushort param is read once, before the loop.
void Server_BroadcastGroupControl(unsigned int MsgType, unsigned int Param) {
    PlayerData* Slot = Players.traverse_active(nullptr);
    if (Slot == nullptr)
        return;
    unsigned int const Word = Param & 0xffff;
    do {
        unsigned int const Target = MakeChatId(Slot->client_id);
        FGroupMsgPayload Payload;
        Payload.MsgType = MsgType;
        Payload.Param = Word;
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(Target), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
        Slot = Players.traverse_active(Slot);
    } while (Slot != nullptr);
}

// ===========================================================================
// PlayerDB class implementation
// ===========================================================================

// The one PlayerDB, `?Players@@3VPlayerDB@@A` (ordinal 352, .data:06d90260, CRT initializer
// 0x06d50b10), defined near the bottom of this file and declared in common_server.h.

// PlayerData::PlayerData -- retail FUN_06d494f0, run by PlayerDB::init's __vec_new helper
// FUN_06d601a8. Retail leaves as operator new returned: AccountName (+0x00),
// money_needed_to_next_rank (+0x288), both costumes' accessory ids, cash (+0x31c), the
// three mission counters (+0x334..+0x33c), bSkipNextAutosave (+0x340), interface_state
// (+0x39c), system_id (+0x3e0), current_base (+0x3ec), last_base_id (+0x3f0),
// current_location (+0x3f8), Field_0x3FC and exited_base_id (+0x410). The port zeroes them
// so a fresh slot is deterministic (as FSavedCharacter's constructor does).
PlayerData::PlayerData()
    : current_location(0), current_base(0), cash(0), system_id(0), ship_archetype(0),
      entered_base_id(0), equipment(), collision_groups(), equip_snapshot_arch(0),
      equip_snapshot(), voice_length(0), rank_level(0), money_needed_to_next_rank(0),
      difficulty_scale(1.0f), num_kills(0), num_mission_successes(0),
      num_mission_failures(0), rep_vibe(-1), relative_health(0.0f), group_ptr(nullptr),
      group_mission_id(0), group_mission_set_by(0), total_earned(0), client_id(0),
      bCheatReported(0), bSkipNextAutosave(0), save_failure_count(0), starting_ring(0),
      selected_character(), weapon_group_name(), interface_state(0), mission_log(),
      visit_map(), visited_states(), equip_id_maker(), trade_offer(nullptr),
      trade_request_partner(0), inbound_trade_requests(), ship_object_id(0),
      last_base_id(0), account_ptr(nullptr), saved_characters(), created_ship_id(0),
      character_id(0), exited_base_id(0) {
    for (int Part = 0; Part < 4; ++Part) {
        com_costume.PartIds[Part] = 0;
        costume.PartIds[Part] = 0;
    }
    com_costume.AccessoryCount = 0;
    costume.AccessoryCount = 0;
    voice[0] = '\0';
    // Not written by retail: zeroed for determinism only.
    for (int Accessory = 0; Accessory < 8; ++Accessory) {
        com_costume.AccessoryIds[Accessory] = 0;
        costume.AccessoryIds[Accessory] = 0;
    }
    for (size_t Index = 1; Index < sizeof(voice); ++Index)
        voice[Index] = '\0';
    for (size_t Index = 0; Index < sizeof(AccountName) / sizeof(AccountName[0]); ++Index)
        AccountName[Index] = L'\0';
    // Zero Vector, identity 3x3.
    for (int Axis = 0; Axis < 3; ++Axis)
        initial_pos[Axis] = 0.0f;
    for (int Cell = 0; Cell < 9; ++Cell)
        initial_ornt[Cell] = (Cell % 4 == 0) ? 1.0f : 0.0f;
}

// PlayerData::~PlayerData -- retail FUN_06d50e90: the slot teardown FUN_06d49230, then the
// member destruction.
static void PlayerData_Teardown(PlayerData& Slot);

PlayerData::~PlayerData() {
    PlayerData_Teardown(*this);
}

// CiWString and std::wstring differ only in their traits type, so the few helpers that
// take a std::wstring get a copy.
static std::wstring CiToWide(const CiWString& Str) {
    return std::wstring(Str.c_str(), Str.size());
}

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

// PlayerDB::PlayerDB -- retail RVA 0x70B10, ordinal 15. ServerID starts at ZERO.
PlayerDB::PlayerDB() {
    m_PlayerArray = nullptr;
    m_MaxPlayers = 0;
    m_ServerID = 0;
    m_bAllowNewPlayers = false;
    m_PlayerArrayEnd = nullptr;
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

// PlayerDB::~PlayerDB -- retail RVA 0x70C30, ordinal 21.
PlayerDB::~PlayerDB() {
    free();

    // The name map owns its CAccount objects.
    std::map<CiWString, CAccount*>::iterator It = m_AccountNameMap.begin();
    for (; It != m_AccountNameMap.end(); ++It) {
        delete It->second;
        It->second = nullptr;
    }
    m_AccountNameMap.clear();
    // The character-id map at this+0x30 only borrows the same pointers.
    m_CharacterIDMap.clear();
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

// Retail keeps the accounts root in the global char buffer at DAT_06d90158.
static char GAccountRootDir[260] = { 0 };

// Server-account bootstrap, part of retail PlayerDB::init (0x06d510c0). Builds
// <UserData>\Accts[\MultiPlayer], then for an MP server opens <root>\accounts.cfg,
// creating it when missing: two random dwords, then the 0x24-byte account-name and
// account-signature hashes of a fresh "SERVER" account. On read-back the server id is
// re-derived by sscanf("%x") from the *signature* string (retail discards the dword it
// wrote); a failing verify unlinks the file and retries.
static void InitServerAccount(CPlayerAccount& ServerAccount, unsigned int& OutServerID) {
    char UserDataPath[260];
    GetUserDataPath(UserDataPath);
    const char* SubDir = IsMPServer() ? "Accts\\MultiPlayer" : "Accts";
    _makepath_s(GAccountRootDir, sizeof(GAccountRootDir), nullptr, UserDataPath, SubDir, nullptr);

    if (!IsMPServer()) {
        return;
    }

    BuildDirectory(GAccountRootDir);

    char CfgPath[260];
    _makepath_s(CfgPath, sizeof(CfgPath), nullptr, GAccountRootDir, "accounts", "cfg");

    // Deviation: retail loops forever here; the port bounds the retry count.
    for (int Attempt = 0; Attempt < 4; ++Attempt) {
        FILE* File = nullptr;
        if (fopen_s(&File, CfgPath, "rb") != 0 || File == nullptr) {
            if (fopen_s(&File, CfgPath, "wb") != 0 || File == nullptr) {
                return;
            }
            srand(timeGetTime());
            unsigned int Salt = ((unsigned int)rand() << 16) | ((unsigned int)rand() & 0xffff);
            unsigned int IdA = ((unsigned int)rand() << 16) | ((unsigned int)rand() & 0xffff);
            unsigned int IdB = ((unsigned int)rand() << 16) | ((unsigned int)rand() & 0xffff);
            OutServerID = IdB ^ IdA;
            fwrite(&Salt, 4, 1, File);
            fwrite(&OutServerID, 4, 1, File);
            ServerAccount.GenerateAccount("SERVER");
            const char* NameHash = ServerAccount.GetAccountNameChar();
            const char* SigHash = ServerAccount.GetAccountNameSigChar();
            fwrite(NameHash, 0x24, 1, File);
            fwrite(SigHash, 0x24, 1, File);
            fclose(File);
            continue;
        }

        unsigned int SaltIn = 0;
        unsigned int IdIn = 0;
        char NameHash[0x25];
        char SigHash[0x25];
        memset(NameHash, 0, sizeof(NameHash));
        memset(SigHash, 0, sizeof(SigHash));
        fread(&SaltIn, 4, 1, File);
        fread(&IdIn, 4, 1, File);
        fread(NameHash, 0x24, 1, File);
        fread(SigHash, 0x24, 1, File);
        fclose(File);

        // The server id is the leading hex run of the signature hash, not the dword
        // stored above.
        sscanf_s(SigHash, "%x", &OutServerID);
        ServerAccount.LoadAccountFromStrings(NameHash, SigHash);
        if (ServerAccount.VerifyAccount()) {
            return;
        }
        _unlink(CfgPath);
    }
}

// GCheatMessage / LoadCheatMessage -- retail .data:06d8d160 and FUN_06d46150, the last
// thing PlayerDB::init does. MP only: <exe dir>\CheatMessage.txt is read as raw UTF-16
// (at most 0x50 wchar) over the default L"Possible cheating detected: ". The consumer is
// PlayerData_ReportCheat below.
static wchar_t GCheatMessage[0x50] = L"Possible cheating detected: ";

static void LoadCheatMessage() {
    if (SinglePlayer()) {
        return;
    }

    char ModulePath[260];
    GetModuleFileNameA(nullptr, ModulePath, sizeof(ModulePath));

    char Drive[4], Dir[260];
    _splitpath(ModulePath, Drive, Dir, nullptr, nullptr);

    char MessagePath[260];
    _makepath(MessagePath, Drive, Dir, "CheatMessage", "txt");

    FILE* File = fopen(MessagePath, "rb");
    if (File == nullptr) {
        return;
    }

    wchar_t Buffer[0x50];
    size_t Count = fread(Buffer, 2, 0x50, File);
    if (Count != 0) {
        memcpy(GCheatMessage, Buffer, Count * 2);
        if (Count < 0x4f) {
            GCheatMessage[Count] = 0;
            fclose(File);
            return;
        }
        GCheatMessage[0x4f] = 0;
    }
    fclose(File);
}

// PlayerData_ReportCheat -- retail FUN_06d4f570; its only caller is the gate
// PlayerData_CheckForCheats (FUN_06d4ccd0, latch +0x34C, gated on the current base).
//  (1) FDUMP "LOG: Possible cheating detected (%s) by %s (%s)!" with the reason, the
//      character name (from the vibe, fallback L">>>BAD   NAME<<<", three spaces) and the
//      slot's AccountName, both as NARROW copies built with a truncating (char) cast.
//  (2) StyleNode(0xd681) + TextNode(GCheatMessage) + TextNode(characterName).
//  (3) On a successful encode, IServerImpl::SubmitChat(CHAT_ID(0), Size, buffer,
//      DAT_06d902c8 = 0x00010000, -1); the `unsigned long` is the BYTE COUNT.
//  (4) A line appended to <UserDataPath>\Accts[\MultiPlayer]\cheaters.log.
namespace {
    // .data:06d8d430, five entries.
    const char* const GCheatReason[5] = {
        "None",
        "Ship Related",
        "Ship or Equipment not sold on base",
        "Equipment exceeds ship specs",
        "Ship capacity exceeded"
    };

    // .data:06d8fb38, the 0x200-byte scratch the writer fills and SubmitChat sends.
    unsigned char GCheatChatBuffer[0x200];

    // .data:06d902c8, set once by FUN_06d46020.
    const unsigned int GCheatChatTargetId = 0x00010000u;

    // Retail's narrow-copy loop: `append(1, (char)wideChar)` per character, no null test.
    std::string NarrowCopy(const wchar_t* Text) {
        std::string Out;
        while (*Text != L'\0') {
            Out.append(1, static_cast<char>(*Text));
            ++Text;
        }
        return Out;
    }

    const wchar_t GCheatBadName[] = L">>>BAD   NAME<<<";
}

// Server.dll's own RDL append (FUN_06d1b1b0). Retail clones the node, inserts it at the
// tail (the insert clones AGAIN, FUN_06d1b4a0) and deletes the first clone; the net effect
// is one clone at the tail, which this does with a single Clone. No null or allocation
// tests, as in retail. Common.dll keeps its own copy, as retail does. Shared with
// server_chat.cpp.
void Server_RdlAppend(RenderDisplayList& Rdl, const RDLNode& Node) {
    RDLNode* Copy = Node.Clone();
    void** Sentinel = (void**)Rdl.NodeList;
    void** Tail = static_cast<void**>(Sentinel[1]);
    void** Entry = static_cast<void**>(malloc(0xC));
    Entry[0] = Sentinel;   // next
    Entry[1] = Tail;       // prev
    Entry[2] = Copy;
    Sentinel[1] = Entry;
    Tail[0] = Entry;
    Rdl.Count++;
}

void PlayerData_ReportCheat(PlayerData& Slot, int Reason) {
    // (1) resolve the character name off the reputation vibe.
    const wchar_t* CharacterNameWide = GCheatBadName;
    {
        FmtStr NameA;
        FmtStr NameB;
        const wchar_t* Resolved = GCheatBadName;
        int Result = ::Reputation::Vibe::GetName(Slot.rep_vibe, NameA, NameB, Resolved);
        // Retail only re-forces the fallback on -2; any other failure code
        // leaves whatever GetName wrote into the out-parameter in place.
        CharacterNameWide = (Result == -2) ? GCheatBadName : Resolved;
    }

    const std::string CharacterName = NarrowCopy(CharacterNameWide);
    const std::string AccountName = NarrowCopy(Slot.AccountName);

    // Retail indexes the reason table with no range clamp.
    const char* ReasonText = GCheatReason[Reason];

    FDUMP(0x100001, "LOG: Possible cheating detected (%s) by %s (%s)!",
          ReasonText,
          CharacterName.empty() ? "" : CharacterName.c_str(),
          AccountName.empty() ? "" : AccountName.c_str());

    // (2) build the rich-text message.
    RenderDisplayList Rdl;
    {
        StyleNode Style(static_cast<unsigned short>(0xd681));
        Server_RdlAppend(Rdl, Style);
    }
    {
        TextNode Prefix(GCheatMessage, -1);
        Server_RdlAppend(Rdl, Prefix);
    }
    {
        // Retail does NOT reuse the name from (1): it calls GetName again.
        wchar_t const* NodeName = GCheatBadName;
        {
            FmtStr NameC;
            FmtStr NameD;
            int const Result = ::Reputation::Vibe::GetName(Slot.rep_vibe, NameC, NameD, NodeName);
            if (Result == -2)
                NodeName = GCheatBadName;
        }
        TextNode Name(NodeName, -1);
        Server_RdlAppend(Rdl, Name);
    }

    // (3) serialize and submit.
    {
        BinaryRDLWriter Writer;
        unsigned int Size = 0;
        if (Writer.write_buffer(Rdl, reinterpret_cast<char*>(GCheatChatBuffer),
                                sizeof(GCheatChatBuffer), Size)) {
            // Server + 4, a static object: no null test.
            IChatServer* Chat = static_cast<IChatServer*>(GetChatServerInterface());
            Chat->SubmitChat(CHAT_ID(0), Size, GCheatChatBuffer,
                             CHAT_ID(GCheatChatTargetId), -1);
        }
    }

    // (4) append to cheaters.log.
    char UserDataPath[260] = {0};
    GetUserDataPath(UserDataPath);

    char AcctDir[260] = {0};
    _makepath(AcctDir, nullptr, UserDataPath,
              IsMPServer() ? "Accts\\MultiPlayer" : "Accts", nullptr);

    char LogPath[260] = {0};
    _makepath(LogPath, nullptr, AcctDir, "cheaters.log", nullptr);

    FILE* Log = fopen(LogPath, "a+");
    if (Log != nullptr) {
        SYSTEMTIME Now;
        GetLocalTime(&Now);
        fprintf(Log, "%d/%d/%d %.2d:%.2d:%.2d Possible cheating detected (",
                (unsigned int)Now.wMonth, (unsigned int)Now.wDay, (unsigned int)Now.wYear,
                (unsigned int)Now.wHour, (unsigned int)Now.wMinute, (unsigned int)Now.wSecond);
        fprintf(Log, "%s) by %s(%s)\n", ReasonText,
                CharacterName.empty() ? "" : CharacterName.c_str(),
                AccountName.empty() ? "" : AccountName.c_str());
        fclose(Log);
    }
}

// PlayerData_CheckForCheats -- the gate at the top of retail FUN_06d4ccd0 (the save/land
// record writer; only the gate is ported here). Outside single player and unless the
// +0x34C latch is set, a player in a base is checked by four detectors whose reason codes
// line up with GCheatReason[]:
//   3 "Equipment exceeds ship specs"       FUN_06d50120
//   2 "Ship or Equipment not sold on base" FUN_06d4fd20
//   1 "Ship Related"                       FUN_06d4faf0 (price of the whole EquipDescList
//                                          against total_earned)
//   4 "Ship capacity exceeded"             FUN_06d4faa0
// A hit reports, sets the latch and sends IClient slot +0x24 (client_id, 0). `true` means
// the caller must stop (latch already set, or a cheat just reported).
namespace {
    // Retail's cash ceiling, 999999999; every arithmetic path in FUN_06d4faf0 saturates at it.
    int const GCashCeiling = 0x3b9ac9ff;

    // FUN_06d50120 -- reason 3, "Equipment exceeds ship specs". Two 0-terminated chains of
    // HpAttachmentType written onto the stack: (fighter, elite, freighter) shield-special 10
    // down to 1, and (gun, turret) special 10 down to 1. An item's INDEX in the chain is its
    // rank; a SMALLER index is a better hardpoint. The hull's budget is the first chain entry
    // get_legal_hps accepts (none: the terminator's index, worse than anything). Cargo-bay
    // items and unknown archetypes are skipped; a shield generator ranks its hp type, a gun
    // the MAXIMUM of its hp types; beating the hull's rank is cheating.
    HpAttachmentType const* ShieldSpecialChain(unsigned int& OutCount) {
        // In store order.
        static HpAttachmentType const kChain[30] = {
            HpTypeFighterShieldSpecial10, HpTypeEliteShieldSpecial10, HpTypeFreighterShieldSpecial10,
            HpTypeFighterShieldSpecial9,  HpTypeEliteShieldSpecial9,  HpTypeFreighterShieldSpecial9,
            HpTypeFighterShieldSpecial8,  HpTypeEliteShieldSpecial8,  HpTypeFreighterShieldSpecial8,
            HpTypeFighterShieldSpecial7,  HpTypeEliteShieldSpecial7,  HpTypeFreighterShieldSpecial7,
            HpTypeFighterShieldSpecial6,  HpTypeEliteShieldSpecial6,  HpTypeFreighterShieldSpecial6,
            HpTypeFighterShieldSpecial5,  HpTypeEliteShieldSpecial5,  HpTypeFreighterShieldSpecial5,
            HpTypeFighterShieldSpecial4,  HpTypeEliteShieldSpecial4,  HpTypeFreighterShieldSpecial4,
            HpTypeFighterShieldSpecial3,  HpTypeEliteShieldSpecial3,  HpTypeFreighterShieldSpecial3,
            HpTypeFighterShieldSpecial2,  HpTypeEliteShieldSpecial2,  HpTypeFreighterShieldSpecial2,
            HpTypeFighterShieldSpecial1,  HpTypeEliteShieldSpecial1,  HpTypeFreighterShieldSpecial1};
        OutCount = 30;
        return kChain;
    }

    HpAttachmentType const* GunSpecialChain(unsigned int& OutCount) {
        // In store order.
        static HpAttachmentType const kChain[20] = {
            HpTypeGunSpecial10, HpTypeTurretSpecial10,
            HpTypeGunSpecial9,  HpTypeTurretSpecial9,
            HpTypeGunSpecial8,  HpTypeTurretSpecial8,
            HpTypeGunSpecial7,  HpTypeTurretSpecial7,
            HpTypeGunSpecial6,  HpTypeTurretSpecial6,
            HpTypeGunSpecial5,  HpTypeTurretSpecial5,
            HpTypeGunSpecial4,  HpTypeTurretSpecial4,
            HpTypeGunSpecial3,  HpTypeTurretSpecial3,
            HpTypeGunSpecial2,  HpTypeTurretSpecial2,
            HpTypeGunSpecial1,  HpTypeTurretSpecial1};
        OutCount = 20;
        return kChain;
    }

    // Index of Type in Chain, or Count when Type is not in it -- including Type == 0 ("the
    // hull has no special hardpoint of this family").
    unsigned int HpChainRank(HpAttachmentType const* Chain, unsigned int Count, HpAttachmentType Type) {
        for (unsigned int Index = 0; Index < Count; ++Index) {
            if (Chain[Index] == Type)
                return Index;
        }
        return Count;
    }

    // The first chain entry the hull declares.
    HpAttachmentType BestLegalHp(Archetype::Ship const& Hull, HpAttachmentType const* Chain,
                                 unsigned int Count) {
        for (unsigned int Index = 0; Index < Count; ++Index) {
            if (Hull.get_legal_hps(Chain[Index]) != nullptr)
                return Chain[Index];
        }
        return static_cast<HpAttachmentType>(0);
    }

    bool PlayerData_EquipmentExceedsShipSpecs(PlayerData const& Slot) {
        Archetype::Ship* Hull = Archetype::GetShip(Slot.ship_archetype);
        if (Hull == nullptr)
            return false;

        unsigned int ShieldCount = 0;
        HpAttachmentType const* ShieldChain = ShieldSpecialChain(ShieldCount);
        unsigned int GunCount = 0;
        HpAttachmentType const* GunChain = GunSpecialChain(GunCount);

        unsigned int const HullShieldRank =
            HpChainRank(ShieldChain, ShieldCount, BestLegalHp(*Hull, ShieldChain, ShieldCount));
        unsigned int const HullGunRank =
            HpChainRank(GunChain, GunCount, BestLegalHp(*Hull, GunChain, GunCount));

        CacheString const& CargoBayHp = EquipDesc::CARGO_BAY_HP_NAME;

        for (std::list<EquipDesc>::const_iterator ItemIt = Slot.equipment.Items.begin();
             ItemIt != Slot.equipment.Items.end(); ++ItemIt) {
            EquipDesc const& Item = *ItemIt;

            // Retail compares the interned Hardpoint POINTER.
            if (Item.Hardpoint == CargoBayHp.Value)
                continue;

            Archetype::Equipment* Equip = Archetype::GetEquipment(Item.ArchId);
            if (Equip == nullptr)
                continue;

            Archetype::AClassType const Class = Equip->get_class_type();

            if (Class == Archetype::AClassShieldGenerator) {
                unsigned int const Rank =
                    HpChainRank(ShieldChain, ShieldCount, Equip->get_hp_type());
                if (Rank < HullShieldRank)
                    return true;
            }

            // Retail re-reads the class here rather than using an else-if; same
            // observable behaviour.
            if (Class == Archetype::AClassGun) {
                Archetype::Gun const* GunArch = static_cast<Archetype::Gun const*>(Equip);
                HpAttachmentType Best = static_cast<HpAttachmentType>(0);
                int const Types = GunArch->get_number_of_hp_types();
                for (int TypeIndex = 0; TypeIndex < Types; ++TypeIndex) {
                    HpAttachmentType const Candidate = GunArch->get_hp_type_by_index(TypeIndex);
                    if (Candidate > Best)
                        Best = Candidate;
                }
                unsigned int const Rank = HpChainRank(GunChain, GunCount, Best);
                if (Rank < HullGunRank)
                    return true;
            }
        }

        return false;
    }

    // Retail's per-archetype tally, inlined three times in FUN_06d4fd20: a node only
    // contributes when its Count is > 0 and < 1000000.
    int CountArchInList(std::list<EquipDesc> const& Items, unsigned int ArchId) {
        int Total = 0;
        for (std::list<EquipDesc>::const_iterator It = Items.begin(); It != Items.end(); ++It) {
            if (It->ArchId != ArchId)
                continue;
            int const Count = It->Count;
            if (Count > 0 && Count < 0xf4240)
                Total += Count;
        }
        return Total;
    }

    // FUN_06d4fd20 -- reason 2, "Ship or Equipment not sold on base". The market map hangs
    // off FBaseEntry+0x04 -> BaseData+0x10.
    // Step 1 -- only when the ship archetype differs from the landing snapshot: the base
    // must sell a ship good (GoodCategoryShip) whose hull good is this archetype, else
    // cheat. On a hit retail copies that good's +0x94 addon list (the default/"low" slot;
    // "mid" goes to +0xa0, "high" to +0xac).
    // Step 2 -- copy the live EquipDescList; when the ship DID match the snapshot, drop
    // every item the snapshot already knew about.
    // Step 3 -- every survivor must have an archetype (else cheat) and either be sold here
    // (transaction type 0 or 2, the same test pub::Market::GetCommoditiesForSale applies)
    // or not exceed the snapshot's count -- unless the ship changed AND the new ship's addon
    // list contains it.
    bool PlayerData_EquipmentNotSoldOnBase(PlayerData const& Slot) {
        if (Slot.current_base == 0)
            return false;
        // Only find_base's result is tested (a miss FDUMPs inside it); the BaseData* at
        // entry+0x04 is read untested.
        FBaseEntry* Base = GetBaseDB()->find_base(Slot.current_base);
        if (Base == nullptr)
            return false;
        BaseData const& Data = *Base->Data;

        bool const bShipUnchanged = (Slot.equip_snapshot_arch == Slot.ship_archetype);

        // Stays empty when the ship did not change.
        std::list<EquipDesc> ShipAddons;

        if (!bShipUnchanged) {
            GoodInfo const* ShipGood = nullptr;
            for (std::map<unsigned int, MarketGoodInfo>::const_iterator It = Data.Market.begin();
                 It != Data.Market.end(); ++It) {
                if (It->second.Type != TransactionBuyAndSell &&
                    It->second.Type != TransactionSaleOnly)
                    continue;
                GoodInfo const* Good = GoodList::find_by_id(It->second.GoodId);
                if (Good == nullptr)
                    continue;
                if (Good->Category != (unsigned)GoodCategoryShip)
                    continue;
                // The hull good is read off find_by_id's result with no null test.
                GoodInfo const* Hull = GoodList::find_by_id(Good->HullGoodId);
                if (Hull->ShipArchId == Slot.ship_archetype) {
                    ShipGood = Good;
                    break;
                }
            }
            if (ShipGood == nullptr)
                return true;
            ShipAddons.assign(ShipGood->AddonLow.begin(), ShipGood->AddonLow.end());
        }

        EquipDescList Work(Slot.equipment);

        if (bShipUnchanged) {
            for (std::list<EquipDesc>::const_iterator It = Slot.equipment.Items.begin();
                 It != Slot.equipment.Items.end(); ++It) {
                unsigned short const Id = It->Id;
                if (Slot.equip_snapshot.find_equipment_item(Id) != nullptr)
                    Work.remove_equipment_item(Id, -1);
            }
        }

        for (std::list<EquipDesc>::const_iterator WorkIt = Work.Items.begin();
             WorkIt != Work.Items.end(); ++WorkIt) {
            unsigned int const ArchId = WorkIt->ArchId;

            if (Archetype::GetEquipment(ArchId) == nullptr)
                return true;

            unsigned int const GoodId = Arch2Good(ArchId);
            if (GoodId != 0) {
                std::map<unsigned int, MarketGoodInfo>::const_iterator It = Data.Market.find(GoodId);
                if (It != Data.Market.end() &&
                    (It->second.Type == TransactionBuyAndSell ||
                     It->second.Type == TransactionSaleOnly)) {
                    continue; // sold here
                }
            }

            int const Accounted = CountArchInList(Slot.equip_snapshot.Items, ArchId);
            int const OnBoard = CountArchInList(Slot.equipment.Items, ArchId);
            if (OnBoard <= Accounted)
                continue;

            // Ship unchanged: the surplus has no source at all.
            if (bShipUnchanged)
                return true;

            // Otherwise it has to come out of the new ship's addon list.
            if (CountArchInList(ShipAddons, ArchId) <= 0)
                return true;
        }

        return false;
    }

    // FUN_06d4faf0 -- the player's whole NET WORTH: cash plus every equipment item priced at
    // the current base (combinable: Count * (int)price; otherwise (int)(0.3f*price +
    // 0.4999989867f) * Count), plus 0.8 of the hull's price, every add saturating at
    // GCashCeiling. Worth more than total_earned is the cheat.
    // Retail inlines FUN_06d12cd0(BaseDB::find_base(base), good), which is exactly retail's
    // pub::Market::GetPrice (.text:06d18610), so calling GetPrice reproduces its data path:
    // the base's stored price ((float)(int)(Price * price_mult), computed at load) on a hit,
    // the nominal GoodInfo price on a miss.
    int PlayerData_TotalEquipmentValue(PlayerData const& Slot) {
        if (Slot.current_base == 0)
            return 0;

        int Total = Slot.cash;

        for (std::list<EquipDesc>::const_iterator ItemIt = Slot.equipment.Items.begin();
             ItemIt != Slot.equipment.Items.end(); ++ItemIt) {
            EquipDesc const& Item = *ItemIt;
            unsigned int const GoodId = Arch2Good(Item.ArchId);
            if (GoodId == 0)
                continue;

            float Price = 0.0f;
            pub::Market::GetPrice(Slot.current_base, GoodId, Price);

            int Value = 0;
            if (arch_is_combinable(Item.ArchId)) {
                Value = Item.Count * (int)Price;
            } else {
                // .rdata:06d6ae7c = 0x3e99999a and .rdata:06d6b87c = 0x3effffde.
                Value = (int)(0.3f * Price + 0.4999989867f) * Item.Count;
            }

            // Either side over the ceiling pins the running total AT the ceiling.
            if (Value > GCashCeiling || Total > GCashCeiling) {
                Total = GCashCeiling;
            } else {
                Total += Value;
            }
        }

        int HullValue = 0;
        if ((Slot.ship_archetype & 0x3fffffffu) != 0) {
            GoodInfoList* Goods = GoodList_get();
            GoodInfo const* ShipGood = (Goods != nullptr)
                ? Goods->find_by_ship_arch(Slot.ship_archetype)
                : nullptr;
            if (ShipGood != nullptr) {
                float HullPrice = 0.0f;
                // GoodInfo+0x48 is the good's OWN Id (the market map key,
                // BaseDataList::load_market_data); find_by_ship_arch returns the
                // `shiphull` good, which carries the price.
                pub::Market::GetPrice(Slot.current_base, ShipGood->Id, HullPrice);
                HullValue = (int)(0.8f * HullPrice);
                if (HullValue > GCashCeiling)
                    return GCashCeiling;
            }
        }

        if (Total > GCashCeiling)
            return GCashCeiling;
        return Total + HullValue;
    }

    // FUN_06d4faa0. The x87 compare makes the cheat strictly-greater.
    bool PlayerData_HoldOverCapacity(PlayerData const& Slot) {
        Archetype::Ship* Hull = Archetype::GetShip(Slot.ship_archetype);
        if (Hull == nullptr)
            return false;
        return Slot.equipment.get_cargo_space_occupied() > Hull->HoldSize;
    }
}

bool __cdecl PlayerData_CheckForCheats(PlayerData& Slot) {
    if (SinglePlayer())
        return false;

    // One report per session.
    if (Slot.bCheatReported == 1)
        return true;

    // The whole check needs a base to price against.
    if (Slot.current_base == 0)
        return false;

    int Reason = 0;
    if (PlayerData_EquipmentExceedsShipSpecs(Slot)) {
        Reason = 3;
    } else if (PlayerData_EquipmentNotSoldOnBase(Slot)) {
        Reason = 2;
    } else if (PlayerData_TotalEquipmentValue(Slot) > Slot.total_earned) {
        Reason = 1;
    } else if (PlayerData_HoldOverCapacity(Slot)) {
        Reason = 4;
    } else {
        return false;
    }

    PlayerData_ReportCheat(Slot, Reason);
    Slot.bCheatReported = 1;

    // IClient slot 9, PushToClient(client_id, NULL), with no null test on the interface.
    if (!SinglePlayer())
        ::g_pClientInterface->PushToClient(Slot.client_id, nullptr);
    return true;
}

// ===========================================================================
// PlayerData's equipment-list family -- retail FUN_06d4ec10 / FUN_06d4eeb0 / FUN_06d4efc0 /
// FUN_06d4f100 / FUN_06d4f170 / FUN_06d4fc30, thiscall members of PlayerData spelled as free
// functions (declared in common_server_playerdb.h). Four of the six open with the MP gate
// `!IsMPServer() || selected_character[0] != 0`.
// ===========================================================================
static bool PlayerData_EquipGateOpen(PlayerSlot const& Slot) {
    if (!IsMPServer())
        return true;
    return Slot.selected_character.is_valid();
}

// SubObjectID::EquipIdMaker::CreateEquipID (Common.dll 0xd4560) PRE-increments, so the
// first id after a Reset (0x21) is 0x22.
static unsigned short PlayerData_NextEquipId(PlayerSlot& Slot) {
    return Slot.equip_id_maker.CreateEquipID();
}

unsigned short __cdecl PlayerData_AddEquipment(PlayerSlot& Slot, unsigned int ArchId,
                                               CacheString const& Hardpoint, int Count,
                                               float Health, unsigned char bEquipped,
                                               bool bMission, bool bNotify) {
    if (!PlayerData_EquipGateOpen(Slot))
        return SUBOBJ_ID_NONE;

    unsigned short EquipId = 0;
    bool bCombine = false;

    if (arch_is_combinable(ArchId) && !bMission) {
        // Retail sets the combinable flag BEFORE it knows whether a stack was found, so a
        // miss still adds combinable; it just gets a fresh id.
        bCombine = true;
        EquipDesc const* Existing = Slot.equipment.find_matching_cargo(ArchId, 0, 1.0f);
        if (Existing != nullptr) {
            EquipId = Existing->Id;
        } else {
            EquipId = PlayerData_NextEquipId(Slot);
        }
    } else {
        EquipId = PlayerData_NextEquipId(Slot);
    }

    // The six setters, in retail's order; bMission is a plain byte store (06d4f0aa).
    EquipDesc Desc;
    Desc.set_arch_id(ArchId);
    Desc.set_id(EquipId);
    Desc.set_hardpoint(Hardpoint);
    Desc.set_count(Count);
    Desc.set_status(Health);
    Desc.set_equipped(bEquipped);
    Desc.bMission = bMission;

    int const Combined = Slot.equipment.add_equipment_item(Desc, bCombine);

    // FLPACKET_SERVER_SETADDITEM. The client id goes BY VALUE here.
    if (bNotify) {
        ::g_pClientInterface->SendServerSetAddItem(Slot.client_id, Combined, Desc);
    }

    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
    return EquipId;
}

void __cdecl PlayerData_RemoveEquipment(PlayerSlot& Slot, unsigned short Id, int Count,
                                        bool bNotify) {
    if (!PlayerData_EquipGateOpen(Slot))
        return;

    int const Result = Slot.equipment.remove_equipment_item(Id, Count);

    // FLPACKET_SERVER_SETREMOVEITEM.
    if (bNotify) {
        ::g_pClientInterface->SendServerSetRemoveItem(Slot.client_id, Result, Id, Count);
    }

    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// PlayerData_RemoveCargo -- retail FUN_06d4f260 (thiscall): find the matching cargo (a
// miss FDUMPs and returns), then FUN_06d4f100's body inlined.
void __cdecl PlayerData_RemoveCargo(PlayerSlot& Slot, unsigned int ArchId, int Count,
                                    float Health, bool bNotify) {
    EquipDesc const* Found = Slot.equipment.find_matching_cargo(ArchId, Count, Health);
    if (Found == nullptr) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xdea,
              "Tried to remove cargo player doesn't have!");
        return;
    }
    PlayerData_RemoveEquipment(Slot, Found->Id, Count, bNotify);
}

void __cdecl PlayerData_ModifyEquipment(PlayerSlot& Slot, unsigned short Id,
                                        CacheString const& Hardpoint, int Count,
                                        float Health, unsigned char bEquipped) {
    if (!PlayerData_EquipGateOpen(Slot))
        return;

    EquipDesc* Item = Slot.equipment.find_equipment_item(Id);
    if (Item == nullptr) {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: PlayerData::modify_item(): CLI(%d) ID(0x%X) NEW_STATUS(%f). "
              "Couldn't find item, expect strange inventory behavior",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xdd7, Slot.client_id,
              (unsigned int)Id, (double)Health);
        return;
    }

    // Four setters; no set_arch_id here.
    Item->set_hardpoint(Hardpoint);
    Item->set_count(Count);
    Item->set_status(Health);
    Item->set_equipped(bEquipped);

    // No IClient packet on this path, only the controller broadcast.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// PlayerData::set_relative_health -- retail FUN_06d4ed10 (thiscall, two stack arguments).
// +0x268 is relative_health and the first argument is a FLOAT: the arch-group damage
// walker (.text:06d0ba6a) FSTPs CSimple::get_relative_health straight into its slot.
void __cdecl PlayerData_SetRelativeHealth(PlayerSlot& Slot, float RelativeHealth,
                                          bool bNotify) {
    Slot.relative_health = RelativeHealth;
    // IClient slot 22; the health crosses as its raw bits.
    if (bNotify) {
        ::g_pClientInterface->SendServerSetHullStatus(Slot.client_id, RelativeHealth);
    }
}

// PlayerData::terminate_trade -- retail FUN_06d50af0 (thiscall, no arguments); four call
// sites, one of them StarSystem::on_object_destroyed.
void __cdecl PlayerData_TerminateTrade(PlayerSlot& Slot) {
    if (Slot.trade_offer == nullptr)
        return;
    GetServerInterface()->TerminateTrade(Slot.client_id, 0);
}

// PlayerData::cancel_trade_request -- retail FUN_06d50a40 (thiscall, ONE stack argument;
// Ghidra's `char unaff_BL` is an artifact). Tells the OUTBOUND partner (+0x3d0) the
// request is gone (IClient slot 108), optionally removes MY client id from the partner's
// inbound list, and clears +0x3d0.
void __cdecl PlayerData_CancelTradeRequest(PlayerSlot& Slot, bool bDropPartnerEntry) {
    if (Slot.trade_request_partner == 0)
        return;

    // MY ship object id, not the partner's.
    ::g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(Slot.trade_request_partner,
                                                                Slot.ship_object_id);

    if (bDropPartnerEntry) {
        PlayerSlot* const Partner = PlayerDB_GetPlayer(Slot.trade_request_partner);
        if (Partner != nullptr) {
            std::vector<unsigned int>& Requests = Partner->inbound_trade_requests;
            for (std::vector<unsigned int>::iterator It = Requests.begin();
                 It != Requests.end(); ) {
                if (*It == Slot.client_id)
                    It = Requests.erase(It);
                else
                    ++It;
            }
        }
    }

    Slot.trade_request_partner = 0;
}

// PlayerData::set_trade_request -- retail FUN_06d50920, called only from
// IServerImpl::RequestTrade. FUN_06d50a40(true) inlined, then -- only when the TARGET slot
// has a client (no range check on the id) -- MY client id appended to its inbound list,
// +0x3d0 = target and IClient slot 107.
void __cdecl PlayerData_SetTradeRequest(PlayerSlot& Slot, unsigned int TargetId) {
    PlayerData_CancelTradeRequest(Slot, true);

    PlayerData& Target = Players[TargetId];
    if (Target.client_id == 0)
        return;

    Target.inbound_trade_requests.push_back(Slot.client_id);
    Slot.trade_request_partner = TargetId;
    ::g_pClientInterface->SendCommonPlayerTradeRequestTrade(TargetId, Slot.ship_object_id);
}

// PlayerData::set_collision_groups -- retail FUN_06d4f320. The only writer of +0x26C other
// than set_ship_arch. No gate (its caller has one) and no controller broadcast.
void __cdecl PlayerData_SetCollisionGroups(PlayerSlot& Slot,
                                           std::list<CollisionGroupDesc> const& Groups) {
    if (&Slot.collision_groups == &Groups)
        return;
    Slot.collision_groups = Groups;
    ::g_pClientInterface->SendServerSetCollisionGroups(Slot.client_id, Slot.collision_groups);
}

// FUN_06d4fc30.
void __cdecl PlayerData_SnapshotEquipment(PlayerSlot& Slot) {
    Slot.equip_snapshot_arch = Slot.ship_archetype;
    Slot.equip_snapshot = Slot.equipment;
}

// PlayerData_ResetTotalEarned -- retail FUN_06d4f560. Sole caller: IServerImpl::BaseEnter,
// MP only, right before PlayerData_SnapshotEquipment.
void __cdecl PlayerData_ResetTotalEarned(PlayerSlot& Slot) {
    Slot.total_earned = PlayerData_TotalEquipmentValue(Slot);
}

// PlayerData_GetCash -- retail FUN_06d4f460: clamped on the way out, never written back.
int __cdecl PlayerData_GetCash(PlayerSlot const& Slot) {
    int Cash = Slot.cash;
    if (Cash < 0)
        return 0;
    if (Cash > 999999999)
        Cash = 999999999;
    return Cash;
}

// PlayerData_SetCash -- retail FUN_06d4f480 (thiscall); the MP selected-character gate,
// then clamp and store. Caller: IServerImpl::ReqSetCash.
void __cdecl PlayerData_SetCash(PlayerSlot& Slot, int Cash) {
    if (IsMPServer() && !Slot.selected_character.is_valid())
        return;
    if (Cash < 0)
        Cash = 0;
    else if (Cash > 999999999)
        Cash = 999999999;
    Slot.cash = Cash;
    ::g_pClientInterface->SendServerSetCash(Slot.client_id, Cash);
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// PlayerData_AdjustCash -- retail FUN_06d4f4e0 (thiscall). total_earned is bumped BEFORE
// the MP gate. pub::Player::AdjustCash (0x06d553c0) inlines it with true.
void __cdecl PlayerData_AdjustCash(PlayerSlot& Slot, int Amount, bool bCountEarned) {
    if (bCountEarned)
        Slot.total_earned += Amount;
    int NewCash = Slot.cash + Amount;
    if (IsMPServer() && !Slot.selected_character.is_valid())
        return;
    if (NewCash < 0)
        NewCash = 0;
    else if (NewCash > 999999999)
        NewCash = 999999999;
    Slot.cash = NewCash;
    ::g_pClientInterface->SendServerSetCash(Slot.client_id, NewCash);
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// PlayerData_CheckPositionBound -- retail FUN_06d498f0 (thiscall). Outside single player, a
// live ship whose squared distance from the origin is strictly greater than 750000^2
// (DAT_06d6b450; equal or unordered passes) is destroyed and false returned. Retail sums
// in x87 extended precision, the port in double; they can only disagree within a double
// ulp of 5.625e11. Sole caller: IServerImpl::SPObjUpdate.
bool __cdecl PlayerData_CheckPositionBound(PlayerSlot& Slot, Vector const* Position) {
    if (SinglePlayer())
        return true;
    GameObject* const ShipObj = Server_LookupLiveObject(Slot.ship_object_id);
    if (ShipObj == nullptr)
        return true;
    double const X = Position->x;
    double const Y = Position->y;
    double const Z = Position->z;
    double const DistSq = X * X + Y * Y + Z * Z;
    if (!(DistSq > 562500000000.0))
        return true;
    ShipObj->destroy(1, Slot.ship_object_id);
    return false;
}

// PlayerData_ReloadShipData -- retail FUN_06d4a200 (thiscall; the name is from the FDUMP
// text). In single player it FDUMPs and execution CONTINUES. Restores the base hull status
// and collision groups from the selected character's record (operator[], so a missing
// record is inserted default), keeps the record's mounted base equipment plus every
// unmounted shield battery / repair kit / munition / mine / countermeasure currently
// carried (no null test on the archetype), and writes that list back to the slot and to
// both of the record's lists.
void __cdecl PlayerData_ReloadShipData(PlayerSlot& Slot) {
    if (!IsMPServer()) {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0x70e,
              "reload_ship_data() is not valid in singleplayer\n");
    }
    Slot.current_base = Slot.last_base_id;

    PlayerData::FSavedCharacter& Record =
        Slot.saved_characters[Slot.selected_character];

    Slot.relative_health = Record.BaseHullStatus;
    Slot.collision_groups = Record.BaseCollisionGroups;

    std::list<EquipDesc> Kept;
    std::list<EquipDesc>::const_iterator Base;
    for (Base = Record.BaseEquipment.Items.begin(); Base != Record.BaseEquipment.Items.end();
         ++Base) {
        if (Base->bEquipped)
            Kept.push_back(*Base);
    }
    std::list<EquipDesc>::const_iterator Cur;
    for (Cur = Slot.equipment.Items.begin(); Cur != Slot.equipment.Items.end(); ++Cur) {
        if (Cur->bEquipped)
            continue;
        Archetype::AClassType const Class =
            Archetype::GetEquipment(Cur->ArchId)->get_class_type();
        if (Class == Archetype::AClassShieldBattery ||                     // 0x1b
            Class == Archetype::AClassRepairKit ||                         // 0x1a
            Class == Archetype::AClassMunition ||                          // 0x1f
            Class == Archetype::AClassMine ||                              // 0x20
            Class == Archetype::AClassCounterMeasure) {                    // 0x21
            Kept.push_back(*Cur);
        }
    }

    Slot.equipment.Items.clear();
    Slot.equipment.Items.assign(Kept.begin(), Kept.end());
    Record.BaseEquipment.Items = Slot.equipment.Items;
    Record.Equipment.Items = Slot.equipment.Items;
}

// FUN_06d4ec10. Slot+0x26c is the hull's undamaged collision group list
// (Archetype::EqObj::get_undamaged_collision_group_list, IAT .rdata:06d644a0).
void __cdecl PlayerData_SetShipArch(PlayerSlot& Slot, unsigned int ShipArch) {
    // A non-zero archetype that does not resolve aborts the whole call, notifications
    // included.
    if (ShipArch != 0 && Archetype::GetShip(ShipArch) == nullptr)
        return;

    // Only when the archetype changes: the list is emptied (even when the archetype will
    // not resolve below), then refilled.
    if (Slot.ship_archetype != ShipArch) {
        Slot.collision_groups.clear();

        // The low 30 bits, not the whole dword.
        if ((ShipArch & 0x3fffffffu) != 0) {
            Archetype::Ship* const Arch = Archetype::GetShip(ShipArch);
            if (Arch != nullptr) {
                Arch->get_undamaged_collision_group_list(Slot.collision_groups);
            } else {
                FDUMP(0x100001, "%s(%d) : *** ERROR: Error retrieving ship type %u",
                      "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xd07, ShipArch);
            }
        }
    }

    // The store and both notifications are unconditional.
    Slot.ship_archetype = ShipArch;

    // FLPACKET_SERVER_SETSHIPARCH; no null test on the interface.
    ::g_pClientInterface->SendServerSetShipArch(Slot.client_id, ShipArch);
    // Message 0x35 here, NOT the 0 every other member of this family broadcasts.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerShipArchSet, &Slot.client_id);
}

// FUN_06d4eeb0.
void __cdecl PlayerData_LoadEquipment(PlayerSlot& Slot, EquipDescList const& NewList) {
    if (!PlayerData_EquipGateOpen(Slot))
        return;

    Slot.equipment = NewList;

    // Every element that arrived carrying SUBOBJ_ID_NONE gets a fresh id.
    for (std::list<EquipDesc>::iterator It = Slot.equipment.Items.begin();
         It != Slot.equipment.Items.end(); ++It) {
        if (It->Id == SUBOBJ_ID_NONE) {
            It->set_id(PlayerData_NextEquipId(Slot));
        }
    }

    // FLPACKET_SERVER_SETEQUIPMENT.
    EquipDescVector Wire(Slot.equipment);
    ::g_pClientInterface->SendServerSetEquipment(Slot.client_id, Wire);
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// PlayerDB::init -- retail 0x06d510c0. The account/character directory scan is inline,
// as in retail, MP only: every "<root>\*" subdirectory (not starting with '.') becomes a
// CAccount (operator new(0x38), InitFromFolder) in the name map; every "<dir>\*.fl" save
// is registered lower-cased in the character-id map and its character NAME pushed onto the
// account's list. Map values are bare CAccount* (flags at CAccount+0x30..+0x32, see
// common_server.h).
void PlayerDB::init(unsigned int MaxPlayers, bool bAllowNewPlayers) {
    free();
    m_bAllowNewPlayers = bAllowNewPlayers;
    m_MaxPlayers = MaxPlayers;
    if (MaxPlayers > 0) {
        // Retail's count-prefixed operator new plus __vec_new (FUN_06d601a8) running
        // PlayerData::PlayerData (FUN_06d494f0); no per-slot wipe follows.
        m_PlayerArray = new PlayerData[MaxPlayers];
    }
    m_PlayerArrayEnd = m_PlayerArray ? (m_PlayerArray + MaxPlayers) : nullptr;
    InitServerAccount(m_ServerAccount, m_ServerID);
    if (IsMPServer() && GAccountRootDir[0] != 0) {
        char SearchPath[260];
        _makepath_s(SearchPath, sizeof(SearchPath), nullptr, GAccountRootDir, "*", nullptr);

        std::list<std::string> AccountDirs;
        struct _finddata_t DirFind;
        intptr_t DirHandle = _findfirst(SearchPath, &DirFind);
        if (DirHandle != -1) {
            do {
                // The '.' test is on the FIRST CHARACTER only.
                if (DirFind.name[0] == '.') continue;
                if ((DirFind.attrib & _A_SUBDIR) == 0) continue;
                AccountDirs.push_back(std::string(DirFind.name));
            } while (_findnext(DirHandle, &DirFind) == 0);
            _findclose(DirHandle);
        }

        std::list<std::string>::const_iterator DirIt = AccountDirs.begin();
        for (; DirIt != AccountDirs.end(); ++DirIt) {
            char AccountDir[260];
            _makepath_s(AccountDir, sizeof(AccountDir), nullptr, GAccountRootDir,
                        DirIt->c_str(), nullptr);

            CAccount* Account = new CAccount();
            Account->InitFromFolder(AccountDir);

            // PlayerDB owns these CAccount objects. Retail uses operator[] without testing
            // the insert, so a second directory with the same account name REPLACES the
            // first entry (the first CAccount is orphaned) and its *.fl files are still
            // registered below.
            m_AccountNameMap[Account->m_Name] = Account;

            // Every "<dir>\*.fl" save file.
            char FileSearch[260];
            _makepath_s(FileSearch, sizeof(FileSearch), nullptr, AccountDir, "*", "fl");

            struct _finddata_t FileFind;
            intptr_t FileHandle = _findfirst(FileSearch, &FileFind);
            if (FileHandle == -1) {
                continue;
            }
            do {
                char LowerName[260];
                strcpy_s(LowerName, sizeof(LowerName), FileFind.name);
                _strlwr(LowerName);

                m_CharacterIDMap[CiString(LowerName)] = Account;

                char SavePath[260];
                _makepath_s(SavePath, sizeof(SavePath), nullptr, AccountDir,
                            FileFind.name, nullptr);
                CiWString CharacterName;
                ReadCharacterName(SavePath, CharacterName);
                if (!CharacterName.empty()) {
                    // The NAME, not the file name (CAccount::DeleteCharacterFromID,
                    // 0x06d56bd0, hashes each entry back into "<hash>.fl").
                    Account->m_Characters.push_back(CharacterName);
                }
            } while (_findnext(FileHandle, &FileFind) == 0);
            _findclose(FileHandle);
        }
    }

    // The last call in PlayerDB::init; outside the IsMPServer() gate because it does its
    // own SinglePlayer() check.
    LoadCheatMessage();
}

void PlayerDB::free() {
    if (m_PlayerArray) {
        // Retail's vector destructor iterator (FUN_06d6001e with FUN_06d50e90), last slot to
        // first, then operator delete on array-4: delete[].
        delete[] m_PlayerArray;
        m_PlayerArray = nullptr;
    }
    m_PlayerArrayEnd = nullptr;
    m_MaxPlayers = 0;
}

// The counter block IServerImpl::GetServerStats copies out (.data:06d8f998); PlayerDB::logout
// bumps +0x08. Defined in IServerImpl.cpp.
extern ServerStats GServerStats;

// PlayerData_FlushPendingVisits -- retail FUN_06d4b450 (fastcall on the slot): commits the
// pending visit queue (+0x3b8) into the persistent visit map (+0x3a0), adding the
// "committed" bit 0x08 and ORing in the same key's existing flags (FUN_06d15d10 is find).
void __cdecl PlayerData_FlushPendingVisits(PlayerData& Slot) {
    for (size_t Index = 0; Index < Slot.visited_states.size(); ++Index) {
        const PlayerData::FVisitedState& Pending = Slot.visited_states[Index];
        unsigned char Flags = static_cast<unsigned char>(Pending.visit_flags | 0x08);

        std::map<unsigned int, unsigned char>::iterator Found =
            Slot.visit_map.find(Pending.system_id);
        if (Found != Slot.visit_map.end())
            Flags = static_cast<unsigned char>(Flags | Found->second);
        Slot.visit_map[Pending.system_id] = Flags;
    }
    Slot.visited_states.clear();
}

// PlayerData_ShouldQueueVisit -- retail FUN_06d4b200 (thiscall): false when the flags,
// ORed with the map's entry for Key, have both 0x10 and 0x08. Callers pass
// (solar+0xb0, CSolar::GetVisitValue()).
bool __cdecl PlayerData_ShouldQueueVisit(PlayerData const& Slot, unsigned int Key,
                                         unsigned char Flags) {
    std::map<unsigned int, unsigned char>::const_iterator Found = Slot.visit_map.find(Key);
    if (Found != Slot.visit_map.end())
        Flags = static_cast<unsigned char>(Flags | Found->second);
    if ((Flags & 0x10) != 0 && (Flags & 0x08) != 0)
        return false;
    return true;
}

// PlayerData_QueueVisit -- retail FUN_06d4b270 (thiscall): push_back onto the pending
// vector.
void __cdecl PlayerData_QueueVisit(PlayerData& Slot, unsigned int Key, unsigned char Flags) {
    PlayerData::FVisitedState State;
    State.system_id = Key;
    State.visit_flags = Flags;
    Slot.visited_states.push_back(State);
}

// The two teardown steps PlayerDB::cleanup and PlayerDB::logout share, retail FUN_06d00010
// (leave the system, via FUN_06d08890) and FUN_06d18880 (leave the base).
//
// FUN_06d00010 / FUN_06d08890: StarSystem::remove_player, flush the pending visits, event
// 0x3a when system_id was set, and in single player the last player out unloads the
// system (event 0x3d, StarSystem::unload).
//
// THE MISS ARM: on a SysMap miss retail runs FUN_06d08890 on the head node's value slot,
// which SysMap's constructor (FUN_06d104a0) never constructs, so it reads stale heap bytes
// (player-list head, ISystem*, loaded byte) and normally faults at .text:06d0874c. A miss
// needs a system id with no SysMap entry: +0x3e0 can come from IBase::SystemId, a bare
// CreateID of the base's `system` key that Common never checks against the system list.
// MSVC's std::map head is equally unconstructed (_Buyheadnode), so the port addresses that
// storage through the iterator's node pointer (MSVC-STL specific: _Ptr, _Myval) and runs
// the same body on it.
static void PlayerDB_LeaveSystem(PlayerData& Slot, unsigned int SessionId) {
    unsigned int const SystemId = Slot.system_id;
    if ((SystemId & 0x3fffffff) == 0) {
        return;
    }

    // A key above the id is a miss too.
    SysDB::SysMapType::iterator It = SysDB::SysMap.lower_bound(SystemId);
    if (It == SysDB::SysMap.end() || SystemId < It->first) {
        It = SysDB::SysMap.end();
    }
    // KNOWN CRASH (intentional, retail-faithful, owner's decision): on a miss System is the
    // unconstructed storage of SysMap's head node -- the player's system id has no SysMap
    // entry; check universe.ini. See THE MISS ARM above.
    StarSystem& System = It._Ptr->_Myval.second; // KNOWN CRASH on SysMap miss, see above

    System.remove_player(SessionId);                               // FUN_06d08740

    PlayerData_FlushPendingVisits(Slot);

    if (Slot.system_id != 0) {
        Slot.system_id = 0;
        unsigned int Payload[3];
        // KNOWN CRASH site on SysMap miss (garbage SystemInfo) -- see System above.
        Payload[0] = System.SystemInfo->NicknameId;
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // Single player: the last player out turns the system off.
    if (SinglePlayer() && System.bLoaded != 0) {
        System.bLoaded = 0;
        // KNOWN CRASH site on SysMap miss (garbage SystemInfo) -- see System above.
        unsigned int SystemNickname = System.SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemUnloaded, &SystemNickname);
        System.unload();                                           // FUN_06d072c0
    }
}

// FUN_06d18880 -- the UNDOCK: BaseDB::exit_location / unload_base, then current_base and
// current_location zeroed on both paths.
static void PlayerDB_LeaveBase(PlayerData& Slot, unsigned int SessionId) {
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

// PlayerDB::cleanup -- retail RVA 0x74340, ordinal 473. Retail ignores `this` and indexes
// the global `Players` (the same object). Leave the system, leave the base, event 0x3a,
// cancel any trade with reason 4. Not a wrapper around logout(), which starts with the
// same steps and then does much more.
void PlayerDB::cleanup(unsigned int SessionId) {
    if (SessionId == 0 || SessionId > m_MaxPlayers || !m_PlayerArray) return;

    PlayerData& Slot = m_PlayerArray[SessionId - 1];

    PlayerDB_LeaveSystem(Slot, SessionId);
    PlayerDB_LeaveBase(Slot, SessionId);

    // Vestigial in retail: leaving the system has already zeroed system_id, so this guard
    // cannot pass in a normal teardown. Kept for shape.
    unsigned int SystemField = Slot.system_id;
    if ((SystemField & 0x3fffffff) != 0) {
        unsigned int Payload[3];
        Payload[0] = SystemField;
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    if (Slot.trade_offer != nullptr) {
        GetServerInterface()->TerminateTrade(SessionId, 4);
    }
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

PlayerData* PlayerDB::traverse_active(PlayerData* Current) const {
    if (!m_PlayerArray) return nullptr;
    // Retail walks to [this+0x1c], the end pointer init stores, not to a bound
    // recomputed from MaxPlayers.
    PlayerData* Start = (Current == nullptr) ? m_PlayerArray : Current + 1;
    for (PlayerData* Slot = Start; Slot < m_PlayerArrayEnd; ++Slot) {
        if (Slot->client_id != 0) return Slot;
    }
    return nullptr;
}

// PlayerDB::login -- retail RVA 0x732F0, ordinal 501: 1 when the account is already logged
// in (wcsicmp on AccountName at slot+0x00), else load_user_data.
unsigned char PlayerDB::login(const SLoginInfo& Info, unsigned int SessionId) {
    const wchar_t* LoginName = Info.AccountName;
    if (!m_PlayerArray) return 2;

    PlayerData* Current = traverse_active(nullptr);
    while (Current != nullptr) {
        if (_wcsicmp(Current->AccountName, LoginName) == 0) {
            return 1; // Account already logged in
        }
        Current = traverse_active(Current);
    }

    // Not already logged in — proceed with load_user_data
    return load_user_data(Info, SessionId);
}

// PlayerData_Teardown -- retail FUN_06d49230: the prologue that touches other slots and
// subsystems, then the field wipe.
//  (a) the OUTBOUND trade request (+0x3D0): IClient slot 108 to the partner with
//      Players[slot->client_id].ship_object_id (indexed by client id, NOT session id),
//      and our id pulled out of the partner's inbound list;
//  (b) every INBOUND request: a requester whose own +0x3D0 is set gets the same
//      notification and its +0x3D0 cleared; then the list is erased;
//  (c) the reputation vibe freed (-1, not 0, is the empty sentinel);
//  (d) the CAccount marked logged out (m_ClientID = 0);
//  (e) SubObjectID::EquipIdMaker::Reset (+0x3C8).
static void PlayerData_Teardown(PlayerData& Slot) {
    PlayerDB& Db = Players;

    // (a) No bounds checks anywhere, and IClient is called with no null test.
    if (Slot.trade_request_partner != 0) {
        unsigned int const Partner = Slot.trade_request_partner;
        g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(Partner,
                                                                  Db[Slot.client_id].ship_object_id);
        // FUN_06d571a0 = list::remove(value): EVERY node equal to our client id goes.
        std::vector<unsigned int>& PartnerInbound = Db[Partner].inbound_trade_requests;
        for (size_t Index = 0; Index < PartnerInbound.size();) {
            if (PartnerInbound[Index] == Slot.client_id) {
                PartnerInbound.erase(PartnerInbound.begin()
                                     + static_cast<std::ptrdiff_t>(Index));
            } else {
                ++Index;
            }
        }
        Slot.trade_request_partner = 0;
    }

    // (b) Each node's session indexes Players raw.
    for (size_t Index = 0; Index < Slot.inbound_trade_requests.size(); ++Index) {
        PlayerData& RequesterSlot = Db[Slot.inbound_trade_requests[Index]];
        if (RequesterSlot.trade_request_partner == 0) continue;
        g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(RequesterSlot.trade_request_partner,
                                                                  Db[RequesterSlot.client_id].ship_object_id);
        RequesterSlot.trade_request_partner = 0;
    }
    Slot.inbound_trade_requests.clear();

    // (c)
    if (Slot.rep_vibe != -1) {
        Reputation::Vibe::Free(Slot.rep_vibe);
        Slot.rep_vibe = -1;
    }

    // The field wipe, in retail order. AccountName and both costumes get the 0xcd fill.
    std::memset(Slot.AccountName, 0xcd, sizeof(Slot.AccountName));
    Slot.saved_characters.clear();
    Slot.ship_archetype = 0;
    Slot.relative_health = 0.0f;
    Slot.equipment.Items.clear();
    Slot.collision_groups.clear();
    std::memset(&Slot.com_costume, 0xcd, sizeof(Slot.com_costume));
    Slot.voice_length = 0;
    Slot.voice[0] = '\0';
    std::memset(&Slot.costume, 0xcd, sizeof(Slot.costume));
    for (int Axis = 0; Axis < 3; ++Axis)
        Slot.initial_pos[Axis] = 0.0f;
    for (int Cell = 0; Cell < 9; ++Cell)
        Slot.initial_ornt[Cell] = (Cell % 4 == 0) ? 1.0f : 0.0f;
    // 0x29a (666), the retail fill value.
    Slot.cash = 0x29a;
    Slot.num_kills = 0;
    Slot.num_mission_successes = 0;
    Slot.num_mission_failures = 0;
    Slot.bSkipNextAutosave = 0;
    Slot.save_failure_count = 0;
    Slot.client_id = 0;
    Slot.bCheatReported = 0;

    // (d) account_ptr itself is NOT cleared.
    CAccount* Account = static_cast<CAccount*>(Slot.account_ptr);
    if (Account != nullptr) {
        Account->m_ClientID = 0;
    }

    // (e) Reset stores 0x21.
    Slot.equip_id_maker.Reset();

    Slot.system_id = 0;
    Slot.ship_object_id = 0;
    Slot.created_ship_id = 0;
    Slot.current_base = 0;
    Slot.entered_base_id = 0;
    Slot.last_base_id = 0;
    Slot.current_location = 0;
    Slot.character_id = 0;
    Slot.exited_base_id = 0;
    Slot.starting_ring = CreateID("NO WHERE");
    Slot.selected_character.invalidate();
    // group_mission_id (+0x408) is left alone.
    Slot.group_ptr = nullptr;
    Slot.group_mission_set_by = 0;
    Slot.visit_map.clear();
    // Not touched by retail: rank_level, +0x288, total_earned, the equipment
    // snapshot, weapon_group_name, mission_log, interface_state, the pending
    // visits vector, difficulty_scale, trade_offer, group_mission_id.
}

void PlayerDB::logout(unsigned int SessionId) {
    if (SessionId == 0 || SessionId > m_MaxPlayers || !m_PlayerArray) return;
    PlayerData& Slot = m_PlayerArray[SessionId - 1];

    // (1) MP only: FUN_06d4c430, the save-game writer (PlayerData_AutoSave,
    //     server_savegame.cpp) -- flush this session's character to disk before tearing
    //     the slot down. Its own gates decide whether anything is written.
    if (!SinglePlayer()) {
        PlayerData_AutoSave(Slot);
    }

    // (2) The same two teardown steps cleanup() runs, in the same order.
    PlayerDB_LeaveSystem(Slot, SessionId);
    PlayerDB_LeaveBase(Slot, SessionId);

    // (3) Event 0x3a, vestigial as in cleanup.
    unsigned int SystemField = Slot.system_id;
    if ((SystemField & 0x3fffffff) != 0) {
        unsigned int Payload[3];
        Payload[0] = SystemField;
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // (4)
    if (Slot.trade_offer != nullptr) {
        GetServerInterface()->TerminateTrade(SessionId, 4);
    }

    // (5) The logout event, a ONE-dword payload.
    unsigned int LogoutPayload = SessionId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogout, &LogoutPayload);

    // (6) .data:DAT_06d8f9a0.
    ++GServerStats.LogoutCount;

    // (7) Tell the player's group, if any (+0x404).
    if (Slot.group_ptr != nullptr) {
        static_cast<CPlayerGroup*>(Slot.group_ptr)->HandleClientLogout(SessionId);
    }

    // (8) Every OTHER occupied session drops this player from its player list
    //     (FLPACKET_SERVER_PLAYERLIST), when this session actually picked a character.
    if ((!IsMPServer() || Slot.selected_character.is_valid())
        && m_MaxPlayers != 0) {
        for (unsigned int Other = 1; Other <= m_MaxPlayers; ++Other) {
            if (Other == SessionId) continue;
            if (m_PlayerArray[Other - 1].client_id == 0) continue;
            g_pClientInterface->SendServerPlayerListRemove(Other, SessionId);
        }
    }

    // (9) On an MP server, log the logout. The second field is the account DIRECTORY PATH
    //     (CAccount+0x14, filled at login by FUN_06d48e80).
    CiWString LogoutName(Slot.AccountName);
    CAccount* Account = FindAccountFromName(LogoutName);
    if (Account != nullptr && !SinglePlayer()) {
        FDUMP(0x100003, "LOG: Logout on account %S (\"%s\")\n",
              Slot.AccountName, Account->m_DirectoryPath.c_str());
    }

    // (10) The slot teardown; clearing client_id is what frees the slot for reuse.
    PlayerData_Teardown(Slot);
}

// PlayerData_DeselectCharacter -- retail FUN_06d4a4a0 (thiscall). Leaving the world without
// logging out: logout's system / base / trade teardown, the player-list removal and event
// 7, then the selection is dropped. `this` is only read for +0x64; everything else goes
// through Players[SessionId].
void __cdecl PlayerData_DeselectCharacter(PlayerData& Slot, unsigned int SessionId) {
    if (SinglePlayer())
        return;
    PlayerData& Owner = Players[SessionId];
    if (!Slot.selected_character.is_valid())
        return;

    PlayerDB_LeaveSystem(Owner, SessionId);
    PlayerDB_LeaveBase(Owner, SessionId);

    // Vestigial, as in cleanup / logout.
    unsigned int const SystemField = Owner.system_id;
    if ((SystemField & 0x3fffffff) != 0) {
        unsigned int Payload[3];
        Payload[0] = SystemField;
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    if (Owner.trade_offer != nullptr)
        GetServerInterface()->TerminateTrade(SessionId, 4);

    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogout, &SessionId);

    if (Owner.group_ptr != nullptr)
        static_cast<CPlayerGroup*>(Owner.group_ptr)->HandleClientLogout(SessionId);

    // No null test on the interface.
    unsigned int const MaxPlayers = Players.GetMaxPlayerCount();
    for (unsigned int Other = 1; Other <= MaxPlayers; ++Other) {
        if (!Players.is_valid(Other) || Other == SessionId)
            continue;
        g_pClientInterface->SendServerPlayerListRemove(Other, SessionId);
    }

    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogin, &SessionId);

    Slot.selected_character.Buffer[0] = '\0';
}

// FUN_06d49960 (cdecl): the character list's sort order. True when New goes BEFORE
// Existing: Restart.fl first, then AutoSave.fl, then the more recent save.
static bool CharacterInfo_GoesBefore(CCharacterSet::CharacterInfo const& New,
                                     CCharacterSet::CharacterInfo const& Existing) {
    if (New.bIsRestart != Existing.bIsRestart)
        return New.bIsRestart != 0;
    if (New.bIsAutoSave != Existing.bIsAutoSave)
        return New.bIsAutoSave != 0;
    FILETIME NewTime;
    NewTime.dwLowDateTime = New.TimestampLow;
    NewTime.dwHighDateTime = New.TimestampHigh;
    FILETIME ExistingTime;
    ExistingTime.dwLowDateTime = Existing.TimestampLow;
    ExistingTime.dwHighDateTime = Existing.TimestampHigh;
    LONG const Order = CompareFileTime(&NewTime, &ExistingTime);
    if (Order == -1)
        return false;
    return Order == 1;
}

// PlayerData_BuildCharacterSet -- retail FUN_06d499c0 (thiscall). Only caller:
// CharacterInfoReq, when its bool is false.
void __cdecl PlayerData_BuildCharacterSet(PlayerData& Slot, CCharacterSet& Out) {
    // IsMPServer() twice, as retail: an MP server with a selected character leaves the
    // world first.
    if (IsMPServer() && (!IsMPServer() || Slot.selected_character.Buffer[0] != '\0')) {
        PlayerData_AutoSave(Slot);

        // The session id is the slot's own client_id.
        unsigned int const SessionId = Slot.client_id;
        PlayerData& Owner = Players[SessionId];

        PlayerDB_LeaveSystem(Owner, SessionId);
        PlayerDB_LeaveBase(Owner, SessionId);

        unsigned int const SystemField = Owner.system_id;
        if ((SystemField & 0x3fffffff) != 0) {
            unsigned int Payload[3];
            Payload[0] = SystemField;
            Payload[1] = 0;
            Payload[2] = SessionId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
        }

        if (Owner.trade_offer != nullptr)
            GetServerInterface()->TerminateTrade(SessionId, 4);
    }

    char RestartName[0x104];
    char AutoSaveName[0x104];
    sprintf(RestartName, "%s.fl", "Restart");
    sprintf(AutoSaveName, "%s.fl", "AutoSave");

    // Every record, in key order.
    for (std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::const_iterator It =
             Slot.saved_characters.begin();
         It != Slot.saved_characters.end(); ++It) {
        PlayerData::FSavedCharacter const& Saved = It->second;

        CCharacterSet::CharacterInfo* Info = new CCharacterSet::CharacterInfo();   // new(0x2dc)

        Info->Id = It->first;
        Info->bIsRestart = static_cast<unsigned char>(_stricmp(RestartName, Saved.FileName) == 0);
        Info->bIsAutoSave = static_cast<unsigned char>(_stricmp(AutoSaveName, Saved.FileName) == 0);
        Info->Name = Saved.Name;
        Info->Description = Saved.Description;
        Info->DescripStrId = Saved.DescripStrId;
        Info->TimestampLow = Saved.TimestampLow;
        Info->TimestampHigh = Saved.TimestampHigh;

        // The autosave's description becomes L"" (.rdata:06d6b2f8) and its strid 0x65c.
        if (Info->bIsAutoSave != 0) {
            Info->Description = L"";
            Info->DescripStrId = 0x65c;
        }

        Info->ShipArchetype = Saved.ShipArchetype;
        Info->Money = Saved.Money;
        Info->NumKills = Saved.NumKills;
        Info->NumMissionSuccesses = Saved.NumMissionSuccesses;
        Info->NumMissionFailures = Saved.NumMissionFailures;
        Info->HullStatus = Saved.HullStatus;
        Info->Equipment = Saved.Equipment;
        Info->CollisionGroups = Saved.CollisionGroups;
        Info->SystemId = Saved.SystemId;
        Info->BaseId = Saved.BaseId;
        Info->LocationId = Saved.LocationId;
        Info->VoiceId = CreateID(Saved.Voice.Data);
        Info->Rank = Saved.Rank;
        CharacterInfoCopyCostume(Info->BaseCostume, Saved.BaseCostume);
        CharacterInfoCopyCostume(Info->ComCostume, Saved.ComCostume);

        // Sorted insert: linked after the node before the first one it goes before;
        // running off the end appends.
        MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
        MetaNode<CCharacterSet::CharacterInfo>* Previous = nullptr;
        bool bPlaced = false;
        while (Out.Characters.traverse(Node, true)) {
            if (CharacterInfo_GoesBefore(*Info, *Node->Data)) {
                Out.Characters.insert_after(Previous, Info);
                bPlaced = true;
                break;
            }
            Previous = Node;
        }
        if (!bPlaced)
            Out.Characters.append(Info);
    }

    // The failed-save counter travels with the list and is reset.
    Out.SaveFailureCount = Slot.save_failure_count;
    Slot.save_failure_count = 0;
}

// PlayerData_SelectCharacter -- retail FUN_06d4a5d0 (thiscall), only caller
// IServerImpl::CharacterSelect. This is the head and the MP-only front; the shared tail is
// PlayerData_LoadSelectedCharacter (server_savegame.cpp). Players[sid] is recomputed from
// the argument rather than using `this`.
bool __cdecl PlayerData_SelectCharacter(PlayerData& Slot, CHARACTER_ID const& Id,
                                        unsigned int SessionId) {
    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator Found =
        Slot.saved_characters.find(Id);
    if (Found == Slot.saved_characters.end())
        return false;
    PlayerData::FSavedCharacter const& Record = Found->second;

    // SinglePlayer() twice (the first result is discarded); single player skips the whole
    // front.
    SinglePlayer();
    if (!SinglePlayer()) {
        PlayerData& Owner = Players[SessionId];

        unsigned int OwnGroupId = 0;
        if (Players.is_valid(SessionId) && Owner.group_ptr != nullptr)
            OwnGroupId = static_cast<CPlayerGroup*>(Owner.group_ptr)->GetID();

        // Only when a character is already selected.
        if (Slot.selected_character.Buffer[0] != '\0') {
            PlayerDB_LeaveSystem(Owner, SessionId);
            PlayerDB_LeaveBase(Owner, SessionId);

            unsigned int const SystemField = Owner.system_id;
            if ((SystemField & 0x3fffffff) != 0) {
                unsigned int Payload[3];
                Payload[0] = SystemField;
                Payload[1] = 0;
                Payload[2] = SessionId;
                Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
            }

            if (Owner.trade_offer != nullptr)
                GetServerInterface()->TerminateTrade(SessionId, 4);

            // Re-selecting the same character skips the player-list removal.
            if (Id != Slot.selected_character) {
                Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogout, &SessionId);

                // The cached group id drops to 0.
                if (Owner.group_ptr != nullptr) {
                    static_cast<CPlayerGroup*>(Owner.group_ptr)->HandleClientLogout(SessionId);
                    OwnGroupId = 0;
                }

                unsigned int const MaxPlayers = Players.GetMaxPlayerCount();
                for (unsigned int Other = 1; Other <= MaxPlayers; ++Other) {
                    if (!Players.is_valid(Other) || Other == SessionId)
                        continue;
                    g_pClientInterface->SendServerPlayerListRemove(Other, SessionId);
                }

                Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogin, &SessionId);
            }
        }

        Slot.selected_character = Id;

        g_pClientInterface->SendServerPlayerListClear(SessionId);

        wchar_t const* const RecordName = Record.Name.c_str();

        // The reputation / player-list exchange with every other session.
        unsigned int const MaxPlayers = Players.GetMaxPlayerCount();
        for (unsigned int Other = 1; Other <= MaxPlayers; ++Other) {
            if (!Players.is_valid(Other))
                continue;
            PlayerData& OtherSlot = Players[Other];
            // On an MP server, only sessions that have picked a character.
            if (IsMPServer() && OtherSlot.selected_character.Buffer[0] == '\0')
                continue;

            int OtherVibe = OtherSlot.rep_vibe;
            unsigned int OtherRank = 0;
            ::Reputation::Vibe::GetRank(OtherVibe, OtherRank);

            unsigned int OtherGroupId = 0;
            if (Players.is_valid(Other) && OtherSlot.group_ptr != nullptr)
                OtherGroupId = static_cast<CPlayerGroup*>(OtherSlot.group_ptr)->GetID();

            // Tell the selecting session about everyone else.
            if (Other != SessionId) {
                static wchar_t const BadName[] = L">>>BAD   NAME<<<";
                wchar_t const* OtherName = BadName;
                {
                    FmtStr NameA;
                    FmtStr NameB;
                    int const NameResult = ::Reputation::Vibe::GetName(
                        OtherSlot.rep_vibe, NameA, NameB, OtherName);
                    if (NameResult == -2)
                        OtherName = BadName;
                }
                g_pClientInterface->SendServerPlayerListAdd(SessionId, OtherName, Other, true);
                g_pClientInterface->SendServerMiscObjUpdatePlayerGroup(SessionId, Other, OtherGroupId);
                g_pClientInterface->SendServerMiscObjUpdateRank(SessionId, Other,
                                                                static_cast<int>(OtherRank));
                g_pClientInterface->SendServerMiscObjUpdateSystem(SessionId, Other,
                                                                  OtherSlot.system_id);
            }

            // Tell session i (the selecting one included) about the new character, from
            // the RECORD.
            g_pClientInterface->SendServerPlayerListAdd(Other, RecordName, SessionId, false);
            g_pClientInterface->SendServerMiscObjUpdatePlayerGroup(Other, SessionId, OwnGroupId);
            g_pClientInterface->SendServerMiscObjUpdateRank(Other, SessionId,
                                                            static_cast<int>(Record.Rank));
            g_pClientInterface->SendServerMiscObjUpdateSystem(Other, SessionId, Record.SystemId);
        }
    }

    return PlayerData_LoadSelectedCharacter(Slot, Record, SessionId);
}

// PlayerData_DestroyCharacter -- retail FUN_06d49f60 (thiscall); only caller
// IServerImpl::DestroyCharacter.
bool __cdecl PlayerData_DestroyCharacter(PlayerData& Slot, CHARACTER_ID const& Id,
                                         unsigned int SessionId) {
    // Deleting the selected character deselects it first.
    if (Slot.selected_character == Id)
        PlayerData_DeselectCharacter(Slot, SessionId);

    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
        Slot.saved_characters.find(Id);
    if (It == Slot.saved_characters.end())
        return false;
    PlayerData::FSavedCharacter& Record = It->second;

    // Restart.fl is never deleted.
    char RestartName[0x200];
    sprintf(RestartName, "%s.fl", "Restart");
    if (_stricmp(Record.FileName, RestartName) == 0)
        return false;

    // <accts>\<account dir>\<file name>: the inline FUN_06d46370 hash in MP, wcstombs in SP.
    char AccountDirName[0x104];
    if (IsMPServer()) {
        Server_AccountDirName(AccountDirName, Slot.AccountName);
    } else {
        wcstombs(AccountDirName, Slot.AccountName, 0x104);
    }
    // Retail reuses one buffer as both the second _makepath's dir and its output; two
    // buffers here, because the debug CRT may fill the destination first.
    char AccountDir[0x104];
    _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);
    char Path[0x104];
    _makepath(Path, nullptr, AccountDir, Record.FileName, nullptr);

    // Lower-cases the RECORD's own file name, in place.
    _strlwr(Record.FileName);

    CiString CharacterKey(Record.FileName);
    Players.DeleteCharacterFromID(CharacterKey);

    _unlink(Path);

    Slot.saved_characters.erase(It);

    if (!SinglePlayer()) {
        FDUMP(0x100003, "LOG: Delete character on account %S (\"%s\")\n",
              Slot.AccountName, Path);
    }
    return true;
}

// Retail 0x06d54640. On an MP server a session that has not selected a character has no
// active name. The name is FUN_06d4eda0's: resolved through the reputation vibe, aliasing
// the vibe's own buffer (or the static fallback).
const wchar_t* PlayerDB::GetActiveCharacterName(unsigned int SessionId) const {
    if (SessionId == 0 || SessionId > m_MaxPlayers || !m_PlayerArray) return nullptr;
    const PlayerData& Slot = m_PlayerArray[SessionId - 1];
    if (Slot.client_id == 0) return nullptr;
    if (IsMPServer() && !Slot.selected_character.is_valid()) return nullptr;

    static const wchar_t BadName[] = L">>>BAD   NAME<<<";
    const wchar_t* DisplayName = BadName;
    FmtStr Name1;
    FmtStr Name2;
    const int VibeId = Slot.rep_vibe;
    if (::Reputation::Vibe::GetName(VibeId, Name1, Name2, DisplayName) == -2) {
        DisplayName = BadName;
    }
    return DisplayName;
}

unsigned int PlayerDB::CountPlayersInSystem(int SystemId) {
    unsigned int Count = 0;
    if (!m_PlayerArray) return 0;
    for (unsigned int Index = 0; Index < m_MaxPlayers; ++Index) {
        const PlayerData& Slot = m_PlayerArray[Index];
        if (Slot.client_id != 0 && (int)Slot.system_id == SystemId) {
            Count++;
        }
    }
    return Count;
}

unsigned int PlayerDB::GetGroupID(unsigned int PlayerId) {
    if (!is_valid(PlayerId)) return 0;
    void* GroupPtr = m_PlayerArray[PlayerId - 1].group_ptr;
    if (GroupPtr != nullptr) {
        CPlayerGroup* Group = (CPlayerGroup*)GroupPtr;
        return Group->GetID();
    }
    return 0;
}

void PlayerDB::SendGroupID(unsigned int PlayerId, unsigned int GroupId) {
    // Retail RVA 0x74890: IClient slot 118 to every valid player.
    extern IClient* g_pClientInterface;

    unsigned int max_players = PlayerDB_GetMaxPlayers();
    if (max_players == 0) return;

    for (unsigned int i = 1; i <= max_players; i++) {
        if (!is_valid(i)) continue;
        g_pClientInterface->SendServerMiscObjUpdatePlayerGroup(i, PlayerId, GroupId);
    }
}

void PlayerDB::SendSystemID(unsigned int PlayerId, unsigned int SystemId) {
    // Retail RVA 0x74900: IClient slot 119 to every valid player.
    extern IClient* g_pClientInterface;

    unsigned int max_players = PlayerDB_GetMaxPlayers();
    if (max_players == 0) return;

    for (unsigned int i = 1; i <= max_players; i++) {
        if (!is_valid(i)) continue;
        g_pClientInterface->SendServerMiscObjUpdateSystem(i, PlayerId, SystemId);
    }
}

// PlayerDB::BuildLocalUserDir -- retail .text:06d518f0: single player builds nothing; the
// accounts root (FUN_06d460f0 inlined, global DAT_06d90158) is built with
// COMMON.DLL::BuildDirectory.
void PlayerDB::BuildLocalUserDir() {
    if (!IsMPServer())
        return;
    BuildDirectory(Server_GetAccountsRoot());
}

// PlayerDB::MakeLocalUserPath -- retail .text:06d51960: "<accounts root>\<file>"; no null
// tests, no failure path.
bool PlayerDB::MakeLocalUserPath(char* OutPath, const char* FileName) {
    char const* const AccountsRoot = Server_GetAccountsRoot();
    sprintf(OutPath, "%s\\%s", AccountsRoot, FileName);
    return true;
}

CAccount* PlayerDB::FindAccountFromClientID(unsigned int ClientId) {
    // Retail (0x06d52b40) returns slot[ClientId-1].account_ptr with NO bounds check; the
    // port's range test is an addition.
    if (ClientId == 0) return nullptr;
    if (ClientId > m_MaxPlayers || !m_PlayerArray) return nullptr;
    return reinterpret_cast<CAccount*>(m_PlayerArray[ClientId - 1].account_ptr);
}

// Account/character hashes use Server_AccountCrc (server_pub.cpp, server_savefile.h):
// retail's CRC engine DAT_06d8fd38 -- width 31, poly 0x8005, reflected, XorIn/XorOut 0 --
// NOT the standard CRC-32, formatted "%02x-%08x" (length, hash).

// The account "name" file is obfuscated with a CONSTANT key: the low 16 bits of the CRC of
// the wide marker at .rdata:06d6ae80 (PlayerDB::CreateAccount, load_user_data,
// CAccount::InitFromFolder). Unlike the password file, it is reversible.
static const wchar_t* const GAccountNameFileKeySource = L"AKL!8167fGBA0712";

static unsigned short AccountNameFileXorKey() {
    return (unsigned short)Server_AccountCrc(
        reinterpret_cast<const unsigned char*>(GAccountNameFileKeySource),
        (unsigned int)(wcslen(GAccountNameFileKeySource) * sizeof(wchar_t)));
}

// The lower-cased wide name's raw bytes, account-CRC'd, as "%02x-%08x" (length, hash).
static std::string ComputeHashedCharacterID(const std::wstring& CharName) {
    std::wstring Lower = CharName;
    for (std::wstring::size_type i = 0; i < Lower.size(); ++i) {
        Lower[i] = (wchar_t)towlower(Lower[i]);
    }
    unsigned int CharLen = (unsigned int)Lower.size();
    unsigned int Hash = Server_AccountCrc(
        reinterpret_cast<const unsigned char*>(Lower.c_str()),
        CharLen * (unsigned int)sizeof(wchar_t));
    char Buf[32];
    sprintf_s(Buf, sizeof(Buf), "%02x-%08x", CharLen, Hash);
    return std::string(Buf);
}

static std::string ComputeHashedCharacterID(const CiWString& CharName) {
    return ComputeHashedCharacterID(CiToWide(CharName));
}

// <UserDataPath>\Accts or \Accts\MultiPlayer.
static void BuildAccountFolderPath(char* OutPath, unsigned int OutPathSize) {
    char UserDataPath[260];
    GetUserDataPath(UserDataPath);
    const char* SubDir = SinglePlayer() ? "Accts" : "Accts\\MultiPlayer";
    _makepath_s(OutPath, OutPathSize, nullptr, UserDataPath, SubDir, nullptr);
}

// The account's directory: the hashed lower-case name in MP, the plain name in SP.
static void BuildAccountFilePath(char* OutPath, unsigned int OutPathSize,
                                 const std::wstring& AccountName) {
    char DirPath[260];
    BuildAccountFolderPath(DirPath, sizeof(DirPath));
    if (!SinglePlayer()) {
        std::wstring Lower = AccountName;
        for (std::wstring::size_type i = 0; i < Lower.size(); ++i) {
            Lower[i] = (wchar_t)towlower(Lower[i]);
        }
        unsigned int CharLen = (unsigned int)Lower.size();
        unsigned int Hash = Server_AccountCrc(
            reinterpret_cast<const unsigned char*>(Lower.c_str()),
            CharLen * (unsigned int)sizeof(wchar_t));
        char HashBuf[32];
        sprintf_s(HashBuf, sizeof(HashBuf), "%02x-%08x", CharLen, Hash);
        _makepath_s(OutPath, OutPathSize, nullptr, DirPath, HashBuf, nullptr);
    } else {
        char NameBuf[260];
        wcstombs(NameBuf, AccountName.c_str(), sizeof(NameBuf));
        _makepath_s(OutPath, OutPathSize, nullptr, DirPath, NameBuf, nullptr);
    }
}

// FindAccountFromCharacterID -- RVA 0x72AE0, export 125.
CAccount* PlayerDB::FindAccountFromCharacterID(CiString& CharId) {
    std::map<CiString, CAccount*>::iterator It =
        m_CharacterIDMap.find(CharId);
    if (It != m_CharacterIDMap.end()) {
        return It->second;
    }
    return nullptr;
}

// FindAccountFromCharacterName -- RVA 0x72B60, export 126: the hashed character id
// "XX-XXXXXXXX.fl" looked up in the character-id map.
CAccount* PlayerDB::FindAccountFromCharacterName(CiWString& CharName) {
    std::string CharId = ComputeHashedCharacterID(CharName);
    char FileName[260];
    _makepath_s(FileName, sizeof(FileName), nullptr, nullptr, CharId.c_str(), "fl");
    _strlwr(FileName);
    std::map<CiString, CAccount*>::iterator It =
        m_CharacterIDMap.find(CiString(FileName));
    if (It != m_CharacterIDMap.end()) {
        return It->second;
    }
    return nullptr;
}

// FindAccountFromName -- RVA 0x72CD0, export 128.
CAccount* PlayerDB::FindAccountFromName(CiWString& Name) {
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(Name);
    if (It != m_AccountNameMap.end()) {
        return It->second;
    }
    return nullptr;
}

// GetAccountAdminRights -- RVA 0x72730, export 144: CAccount+0x32.
bool PlayerDB::GetAccountAdminRights(CiWString& Name) {
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(Name);
    if (It != m_AccountNameMap.end() && It->second != nullptr) {
        return It->second->m_bAdmin;
    }
    return false;
}

// SetAccountAdminRights -- RVA 0x72790, export 406: creates or unlinks the account's
// "admin" marker file and sets CAccount+0x32.
bool PlayerDB::SetAccountAdminRights(CiWString& Name, bool bAdmin) {
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(Name);
    if (It == m_AccountNameMap.end() || It->second == nullptr) {
        return false;
    }

    CAccount* Account = It->second;

    // Build the account file path
    char AccountPath[260];
    BuildAccountFilePath(AccountPath, sizeof(AccountPath), CiToWide(Name));

    // Build the "admin" file path
    char AdminFilePath[260];
    _makepath_s(AdminFilePath, sizeof(AdminFilePath), nullptr, AccountPath, "admin", nullptr);

    if (bAdmin) {
        // Create the admin marker file
        FILE* File = fopen(AdminFilePath, "wb");
        if (File != nullptr) {
            fclose(File);
            Account->m_bAdmin = bAdmin;                                     // account+0x32
            return true;
        }
    } else {
        _unlink(AdminFilePath);
    }
    Account->m_bAdmin = bAdmin;
    return true;
}

// LockAccountAccess -- RVA 0x72A40, export 331: a logged-in account is kicked through
// IClient slot 17 (no null test on the interface), then CAccount+0x30 is set.
void PlayerDB::LockAccountAccess(CiWString& Name) {
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(Name);
    if (It == m_AccountNameMap.end() || It->second == nullptr) {
        return;
    }

    CAccount* Account = It->second;

    if (Account->m_ClientID != 0) {
        extern IClient* g_pClientInterface;
        g_pClientInterface->DisconnectClient(Account->m_ClientID);
    }

    Account->m_bLocked = true;
}

// PlayerDB::create_restart_file -- retail .text:06d536d0, ordinal 479. Builds
// <AccountDir>\Restart.fl through the ini writer (SaveGame_Writer) from <exe
// dir>\NewPlayer.fl (FLS1-encoded or plain; FLSG fails) followed by the initial_world
// file, copied in 0x200-byte blocks; then the FLS1 rewrite. The template text is passed
// to fprintf AS THE FORMAT, as in retail. A missing world file still rewrites, then
// removes Restart.fl and fails.

// Retail FUN_06d53b60: the FDUMP flags word ((Category << 4) with the level in the low
// nibble).
static int PlayerDB_MakeDumpFlags(int Category, unsigned int Level) {
    unsigned int const High = static_cast<unsigned int>(Category) << 4;
    return static_cast<int>(((High ^ Level) & 0xfu) ^ High);
}

bool PlayerDB::create_restart_file(const char* AccountDir) {
    static char const* const SourceFile = "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp";
    bool bResult = false;
    char RestartPath[0x104];
    _makepath(RestartPath, nullptr, AccountDir, "Restart", "fl");

    SaveGame_Writer Save;
    if (!Save.Open(RestartPath)) {
        FDUMP(PlayerDB_MakeDumpFlags(0x10000, 2),
              "%s(%d) : *** WARNING: Failed to open '%s' for writing.\n",
              SourceFile, 0x156c, RestartPath);
        return bResult;
    }

    char TemplatePath[0x104];
    GetModuleFileNameA(nullptr, TemplatePath, 0x104);
    char Drive[4];
    char Dir[0x100];
    _splitpath(TemplatePath, Drive, Dir, nullptr, nullptr);
    _makepath(TemplatePath, Drive, Dir, "NewPlayer.fl", nullptr);

    FSaveFileBuffer Template;
    Template.File = fopen(TemplatePath, "rt");
    if (Template.File != nullptr) {
        // Retail reads into an uninitialised dword; zero here only so that a file shorter
        // than four bytes compares as neither magic.
        unsigned int Magic = 0;
        fread(&Magic, 1, 4, Template.File);
        if (Magic == 0x31534c46u) {                                         // "FLS1"
            Template.CloseFile();
            Template.File = fopen(TemplatePath, "rb");
            if (Template.File != nullptr) {
                Template.ReadTail(4);                                       // FUN_06d48960
                Template.Encode();                                          // FUN_06d48b60
            }
        } else if (Magic == 0x47534c46u) {                                  // "FLSG"
            goto TemplateFailed;
        } else {
            Template.ReadTail(0);
        }
    }
    if (Template.File == nullptr)
        goto TemplateFailed;

    {
        fprintf(Save.File, Template.Contents.c_str());
        if (Template.File != nullptr) {
            fclose(Template.File);
            Template.File = nullptr;
        }

        // Retail builds the world path in place; the data directory gets its own buffer
        // here so the result does not depend on _makepath's handling of overlapping output.
        char DataPath[0x104];
        DALib::Data->GetFileSysPath(DataPath, 0x104);
        extern char g_InitialWorldPath[260];                                // .data:DAT_06d8f9c8
        char WorldPath[0x104];
        _makepath(WorldPath, nullptr, DataPath, g_InitialWorldPath, nullptr);

        FILE* World = fopen(WorldPath, "rt");
        if (World != nullptr) {
            char Block[0x200];
            size_t Got;
            do {
                Got = fread(Block, 1, 0x200, World);
                if (Got != 0)
                    fwrite(Block, 1, Got, Save.File);
            } while (Got >= 0x200);
            fclose(World);
            bResult = true;
            if (Save.File != nullptr) {
                Save.ReadBackAndEncode();
                Save.CloseFile();
                Save.RewriteEncoded();
            }
            return bResult;
        }

        // This severity is a literal in retail; the other two use FUN_06d53b60.
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Failed to open '%s' for reading.\n",
              SourceFile, 0x155e, WorldPath);
        if (Save.File != nullptr) {
            Save.ReadBackAndEncode();
            Save.CloseFile();
            Save.RewriteEncoded();
        }
        remove(RestartPath);
        return bResult;
    }

TemplateFailed:
    FDUMP(PlayerDB_MakeDumpFlags(0x10000, 2),
          "%s(%d) : *** WARNING: Failed to open '%s' for reading.\n",
          SourceFile, 0x1565, TemplatePath);
    Save.Close();                                                           // FUN_06d4eb00
    remove(RestartPath);
    return bResult;
}

unsigned int PlayerDB::to_index(unsigned int SessionId) {
    return (SessionId > 0) ? SessionId - 1 : 0;
}

std::wstring& PlayerDB::GetMOTD() {
    return m_MOTD;
}

// PlayerDB::SetMOTD -- retail RVA 0x746E0, ordinal 421: the MOTD at this+0x20, then
// IClient slot 115 to every connected slot.
void PlayerDB::SetMOTD(std::wstring& Message) {
    m_MOTD = Message;

    extern IClient* g_pClientInterface;

    for (unsigned int Index = 1; Index <= m_MaxPlayers; ++Index) {
        if (m_PlayerArray[Index - 1].client_id != 0) {
            g_pClientInterface->SendServerMiscObjUpdateMotd(Index, Message);
        }
    }
}

// PlayerDB::UnlockAccountAccess -- retail RVA 0x72A80, ordinal 469: CAccount+0x30 = 0.
void PlayerDB::UnlockAccountAccess(CiWString& CharName) {
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(CharName);

    if (It == m_AccountNameMap.end()) {
        return;
    }

    CAccount* Account = It->second;
    if (Account != nullptr) {
        Account->m_bLocked = false;
    }
}

// PlayerDB::ReadCharacterName -- retail .text:06d52fe0 (thiscall). The save is read as FLS1
// (decoded), plain, or rejected (FLSG). Only "Player" headers are walked; "/*" and "*/"
// values nest a comment counter, and "name" counts only while it is <= 0; the value is
// hex-decoded. The out-string is written only on the hit and the fail exits (L""); retail
// never clears it on entry and has no null test on the path.
void PlayerDB::ReadCharacterName(const char* FilePath, CiWString& OutName) {
    FSaveFileBuffer Buffer;
    INI_Reader Reader;

    bool bLoaded = false;
    Buffer.File = fopen(FilePath, "rt");
    if (Buffer.File != nullptr) {
        char Magic[4] = { 0, 0, 0, 0 };
        fread(Magic, 1, 4, Buffer.File);
        if (memcmp(Magic, Server_FLS1Magic, 4) == 0) {
            fclose(Buffer.File);
            Buffer.File = nullptr;
            Buffer.File = fopen(FilePath, "rb");
            if (Buffer.File != nullptr) {
                Buffer.ReadTail(4);
                Buffer.Encode();
                bLoaded = true;
            }
        } else if (memcmp(Magic, "FLSG", 4) != 0) {
            Buffer.ReadTail(0);
            bLoaded = true;
        }
    }

    if (bLoaded) {
        Reader.open_memory(Buffer.Contents.c_str(),
                           static_cast<unsigned int>(Buffer.Contents.size()));
        if (Reader.read_header()) {
            do {
                int CommentDepth = 0;
                if (Reader.is_header("Player") && Reader.read_value()) {
                    do {
                        if (Reader.is_value("/*"))
                            ++CommentDepth;
                        else if (Reader.is_value("*/"))
                            --CommentDepth;
                        if (CommentDepth <= 0 && Reader.is_value("name")) {
                            wchar_t WideBuf[0x400];
                            HexToWide(Reader.get_value_string(), WideBuf);
                            OutName.assign(WideBuf, wcslen(WideBuf));
                            return;
                        }
                    } while (Reader.read_value());
                }
            } while (Reader.read_header());
        }
    }

    OutName.assign(L"", wcslen(L""));
}

// PlayerDB::DeleteCharacterFromID -- retail RVA 0x72D30, ordinal 100: the account (when
// non-null) deletes the .fl file and its list entry, then the map entry is erased.
void PlayerDB::DeleteCharacterFromID(CiString& CharId) {
    std::map<CiString, CAccount*>::iterator It =
        m_CharacterIDMap.find(CharId);

    if (It == m_CharacterIDMap.end()) {
        return;
    }

    CAccount* Account = It->second;
    if (Account != nullptr) {
        Account->DeleteCharacterFromID(CharId);
    }

    m_CharacterIDMap.erase(It);
}

// Retail RVA 0x72D90: the name hashed to its character id, then DeleteCharacterFromID.
bool PlayerDB::DeleteCharacterFromName(CiWString& CharName) {
    if (CharName.empty()) return true;

    std::string CharId = ComputeHashedCharacterID(CharName);

    char FileName[260];
    _makepath_s(FileName, sizeof(FileName), nullptr, nullptr, CharId.c_str(), "fl");
    _strlwr(FileName);

    CiString CharKey(FileName);
    DeleteCharacterFromID(CharKey);

    return true;
}

void PlayerDB::logout_all() {
    if (!m_PlayerArray) return;

    PlayerData* Current = nullptr;
    while (true) {
        Current = traverse_active(Current);
        if (Current == nullptr) break;

        // The session id is the slot's client_id.
        unsigned int SessionId = Current->client_id;
        if (SessionId != 0) {
            logout(SessionId);
        }
    }
}

// PlayerData_LoginAccount -- retail FUN_06d48e80 (thiscall), only caller
// PlayerDB::load_user_data. A locked account returns 7, a banned one 0. The name copy is an
// unbounded wcscpy. Result 6 unless the directory is accessible; every saved character
// loaded (FUN_06d48d50) makes it 3. In single player a missing Restart.fl is created and
// loaded; in MP the login is logged and the account's directory path stored with no null
// test. The account is looked up a SECOND time at the end (create_account may have
// inserted it).
static unsigned char PlayerData_LoginAccount(PlayerData& Slot, const char* AccountDir,
                                             const SLoginInfo& Info, unsigned int SessionId) {
    {
        CiWString LoginName(Info.AccountName, wcslen(Info.AccountName));
        Slot.account_ptr = Players.FindAccountFromName(LoginName);
    }
    CAccount* Account = static_cast<CAccount*>(Slot.account_ptr);
    if (Account != nullptr) {
        if (Account->m_bLocked)
            return 7;
        if (!Account->m_bNotBanned)
            return 0;
    }
    Slot.client_id = SessionId;
    if (Account != nullptr)
        Account->m_ClientID = SessionId;
    wcscpy(Slot.AccountName, Info.AccountName);

    unsigned char Result = 6;
    if (_access(AccountDir, 6) == 0) {
        char SearchPattern[260];
        _makepath(SearchPattern, nullptr, AccountDir, "*", "fl");
        struct _finddata_t FindData;
        intptr_t const FindHandle = _findfirst(SearchPattern, &FindData);
        char RestartName[260];
        sprintf(RestartName, "%s.fl", "Restart");
        bool bFoundRestart = false;
        if (FindHandle != -1) {
            int FindResult = 0;
            while (FindResult == 0) {
                if (PlayerData_LoadSavedCharacter(Slot, AccountDir, FindData.name)) {
                    Result = 3;
                    if (!bFoundRestart && _stricmp(FindData.name, RestartName) == 0)
                        bFoundRestart = true;
                }
                FindResult = _findnext(FindHandle, &FindData);
            }
        }
        _findclose(FindHandle);                                             // also for -1

        if (SinglePlayer()) {
            if (!bFoundRestart && Players.create_restart_file(AccountDir) &&
                PlayerData_LoadSavedCharacter(Slot, AccountDir, RestartName)) {
                Result = 3;
            }
        } else {
            Result = 3;
            if (!SinglePlayer()) {                                          // re-tested in retail
                FDUMP(0x100003, "LOG: Login on account %S (\"%s\")\n",
                      Info.AccountName, AccountDir);
            }
            static_cast<CAccount*>(Slot.account_ptr)->m_DirectoryPath = AccountDir;
        }
    }

    Slot.system_id = 0;
    Slot.ship_object_id = 0;
    Slot.created_ship_id = 0;
    Slot.current_base = 0;
    Slot.entered_base_id = 0;
    Slot.last_base_id = 0;
    Slot.current_location = 0;
    Slot.character_id = 0;
    {
        CiWString LoginName(Info.AccountName, wcslen(Info.AccountName));
        Slot.account_ptr = Players.FindAccountFromName(LoginName);
    }
    Slot.exited_base_id = 0;
    return Result;
}

// PlayerDB::load_user_data -- retail .text:06d533a0, ordinal 499. The account directory is
// the hashed name in MP (FUN_06d46370 inlined, Server_AccountDirName), the plain name in
// SP. A missing directory is created when the login asks for it. Outside single player the
// "name" file is XOR-decoded with the constant key and must match the login name. The slot
// is indexed raw.
unsigned char PlayerDB::load_user_data(const SLoginInfo& Info, unsigned int SessionId) {
    char DirName[260];
    char AccountDir[260];
    if (IsMPServer()) {
        Server_AccountDirName(DirName, Info.AccountName);
        _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), DirName, nullptr);
    } else {
        wcstombs(DirName, Info.AccountName, 0x104);
        _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), DirName, nullptr);
    }

    if (_access(AccountDir, 6) != 0) {
        // Retail tests the low BYTE only.
        if (static_cast<unsigned char>(Info.CreateFlag) == 0)
            return 2;
        if (create_account(Info) != 0)
            return 2;
    }

    if (!SinglePlayer()) {
        char NamePath[260];
        _makepath(NamePath, nullptr, AccountDir, "name", nullptr);
        FILE* NameFile = fopen(NamePath, "rb");
        if (NameFile == nullptr)
            return 2;
        unsigned short Encoded[0x400];
        size_t const CharsRead = fread(Encoded, 2, 0x400, NameFile);
        fclose(NameFile);
        unsigned short const XorKey = AccountNameFileXorKey();
        unsigned short Decoded[0x400];
        for (size_t I = 0; I < CharsRead; ++I)
            Decoded[I] = static_cast<unsigned short>(Encoded[I] ^ XorKey);
        if (CharsRead != wcslen(Info.AccountName))
            return 2;
        if (memcmp(Info.AccountName, Decoded, CharsRead * 2) != 0)
            return 2;
    }

    return PlayerData_LoginAccount(m_PlayerArray[SessionId - 1], AccountDir, Info,
                                   SessionId);
}

// PlayerDB::create_account -- retail .text:06d53d50, ordinal 477. SP: build the directory
// and Restart.fl. MP: refused unless new players are allowed (+0x0c), else CreateAccount.
int PlayerDB::create_account(const SLoginInfo& Info) {
    int Result = -1;
    char DirName[260];
    char AccountDir[260];
    if (IsMPServer()) {
        Server_AccountDirName(DirName, Info.AccountName);
        _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), DirName, nullptr);
    } else {
        wcstombs(DirName, Info.AccountName, 0x104);
        _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), DirName, nullptr);
    }

    if (SinglePlayer()) {
        if (_access(AccountDir, 6) != 0)
            BuildDirectory(AccountDir);
        if (!create_restart_file(AccountDir))
            return Result;
        return 0;
    }

    // FLServer's "Allow New Players" (+0x0c) gates account creation.
    if (!m_bAllowNewPlayers)
        return Result;
    CiWString Name(Info.AccountName, wcslen(Info.AccountName));
    if (CreateAccount(Name))
        Result = 0;
    return Result;
}

// PlayerDB::create_new_character -- retail RVA 0x73FD0, ordinal 478. The character id is
// the HASH of the lower-cased name plus ".fl", the spelling every lookup path expects. An
// id already taken fails. A full account (5 characters) returns TRUE without creating
// anything, as retail does. The save file is written FIRST (FUN_06d4b790); its result
// gates the registration and is returned.
bool PlayerDB::create_new_character(const SCreateCharacterInfo& Info, unsigned int SessionId) {
    std::string HashedId = ComputeHashedCharacterID(std::wstring(Info.Name));
    char FileName[260];
    _makepath(FileName, nullptr, nullptr, HashedId.c_str(), "fl");

    char LowerFileName[260];
    strcpy_s(LowerFileName, sizeof(LowerFileName), FileName);
    _strlwr(LowerFileName);

    CiString CharKey(LowerFileName);
    std::map<CiString, CAccount*>::iterator ExistingIt =
        m_CharacterIDMap.find(CharKey);

    if (ExistingIt != m_CharacterIDMap.end()) {
        return false;
    }

    // The slot is indexed directly; no null test on the account.
    PlayerData* Player = &m_PlayerArray[SessionId - 1];
    CAccount* Account = reinterpret_cast<CAccount*>(Player->account_ptr);

    if (Account->m_Characters.size() >= 5) {
        return true;
    }

    if (!PlayerData_CreateNewCharacter(*Player, Info)) {
        return false;
    }

    // The NAME, onto the account's list.
    Account->m_Characters.push_back(CiWString(Info.Name));

    m_CharacterIDMap[CharKey] = Account;

    return true;
}

// ===========================================================================
// Account management helpers (static, file-local)
// ===========================================================================

// <UserData>\Accts[\MultiPlayer]\<DirName>: in MP the "%02x-%08x" account-CRC hash of the
// LOWER-CASED name (retail always runs _wcsdup + _wcslwr first), in SP wcstombs(name).
static void BuildAccountDir(char* OutPath, const wchar_t* AccountName) {
    char BaseDir[260];
    GetUserDataPath(BaseDir);
    char AcctsDir[260];
    if (!SinglePlayer()) {
        std::wstring Lowered(AccountName);
        for (unsigned int i = 0; i < Lowered.size(); ++i) {
            Lowered[i] = (wchar_t)towlower(Lowered[i]);
        }
        AccountName = Lowered.c_str();
        size_t NameLen = wcslen(AccountName);
        unsigned int Hash = Server_AccountCrc(
            reinterpret_cast<const unsigned char*>(AccountName),
            (unsigned int)(NameLen * sizeof(wchar_t)));
        char HashedName[64];
        sprintf_s(HashedName, sizeof(HashedName), "%02x-%08x", (unsigned int)NameLen, Hash);
        _makepath(AcctsDir, nullptr, BaseDir, "Accts\\MultiPlayer", nullptr);
        _makepath(OutPath, nullptr, AcctsDir, HashedName, nullptr);
    } else {
        char NarrowName[260];
        wcstombs(NarrowName, AccountName, 260);
        _makepath(AcctsDir, nullptr, BaseDir, "Accts", nullptr);
        _makepath(OutPath, nullptr, AcctsDir, NarrowName, nullptr);
    }
}

static void GetAccountsBaseDir(char* OutPath) {
    char BaseDir[260];
    GetUserDataPath(BaseDir);
    if (!SinglePlayer()) {
        _makepath(OutPath, nullptr, BaseDir, "Accts\\MultiPlayer", nullptr);
    } else {
        _makepath(OutPath, nullptr, BaseDir, "Accts", nullptr);
    }
}

static void XorEncodeWString(unsigned short* Out, const wchar_t* In, unsigned short Key) {
    while (*In != L'\0') {
        *Out = (unsigned short)(*In ^ Key);
        ++In;
        ++Out;
    }
}

// PlayerDB::GetCharactersForAccount -- retail 0x719E0 (ordinal 171) returns true and reads
// neither argument (ICF-folded with IServerImpl::SwapConnections).
bool PlayerDB::GetCharactersForAccount(CiWString& AccountName, std::list<CiWString>& OutCharacters) {
    (void)AccountName;
    (void)OutCharacters;
    return true;
}

// PlayerDB::SetAccountPassword -- retail 0x719F0 (ordinal 407).
bool PlayerDB::SetAccountPassword(CiWString& AccountName, CiWString& Password) {
    const wchar_t* NamePtr = AccountName.c_str();

    char AccountDir[260];
    BuildAccountDir(AccountDir, NamePtr);

    // Read AND write permission, not mere existence.
    if (_access(AccountDir, 6) != 0) {
        return false;
    }

    char PasswordPath[260];
    _makepath(PasswordPath, nullptr, AccountDir, "password", nullptr);

    FILE* File = fopen(PasswordPath, "wb");
    if (File != nullptr) {
        const wchar_t* PassPtr = Password.c_str();
        size_t PassLen = wcslen(PassPtr);

        // The XOR key is the low 16 bits of the account CRC of the password itself, so the
        // file is a verification token, not reversible storage.
        unsigned short XorKey = (unsigned short)Server_AccountCrc(
            reinterpret_cast<const unsigned char*>(PassPtr),
            (unsigned int)(PassLen * sizeof(wchar_t)));

        unsigned short Encoded[1024];
        XorEncodeWString(Encoded, PassPtr, XorKey);
        fwrite(Encoded, 2, PassLen, File);
        fclose(File);
    }
    return true;
}

// ===========================================================================
// PlayerDB::BanAccount — retail 0x71D20 (ordinal 62)
// Looks up account in map; if found, creates or deletes "banned" marker file
// and sets the account's banned flag.
// ===========================================================================
bool PlayerDB::BanAccount(CiWString& AccountName, bool bBan) {
    // Look up account in our map
    std::map<CiWString, CAccount*>::iterator It = m_AccountNameMap.find(AccountName);
    if (It == m_AccountNameMap.end() || It->second == nullptr) {
        return false;
    }
    CAccount* Account = It->second;

    // Build account directory and "banned" file path
    const wchar_t* NamePtr = AccountName.c_str();
    char AccountDir[260];
    BuildAccountDir(AccountDir, NamePtr);

    char BannedPath[260];
    _makepath(BannedPath, nullptr, AccountDir, "banned", nullptr);

    if (bBan) {
        FILE* File = fopen(BannedPath, "wb");
        if (File != nullptr) {
            fclose(File);
        }
    } else {
        _unlink(BannedPath);
    }

    // CAccount+0x31 is the "not banned" byte.
    Account->m_bNotBanned = !bBan;
    return true;
}

// PlayerDB::GetAccountBanned -- retail 0x71FD0 (ordinal 145).
bool PlayerDB::GetAccountBanned(CiWString& AccountName) {
    std::map<CiWString, CAccount*>::iterator It = m_AccountNameMap.find(AccountName);
    if (It == m_AccountNameMap.end() || It->second == nullptr) {
        return false;
    }
    CAccount* Account = It->second;
    return Account->m_bNotBanned == false;
}

// PlayerDB::DeleteAccount -- retail 0x72040 (ordinal 98).
void PlayerDB::DeleteAccount(CiWString& AccountName) {
    const wchar_t* NamePtr = AccountName.c_str();

    std::map<CiWString, CAccount*>::iterator It = m_AccountNameMap.find(AccountName);
    CAccount* Account = nullptr;
    if (It != m_AccountNameMap.end()) {
        Account = It->second;
    }

    // Retail drops the account's live client (IClient slot 17) before touching the disk.
    if (Account != nullptr && Account->m_ClientID != 0) {
        extern IClient* g_pClientInterface;
        g_pClientInterface->DisconnectClient(Account->m_ClientID);
    }

    char AccountDir[260];
    BuildAccountDir(AccountDir, NamePtr);

    char SearchPath[260];
    _makepath(SearchPath, nullptr, AccountDir, "*.*", nullptr);

    struct _finddata_t FindData;
    intptr_t FindHandle = _findfirst(SearchPath, &FindData);
    if (FindHandle != -1) {
        do {
            if (FindData.name[0] != '.' && (FindData.attrib & _A_SUBDIR) == 0) {
                char FilePath[260];
                _makepath(FilePath, nullptr, AccountDir, FindData.name, nullptr);
                _unlink(FilePath);
            }
        } while (_findnext(FindHandle, &FindData) == 0);
        _findclose(FindHandle);
    }

    _rmdir(AccountDir);

    if (!SinglePlayer()) {
        FDUMP(0x100003, "LOG: Delete account %S \"%s\"\n", NamePtr, AccountDir);
    }

    if (Account != nullptr) {
        Account->ForceLogout();

        // Retail rebuilds each "<hash>.fl" key from the account's character list and
        // erases it from the character-id map; erasing by stored pointer is equivalent.
        std::map<CiString, CAccount*>::iterator CharIt =
            m_CharacterIDMap.begin();
        while (CharIt != m_CharacterIDMap.end()) {
            if (CharIt->second == Account) {
                m_CharacterIDMap.erase(CharIt++);
            } else {
                ++CharIt;
            }
        }

        m_AccountNameMap.erase(It);

        delete Account;
    }
}

// PlayerDB::CreateAccount -- retail 0x723E0 (ordinal 89). False when the directory already
// exists (read AND write access, tested before and after BuildDirectory).
bool PlayerDB::CreateAccount(CiWString& AccountName) {
    const wchar_t* NamePtr = AccountName.c_str();

    char AccountDir[260];
    BuildAccountDir(AccountDir, NamePtr);

    if (_access(AccountDir, 6) == 0) {
        return false;
    }

    BuildDirectory(AccountDir);

    if (_access(AccountDir, 6) != 0) {
        return false;
    }

    char NamePath[260];
    _makepath(NamePath, nullptr, AccountDir, "name", nullptr);

    FILE* NameFile = fopen(NamePath, "wb");
    if (NameFile == nullptr) {
        DeleteAccount(AccountName);
        return false;
    }

    // The constant marker-string key.
    size_t NameLen = wcslen(NamePtr);
    unsigned short XorKey = AccountNameFileXorKey();
    unsigned short Encoded[1024];
    XorEncodeWString(Encoded, NamePtr, XorKey);
    fwrite(Encoded, 2, NameLen, NameFile);
    fclose(NameFile);

    if (!SinglePlayer()) {
        FDUMP(0x100003, "LOG: Create account %S \"%s\"\n", NamePtr, AccountDir);
    }

    // operator new(0x38); InitFromFolder fills the name from the "name" file just written.
    CAccount* NewAccount = new CAccount();
    NewAccount->InitFromFolder(AccountDir);

    // Keyed by the ACCOUNT's own name.
    m_AccountNameMap[NewAccount->m_Name] = NewAccount;

    return true;
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
// system told (StarSystem_SendGroupChange, FUN_06d0f1c0). No IsFull test (the callers do
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
        StarSystem_SendGroupChange(System->second, Joiner.ship_object_id);
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
        StarSystem_SendGroupChange(System->second, Leaver.ship_object_id);
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

// CPlayerGroup::RewardMembers -- retail .text:06d5c2b0, ordinal 389: PlayerData_AdjustCash
// (bCountEarned = true) on every member, indexed raw.
void CPlayerGroup::RewardMembers(int Amount) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        PlayerData_AdjustCash(Players[*It], Amount, true);
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

// ===========================================================================
// SrvAsteroid -- the server's per-system asteroid bookkeeping. Retail source file
// "E:\FL\Scratch\Source\Server\Space\SrvAsteroid.cpp" (.rdata:06d6777c).
//
// Three levels, all owned by the Server.dll-static map DAT_06d8d980:
//   std::map<unsigned int, SrvAsteroidSystem>   one per loaded system
//     SrvAsteroidSystem::m_Fields               one FSrvAsteroidField per
//                                               CmnAsteroid::CAsteroidField
//       FSrvAsteroidField::Cubes                one FSrvAsteroidCube per cube
//                                               that reported changes
//         FSrvAsteroidCube::Objects             the server object standing in
//                                               for each live asteroid/mine
// The containers are std:: containers, not VC6 layouts; every member keeps its retail
// offset in a comment.
//
// The server objects are MedAsteroid (ctor FUN_06d04ba0, vtable .rdata:06d67894) and
// MedMine (ctor FUN_06d04c70, vtable .rdata:06d67aec), declared in server_gameobject.h;
// StarSystem makes them (create_med_asteroid / create_med_mine, FUN_06d09bb0 /
// FUN_06d09c80).
// ===========================================================================

namespace SrvAsteroid {

namespace {

// .rdata:06d6777c -- the __FILE__ of SrvAsteroidSystem::load's FDUMP.
char const* const SrvAsteroidSource = "E:\\FL\\Scratch\\Source\\Server\\Space\\SrvAsteroid.cpp";

// FUN_06d03cf0 (cdecl); the same body is CopyBoundedString in server_savegame.cpp.
// SrvAsteroidSystem::load inlines the null arm itself.
unsigned int SrvAsteroid_CopyBounded(char* Dst, unsigned int Size, char const* Src) {
    unsigned int Length = static_cast<unsigned int>(strlen(Src));
    if (!(Length < Size))
        Length = Size - 1;
    memcpy(Dst, Src, Length);
    Dst[Length] = '\0';
    return Length;
}

// DAT_06d8d980 (static initializer LAB_06d03270, atexit FUN_06d032a0).
typedef std::map<unsigned int, SrvAsteroidSystem> FSrvAsteroidSystemMap;
FSrvAsteroidSystemMap GSrvAsteroidSystems;

} // namespace

// ---------------------------------------------------------------------------
// MedAsteroid / MedMine. Declarations and the vtable dump in
// src/include/server_gameobject.h.
// ---------------------------------------------------------------------------

// FUN_06d04ba0 (vtables .rdata:06d67894 / 06d67814 / 06d67808).
MedAsteroid::MedAsteroid()
    : Field_0x60(0)
    , Field_0x64(nullptr)
{
}

// FUN_06d04c00 (scalar deleting destructor FUN_06d04bd0).
MedAsteroid::~MedAsteroid()
{
    if (Field_0x64 != nullptr)
        operator delete(Field_0x64);
}

// LAB_06d02cb0: returns false; the same body sits in 21 slots across Server.dll's wrappers.
bool MedAsteroid::create(Archetype::Root* Arch)
{
    (void)Arch;
    return false;
}

// LAB_06d04c30, shared by MedAsteroid and MedMine. Unlike Ship::find
// (GameObject::FindOfClass), advise runs only on a hit.
bool MedAsteroid::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

// Empty (.text:06d28880, ICF-folded with IServerImpl::Connect).
void MedAsteroid::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// Empty (.text:06d28880).
void MedAsteroid::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// Empty (.text:06d28880).
void MedAsteroid::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// Empty (.text:06ce1050). MedMine does NOT override this slot; it keeps GameObject's
// FUN_06ceee70.
void MedAsteroid::apply_damage_list(DamageList& Damage)
{
    (void)Damage;
}

// FUN_06d04c70 (vtables .rdata:06d67aec / 06d67a6c / 06d67a60, CObjectCB .rdata:06d67a54 at
// +0x60). Retail leaves +0x64, +0x68, +0x6c..+0x74 and +0x84 for arm() to write; the port
// zeroes them.
MedMine::MedMine()
    : Phantom(nullptr)
    , IntruderCount(0)
    , Explosion(nullptr)
    , RechargeTimer(1.0f)
    , RechargeTime(1.0f)
    , ExplosionOffset(0.0f)
{
    Direction.x = 0.0f;
    Direction.y = 0.0f;
    Direction.z = 0.0f;
}

// FUN_06d04ce0 (scalar deleting destructor FUN_06d04cb0). No null test before
// DestroyPhantom.
MedMine::~MedMine()
{
    PhySys::DestroyPhantom(Phantom);
    Phantom = nullptr;
}

// LAB_06d02cb0 -- see MedAsteroid::create.
bool MedMine::create(Archetype::Root* Arch)
{
    (void)Arch;
    return false;
}

// LAB_06d04c30 -- see MedAsteroid::find (the same body, ClassAsteroid 0x1003).
bool MedMine::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

// Empty (.text:06d28880).
void MedMine::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// Empty (.text:06d28880).
void MedMine::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// Empty (.text:06d28880).
void MedMine::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// Empty (.text:06d28880) -- MedMine's own +0x1bc (.rdata:06d67ca8).
void MedMine::Slot1bc(unsigned int Arg1, unsigned int Arg2)
{
    (void)Arg1;
    (void)Arg2;
}

// FUN_06d04d60: a phantom of the detect radius at the mine's position, listening through
// the CObjectCB sub-object (+0x60), attached to the raw CBase::Object; then the explosion,
// offset and recharge time from the archetype. Name[1..9] of the parms is never written
// (stack garbage in retail).
void MedMine::arm(float DetectRadius, unsigned int SystemId)
{
    IntruderCount = 0;

    PhySys::PhantomParms Parms;
    Parms.bPhysicalUnmoveable = 0;
    Parms.Flag05 = 0;
    Parms.Name[0] = '\0';
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.SystemId = SystemId;
    Parms.Position = get_position();
    Parms.Listener = static_cast<PhySys::CObjectCB*>(this);

    PhySys::CreatePhantom(Parms, DetectRadius, Phantom);
    PhySys::AttachPhantom(Object, Phantom);
    PhySys::SetCollisionStatePhantom(Phantom, true);

    Archetype::Asteroid const* const Arch =
        static_cast<Archetype::Asteroid const*>(Object->get_archetype());
    Explosion = Archetype::GetExplosion(Arch->ExplosionArch);
    ExplosionOffset = Arch->ExplosionOffset;
    RechargeTime = Arch->RechargeTime;
    RechargeTimer = Arch->RechargeTime;
}

// LAB_06d04e50 -- MedMine's update (+0x16c). While an intruder is inside and the recharge
// timer has run out, the mine explodes (when the explosion has a radius and any damage
// above 0.0001, DAT_06d65190) and the mine-explosion packet goes out; a failed explosion
// test skips only the explode call, not the packet or the timer reset.
bool MedMine::update(float Dt)
{
    if (RechargeTimer < RechargeTime)
        RechargeTimer = Dt + RechargeTimer;

    if (IntruderCount != 0 && RechargeTimer >= RechargeTime) {
        ExplosionDamageEvent Blast;
        Blast.ExplodingObjId = get_id();
        Blast.InflictorId = get_id();
        // 7 is not one of common_misc.h's (unsourced) DamageCause names; retail's literal.
        Blast.Cause = static_cast<DamageCause>(7);
        Blast.Position = get_position();
        Blast.Explosion = Explosion;
        Blast.SkipCollisionGroup = nullptr;
        if (Explosion != nullptr && Explosion->Radius > 0.0001 &&
            (Explosion->Impulse > 0.0001 || Explosion->HullDamage > 0.0001 ||
             Explosion->EnergyDamage > 0.0001))
            OwnerSystem->explode(Blast);

        FMineExplosion Rec;
        Rec.Offset.x = ExplosionOffset * Direction.x;
        Rec.Offset.y = ExplosionOffset * Direction.y;
        Rec.Offset.z = ExplosionOffset * Direction.z;
        Rec.MineId = get_id();
        OwnerSystem->send_mine_explosion(Rec);
        RechargeTimer = 0.0f;
    }
    return !is_destroyed();
}

// LAB_06d04fb0 -- CObjectCB slot 0 (this + 0x60). A ship root counts as an intruder; the
// direction is from the mine to the ARGUMENT's own position (not its root), normalised with
// no zero test.
void MedMine::added(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr)
        return;
    if ((static_cast<unsigned int>(Root->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;

    Vector const& MinePos = get_position();
    Direction.x = Obj->WorldPosition.x - MinePos.x;
    Direction.y = Obj->WorldPosition.y - MinePos.y;
    Direction.z = Obj->WorldPosition.z - MinePos.z;

    float const Length = sqrtf((Direction.x * Direction.x + Direction.y * Direction.y) +
                               Direction.z * Direction.z);
    float const Inverse = 1.0f / Length;
    Direction.x = Inverse * Direction.x;
    Direction.y = Inverse * Direction.y;
    Direction.z = Inverse * Direction.z;

    ++IntruderCount;
}

// LAB_06d05070 -- CObjectCB slot 1.
void MedMine::deleted(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr)
        return;
    if ((static_cast<unsigned int>(Root->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;
    --IntruderCount;
}

// The value of a field entry's cube map, 0x24 bytes (offsets from FUN_06d04480).
struct FSrvAsteroidCube {
    CmnAsteroid::CAsteroidCube* Cube;              // +0x00
    unsigned int SystemId;                         // +0x04 (FUN_06d04ab0 reads it for SysMap)
    unsigned long CubeId;                          // +0x08
    int CubeSize;                                  // +0x0c
    // +0x10..+0x20: asteroid_marker::AsteroidId -> the server object (node+0x10).
    // Size at +0x20 is what FUN_06d04480's prune pass tests (node+0x30).
    std::map<unsigned long, GameObject*> Objects;

    FSrvAsteroidCube()
        : Cube(nullptr)
        , SystemId(0)
        , CubeId(0)
        , CubeSize(0xfa) {
    }

    // FUN_06d048f0 / FUN_06d046c0: destroy_notify on every object, in map order.
    ~FSrvAsteroidCube() {
        for (std::map<unsigned long, GameObject*>::iterator It = Objects.begin(); It != Objects.end(); ++It)
            It->second->destroy_notify();
    }

    void apply_marker(CmnAsteroid::asteroid_marker const* Marker);
};

// FUN_06d04ab0 (thiscall); only caller FUN_06d04480. An inactive cube destroys the marker's
// object; an active one with no object creates a MedMine (armed with the archetype's detect
// radius, read through cobject() with no class narrowing) or a MedAsteroid, with no end
// check on the SysMap lookup. An active one with an object calls get_position and drops the
// result.
void FSrvAsteroidCube::apply_marker(CmnAsteroid::asteroid_marker const* Marker) {
    std::map<unsigned long, GameObject*>::iterator It = Objects.find(Marker->AsteroidId);
    if (!Marker->bCubeActive) {
        if (It != Objects.end()) {
            It->second->destroy_notify();
            Objects.erase(It);
        }
        return;
    }
    if (It == Objects.end()) {
        StarSystem& System = SysDB::SysMap.find(SystemId)->second;
        GameObject* Obj = nullptr;
        if (Marker->bMine) {
            MedMine* const Mine = System.create_med_mine(Marker->ObjId);
            Archetype::Asteroid const* const Arch =
                static_cast<Archetype::Asteroid const*>(Mine->cobject()->get_archetype());
            Mine->arm(Arch->DetectRadius, SystemId);
            Obj = Mine;
        } else {
            Obj = System.create_med_asteroid(Marker->ObjId);
        }
        Objects.insert(std::map<unsigned long, GameObject*>::value_type(Marker->AsteroidId, Obj));
        return;
    }
    It->second->get_position();
}

// A field entry, operator_new(0x20).
struct FSrvAsteroidField {
    CmnAsteroid::CAsteroidField* Field;                   // +0x00
    std::map<unsigned long, FSrvAsteroidCube> Cubes;      // +0x04..+0x14, keyed by CubeId
    int CubeSize;                                         // +0x18
    unsigned int SystemId;                                // +0x1c

    FSrvAsteroidField()
        : Field(nullptr)
        , CubeSize(0xfa)
        , SystemId(0) {
    }

    // FUN_06d03fc0 (scalar deleting destructor FUN_06d038a0); the cube map's destructor
    // runs after this body, as in retail.
    ~FSrvAsteroidField() {
        if (Field != nullptr)
            Field->Release();
    }

    void set_field(CmnAsteroid::CAsteroidField* InField);
    void load(char const* Path);
    void update();
};

// FUN_06d04810. The previous field is not released.
void FSrvAsteroidField::set_field(CmnAsteroid::CAsteroidField* InField) {
    Field = InField;
    if (InField != nullptr) {
        InField->AddRef();
        CubeSize = Field->CubeSize;
    }
}

// FUN_06d04140 (thiscall; the [Asteroids] "file" value), only caller SrvAsteroidSystem::load.
// [Field] cube_size / fill_dist / empty_cube_frequency are ftol'd ints; with a field set,
// the reader is rewound for CAsteroidField::load and the two values stored (the frequency
// as a float).
void FSrvAsteroidField::load(char const* Path) {
    int FillDist = 0;
    int EmptyFreq = 0;
    INI_Reader Reader;
    char FullPath[0x100];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (!Reader.open(FullPath, false))
        return;

    INI_Reader::State Start;
    Reader.get_state(Start);
    while (Reader.read_header()) {
        if (!Reader.is_header("Field"))
            continue;
        while (Reader.read_value()) {
            // x87: ftol straight on the unrounded ST0 (64-bit truncation, low dword stored).
            if (Reader.is_value("cube_size"))
                CubeSize = (int)(long long)Reader.get_value_float(0);
            else if (Reader.is_value("fill_dist"))
                FillDist = (int)(long long)Reader.get_value_float(0);
            else if (Reader.is_value("empty_cube_frequency"))
                EmptyFreq = (int)(long long)Reader.get_value_float(0);
        }
    }

    if (Field != nullptr) {
        Reader.set_state(Start);
        Field->load(Reader, CubeSize);
        Field->FillDist = FillDist;
        Field->EmptyFreq = static_cast<float>(EmptyFreq);
    }
}

// FUN_06d04480 (thiscall on the entry); callers SrvAsteroidSystem::update and FUN_06d03480.
// Each changed marker is applied to its cube record (created on first sight); then every
// record with no objects is erased.
void FSrvAsteroidField::update() {
    CmnAsteroid::asteroid_marker* Marker = Field->get_changes();
    while (Marker != nullptr) {
        FSrvAsteroidCube* Record;
        std::map<unsigned long, FSrvAsteroidCube>::iterator It = Cubes.find(Marker->CubeId);
        if (It == Cubes.end()) {
            It = Cubes.insert(std::make_pair(Marker->CubeId, FSrvAsteroidCube())).first;
            Record = &It->second;
            CmnAsteroid::CAsteroidCube* Cube = Field->find_cube(Marker->CubeId);
            if (Cube != nullptr) {
                Record->Cube = Cube;
                Record->CubeId = Cube->get_id();
                Record->CubeSize = CubeSize;
                Record->SystemId = SystemId;
            }
        } else {
            Record = &It->second;
        }
        Record->apply_marker(Marker);
        Marker = Marker->Next;
    }

    std::map<unsigned long, FSrvAsteroidCube>::iterator It = Cubes.begin();
    while (It != Cubes.end()) {
        if (It->second.Objects.size() == 0)
            It = Cubes.erase(It);
        else
            ++It;
    }
}

// Retail .text:06d03710 (ordinal 17).
SrvAsteroidSystem::SrvAsteroidSystem()
    : m_CmnSystem(nullptr)
    , m_SystemId(0)
    , m_RefCount(0) {
}

// Retail .text:06ce30d0 (ordinal 16): member-wise; CmnSystem without an AddRef, the entry
// pointers shallow.
SrvAsteroidSystem::SrvAsteroidSystem(const SrvAsteroidSystem& Other)
    : m_CmnSystem(Other.m_CmnSystem)
    , m_Fields(Other.m_Fields)
    , m_SystemId(Other.m_SystemId)
    , m_RefCount(Other.m_RefCount) {
}

// Retail .text:06d03740 (ordinal 22).
SrvAsteroidSystem::~SrvAsteroidSystem() {
    if (m_CmnSystem != nullptr)
        m_CmnSystem->Release();
    for (std::list<FSrvAsteroidField*>::iterator It = m_Fields.begin(); It != m_Fields.end(); ++It) {
        if (*It != nullptr)
            delete *It;
    }
    m_Fields.clear();
}

// Retail .text:06ce3170 (ordinal 32): CmnSystem copied without AddRef; retail's self check
// guards only the list assign.
SrvAsteroidSystem& SrvAsteroidSystem::operator=(const SrvAsteroidSystem& Other) {
    m_CmnSystem = Other.m_CmnSystem;
    m_Fields = Other.m_Fields;
    m_SystemId = Other.m_SystemId;
    m_RefCount = Other.m_RefCount;
    return *this;
}

// Retail .text:06d038c0 (ordinal 498): every [Asteroids] section with both a "file" and a
// "zone" becomes a field entry. A missing zone FDUMPs and retail then calls through it
// anyway.
void SrvAsteroidSystem::load(const char* Path) {
    INI_Reader Reader;
    char FullPath[0x100];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (!Reader.open(FullPath, false))
        return;

    while (Reader.read_header()) {
        if (!Reader.is_header("Asteroids"))
            continue;

        unsigned int FileLen = 0;
        char FileName[0x100];
        FileName[0] = '\0';
        unsigned int ZoneLen = 0;
        char ZoneName[0x40];
        ZoneName[0] = '\0';

        while (Reader.read_value()) {
            if (Reader.is_value("file")) {
                char const* Value = Reader.get_value_string();
                if (Value == nullptr) {
                    FileLen = 0;
                    FileName[0] = '\0';
                } else {
                    FileLen = SrvAsteroid_CopyBounded(FileName, 0x100, Value);
                }
            } else if (Reader.is_value("zone")) {
                char const* Value = Reader.get_value_string();
                if (Value == nullptr) {
                    ZoneLen = 0;
                    ZoneName[0] = '\0';
                } else {
                    ZoneLen = SrvAsteroid_CopyBounded(ZoneName, 0x40, Value);
                }
            }
        }

        if (FileLen == 0 || ZoneLen == 0)
            continue;

        CmnAsteroid::CAsteroidField* Field = CmnAsteroid::CreateAsteroidField(m_SystemId);
        m_Fields.push_back(new FSrvAsteroidField());
        FSrvAsteroidField* Entry = m_Fields.back();
        Entry->Field = Field;
        if (Field != nullptr) {
            Field->AddRef();
            Entry->CubeSize = Entry->Field->CubeSize;
        }
        Entry->SystemId = m_SystemId;

        Universe::IZone const* Zone = Universe::get_zone(m_SystemId, ZoneName);
        if (Zone == nullptr)
            FDUMP(0x100001, "%s(%d) : *** ERROR: AsteroidField linked to missing Zone (%s)",
                  SrvAsteroidSource, 0xa1, ZoneName);
        // Zone vtable +0x18, the shape id; retail discards the result. A null zone faults
        // here, as in retail.
        static_cast<void>(Zone->Shape);
        Field->set_zone(Zone);
        Entry->load(FileName);
    }
}

// Retail .text:06d03d40 (ordinal 520): FUN_06d04480 on every field entry.
void SrvAsteroidSystem::update() {
    for (std::list<FSrvAsteroidField*>::iterator It = m_Fields.begin(); It != m_Fields.end(); ++It)
        (*It)->update();
}

// Retail .text:06d03d70 (ordinal 514).
void SrvAsteroidSystem::set_sys_id(unsigned int SystemId) {
    m_SystemId = SystemId;
}

// Retail .text:06d03d80 (ordinal 511). The previous pointer is NOT released.
void SrvAsteroidSystem::set_cmn_system(CmnAsteroid::CAsteroidSystem* Sys) {
    m_CmnSystem = Sys;
    if (Sys == nullptr) {
        m_SystemId = 0;
        return;
    }
    Sys->AddRef();
    m_SystemId = m_CmnSystem->get_sys_id();
}

// Retail .text:06d03db0 (ordinal 504). No null check on CmnSystem.
void SrvAsteroidSystem::map_asteroid_fields() {
    CmnAsteroid::CAsteroidField* Field = m_CmnSystem->FindFirst();
    while (Field != nullptr) {
        Field->AddRef();
        m_Fields.push_back(new FSrvAsteroidField());
        FSrvAsteroidField* Entry = m_Fields.back();
        Entry->set_field(Field);
        Entry->SystemId = m_SystemId;
        Field->Release();
        Field = m_CmnSystem->FindNext();
    }
}

// Retail .text:06d03ed0 (ordinal 54).
int SrvAsteroidSystem::AddRef() {
    return ++m_RefCount;
}

// Retail .text:06d03ee0 (ordinal 358).
int SrvAsteroidSystem::Release() {
    return --m_RefCount;
}

// FUN_06d032b0: 2 when the system is already loaded (ref-counted), else 1 after loading.
int SrvAsteroid_LoadSystem(unsigned int SystemId, const char* Path) {
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It != GSrvAsteroidSystems.end()) {
        ++It->second.m_RefCount;
        return 2;
    }
    It = GSrvAsteroidSystems.insert(std::make_pair(SystemId, SrvAsteroidSystem())).first;
    ++It->second.m_RefCount;
    It->second.m_SystemId = SystemId;
    It->second.load(Path);
    return 1;
}

// FUN_06d03410: 0 when missing, 2 while still referenced, 1 after the erase.
int SrvAsteroid_UnloadSystem(unsigned int SystemId) {
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It == GSrvAsteroidSystems.end())
        return 0;
    if (--It->second.m_RefCount != 0)
        return 2;
    GSrvAsteroidSystems.erase(It);
    return 1;
}

// FUN_06d03480 (cdecl). The argument is the StarSystem as an IObjDB; GetZoneDb gives its
// Universe::ISystem, whose id (NicknameId in this port) keys the map.
void SrvAsteroid_UpdateSystem(IObjDB* Db) {
    Universe::ISystem const* Info = Db->GetZoneDb();
    unsigned int SystemId = Info->NicknameId;
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It == GSrvAsteroidSystems.end())
        return;
    for (std::list<FSrvAsteroidField*>::iterator Field = It->second.m_Fields.begin();
         Field != It->second.m_Fields.end(); ++Field)
        (*Field)->update();
}

// FUN_06d03500 (cdecl), single player. No find first: an existing entry is kept and reused,
// and its fields are mapped again.
void SrvAsteroid_MapSystem(unsigned int SystemId) {
    CmnAsteroid::CAsteroidSystem* Cmn = CmnAsteroid::Find(SystemId);
    if (Cmn == nullptr)
        return;
    Cmn->AddRef();
    FSrvAsteroidSystemMap::iterator It =
        GSrvAsteroidSystems.insert(std::make_pair(SystemId, SrvAsteroidSystem())).first;
    SrvAsteroidSystem& System = It->second;
    ++System.m_RefCount;
    System.set_cmn_system(Cmn);
    System.map_asteroid_fields();
    Cmn->Release();
}

} // namespace SrvAsteroid

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

// Players -- `?Players@@3VPlayerDB@@A`, ordinal 352, .data:06d90260. The port's only player
// table. The ctor only nulls pointers and default-constructs containers, and nothing
// touches the table before IServerImpl::Startup calls Players.init().
SERVER_API PlayerDB Players;

// SysDB::SysMap -- RVA 0xADA2C, export ordinal 460: THE system table.
namespace SysDB {
    SysMapType SysMap;
}

// Chat hooks -- RVAs 0xAF604, 0xAF608, 0xAF60C. Retail type void (__cdecl*)(unsigned short*,
// int) (VC6's wchar_t); the text is the decoded chat message (DAT_06d8e9b0).
typedef void (__cdecl *ChatHookFn)(wchar_t*, int);
SERVER_API ChatHookFn g_pUniverseChatHook = nullptr;  // RVA 0xAF604
SERVER_API ChatHookFn g_pPrivateChatHook = nullptr;   // RVA 0xAF608
SERVER_API ChatHookFn g_pSystemChatHook = nullptr;    // RVA 0xAF60C

// The `Server` export (ordinal 524) is the IServerImpl object, defined in IServerImpl.cpp.

// StarSystem::count_players -- retail RVA 0x2BCB0, ordinal 476: the players whose id
// (node+0x40) is not ExcludeId. No null test on the sentinel.
unsigned int StarSystem::count_players(unsigned int ExcludeId) const
{
    unsigned int Count = 0;
    SystemPlayerNode const* const Sentinel = static_cast<SystemPlayerNode const*>(PlayerListHead);
    for (SystemPlayerNode const* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->View.PlayerId != ExcludeId) {
            Count++;
        }
    }
    return Count;
}

// StarSystem::remove_player -- retail FUN_06d08740, the first half of both system-exit
// entry points (FUN_06d087f0 and FUN_06d08890).
void StarSystem::remove_player(unsigned int SessionId)
{
    // (a) Unlink this session's player node (one match only). Retail dereferences the
    //     head at once, with no null test.
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    // KNOWN CRASH (intentional, retail-faithful): a fault on a garbage Sentinel coming from
    // PlayerDB_LeaveSystem means `this` is SysMap's unconstructed head node -- a SysMap
    // miss (bad universe.ini system id). See PlayerDB_LeaveSystem.
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->View.PlayerId == SessionId) {
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            // The element is embedded IN the node at +0x08 (a LEA, despite Ghidra's
            // rendering); retail calls its scalar deleting destructor with 0 -- destroy in
            // place, do not free -- and frees the node separately.
            Observer* const Element = &Node->View;
            Element->~Observer();
            ::operator delete(Node);
            --PlayerListSize;
            break;
        }
    }

    // (b) Destroy every live ship this session owns. The next node is read AFTER the
    //     destroy. Retail reads CSimple::OwnerPlayer (+0xb4) with no test after the (& 3)
    //     narrowing, so a non-CSimple object faults; every object on this list is a
    //     CSimple there, and the port does the same unguarded read.
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->csimple()->OwnerPlayer != SessionId)
            continue;
        if (Obj->is_destroyed())
            continue;
        Obj->destroy(0, Obj->get_id());
    }
}

// StarSystem::player_switched_out -- retail FUN_06d087f0. See server_sysdb.h.
void StarSystem::player_switched_out(unsigned int SessionId, unsigned int DestSystemId)
{
    remove_player(SessionId);

    // The raw slot.
    PlayerData_FlushPendingVisits(Players[SessionId]);

    if (DestSystemId != 0u)
    {
        unsigned int Payload[3];
        Payload[0] = SystemInfo->NicknameId;
        Payload[1] = DestSystemId;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // Single player: the system turns itself off.
    if (SinglePlayer() && bLoaded != 0)
    {
        bLoaded = 0;
        unsigned int SystemNickname = SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemUnloaded, &SystemNickname);
        unload();                                                   // FUN_06d072c0
    }
}

// FUN_06d0d8a0. See server_sysdb.h for the map.
bool StarSystem::activate_object(GameObject* Obj, bool bOn, unsigned int Index)
{
    EqObj* const Eq = dynamic_cast<EqObj*>(Obj);
    if (Eq == nullptr)
        return false;

    // FUN_06ce7850: the (& 0x103) narrowing of the raw object (not null-tested), then
    // CEqObj::activate.
    ObjActivateResult const Result = Eq->activate(bOn, Index);

    // Only an actual change is announced.
    if (Result == ObjActivateFailed)
        return false;
    if (Result != ObjActivateChanged)
        return true;

    // A solar is announced to every player, anything else only where seen.
    CObject const* Solar = Obj->cobject();
    if (Solar != nullptr &&
        (static_cast<unsigned int>(Solar->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        Solar = nullptr;

    // The pad bytes are never written.
    FPayloadServerActivateObject Rec;
    Rec.ObjectId = Obj->get_id();
    Rec.bActivate = static_cast<unsigned char>(bOn);
    Rec.ActivateParam = Index;

    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Solar == nullptr && !SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        ::g_pClientInterface->SendServerActivateObject(Node->View.PlayerId, Rec);
    }

    return true;
}

// FUN_06d0d9b0. See server_sysdb.h for the map.
void StarSystem::spawn_mined_loot(Vector const& Pos, unsigned int ContainerArchId,
                                  unsigned int ContentsArchId, int Count)
{
    // Bytes +0x6a/+0x6b of the record are never written.
    FLootRecord Rec;
    Rec.Arch = nullptr;
    Rec.HitPts = 0.0f;
    Rec.ContentsHitPts = 0.0f;
    Rec.Count = 0;
    Rec.MarkForPlayer = 0u;
    Rec.NameId = 0u;
    Rec.bCanAiTractor = 1;
    Rec.bMission = 0;

    // Allocated before any archetype is looked up, so spent even on the early outs.
    Rec.Id = CFLIDMaker::GetInstance()->AllocateObjectID();

    Archetype::Equipment* const Contents = Archetype::GetEquipment(ContentsArchId);
    if (Contents == nullptr)
        return;
    Rec.ContentsArch = Contents;
    Rec.ContentsHitPts = Contents->HitPts;
    Rec.Count = Count;

    // A zero or unknown container falls back to the contents' loot appearance.
    Archetype::Equipment* Container = nullptr;
    if (ContainerArchId != 0u)
        Container = Archetype::GetEquipment(ContainerArchId);
    if (Container == nullptr)
    {
        Container = Contents->get_loot_appearance();
        if (Container == nullptr)
            return;
    }
    Rec.Arch = Container;
    Rec.HitPts = Container->HitPts;

    Rec.Position = Pos;

    Rec.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                             0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 1.0f);

    Rec.Velocity = random_inside_sphere(20.0f);

    Rec.AngularVelocity = random_inside_sphere(3.0f);

    create_loot(0u, 3, &Rec, 1);                                    // FUN_06d08a40
}

// FUN_06d0e7a0. See server_sysdb.h for the map.
void StarSystem::send_tractor_objects(unsigned int const& SkipPlayerId, XTractorObjects const& Info)
{
    // Every player but SkipPlayerId that sees the tractoring object gets the target ids it
    // can see (sent even when none survive). The pad word +0x06 is never written.
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        if (Node->View.PlayerId == SkipPlayerId)
            continue;

        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Info.ObjectId))
            continue;

        XTractorObjects Msg;
        Msg.ObjectId = Info.ObjectId;
        Msg.TractorEquipId = Info.TractorEquipId;

        int const Count = static_cast<int>(Info.TargetIds.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            if (SystemPlayerView_IsObjectIdVisible(&Node->View, Info.TargetIds[Index]))
                Msg.TargetIds.push_back(Info.TargetIds[Index]);
        }

        ::g_pClientInterface->SendCommonTractorObjects(Node->View.PlayerId, Msg);
    }
}

// FUN_06d0ed40. See server_sysdb.h for the map.
void StarSystem::tractor_loot(::Loot* LootObj, EqObj* Ship, unsigned int Space, unsigned short TractorId)
{
    // Not null-tested before use.
    CLoot* const Cl = (LootObj != nullptr) ? LootObj->cloot() : nullptr;

    // Take = min(Space, Units), unsigned.
    Archetype::Equipment const* const Contents = Cl->ContentsArch;
    unsigned int const Units = Cl->Units;
    unsigned int const ContentsArchId = Contents->get_id();
    unsigned int Take = Space;
    if (Take > Units)
        Take = Units;

    float Health = 1.0f;

    // The carried health.
    if (!arch_is_combinable(ContentsArchId))
    {
        Health = Cl->ContentsHitPts / Contents->HitPts;
    }
    else if (Contents->get_class_type() == Archetype::AClassCommodity)
    {
        // Only a strictly positive decay, and only when the whole stack is taken.
        Archetype::Commodity const* const CommodityArch =
            static_cast<Archetype::Commodity const*>(Contents);
        if (CommodityArch->get_decay_per_second() > 0.0f && Take == Units)
        {
            // x87: the difference stays at extended precision until the final float store.
            double Remaining = static_cast<double>(Cl->ContentsHitPts) -
                               static_cast<double>(LootObj->DecayAccum);
            if (!(Remaining > 0.0))
                Remaining = 0.0;
            Health = static_cast<float>(Remaining / static_cast<double>(Contents->HitPts));
        }
    }

    // The CLoot's bTemporary is the mission flag.
    unsigned short const SubObjId = add_cargo(Ship, ContentsArchId, Take, Health, Cl->bTemporary);

    // Controller event 0x18. The word at +0x0a is never written.
    struct FControllerMsg18 {
        unsigned int LootId;          // +0x00
        unsigned int ShipId;          // +0x04
        unsigned short SubObjId;      // +0x08  add_cargo's result
        unsigned short Pad0a;         // +0x0a
        unsigned int GoodId;          // +0x0c
        unsigned int Count;           // +0x10  Take
    } Msg18;
    Msg18.LootId = LootObj->get_id();
    Msg18.ShipId = Ship->get_id();
    Msg18.GoodId = Arch2Good(ContentsArchId);
    Msg18.SubObjId = SubObjId;
    Msg18.Count = Take;
    Server_ControllerDispatch(0, pub::Controller::EMessage::LootAcquired, &Msg18);

    // The inlined constructor's defaults, then overwritten field by field.
    FPayloadServerItemTractored Tractored;
    Tractored.LootId = 0u;
    Tractored.ShipId = 0u;
    Tractored.TractorEquipId = SUBOBJ_ID_NONE;
    Tractored.UnitsTaken = 0;
    Tractored.LootId = LootObj->get_id();
    Tractored.ShipId = Ship->get_id();
    Tractored.TractorEquipId = TractorId;
    Tractored.UnitsTaken = static_cast<unsigned short>(Take);

    // Every player that sees the ship.
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Tractored.ShipId))
            continue;
        ::g_pClientInterface->SendServerItemTractored(Node->View.PlayerId, Tractored);
    }

    // Part of the stack: the rest stays in space.
    if (Take != Units)
    {
        Cl->Units = Units - Take;
        return;
    }

    // A player's ship in single player keeps its tractor target (CSimple not null-tested).
    if (!(Ship->csimple()->OwnerPlayer != 0 && SinglePlayer()))
    {
        // The CShip narrowing is not null-tested before its EquipManager is used.
        CObject* const Raw = Ship->Object;
        CShip* const ShipCobj =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CShip*>(Raw) : nullptr;
        CETractor* const Tractor = CETractor::cast(ShipCobj->EquipManager.FindByID(TractorId));
        if (Tractor != nullptr)
            Tractor->RemoveTarget(LootObj->get_id());
    }

    unsigned int const ShipId = Ship->get_id();
    LootObj->destroy(2, ShipId);

    Cl->Units = 0u;
}

// LAB_06d06a40 -- SrvTractor's vtable slot 0 (.rdata:06d65544).
void SrvTractor::notify(Event Code, void* Param)
{
    // Event 0x18: TractorArm::Update's consume timer ran out; Param is the TractorArm.
    if (Code == TractorArmAcquireTimeUp)
    {
        CLoot* const Target = static_cast<TractorArm*>(Param)->GetTarget();
        if (Target != nullptr)
        {
            // The owner's CShip, not null-tested before the call.
            CObject* const Raw = (Owner != nullptr) ? Owner->Object : nullptr;
            CShip* const ShipCobj =
                (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                    ? static_cast<CShip*>(Raw) : nullptr;

            int const Space = ShipCobj->get_space_for_cargo_type(Target->ContentsArch);
            if (Space != 0)
            {
                // The mask only matches Loot::get_type_bits.
                GameObject* const LootObj = Owner->OwnerSystem->find_object(Target->Id, 0x10000000u);
                if (LootObj != nullptr && !LootObj->is_destroyed())
                {
                    Owner->OwnerSystem->tractor_loot(static_cast<::Loot*>(LootObj), Owner,
                                                     static_cast<unsigned int>(Space), Equip->Id);
                }
            }
        }
    }

    // Always the base body.
    ServerEquip::notify(Code, Param);
}

// FUN_06d0efe0. See server_sysdb.h for the map.
void StarSystem::use_item(GameObject* Obj, unsigned short EquipId, int Count)
{
    // The (& 3) narrowing is not null-tested.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();

    if (OwnerPlayer != 0u && Players.is_valid_ship_owner(OwnerPlayer, ObjId))
        PlayerData_RemoveEquipment(Players[OwnerPlayer], EquipId, Count, false);

    // get_id() is called again here.
    FPayloadServerUseItem Rec;
    Rec.ObjectId = Obj->get_id();
    Rec.EquipId = EquipId;
    Rec.Count = static_cast<unsigned short>(Count);

    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Rec.ObjectId))
            continue;
        ::g_pClientInterface->SendServerUseItem(Node->View.PlayerId, Rec);
    }
}

// FUN_06d0bbc0. See server_sysdb.h for the map.
void StarSystem::sp_obj_update(SSPObjUpdateInfo const& Info, unsigned int SessionId)
{
    // Find the ship; its CSimple Id is read without a test on the (& 3) narrowing.
    CSimple* Ship = nullptr;
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            return;
        Ship = (Node->Data != nullptr) ? Node->Data->csimple() : nullptr;
        if (Ship->Id == Info.ObjectId)
            break;
    }

    PhySys::RCSet(Ship, Info.Position, Info.Orientation, Info.Timestamp);

    // The timestamp narrowed to a float, the throttle a raw dword copy; the pad bytes are
    // never written.
    FCommonUpdateObjectInfo Rec;
    Rec.ObjectId = Info.ObjectId;
    Rec.Orientation = Info.Orientation;
    Rec.Position = Info.Position;
    Rec.Timestamp = static_cast<float>(Info.Timestamp);
    std::memcpy(&Rec.Throttle, &Info.ThrottleBits, sizeof(Rec.Throttle));
    Rec.State = Info.State;

    // Every player but the sender.
    for (SystemPlayerNode* Player = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Player != static_cast<SystemPlayerNode*>(PlayerListHead); Player = Player->Next)
    {
        if (Player->View.PlayerId == SessionId)
            continue;
        SystemPlayerView_SendObjectUpdate(&Player->View, &Rec);
    }
}

// FUN_06d0bcd0 (0x201c-byte frame). See server_sysdb.h for the map.
unsigned int StarSystem::scan_objects(unsigned int* OutIds, unsigned int MaxCount, Vector const* Pos,
                                      float Radius, unsigned int TypeMask)
{
    unsigned int Count = 0;

    // Ships first.
    if ((TypeMask & 0x7f0000u) != 0u)
        Count = Ships.collect_in_sphere(OutIds, MaxCount, Pos, Radius, TypeMask);

    if ((TypeMask & 0xfdffu) == 0u)
        return Count;

    // A radius that is not strictly positive walks the solar list instead of the spatial
    // bucket, into what is left of the buffer.
    if (!(Radius > 0.0f))
    {
        Count += Solars.collect_in_sphere(OutIds + Count, MaxCount - Count, Pos, Radius, TypeMask);
        return Count;
    }

    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket == nullptr)
        return Count;

    // Up to 0x800 hits; the query's return value is not used, the count is read back from
    // the block.
    struct FBucketQuery
    {
        Vector Pos;
        float Radius;
    } Sphere;
    Sphere.Pos = *Pos;
    Sphere.Radius = Radius;
    struct FBucketResult
    {
        unsigned int Capacity;
        unsigned int Count;
        IObjRW* Data[0x800];
    } Found;
    Found.Capacity = 0x800u;
    Found.Count = 0u;
    // ISpatialPartition +0x1c (sphere query); the stack sphere and result have the layouts of
    // Geometry::Sphere and FSpatialQueryResult.
    static_cast<ISpatialPartition*>(Bucket)->QuerySphere(reinterpret_cast<Geometry::Sphere const*>(&Sphere),
                                                         reinterpret_cast<FSpatialQueryResult*>(&Found));

    // Each hit while the buffer has room; a non-zero get_type result skips it.
    for (unsigned int Index = 0; Index < Found.Count; ++Index)
    {
        if (Count >= MaxCount)
            return Count;
        IObjRW* const Hit = (Index < Found.Count) ? Found.Data[Index] : nullptr;
        unsigned int HitType;
        if (Hit->get_type(HitType) != 0)
            continue;
        if ((HitType & TypeMask) == 0u)
            continue;
        OutIds[Count] = Hit->get_id();
        ++Count;
    }
    return Count;
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
        if ((TypeMask & 0x7f0000u) != 0u && Wrapper->is_destroyed())
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

// StarSystem::load -- retail FUN_06d06ed0, a thiscall with no stack arguments (Ghidra's
// void(void) is wrong). See server_sysdb.h.
void StarSystem::load()
{
    // Tests for 1, not non-zero (add_player's guard is the one that tests for zero).
    if (bLoaded == 1)
        return;

    // ISystem slot 0 (NicknameId in this port's flat record); no null test on SystemInfo.
    unsigned int const SystemId = SystemInfo->NicknameId;

    PhySys::Startup(SystemId);

    // Single player wraps the solars the client already built and mirrors its asteroid
    // fields.
    if (SinglePlayer()) {
        load_solars();
        SrvAsteroid::SrvAsteroid_MapSystem(SystemId);
    } else {
        char SystemIniPath[260];
        Universe::get_filename(SystemIniPath, SystemInfo->File);

        // Both results dropped.
        CmnAsteroid::Load(SystemId);
        SrvAsteroid::SrvAsteroid_LoadSystem(SystemId, SystemIniPath);

        // Every [Object] section goes to load_object (FUN_06d09d60), its result dropped.
        INI_Reader Reader;
        if (Reader.open(SystemIniPath, false)) {
            while (Reader.read_header()) {
                if (Reader.is_header("Object"))
                    load_object(Reader);
            }
        }
    }

    // The BSP spatial partition over the solars. The array is sized by count_type (which
    // filters) but filled for EVERY node of the list (which does not).
    int const SolarCount = static_cast<int>(Solars.count_type(0xffffffffu));
    Spatial = NewSpatial("BSP");
    if (Spatial != nullptr) {
        FSpatialEntry* const Entries = static_cast<FSpatialEntry*>(
            ::operator new(static_cast<size_t>(SolarCount) * sizeof(FSpatialEntry)));
        FSpatialEntry* Out = Entries;
        FSystemListNode* Node = nullptr;
        for (;;) {
            Node = (Node == nullptr) ? Solars.Head : Node->Next;
            if (Node == nullptr)
                break;
            GameObject* const Obj = Node->Data;
            Out->Object = static_cast<IObjRW*>(Obj);
            Out->Position = Obj->get_position();
            Out->Radius = Obj->cobject()->Radius;
            ++Out;
        }
        // ISpatialPartition +0x14: AddArray(entries, count).
        Spatial->AddArray(Entries, static_cast<unsigned>(SolarCount));
        ::operator delete(Entries);
    }

    bLoaded = 1;

    // "This system is now loaded"; retail re-reads the same id off SystemInfo.
    unsigned int EventData = SystemId;
    Server_ControllerDispatch(0u, pub::Controller::EMessage::SystemLoaded, &EventData);
}

// StarSystem::load_solars -- retail FUN_06d07170. See server_sysdb.h.
void StarSystem::load_solars()
{
    int Count = 0;
    CObject* Obj = CObject::FindFirst(CObject::ClassSolar);
    if (Obj != nullptr)
    {
        do
        {
            // The walk's own reference; attach_existing takes the wrapper's.
            Obj->AddRef();
            ::Solar* const Wrapper = new ::Solar();                 // FUN_06d02b70

            CObject* const Attached =
                ((static_cast<unsigned int>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar) ? Obj : nullptr;
            Wrapper->attach_existing(Attached);                     // FUN_06d02c20

            Wrapper->enter_system(this, nullptr);
            Wrapper->check_fuses();
            Obj->Release();

            Solars.append(Wrapper);
            ++Count;

            Obj = CObject::FindNext();
        } while (Obj != nullptr);

        if (Count != 0)
            return;
    }

    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x129,
          "failed to find any solar objects\n");
}

// StarSystem::load_object -- retail FUN_06d09d60. See server_sysdb.h.
// Retail inlines the CreateParms ctor and dtor. Fields the inlined ctor leaves alone are
// zeroed here (retail leaves stack garbage for CSolar::ReadObj to overwrite).
// ContinualAnims (+0xe4) is filled by ReadObj, one name per "anim" key, and freed by the
// parms' destructor after CSolar::init.
GameObject* StarSystem::load_object(INI_Reader& Reader)
{
    CSolar::CreateParms Parms;
    Parms.Pad58 = 0;
    Parms.DockingBaseId = 0;
    Parms.VibeId = 0;
    Parms.SpaceCostume.PartIds[0] = 0;                             // FUN_06d0a060
    Parms.SpaceCostume.PartIds[1] = 0;
    Parms.SpaceCostume.PartIds[2] = 0;
    Parms.SpaceCostume.PartIds[3] = 0;
    for (int Index = 0; Index < 8; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = 0;
    Parms.SpaceCostume.AccessoryCount = 0;
    Parms.VoiceId = 0;
    Parms.NicknameSrc.Id = 0;                                      // ID_String::clear
    Parms.VisitValue = 0;
    Parms.ParentNicknameId = 0;                                    // ID_String::clear
    Parms.bDestructible = 0;
    Parms.bDynamic = 0;
    Parms.DestSystem = 0;
    Parms.DestGateId = 0;                                          // ID_String::clear
    Parms.PrevRing = 0;
    Parms.NextRing = 0;
    Parms.TradelaneSpaceNameId = 0;
    Parms.AtmosphereRange = 0.0f;
    Parms.DockWithBaseId = 0;

    // The bool result is not read.
    Archetype::Solar* Arch = nullptr;
    CSolar::ReadObj(Reader, Arch, Parms);
    // Set after ReadObj and before the archetype test; CSolar::init's connect() hands it to
    // init_physics.
    Parms.ObjDb = this;

    ::Solar* Wrapper = nullptr;
    if (Arch != nullptr)
    {
        Arch->load();
        Wrapper = new ::Solar();                                   // FUN_06d02b70
        Wrapper->create(Arch);
        Wrapper->csolar()->init(Parms);

        Solars.append(Wrapper);

        Arch->Release();
        Wrapper->enter_system(this, nullptr);
        Wrapper->check_fuses();
    }

    return Wrapper;
}

// StarSystem::create_solar -- retail FUN_06d0a200. See server_sysdb.h.
::Solar* StarSystem::create_solar(CSolarCreateInfo& Req)
{
    ::Solar* const Wrapper = new ::Solar();

    // No null test on the archetype.
    Archetype::Solar* const Arch = Archetype::GetSolar(Req.ArchetypeNickname);
    Arch->load();
    Wrapper->create(Arch);
    Arch->Release();

    CObject* const Raw = Wrapper->Object;
    CSolar* const Cs =
        (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            ? static_cast<CSolar*>(Raw) : nullptr;

    // FUN_06d0a770's values: VibeId -1, the accessory ids (+0x74..+0x93) not written.
    CSolar::CreateParms Parms;
    Parms.Pad58 = 0u;
    Parms.DockingBaseId = 0u;
    Parms.VibeId = 0xffffffffu;
    Parms.SpaceCostume.PartIds[0] = 0u;
    Parms.SpaceCostume.PartIds[1] = 0u;
    Parms.SpaceCostume.PartIds[2] = 0u;
    Parms.SpaceCostume.PartIds[3] = 0u;
    Parms.SpaceCostume.AccessoryCount = 0;
    Parms.VoiceId = 0u;

    Parms.NicknameSrc.clear();
    Parms.VisitValue = 0u;
    reinterpret_cast<ID_String&>(Parms.ParentNicknameId).clear();
    Parms.bDestructible = 0u;
    Parms.bDynamic = 0u;
    Parms.DestSystem = 0u;
    reinterpret_cast<ID_String&>(Parms.DestGateId).clear();
    Parms.PrevRing = 0u;
    Parms.NextRing = 0u;
    Parms.TradelaneSpaceNameId = 0u;
    Parms.AtmosphereRange = 0.0f;
    Parms.DockWithBaseId = 0u;

    Parms.HitPts = static_cast<float>(Req.HitPts);
    Parms.Id = Req.ObjId;
    Parms.ObjDb = this;
    Parms.Pos = Req.Position;
    Parms.OwnerPlayer = 0u;
    Server_QuatToMatrix(Parms.Orient, Req.Orientation);            // DAT_06d8da08 vt +0x54

    // Req.DockingBaseId feeds both DockingBaseId and DockWithBaseId.
    Parms.VisitValue = Req.VisitValue;
    Parms.DockingBaseId = Req.DockingBaseId;
    Parms.DockWithBaseId = Req.DockingBaseId;
    Parms.VibeId = static_cast<unsigned int>(Req.VibeGroupId);
    Parms.SpaceCostume.PartIds[0] = Req.CostumeHead;
    Parms.VoiceId = Req.VoiceId;
    Parms.SpaceCostume.PartIds[1] = Req.CostumeBody;
    Parms.SpaceCostume.PartIds[2] = Req.CostumeLeftHand;
    Parms.SpaceCostume.PartIds[3] = Req.CostumeRightHand;
    Parms.SpaceCostume.AccessoryCount = Req.CostumeAccessoryCount;
    // Signed loop, no clamp to the 8 slots.
    for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = Req.CostumeAccessory[Index];

    // VC6's vector operator= into the still-empty Equipment: the reallocating branch for a
    // non-empty source. Loadout::Get's result is not tested.
    if (Req.LoadoutId != 0u)
    {
        Loadout::Map const* const Map = Loadout::Get(Req.LoadoutId);
        int const Count = static_cast<int>(Map->Items.size());
        if (Count > 0)
        {
            EquipDesc* const Block =
                static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
            EquipDesc* Dest = Block;
            for (int Index = 0; Index < Count; ++Index)
            {
                new (Dest) EquipDesc(Map->Items[static_cast<size_t>(Index)]);
                ++Dest;
            }
            Parms.Equipment.Begin = Block;
            Parms.Equipment.End = Dest;
            Parms.Equipment.Cap = Dest;
        }
    }
    Parms.Equipment.append(Req.Equipment);

    Parms.ArchGroupDescs = Req.CollisionGroups;

    Parms.NicknameSrc = Req.NicknameSrc;
    Parms.bDestructible = 1u;
    Parms.bDynamic = Req.bDynamic;
    Cs->init(Parms);

    if ((Req.MarkPlayerId & 0x3fffffffu) != 0u)
        Wrapper->mark_for_player(Req.MarkPlayerId, 1);

    Wrapper->enter_system(this, nullptr);
    Wrapper->check_fuses();

    Solars.append(Wrapper);

    // The spatial partition's single add, with the record load() bulk-adds.
    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket != nullptr)
    {
        CObject* const BucketRaw = Wrapper->Object;
        CSolar* const BucketSolar =
            (BucketRaw != nullptr && (static_cast<unsigned int>(BucketRaw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                ? static_cast<CSolar*>(BucketRaw) : nullptr;
        float const Radius = BucketSolar->Radius;
        Vector const& Position = Wrapper->get_position();
        FSpatialEntry Entry;
        Entry.Position = Position;
        Entry.Radius = Radius;
        Entry.Object = static_cast<IObjRW*>(Wrapper);
        static_cast<ISpatialPartition*>(Bucket)->Add(&Entry);
    }

    Cs->enable_collisions_r(true);

    // Every player of this system; the request is patched per player.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        Req.bMarkedBy = Wrapper->is_marked_by(Player->View.PlayerId) ? 1u : 0u;
        Req.VibeOtherGroupId = Players[Player->View.PlayerId].rep_vibe;
        SystemPlayerView_CreateSolarForPlayer(&Player->View, &Req, Wrapper);
    }

    return Wrapper;
}

// StarSystem::create_ship -- retail FUN_06d0a880. See server_sysdb.h.
::Ship* StarSystem::create_ship(FShipCreateRequest const& Req)
{
    ::Ship* const Wrapper = new ::Ship();                           // FUN_06d00e80

    CShipCreateInfo Copy(static_cast<CShipCreateInfo const&>(Req));

    CShip* Cs = nullptr;
    if (Copy.OwnerPlayer != 0u && SinglePlayer())
    {
        // Single player: the client's own ship already exists; find it by the request's id
        // (not the copy's).
        Wrapper->find(Req.ObjId);
        CObject* const Raw = Wrapper->Object;
        Cs = (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                 ? static_cast<CShip*>(Raw) : nullptr;
    }
    else
    {
        // No null test on the archetype.
        Archetype::Ship* const Arch = Archetype::GetShip(Copy.ArchetypeNickname);
        Arch->load();
        Wrapper->create(Arch);
        Arch->Release();

        CObject* const Raw = Wrapper->Object;
        Cs = (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                 ? static_cast<CShip*>(Raw) : nullptr;

        // FUN_06d0a770's values (see create_solar), then the CShip tail. The CEqObj tail
        // +0xb8 .. +0xe4 is stack garbage in retail; CShip::init does not read it.
        CShip::CreateParms Parms;
        Parms.Pad58 = 0u;
        Parms.DockingBaseId = 0u;
        Parms.VibeId = 0xffffffffu;
        Parms.SpaceCostume.PartIds[0] = 0u;
        Parms.SpaceCostume.PartIds[1] = 0u;
        Parms.SpaceCostume.PartIds[2] = 0u;
        Parms.SpaceCostume.PartIds[3] = 0u;
        Parms.SpaceCostume.AccessoryCount = 0;
        Parms.VoiceId = 0u;
        Parms.FieldE8 = 0u;
        Parms.GroupNameId = 0u;
        Parms.Rank = 0u;
        Parms.bUsingTradelane = 0u;

        Parms.HitPts = static_cast<float>(Copy.HitPts);
        Parms.OwnerPlayer = Copy.OwnerPlayer;
        Parms.Id = Copy.ObjId;
        Parms.Pos = Copy.Position;
        Parms.ObjDb = this;
        Server_QuatToMatrix(Parms.Orient, Copy.Orientation);
        Parms.Velocity = Copy.Velocity;
        Parms.AngularVelocity = Copy.AngularVelocity;
        Parms.VibeId = static_cast<unsigned int>(Copy.VibeGroupId);
        Parms.bUsingTradelane = Copy.bUsingTradelane;
        Parms.SpaceCostume.PartIds[0] = Copy.CostumeHead;
        Parms.VoiceId = Copy.VoiceId;
        Parms.SpaceCostume.PartIds[3] = Copy.CostumeRightHand;
        Parms.Rank = Copy.ReputationRank;
        Parms.SpaceCostume.PartIds[2] = Copy.CostumeLeftHand;
        Parms.SpaceCostume.PartIds[1] = Copy.CostumeBody;
        Parms.SpaceCostume.AccessoryCount = Copy.CostumeAccessoryCount;
        // Signed loop, no clamp to the 8 slots.
        for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
            Parms.SpaceCostume.AccessoryIds[Index] = Copy.CostumeAccessory[Index];

        // As in create_solar; no test on Loadout::Get.
        if (Copy.LoadoutId != 0u)
        {
            Loadout::Map const* const Map = Loadout::Get(Copy.LoadoutId);
            int const Count = static_cast<int>(Map->Items.size());
            if (Count > 0)
            {
                EquipDesc* const Block =
                    static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
                EquipDesc* Dest = Block;
                for (int Index = 0; Index < Count; ++Index)
                {
                    new (Dest) EquipDesc(Map->Items[static_cast<size_t>(Index)]);
                    ++Dest;
                }
                Parms.Equipment.Begin = Block;
                Parms.Equipment.End = Dest;
                Parms.Equipment.Cap = Dest;
            }
        }
        Parms.Equipment.append(Copy.Equipment);

        Parms.ArchGroupDescs = Copy.CollisionGroups;

        Parms.GroupNameId = Copy.GroupNameId;
        Parms.DockingBaseId = Copy.DockingBaseId;
        Cs->init(Parms);

        // The owner's player group id into CShip::GroupId.
        if (Copy.OwnerPlayer != 0u)
        {
            CPlayerGroup* const Group =
                static_cast<CPlayerGroup*>(Players[Copy.OwnerPlayer].group_ptr);
            if (Group != nullptr)
            {
                unsigned int const GroupId = Group->GetID();
                Cs->GroupId = GroupId;
            }
        }
    }

    ::Ship::FEquipState State;
    State.EquipIds = Req.EquipIds;
    State.Data = Req.EquipStateData;

    if ((Req.MarkPlayerId & 0x3fffffffu) != 0u)
        Wrapper->mark_for_player(Req.MarkPlayerId, 1);

    Wrapper->enter_system(this, nullptr);
    Wrapper->set_equip_state(State);                               // FUN_06d010e0

    Ships.append(Wrapper);

    if (!SinglePlayer() || Copy.OwnerPlayer == 0u)
        Cs->enable_collisions_r(true);

    // Every player of this system but the owner.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == Copy.OwnerPlayer)
            continue;
        Copy.bMarkedBy = Wrapper->is_marked_by(Player->View.PlayerId) ? 1u : 0u;
        SystemPlayerView_CreateShipForPlayer(&Player->View, &Copy, Wrapper);
    }

    return Wrapper;
}

// StarSystem::add_player -- retail FUN_06d07e20. See server_sysdb.h.
void StarSystem::add_player(unsigned int SessionId)
{
    // Tests for zero here, while load()'s own guard compares against 1.
    if (bLoaded == 0)
        load();

    // The id is tested against 0x3fffffff, not zero: an id with only the top two bits set
    // is rejected too.
    if ((SessionId & 0x3fffffffu) == 0)
        return;

    // A push_back; no null test on the sentinel (the StarSystem ctor always allocates it).
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);

    // Retail builds the Observer on the stack, copies it into the node and destroys the
    // temporary at the end of this block.
    {
        Observer Temp(SessionId);

        // FUN_06d0f330 (list::insert), inlined, including MSVC6 _Buynode's null-Prev
        // fallback.
        SystemPlayerNode* Prev = Sentinel->Prev;
        SystemPlayerNode* const Node =
            static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
        Node->Next = Sentinel;
        if (Prev == nullptr)
            Prev = Node;
        Node->Prev = Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        ::new (static_cast<void*>(&Node->View)) Observer(Temp);
        ++PlayerListSize;
    }

    // The live element, kept for every pass below.
    Observer* const View = &Sentinel->Prev->View;

    // The multiplayer-only head is SKIPPED in single player.
    bool const bSingle = SinglePlayer();

    if (!bSingle) {
        // Retail pushes id-then-session, so the session is the FIRST parameter. SystemInfo
        // is read unguarded.
        Players.SendSystemID(SessionId, SystemInfo->NicknameId);

        // Unchecked raw slot; read back only by the AttitudeTowards call below.
        int const PlayerRep = Players[SessionId].rep_vibe;

        // The walk is skipped when count_type finds nothing.
        FSystemListNode* const SolarHead = Solars.Head;
        if (Solars.count_type(0xffffffffu) != 0) {
            for (FSystemListNode* SolarNode = SolarHead; SolarNode != nullptr;
                 SolarNode = SolarNode->Next) {
                // node->Data dereferenced unguarded.
                GameObject* const Obj = SolarNode->Data;

                CObject* Raw = const_cast<CObject*>(Obj->cobject());
                CSolar* Solar = nullptr;
                if (Raw != nullptr &&
                    (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                    Solar = static_cast<CSolar*>(Raw);

                if (Solar != nullptr && Solar->bDynamic) {
                    // A DYNAMIC solar gets CREATESOLAR and skips the damage list.
                    CSolarCreateInfo Info;
                    CSolarCreateInfo_Fill(&Info, Solar);                // FUN_06d07950
                    Info.bMarkedBy = Obj->is_marked_by(SessionId) ? 1u : 0u;
                    ::g_pClientInterface->SendServerCreateSolar(SessionId, Info);
                } else {
                    // Everything else, a static solar included: its damage list, sent
                    // only when non-empty.
                    DamageList Damage;
                    Damage.set_destroyed(false);
                    // The same unnamed cause 0x19 the observer writes; see the note there.
                    Damage.set_cause(static_cast<DamageCause>(0x19));
                    Damage.set_inflictor_id(0u);
                    Damage.set_inflictor_owner_player(0u);
                    Obj->get_damage_list(Damage, 1.0f);
                    if (!Damage.Entries.empty()) {
                        ::g_pClientInterface->SendServerDamageObject(SessionId, Obj->get_id(), Damage);
                    }
                }

                // The rest only for a solar, dynamic or not.
                if (Solar == nullptr)
                    continue;

                // SETREPUTATION for the solar's vibe. +0x0c is never written (stack residue
                // in retail, zeroed here).
                FServerSetReputationInfo Rep;
                Rep.Rank = 0u;
                Rep.Relations = nullptr;
                Rep.RelationCount = 0;
                Rep.DisplayName[0] = L'\0';
                Rep.VibeHandle = static_cast<int>(Solar->VibeId);
                ::Reputation::Vibe::AttitudeTowards(Rep.VibeHandle, PlayerRep,
                                                    Rep.Attitude);
                ::Reputation::Vibe::GetAffiliation(Rep.VibeHandle, Rep.Affiliation,
                                                   false);
                ::g_pClientInterface->SendServerSetReputation(SessionId, Rep);

                // Each functioning gun's target. No CEGun::cast: the traverser's mask
                // already selects guns.
                CEquipTraverser Walk(0x80);
                for (CEquip* Equip = Solar->EquipManager.Traverse(Walk);
                     Equip != nullptr;
                     Equip = Solar->EquipManager.Traverse(Walk)) {
                    if (!Equip->IsFunctioning())
                        continue;
                    CSimple* Target = nullptr;
                    unsigned short TargetSub = SUBOBJ_ID_NONE;
                    static_cast<CEGun*>(Equip)->GetTarget(Target, TargetSub);
                    if (Target == nullptr)
                        continue;
                    // Retail never writes the two pad words.
                    XSetTarget Msg;
                    Msg.ObjectId = Solar->Id;
                    Msg.SubObjId = Equip->Id;
                    Msg.Pad_0x06 = 0u;
                    Msg.TargetObjectId = Target->Id;
                    Msg.TargetSubObjId = TargetSub;
                    Msg.Pad_0x0e = 0u;
                    ::g_pClientInterface->SendCommonSetTarget(SessionId, Msg);
                }

                // One ACTIVATEOBJECT per active entry.
                std::vector<bool> States;
                if (Solar->get_activate_state(States)) {
                    for (unsigned int Index = 0;
                         Index < static_cast<unsigned int>(States.size()); ++Index) {
                        if (!States[Index])
                            continue;
                        FPayloadServerActivateObject Activate;
                        Activate.ObjectId = Solar->Id;
                        Activate.bActivate = 1;
                        Activate.Pad_0x05[0] = 0;
                        Activate.Pad_0x05[1] = 0;
                        Activate.Pad_0x05[2] = 0;
                        Activate.ActivateParam = Index;
                        ::g_pClientInterface->SendServerActivateObject(SessionId, Activate);
                    }
                }
            }
        }
    }

    // Ships: single player sends CREATESHIP at once; multiplayer only tracks the live ones.
    {
        FSystemListNode* Node2 = Ships.Head;
        while (Node2 != nullptr) {
            FSystemListNode* const Next = Node2->Next;
            GameObject* const Obj = Node2->Data;
            if (bSingle) {
                CObject* const Raw = Obj->cobj();
                CShip* const Ship =
                    (Raw != nullptr &&
                     (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                        ? static_cast<CShip*>(Raw)
                        : nullptr;
                if (Ship != nullptr) {
                    CShipCreateInfo Info;
                    CShipCreateInfo_Fill(&Info, Ship);
                    Info.bMarkedBy =
                        Obj->is_marked_by(SessionId) ? 1u : 0u;
                    SystemPlayerView_CreateShipForPlayer(View, &Info, Obj);
                }
            } else if (!Obj->is_destroyed()) {
                SystemPlayerView_AddTrackedObject(View, Obj);           // FUN_06cf7140
            }
            Node2 = Next;
        }
    }

    // Loot, counted the same way; both branches return at once on a zero count.
    {
        FSystemListNode* const LootHead = Loot.Head;
        unsigned int const LootCount = Loot.count_type(0xffffffffu);
        if (LootCount == 0)
            return;

        if (bSingle) {
            // Single player: one CREATELOOT for all of it. OwnerId stays 0 here (the
            // observer's per-object path is the one that copies CLoot+0xe4).
            std::vector<GameObject*> LootObjs;
            LootObjs.reserve(LootCount);
            FServerLootDesc* const Descs = static_cast<FServerLootDesc*>(
                ::operator new(sizeof(FServerLootDesc) * LootCount));
            if (Descs == nullptr)
                return;
            unsigned int Filled = 0u;
            for (FSystemListNode* LootNode = LootHead; LootNode != nullptr;
                 LootNode = LootNode->Next) {
                if (Filled >= LootCount)
                    break;
                GameObject* const Obj = LootNode->Data;
                CObject* const Raw = Obj->cobj();
                CLoot* const Loot =
                    (Raw != nullptr &&
                     (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassLoot) == CObject::ClassLoot)
                        ? static_cast<CLoot*>(Raw)
                        : nullptr;
                FServerLootDesc* const Desc = &Descs[Filled];
                std::memset(Desc, 0, sizeof(*Desc));
                Desc->Field_0x44 = 1;
                ServerLootDesc_Fill(Desc, Loot);
                Desc->bMarkedBy = Obj->is_marked_by(SessionId) ? 1u : 0u;
                LootObjs.push_back(Obj);
                ++Filled;
            }

            FServerCreateLootRequest Req;
            Req.OwnerId = 0u;
            Req.Kind = 5u;
            Req.bMarkedBy = 0;
            Req.bTemporary = 0;
            Req.Pad0a[0] = 0;
            Req.Pad0a[1] = 0;
            Req.Items = Descs;
            Req.Count = static_cast<unsigned char>(Filled);
            Req.Pad11[0] = 0;
            Req.Pad11[1] = 0;
            Req.Pad11[2] = 0;

            if (Filled > 0u) {
                SystemPlayerView_CreateLootForPlayer(
                    View, &Req, &LootObjs[0], &LootObjs[0] + LootObjs.size());
            }
            ::operator delete(Descs);
        } else {
            FSystemListNode* Node2 = LootHead;
            while (Node2 != nullptr) {
                FSystemListNode* const Next = Node2->Next;
                GameObject* const Obj = Node2->Data;
                if (!Obj->is_destroyed()) {
                    SystemPlayerView_AddTrackedObject(View, Obj);
                }
                Node2 = Next;
            }
        }
    }
}

// ===========================================================================
// The per-player object tracker that hangs off StarSystem+0x0C. Structures are in
// server_sysdb.h.
// ===========================================================================

// FUN_06cf7260's squared view radii: 2500^2, 5000^2 and 10000^2.
static float const kViewRadiusSqUnowned = 6.25e6f;    // .rdata:06d66aec
static float const kViewRadiusSqStranger = 2.5e7f;    // .rdata:06d66af0
static float const kViewRadiusSqGroupMate = 1.0e8f;   // .rdata:06d66af4

// The mask FUN_06cf6820's third exit tests get_type_bits against: every OBJECT_TYPE bit
// below 0x10000 except WAYPOINT (0x200), per Common.dll's DecodeType table
// (.data:063ec810). So it reads "a solar, not a waypoint": static system furniture the
// client builds from the system ini, which is why retail still sends the destroy for an
// object never in this player's tracked list. Ships, projectiles, mines, loot and
// asteroids stay silent.
static unsigned int const kSolarTypeBits = 0x0000FDFFu;

// FObserverTrackedList -- Observer+0x08, retail's std::list<SystemTrackedObject*>. See
// server_sysdb.h.

// Inside FUN_06cf51e0.
FObserverTrackedList::FObserverTrackedList()
{
    // Retail's allocator byte is a leftover argument byte with no value; the port writes 0.
    Alloc = 0;
    SystemTrackedNode* const Sentinel = new SystemTrackedNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Head = Sentinel;
    Size = 0;
}

// Inside FUN_06d0f6d0.
FObserverTrackedList::FObserverTrackedList(FObserverTrackedList const& Source)
{
    Alloc = Source.Alloc;
    SystemTrackedNode* const Sentinel = new SystemTrackedNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Head = Sentinel;
    Size = 0;

    // The record pointers are copied: the RECORD is shared, not duplicated.
    SystemTrackedNode* const SrcSentinel = Source.Head;
    for (SystemTrackedNode* Src = SrcSentinel->Next; Src != SrcSentinel; Src = Src->Next) {
        SystemTrackedNode* const Node = new SystemTrackedNode;
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        Node->Data = Src->Data;
        ++Size;
    }
}

// The tail of FUN_06cf52d0, MSVC6's ~list(). ~Observer's body has already emptied the
// list, so the walk finds nothing, in retail as here.
FObserverTrackedList::~FObserverTrackedList()
{
    SystemTrackedNode* const Sentinel = Head;
    SystemTrackedNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemTrackedNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Size;
        Node = Next;
    }
    delete Sentinel;
    Head = nullptr;
    Size = 0;
}

// FUN_06cf51e0 -- Observer's constructor; members in retail's order. +0x04 and +0x2C are
// not written, in retail or here.
Observer::Observer(unsigned int InPlayerId)
    : Tracked()
    , PendingDamageIds()
{
    bShipGone = 0;
    ElapsedTime = 0.0;
    PlayerId = InPlayerId;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
}

// FUN_06d0f6d0 -- Observer's copy constructor, compiled as std::list's _Construct (a
// placement new). Callers: list::insert (StarSystem::add_player) and StarSystem's copy
// constructor (FUN_06d109c0). +0x04 and +0x2c are not written.
Observer::Observer(Observer const& Source)
    : Tracked(Source.Tracked)
    , PendingDamageIds(Source.PendingDamageIds)
{
    bShipGone = Source.bShipGone;
    ElapsedTime = Source.ElapsedTime;
    PlayerId = Source.PlayerId;
    Position = Source.Position;
}

// FUN_06cf52d0 -- ~Observer. Its scalar deleting destructor (FUN_06cf52b0) is vtable slot
// 0; every retail dispatch passes flag 0.
Observer::~Observer()
{
    // No null test on the list head; both constructors always allocate the sentinel.
    SystemTrackedNode* const Sentinel = Tracked.Head;
    SystemTrackedNode* Node;
    // Pass 1: every RECORD (its CRemotePhysicsSimulation destroyed with it).
    for (Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        delete Node->Data;
    }
    // Pass 2: every node erased.
    Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemTrackedNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Tracked.Size;
        Node = Next;
    }
    // ~PendingDamageIds, then ~Tracked, follow.
}

// FUN_06cf7140 -- the only producer the tracked-object list has in retail.
SystemTrackedObject* SystemPlayerView_AddTrackedObject(Observer* View, GameObject* Obj)
{
    // No null test on the view or its list head; every Observer ctor allocates the sentinel.

    // +0x04 is never written (see SystemTrackedObject in server_sysdb.h).
    SystemTrackedObject* const Record = new SystemTrackedObject;
    Record->Object = nullptr;
    Record->bCreatedOnClient = false;

    Record->Object = Obj;

    // Seeded from SinglePlayer(): in single player the client already has everything, so
    // the record counts as created the moment it exists.
    Record->bCreatedOnClient = SinglePlayer();

    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    SystemTrackedNode* const Node = new SystemTrackedNode;
    Node->Next = Sentinel;
    Node->Prev = Sentinel->Prev;
    Node->Data = Record;
    Sentinel->Prev->Next = Node;
    Sentinel->Prev = Node;

    ++View->Tracked.Size;

    // Outside single player, prime the simulation with the object's current state.
    // SinglePlayer() is called a SECOND time, not reused from the flag above. The math
    // engine (DAT_06d8d7f8, shared with the observer update) is never null-checked.
    if (!SinglePlayer())
    {
        Vector const& Position = Obj->get_position();          // slot 0
        Transform const& Xform = Obj->get_transform();         // slot 3
        Quaternion Orientation;
        Observer_MatrixToQuat(Orientation, Xform.Rotation);
        Record->Simulation.RemoteUpdate(Position, Orientation, View->ElapsedTime);
    }

    return Record;
}

// FUN_06cf7260.
bool SystemPlayerView_ShouldBeVisible(Observer* View, GameObject* Obj)
{
    // A null object and single player are always "visible"; the tracker is a
    // multiplayer-only filter. No test on the view.
    if (Obj == nullptr)
        return true;
    if (SinglePlayer())
        return true;

    // OwnerPlayer is read through the (& 3) narrowing with no test; every tracked object
    // is a CSimple.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    // Your own ship is never filtered.
    if (OwnerPlayer == View->PlayerId)
        return true;

    Vector const& ObjPos = Obj->get_position();

    // x87: retail keeps the squared distance on the FPU stack across the group test.
    float const Dx = ObjPos.x - View->Position.x;
    float const Dy = ObjPos.y - View->Position.y;
    float const Dz = ObjPos.z - View->Position.z;
    float const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;

    // An object nobody owns gets the tightest radius. Each compare is "visible when
    // DistSq <= radius"; a NaN is not visible.
    if (OwnerPlayer == 0)
        return DistSq <= kViewRadiusSqUnowned;

    // Group mates see each other further. Both slots are indexed raw (no is_valid). A group
    // mate outside 10000 falls through to the stranger test, which it then fails as well.
    void const* const MyGroup = Players[View->PlayerId].group_ptr;
    if (MyGroup != nullptr && MyGroup == Players[OwnerPlayer].group_ptr) {
        if (DistSq <= kViewRadiusSqGroupMate)
            return true;
    }
    return DistSq <= kViewRadiusSqStranger;
}

// Retail's inner-list search, inlined in FUN_06cf73d0 (by object POINTER) and FUN_06cf6820
// (by get_id()). For a live object those agree; the id form is what survives an object
// already being torn down, which is why the destroy path uses it. Neither tests the
// sentinel or the records.
static SystemTrackedNode* SystemPlayerView_FindByPointer(Observer* View, GameObject* Obj)
{
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->Data->Object == Obj)
            return Node;
    }
    return nullptr;
}

static SystemTrackedNode* SystemPlayerView_FindById(Observer* View, unsigned int ObjId)
{
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->Data->Object->get_id() == ObjId)
            return Node;
    }
    return nullptr;
}

// FUN_06cf73d0.
bool SystemPlayerView_IsObjectVisible(Observer* View, GameObject* Obj)
{
    if (Obj == nullptr)
        return true;
    if (SinglePlayer())
        return true;

    // Your own ship again; the (& 3) narrowing is read through unguarded.
    if (Obj->csimple()->OwnerPlayer == View->PlayerId)
        return true;

    // Already tracked: the record's "created on the client" byte, verbatim.
    SystemTrackedNode* const Node = SystemPlayerView_FindByPointer(View, Obj);
    if (Node != nullptr)
        return Node->Data->bCreatedOnClient;

    return SystemPlayerView_ShouldBeVisible(View, Obj);
}

// FUN_06cf7370. FUN_06d006c0 is Server_LookupGameObject here; its null on a miss is
// exactly what retail's miss path hands on.
bool SystemPlayerView_IsObjectIdVisible(Observer* View, unsigned int ObjId)
{
    if (SinglePlayer())
        return true;
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    return SystemPlayerView_IsObjectVisible(View, Obj);
}

// FUN_06cf65b0 (thiscall on the view); only caller StarSystem::sp_obj_update. The update
// goes to the client only once the object was created there, but the simulation is
// always fed.
void SystemPlayerView_SendObjectUpdate(Observer* View, FCommonUpdateObjectInfo const* Rec)
{
    if (SinglePlayer())
        return;

    for (SystemTrackedNode* Node = View->Tracked.Head->Next; Node != View->Tracked.Head; Node = Node->Next)
    {
        SystemTrackedObject* const Tracked = Node->Data;
        if (Tracked->Object->get_id() != Rec->ObjectId)
            continue;

        if (Tracked->bCreatedOnClient)
            ::g_pClientInterface->SendCommonUpdateObject(View->PlayerId, *Rec);

        Tracked->Simulation.RemoteUpdate(Rec->Position, Rec->Orientation,
                                         static_cast<double>(Rec->Timestamp));
        return;
    }
}

// FUN_06cf70c0 (thiscall on the view); only caller FUN_06d0f1c0. Tells this view's client
// the group id CPlayerGroup::AddMember / DelMember just stamped on a visible ship.
// Server_LookupGameObject's null covers both of retail's lookup early-outs.
void SystemPlayerView_SendGroupChange(Observer* View, unsigned int ObjId)
{
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    if (Obj == nullptr)
        return;
    if (!SystemPlayerView_IsObjectVisible(View, Obj))
        return;
    CObject const* const Cobj = Obj->cobject();
    if (Cobj == nullptr)
        return;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;
    unsigned int const GroupId = static_cast<CShip const*>(Cobj)->GroupId;
    g_pClientInterface->SendServerMiscObjUpdateObjectGroup(View->PlayerId, ObjId, GroupId);
}

// FUN_06d0f1c0 (thiscall on the StarSystem). Callers CPlayerGroup::AddMember and DelMember.
void StarSystem_SendGroupChange(StarSystem& System, unsigned int ObjId)
{
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(System.PlayerListHead);
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        SystemPlayerView_SendGroupChange(&Node->View, ObjId);
}

// FUN_06cf6730 (thiscall on the view): the ship's equipment list, only for a tracked
// object already created on the client. NO null test on the CShip narrowing: retail calls
// through it.
void SystemPlayerView_SendRemoteCargoUpdate(Observer* View, GameObject* Obj)
{
    SystemTrackedNode* const Head = View->Tracked.Head;
    SystemTrackedNode* Node = Head->Next;
    for (; Node != Head; Node = Node->Next)
    {
        if (Node->Data->Object == Obj)
            break;
    }
    if (Node == Head)
        return;
    if (!Node->Data->bCreatedOnClient)
        return;

    EquipDescVector Items;
    CObject const* const Cobj = Obj->cobject();
    CShip const* Ship = nullptr;
    if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
        Ship = static_cast<CShip const*>(Cobj);
    Ship->get_equip_desc_list(Items);
    g_pClientInterface->SendServerRemoteObjectCargoUpdate(View->PlayerId, Ship->Id, Items, 1u);
}

// FUN_06d0b110 (thiscall on the StarSystem): every player but the owner (OwnerPlayer read
// with no null test on the narrowing).
void StarSystem_SendRemoteCargoUpdate(StarSystem& System, GameObject* Obj)
{
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(System.PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(System.PlayerListHead); Node = Node->Next)
    {
        if (Obj->csimple()->OwnerPlayer != Node->View.PlayerId)
            SystemPlayerView_SendRemoteCargoUpdate(&Node->View, Obj);
    }
}

// FUN_06cf7440. Only caller FUN_06d0f190 below (IServerImpl::SubmitChat's local chat).
void SystemPlayerView_CollectCreatedOwners(Observer* View, std::set<unsigned int>* Owners)
{
    for (SystemTrackedNode* Node = View->Tracked.Head->Next; Node != View->Tracked.Head;
         Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;
        GameObject* const Obj = Record->Object;
        if (Obj == nullptr)
            continue;
        // OwnerPlayer read with NO null check on the (& 3) narrowing, as in retail.
        unsigned int const Owner = Obj->csimple()->OwnerPlayer;
        if (Owner == 0)
            continue;
        if (!Record->bCreatedOnClient)
            continue;
        Owners->insert(Owner);
    }
}

// FUN_06d0f190 (thiscall on the StarSystem).
void StarSystem_CollectCreatedOwners(StarSystem* System, unsigned int PlayerId,
                                     std::set<unsigned int>* Owners)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(System->PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next) {
        if (Node->View.PlayerId == PlayerId) {
            SystemPlayerView_CollectCreatedOwners(&Node->View, Owners);
            return;
        }
    }
}

// FUN_06cf6820. Returns nothing in retail; the bool says whether the record was found and
// dropped, which the callers ignore.
bool SystemPlayerView_OnObjectDestroyed(Observer* View, GameObject* Obj, int Kind)
{
    // No null test on View or Obj.

    // Built BEFORE the search and reused on every exit. Retail stores Kind as a whole
    // dword, so its upper three bytes land in the pad.
    FServerDestroyObjectInfo Wire;
    Wire.ObjectId = Obj->get_id();
    unsigned int const KindDword = (unsigned int)Kind;
    Wire.DestroyType = static_cast<unsigned char>(KindDword & 0xffu);
    Wire.Pad_0x05[0] = static_cast<unsigned char>((KindDword >> 8) & 0xffu);
    Wire.Pad_0x05[1] = static_cast<unsigned char>((KindDword >> 16) & 0xffu);
    Wire.Pad_0x05[2] = static_cast<unsigned char>((KindDword >> 24) & 0xffu);

    SystemTrackedNode* const Node = SystemPlayerView_FindById(View, Wire.ObjectId);

    if (Node == nullptr) {
        // Not tracked. Retail still tells the client when the object belonged to this very
        // player (OwnerPlayer read through the narrowing with no null test)...
        unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
        if (OwnerPlayer == View->PlayerId) {
            g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
            return false;
        }

        // ...or when it is a solar other than a waypoint (kSolarTypeBits): those exist on
        // the client whether or not this player ever tracked them.
        if ((Obj->get_type_bits() & kSolarTypeBits) != 0) {
            g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
        }
        return false;
    }

    // Tracked: notify only when the client was ever told the object exists. The record is
    // read with no null test here; the only null test is the one before the free below.
    SystemTrackedObject* const Record = Node->Data;
    if (Record->bCreatedOnClient) {
        g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
    }

    // delete runs ~CRemotePhysicsSimulation, as FUN_06cf6940 does before retail frees the
    // record.
    if (Record != nullptr) {
        delete Record;
        Node->Data = nullptr;
    }

    Node->Prev->Next = Node->Next;
    Node->Next->Prev = Node->Prev;
    delete Node;
    View->Tracked.Size--;
    return true;
}

// ===========================================================================
// The five "create this object on that player's client" notifiers. Declared in
// server_sysdb.h. Retail's source file for all five is Observer.cpp (.rdata:06d669dc),
// not StarSys.cpp; RTTI .?AVObserver@@ names the class.
// ===========================================================================

static char const kObserverCppPath[] =
    "E:\\FL\\Scratch\\Source\\Server\\space\\Observer.cpp";

// FUN_06cf6630 -- CREATESHIP. The odd one out: it does NOT consult ShouldBeVisible or set
// bCreatedOnClient, and sends only in SINGLE player (consistent, since AddTrackedObject
// seeds the flag from SinglePlayer()). In multiplayer it only registers the ship and lets
// the per-frame range check send the create later.
void SystemPlayerView_CreateShipForPlayer(Observer* View, CShipCreateInfo* Info, GameObject* Obj)
{
    // No test of View or Obj, nor on the (& 3) narrowing; every object here is a ship.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    // A player is never told to create their own ship.
    if (OwnerPlayer == View->PlayerId)
        return;

    if (View->bShipGone != 0)
        return;

    if (Obj->is_destroyed()) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Ship(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x19b, Obj->get_id());
        return;
    }

    // The record is discarded here, unlike in the other four.
    SystemPlayerView_AddTrackedObject(View, Obj);

    if (!SinglePlayer())
        return;

    // Raw slot, no is_valid; the same value goes into CREATESOLAR's payload.
    Info->VibeOtherGroupId = Players[View->PlayerId].rep_vibe;

    // The player id goes by VALUE.
    g_pClientInterface->SendServerCreateShip(View->PlayerId, *Info);
}

// FUN_06cf6bc0 -- CREATECOUNTER.
void SystemPlayerView_CreateCounterForPlayer(Observer* View, void* Payload, GameObject* Obj)
{
    // No null test on View or Obj.
    if (View->bShipGone != 0)
        return;

    if (Obj->is_destroyed()) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: CounterMeasure(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x23c, Obj->get_id());
        return;
    }

    // The record is KEPT this time.
    SystemTrackedObject* const Record = SystemPlayerView_AddTrackedObject(View, Obj);

    // Outside single player the object has to be in range before the client hears of it.
    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;

    Record->bCreatedOnClient = true;

    // This slot takes the ADDRESS of the player id (const unsigned int&), unlike the
    // ship / loot / mine slots.
    g_pClientInterface->SendServerCreateCounter(
        View->PlayerId, *static_cast<const FServerCreateProjectileInfo*>(Payload));
}

// FUN_06cf6d60 -- CREATEMINE: the countermeasure one with a different FDUMP line and slot,
// and the player id passed BY VALUE.
void SystemPlayerView_CreateMineForPlayer(Observer* View, void* Payload, GameObject* Obj)
{
    if (View->bShipGone != 0)
        return;

    if (Obj->is_destroyed()) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Mine(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x28a, Obj->get_id());
        return;
    }

    SystemTrackedObject* const Record =
        SystemPlayerView_AddTrackedObject(View, Obj);

    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;

    Record->bCreatedOnClient = true;

    g_pClientInterface->SendServerCreateMine(
        View->PlayerId, *static_cast<const FServerCreateProjectileInfo*>(Payload));
}

// FUN_06cf6df0 -- EXPLODEASTEROIDMINE; only caller StarSystem::send_mine_explosion.
// Neither the IClient pointer nor the record is tested.
void SystemPlayerView_SendMineExplosion(Observer* View, FMineExplosion* Rec)
{
    unsigned int const MineId = Rec->MineId;
    if (!SinglePlayer()) {
        GameObject* const Obj = Server_LookupGameObject(MineId);
        if (!SystemPlayerView_IsObjectVisible(View, Obj))
            return;
    }
    g_pClientInterface->SendServerExplodeAsteroidMine(View->PlayerId, *Rec);
}

// FUN_06cf66f0 (thiscall on the view). A dynamic solar is sent even out of range. Unlike
// the other notifiers a solar never gets a tracked record.
void SystemPlayerView_CreateSolarForPlayer(Observer* View, CSolarCreateInfo* Info, GameObject* Obj)
{
    if (View->bShipGone != 0)
        return;
    if (!SystemPlayerView_ShouldBeVisible(View, Obj) && Info->bDynamic == 0)
        return;
    g_pClientInterface->SendServerCreateSolar(View->PlayerId, *Info);
}

// FUN_06cf6ac0 (thiscall on the view, 21 stack dwords; the Costume by value). Outside
// single player both sender and receiver must be visible (SinglePlayer() is called twice).
// Retail sets only the low byte of the own-ship flag; the marshaller reads & 0xff, so the
// port passes 0/1.
void SystemPlayerView_SendComm(Observer* View, unsigned int SenderId,
                               unsigned int ReceiverId, unsigned int VoiceId,
                               Costume const& Cost, unsigned int CaptionIds,
                               unsigned int* TextIds, int TextCount, unsigned int PermutationIds,
                               float Duration)
{
    if (!SinglePlayer()) {
        GameObject* SenderObj = nullptr;
        StarSystem* SenderSystem = nullptr;
        if (!Server_FindObject(SenderId, SenderObj, SenderSystem))
            SenderObj = nullptr;
        if (!SystemPlayerView_IsObjectVisible(View, SenderObj))
            return;
    }
    if (!SinglePlayer()) {
        GameObject* ReceiverObj = nullptr;
        StarSystem* ReceiverSystem = nullptr;
        if (!Server_FindObject(ReceiverId, ReceiverObj, ReceiverSystem))
            ReceiverObj = nullptr;
        if (!SystemPlayerView_IsObjectVisible(View, ReceiverObj))
            return;
    }

    bool const bReceiverIsOwnShip =
        ReceiverId == Players[View->PlayerId].ship_object_id;
    static_assert(sizeof(Costume) == 13 * sizeof(unsigned int), "Costume is 13 dwords");
    g_pClientInterface->SendServerSendComm(View->PlayerId, SenderId, ReceiverId, VoiceId,
                                           Cost, CaptionIds, TextIds, TextCount, PermutationIds, Duration,
                                           bReceiverIsOwnShip);
}

// FUN_06cf6c50 -- CREATELOOT. The only notifier that tracks MORE THAN ONE object per call:
// it walks [First, Last) and sends one packet afterwards. No null tests on View, the
// elements or the payload.
void SystemPlayerView_CreateLootForPlayer(Observer* View, FServerCreateLootRequest* Payload,
                                          GameObject* const* First, GameObject* const* Last)
{
    if (View->bShipGone != 0)
        return;

    // An empty range skips straight to the send; it does not return.
    for (GameObject* const* It = First; It != Last; ++It) {
        GameObject* const Obj = *It;

        // Retail ABANDONS the whole call on the first dead entry (not a `continue`), and
        // at ERROR level, unlike the other three.
        if (Obj->is_destroyed()) {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Loot(%d) 'dead' before creation packet sent.",
                  kObserverCppPath, 0x256, Obj->get_id());
            return;
        }

        // Stored INSIDE the loop, so the last element wins -- retail's own behaviour; do
        // not hoist it.
        Payload->bMarkedBy = Obj->is_marked_by(View->PlayerId) ? 1u : 0u;

        SystemPlayerView_AddTrackedObject(View, Obj);
    }

    // The loot packet goes out ONLY in single player; there is no ShouldBeVisible arm and
    // bCreatedOnClient is left to AddTrackedObject's seeding.
    if (!SinglePlayer())
        return;

    g_pClientInterface->SendServerCreateLoot(View->PlayerId, *Payload);
}

// FUN_06cf6cf0 -- CREATEGUIDED. Takes the loot one's vector but uses ONLY its first element.
void SystemPlayerView_CreateGuidedForPlayer(Observer* View, void* Payload,
                                            GameObject* const* First, GameObject* const* Last)
{
    // No null test on View, the element or the record; the null-first and empty-range
    // exits are retail's.
    if (View->bShipGone != 0)
        return;

    if (First == nullptr || First == Last)
        return;

    GameObject* const Obj = *First;

    // No is_destroyed() guard and no FDUMP in this one -- the only member of the family
    // without them. Do not add one.
    SystemTrackedObject* const Record =
        SystemPlayerView_AddTrackedObject(View, Obj);

    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;

    Record->bCreatedOnClient = true;

    // Like the countermeasure slot, this one takes the ADDRESS of the player id.
    g_pClientInterface->SendServerCreateGuided(
        View->PlayerId, *static_cast<const FServerCreateGuidedInfo*>(Payload));
}

// FUN_06cf6950 -- the per-player DAMAGEOBJECT gate, and the only writer of the deferred-
// damage set at View+0x14. The send takes `(PlayerId, ObjId, record)`, where `record` is
// the PRIMARY list on the visible path and the DEFERRED one on the deferred-but-non-empty
// path -- two arguments reaching one PUSH, which Ghidra's `piVar7` hides.
void SystemPlayerView_DamageObject(Observer* View, unsigned int ObjId,
                                   DamageList* PrimaryRecord,
                                   DamageList* DeferredRecord)
{
    // No null test on View.
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    if (Obj == nullptr)
        return;

    // Solar only ((& 0x303), composed with cobj()'s (& 3)).
    CObject* const Raw = Obj->cobj();
    CSimple* Solar = nullptr;
    if (Raw != nullptr &&
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar) {
        Solar = Obj->csimple();
    }

    // Visible, or a tradelane ring (TypeBits 0x80): a ring's damage state has to reach the
    // client whether or not it is in range.
    bool bSend = SystemPlayerView_IsObjectVisible(View, Obj);
    DamageList* Record = PrimaryRecord;
    if (!bSend && Solar != nullptr && (Solar->TypeBits & 0x80u) != 0)
        bSend = true;

    if (!bSend) {
        // In single player there is nothing to defer TO.
        if (SinglePlayer())
            return;
        // A non-solar is dropped outright.
        if (Solar == nullptr)
            return;
        if (DeferredRecord->Count != 0) {
            // The caller's second record is already populated, so send that one instead of
            // remembering the id.
            Record = DeferredRecord;
            bSend = true;
        } else {
            View->PendingDamageIds.insert(ObjId);
            return;
        }
    }

    // The player id goes by VALUE.
    g_pClientInterface->SendServerDamageObject(View->PlayerId, ObjId, *Record);
}

// FUN_06cf6e60 -- "this object has left the system": the tracked-list drop, the
// deferred-damage set CLEAR, and the bShipGone latch.
void SystemPlayerView_OnObjectLeftSystem(Observer* View,
                                         unsigned int const* ObjId,
                                         unsigned int const* Destination)
{
    // No null test on View, ObjId or Destination, nor on the sentinel, nodes, records or
    // their objects. The first hit ends the walk; +0x2f0 is read before the record's null
    // test, and size-- is unconditional.
    bool bWasCreatedOnClient = false;
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;
        if (Record->Object->get_id() != *ObjId)
            continue;
        bWasCreatedOnClient = (Record->bCreatedOnClient != false);
        delete Record;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --View->Tracked.Size;
        break;
    }

    View->PendingDamageIds.clear();

    // The latch: the player's own ship (raw slot, no is_valid).
    if (*ObjId == Players[View->PlayerId].ship_object_id)
        View->bShipGone = 1;

    // Sent when EITHER the dropped record had been created on the client OR bShipGone is set.
    if (bWasCreatedOnClient || View->bShipGone != 0) {
        FServerSystemSwitchOutInfo Wire;
        Wire.ShipId = *ObjId;
        Wire.GateId = *Destination;
        g_pClientInterface->SendServerSystemSwitchOut(View->PlayerId, Wire);
    }
}

// FUN_06cf6fc0 (thiscall on the view) -- "this object has landed". The LAND twin of the
// function above: same tracked-list drop, no set clear, no latch.
void SystemPlayerView_OnObjectLanded(Observer* View,
                                     unsigned int const* ObjId,
                                     unsigned int const* Arg2,
                                     unsigned int const* Arg3)
{
    // As above: one hit at most, +0x2f0 read before the null test, size-- unguarded.
    bool bWasCreatedOnClient = false;
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;
        if (Record->Object->get_id() != *ObjId)
            continue;
        bWasCreatedOnClient = (Record->bCreatedOnClient != false);
        delete Record;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --View->Tracked.Size;
        break;
    }

    // Built before either test.
    FServerLandInfo Wire;
    Wire.ShipId = *ObjId;
    Wire.DockId = *Arg2;
    Wire.BaseId = *Arg3;

    // A record never created on the client only goes out for this player's own ship.
    PlayerData& Owner = Players[View->PlayerId];
    if (!bWasCreatedOnClient && Wire.ShipId != Owner.ship_object_id)
        return;

    // Outside single player, the player's own ship landing autosaves the slot.
    if (!SinglePlayer() && *ObjId == Owner.ship_object_id)
        PlayerData_AutoSave(Owner);                                 // FUN_06d4c430

    g_pClientInterface->SendServerLand(View->PlayerId, Wire);
}

// StarSystem::create_med_asteroid / create_med_mine -- retail FUN_06d09bb0 / FUN_06d09c80.
// See server_sysdb.h. No test of find()'s answer: the object is entered either way.
SrvAsteroid::MedAsteroid* StarSystem::create_med_asteroid(unsigned int const& ObjId)
{
    SrvAsteroid::MedAsteroid* const Obj = new SrvAsteroid::MedAsteroid();
    MedAsteroids.append(Obj);
    Obj->find(ObjId);
    Obj->enter_system(this, nullptr);
    return Obj;
}

SrvAsteroid::MedMine* StarSystem::create_med_mine(unsigned int const& ObjId)
{
    SrvAsteroid::MedMine* const Obj = new SrvAsteroid::MedMine();
    MedAsteroids.append(Obj);
    Obj->find(ObjId);
    Obj->enter_system(this, nullptr);
    return Obj;
}

// StarSystem::send_mine_explosion -- retail FUN_06d09270, on a local copy of the record.
void StarSystem::send_mine_explosion(FMineExplosion const& Rec)
{
    FMineExplosion Local = Rec;
    SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
    while (Node != static_cast<SystemPlayerNode*>(PlayerListHead)) {
        SystemPlayerView_SendMineExplosion(&Node->View, &Local);
        Node = Node->Next;
    }
}

// StarSystem::explode -- retail FUN_06d0b260. See server_sysdb.h for the outline.
void StarSystem::explode(ExplosionDamageEvent const& Blast)
{
    GameObject* const Inflictor = find_object(Blast.InflictorId);

    // Up to 0x3c parts in the blast sphere.
    CObject* Parts[0x3c];
    int const Count = PhySys::FindSphereCollisions(SystemInfo->NicknameId, Blast.Position,
                                                   Blast.Explosion->Radius, Parts, 0x3c);

    // Reserve only for a non-zero count; a negative count clamps to 0.
    std::vector<CObject*> Roots;
    if (Count != 0)
        Roots.reserve(Count < 0 ? 0u : static_cast<size_t>(Count));

    // Parts of the skipped collision group are left out; the rest go in by root.
    for (int Index = 0; Index < Count; ++Index) {
        char const* const Group = PhySys::GetCollisionGroup(Parts[Index]);
        if (Blast.SkipCollisionGroup != nullptr && Group != nullptr &&
            _stricmp(Blast.SkipCollisionGroup, Group) == 0)
            continue;
        Roots.push_back(GetRoot(Parts[Index]));
    }

    Archetype::Explosion const* const Explosion = Blast.Explosion;

    // The damage pass, when there is hull or energy damage.
    if (Explosion->HullDamage > 0.0f || Explosion->EnergyDamage > 0.0f) {
        // The ids already damaged.
        std::vector<unsigned int> Damaged;

        for (std::vector<CObject*>::iterator It = Roots.begin(); It != Roots.end(); ++It) {
            CObject* const Root = *It;
            if (Root == nullptr || (static_cast<unsigned int>(Root->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
                continue;
            unsigned int const Id = static_cast<CSimple*>(Root)->Id;
            // Not the exploding object itself.
            if (Id == Blast.ExplodingObjId)
                continue;
            // Each object once.
            if (std::find(Damaged.begin(), Damaged.end(), Id) != Damaged.end())
                continue;
            Damaged.push_back(Id);

            GameObject* const Obj = find_object(Id);
            if (Obj == nullptr)
                continue;
            if (Obj->is_destroyed())
                continue;
            // Player-on-player damage is skipped unless DAT_06d88a80 is set.
            if (GbAllowPvp == 0 && Inflictor != nullptr && Inflictor->is_player() &&
                Obj->is_player())
                continue;

            DamageList Damage;
            Damage.set_cause(Blast.Cause);
            Damage.set_inflictor_id(Blast.InflictorId);
            // Read through the (& 3) narrowing with no null test, as retail does.
            if (Inflictor != nullptr)
                Damage.set_inflictor_owner_player(Inflictor->csimple()->OwnerPlayer);

            Obj->explosion_hit(Blast, Damage);
            Obj->apply_damage_list(Damage);
        }
    }

    // The impulse pass; skipped only when the impulse is exactly 0.
    if (!(Explosion->Impulse == 0.0f)) {
        std::vector<unsigned int> Pushed;

        for (std::vector<CObject*>::iterator It = Roots.begin(); It != Roots.end(); ++It) {
            CObject* const Root = *It;
            if (Root == nullptr || (static_cast<unsigned int>(Root->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
                continue;
            CSimple* const Simple = static_cast<CSimple*>(Root);
            // Players' objects are not pushed.
            if (Simple->OwnerPlayer != 0)
                continue;
            // Unlike the damage pass the insert's answer is NOT tested, so an object whose
            // several parts were in range is pushed once per part (retail).
            if (std::find(Pushed.begin(), Pushed.end(), Simple->Id) == Pushed.end())
                Pushed.push_back(Simple->Id);

            Vector const Center = Root->get_center_of_mass();
            Vector Dir;
            Dir.x = Center.x - Blast.Position.x;
            Dir.y = Center.y - Blast.Position.y;
            Dir.z = Center.z - Blast.Position.z;

            // Retail's order: y*y + z*z, then + x*x. A zero length gets a random direction.
            float const LengthSq = (Dir.y * Dir.y + Dir.z * Dir.z) + Dir.x * Dir.x;
            if (!(LengthSq == 0.0f)) {
                float const Inverse = 1.0f / sqrtf(LengthSq);
                Dir.x = Dir.x * Inverse;
                Dir.y = Dir.y * Inverse;
                Dir.z = Dir.z * Inverse;
            } else {
                Dir = random_on_unit_sphere();
            }

            float const Impulse = Blast.Explosion->Impulse;
            Dir.x = Dir.x * Impulse;
            Dir.y = Dir.y * Impulse;
            Dir.z = Dir.z * Impulse;

            // Only with a physics body.
            if (Root->BodyPtr != nullptr)
                PhySys::AddImpulseAtPoint(Root, Dir, Blast.Position);
        }
    }
}

// FUN_06d0d500 -- StarSystem::munition_collision. See server_sysdb.h for the record.
void StarSystem::munition_collision(SSPMunitionCollisionInfo const& Info)
{
    GameObject* const Target = find_object(Info.ObjectId);
    GameObject* const Shooter = find_object(Info.AttackerId);

    // Without PvP (DAT_06d88a80), a player's shot at a missing or player target ends here.
    if (GbAllowPvp == 0 && Shooter != nullptr && Shooter->is_player())
    {
        if (Target == nullptr)
            return;
        if (Target->is_player())
            return;
    }

    if (Target == nullptr)
        return;
    if (Target->is_destroyed())
        return;

    // GetEquipment's result is not tested.
    FMunitionImpact Impact;
    Impact.ShooterId = 0u;
    Impact.Munition = nullptr;
    Impact.SubObjId = SUBOBJ_ID_NONE;
    Archetype::Equipment* const MunitionArch = Archetype::GetEquipment(Info.ProjectileArchId);
    Impact.Munition = static_cast<Archetype::Munition const*>(MunitionArch);
    Impact.ShooterId = Info.AttackerId;
    Impact.SubObjId = Info.SubObjId;
    std::memcpy(&Impact.HitPos, &Info.HitPosX, sizeof(Vector));

    DamageList Damage;

    // The cause from the munition's type: BULLET (0x2000000) -> 2, GUIDED (0x1000000) -> 3,
    // anything else -> 1.
    unsigned int const ObjectType = MunitionArch->ObjectType;
    if (ObjectType == 0x2000000u)
        Damage.set_cause(static_cast<DamageCause>(2));
    else
        Damage.set_cause(static_cast<DamageCause>((ObjectType == 0x1000000u) ? 3 : 1));

    Damage.set_inflictor_id(Info.AttackerId);

    // Not null-tested after the (& 3) narrowing.
    if (Shooter != nullptr)
        Damage.set_inflictor_owner_player(Shooter->csimple()->OwnerPlayer);

    Target->munition_hit(Impact, Damage);
    Target->apply_damage_list(Damage);
}

// FUN_06d08e80 -- FServerLootDesc's constructor. Position, orientation, velocity and +0x47
// are left alone.
static void ServerLootDesc_Init(FServerLootDesc* Desc)
{
    Desc->LootId = 0u;
    Desc->ContentsNickname = 0u;
    Desc->ContentsHitPts = 0.0f;
    Desc->Units = 0u;
    Desc->ContainerNickname = 0u;
    Desc->HitPts = 0.0f;
    Desc->NameOverride = 0u;
    Desc->Field_0x44 = 1;
    Desc->bTemporary = 0;
    Desc->bMarkedBy = 0;
}

// StarSystem::create_loot -- retail FUN_06d08a40.
void StarSystem::create_loot(unsigned int SourceObjId, int Mode, FLootRecord const* Records, int Count)
{
    FServerLootDesc* const Items =
        static_cast<FServerLootDesc*>(operator new(static_cast<size_t>(Count) * sizeof(FServerLootDesc)));
    for (int Index = 0; Index < Count; ++Index)
        ServerLootDesc_Init(&Items[Index]);

    // The CREATELOOT request every player view gets at the end.
    FServerCreateLootRequest Request = {};
    Request.OwnerId = SourceObjId;
    Request.Kind = static_cast<unsigned int>(Mode);
    Request.bMarkedBy = 0;
    Request.bTemporary = 0;
    Request.Items = Items;
    Request.Count = static_cast<unsigned char>(Count);

    std::vector<GameObject*> Spawned;

    // An unsigned test: nothing to spawn skips straight to the player walk (which still
    // runs).
    if (static_cast<unsigned int>(Count) != 0u) {
        Spawned.reserve(static_cast<size_t>(Count >= 0 ? Count : 0));

        FLootRecord const* Record = Records;
        FServerLootDesc* Item = Items;
        for (unsigned int Left = static_cast<unsigned int>(Count); Left != 0u; --Left) {
            ::Loot* const Obj = new ::Loot();

            // The result is not tested.
            Obj->create(const_cast<Archetype::Equipment*>(Record->Arch));

            CLoot* const LootObject = Obj->cloot();

            // FUN_06d08e20's defaults come from the member initialisers.
            CLoot::CreateParms Parms;
            Parms.Pad58 = 0u;
            Parms.Id = Record->Id;
            Parms.ObjDb = this;
            Parms.HitPts = Record->HitPts;
            Parms.Pos = Record->Position;
            Parms.OwnerPlayer = 0u;
            Parms.Orient = Record->Orientation;
            Parms.Velocity = Record->Velocity;
            Parms.AngularVelocity = Record->AngularVelocity;
            Parms.OwnerId = SourceObjId;
            Parms.Units = static_cast<unsigned int>(Record->Count);
            Parms.NameId = static_cast<int>(Record->NameId);
            Parms.ContentsArch = Record->ContentsArch;
            Parms.bTemporary = Record->bMission != 0;
            Parms.ContentsHitPts = Record->ContentsHitPts;
            Parms.bCanAiTractor = Record->bCanAiTractor != 0;

            LootObject->init(Parms);

            if ((Record->MarkForPlayer & 0x3fffffffu) != 0u)
                Obj->mark_for_player(Record->MarkForPlayer, 1);

            Obj->enter_system(this, nullptr);

            Loot.append(Obj);

            Spawned.push_back(Obj);

            // Controller message 0x2b. The field names are what the stores say; retail
            // names none.
            struct FControllerMsg2B {
                unsigned int LootId;
                unsigned int SourceObjId;
                unsigned int GoodId;
                int Count;
            } Msg;
            Msg.LootId = Parms.Id;
            Msg.SourceObjId = SourceObjId;
            Msg.GoodId = Arch2Good(Record->ContentsArch->get_id());
            Msg.Count = Record->Count;
            Server_ControllerDispatch(0, pub::Controller::EMessage::LootSpawned, &Msg);

            ServerLootDesc_Fill(Item, LootObject);                  // FUN_06d07b90

            ++Item;
            ++Record;
        }
    }

    SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
    while (Node != static_cast<SystemPlayerNode*>(PlayerListHead)) {
        GameObject* const* const First = Spawned.empty() ? nullptr : &Spawned[0];
        GameObject* const* const Last = First + Spawned.size();
        SystemPlayerView_CreateLootForPlayer(&Node->View, &Request, First, Last);
        Node = Node->Next;
    }

    operator delete(Items);
}

// StarSystem::create_mine -- retail FUN_06d08ef0. One mine per launch record, from
// SrvMineDropper::launch.
void StarSystem::create_mine(FServerLaunchRecord const& Rec)
{
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x3aa,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    ::Mine* const Obj = new ::Mine();                               // FUN_06cf3330

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();
    Obj->create(Arch);
    Arch->Release();

    CMine* const MineObject = Obj->cmine();

    // FUN_06d08e20's defaults are the member initialisers.
    CMine::CreateParms Parms;

    Parms.Id = Maker->AllocateObjectID();
    if ((Parms.Id & 0x3fffffffu) == 0u)
    {
        // The mine wrapper built above is neither entered nor freed (retail).
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x3c2,
              "Failed to allocate valid ID for new mine.\n");
        return;
    }

    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;

    // random_inside_sphere(|Velocity|). x87: (x*x + y*y) + z*z at double precision, the
    // root passed as a float.
    double LengthSq = static_cast<double>(Rec.Velocity.x) * static_cast<double>(Rec.Velocity.x) +
                      static_cast<double>(Rec.Velocity.y) * static_cast<double>(Rec.Velocity.y);
    LengthSq += static_cast<double>(Rec.Velocity.z) * static_cast<double>(Rec.Velocity.z);
    float const Speed = static_cast<float>(std::sqrt(LengthSq));
    Parms.AngularVelocity = random_inside_sphere(Speed);
    Parms.OwnerId = Rec.OwnerId;

    MineObject->init(Parms);
    Obj->enter_system(this, nullptr);
    Obj->arm();                                                     // FUN_06cf3560

    Mines.append(Obj);

    // The CREATEMINE record; every default of retail's inlined ctor but +0x44 is
    // overwritten.
    FServerCreateProjectileInfo Info;
    Info.ObjId = Parms.Id;
    Info.ArchetypeNickname = Arch->get_id();
    Info.OwnerId = Rec.OwnerId;
    Info.SubObjId = Rec.EquipId;
    Info.Pad0e = 0;
    Info.Position = Parms.Pos;
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);
    Info.Velocity = Parms.Velocity;
    Info.AngularVelocity = Parms.AngularVelocity;
    Info.OwnerPlayer = 0u;

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
        SystemPlayerView_CreateMineForPlayer(&Player->View, &Info, Obj);
}

// StarSystem::create_counter_measure -- retail FUN_06d092c0, from
// SrvCounterMeasureDropper::launch. create_mine's body with different FDUMP lines (the id
// text still says "new mine", retail's string), the (& 0x17) narrowing, divert_missiles
// after enter_system, CounterMeasures, and the countermeasure notifier.
void StarSystem::create_counter_measure(FServerLaunchRecord const& Rec)
{
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x401,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    ::CounterMeasure* const Obj = new ::CounterMeasure();          // FUN_06ce6170

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();
    Obj->create(Arch);
    Arch->Release();

    CCounterMeasure* const CmObject = Obj->ccountermeasure();

    CCounterMeasure::CreateParms Parms;

    Parms.Id = Maker->AllocateObjectID();
    if ((Parms.Id & 0x3fffffffu) == 0u)
    {
        // The wrapper built above is neither entered nor freed.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x419,
              "Failed to allocate valid ID for new mine.\n");
        return;
    }

    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;

    // As in create_mine (x87, double precision).
    double LengthSq = static_cast<double>(Rec.Velocity.x) * static_cast<double>(Rec.Velocity.x) +
                      static_cast<double>(Rec.Velocity.y) * static_cast<double>(Rec.Velocity.y);
    LengthSq += static_cast<double>(Rec.Velocity.z) * static_cast<double>(Rec.Velocity.z);
    float const Speed = static_cast<float>(std::sqrt(LengthSq));
    Parms.AngularVelocity = random_inside_sphere(Speed);
    Parms.OwnerId = Rec.OwnerId;

    CmObject->init(Parms);
    Obj->enter_system(this, nullptr);
    Obj->divert_missiles();                                         // FUN_06ce6300

    CounterMeasures.append(Obj);

    FServerCreateProjectileInfo Info;
    Info.ObjId = Parms.Id;
    Info.ArchetypeNickname = Arch->get_id();
    Info.OwnerId = Rec.OwnerId;
    Info.SubObjId = Rec.EquipId;
    Info.Pad0e = 0;
    Info.Position = Parms.Pos;
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);
    Info.Velocity = Parms.Velocity;
    Info.AngularVelocity = Parms.AngularVelocity;
    Info.OwnerPlayer = 0u;

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
        SystemPlayerView_CreateCounterForPlayer(&Player->View, &Info, Obj);
}

// StarSystem::create_guided -- retail FUN_06d09640, from SrvGun::launch_guided. Unlike
// create_mine, the id is allocated and the target looked up BEFORE the wrapper is built.
void StarSystem::create_guided(FServerLaunchRecord const& Rec)
{
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x446,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    unsigned int const Id = Maker->AllocateObjectID();
    if ((Id & 0x3fffffffu) == 0u)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x44d,
              "Failed to allocate valid, dynamic ID for guided object.\n");
        return;
    }

    // The lock target.
    GameObject* const Target = find_object(Rec.TargetId);

    ::Guided* const Obj = new ::Guided();                           // FUN_06cf3ce0

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();
    Obj->create(Arch);
    Arch->Release();

    CGuided* const GuidedObject = Obj->cguided();

    CGuided::CreateParms Parms;
    Parms.Id = Id;
    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;
    Parms.OwnerId = Rec.OwnerId;
    Parms.Target = static_cast<IObjRW*>(Target);
    Parms.SubTarget = Rec.TargetSubId;

    GuidedObject->init(Parms);

    GuidedMunitions.append(Obj);

    Obj->enter_system(this, nullptr);
    Obj->arm();

    std::vector<GameObject*> Spawned;
    Spawned.push_back(Obj);

    FServerCreateGuidedInfo Info;
    Info.ObjId = Parms.Id;
    Info.Position = Parms.Pos;
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);
    Info.TargetId = Rec.TargetId;
    Info.TargetSubObjId = Rec.TargetSubId;
    Info.Pad26 = 0;
    Info.ArchetypeNickname = Arch->get_id();
    Info.OwnerPlayer = 0u;
    Info.OwnerId = Rec.OwnerId;
    Info.SubObjId = Rec.EquipId;
    Info.Pad36 = 0;

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        GameObject* const* const First = &Spawned[0];
        SystemPlayerView_CreateGuidedForPlayer(&Player->View, &Info, First, First + Spawned.size());
    }

    // A live CEqObj target with a behavior interface hears of the incoming missile (event 6,
    // {owner id, missile id}).
    if (Target != nullptr && !Target->is_destroyed())
    {
        CObject* const Raw = Target->Object;
        if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
        {
            IBehaviorManager* const Behavior = static_cast<CEqObj*>(Raw)->get_behavior_interface();
            if (Behavior != nullptr)
            {
                unsigned int MissileBlock[2];
                MissileBlock[0] = Rec.OwnerId;
                MissileBlock[1] = Id;
                FBehaviorManagerEvent MissileEvent;
                MissileEvent.Type = 6;
                MissileEvent.Data = MissileBlock;
                Behavior->OnEvent(&MissileEvent);
            }
        }
    }
}

// StarSystem::create_bullet -- retail FUN_06d099e0, one gun round from
// SrvGun::launch_bullets. No player view or controller hears of it here: the client builds
// its own rounds from the FIREWEAPON notify (FUN_06d0e2e0).
void StarSystem::create_bullet(FServerBulletRecord const& Rec)
{
    ::Bullet* const Obj = new ::Bullet();

    Archetype::Munition* const Arch = const_cast<Archetype::Munition*>(Rec.Arch);
    Arch->load();
    Obj->create(Arch);                                              // FUN_06cf4d40
    Arch->Release();

    CBeam* const Beam = Obj->cbeam();

    // Every field is written here; retail inlines the ctor away.
    CProjectile::CreateParms Parms;
    Parms.Id = 0u;
    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.HitPts = -1.0f;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;
    Parms.AngularVelocity = Vector{0.0f, 0.0f, 0.0f};
    Parms.Pad58 = 0u;
    Parms.OwnerId = Rec.OwnerId;
    // Rec.EquipId is not read.

    Beam->init(Parms);
    Obj->enter_system(this, nullptr);

    Beams.append(Obj);
}

// Four StarSystem members GameObject / EqObj reach through OwnerSystem; see server_sysdb.h.

// FUN_06d0cfa0: ships first, then solars.
void StarSystem::notify_object_left(unsigned int ObjId)
{
    MetaNode<GameObject>* Node = nullptr;
    while (Ships.traverse(Node, true))
        Node->Data->on_object_left_system(ObjId);
    Node = nullptr;
    while (Solars.traverse(Node, true))
        Node->Data->on_object_left_system(ObjId);
}

// FUN_06d0d730.
void StarSystem::collision_damage(FCollisionDamage const& Rec, GameObject* Target)
{
    GameObject* const Source = find_object(Rec.SourceId);

    // Without PvP (DAT_06d88a80), a player source never damages a player target.
    if (GbAllowPvp == 0 && Source != nullptr && Source->is_player())
    {
        if (Target == nullptr)
            return;
        if (Target->is_player())
            return;
    }
    if (Target == nullptr)
        return;
    if (Target->is_destroyed())
        return;

    // Byte +0x0c is the low byte of this function's own first argument slot, i.e. of the
    // Rec POINTER; retail never writes a value of its own there. Reproduced as read.
    FSubObjDamage Hit;
    Hit.InflictorId = Rec.SourceId;
    Hit.SubObjId = Rec.TargetSubObjId;
    Hit.Damage = Rec.Damage;
    Hit.Field_0x0c = static_cast<unsigned char>(reinterpret_cast<uintptr_t>(&Rec) & 0xffu);

    // Cause 1; the owner player read through the narrowing without a null test.
    DamageList Damage;
    Damage.Cause = static_cast<DamageCause>(1);
    Damage.InflictorId = Rec.SourceId;
    Damage.InflictorOwner = 0;
    if (Source != nullptr)
        Damage.InflictorOwner = Source->csimple()->OwnerPlayer;

    Target->damage_sub_obj(Hit, Damage);
    Target->apply_damage_list(Damage);
}

// FUN_06d0d6e0.
void StarSystem::badlands_collision(SSPBadLandsObjCollisionInfo const& Info)
{
    GameObject* const Obj = find_object(Info.ObjectId);
    if (Obj == nullptr)
        return;
    if (Obj->is_destroyed())
        return;
    if (Obj->is_invincible())
        return;
    Obj->destroy(1, Info.BadLandsObjId);
}

// FUN_06d0cd60: solars docking at BaseId (any base when 0), plus, for BaseId 0, baseless
// solars whose type bits lie within 0xc40 (jump gate / airlock / jump hole).
void StarSystem::collect_dockable_solars(std::list<GameObject*>& Out, unsigned int BaseId)
{
    MetaNode<GameObject>* Node = nullptr;
    while (Solars.traverse(Node, true))
    {
        GameObject* const Obj = Node->Data;
        CObject* const Cobj = Obj->cobj();
        CEqObj* Eq = nullptr;
        if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            Eq = static_cast<CEqObj*>(Cobj);
        // Retail reads +0x164 through the narrowed pointer without testing it.
        unsigned int const Base = Eq->DockingPointBaseId;
        if (Base != 0)
        {
            if (BaseId != 0 && Base != BaseId)
                continue;
            Out.push_back(Obj);
            continue;
        }
        unsigned int const Bits = Eq->TypeBits;
        if ((Bits & 0xc40u) != Bits)
            continue;
        if (BaseId != 0)
            continue;
        Out.push_back(Obj);
    }
}

// StarSystem::send_comm -- FUN_06d0db30. With no sender (id & 0x3fffffff == 0) every valid
// player gets it in single player, when addressed, or on a broadcast. Otherwise the
// addressee gets it (and a non-broadcast stops there), and a broadcast reaches every
// player within 10000 of the sender (an unordered compare sends). x87: the squared
// distance (dz^2 + dx^2) + dy^2 is taken in double, as at MSVC's default precision.
void StarSystem::send_comm(unsigned int SenderId, unsigned int ReceiverId, unsigned int VoiceId,
                           Costume const& Cost, unsigned int CaptionIds, unsigned int* TextIds,
                           int TextCount, unsigned int PermutationIds, float Duration, bool bBroadcast)
{
    bool bNoSender = false;
    Vector SenderPos = {};
    if ((SenderId & 0x3fffffffu) == 0) {
        bNoSender = true;
    } else {
        GameObject* const Sender = find_object(SenderId);
        if (Sender == nullptr)
            return;
        SenderPos = Sender->get_position();
    }

    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        unsigned int const PlayerId = Node->View.PlayerId;
        if (!Players.is_valid(PlayerId))
            continue;
        unsigned int const ShipId = Players[PlayerId].ship_object_id;

        if (bNoSender) {
            if (SinglePlayer() || ReceiverId == ShipId || bBroadcast)
                SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                          CaptionIds, TextIds, TextCount, PermutationIds, Duration);
            continue;
        }

        if (ReceiverId == ShipId) {
            SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                      CaptionIds, TextIds, TextCount, PermutationIds, Duration);
            if (!bBroadcast)
                return;
            continue;
        }

        if (!bBroadcast)
            continue;
        GameObject* const Ship = find_object(ShipId);
        if (Ship == nullptr)
            continue;
        Vector const& Pos = Ship->get_position();
        double const Dx = static_cast<double>(Pos.x) - static_cast<double>(SenderPos.x);
        double const Dy = static_cast<double>(Pos.y) - static_cast<double>(SenderPos.y);
        double const Dz = static_cast<double>(Pos.z) - static_cast<double>(SenderPos.z);
        double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
        if (DistSq > static_cast<double>(1.0e8f))
            continue;
        SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                  CaptionIds, TextIds, TextCount, PermutationIds, Duration);
    }
}

// FUN_06d0e750.
void StarSystem::broadcast_fuse(FServerBurnFuseInfo const& Info)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Info.ObjectId))
            continue;
        g_pClientInterface->SendServerBurnFuse(Node->View.PlayerId, Info);
    }
}

// ===========================================================================
// The IObjAffect notifiers. Each walks the player list with no null test on the head and
// calls g_pClientInterface unguarded. The player-skip forms compare the view's player with
// *SourceClient, the client that asked for the change, so it is not told of its own action.
// ===========================================================================

// FUN_06d0e500; only caller EqObj slot_4c. The record's pad byte is uninitialised in
// retail, zero here.
void StarSystem::notify_activate_equip(unsigned int const& ObjId, unsigned short const& EquipId,
                                       bool bActivate, int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateEquip Record = {};
        Record.ObjectId = ObjId;
        Record.EquipId = EquipId;
        Record.bActivate = bActivate;
        g_pClientInterface->SendCommonActivateEquip(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e580; only caller Ship slot_50.
void StarSystem::notify_cruise(unsigned int const& ObjId, bool bEngage, bool bArg2,
                               int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateCruise Record = {};
        Record.ObjectId = ObjId;
        Record.bActivate = bEngage;
        Record.bDisruptCruise = bArg2 ? 1u : 0u;
        g_pClientInterface->SendCommonActivateCruise(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e600; only caller Ship slot_54.
void StarSystem::notify_thrusters(unsigned int const& ObjId, bool bActivate, int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateThrusters Record = {};
        Record.ObjectId = ObjId;
        Record.bActivate = bActivate;
        g_pClientInterface->SendCommonActivateThrusters(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e670. Callers: Ship set_target, EqObj slot_44, Guided set_target. A missing
// object is tested per node, not once. A solar type (& 0xfdff) is sent without the
// visibility test; anything else must be visible.
void StarSystem::notify_set_target(unsigned int const& ObjId, unsigned short const& SubObjId,
                                   IObjRW* Target, unsigned short const& SubTarget,
                                   int const& SourceClient)
{
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (Obj == nullptr)
            continue;
        unsigned int Type = 0u;
        Obj->get_type(Type);
        if ((Type & 0xfdffu) == 0u && !SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        XSetTarget Record = {};
        Record.ObjectId = ObjId;
        Record.SubObjId = SubObjId;
        Record.TargetObjectId = (Target != nullptr) ? Target->get_id() : 0u;
        Record.TargetSubObjId = SubTarget;
        g_pClientInterface->SendCommonSetTarget(Node->View.PlayerId, Record);
    }
}

// FUN_06d0f0c0; only caller Ship slot_38 (the jettison). OwnerPlayer is read unguarded;
// a player's ship loses the items without the notify. No player skip here.
void StarSystem::notify_jettison(GameObject* Obj, unsigned short EquipId, int Count)
{
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    if (Players.is_valid_ship_owner(Owner, ObjId))
        PlayerData_RemoveEquipment(Players[Owner], EquipId, Count, false);

    XJettisonCargo Record = {};
    Record.ObjectId = ObjId;
    Record.ItemId = EquipId;
    Record.Count = static_cast<unsigned int>(Count);

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        g_pClientInterface->SendCommonJettisonCargo(Node->View.PlayerId, Record);
    }
}

// StarSystem::notify_fire_weapon -- retail FUN_06d0e2e0, from SrvGun::fire_forward.
void StarSystem::notify_fire_weapon(unsigned int const& ObjId, std::vector<unsigned short> const& EquipIds,
                                    Vector const& Target, int SourceClient)
{
    XFireWeaponInfo Info;
    Info.ObjectId = ObjId;
    Info.TargetPos = Target;

    // VC6's operator= into an empty vector, as std::vector's.
    Info.Hardpoints = EquipIds;

    // Every player but SourceClient that can see the shooter.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ObjId))
            continue;
        g_pClientInterface->SendCommonFireWeapon(Player->View.PlayerId, Info);
    }
}

// FUN_06d0c970. See server_sysdb.h.
void StarSystem::complete_dock(GameObject* ShipObj, GameObject* Dock)
{
    ::Ship* const DockingShip = static_cast< ::Ship*>(ShipObj);
    if (DockingShip->DockedObjId != 0)
        return;

    // Every unowned ship's behavior manager hears of the dock (OwnerPlayer read unguarded,
    // no test on the 0x503 narrowing).
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
        CEqObj* const Other =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Other->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // Event 3, {dock id, dock type, ship id}; the calls in retail order.
        unsigned int LeftBlock[3];
        LeftBlock[2] = ShipObj->get_id();
        LeftBlock[0] = Dock->get_id();
        unsigned int DockType;
        Dock->get_type(DockType);
        LeftBlock[1] = DockType;
        FBehaviorManagerEvent LeftEvent;
        LeftEvent.Type = 3;
        LeftEvent.Data = LeftBlock;
        Behavior->OnEvent(&LeftEvent);
    }

    unsigned int Msg[3];
    Msg[0] = ShipObj->get_id();
    Msg[1] = Dock->get_id();
    Msg[2] = SystemInfo->NicknameId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::DockComplete, Msg);

    // Every player view hears that the ship left for the dock; both ids fetched per player.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        unsigned int const DockId = Dock->get_id();
        unsigned int const ShipId = ShipObj->get_id();
        SystemPlayerView_OnObjectLeftSystem(&Player->View, &ShipId, &DockId);
    }

    DockingShip->DockedObjId = Dock->get_id();

    if (ShipObj->csimple()->OwnerPlayer != 0u)
    {
        // A jump gate / jump hole protects the player's ship while it goes through.
        if ((Dock->get_type_bits() & 0x40u) != 0u || (Dock->get_type_bits() & 0x800u) != 0u)
        {
            FInvincibility Cmd;
            Cmd.Type = 2;
            Cmd.bProtect = true;
            Cmd.bFromPlayers = true;
            Cmd.Pad06[0] = 0;
            Cmd.Pad06[1] = 0;
            Cmd.MaxHpLoss = 0.0f;
            ShipObj->set_invincible(Cmd);
        }
        return;
    }

    // An AI ship's CShip (not null-tested after the narrowing).
    CObject* const Raw = ShipObj->Object;
    CShip* const Cs =
        (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            ? static_cast<CShip*>(Raw) : nullptr;

    if (SinglePlayer())
    {
        Cs->bJumpedOut = 0;
        return;
    }

    // A gate or hole gives the ship 12 seconds to go through.
    Cs->enable_collisions_r(false);
    if ((Dock->get_type_bits() & 0x40u) != 0u || (Dock->get_type_bits() & 0x800u) != 0u)
        DockingShip->DockExitTimer = 12.0f;                // .rdata:06d67cc0
    else
        DockingShip->DockExitTimer = 0.0f;
}

// FUN_06d0cc00. See server_sysdb.h.
void StarSystem::arrive_ship(GameObject* Obj, GameObject* JumpObj, Vector const& Pos, Matrix const& Orient)
{
    unsigned int Bits = 0u;
    if (JumpObj != nullptr)
        Bits = JumpObj->get_type_bits();

    // OwnerPlayer read unguarded.
    if (Obj->csimple()->OwnerPlayer == 0u)
    {
        // Handed on without a null test.
        CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
        CShip* const Cs =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CShip*>(Raw) : nullptr;

        if ((Bits & 0x40u) != 0u || (Bits & 0x800u) != 0u)
        {
            // Retail passes Obj itself as the jump object.
            Cs->jump_in(static_cast<IObjRW*>(Obj), Pos, Orient, 0.5f);
        }
        else if ((Bits & 0x400u) != 0u)
        {
            Cs->airlock_in(Pos, Orient);
        }
    }

    FPayloadServerSystemSwitchIn Record;
    Record.ShipId = Obj->get_id();
    Record.JumpObjTypeBits = Bits;
    Record.Position = Pos;
    Server_MatrixToQuat(Record.Orientation, Orient);

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        g_pClientInterface->SendServerSystemSwitchIn(Node->View.PlayerId, Record);
    }
}

// FUN_06d0d000. See server_sysdb.h.
void StarSystem::remove_ship(GameObject* Obj, PhySys::CreateParms* OutParms)
{
    // Read unguarded.
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;

    // Every view but the owner's.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == Owner)
            continue;
        SystemPlayerView_OnObjectDestroyed(&Player->View, Obj, 0);
    }

    unsigned int const ObjId = Obj->get_id();
    Server_LookupCacheForget(ObjId);

    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data == Obj)
        {
            Ships.remove(Node);
            break;
        }
    }

    // On both paths.
    Obj->leave_system(OutParms);
}

// FUN_06d0d100. See server_sysdb.h.
void StarSystem::add_ship(GameObject* Obj, PhySys::CreateParms* Parms, Vector const& Pos, Matrix const& Orient)
{
    Parms->SystemId = SystemInfo->NicknameId;

    // The (& 1) narrowing; the writes below do not test the result.
    CObject* const Raw =
        (Obj != nullptr && Obj->Object != nullptr &&
         (static_cast<unsigned int>(Obj->Object->ObjectClass) & CObject::ClassObject) == CObject::ClassObject)
            ? Obj->Object : nullptr;
    Raw->WorldPosition = Pos;
    Raw->Orientation = Orient;

    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Raw->EngineInstanceId), 0, nullptr);

    Obj->enter_system(this, Parms);

    Ships.append(Obj);

    CObject* const ShipRaw = (Obj != nullptr) ? Obj->Object : nullptr;
    CShip* const Cs =
        (ShipRaw != nullptr && (static_cast<unsigned int>(ShipRaw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            ? static_cast<CShip*>(ShipRaw) : nullptr;

    // Filled only when the object is a CShip.
    CShipCreateInfo Info;
    if (Cs != nullptr)
        CShipCreateInfo_Fill(&Info, Cs);                            // FUN_06d07690

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        Info.bMarkedBy = Obj->is_marked_by(Player->View.PlayerId) ? 1 : 0;
        SystemPlayerView_CreateShipForPlayer(&Player->View, &Info, Obj);
    }
}

// FUN_06d0cee0. See server_sysdb.h.
void StarSystem::launch(GameObject* ShipObj, GameObject* Dock, int DockPortIndex)
{
    if (ShipObj->csimple()->OwnerPlayer != 0u)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x9f2,
              "StarSystem::launch() called on a player!");
        return;
    }

    CObject* const DockRaw = (Dock != nullptr) ? Dock->Object : nullptr;
    CEqObj* const DockEq =
        (DockRaw != nullptr && (static_cast<unsigned int>(DockRaw->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
            ? static_cast<CEqObj*>(DockRaw) : nullptr;

    // Not tested afterwards.
    CObject* const ShipRaw = (ShipObj != nullptr) ? ShipObj->Object : nullptr;
    CShip* const Cs =
        (ShipRaw != nullptr && (static_cast<unsigned int>(ShipRaw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            ? static_cast<CShip*>(ShipRaw) : nullptr;

    Cs->launch(static_cast<IObjRW*>(ShipObj), DockEq, DockPortIndex);
    static_cast< ::Ship*>(ShipObj)->apply_launch_invincibility();  // FUN_06d01110
}

// FUN_06d0df40. Callers: IServerImpl::GoTradelane and Ship go_tradelane.
// Pass 1 over the ships: the lane ship itself fires controller event 0x16 and
// use_tradelane(true), and moves on; every other unowned ship's behavior manager gets a
// type-4 "entered a lane" event (get_behavior_interface called with no test on the
// narrowing). Pass 2 sends GOTRADELANE to every player but the owner who can see the ship.
void StarSystem::notify_go_tradelane(unsigned int ShipId, unsigned int EntranceId,
                                     unsigned int ExitId, bool bFastExit, float Speed)
{
    unsigned int SkipPlayer = 0u;
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->get_id() == ShipId)
        {
            SkipPlayer = Obj->csimple()->OwnerPlayer;
            unsigned int Msg[4];
            Msg[0] = ShipId;
            Msg[1] = EntranceId;
            Msg[2] = ExitId;
            Msg[3] = SystemInfo->NicknameId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneEnter, Msg);
            CObject* const Raw = Obj->Object;
            if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                static_cast<CShip*>(Raw)->use_tradelane(true);
            continue;
        }

        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        CObject* const Raw = Obj->Object;
        CEqObj* const Ship =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // Byte +3 is never written.
        FBehaviorTradelaneEvent Payload;
        Payload.EntranceId = EntranceId;
        Payload.bFastExit = static_cast<unsigned char>(bFastExit);
        Payload.Speed = Speed;
        Payload.bExitRequested = 0u;
        Payload.bEnteringLane = 1u;
        Payload.Field_0x08 = 0u;
        Payload.ExitId = ExitId;
        Payload.ShipId = ShipId;
        FBehaviorManagerEvent LaneEvent;
        LaneEvent.Type = 4;
        LaneEvent.Data = &Payload;
        Behavior->OnEvent(&LaneEvent);
    }

    XGoTradelane Record = {};
    Record.ObjectId = ShipId;
    Record.TradelaneRing1 = EntranceId;
    Record.TradelaneRing2 = ExitId;
    Record.bMidLane = bFastExit;
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (SkipPlayer == Player->View.PlayerId)
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ShipId))
            continue;
        g_pClientInterface->SendCommonGoTradeLane(Player->View.PlayerId, Record);
    }
}

// FUN_06d0e120. Callers: IServerImpl::StopTradelane and Ship stop_tradelane. NOT the same
// shape as notify_go_tradelane: after event 0x17 and use_tradelane(false) the lane ship
// FALLS THROUGH into the second test, so every live unowned ship, the matched one
// included, gets the type-4 "left the lane" event.
void StarSystem::notify_stop_tradelane(unsigned int ShipId, unsigned int EntranceId,
                                       unsigned int ExitId)
{
    unsigned int SkipPlayer = 0u;
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->get_id() == ShipId)
        {
            SkipPlayer = Obj->csimple()->OwnerPlayer;
            unsigned int Msg[4];
            Msg[0] = ShipId;
            Msg[1] = EntranceId;
            Msg[2] = ExitId;
            Msg[3] = SystemInfo->NicknameId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneExit, Msg);
            CObject* const Raw = Obj->Object;
            if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                static_cast<CShip*>(Raw)->use_tradelane(false);
        }

        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        if (Obj->is_destroyed())
            continue;
        CObject* const Raw = Obj->Object;
        CEqObj* const Ship =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // The "left the lane" form; byte +3 is never written.
        FBehaviorTradelaneEvent Payload;
        Payload.EntranceId = EntranceId;
        Payload.bFastExit = 0u;
        Payload.Speed = 0.0f;
        Payload.ExitId = 0u;
        Payload.bEnteringLane = 0u;
        Payload.bExitRequested = 0u;
        Payload.Field_0x08 = 0u;
        Payload.ShipId = ShipId;
        FBehaviorManagerEvent LaneEvent;
        LaneEvent.Type = 4;
        LaneEvent.Data = &Payload;
        Behavior->OnEvent(&LaneEvent);
    }

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (SkipPlayer == Player->View.PlayerId)
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ShipId))
            continue;
        g_pClientInterface->SendCommonStopTradeLane(Player->View.PlayerId, ShipId, EntranceId, ExitId);
    }
}

// FUN_06d0c6d0: solars only.
void StarSystem::add_docking_solar(GameObject* Obj)
{
    if ((Obj->get_type_bits() & 0xfdffu) == 0u)
        return;
    DockingSolars.append(Obj);
}

// FUN_06d0c730: the first match only.
void StarSystem::remove_docking_solar(GameObject* Obj)
{
    MetaNode<GameObject>* Node = nullptr;
    while (DockingSolars.traverse(Node, true))
    {
        if (Node->Data == Obj)
        {
            DockingSolars.remove(Node);
            return;
        }
    }
}

// StarSystem::find_solar -- retail FUN_06d0c7e0. See server_sysdb.h.
GameObject* StarSystem::find_solar(unsigned int const* NicknameId) const
{
    GameObject* Found = nullptr;
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? Solars.Head : Node->Next;
        if (Node == nullptr || Found != nullptr)
            break;
        GameObject* const Obj = Node->Data;
        CObject* const Raw = const_cast<CObject*>(Obj->cobject());
        CSolar* Solar = nullptr;
        if (Raw != nullptr &&
            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            Solar = static_cast<CSolar*>(Raw);
        // Retail reads the nickname even off the zeroed pointer; the solar list never
        // holds anything else.
        if (Solar->Nickname.Id == *NicknameId)
            Found = Obj;
    }
    return Found;
}

// StarSystem::on_object_destroyed -- retail FUN_06d0b160. See server_sysdb.h.
void StarSystem::on_object_destroyed(GameObject* Obj, int Kind)
{
    // No null test on Obj; OwnerPlayer read through the (& 3) narrowing unguarded.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;

    if (SessionId != 0)
    {
        // Computed BEFORE the slot lookup.
        unsigned int const ObjId = Obj->get_id();

        PlayerSlot* const Slot = PlayerDB_GetPlayer(SessionId);

        // Only when the destroyed object IS the session's current ship.
        if (Slot != nullptr && Slot->ship_object_id == ObjId)
        {
            PlayerData_TerminateTrade(*Slot);
            // Retail passes true (lost as `unaff_BL` in Ghidra): the partner's inbound
            // entry is dropped too.
            PlayerData_CancelTradeRequest(*Slot, /*bDropPartnerEntry*/true);
            // Only Kind == 1 zeroes the health.
            if (Kind == 1)
                PlayerData_SetRelativeHealth(*Slot, 0.0f, /*bNotify*/false);
            Slot->ship_object_id = 0;
            unsigned int Payload[2];
            Payload[0] = Slot->system_id;
            Payload[1] = SessionId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::SpaceExit, Payload);
        }
    }

    // Unconditionally, every player in this system is told. No null test on the sentinel
    // or a node.
    {
        for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
             Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next) {
            SystemPlayerView_OnObjectDestroyed(&Node->View, Obj, Kind);
        }
    }
}

// Retail FUN_06ce6870, the (& 0x103) narrowing both cargo paths use. In retail it is a
// FREE function on the raw object, so it can take a plain GameObject*; EqObj::ceqobj() is
// the same test as a member, and this is its free twin.
static CEqObj* GameObject_ceqobj(GameObject* Obj)
{
    CObject* const Raw = (Obj != nullptr) ? Obj->cobj() : nullptr;
    if (Raw == nullptr)
        return nullptr;
    if ((static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return nullptr;
    return static_cast<CEqObj*>(Raw);
}

// StarSystem::add_cargo -- retail FUN_06d0e8d0.
unsigned short StarSystem::add_cargo(GameObject* Obj, unsigned int ArchId,
                                     unsigned int Count, float Health, bool bMission)
{
    // No null test on Obj.
    CECargo* Stack = nullptr;
    bool bCombined = false;

    // The mission flag SUPPRESSES the combine, so such an item always becomes its own entry.
    if (arch_is_combinable(ArchId) && !bMission)
    {
        // No test of the narrowing.
        CEqObj* const EqObj = GameObject_ceqobj(Obj);
        {
            CEquipTraverser Walk(EquipTypeCargo);
            CEquipManager& Mgr = EqObj->EquipManager;
            for (CEquip* Item = Mgr.Traverse(Walk); Item != nullptr;
                 Item = Mgr.Traverse(Walk))
            {
                // No test of Arch.
                if (Item->Arch->get_id() != ArchId)
                    continue;

                // Retail trusts the traverser's cargo mask and reads Count with no cast;
                // CECargo::cast cannot disagree for that mask.
                CECargo* const Cargo = CECargo::cast(Item);
                if (Cargo != nullptr && Cargo->Count != 0)
                {
                    Cargo->AddToStack(Count, Health);
                    Stack = Cargo;
                    bCombined = true;
                }
                else
                {
                    // Then fall through to the "make a new one" path below.
                    Mgr.CleanUp();
                }
                break;
            }
        }
    }

    // The owning player's slot, resolved the same way on both paths: a live client whose
    // ship is this object. OwnerPlayer read unguarded.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    PlayerSlot* Slot = (SessionId != 0) ? PlayerDB_GetPlayer(SessionId) : nullptr;
    if (Slot != nullptr && (Slot->client_id == 0 || Slot->ship_object_id != ObjId))
        Slot = nullptr;

    if (bCombined)
    {
        // FUN_06d4f230: add_equipment with the cargo-bay hardpoint baked in. The combine
        // path forces bMission and bNotify both to zero.
        if (Slot != nullptr)
        {
            PlayerData_AddEquipment(*Slot, ArchId, EquipDesc::CARGO_BAY_HP_NAME,
                                    (int)Count, Health, /*bEquipped*/false,
                                    /*bMission*/false, /*bNotify*/false);
        }
    }
    else
    {
        // Here the mission flag DOES travel on.
        unsigned short EquipId = SUBOBJ_ID_NONE;
        if (Slot != nullptr)
        {
            EquipId = PlayerData_AddEquipment(*Slot, ArchId,
                                              EquipDesc::CARGO_BAY_HP_NAME,
                                              (int)Count, Health, /*bEquipped*/false,
                                              bMission, /*bNotify*/false);
        }
        else
        {
            // No class test: retail assumes anything reaching here without a player slot
            // is a Ship (FUN_06d01c30).
            EquipId = static_cast<Ship*>(Obj)->allocate_equip_id();
        }

        // Built in retail's own order.
        EquipDesc Desc;
        Desc.set_id(EquipId);
        Desc.set_arch_id(ArchId);
        Desc.set_count((int)Count);
        Desc.set_status(Health);
        Desc.set_equipped(false);
        // A direct byte store in retail (no setter import), so the in-space path DOES put
        // AddCargo's mission flag into EquipDesc::bMission.
        Desc.bMission = bMission;
        Desc.make_internal();

        // No test of the narrowing.
        CEqObj* const EqObj = GameObject_ceqobj(Obj);
        if (EqObj->add_item(Desc))
        {
            Stack = CECargo::cast(EqObj->EquipManager.FindByID(EquipId));
        }
    }

    // The broadcast, from a SECOND EquipDesc filled by the cargo's own GetEquipDesc; with
    // no cargo or no description retail FDUMPs and sends nothing.
    EquipDesc Wire;
    bool const bHaveDesc = (Stack != nullptr) && Stack->GetEquipDesc(Wire);
    if (bHaveDesc)
    {
        // Every player that can see the object; no null tests.
        SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
        {
            for (SystemPlayerNode* Node = Sentinel->Next;
                 Node != Sentinel; Node = Node->Next) {
                if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))
                    continue;
                // The first argument goes by ADDRESS.
                g_pClientInterface->SendServerObjectCargoUpdate(Node->View.PlayerId,
                                                                Obj->get_id(), Wire);
            }
        }
    } else {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0xd8e,
              "StarSys::add_cargo().  Failed to get client cargo description.");
    }

    return (Stack != nullptr) ? Stack->Id : SUBOBJ_ID_NONE;
}

// StarSystem::remove_cargo -- retail FUN_06d0ebf0.
void StarSystem::remove_cargo(GameObject* Obj, unsigned short EquipId, int Count)
{
    // No test on the narrowing; only FindByID's result is tested.
    CEqObj* const EqObj = GameObject_ceqobj(Obj);
    CEquip* const Item = EqObj->EquipManager.FindByID(EquipId);
    if (Item == nullptr)
        return;

    // Unlike add_cargo, the remove path really casts: FindByID is not filtered by type.
    CECargo* const Cargo = CECargo::cast(Item);
    if (Cargo == nullptr)
        return;
    // An empty stack is left alone entirely.
    if (Cargo->Count == 0)
        return;

    // Filled from the cargo first...
    EquipDesc Wire;
    Cargo->GetEquipDesc(Wire);

    // Ghidra passes param_2 here; the raw bytes load the THIRD argument.
    Cargo->RemoveFromStack((unsigned)Count);

    // ...then patched with what is left.
    Wire.set_count((int)Cargo->Count);
    Wire.set_status((float)Cargo->GetRelativeHealth());

    // OwnerPlayer read unguarded; the player's copy loses the items without the notify.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    if (SessionId != 0u && Players.is_valid_ship_owner(SessionId, ObjId))
        PlayerData_RemoveEquipment(Players[SessionId], EquipId, Count, /*bNotify*/false);

    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next) {
        if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        // The player id goes by ADDRESS.
        g_pClientInterface->SendServerObjectCargoUpdate(Node->View.PlayerId, Obj->get_id(), Wire);
    }
}

// ===========================================================================
// StarSystem::unload -- retail FUN_06d072c0. Single player only, after event 0x3d, when the
// last player leaves a system. See the declaration for the order.
// ===========================================================================

// Retail's per-object disposal: the scalar deleting destructor (flag 1) of the wrapper's
// CBase sub-object at +0x0c, which destroys and frees the whole wrapper; null Data skipped.
static void StarSystem_DeleteObject(GameObject* Obj)
{
    delete static_cast<CBase*>(Obj);
}

// The two passes retail runs over each list: pass 1 deletes every non-null Data (without
// clearing it), pass 2 frees the nodes and zeroes Head/Tail.
static void StarSystem_DeleteBlock(ObjectList& List)
{
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? List.Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data != nullptr)
            StarSystem_DeleteObject(Node->Data);
    }

    Node = List.Head;
    while (Node != nullptr) {
        FSystemListNode* const Current = Node;
        Node = Node->Next;
        List.free_node(Current);
    }
    List.Tail = nullptr;
    List.Head = nullptr;
}

void StarSystem::unload()
{
    // FUN_06d0f390: erase the whole player list (each view destroyed in place, flag 0).
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    SystemPlayerNode* View = Sentinel->Next;
    while (View != Sentinel) {
        SystemPlayerNode* const Node = View;
        View = View->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        Observer* const Element = &Node->View;
        Element->~Observer();
        ::operator delete(Node);
        --PlayerListSize;
    }

    // Six lists in retail's order.
    StarSystem_DeleteBlock(Ships);           // +0x34
    StarSystem_DeleteBlock(Loot);            // +0x48
    StarSystem_DeleteBlock(GuidedMunitions); // +0x70
    StarSystem_DeleteBlock(Beams);           // +0x84
    StarSystem_DeleteBlock(Mines);           // +0x98
    StarSystem_DeleteBlock(CounterMeasures); // +0xac

    // +0xe8: a forward traverse deleting every non-null Data, then MetaList::free.
    {
        FSystemListNode* Node = nullptr;
        while (Deferred.traverse(Node, true)) {
            if (Node->Data != nullptr)
                StarSystem_DeleteObject(Node->Data);
        }
        Deferred.free();
    }

    // FUN_06d07570: the solars, then the partition.
    StarSystem_DeleteBlock(Solars);
    if (Spatial != nullptr) {
        FreeSpatial(Spatial);
        Spatial = nullptr;
    }

    // FUN_06d075f0: drop one reference on the asteroid system (result dropped), then the
    // asteroid objects.
    unsigned int const SystemId = SystemInfo->NicknameId;
    SrvAsteroid::SrvAsteroid_UnloadSystem(SystemId);
    CmnAsteroid::Unload(SystemId);
    StarSystem_DeleteBlock(MedAsteroids);

    // FUN_06d0f2f0: clear the 0xC-byte POD vector.
    VectorEnd = VectorBegin;

    PhySys::Shutdown(SystemInfo->NicknameId);
}

// ===========================================================================
// StarSystem's constructors, destructor and SysDB init.
// ===========================================================================

// FUN_06d0f3e0(0, 0): the player list's self-linked sentinel. MSVC6's _Buynode allocates a
// full node (0x50 in retail; sizeof here, since the port's Observer embeds a CRT-sized
// std::set).
static void* StarSystem_NewPlayerSentinel()
{
    SystemPlayerNode* const Node =
        static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
    Node->Next = Node;
    Node->Prev = Node;
    return Node;
}

// The dock queue's self-linked 0x20-byte sentinel (also FUN_06d10aa0 in the copy).
static void* StarSystem_NewDockSentinel()
{
    SystemDockNode* const Node =
        static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
    Node->Next = Node;
    Node->Prev = Node;
    return Node;
}

// FUN_06d06ce0. The ten ObjectLists get bUseHeap = 1. Retail's allocator bytes are copies
// of an uninitialised stack byte and +0x04 is not written at all (FUN_06d06ea0 sets it on
// the map's copy first); the port writes zero instead of copying indeterminate memory.
StarSystem::StarSystem()
    : SystemInfo(nullptr),
      ListAlloc(0),
      PlayerListHead(StarSystem_NewPlayerSentinel()),
      PlayerListSize(0),
      DockQueueAlloc(0),
      DockQueueHead(StarSystem_NewDockSentinel()),
      DockQueueSize(0),
      Spatial(nullptr),
      VectorAlloc(0),
      VectorBegin(nullptr),
      VectorEnd(nullptr),
      VectorCapacity(nullptr),
      bLoaded(0)
{
    Pad1[0] = Pad1[1] = Pad1[2] = 0;
    Pad2[0] = Pad2[1] = Pad2[2] = 0;
    Pad3[0] = Pad3[1] = Pad3[2] = 0;
    Pad4[0] = Pad4[1] = Pad4[2] = 0;
}

// FUN_06d10520; FUN_06d11330 inlines the same body for the map node. Both player views and
// dock entries are copied; the partition POINTER is shared; the vector is copied with a
// block allocated even for 0 elements; the ten ObjectLists come out EMPTY with bUseHeap = 0
// (nothing of Other's is copied).
StarSystem::StarSystem(StarSystem const& Other)
    : IObjDB(),
      SystemInfo(Other.SystemInfo),
      ListAlloc(Other.ListAlloc),
      PlayerListHead(StarSystem_NewPlayerSentinel()),
      PlayerListSize(0),
      DockQueueAlloc(Other.DockQueueAlloc),
      DockQueueHead(StarSystem_NewDockSentinel()),
      DockQueueSize(0),
      Spatial(Other.Spatial),
      VectorAlloc(Other.VectorAlloc),
      VectorBegin(nullptr),
      VectorEnd(nullptr),
      VectorCapacity(nullptr),
      Ships(Other.Ships),
      Loot(Other.Loot),
      Solars(Other.Solars),
      GuidedMunitions(Other.GuidedMunitions),
      Beams(Other.Beams),
      Mines(Other.Mines),
      CounterMeasures(Other.CounterMeasures),
      MedAsteroids(Other.MedAsteroids),
      DockingSolars(Other.DockingSolars),
      Deferred(Other.Deferred),
      bLoaded(Other.bLoaded)
{
    Pad1[0] = Pad1[1] = Pad1[2] = 0;
    Pad2[0] = Pad2[1] = Pad2[2] = 0;
    Pad3[0] = Pad3[1] = Pad3[2] = 0;
    Pad4[0] = Pad4[1] = Pad4[2] = 0;

    // FUN_06d109c0: push_back of a copy of every player view.
    {
        SystemPlayerNode* const End = static_cast<SystemPlayerNode*>(PlayerListHead);
        SystemPlayerNode* const SrcEnd = static_cast<SystemPlayerNode*>(Other.PlayerListHead);
        for (SystemPlayerNode* Src = SrcEnd->Next; Src != SrcEnd; Src = Src->Next) {
            SystemPlayerNode* const Node =
                static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
            Node->Next = End;
            Node->Prev = (End->Prev != nullptr) ? End->Prev : Node;
            End->Prev = Node;
            Node->Prev->Next = Node;
            ::new (static_cast<void*>(&Node->View)) Observer(Src->View);   // FUN_06d0f6d0
            ++PlayerListSize;
        }
    }

    // FUN_06d10a30: the same for the dock queue (FUN_06d0f830 copies each entry).
    {
        SystemDockNode* const End = static_cast<SystemDockNode*>(DockQueueHead);
        SystemDockNode* const SrcEnd = static_cast<SystemDockNode*>(Other.DockQueueHead);
        for (SystemDockNode* Src = SrcEnd->Next; Src != SrcEnd; Src = Src->Next) {
            SystemDockNode* const Node =
                static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
            Node->Next = End;
            Node->Prev = (End->Prev != nullptr) ? End->Prev : Node;
            End->Prev = Node;
            Node->Prev->Next = Node;
            ::new (static_cast<void*>(&Node->Entry)) FSystemDockEntry(Src->Entry);
            ++DockQueueSize;
        }
    }

    // The 0xC-byte-element vector.
    int Count = 0;
    if (Other.VectorBegin != nullptr) {
        Count = static_cast<int>(
            (static_cast<char const*>(Other.VectorEnd) -
             static_cast<char const*>(Other.VectorBegin)) / 0xC);
    }
    if (Count < 0)
        Count = 0;
    VectorBegin = ::operator new(static_cast<size_t>(Count) * 0xC);
    size_t const Bytes = static_cast<size_t>(
        static_cast<char const*>(Other.VectorEnd) - static_cast<char const*>(Other.VectorBegin));
    if (Bytes != 0)
        memcpy(VectorBegin, Other.VectorBegin, Bytes);
    VectorEnd = static_cast<char*>(VectorBegin) + Bytes;
    VectorCapacity = VectorEnd;
}

// FUN_06d10030 (the map pair's destructor inlines it). The ObjectLists' own destructors run
// afterwards in member order. Nothing deletes the objects the lists point at, and the
// partition is not freed here: unload() does both.
StarSystem::~StarSystem()
{
    ::operator delete(VectorBegin);
    VectorBegin = nullptr;
    VectorEnd = nullptr;
    VectorCapacity = nullptr;

    SystemDockNode* const DockHead = static_cast<SystemDockNode*>(DockQueueHead);
    erase_docks(DockHead->Next, DockHead);
    ::operator delete(DockQueueHead);
    DockQueueHead = nullptr;
    DockQueueSize = 0;

    // FUN_06d0f230.
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    SystemPlayerNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemPlayerNode* const Current = Node;
        Node = Node->Next;
        Current->Prev->Next = Current->Next;
        Current->Next->Prev = Current->Prev;
        Observer* const Element = &Current->View;
        Element->~Observer();
        ::operator delete(Current);
        --PlayerListSize;
    }
    ::operator delete(PlayerListHead);
    PlayerListHead = nullptr;
    PlayerListSize = 0;
    // The ten ObjectLists are destroyed after this body; retail runs them first. The
    // order is not observable: a list's destructor frees only its own nodes.
}

// LAB_06d06c30 -- the multiplayer collision filter StarSystem::init installs into
// PhySys::m_pCollisionFilter. Collides when either side's client object is (& 0x27) (a
// projectile type). Retail calls GetRoot on both and DISCARDS the results.
static IVP_BOOL __cdecl StarSystem_MpCollisionFilter(IVP_Real_Object* A, IVP_Real_Object* B)
{
    if (SinglePlayer())
        return IVP_TRUE;
    if (A == nullptr)
        return IVP_TRUE;
    if (B == nullptr)
        return IVP_TRUE;

    CObject* ClientA = static_cast<CObject*>(A->client_data);
    if (ClientA == nullptr && A->controller_phantom != nullptr)
        return IVP_TRUE;
    if (B->client_data == nullptr && B->controller_phantom != nullptr)
        return IVP_TRUE;

    if (ClientA != nullptr)
        GetRoot(ClientA);
    CObject* ClientB = static_cast<CObject*>(B->client_data);
    if (ClientB != nullptr)
        GetRoot(ClientB);

    if (ClientA != nullptr && (static_cast<unsigned int>(ClientA->ObjectClass) & 0x27u) != 0x27u)
        ClientA = nullptr;
    if (ClientB != nullptr && (static_cast<unsigned int>(ClientB->ObjectClass) & 0x27u) != 0x27u)
        ClientB = nullptr;

    if (ClientA != nullptr || ClientB != nullptr)
        return IVP_TRUE;
    return IVP_FALSE;
}

// FUN_06d06ea0.
void StarSystem::init(unsigned int SystemId)
{
    SystemInfo = const_cast<Universe::ISystem*>(Universe::get_system(SystemId));
    if (!SinglePlayer()) {
        PhySys::m_pCollisionFilter = &StarSystem_MpCollisionFilter;
    }
}

// ===========================================================================
// StarSystem as IObjDB -- the four slots of .rdata:06d67cd0.
// ===========================================================================

// The walk state Common hands to slot 0 ({0, 0, mask} at the start): the node last
// returned, its list, and the type mask, which slot 0 re-reads before every list.
struct FStarSystemObjDbCursor {
    FSystemListNode* Node;             // +0x00
    ObjectList* List;                  // +0x04
    unsigned int Mask;                 // +0x08
};

// FUN_06d0dd50 (cdecl). A walk already running in ANOTHER list returns null at once,
// cursor untouched. With bFilter, a non-zero get_type result or no common bit skips the
// node (retail uses the bFilter stack slot as get_type's out parameter). A null Data ends
// this list's walk like running off its end.
static GameObject* StarSystem_ObjDbStep(unsigned int Mask, ObjectList* List,
                                        FSystemListNode** CursorNode,
                                        ObjectList** CursorList, bool bFilter)
{
    if (*CursorList != nullptr && *CursorList != List)
        return nullptr;

    FSystemListNode* Node = *CursorNode;
    GameObject* Data = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? List->Head : Node->Next;
        if (Node == nullptr) {
            Data = nullptr;
            break;
        }
        Data = Node->Data;
        if (bFilter) {
            unsigned int Bits = 0;
            if (Data->get_type(Bits) != 0)
                continue;
            if ((Bits & Mask) == 0)
                continue;
        }
        if (Data != nullptr) {
            *CursorNode = Node;
            *CursorList = List;
            return Data;
        }
        break;
    }
    *CursorNode = nullptr;
    *CursorList = nullptr;
    return Data;
}

// Slot 0 -- FUN_06d0dde0. Each list is tried when its mask test passes, in this order; the
// first object found wins. Ships and solars are filtered unless the mask covers their
// whole group; the rest are unfiltered.
IObjRW* __thiscall StarSystem::GetNext(void* IterState)
{
    FStarSystemObjDbCursor* const Cursor = static_cast<FStarSystemObjDbCursor*>(IterState);
    GameObject* Found = nullptr;

    unsigned int Mask = Cursor->Mask;
    if ((Mask & 0x7f0000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Ships, &Cursor->Node, &Cursor->List,
                                     (Mask & 0x7f0000u) != 0x7f0000u);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0xfdffu) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Solars, &Cursor->Node, &Cursor->List,
                                     (Mask & 0xfdffu) != 0xfdffu);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x10000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Loot, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x1000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &GuidedMunitions, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x2000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Beams, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x4000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Mines, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x8000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &CounterMeasures, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x20000000u) != 0)
        Found = StarSystem_ObjDbStep(Mask, &MedAsteroids, &Cursor->Node, &Cursor->List, false);
    return Found;
}

// Slot 1 -- LAB_06d0dd40, a tail jump into find_object.
IObjRW* __thiscall StarSystem::FindById(unsigned const* ObjectId)
{
    return find_object(*ObjectId);
}

// Slot 2 -- .text:06ce10f0 (ICF-shared with CPlayerGroup::GetID).
Universe::ISystem* __thiscall StarSystem::GetZoneDb()
{
    return SystemInfo;
}

// Slot 3 -- LAB_06d06e90. The mask is ignored.
void* __thiscall StarSystem::GetBucket(unsigned TypeMask)
{
    (void)TypeMask;
    return Spatial;
}

// FUN_06d0c840: the lists in retail's order, first hit wins. Beams (+0x84) is not searched.
GameObject* StarSystem::find_object(unsigned int Id)
{
    if ((Id & 0x3fffffffu) == 0)
        return nullptr;
    FSystemListNode* Node = Ships.find_by_id(Id);
    if (Node == nullptr)
        Node = Loot.find_by_id(Id);
    if (Node == nullptr)
        Node = GuidedMunitions.find_by_id(Id);
    if (Node == nullptr)
        Node = Mines.find_by_id(Id);
    if (Node == nullptr)
        Node = CounterMeasures.find_by_id(Id);
    if (Node == nullptr)
        Node = MedAsteroids.find_by_id(Id);
    if (Node == nullptr)
        Node = Solars.find_by_id(Id);
    return (Node != nullptr) ? Node->Data : nullptr;
}

// FUN_06d0c8d0: a mask of -1 searches everything; otherwise the FIRST matching test picks
// one list.
GameObject* StarSystem::find_object(unsigned int Id, unsigned int TypeMask)
{
    if ((Id & 0x3fffffffu) == 0)
        return nullptr;
    if (TypeMask == 0xffffffffu)
        return find_object(Id);

    ObjectList* List = nullptr;
    if ((TypeMask & 0x7f0000u) != 0)
        List = &Ships;
    else if ((TypeMask & 0x10000000u) != 0)
        List = &Loot;
    else if ((TypeMask & 0xfdffu) != 0)
        List = &Solars;
    else if ((TypeMask & 0x4000000u) != 0)
        List = &Mines;
    else if ((TypeMask & 0x1000000u) != 0)
        List = &GuidedMunitions;
    else if ((TypeMask & 0x8000000u) != 0)
        List = &CounterMeasures;
    else if ((TypeMask & 0x60000000u) != 0)
        List = &MedAsteroids;
    else
        return nullptr;

    FSystemListNode* const Node = List->find_by_id(Id);
    return (Node != nullptr) ? Node->Data : nullptr;
}


// ===========================================================================
// The dock queue at StarSystem+0x14 (server_sysdb.h, SystemDockNode).
// ===========================================================================

// Retail's `[watcher] - 8` back to the wrapper, with its null test. The Watchable is a
// non-virtual base of IObjRW, so the downcast is a static one.
GameObject* FSystemDockEntry::WatchedObject(BaseWatcher const& Watcher)
{
    Watchable const* const Target = Watcher.target();
    if (Target == nullptr)
        return nullptr;
    return static_cast<GameObject*>(static_cast<IObjRW*>(const_cast<Watchable*>(Target)));
}

// FUN_06d0d370. See server_sysdb.h.
void StarSystem::queue_dock(GameObject* Object, GameObject* Target,
                            unsigned int const* BaseId, bool const* bLand)
{
    // BaseWatcher::set is retail's compare-then-set_pointer.
    FSystemDockEntry Element;
    Element.Object.set(Object != nullptr ? static_cast<Watchable const*>(Object) : nullptr);
    Element.Target.set(Target != nullptr ? static_cast<Watchable const*>(Target) : nullptr);
    Element.BaseId = *BaseId;
    Element.bLand = *bLand;

    // A push_back, with MSVC6 _Buynode's null-Prev fallback.
    SystemDockNode* const Sentinel = static_cast<SystemDockNode*>(DockQueueHead);
    SystemDockNode* const Node =
        static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
    Node->Next = Sentinel;
    Node->Prev = Sentinel->Prev != nullptr ? Sentinel->Prev : Node;
    Sentinel->Prev = Node;
    Node->Prev->Next = Node;

    new (&Node->Entry) FSystemDockEntry(Element);                   // FUN_06d0f830

    ++DockQueueSize;
}

// FUN_06d0f410. See server_sysdb.h.
SystemDockNode* StarSystem::erase_docks(SystemDockNode* First, SystemDockNode* Last)
{
    while (First != Last) {
        SystemDockNode* const Node = First;
        First = First->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        Node->Entry.~FSystemDockEntry();             // FUN_06d0d4a0
        ::operator delete(Node);
        --DockQueueSize;
    }
    return First;
}

// FUN_06d07130. See server_sysdb.h.
void StarSystem::shutdown()
{
    if (bLoaded == 0)
        return;

    // Cleared before the event goes out.
    bLoaded = 0;

    // No null test on SystemInfo.
    unsigned int SystemId = SystemInfo->NicknameId;
    Server_ControllerDispatch(0u, pub::Controller::EMessage::SystemUnloaded, &SystemId);

    unload();
}

// FUN_06d005d0 (cdecl). See server_sysdb.h. Forgets the lookup cache when it holds ObjId
// (the cached object's CSimple id read unguarded).
void Server_LookupCacheForget(unsigned int const& ObjId)
{
    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched == nullptr)
        return;
    GameObject const* const Obj =
        static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched));
    if (Obj->csimple()->Id != ObjId)
        return;
    GLookupCacheObject.set(nullptr);
    GLookupCacheSystem = nullptr;
}

// FUN_06d08940. See server_sysdb.h.
bool StarSystem::remove_solar(GameObject* Obj)
{
    unsigned int const ObjId = Obj->get_id();
    Server_LookupCacheForget(ObjId);

    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Solars.Head : Node->Next;
        if (Node == nullptr)
            return false;
        if (Node->Data == Obj)
            break;
    }

    Solars.remove(Node);

    // Out of the BSP the load pass bulk-added it to.
    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket != nullptr)
    {
        static_cast<ISpatialPartition*>(Bucket)->Remove(static_cast<IObjRW*>(Obj));
    }

    // Onto the Deferred list (+0xe8).
    Deferred.append(Obj);
    return true;
}

// ---------------------------------------------------------------------------
// The objects between systems and the object lookup by id. See server_gameobject.h.
// ---------------------------------------------------------------------------

// Retail's CRT init LAB_06d002e0 / atexit FUN_06d00320 pair is this global std::list's
// constructor and destructor.
std::list<FInTransitObject> GInTransitObjects;

// FUN_06d00380 (cdecl). The cache's system half is left alone.
bool Server_TakeInTransit(GameObject* Obj, PhySys::CreateParms& OutParms)
{
    std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
    while (It != GInTransitObjects.end() && It->Obj != Obj)
        ++It;
    if (It == GInTransitObjects.end())
        return false;

    OutParms = It->Parms;
    GInTransitObjects.erase(It);

    // set(nullptr) is set_pointer(0): something is watched on this path.
    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched != nullptr &&
        static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched)) == Obj)
        GLookupCacheObject.set(nullptr);
    return true;
}

// FUN_06d00410 (cdecl); only caller pub::SpaceObj::Destroy. Controller message 0x39 with
// every field 0 but the id, then the scalar deleting destructor.
bool Server_DestroyInTransit(GameObject* Obj)
{
    std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
    while (It != GInTransitObjects.end() && It->Obj != Obj)
        ++It;
    if (It == GInTransitObjects.end())
        return false;

    GInTransitObjects.erase(It);

    Watchable const* const Watched = GLookupCacheObject.target();
    GameObject const* const Cached = (Watched != nullptr)
        ? static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched))
        : nullptr;
    if (Cached == Obj)
        GLookupCacheObject.set(nullptr);

    FObjectDestroyedEvent Event;
    Event.KillerId = 0;
    Event.SystemId = 0;
    Event.OwnerPlayer = 0;
    Event.Kind = 0;
    Event.Position.x = 0.0f;
    Event.Position.y = 0.0f;
    Event.Position.z = 0.0f;
    Event.ObjId = Obj->get_id();
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDestroyed, &Event);

    delete Obj;
    return true;
}

// FUN_06d00560 (cdecl); only caller the system-transfer path when the target system is null.
void Server_PushInTransit(GameObject* Obj, PhySys::CreateParms const& Parms)
{
    FInTransitObject Entry;
    Entry.Parms = Parms;
    Entry.Obj = Obj;
    GInTransitObjects.push_back(Entry);
}

// FUN_06d006c0 (cdecl). The cache first (CSimple id read with no null test), then the
// cached system, then the in-transit list (no break: the LAST match stays, with OutSys 0),
// then every other system in SysMap order. On a miss OutSys keeps what the cached-system
// step wrote. A hit refreshes the cache.
bool Server_FindObject(unsigned int const& Id, GameObject*& OutObj, StarSystem*& OutSys)
{
    OutObj = nullptr;
    OutSys = nullptr;

    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched != nullptr)
    {
        GameObject* const Cached =
            static_cast<GameObject*>(static_cast<IObjRW*>(const_cast<Watchable*>(Watched)));
        if (Cached->csimple()->Id == Id)
        {
            ++GLookupCacheHits;
            OutObj = Cached;
            OutSys = GLookupCacheSystem;
            return true;
        }
    }
    ++GLookupCacheMisses;

    if (OutObj == nullptr)
    {
        if (GLookupCacheSystem != nullptr)
        {
            OutObj = GLookupCacheSystem->find_object(Id);
            OutSys = GLookupCacheSystem;
        }
        if (OutObj == nullptr)
        {
            for (std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
                 It != GInTransitObjects.end(); ++It)
            {
                if (It->Obj->get_id() == Id)
                {
                    OutObj = It->Obj;
                    OutSys = nullptr;
                }
            }
            if (OutObj == nullptr)
            {
                for (SysDB::SysMapType::iterator SysIt = SysDB::SysMap.begin();
                     SysIt != SysDB::SysMap.end(); ++SysIt)
                {
                    if (&SysIt->second == GLookupCacheSystem)
                        continue;
                    OutObj = SysIt->second.find_object(Id);
                    if (OutObj != nullptr)
                    {
                        OutSys = &SysIt->second;
                        break;
                    }
                }
                if (OutObj == nullptr)
                    return false;
            }
        }
    }

    GLookupCacheObject.set(static_cast<Watchable const*>(OutObj));
    GLookupCacheSystem = OutSys;
    return true;
}

// FUN_06d00630 (cdecl). A destroyed object counts as missing; OutSys is left as written.
bool Server_FindLiveObject(unsigned int Id, GameObject*& OutObj, StarSystem*& OutSys)
{
    if (Server_FindObject(Id, OutObj, OutSys) && !OutObj->is_destroyed())
        return true;
    OutObj = nullptr;
    return false;
}

// DAT_06d8d924 -- ServerSpace.cpp's own lazily fetched math engine; never null-tested.
static I3DMathEngine* GServerSpaceMathEngine = nullptr;

// Server_GetLaunchPosition -- retail FUN_06d00830 (cdecl).
// - Leaving a base: the first dockable solar of that base. A docking ring (type 0x20) is
//   kind 1; otherwise controller event 0x32 asks for a free terminal through the
//   GReportedFreeTerminal mailbox (-1 read back as 0). No ring at all FDUMPs and places
//   the ship 2.2 radii out from the base, facing away from the origin.
// - Otherwise the starting_ring solar, kind from its type bit 0x20; or, with no ring, the
//   slot's initial position and orientation.
// No object or system pointer is tested where retail does not test it.
void Server_GetLaunchPosition(unsigned int PlayerId, unsigned int* OutObjId, int* OutKind,
                              Vector* OutPos, Quaternion* OutOrient)
{
    *OutObjId = 0u;
    *OutKind = -1;

    if ((Players[PlayerId].exited_base_id & 0x3fffffffu) != 0u)
    {
        std::list<GameObject*> Rings;
        StarSystem* const System = SysDB_LookupSystem(Players[PlayerId].system_id);
        System->collect_dockable_solars(Rings, Players[PlayerId].exited_base_id);

        if (Rings.size() == 0u)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: No docking rings for base %X",
                  "E:\\FL\\Scratch\\Source\\Server\\space\\ServerSpace.cpp", 0x1a3,
                  Players[PlayerId].exited_base_id);

            GameObject* const Base = System->find_object(Players[PlayerId].exited_base_id);
            CObject* const BaseRaw = (Base != nullptr) ? Base->Object : nullptr;
            CSimple* const BaseSimple =
                (BaseRaw != nullptr && (static_cast<unsigned int>(BaseRaw->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
                    ? static_cast<CSimple*>(BaseRaw) : nullptr;
            Vector const& BasePos = BaseSimple->WorldPosition;

            // x87: each component stored as a float.
            Vector Dir;
            Dir.x = -BasePos.x;
            Dir.y = -BasePos.y;
            Dir.z = -BasePos.z;
            double const Len = std::sqrt(static_cast<double>(Dir.x) * Dir.x +
                                         static_cast<double>(Dir.y) * Dir.y +
                                         static_cast<double>(Dir.z) * Dir.z);
            double const Inv = 1.0 / Len;
            Dir.x = static_cast<float>(Dir.x * Inv);
            Dir.y = static_cast<float>(Dir.y * Inv);
            Dir.z = static_cast<float>(Dir.z * Inv);

            Vector Center;
            float const Radius = BaseSimple->get_physical_radius_r(Center);

            // Two FUN_06cf4a10 scalings, each stored as a float.
            Vector Scaled;
            Scaled.x = static_cast<float>(static_cast<double>(Radius) * Dir.x);
            Scaled.y = static_cast<float>(static_cast<double>(Radius) * Dir.y);
            Scaled.z = static_cast<float>(static_cast<double>(Radius) * Dir.z);
            Vector Off;
            Off.x = static_cast<float>(static_cast<double>(2.2f) * Scaled.x);
            Off.y = static_cast<float>(static_cast<double>(2.2f) * Scaled.y);
            Off.z = static_cast<float>(static_cast<double>(2.2f) * Scaled.z);

            OutPos->x = static_cast<float>(static_cast<double>(BasePos.x) + Off.x);
            OutPos->y = static_cast<float>(static_cast<double>(BasePos.y) + Off.y);
            OutPos->z = static_cast<float>(static_cast<double>(BasePos.z) + Off.z);

            Matrix const Look = LookMatrix(Dir);
            if (GServerSpaceMathEngine == nullptr)
                GServerSpaceMathEngine = Server_Acquire3DMathEngine();
            Quaternion Orient;
            GServerSpaceMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orient),
                                                       reinterpret_cast<FMatrix const&>(Look));
            *OutOrient = Orient;
        }
        else
        {
            GameObject* const Ring = Rings.front();
            *OutObjId = Ring->get_id();
            if ((Ring->get_type_bits() & 0x20u) != 0u)
            {
                *OutKind = 1;
            }
            else
            {
                GReportedFreeTerminal = -1;
                unsigned int Msg[2];
                Msg[1] = *OutObjId;
                Msg[0] = PlayerId;
                Server_ControllerDispatch(0, pub::Controller::EMessage::FreeTerminalRequest, Msg);
                int const Reported = GReportedFreeTerminal;
                *OutKind = (Reported == -1) ? 0 : Reported;
            }

            CObject* const RingRaw = Ring->Object;
            CEqObj* const RingEq =
                (RingRaw != nullptr && (static_cast<unsigned int>(RingRaw->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
                    ? static_cast<CEqObj*>(RingRaw) : nullptr;
            Matrix LaunchOrient;
            RingEq->launch_pos(*OutPos, LaunchOrient, *OutKind);

            if (GServerSpaceMathEngine == nullptr)
                GServerSpaceMathEngine = Server_Acquire3DMathEngine();
            Quaternion Orient;
            GServerSpaceMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orient),
                                                       reinterpret_cast<FMatrix const&>(LaunchOrient));
            *OutOrient = Orient;
        }

        Players[PlayerId].exited_base_id = 0u;
        return;                                  // the list's erase + head free (~list)
    }

    // +0x414 is an ID_String in retail (ID_String::IsEmpty is called on it); the
    // port's PlayerData declares it as its one dword.
    if (!reinterpret_cast<ID_String const&>(Players[PlayerId].starting_ring).IsEmpty())
    {
        StarSystem* const System = SysDB_LookupSystem(Players[PlayerId].system_id);
        GameObject* const Found = System->find_solar(&Players[PlayerId].starting_ring);
        CObject* const FoundRaw = (Found != nullptr) ? Found->Object : nullptr;
        CSolar* const Solar =
            (FoundRaw != nullptr && (static_cast<unsigned int>(FoundRaw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                ? static_cast<CSolar*>(FoundRaw) : nullptr;
        *OutObjId = Solar->Id;
        int const Kind = static_cast<int>(
            (reinterpret_cast<unsigned char const*>(&Solar->TypeBits)[0] >> 5) & 1u);
        *OutKind = Kind;
        Matrix LaunchOrient;
        Solar->launch_pos(*OutPos, LaunchOrient, Kind);

        if (GServerSpaceMathEngine == nullptr)
            GServerSpaceMathEngine = Server_Acquire3DMathEngine();
        Quaternion Orient;
        GServerSpaceMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orient),
                                                   reinterpret_cast<FMatrix const&>(LaunchOrient));
        *OutOrient = Orient;
        return;
    }

    PlayerData& Slot = Players[PlayerId];
    OutPos->x = Slot.initial_pos[0];
    OutPos->y = Slot.initial_pos[1];
    OutPos->z = Slot.initial_pos[2];
    if (GServerSpaceMathEngine == nullptr)
        GServerSpaceMathEngine = Server_Acquire3DMathEngine();
    Quaternion Orient;
    GServerSpaceMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orient),
                                               reinterpret_cast<FMatrix const&>(Slot.initial_ornt));
    *OutOrient = Orient;
}
