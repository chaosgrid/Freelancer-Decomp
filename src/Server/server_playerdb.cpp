// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_net.h"
#include "../include/common_physys.h"
#include "../include/common_id.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_savefile.h" // Server_AccountDirName / Server_GetAccountsRoot (FUN_06d49f60)
#include "../include/common_ini.h"
#include "../include/common_misc.h"
#include "../include/common_universe.h"
#include "../include/common_reputation.h"
#include "../include/server_sysdb.h"
#include "../include/server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "../include/DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "../include/readfile.h"       // IFileSystem::GetFileSysPath (+0x84), create_restart_file
#include "../include/server_characterset.h" // CCharacterSet (FUN_06d499c0)
#include "../include/server_basedb.h"
#include "../include/rpc_iclient.h"
#include "server_internal.h"
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
#include <new>          // placement new: Observer is constructed into a raw
                        // player-list node, as std::list's _Construct does
#include <io.h>
#include <direct.h>
#include <wchar.h>
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/server_cobject.h"
#include "../include/common_server_controller.h"
#include <cstddef>

// The IClient / IChatClient halves of rpclocal's Client object (.data:DAT_06d8fb14 /
// DAT_06d8fb18), defined in IServerImpl.cpp.
extern IClient* g_pClientInterface;

namespace GlobalReputation = ::Reputation;

// ===========================================================================
// The save-file writer family: retail FUN_06d460f0, FUN_06d46370, FUN_06d46930
// and FUN_06d4ccd0. Key names, format strings and source fields are taken from
// the listing at the address in each comment.
// ===========================================================================

// Server_GetAccountsRoot -- retail FUN_06d460f0. Returns one static 0x104 buffer
// (DAT_06d90158), so a caller can pass the result as _makepath's `dir` and reuse its
// own locals. Declared in server_savefile.h (also used by server_savegame.cpp).
static char GAccountsRoot[260];

// GCheatMessage / LoadCheatMessage -- retail .data:06d8d160 and FUN_06d46150, the last
// thing PlayerDB::init does. MP only: <exe dir>\CheatMessage.txt is read as raw UTF-16
// (at most 0x50 wchar) over the default L"Possible cheating detected: ". The consumer is
// PlayerData::report_cheat below.
static wchar_t GCheatMessage[0x50] = L"Possible cheating detected: ";

// Retail DAT_06d902f8, see server_savefile.h.
char GServerLastSavePath[0x104];

char const* Server_GetAccountsRoot() {
    char UserDataPath[260];
    GetUserDataPath(UserDataPath);
    _makepath(GAccountsRoot, nullptr, UserDataPath,
              IsMPServer() ? "Accts\\MultiPlayer" : "Accts", nullptr);
    return GAccountsRoot;
}

// Server_AccountDirName -- retail FUN_06d46370, the MP account-directory name
// "%02x-%08x" (wcslen(name), crc).
//
// The CRC is the Rocksoft-model engine Common.dll's CFLIDMaker uses, but a separate
// instance in Server.dll configured by LAB_06d46250 (DAT_06d90138..DAT_06d9014d):
// register width 32, crc width 31, poly 0x8005, XorIn = XorOut = 0, reflect-in and
// table path both on. The finalize (.text:06d4645a) differs from CreateID and must not
// be copied from it: no `>> (32 - width)` fold-back and no 0x80000000 top bit, so the
// account hash is byteswap32(crc & 0x7fffffff). The input is the lowercased wide name,
// 2 * wcslen bytes.

namespace {

    const unsigned int AccountCrcWidth = 31;

    unsigned int GAccountCrcTable[256];

    // The bit-overlay reflect CFLIDMaker uses; Server.dll has its own copy.
    unsigned int AccountCrcReflect(unsigned int Value, unsigned int BitCount) {
        unsigned int Result = Value;
        unsigned int Source = Value;
        for (unsigned int BitIndex = 0; BitIndex < BitCount; ++BitIndex) {
            unsigned int Mask = 1u << ((BitCount - BitIndex) - 1u);
            if ((Source & 1u) == 0)
                Result &= ~Mask;
            else
                Result |= Mask;
            Source >>= 1;
        }
        return Result;
    }

    const unsigned int AccountCrcPoly = 0x8005;

    bool bAccountCrcTableReady = false;

    void AccountCrcBuildTable() {
        if (bAccountCrcTableReady)
            return;
        unsigned int const TopBit = 1u << (AccountCrcWidth - 1u);
        unsigned int const Mask = ((TopBit - 1u) * 2u + 1u) | 1u;
        for (unsigned int Index = 0; Index < 256; ++Index) {
            // Reflect-in reflects the table index byte first.
            unsigned int Work = AccountCrcReflect(Index, 8) & 0xffu;
            Work <<= (AccountCrcWidth - 8u);
            for (int Bit = 0; Bit < 8; ++Bit) {
                if ((Work & TopBit) != 0)
                    Work = (Work << 1) ^ AccountCrcPoly;
                else
                    Work <<= 1;
            }
            // Reflect-out folds the entry back.
            Work = AccountCrcReflect(Work, AccountCrcWidth);
            GAccountCrcTable[Index] = Work & Mask;
        }
        bAccountCrcTableReady = true;
    }

    const unsigned int AccountCrcRegisterWidth = 32;

} // namespace

// Server_AccountCrc -- reset/update/finalize over the DAT_06d8fd38 engine: reset to
// XorIn, FUN_06d46850 (block update; FUN_06d466f0 is the one-byte step), FUN_06d467b0
// (finalize). Only the reflect-in + table branch is live in retail: the two flags are
// written solely by the static initializer (both 1). Declared in server_savefile.h.
unsigned int Server_AccountCrc(unsigned char const* Bytes, unsigned int ByteCount) {
    AccountCrcBuildTable();

    unsigned int Crc = 0; // XorIn
    for (unsigned int I = 0; I < ByteCount; ++I)
        Crc = GAccountCrcTable[(Crc & 0xffu) ^ Bytes[I]] ^ (Crc >> 8);

    // Finalize, reflect path: mask with XorOut (0), then the byte-gather loop.
    unsigned int const TopBit = 1u << (AccountCrcWidth - 1u);
    unsigned int const Mask = ((TopBit - 1u) * 2u + 1u) | 1u;
    unsigned int Remain = Crc & Mask;
    unsigned int Folded = 0;
    for (unsigned int Shift = 8; Shift <= AccountCrcRegisterWidth; Shift += 8) {
        Folded |= (Remain & 0xffu) << (AccountCrcRegisterWidth - Shift);
        Remain >>= 8;
    }
    return Folded;
}

// Declared in server_savefile.h.
void Server_AccountDirName(char* Out, wchar_t const* AccountName) {
    // No null check in retail.
    unsigned int const Length = static_cast<unsigned int>(wcslen(AccountName));

    // Retail _wcsdup + _wcslwr; towlower on a local copy is the same transform.
    std::wstring Lowered;
    Lowered.reserve(Length);
    for (unsigned int I = 0; I < Length; ++I)
        Lowered.push_back(static_cast<wchar_t>(towlower(AccountName[I])));

    unsigned int const Folded = Server_AccountCrc(
        reinterpret_cast<unsigned char const*>(Lowered.c_str()), Length * 2u);

    sprintf(Out, "%02x-%08x", Length, Folded);
}

// Server_WriteCostume -- retail FUN_06d46930, cdecl (writer, prefix, costume). Writes one
// "<prefix><part>" key per body part via find_bodypart + get_name; a zero part id, a
// missed part or a null name skips the line. Note the body/head slot swap below. The
// prefix is "" for the worn costume and "com_" (.rdata:06d6b12c) for the comm costume;
// Server.dll has no literal "com_body". The writer is SaveGame_Writer (WriteString is
// vtable +0x14).
static void Server_WriteCostume(ISave& Save, char const* Prefix, Costume const& Cost) {
    CostumeDescriptions const* Descriptions = GetCostumeDescriptions();
    if (Descriptions == nullptr)
        return;

    struct FCostumePart {
        int TypeId;
        int PartSlot;
        char const* Format;
    };
    static const FCostumePart Parts[4] = {
        { 0, 1, "%sbody" },
        { 1, 0, "%shead" },
        { 2, 2, "%slefthand" },
        { 3, 3, "%srighthand" },
    };

    for (int I = 0; I < 4; ++I) {
        unsigned int const PartId = Cost.PartIds[Parts[I].PartSlot];
        if (PartId == 0)
            continue;
        char const* PartName = Descriptions->get_part_name(Parts[I].TypeId, PartId);
        if (PartName == nullptr)
            continue;
        char Key[128];
        sprintf_s(Key, Parts[I].Format, Prefix);
        Save.WriteString(Key, PartName);
    }
}

// PlayerData member FUN_06d48d20 (thiscall on the slot): outside single player,
// IClient::PushToClient(client_id, NULL packet), slot 9, with no null test on the interface.
// Callers: BaseExit's two refusals, the inlined LocationExit refusal inside BaseExit and
// LocationExit's own. What the MP transport does with a NULL packet is not recovered, so the
// name only says what the code does.
void PlayerData::send_client_null_packet() {
    if (SinglePlayer()) return;
    ::g_pClientInterface->PushToClient(client_id, nullptr);
}

// PlayerData::login_account -- retail FUN_06d48e80 (thiscall), only caller
// PlayerDB::load_user_data. A locked account returns 7, a banned one 0. The name copy is an
// unbounded wcscpy. Result 6 unless the directory is accessible; every saved character
// loaded (FUN_06d48d50) makes it 3. In single player a missing Restart.fl is created and
// loaded; in MP the login is logged and the account's directory path stored with no null
// test. The account is looked up a SECOND time at the end (create_account may have
// inserted it).
unsigned char PlayerData::login_account(const char* AccountDir, const SLoginInfo& Info,
                                        unsigned int SessionId) {
    {
        CiWString LoginName(Info.AccountName, wcslen(Info.AccountName));
        account_ptr = Players.FindAccountFromName(LoginName);
    }
    CAccount* Account = static_cast<CAccount*>(account_ptr);
    if (Account != nullptr) {
        if (Account->m_bLocked)
            return kLoginAccountLocked;
        if (!Account->m_bNotBanned)
            return kLoginBanned;
    }
    client_id = SessionId;
    if (Account != nullptr)
        Account->m_ClientID = SessionId;
    wcscpy(AccountName, Info.AccountName);

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
                if (load_saved_character(AccountDir, FindData.name)) {
                    Result = kLoginAccepted;
                    if (!bFoundRestart && _stricmp(FindData.name, RestartName) == 0)
                        bFoundRestart = true;
                }
                FindResult = _findnext(FindHandle, &FindData);
            }
        }
        _findclose(FindHandle);                                             // also for -1

        if (SinglePlayer()) {
            if (!bFoundRestart && Players.create_restart_file(AccountDir) &&
                load_saved_character(AccountDir, RestartName)) {
                Result = kLoginAccepted;
            }
        } else {
            Result = kLoginAccepted;
            if (!SinglePlayer()) {                                          // re-tested in retail
                FDUMP(kFdumpNotice, "LOG: Login on account %S (\"%s\")\n",
                      Info.AccountName, AccountDir);
            }
            static_cast<CAccount*>(account_ptr)->m_DirectoryPath = AccountDir;
        }
    }

    system_id = 0;
    ship_object_id = 0;
    created_ship_id = 0;
    current_base = 0;
    entered_base_id = 0;
    last_base_id = 0;
    current_location = 0;
    character_id = 0;
    {
        CiWString LoginName(Info.AccountName, wcslen(Info.AccountName));
        account_ptr = Players.FindAccountFromName(LoginName);
    }
    exited_base_id = 0;
    return Result;
}

// PlayerData::teardown -- retail FUN_06d49230: the prologue that touches other slots and
// subsystems, then the field wipe.
//  (a) the OUTBOUND trade request (+0x3D0): IClient slot 108 to the partner with
//      Players[slot->client_id].ship_object_id (indexed by client id, NOT session id),
//      and our id pulled out of the partner's inbound list;
//  (b) every INBOUND request: a requester whose own +0x3D0 is set gets the same
//      notification and its +0x3D0 cleared; then the list is erased;
//  (c) the reputation vibe freed (-1, not 0, is the empty sentinel);
//  (d) the CAccount marked logged out (m_ClientID = 0);
//  (e) SubObjectID::EquipIdMaker::Reset (+0x3C8).
void PlayerData::teardown() {
    PlayerDB& Db = Players;

    // (a) No bounds checks anywhere, and IClient is called with no null test.
    if (trade_request_partner != 0) {
        unsigned int const Partner = trade_request_partner;
        g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(Partner,
                                                                  Db[client_id].ship_object_id);
        // FUN_06d571a0 = list::remove(value): EVERY node equal to our client id goes.
        std::vector<unsigned int>& PartnerInbound = Db[Partner].inbound_trade_requests;
        for (size_t Index = 0; Index < PartnerInbound.size();) {
            if (PartnerInbound[Index] == client_id) {
                PartnerInbound.erase(PartnerInbound.begin()
                                     + static_cast<std::ptrdiff_t>(Index));
            } else {
                ++Index;
            }
        }
        trade_request_partner = 0;
    }

    // (b) Each node's session indexes Players raw.
    for (size_t Index = 0; Index < inbound_trade_requests.size(); ++Index) {
        PlayerData& RequesterSlot = Db[inbound_trade_requests[Index]];
        if (RequesterSlot.trade_request_partner == 0) continue;
        g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(RequesterSlot.trade_request_partner,
                                                                  Db[RequesterSlot.client_id].ship_object_id);
        RequesterSlot.trade_request_partner = 0;
    }
    inbound_trade_requests.clear();

    // (c)
    if (rep_vibe != -1) {
        Reputation::Vibe::Free(rep_vibe);
        rep_vibe = -1;
    }

    // The field wipe, in retail order. AccountName and both costumes get the 0xcd fill.
    std::memset(AccountName, 0xcd, sizeof(AccountName));
    saved_characters.clear();
    ship_archetype = 0;
    relative_health = 0.0f;
    equipment.Items.clear();
    collision_groups.clear();
    std::memset(&com_costume, 0xcd, sizeof(com_costume));
    voice_length = 0;
    voice[0] = '\0';
    std::memset(&costume, 0xcd, sizeof(costume));
    for (int Axis = 0; Axis < 3; ++Axis)
        (&initial_pos.x)[Axis] = 0.0f;
    for (int Cell = 0; Cell < 9; ++Cell)
        (&initial_ornt.E[0][0])[Cell] = (Cell % 4 == 0) ? 1.0f : 0.0f;
    // 0x29a (666), the retail fill value.
    cash = 0x29a;
    num_kills = 0;
    num_mission_successes = 0;
    num_mission_failures = 0;
    bSkipNextAutosave = 0;
    save_failure_count = 0;
    client_id = 0;
    bCheatReported = 0;

    // (d) account_ptr itself is NOT cleared.
    CAccount* Account = static_cast<CAccount*>(account_ptr);
    if (Account != nullptr) {
        Account->m_ClientID = 0;
    }

    // (e) Reset stores 0x21.
    equip_id_maker.Reset();

    system_id = 0;
    ship_object_id = 0;
    created_ship_id = 0;
    current_base = 0;
    entered_base_id = 0;
    last_base_id = 0;
    current_location = 0;
    character_id = 0;
    exited_base_id = 0;
    starting_ring = CreateID("NO WHERE");
    selected_character.invalidate();
    // group_mission_id (+0x408) is left alone.
    group_ptr = nullptr;
    group_mission_set_by = 0;
    visit_map.clear();
    // Not touched by retail: rank_level, +0x288, total_earned, the equipment
    // snapshot, weapon_group_name, mission_log, interface_state, the pending
    // visits vector, difficulty_scale, trade_offer, group_mission_id.
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
        (&initial_pos.x)[Axis] = 0.0f;
    for (int Cell = 0; Cell < 9; ++Cell)
        (&initial_ornt.E[0][0])[Cell] = (Cell % 4 == 0) ? 1.0f : 0.0f;
}

// PlayerData::check_position_bound -- retail FUN_06d498f0 (thiscall). Outside single player, a
// live ship whose squared distance from the origin is strictly greater than 750000^2
// (DAT_06d6b450; equal or unordered passes) is destroyed and false returned. Retail sums
// in x87 extended precision, the port in double; they can only disagree within a double
// ulp of 5.625e11. Sole caller: IServerImpl::SPObjUpdate.
bool PlayerData::check_position_bound(Vector const* Position) {
    if (SinglePlayer())
        return true;
    GameObject* const ShipObj = Server_LookupLiveObject(ship_object_id);
    if (ShipObj == nullptr)
        return true;
    double const X = Position->x;
    double const Y = Position->y;
    double const Z = Position->z;
    double const DistSq = X * X + Y * Y + Z * Z;
    if (!(DistSq > 562500000000.0))
        return true;
    ShipObj->destroy(DESTROY_FUSE, ship_object_id);
    return false;
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

// PlayerData::build_character_set -- retail FUN_06d499c0 (thiscall). Only caller:
// CharacterInfoReq, when its bool is false.
void PlayerData::build_character_set(CCharacterSet& Out) {
    // IsMPServer() twice, as retail: an MP server with a selected character leaves the
    // world first.
    if (IsMPServer() && (!IsMPServer() || selected_character.Buffer[0] != '\0')) {
        auto_save();

        // The session id is the slot's own client_id.
        unsigned int const SessionId = client_id;
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
             saved_characters.begin();
         It != saved_characters.end(); ++It) {
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
    Out.SaveFailureCount = save_failure_count;
    save_failure_count = 0;
}

// PlayerData::destroy_character -- retail FUN_06d49f60 (thiscall); only caller
// IServerImpl::DestroyCharacter.
bool PlayerData::destroy_character(CHARACTER_ID const& Id, unsigned int SessionId) {
    // Deleting the selected character deselects it first.
    if (selected_character == Id)
        deselect_character(SessionId);

    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
        saved_characters.find(Id);
    if (It == saved_characters.end())
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
        Server_AccountDirName(AccountDirName, AccountName);
    } else {
        wcstombs(AccountDirName, AccountName, 0x104);
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

    saved_characters.erase(It);

    if (!SinglePlayer()) {
        FDUMP(kFdumpNotice, "LOG: Delete character on account %S (\"%s\")\n",
              AccountName, Path);
    }
    return true;
}

// PlayerData::reload_ship_data -- retail FUN_06d4a200 (thiscall; the name is from the FDUMP
// text). In single player it FDUMPs and execution CONTINUES. Restores the base hull status
// and collision groups from the selected character's record (operator[], so a missing
// record is inserted default), keeps the record's mounted base equipment plus every
// unmounted shield battery / repair kit / munition / mine / countermeasure currently
// carried (no null test on the archetype), and writes that list back to the slot and to
// both of the record's lists.
void PlayerData::reload_ship_data() {
    if (!IsMPServer()) {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0x70e,
              "reload_ship_data() is not valid in singleplayer\n");
    }
    current_base = last_base_id;

    PlayerData::FSavedCharacter& Record =
        saved_characters[selected_character];

    relative_health = Record.BaseHullStatus;
    collision_groups = Record.BaseCollisionGroups;

    std::list<EquipDesc> Kept;
    std::list<EquipDesc>::const_iterator Base;
    for (Base = Record.BaseEquipment.Items.begin(); Base != Record.BaseEquipment.Items.end();
         ++Base) {
        if (Base->bEquipped)
            Kept.push_back(*Base);
    }
    std::list<EquipDesc>::const_iterator Cur;
    for (Cur = equipment.Items.begin(); Cur != equipment.Items.end(); ++Cur) {
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

    equipment.Items.clear();
    equipment.Items.assign(Kept.begin(), Kept.end());
    Record.BaseEquipment.Items = equipment.Items;
    Record.Equipment.Items = equipment.Items;
}

// PlayerData::deselect_character -- retail FUN_06d4a4a0 (thiscall). Leaving the world without
// logging out: logout's system / base / trade teardown, the player-list removal and event
// 7, then the selection is dropped. `this` is only read for +0x64; everything else goes
// through Players[SessionId].
void PlayerData::deselect_character(unsigned int SessionId) {
    if (SinglePlayer())
        return;
    PlayerData& Owner = Players[SessionId];
    if (!selected_character.is_valid())
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

    selected_character.Buffer[0] = '\0';
}

// PlayerData::select_character -- retail FUN_06d4a5d0 (thiscall), only caller
// IServerImpl::CharacterSelect. This is the head and the MP-only front; the shared tail is
// PlayerData::load_selected_character (server_savegame.cpp). Players[sid] is recomputed from
// the argument rather than using `this`.
bool PlayerData::select_character(CHARACTER_ID const& Id, unsigned int SessionId) {
    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator Found =
        saved_characters.find(Id);
    if (Found == saved_characters.end())
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
        if (selected_character.Buffer[0] != '\0') {
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
            if (Id != selected_character) {
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

        selected_character = Id;

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

    return load_selected_character(Record, SessionId);
}

// PlayerData::should_queue_visit -- retail FUN_06d4b200 (thiscall): false when the flags,
// ORed with the map's entry for Key, have both 0x10 and 0x08. Callers pass
// (solar+0xb0, CSolar::GetVisitValue()).
bool PlayerData::should_queue_visit(unsigned int Key, unsigned char Flags) const {
    std::map<unsigned int, unsigned char>::const_iterator Found = visit_map.find(Key);
    if (Found != visit_map.end())
        Flags = static_cast<unsigned char>(Flags | Found->second);
    if ((Flags & 0x10) != 0 && (Flags & 0x08) != 0)
        return false;
    return true;
}

// PlayerData::queue_visit -- retail FUN_06d4b270 (thiscall): push_back onto the pending
// vector.
void PlayerData::queue_visit(unsigned int Key, unsigned char Flags) {
    PlayerData::FVisitedState State;
    State.system_id = Key;
    State.visit_flags = Flags;
    visited_states.push_back(State);
}

// The counter block IServerImpl::GetServerStats copies out (.data:06d8f998); PlayerDB::logout
// bumps +0x08. Defined in IServerImpl.cpp.
extern ServerStats GServerStats;

// PlayerData::flush_pending_visits -- retail FUN_06d4b450 (fastcall on the slot): commits the
// pending visit queue (+0x3b8) into the persistent visit map (+0x3a0), adding the
// "committed" bit 0x08 and ORing in the same key's existing flags (FUN_06d15d10 is find).
void PlayerData::flush_pending_visits() {
    for (size_t Index = 0; Index < visited_states.size(); ++Index) {
        const PlayerData::FVisitedState& Pending = visited_states[Index];
        unsigned char Flags = static_cast<unsigned char>(Pending.visit_flags | 0x08);

        std::map<unsigned int, unsigned char>::iterator Found =
            visit_map.find(Pending.system_id);
        if (Found != visit_map.end())
            Flags = static_cast<unsigned char>(Flags | Found->second);
        visit_map[Pending.system_id] = Flags;
    }
    visited_states.clear();
}

// PlayerData::report_cheat -- retail FUN_06d4f570; its only caller is the gate
// PlayerData::check_for_cheats (FUN_06d4ccd0, latch +0x34C, gated on the current base).
//  (1) FDUMP "LOG: Possible cheating detected (%s) by %s (%s)!" with the reason, the
//      character name (from the vibe, fallback L">>>BAD   NAME<<<", three spaces) and the
//      slot's AccountName, both as NARROW copies built with a truncating (char) cast.
//  (2) StyleNode(0xd681) + TextNode(GCheatMessage) + TextNode(characterName).
//  (3) On a successful encode, IServerImpl::SubmitChat(CHAT_ID(0), Size, buffer,
//      DAT_06d902c8 = 0x00010000, -1); the `unsigned long` is the BYTE COUNT.
//  (4) A line appended to <UserDataPath>\Accts[\MultiPlayer]\cheaters.log.
// PlayerData::check_for_cheats -- the gate at the top of retail FUN_06d4ccd0 (the save/land
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

    // FUN_06d4faa0. The x87 compare makes the cheat strictly-greater.
    bool PlayerData_HoldOverCapacity(PlayerData const& Slot) {
        Archetype::Ship* Hull = Archetype::GetShip(Slot.ship_archetype);
        if (Hull == nullptr)
            return false;
        return Slot.equipment.get_cargo_space_occupied() > Hull->HoldSize;
    }

    // Retail's cash ceiling, 999999999; every arithmetic path in FUN_06d4faf0 saturates at it.
    int const GCashCeiling = 0x3b9ac9ff;

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
        return HpTypeNone;
    }

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
                HpAttachmentType Best = HpTypeNone;
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

} // namespace

bool PlayerData::check_for_cheats() {
    if (SinglePlayer())
        return false;

    // One report per session.
    if (bCheatReported == 1)
        return true;

    // The whole check needs a base to price against.
    if (current_base == 0)
        return false;

    int Reason = 0;
    if (PlayerData_EquipmentExceedsShipSpecs(*this)) {
        Reason = 3;
    } else if (PlayerData_EquipmentNotSoldOnBase(*this)) {
        Reason = 2;
    } else if (PlayerData_TotalEquipmentValue(*this) > total_earned) {
        Reason = 1;
    } else if (PlayerData_HoldOverCapacity(*this)) {
        Reason = 4;
    } else {
        return false;
    }

    report_cheat(Reason);
    bCheatReported = 1;

    // IClient slot 9, PushToClient(client_id, NULL), with no null test on the interface.
    if (!SinglePlayer())
        ::g_pClientInterface->PushToClient(client_id, nullptr);
    return true;
}

// Server_FormatMissionLogEntry -- retail FUN_06d4cc20, the recursive formatter for one
// FmtStr `log =` line: "id, count" then ", data, tag, flags" per Val. Retail also passes
// an ECX that is only handed to the recursive call, never read; the port drops it.
// The output was checked against saved character files: a nested FmtStr (tag 0x21)
// follows a bare ',' with no space (`..., 33, 0,327682, 2, ...`).
// The destination is FUN_06d4ccd0's 0x1000-byte stack buffer. Retail checks neither
// its size, nor a null subtree on a 0x21 node, nor the chain against the count taken
// first; the port does the same.
static void Server_FormatMissionLogEntry(char* Out, FmtStr const& Entry) {
    int Count = 0;
    for (FmtStr::Val const* Node = Entry.Head; Node != nullptr; Node = Node->Next)
        ++Count;

    sprintf(Out, "%u, %u", Entry.MadLibId, Count);

    // Runs Count times, following Val::Next unchecked.
    FmtStr::Val const* Node = Entry.Head;
    for (int Remaining = Count; Remaining > 0; --Remaining) {
        sprintf(Out + strlen(Out), ", %u, %u, %u",
                Node->Value,   // the raw payload dword, whatever the tag
                (unsigned int)Node->TypeTag,
                (unsigned int)Node->Flags);

        // TagFmtStr = nested FmtStr (common_text.h): bare ',' then recurse after it.
        if (Node->TypeTag == FmtStr::TagFmtStr) {
            char* const Tail = Out + strlen(Out);
            *Tail = ',';
            Server_FormatMissionLogEntry(Tail + 1, *Node->Subtree);
        }
        Node = Node->Next;
    }
}

// Server_CopyCostumeCounted -- the Costume copy the MP record refresh in FUN_06d4ccd0
// inlines twice (.text:06d4e63c, .text:06d4e6f6): only `count` accessory ids, under a
// signed loop. Slots past the count keep what the destination held.
static void Server_CopyCostumeCounted(Costume& Dst, Costume const& Src) {
    Dst.PartIds[0] = Src.PartIds[0];
    Dst.PartIds[1] = Src.PartIds[1];
    Dst.PartIds[2] = Src.PartIds[2];
    Dst.PartIds[3] = Src.PartIds[3];
    Dst.AccessoryCount = Src.AccessoryCount;
    int const Count = static_cast<int>(Src.AccessoryCount);
    for (int I = 0; I < Count; ++I)
        Dst.AccessoryIds[I] = Src.AccessoryIds[I];
}

// PlayerData::serialize_to_file -- retail FUN_06d4ccd0 (.text:06d4ccd0 .. .text:06d4ea4f).
//
// Signature: thiscall with three dword stack arguments (every exit is `RET 0xc`),
// returning AL; Ghidra's 15-parameter prototype is wrong. FileName is a base file name
// (_makepath's `fname`), Description a wchar_t* (WideToHex'd into `description`),
// DescripStrId is printed as `descrip_strid`. The autosave call site agrees.
//
// The cheat gate and the writer are one function: the save is refused when the cheat
// check fires.
//
// The "FLS1" container is written on every path, single player included: fopen "wb",
// the four bytes "FLS1", then the text ciphered with
// out[i] ^= (("Gene"[i & 3] + (unsigned char)i) | 0x80) (key at .rdata:06d6b318).
// Multiplayer-only is the destination: outside single player the text goes to
// `<accts>\temp.fl`, then the real path is unlinked and the temp renamed onto it.
//
// The MP base-record copy (.text:06d4d4dd) and the base_* keys read FSavedCharacter's
// BaseHullStatus (+0x25c), BaseEquipment (+0x260), BaseCollisionGroups (+0x26c); the
// listing's +0x468/+0x46c/+0x478 are node offsets (record at node + 0x20c). The map key
// is +0x64 alone (FUN_06d57da0 pairs it with a default record from FUN_06d46ab0).
bool PlayerData::serialize_to_file(char const* FileName, wchar_t const* Description,
                                   unsigned int DescripStrId) {
    // No null test on FileName, as retail.

    // The cheat gate (.text:06d4cd01), returning 0 (save refused) from inside it.
    // PlayerData::check_for_cheats (server_playerdb.cpp) includes the already-reported
    // latch at +0x34C.
    if (check_for_cheats())
        return false;

    // A dead MP ship is put back to the last-base loadout before it is written. Retail
    // compares the hull (+0x268) with 0.0 (DAT_06d65538) so the call runs for hull <= 0
    // only; an unordered (NaN) hull skips it.
    if (!SinglePlayer() && relative_health <= 0.0f)
        reload_ship_data();

    // The pending-visit flush (queue at +0x3B8 into FUN_06d508a0 with flags | 8). The
    // `visit` key reads the committed map at +0x3A0, so the flush must come first or
    // the newest systems are lost.
    flush_pending_visits();

    // The destination, built by retail itself (.text:06d4ce2b): account dir from the wide
    // account name at PlayerData+0x00, then FileName with extension "fl" (.rdata:06d6aea4)
    // in SP only -- in MP FileName carries its own extension.
    char AccountDirName[260];                                               // Stack[0x1f4], no pre-init
    if (IsMPServer()) {
        Server_AccountDirName(AccountDirName, AccountName);
    } else {
        // No return-value test and no explicit terminator. Retail UB kept: a 260+
        // character name, or one that does not convert, leaves the buffer unterminated.
        wcstombs(AccountDirName, AccountName, 0x104);
    }

    char AccountDir[260];
    _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);

    char SavePath[260];
    _makepath(SavePath, nullptr, AccountDir, FileName, IsMPServer() ? nullptr : "fl");

    char TempPath[260];
    sprintf_s(TempPath, "%s\\temp.fl", Server_GetAccountsRoot());

    char const* const WritePath = SinglePlayer() ? SavePath : TempPath;

    // The writer object lives on retail's stack (base vtable 06d6b5c0, concrete 06d6b5a0).
    // Open mode is "w+t" (.rdata:DAT_06d6b598 = 77 2b 74 00; the listing shows only "w"):
    // "+" lets Close() read the file back for encoding, "t" makes saves LF-only.
    SaveGame_Writer Writer;
    if (!Writer.Open(WritePath)) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Failed to open %s for writing",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xad4, WritePath);
        return false;
    }

    // Retail keeps the path being written in DAT_06d902f8; PlayerData::mission_save
    // (FUN_06d4c600) copies that file to AutoSave.fl afterwards.
    safe_strcpy(GServerLastSavePath, 0x104, WritePath);

    // Retail writes most keys straight to the FILE* (writer+0x04); only the costume
    // blocks and the single-player hook go through the writer's vtable.
    FILE* const Out = Writer.File;

    fprintf(Out, "\n[%s]\n", "Player");

    if (Description != nullptr) {
        char HexBuf[2048];
        WideToHex(Description, HexBuf);
        fprintf(Out, "%s = %s\n", "description", HexBuf);
    }

    if (DescripStrId != 0)
        fprintf(Out, "%s = %u\n", "descrip_strid", DescripStrId);

    fprintf(Out, "\n");

    // `tstamp`: the high FILETIME half is printed first, no space after the comma.
    {
        SYSTEMTIME SysTime;
        FILETIME FileTime;
        GetLocalTime(&SysTime);
        SystemTimeToFileTime(&SysTime, &FileTime);
        char TstampBuf[64];
        sprintf_s(TstampBuf, "%u,%u", FileTime.dwHighDateTime, FileTime.dwLowDateTime);
        fprintf(Out, "%s = %s\n", "tstamp", TstampBuf);
    }

    // The reputation block: one Reputation::Vibe::Get on PlayerData::rep_vibe (+0x318) and
    // four keys off its out-parameters. `name` is the vibe's display name (not the
    // character name); `rank` is overridden in SP from rank_level (+0x284); `house` is the
    // per-group line; `rep_group` is written once, only for a valid affiliation.
    // The Relation array (group_count() * 8) and the rank/count live at function scope:
    // the MP record refresh at the end reads them again before retail frees the array.
    // Vibe::Get is called unconditionally, as retail.
    unsigned int const GroupCount = GlobalReputation::group_count();
    GlobalReputation::Relation* const Relations = static_cast<GlobalReputation::Relation*>(
        operator new(GroupCount * sizeof(GlobalReputation::Relation)));
    unsigned int Affiliation = kNoAffiliation;
    unsigned int Rank = 0;
    unsigned char RelationCount = 0;
    FmtStr Name1;
    FmtStr Name2;
    {
        wchar_t const* DisplayName = nullptr;

        GlobalReputation::Vibe::Get(rep_vibe, Affiliation, Rank, RelationCount,
                                    Relations, Name1, Name2, DisplayName);

        // `name`, unconditionally. Retail UB kept as a path: Vibe::Get writes its
        // out-parameters only on a hit, so on a miss retail hands an uninitialised stack
        // word to WideToHex; the port passes nullptr, which faults the same way. Retail
        // also leaves Affiliation, Rank and the count uninitialised on a miss; the port
        // starts them at 0xffffffff, 0, 0.
        {
            char HexBuf[2048];
            WideToHex(DisplayName, HexBuf);
            fprintf(Out, "%s = %s\n", "name", HexBuf);
        }

        if (SinglePlayer())
            Rank = rank_level;
        fprintf(Out, "%s = %u\n", "rank", Rank);

        // The bound is the byte counter Vibe::Get filled.
        for (unsigned int I = 0; I < static_cast<unsigned int>(RelationCount); ++I) {
            TString<16> GroupNick;
            GroupNick.Length = 0;
            GroupNick.Data[0] = '\0';
            GlobalReputation::get_nickname(GroupNick, Relations[I].GroupId);
            char HouseBuf[256];
            sprintf_s(HouseBuf, "%g, %s", static_cast<double>(Relations[I].Feeling),
                      GroupNick.Data);
            fprintf(Out, "%s = %s\n", "house", HouseBuf);
        }

        if (Affiliation != kNoAffiliation) {
            TString<16> AffilNick;
            AffilNick.Length = 0;
            AffilNick.Data[0] = '\0';
            GlobalReputation::get_nickname(AffilNick, Affiliation);
            fprintf(Out, "%s = %s\n", "rep_group", AffilNick.Data);
        }
    }

    // Retail order: money first. Offsets +0x31c, +0x334, +0x338, +0x33c.
    fprintf(Out, "%s = %d\n", "money", cash);
    fprintf(Out, "%s = %d\n", "num_kills", num_kills);
    fprintf(Out, "%s = %d\n", "num_misn_successes", num_mission_successes);
    fprintf(Out, "%s = %d\n", "num_misn_failures", num_mission_failures);

    fprintf(Out, "\n");

    // The inline buffer at +0x2C4.
    fprintf(Out, "%s = %s\n", "voice", voice);

    Server_WriteCostume(Writer, "com_", com_costume);
    Server_WriteCostume(Writer, "", costume);

    fprintf(Out, "\n");

    // `system` of PlayerData+0x3E0. Retail does not null-check get_system; the nickname
    // is ISystem+0x50 (SYSTEM vtable slot 2).
    {
        Universe::ISystem const* Sys = Universe::get_system(system_id);
        fprintf(Out, "%s = %s\n", "system", Sys->Nickname);
    }

    // The base / position branch (.text:06d4d46a). With no live ship and no object bits in
    // current_base (+0x3ec), current_base is refilled from last_base_id (+0x3f0) first.
    //
    // SavedPos/SavedOrient are written only by the `pos`/`rotate` arm, but the MP record
    // refresh at the end reads them whether or not it ran. Retail zeroes the position
    // unconditionally before `system`, so {0,0,0} is retail. Retail UB not reproduced:
    // nothing initialises the orientation outside the `rotate` arm, so retail copies nine
    // uninitialised stack dwords into the record; the port copies zeros.
    Vector SavedPos = { 0.0f, 0.0f, 0.0f };
    Matrix SavedOrient(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    {
        if (ship_object_id == 0 && (current_base & 0x3fffffffu) == 0)
            current_base = last_base_id;

        Universe::IBase* Base = Universe::get_base(current_base);
        if (Base != nullptr) {
            // IBase+0x8 nickname (BASE vtable slot 1); retail does not null-test it.
            fprintf(Out, "%s = %s\n", "base", Base->Nickname);
            fprintf(Out, "\n");
            // Outside SP the docked state goes into the selected character's record
            // (+0x50): unique insert keyed on +0x64 (an existing record is kept), then
            // relative_health, collision groups and equipment are copied into it.
            if (!SinglePlayer()) {
                // insert(pair(key, default)).first == operator[] (find or
                // default-insert, never overwrite).
                PlayerData::FSavedCharacter& BaseRecord =
                    saved_characters[selected_character];
                BaseRecord.BaseHullStatus = relative_health;
                BaseRecord.BaseCollisionGroups = collision_groups;
                BaseRecord.BaseEquipment.Items = equipment.Items;
            }
        } else {
            // No base: `pos` and `rotate`, skipped (with the newline) when
            // Server_LookupLiveObject (FUN_06d00670, lookup + is_destroyed reject) fails.
            // Angles: FUN_06d4ea60/ea80/eaa0 extract the matrix columns; H =
            // sqrt(E00*E00 + E10*E10) is stored to float and compared with DAT_06d6b6a0
            // (1.9073486e-06f):
            //   H > eps : A = atan2(E21, E22), B = atan2(-E20, H), C = atan2(E10, E00)
            //   else    : A = atan2(-E12, E11), B = atan2(-E20, H), C = 0
            // each times DAT_06d6b69c (57.2957763671875f), printed as degrees in order
            // A, B, C; the loader feeds them to EulerMatrix, which takes degrees.
            GameObject* const LiveObj = Server_LookupLiveObject(ship_object_id);
            if (LiveObj != nullptr) {
                // Position source: SP -> get_position. MP -> the CShip's
                // CRemotePhysicsController (__RTDynamicCast of the controller at ship+0x60):
                // latest sample (+0xe0) when the sample count (+0x388) is non-zero, else
                // get_position; a cast miss leaves the zeros.
                // Retail UB reproduced as a path: a null or non-CShip cobject() makes
                // retail read address 0x60 and fault; so does the port.
                if (SinglePlayer()) {
                    Vector const& LivePos = LiveObj->get_position();
                    SavedPos.x = LivePos.x;
                    SavedPos.y = LivePos.y;
                    SavedPos.z = LivePos.z;
                } else {
                    CObject const* const Cobj = LiveObj->cobject();
                    CObject const* const Ship =
                        (Cobj != nullptr &&
                         (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                            ? Cobj
                            : nullptr;
                    IVP_Controller* const Rc =
                        static_cast<IVP_Controller*>(Ship->GolemController);
                    CRemotePhysicsController* const Remote =
                        dynamic_cast<CRemotePhysicsController*>(Rc);
                    if (Remote != nullptr) {
                        if (Remote->GetSampleCount() != 0) {
                            Vector const Latest = Remote->GetLatestUpdate();
                            SavedPos.x = Latest.x;
                            SavedPos.y = Latest.y;
                            SavedPos.z = Latest.z;
                        } else {
                            Vector const& LivePos = LiveObj->get_position();
                            SavedPos.x = LivePos.x;
                            SavedPos.y = LivePos.y;
                            SavedPos.z = LivePos.z;
                        }
                    }
                }
                char PosBuf[128];
                sprintf(PosBuf, "%g,%g,%g", static_cast<double>(SavedPos.x),
                        static_cast<double>(SavedPos.y),
                        static_cast<double>(SavedPos.z));
                fprintf(Out, "%s = %s\n", "pos", PosBuf);

                ::Matrix const& LiveOrient = LiveObj->get_transform().Rotation;
                for (int I = 0; I < 9; ++I)
                    SavedOrient.E[I / 3][I % 3] = LiveOrient.E[I / 3][I % 3];

                Matrix const& M = SavedOrient;
                double const DegPerRad = static_cast<double>(57.2957763671875f);
                double const HypotExact = sqrt(static_cast<double>(M.E[0][0]) * M.E[0][0] +
                                               static_cast<double>(M.E[1][0]) * M.E[1][0]);
                float const Hypot = static_cast<float>(HypotExact);      // FST: compare uses the double
                double AngleA = 0.0;
                double AngleB = 0.0;
                float AngleC = 0.0f;
                if (HypotExact > static_cast<double>(1.9073486328125e-06f)) {
                    AngleA = atan2(static_cast<double>(M.E[2][1]), static_cast<double>(M.E[2][2]));
                    AngleB = atan2(-static_cast<double>(M.E[2][0]), static_cast<double>(Hypot));
                    AngleC = static_cast<float>(atan2(static_cast<double>(M.E[1][0]),
                                                      static_cast<double>(M.E[0][0])));
                } else {
                    AngleA = atan2(-static_cast<double>(M.E[1][2]), static_cast<double>(M.E[1][1]));
                    AngleB = atan2(-static_cast<double>(M.E[2][0]), static_cast<double>(Hypot));
                    AngleC = 0.0f;
                }
                float const DegA = static_cast<float>(AngleA * DegPerRad); // retail stores A as float
                double const DegB = AngleB * DegPerRad;
                double const DegC = static_cast<double>(AngleC) * DegPerRad;

                char RotBuf[128];
                sprintf(RotBuf, "%g,%g,%g", static_cast<double>(DegA), DegB, DegC);
                fprintf(Out, "%s = %s\n", "rotate", RotBuf);
                fprintf(Out, "\n");
            }
        }
    }

    // +0x3F8, only when non-zero.
    if (current_location != 0)
        fprintf(Out, "%s = %u\n", "location", current_location);

    // ship_archetype / hull_status / collision_group, all gated on the archetype resolving.
    if (ship_archetype != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(ship_archetype);
        if (ShipArch != nullptr) {
            // The resolved archetype's Root::Nickname id (+0x8).
            char ArchBuf[32];
            sprintf_s(ArchBuf, "%u", ShipArch->get_id());
            fprintf(Out, "%s = %s\n", "ship_archetype", ArchBuf);

            if (relative_health < 1.0f) {
                fprintf(Out, "%s = %g\n", "hull_status",
                        static_cast<double>(relative_health));
            }

            // The player's own list at +0x26C.
            for (std::list<CollisionGroupDesc>::const_iterator It =
                     collision_groups.begin();
                 It != collision_groups.end(); ++It) {
                if (!(It->Health < 1.0f))
                    continue;
                Archetype::CollisionGroup const* Group = ShipArch->get_group_by_id(It->Id);
                if (Group == nullptr)
                    continue;
                char GroupBuf[256];
                sprintf_s(GroupBuf, "%s, %g", Group->ObjName, static_cast<double>(It->Health));
                fprintf(Out, "%s = %s\n", "collision_group", GroupBuf);
            }
        }
    }

    // Written on the join point of both archetype skips.
    fprintf(Out, "\n");

    // The equipment list: `cargo` and `equip`.
    for (std::list<EquipDesc>::const_iterator CI = equipment.Items.begin();
         CI != equipment.Items.end(); ++CI) {
        EquipDesc const& Item = *CI;

        if (!::SinglePlayer() && Item.bMission)
            continue;

        Archetype::Equipment* Equip = Archetype::GetEquipment(Item.ArchId);
        if (Equip == nullptr) continue;
        unsigned int const NicknameId = Equip->Nickname.Id;

        char Line[512];
        if (!Item.bEquipped) {
            if (Item.Health == 1.0f) {
                sprintf(Line, "%u, %d, , , %d", NicknameId, Item.Count, Item.bMission ? 1 : 0);
            } else {
                sprintf(Line, "%u, %d, , %g, %d", NicknameId, Item.Count,
                        static_cast<double>(Item.Health), Item.bMission ? 1 : 0);
            }
            fprintf(Out, "%s = %s\n", "cargo", Line);
        } else {
            if (Item.is_internal()) {
                sprintf(Line, "%u, , %g", NicknameId, static_cast<double>(Item.Health));
            } else {
                sprintf(Line, "%u, %s, %g", NicknameId, Item.Hardpoint,
                        static_cast<double>(Item.Health));
            }
            fprintf(Out, "%s = %s\n", "equip", Line);
        }
    }

    fprintf(Out, "\n");

    // `last_base` and the base_* keys, MP only and only when a second base resolves from
    // last_base_id (+0x3F0) -- not the base written above. The base_* keys read the
    // selected character's record (+0x50), looked up before `last_base` is written.
    if (!SinglePlayer()) {
        Universe::IBase* LastBase = Universe::get_base(last_base_id);
        if (LastBase != nullptr) {
            PlayerData::FSavedCharacter const& BaseRecord =
                saved_characters[selected_character];

            fprintf(Out, "%s = %s\n", "last_base", LastBase->Nickname);
            fprintf(Out, "\n");

            // Unconditional (no `< 1` test, unlike hull_status).
            fprintf(Out, "%s = %g\n", "base_hull_status",
                    static_cast<double>(BaseRecord.BaseHullStatus));

            // GetShip(+0x264) without a null test; every group, no health filter.
            Archetype::Ship* BaseShip = Archetype::GetShip(ship_archetype);
            for (std::list<CollisionGroupDesc>::const_iterator It =
                     BaseRecord.BaseCollisionGroups.begin();
                 It != BaseRecord.BaseCollisionGroups.end(); ++It) {
                Archetype::CollisionGroup const* Group = BaseShip->get_group_by_id(It->Id);
                if (Group == nullptr)
                    continue;
                char GroupBuf[256];
                sprintf(GroupBuf, "%s, %g", Group->ObjName, static_cast<double>(It->Health));
                fprintf(Out, "%s = %s\n", "base_collision_group", GroupBuf);
            }
            fprintf(Out, "\n");

            // Mission items are skipped in every mode here.
            for (std::list<EquipDesc>::const_iterator It =
                     BaseRecord.BaseEquipment.Items.begin();
                 It != BaseRecord.BaseEquipment.Items.end(); ++It) {
                EquipDesc const& Item = *It;
                if (Item.bMission)
                    continue;
                Archetype::Equipment* Equip = Archetype::GetEquipment(Item.ArchId);
                if (Equip == nullptr)
                    continue;
                unsigned int const NicknameId = Equip->Nickname.Id;

                char Line[512];
                if (!Item.bEquipped) {
                    if (Item.Health == 1.0f) {
                        sprintf(Line, "%u, %d, , , %d", NicknameId, Item.Count,
                                Item.bMission ? 1 : 0);
                    } else {
                        sprintf(Line, "%u, %d, , %g, %d", NicknameId, Item.Count,
                                static_cast<double>(Item.Health), Item.bMission ? 1 : 0);
                    }
                    fprintf(Out, "%s = %s\n", "base_cargo", Line);
                } else {
                    if (Item.is_internal()) {
                        sprintf(Line, "%u, , %g", NicknameId, static_cast<double>(Item.Health));
                    } else {
                        sprintf(Line, "%u, %s, %g", NicknameId, Item.Hardpoint,
                                static_cast<double>(Item.Health));
                    }
                    fprintf(Out, "%s = %s\n", "base_equip", Line);
                }
            }
            fprintf(Out, "\n");
        }
    }

    // The weapon-group block: a pre-rendered block of ini lines (std::string data at
    // +0x384) passed as fprintf's whole format string. Retail UB reproduced as a path:
    // a '%' in it is interpreted with no arguments behind it.
#pragma warning(push)
#pragma warning(disable : 4774 6387)  // non-literal format string: retail's own call shape
    fprintf(Out, weapon_group_name.c_str());
#pragma warning(pop)

    fprintf(Out, "\n");

    // `visit`: one line per entry of the committed map at +0x3A0.
    for (std::map<unsigned int, unsigned char>::const_iterator VI = visit_map.begin();
         VI != visit_map.end(); ++VI) {
        char VisitBuf[64];
        sprintf_s(VisitBuf, "%u, %u", VI->first, static_cast<unsigned int>(VI->second));
        fprintf(Out, "%s = %s\n", "visit", VisitBuf);
    }

    fprintf(Out, "\n");

    // `log`: one line per mission_log FmtStr (list at +0x390).
    for (std::list<FmtStr>::const_iterator LI = mission_log.begin();
         LI != mission_log.end(); ++LI) {
        // FUN_06d4ccd0's 0x1000-byte stack scratch.
        char LogText[0x1000];
        Server_FormatMissionLogEntry(LogText, *LI);
        fprintf(Out, "%s = %s\n", "log", LogText);
    }

    fprintf(Out, "\n");

    // +0x39C.
    fprintf(Out, "%s = %u\n", "interface", interface_state);

    fprintf(Out, "\n");

    // Controller event 4 (FUN_06d43650) with {client_id (+0x348), the open save file}.
    struct FSaveWrittenEvent {
        unsigned int ClientId;                  // PlayerData+0x348
        FILE* File;                             // the writer's +0x04
    };
    FSaveWrittenEvent SaveEvent;
    SaveEvent.ClientId = client_id;
    SaveEvent.File = Out;
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerSave, &SaveEvent);

    // Single player: the [Time] section, the feelings and event 0x41, still in the same
    // file before the error latch and the encode.
    if (SinglePlayer())
        Server_SaveSinglePlayerState(&Writer);

    // The write-error latch (an inlined ferror). On error retail still runs the whole
    // encode below and then unlinks what it just wrote, so a broken file is never
    // loaded back.
    bool const WriteFailed = Writer.HadWriteError();
    if (WriteFailed) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Error writing %s\n",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xc6d, WritePath);
    }

    // The "FLS1" container (.text:06d4e0cf), in every mode; Close() is FUN_06d4eb00.
    Writer.Close();

    if (WriteFailed) {
        _unlink(WritePath);
        // Retail leaks the relation array on this path (only the success path reaches
        // its free at .text:06d4e92a); so does the port.
        return false;
    }

    // The MP swap: the finished temp.fl replaces the real file, so a crash mid-write
    // leaves the previous character file intact.
    if (!SinglePlayer()) {
        _unlink(SavePath);
        rename(TempPath, SavePath);
    }

    // The MP record refresh (.text:06d4e18c): the selected character's record (+0x50) is
    // brought up to what was just written, so it reflects the last save, not the last
    // load. Record lookup is insert(pair(+0x64, default)).first, i.e. operator[]; node
    // offsets below are record + 0x20c.
    //   node+0x44c  HullStatus      = +0x268
    //   node+0x450  Equipment       = +0x278 (list operator=)
    //   node+0x56c  EquipIdMaker    = +0x3c8 (a WORD)
    //   node+0x45c  CollisionGroups = +0x26c (list operator=)
    //   node+0x43c..0x448  Money / NumKills / NumMissionSuccesses /
    //               NumMissionFailures = +0x31c/+0x334/+0x338/+0x33c
    //   node+0x4c8  Rank            = Vibe::Get's rank
    //   node+0x590  Visits          = +0x3a0 (map operator=, FUN_06d57540 / FUN_06d59e10)
    //   node+0x570  WeaponGroups    = +0x380 (string assign)
    //   node+0x484 / 0x488  BaseId / LastBaseId = +0x3ec / +0x3f0
    //   node+0x494  Position        = the `pos` locals
    //   node+0x4a0  Orientation     = the nine matrix dwords
    //   node+0x438 / 0x48c / 0x490  ShipArchetype / LocationId / SystemId =
    //               +0x264 / +0x3f8 / +0x3e0
    //   node+0x580  MissionLog      = +0x390 (FUN_06d58e10 erase, FUN_06d57420 insert)
    //   node+0x4c4  StartingRing    = +0x414
    //   node+0x4e0  ComCostume      = +0x28c (counted copy)
    //   node+0x514  Voice           = +0x2c4 (capped at 0x1f)
    //   node+0x538  BaseCostume     = +0x2e4 (counted copy)
    //   node+0x58c  Interface       = +0x39c
    //   node+0x4cc  Relations: cleared, then one push_back per Vibe::Get relation
    // The relation array is then freed on both the SP and the MP path.
    if (!SinglePlayer()) {
        PlayerData::FSavedCharacter& Record =
            saved_characters[selected_character];
        Record.HullStatus = relative_health;
        Record.Equipment.Items = equipment.Items;
        Record.EquipIdMaker = equip_id_maker;
        Record.CollisionGroups = collision_groups;
        Record.Money = cash;
        Record.NumKills = num_kills;
        Record.NumMissionSuccesses = num_mission_successes;
        Record.NumMissionFailures = num_mission_failures;
        Record.Rank = Rank;
        Record.Visits = visit_map;
        Record.WeaponGroups = weapon_group_name;
        Record.BaseId = current_base;
        Record.LastBaseId = last_base_id;
        Record.Position.x = SavedPos.x;
        Record.Position.y = SavedPos.y;
        Record.Position.z = SavedPos.z;
        for (int I = 0; I < 9; ++I)
            Record.Orientation.E[I / 3][I % 3] = SavedOrient.E[I / 3][I % 3];
        Record.ShipArchetype = ship_archetype;
        Record.LocationId = current_location;
        Record.SystemId = system_id;
        Record.MissionLog = mission_log;
        Record.StartingRing = starting_ring;

        Server_CopyCostumeCounted(Record.ComCostume, com_costume);
        {
            // TString<32>::operator=(char const*) inlined; its null arm cannot fire
            // (the source is an inline buffer).
            size_t VoiceLen = strlen(voice);
            if (VoiceLen >= 0x20)
                VoiceLen = 0x1f;
            memcpy(Record.Voice.Data, voice, VoiceLen);
            Record.Voice.Data[VoiceLen] = '\0';
            Record.Voice.Length = static_cast<unsigned>(VoiceLen);
        }
        Server_CopyCostumeCounted(Record.BaseCostume, costume);
        Record.Interface = interface_state;

        Record.Relations.clear();
        for (unsigned int I = 0; I < static_cast<unsigned int>(RelationCount); ++I)
            Record.Relations.push_back(Relations[I]);
    }

    operator delete(Relations);                                            // .text:06d4e92a
    return true;
}

// ===========================================================================
// PlayerData's equipment-list family -- retail FUN_06d4ec10 / FUN_06d4eeb0 / FUN_06d4efc0 /
// FUN_06d4f100 / FUN_06d4f170 / FUN_06d4fc30, thiscall members of PlayerData spelled as free
// functions (declared in common_server_playerdb.h). Four of the six open with the MP gate
// `!IsMPServer() || selected_character[0] != 0`.
// ===========================================================================
bool PlayerData::equip_gate_open() const {
    if (!IsMPServer())
        return true;
    return selected_character.is_valid();
}

// FUN_06d4ec10. Slot+0x26c is the hull's undamaged collision group list
// (Archetype::EqObj::get_undamaged_collision_group_list, IAT .rdata:06d644a0).
void PlayerData::set_ship_arch(unsigned int ShipArch) {
    // A non-zero archetype that does not resolve aborts the whole call, notifications
    // included.
    if (ShipArch != 0 && Archetype::GetShip(ShipArch) == nullptr)
        return;

    // Only when the archetype changes: the list is emptied (even when the archetype will
    // not resolve below), then refilled.
    if (ship_archetype != ShipArch) {
        collision_groups.clear();

        // The low 30 bits, not the whole dword.
        if ((ShipArch & 0x3fffffffu) != 0) {
            Archetype::Ship* const Arch = Archetype::GetShip(ShipArch);
            if (Arch != nullptr) {
                Arch->get_undamaged_collision_group_list(collision_groups);
            } else {
                FDUMP(kFdumpError, "%s(%d) : *** ERROR: Error retrieving ship type %u",
                      "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xd07, ShipArch);
            }
        }
    }

    // The store and both notifications are unconditional.
    ship_archetype = ShipArch;

    // FLPACKET_SERVER_SETSHIPARCH; no null test on the interface.
    ::g_pClientInterface->SendServerSetShipArch(client_id, ShipArch);
    // Message 0x35 here, NOT the 0 every other member of this family broadcasts.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerShipArchSet, &client_id);
}

void PlayerData::modify_equipment(unsigned short Id, CacheString const& Hardpoint, int Count,
                                  float Health, unsigned char bEquipped) {
    if (!equip_gate_open())
        return;

    EquipDesc* Item = equipment.find_equipment_item(Id);
    if (Item == nullptr) {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: PlayerData::modify_item(): CLI(%d) ID(0x%X) NEW_STATUS(%f). "
              "Couldn't find item, expect strange inventory behavior",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xdd7, client_id,
              (unsigned int)Id, (double)Health);
        return;
    }

    // Four setters; no set_arch_id here.
    Item->set_hardpoint(Hardpoint);
    Item->set_count(Count);
    Item->set_status(Health);
    Item->set_equipped(bEquipped);

    // No IClient packet on this path, only the controller broadcast.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &client_id);
}

// PlayerData::set_relative_health -- retail FUN_06d4ed10 (thiscall, two stack arguments).
// +0x268 is relative_health and the first argument is a FLOAT: the arch-group damage
// walker (.text:06d0ba6a) FSTPs CSimple::get_relative_health straight into its slot.
void PlayerData::set_relative_health(float RelativeHealth, bool bNotify) {
    relative_health = RelativeHealth;
    // IClient slot 22; the health crosses as its raw bits.
    if (bNotify) {
        ::g_pClientInterface->SendServerSetHullStatus(client_id, RelativeHealth);
    }
}

// FUN_06d4eeb0.
void PlayerData::load_equipment(EquipDescList const& NewList) {
    if (!equip_gate_open())
        return;

    equipment = NewList;

    // Every element that arrived carrying SUBOBJ_ID_NONE gets a fresh id.
    for (std::list<EquipDesc>::iterator It = equipment.Items.begin();
         It != equipment.Items.end(); ++It) {
        if (It->Id == SUBOBJ_ID_NONE) {
            It->set_id(next_equip_id());
        }
    }

    // FLPACKET_SERVER_SETEQUIPMENT.
    EquipDescVector Wire(equipment);
    ::g_pClientInterface->SendServerSetEquipment(client_id, Wire);
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &client_id);
}

// SubObjectID::EquipIdMaker::CreateEquipID (Common.dll 0xd4560) PRE-increments, so the
// first id after a Reset (0x21) is 0x22.
unsigned short PlayerData::next_equip_id() {
    return equip_id_maker.CreateEquipID();
}

unsigned short PlayerData::add_equipment(unsigned int ArchId, CacheString const& Hardpoint,
                                         int Count, float Health, unsigned char bEquipped,
                                         bool bMission, bool bNotify) {
    if (!equip_gate_open())
        return SUBOBJ_ID_NONE;

    unsigned short EquipId = 0;
    bool bCombine = false;

    if (arch_is_combinable(ArchId) && !bMission) {
        // Retail sets the combinable flag BEFORE it knows whether a stack was found, so a
        // miss still adds combinable; it just gets a fresh id.
        bCombine = true;
        EquipDesc const* Existing = equipment.find_matching_cargo(ArchId, 0, 1.0f);
        if (Existing != nullptr) {
            EquipId = Existing->Id;
        } else {
            EquipId = next_equip_id();
        }
    } else {
        EquipId = next_equip_id();
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

    int const Combined = equipment.add_equipment_item(Desc, bCombine);

    // FLPACKET_SERVER_SETADDITEM. The client id goes BY VALUE here.
    if (bNotify) {
        ::g_pClientInterface->SendServerSetAddItem(client_id, Combined, Desc);
    }

    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &client_id);
    return EquipId;
}

void PlayerData::remove_equipment(unsigned short Id, int Count, bool bNotify) {
    if (!equip_gate_open())
        return;

    int const Result = equipment.remove_equipment_item(Id, Count);

    // FLPACKET_SERVER_SETREMOVEITEM.
    if (bNotify) {
        ::g_pClientInterface->SendServerSetRemoveItem(client_id, Result, Id, Count);
    }

    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &client_id);
}

// PlayerData::remove_cargo -- retail FUN_06d4f260 (thiscall): find the matching cargo (a
// miss FDUMPs and returns), then FUN_06d4f100's body inlined.
void PlayerData::remove_cargo(unsigned int ArchId, int Count, float Health, bool bNotify) {
    EquipDesc const* Found = equipment.find_matching_cargo(ArchId, Count, Health);
    if (Found == nullptr) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xdea,
              "Tried to remove cargo player doesn't have!");
        return;
    }
    remove_equipment(Found->Id, Count, bNotify);
}

// PlayerData::set_collision_groups -- retail FUN_06d4f320. The only writer of +0x26C other
// than set_ship_arch. No gate (its caller has one) and no controller broadcast.
void PlayerData::set_collision_groups(std::list<CollisionGroupDesc> const& Groups) {
    if (&collision_groups == &Groups)
        return;
    collision_groups = Groups;
    ::g_pClientInterface->SendServerSetCollisionGroups(client_id, collision_groups);
}

// PlayerData::get_cash -- retail FUN_06d4f460: clamped on the way out, never written back.
int PlayerData::get_cash() const {
    int Cash = cash;
    if (Cash < 0)
        return 0;
    if (Cash > 999999999)
        Cash = 999999999;
    return Cash;
}

// PlayerData::set_cash -- retail FUN_06d4f480 (thiscall); the MP selected-character gate,
// then clamp and store. Caller: IServerImpl::ReqSetCash.
void PlayerData::set_cash(int Cash) {
    if (IsMPServer() && !selected_character.is_valid())
        return;
    if (Cash < 0)
        Cash = 0;
    else if (Cash > 999999999)
        Cash = 999999999;
    cash = Cash;
    ::g_pClientInterface->SendServerSetCash(client_id, Cash);
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &client_id);
}

// PlayerData::adjust_cash -- retail FUN_06d4f4e0 (thiscall). total_earned is bumped BEFORE
// the MP gate. pub::Player::AdjustCash (0x06d553c0) inlines it with true.
void PlayerData::adjust_cash(int Amount, bool bCountEarned) {
    if (bCountEarned)
        total_earned += Amount;
    int NewCash = cash + Amount;
    if (IsMPServer() && !selected_character.is_valid())
        return;
    if (NewCash < 0)
        NewCash = 0;
    else if (NewCash > 999999999)
        NewCash = 999999999;
    cash = NewCash;
    ::g_pClientInterface->SendServerSetCash(client_id, NewCash);
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &client_id);
}

// PlayerData::reset_total_earned -- retail FUN_06d4f560. Sole caller: IServerImpl::BaseEnter,
// MP only, right before PlayerData::snapshot_equipment.
void PlayerData::reset_total_earned() {
    total_earned = PlayerData_TotalEquipmentValue(*this);
}

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

} // namespace

void PlayerData::report_cheat(int Reason) {
    // (1) resolve the character name off the reputation vibe.
    const wchar_t* CharacterNameWide = GCheatBadName;
    {
        FmtStr NameA;
        FmtStr NameB;
        const wchar_t* Resolved = GCheatBadName;
        int Result = ::Reputation::Vibe::GetName(rep_vibe, NameA, NameB, Resolved);
        // Retail only re-forces the fallback on -2; any other failure code
        // leaves whatever GetName wrote into the out-parameter in place.
        CharacterNameWide = (Result == -2) ? GCheatBadName : Resolved;
    }

    const std::string CharacterName = NarrowCopy(CharacterNameWide);
    const std::string AccountName = NarrowCopy(this->AccountName);

    // Retail indexes the reason table with no range clamp.
    const char* ReasonText = GCheatReason[Reason];

    FDUMP(kFdumpError, "LOG: Possible cheating detected (%s) by %s (%s)!",
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
            int const Result = ::Reputation::Vibe::GetName(rep_vibe, NameC, NameD, NodeName);
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

// FUN_06d4fc30.
void PlayerData::snapshot_equipment() {
    equip_snapshot_arch = ship_archetype;
    equip_snapshot = equipment;
}

// PlayerData::set_trade_request -- retail FUN_06d50920, called only from
// IServerImpl::RequestTrade. FUN_06d50a40(true) inlined, then -- only when the TARGET slot
// has a client (no range check on the id) -- MY client id appended to its inbound list,
// +0x3d0 = target and IClient slot 107.
void PlayerData::set_trade_request(unsigned int TargetId) {
    cancel_trade_request(true);

    PlayerData& Target = Players[TargetId];
    if (Target.client_id == 0)
        return;

    Target.inbound_trade_requests.push_back(client_id);
    trade_request_partner = TargetId;
    ::g_pClientInterface->SendCommonPlayerTradeRequestTrade(TargetId, ship_object_id);
}

// PlayerData::cancel_trade_request -- retail FUN_06d50a40 (thiscall, ONE stack argument;
// Ghidra's `char unaff_BL` is an artifact). Tells the OUTBOUND partner (+0x3d0) the
// request is gone (IClient slot 108), optionally removes MY client id from the partner's
// inbound list, and clears +0x3d0.
void PlayerData::cancel_trade_request(bool bDropPartnerEntry) {
    if (trade_request_partner == 0)
        return;

    // MY ship object id, not the partner's.
    ::g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(trade_request_partner,
                                                                ship_object_id);

    if (bDropPartnerEntry) {
        PlayerSlot* const Partner = PlayerDB_GetPlayer(trade_request_partner);
        if (Partner != nullptr) {
            std::vector<unsigned int>& Requests = Partner->inbound_trade_requests;
            for (std::vector<unsigned int>::iterator It = Requests.begin();
                 It != Requests.end(); ) {
                if (*It == client_id)
                    It = Requests.erase(It);
                else
                    ++It;
            }
        }
    }

    trade_request_partner = 0;
}

// PlayerData::terminate_trade -- retail FUN_06d50af0 (thiscall, no arguments); four call
// sites, one of them StarSystem::on_object_destroyed.
void PlayerData::terminate_trade() {
    if (trade_offer == nullptr)
        return;
    GetServerInterface()->TerminateTrade(client_id, 0);
}

// CiWString and std::wstring differ only in their traits type, so the few helpers that
// take a std::wstring get a copy.
static std::wstring CiToWide(const CiWString& Str) {
    return std::wstring(Str.c_str(), Str.size());
}

// PlayerDB::PlayerDB -- retail RVA 0x70B10, ordinal 15. ServerID starts at ZERO.
PlayerDB::PlayerDB() {
    m_PlayerArray = nullptr;
    m_MaxPlayers = 0;
    m_ServerID = 0;
    m_bAllowNewPlayers = false;
    m_PlayerArrayEnd = nullptr;
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

// PlayerData::~PlayerData -- retail FUN_06d50e90: the slot teardown FUN_06d49230, then the
// member destruction.
PlayerData::~PlayerData() {
    teardown();
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
        FDUMP(kFdumpNotice, "LOG: Delete account %S \"%s\"\n", NamePtr, AccountDir);
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

// Account/character hashes use Server_AccountCrc (server_playerdb.cpp, server_savefile.h):
// retail's CRC engine DAT_06d8fd38 -- width 31, poly 0x8005, reflected, XorIn/XorOut 0 --
// NOT the standard CRC-32, formatted "%02x-%08x" (length, hash).

// The account "name" file is obfuscated with a CONSTANT key: the low 16 bits of the CRC of
// the wide marker at .rdata:06d6ae80 (PlayerDB::CreateAccount, load_user_data,
// CAccount::InitFromFolder). Unlike the password file, it is reversible.
static const wchar_t* const GAccountNameFileKeySource = L"AKL!8167fGBA0712";

unsigned short AccountNameFileXorKey() {
    return (unsigned short)Server_AccountCrc(
        reinterpret_cast<const unsigned char*>(GAccountNameFileKeySource),
        (unsigned int)(wcslen(GAccountNameFileKeySource) * sizeof(wchar_t)));
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
        FDUMP(kFdumpNotice, "LOG: Create account %S \"%s\"\n", NamePtr, AccountDir);
    }

    // operator new(0x38); InitFromFolder fills the name from the "name" file just written.
    CAccount* NewAccount = new CAccount();
    NewAccount->InitFromFolder(AccountDir);

    // Keyed by the ACCOUNT's own name.
    m_AccountNameMap[NewAccount->m_Name] = NewAccount;

    return true;
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

// FindAccountFromCharacterID -- RVA 0x72AE0, export 125.
CAccount* PlayerDB::FindAccountFromCharacterID(CiString& CharId) {
    std::map<CiString, CAccount*>::iterator It =
        m_CharacterIDMap.find(CharId);
    if (It != m_CharacterIDMap.end()) {
        return It->second;
    }
    return nullptr;
}

CAccount* PlayerDB::FindAccountFromClientID(unsigned int ClientId) {
    // Retail (0x06d52b40) returns slot[ClientId-1].account_ptr with NO bounds check; the
    // port's range test is an addition.
    if (ClientId == 0) return nullptr;
    if (ClientId > m_MaxPlayers || !m_PlayerArray) return nullptr;
    return reinterpret_cast<CAccount*>(m_PlayerArray[ClientId - 1].account_ptr);
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

// PlayerDB::login -- retail RVA 0x732F0, ordinal 501: 1 when the account is already logged
// in (wcsicmp on AccountName at slot+0x00), else load_user_data.
unsigned char PlayerDB::login(const SLoginInfo& Info, unsigned int SessionId) {
    const wchar_t* LoginName = Info.AccountName;
    if (!m_PlayerArray) return 2;

    PlayerData* Current = traverse_active(nullptr);
    while (Current != nullptr) {
        if (_wcsicmp(Current->AccountName, LoginName) == 0) {
            return kLoginAlreadyLoggedIn;
        }
        Current = traverse_active(Current);
    }

    // Not already logged in — proceed with load_user_data
    return load_user_data(Info, SessionId);
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

    return m_PlayerArray[SessionId - 1].login_account(AccountDir, Info,
                                                      SessionId);
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
        FDUMP(kFdumpWarning,
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

    if (!Player->create_new_character(Info)) {
        return false;
    }

    // The NAME, onto the account's list.
    Account->m_Characters.push_back(CiWString(Info.Name));

    m_CharacterIDMap[CharKey] = Account;

    return true;
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

void PlayerDB::logout(unsigned int SessionId) {
    if (SessionId == 0 || SessionId > m_MaxPlayers || !m_PlayerArray) return;
    PlayerData& Slot = m_PlayerArray[SessionId - 1];

    // (1) MP only: FUN_06d4c430, the save-game writer (PlayerData::auto_save,
    //     server_savegame.cpp) -- flush this session's character to disk before tearing
    //     the slot down. Its own gates decide whether anything is written.
    if (!SinglePlayer()) {
        Slot.auto_save();
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
        FDUMP(kFdumpNotice, "LOG: Logout on account %S (\"%s\")\n",
              Slot.AccountName, Account->m_DirectoryPath.c_str());
    }

    // (10) The slot teardown; clearing client_id is what frees the slot for reuse.
    Slot.teardown();
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

// Retail 0x06d54640. On an MP server a session that has not selected a character has no
// active name. The name is FUN_06d4eda0's: resolved through the reputation vibe, aliasing
// the vibe's own buffer (or the static fallback).
const wchar_t* PlayerDB::GetActiveCharacterName(unsigned int SessionId) const {
    if (SessionId == 0 || SessionId > m_MaxPlayers || !m_PlayerArray) return nullptr;
    const PlayerData& Slot = m_PlayerArray[SessionId - 1];
    if (Slot.client_id == 0) return nullptr;
    if (IsMPServer() && !Slot.selected_character.is_valid()) return nullptr;
    return Slot.get_character_name();
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

// Players -- `?Players@@3VPlayerDB@@A`, ordinal 352, .data:06d90260. The port's only player
// table. The ctor only nulls pointers and default-constructs containers, and nothing
// touches the table before IServerImpl::Startup calls Players.init().
SERVER_API PlayerDB Players;
