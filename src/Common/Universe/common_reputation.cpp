// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_reputation.h"
#include "Universe/common_reputation_internal.h"

#include "common_ini.h"
#include "common_misc.h"
#include "common_net.h"
#include "server_savefile.h"  // ISave (declared once, Server implements it)
#include "dacom.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

using CmnReputation::EnsureStoryFactionIds;
using CmnReputation::FGroup;
using CmnReputation::FindGroupConst;
using CmnReputation::GStoryFactionIds;

namespace
{
    const char kReputationSource[] = "E:\\FL\\Scratch\\Source\\Common\\Reputation.cpp";
    const unsigned kMissingGroup = 0xFFFFFFFFu;

    char const* const kStoryFactionNames[] = {
        "fc_ln_grp",
        "fc_kn_grp",
        "fc_rn_grp",
        "fc_n_grp",
        "fc_or_grp",
        "fc_f_grp",
        "fc_q_grp",
        "fc_ouk_grp",
        "fc_uk_grp",
        nullptr,
    };

    std::map<unsigned, FGroup> GGroups;
}

namespace CmnReputation
{
    std::vector<unsigned> GStoryFactionIds;

    FGroup const* FindGroupConst(unsigned GroupId)
    {
        std::map<unsigned, FGroup>::const_iterator It = GGroups.find(GroupId);
        if (It == GGroups.end())
            return nullptr;
        return &It->second;
    }

    void EnsureStoryFactionIds()
    {
        if (!GStoryFactionIds.empty())
            return;

        unsigned Index = 0;
        while (kStoryFactionNames[Index] != nullptr)
        {
            unsigned short Id = MakeId(kStoryFactionNames[Index]);
            GStoryFactionIds.push_back(Id);
            Index += 1;
        }
    }
}

namespace
{
    void ClearTString(TString<16>& Dest)
    {
        Dest.Length = 0;
        Dest.Data[0] = '\0';
        unsigned Index = 1;
        for (; Index < 16; ++Index)
            Dest.Data[Index] = '\0';
    }

    void CopyTString(TString<16>& Dest, char const* Src)
    {
        ClearTString(Dest);
        if (Src == nullptr)
            return;

        unsigned Len = 0;
        while (Src[Len] != '\0' && Len < 15)
        {
            Dest.Data[Len] = Src[Len];
            Len += 1;
        }
        Dest.Data[Len] = '\0';
        Dest.Length = Len;
    }

    FGroup* FindGroup(unsigned GroupId)
    {
        std::map<unsigned, FGroup>::iterator It = GGroups.find(GroupId);
        if (It == GGroups.end())
            return nullptr;
        return &It->second;
    }


    void ReadRepValue(FGroup& Group, INI_Reader& Reader)
    {
        // 0631f0a0: INSERTED (FUN_063222f0, unique insert): a group already in the map keeps
        // its first value.
        TString<16> OtherName;
        CopyTString(OtherName, Reader.get_value_string(1));
        unsigned OtherId = (unsigned)MakeId(OtherName.Data);
        float Feeling = Reader.get_value_float(0);
        Group.Feelings.insert(std::pair<unsigned const, float>(OtherId, Feeling));
    }

    void ReadGroupBlock(INI_Reader& Reader)
    {
        TString<16> Name;
        ClearTString(Name);
        Reader.read_value();
        CopyTString(Name, Reader.get_value_string());
        unsigned Id = MakeId(Name.Data);

        // Unique insert of an empty record (FUN_063225b0); an existing group keeps its feelings.
        // Retail's stack record leaves the three Ids uninitialised, but all are overwritten
        // below on every path.
        FGroup& Group = GGroups[Id];
        Group.Nickname = Name;

        Reader.read_value();
        Group.IdsName = (int)Reader.get_value_float(0);

        Reader.read_value();
        if (Reader.is_value("ids_info"))
            Group.IdsInfo = (int)Reader.get_value_float(0);
        else
            Group.IdsInfo = 0;

        Reader.read_value();
        if (Reader.is_value("ids_short_name"))
            Group.IdsShortName = (int)Reader.get_value_float(0);
        else
            Group.IdsShortName = 0;

        // Startup reads no `rep` values: feelings come from Reputation::LoadFeelings (the
        // server's load of the initial world / save game) and Reputation::set.
    }
}

bool __cdecl Reputation::Startup(char const* Path)
{
    // .text:0631e9a0. No null check of Path.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return false;

    while (Reader.read_header())
    {
        if (Reader.is_header("Group"))
            ReadGroupBlock(Reader);
    }
    return true;
}

void __cdecl Reputation::Shutdown()
{
    // .text:0631e8e0: the group registry DAT_064018ec only; the story-faction id list
    // DAT_064018dc is left as it is.
    GGroups.clear();
}

void __cdecl Reputation::Save(ISave* Writer)
{
    // 0631eef0. No null check on Writer.
    std::map<unsigned, FGroup>::const_iterator It = GGroups.begin();
    for (; It != GGroups.end(); ++It)
    {
        FGroup const& Group = It->second;
        Writer->WriteSection("Group");
        Writer->WriteString("nickname", Group.Nickname.Data);

        std::map<unsigned, float>::const_iterator FeelIt = Group.Feelings.begin();
        for (; FeelIt != Group.Feelings.end(); ++FeelIt)
        {
            // Retail UB: for a stale relation (the id no longer names a group) retail prints the
            // map head node's uninitialised value area. The port prints an empty string there
            // and still writes the line.
            FGroup const* Other = FindGroupConst(FeelIt->first);
            char const* OtherNick = (Other != nullptr) ? Other->Nickname.Data : "";
            char Buffer[260];
            _snprintf_s(Buffer, sizeof(Buffer), _TRUNCATE, "%g, %s",
                        (double)FeelIt->second, OtherNick);
            Writer->WriteString("rep", Buffer);
        }
    }
}

void __cdecl Reputation::LoadFeelings(INI_Reader& Reader)
{
    while (Reader.read_header())
    {
        if (!Reader.is_header("Group"))
            continue;

        TString<16> Name;
        ClearTString(Name);
        Reader.read_value();
        CopyTString(Name, Reader.get_value_string());
        unsigned Id = MakeId(Name.Data);
        // Retail UB: find() is not tested for end(), so an unknown group's `rep` lines go into
        // the head node's uninitialised map. The port skips the lines instead.
        FGroup* Group = FindGroup(Id);
        unsigned RepCount = 0;
        while (Reader.read_value())
        {
            if (!Reader.is_value("rep"))
                continue;
            RepCount += 1;
            if (Group != nullptr)
                ReadRepValue(*Group, Reader);
        }

        if (RepCount == 0)
        {
            // The name printed is the found record's nickname, which differs from Name when two
            // nicknames share a 16-bit MakeId. Name.Data only stands in for the UB case above.
            char const* GroupNick = (Group != nullptr) ? Group->Nickname.Data : Name.Data;
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: FACTION: save game has corrupted reputation data for group "
                  "%s, please re-create the savegame",
                  kReputationSource, 0x10e, GroupNick);
        }
    }
}

void __cdecl Reputation::FreeFeelings()
{
    std::map<unsigned, FGroup>::iterator It = GGroups.begin();
    for (; It != GGroups.end(); ++It)
        It->second.Feelings.clear();
}

unsigned __cdecl Reputation::get_id(TString<16> const& Name)
{
    if (Name.Length == 0)
        return kMissingGroup;
    unsigned Id = MakeId(Name.Data);
    if (FindGroupConst(Id) == nullptr)
        return kMissingGroup;
    return Id;
}

int __cdecl Reputation::get_nickname(TString<16>& Out, unsigned const& GroupId)
{
    FGroup const* Group = FindGroupConst(GroupId);
    if (Group == nullptr)
        return -1;
    // Out's bytes past the terminator are left as they were.
    char const* Src = Group->Nickname.Data;
    unsigned Len = (unsigned)strlen(Src);
    if (Len >= 16)
        Len = 15;
    memcpy(Out.Data, Src, Len);
    Out.Data[Len] = '\0';
    Out.Length = Len;
    return 0;
}

unsigned __cdecl Reputation::get_name(unsigned GroupId)
{
    FGroup const* Group = FindGroupConst(GroupId);
    if (Group == nullptr)
        return 0;
    return (unsigned)Group->IdsName;
}

unsigned __cdecl Reputation::get_short_name(unsigned GroupId)
{
    FGroup const* Group = FindGroupConst(GroupId);
    if (Group == nullptr)
        return 0;
    return (unsigned)Group->IdsShortName;
}

unsigned __cdecl Reputation::get_info_card(unsigned GroupId)
{
    FGroup const* Group = FindGroupConst(GroupId);
    if (Group == nullptr)
        return 0;
    return (unsigned)Group->IdsInfo;
}

unsigned __cdecl Reputation::group_count()
{
    return (unsigned)GGroups.size();
}

void __cdecl Reputation::enumerate(RepGroupCB* Callback)
{
    // 0631f600. No null test of Callback.
    std::map<unsigned, FGroup>::const_iterator It = GGroups.begin();
    for (; It != GGroups.end(); ++It)
    {
        unsigned const& Id = It->first;
        if (Callback->Execute(&Id) == 0)
            return;
    }
}

int __cdecl Reputation::get_feelings_towards(unsigned FromId, unsigned ToId, float& Out)
{
    if (FromId == kMissingGroup || ToId == kMissingGroup)
    {
        Out = 0.0f;
        return 0;
    }

    FGroup const* From = FindGroupConst(FromId);
    if (From == nullptr)
        return -1;

    FGroup const* To = FindGroupConst(ToId);
    if (To == nullptr)
        return -2;

    Out = 0.0f;
    if (FromId == ToId)
    {
        Out = 1.0f;
        return 0;
    }

    std::map<unsigned, float>::const_iterator It = From->Feelings.find(ToId);
    if (It != From->Feelings.end())
        Out = It->second;
    return 0;
}

int __cdecl Reputation::affect_relation(unsigned const& FromId, unsigned const& ToId, float Delta)
{
    FGroup* From = FindGroup(FromId);
    if (From == nullptr)
        return -1;

    // 0631f7a0. Retail checks for an existing Feelings entry FIRST and validates ToId only when
    // there is none: a relation to a group that has since disappeared is still updated in
    // place, not rejected with -2.
    std::map<unsigned, float>::iterator It = From->Feelings.find(ToId);
    if (It != From->Feelings.end())
    {
        // The sum is clamped unrounded (x87); a NaN becomes 1.0.
        double Value = (double)Delta + (double)It->second;
        if (!(Value < 1.0))
            Value = 1.0;
        if (-1.0 > Value)
            Value = -1.0;
        It->second = (float)Value;
        return 0;
    }

    if (FindGroupConst(ToId) == nullptr)
        return -2;

    // The same clamp (NaN -> 1.0), then a unique insert (FUN_063222f0).
    float Value = Delta;
    if (!(Value < 1.0f))
        Value = 1.0f;
    if (-1.0f > Value)
        Value = -1.0f;
    From->Feelings.insert(std::pair<unsigned const, float>(ToId, Value));
    return 0;
}

bool __cdecl Reputation::IsStoryFaction(unsigned GroupId)
{
    EnsureStoryFactionIds();
    unsigned Index = 0;
    for (; Index < GStoryFactionIds.size(); ++Index)
    {
        if (GStoryFactionIds[Index] == GroupId)
            return true;
    }
    return false;
}

void __cdecl Reputation::set(TString<16> const& Name, Relation* const Rels, unsigned Count)
{
    // .text:0631f260.
    unsigned const Id = MakeId(Name.Data);
    std::map<unsigned, FGroup>::iterator It = GGroups.find(Id);
    if (It != GGroups.end())
    {
        It->second.Feelings.clear();
    }
    else
    {
        // The new group has an EMPTY nickname, not Name. Retail UB: IdsName/IdsInfo/
        // IdsShortName (+0x14/+0x18/+0x1c) carry stack garbage into the registry; the port
        // value-initialises them to 0.
        FGroup Record = FGroup();
        Record.Nickname.Length = 0;
        Record.Nickname.Data[0] = '\0';
        It = GGroups.insert(std::pair<unsigned const, FGroup>(Id, Record)).first;
    }

    // Unique inserts: for a repeated GroupId in Rels the first value wins.
    // Retail UB: the loop at 0631f3b0 has no exit -- after the Count == 0 test it jumps back
    // unconditionally, reading past Rels until the read faults. The port stops after Count
    // entries.
    if (Count == 0)
        return;
    std::map<unsigned, float>& Feelings = It->second.Feelings;
    unsigned Index = 0;
    for (; Index < Count; ++Index)
        Feelings.insert(std::pair<unsigned const, float>(Rels[Index].GroupId, Rels[Index].Feeling));
}
