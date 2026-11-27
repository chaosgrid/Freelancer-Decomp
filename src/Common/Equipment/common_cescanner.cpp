// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ScanList, CEScanner. Declared in common_cequip.h; helpers shared with the sibling TUs are in
// common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"

#include "common_cobject.h"
#include "common_cobjdb.h"   // IObjDB::GetBucket / GetNext (ScanList::add_in_range)
#include "common_csys.h"
#include "common_fuses.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_physys.h"
#include "common_spatial.h"  // ISpatialPartition (ScanList::add_in_range bucket query)
#include "common_timing.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "ivp_physics/ivp_real_object.h"
#include "ivp_physics/ivp_controller.h"

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Equipment/common_cequip_internal.h"

using namespace CmnCequip;

// ============================================================
// CEScanner -- scanner equipment
// ============================================================

namespace
{
    const float kScannerOne = 1.0f;

    // IObjRW's Watchable base lives at +8; every ScanList member converts null-preservingly.
    Watchable const* ScannerWatchableOf(IObjRW* Obj)
    {
        return Obj;   // the IObjRW -> Watchable base conversion keeps null as null
    }
}

// ============================================================
// ScanList
// ============================================================

// FUN_062c2610 (entries by FUN_062c2920(0)).
ScanList::ScanList()
    : Count(0)
    , Capacity(0x100)
{
}

// FUN_062948b0: the BaseWatcher copy ctor inlined per entry. A target of 8 (a null IObjRW
// + 8) counts as none.
ScanList::ScanList(ScanList const& Other)
    : Count(Other.Count)
    , Capacity(Other.Capacity)
{
    for (int Index = 0; Index < 256; ++Index)
    {
        Watchable const* SrcWatched = Other.Entries[Index].target();
        if (SrcWatched != nullptr && SrcWatched != reinterpret_cast<Watchable const*>(8))
            Entries[Index].set(SrcWatched);
    }
}

// FUN_06294960. Not BaseWatcher::operator= (0x06280ee0, a raw two-dword copy with no chain
// maintenance), which retail does not call here.
ScanList& ScanList::operator=(ScanList const& Other)
{
    for (int Index = 0; Index < 256; ++Index)
    {
        Watchable const* SrcWatched = Other.Entries[Index].target();
        if (SrcWatched == reinterpret_cast<Watchable const*>(8))
            SrcWatched = nullptr;
        Entries[Index].set(SrcWatched);
    }
    Count = Other.Count;
    Capacity = Other.Capacity;
    return *this;
}

// FUN_062c2660: only the bookkeeping; the entries keep their watch links until overwritten.
void ScanList::reset()
{
    Count = 0;
    Capacity = 0x100;
}

// FUN_062c2680
bool ScanList::add(IObjRW* Obj)
{
    if (Count >= Capacity)
        return false;
    int Index = Count;
    ++Count;
    Entries[Index].set(ScannerWatchableOf(Obj));
    return true;
}

// FUN_062c26c0: erase-remove compaction over [0, Count); entries beyond keep their old links.
void ScanList::remove(IObjRW* Obj)
{
    int WriteIndex = 0;
    for (int ReadIndex = 0; ReadIndex < Count; ++ReadIndex)
    {
        Watchable const* Watched = Entries[ReadIndex].target();
        IObjRW* WatchedOwner = (Watched != nullptr)
            ? const_cast<IObjRW*>(static_cast<IObjRW const*>(Watched))
            : nullptr;
        if (WatchedOwner != Obj)
        {
            // Rebuilt from the owner, so a watcher on 8 (owner null) is written back as null.
            Entries[WriteIndex].set(ScannerWatchableOf(WatchedOwner));
            ++WriteIndex;
        }
    }
    Count = WriteIndex;
}

// FUN_062c2720 (follows the listing; Ghidra's output is wrecked by the __chkstk probe of the
// 0x2030-byte frame). Two phases, each candidate gated through Filt->add_obj (no null check on
// Filt): a spatial-bucket sphere query of up to 0x800 hits for the 0xfdff types, then a
// get-next walk for the 0xffff0200 types tested strictly closer than Range. A full list ends
// phase 1 but returns from phase 2. Returns the number appended.
int ScanList::add_in_range(Vector const& Pos, float Range, IObjDB* Db, unsigned TypeMask,
                           Filter* Filt)
{
    float RangeSq = Range * Range;
    int Added = 0;

    if (TypeMask & OBJ_SOLAR_NO_WAYPOINT_MASK)
    {
        ISpatialPartition* Bucket = Db->GetBucket(OBJ_SOLAR_NO_WAYPOINT_MASK);
        if (Bucket != nullptr)
        {
            Geometry::Sphere Sphere;
            Sphere.Center = Pos;
            Sphere.Radius = Range;
            FSpatialQueryResult Query;
            Query.Capacity = 0x800;
            Query.Count = 0;

            int Result = Bucket->QuerySphere(&Sphere, &Query);
            if (Result != 0 && Query.Count != 0)
            {
                for (unsigned Index = 0; Index < Query.Count; ++Index)
                {
                    IObjRW* Candidate = static_cast<IObjRW*>(Query.Items[Index]);
                    if (Count >= Capacity)
                        break;
                    if (Filt->add_obj(Candidate))
                    {
                        add(Candidate);
                        ++Added;
                    }
                }
            }
        }
    }

    struct FIterState
    {
        unsigned Unused0;
        unsigned Unused1;
        unsigned Mask;
    } IterState = { 0, 0, TypeMask & ~OBJ_SOLAR_NO_WAYPOINT_MASK };

    IObjRW* Candidate = Db->GetNext(&IterState);
    while (Candidate != nullptr)
    {
        Vector const* CandPos = &Candidate->get_position();

        // Unrounded, summed (x^2 + y^2) + z^2; NaN is skipped.
        double Dx = (double)CandPos->x - Pos.x;
        double Dy = (double)CandPos->y - Pos.y;
        double Dz = (double)CandPos->z - Pos.z;
        double DistSq = (Dx * Dx + Dy * Dy) + Dz * Dz;
        if (DistSq < RangeSq)
        {
            if (Count >= Capacity)
                return Added;
            if (Filt->add_obj(Candidate))
            {
                add(Candidate);
                ++Added;
            }
        }

        Candidate = Db->GetNext(&IterState);
    }

    return Added;
}

namespace
{
    // Inlined twice: a static CSolar gets the restricted mask.
    bool OwnerIsStaticSolar(CEqObj const* Owner)
    {
        if (Owner == nullptr)
            return false;
        if ((Owner->ObjectClass & CObject::ClassSolar) != CObject::ClassSolar)
            return false;
        return !static_cast<CSolar const*>(Owner)->bDynamic;
    }
}

// FUN_0629f4e0
CEScanner::CEScanner(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Scanner const* ArchObj,
                     bool bMissionFlag)
    : CInternalEquip(EquipTypeScanner, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    this->bMissionFlag = bMissionFlag;
    CargoVecBegin = nullptr;
    CargoVecEnd = nullptr;
    CargoVecCapacityEnd = nullptr;
    CachedTypeMask = 0;
    CEquip::Activate(true);
}

// FUN_06294be0
CEScanner::CEScanner(CEScanner const& Other)
    : CInternalEquip(Other)
    , ScanList::Filter(Other)
    , Scans(Other.Scans)
    , bMissionFlag(Other.bMissionFlag)
{
    // The VC6 vector<EquipDesc> copy: exact-fit, allocated even for an empty source.
    int SrcSize = (Other.CargoVecBegin == nullptr) ? 0 : (int)(Other.CargoVecEnd - Other.CargoVecBegin);
    if (SrcSize < 0)
        SrcSize = 0;
    CargoVecBegin = new EquipDesc[(unsigned)SrcSize];
    EquipDesc* Dest = CargoVecBegin;
    for (EquipDesc const* Src = Other.CargoVecBegin; Src != Other.CargoVecEnd; ++Src, ++Dest)
        *Dest = *Src;
    CargoVecEnd = Dest;
    CargoVecCapacityEnd = Dest;

    CachedTypeMask = Other.CachedTypeMask;
}

// RVA 0x3f590
CEScanner::~CEScanner()
{
    CEquip::Destroy();

    delete[] CargoVecBegin;
    CargoVecBegin = nullptr;
    CargoVecEnd = nullptr;
    CargoVecCapacityEnd = nullptr;
}

// FUN_0629f630
void CEScanner::scan_for_types(unsigned TypeMask)
{
    if (!IsFunctioning())
        return;
    if (Owner->ObjectDb == nullptr)
        return;

    unsigned EffectiveMask = TypeMask;
    if (OwnerIsStaticSolar(Owner))
        EffectiveMask = TypeMask & ~OBJ_SOLAR_NO_WAYPOINT_MASK & ~OBJ_ASTEROID;

    unsigned const NewTypes = EffectiveMask & ~CachedTypeMask;

    // Unrounded until the float range. Even a zero type mask queries Db.
    double const Interference = Owner->get_scanner_interference();
    float const Range = (float)((kScannerOne - Interference) * ScannerArch()->RadarRange);

    Vector const& OwnerPos = Owner->WorldPosition;

    Scans.add_in_range(OwnerPos, Range, Owner->ObjectDb, NewTypes, this);

    CachedTypeMask |= NewTypes;
}

// FUN_0629f6d0
void CEScanner::clear_cache()
{
    Scans.reset();
    CachedTypeMask = 0;
}

// RVA 0x3f6f0: the scan is redone every tick; Flags bit 4 skips it. IsFunctioning is tested
// twice, as in retail.
bool CEScanner::Update(float Dt, unsigned Flags)
{
    Scans.reset();
    CachedTypeMask = 0;

    if (!IsFunctioning())
        goto Done;

    if ((Flags & 4u) != 0)
        goto Done;

    if (!IsFunctioning())
        goto Done;

    if (Owner->ObjectDb == nullptr)
        goto Done;

    {
        unsigned EffectiveMask = 0x357ffdffu;
        if (OwnerIsStaticSolar(Owner))
            EffectiveMask = 0x157f0000u;

        unsigned NewTypes = EffectiveMask & ~CachedTypeMask;

        double const Interference = Owner->get_scanner_interference();
        float const Range = (float)((kScannerOne - Interference) * ScannerArch()->RadarRange);

        Vector const& OwnerPos = Owner->WorldPosition;

        Scans.add_in_range(OwnerPos, Range, Owner->ObjectDb, NewTypes, this);

        CachedTypeMask |= NewTypes;
    }

Done:
    return CEquip::Update(Dt, Flags);
}

// FUN_0629f7c0 (the Filter base at +0x20). Distance is re-checked only when the target has
// stronger interference than the owner.
bool CEScanner::add_obj(IObjRW* Obj)
{
    if (Obj->get_id() == Owner->Id)
        return false;
    if (!Obj->is_targetable())
        return false;

    float const TargetInterference = Obj->get_scanner_interference();
    double const OwnerInterference = Owner->get_scanner_interference();
    // NaN accepts; add_in_range already applied the owner's range.
    if (!(OwnerInterference < TargetInterference))
        return true;

    float const Range = (float)(((double)(kScannerOne) - TargetInterference) *
                                          ScannerArch()->RadarRange);
    Vector const& ObjPos = Obj->get_position();
    Vector const& OwnerPos = Owner->WorldPosition;
    double const Dx = (double)(ObjPos.x) - OwnerPos.x;
    double const Dy = (double)(ObjPos.y) - OwnerPos.y;
    double const Dz = (double)(ObjPos.z) - OwnerPos.z;
    double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
    // NaN accepts.
    return !((double)(Range) * Range < DistSq);
}

// FUN_0629f880
float CEScanner::GetRadarRange() const
{
    double const Interference = Owner->get_scanner_interference();
    return (float)((kScannerOne - Interference) * ScannerArch()->RadarRange);
}

// FUN_0629f8a0
float CEScanner::GetCargoScanRange() const
{
    double const Interference = Owner->get_scanner_interference();
    return (float)((kScannerOne - Interference) * ScannerArch()->CargoScanRange);
}

// FUN_0629f8c0: the target's equipment descriptors, verbatim.
CEScanner::ScanResult CEScanner::ScanCargo(CEqObj* Target)
{
    // The previous result is cleared first; capacity is kept even on failure.
    CargoVecEnd = CargoVecBegin;
    if (Target == nullptr)
        return ScanNoTarget;

    // The stronger interference wins (the target's rounded to float, the owner's not).
    float const TargetInterference = Target->get_scanner_interference();
    double Interference = (double)(Owner->get_scanner_interference());
    if (TargetInterference > Interference)
        Interference = TargetInterference;

    Archetype::Scanner const* const Scanner = ScannerArch();
    double const Range = (kScannerOne - Interference) * Scanner->CargoScanRange;
    Vector const& TargetPos = Target->WorldPosition;
    Vector const& OwnerPos = Owner->WorldPosition;
    double const Dx = (double)(TargetPos.x) - OwnerPos.x;
    double const Dy = (double)(TargetPos.y) - OwnerPos.y;
    double const Dz = (double)(TargetPos.z) - OwnerPos.z;
    double const DistSq = (Dy * Dy + Dz * Dz) + Dx * Dx;
    // NaN continues.
    if (Range * Range < DistSq)
        return ScanTooFar;

    // NaN fails.
    float const Threshold = Scanner->PowerThreshold;
    if (!(Threshold <= Owner->Power))
        return ScanNotEnoughPower;
    Owner->Power = (float)((double)(Owner->Power) - Threshold);

    CEquipTraverser Walk(-1);
    CEquip* Equip = Target->EquipManager.Traverse(Walk);
    while (Equip != nullptr)
    {
        EquipDesc Desc;
        if (!Equip->GetEquipDesc(Desc))
        {
            Equip = Target->EquipManager.Traverse(Walk);
            continue;
        }

        // The inlined VC6 vector insert at End; growth is size + max(size, 1).
        if (CargoVecEnd == CargoVecCapacityEnd)
        {
            unsigned const CurSize = CargoVecBegin == nullptr ? 0 :
                (unsigned)(CargoVecEnd - CargoVecBegin);
            unsigned const NewCapacity = CurSize + (CurSize > 1 ? CurSize : 1);
            EquipDesc* const NewBuf = new EquipDesc[NewCapacity];
            for (unsigned Index = 0; Index < CurSize; ++Index)
                NewBuf[Index] = CargoVecBegin[Index];
            NewBuf[CurSize] = Desc;
            delete[] CargoVecBegin;
            CargoVecBegin = NewBuf;
            CargoVecEnd = NewBuf + CurSize + 1;
            CargoVecCapacityEnd = NewBuf + NewCapacity;
        }
        else
        {
            *CargoVecEnd = Desc;
            ++CargoVecEnd;
        }

        Equip = Target->EquipManager.Traverse(Walk);
    }

    return ScanComplete;
}

// RVA 0x3fb90 (ICF-shared).
Archetype::Scanner const* CEScanner::ScannerArch() const
{
    return static_cast<Archetype::Scanner const*>(Arch);
}

// RVA 0x3fba0, shared by both overloads.
CEScanner* __cdecl CEScanner::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeScanner)
        return nullptr;
    return static_cast<CEScanner*>(Equip);
}

CEScanner const* __cdecl CEScanner::cast(CEquip const* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeScanner)
        return nullptr;
    return static_cast<CEScanner const*>(Equip);
}
