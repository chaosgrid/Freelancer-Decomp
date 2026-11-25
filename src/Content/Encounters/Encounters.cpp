// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll: encounters and the static game tables.
//   TU31 tail 06efc9b0-06efe100  missions\formations.ini database
//   TU32      06efe100-06efe8e0  LoadContentGameTables / UnloadContentGameTables

#include "Encounters/Encounters.h"
#include "Bases/BaseSupport.h"
#include "Bases/BaseDB.h"
#include "Bases/GenericScripts.h"
#include "Comms/Voices.h"
#include "Bases/LootProps.h"

#include "Core/ContentInternal.h"
#include "Core/ContentPlayerState.h"
#include "Core/ContentStaticIds.h"   // TU30 and TU32 static-init thunks
#include "NPCs/DockQueues.h"         // CountNpcShipsNear (TU53 FUN_06f67440)
#include "NPCs/FactionProps.h"
#include "NPCs/PilotPersonalities.h"
#include "NPCs/NpcShips.h"
#include "Encounters/Populators.h"         // FSpawnRequest
#include "RandomMissions/RandomMissionDB.h"
#include "RandomMissions/RandomMissionGen.h"
#include "Universe/SystemInfo.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_server.h"      // pub::SinglePlayer

#include <cstdlib>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------------------
// TU31 tail -- formations
// ---------------------------------------------------------------------------------------

// LAB_06efc9b0 constructs the map, FUN_06efc9e0 (atexit) destroys it.
std::map<unsigned int, FFormation> GFormations;

namespace
{

    I3DMathEngine* GetFormationMath()
    {
        return ContentMathEngine();
    }
}

// FUN_06efc7a0.
void SetMatrixIdentity(Matrix* Mat)
{
    Mat->E[2][2] = 1.0f;
    Mat->E[1][1] = 1.0f;
    Mat->E[0][0] = 1.0f;
    Mat->E[2][1] = 0.0f;
    Mat->E[2][0] = 0.0f;
    Mat->E[1][2] = 0.0f;
    Mat->E[1][0] = 0.0f;
    Mat->E[0][2] = 0.0f;
    Mat->E[0][1] = 0.0f;
}

// FUN_06efcac0.
int FFormation::CountPlayerSlots() const
{
    int Count = 0;
    for (std::vector<FFormationSlot>::const_iterator It = Slots.begin(); It != Slots.end(); ++It)
    {
        if (It->bPlayerSlot)
            ++Count;
    }
    return Count;
}

// FUN_06efcaf0.
int FFormation::CountShipSlots() const
{
    int Count = 0;
    for (std::vector<FFormationSlot>::const_iterator It = Slots.begin(); It != Slots.end(); ++It)
    {
        if (!It->bPlayerSlot)
            ++Count;
    }
    return Count;
}

// FUN_06efcb20 (hidden return pointer).
Vector FFormation::GetSlotPosition(unsigned int Index) const
{
    Vector Result;
    if (Index < Slots.size())
    {
        Vector Rotated;
        GetFormationMath()->TransformMatrix(Rotated, Orientation, Slots[Index].Position);
        Result.x = Rotated.x + Position.x;
        Result.y = Rotated.y + Position.y;
        Result.z = Rotated.z + Position.z;
        return Result;
    }
    Result.x = 0.0f;
    Result.y = 0.0f;
    Result.z = 0.0f;
    return Result;
}

// FUN_06efcbe0.
bool FFormation::GetSlotTransform(unsigned int Index, Vector* OutPosition,
                                  Matrix* OutOrientation) const
{
    if (Index >= Slots.size())
        return false;

    FFormationSlot const& Slot = Slots[Index];
    Vector Rotated;
    GetFormationMath()->TransformMatrix(Rotated, Orientation, Slot.Position);
    Vector World;
    World.x = Rotated.x + Position.x;
    World.y = Rotated.y + Position.y;
    World.z = Rotated.z + Position.z;
    *OutPosition = World;

    Matrix Combined;
    GetFormationMath()->MulMatrix(Combined, Orientation, Slot.Orientation);
    *OutOrientation = Combined;
    return true;
}

// FUN_06efccd0 (Index unchecked).
bool FFormation::IsPlayerSlot(int Index) const
{
    return Slots.data()[Index].bPlayerSlot;
}

// FUN_06efccf0: one [Formation] block; the caller already matched
// the header. Values: nickname, pos (ship slot, .rdata:06fb7b50), pl_pos (player slot).
void __cdecl LoadFormation(INI_Reader* Reader)
{
    FFormation Formation;
    Formation.Nickname.clear();
    Formation.Position.x = 0.0f;                       // FUN_06eb7810(0, 0, 0)
    Formation.Position.y = 0.0f;
    Formation.Position.z = 0.0f;
    SetMatrixIdentity(&Formation.Orientation);

    ID_String Key;
    Key.clear();

    // The orientation copied into every slot.
    Matrix SlotOrientation;
    SetMatrixIdentity(&SlotOrientation);

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            unsigned int Id = CreateID(Reader->get_value_string());
            Key.Id = Id;
            Formation.Nickname.Id = Id;
        }
        else if (Reader->is_value("pos"))
        {
            FFormationSlot Slot;
            Slot.Position.x = Reader->get_value_float(0);
            Slot.Position.y = Reader->get_value_float(1);
            Slot.Position.z = Reader->get_value_float(2);
            Slot.Orientation = SlotOrientation;
            Slot.bPlayerSlot = false;
            Formation.Slots.insert(Formation.Slots.end(), Slot);          // FUN_06efd1f0
        }
        else if (Reader->is_value("pl_pos"))
        {
            FFormationSlot Slot;
            Slot.Position.x = Reader->get_value_float(0);
            Slot.Position.y = Reader->get_value_float(1);
            Slot.Position.z = Reader->get_value_float(2);
            Slot.Orientation = SlotOrientation;
            Slot.bPlayerSlot = true;
            Formation.Slots.insert(Formation.Slots.end(), 1, Slot);       // FUN_06efd610
        }
    }

    // An existing key keeps its formation.
    GFormations.insert(std::pair<unsigned int const, FFormation>(Key.Id, Formation));
}

// FUN_06efd070.
void __cdecl ClearFormations()
{
    for (std::map<unsigned int, FFormation>::iterator It = GFormations.begin();
         It != GFormations.end(); ++It)
    {
        It->second.Slots.clear();
    }
    GFormations.clear();
}

// FUN_06efd1a0.
FFormation* __cdecl FindFormation(unsigned int const* FormationId)
{
    std::map<unsigned int, FFormation>::iterator It = GFormations.find(*FormationId);
    if (It == GFormations.end())
        return nullptr;
    return &It->second;
}

// ---------------------------------------------------------------------------------------
// TU30 -- the zone population block (SystemInfo.h). The ctor FUN_06ef8f90 is the member
// initialisers of FZonePopulation.
// ---------------------------------------------------------------------------------------

// FUN_06ef9000.
bool FZonePopulation::ParseValue(INI_Reader* Reader, FSystemInfo* InSystem, FSystemZone* Zone)
{
    System = InSystem;
    if (Reader->is_value("encounter"))
    {
        // "encounter = id, level, chance".
        unsigned int const EncounterId = CreateID(Reader->get_value_string(0));
        int const Level = static_cast<int>(Reader->get_value_float(1));     // ftol
        float const Chance = Reader->get_value_float(2);

        // The system's parameters for that id, bound to this zone and tagged with the level.
        FEncounterParameters Parameters;                                    // FUN_06ef3950
        InSystem->GetEncounterParameters(EncounterId, &Parameters);         // FUN_06f78ef0
        Parameters.ZoneId = Zone->ZoneId;
        Parameters.ApplyDifficulty(Level);                                  // FUN_06ef39a0

        // Parameters without faction weights take the "faction = name, weight" lines that
        // follow; the first other line is put back (set_state).
        if (Parameters.Factions.Items.empty())
        {
            weighted_vector<unsigned int> Factions;
            for (;;)
            {
                INI_Reader::State Saved;
                Reader->get_state(Saved);
                if (!Reader->read_value())
                    break;
                if (!Reader->is_value("faction"))
                {
                    Reader->set_state(Saved);
                    break;
                }
                unsigned int Faction;
                pub::Reputation::GetReputationGroup(Faction, Reader->get_value_string(0));
                float const Weight = Reader->get_value_float(1);
                Factions.Items.insert(Factions.Items.end(), 1,
                                      std::pair<unsigned int, float>(Faction, Weight));  // FUN_06f96770
            }
            Parameters.Factions = Factions;
        }

        // The copy (FUN_06ef9e50) with its chance goes at the end.
        FZoneEncounter Encounter(Parameters, Chance);
        Encounters.insert(Encounters.end(), 1, Encounter);                  // FUN_06efa050
    }
    else if (Reader->is_value("density_restriction"))
    {
        FDensityRestriction Restriction;
        Restriction.Type.clear();
        Restriction.MaxCount = Reader->get_value_int(0);
        Restriction.Type.Id = CreateID(Reader->get_value_string(1));
        DensityRestrictions.push_back(Restriction);
    }
    else if (Reader->is_value("population_additive"))
    {
        bPopulationAdditive = Reader->get_value_bool(0);
    }
    else if (Reader->is_value("tradelane_attack"))
    {
        // Double 0.01 (DAT_06fb7a88), then stored as float.
        TradelaneAttack = static_cast<float>(static_cast<double>(Reader->get_value_float(0)) * 0.01);
    }
    else if (Reader->is_value("max_battle_size"))
    {
        MaxBattleSize = Reader->get_value_int(0);
    }
    else if (Reader->is_value("relief_time"))
    {
        // Retail tests RepopTime (not ReliefTime): a negative repop time zeroes the relief
        // time just read.
        ReliefTime = Reader->get_value_float(0);
        if (RepopTime < 0.0f)
            ReliefTime = 0.0f;
    }
    else
    {
        return false;
    }
    bHasPopulationKeys = true;
    return true;
}

// FUN_06ef9dd0.
bool FZonePopulation::Validate()
{
    // Every encounter must refresh at time 0.
    for (std::vector<FZoneEncounter>::iterator It = Encounters.begin(); It != Encounters.end(); ++It)
    {
        if (!It->Refresh(0.0f))                                             // FUN_06ef52d0
        {
            bValid = false;
            return false;
        }
    }
    bValid = true;
    return true;
}

// TU30 static initialisers.
float GEncounterDensityRadiusSP = 3750.0f;                                  // LAB_06ef8f40
// LAB_06ef8f50: 850.0f (DAT_06fb7a60) doubled, plus 7500.0f (DAT_06fb7a68).
float GEncounterDensityRadiusMP = static_cast<float>(850.0 + 850.0 + 7500.0);
float GEncounterRestrictionCone = 3750.0f;                                  // LAB_06ef8f70
float GEncounterPlacementRange = 2700.0f;                                   // LAB_06ef8f80

namespace
{
    // DAT_06fd1c7c: FUN_06ef9940 writes -0.01f and then the clamped chance; nothing in
    // Content reads it.
    float GZoneSpawnChance;

    typedef std::vector<std::pair<unsigned int, float>> FEncounterPool;

    // Shared head of FUN_06ef95d0 / FUN_06ef9a60: every encounter refreshes at *Time; the
    // ones that fail are erased (bValid drops when the list becomes empty), the others spawn
    // their starred formations. Returns the ships created.
    int RefreshAndSpawnStarred(FZonePopulation* Population, FSpawnRequest const* Request,
                               float const* Time)
    {
        int Created = 0;
        std::vector<FZoneEncounter>::iterator It = Population->Encounters.begin();
        while (It != Population->Encounters.end())
        {
            if (It->Refresh(*Time))                                         // FUN_06ef52d0
            {
                bool const bStarred = true;
                Created += It->Spawn(Request, &bStarred, &Population->DensityRestrictions);
                ++It;
                continue;
            }
            It = Population->Encounters.erase(It);                          // FUN_06efa4f0 / FUN_06efa020
            if (Population->Encounters.empty())
                Population->bValid = false;
        }
        return Created;
    }

    // {encounter index, chance} for every encounter (capacity = count).
    void BuildEncounterPool(FZonePopulation const* Population, FEncounterPool* Pool)
    {
        Pool->reserve(Population->Encounters.size());
        for (std::vector<FZoneEncounter>::size_type Index = 0; Index < Population->Encounters.size(); ++Index)
        {
            Pool->insert(Pool->end(), 1,
                         std::pair<unsigned int, float>(static_cast<unsigned int>(Index),
                                                        Population->Encounters[Index].Chance));   // FUN_06f96770
        }
    }

    // A weighted pick; *bEmpty when the weights sum to 0 (no rand then).
    // The sum and the running total stay on the x87 stack; the sum and the scaled roll are
    // stored as floats. Returns Pool->size() when nothing is picked.
    FEncounterPool::size_type PickFromPool(FEncounterPool const* Pool, bool* bEmpty)
    {
        double Sum = 0.0;
        for (FEncounterPool::const_iterator It = Pool->begin(); It != Pool->end(); ++It)
            Sum += static_cast<double>(It->second);
        float const SumStored = static_cast<float>(Sum);
        // Only an exactly zero sum stops the draw (a NaN sum does not).
        *bEmpty = Sum == 0.0;
        if (*bEmpty)
            return Pool->size();
        float const Roll = static_cast<float>(static_cast<double>(rand()) *
                                              static_cast<double>(1.0f / 32768.0f) *
                                              static_cast<double>(SumStored));  // DAT_06fb4448
        double Running = 0.0;
        for (FEncounterPool::size_type Index = 0; Index < Pool->size(); ++Index)
        {
            Running += static_cast<double>((*Pool)[Index].second);
            if (static_cast<double>(Roll) <= Running)
                return Index;
        }
        return Pool->size();
    }
}

// FUN_06ef95d0.
int FZonePopulation::Populate(FSpawnRequest const* Request, float const* Time,
                              float const* DensityScale, unsigned int const* MaxDensity)
{
    int const StarredCount = RefreshAndSpawnStarred(this, Request, Time);

    int DrawnCount = 0;
    if (!RollSpawnChance(&Request->Placement, DensityScale, MaxDensity))  // FUN_06ef9940
        return StarredCount + DrawnCount;

    FEncounterPool Pool;
    BuildEncounterPool(this, &Pool);
    for (;;)
    {
        bool bEmpty;
        FEncounterPool::size_type const Picked = PickFromPool(&Pool, &bEmpty);
        // The loop ends on no pick or once an encounter has spawned (the roll of that extra
        // round is still drawn).
        if (bEmpty || Picked == Pool.size() || DrawnCount != 0)
            break;
        bool const bStarred = false;
        DrawnCount = Encounters[Pool[Picked].first].Spawn(Request, &bStarred, &DensityRestrictions);
        Pool.erase(Pool.begin() + Picked);
    }
    return StarredCount + DrawnCount;
}

// FUN_06ef9940.
bool FZonePopulation::RollSpawnChance(FSpaceObjPlacement const* Placement,
                                      float const* DensityScale, unsigned int const* MaxDensity)
{
    GZoneSpawnChance = -0.01f;
    unsigned int Limit = *MaxDensity;
    if (Limit == 0xffffffffu)
        Limit = static_cast<unsigned int>(Density);
    float const* Radius = pub::SinglePlayer() ? &GEncounterDensityRadiusSP : &GEncounterDensityRadiusMP;
    int const Ships = CountNpcShipsNear(System, Placement, Radius, &GEncounterRestrictionCone,
                                        nullptr);                            // FUN_06f67440
    if (static_cast<unsigned int>(Ships) >= Limit)                          // unsigned compare
        return false;

    // Limit is loaded as a 64-bit integer (unsigned); 1/3 = DAT_06fb7ad8. All in x87 precision.
    double const Third = static_cast<double>(1.0f / 3.0f);
    double const Numerator = (static_cast<double>(Limit) * static_cast<double>(*DensityScale) -
                              static_cast<double>(Ships)) * Third;
    double const Denominator = static_cast<double>(RepopTime) / static_cast<double>(*DensityScale) * Third;
    float Chance = static_cast<float>(Numerator / Denominator);
    if (Chance > 0.0f)
    {
        if (!(Chance < 1.0f))
            Chance = 1.0f;
    }
    else
    {
        Chance = 0.0f;
    }
    GZoneSpawnChance = Chance;
    // A roll at or above the chance fails.
    return static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) <
           static_cast<double>(Chance);
}

// FUN_06ef9a60.
int FZonePopulation::PopulateUpTo(FSpawnRequest const* Request, float const* Time,
                                  unsigned int const* MaxCount)
{
    int const StarredCount = RefreshAndSpawnStarred(this, Request, Time);

    int DrawnCount = 0;
    FEncounterPool Pool;
    BuildEncounterPool(this, &Pool);
    for (;;)
    {
        bool bEmpty;
        FEncounterPool::size_type const Picked = PickFromPool(&Pool, &bEmpty);
        if (bEmpty || Picked == Pool.size())
            break;
        // The total compares unsigned.
        if (static_cast<unsigned int>(DrawnCount + StarredCount) >= *MaxCount)
            break;
        FZoneEncounter& Encounter = Encounters[Pool[Picked].first];
        Encounter.Refresh(*Time);                                           // result ignored
        bool const bStarred = false;
        int const Created = Encounter.Spawn(Request, &bStarred, &DensityRestrictions);
        if (Created != 0)
        {
            // A successful encounter stays in the pool.
            DrawnCount += Created;
            continue;
        }
        Pool.erase(Pool.begin() + Picked);
    }
    return StarredCount + DrawnCount;
}

// ---------------------------------------------------------------------------------------
// TU32
// ---------------------------------------------------------------------------------------

// FUN_06efe300. One INI_Reader for every file; the result of OpenContentDataIni
// (FUN_06f24280) is never tested, the loader runs regardless.
void __cdecl LoadContentGameTables()
{
    INI_Reader Reader;

    OpenContentDataIni("audio\\voices_space_male.ini", &Reader);
    LoadVoiceLines(&Reader);                            // FUN_06f67840
    Reader.reset();
    LoadVoiceProperties(&Reader);                       // FUN_06fa6760
    Reader.close();

    OpenContentDataIni("audio\\voices_space_female.ini", &Reader);
    LoadVoiceLines(&Reader);
    Reader.reset();
    LoadVoiceProperties(&Reader);
    Reader.close();

    OpenContentDataIni("audio\\voices_recognizable.ini", &Reader);
    LoadVoiceLines(&Reader);
    Reader.reset();
    LoadVoiceProperties(&Reader);
    Reader.close();

    LoadFactionEmpathy("missions\\empathy.ini");        // FUN_06f50880

    OpenContentDataIni("missions\\voice_properties.ini", &Reader);
    LoadVoiceProperties(&Reader);
    Reader.close();

    OpenContentDataIni("missions\\formations.ini", &Reader);
    while (Reader.read_header())
    {
        if (Reader.is_header("Formation"))
            LoadFormation(&Reader);
    }
    Reader.close();

    OpenContentDataIni("missions\\pilots_population.ini", &Reader);
    LoadPilotPersonalities(&Reader);
    Reader.close();

    OpenContentDataIni("missions\\pilots_story.ini", &Reader);
    LoadPilotPersonalities(&Reader);
    Reader.close();

    OpenContentDataIni("missions\\npcships.ini", &Reader);
    LoadNpcShipArchs(&Reader, &GNpcShipArchs);          // FUN_06f32510, DAT_06fd0830
    Reader.close();

    OpenContentDataIni("missions\\npcships_test.ini", &Reader);
    LoadNpcShipArchs(&Reader, &GNpcShipArchs);
    Reader.close();

    OpenContentDataIni("missions\\faction_prop.ini", &Reader);
    LoadFactionProps(&Reader);                          // FUN_06f4f870
    Reader.close();

    OpenContentDataIni("equipment\\commodities_per_faction.ini", &Reader);
    LoadFactionGoods(&Reader);                          // FUN_06f503d0
    Reader.close();

    OpenContentDataIni("missions\\shipclasses.ini", &Reader);
    LoadShipClasses(&Reader);
    Reader.close();

    OpenContentDataIni("ships\\shiparch.ini", &Reader);
    LoadShipMissionProperties(&Reader);
    Reader.close();

    OpenContentDataIni("missions\\lootprops.ini", &Reader);
    LoadLootProps(&Reader);                             // FUN_06f04cb0
    Reader.reset();
    LoadPhantomLoot(&Reader);                           // FUN_06f050e0
    Reader.close();

    LoadGcsExclusions("scripts\\GCS\\GCSExcl.ini");      // FUN_06f02330

    OpenContentDataIni("missions\\PTough.ini", &Reader);
    LoadPlayerToughnessScale(&Reader);
    Reader.close();

    OpenContentDataIni("missions\\RankDiff.ini", &Reader);
    LoadRankDifficulties(&Reader);
    Reader.close();

    OpenContentDataIni("missions\\news.ini", &Reader);
    LoadNewsItems(&Reader);                             // FUN_06eb44c0
    Reader.close();
}

// FUN_06efe870 (tail jump into FUN_06efd070).
void __cdecl UnloadContentGameTables()
{
    ClearRankDifficulties();
    UnloadContentBases();                               // FUN_06eabe80
    ClearSystemInfos();
    ClearShipClasses();
    UnloadFactionEmpathy();                             // FUN_06f50ed0
    UnloadFactionProps();                               // FUN_06f506a0
    ClearNpcShipArchs();                                // FUN_06f32850
    ClearPilotPersonalities();
    ClearFormations();
}
