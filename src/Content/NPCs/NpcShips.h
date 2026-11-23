// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU42: NPC ship archetypes (missions\npcships.ini,
// missions\npcships_test.ini, mission-ini [NPCShipArch]), NPC character
// descriptions (missions\specific_npc.ini [NPC], mbases.ini [GF_NPC]), mission
// [NPC] blocks and the space_costume value.

#include "common_id.h"
#include "common_reputation.h"

#include <map>
#include <vector>

class INI_Reader;

// Value of an npcships map node (node+0x10, 0x28 bytes), filled by FUN_06f32510.
struct FNpcShipArch
{
    ID_String Nickname;                   // +0x00 nickname (also the map key)
    unsigned int ShipArchetype;           // +0x04 ship_archetype, pub::GetShipID
    unsigned int Loadout;                 // +0x08 loadout, pub::GetLoadoutID
    ID_String Pilot;                      // +0x0C pilot
    int StateGraph;                       // +0x10 state_graph, pub::StateGraph::get_state_graph
    std::vector<unsigned int> NpcClasses; // +0x14 npc_class ids, unique, file order
    int Level;                            // +0x24 level

    // The per-section defaults (inlined in FUN_06f32510).
    FNpcShipArch();

    // FUN_06f32910.
    int GetLevel() const;
};

typedef std::map<unsigned int, FNpcShipArch> FNpcShipArchMap;

// DAT_06fd0830: the npcships table. Constructed by the _initterm thunk
// LAB_06f32440 (FUN_06f1d4f0), destroyed by the atexit thunk FUN_06f32470.
extern FNpcShipArchMap GNpcShipArchs;

// FUN_06f32510 (cdecl): walk every header of an opened ini; each [NPCShipArch]
// section is read into one FNpcShipArch and inserted under its nickname id
// (unique insert: a duplicate nickname keeps the first entry). Called with
// &GNpcShipArchs by LoadContentGameTables and with a mission's own map by
// 06f18910.
void __cdecl LoadNpcShipArchs(INI_Reader* Reader, FNpcShipArchMap* Map);

// FUN_06f32850 (cdecl); called by UnloadContentGameTables.
void __cdecl ClearNpcShipArchs();

// FUN_06f32480 (cdecl): null when absent.
FNpcShipArch* __cdecl FindNpcShipArch(unsigned int const* Id, FNpcShipArchMap* Map);

// FUN_06f324d0 (cdecl): the first entry, in key order, whose ShipArchetype is
// ShipArchetype; &value, or null.
FNpcShipArch* __cdecl FindNpcShipArchByShip(unsigned int ShipArchetype, FNpcShipArchMap* Map);

// FUN_06f31390 (cdecl): the space_costume value "head, body, accessory..." of the
// current INI_Reader line. Accessories are appended (the count is not reset). true
// when both head and body resolved.
bool __cdecl ParseSpaceCostume(Costume* OutCostume, INI_Reader* Reader);

// 0x44-byte NPC character description: a [NPC] block of missions\specific_npc.ini
// (StoryLauncher FUN_06ea6ff0) or a [GF_NPC] block of a base's mbases.ini
// (FUN_06eaa9f0 area).
struct FNpcCharacterDesc
{
    Costume Appearance;        // +0x00 head/body/lefthand/righthand, accessories
    unsigned int Voice;        // +0x34 voice, pub::GetVoiceID
    int IndividualName;        // +0x38 individual_name
    unsigned int Affiliation;  // +0x3C affiliation, pub::Reputation::GetReputationGroup
    ID_String Nickname;        // +0x40 nickname

    // FUN_06f32920.
    FNpcCharacterDesc();

    // FUN_06f32950: one key of the current line; false for an unknown key.
    // nickname also writes OutNickname->Id.
    bool ParseValue(INI_Reader* Reader, ID_String* OutNickname);

    // FUN_06f32b50.
    bool IsComplete() const;
};

typedef std::map<unsigned int, FNpcCharacterDesc> FNpcCharacterDescMap;

// DAT_06fd081c: the specific NPC table filled by StoryLauncher (FUN_06ea6ff0,
// missions\specific_npc.ini) and read by TU23. Constructed by the _initterm thunk
// LAB_06f32be0 (FUN_06f32ce0), destroyed by the atexit thunk FUN_06f32c10.
extern FNpcCharacterDescMap GSpecificNpcs;

// FUN_06f32b70 (cdecl): parse the rest of the current section.
bool __cdecl LoadNpcCharacterDesc(ID_String* OutNickname, FNpcCharacterDesc* Desc, INI_Reader* Reader);

// 0x4c-byte mission [NPC] block (mission ini FUN_06eb84c0; random mission runtime).
struct FMsnNpc
{
    unsigned int Affiliation;  // +0x00 affiliation, pub::Reputation::GetReputationGroup
    int IndividualName;        // +0x04 individual_name
    ID_String NpcShipArch;     // +0x08 npc_ship_arch
    ID_String Field_0x0C;      // +0x0C cleared by the ctor; not written by the parser.
                               // TODO: meaning from its readers (TU21/TU61).
    Costume Appearance;        // +0x10 space_costume
    unsigned int Voice;        // +0x44 voice, pub::GetVoiceID
    ID_String Nickname;        // +0x48 nickname

    // FUN_06f31430.
    FMsnNpc();
};

// NPC name helpers. pub::Reputation::GetName fills two FmtStr (First, Second)
// whose mad-lib ids 0x3f17..0x3f24 select the name pattern; these pick the
// appended values (FmtStr::Val::Data of the Nth node) out of them. All cdecl.
struct FmtStr;

// FUN_06f322a0: true for the Second mad-lib ids that carry these values.
bool __cdecl GetSecondNameValues(FmtStr const* First, FmtStr const* Second, int* OutA, int* OutB);

// FUN_06f32320.
bool __cdecl GetSecondNameThirdValue(FmtStr const* First, FmtStr const* Second, int* Out);

// FUN_06f32350.
bool __cdecl GetFirstNameValue(FmtStr const* First, FmtStr const* Second, int* Out);

// FUN_06f323c0.
bool __cdecl GetFirstNameLastValue(FmtStr const* First, FmtStr const* Second, int* Out);

// FUN_06f32420: *Out in 1..5; returns false.
bool __cdecl RollNpcNameValue(FmtStr const* First, FmtStr const* Second, int* Out);

// FUN_06f31470 (cdecl): read the rest of the current [NPC] section into Npc. true
// when a nickname was read.
bool __cdecl LoadMsnNpc(ID_String* OutNickname, FMsnNpc* Npc, INI_Reader* Reader);
