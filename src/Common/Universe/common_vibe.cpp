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
    // Reputation::Vibe keeps its records in two stores (see the note on Reputation::Vibe in
    // common_reputation.h).
    //
    // The static store: retail's std::vector at DAT_064018b4 (ctor LAB_06320210, atexit
    // LAB_06320240), 0x90-byte elements. Filled while the universe loads (BeginStaticLoad ..
    // EndStaticLoad), survives FreeDynamic, emptied by FreeStatic.
    struct FVibeSlot
    {
        int Key;
        Reputation::Vibe::VibeValue Value;
    };
    std::vector<FVibeSlot> GStaticVibes;
    // DAT_06401934: set by BeginStaticLoad, cleared by EndStaticLoad. While set, Alloc appends
    // to the static store and FindStaticVibe scans it linearly; afterwards it is sorted and
    // binary-searched.
    bool GbStaticVibeLoading = false;
    // DAT_06401938: FindStaticVibe's one-entry cache (the last slot it returned, or null).
    FVibeSlot* GStaticVibeHit = nullptr;

    // The dynamic store: retail's std::map<int, VibeValue> DAT_064018c4 (signed key compare;
    // ctor FUN_06320080, atexit FUN_06320120). Alloc inserts here once the universe has loaded;
    // FreeDynamic empties it.
    typedef std::map<int, Reputation::Vibe::VibeValue> FDynamicVibeMap;
    FDynamicVibeMap GDynamicVibes;
    // DAT_064018d8: the last node a lookup landed on, end() when it missed.
    FDynamicVibeMap::iterator GDynamicVibeHit = GDynamicVibes.end();

    // DAT_06401900 (ctor .text:0631e6c0, atexit .text:0631e6f0): client id -> previous
    // affiliation. Written by SetAffiliation and the auto-affiliation pass; cleared by the
    // consumers of GetChangedAffiliationClientMap.
    std::map<unsigned int, unsigned int> GChangedAffiliationClientMap;

    // DAT_063ed5d0 / DAT_063ed5d4 (.data, never written).
    float const kAffiliationKeepFeeling = 0.65f;
    float const kAffiliationJoinFeeling = 0.9f;

    // Retail quirk kept: static-store keys are compared by subtraction (the sort FUN_063241c0
    // and the bsearch comparator FUN_063782e0). The 32-bit difference wraps for keys more than
    // 2^31 apart (nickname hashes, so such pairs exist), which makes the order non-transitive:
    // the sort result, and which keys bsearch then finds, depend on the exact algorithms, so
    // both are reproduced below.
    int StaticVibeKeyDiff(int A, int B)
    {
        return (int)((unsigned)A - (unsigned)B);
    }

    bool StaticVibeSlotBefore(FVibeSlot const& A, FVibeSlot const& B)
    {
        return StaticVibeKeyDiff(A.Key, B.Key) < 0;
    }

    // FUN_063782e0, handed to MSVCRT bsearch: the search key against a slot's key.
    int __cdecl StaticVibeKeyCompare(void const* KeyPtr, void const* SlotPtr)
    {
        return StaticVibeKeyDiff(*static_cast<int const*>(KeyPtr), static_cast<FVibeSlot const*>(SlotPtr)->Key);
    }

    // VC6 <algorithm> std::sort as FUN_063241c0 instantiates it for the static store: _Sort
    // FUN_06324640, _Median FUN_06324b00, _Insertion_sort FUN_06324920, _Unguarded_insert
    // FUN_06324830, iter_swap FUN_06324d10, copy_backward FUN_06323e90; slot copy ctor
    // FUN_06324570.
    int const kVc6SortMax = 16;

    // FUN_06324550: the slot assignment every step of the sort uses. It prepends the source's
    // deltas to the destination's list without freeing it (CopyFieldsFrom); it is not
    // VibeValue::operator=, which frees the destination's list first.
    void Vc6AssignSlot(FVibeSlot& Dst, FVibeSlot const& Src)
    {
        Dst.Key = Src.Key;
        Dst.Value.CopyFieldsFrom(Src.Value);
    }

    // FUN_06324d10
    void Vc6IterSwap(FVibeSlot* X, FVibeSlot* Y)
    {
        FVibeSlot const Tmp = *X;
        Vc6AssignSlot(*X, *Y);
        Vc6AssignSlot(*Y, Tmp);
    }

    // FUN_06323e90
    FVibeSlot* Vc6CopyBackward(FVibeSlot* First, FVibeSlot* Last, FVibeSlot* DestLast)
    {
        while (First != Last)
            Vc6AssignSlot(*--DestLast, *--Last);
        return DestLast;
    }

    void Vc6UnguardedInsert(FVibeSlot* Last, FVibeSlot Value)
    {
        FVibeSlot* Mid = Last;
        while (StaticVibeSlotBefore(Value, *--Mid))
        {
            Vc6AssignSlot(*Last, *Mid);
            Last = Mid;
        }
        Vc6AssignSlot(*Last, Value);
    }

    void Vc6InsertionSort(FVibeSlot* First, FVibeSlot* Last)
    {
        if (First == Last)
            return;
        for (FVibeSlot* Mid = First; ++Mid != Last;)
        {
            FVibeSlot Value = *Mid;
            if (!StaticVibeSlotBefore(Value, *First))
            {
                Vc6UnguardedInsert(Mid, Value);
            }
            else
            {
                Vc6CopyBackward(First, Mid, Mid + 1);
                Vc6AssignSlot(*First, Value);
            }
        }
    }

    FVibeSlot Vc6Median(FVibeSlot const& X, FVibeSlot const& Y, FVibeSlot const& Z)
    {
        if (StaticVibeSlotBefore(X, Y))
            return StaticVibeSlotBefore(Y, Z) ? Y : (StaticVibeSlotBefore(X, Z) ? Z : X);
        return StaticVibeSlotBefore(X, Z) ? X : (StaticVibeSlotBefore(Y, Z) ? Z : Y);
    }

    FVibeSlot* Vc6UnguardedPartition(FVibeSlot* First, FVibeSlot* Last, FVibeSlot const& Pivot)
    {
        for (;; ++First)
        {
            while (StaticVibeSlotBefore(*First, Pivot))
                ++First;
            while (StaticVibeSlotBefore(Pivot, *--Last))
            {
            }
            if (Last <= First)
                return First;
            Vc6IterSwap(First, Last);
        }
    }

    void Vc6Sort(FVibeSlot* First, FVibeSlot* Last)
    {
        while (kVc6SortMax < Last - First)
        {
            FVibeSlot* Mid = nullptr;
            {
                // The pivot temporary dies before the recursion.
                FVibeSlot const Pivot =
                    Vc6Median(*First, *(First + (Last - First) / 2), *(Last - 1));
                Mid = Vc6UnguardedPartition(First, Last, Pivot);
            }
            if (Last - Mid <= Mid - First)
            {
                Vc6Sort(Mid, Last);
                Last = Mid;
            }
            else
            {
                Vc6Sort(First, Mid);
                First = Mid;
            }
        }
    }

    void Vc6SortStaticVibes(FVibeSlot* First, FVibeSlot* Last)
    {
        if (Last - First <= kVc6SortMax)
        {
            Vc6InsertionSort(First, Last);
            return;
        }
        Vc6Sort(First, Last);
        Vc6InsertionSort(First, First + kVc6SortMax);
        for (First += kVc6SortMax; First != Last; ++First)
            Vc6UnguardedInsert(First, *First);
    }

    // FUN_06320290: the static-store lookup. Retail's bsearch key is a lazily constructed static
    // slot (DAT_06401820) of which only the key is read; the port passes the key alone. A miss
    // also becomes the (null) cache.
    Reputation::Vibe::VibeValue* FindStaticVibe(int Key)
    {
        if (GStaticVibeHit != nullptr && GStaticVibeHit->Key == Key)
            return &GStaticVibeHit->Value;

        FVibeSlot* Found = nullptr;
        if (GbStaticVibeLoading)
        {
            std::size_t Index = 0;
            for (; Index < GStaticVibes.size(); ++Index)
            {
                if (GStaticVibes[Index].Key == Key)
                {
                    Found = &GStaticVibes[Index];
                    break;
                }
            }
        }
        else
        {
            int SearchKey = Key;
            Found = (FVibeSlot*)bsearch(&SearchKey, GStaticVibes.data(), GStaticVibes.size(),
                                        sizeof(FVibeSlot), StaticVibeKeyCompare);
        }

        GStaticVibeHit = Found;
        if (Found == nullptr)
            return nullptr;
        return &Found->Value;
    }

    // The dynamic-store lookup every accessor inlines (or reaches through FUN_063227c0,
    // FUN_06321eb0, FUN_0627f670). end() is cached too.
    Reputation::Vibe::VibeValue* FindDynamicVibe(int Key)
    {
        if (GDynamicVibeHit == GDynamicVibes.end() || GDynamicVibeHit->first != Key)
            GDynamicVibeHit = GDynamicVibes.find(Key);
        if (GDynamicVibeHit == GDynamicVibes.end())
            return nullptr;
        return &GDynamicVibeHit->second;
    }

    // Every accessor except SetClientID / GetClientID: the dynamic store first, then the
    // static one.
    Reputation::Vibe::VibeValue* FindVibe(int Key)
    {
        Reputation::Vibe::VibeValue* Value = FindDynamicVibe(Key);
        if (Value != nullptr)
            return Value;
        return FindStaticVibe(Key);
    }

    // The delta-list purge FreeDynamic and Free inline: every AttitudeDelta whose GroupId equals
    // Key is unlinked and freed.
    void PurgeVibeDeltas(Reputation::Vibe::VibeValue& Value, int Key)
    {
        Reputation::Vibe::AttitudeDelta** Link = &Value.Deltas;
        while (*Link != nullptr)
        {
            Reputation::Vibe::AttitudeDelta* Node = *Link;
            if (Node->GroupId == (unsigned)Key)
            {
                *Link = Node->Next;
                delete Node;
            }
            else
            {
                Link = &Node->Next;
            }
        }
    }

    // FUN_0631fdd0 (thiscall on the entry): the player-only auto-affiliation pass that ends
    // Vibe::Set and Vibe::SetGroupFeelingsTowards. A player who still likes the current
    // affiliation more than 0.65 keeps it; otherwise the new one is the group with the highest
    // feeling strictly above 0.9 that is not a story faction, or none. The changed-affiliation
    // map is written only outside SinglePlayer.
    void UpdatePlayerAffiliation(Reputation::Vibe::VibeValue& V)
    {
        if (V.ClientID == 0)
            return;

        float BestFeeling = kAffiliationJoinFeeling;
        unsigned NewAffiliation = kNoAffiliation;
        std::vector<Reputation::Vibe::GroupFeeling>::const_iterator It = V.GroupFeelings.begin();
        for (; It != V.GroupFeelings.end(); ++It)
        {
            if (It->GroupId == V.Affiliation && It->Feeling > kAffiliationKeepFeeling)
                return;
            if (!(It->Feeling > BestFeeling))
                continue;

            EnsureStoryFactionIds();
            bool bStoryFaction = false;
            std::size_t Index = 0;
            for (; Index < GStoryFactionIds.size(); ++Index)
            {
                if (GStoryFactionIds[Index] == It->GroupId)
                {
                    bStoryFaction = true;
                    break;
                }
            }
            if (bStoryFaction)
                continue;

            NewAffiliation = It->GroupId;
            BestFeeling = It->Feeling;
        }

        unsigned const OldAffiliation = V.Affiliation;
        if (NewAffiliation == OldAffiliation)
            return;
        V.Affiliation = NewAffiliation;
        if (SinglePlayer())
        {
            V.AffiliationGroup = nullptr;
            return;
        }
        GChangedAffiliationClientMap[V.ClientID] = OldAffiliation;
        V.AffiliationGroup = nullptr;
    }
}

std::map<unsigned int, unsigned int>* __cdecl Reputation::GetChangedAffiliationClientMap()
{
    // .text:0631e8d0: null when the map is empty.
    if (GChangedAffiliationClientMap.size() != 0)
        return &GChangedAffiliationClientMap;
    return nullptr;
}

Reputation::Vibe::VibeValue::VibeValue()
    : Affiliation(kNoAffiliation)
    , AttitudeTowardsPlayer(0.0f)
    , ClientID(0)
    , AffiliationGroup(nullptr)
    , Infocard(0)
    , Rank(0)
    , Reserved78(0)
    , Deltas(nullptr)
{
    // FUN_0631f9a0, the default record Alloc builds on its stack.
    DisplayName[0] = L'\0';
}

void Reputation::Vibe::VibeValue::CopyFieldsFrom(VibeValue const& Other)
{
    // FUN_0631fad0. Retail quirk kept: each source delta (+0x88 list) is PREPENDED to this
    // entry's EXISTING list, which is never freed here (so the copy is reversed).
    Affiliation = Other.Affiliation;
    AffiliationGroup = Other.AffiliationGroup;
    ClientID = Other.ClientID;

    for (AttitudeDelta const* Src = Other.Deltas; Src != nullptr; Src = Src->Next)
    {
        AttitudeDelta* Node = new AttitudeDelta();
        Node->GroupId = Src->GroupId;
        Node->Delta = Src->Delta;
        Node->Next = Deltas;
        Deltas = Node;
    }

    Rank = Other.Rank;
    if (&GroupFeelings != &Other.GroupFeelings)
        GroupFeelings = Other.GroupFeelings;
    Name1 = Other.Name1;
    Name2 = Other.Name2;
    AttitudeTowardsPlayer = Other.AttitudeTowardsPlayer;
    Infocard = Other.Infocard;
    wcsncpy(DisplayName, Other.DisplayName, 0x17);
    DisplayName[0x17] = L'\0';
}

Reputation::Vibe::VibeValue::VibeValue(VibeValue const& Other)
    : Affiliation(kNoAffiliation)
    , AttitudeTowardsPlayer(Other.AttitudeTowardsPlayer)
    , ClientID(0)
    , AffiliationGroup(nullptr)
    , Infocard(0)
    , Rank(0)
    , Reserved78(0)
    , Deltas(nullptr)
{
    DisplayName[0] = L'\0';
    CopyFieldsFrom(Other);
}

Reputation::Vibe::VibeValue& Reputation::Vibe::VibeValue::operator=(VibeValue const& Other)
{
    if (this == &Other)
        return *this;
    for (AttitudeDelta* Node = Deltas; Node != nullptr;)
    {
        AttitudeDelta* Next = Node->Next;
        delete Node;
        Node = Next;
    }
    Deltas = nullptr;
    CopyFieldsFrom(Other);
    return *this;
}

Reputation::Vibe::VibeValue::~VibeValue()
{
    // FUN_063221f0
    for (AttitudeDelta* Node = Deltas; Node != nullptr;)
    {
        AttitudeDelta* Next = Node->Next;
        delete Node;
        Node = Next;
    }
}

unsigned __cdecl Reputation::Vibe::Count()
{
    // .text:06320f80
    return (unsigned)(GStaticVibes.size() + GDynamicVibes.size());
}

bool __cdecl Reputation::Vibe::Verify(int GroupId)
{
    // .text:063206c0
    return FindVibe(GroupId) != nullptr;
}

void __cdecl Reputation::Vibe::BeginStaticLoad()
{
    // FUN_06320430, called by the universe load right after its INI opens. The static cache is
    // not touched: the load has just cleared it through FreeStatic.
    GbStaticVibeLoading = true;
    if (GStaticVibes.capacity() < 0x1000)
        GStaticVibes.reserve(0x1000);
}

void __cdecl Reputation::Vibe::EndStaticLoad()
{
    // FUN_06320530. The cache is kept: it still points at a slot, and FindStaticVibe's key check
    // handles whatever key now sits there.
    GbStaticVibeLoading = false;
    Vc6SortStaticVibes(GStaticVibes.data(), GStaticVibes.data() + GStaticVibes.size());
}

void __cdecl Reputation::Vibe::FreeStatic()
{
    // FUN_06320550 (thunk 062b8e20), from the universe object's destructor FUN_06336ea0,
    // Universe::Startup, the universe load and Universe::Shutdown. Capacity is kept.
    GStaticVibes.clear();
    GStaticVibeHit = nullptr;
}

int __cdecl Reputation::Vibe::Alloc(int const& GroupId)
{
    // .text:06320720. An existing record is reset through the copy tail FUN_0631fad0, which
    // resets every field but keeps the record's own delta list. After loading, the static store
    // is not consulted: a key that already has a static slot gets a dynamic record too, which
    // every later lookup finds first. The dynamic insert does not touch the cache.
    if (GroupId == -1)
        return -1;

    if (GbStaticVibeLoading)
    {
        VibeValue const Default;
        VibeValue* Found = FindStaticVibe(GroupId);
        if (Found != nullptr)
        {
            Found->CopyFieldsFrom(Default);
        }
        else
        {
            FVibeSlot Slot;
            Slot.Key = GroupId;
            Slot.Value.CopyFieldsFrom(Default);
            GStaticVibes.push_back(Slot);
            GStaticVibeHit = &GStaticVibes.back();
        }
        return 0;
    }

    FDynamicVibeMap::iterator It;
    {
        VibeValue const Default;
        It = GDynamicVibes.insert(std::pair<int const, VibeValue>(GroupId, Default)).first;
    }
    VibeValue const Reset;
    It->second.CopyFieldsFrom(Reset);
    return 0;
}

int __cdecl Reputation::Vibe::EnsureExists(int const& GroupId)
{
    // .text:06320a70. Alloc's result is ignored.
    if (FindVibe(GroupId) == nullptr)
        Alloc(GroupId);
    return 0;
}

int __cdecl Reputation::Vibe::Free(int const& GroupId)
{
    // .text:06320ae0. Only the dynamic store is freed from: a key that only has a static slot
    // gives -2. The cache is reset whatever the result.
    std::size_t const Erased = GDynamicVibes.erase(GroupId);
    GDynamicVibeHit = GDynamicVibes.end();
    if (Erased == 0)
        return -2;

    FDynamicVibeMap::iterator It = GDynamicVibes.begin();
    for (; It != GDynamicVibes.end(); ++It)
        PurgeVibeDeltas(It->second, GroupId);

    std::size_t Index = 0;
    for (; Index < GStaticVibes.size(); ++Index)
        PurgeVibeDeltas(GStaticVibes[Index].Value, GroupId);
    return 0;
}

void __cdecl Reputation::Vibe::FreeDynamic()
{
    // .text:06320590. The static store itself is kept; only its deltas towards dynamic keys go.
    FDynamicVibeMap::iterator It = GDynamicVibes.begin();
    for (; It != GDynamicVibes.end(); ++It)
    {
        std::size_t Index = 0;
        for (; Index < GStaticVibes.size(); ++Index)
            PurgeVibeDeltas(GStaticVibes[Index].Value, It->first);
    }
    GDynamicVibes.clear();
    GDynamicVibeHit = GDynamicVibes.end();
}

int __cdecl Reputation::Vibe::GetAffiliation(int const& GroupId, unsigned& Out, bool ExcludeIfPlayerOwned)
{
    Out = 0xffffffffu;
    VibeValue const* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    if (ExcludeIfPlayerOwned && V->ClientID != 0)
        Out = 0xffffffffu;
    else
        Out = V->Affiliation;
    return 0;
}

int __cdecl Reputation::Vibe::SetAffiliation(int const& GroupId, unsigned Affiliation, bool Notify)
{
    // .text:063213b0. Group registry DAT_064018ec.
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    FGroup const* Group = FindGroupConst(Affiliation);
    if (Group == nullptr)
        return -3;
    unsigned const OldAffiliation = V->Affiliation;
    if (OldAffiliation == Affiliation)
        return 0;
    V->Affiliation = Affiliation;
    if (SinglePlayer() || !Notify)
    {
        V->AffiliationGroup = Group;
        return 0;
    }
    GChangedAffiliationClientMap[V->ClientID] = OldAffiliation;
    V->AffiliationGroup = nullptr;
    return 0;
}

unsigned __cdecl Reputation::Vibe::GetClientID(int GroupId)
{
    // .text:06320f10: the dynamic store ONLY -- no static-store fallback.
    VibeValue const* V = FindDynamicVibe(GroupId);
    if (V == nullptr)
        return 0;
    return V->ClientID;
}

int __cdecl Reputation::Vibe::SetClientID(int GroupId, unsigned ClientID)
{
    // .text:06320ea0: the dynamic store ONLY, like GetClientID.
    VibeValue* V = FindDynamicVibe(GroupId);
    if (V == nullptr)
        return -2;
    V->ClientID = ClientID;
    return 0;
}

int __cdecl Reputation::Vibe::GetInfocard(int const& GroupId, unsigned& Out)
{
    Out = 0;
    VibeValue const* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    Out = V->Infocard;
    return 0;
}

int __cdecl Reputation::Vibe::SetInfocard(int const& GroupId, unsigned Infocard)
{
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    V->Infocard = Infocard;
    return 0;
}

int __cdecl Reputation::Vibe::GetRank(int const& GroupId, unsigned& Out)
{
    Out = 0;
    VibeValue const* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    Out = V->Rank;
    return 0;
}

int __cdecl Reputation::Vibe::SetRank(int const& GroupId, unsigned Rank)
{
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    V->Rank = Rank;
    return 0;
}

int __cdecl Reputation::Vibe::GetName(int const& GroupId, FmtStr& Name1Out, FmtStr& Name2Out, wchar_t const*& DisplayNameOut)
{
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    Name1Out = V->Name1;
    Name2Out = V->Name2;
    DisplayNameOut = V->DisplayName;
    return 0;
}

int __cdecl Reputation::Vibe::SetName(int const& GroupId, FmtStr const& Name1, FmtStr const& Name2, wchar_t const* DisplayName)
{
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    V->Name1 = Name1;
    V->Name2 = Name2;
    if (DisplayName != nullptr)
    {
        wcsncpy(V->DisplayName, DisplayName, 0x17);
        V->DisplayName[0x17] = L'\0';
    }
    else
    {
        V->DisplayName[0] = L'\0';
    }
    return 0;
}

int __cdecl Reputation::Vibe::GetGroupFeelingsTowards(int const& GroupId, unsigned const& OtherGroupId, float& Out)
{
    // .text:06320fc0. A player-owned record ignores the faction base feeling.
    Out = 0.0f;
    VibeValue const* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;

    if (Reputation::get_feelings_towards(OtherGroupId, V->Affiliation, Out) != 0)
        return -3;
    if (V->ClientID != 0)
        Out = 0.0f;

    float Override = 0.0f;
    if (OtherGroupId != 0xffffffffu)
    {
        std::size_t Index = 0;
        for (; Index < V->GroupFeelings.size(); ++Index)
        {
            if (V->GroupFeelings[Index].GroupId == OtherGroupId)
            {
                Override = V->GroupFeelings[Index].Feeling;
                break;
            }
        }
    }
    Out = Override + Out;
    if (1.0f < Out)
        Out = 1.0f;
    if (-1.0f > Out)
        Out = -1.0f;
    return 0;
}

int __cdecl Reputation::Vibe::SetGroupFeelingsTowards(int const& GroupId, unsigned const& OtherGroupId, float Feeling)
{
    // .text:063214f0. Clamp constants DAT_063a3ccc / DAT_063a3cc8. The tail runs even when
    // get_feelings_towards failed: a player whose feeling towards its own affiliation is not
    // above 0.65 loses it, then the auto-affiliation pass FUN_0631fdd0 runs.
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;

    int Result = -3;
    float Base = 0.0f;
    if (Reputation::get_feelings_towards(OtherGroupId, V->Affiliation, Base) == 0)
    {
        Result = 0;
        float const Delta = Feeling - (V->ClientID != 0 ? 0.0f : Base);
        if (FindGroupConst(OtherGroupId) != nullptr)
        {
            float Clamped = (1.0f < Delta) ? 1.0f : Delta;
            if (Clamped < -1.0f)
                Clamped = -1.0f;

            bool bStored = false;
            std::size_t Index = 0;
            for (; Index < V->GroupFeelings.size(); ++Index)
            {
                if (V->GroupFeelings[Index].GroupId == OtherGroupId)
                {
                    V->GroupFeelings[Index].Feeling = Clamped;
                    bStored = true;
                    break;
                }
            }
            if (!bStored)
            {
                GroupFeeling Entry;
                Entry.GroupId = OtherGroupId;
                Entry.Feeling = Clamped;
                V->GroupFeelings.push_back(Entry);
            }
        }
    }

    if (V->ClientID == 0)
        return Result;
    if (V->Affiliation != kNoAffiliation)
    {
        float Own = 0.0f;
        std::size_t Index = 0;
        for (; Index < V->GroupFeelings.size(); ++Index)
        {
            if (V->GroupFeelings[Index].GroupId == V->Affiliation)
            {
                Own = V->GroupFeelings[Index].Feeling;
                break;
            }
        }
        if (!(Own <= kAffiliationKeepFeeling))
            return Result;
        V->Affiliation = kNoAffiliation;
    }
    UpdatePlayerAffiliation(*V);
    return Result;
}

int __cdecl Reputation::Vibe::SetAttitudeTowardsPlayer(int GroupId, float Attitude)
{
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;
    V->AttitudeTowardsPlayer = Attitude;
    return 0;
}

int __cdecl Reputation::Vibe::Get(int const& GroupId, unsigned& AffiliationOut, unsigned& RankOut, unsigned char& CountOut, Relation* const Relations, FmtStr& Name1Out, FmtStr& Name2Out, wchar_t const*& DisplayNameOut)
{
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;

    AffiliationOut = V->Affiliation;
    RankOut = V->Rank;

    // Single-byte counter: wraps at 256 like retail's (see the declaration in
    // common_reputation.h).
    CountOut = 0;
    for (std::size_t i = 0; i < V->GroupFeelings.size(); ++i)
    {
        Relations[CountOut].GroupId = V->GroupFeelings[i].GroupId;
        Relations[CountOut].Feeling = V->GroupFeelings[i].Feeling;
        ++CountOut;
    }

    Name1Out = V->Name1;
    Name2Out = V->Name2;
    DisplayNameOut = V->DisplayName;
    return 0;
}

int __cdecl Reputation::Vibe::Set(int const& GroupId, unsigned Affiliation, unsigned Rank, unsigned char Count, Relation const* const Relations, FmtStr const& Name1, FmtStr const& Name2, wchar_t const* DisplayName)
{
    if (GroupId == -1)
        return -1;
    VibeValue* V = FindVibe(GroupId);
    if (V == nullptr)
        return -2;

    // .text:06320d0d (see the declaration in common_reputation.h).
    V->Affiliation = Affiliation;
    // NPC entries cache the new affiliation's group record (null when not registered); player
    // entries leave AffiliationGroup as it was.
    if (V->ClientID == 0)
        V->AffiliationGroup = FindGroupConst(Affiliation);
    V->Rank = Rank;

    // Only registered groups are kept.
    V->GroupFeelings.clear();
    V->GroupFeelings.reserve(Count);
    for (unsigned char i = 0; i < Count; ++i)
    {
        if (FindGroupConst(Relations[i].GroupId) == nullptr)
            continue;
        GroupFeeling Entry;
        Entry.GroupId = Relations[i].GroupId;
        Entry.Feeling = Relations[i].Feeling;
        V->GroupFeelings.push_back(Entry);
    }

    V->Name1 = Name1;
    V->Name2 = Name2;
    wcsncpy(V->DisplayName, DisplayName, 0x17);
    V->DisplayName[0x17] = L'\0';
    UpdatePlayerAffiliation(*V);
    return 0;
}

namespace
{
    // FUN_0631ff70, the attitude-blend core of FUN_063217e0.
    //  MP client (not SinglePlayer() and not IsMPServer()): From->AttitudeTowardsPlayer, no
    //  blend -- a networked client trusts its last-replicated value.
    //  Authoritative side: Base comes from the CACHED From->AffiliationGroup (+0xc), not from
    //  get_feelings_towards: that pointer is cleared by Alloc, SetAffiliation's MP-notify path
    //  and the auto-affiliation pass, so Base is 0 for such an entry until its affiliation is
    //  set again. Base is 0 for a player-owned To. Override is the TARGET's GroupFeelings entry
    //  for the FROM side's faction. The delta is looked up by the *raw* target GroupId, not its
    //  affiliation, matching AdjustAttitude's bookkeeping.
    // The result stays on the x87 stack for every caller, so it is double here.
    double BlendAttitudeRaw(Reputation::Vibe::VibeValue const& From, Reputation::Vibe::VibeValue const& To, int RawToGroupId)
    {
        if (!SinglePlayer() && !IsMPServer())
            return From.AttitudeTowardsPlayer;

        float Base = 0.0f;
        FGroup const* FromGroup = static_cast<FGroup const*>(From.AffiliationGroup);
        if (FromGroup != nullptr && To.Affiliation != kNoAffiliation)
        {
            if (From.Affiliation == To.Affiliation)
            {
                Base = 1.0f;                                          // DAT_0639a1d0
            }
            else
            {
                std::map<unsigned, float>::const_iterator It = FromGroup->Feelings.find(To.Affiliation);
                if (It != FromGroup->Feelings.end())
                    Base = It->second;
            }
        }
        if (To.ClientID != 0)
            Base = 0.0f;

        float Override = 0.0f;
        if (From.Affiliation != kNoAffiliation)
        {
            for (std::size_t i = 0; i < To.GroupFeelings.size(); ++i)
            {
                if (To.GroupFeelings[i].GroupId == From.Affiliation)
                {
                    Override = To.GroupFeelings[i].Feeling;
                    break;
                }
            }
        }

        double Sum = (double)Base + Override;

        unsigned RawTo = (unsigned)RawToGroupId;
        for (Reputation::Vibe::AttitudeDelta const* Node = From.Deltas; Node != nullptr; Node = Node->Next)
        {
            if (Node->GroupId == RawTo)
            {
                Sum += Node->Delta;
                break;
            }
        }

        return Sum;
    }

    // FUN_063217e0's clamp (DAT_063a3ccc = +1.0f, DAT_063a3cc8 = -1.0f).
    double ClampAttitude(double Value)
    {
        if (Value > 1.0)
            return 1.0;
        if (Value < -1.0)
            return -1.0;
        return Value;
    }

    // FUN_063217e0's quantize step, after ClampAttitude: the attitude is rounded half away from
    // zero to two decimal places, and a value within 1e-8 (DAT_063a3ce8) of zero snaps to 0.
    // The constants come from CRT initialisers: DAT_06401920 = 1.0 / 0.01 (LAB_0631e680) and
    // DAT_06401918 = 0.5 / DAT_06401920 (LAB_0631e6a0), kept as those divisions.
    float QuantizeAttitude(double Value)
    {
        double const Step = 1.0 / 0.01;      // retail DAT_06401920
        double const HalfStep = 0.5 / Step;  // retail DAT_06401918
        double const Epsilon = 1.0e-8;       // retail .rdata DAT_063a3ce8

        double const V = Value;
        if (V < -Epsilon)
            return (float)((double)(long)((V - HalfStep) * Step) / Step - Epsilon);
        if (V > Epsilon)
            return (float)((double)(long)((V + HalfStep) * Step) / Step + Epsilon);
        return 0.0f;
    }

    // FUN_0631ff30, called only by AdjustAttitude: the same GroupFeelings (+0x7c) override
    // lookup BlendAttitudeRaw inlines.
    float FindGroupFeelingOverride(Reputation::Vibe::VibeValue const& Owner, unsigned SourceAffiliation)
    {
        if (SourceAffiliation == kNoAffiliation)
            return 0.0f;

        for (std::size_t i = 0; i < Owner.GroupFeelings.size(); ++i)
        {
            if (Owner.GroupFeelings[i].GroupId == SourceAffiliation)
                return Owner.GroupFeelings[i].Feeling;
        }
        return 0.0f;
    }
}

namespace
{
    // FUN_063217e0, the body of AttitudeTowards (null Out2) and AttitudeTowardsSymmetrical. Out2
    // is not touched before the end. The blend, the clamp and the quantize input stay on the x87
    // stack in retail: double here.
    int AttitudeTowardsCore(int const& FromGroupId, int const& ToGroupId, float& Out1, float* Out2)
    {
        Out1 = 0.0f;
        Reputation::Vibe::VibeValue const* From = FindVibe(FromGroupId);
        if (From == nullptr)
            return -2;
        Reputation::Vibe::VibeValue const* To = FindVibe(ToGroupId);
        if (To == nullptr)
            return -3;

        Out1 = QuantizeAttitude(ClampAttitude(BlendAttitudeRaw(*From, *To, ToGroupId)));
        if (Out2 == nullptr)
            return 0;
        *Out2 = QuantizeAttitude(ClampAttitude(BlendAttitudeRaw(*To, *From, FromGroupId)));
        return 0;
    }
}

int __cdecl Reputation::Vibe::AttitudeTowards(int const& FromGroupId, int const& ToGroupId, float& Out)
{
    // .text:063217c0
    return AttitudeTowardsCore(FromGroupId, ToGroupId, Out, nullptr);
}

int __cdecl Reputation::Vibe::AttitudeTowardsSymmetrical(int const& FromGroupId, int const& ToGroupId, float& Out1, float& Out2)
{
    // .text:06321a70: a tail jump to FUN_063217e0.
    return AttitudeTowardsCore(FromGroupId, ToGroupId, Out1, &Out2);
}

int __cdecl Reputation::Vibe::AdjustAttitude(int const& FromGroupId, int const& ToGroupId, float Delta)
{
    // .text:06321af0. Retail also calls the blend FUN_0631ff70 and discards the result -- a pure
    // read, so the port does not call it. The repeated lookups find the records the first two
    // did and are kept as retail has them. When get_feelings_towards fails the delta is stored
    // unclamped.
    VibeValue* From = FindVibe(FromGroupId);
    if (From == nullptr)
        return -2;
    VibeValue* To = FindVibe(ToGroupId);
    if (To == nullptr)
        return -3;

    unsigned const ToId = (unsigned)ToGroupId;
    AttitudeDelta* Node = From->Deltas;
    while (Node != nullptr && Node->GroupId != ToId)
        Node = Node->Next;
    if (Node == nullptr)
    {
        Node = new AttitudeDelta();
        Node->Next = From->Deltas;
        From->Deltas = Node;
        Node->Delta = 0.0f;
        Node->GroupId = ToId;
    }

    unsigned FromAffiliation = kNoAffiliation;
    VibeValue const* FromAgain = FindVibe(FromGroupId);
    if (FromAgain != nullptr)
        FromAffiliation = FromAgain->Affiliation;

    float Base = 0.0f;
    VibeValue const* ToAgain = FindVibe(ToGroupId);
    if (ToAgain == nullptr || Reputation::get_feelings_towards(FromAffiliation, ToAgain->Affiliation, Base) != 0)
    {
        Node->Delta = (float)((double)Delta - Base);
        return 0;
    }
    if (ToAgain->ClientID != 0)
        Base = 0.0f;

    // override + Base stays on the x87 stack through the clamp.
    double Sum = (double)FindGroupFeelingOverride(*ToAgain, FromAffiliation) + Base;
    if (1.0 < Sum)
        Sum = 1.0;
    if (-1.0 > Sum)
        Sum = -1.0;
    Node->Delta = (float)((double)Delta - Sum);
    return 0;
}
