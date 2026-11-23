// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU48 (06f4ed80-06f55e20): per-faction properties (missions\faction_prop.ini
// [FactionProps]), the per-faction name tables read from the same sections, the goods a
// faction trades (equipment\commodities_per_faction.ini [FactionGood]) and the reputation
// empathy tables (missions\empathy.ini [RepChangeEffects]).
// Retail's std::map / std::vector instantiations for these tables are the std containers
// below.

#include "common_net.h"         // CommReferrable
#include "common_reputation.h"  // Costume

#include <list>
#include <map>
#include <vector>

class INI_Reader;

namespace pub
{
namespace Controller
{
struct TimerExpired;
}
}

// scan_for_cargo entry, retail 8 bytes; equality FUN_06f50140 compares both fields.
struct FFactionScanCargo
{
    unsigned int Archetype;     // +0x00 pub::SpaceObj::GetArchetypeID(GetGoodID(value 0))
    int Level;                  // +0x04 value 1 when it is 1 or 2, else 0
};

// jump_preference values (+0x58). The value string is compared case-sensitively.
enum EFactionJumpPreference
{
    FactionJumpAny = 0,
    FactionJumpGate = 1,        // "jumpgate"
    FactionJumpHole = 2         // "jumphole"
};

// Legality values (+0x94).
enum EFactionLegality
{
    FactionLegalityUnset = 0,
    FactionLegalityLawful = 1,  // "lawful"
    FactionLegalityUnlawful = 2 // "unlawful"
};

// One [FactionProps] section, retail 0xd4 bytes: ctor FUN_06f4f060, copy ctor FUN_06f52a20,
// dtor FUN_06f501c0 (also FUN_06f502f0). The vtable 06fbb798 holds only the three
// CommReferrable import thunks, so nothing is overridden.
struct FFactionProps : public CommReferrable
{
    FFactionProps();                                            // FUN_06f4f060

    std::vector<unsigned int> NpcShips;             // +0x48 npc_ship, CreateID
    int JumpPreference;                             // +0x58 EFactionJumpPreference, 0
    std::vector<FFactionScanCargo> ScanForCargo;    // +0x5c scan_for_cargo, each pair once
    float ScanChance;                               // +0x6c scan_chance, 1.0f
    bool bScanAnnounce;                             // +0x70 scan_announce, true
    std::vector<unsigned int> MarketGoods;          // +0x74 commodities_per_faction MarketGood
    std::vector<unsigned int> AssassinateTargets;   // +0x84 assassinate_target, CreateID
    int Legality;                                   // +0x94 EFactionLegality, 0
    bool bSingularNickname;                         // +0x98 nickname_plurality: singular
                                                    //       true, plural false; true
    std::map<unsigned int, unsigned int> Formations; // +0x9c formation: CreateID(v0) ->
                                                     //       CreateID(v1), first one kept
    std::vector<Costume> SpaceCostumes;             // +0xb0 space_costume
    std::vector<unsigned int> Voices;               // +0xc0 voice, pub::GetVoiceID
    int McCostume;                                  // +0xd0 mc_costume, pub::GetCostumeID; 0

    // FUN_06f4f3d0.
    bool GetFormation(unsigned int const* Key, unsigned int* Out) const;

    // FUN_06f4f430: a random space costume whose skeleton gender is Gender; false when none
    // matches.
    bool PickSpaceCostume(int Gender, Costume* Out) const;

    // FUN_06f4f620: a random voice whose voice_properties record exists and supports every
    // bit of Roles; candidates that fail are removed and the draw repeats.
    bool PickVoice(unsigned int Roles, unsigned int* Out) const;

    // FUN_06f4f7c0: false when there is no voice; else *Out = 1 (male) with the probability
    // of the voices supporting role 1, else 2.
    bool PickGender(int* Out) const;
};

// A name range "first, last" of the name keys (retail pair<int, int>, 8 bytes).
struct FFactionNameRange
{
    int First;
    int Last;
};

// One name key list, retail 0x14 bytes: the total name count, then the ranges.
struct FFactionNameList
{
    FFactionNameList();

    // Inlined five times in FUN_06f52230: value 1 defaults to value 0 when empty.
    void AddRange(INI_Reader* Reader);

    // FUN_06f4f020: no bounds check.
    int GetName(unsigned int Index) const;

    // FUN_06f4f050 (ICF-shared body).
    bool IsEmpty() const;

    int Count;                                  // +0x00
    std::vector<FFactionNameRange> Ranges;      // +0x04
};

// A reserved name and its use count (retail 8 bytes).
struct FFactionNameUse
{
    int Name;
    int UseCount;
};

// A reserved name pair (pilot first/last name, formation designation/number) and its use
// count (retail 12 bytes; found by the pair only, FUN_06f55310).
struct FFactionNamePairUse
{
    int First;
    int Second;
    int UseCount;
};

// A name pair queued for a delayed release (list node payload, 8 bytes).
struct FFactionNamePair
{
    int First;
    int Second;
};

// The name tables of one [FactionProps] section, retail 0xf8 bytes, keyed by the
// section's affiliation in GFactionNames (DAT_06fd0ca4): ctor FUN_06f50ff0, copy ctor
// FUN_06f52c70, dtor FUN_06f51160.
struct FFactionNames
{
    FFactionNames();                                            // FUN_06f50ff0

    // FUN_06f52230: true only for firstname_male / firstname_female / lastname /
    // formation_desig / large_ship_names; "affiliation", "large_ship_desig" and
    // "rank_desig" are read but answer false, "firstname" is ignored.
    bool ReadIniValue(INI_Reader* Reader);

    // FUN_06f521d0.
    int GetLargeShipDesig() const;
    // FUN_06f521e0.
    unsigned int GetShortGroupName() const;
    // FUN_06f521f0.
    unsigned int GetNameId(int Index) const;
    // FUN_06f52200.
    unsigned int GetRankDesig(int const* Level) const;

    // FUN_06f51520: first name from the male list when Gender is 1, else the female list.
    // True when the walk from a random start ended on a free pair. Retail quirks kept: with
    // OutFirst null the key is {1, last}, with OutLast null {first, 1} and the walk uses the
    // last-name count; once every pair is used the draw is random, false, and a null OutLast
    // sets the first name to 1.
    bool PickPilotName(int* OutFirst, int* OutLast, int Gender, bool bReserve);

    // FUN_06f51920: a designation and a number 0..19; false (and a random pair) once every
    // pair is used.
    bool PickFormationDesig(int* OutDesig, int* OutNumber, bool bReserve);

    // FUN_06f51b80: false (and a random name) once every name is used.
    bool PickLargeShipName(int* OutName, bool bReserve);

    // FUN_06f51db0.
    void AddFormationDesigUse(int Desig, int Number);

    // FUN_06f51ee0 / FUN_06f51fe0 / FUN_06f520d0: bImmediate drops one use now; otherwise the
    // name is queued and a SpaceSupport timer fires in 30 s (HandleFactionNameTimer).
    void ReleasePilotName(int First, int Last, bool bImmediate);
    void ReleaseLargeShipName(int Name, bool bImmediate);
    void ReleaseFormationDesig(int Desig, int Number, bool bImmediate);

    unsigned int Group;                             // +0x00 affiliation (not set by the ctor)
    FFactionNameList FemaleFirstNames;              // +0x04 firstname_female
    FFactionNameList MaleFirstNames;                // +0x18 firstname_male
    FFactionNameList LastNames;                     // +0x2c lastname
    FFactionNameList FormationDesigs;               // +0x40 formation_desig
    FFactionNameList LargeShipNames;                // +0x54 large_ship_names
    int LargeShipDesig;                             // +0x68 large_ship_desig value 0
    unsigned int ShortGroupName;                    // +0x6c pub::Reputation::GetShortGroupName
    unsigned int RankDesig[3];                      // +0x70 rank_desig values 0..2
    // +0x7c: eight name ids. The loader writes [2] = 0x4076 and [3], [4], [7] = 0x4074
    // (lawful) / 0x4075 (unlawful) after the section; TODO: meaning of the index.
    unsigned int NameIds[8];
    int RankThreshold[2];                           // +0x9c rank_desig values 3, 4
    std::vector<FFactionNameUse> LargeShipNameUses;         // +0xa4
    std::vector<FFactionNamePairUse> PilotNameUses;         // +0xb4
    std::vector<FFactionNamePairUse> FormationDesigUses;    // +0xc4
    std::list<FFactionNamePair> PendingPilotNames;          // +0xd4
    std::list<int> PendingLargeShipNames;                   // +0xe0
    std::list<FFactionNamePair> PendingFormationDesigs;     // +0xec
};

typedef std::map<unsigned int, FFactionProps> FFactionPropsMap;
typedef std::map<unsigned int, FFactionNames> FFactionNamesMap;

// One [RepChangeEffects] section of missions\empathy.ini, the value of GFactionEmpathy
// (map node value 0x20 bytes): the empathy rates sorted by rate, then the four event
// rates.
struct FFactionEmpathyRate
{
    unsigned int Group;         // empathy_rate value 0, GetReputationGroup
    float Rate;                 // empathy_rate value 1
};

enum EFactionEmpathyEvent
{
    FactionEventObjectDestruction = 0,
    FactionEventRandomMissionSuccess = 1,
    FactionEventRandomMissionFailure = 2,
    FactionEventRandomMissionAbortion = 3
};

struct FFactionEmpathy
{
    std::vector<FFactionEmpathyRate> EmpathyRates;  // +0x00, ascending Rate
    float EventRates[4];                            // +0x10 EFactionEmpathyEvent
};

typedef std::map<unsigned int, FFactionEmpathy> FFactionEmpathyMap;

extern FFactionPropsMap GFactionProps;              // DAT_06fd0cb8
extern FFactionNamesMap GFactionNames;              // DAT_06fd0ca4
extern FFactionEmpathyMap GFactionEmpathy;          // DAT_06fd0c90
extern std::vector<unsigned int> GFactionGroups;    // DAT_06fd0cdc

// FUN_06f4f870: after the last section every reputation group is appended to GFactionGroups
// (once each).
void __cdecl LoadFactionProps(INI_Reader* Reader);

// FUN_06f503d0: the named faction's props are not null-checked (as retail).
void __cdecl LoadFactionGoods(INI_Reader* Reader);

// FUN_06f50880: opens RelativePath itself.
void __cdecl LoadFactionEmpathy(char const* RelativePath);

// FUN_06f506a0.
void __cdecl UnloadFactionProps();

// FUN_06f50ed0.
void __cdecl UnloadFactionEmpathy();

// FUN_06f50650: null when absent.
FFactionProps* __cdecl GetFactionProps(unsigned int const* Group);

// FUN_06f51370: null when absent.
FFactionNames* __cdecl GetFactionNames(unsigned int const* Group);

// FUN_06f513c0 (SpaceSupport timer case, before the message router): the group's oldest
// queued name of that kind is released now. The group lookup is not null-checked (as
// retail).
void __cdecl HandleFactionNameTimer(pub::Controller::TimerExpired const* Timer);

// FUN_06f50760.
std::vector<unsigned int>* __cdecl GetFactionGroups();

// FUN_06f50c50: clamped to [-0.9, 0.9]; with bEmpathy also Delta * rate to every group of
// Group's empathy list. Nothing when Delta is 0 or Group is -1.
void __cdecl ApplyFactionReputationChange(int VibeHandle, float Delta, unsigned int Group,
                                          bool bEmpathy);

// FUN_06f50e10: the leading groups of Group's empathy list whose rate is below zero, -1 for
// the rest of the Count slots; Out untouched when Group has no list.
void __cdecl GetFactionHostileGroups(unsigned int Group, unsigned int* Out, unsigned int Count);

// FUN_06f50f90: 0.0f when Group has no list.
float __cdecl GetFactionEventRate(unsigned int Group, int Event);
