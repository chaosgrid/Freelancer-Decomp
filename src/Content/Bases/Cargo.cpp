// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU44 (06f34ef0-06f36f10): faction / NPC ship arch / cargo helpers.

#include "Bases/Cargo.h"

#include "Bases/BaseDB.h"             // FContentBase, FBaseCommodity (TU17)
#include "Core/ContentPlayerState.h" // FContentPlayerStateBase::GetAffiliation (FUN_06eb6bb0)
#include "Core/ContentStaticIds.h"   // TU44's 16 static-init thunks 06f34ef0-06f350d0
#include "Encounters/EncounterParams.h"    // FEncounterShip
#include "NPCs/FactionProps.h"       // GetFactionProps, GetFactionGroups (TU48)
#include "Guides/Guides.h"             // FGuideDesc (TU37)
#include "NPCs/NpcShip.h"            // FContentNpcShipParams (TU43)
#include "NPCs/NpcShips.h"           // FNpcShipArch, GNpcShipArchs, FindNpcShipArch (TU42)
#include "RandomMissions/RandomMissionDB.h"    // FindShipClass, GRmShipMissionProperties (TU51)
#include "common_server.h"      // pub::Reputation::GetReputation, pub::GetCargoHoldSize
#include "server_cobject.h"     // pub::SpaceObj::CargoDesc

#include <cstdlib>

namespace
{
    bool ContainsId(std::vector<unsigned int> const& Ids, unsigned int Id)
    {
        for (std::vector<unsigned int>::const_iterator It = Ids.begin(); It != Ids.end(); ++It)
        {
            if (*It == Id)
                return true;
        }
        return false;
    }

    // The unique append of FUN_06f35310 / FUN_06f35540 (retail re-finds after the insert and
    // drops the result).
    void AddUniqueId(std::vector<unsigned int>* Ids, unsigned int Id)
    {
        if (!ContainsId(*Ids, Id))
            Ids->insert(Ids->end(), 1, Id);
    }

    // The thresholds shared by FUN_06f350f0 / FUN_06f35140 / FUN_06f35190: 0.2f
    // (DAT_06fb95c0) and -0.2f (DAT_06fb95bc); both are ordered compares, so NaN is hostile.
    unsigned int FeelingMask(float Feeling)
    {
        if (Feeling >= 0.2f)
            return 1;
        if (Feeling > -0.2f)
            return 2;
        return 4;
    }
}

// FUN_06f350f0 (06f350f0-06f3513d, cdecl). A failed lookup leaves the local unset in
// retail; the port starts it at 0.
unsigned int __cdecl GetFactionFeelingMask(unsigned int FromGroup, unsigned int ToGroup)
{
    float Feeling = 0.0f;
    pub::Reputation::GetReputation(FromGroup, ToGroup, Feeling);
    return FeelingMask(Feeling);
}

// FUN_06f35140 (06f35140-06f3518d, cdecl): the vibe is the second argument. The local is unset in retail on a failed lookup; the port starts it
// at 0.
unsigned int __cdecl GetVibeFeelingMask(unsigned int Group, int VibeHandle)
{
    float Feeling = 0.0f;
    pub::Reputation::GetReputation(VibeHandle, Group, Feeling);
    return FeelingMask(Feeling);
}

// FUN_06f35190 (06f35190-06f35300, cdecl). The sums are float locals; the shares are
// (1.0f / Total) kept on the x87 stack times each sum, stored as floats.
void __cdecl GetHostileFactionShares(weighted_vector<unsigned int> const* Factions,
                                     unsigned int const* Group, float* OutHostileFrom,
                                     float* OutHostileTo)
{
    if (Factions->Items.empty())
    {
        *OutHostileFrom = 0.0f;
        *OutHostileTo = 0.0f;
        return;
    }
    float HostileFrom = 0.0f;
    float HostileTo = 0.0f;
    float Total = 0.0f;             // the reused argument slot
    for (std::vector<std::pair<unsigned int, float>>::const_iterator It = Factions->Items.begin();
         It != Factions->Items.end(); ++It)
    {
        float Feeling = 0.0f;
        pub::Reputation::GetReputation(*Group, It->first, Feeling);
        if ((FeelingMask(Feeling) & 4) != 0)
            HostileFrom = HostileFrom + It->second;
        float Back = 0.0f;
        pub::Reputation::GetReputation(It->first, *Group, Back);
        if ((FeelingMask(Back) & 4) != 0)
            HostileTo = HostileTo + It->second;
        Total = Total + It->second;
    }
    // 1.0f = DAT_06fb5810.
    double const Scale = 1.0 / static_cast<double>(Total);
    *OutHostileFrom = static_cast<float>(static_cast<double>(HostileFrom) * Scale);
    *OutHostileTo = static_cast<float>(Scale * static_cast<double>(HostileTo));
}

// FUN_06f35310 (06f35310-06f353f3, cdecl).
void __cdecl GetNpcArchFactions(std::vector<unsigned int> const* NpcArchs,
                                std::vector<unsigned int>* Out)
{
    std::vector<unsigned int>* const Groups = GetFactionGroups();         // FUN_06f50760
    Out->clear();
    for (std::vector<unsigned int>::iterator Group = Groups->begin(); Group != Groups->end(); ++Group)
    {
        FFactionProps* const Props = GetFactionProps(&*Group);             // FUN_06f50650
        if (Props == nullptr)
            continue;
        bool bAll = true;
        for (std::vector<unsigned int>::const_iterator Arch = NpcArchs->begin();
             Arch != NpcArchs->end(); ++Arch)
        {
            if (!ContainsId(Props->NpcShips, *Arch))
            {
                bAll = false;
                break;
            }
        }
        if (bAll)
            AddUniqueId(Out, *Group);
    }
}

// FUN_06f36d80 (06f36d80-06f36ecf, cdecl, returns the vector by value; caller 06f3547c):
// the ids of First that are also in Second, each once, in First's order.
static std::vector<unsigned int> IntersectIds(std::vector<unsigned int> const* First,
                                              std::vector<unsigned int> const* Second)
{
    std::vector<unsigned int> Result;
    for (std::vector<unsigned int>::const_iterator Id = First->begin(); Id != First->end(); ++Id)
    {
        if (!ContainsId(*Second, *Id))
            continue;
        AddUniqueId(&Result, *Id);
    }
    return Result;
}

// FUN_06f35400 (06f35400-06f3553c, cdecl). The faction's props and the arch are read
// without a test; the intersection is built and freed for every arch.
void __cdecl GetNpcClassFactions(std::vector<unsigned int> const* ClassIds,
                                 std::vector<unsigned int>* Out)
{
    std::vector<unsigned int>* const Groups = GetFactionGroups();         // FUN_06f50760
    Out->clear();
    for (std::vector<unsigned int>::iterator Group = Groups->begin(); Group != Groups->end(); ++Group)
    {
        FFactionProps* const Props = GetFactionProps(&*Group);             // FUN_06f50650
        for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
             Id != Props->NpcShips.end(); ++Id)
        {
            FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs); // FUN_06f32480
            bool const bShared = !IntersectIds(ClassIds, &Arch->NpcClasses).empty();
            if (bShared)
            {
                AddUniqueId(Out, *Group);
                break;
            }
        }
    }
}

// FUN_06f35540 (06f35540-06f35636, cdecl). The faction's props are read without a test.
void __cdecl GetShipLineFactions(FEncounterShip const* Ship, std::vector<unsigned int>* Out)
{
    std::vector<unsigned int>* const Groups = GetFactionGroups();
    Out->clear();
    for (std::vector<unsigned int>::iterator Group = Groups->begin(); Group != Groups->end(); ++Group)
    {
        FFactionProps* const Props = GetFactionProps(&*Group);
        for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
             Id != Props->NpcShips.end(); ++Id)
        {
            FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs); // FUN_06f32480
            if (NpcArchFitsShipLine(Ship, Arch))                           // FUN_06f35640
            {
                AddUniqueId(Out, *Group);
                break;
            }
        }
    }
}

// FUN_06f35640 (06f35640-06f35739, cdecl). Arch is read without a test.
bool __cdecl NpcArchFitsShipLine(FEncounterShip const* Ship, FNpcShipArch const* Arch)
{
    for (std::vector<unsigned int>::const_iterator Class = Ship->ShipClasses.begin();
         Class != Ship->ShipClasses.end(); ++Class)
    {
        std::vector<unsigned int>* const Members = FindShipClass(&*Class);   // FUN_06f58080
        if (Members == nullptr)
        {
            // The arch's own npc classes, then the ship's mission properties (DAT_06fd0dc8).
            if (ContainsId(Arch->NpcClasses, *Class))
                continue;
            std::map<unsigned int, std::vector<unsigned int>>::const_iterator Properties =
                GRmShipMissionProperties.find(Arch->ShipArchetype);
            if (Properties != GRmShipMissionProperties.end() &&
                ContainsId(Properties->second, *Class))
                continue;
            return false;
        }
        bool bMember = false;
        for (std::vector<unsigned int>::iterator Member = Members->begin();
             Member != Members->end(); ++Member)
        {
            if (ContainsId(Arch->NpcClasses, *Member))
            {
                bMember = true;
                break;
            }
        }
        if (!bMember)
            return false;
    }
    return true;
}

// FUN_06f35740 (06f35740-06f357cb, cdecl): the arch is looked up again for every class id;
// a match ends the class loop of that arch only.
bool __cdecl FactionHasNpcClass(unsigned int Group, std::vector<unsigned int> const* ClassIds)
{
    bool bResult = false;
    FFactionProps* const Props = GetFactionProps(&Group);
    if (Props == nullptr)
        return bResult;
    for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
         Id != Props->NpcShips.end(); ++Id)
    {
        for (std::vector<unsigned int>::const_iterator Class = ClassIds->begin();
             Class != ClassIds->end(); ++Class)
        {
            FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs);
            if (Arch != nullptr && ContainsId(Arch->NpcClasses, *Class))
            {
                bResult = true;
                break;
            }
        }
    }
    return bResult;
}

// FUN_06f357d0 (06f357d0-06f35883, cdecl). The faction's props are read without a test.
void __cdecl GetFactionShipLineArchs(unsigned int Group, FEncounterShip const* Ship,
                                     std::list<FNpcShipArch*>* Out)
{
    Out->clear();
    FFactionProps* const Props = GetFactionProps(&Group);
    for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
         Id != Props->NpcShips.end(); ++Id)
    {
        FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs);
        if (NpcArchFitsShipLine(Ship, Arch))
            Out->push_back(Arch);
    }
}

// FUN_06f35890 (06f35890-06f35b41, cdecl).
FNpcShipArch* __cdecl PickFactionShipLineArch(unsigned int Group, FEncounterShip const* Ship,
                                              int Level)
{
    std::list<FNpcShipArch*> Candidates;
    FNpcShipArch* Closest = nullptr;
    if (GetFactionProps(&Group) != nullptr)
    {
        // Retail looks the props up a second time for the loop.
        Candidates.clear();
        FFactionProps* const Props = GetFactionProps(&Group);
        for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
             Id != Props->NpcShips.end(); ++Id)
        {
            FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs);
            if (NpcArchFitsShipLine(Ship, Arch))
                Candidates.push_back(Arch);                                // FUN_06ecfc30
        }

        // With a level, drop archs more than one level off it and keep the closest one (only
        // a strictly closer one replaces it).
        std::list<FNpcShipArch*>::iterator It = Candidates.begin();
        while (It != Candidates.end())
        {
            if (Level <= 0)
            {
                ++It;
                continue;
            }
            int const ArchLevel = (*It)->GetLevel();                       // FUN_06f32910
            bool const bKeep = ArchLevel >= Level - 1 && ArchLevel <= Level + 1;
            if (Closest == nullptr || abs(Level - ArchLevel) < abs(Level - Closest->GetLevel()))
                Closest = *It;
            if (bKeep)
                ++It;
            else
                It = Candidates.erase(It);
        }

        // Nothing in range -> the closest one is the only candidate.
        if (Closest != nullptr && Candidates.empty())
            Candidates.push_back(Closest);                                 // FUN_06f1e390
    }

    // A random candidate: index 0 for a single one (no rand call), else
    // (rand() * count) / 0x8000.
    int const Count = static_cast<int>(Candidates.size());
    if (Count == 0)
        return Closest;
    int Index = 0;
    if (Count - 1 != 0)
        Index = (rand() * Count) / 0x8000;
    std::list<FNpcShipArch*>::iterator Picked = Candidates.begin();
    for (int Step = Index; Step != 0; --Step)
        ++Picked;
    if (Picked == Candidates.end())
        return Closest;
    return *Picked;
}

// ---------------------------------------------------------------------------------------
// Cargo generation (06f35bf0-06f36d30).
// ---------------------------------------------------------------------------------------

// FUN_06f36cf0 (06f36cf0-06f36d09, RET 8).
FCargoPick::FCargoPick(FBaseCommodity const* Entry, float const* InWeight)
    : GoodId(Entry->GoodId)
    , Count(Entry->Count)
    , Weight(*InWeight)
{
}

namespace
{
    // MSVCRT ftol: truncation to a 64-bit integer, the low dword kept.
    unsigned int FtolLow(double Value)
    {
        return static_cast<unsigned int>(static_cast<long long>(Value));
    }

    // The unsigned count (0 -> 1) as a float.
    float CargoWeightOf(unsigned int Count)
    {
        unsigned int const Weight = Count == 0 ? 1u : Count;
        return static_cast<float>(static_cast<double>(Weight));
    }

    // The intersection GenerateCargo builds twice (inlined): over copies of both lists, for
    // every market entry (outer) each pick of the same good (inner) is appended.
    void MatchCargoPicks(std::vector<FBaseCommodity> const& Market,
                         std::vector<FCargoPick> const& Picks, std::vector<FCargoPick>* Out)
    {
        std::vector<FCargoPick> const PicksCopy(Picks);                    // FUN_06f52f60
        std::vector<FBaseCommodity> const MarketCopy(Market);              // FUN_06f55880
        for (std::vector<FBaseCommodity>::const_iterator Entry = MarketCopy.begin();
             Entry != MarketCopy.end(); ++Entry)
        {
            for (std::vector<FCargoPick>::const_iterator Pick = PicksCopy.begin();
                 Pick != PicksCopy.end(); ++Pick)
            {
                if (Entry->GoodId == Pick->GoodId)
                    Out->insert(Out->end(), *Pick);
            }
        }
    }
}

// FUN_06f35bf0 (06f35bf0-06f35da1, cdecl). DAT_06fb95c8 = 0.8 (double). The mass out-param
// of pub::GetGoodProperties is a stack slot the function never reads.
void __cdecl FitCargoToHold(unsigned int ShipArchId, std::vector<FCargoPick>* Cargo)
{
    unsigned int HoldSize = 0;
    pub::GetCargoHoldSize(ShipArchId, HoldSize);
    unsigned int FreeSpace = FtolLow(static_cast<double>(HoldSize) * 0.8);

    // The summed weights (truncated after every float add) and the volume the listed counts
    // already take.
    unsigned int TotalWeight = 0;
    for (std::vector<FCargoPick>::iterator It = Cargo->begin(); It != Cargo->end(); ++It)
    {
        TotalWeight = FtolLow(static_cast<double>(TotalWeight) + It->Weight);
        if (It->Count == 0)
            continue;
        float Volume = 0.0f;
        float Mass;
        pub::GetGoodProperties(It->GoodId, Volume, Mass);
        unsigned int const Used = FtolLow(static_cast<double>(It->Count) * Volume);
        if (Used < FreeSpace)
            FreeSpace -= Used;
        else
            FreeSpace = 0;
    }

    // Each pick's share of the free space by weight becomes its count.
    std::vector<FCargoPick>::iterator It = Cargo->begin();
    while (It != Cargo->end())
    {
        // A weight not greater than 0 (or unordered) keeps the count; so does a zero total.
        if (It->Weight > 0.0f && TotalWeight != 0)
        {
            // The free space multiplies as a signed int (FIMUL).
            float const Share = static_cast<float>(static_cast<double>(It->Weight)
                                                   / static_cast<double>(TotalWeight)
                                                   * static_cast<int>(FreeSpace));
            float Volume = 0.0f;
            float Mass;
            pub::GetGoodProperties(It->GoodId, Volume, Mass);
            if (Volume > 0.0f && Volume <= Share)
                It->Count = FtolLow(static_cast<double>(Share) / Volume);
            else
                It->Count = 0;
        }
        if (It->Count != 0)
            ++It;
        else
            It = Cargo->erase(It);
    }
}

// FUN_06f35db0 (06f35db0-06f36048, cdecl).
void __cdecl FilterCargoPicks(std::vector<unsigned int> const* Goods,
                              std::vector<FBaseCommodity> const* Market,
                              std::vector<FCargoPick>* Out)
{
    for (std::vector<unsigned int>::const_iterator Good = Goods->begin(); Good != Goods->end(); ++Good)
    {
        for (std::vector<FBaseCommodity>::const_iterator Entry = Market->begin();
             Entry != Market->end(); ++Entry)
        {
            if (Entry->GoodId != *Good)
                continue;
            float const Weight = CargoWeightOf(Entry->Count);
            FCargoPick const Pick(&*Entry, &Weight);                       // FUN_06f36cf0
            Out->insert(Out->end(), 1, Pick);
        }
    }
}

// FUN_06f36c60 (06f36c60-06f36cea). DAT_06fb4448 = 1 / 32768 (float). The total is the x87
// sum kept as a float; the roll is rand() * (1/32768) * total, kept as a float; the first
// pick whose running sum reaches the roll wins.
// Deviation: retail returns end() for an empty list, a zero total or a roll past the last
// sum, and every caller reads the good at the result. Callers always pass a non-empty list
// with weights >= 1, so end() is never reached; the port returns the last pick there instead
// of reading past the buffer.
FCargoPick const* __cdecl PickWeightedCargo(std::vector<FCargoPick> const* Picks)
{
    double Sum = 0.0;
    for (std::vector<FCargoPick>::const_iterator It = Picks->begin(); It != Picks->end(); ++It)
        Sum += It->Weight;
    float const Total = static_cast<float>(Sum);
    if (Picks->empty() || Total == 0.0f)
        return &Picks->back();

    float const Roll = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05f * Total);
    double Running = 0.0;
    for (std::vector<FCargoPick>::const_iterator It = Picks->begin(); It != Picks->end(); ++It)
    {
        Running += It->Weight;
        if (Roll <= Running)
            return &*It;
    }
    return &Picks->back();
}

// FUN_06f36050 (06f36050-06f36c5c, cdecl). The retail loop runs once (the counter is
// incremented first and compared with 1). Several PickWeightedCargo / FilterCargoPicks
// calls are inlined in retail.
void __cdecl GenerateCargo(FSystemInfo* Unused, FContentNpcShipParams* Params,
                           std::vector<FCargoPick>* Out)
{
    (void)Unused;
    Out->clear();                                                          // FUN_06f36d30
    // +0xfc taken as an FGuideDesc without a type check, as retail does: this path only runs
    // for encounter ships.
    FGuideDesc* const Desc = static_cast<FGuideDesc*>(Params->GuideDesc.Pointer);  // +0xfc
    unsigned int FromTargetId;                                             // +0x54, unused
    unsigned int ToTargetId;                                               // +0x58, unused
    FContentBase* FromBase = nullptr;                                      // +0x5c
    FContentBase* ToBase = nullptr;                                        // +0x60
    Desc->GetTargets(&FromTargetId, &ToTargetId, &FromBase, &ToBase);      // FUN_06f062d0

    int Attempt = 0;
    do
    {
        ++Attempt;
        unsigned int Faction = Params->ReputationHandle->GetAffiliation();  // FUN_06eb6bb0
        FFactionProps* const Props = GetFactionProps(&Faction);           // FUN_06f50650
        bool bPicked = false;
        unsigned int Good = 0;                                             // -> +0x4c
        unsigned int ReturnGood = 0;                                       // -> +0x50

        if (FromBase != nullptr)
        {
            // The faction's goods FromBase sells.
            std::vector<FBaseCommodity> FromSale;
            FromBase->GetCommoditiesForSale(&FromSale);
            std::vector<FCargoPick> FromSalePicks;
            FilterCargoPicks(&Props->MarketGoods, &FromSale, &FromSalePicks);
            // The goods ToBase asks for (empty without ToBase).
            std::vector<FBaseCommodity> ToDemand;
            if (ToBase != nullptr)
                ToBase->GetCommoditiesInDemand(&ToDemand);
            // Bought here, wanted there.
            std::vector<FCargoPick> Trade;
            MatchCargoPicks(ToDemand, FromSalePicks, &Trade);
            if (!Trade.empty())
            {
                bPicked = true;
                Good = PickWeightedCargo(&Trade)->GoodId;
            }
            else
            {
                // The faction's goods FromBase asks for, wanted at ToBase.
                std::vector<FBaseCommodity> FromDemand;
                FromBase->GetCommoditiesInDemand(&FromDemand);
                std::vector<FCargoPick> FromDemandPicks;
                FilterCargoPicks(&Props->MarketGoods, &FromDemand, &FromDemandPicks);
                std::vector<FCargoPick> Resale;
                MatchCargoPicks(ToDemand, FromDemandPicks, &Resale);
                if (!Resale.empty())
                {
                    bPicked = true;
                    Good = PickWeightedCargo(&Resale)->GoodId;
                }
                else
                {
                    // What ToBase sells -- the return load.
                    std::vector<FBaseCommodity> ToSale;
                    std::vector<FCargoPick> ToSalePicks;
                    bool bReturnPicked = false;
                    if (ToBase != nullptr)
                    {
                        ToBase->GetCommoditiesForSale(&ToSale);
                        FilterCargoPicks(&Props->MarketGoods, &ToSale, &ToSalePicks);
                        if (!ToSalePicks.empty())
                        {
                            bPicked = true;
                            ReturnGood = PickWeightedCargo(&ToSalePicks)->GoodId;
                            bReturnPicked = true;
                        }
                    }
                    // Else anything FromBase sells, else anything it asks for.
                    if (!bReturnPicked)
                    {
                        if (!FromSalePicks.empty())
                        {
                            bPicked = true;
                            Good = PickWeightedCargo(&FromSalePicks)->GoodId;
                        }
                        else if (!FromDemandPicks.empty())
                        {
                            bPicked = true;
                            Good = PickWeightedCargo(&FromDemandPicks)->GoodId;
                        }
                    }
                }
            }
        }
        else if (ToBase != nullptr)
        {
            // Only a destination -- its goods for sale, else its demand.
            std::vector<FBaseCommodity> ToSale;
            ToBase->GetCommoditiesForSale(&ToSale);
            std::vector<FCargoPick> ToSalePicks;
            FilterCargoPicks(&Props->MarketGoods, &ToSale, &ToSalePicks);
            if (!ToSalePicks.empty())
            {
                bPicked = true;
                ReturnGood = PickWeightedCargo(&ToSalePicks)->GoodId;
            }
            else
            {
                std::vector<FBaseCommodity> ToDemand;
                ToBase->GetCommoditiesInDemand(&ToDemand);
                std::vector<FCargoPick> ToDemandPicks;
                FilterCargoPicks(&Props->MarketGoods, &ToDemand, &ToDemandPicks);
                if (!ToDemandPicks.empty())
                {
                    bPicked = true;
                    Good = PickWeightedCargo(&ToDemandPicks)->GoodId;
                }
            }
        }

        if (!bPicked)
        {
            // A random good of the faction; none -> nothing is written.
            if (Props->MarketGoods.empty())
                continue;
            int const Count = static_cast<int>(Props->MarketGoods.size()); // FUN_06fa3db0
            Good = Props->MarketGoods[(rand() * Count) / 0x8000];
        }

        Desc->TradeGood = Good;                                            // +0x4c
        Desc->ReturnGood = ReturnGood;                                     // +0x50
        if (Good == 0)
            continue;

        // One unit of weight 1.0f, fitted to the hold, then appended to the ship's cargo.
        FCargoPick const Pick(Good, 1, 1.0f);
        Out->insert(Out->end(), Pick);                                     // FUN_06f38170
        FitCargoToHold(Params->ShipArchId, Out);
        for (std::vector<FCargoPick>::const_iterator It = Out->begin(); It != Out->end(); ++It)
        {
            pub::SpaceObj::CargoDesc const Entry = {It->GoodId, It->Count};
            Params->Cargo.insert(Params->Cargo.end(), Entry);
        }
    } while (Attempt < 1);
}
