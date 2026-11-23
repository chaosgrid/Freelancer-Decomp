// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RandomMissions/RandomMissionGen.h"

#include "Core/ContentInternal.h"
#include "Core/ContentStaticIds.h"
#include "Encounters/RandomEncounters.h"
#include "RandomMissions/RandomMissionDB.h"
#include "Bases/Cargo.h"              // GetFactionShipLineArchs (TU44 FUN_06f357d0)
#include "Encounters/EncounterParams.h"    // FEncounterShip
#include "Encounters/Encounters.h"         // FindFormation (FUN_06efd1a0)
#include "NPCs/FactionProps.h"       // GetFactionProps, GetFactionNames
#include "NPCs/NpcNames.h"           // FNpcNameRequest, BuildNpcName
#include "NPCs/NpcShips.h"           // FNpcShipArch
#include "NPCs/PilotPersonalities.h" // GetPilotPersonality (FUN_06f3e020)
#include "RandomMissions/RandomMission.h"      // TRmRef overloads for the offer
#include "RandomMissions/RandomMissionOffer.h"
#include "Universe/SystemInfo.h"         // FSystemInfo::SystemId (GetRmZoneAtPosition)
#include "common_ini.h"
#include "common_misc.h"        // random_inside_ring (FUN_06f9c100)
#include "common_server.h"
#include "common_weighted_vector.h"
#include "msvcrt_compat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <stdlib.h>
#include <string.h>

// =======================================================================================
// TU63 -- solar formation pick
// =======================================================================================

// FUN_06f97d90. Walks the formations in key order; the 0x40th candidate ends the walk at
// once. The index is (rand() * count) / 0x8000 as a signed shift.
FRmSolarFormation* __cdecl PickSolarFormation(std::vector<int> const* SpawnTypes)
{
    FRmSolarFormation* Candidates[0x40];
    int CandidateCount = 0;

    std::map<unsigned int, FRmSolarFormation>* Formations = GetSolarFormations();
    std::map<unsigned int, FRmSolarFormation>::iterator It = Formations->begin();
    if (It == Formations->end())
        return nullptr;

    for (; It != Formations->end(); ++It)
    {
        FRmSolarFormation* Formation = &It->second;
        std::vector<int> Mandatory;
        std::vector<int> Optional;
        Formation->CollectMandatorySlotTypes(&Mandatory);
        Formation->CollectOptionalSlotTypes(&Optional);

        bool bMatches = true;
        for (std::vector<int>::const_iterator Type = Mandatory.begin(); Type != Mandatory.end();
             ++Type)
        {
            if (std::find(SpawnTypes->begin(), SpawnTypes->end(), *Type) == SpawnTypes->end())
            {
                bMatches = false;
                break;
            }
        }
        if (bMatches)
        {
            for (std::vector<int>::const_iterator Type = SpawnTypes->begin();
                 Type != SpawnTypes->end(); ++Type)
            {
                if (std::find(Mandatory.begin(), Mandatory.end(), *Type) != Mandatory.end())
                    continue;
                if (std::find(Optional.begin(), Optional.end(), *Type) == Optional.end())
                {
                    bMatches = false;
                    break;
                }
            }
        }
        if (!bMatches)
            continue;

        Candidates[CandidateCount] = Formation;
        ++CandidateCount;
        if (CandidateCount == 0x40)
            break;
    }

    if (CandidateCount <= 0)
        return nullptr;

    int const Product = rand() * CandidateCount;
    int const Index = (Product + ((Product >> 31) & 0x7fff)) >> 15;
    return Candidates[Index];
}

// FUN_06f98010. Both radius compares are ordered only (-1.0 is DAT_06fb5e84). Navmap cell:
// 133333.0 is DAT_06fbcd10, 0.25 DAT_06fbbb5c; Size and Cell are stored as float, the
// quotients are floored as doubles.
unsigned int __cdecl GetRmZoneAtPosition(FSystemInfo const* System, Vector const* Position)
{
    Vector const Where = *Position;
    unsigned int Zones[0x14];
    unsigned int ZoneCount = 0;
    pub::System::InZones(System->SystemId, Where, 1.0f, Zones, 0x14, ZoneCount);

    std::vector<std::pair<unsigned int, float>> Named;
    if (ZoneCount > 0)
        Named.reserve(ZoneCount);
    for (unsigned int Index = 0; Index < ZoneCount; ++Index)
    {
        unsigned int NameId = 0;
        if (pub::Zone::GetName(Zones[Index], NameId) == 0 && NameId != 0)
        {
            float const Radius = pub::Zone::GetRadius(Zones[Index]);
            Named.insert(Named.end(), 1, std::pair<unsigned int, float>(Zones[Index], Radius));
        }
    }

    unsigned int BestZone = 0;
    float BestRadius = -1.0f;
    for (std::vector<std::pair<unsigned int, float>>::const_iterator It = Named.begin();
         It != Named.end(); ++It)
    {
        if (It->second < BestRadius || BestRadius == -1.0f)
        {
            BestRadius = It->second;
            BestZone = It->first;
        }
    }
    if (BestZone != 0)
        return BestZone;

    float Scale = 0.0f;
    pub::GetNavMapScale(System->SystemId, Scale);
    float const Size = 133333.0f / Scale;
    float const Cell = Size * 0.25f;
    int const Column = static_cast<int>(
        std::floor((static_cast<double>(Where.x) + static_cast<double>(Size)) /
                   static_cast<double>(Cell)));
    int const Row = static_cast<int>(
        std::floor((static_cast<double>(Where.z) + static_cast<double>(Size)) /
                   static_cast<double>(Cell)));
    return static_cast<unsigned int>(Row + Column * 8 + 1);
}

// =======================================================================================
// TU63 -- NpcRankToDiff / Diff2Money
// =======================================================================================

std::map<int, std::vector<float>> GRmNpcRankToDifficulty;   // DAT_06fd3cd4
std::map<float, int> GRmDiff2Money;                        // DAT_06fd3cec

// FUN_06f98540 -- [RankAndFormationSizeToDifficulty] "NPCRank = rank, diff1, diff2, ...".
// A repeated rank appends to the existing entry.
void __cdecl LoadNpcRankToDifficulty(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("RankAndFormationSizeToDifficulty") || !Reader->read_value())
            continue;
        do
        {
            if (Reader->is_value("NPCRank"))
            {
                int Rank = Reader->get_value_int(0);
                GRmNpcRankToDifficulty.insert(std::make_pair(Rank, std::vector<float>()));
                std::vector<float>& Difficulties = GRmNpcRankToDifficulty[Rank];
                for (unsigned int Index = 1; !Reader->is_value_empty(Index); ++Index)
                    Difficulties.insert(Difficulties.end(), Reader->get_value_float(Index));
            }
        } while (Reader->read_value());
    }
}

// FUN_06f98380: linear interpolation between the entries around Difficulty on the x87
// stack, truncated before RoundRmMoney; the money difference and lo.money are ints.
unsigned int __cdecl RmDifficultyToMoney(float Difficulty)
{
    std::map<float, int>::iterator Hi = GRmDiff2Money.begin();
    while (Hi != GRmDiff2Money.end() && !(Hi->first > Difficulty))
        ++Hi;

    if (Hi == GRmDiff2Money.end())
    {
        // Retail returns the map head node's unconstructed value slot (+0x10); the port
        // returns 0.
        return 0;
    }

    // Retail decrements begin() (FUN_06f00610, no nil check) onto the head node and reads
    // its unconstructed key/value (or, when begin() is the root, lo == hi: NaN -> 0). This
    // needs Difficulty below the first Diff2Money key (0 in DATA); the port interpolates
    // from {0, 0}.
    float LoKey = 0.0f;
    int LoMoney = 0;
    if (Hi != GRmDiff2Money.begin())
    {
        std::map<float, int>::iterator Lo = Hi;
        --Lo;
        LoKey = Lo->first;
        LoMoney = Lo->second;
    }

    double Fraction = (static_cast<double>(Difficulty) - static_cast<double>(LoKey)) /
                      (static_cast<double>(Hi->first) - static_cast<double>(LoKey));
    int Money = static_cast<int>(Fraction * static_cast<double>(Hi->second - LoMoney) +
                                 static_cast<double>(LoMoney));
    return RoundRmMoney(static_cast<unsigned int>(Money));
}

// TU44 FUN_06f35b50: for Amount > 100, keep the two leading decimal digits and, when the
// leading digit is below 5, add 5 (third digit >= 5) or 0 in the unit below the second.
unsigned int __cdecl RoundRmMoney(unsigned int Amount)
{
    if (Amount > 100)
    {
        unsigned int Digits[16];
        unsigned int Power = 1;
        int Count = 0;
        for (; Amount != 0; Amount = Amount / 10)
        {
            Power = Power * 10;
            Digits[Count] = Amount % 10;
            ++Count;
        }
        unsigned int Second = (Power / 10) / 10;
        Amount = Digits[Count - 1] * (Power / 10) + Digits[Count - 2] * Second;
        if (Digits[Count - 1] < 5)
        {
            unsigned int Third = Digits[Count - 3] < 5 ? 0u : 5u;
            Amount = Amount + Third * (Second / 10);
        }
    }
    return Amount;
}

// =======================================================================================
// TU63 -- all reputation groups (DAT_06fd3cb8)
// =======================================================================================

FRmAllGroupsEnumerator* GRmAllGroups = nullptr;   // DAT_06fd3d8c

// FUN_06f98bf0
FRmAllGroupsEnumerator::FRmAllGroupsEnumerator()
    : bEnumerated(false)
{
    GRmAllGroups = this;
}

// FUN_06f98c90.
FRmAllGroupsEnumerator::~FRmAllGroupsEnumerator()
{
    GRmAllGroups = nullptr;
}

// LAB_06f98c20.
char FRmAllGroupsEnumerator::Execute(unsigned int GroupId)
{
    std::vector<unsigned int>::iterator It = Groups.begin();
    for (; It != Groups.end(); ++It)
    {
        if (*It == GroupId)
            break;
    }
    if (It == Groups.end())
        Groups.insert(Groups.end(), GroupId);
    return 1;
}

static FRmAllGroupsEnumerator GRmAllGroupsInstance;   // DAT_06fd3cb8

// =======================================================================================
// TU63 -- generator root (DAT_06fd3c78)
// =======================================================================================

FRandomMissionGenerator* GRandomMissionGenerator = nullptr;   // DAT_06fcf25c

// Published before the members are built.
FRmGeneratorRegistration::FRmGeneratorRegistration(FRandomMissionGenerator* Generator)
{
    GRandomMissionGenerator = Generator;
}

// FUN_06f9fc10 (also inlined in the generator dtor, after its members).
FRmGeneratorRegistration::~FRmGeneratorRegistration()
{
    GRandomMissionGenerator = nullptr;
}

// FUN_06f9d550. Every table is opened with FUN_06f24280 and closed only when it opened.
FRandomMissionGenerator::FRandomMissionGenerator()
    : FRmGeneratorRegistration(this)
    , bLoaded(false)
    , bLinked(false)
    , RootNodeId(0xffffffffu)
{
    INI_Reader Reader;
    if (OpenContentDataIni("RandomMissions\\NpcRankToDiff.ini", &Reader))
    {
        LoadNpcRankToDifficulty(&Reader);                               // FUN_06f98540
        Reader.close();
    }

    // Read inline; FUN_06fa0860 is a unique insert.
    if (OpenContentDataIni("RandomMissions\\Diff2Money.ini", &Reader))
    {
        while (Reader.read_header())
        {
            if (!Reader.is_header("Diff2Money") || !Reader.read_value())
                continue;
            do
            {
                if (Reader.is_value("Diff2Money"))
                {
                    float Difficulty = Reader.get_value_float(0);
                    int Money = Reader.get_value_int(1);
                    GRmDiff2Money.insert(std::make_pair(Difficulty, Money));
                }
            } while (Reader.read_value());
        }
        Reader.close();
    }

    if (OpenContentDataIni("RandomMissions\\RMLootInfo.ini", &Reader))
    {
        LoadRmLootInfo(&Reader);                                        // FUN_06f566a0
        Reader.close();
    }
    if (OpenContentDataIni("RandomMissions\\SolarFormations.ini", &Reader))
    {
        LoadSolarFormations(&Reader);                                   // FUN_06f58f30
        Reader.close();
    }
    if (OpenContentDataIni("RandomMissions\\KillableSolars.ini", &Reader))
    {
        LoadKillableSolars(&Reader);                                    // FUN_06f95040
        Reader.close();
    }
    if (OpenContentDataIni("RandomMissions\\VignetteCriticalLoot.ini", &Reader))
    {
        LoadVignetteCriticalLoot(&Reader);                              // FUN_06f95890
        Reader.close();
    }
    if (OpenContentDataIni("RandomMissions\\VignetteParams.ini", &Reader))
    {
        // Called directly, not through the vtable.
        Load(&Reader);
        Reader.close();
    }
}

// FUN_06f9d970; DAT_06fcf25c is cleared last by the registration base (FUN_06f9fc10).
FRandomMissionGenerator::~FRandomMissionGenerator()
{
    if (bLoaded)
    {
        bLoaded = false;
        bLinked = false;
        RootNodeId = 0xffffffffu;
        LeafNodeIds.clear();
        Nodes.clear();
    }
}

// LAB_06f9f600 (vtable slot 2); the map clear is inlined (FUN_06fa38c0 / FUN_06fa3470).
void FRandomMissionGenerator::Unload()
{
    bLoaded = false;
    bLinked = false;
    RootNodeId = 0xffffffffu;
    LeafNodeIds.clear();
    Nodes.clear();
}

// FUN_06f9ecb0 (vtable slot 1). The node headers run an inline copy of LAB_06f99e40; any
// other header still inserts a default node keyed -1. FUN_06fa0e70 is a unique insert: an
// existing key keeps its node.
void FRandomMissionGenerator::Load(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        FRmVignetteNode Node;
        if (Reader->is_header("DecisionNode") || Reader->is_header("DocumentationNode") ||
            Reader->is_header("DataNode"))
        {
            Node.Load(Reader);
        }
        Nodes.insert(std::make_pair(Node.NodeId, Node));
    }
    Link();                                                             // FUN_06f9dd50
    bLoaded = true;
}

// FUN_06f9dd50. The parent pass goes through map operator[] (FUN_06f9fc20), so a child id
// with no node of its own gets a default node (NodeId -1) inserted; afterwards every child
// id is a key and the later finds always hit.
void FRandomMissionGenerator::Link()
{
    if (bLinked)
        return;

    for (std::map<unsigned int, FRmVignetteNode>::iterator It = Nodes.begin();
         It != Nodes.end(); ++It)
    {
        FRmVignetteNode& Parent = It->second;
        for (unsigned int Index = 0; Index < Parent.Children.size(); ++Index)
        {
            FRmVignetteNode& Child = Nodes[Parent.Children[Index]];
            Child.Parents.insert(Child.Parents.end(), Parent.NodeId);  // FUN_06eb5770
        }
    }

    RootNodeId = Nodes.begin()->first;
    for (;;)
    {
        FRmVignetteNode& Node = Nodes.find(RootNodeId)->second;           // FUN_06f7b0f0
        if (Node.Parents.empty())
            break;
        RootNodeId = Node.Parents[0];
    }

    for (std::map<unsigned int, FRmVignetteNode>::iterator It = Nodes.begin();
         It != Nodes.end(); ++It)
    {
        FRmVignetteNode& Node = It->second;
        if (Node.bImplemented)
            continue;
        for (unsigned int Index = 0; Index < Node.Children.size(); ++Index)
        {
            FRmVignetteNode& Child = Nodes.find(Node.Children[Index])->second;
            if (Child.bImplemented)
            {
                Child.bImplemented = false;
                DisableChildren(&Child.Children);
            }
        }
    }

    for (std::map<unsigned int, FRmVignetteNode>::iterator It = Nodes.begin();
         It != Nodes.end(); ++It)
    {
        FRmVignetteNode& Node = It->second;
        if (Node.bImplemented && Node.Children.empty())
            LeafNodeIds.insert(LeafNodeIds.end(), Node.NodeId);        // FUN_06f937c0
    }

    PropagateDifficultyWindows(RootNodeId);
    bLinked = true;
}

// FUN_06f9da60.
void FRandomMissionGenerator::DisableChildren(std::vector<unsigned int> const* ChildIds)
{
    for (unsigned int Index = 0; Index < ChildIds->size(); ++Index)
    {
        FRmVignetteNode& Child = Nodes.find((*ChildIds)[Index])->second;
        if (Child.bImplemented)
        {
            Child.bImplemented = false;
            DisableChildren(&Child.Children);
        }
    }
}

// FUN_06f9dad0. The equal-window search of an empty list is retail's (it never matches).
// The recursion goes into the child's NodeId (map node +0x18), not the child id. Children
// created by Link's operator[] carry NodeId -1 there; retail then searches -1 (and reads the
// map head node when no key -1 exists).
void FRandomMissionGenerator::PropagateDifficultyWindows(unsigned int NodeId)
{
    FRmVignetteNode& Node = Nodes.find(NodeId)->second;
    if (Node.Windows.Windows.empty())
    {
        bool bFound = false;
        for (std::list<FRmDifficultyWindow>::iterator It = Node.Windows.Windows.begin();
             It != Node.Windows.Windows.end(); ++It)
        {
            if (It->Equals(&Node.Params.Difficulty))
            {
                bFound = true;
                break;
            }
        }
        if (!bFound)
            Node.Windows.PushBack(&Node.Params.Difficulty);
    }

    for (unsigned int Index = 0; Index < Node.Children.size(); ++Index)
    {
        FRmVignetteNode& Child = Nodes.find(Node.Children[Index])->second;

        FRmDifficultyWindowList Shifted;                                // FUN_06fa0350 copy
        Shifted.Windows = Node.Windows.Windows;
        if (Shifted.Windows.empty())
        {
            Shifted.AddUnique(&Child.Params.Difficulty);
        }
        else
        {
            for (std::list<FRmDifficultyWindow>::iterator It = Shifted.Windows.begin();
                 It != Shifted.Windows.end(); ++It)
            {
                It->Add(&Child.Params.Difficulty);
            }
        }
        for (std::list<FRmDifficultyWindow>::iterator It = Shifted.Windows.begin();
             It != Shifted.Windows.end(); ++It)
        {
            Child.Windows.AddUnique(&*It);
        }

        PropagateDifficultyWindows(Child.NodeId);
    }
}

// =======================================================================================
// TU63 -- vignette node pieces
// =======================================================================================

FRmDifficultyWindow::FRmDifficultyWindow()
    : Min(0.0f)
    , Max(0.0f)
{
}

// FUN_06f9d500
FRmDifficultyWindow::FRmDifficultyWindow(float InMin, float InMax)
    : Min(InMin)
    , Max(InMax)
{
}

// LAB_06f9cff0 (sdtor).
FRmDifficultyWindow::~FRmDifficultyWindow()
{
}

// LAB_06f9cfa0. Unordered -> false.
bool FRmDifficultyWindow::IsInRange(float Difficulty) const
{
    return Min <= Difficulty && Difficulty <= Max;
}

// LAB_06f98cc0; all three tests go through vtable slot 1.
bool FRmDifficultyWindow::Overlaps(FRmDifficultyWindow const* Other) const
{
    if (IsInRange(Other->Min))
        return true;
    if (IsInRange(Other->Max))
        return true;
    if (Other->IsInRange(Min))
        return true;
    return false;
}

// LAB_06f98d10: "difficulty = min, max".
void FRmDifficultyWindow::Parse(INI_Reader* Reader)
{
    Min = Reader->get_value_float(0);
    Max = Reader->get_value_float(1);
}

// LAB_06f9cfd0
void FRmDifficultyWindow::Set(float InMin, float InMax)
{
    Min = InMin;
    Max = InMax;
}

// FUN_06f98dc0. Unordered -> false.
bool FRmDifficultyWindow::Equals(FRmDifficultyWindow const* Other) const
{
    return Other->Min == Min && Other->Max == Max;
}

// FUN_06f9dcb0.
void FRmDifficultyWindow::Add(FRmDifficultyWindow const* Other)
{
    Min = Other->Min + Min;
    Max = Other->Max + Max;
}

// FUN_06f9dcd0.
FRmDifficultyWindowList::~FRmDifficultyWindowList()
{
}

// FUN_06f98d40.
void FRmDifficultyWindowList::AddUnique(FRmDifficultyWindow const* Window)
{
    for (std::list<FRmDifficultyWindow>::iterator It = Windows.begin(); It != Windows.end();
         ++It)
    {
        if (Window->Min == It->Min && Window->Max == It->Max)
            return;
    }
    Windows.push_back(*Window);                                         // FUN_06fa4160
}

// FUN_06f9f860.
void FRmDifficultyWindowList::PushBack(FRmDifficultyWindow const* Window)
{
    Windows.push_back(*Window);
}

FRmOfferTextEntry::FRmOfferTextEntry()
    : StrId(0)
    , Form(3)
{
}

// Inlined in FUN_06f998c0.
FRmOfferText::FRmOfferText()
    : bAppend(false)
{
}

// FUN_06f99010. From value 1 on: a numeric token is the entry's StrId, followed by parameter
// names; the first name not in GRmTextParamNames closes the entry and is read again as the
// first token of the next one. A non-numeric token ("singular" / "plural" / anything else)
// leaves the entry open.
void FRmOfferText::Parse(INI_Reader* Reader)
{
    char const* Mode = Reader->get_value_string(0);
    if (_stricmp(Mode, "append") == 0)
        bAppend = true;
    else if (_stricmp(Mode, "replace") == 0)
        bAppend = false;

    unsigned int Index = 1;
    bool bNewEntry = true;
    FRmOfferTextEntry* Entry = nullptr;
    while (!Reader->is_value_empty(Index))
    {
        if (bNewEntry)
        {
            bNewEntry = false;
            Entries.insert(Entries.end(), FRmOfferTextEntry());         // FUN_06fa1eb0
            Entry = &Entries.back();
        }

        char const* Token = Reader->get_value_string(Index);
        if (MsvcrtAtoi(Token) != 0)
        {
            Entry->StrId = Reader->get_value_int(Index);
            ++Index;
            while (!Reader->is_value_empty(Index))
            {
                char const* Name = Reader->get_value_string(Index);
                int Row = 0;
                for (; Row < 15; ++Row)
                {
                    if (GRmTextParamNames[Row].Name != nullptr &&
                        _stricmp(Name, GRmTextParamNames[Row].Name) == 0)
                        break;
                }
                if (Row == 15)
                    break;
                Entry->Params.insert(Entry->Params.end(), GRmTextParamNames[Row].Value);
                ++Index;
            }
            bNewEntry = true;
        }
        else
        {
            if (_stricmp(Token, "singular") == 0)
                Entry->Form = 1;
            else if (_stricmp(Token, "plural") == 0)
                Entry->Form = 2;
            ++Index;
        }
    }
}

// LAB_06f99390.
bool FRmOfferText::IsEmpty() const
{
    return Entries.empty();
}

// FUN_06f993c0. Form 1 (singular) / 2 (plural) entries build only when the group named by
// their parameter 1 (offer group) or 2 (hostile group) matches; Form 3 always builds, any
// other Form is skipped.
void FRmOfferText::Build(bool bOfferGroupSingular, bool bHostileGroupSingular, FmtStr* Out,
                         IRmTextParamSource* Source)
{
    bool const bFlag1 = bOfferGroupSingular;
    bool const bFlag2 = bHostileGroupSingular;
    for (std::vector<FRmOfferTextEntry>::iterator Entry = Entries.begin(); Entry != Entries.end();
         ++Entry)
    {
        bool const bHas1 = std::find(Entry->Params.begin(), Entry->Params.end(), 1) !=
                           Entry->Params.end();
        bool bBuild = false;
        if (Entry->Form == 1)
        {
            if (bHas1 && bFlag1)
                bBuild = true;
            else if (std::find(Entry->Params.begin(), Entry->Params.end(), 2) !=
                     Entry->Params.end())
                bBuild = bFlag2;
        }
        else if (Entry->Form == 2)
        {
            if (bHas1 && !bFlag1)
                bBuild = true;
            else if (std::find(Entry->Params.begin(), Entry->Params.end(), 2) !=
                     Entry->Params.end())
                bBuild = !bFlag2;
        }
        else if (Entry->Form == 3)
        {
            bBuild = true;
        }
        if (!bBuild)
            continue;

        FmtStr Part;
        Part.begin_mad_lib(static_cast<unsigned int>(Entry->StrId));
        for (std::vector<int>::iterator Param = Entry->Params.begin();
             Param != Entry->Params.end(); ++Param)
            Source->GetTextParam(*Param, &Part);
        Part.end_mad_lib();
        Out->append_fmt_str(Part);
    }
}

// FUN_06f99560. A repeated objective re-parses the existing text. Retail quirk: an unknown
// name uses the stack slot holding this (the saved ECX) as the key.
char FRmObjectiveText::Parse(INI_Reader* Reader, unsigned int Index)
{
    char const* Name = Reader->get_value_string(Index);
    int Objective = 0;
    if (_stricmp(Name, "guide_to_main_battle") == 0)
        Objective = 2;
    else if (_stricmp(Name, "guide_to_pre_battle") == 0)
        Objective = 1;
    else if (_stricmp(Name, "guide_to_main_from_pre") == 0)
        Objective = 3;
    else if (_stricmp(Name, "kill_big_solar") == 0)
        Objective = 4;
    else if (_stricmp(Name, "kill_other_solars") == 0)
        Objective = 5;
    else if (_stricmp(Name, "pb_kill_ships") == 0)
        Objective = 0x13;
    else if (_stricmp(Name, "pb_kill_big_ships") == 0)
        Objective = 0x14;
    else if (_stricmp(Name, "pb_dont_kill_ships") == 0)
        Objective = 0x15;
    else if (_stricmp(Name, "mb_kill_ships") == 0)
        Objective = 0x16;
    else if (_stricmp(Name, "mb_kill_big_ships") == 0)
        Objective = 0x17;
    else if (_stricmp(Name, "mb_dont_kill_ships") == 0)
        Objective = 0x18;
    else if (_stricmp(Name, "return_to_battle") == 0)
        Objective = 9;
    else if (_stricmp(Name, "destroy_loot") == 0)
        Objective = 0xa;
    else if (_stricmp(Name, "tractor_loot") == 0)
        Objective = 0xb;
    else if (_stricmp(Name, "return_loot") == 0)
        Objective = 0xc;
    else if (_stricmp(Name, "jettison_loot") == 0)
        Objective = 0xd;
    else
        Objective = static_cast<int>(reinterpret_cast<intptr_t>(this));

    std::pair<std::map<int, FRmParamText>::iterator, bool> Inserted =
        Texts.insert(std::make_pair(Objective, FRmParamText()));
    Inserted.first->second.Parse(Reader, Index + 1);
    return 0;
}

// Inlined in FUN_06f98df0.
FRmCommLine::FRmCommLine()
    : Speaker(1)
    , Length(0)
{
    Text[0] = '\0';
}

// Inlined in FUN_06f998c0.
FRmCommSequence::FRmCommSequence()
    : Trigger(0)
    , Target(1)
    , Field_0x08(0.0f)
    , MinDelay(0.0f)
    , MaxDelay(0.0f)
{
}

// FUN_06f98df0: "comm_sequence = trigger, target, f, f, f, {speaker, text}...". No name
// match keeps the default. For triggers 13..16 an unordered MaxDelay is not clamped, an
// unordered MinDelay is.
void FRmCommSequence::Parse(INI_Reader* Reader)
{
    char const* TriggerName = Reader->get_value_string(0);
    for (int Row = 0; Row < 17; ++Row)
    {
        if (GRmCommTriggerNames[Row].Name != nullptr &&
            _stricmp(TriggerName, GRmCommTriggerNames[Row].Name) == 0)
        {
            Trigger = GRmCommTriggerNames[Row].Value;
            break;
        }
    }
    char const* TargetName = Reader->get_value_string(1);
    for (int Row = 0; Row < 12; ++Row)
    {
        if (GRmCommTargetNames[Row].Name != nullptr &&
            _stricmp(TargetName, GRmCommTargetNames[Row].Name) == 0)
        {
            Target = GRmCommTargetNames[Row].Value;
            break;
        }
    }
    Field_0x08 = Reader->get_value_float(2);
    MinDelay = Reader->get_value_float(3);
    MaxDelay = Reader->get_value_float(4);
    if (Trigger == 0xd || Trigger == 0xe || Trigger == 0xf || Trigger == 0x10)
    {
        if (MaxDelay > 5.0f)
        {
            MaxDelay = 5.0f;
            if (!(MinDelay < 5.0f))
                MinDelay = 5.0f;
        }
    }

    unsigned int Index = 5;
    while (!Reader->is_value_empty(Index))
    {
        Lines.insert(Lines.end(), FRmCommLine());
        FRmCommLine& Line = Lines.back();
        char const* SpeakerName = Reader->get_value_string(Index);
        ++Index;
        for (int Row = 0; Row < 12; ++Row)
        {
            if (GRmCommTargetNames[Row].Name != nullptr &&
                _stricmp(SpeakerName, GRmCommTargetNames[Row].Name) == 0)
            {
                Line.Speaker = GRmCommTargetNames[Row].Value;
                break;
            }
        }
        char const* LineText = Reader->get_value_string(Index);
        ++Index;
        if (LineText == nullptr)
        {
            Line.Length = 0;
            Line.Text[0] = '\0';
        }
        else
        {
            unsigned int Length = static_cast<unsigned int>(strlen(LineText));
            if (Length >= 0x40)
                Length = 0x3f;
            memcpy(Line.Text, LineText, Length);
            Line.Text[Length] = '\0';
            Line.Length = Length;
        }
    }
}

// FUN_06f9efb0
FRmVignetteParams::FRmVignetteParams()
    : Weight(0.0f)
    , AllowableZoneTypes(7)
{
}

// FUN_06f9f100
FRmVignetteParams::~FRmVignetteParams()
{
}

// "offer_group" / "hostile_group" of FUN_06f998c0: "all" enumerates every group once into
// DAT_06fd3cb8.
static void ParseRmReputationGroups(INI_Reader* Reader, std::vector<unsigned int>* Groups)
{
    if (_stricmp(Reader->get_value_string(0), "all") == 0)
    {
        FRmAllGroupsEnumerator* AllGroups = GRmAllGroups;
        if (!AllGroups->bEnumerated)
        {
            pub::Reputation::EnumerateGroups(*AllGroups);
            AllGroups->bEnumerated = true;
        }
        *Groups = AllGroups->Groups;
        return;
    }
    // One out-slot for every value; GetReputationGroup writes it on both paths (the id or
    // -1), so its initial value is never read.
    unsigned int GroupId = 0;
    for (unsigned int Index = 0; !Reader->is_value_empty(Index); ++Index)
    {
        pub::Reputation::GetReputationGroup(GroupId, Reader->get_value_string(Index));
        RmInsertUnique(Groups, GroupId);
    }
}

// FUN_06f998c0 (vtable slot 1): 1 when the key was one of this block's.
char FRmVignetteParams::ParseKey(INI_Reader* Reader)
{
    if (Reader->is_value("weight"))
    {
        Weight = Reader->get_value_float(0);
        return 1;
    }
    if (Reader->is_value("offer_text"))
    {
        // FUN_06fa2db0 appends a {bAppend 0, []} element, then its slot 0 parses.
        OfferTexts.insert(OfferTexts.end(), FRmOfferText());
        OfferTexts.back().Parse(Reader);
        return 1;
    }
    if (Reader->is_value("objective_text"))
    {
        ObjectiveText.Parse(Reader, 0);
        return 1;
    }
    if (Reader->is_value("failure_text"))
    {
        FailureText.Parse(Reader, 0);
        return 1;
    }
    if (Reader->is_value("reward_text"))
    {
        RewardText.Parse(Reader, 0);
        return 1;
    }
    if (Reader->is_value("difficulty"))
    {
        Difficulty.Parse(Reader);
        return 1;
    }
    if (Reader->is_value("offer_group"))
    {
        ParseRmReputationGroups(Reader, &OfferGroups);
        return 1;
    }
    if (Reader->is_value("hostile_group"))
    {
        ParseRmReputationGroups(Reader, &HostileGroups);
        return 1;
    }
    if (Reader->is_value("comm_sequence"))
    {
        CommSequences.insert(CommSequences.end(), FRmCommSequence());
        CommSequences.back().Parse(Reader);
        return 1;
    }
    if (Reader->is_value("allowable_zone_types"))
    {
        AllowableZoneTypes = 0;
        for (unsigned int Index = 0; !Reader->is_value_empty(Index); ++Index)
        {
            if (_stricmp(Reader->get_value_string(Index), "open") == 0)
                AllowableZoneTypes = AllowableZoneTypes | 0x1;
            else if (_stricmp(Reader->get_value_string(Index), "field") == 0)
                AllowableZoneTypes = AllowableZoneTypes | 0x2;
            else if (_stricmp(Reader->get_value_string(Index), "exclusion") == 0)
                AllowableZoneTypes = AllowableZoneTypes | 0x4;
        }
        return 1;
    }
    return 0;
}

// Inlined in FUN_06f9ecb0 and FUN_06f9fc20.
FRmVignetteNode::FRmVignetteNode()
    : NodeId(0xffffffffu)
    , bImplemented(true)
    , FirstChildChance(-1.0f)
{
    Nickname.clear();
}

// LAB_06f99e40 (vtable slot 0).
void FRmVignetteNode::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (!ParseCommonKey(Reader))
            Params.ParseKey(Reader);
    }
    if (Params.Weight == 0.0f)
        Params.Weight = 1.0f;
}

// FUN_06f99d60.
bool FRmVignetteNode::ParseCommonKey(INI_Reader* Reader)
{
    if (Reader->is_value("node_id"))
    {
        NodeId = static_cast<unsigned int>(Reader->get_value_int(0));
        return true;
    }
    if (Reader->is_value("child_node"))
    {
        unsigned int ChildId = static_cast<unsigned int>(Reader->get_value_int(0));
        Children.insert(Children.end(), ChildId);
        return true;
    }
    if (Reader->is_value("implemented"))
    {
        bImplemented = Reader->get_value_bool(0);
        return true;
    }
    if (Reader->is_value("nickname") || Reader->is_value("documentation"))
    {
        Nickname.Id = CreateID(Reader->get_value_string());
        return true;
    }
    return false;
}

static FRandomMissionGenerator GRandomMissionGeneratorInstance;   // DAT_06fd3c78

// =======================================================================================
// TU63 -- offer request, tree path, path filters
// =======================================================================================

// FUN_06f9cac0.
FRmOfferQuery::FRmOfferQuery()
    : OfferGroup(-1)
    , EnemyGroup(0xffffffffu)
    , Reward(0)
    , RepReward(0.0f)
    , RepHit(0.0f)
    , OfferBase(0)
    , Difficulty(0.0f)
    , ObjectiveZone(0)
    , PlayerId(0)
    , DestSys(0)
{
    ObjectivePos.x = 0.0f;
    ObjectivePos.y = 0.0f;
    ObjectivePos.z = 0.0f;
}

// LAB_06f99ea0. No length check on the 0x400-byte stack buffer, as in retail.
void FRmTreePath::Save(FILE* File)
{
    char Text[0x400];
    unsigned int Length = 0;
    for (std::vector<char>::const_iterator It = Bits.begin(); It != Bits.end(); ++It)
    {
        Text[Length] = (*It == 0) ? 'F' : 'T';
        ++Length;
    }
    Text[Length] = '\0';
    fprintf(File, "tree_path = %s\n", Text);
}

// FUN_06f99f10. The repeated is_value test is retail's.
void FRmTreePath::Load(INI_Reader* Reader)
{
    if (!Reader->is_value("tree_path"))
        return;
    Bits.clear();

    char Text[0x400];
    unsigned int Length = 0;
    if (Reader->is_value("tree_path"))
    {
        char const* Value = Reader->get_value_string(0);
        if (Value != nullptr)
        {
            Length = static_cast<unsigned int>(strlen(Value));
            if (Length >= 0x400)
                Length = 0x3ff;
            memcpy(Text, Value, Length);
            Text[Length] = '\0';
        }
    }
    for (unsigned int Index = 0; Index < Length; ++Index)
        Bits.insert(Bits.end(), static_cast<char>(Text[Index] == 'T' ? 1 : 0));
}

// Inlined at 06f9e9e0.
FRmPathFilter::FRmPathFilter()
    : bRandomMission(false)
    , bDestroySolars(false)
    , bAssassinate(false)
    , bDestroyShips(false)
    , bLootItem(false)
{
}

// FUN_06ea1450.
int FRmPathFilter::Slot00() const
{
    return 0;
}

// FUN_06f9a170: random_mission does not count.
bool FRmPathFilter::HasMissionType() const
{
    return bDestroySolars || bAssassinate || bDestroyShips || bLootItem;
}

// FUN_06f9a1a0: only a taken branch (Bit != 0) is looked at. The unused IsEmpty call is
// retail's.
bool FRmPathFilter::ApplyNode(FRmVignetteNode const* Node, char Bit)
{
    if (Bit == 0)
        return false;
    ID_String const& Nickname = Node->Nickname;
    if (Nickname.Id == CreateID("random_mission"))
    {
        bRandomMission = true;
        return true;
    }
    if (Nickname.Id == CreateID("destroy_solars_mission"))
    {
        bDestroySolars = true;
        return true;
    }
    if (Nickname.Id == CreateID("assassinate_mission"))
    {
        bAssassinate = true;
        return true;
    }
    if (Nickname.Id == CreateID("destroy_ships_mission"))
    {
        bDestroyShips = true;
        return true;
    }
    if (Nickname.Id == CreateID("loot_item_mission"))
    {
        bLootItem = true;
        return true;
    }
    Nickname.IsEmpty();
    return false;
}

// FUN_06f9a270. Destroy-ships and loot-item start with every flag cleared, unlike the other
// two.
FRmPathFilter* FRmPathFilter::Specialize() const
{
    if (bDestroySolars)
    {
        FRmDestroySolarsFilter* Filter = new FRmDestroySolarsFilter();
        Filter->bRandomMission = bRandomMission;
        Filter->bDestroySolars = bDestroySolars;
        Filter->bAssassinate = bAssassinate;
        Filter->bDestroyShips = bDestroyShips;
        Filter->bLootItem = bLootItem;
        return Filter;
    }
    if (bAssassinate)
    {
        FRmAssassinateFilter* Filter = new FRmAssassinateFilter();
        Filter->bRandomMission = bRandomMission;
        Filter->bDestroySolars = bDestroySolars;
        Filter->bAssassinate = bAssassinate;
        Filter->bDestroyShips = bDestroyShips;
        Filter->bLootItem = bLootItem;
        return Filter;
    }
    if (bDestroyShips)
        return new FRmDestroyShipsFilter();
    if (bLootItem)
        return new FRmLootItemFilter();
    return nullptr;
}

// LAB_06f9a430 (byte table 06f9a458): parameters 1..3 (OFFER_GROUP, HOSTILE_GROUP,
// BIG_SOLAR) and 9..13 (FACTION_HIT .. TARGET_ZONE).
bool FRmPathFilter::SupportsTextParam(int Param) const
{
    unsigned int const Index = static_cast<unsigned int>(Param) - 1u;
    if (Index > 0xcu)
        return false;
    return Index <= 2u || Index >= 8u;
}

// Inlined at 06f9a2aa / 06f9a6aa.
FRmDestroySolarsFilter::FRmDestroySolarsFilter()
    : bBigSolar(false)
    , bPkBigSolar(false)
    , bDefensiveSolars(false)
    , bPkDefensiveSolars(false)
    , bCavalry(false)
    , bHostileShips(false)
    , bPkHostileShips(false)
    , bHostileShipsAtInstallation(false)
    , bHostileShipsAfter30S(false)
    , bHostileWaves(false)
    , bFriendlyShips(false)
    , bFriendlyShipsAtInstallation(false)
    , bFriendlyShipsAfter30S(false)
    , bFriendlyWaves(false)
{
}

// FUN_06f06190.
bool FRmDestroySolarsFilter::HasMissionType() const
{
    return bCavalry;
}

// LAB_06f9a470: the names are in field order (+0x0c..+0x19). The unused IsEmpty call is
// retail's.
bool FRmDestroySolarsFilter::ApplyNode(FRmVignetteNode const* Node, char Bit)
{
    static char const* const Names[14] = {
        "big_solar", "pk_big_solar", "defensive_solars", "pk_defensive_solars", "cavalry",
        "hostile_ships", "pk_hostile_ships", "hostile_ships_at_installation",
        "hostile_ships_after_30_s", "hostile_waves", "friendly_ships",
        "friendly_ships_at_installation", "friendly_ships_after_30_s", "friendly_waves",
    };
    bool* const Flags[14] = {
        &bBigSolar, &bPkBigSolar, &bDefensiveSolars, &bPkDefensiveSolars, &bCavalry,
        &bHostileShips, &bPkHostileShips, &bHostileShipsAtInstallation,
        &bHostileShipsAfter30S, &bHostileWaves, &bFriendlyShips,
        &bFriendlyShipsAtInstallation, &bFriendlyShipsAfter30S, &bFriendlyWaves,
    };

    if (Bit == 0)
        return false;
    ID_String const& Nickname = Node->Nickname;
    for (int Index = 0; Index < 14; ++Index)
    {
        if (Nickname.Id == CreateID(Names[Index]))
        {
            *Flags[Index] = true;
            return true;
        }
    }
    if (Nickname.Id == CreateID("branch"))
        return true;
    Nickname.IsEmpty();
    return false;
}

// FUN_06f9a690: the cavalry filter starts with every flag cleared, base flags included.
FRmPathFilter* FRmDestroySolarsFilter::Specialize() const
{
    if (!bCavalry)
        return nullptr;
    return new FRmCavalryFilter();
}

// FUN_06f9abe0 (byte tables 06f9ac50 / 06f9ac70).
bool FRmDestroySolarsFilter::SupportsTextParam(int Param) const
{
    switch (Param)
    {
    case 1:
        return false;
    case 4:
        return bPkBigSolar;
    case 5:
        return bPkDefensiveSolars;
    case 22:
        return bPkHostileShips;
    case 24:
        return !bPkHostileShips;
    default:
        return FRmPathFilter::SupportsTextParam(Param);
    }
}

// Slot 6 of 06fbd034 / 06fbd164, LAB_06ee9e10 (ICF-shared).
int FRmDestroySolarsFilter::Slot06() const
{
    return 1;
}

// FUN_06ea1480.
bool FRmCavalryFilter::HasMissionType() const
{
    return false;
}

// FUN_06f9a6f0.
bool FRmCavalryFilter::ApplyNode(FRmVignetteNode const* /*Node*/, char /*Bit*/)
{
    return false;
}

// FUN_06ea1480.
bool FRmDestroyShipsFilter::HasMissionType() const
{
    return false;
}

// FUN_06f9a6f0.
bool FRmDestroyShipsFilter::ApplyNode(FRmVignetteNode const* /*Node*/, char /*Bit*/)
{
    return false;
}

// FUN_06ea1480.
bool FRmLootItemFilter::HasMissionType() const
{
    return false;
}

// FUN_06f9a6f0.
bool FRmLootItemFilter::ApplyNode(FRmVignetteNode const* /*Node*/, char /*Bit*/)
{
    return false;
}

// Node names of LAB_06f9b3f0 in flag order (+0x0c..+0x26).
char const* const GRmAssassinateNodeNames[27] = {
    "assassinate_ship", "assassinate_solar", "target_drops_critical_loot", "bring_back_loot",
    "tractor_in_loot", "destroy_loot", "target_spawns_with_wave", "pre_battle",
    "target_spawns_at_pre_battle", "pre_battle_non_target_wave", "pre_battle_wave_runs",
    "pk_pre_battle_wave", "target_runs_to_main_battle", "main_battle",
    "defensive_solars_at_main_battle", "target_spawns_at_main_battle",
    "main_battle_non_target_wave", "wave_just_after_main_battle_starts",
    "continuous_reenforcements", "pk_all_ships", "pre_battle_wave_1", "pre_battle_wave_2",
    "main_battle_wave_1", "main_battle_wave_2", "friendly_ships_at_main_battle",
    "friendly_ships_after_30_s", "friendly_ships_come_in_to_whoop_up",
};

// FUN_06f9b390.
FRmAssassinateFilter::FRmAssassinateFilter()
{
    for (int Index = 0; Index < 27; ++Index)
        Flags[Index] = false;
}

// LAB_06f9a400: Flags[0] is assassinate_ship.
int FRmAssassinateFilter::Slot00() const
{
    return Flags[0] ? 2 : 3;
}

// FUN_06ea1480.
bool FRmAssassinateFilter::HasMissionType() const
{
    return false;
}

// FUN_06f9b3f0: same shape as LAB_06f9a470; the unused IsEmpty call is retail's.
bool FRmAssassinateFilter::ApplyNode(FRmVignetteNode const* Node, char Bit)
{
    if (Bit == 0)
        return false;
    ID_String const& Nickname = Node->Nickname;
    for (int Index = 0; Index < 27; ++Index)
    {
        if (Nickname.Id == CreateID(GRmAssassinateNodeNames[Index]))
        {
            Flags[Index] = true;
            return true;
        }
    }
    if (Nickname.Id == CreateID("branch"))
        return true;
    Nickname.IsEmpty();
    return false;
}

// FUN_06ea1450 (slot 3).
FRmPathFilter* FRmAssassinateFilter::Specialize() const
{
    return nullptr;
}

// FUN_06f9ba50 (byte table 06f9bac4). Flags[7] pre_battle, [8] target_spawns_at_pre_battle,
// [15] target_spawns_at_main_battle, [16] main_battle_non_target_wave.
bool FRmAssassinateFilter::SupportsTextParam(int Param) const
{
    switch (Param)
    {
    case 1:
    case 3:
    case 19:
        return Flags[7];
    case 20:
        return Flags[8];
    case 4:
    case 5:
    case 21:
    case 24:
        return false;
    case 22:
        return Flags[15] && Flags[16];
    case 23:
        return Flags[15];
    default:
        return FRmPathFilter::SupportsTextParam(Param);
    }
}

// =======================================================================================
// TU63 -- offer node and tree path choice
// =======================================================================================

namespace
{
    // {path, weight} entry of the tree path pick (8 bytes; a plain vector local in retail).
    struct FRmWeightedPath
    {
        FRmTreePath* Path;
        float Weight;
    };
}

// FUN_06f9cb00. The candidates are a Common weighted_vector<unsigned int>; its Pick is
// inlined in retail. The leaf lookup has no end check, as retail.
unsigned int FRandomMissionGenerator::PickOfferNode(FRmOfferQuery const* Query)
{
    weighted_vector<unsigned int> Candidates;
    unsigned int const OfferGroup = static_cast<unsigned int>(Query->OfferGroup);
    for (std::vector<unsigned int>::const_iterator LeafIt = LeafNodeIds.begin();
         LeafIt != LeafNodeIds.end(); ++LeafIt)
    {
        FRmVignetteNode const& Node = Nodes.find(*LeafIt)->second;
        std::vector<unsigned int> const& OfferGroups = Node.Params.OfferGroups;
        if (std::find(OfferGroups.begin(), OfferGroups.end(), OfferGroup) == OfferGroups.end())
            continue;
        std::vector<unsigned int> const& HostileGroups = Node.Params.HostileGroups;
        if (std::find(HostileGroups.begin(), HostileGroups.end(), Query->EnemyGroup) ==
            HostileGroups.end())
            continue;
        float const Difficulty = Query->Difficulty;
        for (std::list<FRmDifficultyWindow>::const_iterator Window = Node.Windows.Windows.begin();
             Window != Node.Windows.Windows.end(); ++Window)
        {
            if (Window->IsInRange(Difficulty))
            {
                Candidates.Items.insert(Candidates.Items.end(), 1,
                                        std::pair<unsigned int, float>(Node.NodeId,
                                                                       Node.Params.Weight));
                break;
            }
        }
    }

    if (Candidates.Items.empty())
        return 0xffffffffu;

    std::pair<unsigned int, float> const* Picked = Candidates.Pick();
    if (Picked != Candidates.Items.data() + Candidates.Items.size())
        return Picked->first;
    // Retail returns the dword after the last candidate (undefined); the port returns -1. A
    // zero sum is unreachable while every node weight is forced non-zero (FUN_06f99e40), a
    // run past the end only through float rounding.
    return 0xffffffffu;
}

// FUN_06f9cd00. The decision bits of the current branch live in the function-static list
// DAT_06fd3ca8 (guard DAT_06fd3cb4 bit 0, atexit FUN_06f9d010). Node lookups have no end
// check, as retail.
void FRandomMissionGenerator::CollectTreePaths(std::vector<FRmTreePath>* OutPaths,
                                               unsigned int NodeId, FRmDifficultyWindow Window,
                                               int OfferGroup)
{
    static std::list<char> Choices;

    FRmVignetteNode const& Node = Nodes.find(NodeId)->second;
    if (Node.Parents.empty())
    {
        OutPaths->insert(OutPaths->end(), FRmTreePath());
        std::vector<char>& Bits = OutPaths->back().Bits;
        for (std::list<char>::const_iterator It = Choices.begin(); It != Choices.end(); ++It)
            Bits.insert(Bits.end(), *It);
        return;
    }

    for (std::vector<unsigned int>::const_iterator ParentIt = Node.Parents.begin();
         ParentIt != Node.Parents.end(); ++ParentIt)
    {
        FRmVignetteNode const& Parent = Nodes.find(*ParentIt)->second;
        char const Bit = (Parent.Children.front() == NodeId) ? 1 : 0;

        std::list<FRmDifficultyWindow>::const_iterator ParentWindow =
            Parent.Windows.Windows.begin();
        while (ParentWindow != Parent.Windows.Windows.end() && !ParentWindow->Overlaps(&Window))
            ++ParentWindow;
        if (ParentWindow == Parent.Windows.Windows.end())
            continue;

        std::vector<unsigned int> const& OfferGroups = Parent.Params.OfferGroups;
        if (!OfferGroups.empty() &&
            std::find(OfferGroups.begin(), OfferGroups.end(),
                      static_cast<unsigned int>(OfferGroup)) == OfferGroups.end())
            continue;

        Choices.insert(Choices.begin(), Bit);
        FRmDifficultyWindow const Shifted(Window.Min - Parent.Params.Difficulty.Max,
                                          Window.Max - Parent.Params.Difficulty.Min);
        CollectTreePaths(OutPaths, Parent.NodeId, Shifted, OfferGroup);
        Choices.erase(Choices.begin());
    }
}

// FUN_06f9d070. A chance of exactly -1.0 means 1 / child count. Products are rounded to
// float at every step.
FRmTreePath* FRandomMissionGenerator::PickTreePath(std::vector<FRmTreePath>* Paths)
{
    std::vector<FRmWeightedPath> Weighted;
    for (std::vector<FRmTreePath>::iterator PathIt = Paths->begin(); PathIt != Paths->end();
         ++PathIt)
    {
        FRmWeightedPath Entry;
        Entry.Path = &*PathIt;
        Entry.Weight = 1.0f;
        Weighted.insert(Weighted.end(), Entry);
        float& Weight = Weighted.back().Weight;

        unsigned int CurrentId = RootNodeId;
        for (std::vector<char>::const_iterator BitIt = PathIt->Bits.begin();
             BitIt != PathIt->Bits.end(); ++BitIt)
        {
            FRmVignetteNode const& Node = Nodes.find(CurrentId)->second;
            bool const bUniform = (Node.FirstChildChance == -1.0f);
            double const Chance =
                bUniform ? 1.0 / static_cast<double>(Node.Children.size())
                         : static_cast<double>(Node.FirstChildChance);
            if (*BitIt == 1)
            {
                Weight = static_cast<float>(Chance * Weight);
                CurrentId = Node.Children[0];
            }
            else
            {
                Weight = static_cast<float>((1.0 - Chance) * Weight);
                CurrentId = Node.Children[1];
            }
        }
    }

    double Sum = 0.0;
    for (std::vector<FRmWeightedPath>::const_iterator It = Weighted.begin();
         It != Weighted.end(); ++It)
        Sum += It->Weight;
    float const Total = static_cast<float>(Sum);
    if (Sum == 0.0)
    {
        // Retail returns the dword after the last entry (undefined), which its caller then
        // dereferences.
        return nullptr;
    }

    float const Target = static_cast<float>(
        static_cast<double>(rand()) * static_cast<double>(3.0517578e-05f) * Total);
    double Accum = 0.0;
    for (std::vector<FRmWeightedPath>::const_iterator It = Weighted.begin();
         It != Weighted.end(); ++It)
    {
        Accum += It->Weight;
        if (static_cast<double>(Target) <= Accum)
            return It->Path;
    }
    // Retail returns the dword after the last entry (undefined).
    return nullptr;
}

// FUN_06f9d2b0: true as soon as at least one path exists.
bool FRandomMissionGenerator::ChooseTreePath(FRmTreePath* OutPath, unsigned int OfferNodeId,
                                             float Difficulty, int OfferGroup)
{
    bool bChosen = false;
    std::vector<FRmTreePath> Paths;
    CollectTreePaths(&Paths, OfferNodeId, FRmDifficultyWindow(Difficulty, Difficulty),
                     OfferGroup);
    if (!Paths.empty())
    {
        bChosen = true;
        FRmTreePath* Picked = PickTreePath(&Paths);
        if (&OutPath->Bits != &Picked->Bits)
            OutPath->Bits = Picked->Bits;
    }
    return bChosen;
}

// =======================================================================================
// TU63 -- voice properties
// =======================================================================================

std::vector<FVoiceProperties> GVoiceProperties;   // DAT_06fd3d90

// FUN_06fa6760 -- [mVoiceProp] sections; called by LoadContentGameTables (FUN_06efe300) for
// audio\voices_space_male.ini, voices_space_female.ini, voices_recognizable.ini and
// missions\voice_properties.ini.
void __cdecl LoadVoiceProperties(INI_Reader* Reader)
{
    // One GetVoiceID out-slot for all sections.
    unsigned int VoiceId = 0;

    while (Reader->read_header())
    {
        if (!Reader->is_header("mVoiceProp"))
            continue;

        // The record being filled -- an existing element, or a heap record (operator new
        // 0x1c) that is copied in at the end of the section and freed.
        FVoiceProperties* Record = nullptr;
        std::unique_ptr<FVoiceProperties> Fresh;
        bool bExisting = false;

        while (Reader->read_value())
        {
            if (Reader->is_value("voice"))
            {
                pub::GetVoiceID(VoiceId, Reader->get_value_string());
                FVoiceProperties* Found = nullptr;
                for (std::vector<FVoiceProperties>::iterator It = GVoiceProperties.begin();
                     It != GVoiceProperties.end(); ++It)
                {
                    if (It->VoiceId == VoiceId)
                    {
                        Found = &*It;
                        break;
                    }
                }
                if (Found != nullptr)
                {
                    bExisting = true;
                    Record = Found;
                }
                else
                {
                    Fresh.reset(new FVoiceProperties());
                    Fresh->VoiceId = 0;
                    Fresh->Flags = 0;
                    Record = Fresh.get();
                }
                Record->VoiceId = VoiceId;
            }
            else if (Reader->is_value("gender"))
            {
                // "male" is compared case-insensitively, "female" case-sensitively.
                char const* Value = Reader->get_value_string();
                if (_stricmp(Value, "male") == 0)
                    Record->Flags |= 0x1;
                else if (strcmp(Value, "female") == 0)
                    Record->Flags |= 0x2;
            }
            else if (Reader->is_value("supports_roles"))
            {
                // Compared case-sensitively.
                for (unsigned int Index = 0; !Reader->is_value_empty(Index); ++Index)
                {
                    char const* Role = Reader->get_value_string(Index);
                    if (strcmp(Role, "combat_lev_1") == 0)
                        Record->Flags |= 0x4000;
                    else if (strcmp(Role, "combat_lev_2") == 0)
                        Record->Flags |= 0x4;
                    else if (strcmp(Role, "combat_lev_3") == 0)
                        Record->Flags |= 0xc004;
                    else if (strcmp(Role, "scan") == 0)
                        Record->Flags |= 0x8;
                    else if (strcmp(Role, "hail_hailee") == 0)
                        Record->Flags |= 0x10;
                    else if (strcmp(Role, "hail_dock") == 0)
                        Record->Flags |= 0x20;
                    else if (strcmp(Role, "dockrequest_dock") == 0)
                        Record->Flags |= 0x40;
                    else if (strcmp(Role, "dockrequest_docker") == 0)
                        Record->Flags |= 0x80;
                    else if (strcmp(Role, "behavior") == 0)
                        Record->Flags |= 0x100;
                    else if (strcmp(Role, "misc_chatter") == 0)
                        Record->Flags |= 0x200;
                    else if (strcmp(Role, "fidget") == 0)
                        Record->Flags |= 0x400;
                    else if (strcmp(Role, "combat_dock") == 0)
                        Record->Flags |= 0x800;
                    else if (strcmp(Role, "random_mission_ship") == 0)
                        Record->Flags |= 0x1000;
                    else if (strcmp(Role, "mission_commission") == 0)
                        Record->Flags |= 0x2000;
                }
            }
            else if (Reader->is_value("permutation_count"))
            {
                // Unique insert: an existing key is kept.
                unsigned int Key = pub::GetNicknameId(Reader->get_value_string(0));
                int Count = Reader->get_value_int(1);
                Record->PermutationCounts.insert(std::make_pair(Key, Count));
            }
        }

        // The record is dereferenced unconditionally, as in retail (a section without a
        // "voice" key has none).
        if (!bExisting)
            GVoiceProperties.insert(GVoiceProperties.end(), *Record);
    }
}

// FUN_06fa6de0; callers in GCS.
int FVoiceProperties::GetPermutationCount(unsigned int const* NicknameId) const
{
    std::map<unsigned int, int>::const_iterator It = PermutationCounts.find(*NicknameId);
    if (It == PermutationCounts.end())
        return 0;
    return It->second;
}

// FUN_06fa6e30; callers in GCS TU25, TU38, TU48.
bool FVoiceProperties::SupportsRoles(unsigned int Mask) const
{
    return (Flags & Mask) == Mask;
}

// FUN_06fa6e50; callers in GCS TU25, TU38, TU48.
FVoiceProperties* __cdecl FindVoiceProperties(unsigned int const* VoiceId)
{
    for (std::vector<FVoiceProperties>::iterator It = GVoiceProperties.begin();
         It != GVoiceProperties.end(); ++It)
    {
        if (It->VoiceId == *VoiceId)
            return &*It;
    }
    return nullptr;
}

// =======================================================================================
// TU63 -- building the TU61 offer (RandomMissionOffer.h)
// =======================================================================================

namespace
{
    // .rdata DAT_06fbcc58 (single player) and DAT_06fbcc7c (multiplayer), indexed by the
    // formation size: nine floats each, stored back to back.
    float const GRmFormationSizeWeights[18] = {
        0.0f, 0.0f, 3.0f, 9.0f, 6.0f, 2.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 2.0f, 3.0f, 9.0f, 6.0f, 4.0f, 2.0f, 1.0f,
    };

    // The 0x10-byte candidate of FUN_06f98780.
    struct FRmFormationSizeChoice
    {
        FRmShipSize Size;
        float Weight;
    };

    // DAT_06fd3d88: the counter of the "Vignette_solar_%X" nicknames (FUN_06f9af40,
    // FUN_06f9bb00).
    unsigned int GRmVignetteSolarCounter = 0;
    // DAT_06fd3d00: the .data buffer both solar spawners format the nickname into (0x20 bytes
    // up to DAT_06fd3d20).
    char GRmVignetteSolarName[0x20];

    // .rdata 06fbcbf8: the pilots of vignette solars, 0x18 bytes apart.
    char const* const GRmSolarPilotNames[4] = {
        "pilot_rm_solar_easiest",
        "pilot_rm_solar_easy",
        "pilot_rm_solar_hard",
        "pilot_rm_solar_hardest",
    };

    // Inlined in FUN_06f9e9b0 (x87): Difficulty -> offer +0x2c. NaN takes the log branch;
    // 0.5 is DAT_06fb74e0.
    float RemapRmOfferDifficulty(float Difficulty)
    {
        double const LogThreshold = 0.900376;       // DAT_06fbccd0 3feccfe1'54434e33
        double const LogScale = 6.3693;             // DAT_06fbcca8 40197a29'c779a6b5
        double const LogOffset = 13.668;            // DAT_06fbcca0 402b5604'189374bc
        double const CubeScale = 15.193;            // DAT_06fbccc8 402e62d0'e5604189
        double const SquareScale = 28.236;          // DAT_06fbccc0 403c3c6a'7ef9db23
        double const LinearScale = 28.682;          // DAT_06fbccb8 403cae97'8d4fdf3b
        double const CubicOffset = 1.0113;          // DAT_06fbccb0 3ff02e48'e8a71de7

        double const D = static_cast<double>(Difficulty);
        double Mapped = 0.0;
        if (!(D < LogThreshold))
            Mapped = std::log(D) * LogScale + LogOffset;
        else
            Mapped = D * LinearScale + (D * D * D * CubeScale - D * D * SquareScale) - CubicOffset;
        if (Mapped < 0.0)
            Mapped = 0.0;
        int const Rounded = static_cast<int>(std::floor(Mapped + 0.5));
        float Rank = static_cast<float>(Rounded);
        if (1.0f > Rank)
            Rank = 1.0f;
        return Rank;
    }
}

// FUN_06f98780. The level flags (0x1a bytes) and the weight index (the count; at most 8 with
// the eight values per rank of npcranktodiff.ini) have no bound check, as retail. The
// class-id local is a bare vector<uint> in retail; FUN_06f357d0 reads only that vector,
// which is FEncounterShip::ShipClasses here. The weighted pick is weighted_vector::Pick
// inlined.
void __cdecl PickRmShipFormationSize(FRmShipSize* Out, float Difficulty, unsigned int Faction)
{
    FEncounterShip Line;
    Line.ShipClasses.push_back(CreateID("class_fighter"));
    std::list<FNpcShipArch*> Archs;
    GetFactionShipLineArchs(Faction, &Line, &Archs);

    unsigned char bLevels[0x1a] = {};
    for (std::list<FNpcShipArch*>::const_iterator It = Archs.begin(); It != Archs.end(); ++It)
    {
        int const Level = (*It)->Level;
        if (Level != 0)
            bLevels[Level] = 1;
    }

    int Counts[0x1a];
    for (int Level = 1; Level <= 0x19; ++Level)
    {
        Counts[Level] = -1;
        if (bLevels[Level] == 0)
            continue;
        std::vector<float> const& Diffs = GRmNpcRankToDifficulty[Level];
        for (unsigned int Index = 0; Index < Diffs.size(); ++Index)
        {
            if (Diffs[Index] > Difficulty)
            {
                if (Index != 0)
                {
                    double const Below = static_cast<double>(Difficulty) - Diffs[Index - 1];
                    double const Above = static_cast<double>(Diffs[Index]) - Difficulty;
                    unsigned int const Picked = (Above < Below) ? Index : Index - 1;
                    Counts[Level] = static_cast<int>(Picked) + 1;
                }
                break;
            }
        }
    }

    std::vector<FRmFormationSizeChoice> Choices;
    float const* const Weights =
        pub::SinglePlayer() ? &GRmFormationSizeWeights[0] : &GRmFormationSizeWeights[9];
    for (int Level = 1; Level <= 0x19; ++Level)
    {
        int const Count = Counts[Level];
        if (Count < 1)
            continue;
        FRmFormationSizeChoice Choice;
        Choice.Size.Difficulty = GRmNpcRankToDifficulty[Level][Count - 1];
        Choice.Size.Level = Level;
        Choice.Size.Count = Count;
        Choice.Weight = Weights[Count];
        Choices.insert(Choices.end(), 1, Choice);
    }

    if (!Choices.empty())
    {
        float Sum = 0.0f;
        for (unsigned int Index = 0; Index < Choices.size(); ++Index)
            Sum += Choices[Index].Weight;
        if (Sum != 0.0f)
        {
            float const Target = static_cast<float>(rand()) * (1.0f / 32768.0f) * Sum;
            float Running = 0.0f;
            for (unsigned int Index = 0; Index < Choices.size(); ++Index)
            {
                Running += Choices[Index].Weight;
                if (Target <= Running)
                {
                    *Out = Choices[Index].Size;
                    return;
                }
            }
        }
    }
    Out->Count = 0;
}

namespace
{
    // DAT_06fd3d84: the counter of the "Vignette_ship_%X" nicknames (FUN_06f97580).
    unsigned int GRmVignetteShipCounter = 0;
    // DAT_06fd3d20: the .data buffer FUN_06f97580 formats the nickname into (0x20 bytes; the
    // TU's static-ID globals follow at DAT_06fd3d40).
    char GRmVignetteShipName[0x20];
}

// FUN_06f97580. GetFactionProps (GetFormation FUN_06f4f3d0, result ignored) and FindFormation
// (FUN_06efd1a0) are unchecked. The "D%d" / "D1" formats are DAT_06fbcd00 / DAT_06fbcd04.
// The arch pick is FUN_06f35890; the ship local is FUN_06f862e0.
bool __cdecl BuildRmShips(FRmShipGroup* Group, unsigned int GroupId, FRmShipSize Size,
                          int FirstTargetFlags, int OtherTargetFlags, int FirstShipKind,
                          int OtherShipKind, FRmLootSpawn const* Loot, FNpcNameParams* TargetName,
                          bool bMatchLevel)
{
    if (Size.Count == 0)
        return false;

    unsigned int FormationType = CreateID("fighters");
    GetFactionProps(&GroupId)->GetFormation(&FormationType, &Group->Formation.Id);
    int SlotsLeft = static_cast<int>(FindFormation(&Group->Formation.Id)->Slots.size());

    FEncounterShip Classes;
    std::vector<unsigned int>& ClassIds = Classes.ShipClasses;
    unsigned int ClassId = CreateID("class_fighter");
    if (std::find(ClassIds.begin(), ClassIds.end(), ClassId) == ClassIds.end())
        ClassIds.insert(ClassIds.end(), ClassId);

    char LevelClass[8];
    if (Size.Level == 0)
        std::strcpy(LevelClass, "D1");
    else
        std::sprintf(LevelClass, "D%d", Size.Level);
    ClassId = CreateID(LevelClass);
    if (std::find(ClassIds.begin(), ClassIds.end(), ClassId) == ClassIds.end())
        ClassIds.insert(ClassIds.end(), ClassId);

    FNpcShipArch* const Arch = PickFactionShipLineArch(GroupId, &Classes, -1);
    if (Arch == nullptr)
        return false;
    if (bMatchLevel)
    {
        if (Size.Level == 0)
            return false;
        if (std::abs(Arch->Level - Size.Level) >= 2)
            return false;
    }

    FRmShipSpawn Ship;
    bool const bHasLoot = (Loot != nullptr);
    Ship.bRandomName = 1;
    Ship.Npc.Affiliation = GroupId;
    Ship.Npc.NpcShipArch.Id = Arch->Nickname.Id;
    bool bFirst = true;
    for (int Index = 0; Index < Size.Count; ++Index)
    {
        int const Slots = SlotsLeft;
        --SlotsLeft;
        if (Slots <= 0)
            break;

        if (bFirst)
        {
            bFirst = false;
            if (bHasLoot)
                Ship.Loot.insert(Ship.Loot.end(), 1, *Loot);
            Ship.TargetFlags = FirstTargetFlags;
            Ship.ShipKind = FirstShipKind;
            if (FirstShipKind == 2)
            {
                Ship.bRandomName = 0;
                Ship.TargetName = TargetName;
            }
        }
        else
        {
            Ship.Loot.clear();
            Ship.TargetFlags = OtherTargetFlags;
            Ship.ShipKind = OtherShipKind;
            Ship.bRandomName = 1;
            Ship.TargetName = nullptr;
        }

        std::sprintf(GRmVignetteShipName, "Vignette_ship_%X", GRmVignetteShipCounter);
        ++GRmVignetteShipCounter;
        Ship.Nickname = GRmVignetteShipName;
        Group->Ships.insert(Group->Ships.end(), 1, Ship);
    }
    return true;
}

// FUN_06f979b0. -0.9f is DAT_06fbcbd4; the hit points scale constants 0.7f / 0.3f are
// DAT_06fbcd08 / DAT_06fbbc10, widened to double. The critical (FUN_06f95cd0) and bonus
// (FUN_06f564d0) loot picks are not null-checked, as retail.
bool __cdecl BuildRmShipGroup(FRmOfferQuery const* Query, FRmShipComponent* Component,
                              FRmShipBlock const* Block, Vector const* Position,
                              bool bFriendly, FNpcNameParams* TargetName)
{
    Component->bActive = true;
    unsigned int const GroupId =
        bFriendly ? static_cast<unsigned int>(Query->OfferGroup) : Query->EnemyGroup;

    Component->Groups.insert(Component->Groups.end(), 1, FRmShipGroup());
    FRmShipGroup* const Group = &Component->Groups.back();
    Group->bRequired = Block->bRequired;
    Group->bClearPriorTargets = Block->bClearPriorTargets;
    Group->SpawnTriggerMode = Block->SpawnTriggerMode;
    Group->SpawnTriggerParam = Block->SpawnTriggerParam;
    Group->Position = *Position;
    Group->Attitude = bFriendly ? 0.9f : -0.9f;
    Group->System = FindSystemInfo(&Query->DestSys);

    int FirstTargetFlags = 0;
    int OtherTargetFlags = 0;
    if (Block->bKillAllShips != 0)
    {
        FirstTargetFlags = 2;
        OtherTargetFlags = 2;
    }
    if (Block->bHasTargetShip != 0)
    {
        FirstTargetFlags = 2;
        OtherTargetFlags = 0;
    }

    bool bLoot = true;
    if (Block->bDropsCriticalLoot == 0)
    {
        float const Roll = static_cast<float>(static_cast<double>(rand()) *
                                              static_cast<double>(1.0f / 32768.0f));
        bLoot = GetRmBonusLootDropChance() > Roll;
    }

    FRmLootSpawn Loot;
    FRmLootSpawn const* LootArg = nullptr;
    if (bLoot)
    {
        Loot.Info.bCanAiTractor = 0;
        if (Block->bDropsCriticalLoot != 0)
        {
            FRmCriticalLoot* const Critical =
                PickCriticalLoot(GroupId, static_cast<unsigned int>(Block->CriticalLootMode));
            Loot.Kind = 2;
            Loot.CriticalLoot = Critical;
            Loot.CriticalLootMode = Block->CriticalLootMode;
            Loot.Info.EquipArchID = Critical->ArchetypeId;
            Loot.Info.Units = 1;
        }
        else
        {
            FRmBonusLootEntry const* const Bonus =
                PickRmBonusLoot(Query->EnemyGroup, Query->Difficulty);
            Loot.Kind = 1;
            Loot.CriticalLoot = nullptr;
            Loot.CriticalLootMode = 0;
            Loot.Info.EquipArchID = Bonus->Archetype;
            int const Min = Bonus->NumToDropMin;
            Loot.Info.Units = static_cast<unsigned int>(
                (rand() * (Bonus->NumToDropMax - Min + 1)) / 0x8000 + Min);
        }
        Loot.bRelPos = 1;
        Loot.Info.SystemID = Query->DestSys;
        Loot.RelPosOffset.x = 0.0f;
        Loot.RelPosOffset.y = 0.0f;
        Loot.RelPosOffset.z = 0.0f;
        Loot.Info.HitPtsScale = static_cast<float>(
            static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) *
                static_cast<double>(0.7f) +
            static_cast<double>(0.3f));
        LootArg = &Loot;
    }

    int const FirstShipKind = (Block->bHasTargetShip != 0) ? 2 : 1;
    return BuildRmShips(Group, GroupId, Block->Size, FirstTargetFlags, OtherTargetFlags,
                        FirstShipKind, 1, LootArg, TargetName, !bFriendly);
}

// FUN_06f9a410: the base filter never creates an offer.
bool FRmPathFilter::CreateOffer(FRmOfferQuery const* /*Query*/, FRmOffer** OutOffer)
{
    if (*OutOffer != nullptr)
    {
        (*OutOffer)->Release();
        *OutOffer = nullptr;
    }
    return false;
}

// FUN_06f9a700. The offer is new 0x218 + FUN_06f88240(1). Shares on x87: 0.5 is
// DAT_06fb605c, 0.6 DAT_06fbd180; an unordered divisor becomes 1.0. A friendly block with no
// count gets count 1 (and level 1 for level 0) on a copy of the block. Every block is built
// even after a failure; the results are ANDed.
bool FRmDestroySolarsFilter::CreateOffer(FRmOfferQuery const* Query, FRmOffer** OutOffer)
{
    FRmVignetteOffer* const NewOffer = new FRmVignetteOffer(1);
    TRmRef<FRmVignetteOffer> Holder(NewOffer);
    NewOffer->Release();
    FRmVignetteOffer* const Offer = Holder.Get();

    Offer->General.bActive = true;
    Offer->Type = 1;
    bool bSuccess = FillSolarSpawns(Query, &Offer->Solars);
    Vector const Position = Query->ObjectivePos;
    Offer->Ships.Position = Position;

    double Divisor =
        static_cast<double>(static_cast<int>(bHostileShipsAfter30S) +
                            static_cast<int>(bHostileShipsAtInstallation)) +
        static_cast<double>(static_cast<int>(bHostileWaves)) * 0.5;
    if (!(1.0 <= Divisor))
        Divisor = 1.0;
    float const HostileShare =
        static_cast<float>(static_cast<double>(0.6f) / Divisor * Query->Difficulty);
    float const FriendlyShare = HostileShare * 0.5f;

    if (bHostileShipsAtInstallation)
    {
        FRmShipBlock Block = {};
        Block.bRequired = bPkHostileShips;
        Block.bKillAllShips = bPkHostileShips;
        Block.Size.Difficulty = 0.0f;
        Block.SpawnTriggerParam = -1;
        PickRmShipFormationSize(&Block.Size, HostileShare, Query->EnemyGroup);
        bool const bBuilt =
            BuildRmShipGroup(Query, &Offer->Ships, &Block, &Position, false, nullptr);
        bSuccess = bSuccess && bBuilt;
    }

    if (bFriendlyShipsAtInstallation)
    {
        FRmShipBlock Block = {};
        Block.Size.Difficulty = 0.0f;
        Block.SpawnTriggerMode = 1;
        Block.SpawnTriggerParam = 1;
        PickRmShipFormationSize(&Block.Size, FriendlyShare, static_cast<unsigned int>(Query->OfferGroup));
        FRmShipBlock Friendly = Block;
        if (Block.Size.Count == 0)
        {
            if (Block.Size.Level == 0)
                Friendly.Size.Level = 1;
            Friendly.Size.Count = 1;
        }
        bool const bBuilt =
            BuildRmShipGroup(Query, &Offer->Ships, &Friendly, &Position, true, nullptr);
        bSuccess = bSuccess && bBuilt;
    }

    if (bHostileShipsAfter30S)
    {
        FRmShipBlock Block = {};
        Block.bRequired = bPkHostileShips;
        Block.bKillAllShips = bPkHostileShips;
        Block.Size.Difficulty = 0.0f;
        if (bHostileShipsAtInstallation)
        {
            Block.SpawnTriggerMode = 3;
            Block.SpawnTriggerParam = 1;
        }
        else
        {
            Block.SpawnTriggerMode = 1;
            Block.SpawnTriggerParam = 8;
        }
        PickRmShipFormationSize(&Block.Size, HostileShare, Query->EnemyGroup);
        bool const bBuilt =
            BuildRmShipGroup(Query, &Offer->Ships, &Block, &Position, false, nullptr);
        bSuccess = bSuccess && bBuilt;
    }

    if (bFriendlyShipsAfter30S)
    {
        FRmShipBlock Block = {};
        Block.Size.Difficulty = 0.0f;
        Block.SpawnTriggerMode = 1;
        Block.SpawnTriggerParam = 9;
        PickRmShipFormationSize(&Block.Size, FriendlyShare, static_cast<unsigned int>(Query->OfferGroup));
        FRmShipBlock Friendly = Block;
        if (Block.Size.Count == 0)
        {
            if (Block.Size.Level == 0)
                Friendly.Size.Level = 1;
            Friendly.Size.Count = 1;
        }
        bool const bBuilt =
            BuildRmShipGroup(Query, &Offer->Ships, &Friendly, &Position, true, nullptr);
        bSuccess = bSuccess && bBuilt;
    }

    if (bHostileWaves)
    {
        FRmShipBlock Block = {};
        Block.bKillAllShips = bPkHostileShips;
        Block.Size.Difficulty = 0.0f;
        if ((bHostileShipsAtInstallation || bHostileShipsAfter30S) && (rand() % 2) != 0)
        {
            Block.SpawnTriggerMode = 3;
            Block.SpawnTriggerParam = 1;
        }
        else
        {
            Block.SpawnTriggerMode = 1;
            Block.SpawnTriggerParam = 0xf0;
        }
        PickRmShipFormationSize(&Block.Size, HostileShare, Query->EnemyGroup);
        bool const bBuilt =
            BuildRmShipGroup(Query, &Offer->Ships, &Block, &Position, false, nullptr);
        bSuccess = bSuccess && bBuilt;
    }

    if (*OutOffer != Offer)
    {
        if (*OutOffer != nullptr)
        {
            (*OutOffer)->Release();
            *OutOffer = nullptr;
        }
        *OutOffer = Offer;
        Offer->AddRef();
    }
    return bSuccess;
}

// FUN_06f9ac80. Each scale product is stored as float; 0.4 is DAT_06fbd188, 0.6
// DAT_06fbd180, 0.39999998 DAT_06fbd184.
bool FRmDestroySolarsFilter::FillSolarSpawns(FRmOfferQuery const* Query,
                                             FRmSolarComponent* Solars) const
{
    Solars->Position = Query->ObjectivePos;
    Solars->bActive = true;

    std::vector<int> Types;
    if (bBigSolar)
    {
        if (std::find(Types.begin(), Types.end(), 0) == Types.end())
            Types.push_back(0);
    }
    if (bDefensiveSolars)
    {
        if (std::find(Types.begin(), Types.end(), 1) == Types.end())
            Types.push_back(1);
    }

    FRmSolarFormation* const Formation = PickSolarFormation(&Types);
    if (Formation == nullptr)
        return false;

    unsigned long NestedProps = 0;
    pub::System::GetNestedProperties(Query->DestSys, Query->ObjectivePos, NestedProps);

    std::vector<std::pair<FRmKillableSolar*, float>> Entries;
    for (std::vector<int>::const_iterator Type = Types.begin(); Type != Types.end(); ++Type)
    {
        float Scaled = Query->Difficulty;
        if (*Type == 0)
        {
            if (bHostileShipsAtInstallation)
                Scaled = Scaled * 0.4f;
            if (bDefensiveSolars)
                Scaled = Scaled * 0.6f;
        }
        else if (*Type == 1)
        {
            if (bHostileShipsAtInstallation)
                Scaled = Scaled * 0.4f;
            if (bBigSolar)
                Scaled = Scaled * 0.39999998f;
        }
        FRmKillableSolar* const Entry = PickKillableSolar(
            Query->EnemyGroup, *Type, static_cast<unsigned int>(NestedProps), Scaled);
        if (Entry == nullptr)
            return false;
        Entries.insert(Entries.end(), 1, std::pair<FRmKillableSolar*, float>(Entry, Scaled));
    }

    SpawnSolars(Query, Query->EnemyGroup, Query->Difficulty, Formation, &Entries,
                &Solars->Solars);
    return true;
}

// FUN_06f9af40. The solar local is FUN_06f852e0; -0.9 is DAT_06fbcbd0. An unordered fraction
// takes the cap.
void FRmDestroySolarsFilter::SpawnSolars(
    FRmOfferQuery const* Query, unsigned int EnemyGroup, float Difficulty,
    FRmSolarFormation* Formation,
    std::vector<std::pair<FRmKillableSolar*, float>> const* Entries,
    std::list<FRmSolarSpawn>* OutSolars) const
{
    int const SlotCount = Formation->GetSlotCount();
    float Committed = 0.0f;
    for (std::vector<std::pair<FRmKillableSolar*, float>>::const_iterator Entry =
             Entries->begin();
         Entry != Entries->end(); ++Entry)
    {
        FRmKillableSolar const* const Arch = Entry->first;
        float const Scaled = Entry->second;

        int Mode = 0;
        if (Arch->SolarType == 0)
        {
            if (bPkBigSolar)
                Mode = 2;
        }
        else if (Arch->SolarType == 1)
        {
            if (bPkDefensiveSolars)
                Mode = 2;
        }

        float Used = 0.0f;
        for (int SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
        {
            FRmSolarFormationSlot const* const Slot =
                Formation->GetSlot(static_cast<unsigned int>(SlotIndex));
            if (Slot->SolarType == Arch->SolarType && (Used == 0.0f || Committed < Difficulty))
            {
                FRmSolarSpawn Solar;

                std::sprintf(GRmVignetteSolarName, "Vignette_solar_%X", GRmVignetteSolarCounter);
                ++GRmVignetteSolarCounter;
                unsigned int Length =
                    static_cast<unsigned int>(std::strlen(GRmVignetteSolarName));
                if (Length > 0x2f)
                    Length = 0x2f;
                std::memcpy(Solar.Nickname.Data, GRmVignetteSolarName, Length);
                Solar.Nickname.Data[Length] = '\0';
                Solar.Nickname.Length = Length;

                Solar.Faction = EnemyGroup;
                Solar.Orientation = Slot->Orientation;
                Solar.Position.x = Slot->Position.X + Query->ObjectivePos.x;
                Solar.Position.y = Slot->Position.Y + Query->ObjectivePos.y;
                Solar.Position.z = Slot->Position.Z + Query->ObjectivePos.z;
                Solar.ArchetypeId = Arch->ArchetypeId;
                Solar.LoadoutId = Arch->LoadoutId;

                unsigned int const FractionCapBits = 0x3f7fbe77u;
                float FractionCap = 0.0f;
                std::memcpy(&FractionCap, &FractionCapBits, sizeof(FractionCap));
                float Fraction = (Scaled - Arch->DifficultyMin) /
                                 (Arch->DifficultyMax - Arch->DifficultyMin);
                if (!(Fraction < FractionCap))
                    Fraction = FractionCap;
                int const PilotIndex = static_cast<int>(
                    static_cast<double>(Arch->PilotChoices) * Fraction);
                unsigned int PilotId = CreateID(GRmSolarPilotNames[PilotIndex]);
                GetPilotPersonality(&PilotId, &Solar.Personality);

                Solar.Attitude = -0.9f;
                Solar.bPilot = 1;
                Solar.DestSys = Query->DestSys;
                Solar.ModeFlag = Mode;
                Solar.Hitpoints = static_cast<int>(
                    (static_cast<double>(Arch->HitpointsMax) - Arch->HitpointsMin) * Fraction +
                    Arch->HitpointsMin);
                Solar.Loot.clear();
                Solar.SolarType = Arch->SolarType;
                Solar.Arch = Arch;
                Used = Scaled + Used;
                OutSolars->push_back(Solar);
            }
            if (Used + Committed >= Difficulty)
                break;
        }
        Committed = Used + Committed;
    }
}

// FUN_06f9b790. 0.9 is DAT_06fbd3e4. Flags indices follow GRmAssassinateNodeNames.
bool FRmAssassinateFilter::CreateOffer(FRmOfferQuery const* Query, FRmOffer** OutOffer)
{
    FRmVignetteOffer* const NewOffer = new FRmVignetteOffer(Flags[0] ? 2 : 3);
    TRmRef<FRmVignetteOffer> Holder(NewOffer);
    NewOffer->Release();
    FRmVignetteOffer* const Offer = Holder.Get();

    Offer->General.bActive = true;
    if (Flags[0])
    {
        if (Flags[3])
            Offer->Type = 4;
        else if (Flags[4])
            Offer->Type = 5;
        else if (Flags[5])
            Offer->Type = 3;
        else
            Offer->Type = 2;
    }
    else
    {
        Offer->Type = 0;
    }

    FRmOfferQuery Scaled = *Query;
    if (Flags[2] && Flags[3])
        Scaled.Difficulty = Scaled.Difficulty * 0.9f;

    if (Flags[0])
    {
        FFactionNames* const Names = GetFactionNames(&Scaled.EnemyGroup);
        if (Names->MaleFirstNames.IsEmpty())
            return false;
        Offer->TargetName.Gender = 1;
        FNpcNameRequest Request;
        if (!BuildNpcName(Scaled.EnemyGroup, &Request, &Offer->TargetName, true))
            return false;
    }

    int Count = 0;
    if (Flags[7])
        Count = Flags[19] ? static_cast<int>(Flags[21]) + 1
                          : static_cast<int>(Flags[9]) + static_cast<int>(Flags[8]);
    int const MainBattle =
        Flags[19] ? static_cast<int>(Flags[18]) + static_cast<int>(Flags[23]) * 2
                  : static_cast<int>(Flags[17]) + static_cast<int>(Flags[18]) +
                        static_cast<int>(Flags[16]) + static_cast<int>(Flags[15]);
    Count = Count + MainBattle;
    float Share = 0.0f;
    if (Count != 0)
        Share = static_cast<float>(static_cast<double>(Scaled.Difficulty) /
                                   ((static_cast<double>(Count) + 1.0) * 0.5));

    // The target ships replace the success byte; the ship groups are ANDed.
    bool bSuccess = true;
    if (Flags[7])
        bSuccess = FillTargetShips(&Scaled, &Offer->TargetShips, Share, &Offer->TargetName);
    bool const bShips =
        FillShipGroups(&Scaled, &Offer->Ships, Share, &Offer->Solars, &Offer->TargetName);
    bSuccess = bSuccess && bShips;

    if (*OutOffer != Offer)
    {
        if (*OutOffer != nullptr)
        {
            (*OutOffer)->Release();
            *OutOffer = nullptr;
        }
        *OutOffer = Offer;
        Offer->AddRef();
    }
    return bSuccess;
}

namespace
{
    // The inline block templates of FUN_06f9c100 / FUN_06f9c410.
    void ResetRmShipBlock(FRmShipBlock* Block, unsigned char Field00, unsigned char Field01,
                          unsigned char Field02, unsigned char Field03, unsigned char Field04,
                          int Field18, int Field1c)
    {
        Block->bRequired = Field00;
        Block->bHasTargetShip = Field01;
        Block->bKillAllShips = Field02;
        Block->bClearPriorTargets = Field03;
        Block->bDropsCriticalLoot = Field04;
        Block->Size.Difficulty = 0.0f;
        Block->Size.Level = 0;
        Block->Size.Count = 0;
        Block->CriticalLootMode = 0;
        Block->SpawnTriggerMode = Field18;
        Block->SpawnTriggerParam = Field1c;
    }

    // The friendly block fix of FUN_06f9c410 (as FUN_06f9a700).
    FRmShipBlock CopyFriendlyRmShipBlock(FRmShipBlock const* Block)
    {
        FRmShipBlock Copy = *Block;
        if (Block->Size.Count == 0)
        {
            if (Block->Size.Level == 0)
                Copy.Size.Level = 1;
            Copy.Size.Count = 1;
        }
        return Copy;
    }

    // The critical loot kind of a target block: bring_back_loot 3, else tractor_in_loot 2,
    // else destroy_loot 1, else 0.
    int GetRmCriticalLootKind(bool const* Flags)
    {
        if (Flags[3])
            return 3;
        if (Flags[4])
            return 2;
        return Flags[5] ? 1 : 0;
    }
}

// FUN_06f9c100. Ring radii 6000.0 / 5500.0 are DAT_06fbcbcc / DAT_06fbd3e8, 1.5 is
// DAT_06fb6058. Flags[19] pk_all_ships, [10] pre_battle_wave_runs, [21] pre_battle_wave_2,
// [9] pre_battle_non_target_wave, [8] target_spawns_at_pre_battle, [2]
// target_drops_critical_loot. The end trigger (component +0x50 / +0x54) is stored last.
bool FRmAssassinateFilter::FillTargetShips(FRmOfferQuery const* Query,
                                           FRmTargetShipComponent* TargetShips, float Share,
                                           FNpcNameParams* TargetName) const
{
    int Field50 = 0;
    int Field54 = -1;
    bool bSuccess = true;

    Vector const Offset = random_inside_ring(6000.0f, 6000.0f - 500.0f, 300.0f);
    Vector Position;
    Position.x = Query->ObjectivePos.x + Offset.x;
    Position.y = Query->ObjectivePos.y + Offset.y;
    Position.z = Query->ObjectivePos.z + Offset.z;
    TargetShips->Position = Position;

    FRmShipBlock Block;
    if (Flags[19])
    {
        if (Flags[10])
        {
            Field50 = 2;
            Field54 = 1;
        }
        ResetRmShipBlock(&Block, 1, 0, 1, 0, 0, 0, -1);
        PickRmShipFormationSize(&Block.Size, Share, Query->EnemyGroup);
        bSuccess = BuildRmShipGroup(Query, TargetShips, &Block, &Position, false, nullptr);
        if (Flags[21])
        {
            Block.SpawnTriggerMode = 3;
            Block.SpawnTriggerParam = 1;
            PickRmShipFormationSize(&Block.Size, Share, Query->EnemyGroup);
            bool const bBuilt =
                BuildRmShipGroup(Query, TargetShips, &Block, &Position, false, nullptr);
            bSuccess = bSuccess && bBuilt;
        }
    }
    else
    {
        if (Flags[9])
        {
            ResetRmShipBlock(&Block, 1, 0, 1, 0, 0, 0, -1);
            if (!Flags[8])
            {
                if (Flags[10])
                {
                    Field50 = 2;
                    Field54 = 1;
                }
                else
                {
                    Field50 = 0;
                }
            }
            PickRmShipFormationSize(&Block.Size, Share, Query->EnemyGroup);
            bSuccess = BuildRmShipGroup(Query, TargetShips, &Block, &Position, false, nullptr);
        }
        if (Flags[8])
        {
            ResetRmShipBlock(&Block, 1, 1, 0, 1, Flags[2] ? 1 : 0, 0, -1);
            if (Flags[2])
                Block.CriticalLootMode = GetRmCriticalLootKind(Flags);
            if (Flags[9])
            {
                std::vector<FRmShipSpawn> const& Ships = TargetShips->Groups.back().Ships;
                if (Ships.size() > 1)
                {
                    Block.SpawnTriggerMode = 3;
                    Block.SpawnTriggerParam = 1;
                }
                else
                {
                    Block.SpawnTriggerMode = 2;
                    Block.SpawnTriggerParam = 8;
                }
            }
            else
            {
                Block.SpawnTriggerMode = 0;
            }
            Field50 = 3;
            Field54 = 0x63;
            PickRmShipFormationSize(&Block.Size, Share * 1.5f, Query->EnemyGroup);
            bool const bBuilt =
                BuildRmShipGroup(Query, TargetShips, &Block, &Position, false, TargetName);
            bSuccess = bSuccess && bBuilt;
        }
    }

    TargetShips->EndTriggerMode = Field50;
    TargetShips->EndTriggerParam = Field54;
    return bSuccess;
}

// FUN_06f9c410. 0.5 is DAT_06fb605c, 1.3 DAT_06fbd3ec. Flags[14]
// defensive_solars_at_main_battle, [19] pk_all_ships, [22] / [23] main_battle_wave_1 / _2,
// [24] friendly_ships_at_main_battle, [25] friendly_ships_after_30_s, [18]
// continuous_reenforcements, [26] friendly_ships_come_in_to_whoop_up, [16]
// main_battle_non_target_wave, [15] target_spawns_at_main_battle, [17]
// wave_just_after_main_battle_starts. The size picks reuse each block, so a failed pick
// keeps the previous level.
bool FRmAssassinateFilter::FillShipGroups(FRmOfferQuery const* Query, FRmShipComponent* Ships,
                                          float Share, FRmSolarComponent* Solars,
                                          FNpcNameParams* TargetName) const
{
    Vector const Position = Query->ObjectivePos;
    bool bSuccess = true;
    if (Flags[14])
        bSuccess = FillDefensiveSolars(Query, Solars);
    Ships->Position = Query->ObjectivePos;
    float FriendlyShare = Share * 0.5f;
    unsigned int const OfferGroup = static_cast<unsigned int>(Query->OfferGroup);

    FRmShipBlock Hostile;
    FRmShipBlock Friendly;
    if (Flags[19])
    {
        ResetRmShipBlock(&Hostile, 1, 0, 1, 0, 0, 0, -1);
        ResetRmShipBlock(&Friendly, 0, 0, 0, 0, 0, 2, 1);
        if (Flags[22])
        {
            PickRmShipFormationSize(&Hostile.Size, Share, Query->EnemyGroup);
            bool const bBuilt =
                BuildRmShipGroup(Query, Ships, &Hostile, &Position, false, nullptr);
            bSuccess = bSuccess && bBuilt;
        }
        if (Flags[24])
        {
            PickRmShipFormationSize(&Friendly.Size, FriendlyShare, OfferGroup);
            FRmShipBlock const Copy = CopyFriendlyRmShipBlock(&Friendly);
            bool const bBuilt = BuildRmShipGroup(Query, Ships, &Copy, &Position, true, nullptr);
            bSuccess = bSuccess && bBuilt;
        }
        if (Flags[23])
        {
            if (Flags[22])
            {
                Hostile.SpawnTriggerMode = 3;
                Hostile.SpawnTriggerParam = 1;
            }
            else
            {
                Hostile.SpawnTriggerMode = 1;
                Hostile.SpawnTriggerParam = 8;
            }
            PickRmShipFormationSize(&Hostile.Size, Share, Query->EnemyGroup);
            bool const bBuilt =
                BuildRmShipGroup(Query, Ships, &Hostile, &Position, false, nullptr);
            bSuccess = bSuccess && bBuilt;
        }
        if (Flags[25])
        {
            Friendly.SpawnTriggerMode = 1;
            Friendly.SpawnTriggerParam = 0x3c;
            PickRmShipFormationSize(&Friendly.Size, FriendlyShare, OfferGroup);
            FRmShipBlock const Copy = CopyFriendlyRmShipBlock(&Friendly);
            bool const bBuilt = BuildRmShipGroup(Query, Ships, &Copy, &Position, true, nullptr);
            bSuccess = bSuccess && bBuilt;
        }
        if (Flags[18])
        {
            Hostile.bRequired = 0;
            Hostile.bKillAllShips = 1;
            Hostile.SpawnTriggerMode = 2;
            Hostile.SpawnTriggerParam = 0xf0;
            PickRmShipFormationSize(&Hostile.Size, Share, Query->EnemyGroup);
            bool const bBuilt =
                BuildRmShipGroup(Query, Ships, &Hostile, &Position, false, nullptr);
            return bSuccess && bBuilt;
        }
        if (Flags[26])
        {
            Friendly.SpawnTriggerMode = 2;
            Friendly.SpawnTriggerParam = 9;
            PickRmShipFormationSize(&Friendly.Size, FriendlyShare, OfferGroup);
            for (int Wave = 0; Wave < 3; ++Wave)
            {
                if (Wave != 0)
                    Friendly.SpawnTriggerParam = 0x19;
                FRmShipBlock const Copy = CopyFriendlyRmShipBlock(&Friendly);
                bool const bBuilt =
                    BuildRmShipGroup(Query, Ships, &Copy, &Position, true, nullptr);
                bSuccess = bSuccess && bBuilt;
            }
        }
        return bSuccess;
    }

    if (Flags[16])
    {
        unsigned char const bTarget = Flags[15] ? 1 : 0;
        ResetRmShipBlock(&Hostile, bTarget, 0, bTarget, 0, 0, 0, -1);
        PickRmShipFormationSize(&Hostile.Size, Share, Query->EnemyGroup);
        bool const bBuilt = BuildRmShipGroup(Query, Ships, &Hostile, &Position, false, nullptr);
        bSuccess = bSuccess && bBuilt;
    }
    if (Flags[15])
    {
        ResetRmShipBlock(&Hostile, 1, 1, 0, 1, 0, 0, -1);
        if (Flags[16])
        {
            Hostile.SpawnTriggerMode = 2;
            Hostile.SpawnTriggerParam = 0x2d;
        }
        else
        {
            Hostile.SpawnTriggerMode = 0;
        }
        Hostile.bDropsCriticalLoot = Flags[2] ? 1 : 0;
        if (Flags[2])
            Hostile.CriticalLootMode = GetRmCriticalLootKind(Flags);
        FriendlyShare = Share * 1.5f;
        if (!Flags[16])
            FriendlyShare = FriendlyShare * 1.3f;
        PickRmShipFormationSize(&Hostile.Size, FriendlyShare, Query->EnemyGroup);
        bool const bBuilt =
            BuildRmShipGroup(Query, Ships, &Hostile, &Position, false, TargetName);
        bSuccess = bSuccess && bBuilt;
    }
    if (Flags[17])
    {
        ResetRmShipBlock(&Hostile, 0, 0, 0, 0, 0, 2, 0xf0);
        PickRmShipFormationSize(&Hostile.Size, Share, Query->EnemyGroup);
        bool const bBuilt = BuildRmShipGroup(Query, Ships, &Hostile, &Position, false, nullptr);
        bSuccess = bSuccess && bBuilt;
    }
    if (Flags[18])
    {
        ResetRmShipBlock(&Hostile, 0, 0, 0, 0, 0, 2, 0x78);
        PickRmShipFormationSize(&Hostile.Size, Share, Query->EnemyGroup);
        bool const bBuilt = BuildRmShipGroup(Query, Ships, &Hostile, &Position, false, nullptr);
        bSuccess = bSuccess && bBuilt;
    }
    return bSuccess;
}

// FUN_06f9bee0. 0.25 is DAT_06fbbb5c.
bool FRmAssassinateFilter::FillDefensiveSolars(FRmOfferQuery const* Query,
                                               FRmSolarComponent* Solars) const
{
    int const Count = (rand() * 3) / 0x8000 + 1;
    Solars->bActive = true;
    Solars->Position = Query->ObjectivePos;

    std::vector<int> Types;
    int const Type = 1;
    if (std::find(Types.begin(), Types.end(), Type) == Types.end())
        Types.insert(Types.end(), Type);

    FRmSolarFormation* const Formation = PickSolarFormation(&Types);
    if (Formation == nullptr)
        return false;

    unsigned long NestedProps = 0;
    pub::System::GetNestedProperties(Query->DestSys, Query->ObjectivePos, NestedProps);

    std::vector<std::pair<FRmKillableSolar*, float>> Entries;
    float const Total = Query->Difficulty * 0.25f;
    float const PerSolar =
        static_cast<float>(static_cast<double>(Total) / static_cast<double>(Count));
    FRmKillableSolar* const Entry = PickKillableSolar(
        Query->EnemyGroup, 1, static_cast<unsigned int>(NestedProps), PerSolar);
    if (Entry == nullptr)
        return false;
    Entries.insert(Entries.end(), 1, std::pair<FRmKillableSolar*, float>(Entry, PerSolar));

    SpawnSolars(Query, Query->EnemyGroup, Total, Formation, &Entries, &Solars->Solars);
    return true;
}

// FUN_06f9bb00: FUN_06f9af40 without the pk mode (each solar's +0x4d8 is 0).
void FRmAssassinateFilter::SpawnSolars(
    FRmOfferQuery const* Query, unsigned int EnemyGroup, float Difficulty,
    FRmSolarFormation* Formation,
    std::vector<std::pair<FRmKillableSolar*, float>> const* Entries,
    std::list<FRmSolarSpawn>* OutSolars) const
{
    int const SlotCount = Formation->GetSlotCount();
    float Committed = 0.0f;
    for (std::vector<std::pair<FRmKillableSolar*, float>>::const_iterator Entry =
             Entries->begin();
         Entry != Entries->end(); ++Entry)
    {
        FRmKillableSolar const* const Arch = Entry->first;
        float const Scaled = Entry->second;

        float Used = 0.0f;
        for (int SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
        {
            FRmSolarFormationSlot const* const Slot =
                Formation->GetSlot(static_cast<unsigned int>(SlotIndex));
            if (Slot->SolarType == Arch->SolarType && (Used == 0.0f || Committed < Difficulty))
            {
                FRmSolarSpawn Solar;

                std::sprintf(GRmVignetteSolarName, "Vignette_solar_%X", GRmVignetteSolarCounter);
                ++GRmVignetteSolarCounter;
                unsigned int Length =
                    static_cast<unsigned int>(std::strlen(GRmVignetteSolarName));
                if (Length > 0x2f)
                    Length = 0x2f;
                std::memcpy(Solar.Nickname.Data, GRmVignetteSolarName, Length);
                Solar.Nickname.Data[Length] = '\0';
                Solar.Nickname.Length = Length;

                Solar.Faction = EnemyGroup;
                Solar.Orientation = Slot->Orientation;
                Solar.Position.x = Slot->Position.X + Query->ObjectivePos.x;
                Solar.Position.y = Slot->Position.Y + Query->ObjectivePos.y;
                Solar.Position.z = Slot->Position.Z + Query->ObjectivePos.z;
                Solar.ArchetypeId = Arch->ArchetypeId;
                Solar.LoadoutId = Arch->LoadoutId;

                unsigned int const FractionCapBits = 0x3f7fbe77u;
                float FractionCap = 0.0f;
                std::memcpy(&FractionCap, &FractionCapBits, sizeof(FractionCap));
                float Fraction = (Scaled - Arch->DifficultyMin) /
                                 (Arch->DifficultyMax - Arch->DifficultyMin);
                if (!(Fraction < FractionCap))
                    Fraction = FractionCap;
                int const PilotIndex = static_cast<int>(
                    static_cast<double>(Arch->PilotChoices) * Fraction);
                unsigned int PilotId = CreateID(GRmSolarPilotNames[PilotIndex]);
                GetPilotPersonality(&PilotId, &Solar.Personality);

                Solar.Attitude = -0.9f;
                Solar.bPilot = 1;
                Solar.DestSys = Query->DestSys;
                Solar.ModeFlag = 0;
                Solar.Hitpoints = static_cast<int>(
                    (static_cast<double>(Arch->HitpointsMax) - Arch->HitpointsMin) * Fraction +
                    Arch->HitpointsMin);
                Solar.Loot.clear();
                Solar.SolarType = Arch->SolarType;
                Solar.Arch = Arch;
                Used = Scaled + Used;
                OutSolars->push_back(Solar);
            }
            if (Used + Committed >= Difficulty)
                break;
        }
        Committed = Used + Committed;
    }
}

// FUN_06f9f6d0. NaN fails the RepReward, RepHit and lower Difficulty tests; the
// |ObjectivePos|^2 sum is unrounded x87 and fails only on == 0.
bool FRandomMissionGenerator::BuildOffer(FRmOfferQuery const* Query, FRmOffer** OutOffer,
                                         FRmTreePath* OutPath)
{
    RegisterRmDestSystem(Query->DestSys);
    if (*OutOffer != nullptr)
    {
        (*OutOffer)->Release();
        *OutOffer = nullptr;
    }

    if (Query->OfferGroup == -1)
        return false;
    if (Query->EnemyGroup == 0xffffffffu)
        return false;
    if (Query->Reward <= 0)
        return false;
    if (!(Query->RepReward > 0.0f))
        return false;
    if (!(Query->RepHit < 0.0f))
        return false;
    if (Query->OfferBase == 0)
        return false;
    if (Query->Difficulty < 0.0f || std::isnan(Query->Difficulty))
        return false;
    if (Query->Difficulty > 100.0f)
        return false;
    if (Query->ObjectiveZone == 0)
        return false;
    {
        double const X = Query->ObjectivePos.x;
        double const Y = Query->ObjectivePos.y;
        double const Z = Query->ObjectivePos.z;
        if (X * X + Y * Y + Z * Z == 0.0)
            return false;
    }
    if (Query->PlayerId == 0)
        return false;
    if (Query->DestSys == 0)
        return false;

    unsigned int const OfferNodeId = PickOfferNode(Query);
    if (OfferNodeId == 0xffffffffu)
        return false;
    if (!ChooseTreePath(OutPath, OfferNodeId, Query->Difficulty, Query->OfferGroup))
        return false;
    return BuildOfferFromPath(OutPath, Query, OutOffer);
}

// FUN_06f9e9b0. The system / zone lookups (FUN_06f75f30 -> FUN_06f72cb0) and
// GetFactionProps are unchecked, as retail. Filters are freed with plain operator delete.
bool FRandomMissionGenerator::BuildOfferFromPath(FRmTreePath const* Path,
                                                 FRmOfferQuery const* Query,
                                                 FRmOffer** OutOffer)
{
    FRmPathFilter* Filter = new FRmPathFilter();
    FSystemInfo* System = FindSystemInfo(&Query->DestSys);
    unsigned int const ZoneTypes = System->FindZone(Query->ObjectiveZone)->VignetteType;

    bool bBuilt = false;
    FRmVignetteNode const* Node = &Nodes.find(RootNodeId)->second;
    std::vector<char>::const_iterator BitIt = Path->Bits.begin();
    for (; BitIt != Path->Bits.end(); ++BitIt)
    {
        if ((Node->Params.AllowableZoneTypes & ZoneTypes) == 0)
            break;
        char const Bit = *BitIt;
        if (Filter->ApplyNode(Node, Bit) && Filter->HasMissionType())
        {
            FRmPathFilter* Specialized = Filter->Specialize();
            ::operator delete(static_cast<void*>(Filter));
            Filter = Specialized;
        }
        unsigned int const NextId = (Bit == 1) ? Node->Children[0] : Node->Children[1];
        Node = &Nodes.find(NextId)->second;
    }

    if (BitIt == Path->Bits.end() && Filter->CreateOffer(Query, OutOffer) &&
        *OutOffer != nullptr)
    {
        FRmOffer* const Offer = *OutOffer;
        Offer->System = FindSystemInfo(&Query->DestSys);
        Offer->OfferGroup = Query->OfferGroup;
        Offer->HostileGroup = Query->EnemyGroup;
        Offer->Difficulty = RemapRmOfferDifficulty(Query->Difficulty);
        Offer->Reward = Query->Reward;
        Offer->RepHit = Query->RepHit;
        Offer->RepReward = Query->RepReward;
        Offer->OfferBase = Query->OfferBase;
        unsigned int const OfferGroup = static_cast<unsigned int>(Query->OfferGroup);
        Offer->McCostume = GetFactionProps(&OfferGroup)->McCostume;
        pub::GetVoiceID(Offer->VoiceId, "mc_leg_m01");
        BuildOfferTexts(Query, Path, Filter, *OutOffer, OutOffer);
        // *OutOffer is read again; a local holder plus its by-value copy for FUN_06f9e770.
        FRmOffer* const Built = *OutOffer;
        if (Built->MachineType > 0 && Built->MachineType <= 3)
        {
            TRmRef<FRmOffer> Holder(Built);
            BuildCommSequences(Path, Holder);
        }
        bBuilt = true;
    }

    ::operator delete(static_cast<void*>(Filter));
    return bBuilt;
}

// FUN_06f9e1f0. GetFactionProps (FUN_06f50650) and the node finds (FUN_06f7b0f0) are
// unchecked, and the path iterator has no end check, as retail. For objective texts a later
// node wins. A "replace" offer text resets Text to an empty mad-lib first.
void FRandomMissionGenerator::BuildOfferTexts(FRmOfferQuery const* Query,
                                              FRmTreePath const* Path, FRmPathFilter* Filter,
                                              FRmOffer* Source, FRmOffer** OutOffer)
{
    FRmOffer* const Offer = *OutOffer;
    FmtStr* const Text = &Offer->Text;
    unsigned int const OfferGroup = static_cast<unsigned int>(Query->OfferGroup);
    bool const bOfferGroupSingular = GetFactionProps(&OfferGroup)->bSingularNickname;
    bool const bHostileGroupSingular = GetFactionProps(&Query->EnemyGroup)->bSingularNickname;

    FRmVignetteNode* Node = &Nodes.find(RootNodeId)->second;
    FRmObjectiveText Objectives;
    Text->begin_mad_lib(0x50001);

    std::vector<char>::const_iterator BitIt = Path->Bits.begin();
    for (;;)
    {
        std::map<int, FRmParamText> const& NodeTexts = Node->Params.ObjectiveText.Texts;
        for (std::map<int, FRmParamText>::const_iterator It = NodeTexts.begin();
             It != NodeTexts.end(); ++It)
        {
            std::pair<std::map<int, FRmParamText>::iterator, bool> Inserted =
                Objectives.Texts.insert(std::make_pair(It->first, FRmParamText()));
            Inserted.first->second = It->second;
        }

        std::vector<FRmOfferText>& OfferTexts = Node->Params.OfferTexts;
        if (!OfferTexts.empty())
        {
            int const Count = static_cast<int>(OfferTexts.size());
            int Index = 0;
            if (Count - 1 != 0)
                Index = (rand() * Count) / 0x8000;
            FRmOfferText* const Chosen = &OfferTexts[0] + static_cast<unsigned int>(Index);
            if (!Chosen->bAppend)
            {
                Text->destroy();
                Text->Head = nullptr;
                Text->MadLibId = 0;
                for (int Kind = 0; Kind < FmtStr::KindCount; ++Kind)
                    Text->ValueIndex[Kind] = 0;
                Text->begin_mad_lib(0x50001);
            }
            Chosen->Build(bOfferGroupSingular, bHostileGroupSingular, Text, Source);
        }

        if (!Node->Params.RewardText.IsEmpty())
            Offer->RewardText = Node->Params.RewardText;
        if (!Node->Params.FailureText.IsEmpty())
            Offer->FailureText = Node->Params.FailureText;

        if (Node->Children.empty())
            break;
        unsigned int const NextId = (*BitIt == 1) ? Node->Children[0] : Node->Children[1];
        Node = &Nodes.find(NextId)->second;
        ++BitIt;
    }

    BuildObjectives(Query, OutOffer, &Objectives, Filter);
    Text->end_mad_lib();
}

// FUN_06f9dee0. Objectives: keys 1..3 (guide_to_*) are FUN_06f855f0 (new 0x5c), the rest
// FUN_06f858c0 (new 0x70). Retail quirks kept: a key outside the switch inserts through a
// null map, and an objective whose key is already present is leaked.
void FRandomMissionGenerator::BuildObjectives(FRmOfferQuery const* Query, FRmOffer** OutOffer,
                                              FRmObjectiveText const* Texts,
                                              FRmPathFilter* /*Filter*/)
{
    TRmRef<FRmOffer> Holder(*OutOffer);
    FRmVignetteOffer* const Offer = static_cast<FRmVignetteOffer*>(Holder.Get());
    FRmObjectiveMap* const GeneralMap = &Offer->General.Objectives;
    FRmObjectiveMap* const SolarMap = &Offer->Solars.Objectives;
    FRmObjectiveMap* const TargetMap = &Offer->TargetShips.Objectives;
    FRmObjectiveMap* const ShipMap = &Offer->Ships.Objectives;

    for (std::map<int, FRmParamText>::const_iterator It = Texts->Texts.begin();
         It != Texts->Texts.end(); ++It)
    {
        int Key = It->first;
        FRmParamText* Objective = nullptr;
        if (Key == 2 || Key == 1 || Key == 3)
        {
            FRmObjectiveType2* const Guide = new FRmObjectiveType2(&It->second);
            Guide->SetMarker(Query->DestSys, 0x44d, Query->ObjectivePos);
            Objective = Guide;
        }
        else
        {
            Objective = new FRmObjectiveType3(&It->second);
        }

        FRmObjectiveMap* Map = nullptr;
        switch (Key)
        {
        case 1:
        case 2:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
            Map = GeneralMap;
            break;
        case 4:
        case 5:
            Map = SolarMap;
            break;
        case 3:
        case 19:
        case 20:
        case 21:
            Map = TargetMap;
            break;
        case 22:
        case 23:
        case 24:
            Map = ShipMap;
            break;
        default:
            break;
        }

        switch (Key)
        {
        case 19:
        case 22:
            Key = 6;
            break;
        case 20:
        case 23:
            Key = 7;
            break;
        case 21:
        case 24:
            Key = 8;
            break;
        default:
            break;
        }

        Map->Objectives.insert(std::make_pair(Key, Objective));
    }
}

// FUN_06f9e770. Each conversation is a local (FUN_06f849f0) appended by copy. The delay is
// computed on x87 (1/32768 is DAT_06fb4448) and stored as float. Node finds are unchecked.
void FRandomMissionGenerator::BuildCommSequences(FRmTreePath const* Path, TRmRef<FRmOffer> Offer)
{
    FRmVignetteOffer* const Vignette = static_cast<FRmVignetteOffer*>(Offer.Get());
    FRmVignetteNode const* Node = &Nodes.find(RootNodeId)->second;
    std::vector<char>::const_iterator BitIt = Path->Bits.begin();
    for (;;)
    {
        std::vector<FRmCommSequence> const& Sequences = Node->Params.CommSequences;
        for (std::vector<FRmCommSequence>::const_iterator Sequence = Sequences.begin();
             Sequence != Sequences.end(); ++Sequence)
        {
            {
                FRmConversation Local;
                Vignette->General.Conversations.insert(Vignette->General.Conversations.end(),
                                                       Local);
            }
            FRmConversation& Conversation = Vignette->General.Conversations.back();
            Conversation.SetTrigger(Sequence->Trigger);
            double const Fraction =
                static_cast<double>(std::rand()) * static_cast<double>(3.0517578125e-05f);
            double const Delay =
                (static_cast<double>(Sequence->MaxDelay) - static_cast<double>(Sequence->MinDelay)) *
                    Fraction +
                static_cast<double>(Sequence->MinDelay);
            Conversation.SetDelay(static_cast<float>(Delay));
            for (std::vector<FRmCommLine>::const_iterator CommLine = Sequence->Lines.begin();
                 CommLine != Sequence->Lines.end(); ++CommLine)
            {
                FRmLine* const Line = new FRmLine();
                Line->TargetKind = Sequence->Target;
                Line->SpeakerKind = CommLine->Speaker;
                Line->Text = CommLine->Text;
                FGcsLine* const Appended = Line;
                Conversation.Lines.insert(Conversation.Lines.end(), 1, Appended);
            }
        }
        if (Node->Children.empty())
            break;
        unsigned int const NextId = (*BitIt == 1) ? Node->Children[0] : Node->Children[1];
        Node = &Nodes.find(NextId)->second;
        ++BitIt;
    }
}
