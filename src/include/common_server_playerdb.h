// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_server_types.h"   // CHARACTER_ID (PlayerData::selected_character)
#include "common_goods.h"            // EquipDescList (PlayerData::equipment, +0x278)
#include "common_archetype.h"        // SubObjectID::EquipIdMaker (+0x3C8), CollisionGroupDesc
#include "common_text.h"             // FmtStr (PlayerData::mission_log, +0x390)

#include <cstddef>
#include <list>
#include <map>
#include <string>
#include <vector>

class CPlayerTradeOffer;   // common_tradeoffer.h (PlayerData::trade_offer, +0x3CC)
class Vector;              // common_ini.h (PlayerData::check_position_bound)
class CCharacterSet;       // server_characterset.h (PlayerData::build_character_set)

// ---------------------------------------------------------------------------
// PlayerData — retail's `struct PlayerData` (0x418 bytes, stride of the Server.dll
// `Players` array). This header is the only definition; do not re-declare it in a .cpp.
//
// Retail keeps one player table in Server.dll: the exported `PlayerDB Players`
// (.data:06d90260, ordinal 352) whose first member is a pointer to the heap array that
// PlayerDB::init (0x06d510c0) allocates (count dword, then MaxPlayers*0x418 bytes). The
// port's single store lives in PlayerDB (src/Server/server_playerdb.cpp); `PlayerSlot`
// below is a compatibility alias.
//
// Offsets in the comments are retail PlayerData offsets in the two bases Server.dll uses:
//   * `+0xNNN` — from the slot base, as PlayerData methods address `this`.
//   * `-0xNN`  — from the end of the slot, as the array walkers address it
//                (`IMUL EAX,idx,0x418` then `[EAX + Players - 0xNN]`); == +(0x418 - 0xNN).
// The port does not reproduce the retail byte layout: retail embeds VC6 std::string /
// std::list members, and this struct uses modern STL in a port-native order.
// ---------------------------------------------------------------------------
struct PlayerData {
    // FUN_06d494f0 -- the element constructor PlayerDB::init runs over every slot.
    // Defined in src/Server/server_playerdb.cpp.
    PlayerData();
    // FUN_06d50e90 -- the element destructor PlayerDB::free runs. Its first act is the
    // slot teardown FUN_06d49230 (teardown); the member destructors follow.
    ~PlayerData();

    // Retail has no "active" flag or in-room / in-base flags: occupancy is client_id
    // (+0x348) != 0 (PlayerDB::is_valid 0x06ce1a00) and the room / base tests are the
    // masked ids themselves (`TEST [slot-0x20],0x3fffffff`).
    unsigned int current_location;     // -0x20 (+0x3F8): location ID
    unsigned int current_base;         // -0x2c (+0x3EC): base ID

    // Ship/character state.
    // Cash is an int: PlayerData::AdjustCash (0x06d4f4e0) adds plain dwords and clamps
    // against 999999999.
    int cash;                          // -0xfc (+0x31c): cash, clamped [0, 999999999]
    unsigned int system_id;            // -0x38 (+0x3E0): current system
    unsigned int ship_archetype;       // -0x1b4 (+0x264): ship archetype ID

    // +0x3F4: the base the session entered through IServerImpl::BaseEnter.
    //   Written: BaseEnter (= base id), BaseExit, the internal-login clear FUN_06d48e80,
    //     the slot teardown and the slot ctor (= 0). CharacterSelect and PlayerDB::cleanup's
    //     undock (FUN_06d18880) leave it alone.
    //   Read: BaseEnter (`& 0x3fffffff` must be 0), BaseExit (!= 0, then == base), and the
    //     MP gate of ReqShipArch, ReqHullStatus, ReqCollisionGroups, ReqEquipment,
    //     ReqAddItem, ReqRemoveItem, ReqModifyItem, ReqSetCash and ReqChangeCash:
    //     `SinglePlayer() || dword != 0` (unmasked test).
    unsigned int entered_base_id;      // +0x3F4 (-0x24): base entered via BaseEnter, 0 = none

    // The ship FLID is ship_object_id (+0x3E4).

    // Initial spawn position/orientation (SetInitialPos/SetInitialOrnt). The ctor and the
    // teardown store a zero Vector and a 3x3 identity.
    float initial_pos[3];              // -0xc8 (+0x350): initial position vector
    float initial_ornt[9];             // -0xbc (+0x35C): initial orientation matrix (3x3)

    // ------------------------------------------------------------------
    // +0x278: the session's EquipDescList -- mounted equipment and cargo alike. Server.dll
    // calls Common's EquipDescList members on [slot-0x1a0] (get_cargo_space_occupied,
    // find_matching_cargo, add/remove/find_equipment_item) and the slot reset runs its
    // ctor. Only the internal list node layout differs from retail (VC6 node = {_Next,
    // _Prev, value}); nothing outside Common.dll touches it. VC6 node-to-field mapping:
    //   node+0x0a -> EquipDesc::Id        node+0x0c -> EquipDesc::ArchId
    //   node+0x10 -> EquipDesc::Hardpoint node+0x14 -> EquipDesc::bEquipped
    //   node+0x18 -> EquipDesc::Health    node+0x1c -> EquipDesc::Count
    // ------------------------------------------------------------------
    EquipDescList equipment;           // +0x278

    // ------------------------------------------------------------------
    // +0x26C: the hull's undamaged collision groups, a VC6 std::list. Only writer:
    // PlayerData::set_ship_arch (.text:06d4ec10), which clears it and refills it through
    // EqObj::get_undamaged_collision_group_list(std::list<CollisionGroupDesc>&).
    // ------------------------------------------------------------------
    std::list<CollisionGroupDesc> collision_groups;   // +0x26C

    // +0x324 / +0x328: the "what the player had when they last landed" snapshot, written by
    // FUN_06d4fc30 (PlayerData::snapshot_equipment): ship_archetype, then a list assignment
    // from +0x278.
    unsigned int equip_snapshot_arch;  // +0x324: ship_archetype at snapshot time
    EquipDescList equip_snapshot;      // +0x328: equipment at snapshot time

    // +0x2E4: the session Costume. pub::Player::SetMonkey / SetRobot write PartIds
    // directly; SetTrent passes the same address to CostumeDescriptions::get_costume.
    Costume costume;                   // +0x2E4

    // +0x28C: the second Costume, saved under the "com_" key prefix (com_body, com_head,
    // ...); the save serializer calls the costume writer FUN_06d46930 for +0x28C with
    // "com_" and for +0x2E4 with "".
    Costume com_costume;               // +0x28C

    // +0x2C4: the `voice` nickname the save file records, an inline char buffer (retail
    // passes its address to the "%s = %s\n" fprintf); 0x20 bytes up to +0x2E4.
    char voice[0x20];                  // +0x2C4

    // +0x2C0: the Length half of the TString<32> whose Data is `voice`; CharacterSelect's
    // record copy stores it.
    unsigned int voice_length;         // +0x2C0

    // Rank & difficulty (retail offsets +0x284, +0x3C4)
    unsigned int rank_level;           // -0x194: player rank level
    int money_needed_to_next_rank;     // -0x190: cash needed for next rank
    float difficulty_scale;            // +0x3C4: clamped [0.0, 2.0]

    // Stats
    int num_kills;                     // -0xe4: kill count
    int num_mission_successes;         // -0xe0: mission success count
    int num_mission_failures;          // -0xdc: mission failure count

    // Reputation
    int rep_vibe;                      // -0x100: reputation vibe handle

    // Health. The ctor and the teardown store 0 here, not 1.0f.
    float relative_health;             // -0x1b0 (+0x268): normalized hull health (0..1)

    // Group pointer (CPlayerGroup*)
    void* group_ptr;                   // -0x14: CPlayerGroup* (nullptr if no group)

    // Group mission state. Retail keeps it PER PLAYER, not on CPlayerGroup:
    // CPlayerGroup::GetMissionID (0x06d5c330) walks its member set and returns
    // the first non-zero [PlayerData+0x408]; GetMissionSetBy (0x06d5c3b0) does
    // the same with +0x40c; SetMissionID (0x06d5c430) writes both on every
    // member and then fires IClient slot 114 (+0x1c8).
    unsigned int group_mission_id;     // -0x10 (+0x408): mission id shared by the group
    unsigned int group_mission_set_by; // -0x0c (+0x40c): who set it

    // Running total of cash awarded. PlayerData::AdjustCash (FUN_06d4f4e0) adds the delta
    // here first when its bool argument is true, then clamps cash to [0, 999999999].
    int total_earned;                  // +0x320

    // Retail's slot-occupied gate: a client id (not an IClient*; the IClient is the global
    // DAT_06d8fb14), passed by address to IClient methods and tested for zero by
    // PlayerDB::is_valid (0x06ce1a00).
    unsigned int client_id;            // -0xd0 (+0x348): client id, 0 = slot free

    // +0x34C: "a cheat has already been reported for this session" latch; the cheat report
    // path bails out when set and sets it after PlayerData::report_cheat.
    unsigned char bCheatReported;      // -0xcc (+0x34C)

    // +0x340: one-shot "skip the next autosave" byte. Set by FUN_06d4c5f0 and by
    // pub::Player::DisplayMissionMessage for a type-0 message; the autosave FUN_06d4c430
    // clears it instead of saving when set (or when the base forbids autosaving);
    // CharacterSelect clears it.
    unsigned char bSkipNextAutosave;   // +0x340

    // +0x344: failed-save counter, incremented by FUN_06d4c800 when FUN_06d4ccd0 fails;
    // CharacterSelect zeroes it.
    unsigned int save_failure_count;   // +0x344

    // +0x414: the save record's `starting_ring` (record+0x2b8), copied by CharacterSelect: the
    // ring solar's nickname id. Retail treats it as an ID_String (Server_GetLaunchPosition calls
    // ID_String::IsEmpty on it; the slot reset stores CreateID("NO WHERE")); held as its one dword.
    unsigned int starting_ring;        // +0x414  ID_String

    // +0x64: the CHARACTER_ID this session has selected (CharacterSelect worker FUN_06d4a5d0
    // assigns it with CHARACTER_ID::operator=; slot init/reset and deselect store "").
    // The gates `!IsMPServer() || selected_character[0] != 0` read it as "single player,
    // or a character has been picked" (e.g. PlayerDB::GetActiveCharacterName 0x06d54684).
    CHARACTER_ID selected_character;   // +0x64: "" = none selected

    // Weapon group (retail offset +0x380)
    std::string weapon_group_name;     // +0x380: weapon group assignment string

    // Interface state (retail offset +0x39C)
    unsigned int interface_state;      // +0x39C: UI/interface state flag

    // -----------------------------------------------------------------------
    // Mission log: a VC6 std::list at +0x390 (head node +0x394, size +0x398) of FmtStr.
    // The save writer FUN_06d4cc20 emits each entry as
    //   MadLibId, ValCount, { Data, TypeTag, Flags } x ValCount
    // recursing into a nested record for TypeTag 0x21; the loader FUN_06d4ca60 rebuilds it
    // with FmtStr::begin_mad_lib / append / end_mad_lib.
    // -----------------------------------------------------------------------
    std::list<FmtStr> mission_log;     // +0x390

    // Visit tracking, two containers:
    //   +0x3A0  std::map<unsigned int, unsigned char>  -- the persistent table
    //   +0x3B8  std::vector<pair<unsigned int, unsigned char>> -- the pending queue
    // The flush FUN_06d4b450 (PlayerData::flush_pending_visits) ORs `entry.flags | 8` into the
    // map's byte for each pending key (inserting when absent) and empties the vector; bit 3
    // marks "committed". FUN_06d4b520 packs the map (dword key + byte flags per entry, after
    // a dword count) and hands it to IClient vtable +0x170.
    struct FVisitedState {
        unsigned int system_id;
        unsigned char visit_flags;
    };
    std::map<unsigned int, unsigned char> visit_map;  // +0x3A0: committed visit flags
    std::vector<FVisitedState> visited_states;        // +0x3B8: pending, flushed by FUN_06d4b450

    // +0x3C8: the per-player counter that hands out sub-object ids for the equipment on
    // this player's ship; the slot teardown calls EquipIdMaker::Reset on logout.
    SubObjectID::EquipIdMaker equip_id_maker; // +0x3C8

    // Trade state
    // +0x3CC: the live trade offer (common_tradeoffer.h). IServerImpl::InitiateTrade
    // allocates one per side; TerminateTrade deletes them through vtable[0](1).
    CPlayerTradeOffer* trade_offer;
    unsigned int trade_request_partner; // +0x3D0: pending OUTBOUND trade request, partner session id
    // +0x3D4: retail std::list of session ids with an outstanding trade request pointed at
    // this player. The slot teardown clears each requester's +0x3D0, notifies it through
    // IClient slot 108 and erases the list.
    std::vector<unsigned int> inbound_trade_requests; // +0x3D4
    unsigned int ship_object_id;       // +0x3E4 (-0x34): current ship FLID, 0 when docked

    // +0x3F0: the base the save file records as `last_base`, distinct from current_base.
    // BaseEnter stores the entered base here. The save serializer refills current_base
    // from it when the ship id is 0 and current_base has no object bits, and resolves
    // `last_base` from it.
    unsigned int last_base_id;         // +0x3F0: the base `last_base` names

    // Account pointer (retail offset -0x18)
    void* account_ptr;                 // -0x18 (+0x400): CAccount* for this player

    // +0x00: the login account name; the internal-login helper FUN_06d48e80 wcscpy's it into
    // the slot base. 0x50 bytes (40 wide characters): +0x50 is the saved-characters map,
    // and the slot reset fills exactly 0x14 dwords here with 0xcdcdcdcd.
    wchar_t AccountName[40];           // +0x00: login account name (0x50 bytes)

    // ------------------------------------------------------------------
    // The saved characters found in this player's account directory: the map at +0x50
    // keyed by CHARACTER_ID (the `.fl` file name). FUN_06d48d50 inserts the key, hands the
    // record to the save-file parser FUN_06d47120 and erases it again when the parse fails.
    // Login does not apply any of this to the live state: FUN_06d48e80 runs the whole scan
    // and then zeroes +0x3E0..+0x3FC.
    //
    // The value is the whole retail record (0x398 bytes; node 0x5a8 = 0xc links + 0x200 key
    // + record), filled by FUN_06d47120 and copied onto the live slot by FUN_06d4a5d0;
    // defaults from the record ctor FUN_06d46ab0. Offsets are record offsets; only order and
    // values are retail's. "(ctor: untouched)" fields are left to the per-[Player] reset.
    // ------------------------------------------------------------------
    struct FSavedCharacter {
        char FileName[0x200];                        // +0x000 ctor "" ; FUN_06d48d50 safe_strcpy's the file name here
        std::wstring Name;                           // +0x200 `name`, HexToWide'd
        std::wstring Description;                    // +0x210 `description`, HexToWide'd
        unsigned int DescripStrId;                   // +0x220 `descrip_strid`
        unsigned int TimestampLow;                   // +0x224 `tstamp` value 1
        unsigned int TimestampHigh;                  // +0x228 `tstamp` value 0
        unsigned int ShipArchetype;                  // +0x22c `ship_archetype`
        int Money;                                   // +0x230 `money` (ftol of a float)
        int NumKills;                                // +0x234
        int NumMissionSuccesses;                     // +0x238
        int NumMissionFailures;                      // +0x23c
        float HullStatus;                            // +0x240 ctor 1.0f
        // +0x244 / +0x260 are Common.dll EquipDescLists: the record's EH funclets destroy
        // them with the imported EquipDescList::~EquipDescList.
        EquipDescList Equipment;                     // +0x244 `equip` + `cargo`
        std::list<CollisionGroupDesc> CollisionGroups;     // +0x250
        float BaseHullStatus;                        // +0x25c ctor 1.0f
        EquipDescList BaseEquipment;                 // +0x260 `base_equip` + `base_cargo`
        std::list<CollisionGroupDesc> BaseCollisionGroups; // +0x26c
        unsigned int BaseId;                         // +0x278
        unsigned int LastBaseId;                     // +0x27c
        unsigned int LocationId;                     // +0x280
        unsigned int SystemId;                       // +0x284
        float Position[3];                           // +0x288 (ctor: untouched)
        float Orientation[3][3];                     // +0x294 (ctor: untouched)
        unsigned int StartingRing;                   // +0x2b8 an ID_String; ctor ID_String::clear
        unsigned int Rank;                           // +0x2bc
        std::vector< ::Reputation::Relation> Relations; // +0x2c0 `house` / `group` / `initial_rep`
        unsigned int RepGroup;                       // +0x2d0 ctor 0xffffffff
        Costume ComCostume;                          // +0x2d4 the `com_*` keys
        TString<32> Voice;                           // +0x308
        Costume BaseCostume;                         // +0x32c the bare costume keys
        SubObjectID::EquipIdMaker EquipIdMaker;      // +0x360 ctor 0x21 (Common.dll 0xd4580)
        std::string WeaponGroups;                    // +0x364 `wg` lines, verbatim
        std::list<FmtStr> MissionLog;                // +0x374 `log`
        unsigned int Interface;                      // +0x380 (ctor: untouched)
        std::map<unsigned int, unsigned char> Visits; // +0x384 `visit`

        // FUN_06d46ab0. What retail leaves uninitialised (Position, Orientation,
        // Interface, accessory slots past the count) is zeroed here.
        FSavedCharacter()
            : Name(), Description(), DescripStrId(0), TimestampLow(0), TimestampHigh(0),
              ShipArchetype(0), Money(0), NumKills(0), NumMissionSuccesses(0),
              NumMissionFailures(0), HullStatus(1.0f), Equipment(), CollisionGroups(),
              BaseHullStatus(1.0f), BaseEquipment(), BaseCollisionGroups(), BaseId(0),
              LastBaseId(0), LocationId(0), SystemId(0), StartingRing(0), Rank(0),
              Relations(), RepGroup(0xffffffffu), EquipIdMaker(), WeaponGroups(),
              MissionLog(), Interface(0), Visits() {
            FileName[0] = '\0';
            Position[0] = 0.0f;
            Position[1] = 0.0f;
            Position[2] = 0.0f;
            for (int Row = 0; Row < 3; ++Row)
                for (int Col = 0; Col < 3; ++Col)
                    Orientation[Row][Col] = 0.0f;
            ComCostume = Costume();
            BaseCostume = Costume();
            Voice.Length = 0;
            Voice.Data[0] = '\0';
        }
    };
    // Keyed by CHARACTER_ID, i.e. ordered by Common.dll's operator< (stricmp,
    // Common.dll 0xce740) -- which is why retail never lowercases the key.
    std::map<CHARACTER_ID, FSavedCharacter> saved_characters; // +0x50

    // +0x3E8: the ship id IServerImpl::RequestCreateShip reserved for the session (the
    // masked CFLIDMaker id; RequestCreateShip answers with it when already set);
    // PlayerLaunch requires it non-zero and equal to its argument. Zeroed by the login
    // clear, the slot teardown, the initializer and CharacterSelect.
    unsigned int created_ship_id;      // +0x3E8: ship id reserved by RequestCreateShip
    // +0x3FC: what pub::Player::GetCharacter (0x74c70) reads; every Server.dll write stores
    // zero, so its meaning is unrecovered.
    unsigned int character_id;         // +0x3FC: returned by pub::Player::GetCharacter (FLSDK: characterId)

    // +0x410: the base the session last left. BaseExit copies current_base here before
    // clearing it; BaseEnter treats a non-zero value that differs from the entered base as
    // a system change (events 0x3a / 0x3b); PlayerLaunch puts it in event 6's payload.
    // Reset by the internal-login clear, the slot teardown and CharacterSelect.
    unsigned int exited_base_id;       // +0x410: base left by the last BaseExit

    // ---------------------------------------------------------------------------
    // Member functions. Retail has these as thiscall members of PlayerData; none of them
    // is exported, so the names are the port's, in the style of Server.dll's exported ones.
    // ---------------------------------------------------------------------------

    // FUN_06d4efc0. Appends one EquipDesc to the slot's list and returns the sub-object id it
    // was given (SUBOBJ_ID_NONE when the MP gate refuses).
    //   bMission -- stored in EquipDesc::bMission (+0x18). When clear and
    //     arch_is_combinable(ArchId), reuses the Id of an existing stack found with
    //     find_matching_cargo(ArchId, 0, 1.0f); otherwise mints a fresh id from
    //     equip_id_maker. Retail then reuses that stack slot for the combinable flag it
    //     passes to add_equipment_item (1 on the combining path, 0 otherwise).
    //   bNotify -- fires IClient slot 26 (+0x68) with (client_id, add_equipment_item's
    //     return, &Desc).
    unsigned short add_equipment(unsigned int ArchId, CacheString const& Hardpoint, int Count,
                                 float Health, unsigned char bEquipped, bool bMission,
                                 bool bNotify);

    // FUN_06d4f100. remove_equipment_item(Id, Count); IClient slot 27 (+0x6c) with
    // (client_id, result, Id, Count) when bNotify.
    void remove_equipment(unsigned short Id, int Count, bool bNotify);

    // FUN_06d4f260. find_matching_cargo(ArchId, Count, Health), FDUMP on a miss, else
    // PlayerData::remove_equipment(hit->Id, Count, bNotify).
    void remove_cargo(unsigned int ArchId, int Count, float Health, bool bNotify);

    // FUN_06d4f170. find_equipment_item(Id) then set_hardpoint/set_count/set_status/
    // set_equipped; FDUMPs when the id is absent.
    void modify_equipment(unsigned short Id, CacheString const& Hardpoint, int Count, float Health,
                          unsigned char bEquipped);

    // FUN_06d4fc30. No MP gate.
    void snapshot_equipment();

    // FUN_06d4f560, FUN_06d4f460, FUN_06d4f480, FUN_06d4f4e0: +0x320 = total equipment
    // value; cash read with the [0, 999999999] clamp; cash set / adjusted with the MP gate,
    // the clamp, IClient SETCASH (slot 77) and controller message 0.
    void reset_total_earned();
    int get_cash() const;
    void set_cash(int Cash);
    void adjust_cash(int Amount, bool bCountEarned);

    // FUN_06d498f0: the MP position bound SPObjUpdate applies. Retail takes three stack
    // arguments but reads only the position. Returns false after destroying the ship when the
    // position is out of bounds.
    bool check_position_bound(Vector const* Position);

    // FUN_06d4a200, PlayerData::reload_ship_data by its FDUMP text. Puts the ship back to the
    // selected character's last-base loadout; the save serializer calls it for a dead MP ship.
    void reload_ship_data();

    // FUN_06d4f430. Sets Health on the collision group GroupId in the list at +0x26C;
    // silently does nothing when the id is absent. Only caller: StarSystem::damage_object.
    void set_collision_group_health(unsigned short GroupId, float Health);

    // FUN_06d4ed10. Stores the relative hull health at +0x268 and, when bNotify, fires
    // IClient slot 22 (+0x58) with (client_id, the same dword). No MP gate.
    void set_relative_health(float RelativeHealth, bool bNotify);

    // FUN_06d50af0. With a live trade offer at +0x3CC, calls
    // IServerImpl::TerminateTrade(client_id, 0) on the `Server` singleton.
    void terminate_trade();

    // FUN_06d50a40 (one stack argument). Cancels this session's outbound trade request:
    // notifies the partner through IClient slot 108 (+0x1b0), optionally removes this session
    // from the partner's inbound list, and clears +0x3D0.
    void cancel_trade_request(bool bDropPartnerEntry);

    // FUN_06d50920. Cancels any outbound request (FUN_06d50a40 with the bool set, inlined),
    // then -- only when the target's client id is non-zero -- adds this session to the
    // target's inbound list, sets +0x3D0 = TargetId and notifies the target through IClient
    // slot 107 (+0x1ac).
    void set_trade_request(unsigned int TargetId);

    // FUN_06d4f320. Replaces the list at +0x26C wholesale and reports it with IClient
    // slot 23 (+0x5c). No gate of its own.
    void set_collision_groups(std::list<CollisionGroupDesc> const& Groups);

    // FUN_06d4ec10. Validates the archetype and, when it differs from the current one,
    // refills +0x26c with the hull's undamaged collision groups.
    void set_ship_arch(unsigned int ShipArch);

    // FUN_06d4eeb0. Replaces the slot's whole equipment list with NewList, minting a fresh
    // EquipIdMaker id for every element that carries SUBOBJ_ID_NONE.
    void load_equipment(EquipDescList const& NewList);

    // The prologue of FUN_06d4ccd0. True when a cheat was detected, in which case retail's
    // save refuses to run.
    bool check_for_cheats();

    // FUN_06d4b450. Drains the pending-visit queue (+0x3B8) into the committed map (+0x3A0),
    // ORing 8 into every entry's flags. FUN_06d4ccd0 inlines the same drain before
    // serializing the map.
    void flush_pending_visits();

    // FUN_06d4b200. Folds the committed byte for Key into Flags and answers false only when
    // both 0x10 and 0x08 are then set.
    bool should_queue_visit(unsigned int Key, unsigned char Flags) const;

    // FUN_06d4b270. Appends {Key, Flags} to the pending-visit vector.
    void queue_visit(unsigned int Key, unsigned char Flags);

    // FUN_06d4ccd0 (server_playerdb.cpp). FileName is a base file name, not a path: the function
    // builds the destination from GetUserDataPath, "Accts" / "Accts\MultiPlayer" and the
    // account directory, and appends ".fl" in single player. False when the cheat gate fires
    // or the file cannot be opened.
    bool serialize_to_file(char const* FileName, wchar_t const* Description,
                           unsigned int DescripStrId);

    // Retail FUN_06d47120 (thiscall on the PlayerData, `RET 8`). Parses the
    // character file at Path into Record. Returns true when a [Player] section was
    // seen (the byte set at .text:06d472ea).
    bool load_save_record(char const* Path, FSavedCharacter& Record);

    // Retail FUN_06d48d50 (thiscall, `RET 8`): parse <Dir>\<FileName> into
    // saved_characters[FileName]; on failure warn and erase the entry.
    bool load_saved_character(char const* Dir, char const* FileName);

    // Retail FUN_06d4b5a0 (thiscall, `RET 4`): re-read a file the slot has just
    // written into its own record container.
    void reload_saved_character(char const* FileName);

    // Retail FUN_06d4c800 (thiscall, `RET 0xc`). Name "" picks a free "Save%04x".
    bool save_game(char const* Name, wchar_t const* Description, unsigned int DescripStrId);

    // Retail FUN_06d4c600 (thiscall, `RET 4`). Returns CopyFileA's result, or false
    // when the save itself failed.
    bool mission_save(unsigned int DescripStrId);

    // Retail FUN_06d4c430 (thiscall, `RET`).
    bool auto_save();

    // Retail FUN_06d4c5f0: `MOV byte ptr [ECX + 0x340],0x1 / RET`.
    void skip_next_autosave();

    // The record -> live slot copy of retail FUN_06d4a5d0 (.text:06d4ac77 ..
    // .text:06d4b089), including the rep-vibe rebuild. SessionId is the worker's
    // own stack argument (.text:06d4acef `MOV ECX,[ESP + 0x192c]`).
    void apply_saved_character(FSavedCharacter const& Record, unsigned int SessionId);

    // Retail FUN_06d4b520 (thiscall, `RET 4`): the visit map packed as {count,
    // {u32 id, u8 flags}...} into an operator-new block sent by IClient +0x170.
    void send_visited_state(unsigned int SessionId);

    // Retail FUN_06d50740 (thiscall, `RET 4`): the mission log flattened by
    // AppendMissionLogData into an operator-new block sent by IClient +0x184.
    void send_mission_log(unsigned int SessionId);

    // The tail of retail FUN_06d4a5d0 (.text:06d4aa85 .. .text:06d4b1fa), which the
    // SP and MP paths share: re-open the record's file, the SP [Time] / feelings
    // load (FUN_06d44810), the record copy, the visited / weapon-group / mission-log
    // / interface packets and controller event 5. Returns true (.text:06d4b1e2).
    bool load_selected_character(FSavedCharacter const& Record, unsigned int SessionId);

    // Retail FUN_06d4b790 (thiscall, `RET 4`), called only by
    // PlayerDB::create_new_character (.text:06d541a0): expands the exe-side
    // NewCharacter.fl / MPNewCharacter.fl template into
    // <account dir>\<FUN_06d46370(Info.Name)>.fl, FLS1-encodes it, and on success
    // reloads it into saved_characters through FUN_06d4b5a0.
    bool create_new_character(SCreateCharacterInfo const& Info);

    // Retail FUN_06d4a4a0 (thiscall, `RET 4`) -- MP only: leave the world
    // (system, base, trade, events 0x3a / 8 / 7, group logout, player-list
    // removal) and clear +0x64. Defined in server_playerdb.cpp, next to logout.
    void deselect_character(unsigned int SessionId);

    // Retail FUN_06d4a5d0 (thiscall, `RET 8`), IServerImpl::CharacterSelect's worker:
    // find the record (miss -> false); in MP leave the world if a character is
    // selected, drop from the player lists when the id changes, assign +0x64,
    // IClient +0x1c0 and the reputation / player-list exchange with every session;
    // then the shared tail PlayerData::load_selected_character. Defined in
    // server_playerdb.cpp, next to PlayerData::deselect_character.
    bool select_character(CHARACTER_ID const& Id, unsigned int SessionId);

    // Retail FUN_06d499c0 (thiscall, `RET 4`), IServerImpl::CharacterInfoReq's
    // worker (.text:06d452aa): in MP with a selected character, autosave and leave
    // the world; then one CCharacterSet::CharacterInfo per saved character, sorted
    // Restart.fl first, AutoSave.fl next, then newest first; the failed-save
    // counter moves into Out.SaveFailureCount. Defined in server_playerdb.cpp.
    void build_character_set(CCharacterSet& Out);

    // Retail FUN_06d49f60 (thiscall, `RET 8`), IServerImpl::DestroyCharacter's
    // worker: deselect if selected, find the record, refuse Restart.fl, drop the
    // character from Players' id map and its account, unlink the file, erase the
    // record, FDUMP in MP. Defined in server_playerdb.cpp.
    bool destroy_character(CHARACTER_ID const& Id, unsigned int SessionId);

    // FUN_06d48d20: outside single player, IClient::PushToClient(client_id, a null packet).
    // Defined in server_playerdb.cpp.
    void send_client_null_packet();

    // FUN_06d4eda0: the character's name from its reputation vibe, or the ">>>BAD   NAME<<<"
    // fallback. Defined in server_chat.cpp.
    const wchar_t* get_character_name() const;

    // FUN_06d4a1e0: true in single player, otherwise whether a character is selected. Defined
    // in server_chat.cpp.
    bool has_selected_character() const;

    // The gate four of the equipment functions open with: single player, or a selected
    // character. Inlined in retail.
    bool equip_gate_open() const;

    // FUN_06d48e80; only caller PlayerDB::load_user_data. Defined in server_playerdb.cpp.
    unsigned char login_account(const char* AccountDir, const SLoginInfo& Info,
                                unsigned int SessionId);

    // equip_id_maker.CreateEquipID(); inlined in retail.
    unsigned short next_equip_id();

    // FUN_06d4f570; its only caller is the cheat gate of the save. Defined in
    // server_playerdb.cpp.
    void report_cheat(int Reason);

    // FUN_06d49230: the slot teardown the destructor runs first. Defined in server_playerdb.cpp.
    void teardown();
};

// Compatibility alias used by many call sites. The real type name is PlayerData, which
// retail's mangled names carry (`??APlayerDB@@QAEAAUPlayerData@@ABI@Z`), so the alias must
// never become the real type name.
typedef PlayerData PlayerSlot;

// The layout is not retail's (see the struct comment), so only the cash width is pinned.
static_assert(sizeof(((PlayerData*)nullptr)->cash) == 4, "PlayerData::cash must stay a 4-byte int");

// Common.dll exports for Server.dll (C++ linkage to match COMMON_API)
#ifdef COMMON_EXPORTS
#define PLAYERDB_API __declspec(dllexport)
#else
#define PLAYERDB_API __declspec(dllimport)
#endif

// Port-only Server.dll-internal forwarder onto the single `Players` object (retail call
// sites inline `Players.operator[]`). Defined in src/Server/server_classes.cpp.
PlayerSlot* __cdecl PlayerDB_GetPlayer(unsigned int session_id);

unsigned int __cdecl PlayerDB_GetMaxPlayers();
bool __cdecl PlayerDB_AllowsNewPlayers();
