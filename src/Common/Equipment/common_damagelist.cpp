// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_misc.h"

// ============================================================================
// DamageEntry
// ============================================================================

DamageEntry::DamageEntry()
    : SubObjId(0)
    , HitPts(-1.0f)
    , Fate(SubObjFate::None)
{
    // FUN_06280f00
}

bool DamageEntry::operator==(DamageEntry const& Other) const
{
    // FUN_062bbb20
    return SubObjId == Other.SubObjId
        && HitPts == Other.HitPts
        && Fate == Other.Fate;
}

bool DamageEntry::operator!=(DamageEntry const& Other) const
{
    // FUN_062bbb50
    return !(*this == Other);
}

bool DamageEntry::operator<(DamageEntry const& Other) const
{
    // FUN_062bbb90. Retail quirk kept: operator< and operator> are swapped (this one is
    // "greater than").
    return Other.SubObjId < SubObjId;
}

bool DamageEntry::operator>(DamageEntry const& Other) const
{
    // FUN_062bbbb0. Swapped with operator<, see above.
    return SubObjId < Other.SubObjId;
}

char const* DamageEntry::FateToString(SubObjFate Fate)
{
    // FUN_062bbce0, ICF-folded with DamageList::DmgCauseToString: returns .rdata:0639ed24 "???"
    // for every value.
    (void)Fate;
    return "???";
}

// ============================================================================
// DamageList
// ============================================================================

DamageList::DamageList()
    : Count(0)
    , bDestroyed(false)
    , Cause(DamageCause::Unknown)
    , InflictorId(0)
    , InflictorOwner(0)
{
    // FUN_06280f70. Retail allocates the VC6 std::list sentinel node here. Count/bDestroyed/
    // Cause/InflictorId/InflictorOwner are +0x08/+0x0c/+0x10/+0x14/+0x18.
}

DamageList::DamageList(DamageList const& Other)
    : Entries(Other.Entries)
    , Count(Other.Count)
    , bDestroyed(Other.bDestroyed)
    , Cause(Other.Cause)
    , InflictorId(Other.InflictorId)
    , InflictorOwner(Other.InflictorOwner)
{
    // FUN_06280fc0
}

DamageList& DamageList::operator=(DamageList const& Other)
{
    // FUN_06281070
    if (this != &Other)
    {
        Entries = Other.Entries;
        Count = Other.Count;
        bDestroyed = Other.bDestroyed;
        Cause = Other.Cause;
        InflictorId = Other.InflictorId;
        InflictorOwner = Other.InflictorOwner;
    }
    return *this;
}

DamageList::~DamageList()
{
    // Retail frees the list nodes; std::list's own destructor does that here.
}

void DamageList::add_damage_entry(unsigned short SubObjId, float HitPts, DamageEntry::SubObjFate Fate)
{
    // FUN_062bbc00. One entry per SubObjId, unbounded list. An existing entry keeps
    // min(HitPts) and the more severe (higher) Fate.
    bool Found = false;
    for (DamageEntry& Entry : Entries)
    {
        if (Entry.SubObjId == SubObjId)
        {
            // The old value is kept only when strictly less; equal or unordered stores the new.
            Entry.HitPts = (Entry.HitPts < HitPts) ? Entry.HitPts : HitPts;
            if (static_cast<int>(Fate) > static_cast<int>(Entry.Fate))
                Entry.Fate = Fate;
            Found = true;
            break;
        }
    }
    if (!Found)
    {
        DamageEntry NewEntry;
        NewEntry.SubObjId = SubObjId;
        NewEntry.HitPts = HitPts;
        NewEntry.Fate = Fate;
        Entries.push_back(NewEntry);
        ++Count;
    }

    // SubObjId 1 is the root/hull: hull at or below zero marks the object destroyed.
    if (SubObjId == 1 && HitPts <= 0.0f)
        bDestroyed = true;
}

float DamageList::get_hit_pts_left(unsigned short SubObjId) const
{
    // FUN_062bbbd0. -1.0f (DamageEntry's default) when there is no entry.
    for (DamageEntry const& Entry : Entries)
    {
        if (Entry.SubObjId == SubObjId)
            return Entry.HitPts;
    }
    return -1.0f;
}

DamageCause DamageList::get_cause() const
{
    // FUN_06263a70 (ICF-folded)
    return Cause;
}

void DamageList::set_cause(DamageCause NewCause)
{
    // FUN_06332f50 (ICF-folded with TRANode::set_default_mask,
    // StateMachineMessage::set_delivery_state, EquipDesc::set_status)
    Cause = NewCause;
}

unsigned int DamageList::get_inflictor_id() const
{
    return InflictorId;
}

void DamageList::set_inflictor_id(unsigned int Id)
{
    // FUN_0633bad0
    InflictorId = Id;
}

unsigned int DamageList::get_inflictor_owner_player() const
{
    return InflictorOwner;
}

void DamageList::set_inflictor_owner_player(unsigned int Player)
{
    // FUN_0629afd0
    InflictorOwner = Player;
}

bool DamageList::is_destroyed() const
{
    return bDestroyed;
}

void DamageList::set_destroyed(bool Destroyed)
{
    // FUN_06309b50
    bDestroyed = Destroyed;
}

bool DamageList::is_inflictor_a_player() const
{
    // FUN_06280fb0
    return InflictorOwner != 0;
}

char const* DamageList::DmgCauseToString(DamageCause Cause)
{
    // FUN_062bbce0, ICF-folded with DamageEntry::FateToString: returns .rdata:0639ed24 "???"
    // for every value; Common.dll has no per-value strings.
    (void)Cause;
    return "???";
}

// ===========================================================================
// Retail Common.dll exports all 55 members of its VC6 std::list<DamageEntry>; the port exports
// the modern instantiation. The importing declaration is in common_misc.h.
// ===========================================================================
template class COMMON_API std::list<DamageEntry>;
