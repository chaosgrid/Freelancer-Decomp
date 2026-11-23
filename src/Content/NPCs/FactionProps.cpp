// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU48 (06f4ed80-06f55e20): faction properties, faction name tables, faction
// goods and reputation empathy. Retail's std::map / std::vector instantiations for the four
// tables are the std containers of FactionProps.h.

#include "NPCs/FactionProps.h"

#include "Core/ContentInternal.h"    // OpenContentDataIni, GContentNextUniqueId
#include "Core/ContentSort.h"        // ContentSort (VC6 std::sort)
#include "Core/ContentStaticIds.h"
#include "NPCs/NpcShips.h"           // ParseSpaceCostume
#include "RandomMissions/RandomMissionGen.h"   // FindVoiceProperties, FVoiceProperties
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"   // pub::SetTimer, pub::Controller::TimerExpired

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

std::vector<unsigned int> GFactionGroups;
FFactionPropsMap GFactionProps;
FFactionNamesMap GFactionNames;
FFactionEmpathyMap GFactionEmpathy;

namespace
{
    // Three timer message ids drawn in this order (_initterm) from the module id generator
    // DAT_06fce76c: DAT_06fd0cd4 (pilot names), DAT_06fd0cd0 (large ship names), DAT_06fd0ccc
    // (formation designations).
    unsigned int const GFactionNameMsgPilot = GContentNextUniqueId++;
    unsigned int const GFactionNameMsgLargeShip = GContentNextUniqueId++;
    unsigned int const GFactionNameMsgFormation = GContentNextUniqueId++;

    // The random draw of TU48 (signed division by 32768).
    int FactionRandIndex(int Count)
    {
        return (rand() * Count) / 32768;
    }

    // FUN_06f53ab0 / the inlined FUN_06f55310 loops: find by the pair only.
    std::vector<FFactionNamePairUse>::iterator FindPairUse(std::vector<FFactionNamePairUse>* Uses,
                                                           int First, int Second)
    {
        std::vector<FFactionNamePairUse>::iterator It = Uses->begin();
        while (It != Uses->end() && !(It->First == First && It->Second == Second))
            ++It;
        return It;
    }

    // FUN_06f52f40 / the inlined name loops: find by the name.
    std::vector<FFactionNameUse>::iterator FindNameUse(std::vector<FFactionNameUse>* Uses, int Name)
    {
        std::vector<FFactionNameUse>::iterator It = Uses->begin();
        while (It != Uses->end() && It->Name != Name)
            ++It;
        return It;
    }

    // FUN_06f52720 / FUN_06f52ff0 / FUN_06f51db0 bodies.
    void AddPairUse(std::vector<FFactionNamePairUse>* Uses, int First, int Second)
    {
        std::vector<FFactionNamePairUse>::iterator It = FindPairUse(Uses, First, Second);
        if (It != Uses->end())
        {
            ++It->UseCount;
            return;
        }
        FFactionNamePairUse Use;
        Use.First = First;
        Use.Second = Second;
        Use.UseCount = 1;
        Uses->insert(Uses->end(), 1, Use);
    }

    // FUN_06f52ec0 / FUN_06ef6bb0 paths of FUN_06f51b80.
    void AddNameUse(std::vector<FFactionNameUse>* Uses, int Name)
    {
        std::vector<FFactionNameUse>::iterator It = FindNameUse(Uses, Name);
        if (It != Uses->end())
        {
            ++It->UseCount;
            return;
        }
        FFactionNameUse Use;
        Use.Name = Name;
        Use.UseCount = 1;
        Uses->insert(Uses->end(), 1, Use);
    }

    // FUN_06f52810 and the immediate paths of FUN_06f51ee0 / FUN_06f520d0.
    bool ReleasePairUse(std::vector<FFactionNamePairUse>* Uses, int First, int Second,
                        int* OutCount)
    {
        std::vector<FFactionNamePairUse>::iterator It = FindPairUse(Uses, First, Second);
        if (It == Uses->end())
        {
            if (OutCount != nullptr)
                *OutCount = 0;
            return false;
        }
        --It->UseCount;
        if (OutCount != nullptr)
            *OutCount = It->UseCount;
        if (It->UseCount == 0)
            Uses->erase(It);
        return true;
    }

    // FUN_06f526a0 and the immediate path of FUN_06f51fe0.
    bool ReleaseNameUse(std::vector<FFactionNameUse>* Uses, int Name, int* OutCount)
    {
        std::vector<FFactionNameUse>::iterator It = FindNameUse(Uses, Name);
        if (It == Uses->end())
        {
            if (OutCount != nullptr)
                *OutCount = 0;
            return false;
        }
        --It->UseCount;
        if (OutCount != nullptr)
            *OutCount = It->UseCount;
        if (It->UseCount == 0)
            Uses->erase(It);
        return true;
    }

    // The predicate of the empathy-rate sort FUN_06f55a00 / FUN_06f55cd0.
    bool EmpathyRateLess(FFactionEmpathyRate const& A, FFactionEmpathyRate const& B)
    {
        return A.Rate < B.Rate;
    }

    // The three release timers (30.0f = DAT_06fbb78c / DAT_06fbb790 / DAT_06fbb794).
    void SetFactionNameTimer(unsigned int MessageId, unsigned int Group)
    {
        pub::Controller::TimerExpired Timer;
        Timer.Param1 = MessageId;
        Timer.Param2 = Group;
        pub::SetTimer(GSpaceSupportControllerId, Timer, 30.0f);
    }

    // LAB_06f50160 (vtable 06fbb7a4); retail's second linear find has no effect.
    struct FFactionGroupCollector : public pub::Reputation::Enumerator
    {
        char Execute(unsigned int GroupId) override
        {
            if (std::find(GFactionGroups.begin(), GFactionGroups.end(), GroupId) ==
                GFactionGroups.end())
            {
                GFactionGroups.insert(GFactionGroups.end(), 1, GroupId);
            }
            return 1;
        }
    };

    // LAB_06f50c40 (vtable 06fbb928).
    struct FFactionGroupCounter : public pub::Reputation::Enumerator
    {
        int Count = 0;              // +0x04

        char Execute(unsigned int GroupId) override
        {
            (void)GroupId;
            ++Count;
            return 1;
        }
    };

    // The reputation clamp of FUN_06f50c50 (x87; DAT_06fbb788 / DAT_06fbb784 are float,
    // DAT_06fbb930 is double 0.1). Retail tests the upper bound twice, once on the unrounded
    // sum and once on the stored float.
    float ClampFactionReputation(double Sum)
    {
        double const Upper = static_cast<double>(1.0f) - 0.1;
        double const Lower = static_cast<double>(-1.0f) + 0.1;
        float const SumF = static_cast<float>(Sum);

        double const Candidate = (Upper < Sum) ? Upper : static_cast<double>(SumF);
        if (Candidate <= Lower)
            return static_cast<float>(Lower);
        if (Upper < static_cast<double>(SumF))
            return static_cast<float>(Upper);
        return SumF;
    }
}

// ---------------------------------------------------------------------------------------
// FFactionProps
// ---------------------------------------------------------------------------------------

// FUN_06f4f060.
FFactionProps::FFactionProps()
    : CommReferrable()
    , JumpPreference(FactionJumpAny)
    , ScanChance(1.0f)
    , bScanAnnounce(true)
    , Legality(FactionLegalityUnset)
    , bSingularNickname(true)
    , McCostume(0)
{
}

// FUN_06f4f3d0.
bool FFactionProps::GetFormation(unsigned int const* Key, unsigned int* Out) const
{
    std::map<unsigned int, unsigned int>::const_iterator It = Formations.find(*Key);
    if (It == Formations.end())
        return false;
    *Out = It->second;
    return true;
}

// FUN_06f4f430.
bool FFactionProps::PickSpaceCostume(int Gender, Costume* Out) const
{
    std::vector<unsigned int> Matches;
    if (!SpaceCostumes.empty())
        Matches.reserve(SpaceCostumes.size());

    // The out-gender slot is the argument's own stack slot in retail.
    for (unsigned int Index = 0; Index < SpaceCostumes.size(); ++Index)
    {
        int SkeletonGender = Gender;
        pub::GF::GetCostumeSkeletonGender(SpaceCostumes[Index], SkeletonGender);
        if (Gender == SkeletonGender)
            Matches.insert(Matches.end(), 1, Index);
    }

    if (Matches.empty())
        return false;

    int const MatchCount = static_cast<int>(Matches.size());
    int const Pick = (rand() * MatchCount) / 32768;
    Costume const& Source = SpaceCostumes[Matches[Pick]];
    Out->PartIds[0] = Source.PartIds[0];
    Out->PartIds[1] = Source.PartIds[1];
    Out->PartIds[2] = Source.PartIds[2];
    Out->PartIds[3] = Source.PartIds[3];
    Out->AccessoryCount = Source.AccessoryCount;
    for (int Accessory = 0; Accessory < static_cast<int>(Out->AccessoryCount); ++Accessory)
        Out->AccessoryIds[Accessory] = Source.AccessoryIds[Accessory];
    return true;
}

// FUN_06f4f620.
bool FFactionProps::PickVoice(unsigned int Roles, unsigned int* Out) const
{
    std::vector<unsigned int> Candidates(Voices.begin(), Voices.end());

    while (!Candidates.empty())
    {
        // One candidate draws index 0 without calling rand().
        int const Count = static_cast<int>(Candidates.size());
        int Pick = 0;
        if (Count != 1)
            Pick = (rand() * Count) / 32768;

        FVoiceProperties* Props = FindVoiceProperties(&Candidates[Pick]);   // FUN_06fa6e50
        if (Props != nullptr && Props->SupportsRoles(Roles))                // FUN_06fa6e30
        {
            *Out = Candidates[Pick];
            return true;
        }
        Candidates.erase(Candidates.begin() + Pick);
    }
    return false;
}

// FUN_06f4f7c0.
bool FFactionProps::PickGender(int* Out) const
{
    unsigned int MaleCount = 0;
    for (std::vector<unsigned int>::const_iterator It = Voices.begin(); It != Voices.end(); ++It)
    {
        FVoiceProperties* Props = FindVoiceProperties(&*It);
        if (Props != nullptr && Props->SupportsRoles(1))
            ++MaleCount;
    }

    if (Voices.empty())
        return false;

    // Unsigned compare.
    int const Count = static_cast<int>(Voices.size());
    unsigned int const Draw = static_cast<unsigned int>((rand() * Count) / 32768);
    *Out = (Draw < MaleCount) ? 1 : 2;
    return true;
}

// ---------------------------------------------------------------------------------------
// FFactionNames
// ---------------------------------------------------------------------------------------

FFactionNameList::FFactionNameList()
    : Count(0)
{
}

void FFactionNameList::AddRange(INI_Reader* Reader)
{
    FFactionNameRange Range;
    Range.First = Reader->get_value_int(0);
    if (Reader->is_value_empty(1))
        Range.Last = Range.First;
    else
        Range.Last = Reader->get_value_int(1);
    Ranges.insert(Ranges.end(), 1, Range);
    Count += Range.Last - Range.First + 1;
}

// FUN_06f4f020 (unsigned compares).
int FFactionNameList::GetName(unsigned int Index) const
{
    std::vector<FFactionNameRange>::const_iterator Range = Ranges.begin();
    unsigned int Size = static_cast<unsigned int>(Range->Last - Range->First + 1);
    while (Size <= Index)
    {
        Index -= Size;
        ++Range;
        Size = static_cast<unsigned int>(Range->Last - Range->First + 1);
    }
    return Range->First + static_cast<int>(Index);
}

// FUN_06f4f050.
bool FFactionNameList::IsEmpty() const
{
    return Count == 0;
}

// FUN_06f50ff0. Group (+0x00) is not written.
FFactionNames::FFactionNames()
    : LargeShipDesig(0)
    , ShortGroupName(0)
{
    RankDesig[0] = 0;
    RankDesig[1] = 0;
    RankDesig[2] = 0;
    for (int Index = 0; Index < 8; ++Index)
        NameIds[Index] = 0;
    RankThreshold[0] = 0;
    RankThreshold[1] = 0;
}

// FUN_06f52230.
bool FFactionNames::ReadIniValue(INI_Reader* Reader)
{
    if (Reader->is_value("affiliation"))
    {
        pub::Reputation::GetReputationGroup(Group, Reader->get_value_string());
        pub::Reputation::GetShortGroupName(Group, ShortGroupName);
        return false;
    }
    if (Reader->is_value("firstname"))
        return false;
    if (Reader->is_value("firstname_male"))
    {
        MaleFirstNames.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("firstname_female"))
    {
        FemaleFirstNames.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("lastname"))
    {
        LastNames.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("formation_desig"))
    {
        FormationDesigs.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("large_ship_names"))
    {
        LargeShipNames.AddRange(Reader);
        return true;
    }
    if (Reader->is_value("large_ship_desig"))
    {
        LargeShipDesig = Reader->get_value_int(0);
        return false;
    }
    if (Reader->is_value("rank_desig"))
    {
        for (int Index = 0; Index < 3; ++Index)
            RankDesig[Index] = static_cast<unsigned int>(Reader->get_value_int(Index));
        RankThreshold[0] = Reader->get_value_int(3);
        RankThreshold[1] = Reader->get_value_int(4);
    }
    return false;
}

// FUN_06f521d0.
int FFactionNames::GetLargeShipDesig() const
{
    return LargeShipDesig;
}

// FUN_06f521e0.
unsigned int FFactionNames::GetShortGroupName() const
{
    return ShortGroupName;
}

// FUN_06f521f0.
unsigned int FFactionNames::GetNameId(int Index) const
{
    return NameIds[Index];
}

// FUN_06f52200 (signed compares).
unsigned int FFactionNames::GetRankDesig(int const* Level) const
{
    if (*Level < RankThreshold[0])
        return RankDesig[0];
    if (*Level < RankThreshold[1])
        return RankDesig[1];
    return RankDesig[2];
}

// FUN_06f51520.
bool FFactionNames::PickPilotName(int* OutFirst, int* OutLast, int Gender, bool bReserve)
{
    FFactionNameList const* FirstNames = (Gender == 1) ? &MaleFirstNames : &FemaleFirstNames;
    int const FirstCount = FirstNames->Count;
    int const LastCount = LastNames.Count;
    int First = 0;
    int Last = 0;
    bool bResult;

    // Unsigned compare of the use count with the pair count.
    if (static_cast<unsigned int>(PilotNameUses.size()) <
        static_cast<unsigned int>(LastCount * FirstCount))
    {
        if (OutFirst != nullptr && OutLast != nullptr)
        {
            // Retail quirk: the inner walk compares the counter before its increment, so a
            // first name whose last names are all taken ends with the counter at
            // LastCount + 1 and is accepted like a free pair.
            int Outer = 0;
            int const FirstStart = FactionRandIndex(FirstCount);
            if (FirstCount > 0)
            {
                for (;;)
                {
                    First = FirstNames->GetName(
                        static_cast<unsigned int>((FirstStart + Outer) % FirstCount));
                    int Inner = 0;
                    int const LastStart = FactionRandIndex(LastCount);
                    for (;;)
                    {
                        Last = LastNames.GetName(
                            static_cast<unsigned int>((LastStart + Inner) % LastCount));
                        if (FindPairUse(&PilotNameUses, First, Last) == PilotNameUses.end())
                            break;
                        int const Previous = Inner;
                        ++Inner;
                        if (!(Previous < LastCount))
                            break;
                    }
                    if (Inner != LastCount)
                        break;
                    ++Outer;
                    if (!(Outer < FirstCount))
                        break;
                }
            }
            bResult = Outer < FirstCount;
        }
        else if (OutFirst == nullptr && OutLast != nullptr)
        {
            // Last name only, key {1, last}.
            First = 1;
            int Inner = 0;
            int const Start = FactionRandIndex(LastCount);
            for (;;)
            {
                Last = LastNames.GetName(static_cast<unsigned int>((Start + Inner) % LastCount));
                if (FindPairUse(&PilotNameUses, 1, Last) == PilotNameUses.end())
                    break;
                int const Previous = Inner;
                ++Inner;
                if (!(Previous < LastCount))
                    break;
            }
            bResult = Inner != LastCount;
        }
        else
        {
            // First name only, key {first, 1}; the draw and the modulus use the last-name
            // count.
            Last = 1;
            int Inner = 0;
            int const Start = FactionRandIndex(LastCount);
            for (;;)
            {
                First = FirstNames->GetName(
                    static_cast<unsigned int>((Start + Inner) % LastCount));
                if (FindPairUse(&PilotNameUses, First, 1) == PilotNameUses.end())
                    break;
                int const Previous = Inner;
                ++Inner;
                if (!(Previous < LastCount))
                    break;
            }
            bResult = Inner != LastCount;
        }
    }
    else
    {
        // Every pair is in use.
        if (OutFirst != nullptr)
            First = FirstNames->GetName(static_cast<unsigned int>(FactionRandIndex(FirstCount)));
        else
            First = 1;
        if (OutLast != nullptr)
            Last = LastNames.GetName(static_cast<unsigned int>(FactionRandIndex(LastCount)));
        else
            First = 1;                                      // retail writes First, not Last
        bResult = false;
    }

    if (bReserve)
        AddPairUse(&PilotNameUses, First, Last);
    if (OutFirst != nullptr)
        *OutFirst = First;
    if (OutLast != nullptr)
        *OutLast = Last;
    return bResult;
}

// FUN_06f51920.
bool FFactionNames::PickFormationDesig(int* OutDesig, int* OutNumber, bool bReserve)
{
    int const Count = FormationDesigs.Count;

    if (static_cast<unsigned int>(FormationDesigUses.size()) < static_cast<unsigned int>(Count * 20))
    {
        // Same walk (and quirk) as the pilot names, 20 numbers per designation.
        int Outer = 0;
        int const Start = FactionRandIndex(Count);
        if (Count > 0)
        {
            for (;;)
            {
                *OutDesig = FormationDesigs.GetName(static_cast<unsigned int>((Start + Outer) % Count));
                int Inner = 0;
                int const NumberStart = (rand() * 20) / 32768;
                for (;;)
                {
                    int const Number = (NumberStart + Inner) % 20;
                    *OutNumber = Number;
                    if (FindPairUse(&FormationDesigUses, *OutDesig, Number) == FormationDesigUses.end())
                        break;
                    int const Previous = Inner;
                    ++Inner;
                    if (!(Previous < 20))
                        break;
                }
                if (Inner != 20)
                    break;
                ++Outer;
                if (!(Outer < Count))
                    break;
            }
        }
        if (bReserve)
            AddPairUse(&FormationDesigUses, *OutDesig, *OutNumber);
        return true;
    }

    *OutDesig = FormationDesigs.GetName(static_cast<unsigned int>(FactionRandIndex(Count)));
    int const Number = (rand() * 20) / 32768;
    *OutNumber = Number;
    if (bReserve)
        AddPairUse(&FormationDesigUses, *OutDesig, Number);
    return false;
}

// FUN_06f51b80.
bool FFactionNames::PickLargeShipName(int* OutName, bool bReserve)
{
    int const Count = LargeShipNames.Count;
    int Name;

    if (static_cast<unsigned int>(LargeShipNameUses.size()) < static_cast<unsigned int>(Count))
    {
        // The first free name from a random start.
        int Draw = FactionRandIndex(Count);
        for (;;)
        {
            Name = LargeShipNames.GetName(static_cast<unsigned int>(Draw % Count));
            ++Draw;
            if (FindNameUse(&LargeShipNameUses, Name) == LargeShipNameUses.end())
                break;
        }
        if (bReserve)
            AddNameUse(&LargeShipNameUses, Name);
        *OutName = Name;
        return true;
    }

    Name = LargeShipNames.GetName(static_cast<unsigned int>(FactionRandIndex(Count)));
    if (bReserve)
        AddNameUse(&LargeShipNameUses, Name);
    *OutName = Name;
    return false;
}

// FUN_06f51db0.
void FFactionNames::AddFormationDesigUse(int Desig, int Number)
{
    AddPairUse(&FormationDesigUses, Desig, Number);
}

// FUN_06f51ee0.
void FFactionNames::ReleasePilotName(int First, int Last, bool bImmediate)
{
    if (bImmediate)
    {
        ReleasePairUse(&PilotNameUses, First, Last, nullptr);
        return;
    }
    FFactionNamePair Pending;
    Pending.First = First;
    Pending.Second = Last;
    PendingPilotNames.push_back(Pending);
    SetFactionNameTimer(GFactionNameMsgPilot, Group);
}

// FUN_06f51fe0.
void FFactionNames::ReleaseLargeShipName(int Name, bool bImmediate)
{
    if (bImmediate)
    {
        ReleaseNameUse(&LargeShipNameUses, Name, nullptr);
        return;
    }
    PendingLargeShipNames.push_back(Name);
    SetFactionNameTimer(GFactionNameMsgLargeShip, Group);
}

// FUN_06f520d0.
void FFactionNames::ReleaseFormationDesig(int Desig, int Number, bool bImmediate)
{
    if (bImmediate)
    {
        ReleasePairUse(&FormationDesigUses, Desig, Number, nullptr);
        return;
    }
    FFactionNamePair Pending;
    Pending.First = Desig;
    Pending.Second = Number;
    PendingFormationDesigs.push_back(Pending);
    SetFactionNameTimer(GFactionNameMsgFormation, Group);
}

// FUN_06f513c0.
void __cdecl HandleFactionNameTimer(pub::Controller::TimerExpired const* Timer)
{
    unsigned int const Group = Timer->Param2;
    if (Timer->Param1 == GFactionNameMsgPilot)
    {
        FFactionNames* Names = GetFactionNames(&Group);
        FFactionNamePair const Front = Names->PendingPilotNames.front();
        Names->ReleasePilotName(Front.First, Front.Second, true);
        Names->PendingPilotNames.pop_front();
    }
    else if (Timer->Param1 == GFactionNameMsgLargeShip)
    {
        FFactionNames* Names = GetFactionNames(&Group);
        int const Front = Names->PendingLargeShipNames.front();
        ReleaseNameUse(&Names->LargeShipNameUses, Front, nullptr);
        Names->PendingLargeShipNames.pop_front();
    }
    else if (Timer->Param1 == GFactionNameMsgFormation)
    {
        FFactionNames* Names = GetFactionNames(&Group);
        FFactionNamePair const Front = Names->PendingFormationDesigs.front();
        ReleasePairUse(&Names->FormationDesigUses, Front.First, Front.Second, nullptr);
        Names->PendingFormationDesigs.pop_front();
    }
}

// ---------------------------------------------------------------------------------------
// Loaders
// ---------------------------------------------------------------------------------------

// FUN_06f4f870.
void __cdecl LoadFactionProps(INI_Reader* Reader)
{
    // Frame locals that outlive a section. Retail never initialises them; the port starts
    // them at 0.
    unsigned int Group = 0;
    unsigned int Good = 0;
    unsigned int Voice = 0;

    while (Reader->read_header())
    {
        if (!Reader->is_header("FactionProps"))
            continue;

        FFactionProps Props;
        FFactionNames Names;

        while (Reader->read_value())
        {
            if (Reader->is_value("affiliation"))
            {
                pub::Reputation::GetReputationGroup(Group, Reader->get_value_string());
                Names.ReadIniValue(Reader);                              // result ignored
            }
            else if (Reader->is_value("scan_announce"))
            {
                Props.bScanAnnounce = Reader->get_value_bool(0);
            }
            else if (Reader->is_value("formation"))
            {
                unsigned int const Key = CreateID(Reader->get_value_string(0));
                unsigned int const Value = CreateID(Reader->get_value_string(1));
                if (Props.Formations.find(Key) == Props.Formations.end())
                    Props.Formations.insert(std::make_pair(Key, Value));
            }
            else if (Reader->is_value("scan_chance"))
            {
                Props.ScanChance = Reader->get_value_float(0);
            }
            else if (Reader->is_value("npc_ship"))
            {
                unsigned int const Arch = CreateID(Reader->get_value_string());
                Props.NpcShips.insert(Props.NpcShips.end(), 1, Arch);
            }
            else if (Reader->is_value("scan_for_cargo"))
            {
                // Appended when not present.
                FFactionScanCargo Cargo;
                Cargo.Archetype = 0;
                Cargo.Level = 0;
                pub::GetGoodID(Good, Reader->get_value_string(0));
                pub::SpaceObj::GetArchetypeID(Good, Cargo.Archetype);
                int const Level = Reader->get_value_int(1);
                if (Level == 0)
                    Cargo.Level = 0;
                else if (Level == 1)
                    Cargo.Level = 1;
                else if (Level == 2)
                    Cargo.Level = 2;

                std::vector<FFactionScanCargo>::iterator It = Props.ScanForCargo.begin();
                while (It != Props.ScanForCargo.end() &&
                       !(It->Archetype == Cargo.Archetype && It->Level == Cargo.Level))
                {
                    ++It;
                }
                if (It == Props.ScanForCargo.end())
                    Props.ScanForCargo.insert(Props.ScanForCargo.end(), Cargo);
            }
            else if (Props.CommReferrable::read_comm_ini_data(*Reader))
            {
                // msg_id_prefix, a direct (non-virtual) import call.
            }
            else if (Reader->is_value("mc_costume"))
            {
                pub::GetCostumeID(Props.McCostume, Reader->get_value_string(0));
            }
            else if (Reader->is_value("space_costume"))
            {
                // The accessory slots are not cleared.
                Costume Appearance;
                Appearance.PartIds[0] = 0;
                Appearance.PartIds[1] = 0;
                Appearance.PartIds[2] = 0;
                Appearance.PartIds[3] = 0;
                Appearance.AccessoryCount = 0;
                ParseSpaceCostume(&Appearance, Reader);
                Props.SpaceCostumes.insert(Props.SpaceCostumes.end(), 1, Appearance);
            }
            else if (Reader->is_value("voice"))
            {
                pub::GetVoiceID(Voice, Reader->get_value_string());
                Props.Voices.insert(Props.Voices.end(), 1, Voice);
            }
            else if (Names.ReadIniValue(Reader))
            {
            }
            else if (Reader->is_value("jump_preference"))
            {
                // Retail compares a TString<0x20> copy case-sensitively; a truncated copy can
                // never equal either word, so comparing the value itself is the same test.
                char const* Value = Reader->get_value_string(0);
                if (Value != nullptr && strcmp(Value, "jumpgate") == 0)
                    Props.JumpPreference = FactionJumpGate;
                else if (Value != nullptr && strcmp(Value, "jumphole") == 0)
                    Props.JumpPreference = FactionJumpHole;
                else
                    Props.JumpPreference = FactionJumpAny;
            }
            else if (Reader->is_value("assassinate_target"))
            {
                unsigned int const Target = CreateID(Reader->get_value_string(0));
                Props.AssassinateTargets.insert(Props.AssassinateTargets.end(), 1, Target);
            }
            else if (Reader->is_value("Legality"))
            {
                if (_stricmp(Reader->get_value_string(0), "lawful") == 0)
                    Props.Legality = FactionLegalityLawful;
                else if (_stricmp(Reader->get_value_string(0), "unlawful") == 0)
                    Props.Legality = FactionLegalityUnlawful;
            }
            else if (Reader->is_value("nickname_plurality"))
            {
                if (_stricmp(Reader->get_value_string(0), "singular") == 0)
                    Props.bSingularNickname = true;
                else if (_stricmp(Reader->get_value_string(0), "plural") == 0)
                    Props.bSingularNickname = false;
            }
        }

        // Legality name ids.
        if (Props.Legality == FactionLegalityLawful || Props.Legality == FactionLegalityUnlawful)
        {
            unsigned int const LegalityId =
                (Props.Legality == FactionLegalityLawful) ? 0x4074u : 0x4075u;
            Names.NameIds[7] = LegalityId;
            Names.NameIds[3] = LegalityId;
            Names.NameIds[2] = 0x4076u;
            Names.NameIds[4] = LegalityId;
        }

        // An existing key keeps its entry.
        GFactionNames.insert(std::make_pair(Group, Names));
        GFactionProps.insert(std::make_pair(Group, Props));
    }

    FFactionGroupCollector Collector;
    pub::Reputation::EnumerateGroups(Collector);
}

// FUN_06f503d0.
void __cdecl LoadFactionGoods(INI_Reader* Reader)
{
    // The good id outlives the section; retail never initialises it.
    unsigned int Good = 0;

    while (Reader->read_header())
    {
        if (!Reader->is_header("FactionGood"))
            continue;

        FFactionProps* Props = nullptr;
        unsigned int Group = 0xFFFFFFFFu;
        while (Reader->read_value())
        {
            if (Reader->is_value("faction"))
            {
                pub::Reputation::GetReputationGroup(Group, Reader->get_value_string(0));
                FFactionPropsMap::iterator It = GFactionProps.find(Group);
                if (It != GFactionProps.end())
                    Props = &It->second;
                else
                    Props = nullptr;
            }
            else if (Reader->is_value("MarketGood"))
            {
                // No null check on the props (retail faults without a faction line).
                pub::GetGoodID(Good, Reader->get_value_string(0));
                Props->MarketGoods.insert(Props->MarketGoods.end(), 1, Good);
            }
        }
    }
}

// FUN_06f50880.
void __cdecl LoadFactionEmpathy(char const* RelativePath)
{
    INI_Reader Reader;
    if (!OpenContentDataIni(RelativePath, &Reader))
        return;

    // The group count reserves every section's rate list.
    FFactionGroupCounter Counter;
    pub::Reputation::EnumerateGroups(Counter);

    // Frame locals of the empathy_rate line; retail never initialises them.
    FFactionEmpathyRate Rate;
    Rate.Group = 0;
    Rate.Rate = 0.0f;

    while (Reader.read_header())
    {
        if (!Reader.is_header("RepChangeEffects"))
            continue;

        FFactionEmpathy Empathy;
        Empathy.EventRates[0] = 0.0f;
        Empathy.EventRates[1] = 0.0f;
        Empathy.EventRates[2] = 0.0f;
        Empathy.EventRates[3] = 0.0f;
        Empathy.EmpathyRates.reserve(static_cast<unsigned int>(Counter.Count));
        unsigned int Group = 0xFFFFFFFFu;

        while (Reader.read_value())
        {
            if (Reader.is_value("empathy_rate"))
            {
                pub::Reputation::GetReputationGroup(Rate.Group, Reader.get_value_string(0));
                Rate.Rate = Reader.get_value_float(1);
                Empathy.EmpathyRates.insert(Empathy.EmpathyRates.end(), 1, Rate);
            }
            else if (Reader.is_value("group"))
            {
                pub::Reputation::GetReputationGroup(Group, Reader.get_value_string());
            }
            else if (Reader.is_value("event"))
            {
                char const* Event = Reader.get_value_string(0);
                int Index;
                if (_stricmp(Event, "object_destruction") == 0)
                    Index = FactionEventObjectDestruction;
                else if (_stricmp(Event, "random_mission_success") == 0)
                    Index = FactionEventRandomMissionSuccess;
                else if (_stricmp(Event, "random_mission_failure") == 0)
                    Index = FactionEventRandomMissionFailure;
                else if (_stricmp(Event, "random_mission_abortion") == 0)
                    Index = FactionEventRandomMissionAbortion;
                else
                    continue;
                Empathy.EventRates[Index] = Reader.get_value_float(1);
            }
        }

        // FUN_06f55a00. Retail's sort algorithm is kept because it fixes the order of equal
        // rates, which GetFactionHostileGroups exposes.
        if (!Empathy.EmpathyRates.empty())
        {
            FFactionEmpathyRate* First = &Empathy.EmpathyRates[0];
            ContentSort(First, First + Empathy.EmpathyRates.size(), &EmpathyRateLess);
        }

        GFactionEmpathy.insert(std::make_pair(Group, Empathy));
    }
}

// FUN_06f506a0.
void __cdecl UnloadFactionProps()
{
    GFactionProps.clear();
}

// FUN_06f50ed0.
void __cdecl UnloadFactionEmpathy()
{
    GFactionEmpathy.clear();
}

// ---------------------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------------------

// FUN_06f50650.
FFactionProps* __cdecl GetFactionProps(unsigned int const* Group)
{
    FFactionPropsMap::iterator It = GFactionProps.find(*Group);
    if (It == GFactionProps.end())
        return nullptr;
    return &It->second;
}

// FUN_06f51370.
FFactionNames* __cdecl GetFactionNames(unsigned int const* Group)
{
    FFactionNamesMap::iterator It = GFactionNames.find(*Group);
    if (It == GFactionNames.end())
        return nullptr;
    return &It->second;
}

// FUN_06f50760.
std::vector<unsigned int>* __cdecl GetFactionGroups()
{
    return &GFactionGroups;
}

// ---------------------------------------------------------------------------------------
// Reputation empathy
// ---------------------------------------------------------------------------------------

// FUN_06f50c50.
void __cdecl ApplyFactionReputationChange(int VibeHandle, float Delta, unsigned int Group,
                                          bool bEmpathy)
{
    // x87: unordered does not return.
    if (Delta == 0.0f)
        return;
    if (Group == 0xFFFFFFFFu)
        return;

    // The out value is zeroed once; later reads reuse it.
    float Reputation = 0.0f;
    pub::Reputation::GetReputation(VibeHandle, Group, Reputation);
    float Clamped = ClampFactionReputation(static_cast<double>(Reputation) +
                                           static_cast<double>(Delta));
    pub::Reputation::SetReputation(VibeHandle, Group, Clamped);

    if (!bEmpathy)
        return;

    FFactionEmpathyMap::iterator Entry = GFactionEmpathy.find(Group);
    if (Entry == GFactionEmpathy.end())
        return;
    for (std::vector<FFactionEmpathyRate>::iterator It = Entry->second.EmpathyRates.begin();
         It != Entry->second.EmpathyRates.end(); ++It)
    {
        pub::Reputation::GetReputation(VibeHandle, It->Group, Reputation);
        double const Sum = static_cast<double>(Delta) * static_cast<double>(It->Rate) +
                           static_cast<double>(Reputation);
        Clamped = ClampFactionReputation(Sum);
        pub::Reputation::SetReputation(VibeHandle, It->Group, Clamped);
    }
}

// FUN_06f50e10.
void __cdecl GetFactionHostileGroups(unsigned int Group, unsigned int* Out, unsigned int Count)
{
    FFactionEmpathyMap::iterator Entry = GFactionEmpathy.find(Group);
    if (Entry == GFactionEmpathy.end())
        return;

    // The list position only moves past a negative rate, so the first rate at or above zero
    // ends the run.
    std::vector<FFactionEmpathyRate>::iterator It = Entry->second.EmpathyRates.begin();
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        if (It != Entry->second.EmpathyRates.end() && It->Rate < 0.0f)
        {
            Out[Index] = It->Group;
            ++It;
        }
        else
        {
            Out[Index] = 0xFFFFFFFFu;
        }
    }
}

// FUN_06f50f90.
float __cdecl GetFactionEventRate(unsigned int Group, int Event)
{
    FFactionEmpathyMap::iterator Entry = GFactionEmpathy.find(Group);
    if (Entry == GFactionEmpathy.end())
        return 0.0f;
    return Entry->second.EventRates[Event];
}
