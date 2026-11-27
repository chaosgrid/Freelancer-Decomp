// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_iustategraph.h"

#include "common_systemmanager.h"   // SystemManager (Watcher at +0x08, owner FAiTarget at +0x14FC)
#include "common_misc.h"
#include "AI/common_aisteer_internal.h" // CmnAiSteer::InlineFastLength
#include "common_net.h"             // Watchable

#include <cfloat>
#include <cstring>
#include <new>

// See common_iustategraph.h for the cluster writeup and per-function derivations.

namespace
{
    // The "-8 Watchable adjustment": a BaseWatcher stores its target's Watchable sub-object,
    // which IObjRW places at +0x08, so subtracting 8 recovers the object (and its primary
    // IObjInspect vtable). Retail spells it `if (p) p - 8 else 0` and re-tests the result
    // against 0, which only a literal 0x8 sentinel can fail.
    IObjInspect* RecoverObjInspect(Watchable const* Watched)
    {
        if (Watched == nullptr)
        {
            return nullptr;
        }
        unsigned char* const Raw = reinterpret_cast<unsigned char*>(const_cast<Watchable*>(Watched)) - 8;
        if (Raw == nullptr)
        {
            return nullptr;
        }
        return static_cast<IObjInspect*>(reinterpret_cast<IObjRW*>(Raw));
    }

    // SystemManager+0x14FC: the owner's own FAiTarget record (SystemManager::SelfTarget).
    FAiTarget* OwnerElementOf(SystemManager* Sys)
    {
        return &Sys->SelfTarget;
    }

    // Inlined identically in FUN_062e7820 and FUN_062e8280; FUN_062e6a80 (FAiTarget::GetCachedRange) is
    // the out-of-line variant that also hands the pair back. Slot +0x6C
    // (get_attitude_towards_symmetrical) returns 0 on success; on failure, or when either
    // interface is null, both cached values are 0.0f.
    void RefreshCachedAttitude(FAiTarget* Elem)
    {
        if (Elem->bAttitudeValid != 0)
        {
            return;
        }

        IObjInspect* const RefIface = Elem->AttitudeRefIface;
        IObjInspect* const Other = Elem->Iface;

        Elem->CachedAttitude = 0.0f;
        Elem->CachedReverseAttitude = 0.0f;

        if (RefIface != nullptr && Other != nullptr)
        {
            int const Result = RefIface->get_attitude_towards_symmetrical(
                Elem->CachedAttitude, Other, Elem->CachedReverseAttitude);
            if (Result != 0)
            {
                Elem->CachedAttitude = 0.0f;
                Elem->CachedReverseAttitude = 0.0f;
            }
        }
        else
        {
            Elem->CachedAttitude = 0.0f;
            Elem->CachedReverseAttitude = 0.0f;
        }

        Elem->bAttitudeValid = 1;
    }

    // The cached-position idiom, guarded by bPosValid. One get_physical_radius call fills both
    // CachedRadius (+0x18) and CachedPos (+0x0C).
    void RefreshCachedPosition(FAiTarget* Elem)
    {
        if (Elem->bPosValid == 0)
        {
            Elem->bPosValid = 1;
            Elem->Iface->get_physical_radius(Elem->CachedRadius, Elem->CachedPos);
        }
    }

    // The cached object-type flags, guarded by bFlagsValid. Slot +0x88.
    void RefreshCachedFlags(FAiTarget* Elem)
    {
        if (Elem->bFlagsValid == 0)
        {
            Elem->Iface->get_type(Elem->CachedFlags);
            Elem->bFlagsValid = 1;
        }
    }

    // .rdata:063a0790 == 0xBF19999A.
    float const kHostileAttitudeStrict = -0.6f;
    // .rdata:063a0794 == 0xBE4CCCCD.
    float const kHostileAttitudeLoose = -0.2f;
    // The object-type mask SystemManagerTimerTable::BuildAiTargetList scans and filters with
    // (0x7FFFFF minus bit 0x200).
    unsigned const kScanTypeMask = OBJ_SHIP_MASK | OBJ_SOLAR_NO_WAYPOINT_MASK;
    // The narrower "ship-ish" mask both scan loops re-test each candidate against.
    unsigned const kShipTypeMask = OBJ_SHIP_MASK;
    // .rdata:0x47C35000 -- SystemManagerTimerTable::FindClosestHostileTarget's seed distance.
    float const kMaxConsideredDistance = 100000.0f;
}

FAiTargetList* GetAiTargetList(SystemManager* Sys)
{
    // Every retail call site spells this out as `ADD reg,0x34`; FAiTargetList is
    // SystemManagerTimerTable.
    return &Sys->Timers;
}

// FUN_062ed0d0.
unsigned ElemVectorSize(void const* VectorHeader)
{
    unsigned char const* const H = static_cast<unsigned char const*>(VectorHeader);

    int const Begin = *reinterpret_cast<int const*>(H + 0x4);
    if (Begin == 0)
        return 0;

    int const Used = *reinterpret_cast<int const*>(H + 0x8);
    return static_cast<unsigned>((Used - Begin) / static_cast<int>(kElemStride));
}

// FUN_062ed100. Retail skips the copy for a null Dest but still advances both pointers; only
// matters if DestBegin starts null -- reproduced as-is.
void CopyElemRange(void const* SrcBegin, void const* SrcEnd, void* DestBegin)
{
    unsigned char const* Src = static_cast<unsigned char const*>(SrcBegin);
    unsigned char const* const End = static_cast<unsigned char const*>(SrcEnd);
    unsigned char* Dest = static_cast<unsigned char*>(DestBegin);

    while (Src != End)
    {
        if (Dest != nullptr)
            std::memcpy(Dest, Src, kElemStride);
        Src += kElemStride;
        Dest += kElemStride; // unconditional in retail, even on the null-Dest iteration
    }
}

// 0x0628f050, the compiler-generated destroy-a-range helper Ghidra labels
// `JustifyNode::Execute` (an ICF fold). FAiTarget has no destructor, so it does nothing per
// element. Kept as a real call so the two sites that use it stay legible.
void DestroyAiTargetRange(FAiTargetVector* Self, FAiTarget* First, FAiTarget* Last)
{
    (void)Self;
    (void)First;
    (void)Last;
}

// The inlined `erase(begin(), end())` (in FUN_062e7580). Its `rep movsd` shift loop is dead:
// the source cursor is seeded from End (0x062e75a0) and compared against End. What survives
// is the destroy-range call and `End = Begin`.
void ClearAiTargetVector(FAiTargetVector* Self)
{
    FAiTarget* const First = Self->Begin;
    DestroyAiTargetRange(Self, First, Self->End);
    Self->End = First;
}

// FUN_062e77e0. The same dead shift loop as above; unlike ClearAiTargetVector, retail makes no
// DestroyAiTargetRange call here.
void SystemManagerTimerTable::ResetAiTargetList()
{
    bBuilt = 0;
    Targets.End = Targets.Begin;
}

// FUN_062ed140: MSVC 7.0's `std::vector<FAiTarget>::insert(Where, Count, Value)` /
// `_Insert_n`, all three branches.
//
// Every element move in retail is a 0x22-dword `rep movsd` guarded by the same null-Dest test
// CopyElemRange has. No branch here can reach a null cursor once the allocation succeeded, so
// the memcpy calls below are unconditional.
void FAiTargetVector::InsertAiTargets(FAiTarget* Where, unsigned Count, FAiTarget const& Value)
{
    // Count is retail's size_type: every compare against it is unsigned.
    int const CapacityRoom = (int)(Cap - End);

    if ((unsigned)CapacityRoom < Count)
    {
        // --- Branch 1 (0x062ed172): reallocate. ---
        unsigned Size = 0;
        unsigned Growth = Count;
        if (Begin != nullptr)
        {
            Size = (unsigned)(End - Begin);
            // max(Size, Count).
            if (Count < Size)
            {
                Growth = Size;
            }
        }

        int const NewCapacity = (int)(Size + Growth);
        int const AllocCount = (NewCapacity >= 0) ? NewCapacity : 0;
        FAiTarget* const NewBuf = static_cast<FAiTarget*>(
            ::operator new(static_cast<std::size_t>(AllocCount) * kElemStride));

        // [Begin, Where) -> NewBuf
        FAiTarget* Dest = NewBuf;
        for (FAiTarget* Src = Begin; Src != Where; ++Src, ++Dest)
        {
            std::memcpy(Dest, Src, kElemStride);
        }
        // Count copies of Value
        for (unsigned Filled = 0; Filled < Count; ++Filled, ++Dest)
        {
            std::memcpy(Dest, &Value, kElemStride);
        }
        // [Where, End) -> after the inserted block
        for (FAiTarget* Src = Where; Src != End; ++Src, ++Dest)
        {
            std::memcpy(Dest, Src, kElemStride);
        }

        // Raw operator delete (0x06391d5a); retail does not call the destroy-range helper here.
        ::operator delete(Begin);

        Cap = NewBuf + NewCapacity;
        // Recomputed from the old Begin/End, still in place at this point.
        unsigned OldSize = 0;
        if (Begin != nullptr)
        {
            OldSize = (unsigned)(End - Begin);
        }
        Begin = NewBuf;
        End = NewBuf + (OldSize + Count);
        return;
    }

    unsigned const TailLength = (unsigned)(End - Where);

    if (TailLength < Count)
    {
        // --- Branch 2 (0x062ed2d3): in place, but more new elements than
        // existing tail elements. ---
        FAiTarget* Dest = Where + Count;
        for (FAiTarget* Src = Where; Src != End; ++Src, ++Dest)
        {
            std::memcpy(Dest, Src, kElemStride);
        }

        // Fill the (Count - TailLength) slots past the old End, then [Where, old End).
        FAiTarget* Fill = End;
        for (unsigned Extra = Count - TailLength; Extra != 0; --Extra, ++Fill)
        {
            std::memcpy(Fill, &Value, kElemStride);
        }
        for (FAiTarget* Slot = Where; Slot != End; ++Slot)
        {
            std::memcpy(Slot, &Value, kElemStride);
        }

        End += Count;
        return;
    }

    // --- Branch 3 (0x062ed3ae): in place, enough existing tail elements. ---
    if (Count == 0)
    {
        return;
    }

    // Move the last Count elements past End, then shift the remainder of the
    // tail right (backwards, to stay overlap-safe), then fill the hole.
    FAiTarget* Dest = End;
    for (FAiTarget* Src = End - Count; Src != End; ++Src, ++Dest)
    {
        std::memcpy(Dest, Src, kElemStride);
    }

    FAiTarget* Back = End;
    FAiTarget* Src = End - Count;
    while (Src != Where)
    {
        --Src;
        --Back;
        std::memcpy(Back, Src, kElemStride);
    }

    for (FAiTarget* Slot = Where; Slot != Where + Count; ++Slot)
    {
        std::memcpy(Slot, &Value, kElemStride);
    }

    End += Count;
}

// FUN_062e7580. See common_iustategraph.h.
FAiTargetVector* SystemManagerTimerTable::BuildAiTargetList()
{
    FAiTargetVector* const Vec = &Targets;

    // A non-persistent list is thrown away before anything else.
    if (bKeepCache == 0)
    {
        ClearAiTargetVector(Vec);
    }

    // A persistent list that has already been built stands.
    if (bBuilt != 0 && bKeepCache != 0)
    {
        return Vec;
    }

    SystemManager* const Sys = Owner;
    bBuilt = 1;

    // Zeroed, then filled as get_scanlist's out-parameter (through a LEA'd address, which is
    // why Ghidra's XREFs show only the zeroing).
    ScanList const* List = nullptr;

    IObjInspect* const OwnerObj = RecoverObjInspect(Sys->Watcher.target());
    if (OwnerObj == nullptr)
    {
        return Vec;
    }

    // Retail masks an uninitialised stack local (`Flags &= 0xF8`, `EquipFlags &= 0xE0000000`,
    // the inlined EquipStatus ctor clearing its bitfields). The only bit read here is 0x02,
    // which that mask clears, so a full zero-init is equivalent.
    EquipStatus Status;
    Status.Flags = 0;
    Status.Pad01 = 0;
    Status.Pad02 = 0;
    Status.Pad03 = 0;
    Status.EquipFlags = 0;
    OwnerObj->get_equipment_status(Status, ScannerEquipId);
    if ((Status.Flags & EquipStatusFlagDestroyed) != 0)
    {
        return Vec;
    }

    // Retail re-reads the watcher and, unlike the first read, calls get_scanlist through the
    // adjusted pointer without testing it.
    IObjInspect* const OwnerObjAgain = RecoverObjInspect(Sys->Watcher.target());
    if (OwnerObjAgain->get_scanlist(List, kScanTypeMask, bKeepCache != 0) != 0)
    {
        return Vec;
    }

    // Reserve exactly List->Count elements when the capacity (not the size) falls short.
    // Hand-rolled, not a vector::reserve call.
    int const Wanted = List->Count;
    int Capacity = 0;
    if (Vec->Begin != nullptr)
    {
        Capacity = static_cast<int>(Vec->Cap - Vec->Begin);
    }
    // Unsigned compare.
    if ((unsigned)Capacity < (unsigned)Wanted)
    {
        int const AllocCount = (Wanted >= 0) ? Wanted : 0;
        FAiTarget* const NewBuf = static_cast<FAiTarget*>(
            ::operator new(static_cast<std::size_t>(AllocCount) * kElemStride));

        CopyElemRange(Vec->Begin, Vec->End, NewBuf);
        DestroyAiTargetRange(Vec, Vec->Begin, Vec->End);
        ::operator delete(Vec->Begin);

        Vec->Cap = NewBuf + Wanted;
        // ElemVectorSize still sees the old Begin/End here.
        unsigned const OldSize = ElemVectorSize(Vec);
        Vec->End = NewBuf + OldSize;
        Vec->Begin = NewBuf;
    }

    // One pass over the scanner's result table; List->Count is re-read every iteration, as
    // retail does.
    for (int Index = 0; Index < List->Count; ++Index)
    {
        IObjInspect* const Candidate = RecoverObjInspect(List->Entries[Index].target());
        if (Candidate == nullptr)
        {
            continue;
        }

        // Retail builds this record on its own stack and leaves everything
        // except the six fields below indeterminate; the four guard bytes
        // make that harmless, but the port zero-fills to stay defined.
        FAiTarget Local;
        std::memset(&Local, 0, sizeof(Local));
        Local.AttitudeRefIface = RecoverObjInspect(Owner->Watcher.target());
        Local.Iface = Candidate;
        Local.bAttitudeValid = 0;
        Local.bFlagsValid = 0;
        Local.bPosValid = 0;
        Local.bCornersValid = 0;

        Candidate->get_type(Local.CachedFlags);
        Local.bFlagsValid = 1;   // set before the filter branch in retail, not after
        if ((Local.CachedFlags & kScanTypeMask) == 0)
        {
            continue;
        }

        Vec->InsertAiTargets(Vec->End, 1, Local);
    }

    return Vec;
}

// FUN_062e7820, the body of IBehaviorManager::get_closest_trailing_ship.
float SystemManagerTimerTable::GetClosestTrailingShipDistance()
{
    // Returned unchanged when nothing qualifies.
    float Best = FLT_MAX;

    // The list is reached through the owner (Owner+0x34), not through `this`.
    FAiTargetVector* const Vec = Owner->Timers.BuildAiTargetList();
    SystemManager* const Sys = Owner;
    FAiTarget* const OwnerElem = OwnerElementOf(Sys);

    for (FAiTarget* Elem = Vec->Begin; Elem != Vec->End; ++Elem)
    {
        RefreshCachedFlags(Elem);
        if ((Elem->CachedFlags & kShipTypeMask) == 0)
        {
            continue;
        }

        if (Elem->Iface->is_player())
        {
            continue;
        }

        // get_target (+0x074).
        IObjRW* CandidateTarget = nullptr;
        Elem->Iface->get_target(CandidateTarget);
        if (CandidateTarget == nullptr)
        {
            continue;
        }

        // Equal ids mean the candidate is currently targeting us.
        unsigned const OwnerId = OwnerElem->Iface->get_id();
        unsigned const CandidateTargetId = static_cast<IObjInspect*>(CandidateTarget)->get_id();
        if (CandidateTargetId != OwnerId)
        {
            continue;
        }

        if (!IsTargetCoAlignedAndAhead(Elem, OwnerElem, 0.0f))
        {
            continue;
        }

        RefreshCachedAttitude(Elem);
        if (!(Elem->CachedAttitude <= kHostileAttitudeLoose))
        {
            continue;
        }

        // Retail's guard here is Sys+0x157A, OwnerElem->bPosValid.
        RefreshCachedPosition(OwnerElem);
        RefreshCachedPosition(Elem);

        // Retail's inlined 0x5F3759DF Newton step: everything stays on the x87 stack except
        // the bit-hack input; 0.5f at DAT_0639c13c, 1.5f at DAT_0639e6d0.
        double const Dx = (double)Elem->CachedPos.x - OwnerElem->CachedPos.x;
        double const Dy = (double)Elem->CachedPos.y - OwnerElem->CachedPos.y;
        double const Dz = (double)Elem->CachedPos.z - OwnerElem->CachedPos.z;
        double const DistanceSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        double const Distance = CmnAiSteer::InlineFastLength(DistanceSq);

        // Only strictly less is taken.
        if (Distance < Best)
        {
            Best = (float)Distance;
        }
    }

    return Best;
}

// FUN_062e8280. A method of FAiTargetList itself: SystemManagerTimerTable::BuildAiTargetList gets `this` unchanged, no
// +0x34 adjustment.
IObjInspect* SystemManagerTimerTable::FindClosestHostileTarget(float MaxClearance)
{
    IObjInspect* BestIface = nullptr;
    // Anything farther than 100000 is never returned.
    float BestDistance = kMaxConsideredDistance;

    // The owner's own position is fetched before the list is (re)built.
    IObjInspect* const OwnerObj = RecoverObjInspect(Owner->Watcher.target());
    Vector OwnerPosition;
    GetIfacePositionOrZero(&OwnerPosition, OwnerObj);

    FAiTargetVector* const Vec = BuildAiTargetList();
    FAiTarget* const OwnerElem = OwnerElementOf(Owner);

    for (FAiTarget* Elem = Vec->Begin; Elem != Vec->End; ++Elem)
    {
        RefreshCachedFlags(Elem);
        if ((Elem->CachedFlags & kShipTypeMask) == 0)
        {
            continue;
        }

        RefreshCachedAttitude(Elem);
        if (!(Elem->CachedAttitude <= kHostileAttitudeStrict))
        {
            continue;
        }

        // A negative MaxClearance skips the clearance gate ("nearest hostile at any range").
        if (!(MaxClearance < 0.0f))
        {
            if (!(GetInterceptClearance(OwnerElem, Elem) < MaxClearance))
            {
                continue;
            }
        }

        RefreshCachedPosition(Elem);

        // Distance3 (FUN_062e4f20). The unrounded result is compared (strictly less) and
        // rounded to float only when taken.
        double const Distance = Distance3(&OwnerPosition, &Elem->CachedPos);
        if (Distance < BestDistance)
        {
            BestDistance = (float)Distance;
            BestIface = Elem->Iface;
        }
    }

    return BestIface;
}
