// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_savegame.cpp -- Server.dll's character-file family: the parser that
// reads a saved character into the slot's record container, the login-scan and
// post-save wrappers around it, the three save wrappers that end in the
// serializer, and the record -> live-slot copy CharacterSelect runs.
//
// Retail never parses onto the live slot: it parses into the 0x398-byte record in the
// map at PlayerData+0x50 (PlayerData::FSavedCharacter), keyed by file name, and
// CharacterSelect copies that record onto the slot later.
//
// FUN_06d4a5d0's head and MP-only front (.text:06d4a652 .. .text:06d4aa85) are
// PlayerData::select_character (server_playerdb.cpp); its tail is
// PlayerData::load_selected_character below.

#include "../include/common_pub.h"
#include "../include/common_server.h"          // Players (PlayerDB::operator[])
#include "../include/common_server_playerdb.h"
#include "../include/server_savegame.h"
#include "../include/server_savefile.h"        // FLS1, account paths, last saved path
#include "../include/common_net.h"             // IsMPServer
#include "../include/common_universe.h"
#include "../include/common_archetype.h"
#include "../include/common_cobject.h"         // SubObjectID::EquipIdMaker
#include "../include/common_goods.h"           // Loadout::ReadEquipLine / ReadCargoLine
#include "../include/common_ini.h"             // INI_Reader, StringAlloc, RefreshLoadingProgress
#include "../include/common_misc.h"            // HexToWide, safe_strcpy
#include "../include/common_id.h"              // CreateID, MakeLocationID, ID_String
#include "../include/common_csys.h"            // EulerMatrix
#include "../include/common_reputation.h"      // Reputation::*, CostumeDescriptions
#include "../include/common_text.h"            // FmtStr
#include "../include/common_newchar.h"         // NewChar::TheDB
#include "../include/common_server_types.h"    // SCreateCharacterInfo
#include "../include/dacom.h"                  // FDUMP
#include "../include/rpc_iclient.h"            // IClient slots 91 / 92 / 97 / 98
#include "../include/msvcrt_compat.h"          // MsvcrtAtoi (retail MSVCRT.DLL atoi wraps)

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <ctime>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

// IServerImpl.cpp: the rpclocal client interface (DAT_06d8fb14) and the single-player
// [Time] / feelings load (FUN_06d44810). The controller broadcast (FUN_06d43650,
// Server_ControllerDispatch) is declared in common_server_controller.h.
extern IClient* g_pClientInterface;
void Server_LoadSinglePlayerState(INI_Reader& Reader);

namespace {

// .rdata:06d6b280 -- the __FILE__ every FDUMP in this family passes.
char const* const PlayerDbSource = "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp";

// ---------------------------------------------------------------------------
// FUN_06d03cf0. Every caller stores the result as a TString length.
// ---------------------------------------------------------------------------
unsigned int CopyBoundedString(char* Dst, unsigned int Size, char const* Src) {
    if (Src == nullptr)
        return 0;
    unsigned int Length = static_cast<unsigned int>(strlen(Src));
    if (!(Length < Size))
        Length = Size - 1;
    memcpy(Dst, Src, Length);
    Dst[Length] = '\0';
    return Length;
}

// FSaveFileBuffer (a SaveGame with a closing destructor) and SaveGame's methods
// (FUN_06d48960 ReadTail, FUN_06d48b60 Encode) are in server_savefile.h/.cpp.

} // namespace

namespace {

// The {Costume*, CostumeDescriptions const*} pair FUN_06d47120 keeps on its frame for
// each of its two costumes (record+0x2d4, record+0x32c).
struct FCostumeTarget {
    Costume* Target;
    CostumeDescriptions const* Descriptions;

    // FUN_06d48c80.
    unsigned int find_part_id(int TypeId, char const* Name, char const* File) const;

    // FUN_06d48cd0.
    unsigned int find_accessory_id(char const* Name, char const* File) const;
};

// ---------------------------------------------------------------------------
// FUN_06d48c80 (thiscall on FCostumeTarget). A miss or a part whose Id is 0 both
// warn and return 0.
// ---------------------------------------------------------------------------
unsigned int FCostumeTarget::find_part_id(int TypeId, char const* Name, char const* File) const {
    unsigned int Id = 0;
    bodypart const* Part = Descriptions->find_bodypart(TypeId, Name);
    if (Part != nullptr)
        Id = Part->Id;
    if (Id == 0) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: invalid costume part (%s) in (%s)\n",
              PlayerDbSource, 0x25a, Name, File);
    }
    return Id;
}

// ---------------------------------------------------------------------------
// FUN_06d48cd0 (thiscall on FCostumeTarget). Same shape over find_accessory.
// ---------------------------------------------------------------------------
unsigned int FCostumeTarget::find_accessory_id(char const* Name, char const* File) const {
    unsigned int Id = 0;
    accessory const* Accessory = Descriptions->find_accessory(Name);
    if (Accessory != nullptr)
        Id = Accessory->Id;
    if (Id == 0) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: invalid accessory (%s) in character (%s)\n",
              PlayerDbSource, 0x263, Name, File);
    }
    return Id;
}

// ---------------------------------------------------------------------------
// FUN_06d4ca60. One `log` line back into a FmtStr -- the inverse of the writer
// FUN_06d4cc20 ("%u, %u" then ", %u, %u, %u" per Val, recursing for tag 0x21).
// ---------------------------------------------------------------------------
FmtStr ReadMissionLogEntry(INI_Reader& Reader, unsigned int& Index) {
    unsigned int const MadLibId = static_cast<unsigned int>(Reader.get_value_int(Index++));
    int const Count = Reader.get_value_int(Index++);

    FmtStr Local;
    Local.begin_mad_lib(MadLibId);
    Local.end_mad_lib();

    for (int I = 0; I < Count; ++I) {
        int const Data = Reader.get_value_int(Index++);
        int const Tag = Reader.get_value_int(Index++);
        unsigned char const Flags = static_cast<unsigned char>(Reader.get_value_int(Index++));
        void* DataPointer = reinterpret_cast<void*>(static_cast<intptr_t>(Data));
        if (static_cast<short>(Tag) == 0x21) {
            // operator new(0x18); the Val below owns it (FmtStr::Val::~Val deletes a
            // tag-0x21 subtree).
            DataPointer = new FmtStr(ReadMissionLogEntry(Reader, Index));
        }
        FmtStr::Val* Node = new FmtStr::Val(static_cast<unsigned short>(Tag), DataPointer, Flags);
        Local.append(Node);
    }
    return FmtStr(Local);
}

// ---------------------------------------------------------------------------
// The Reputation::RepGroupCB FUN_06d47120 builds on its frame for `group` /
// `initial_rep`: +0x04 the record, +0x08 the player's own group id. Execute is
// LAB_06d46f10: a feeling of 1.0 or more (or unordered) is stored as 0.95f.
// ---------------------------------------------------------------------------
struct FRepGroupRelationsCB : public ::Reputation::RepGroupCB {
    PlayerData::FSavedCharacter* Record;
    unsigned int GroupId;

    char Execute(unsigned const* OtherGroupId) override {
        float Feeling = 0.0f;
        ::Reputation::get_feelings_towards(*OtherGroupId, GroupId, Feeling);
        if (!(Feeling < 1.0))
            Feeling = 0.95f;
        ::Reputation::Relation Entry;
        Entry.GroupId = *OtherGroupId;
        Entry.Feeling = Feeling;
        Record->Relations.push_back(Entry);
        return 1;
    }
};

// TString<16> from an INI string, the way every rep-group key builds it.
TString<16> MakeGroupName(char const* Text) {
    TString<16> Name;
    Name.Length = 0;
    Name.Data[0] = '\0';
    if (Text != nullptr)
        Name.Length = CopyBoundedString(Name.Data, 0x10, Text);
    return Name;
}

// The costume keys, one call per costume (com_ costume on the suffix, base costume on
// the whole name). Retail compares inline with the terminator counted: an exact,
// case-sensitive match, unlike the INI is_value keys. False when nothing matched.
bool LoadCostumeKey(INI_Reader& Reader, FCostumeTarget const& Holder, char const* Suffix,
                    char const* File) {
    if (strcmp(Suffix, "body") == 0) {
        Holder.Target->PartIds[1] =
            Holder.find_part_id(0, Reader.get_value_string(), File);
        return true;
    }
    if (strcmp(Suffix, "head") == 0) {
        Holder.Target->PartIds[0] =
            Holder.find_part_id(1, Reader.get_value_string(), File);
        return true;
    }
    if (strcmp(Suffix, "lefthand") == 0) {
        Holder.Target->PartIds[2] =
            Holder.find_part_id(2, Reader.get_value_string(), File);
        return true;
    }
    if (strcmp(Suffix, "righthand") == 0) {
        Holder.Target->PartIds[3] =
            Holder.find_part_id(3, Reader.get_value_string(), File);
        return true;
    }
    if (strcmp(Suffix, "hair") == 0) {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: hair is no longer a valid bodypart in character (%s)\n",
              PlayerDbSource, 0x286, File);
        return true;
    }
    if (strcmp(Suffix, "accessory") == 0) {
        unsigned int const Id =
            Holder.find_accessory_id(Reader.get_value_string(), File);
        Holder.Target->add_accessory(Id); // FUN_06d48940; an id of 0 (the miss above) is appended too
        return true;
    }
    if (strcmp(Suffix, "costume") == 0) {
        Holder.Descriptions->get_costume(Reader.get_value_string(), *Holder.Target);
        return true;
    }
    return false;
}

// `equip` / `base_equip` / `cargo` / `base_cargo`: the read result is ignored, and the
// id comes from the record's EquipIdMaker (record+0x360).
void LoadEquipKey(INI_Reader& Reader, PlayerData::FSavedCharacter& Record,
                  EquipDescList& List, bool bCargo) {
    List.Items.insert(List.Items.end(), EquipDesc());
    EquipDesc& Item = List.Items.back();
    if (bCargo)
        Loadout::ReadCargoLine(Reader, Item);
    else
        Loadout::ReadEquipLine(Reader, Item);
    Item.set_id(Record.EquipIdMaker.CreateEquipID());
}

// `collision_group` / `base_collision_group`.
void LoadCollisionGroupKey(INI_Reader& Reader, PlayerData::FSavedCharacter& Record,
                           std::list<CollisionGroupDesc>& Groups, int LineNotFound,
                           int LineBeforeShip) {
    CacheString Name;
    Name.Value = StringAlloc(Reader.get_value_string(0), false);
    if (Record.ShipArchetype == 0) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: collision_group %s specified BEFORE ship. Skipping.",
              PlayerDbSource, LineBeforeShip, Name.Value);
        return;
    }
    // No null test on GetShip.
    Archetype::Ship const* Ship = Archetype::GetShip(Record.ShipArchetype);
    Archetype::CollisionGroup const* Group = Ship->get_group_by_name(Name);
    if (Group == nullptr) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: collision_group(%s) not found on ship(%s). Skipping.",
              PlayerDbSource, LineNotFound, Name.Value, Ship->Nickname.get_string());
        return;
    }
    // Only an existing node is updated; no match adds nothing.
    for (std::list<CollisionGroupDesc>::iterator It = Groups.begin(); It != Groups.end(); ++It) {
        if (It->Id == Group->Id) {
            It->Health = (float)Reader.get_value_float(1);
            return;
        }
    }
}

// The account directory name: FUN_06d46370's "%02x-%08x" hash in MP, the
// narrowed wide account name in SP (wcstombs(buf, slot, 0x104)).
void BuildAccountDirName(char* Out, PlayerData const& Slot) {
    if (IsMPServer()) {
        Server_AccountDirName(Out, Slot.AccountName);
    } else {
        Out[0] = '\0';
        wcstombs(Out, Slot.AccountName, 0x104);
    }
}

// .text:06d4ce2b / .text:06d4b64a / .text:06d4c8f9 -- <accts>\<account dir>.
void BuildAccountDir(char* Out, PlayerData const& Slot) {
    char AccountDirName[0x104];
    BuildAccountDirName(AccountDirName, Slot);
    _makepath(Out, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);
}

} // namespace

// ===========================================================================
// PlayerData::load_save_record -- retail FUN_06d47120, thiscall on the PlayerData.
// ===========================================================================
bool PlayerData::load_save_record(char const* Path, PlayerData::FSavedCharacter& Record) {
    (void)*this; // only forwarded as the log reader's unused ECX

    bool bSawPlayer = false;
    FSaveFileBuffer Buffer;
    INI_Reader Reader;

    Buffer.File = fopen(Path, "rt");
    if (Buffer.File == nullptr)
        return false;
    char Magic[4] = { 0, 0, 0, 0 };
    fread(Magic, 1, 4, Buffer.File);
    if (memcmp(Magic, Server_FLS1Magic, 4) == 0) {
        // Encrypted: reopen binary, read from offset 4, decode in place.
        fclose(Buffer.File);
        Buffer.File = fopen(Path, "rb");
        if (Buffer.File == nullptr)
            return false;
        Buffer.ReadTail(4);
        Buffer.Encode();
    } else if (memcmp(Magic, "FLSG", 4) == 0) {
        // .rdata:06d6b304: refused; ~FSaveFileBuffer closes the file.
        return false;
    } else {
        // Plain text.
        Buffer.ReadTail(0);
    }

    Reader.open_memory(Buffer.Contents.c_str(),
                       static_cast<unsigned int>(Buffer.Contents.size()));

    FCostumeTarget ComCostume = { &Record.ComCostume, nullptr };
    FCostumeTarget BaseCostume = { &Record.BaseCostume, nullptr };

    while (Reader.read_header()) {
        RefreshLoadingProgress();
        int CommentDepth = 0;
        if (!Reader.is_header("Player"))
            continue;

        // ---- per-[Player] reset ----------------------------------------------
        Record.Position[0] = 0.0f;
        Record.Position[1] = 0.0f;
        Record.Position[2] = 0.0f;
        for (int Row = 0; Row < 3; ++Row)
            for (int Col = 0; Col < 3; ++Col)
                Record.Orientation[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        bSawPlayer = true;
        Record.SystemId = 0;
        Record.BaseId = 0;
        Record.LocationId = 0;             // +0x27c (last_base) is NOT reset
        Record.Description = L"";
        Record.DescripStrId = 0;
        Record.Money = 0x7b0;
        Record.Interface = 3;
        ComCostume.Descriptions = GetCostumeDescriptions();
        BaseCostume.Descriptions = GetCostumeDescriptions();
        Record.WeaponGroups.clear();
        Record.Visits.clear();

        while (Reader.read_value()) {
            // Block comments; the depth test is signed.
            if (Reader.is_value("/*"))
                ++CommentDepth;
            else if (Reader.is_value("*/"))
                --CommentDepth;
            if (CommentDepth > 0)
                continue;

            if (Reader.is_value("name")) {
                wchar_t Wide[0x400];
                HexToWide(Reader.get_value_string(), Wide);
                Record.Name.assign(Wide, wcslen(Wide));
                continue;
            }
            if (Reader.is_value("description")) {
                wchar_t Wide[0x400];
                HexToWide(Reader.get_value_string(), Wide);
                Record.Description.assign(Wide, wcslen(Wide));
                continue;
            }
            if (Reader.is_value("descrip_strid")) {
                Record.DescripStrId = static_cast<unsigned int>(Reader.get_value_int(0));
                continue;
            }
            if (Reader.is_value("tstamp")) {
                if (Reader.is_value_empty(0) || Reader.is_value_empty(1)) {
                    Record.TimestampHigh = 0;
                    Record.TimestampLow = 0;
                } else {
                    Record.TimestampHigh = static_cast<unsigned int>(Reader.get_value_int(0));
                    Record.TimestampLow = static_cast<unsigned int>(Reader.get_value_int(1));
                }
                continue;
            }
            if (Reader.is_value("ship_archetype")) {
                // MSVCRT.DLL atoi wraps mod 2^32, so a hash id above INT_MAX
                // round-trips; the UCRT atoi would clamp it to 0x7fffffff.
                Record.ShipArchetype =
                    static_cast<unsigned int>(MsvcrtAtoi(Reader.get_value_string()));
                if (Record.ShipArchetype == 0) {
                    ID_String Nickname;
                    Nickname.Id = CreateID(Reader.get_value_string());
                    Record.ShipArchetype = Archetype::GetShipID(Nickname);
                }
                // No null test on GetShip.
                Archetype::Ship const* Ship = Archetype::GetShip(Record.ShipArchetype);
                Ship->get_undamaged_collision_group_list(Record.CollisionGroups);
                Ship->get_undamaged_collision_group_list(Record.BaseCollisionGroups);
                continue;
            }
            if (Reader.is_value("system")) {
                Record.SystemId = Universe::get_system_id(Reader.get_value_string());
                if (Record.SystemId == 0) {
                    FDUMP(kFdumpError,
                          "%s(%d) : *** ERROR: game (%s) has invalid system name (%s) in player file (%s)\n",
                          PlayerDbSource, 0x376, Reader.get_file_name(),
                          Reader.get_value_string(), Path);
                }
                continue;
            }
            if (Reader.is_value("base")) {
                Record.BaseId = Universe::get_base_id(Reader.get_value_string());
                if (Record.BaseId == 0) {
                    FDUMP(kFdumpError,
                          "%s(%d) : *** ERROR: game (%s) has invalid base name (%s) in player file (%s)\n",
                          PlayerDbSource, 0x37e, Reader.get_file_name(),
                          Reader.get_value_string(), Path);
                }
                continue;
            }
            if (Reader.is_value("last_base")) {
                Record.LastBaseId = Universe::get_base_id(Reader.get_value_string());
                if (Record.LastBaseId == 0) {
                    FDUMP(kFdumpError,
                          "%s(%d) : *** ERROR: game (%s) has invalid last_base name (%s) in player file (%s)\n",
                          PlayerDbSource, 0x386, Reader.get_file_name(),
                          Reader.get_value_string(), Path);
                }
                continue;
            }
            if (Reader.is_value("location")) {
                Record.LocationId = static_cast<unsigned int>(Reader.get_value_int(0));
                continue;
            }
            if (Reader.is_value("rank")) {
                Record.Rank = static_cast<unsigned int>(Reader.get_value_int(0));
                continue;
            }
            if (Reader.is_value("group") || Reader.is_value("initial_rep")) {
                // Rebuild the relations from every group's feeling towards this one.
                TString<16> const GroupName = MakeGroupName(Reader.get_value_string());
                FRepGroupRelationsCB Callback;
                Callback.Record = &Record;
                Callback.GroupId = ::Reputation::get_id(GroupName);
                Record.Relations.clear();
                ::Reputation::enumerate(&Callback);
                continue;
            }
            if (Reader.is_value("rep_group")) {
                // An empty value resolves "".
                TString<16> const GroupName = Reader.is_value_empty(0)
                    ? MakeGroupName("")
                    : MakeGroupName(Reader.get_value_string(0));
                Record.RepGroup = ::Reputation::get_id(GroupName);
                continue;
            }
            if (Reader.is_value("house")) {
                float const Feeling = (float)Reader.get_value_float(0);
                TString<16> const GroupName = MakeGroupName(Reader.get_value_string(1));
                ::Reputation::Relation Entry;
                Entry.GroupId = ::Reputation::get_id(GroupName);
                Entry.Feeling = Feeling;
                Record.Relations.push_back(Entry);   // FUN_06d58b40
                continue;
            }
            if (Reader.is_value("voice")) {
                char const* Voice = Reader.get_value_string();
                if (Voice == nullptr) {
                    Record.Voice.Length = 0;
                    Record.Voice.Data[0] = '\0';
                } else {
                    Record.Voice.Length = CopyBoundedString(Record.Voice.Data, 0x20, Voice);
                }
                continue;
            }

            // The costume keys, on the raw key name.
            {
                char const* const KeyName = Reader.get_name_ptr();
                char const* const File = Reader.get_file_name();
                bool bHandled = false;
                if (strstr(KeyName, "com_") == KeyName)
                    bHandled = LoadCostumeKey(Reader, ComCostume, KeyName + strlen("com_"), File);
                if (!bHandled) {
                    // Retail fetches name and file again.
                    char const* const BareName = Reader.get_name_ptr();
                    bHandled = LoadCostumeKey(Reader, BaseCostume, BareName,
                                              Reader.get_file_name());
                }
                if (bHandled)
                    continue;
            }

            // MSVCRT ftol on the unrounded double: 64-bit truncation, low dword stored.
            if (Reader.is_value("money")) {
                Record.Money = (int)(long long)Reader.get_value_float(0);
            } else if (Reader.is_value("num_kills")) {
                Record.NumKills = (int)(long long)Reader.get_value_float(0);
            } else if (Reader.is_value("num_misn_successes")) {
                Record.NumMissionSuccesses = (int)(long long)Reader.get_value_float(0);
            } else if (Reader.is_value("num_misn_failures")) {
                Record.NumMissionFailures = (int)(long long)Reader.get_value_float(0);
            } else if (Reader.is_value("pos")) {
                Record.Position[0] = (float)Reader.get_value_float(0);
                Record.Position[1] = (float)Reader.get_value_float(1);
                Record.Position[2] = (float)Reader.get_value_float(2);
            } else if (Reader.is_value("rotate")) {
                Vector Angles;
                Angles.x = (float)Reader.get_value_float(0);
                Angles.y = (float)Reader.get_value_float(1);
                Angles.z = (float)Reader.get_value_float(2);
                Matrix const Rotation = EulerMatrix(Angles);
                memcpy(Record.Orientation, Rotation.E, sizeof(Record.Orientation));
            } else if (Reader.is_value("starting_ring")) {
                Record.StartingRing = CreateID(Reader.get_value_string());
            } else if (Reader.is_value("equip")) {
                LoadEquipKey(Reader, Record, Record.Equipment, false);
            } else if (Reader.is_value("base_equip")) {
                LoadEquipKey(Reader, Record, Record.BaseEquipment, false);
            } else if (Reader.is_value("cargo")) {
                LoadEquipKey(Reader, Record, Record.Equipment, true);
            } else if (Reader.is_value("base_cargo")) {
                LoadEquipKey(Reader, Record, Record.BaseEquipment, true);
            } else if (Reader.is_value("collision_group")) {
                LoadCollisionGroupKey(Reader, Record, Record.CollisionGroups, 0x41f, 0x424);
            } else if (Reader.is_value("base_collision_group")) {
                LoadCollisionGroupKey(Reader, Record, Record.BaseCollisionGroups, 0x444, 0x449);
            } else if (Reader.is_value("wg")) {
                // The whole raw line.
                Record.WeaponGroups.append(Reader.get_line_ptr());
                Record.WeaponGroups.append("\n");
            } else if (Reader.is_value("hull_status")) {
                Record.HullStatus = (float)Reader.get_value_float(0);
            } else if (Reader.is_value("base_hull_status")) {
                Record.BaseHullStatus = (float)Reader.get_value_float(0);
            } else if (Reader.is_value("visit")) {
                unsigned int const SystemId = static_cast<unsigned int>(Reader.get_value_int(0));
                unsigned char const Flags = static_cast<unsigned char>(Reader.get_value_int(1));
                Record.Visits.insert(std::make_pair(SystemId, static_cast<unsigned char>(0)))
                    .first->second = Flags;
            } else if (Reader.is_value("interface")) {
                Record.Interface = static_cast<unsigned int>(Reader.get_value_int(0));
            } else if (Reader.is_value("log")) {
                unsigned int LogIndex = 0;
                Record.MissionLog.push_back(ReadMissionLogEntry(Reader, LogIndex));
            } else {
                // `logid` is tested and the result dropped.
                Reader.is_value("logid");
            }
        }
    }

    // "Deck2" folds back to "Deck".
    if (MakeLocationID(Record.BaseId, "Deck2") == Record.LocationId)
        Record.LocationId = MakeLocationID(Record.BaseId, "Deck");

    // ~FSaveFileBuffer closes the file (retail's inlined FUN_06d48c20).
    return bSawPlayer;
}

// ===========================================================================
// PlayerData::load_saved_character -- retail FUN_06d48d50, thiscall.
// ===========================================================================
bool PlayerData::load_saved_character(char const* Dir, char const* FileName) {
    CHARACTER_ID Key;
    safe_strcpy(Key.Buffer, 0x200, FileName);
    PlayerData::FSavedCharacter& Record = saved_characters[Key];  // FUN_06d57090

    char Path[0x104];
    _makepath(Path, nullptr, Dir, FileName, nullptr);

    if (load_save_record(Path, Record)) {
        safe_strcpy(Record.FileName, 0x200, FileName);
        return true;
    }
    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Failed to load saved game %s",
          PlayerDbSource, 0x4a8, Path);
    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
        saved_characters.find(Key);                               // FUN_06d5a180
    if (It != saved_characters.end())
        saved_characters.erase(It);                               // FUN_06d57760
    return false;
}

// ===========================================================================
// PlayerData::reload_saved_character -- retail FUN_06d4b5a0, thiscall.
// ===========================================================================
void PlayerData::reload_saved_character(char const* FileName) {
    // No ".fl" extension in MP.
    char KeyName[0x104];
    _makepath(KeyName, nullptr, nullptr, FileName, IsMPServer() ? nullptr : "fl");

    char AccountDir[0x104];
    BuildAccountDir(AccountDir, *this);

    char Path[0x104];
    _makepath(Path, nullptr, AccountDir, KeyName, nullptr);

    CHARACTER_ID Key;
    safe_strcpy(Key.Buffer, 0x200, KeyName);
    PlayerData::FSavedCharacter& Record = saved_characters[Key];
    if (load_save_record(Path, Record)) {
        safe_strcpy(Record.FileName, 0x200, KeyName);
        return;
    }
    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
        saved_characters.find(Key);
    if (It != saved_characters.end())
        saved_characters.erase(It);
}

// ===========================================================================
// PlayerData::save_game -- retail FUN_06d4c800, thiscall.
// ===========================================================================
bool PlayerData::save_game(char const* Name, wchar_t const* Description, unsigned int DescripStrId) {
    std::string FileName;
    if (Name[0] != '\0') {
        // A fixed 0x200-byte copy, cut at the first '.'.
        char Copy[0x200];
        memcpy(Copy, Name, sizeof(Copy));
        char* Dot = strchr(Copy, '.');
        if (Dot != nullptr)
            *Dot = '\0';
        FileName.assign(Copy, strlen(Copy));
    } else {
        char AccountDir[0x104];
        BuildAccountDir(AccountDir, *this);

        // Up to 200 probes of <dir>\Save%04x.fl from rand() (extension kept even in
        // MP). A name is free when GetFileAttributesA fails with ERROR_FILE_NOT_FOUND.
        int Seed = rand();
        for (int Attempt = 1; ; ++Attempt) {
            char Candidate[0x20];
            sprintf(Candidate, "Save%04x", static_cast<int>(static_cast<short>(Seed))); // sign-extended
            char Probe[0x104];
            _makepath(Probe, nullptr, AccountDir, Candidate, "fl");
            if (GetFileAttributesA(Probe) == INVALID_FILE_ATTRIBUTES &&
                GetLastError() == ERROR_FILE_NOT_FOUND) {
                FileName.assign(Candidate, strlen(Candidate));
                break;
            }
            ++Seed;
            if (!(Attempt < 0xc8))
                break;                                                   // name stays ""
        }
    }

    if (serialize_to_file(FileName.c_str(), Description, DescripStrId)) {
        reload_saved_character(FileName.c_str());
        return true;
    }
    ++save_failure_count;
    return false;
}

// ===========================================================================
// PlayerData::mission_save -- retail FUN_06d4c600, thiscall.
// ===========================================================================
bool PlayerData::mission_save(unsigned int DescripStrId) {
    if (!save_game("", nullptr, DescripStrId))
        return false;

    // Retail inlines the account-name hash and rebuilds the accounts root into
    // DAT_06d90158, which is what Server_GetAccountsRoot returns.
    char AccountDirName[0x104];
    BuildAccountDirName(AccountDirName, *this);
    char AccountDir[0x104];
    _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);

    // Extension kept even in MP.
    char Destination[0x104];
    _makepath(Destination, nullptr, AccountDir, "AutoSave", "fl");

    // The just-written save is copied to AutoSave.fl.
    return CopyFileA(GServerLastSavePath, Destination, FALSE) != 0;
}

// ===========================================================================
// PlayerData::auto_save -- retail FUN_06d4c430.
// ===========================================================================
bool PlayerData::auto_save() {
    if (IsMPServer()) {
        if (IsMPServer() && !selected_character.is_valid())
            return false;
        if (system_id == 0)                                        // +0x3e0
            return false;
    }

    if (IsMPServer()) {
        // No end() test: CharacterSelect stores +0x64 only after its own find
        // succeeded, so a record always exists.
        std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
            saved_characters.find(selected_character);

        // "<date> <time>".
        char Time[0x10];
        char Date[0x10];
        wchar_t Description[0x200];
        _tzset();
        _strtime(Time);
        _strdate(Date);
        swprintf(Description, 0x200, L"%S %S", Date, Time);
        return serialize_to_file(It->second.FileName, Description, 0);
    }

    // Single player. Universe::IBase vt+0x54 is the port's IBase::bAutosaveForbidden.
    bool bAllowed = true;
    if ((current_base & 0x3fffffff) != 0) {
        Universe::IBase const* Base = Universe::get_base(current_base);
        if (Base->bAutosaveForbidden)
            bAllowed = false;
    }
    if (bSkipNextAutosave == 0 && bAllowed) {
        char Name[0x200];
        safe_strcpy(Name, 0x200, "AutoSave");
        return save_game(Name, nullptr, 0x65c);
    }
    bSkipNextAutosave = 0;
    return false;
}

// ===========================================================================
// PlayerData::skip_next_autosave -- retail FUN_06d4c5f0.
// ===========================================================================
void PlayerData::skip_next_autosave() {
    bSkipNextAutosave = 1;
}

// ===========================================================================
// PlayerData::apply_saved_character -- the record -> slot copy of retail
// FUN_06d4a5d0, .text:06d4ac77 .. .text:06d4b089.
// ===========================================================================
void PlayerData::apply_saved_character(PlayerData::FSavedCharacter const& Record,
                                       unsigned int SessionId) {
    current_base = Record.BaseId;                                  // +0x3ec
    last_base_id = Record.LastBaseId;                              // +0x3f0
    current_location = Record.LocationId;                          // +0x3f8
    if ((Record.BaseId & 0x3fffffff) != 0) {
        last_base_id = Record.BaseId;
        // IBase vt+0x10 is the port's IBase::SystemId.
        system_id = Universe::get_base(Record.BaseId)->SystemId;
    } else {
        system_id = Record.SystemId;
    }

    // The rep vibe is rebuilt.
    if (rep_vibe != -1) {
        ::Reputation::Vibe::Free(rep_vibe);
        rep_vibe = -1;
    }
    Server_VibeID_Alloc(reinterpret_cast<unsigned int*>(&rep_vibe)); // FUN_06d45b60
    ::Reputation::Vibe::Alloc(rep_vibe);
    ::Reputation::Vibe::SetClientID(rep_vibe, SessionId);
    FmtStr const EmptyName;
    ::Reputation::Vibe::Set(rep_vibe, Record.RepGroup, Record.Rank,
                            static_cast<unsigned char>(Record.Relations.size()),
                            Record.Relations.empty() ? nullptr : &Record.Relations[0],
                            EmptyName, EmptyName, Record.Name.c_str());

    // The com_ costume: four parts, the count, then only `count` accessory ids.
    for (int Part = 0; Part < 4; ++Part)
        com_costume.PartIds[Part] = Record.ComCostume.PartIds[Part];
    com_costume.AccessoryCount = Record.ComCostume.AccessoryCount;
    for (int I = 0; I < static_cast<int>(com_costume.AccessoryCount); ++I)
        com_costume.AccessoryIds[I] = Record.ComCostume.AccessoryIds[I];

    // slot+0x2c4 from record+0x30c.
    voice_length = CopyBoundedString(voice, 0x20, Record.Voice.Data);

    // The base costume, same shape.
    for (int Part = 0; Part < 4; ++Part)
        costume.PartIds[Part] = Record.BaseCostume.PartIds[Part];
    costume.AccessoryCount = Record.BaseCostume.AccessoryCount;
    for (int I = 0; I < static_cast<int>(costume.AccessoryCount); ++I)
        costume.AccessoryIds[I] = Record.BaseCostume.AccessoryIds[I];

    initial_pos.x = Record.Position[0];
    initial_pos.y = Record.Position[1];
    initial_pos.z = Record.Position[2];
    memcpy(&initial_ornt, Record.Orientation, sizeof(initial_ornt));

    ship_archetype = Record.ShipArchetype;                         // +0x264
    relative_health = Record.HullStatus;                           // +0x268
    equipment.Items = Record.Equipment.Items;
    collision_groups = Record.CollisionGroups;                     // +0x26c

    cash = Record.Money;                                           // +0x31c
    equip_id_maker = Record.EquipIdMaker;                          // +0x3c8
    num_kills = Record.NumKills;                                   // +0x334
    num_mission_successes = Record.NumMissionSuccesses;            // +0x338
    num_mission_failures = Record.NumMissionFailures;              // +0x33c
    save_failure_count = 0;                                        // +0x344
    current_location = 0;                                          // +0x3f8, after the copy above
    character_id = 0;
    ship_object_id = 0;                                            // +0x3e4
    created_ship_id = 0;                                           // +0x3e8
    exited_base_id = 0;                                            // +0x410
    bSkipNextAutosave = 0;                                        // +0x340
    starting_ring = Record.StartingRing;                           // +0x414
    rank_level = Record.Rank;                                      // +0x284

    weapon_group_name = Record.WeaponGroups;                       // +0x380
    visit_map = Record.Visits;                                     // +0x3a0
    mission_log = Record.MissionLog;                               // +0x390
    interface_state = Record.Interface;                            // +0x39c
}

// ===========================================================================
// PlayerData::send_visited_state -- retail FUN_06d4b520, thiscall on the slot.
// Block: dword count, then {dword system id, byte flags} per visit (5-byte stride).
// Sent through IClient +0x170 (slot 92); rpclocal's sender (0x06d8b540, ch1 op 0x13)
// keeps and frees the block.
// ===========================================================================
void PlayerData::send_visited_state(unsigned int SessionId) {
    unsigned int const Count = static_cast<unsigned int>(visit_map.size());
    unsigned int const Size = Count * 5 + 4;
    unsigned char* const Block = static_cast<unsigned char*>(::operator new(Size));
    memcpy(Block, &Count, 4);
    unsigned char* Cursor = Block + 4;
    for (std::map<unsigned int, unsigned char>::const_iterator It = visit_map.begin();
         It != visit_map.end(); ++It) {
        memcpy(Cursor, &It->first, 4);
        Cursor[4] = It->second;
        Cursor += 5;
    }
    g_pClientInterface->SendCommonSetVisitedState(SessionId, Block, Size);
}

namespace {

// FUN_06d50850 (thiscall on FmtStr): the serialized size of one log entry.
// FUN_06d50740 inlines the same loop.
int MissionLogEntrySize(FmtStr const& Entry) {
    int Size = 6;
    for (FmtStr::Val const* Node = Entry.Head; Node != nullptr; Node = Node->Next) {
        int const Tag = Node->TypeTag;
        if (Tag == 0x21)
            Size += MissionLogEntrySize(*Node->Subtree) + 3;
        else
            Size += (Tag == 0x4e) ? 0x17 : 7;
    }
    return Size;
}

} // namespace

// ===========================================================================
// PlayerData::send_mission_log -- retail FUN_06d50740, thiscall on the slot.
// Header {1, entry count (+0x398)}, then the entries. IClient +0x184 (slot 97) gets
// the running offset, not `Size`; rpclocal keeps and frees the block (sender
// 0x06d8b7f0).
// ===========================================================================
void PlayerData::send_mission_log(unsigned int SessionId) {
    int Size = 8;
    for (std::list<FmtStr>::const_iterator It = mission_log.begin();
         It != mission_log.end(); ++It)
        Size += MissionLogEntrySize(*It);

    unsigned char* const Block = static_cast<unsigned char*>(::operator new(static_cast<size_t>(Size)));
    unsigned int const Header[2] = { 1u, static_cast<unsigned int>(mission_log.size()) };
    memcpy(Block, Header, sizeof(Header));
    unsigned char* Cursor = Block + 8;
    int Offset = 8;
    for (std::list<FmtStr>::const_iterator It = mission_log.begin();
         It != mission_log.end(); ++It)
        AppendMissionLogData(&*It, Cursor, Offset, Size);
    g_pClientInterface->SendCommonSetMissionLog(SessionId, Block, static_cast<unsigned int>(Offset));
}

// ===========================================================================
// PlayerData::load_selected_character -- the second half of retail FUN_06d4a5d0,
// .text:06d4aa85 .. .text:06d4b1fa, where the SP and MP paths join. The record is
// the found map node + 0x20c.
// ===========================================================================
bool PlayerData::load_selected_character(PlayerData::FSavedCharacter const& Record,
                                         unsigned int SessionId) {
    // <accts>\<account dir>\<record file name>.
    char AccountDir[0x104];
    BuildAccountDir(AccountDir, *this);
    char Path[0x104];
    _makepath(Path, nullptr, AccountDir, Record.FileName, nullptr);

    // The file is read again with the same FLS1 / FLSG / plain three-way as
    // FUN_06d47120. open_memory runs on every path, over an empty buffer when fopen
    // failed or the file is FLSG.
    FSaveFileBuffer Buffer;
    INI_Reader Reader;
    Buffer.File = fopen(Path, "rt");
    if (Buffer.File != nullptr) {
        char Magic[4] = { 0, 0, 0, 0 };
        fread(Magic, 1, 4, Buffer.File);
        if (memcmp(Magic, Server_FLS1Magic, 4) == 0) {
            fclose(Buffer.File);
            Buffer.File = fopen(Path, "rb");
            if (Buffer.File != nullptr) {
                Buffer.ReadTail(4);
                Buffer.Encode();
            }
        } else if (memcmp(Magic, "FLSG", 4) != 0) {
            Buffer.ReadTail(0);
        }
    }
    Reader.open_memory(Buffer.Contents.c_str(),
                       static_cast<unsigned int>(Buffer.Contents.size()));

    // Single player only: [Time], feelings, controller event 0x40 (FUN_06d44810).
    if (SinglePlayer())
        Server_LoadSinglePlayerState(Reader);

    apply_saved_character(Record, SessionId);

    send_visited_state(SessionId);

    // IClient +0x16c (slot 91). rpclocal's sender leaves the not-owned flag clear
    // (payload+0x08 = 0), so the string's own buffer goes out as is.
    char const* const WeaponGroups = weapon_group_name.c_str();
    g_pClientInterface->SendCommonSetWeaponGroup(SessionId,
                                                 reinterpret_cast<const unsigned char*>(WeaponGroups),
                                                 static_cast<unsigned int>(strlen(WeaponGroups)));

    send_mission_log(SessionId);

    // A 4-byte block holding +0x39c, IClient +0x188 (slot 98); the packet takes it over.
    unsigned int* const InterfaceBlock = static_cast<unsigned int*>(::operator new(4));
    *InterfaceBlock = interface_state;
    g_pClientInterface->SendCommonSetInterfaceState(SessionId,
                                                    reinterpret_cast<unsigned char*>(InterfaceBlock), 4);

    // FUN_06d43650(5, ...): the character-loaded broadcast hands the controllers the
    // open reader.
    struct FCharacterLoadedEvent {
        unsigned int SessionId;
        INI_Reader* Reader;
    };
    FCharacterLoadedEvent Event;
    Event.SessionId = SessionId;
    Event.Reader = &Reader;
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLoad, &Event);

    if (Buffer.File != nullptr) {
        fclose(Buffer.File);
        Buffer.File = nullptr;
    }
    return true;
}

// ===========================================================================
// PlayerData::create_new_character -- retail FUN_06d4b790, thiscall on the PlayerData.
// Only caller: PlayerDB::create_new_character, .text:06d541a0. Writes
// <account dir>\<name hash>.fl from (MP)NewCharacter.fl with the %%...%% placeholders
// substituted from NewChar::TheDB.
// ===========================================================================
namespace {

// FUN_06d464f0: <accounts root>\<account dir> for an account name (the FUN_06d46370
// hash inlined in MP, wcstombs in SP).
void BuildAccountDirForName(char* Out, wchar_t const* AccountName) {
    char AccountDirName[0x104];
    if (IsMPServer())
        Server_AccountDirName(AccountDirName, AccountName);
    else
        wcstombs(AccountDirName, AccountName, 0x104);
    _makepath(Out, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);
}

} // namespace

bool PlayerData::create_new_character(SCreateCharacterInfo const& Info) {
    // Retail's `this == NULL` arm (empty directory) is unreachable: the only caller
    // passes &m_PlayerArray[SessionId - 1].
    char AccountDir[0x104];
    BuildAccountDirForName(AccountDir, AccountName);

    // The file name is the FUN_06d46370 hash of the new character's name, in SP too.
    char HashName[0x104];
    Server_AccountDirName(HashName, Info.Name);
    std::string FileName(HashName);
    FileName.append(".");
    FileName.append("fl");

    // Retail uses one buffer as both directory and destination.
    char Path[0x104];
    _makepath(Path, nullptr, AccountDir, HashName, "fl");
    _strlwr(HashName);

    // NewChar::TheDB find slots 0, 1, 3, 2 (Common.dll's Load, .text:063149d0, fills
    // [Faction], [Package], [Pilot] and every faction `base`); common_newchar.h
    // declares them in slot order.
    NewChar::IDatabase const* Database = NewChar::TheDB;
    FNewCharFaction const* Faction = Database->FindFaction(Info.FactionId);
    FNewCharPackage const* Package = Database->FindPackage(Info.PackageId);
    FNewCharExtra const* HomeBase = Database->FindExtra(Info.BaseId);
    FNewCharPilot const* Pilot = Database->FindPilot(Info.PilotId);

    // Only all four missing returns early with no file.
    if (Faction == nullptr && Package == nullptr && HomeBase == nullptr && Pilot == nullptr)
        return false;

    bool bResult = false;
    {
        SaveGame_Writer Writer;
        if (Writer.Open(Path)) {
            // The template beside the exe.
            char ModulePath[0x104];
            GetModuleFileNameA(nullptr, ModulePath, 0x104);
            char Drive[_MAX_DRIVE];
            char Dir[0x104];
            _splitpath(ModulePath, Drive, Dir, nullptr, nullptr);
            char TemplatePath[0x104];
            _makepath(TemplatePath, Drive, Dir,
                      SinglePlayer() ? "NewCharacter.fl" : "MPNewCharacter.fl", nullptr);

            FSaveFileBuffer Template;
            INI_Reader Reader;

            // The same FLS1 / FLSG / plain three-way FUN_06d47120 uses.
            bool bOpened = false;
            Template.File = fopen(TemplatePath, "rt");
            if (Template.File != nullptr) {
                char Magic[4] = { 0, 0, 0, 0 };
                fread(Magic, 1, 4, Template.File);
                if (memcmp(Magic, Server_FLS1Magic, 4) == 0) {
                    fclose(Template.File);
                    Template.File = fopen(TemplatePath, "rb");
                    if (Template.File != nullptr) {
                        Template.ReadTail(4);
                        Template.Encode();
                        bOpened = true;
                    }
                } else if (memcmp(Magic, "FLSG", 4) != 0) {
                    // Plain text. "FLSG" skips to the finalize, the file left open
                    // until teardown.
                    Template.ReadTail(0);
                    bOpened = true;
                }
            }

            if (bOpened) {
                Reader.open_memory(Template.Contents.c_str(),
                                   static_cast<unsigned int>(Template.Contents.size()));
                // Any one missing: result stays false, but the (empty) output is
                // still finalized below.
                if (Faction != nullptr && Package != nullptr && HomeBase != nullptr &&
                    Pilot != nullptr) {
                    FILE* const Out = Writer.File;
                    // Retail never clears this between keys: a key with no values
                    // prints whatever the previous key joined. Its first contents are
                    // stack garbage in retail; "" is the only defined choice.
                    char Joined[0x400];
                    Joined[0] = '\0';
                    while (Reader.read_header()) {
                        fprintf(Out, "\n[%s]\n", Reader.get_header_ptr());
                        while (Reader.read_value()) {
                            char const* const Key = Reader.get_name_ptr();
                            if (_stricmp(Key, "%%Package%%") == 0) {
                                // Ship, then the loadout (package +0x10 / +0x20).
                                fprintf(Out, "%s = %s\n", "ship_archetype", Package->Ship.c_str());
                                // No null test on Loadout::Get.
                                Loadout::Map const* Map =
                                    Loadout::Get(Loadout::GetID(Package->Loadout.c_str()));
                                for (std::vector<EquipDesc>::const_iterator It = Map->Items.begin();
                                     It != Map->Items.end(); ++It) {
                                    // An unknown archetype is skipped.
                                    Archetype::Equipment const* Equipment =
                                        Archetype::GetEquipment(It->ArchId);
                                    if (Equipment == nullptr)
                                        continue;
                                    char Line[0x100];
                                    if (It->bEquipped) {
                                        sprintf(Line, "%u, %s", Equipment->Nickname.Id, It->Hardpoint);
                                        fprintf(Out, "%s = %s\n", "equip", Line);
                                    } else {
                                        sprintf(Line, "%u, %d", Equipment->Nickname.Id, It->Count);
                                        fprintf(Out, "%s = %s\n", "cargo", Line);
                                    }
                                }
                                continue;
                            }

                            // Every value, substituted, joined with ", ".
                            int const Count = static_cast<int>(Reader.get_num_parameters());
                            for (int Index = 0; Index < Count; ++Index) {
                                if (Index == 0)
                                    Joined[0] = '\0';
                                else
                                    strcat(Joined, ", ");
                                char const* Value = Reader.get_value_string(static_cast<unsigned int>(Index));
                                char Scratch[0x400];
                                if (Value[0] == '%') {
                                    if (_stricmp(Value, "%%Name%%") == 0) {
                                        WideToHex(Info.Name, Scratch);
                                        Value = Scratch;
                                    } else if (_stricmp(Value, "%%Faction%%") == 0) {
                                        Value = Faction->RepGroup.c_str();        // faction +0x08
                                    } else if (_stricmp(Value, "%%Money%%") == 0) {
                                        sprintf(Scratch, "%u", static_cast<unsigned int>(Package->Money)); // package +0x2c
                                        Value = Scratch;
                                    } else if (_stricmp(Value, "%%Base_Costume%%") == 0) {
                                        Value = Pilot->Body.c_str();              // pilot +0x08
                                    } else if (_stricmp(Value, "%%Comm_Costume%%") == 0) {
                                        Value = Pilot->Comm.c_str();              // pilot +0x28
                                    } else if (_stricmp(Value, "%%Voice%%") == 0) {
                                        Value = Pilot->Voice.c_str();             // pilot +0x48
                                    } else if (_stricmp(Value, "%%Home_System%%") == 0) {
                                        // A missing base or system keeps the literal
                                        // placeholder.
                                        Universe::IBase const* Base = Universe::get_base(Info.BaseId);
                                        if (Base != nullptr) {
                                            // IBase vt+0x10 / ISystem vt+0x08 are the
                                            // port's SystemId / Nickname fields.
                                            Universe::ISystem const* System =
                                                Universe::get_system(Base->SystemId);
                                            if (System != nullptr)
                                                Value = System->Nickname;
                                        }
                                    } else if (_stricmp(Value, "%%Home_Base%%") == 0) {
                                        // Home-base record +0x08.
                                        Value = HomeBase->Value.c_str();
                                    }
                                }
                                strcat(Joined, Value);
                            }
                            fprintf(Out, "%s = %s\n", Key, Joined);
                        }
                        fprintf(Out, "\n");
                    }
                    bResult = true;
                }
                Reader.close();
            }

            // Encode-and-rewrite on every path that opened the output.
            Writer.Close();

            if (Template.File != nullptr) {
                fclose(Template.File);
                Template.File = nullptr;
            }
        }

        if (bResult) {
            reload_saved_character(FileName.c_str());
            if (!SinglePlayer()) {
                FDUMP(kFdumpNotice, "LOG: Create character on account %S (\"%s\")\n",
                      AccountName, Path);
            }
        }
        // The writer's destructor finds its FILE* already null.
    }
    return bResult;
}
