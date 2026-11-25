// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RandomMissions/RandomMissionDB.h"

#include "RandomMissions/RandomMission.h"
#include "RandomMissions/RandomMissionObject.h"
#include "Core/ContentInternal.h"
#include "Core/ContentMessageRouter.h"
#include "Core/ContentPlayerState.h"
#include "Core/ContentSort.h"        // ContentSort (VC6 std::sort, FUN_06f4a9f0's tail)
#include "Core/ContentStageNames.h"
#include "Core/ContentStaticIds.h"
#include "Bases/Cargo.h"              // PickFactionShipLineArch (TU44)
#include "NPCs/DockQueues.h"         // PlaceFormationBlocks (TU53)
#include "Encounters/EncounterParams.h"    // FEncounterShip
#include "Encounters/Encounters.h"         // SetMatrixIdentity
#include "NPCs/FactionProps.h"
#include "Missions/MissionObjList.h"     // CreateObjListDirective, FObjListGotoShip (TU40)
#include "NPCs/NpcNames.h"           // FNpcNameRequest / BuildNpcName (TU42)
#include "NPCs/NpcShip.h"            // FContentNpcShipParams (TU43)
#include "NPCs/NpcShips.h"           // FNpcShipArch
#include "Missions/ObjListDirective.h"
#include "Encounters/RandomEncounters.h"   // TU31 classes
#include "Core/RefHandle.h"          // TRefHandle (FUN_06f28e10 holders)
#include "Encounters/SpacePaths.h"         // BuildNnPath (FUN_06f5ea40)
#include "Universe/SystemConnections.h"
#include "Universe/SystemInfo.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"
#include "common_csys.h"        // Transform, Matrix (pub::SpaceObj::GetEmptyPos / GetLocation)
#include "server_cobject.h"     // pub::SpaceObj::IsPosEmpty / GetEmptyPos

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string.h>

namespace
{
    // FUN_06f95e00 and its inline copies: linear find, insert at end when absent.
    void AppendUnique(std::vector<unsigned int>* Values, unsigned int Value)
    {
        std::vector<unsigned int>::iterator It = std::find(Values->begin(), Values->end(), Value);
        if (It == Values->end())
            Values->insert(Values->end(), Value);
    }

    void AppendUniqueInt(std::vector<int>* Values, int Value)
    {
        std::vector<int>::iterator It = std::find(Values->begin(), Values->end(), Value);
        if (It == Values->end())
            Values->insert(Values->end(), Value);
    }
}

// ---------------------------------------------------------------------------------------
// Story stage names -- retail .data PTR_s_base_0_rank_06fce5d0 (see ContentStageNames.h).
// ---------------------------------------------------------------------------------------
FContentStageName const GContentStageNames[43] = {
    { "base_0_rank", 0x00 },
    { "mission_01a_loaded", 0x01 },
    { "mission_01a_accepted", 0x02 },
    { "mission_01b_loaded", 0x03 },
    { "mission_01b_accepted", 0x04 },
    { "freetime_01_02", 0x05 },
    { "mission_02_loaded", 0x07 },
    { "mission_02_accepted", 0x08 },
    { "freetime_02_03", 0x09 },
    { "mission_03_loaded", 0x0a },
    { "mission_03_accepted", 0x0b },
    { "freetime_03_04", 0x0c },
    { "mission_04_loaded", 0x0d },
    { "mission_04_accepted", 0x0e },
    { "freetime_04_05", 0x0f },
    { "mission_05_loaded", 0x10 },
    { "mission_05_accepted", 0x11 },
    { "freetime_05_06", 0x12 },
    { "mission_06_loaded", 0x13 },
    { "mission_06_accepted", 0x14 },
    { "freetime_06_07", 0x15 },
    { "mission_07_loaded", 0x16 },
    { "mission_07_accepted", 0x17 },
    { "freetime_07_08", 0x18 },
    { "mission_08_loaded", 0x19 },
    { "mission_08_accepted", 0x1a },
    { "freetime_08_09", 0x1b },
    { "mission_09_loaded", 0x1c },
    { "mission_09_accepted", 0x1d },
    { "freetime_09_10", 0x1e },
    { "mission_10_loaded", 0x1f },
    { "mission_10_accepted", 0x20 },
    { "freetime_10_11", 0x21 },
    { "mission_11_loaded", 0x22 },
    { "mission_11_accepted", 0x23 },
    { "freetime_11_12", 0x24 },
    { "mission_12_loaded", 0x25 },
    { "mission_12_accepted", 0x26 },
    { "freetime_12_13", 0x27 },
    { "mission_13_loaded", 0x28 },
    { "mission_13_accepted", 0x29 },
    { "mission_end", 0x2a },
    { nullptr, 0x00 },
};

// =======================================================================================
// TU47 -- rank -> difficulty
// =======================================================================================

// DAT_06fd0c38 (static init LAB_06f4a9a0, atexit FUN_06f4a9d0).
std::vector<FRmRankDifficulty> GRmRankDifficulties;

// FUN_06f4a9f0. [RankDiffDB] rank_diff = <stage name>, <difficulty>.
void __cdecl LoadRankDifficulties(INI_Reader* Reader)
{
    // Retail keeps the matched stage in the stack slot of the Reader argument: a name
    // missing from the table reuses the previous line's stage, and the very first line
    // reuses the INI_Reader pointer bits.
    int LastStage = static_cast<int>(reinterpret_cast<intptr_t>(Reader));

    while (Reader->read_header())
    {
        if (!Reader->is_header("RankDiffDB") || !Reader->read_value())
            continue;

        do
        {
            if (Reader->is_value("rank_diff"))
            {
                FRmRankDifficulty Record;
                Record.Stage = 0;
                Record.Difficulty = 0.0f;

                char const* StageName = Reader->get_value_string(0);
                // All 43 entries, null names skipped.
                for (int Index = 0; Index < 43; ++Index)
                {
                    if (GContentStageNames[Index].Name != nullptr &&
                        _stricmp(StageName, GContentStageNames[Index].Name) == 0)
                    {
                        LastStage = GContentStageNames[Index].Value;
                        break;
                    }
                }
                Record.Stage = LastStage;

                // x87 compare order: 100.0 when the floored value is not below 100 (NaN
                // included); else 0.0 when v < 0, else v.
                float const Value = Reader->get_value_float(1);
                float const Floor = (Value < 0.0f) ? 0.0f : Value;
                if (!(Floor < 100.0f))
                    Record.Difficulty = 100.0f;
                else if (Value < 0.0f)
                    Record.Difficulty = 0.0f;
                else
                    Record.Difficulty = Value;

                GRmRankDifficulties.push_back(Record);
            }
        } while (Reader->read_value());
    }

    // VC6 std::sort (_Median FUN_06f4ece0, _Sort FUN_06f4eb70, _Unguarded_insert
    // FUN_06f4eca0), ordered by Stage only. ContentSort runs the same steps, so records with
    // an equal Stage keep retail's order.
    FRmRankDifficulty* const First = GRmRankDifficulties.data();
    ContentSort(First, First + GRmRankDifficulties.size(),
                [](FRmRankDifficulty const& Left, FRmRankDifficulty const& Right)
                {
                    return Left.Stage < Right.Stage;
                });
}

// FUN_06f4ad30.
void __cdecl ClearRankDifficulties()
{
    GRmRankDifficulties.clear();
}

namespace
{
    // The clamp inlined three times in FUN_06f4ad60; !(Low < 100) includes unordered.
    float ClampRmDifficulty(double Value)
    {
        double const Low = (0.0 > Value) ? 0.0 : Value;         // DAT_06fbb228 = 0.0f
        if (!(Low < 100.0))                                     // DAT_06fbb22c = 100.0f
            return 100.0f;
        if (0.0 > Value)
            return 0.0f;
        return static_cast<float>(Value);
    }

    // cos(rand() * 2^-15 (DAT_06fb4448) * DAT_06fb74e8), the double constant being
    // (double)(float)(pi / 2): 1 for rand() == 0, towards 0 for large rand().
    double RandomRmDifficultySpread()
    {
        return cos(static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) *
                   1.5707963705062866);
    }
}

// FUN_06f4ad60. The result stays on the x87 stack; callers store it as a float. 0.0 (DAT_06fbb224) means "no mission in this range" (single
// player only; TU35 FUN_06f041c0 tests it).
float __cdecl PickRmMissionDifficulty(FContentPlayerState const* Player, float MinDifficulty,
                                      float MaxDifficulty)
{
    double const RankStep = static_cast<double>(1.3689f);        // DAT_06fbb230
    if (!pub::SinglePlayer())
    {
        // Two times out of three the top grows by RankStep^(2, 4 or 6).
        if (rand() % 3 != 0)
        {
            int const Exponent = (rand() % 3) * 2 + 2;
            MaxDifficulty = static_cast<float>(pow(RankStep, static_cast<double>(Exponent)) *
                                               static_cast<double>(MaxDifficulty));
        }
        MaxDifficulty = ClampRmDifficulty(MaxDifficulty);
        // Min is not clamped.
        double const Spread = RandomRmDifficultySpread();
        return static_cast<float>(static_cast<double>(MaxDifficulty) -
                                  Spread * (static_cast<double>(MaxDifficulty) -
                                            static_cast<double>(MinDifficulty)));
    }

    // The difficulty of the player's stage -- the exact record, else the last one below it
    // (signed compare), else the first; an empty table reads begin() (retail does not test
    // it).
    int const Stage = static_cast<int>(Player->MissionNumber);
    std::vector<FRmRankDifficulty>::const_iterator Previous = GRmRankDifficulties.begin();
    float StageDifficulty = 0.0f;
    bool bFound = false;
    for (std::vector<FRmRankDifficulty>::const_iterator It = GRmRankDifficulties.begin();
         It != GRmRankDifficulties.end(); ++It)
    {
        if (It->Stage == Stage)
        {
            StageDifficulty = It->Difficulty;
            bFound = true;
            break;
        }
        if (It->Stage > Stage)
            break;
        Previous = It;
    }
    if (!bFound)
        StageDifficulty = Previous->Difficulty;

    // A window [stage / step^3, stage * step^3], each end clamped.
    double const Window = pow(RankStep, 3.0);                   // DAT_06fbb234 = 3.0f
    float const WindowLow = ClampRmDifficulty(static_cast<double>(StageDifficulty) / Window);
    float const WindowHigh = ClampRmDifficulty(static_cast<double>(StageDifficulty) * Window);

    // Unordered goes on.
    if (WindowHigh < MinDifficulty || WindowHigh == MinDifficulty)
        return 0.0f;

    if (WindowHigh >= MaxDifficulty)
    {
        // Ordered and not below Max: [max(Min, Max / step^3), Max].
        float Low = static_cast<float>(static_cast<double>(MaxDifficulty) / Window);
        if (MinDifficulty > Low)
            Low = MinDifficulty;
        double const Spread = RandomRmDifficultySpread();
        return static_cast<float>(static_cast<double>(MaxDifficulty) -
                                  Spread * (static_cast<double>(MaxDifficulty) -
                                            static_cast<double>(Low)));
    }

    // [max(Min, window low), window high]; only an ordered Low < Min takes Min.
    float Low = WindowLow;
    if (Low < MinDifficulty)
        Low = MinDifficulty;
    double const Spread = RandomRmDifficultySpread();
    return static_cast<float>(static_cast<double>(WindowHigh) -
                              Spread * (static_cast<double>(WindowHigh) -
                                        static_cast<double>(Low)));
}

// =======================================================================================
// TU47 -- random mission data base (RandomMission.h)
// =======================================================================================

// FUN_06ef0570 (TU27 COMDAT): ~FmtStr at +0x24, then the base dtors (vptrs 06fb43f8 /
// 06fb4338). The deleting dtor FUN_06f4d370 and the this-4 thunk FUN_06f4ed50 are the
// compiler's.
FRmMissionDataBase::~FRmMissionDataBase()
{
}

// FUN_06f4d390.
void FRmMissionDataBase::Save(FILE* File)
{
    fprintf(File, "offer_group = %d\n", OfferGroup);
    fprintf(File, "offer_base = %u\n", OfferBase);
    fprintf(File, "offer_ids = %d\n", OfferIds);
    fprintf(File, "reward = %d\n", Reward);
    fprintf(File, "rep_reward = %g\n", static_cast<double>(RepReward));
    fprintf(File, "rep_hit = %g\n", static_cast<double>(RepHit));
    fprintf(File, "difficulty = %g\n", static_cast<double>(Difficulty));
    fprintf(File, "jump_pref = %d\n", JumpPref);
}

// FUN_06f4d420.
void FRmMissionDataBase::LoadValue(INI_Reader* Reader)
{
    if (Reader->is_value("offer_group"))
    {
        OfferGroup = Reader->get_value_int(0);
        return;
    }
    if (Reader->is_value("offer_base"))
    {
        OfferBase = static_cast<unsigned int>(Reader->get_value_int(0));
        return;
    }
    if (Reader->is_value("offer_ids"))
    {
        OfferIds = Reader->get_value_int(0);
        return;
    }
    if (Reader->is_value("reward"))
    {
        Reward = Reader->get_value_int(0);
        return;
    }
    if (Reader->is_value("rep_reward"))
    {
        RepReward = Reader->get_value_float(0);
        return;
    }
    if (Reader->is_value("rep_hit"))
    {
        RepHit = Reader->get_value_float(0);
        return;
    }
    if (Reader->is_value("difficulty"))
    {
        Difficulty = Reader->get_value_float(0);
        return;
    }
    if (Reader->is_value("jump_pref"))
        JumpPref = Reader->get_value_int(0);
}

// =======================================================================================
// TU47 -- the random-mission loader (RandomMission.h)
// =======================================================================================

// FUN_06f4d220: operator new(0xc); returns 1 even when new fails.
bool __cdecl CreateRandomMissionLoader(char const* /*MissionType*/,
                                       FRandomMissionLoader** OutLoader)
{
    *OutLoader = new (std::nothrow) FRandomMissionLoader();
    return true;
}

// FUN_06f4d280: the mission is released through its interface at +0x04. The deleting dtor
// FUN_06f4d260 is the compiler's.
FRandomMissionLoader::~FRandomMissionLoader()
{
    if (Mission != nullptr)
    {
        Mission->Release();
        Mission = nullptr;
    }
}

// FUN_06ebf6f0: empty (ICF-shared slot).
void FRandomMissionLoader::ReleaseMission(FRandomMissionData* /*Mission*/)
{
}

// =======================================================================================
// TU47 -- systems loaded for random missions
// =======================================================================================

std::list<unsigned int> GRmDestSystems;   // DAT_06fd0c2c (head) / DAT_06fd0c30 (size)

// FUN_06f4d5e0; callers in TU27, TU34 and TU63. No null check on FindSystemInfo.
void __cdecl RegisterRmDestSystem(unsigned int SystemId)
{
    for (std::list<unsigned int>::iterator It = GRmDestSystems.begin();
         It != GRmDestSystems.end(); ++It)
    {
        if (*It == SystemId)
            return;
    }
    GRmDestSystems.push_back(SystemId);
    FindSystemInfo(&SystemId)->Load(false);
}

// FUN_06f4d650; callers BasePop and TU34.
void __cdecl ReleaseRmDestSystems()
{
    std::list<unsigned int>::iterator It = GRmDestSystems.begin();
    while (It != GRmDestSystems.end())
    {
        FindSystemInfo(&*It)->Unload();
        It = GRmDestSystems.erase(It);
    }
}

// =======================================================================================
// TU49 -- random-mission bonus loot
// =======================================================================================

// DAT_06fd0d2c (static init LAB_06f56040, atexit FUN_06f56070).
std::map<unsigned int, std::vector<FRmBonusLootEntry>> GRmBonusLoot;

// DAT_06fd0d84.
float GRmBonusLootDropChance = 0.0f;

namespace
{
    // .rdata DAT_06fbb9b8 -- the faction key used for "all".
    unsigned int const GRmBonusLootAllFactions = 0xffffffffu;

    // FUN_06f564d0 candidate: {record pointer, weight copied from record+0x14}.
    struct FRmBonusLootCandidate
    {
        FRmBonusLootEntry const* Record;
        float Weight;
    };

    // FUN_06f564d0 inner loop over one faction list; unordered compares skip the record.
    void CollectBonusLootCandidates(std::map<unsigned int, std::vector<FRmBonusLootEntry>>::iterator It,
                                    float Difficulty,
                                    std::vector<FRmBonusLootCandidate>* Candidates)
    {
        if (It == GRmBonusLoot.end())
            return;
        std::vector<FRmBonusLootEntry> const& Records = It->second;
        for (std::vector<FRmBonusLootEntry>::const_iterator Record = Records.begin();
             Record != Records.end(); ++Record)
        {
            if ((Record->DifficultyMin < Difficulty || Record->DifficultyMin == Difficulty) &&
                (Difficulty < Record->DifficultyMax || Difficulty == Record->DifficultyMax))
            {
                FRmBonusLootCandidate Candidate;
                Candidate.Record = &*Record;
                Candidate.Weight = Record->Weight;
                Candidates->push_back(Candidate);
            }
        }
    }
}

// FUN_06f56150.
float __cdecl GetRmBonusLootDropChance()
{
    return GRmBonusLootDropChance;
}

// FUN_06f56160: values of one [RMBonusLoot] section (the header is already read by the
// caller).
void __cdecl LoadRmBonusLootSection(INI_Reader* Reader)
{
    FRmBonusLootEntry Record;
    Record.Archetype = 0;
    Record.NumToDropMin = 0;
    Record.NumToDropMax = 0;
    Record.DifficultyMin = 0.0f;
    Record.DifficultyMax = 0.0f;
    Record.Weight = 0.0f;
    std::vector<unsigned int> Factions;
    // Written only by GetReputationGroup, lives across the section.
    unsigned int GroupId = 0;

    while (Reader->read_value())
    {
        if (Reader->is_value("archetype"))
        {
            pub::GetEquipmentID(Record.Archetype, Reader->get_value_string(0));
        }
        else if (Reader->is_value("num_to_drop"))
        {
            Record.NumToDropMin = Reader->get_value_int(0);
            if (Reader->is_value_empty(1))
                Record.NumToDropMax = Record.NumToDropMin;
            else
                Record.NumToDropMax = Reader->get_value_int(1);
        }
        else if (Reader->is_value("faction"))
        {
            if (_stricmp(Reader->get_value_string(0), "all") == 0)
            {
                AppendUnique(&Factions, GRmBonusLootAllFactions);
            }
            else
            {
                unsigned int ValueIndex = 0;
                while (!Reader->is_value_empty(ValueIndex))
                {
                    pub::Reputation::GetReputationGroup(GroupId,
                                                        Reader->get_value_string(ValueIndex));
                    AppendUnique(&Factions, GroupId);
                    ++ValueIndex;
                }
            }
        }
        else if (Reader->is_value("difficulty_range"))
        {
            Record.DifficultyMin = Reader->get_value_float(0);
            Record.DifficultyMax = Reader->get_value_float(1);
        }
        else if (Reader->is_value("weight"))
        {
            Record.Weight = static_cast<float>(Reader->get_value_int(0));
        }
    }

    // FUN_06f56780 inserts or finds the faction entry, FUN_06f56900 appends the record.
    for (std::vector<unsigned int>::const_iterator It = Factions.begin(); It != Factions.end();
         ++It)
    {
        std::pair<std::map<unsigned int, std::vector<FRmBonusLootEntry>>::iterator, bool> Inserted =
            GRmBonusLoot.insert(std::map<unsigned int, std::vector<FRmBonusLootEntry>>::value_type(
                *It, std::vector<FRmBonusLootEntry>()));
        Inserted.first->second.push_back(Record);
    }
}

// FUN_06f566a0. [Global] BonusLootDropChance / [RMBonusLoot].
void __cdecl LoadRmLootInfo(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (Reader->is_header("Global"))
        {
            while (Reader->read_value())
            {
                if (!Reader->is_value("BonusLootDropChance"))
                    continue;

                // Stored first, then clamped; unordered -> 0.0.
                GRmBonusLootDropChance = Reader->get_value_float(0);
                if (!(GRmBonusLootDropChance > 0.0f))
                    GRmBonusLootDropChance = 0.0f;
                else if (!(GRmBonusLootDropChance < 1.0f))
                    GRmBonusLootDropChance = 1.0f;
            }
        }
        else if (Reader->is_header("RMBonusLoot"))
        {
            LoadRmBonusLootSection(Reader);
        }
    }
}

// FUN_06f564d0: weighted pick among the "all" records and then the
// faction's records whose difficulty range contains Difficulty.
FRmBonusLootEntry const* __cdecl PickRmBonusLoot(unsigned int Faction, float Difficulty)
{
    FRmBonusLootEntry const* Picked = nullptr;
    std::vector<FRmBonusLootCandidate> Candidates;

    std::map<unsigned int, std::vector<FRmBonusLootEntry>>::iterator AllIt =
        GRmBonusLoot.find(GRmBonusLootAllFactions);
    std::map<unsigned int, std::vector<FRmBonusLootEntry>>::iterator FactionIt =
        GRmBonusLoot.find(Faction);
    CollectBonusLootCandidates(AllIt, Difficulty, &Candidates);
    CollectBonusLootCandidates(FactionIt, Difficulty, &Candidates);

    if (!Candidates.empty())
    {
        // x87: the sum stays on the FPU stack, rounded to float only for the target.
        double Sum = 0.0;
        for (std::vector<FRmBonusLootCandidate>::const_iterator It = Candidates.begin();
             It != Candidates.end(); ++It)
        {
            Sum = Sum + It->Weight;
        }
        float const Total = static_cast<float>(Sum);

        // A zero total skips the draw. Retail then dereferences the end iterator (reads
        // past the candidate buffer); the port returns null for that case.
        if (Sum != 0.0)
        {
            float const Target = static_cast<float>(
                static_cast<double>(std::rand()) * 3.0517578125e-05 * Total);
            double Accumulated = 0.0;
            // Loop end without a hit also dereferences end in retail; unreachable because
            // Target < Total.
            for (std::vector<FRmBonusLootCandidate>::const_iterator It = Candidates.begin();
                 It != Candidates.end(); ++It)
            {
                Accumulated = Accumulated + It->Weight;
                if (Target < Accumulated || Target == Accumulated)
                {
                    Picked = It->Record;
                    break;
                }
            }
        }
    }
    return Picked;
}

// =======================================================================================
// TU51 -- ship classes, ship mission properties, solar formations
// =======================================================================================

// .data PTR_s_big_solar_06fce748 (4 entries): read by TU51 FUN_06f58f30 (through
// FUN_06f59550) and inline by TU62 FUN_06f95040.
FRmNamedValue const GRmSolarTypes[4] = {
    { "big_solar", 0 },
    { "defensive_solar", 1 },
    { "killable_neutral", 2 },
    { "background_solar", 3 },
};

namespace
{
    // FUN_06f59550 (thiscall on a {char const*, int} table): stricmp over Count entries,
    // null names skipped; on a hit writes the value and returns true, else leaves *OutValue.
    bool LookupNamedValue(FRmNamedValue const* Table, char const* Name, int* OutValue,
                          unsigned int Count)
    {
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            if (Table[Index].Name != nullptr && _stricmp(Name, Table[Index].Name) == 0)
            {
                *OutValue = Table[Index].Value;
                return true;
            }
        }
        return false;
    }

    // .data PTR_s_big_solar_formation_06fce728 (4 entries).
    FRmNamedValue const GRmSolarFormationTypes[4] = {
        { "big_solar_formation", 0 },
        { "battle_background_formation", 1 },
        { "defensive_outpost_formation", 2 },
        { "loot_group_formation", 3 },
    };

    // DAT_06fd0e30 -- ships\shiparch.ini is read once.
    bool GbRmShipMissionPropertiesLoaded = false;

    // DAT_06fd0e34 (static init LAB_06f58e10, atexit FUN_06f58e40).
    std::map<unsigned int, FRmSolarFormation> GRmSolarFormations;

}

// DAT_06fd0ddc (static init LAB_06f57eb0, atexit FUN_06f57ee0).
std::map<unsigned int, std::vector<unsigned int>> GRmShipClasses;

// DAT_06fd0dc8 (static init LAB_06f583c0, atexit FUN_06f583f0).
std::map<unsigned int, std::vector<unsigned int>> GRmShipMissionProperties;

// FUN_06f58190. [ShipClass] nickname / member.
void __cdecl LoadShipClasses(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("ShipClass"))
            continue;

        std::vector<unsigned int> Members;
        ID_String ClassId;
        ClassId.clear();

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                ClassId.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("member"))
            {
                unsigned int MemberId = CreateID(Reader->get_value_string());
                std::map<unsigned int, std::vector<unsigned int>>::iterator Found =
                    GRmShipClasses.find(MemberId);
                if (Found != GRmShipClasses.end())
                {
                    // A member naming an earlier class expands to its members.
                    std::vector<unsigned int> const& Nested = Found->second;
                    for (std::vector<unsigned int>::const_iterator It = Nested.begin();
                         It != Nested.end(); ++It)
                    {
                        AppendUnique(&Members, *It);
                    }
                }
                else
                {
                    AppendUnique(&Members, CreateID(Reader->get_value_string()));
                }
            }
        }

        // FUN_06f58870 is insert-unique: an existing class keeps its first definition.
        GRmShipClasses.insert(
            std::map<unsigned int, std::vector<unsigned int>>::value_type(ClassId.Id, Members));
    }
}

// FUN_06f57fc0 (called from FUN_06efe870).
void __cdecl ClearShipClasses()
{
    GRmShipClasses.clear();
}

// FUN_06f58080.
std::vector<unsigned int>* __cdecl FindShipClass(unsigned int const* ShipClassId)
{
    std::map<unsigned int, std::vector<unsigned int>>::iterator It =
        GRmShipClasses.find(*ShipClassId);
    if (It == GRmShipClasses.end())
        return nullptr;
    return &It->second;
}

// FUN_06f580d0: the first class, in key order, whose member list equals Members (same
// order).
bool __cdecl FindShipClassByMembers(unsigned int* OutShipClassId,
                                    std::vector<unsigned int> const& Members)
{
    for (std::map<unsigned int, std::vector<unsigned int>>::const_iterator It =
             GRmShipClasses.begin();
         It != GRmShipClasses.end(); ++It)
    {
        if (It->second == Members)
        {
            *OutShipClassId = It->first;
            return true;
        }
    }
    return false;
}

// FUN_06f584d0. [Ship] nickname / mission_property; runs once.
void __cdecl LoadShipMissionProperties(INI_Reader* Reader)
{
    if (GbRmShipMissionPropertiesLoaded)
        return;

    while (Reader->read_header())
    {
        if (!Reader->is_header("Ship"))
            continue;

        std::vector<unsigned int> Properties;
        unsigned int ShipArchId = 0;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                pub::GetShipID(ShipArchId, Reader->get_value_string());
            }
            else if (Reader->is_value("mission_property"))
            {
                // Retail keys: "can_use_berths", "can_use_med_moors" (stored as the
                // can_use_medium_moors id), "can_use_large_moors"; anything else ignored.
                if (_stricmp(Reader->get_value_string(), "can_use_berths") == 0)
                    AppendUnique(&Properties, GIdCanUseBerths);
                else if (_stricmp(Reader->get_value_string(), "can_use_med_moors") == 0)
                    AppendUnique(&Properties, GIdCanUseMediumMoors);
                else if (_stricmp(Reader->get_value_string(), "can_use_large_moors") == 0)
                    AppendUnique(&Properties, GIdCanUseLargeMoors);
            }
        }

        GRmShipMissionProperties.insert(
            std::map<unsigned int, std::vector<unsigned int>>::value_type(ShipArchId, Properties));
    }

    // Set also when the file had no header at all.
    GbRmShipMissionPropertiesLoaded = true;
}

// FUN_06f587d0.
bool __cdecl GetShipMissionProperties(unsigned int ShipArchId,
                                      std::vector<unsigned int>* InOutProperties)
{
    std::map<unsigned int, std::vector<unsigned int>>::iterator Found =
        GRmShipMissionProperties.find(ShipArchId);
    if (Found == GRmShipMissionProperties.end())
        return false;

    std::vector<unsigned int> const& Properties = Found->second;
    for (std::vector<unsigned int>::const_iterator It = Properties.begin();
         It != Properties.end(); ++It)
    {
        AppendUnique(InOutProperties, *It);
    }
    return true;
}

// FUN_06f58f20.
std::map<unsigned int, FRmSolarFormation>* __cdecl GetSolarFormations()
{
    return &GRmSolarFormations;
}

// FUN_06f58f30. [SolarFormation] nickname / type / pos.
void __cdecl LoadSolarFormations(INI_Reader* Reader)
{
    // These live for the whole call: a "pos" line whose first value is neither "mandatory"
    // nor "optional", or whose solar type is unknown, reuses the value of the previous "pos"
    // line. Retail starts both uninitialized; the port starts at 0.
    unsigned char bMandatory = 0;
    int SolarType = 0;

    while (Reader->read_header())
    {
        if (!Reader->is_header("SolarFormation"))
            continue;

        // The section record is the one local with a dtor (FUN_06eb7d10); the slots are
        // appended to it directly, the nickname is copied in before the insert.
        ID_String Nickname;
        Nickname.clear();
        FRmSolarFormation Formation;
        Formation.Nickname = 0;
        Formation.Type = -1;
        std::vector<FRmSolarFormationSlot>& Slots = Formation.Slots;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Nickname.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("type"))
            {
                int Type = 0;
                if (LookupNamedValue(GRmSolarFormationTypes, Reader->get_value_string(), &Type, 4))
                    Formation.Type = Type;
            }
            else if (Reader->is_value("pos"))
            {
                FRmSolarFormationSlot Slot;
                // FUN_06efc7a0: identity.
                Slot.Orientation.D[0][0] = 1.0f;
                Slot.Orientation.D[0][1] = 0.0f;
                Slot.Orientation.D[0][2] = 0.0f;
                Slot.Orientation.D[1][0] = 0.0f;
                Slot.Orientation.D[1][1] = 1.0f;
                Slot.Orientation.D[1][2] = 0.0f;
                Slot.Orientation.D[2][0] = 0.0f;
                Slot.Orientation.D[2][1] = 0.0f;
                Slot.Orientation.D[2][2] = 1.0f;
                Slot.Position.X = 0.0f;
                Slot.Position.Y = 0.0f;
                Slot.Position.Z = 0.0f;

                if (_stricmp(Reader->get_value_string(0), "mandatory") == 0)
                    bMandatory = 1;
                else if (_stricmp(Reader->get_value_string(0), "optional") == 0)
                    bMandatory = 0;

                LookupNamedValue(GRmSolarTypes, Reader->get_value_string(1), &SolarType, 4);

                float const X = Reader->get_value_float(2);
                float const Y = Reader->get_value_float(3);
                float const Z = Reader->get_value_float(4);
                Slot.Position.X = X;
                Slot.Position.Y = Y;
                Slot.Position.Z = Z;

                if (!Reader->is_value_empty(5))
                {
                    // Quaternion (W, X, Y, Z) = values 5..8, normalized on x87 (sum of
                    // squares in order v8,v7,v6,v5 and 1/sqrt kept unrounded; each product
                    // rounded to float).
                    FQuaternion Quat;
                    Quat.W = Reader->get_value_float(5);
                    Quat.X = Reader->get_value_float(6);
                    Quat.Y = Reader->get_value_float(7);
                    float const LastZ = Reader->get_value_float(8);
                    double const Sum = static_cast<double>(LastZ) * LastZ +
                                       static_cast<double>(Quat.Y) * Quat.Y +
                                       static_cast<double>(Quat.X) * Quat.X +
                                       static_cast<double>(Quat.W) * Quat.W;
                    double const InvLength = 1.0 / std::sqrt(Sum);
                    Quat.W = static_cast<float>(Quat.W * InvLength);
                    Quat.X = static_cast<float>(Quat.X * InvLength);
                    Quat.Y = static_cast<float>(Quat.Y * InvLength);
                    Quat.Z = static_cast<float>(InvLength * LastZ);

                    FMatrix Rotation;
                    ContentMathEngine()->QuaternionToMatrix(Rotation, Quat);
                    Slot.Orientation = Rotation;
                }

                Slot.bMandatory = bMandatory;
                Slot.SolarType = SolarType;
                Slots.push_back(Slot);
            }
        }

        Formation.Nickname = Nickname.Id;
        // FUN_06f59620 is insert-unique; the pair dies before the record.
        GRmSolarFormations.insert(
            std::map<unsigned int, FRmSolarFormation>::value_type(Nickname.Id, Formation));
    }
}

// FUN_06f592f0.
void FRmSolarFormation::CollectOptionalSlotTypes(std::vector<int>* InOutTypes) const
{
    int Count = static_cast<int>(Slots.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Slots[Index].bMandatory == 0)
            AppendUniqueInt(InOutTypes, Slots[Index].SolarType);
    }
}

// FUN_06f593a0.
void FRmSolarFormation::CollectMandatorySlotTypes(std::vector<int>* InOutTypes) const
{
    int Count = static_cast<int>(Slots.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Slots[Index].bMandatory != 0)
            AppendUniqueInt(InOutTypes, Slots[Index].SolarType);
    }
}

// FUN_06f59450.
int FRmSolarFormation::GetSlotCount() const
{
    return static_cast<int>(Slots.size());
}

// FUN_06f59480.
FRmSolarFormationSlot* FRmSolarFormation::GetSlot(unsigned int Index)
{
    if (Index < static_cast<unsigned int>(Slots.size()))
        return &Slots[Index];
    return nullptr;
}

// ---------------------------------------------------------------------------------------
// TU47 -- the random mission object base (FRandomMissionObject, RandomMissionObject.h).
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_06fd0c8c: live random mission objects. Retail never reads it.
    int GRmMissionObjectCount = 0;

    // .rdata DAT_06fb5810 (1.0f): a position counts as set when one axis squared exceeds it.
    float const GRmPositionAxisSq = 1.0f;
    // .rdata DAT_06fb8570 (1.0e6f): squared reach of the first path objective (1000 units).
    double const GRmPathReachSq = 1000000.0;
    // .rdata DAT_06fbb23c (0.25f): +0x14 of the victory / failure music.
    float const GRmMusicFade = 0.25f;

    // The axis tests of FUN_06f4b870 and FUN_06f4b9e0: any axis whose square is not
    // <= 1.0f (unordered included) makes the position usable.
    bool RmHasPosition(Vector const& Position)
    {
        return !(Position.x * Position.x <= GRmPositionAxisSq) ||
               !(Position.y * Position.y <= GRmPositionAxisSq) ||
               !(Position.z * Position.z <= GRmPositionAxisSq);
    }

    // FUN_06f4d200: (rand() * Range) / 32768 as a signed shift.
    int RmRandomBelow(int Range)
    {
        int const Product = rand() * Range;
        return (Product + ((Product >> 31) & 0x7fff)) >> 15;
    }

    // The music local of FUN_06f4c6f0 / FUN_06f4c9c0.
    pub::Audio::Tryptich RmMissionMusic(char const* Nickname)
    {
        pub::Audio::Tryptich Music;
        Music.SpaceMusic = 0;
        Music.DangerMusic = 0;
        Music.BattleMusic = 0;
        Music.OverrideMusic = pub::GetNicknameId(Nickname);
        Music.Field10 = 1;
        std::memcpy(&Music.FadeBits, &GRmMusicFade, sizeof(float));
        return Music;
    }
}

// FUN_06f4b0d0. The TU38 base is FUN_06f0e3e0, the group ctor takes DAT_06fcf254. SetPlayer
// (slot 18) and the TU38 GetOrCreatePlayer (slot 2, FUN_06f1b6d0) are called directly, not
// virtually.
FRandomMissionObject::FRandomMissionObject(unsigned int const* InControllerId,
                                           FRandomMissionData* InData)
    : FMissionObject(InControllerId, InData)
    , StateMachineGroup(GMessageRouter)
    , Data(InData)
{
    MissionOffer = Data->Description;
    HeartbeatInterval = 1.0f;
    pub::Controller::SetHeartbeatInterval(ControllerId, 1.0f);
    FRandomMissionObject::SetPlayer(Data->PlayerId);
    ++GRmMissionObjectCount;
    for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
    {
        FMissionPlayer* Record = FMissionObject::GetOrCreatePlayer(PlayerStates[Index]->PlayerId);
        if (Record->GetForceResendObjectives())
            Record->SetForceResendObjectives(false);
        Record->SendPlaceholderObjective();
    }
    {
        // The empty FmtStr is destroyed before the second message.
        FmtStr Empty;
        pub::Player::DisplayMissionMessage(Data->PlayerId, Empty,
                                           static_cast<MissionMessageType>(0xa), true);
    }
    pub::Player::DisplayMissionMessage(Data->PlayerId, MissionOffer, static_cast<MissionMessageType>(8),
                                       true);
}

// FUN_06f4b460. Retail order: +0x224, +0x20c, +0x1fc, the parms holder +0x1f8 (FUN_06fa0330
// inlined), +0x1e4, the group, the TU38 base (FUN_06f0ed00) -- the port's reverse
// declaration order.
FRandomMissionObject::~FRandomMissionObject()
{
    --GRmMissionObjectCount;
}

// LAB_06f4b650. No null test on the record.
FMissionPlayer* FRandomMissionObject::GetOrCreatePlayer(unsigned int InPlayerId)
{
    FMissionPlayer* Record = FMissionObject::GetOrCreatePlayer(InPlayerId);
    if (Record->GetForceResendObjectives())
        Record->SetForceResendObjectives(false);
    return Record;
}

// FUN_06f4c3b0. The TU38 base body is FUN_06f1ca50. No null tests on the group
// (FUN_06f481c0) or on the member states.
void FRandomMissionObject::SetPlayer(unsigned int InPlayerId)
{
    PlayerStateMap.clear();
    PlayerStates.clear();
    FMissionObject::SetPlayer(InPlayerId);
    if (InPlayerId == 0)
    {
        PlayerId = 0;
        PlayerState = nullptr;
        return;
    }
    std::vector<unsigned int> Members;
    PlayerState->GetGroupMembers(&Members);
    for (int Index = 0; Index < static_cast<int>(Members.size()); ++Index)
    {
        FContentPlayerState* State = FindContentPlayerState(Members[Index]);
        PlayerStateMap.insert(
            std::map<unsigned int, FContentPlayerState*>::value_type(Members[Index], State));
        PlayerStates.push_back(State);
        std::wstring Name;
        State->GetName(&Name);
        AddPlayerName(Name);
    }
}

// Inlined twice in retail (in SetPlayer and in FUN_06f4cb76's range). The count grows only
// while fewer than 8 names are listed; the second lookup (FUN_06f4daa0) is retail's.
void FRandomMissionObject::AddPlayerName(std::wstring const& Name)
{
    if (std::find(PlayerNames.begin(), PlayerNames.end(), Name) != PlayerNames.end())
        return;
    if (PlayerNames.size() < 8)
        ++PlayerCount;
    if (std::find(PlayerNames.begin(), PlayerNames.end(), Name) == PlayerNames.end())
        PlayerNames.insert(PlayerNames.end(), Name);
}

// LAB_06f4b680. The TU38 body is reached from OnStateEvent.
void FRandomMissionObject::HandleMissionMessage(pub::Controller::EMessage MessageId,
                                                void const* MessageParms)
{
    // SMControllerEvent (common_net.h, Common.dll's export) keeps the message id as a plain int
    // in its +0x08 field (named ControllerId there); the readers in OnStateEvent cast it back.
    SMControllerEvent Event(static_cast<int>(MessageId), MessageParms);
    process(&Event);
}

// FUN_06f4c9c0; the TU38 body is FUN_06f0fda0. No flag reset here (unlike FailMission).
void FRandomMissionObject::OnMissionSucceed()
{
    pub::Audio::Tryptich const Music = RmMissionMusic("music_victory");
    for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
    {
        FContentPlayerState* State = PlayerStates[Index];
        State->CountRandomMissionCompleted(MissionType);
        if (State->ShipObject != nullptr)
            pub::Audio::SetMusic(State->PlayerId, Music);
    }
    unsigned int const Group = static_cast<unsigned int>(Data->OfferGroup);
    float const Rate = GetFactionEventRate(Group, 1);
    ChangeGroupReputationAlways(Group, Rate);
    FMissionObject::OnMissionSucceed();
}

// LAB_06f4b6c0.
std::vector<FContentPlayerState*>* FRandomMissionObject::GetPlayerStates()
{
    return &PlayerStates;
}

// LAB_06f4b6d0.
std::map<unsigned int, FContentPlayerState*>* FRandomMissionObject::GetPlayerStateMap()
{
    return &PlayerStateMap;
}

// LAB_06f4b7b0. A single listed name leaves *bChanged as it is. Objectives.Add is
// FUN_06f1cc40 (no null test).
bool FRandomMissionObject::UpdatePlayerCountObjective(FMissionPlayer* Player, bool* bChanged)
{
    if (PlayerNames.size() == 1)
        return *bChanged;
    std::list<FMissionObjective*>& List = Player->Objectives.Objectives;
    std::list<FMissionObjective*>::iterator It = List.begin();
    for (; It != List.end(); ++It)
    {
        if ((*It)->GetType() == 8)
            break;
    }
    if (It != List.end())
    {
        FNNObjectiveType8* Existing = static_cast<FNNObjectiveType8*>(*It);
        if (PlayerCount > 0 && Existing->Value != static_cast<unsigned int>(PlayerCount - 1))
        {
            Existing->Value = static_cast<unsigned int>(PlayerCount - 1);
            Existing->Objective.Type |= 0x50;
            Existing->BuildDescription();
            *bChanged = true;
        }
        return *bChanged;
    }
    FNNObjectiveType8* Created = static_cast<FNNObjectiveType8*>(Player->Objectives.Add(8, this));
    Created->Objective.Type = 0x58;
    Created->bHidden = 0;
    Created->Ids = 0x331a;
    Created->Value = static_cast<unsigned int>(PlayerCount - 1);
    Created->BuildDescription();
    *bChanged = true;
    return *bChanged;
}

// LAB_06f4bd00.
int FRandomMissionObject::DecrementPlayerCount()
{
    --PlayerCount;
    return PlayerCount;
}

// LAB_06f4bd10.
int FRandomMissionObject::IncrementPlayerCount()
{
    ++PlayerCount;
    return PlayerCount;
}

// LAB_06f4bd20.
int FRandomMissionObject::GetPlayerCount()
{
    return PlayerCount;
}

// FUN_06f4c6f0. Reason 1 is an abort. ClearObjectives is FUN_06f1b3d0.
void FRandomMissionObject::FailMission(int Reason)
{
    pub::Audio::Tryptich const Music = RmMissionMusic("music_failure");
    for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
    {
        FContentPlayerState* State = PlayerStates[Index];
        State->SetFlags(0);
        if (Reason == 1)
            State->CountRandomMissionAborted(MissionType);
        else
            State->CountRandomMissionFailed(MissionType);
        if (State->ShipObject != nullptr)
            pub::Audio::SetMusic(State->PlayerId, Music);
    }
    unsigned int const Group = static_cast<unsigned int>(Data->OfferGroup);
    float const Rate = GetFactionEventRate(Group, Reason == 1 ? 3 : 2);
    ChangeGroupReputation(Group, Rate, Reason != 1);
    ClearObjectives();
    EndMission();
    FContentPlayerState* Owner = FindContentPlayerState(PlayerId);
    if (Owner != nullptr && Owner->GetLiveMission() == ControllerId)
        Owner->EndLiveMission(ControllerId, VNPC_MISSION_FAILED, true);
}

// FUN_06f4c830. Retail erases the map's find result without an end test (FUN_06f20e00); the
// port erases by key. Retail does not test the state before the count / EndLiveMission calls.
void FRandomMissionObject::RemoveMissionPlayer(unsigned int InPlayerId, int Reason)
{
    unsigned int const Group = static_cast<unsigned int>(Data->OfferGroup);
    float const Rate = GetFactionEventRate(Group, Reason == 1 ? 3 : 2);
    ChangePlayerReputation(InPlayerId, Group, Rate, false, true);
    RemovePlayerById(InPlayerId);
    PlayerStateMap.erase(InPlayerId);
    PlayerStates.clear();
    for (std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerStateMap.begin();
         It != PlayerStateMap.end(); ++It)
        PlayerStates.push_back(It->second);
    if (!PlayerStateMap.empty())
        FMissionObject::SetPlayer(PlayerStateMap.begin()->first);
    else
        FMissionObject::SetPlayer(0);
    FContentPlayerState* State = FindContentPlayerState(InPlayerId);
    if (State != nullptr)
        State->SetFlags(0);
    if (Reason == 1)
    {
        State->CountRandomMissionAborted(MissionType);
        State->EndLiveMission(ControllerId, VNPC_MISSION_ABORTED, false);
        return;
    }
    if (Reason == 0xe)
        return;
    State->CountRandomMissionFailed(MissionType);
    State->EndLiveMission(ControllerId, VNPC_MISSION_FAILED, false);
}

// FUN_06f4b6e0.
void FRandomMissionObject::RefreshAllObjectives()
{
    for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
        RefreshObjectives(PlayerStates[Index]->PlayerId);
}

// LAB_06f4b720. SendObjectives is FUN_06f1d110.
void FRandomMissionObject::RefreshObjectives(unsigned int InPlayerId)
{
    FMissionPlayer* Record = GetOrCreatePlayer(InPlayerId);
    if (Record == nullptr)
        return;
    bool bPathChanged = false;
    bool bDockedChanged = false;
    bool bCountChanged = false;
    UpdatePathObjectives(Record, &bPathChanged);
    UpdateDockedObjective(Record, &bDockedChanged);
    if (!pub::SinglePlayer())
        UpdatePlayerCountObjective(Record, &bCountChanged);
    if (bDockedChanged || bPathChanged || bCountChanged)
        Record->SendObjectives();
}

// LAB_06f4b870: the "docked" objective (ids 0x3319) is shown while the player has no ship.
// CreateMissionObjective is FUN_06f19cd0 (no null test).
bool FRandomMissionObject::UpdateDockedObjective(FMissionPlayer* Player, bool* bChanged)
{
    *bChanged = false;
    FMissionObjective* Front = Player->Objectives.GetFront();
    if (Player->PlayerState->ShipObject == nullptr)
    {
        if (Front != nullptr && Front->Ids == 0x3319)
            return *bChanged;
        FContentPlayerState* State = FindContentPlayerState(Player->PlayerId);
        if (State == nullptr)
            return false;
        Vector Position;
        State->GetPosition(&Position);
        if (!RmHasPosition(Position))
            return false;
        FMissionObjective* Created = nullptr;
        CreateMissionObjective(2, this, &Created);
        Created->bHidden = 0;
        Created->Ids = 0x3319;
        Created->Objective.Type = 0x10;
        FNNObjectiveNavMarker* Marker = static_cast<FNNObjectiveNavMarker*>(Created);
        Marker->Marker.Data[0] = State->GetSystemId();
        Marker->Marker.Data[1] = 0;
        std::memcpy(&Marker->Marker.Data[2], &Position, sizeof(Vector));
        Player->Objectives.Objectives.push_front(Created);
        *bChanged = true;
        return *bChanged;
    }
    if (Front != nullptr && Front->Ids == 0x3319)
    {
        Player->Objectives.PopFront();
        *bChanged = true;
    }
    return *bChanged;
}

// FUN_06f4b9e0. In a trade lane (FUN_06f57710), once per trip, a first type 5 objective
// within 1000 units is popped; otherwise the path is rebuilt towards the next jump of the
// route (FUN_06f29c10, FUN_06f2a190). The early returns that leave *bChanged unset are
// retail's.
bool FRandomMissionObject::UpdatePathObjectives(FMissionPlayer* Player, bool* bChanged)
{
    unsigned int const TargetSystem = static_cast<unsigned int>(GetTargetSystemId());
    if (TargetSystem == 0)
    {
        *bChanged = false;
        return false;
    }
    FContentPlayerState* State = FindContentPlayerState(Player->PlayerId);
    if (State == nullptr)
        return false;
    Vector Position;
    State->GetPosition(&Position);
    if (!RmHasPosition(Position))
        return false;
    FMissionObjectiveList* List = &Player->Objectives;
    FSystemInfo* System = State->GetSystemRecord();
    if (System == nullptr || System->SystemId == TargetSystem)
    {
        *bChanged = false;
        return false;
    }
    FContentPlayerShip* Ship = State->ShipObject;
    if (Ship != nullptr && Ship->IsInTradeLane())
    {
        if (Player->bTradelanePathChecked != 0)
            return *bChanged;
        Player->bTradelanePathChecked = 1;
        if (List->Objectives.empty())
            return *bChanged;
        FMissionObjective* Front = List->GetFront();
        if (Front->GetType() != 5)
            return *bChanged;
        Vector Now;
        State->GetPosition(&Now);
        FNNObjectiveType5 const* Leg = static_cast<FNNObjectiveType5 const*>(Front);
        Vector LegPoint;
        std::memcpy(&LegPoint, &Leg->Marker.Data[2], sizeof(Vector));
        // FUN_06eb7810: each difference is stored as a float; the sum y*y + z*z + x*x stays
        // on the x87 stack. Unordered keeps the objective.
        float const DeltaX = Now.x - LegPoint.x;
        float const DeltaY = Now.y - LegPoint.y;
        float const DeltaZ = Now.z - LegPoint.z;
        double const DistanceSq = static_cast<double>(DeltaY) * DeltaY +
                                  static_cast<double>(DeltaZ) * DeltaZ +
                                  static_cast<double>(DeltaX) * DeltaX;
        if (!(DistanceSq <= GRmPathReachSq))
            return *bChanged;
        List->PopFront();
        *bChanged = true;
        return true;
    }
    Player->bTradelanePathChecked = 0;
    unsigned int const JumpObject =
        GetNextHopConnectionObject(System->SystemId, TargetSystem, GetJumpPreference());
    // Retail's stack slot keeps whatever it held when FUN_06f2a190 finds no connection.
    Vector JumpPosition = {0.0f, 0.0f, 0.0f};
    GetConnectionPosition(System->SystemId, JumpObject, &JumpPosition);
    std::list<FNnPathLeg> Legs;
    // BuildNnPath is FUN_06f5ea40; its result is not read.
    float const NoRange = 0.0f;
    BuildNnPath(System, &Position, &JumpPosition, &Legs, 0x19, &NoRange, 1);
    *bChanged = BuildPathObjectives(System, &Legs, List, 0x330e + RmRandomBelow(5), 1, 0x3318,
                                    JumpObject, 0x5a, 9, 0x1a, nullptr);
    return *bChanged;
}

// LAB_06f4cd50. ApplyFactionReputationChange is FUN_06f50c50.
void FRandomMissionObject::ChangePlayerReputation(unsigned int InPlayerId, unsigned int Group,
                                                  float Delta, bool bForce, bool bEmpathy)
{
    if (!bForce && HasReputationEntry(InPlayerId, Group))
        return;
    std::wstring Name;
    pub::Player::GetName(InPlayerId, Name);
    // The pair lives to the end of the function; the insert takes a converted
    // pair<const wstring, uint> copy that dies right after the insert (FUN_06f4d7c0).
    std::pair<std::wstring, unsigned int> const Entry(Name, Group);
    ReputationChanges.insert(std::multimap<std::wstring, unsigned int>::value_type(Entry));
    // Retail reads the result into the bForce argument slot.
    int Reputation = 0;
    pub::Player::GetRep(InPlayerId, Reputation);
    ApplyFactionReputationChange(Reputation, Delta, Group, bEmpathy);
}

// LAB_06f4cf50.
void FRandomMissionObject::ChangePlayerReputationAlways(unsigned int InPlayerId, unsigned int Group,
                                                        float Delta)
{
    int Reputation = 0;
    pub::Player::GetRep(InPlayerId, Reputation);
    ApplyFactionReputationChange(Reputation, Delta, Group, true);
}

// FUN_06f4cf80.
void FRandomMissionObject::ChangeGroupReputation(unsigned int Group, float Delta, bool bEmpathy)
{
    for (std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerStateMap.begin();
         It != PlayerStateMap.end(); ++It)
        ChangePlayerReputation(It->first, Group, Delta, false, bEmpathy);
}

// FUN_06f4d010.
void FRandomMissionObject::ChangeGroupReputationAlways(unsigned int Group, float Delta)
{
    for (std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerStateMap.begin();
         It != PlayerStateMap.end(); ++It)
        ChangePlayerReputationAlways(It->first, Group, Delta);
}

// FUN_06f4d090. Retail quirk: the walk from the name's first entry runs to the END of the
// multimap, not of the name's range: any entry from there on with value Group answers true.
bool FRandomMissionObject::HasReputationEntry(unsigned int InPlayerId, unsigned int Group)
{
    std::wstring Name;
    pub::Player::GetName(InPlayerId, Name);
    std::multimap<std::wstring, unsigned int>::iterator It = ReputationChanges.lower_bound(Name);
    if (It != ReputationChanges.end() && Name < It->first)
        It = ReputationChanges.end();
    for (; It != ReputationChanges.end(); ++It)
    {
        if (It->second == Group)
            return true;
    }
    return false;
}

// FUN_06f4be60. pub::GetSystem writes into the argument slot, so a failed lookup compares
// equal to SystemId.
void FRandomMissionObject::UpdatePlayerSystemFlags(unsigned int InPlayerId, unsigned int SystemId)
{
    std::map<unsigned int, FContentPlayerState*>::iterator Entry = PlayerStateMap.find(InPlayerId);
    if (Entry == PlayerStateMap.end())
        return;
    FContentPlayerState* State = Entry->second;
    if (SystemId == Data->DestSys)
    {
        if (State->ShipObject != nullptr)
            State->SetFlags(State->GetFlags() | PLAYER_FLAG_MISSION_AREA | PLAYER_FLAG_MISSION_DEST_SYSTEM);
        return;
    }
    unsigned int OfferSystem = SystemId;
    pub::GetSystem(OfferSystem, Data->OfferBase);
    if (SystemId == OfferSystem)
    {
        if (State->ShipObject != nullptr)
            State->SetFlags(State->GetFlags() | PLAYER_FLAG_MISSION_AREA);
        return;
    }
    if (State->ShipObject != nullptr)
        State->SetFlags(State->GetFlags() &
                        ~(PLAYER_FLAG_MISSION_AREA | PLAYER_FLAG_MISSION_DEST_SYSTEM));
}

// FUN_06f4bd30. No null test on the player state (FUN_06f49b00); JoinMission is FUN_06f48ba0.
void FRandomMissionObject::RejoinPlayer(unsigned int InPlayerId)
{
    FContentPlayerState* State = FindContentPlayerState(InPlayerId);
    if (PlayerId == InPlayerId)
        PlayerState = State;
    State->JoinMission(&ControllerId, State->PlayerId, false, 0);
    PlayerStateMap.insert(std::map<unsigned int, FContentPlayerState*>::value_type(InPlayerId, nullptr))
        .first->second = State;
    PlayerStates.clear();
    for (std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerStateMap.begin();
         It != PlayerStateMap.end(); ++It)
        PlayerStates.push_back(FindContentPlayerState(It->first));
    FMissionPlayer* Record = FindPlayer(InPlayerId);
    if (Record != nullptr)
    {
        Record->PlayerState = State;
        RemovePlayer(Record);
    }
}

// FUN_06f4c610: alone in his group, the player aborts the mission.
void FRandomMissionObject::OnPlayerLeftGroup(unsigned int const* InPlayerId)
{
    std::map<unsigned int, FContentPlayerState*>::iterator Entry = PlayerStateMap.find(*InPlayerId);
    if (Entry == PlayerStateMap.end())
        return;
    std::vector<unsigned int> Members;
    Entry->second->GetGroupMembers(&Members);
    if (Members.size() > 1)
        pub::Player::RemoveFromGroup(*InPlayerId);
    else
        FailMission(1);
}

// FUN_06f4caa0. Unlike the ctor, the two mission messages are not broadcast.
void FRandomMissionObject::AddGroupMember(unsigned int InPlayerId)
{
    if (PlayerStateMap.find(InPlayerId) != PlayerStateMap.end())
        return;
    if (PlayerStates.empty())
        return;
    if (!PlayerStates[0]->IsGroupMember(InPlayerId))
        return;
    FContentPlayerState* Found = FindContentPlayerState(InPlayerId);
    std::map<unsigned int, FContentPlayerState*>::iterator Entry =
        PlayerStateMap.insert(std::map<unsigned int, FContentPlayerState*>::value_type(InPlayerId, Found))
            .first;
    FContentPlayerState* State = Entry->second;
    PlayerStates.push_back(State);
    // The name local lives to the end of the function.
    std::wstring Name;
    State->GetName(&Name);
    AddPlayerName(Name);
    State->JoinMission(&ControllerId, 0, false, 0);
    GetOrCreatePlayer(State->PlayerId)->SendPlaceholderObjective();
    unsigned int const Target = State->PlayerId;
    {
        // The empty FmtStr dies before the second message.
        FmtStr Empty;
        pub::Player::DisplayMissionMessage(Target, Empty, static_cast<MissionMessageType>(0xa),
                                           false);
    }
    pub::Player::DisplayMissionMessage(Target, MissionOffer, static_cast<MissionMessageType>(8), false);
}

// FUN_06f4bf30. Only for the "any state" call (State < 0); every controller event (type 4) is
// answered true. Unordered ElapsedTime / FlagTime compares skip the flag update and keep
// adding frame time. The TU38 body is FUN_06f0f150.
bool FRandomMissionObject::OnStateEvent(SMEventData const* Event, int const& State)
{
    if (PlayerStates.empty())
        return false;
    if (State >= 0)
        return false;
    using pub::Controller::EMessage;
    // A type 4 event's +0x08 int is the controller message id (see HandleMissionMessage); it is
    // read, and cast back to EMessage, only after the type test.
    SMControllerEvent const* Message = static_cast<SMControllerEvent const*>(Event);
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::SystemEnter)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Message->DataPtr);
        if (ElapsedTime <= Data->FlagTime)
            UpdatePlayerSystemFlags(Parms[2], Parms[0]);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::SpaceEnter)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Parms[1]) != PlayerStateMap.end())
        {
            GetOrCreatePlayer(Parms[1])->ClearObjectives();
            RefreshObjectives(Parms[1]);
        }
        if (ElapsedTime <= Data->FlagTime)
            UpdatePlayerSystemFlags(Parms[1], Parms[0]);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::Heartbeat)
    {
        bool bCount = false;
        if (pub::SinglePlayer() && PlayerState->ShipObject != nullptr)
            bCount = true;
        else if (!pub::SinglePlayer())
            bCount = true;
        if (bCount && ElapsedTime != -1.0f)
        {
            if (!(ElapsedTime >= Data->FlagTime))
            {
                ElapsedTime += *static_cast<float const*>(Message->DataPtr);
            }
            else
            {
                ElapsedTime = -1.0f;
                for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
                    PlayerStates[Index]->SetFlags(PlayerStates[Index]->GetFlags() &
                                                 ~PLAYER_FLAG_MISSION_AREA);
            }
        }
        FMissionObject::HandleMissionMessage(static_cast<EMessage>(Message->ControllerId),
                                             Message->DataPtr);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::AbortMission)
    {
        OnPlayerLeftGroup(static_cast<unsigned int const*>(Message->DataPtr));
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::ObjectDestroyed)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Parms[3]) == PlayerStateMap.end())
            return true;
        if (DecrementPlayerCount() > 0)
            return true;
        if (pub::SinglePlayer())
            FailMission(2);
        else
            FailMission(3);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::FormationLeaderChanged)
        return true;
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::PlayerLoad)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Parms[0]) == PlayerStateMap.end())
            return true;
        RejoinPlayer(Parms[0]);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::GroupMemberJoined)
    {
        AddGroupMember(*static_cast<unsigned int const*>(Message->DataPtr));
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::PlayerLogout)
    {
        unsigned int const Leaving = *static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Leaving) != PlayerStateMap.end())
            RemoveMissionPlayer(Leaving, 0xe);
        FMissionObject::HandleMissionMessage(static_cast<EMessage>(Message->ControllerId),
                                             Message->DataPtr);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::GroupMemberLeft)
    {
        unsigned int const Leaving = *static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Leaving) != PlayerStateMap.end())
            RemoveMissionPlayer(Leaving, 1);
        FMissionObject::HandleMissionMessage(static_cast<EMessage>(Message->ControllerId),
                                             Message->DataPtr);
        return true;
    }
    if (Event->get_event_type() == 4)
    {
        FMissionObject::HandleMissionMessage(static_cast<EMessage>(Message->ControllerId),
                                             Message->DataPtr);
        return true;
    }
    return false;
}

// =======================================================================================
// TU31 -- random encounter parameters and the factories of their two halves
// =======================================================================================

namespace
{

    // DAT_06fb7b40: the turn of a spawned formation towards its target, (double)(float)pi.
    double const GRandEncTurnAngle = 3.1415927410125732;

    // The weighted pick inlined three times in TU31 -- RandEncPickWeighted (RandomEncounters.h).
    template <typename TElement>
    typename std::list<TElement>::iterator PickWeighted(std::list<TElement>* Elements,
                                                        float TElement::*Weight)
    {
        return RandEncPickWeighted(Elements, Weight);
    }
}

// FUN_06efb550. +0x0c, +0x10 and +0x88 are left unset in retail (the port starts them at
// 0 / null).
FRandEncParams::FRandEncParams()
{
    PilotId.clear();
}

// FUN_06efb710: the member lists' destructors.
FRandEncParams::~FRandEncParams()
{
}

// FUN_06efc2d0. bForced is not read. Each formation takes a weighted position out of the copy
// of Positions, but its value is not read: every formation spawns at the same
// PlaceFacingAnchor point (moved when occupied), turned 180 degrees towards the target.
void FRandEncParams::Spawn(bool bForced)
{
    (void)bForced;
    ShipNameCounter = 0;
    ShipsSpawned = 0;
    Vector TargetPosition;
    Matrix TargetRotation;
    pub::SpaceObj::GetLocation(TargetObjectId, TargetPosition, TargetRotation);
    std::list<FRandEncPosition> FreePositions(Positions);

    float const TurnCos = static_cast<float>(std::cos(GRandEncTurnAngle));
    float const TurnSin = static_cast<float>(std::sin(GRandEncTurnAngle));
    FMatrix Turn;
    Turn.D[0][0] = TurnCos;
    Turn.D[0][1] = 0.0f;
    Turn.D[0][2] = TurnSin;
    Turn.D[1][0] = 0.0f;
    Turn.D[1][1] = 1.0f;
    Turn.D[1][2] = 0.0f;
    Turn.D[2][0] = -TurnSin;
    Turn.D[2][1] = 0.0f;
    Turn.D[2][2] = TurnCos;

    // The list, then the directive holder; the holder is released first at the end.
    FMissionDirectiveList LeaderDirectives;
    TRefHandle<FObjListDirective> Leader;
    CreateObjListDirective(OBJLIST_GOTO_SHIP, &Leader.Pointer);
    FObjListGotoShip* const Goto = static_cast<FObjListGotoShip*>(Leader.Pointer);
    Goto->SetTarget(&GotoTargetId, 200.0f, -1.0f);
    Goto->Op.bGotoCruise = 0;
    Goto->Op.bFlyThrough = 1;
    LeaderDirectives.Items.push_back(Leader.Pointer);
    Leader->AddRef();

    for (int Index = 0; Index < FormationsToSpawn; ++Index)
    {
        std::list<FRandEncPosition>::iterator const Picked =
            PickWeighted(&FreePositions, &FRandEncPosition::Weight);

        FTransform Target;
        std::memcpy(&Target.Rotation, &TargetRotation, sizeof(FMatrix));
        Target.Translation.X = TargetPosition.x;
        Target.Translation.Y = TargetPosition.y;
        Target.Translation.Z = TargetPosition.z;
        FTransform Around;
        SetMatrixIdentity(&Around.Rotation);
        Around.Translation.X = 0.0f;
        Around.Translation.Y = 0.0f;
        Around.Translation.Z = 0.0f;
        PlaceFacingAnchor(&Target, &SpawnAngle, &SpawnDistance, &Around);  // FUN_06f243d0

        FSystemInfo* const System = Player->GetSystemRecord();
        if (System != nullptr && EmptyPosRadius > 0)
        {
            unsigned int const SystemId = System->SystemId;
            float const Radius = static_cast<float>(EmptyPosRadius);
            Vector Point;
            Point.x = Around.Translation.X;
            Point.y = Around.Translation.Y;
            Point.z = Around.Translation.Z;
            bool bEmpty = true;
            pub::SpaceObj::IsPosEmpty(SystemId, Point, Radius, bEmpty);
            if (!bEmpty)
            {
                static_assert(sizeof(Transform) == sizeof(FTransform), "Transform is 0x30");
                Transform AroundTransform;
                std::memcpy(&AroundTransform, &Around, sizeof(Transform));
                pub::SpaceObj::PosSelectionType const Selection =
                    pub::SpaceObj::POS_SEL_3D_VOLUME;
                pub::SpaceObj::GetEmptyPos(SystemId, AroundTransform, Radius, Radius, Selection,
                                           Point);
                Around.Translation.X = Point.x;
                Around.Translation.Y = Point.y;
                Around.Translation.Z = Point.z;
            }
        }

        // Retail unlinks the picked node unconditionally; with nothing picked that is the
        // list's own head (undefined), which the port does not erase.
        FTransform Placement = Around;
        if (Picked != FreePositions.end())
            FreePositions.erase(Picked);
        ContentMathEngine()->MulMatrix(Placement.Rotation, Turn, Target.Rotation);
        SpawnFormation(&Placement, -1, &LeaderDirectives.Items, nullptr, nullptr, 0, DockTarget);
    }
    FormationsToSpawn = 0;
    if (Leader.Pointer != nullptr)
    {
        Leader.Pointer->Release();
        Leader.Pointer = nullptr;
    }
}

// FUN_06efba30. Longevity 24.0f / -1.0f is DAT_06fb7ae0 / DAT_06fb7adc. The ship class id of
// the chosen classes (FUN_06f580d0) forms a one-id line that is tried first, the classes
// themselves second (FUN_06f35890 with level -1); no arch drops the blocks and returns false.
// Retail also builds an unused name block (UnusedName: it has EH states) and an unused default
// name request (FUN_06f31650; no destructor, no EH state, not reproduced).
bool FRandEncParams::SpawnFormation(FTransform const* Placement, int Count,
                                    std::list<FObjListDirective*> const* Directives,
                                    std::list<FRandEncShipChoice> const* Choices,
                                    std::list<TString<0x30>>* Names, int Priority,
                                    unsigned int DockTarget)
{
    FSystemInfo* const System = Player->GetSystemRecord();
    if (System == nullptr)
        return false;
    std::list<FRandEncFormation>::iterator const Picked =
        PickWeighted(&Formations, &FRandEncFormation::Weight);
    if (Picked == Formations.end())
        return false;
    FRandEncFormation& Formation = *Picked;
    if (Count == -1)
    {
        int const Range = Formation.MaxCount - Formation.MinCount + 1;
        Formation.Count = std::rand() * Range / 32768 + Formation.MinCount;
    }
    else
    {
        Formation.Count = Count;
    }

    std::list<FContentNpcShipParams> Blocks;
    ShipsSpawned += Formation.Count;
    bool const bNoDockTarget = (DockTarget & 0x3fffffffu) == 0;
    for (int Index = 0; Index < Formation.Count; ++Index)
    {
        FContentNpcShipParams Params;
        // Never read; built right after the ship block, dies at the end of the iteration
        // (dtor FUN_06ef4e60).
        FNpcNameParams UnusedName;
        if (Names == nullptr)
        {
            char Nickname[0x60];
            std::sprintf(Nickname, "rand_enc_ship_%d", ShipNameCounter);
            ++ShipNameCounter;
            Params.Nickname = Nickname;
        }
        else
        {
            Params.Nickname = Names->front().Data;
            Names->pop_front();
        }
        Params.ControllerId = ControllerId;
        std::memcpy(&Params.Orientation, &Placement->Rotation, sizeof(FMatrix));
        Params.Position.x = Placement->Translation.X;
        Params.Position.y = Placement->Translation.Y;
        Params.Position.z = Placement->Translation.Z;
        Params.Longevity = bNoDockTarget ? 24.0f : -1.0f;
        Params.Priority = Priority;
        for (std::list<unsigned int>::iterator Label = Labels.begin(); Label != Labels.end();
             ++Label)
            Params.LabelIds.push_back(*Label);

        // Retail reads the classes of the list's head when nothing is picked (undefined);
        // the port leaves them empty.
        FEncounterShip MemberLine;
        if (Choices == nullptr)
        {
            std::list<FRandEncShipChoice>::iterator const Choice =
                PickWeighted(&Formation.ShipChoices, &FRandEncShipChoice::Weight);
            if (Choice != Formation.ShipChoices.end())
                MemberLine.ShipClasses = Choice->ShipClasses;
        }
        else
        {
            std::list<FRandEncShipChoice>::const_iterator Choice = Choices->begin();
            for (int Step = 0; Step < Index; ++Step)
                ++Choice;
            MemberLine.ShipClasses = Choice->ShipClasses;
        }
        ID_String ClassId;
        ClassId.clear();
        FindShipClassByMembers(&ClassId.Id, MemberLine.ShipClasses);
        FEncounterShip ClassLine;
        if (std::find(ClassLine.ShipClasses.begin(), ClassLine.ShipClasses.end(), ClassId.Id) ==
            ClassLine.ShipClasses.end())
            ClassLine.ShipClasses.insert(ClassLine.ShipClasses.end(), ClassId.Id);

        FNpcShipArch* Arch = PickFactionShipLineArch(static_cast<unsigned int>(AttackerRep),
                                                     &ClassLine, -1);
        if (Arch == nullptr)
            Arch = PickFactionShipLineArch(static_cast<unsigned int>(AttackerRep), &MemberLine, -1);
        if (Arch == nullptr)
            return false;

        // The name block dies right after the level store, before the leader's directives.
        {
            FNpcNameParams Name;
            PickNpcNameGender(static_cast<unsigned int>(AttackerRep), &Name);
            FNpcNameRequest Request;
            if (BuildNpcName(static_cast<unsigned int>(AttackerRep), &Request, &Name, true))
            {
                ApplyNpcShipFaction(static_cast<unsigned int>(AttackerRep), Name.Gender,
                                    &Params);
                ApplyNpcShipArch(&Params, Arch, nullptr, nullptr);
                if (Params.ReputationHandle != nullptr)
                    Params.ReputationHandle->Release();                     // FUN_06f28e10
                Params.ReputationHandle = nullptr;
                CreateNpcNameReputation(&Name, &Params.ReputationHandle);
            }
            Params.Level = Arch->Level;
        }

        // The leader's directives, each AddRef'ed.
        if (Index == 0)
        {
            for (std::list<FObjListDirective*>::const_iterator It = Directives->begin();
                 It != Directives->end(); ++It)
            {
                Params.Directives.Items.push_back(*It);
                (*It)->AddRef();
            }
        }
        Blocks.push_back(Params);                                          // FUN_06efc860
    }

    PlaceFormationBlocks(&Blocks, Formation.Formation, Placement);
    System->CreateNpcShips(&Blocks, DockTarget, nullptr);
    return true;
}

// FUN_06efb830.
bool __cdecl CreateRandEncParams(char const* Name, FRandEncParams** Out)
{
    FRandEncParams* Created = nullptr;
    if (_stricmp(Name, "TLParams") == 0)
        Created = new FRandEncTLParams();                                  // new 0xbc, 06f81bb0
    else if (_stricmp(Name, "SPParams") == 0)
        Created = new FRandEncSPParams();                                  // new 0xc4, 06f804d0
    *Out = Created;
    return Created != nullptr;
}

// FUN_06efb8e0.
bool __cdecl CreateRandEncParamsByType(int Type, FRandEncParams** Out)
{
    *Out = nullptr;
    if (Type == 2)
        *Out = new FRandEncSPParams();
    else if (Type == 3)
        *Out = new FRandEncTLParams();
    return *Out != nullptr;
}

// FUN_06efb980: the 0xc-byte objects are built inline.
bool __cdecl CreateRandEncPopulator(char const* Name, FRandEncPopulator** Out)
{
    FRandEncPopulator* Created = nullptr;
    if (_stricmp(Name, "TLPzr") == 0)
        Created = new FRandEncTLPopulator();
    else if (_stricmp(Name, "SPPzr") == 0)
        Created = new FRandEncSPPopulator();
    *Out = Created;
    return Created != nullptr;
}
