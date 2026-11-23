// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll EquipDesc, EquipDescVector and EquipDescList, EquipDesc::CARGO_BAY_HP_NAME, and the
// exported std::list / std::vector<EquipDesc> and weighted_vector<unsigned int> instantiations.
//
// Static-initialisation order: CARGO_BAY_HP_NAME is StringAlloc'd by a dynamic initialiser, so
// common_stringcache.cpp (GStringCacheOwner) must precede this TU; among the goods TUs this one
// comes first (then common_constants.cpp, common_loadout.cpp, common_goodinfo.cpp).

#include "common_goods.h"
#include "common_ini.h"  // StringAlloc
#include "common_misc.h" // SUBOBJ_ID_NONE

#include <cstddef>
#include <list>
#include <new>
#include <vector>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

// ---------------------------------------------------------------------------
// EquipDesc
// ---------------------------------------------------------------------------

// Dynamic initialiser FUN_06309bc0 (static-init table entry .data:063ea484).
CacheString const EquipDesc::CARGO_BAY_HP_NAME = { StringAlloc("BAY", false) };

// FUN_06309ae0. Field offsets are asserted below.
EquipDesc::EquipDesc()
    : Cached(0)
    , Id(SUBOBJ_ID_NONE)
    , ArchId(0)
    , Hardpoint(CARGO_BAY_HP_NAME.Value)
    , bEquipped(false)
    , Health(1.0f)
    , Count(1)
    , bMission(false)
    , Owner(0)
{
}

// FUN_06263c60
EquipDesc::EquipDesc(EquipDesc const& Other)
{
    Cached = Other.Cached;
    Id = Other.Id;
    ArchId = Other.ArchId;
    Hardpoint = Other.Hardpoint;
    bEquipped = Other.bEquipped;
    Health = Other.Health;
    Count = Other.Count;
    bMission = Other.bMission;
    Owner = Other.Owner;
}

// get_id FUN_06263b70, get_hardpoint FUN_06263b80, get_status FUN_06263ba0.
unsigned short EquipDesc::get_id() const
{
    return Id;
}

CacheString const& EquipDesc::get_hardpoint() const
{
    // Retail returns the field's address: a CacheString is exactly the one char const* there.
    return *(CacheString const*)&Hardpoint;
}

float EquipDesc::get_status() const
{
    return Health;
}

void EquipDesc::set_id(unsigned short NewId)
{
    Id = NewId;
}

void EquipDesc::set_arch_id(unsigned Arch)
{
    ArchId = Arch;
}

void EquipDesc::set_count(int NewCount)
{
    // FUN_0633bad0
    Count = NewCount;
}

void EquipDesc::set_hardpoint(CacheString const& Hp)
{
    Hardpoint = Hp.Value;
}

void EquipDesc::set_equipped(unsigned char bOn)
{
    bEquipped = bOn;
}

void EquipDesc::set_status(float HealthValue)
{
    Health = HealthValue;
}

// FUN_06309b80
void EquipDesc::make_internal()
{
    Hardpoint = CARGO_BAY_HP_NAME.Value;
}

bool EquipDesc::is_internal() const
{
    return Hardpoint == CARGO_BAY_HP_NAME.Value;
}

bool EquipDesc::get_temporary() const
{
    return bMission;
}

unsigned EquipDesc::get_arch_id() const
{
    return ArchId;
}

unsigned char EquipDesc::is_equipped() const
{
    return bEquipped;
}

int EquipDesc::get_count() const
{
    return Count;
}

void EquipDesc::set_temporary(bool bOn)
{
    bMission = bOn;
}

int EquipDesc::get_owner() const
{
    return (int)Owner;
}

void EquipDesc::set_owner(int NewOwner)
{
    Owner = (unsigned)NewOwner;
}

// FUN_06309b90. Only cargo-bay items with a nonzero ArchId occupy space. The product is left
// in ST0 (hence double); GetEquipment's result is not tested.
double EquipDesc::get_cargo_space_occupied() const
{
    if (Hardpoint != CARGO_BAY_HP_NAME.Value || ArchId == 0)
        return 0.0;
    Archetype::Equipment* Arch = Archetype::GetEquipment(ArchId);
    return (double)Count * Arch->Volume;
}

// FUN_06266310, ICF-folded with weighted_vector<unsigned int>'s default ctor.
EquipDescVector::EquipDescVector()
    : AllocPad(0)
    , Begin(nullptr)
    , End(nullptr)
    , Cap(nullptr)
{
}

// FUN_06263cf0. Allocates exactly Other.size() elements, also when that is 0 (operator new(0)).
EquipDescVector::EquipDescVector(EquipDescVector const& Other)
    : AllocPad(Other.AllocPad)
    , Begin(nullptr)
    , End(nullptr)
    , Cap(nullptr)
{
    int Count = (Other.Begin != nullptr) ? (int)(Other.End - Other.Begin) : 0;
    if (Count < 0) // the VC6 allocator's own clamp
        Count = 0;
    Begin = (EquipDesc*)::operator new(Count * sizeof(EquipDesc));
    EquipDesc* Dst = Begin;
    for (EquipDesc const* Src = Other.Begin; Src != Other.End; ++Src, ++Dst)
        new (Dst) EquipDesc(*Src);
    End = Dst;
    Cap = Dst;
}

// FUN_06269110. EquipDesc is trivially destructible; retail runs no element destructor.
EquipDescVector::~EquipDescVector()
{
    ::operator delete(Begin);
    Begin = nullptr;
    End = nullptr;
    Cap = nullptr;
}

// FUN_06263d90, the VC6 vector assignment (unsigned size compares). AllocPad is never touched.
// Shrinking or equal assigns in place; within capacity assigns then copy-constructs the rest
// after End (FUN_0626c1e0); otherwise reallocates to exactly Other.size().
EquipDescVector& EquipDescVector::operator=(EquipDescVector const& Other)
{
    if (this == &Other)
        return *this;

    unsigned OtherCount = (Other.Begin != nullptr) ? (unsigned)(Other.End - Other.Begin) : 0;
    unsigned OwnCount = (Begin != nullptr) ? (unsigned)(End - Begin) : 0;
    if (OtherCount <= OwnCount)
    {
        EquipDesc* Dst = Begin;
        for (EquipDesc const* Src = Other.Begin; Src != Other.End; ++Src, ++Dst)
            *Dst = *Src;
        End = Begin + ((Other.Begin != nullptr) ? (Other.End - Other.Begin) : 0);
        return *this;
    }

    unsigned OwnCapacity = (Begin != nullptr) ? (unsigned)(Cap - Begin) : 0;
    if (OtherCount <= OwnCapacity)
    {
        EquipDesc const* Mid = Other.Begin + ((Begin != nullptr) ? (End - Begin) : 0);
        EquipDesc* Dst = Begin;
        for (EquipDesc const* Src = Other.Begin; Src != Mid; ++Src, ++Dst)
            *Dst = *Src;
        Dst = End;
        for (EquipDesc const* Src = Mid; Src != Other.End; ++Src, ++Dst)
            new (Dst) EquipDesc(*Src);
        End = Begin + ((Other.Begin != nullptr) ? (Other.End - Other.Begin) : 0);
        return *this;
    }

    ::operator delete(Begin);
    int NewCount = (Other.Begin != nullptr) ? (int)(Other.End - Other.Begin) : 0;
    if (NewCount < 0) // the VC6 allocator's own clamp
        NewCount = 0;
    Begin = (EquipDesc*)::operator new(NewCount * sizeof(EquipDesc));
    EquipDesc* Dst = Begin;
    for (EquipDesc const* Src = Other.Begin; Src != Other.End; ++Src, ++Dst)
        new (Dst) EquipDesc(*Src);
    Cap = Dst;
    End = Dst;
    return *this;
}

namespace
{
    // Growth helper for append / ctor-from-list / add_equipment_item. Deviation: grows to
    // exactly the size needed instead of retail's roughly-doubling growth; no capacity()
    // accessor exists, so this is not observable.
    void EnsureEquipCapacity(EquipDescVector& Vec, unsigned ExtraCount)
    {
        const unsigned OldCount = (unsigned)(Vec.End - Vec.Begin);
        const unsigned OldCap = (unsigned)(Vec.Cap - Vec.Begin);
        const unsigned NewCount = OldCount + ExtraCount;
        if (OldCap >= NewCount)
            return;

        EquipDesc* NewBuf = (EquipDesc*)::operator new(NewCount * sizeof(EquipDesc));
        for (unsigned Index = 0; Index < OldCount; ++Index)
            new (&NewBuf[Index]) EquipDesc(Vec.Begin[Index]);
        ::operator delete(Vec.Begin);
        Vec.Begin = NewBuf;
        Vec.End = NewBuf + OldCount;
        Vec.Cap = NewBuf + NewCount;
    }

    void EquipVectorPushBack(EquipDescVector& Vec, EquipDesc const& Item)
    {
        EnsureEquipCapacity(Vec, 1);
        new (Vec.End) EquipDesc(Item);
        ++Vec.End;
    }
}

// FUN_06309e40
EquipDescVector::EquipDescVector(EquipDescList const& Other)
    : AllocPad(0)
    , Begin(nullptr)
    , End(nullptr)
    , Cap(nullptr)
{
    const unsigned Count = (unsigned)Other.Items.size();
    if (Count == 0)
        return;

    Begin = (EquipDesc*)::operator new(Count * sizeof(EquipDesc));
    unsigned Index = 0;
    for (std::list<EquipDesc>::const_iterator It = Other.Items.begin(); It != Other.Items.end();
         ++It, ++Index)
        new (&Begin[Index]) EquipDesc(*It);
    End = Begin + Count;
    Cap = Begin + Count;
}

// FUN_06309be0
void EquipDescVector::append(EquipDescVector const& Other)
{
    const unsigned AddCount = (unsigned)(Other.End - Other.Begin);
    if (AddCount == 0)
        return;

    EnsureEquipCapacity(*this, AddCount);
    for (unsigned Index = 0; Index < AddCount; ++Index)
        new (End + Index) EquipDesc(Other.Begin[Index]);
    End += AddCount;
}

// FUN_0630a0b0
int EquipDescVector::add_equipment_item(EquipDesc const& Desc, bool bCombinable)
{
    if (bCombinable)
    {
        // Retail quirk kept: when the first ArchId match fails the bMission/Owner/Hardpoint
        // test, the scan restarts from Begin, so it never terminates if that entry never
        // matches. Hardpoints compare with DACOM stricmp. "Not below 1.0" includes NaN.
        for (;;)
        {
            EquipDesc* Match = nullptr;
            for (EquipDesc* It = Begin; It != End; ++It)
            {
                if (It->ArchId == Desc.ArchId)
                {
                    Match = It;
                    break;
                }
            }
            if (Match == nullptr)
                break; // no ArchId match: plain append

            if (Match->bMission != Desc.bMission || Match->Owner != Desc.Owner ||
                CallDacomStricmp(Match->Hardpoint, Desc.Hardpoint) != 0)
                continue; // retail: restart the scan from Begin

            if (!(Match->Health < 1.0f))
            {
                // Full-health stack takes the incoming health (overwritten, not summed).
                Match->Count += Desc.Count;
                Match->Health = Desc.Health;
            }
            else if (!(Desc.Health < 1.0f))
            {
                Match->Count += Desc.Count;
            }
            else
            {
                // The sum stays on the x87 stack (exact in a double).
                double const NewHealth = (double)Match->Health + Desc.Health;
                const int NewCount = Desc.Count + Match->Count;
                if (!(NewHealth < 1.0))
                {
                    Match->Count = NewCount;
                    Match->Health = (float)(NewHealth - 1.0);
                }
                else
                {
                    Match->Health = (float)NewHealth;
                    Match->Count = NewCount - 1;
                }
            }
            return 1;
        }
    }

    EquipVectorPushBack(*this, Desc);
    return 0;
}

// FUN_0630a350
EquipDesc* EquipDescVector::traverse_equipment_type(unsigned Type, EquipDesc const* Prev)
{
    for (EquipDesc* It = Begin; It != End; ++It)
    {
        if (It->ArchId == Type)
        {
            if (Prev == nullptr)
                return It;
            if (Prev == It)
                Prev = nullptr;
        }
    }
    return nullptr;
}

static_assert(offsetof(EquipDesc, Id) == 2, "EquipDesc+2 id");
static_assert(offsetof(EquipDesc, ArchId) == 4, "EquipDesc+4 arch");
static_assert(offsetof(EquipDesc, Hardpoint) == 8, "EquipDesc+8 hp");
static_assert(offsetof(EquipDesc, bEquipped) == 0x0C, "EquipDesc+0xC equipped");
static_assert(offsetof(EquipDesc, Health) == 0x10, "EquipDesc+0x10 health");
static_assert(offsetof(EquipDesc, Count) == 0x14, "EquipDesc+0x14 count");
static_assert(offsetof(EquipDesc, bMission) == 0x18, "EquipDesc+0x18 mission");
static_assert(offsetof(EquipDesc, Owner) == 0x1C, "EquipDesc+0x1C owner");

// ---------------------------------------------------------------------------
// EquipDescList. freelancer.exe holds one static instance (DAT_00672960).
// ---------------------------------------------------------------------------
EquipDescList::EquipDescList()
    : Items()
{
}

EquipDescList::~EquipDescList()
{
}

// FUN_0630a4d0. Returns 0/1 and never assigns an Id; same health-aware merge and
// restart-from-begin() quirk as EquipDescVector::add_equipment_item.
int EquipDescList::add_equipment_item(EquipDesc const& Desc, bool bCombinable)
{
    if (bCombinable)
    {
        for (;;)
        {
            EquipDesc* Match = nullptr;
            for (std::list<EquipDesc>::iterator It = Items.begin(); It != Items.end(); ++It)
            {
                if (It->ArchId == Desc.ArchId)
                {
                    Match = &*It;
                    break;
                }
            }
            if (Match == nullptr)
                break; // no ArchId match: plain append

            if (Match->bMission != Desc.bMission || Match->Owner != Desc.Owner ||
                CallDacomStricmp(Match->Hardpoint, Desc.Hardpoint) != 0)
                continue; // retail: restart the scan from begin()

            if (!(Match->Health < 1.0f))
            {
                Match->Count += Desc.Count;
                Match->Health = Desc.Health;
            }
            else if (!(Desc.Health < 1.0f))
            {
                Match->Count += Desc.Count;
            }
            else
            {
                double const NewHealth = (double)Desc.Health + Match->Health;
                const int NewCount = Desc.Count + Match->Count;
                if (!(NewHealth < 1.0))
                {
                    Match->Count = NewCount;
                    Match->Health = (float)(NewHealth - 1.0);
                }
                else
                {
                    Match->Health = (float)NewHealth;
                    Match->Count = NewCount - 1;
                }
            }
            return 1;
        }
    }

    Items.push_back(Desc);
    return 0;
}

// FUN_0630a460
void EquipDescList::append(EquipDescList const& Other)
{
    for (std::list<EquipDesc>::const_iterator It = Other.Items.begin(); It != Other.Items.end(); ++It)
        Items.push_back(*It);
}

// FUN_0630a380
EquipDescList::EquipDescList(EquipDescVector const& Other)
    : Items()
{
    const unsigned Count = (Other.Begin != nullptr)
        ? (unsigned)(Other.End - Other.Begin)
        : 0;
    for (unsigned Index = 0; Index < Count; ++Index)
        Items.push_back(Other.Begin[Index]);
}

// FUN_0630a660. CountToRemove -1 removes the whole stack; a partial removal resets Health to
// 1.0. Returns -1 if Id is not found, 0 if the item was erased, 1 if some Count is left.
int EquipDescList::remove_equipment_item(unsigned short Id, int CountToRemove)
{
    for (std::list<EquipDesc>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        EquipDesc& Item = *It;
        if (Item.Id != Id)
            continue;

        if (CountToRemove == -1)
        {
            Item.Count = 0;
        }
        else
        {
            Item.Count -= CountToRemove;
            Item.Health = 1.0f;
        }

        if (Item.Count == 0)
        {
            Items.erase(It);
            return 0;
        }
        return 1;
    }
    return -1;
}

EquipDesc* EquipDescList::find_equipment_item(unsigned short Id)
{
    for (std::list<EquipDesc>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        if (It->Id == Id)
            return &*It;
    }
    return nullptr;
}

EquipDesc const* EquipDescList::find_equipment_item(unsigned short Id) const
{
    return const_cast<EquipDescList*>(this)->find_equipment_item(Id);
}

// FUN_0630a6e0. Matches by Hardpoint pointer identity.
EquipDesc* EquipDescList::find_equipment_item(CacheString const& Hp)
{
    for (std::list<EquipDesc>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        if (It->Hardpoint == Hp.Value)
            return &*It;
    }
    return nullptr;
}

EquipDesc const* EquipDescList::find_equipment_item(CacheString const& Hp) const
{
    return const_cast<EquipDescList*>(this)->find_equipment_item(Hp);
}

// FUN_0630a710 / FUN_0630a750. First match, or the next one after Prev.
EquipDesc* EquipDescList::traverse_equipment_type(unsigned Type, EquipDesc const* Prev)
{
    for (std::list<EquipDesc>::iterator Node = Items.begin(); Node != Items.end(); ++Node)
    {
        EquipDesc* It = &*Node;
        if (It->ArchId == Type)
        {
            if (Prev == nullptr)
                return It;
            if (Prev == It)
                Prev = nullptr;
        }
    }
    return nullptr;
}

EquipDesc const* EquipDescList::traverse_equipment_type(unsigned Type, EquipDesc const* Prev) const
{
    return const_cast<EquipDescList*>(this)->traverse_equipment_type(Type, Prev);
}

// FUN_0630a790. Deviation: both branches are direct scans; retail restarts the list scan on
// each rejected candidate, with the same result (first eligible match, or the global best).
EquipDesc const* EquipDescList::find_matching_cargo(unsigned Type, int NeedCount, float TargetHealth) const
{
    if (arch_is_combinable(Type))
    {
        for (std::list<EquipDesc>::const_iterator It = Items.begin(); It != Items.end(); ++It)
        {
            EquipDesc const& Item = *It;
            if (Item.ArchId == Type && !Item.bMission && Item.Count >= NeedCount)
                return &Item;
        }
        return nullptr;
    }

    EquipDesc const* Best = nullptr;
    float BestDiff = 2.0f; // bit pattern 0x40000000 (Ghidra shows it as (float)param_2)
    for (std::list<EquipDesc>::const_iterator It = Items.begin(); It != Items.end(); ++It)
    {
        EquipDesc const& Item = *It;
        if (Item.ArchId != Type || Item.bMission || Item.bEquipped)
            continue;

        // The difference stays unrounded on the x87 stack; unordered keeps the old best and
        // only the winner is rounded to float.
        double const Diff = (double)TargetHealth - Item.Health;
        if (Diff < BestDiff)
        {
            BestDiff = (float)Diff;
            Best = &Item;
        }
    }
    return Best;
}

// FUN_0630a8e0. Each unrounded per-item product is added to the float total, rounded each step.
float EquipDescList::get_cargo_space_occupied() const
{
    float Total = 0.0f;
    for (std::list<EquipDesc>::const_iterator It = Items.begin(); It != Items.end(); ++It)
        Total = (float)(It->get_cargo_space_occupied() + Total);
    return Total;
}

// ===========================================================================
// Retail Common.dll exports every member of its VC6 std::list<EquipDesc> (55) and
// std::vector<EquipDesc> (47); FreeLancer.exe, Server.dll and rpclocal.dll import them by
// name. The port exports the modern instantiations; importing declarations are in common_goods.h.
// ===========================================================================
template class COMMON_API std::list<EquipDesc>;
template class COMMON_API std::vector<EquipDesc>;

// weighted_vector<unsigned int>: retail exports nine members (ctors 06266310 / 06261100 /
// 06266330, dtor 06269110, operator= 062665c0 / 06266490, GetSumOfWeights 062666e0, Pick
// const/non-const 06266700).
template class COMMON_API weighted_vector<unsigned int>;
