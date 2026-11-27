// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- SystemManagerGunGroupManager (SystemManager+0x12C): construction, slot timing,
// the hardpoint resolver family, the auto-turret target scan and the per-tick timers. The
// fire passes are in common_aifirecontrol.cpp; the class is declared in common_systemmanager.h.

#include "common_systemmanager.h"
#include "AI/common_systemmanager_internal.h" // CmnSysMgr::IsWatcherLive / WatchedObject
#include "common_aisteer.h"   // FAiTarget, FAiTarget::GetCachedRange
#include "common_misc.h"      // SUBOBJ_ID_NONE
#include "common_iface.h"     // IObjInspect, IObjRW, ObjRwFromWatchable
#include "common_iustategraph.h" // SystemManagerTimerTable::BuildAiTargetList, GetAiTargetList (ScanAutoTurretTargets)
#include "common_cobject.h"    // CEqObj::EquipManager / get_behavior_interface (ScanAutoTurretTargets)
#include "common_cequip.h"     // CELauncher::cast / GetBarrelPosWS (ScanAutoTurretTargets)

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>

using CmnSysMgr::IsWatcherLive;
using CmnSysMgr::WatchedObject;

// ============================================================================
// SystemManagerGunGroupManager -- FUN_062e9330.
// ============================================================================
namespace
{
    // The ctor's per-slot loop (retail's puVar1 walks each record's +0x03 byte).
    void InitGunGroupArray(FGunGroupSlot* Slots)
    {
        for (int i = 0; i < 0x40; ++i)
        {
            FGunGroupSlot& Slot = Slots[i];
            Slot.bAutoTurret = 0;
            Slot.bSeekerMunition = 0;
            Slot.bFireTick = 0;
            Slot.BurstTime = 3.0f;
            Slot.NoBurstTime = 1.5f;
            Slot.FireInterval = 0.5f;
            Slot.Phase = 0.0f;                                  // retail stores int 0
            Slot.Field_0x1C = 0;
        }
    }
}

void SystemManagerGunGroupManager::Construct()
{
    std::memset(this, 0, sizeof(*this));

    // The target watcher at +0x08/+0x0C (zeroed), +0x28 = 0, ScanPeriod = 1.0f.
    ScanPeriod = 1.0f;

    InitGunGroupArray(Guns);
    InitGunGroupArray(MineDroppers);

    // LineOfFireCache[16]: {InUse=0, TargetId=0, HardpointId=0, Age=0, RefreshTimer=0,
    // NeedsRefresh=1, bBlocked=0}.
    for (int i = 0; i < 0x10; ++i)
    {
        FLauncherFireCacheSlot& Slot = LineOfFireCache[i];
        Slot.InUse = 0;
        Slot.TargetId = 0;
        Slot.HardpointId = 0;
        Slot.NeedsRefresh = 1;
        Slot.Age = 0.0f;
        Slot.RefreshTimer = 0.0f;
        Slot.bBlocked = 0;
    }

    Owner = nullptr;
    bEnabled = 1;

    GunFireIntervalTime = 0.5f;
    AutoTurretIntervalTime = 0.5f;
    MissileLaunchIntervalVariance = 0.5f;
    GunFireNoBurstIntervalTime = 2.0f;
    AutoTurretNoBurstIntervalTime = 2.0f;

    Field_0x10 = 0;
    CruiseHeldTime = 0.0f;
    GunCount = 0;
    bFireAllGuns = 0;

    Field_0x108C = 0.09817477f;                         // 0x3dc90fdb == PI/32
    GunFireBurstIntervalVariance = 0.3f;
    AutoTurretBurstIntervalVariance = 0.3f;
    Field_0x107C = 0.0f;
    MissileLaunchAllowOutOfRange = 0.0f;
    WideConeAngle = 0.09817477f;

    AimRefreshTimer = 0.0f;
    Field_0x834 = -1;

    GunFireIntervalVariance = 0.2f;
    GunFireBurstIntervalTime = 4.0f;
    AutoTurretBurstIntervalTime = 4.0f;
    MissileLaunchIntervalTime = 3.0f;
    MissileLaunchConeAngle = 3.14159274f;
    MineLaunchInterval = 5.0f;
    MineLaunchConeAngle = 0.3926991f;
    GunFireAccuracyConeAngle = 0.05f;
    GunRange = 1100.0f;
    MissileLaunchRange = 800.0f;
}

namespace
{
    // rand() * float 2^-15 (.rdata:063997d0), kept unrounded by every user below.
    double RandUnit()
    {
        return (double)rand() * (double)3.0517578125e-05f;
    }

    // The jitter term FUN_062e9500 repeats five times: Neg = -Half is stored as a float, the
    // rest stays unrounded until the final store.
    float JitterAround(float Half, float Scale)
    {
        float const Neg = -Half;
        double const Unit = RandUnit();
        return (float)((((double)Half - Neg) * Unit + Neg) * Scale + Scale);
    }
}

// FUN_062e9500; Slot is a Guns or MineDroppers record. Every term stays on the x87 stack until
// its store.
void SystemManagerGunGroupManager::RandomizeSlotTiming(FGunGroupSlot* Slot)
{
    int const Case = Slot->MunitionKind;

    bool HaveResult = true;
    float Result = 0.0f;
    switch (Case)
    {
    case FGunGroupSlot::KindPlain:
        if (Slot->bAutoTurret == 0)
        {
            Slot->BurstTime = JitterAround(GunFireBurstIntervalVariance, GunFireBurstIntervalTime);
            Slot->NoBurstTime = JitterAround(GunFireBurstIntervalVariance, GunFireNoBurstIntervalTime);
            Result = JitterAround(GunFireIntervalVariance, GunFireIntervalTime);
        }
        else
        {
            // Doubles .rdata:0639df88 / 0639fe28.
            Slot->BurstTime = JitterAround(AutoTurretBurstIntervalVariance, AutoTurretBurstIntervalTime);
            Slot->NoBurstTime = JitterAround(AutoTurretBurstIntervalVariance, AutoTurretNoBurstIntervalTime);
            Result = (float)((RandUnit() * 0.5 + 0.75) * AutoTurretIntervalTime);
        }
        break;
    case FGunGroupSlot::KindSeeker:
        Result = JitterAround(MissileLaunchIntervalVariance, MissileLaunchIntervalTime);
        break;
    case FGunGroupSlot::KindTorpedo:
    case FGunGroupSlot::KindMineDropper:
        // The doubles at .rdata:063a07f0 / 063a07e8 are (double)0.40000004f and (double)0.8f.
        Slot->BurstTime = 0.0f;
        Slot->NoBurstTime = 0.0f;
        Result = (float)((RandUnit() * (double)0.40000004f + (double)0.8f) * MissileLaunchIntervalTime);
        break;
    case FGunGroupSlot::KindCruiseDisruptor:
        // Owner+0x176c: anti_cruise_missile_interval_time.
        Slot->FireInterval = Owner->PersonalityData.MissileParams.AntiCruiseMissileIntervalTime;
        HaveResult = false;
        break;
    default:
        HaveResult = false;
        break;
    }

    if (HaveResult)
        Slot->FireInterval = Result;

    // Phase = S * Unit - S, in [-S, 0).
    float const Neg = -Slot->FireInterval;
    double const Unit = RandUnit();
    Slot->Phase = (float)(-(double)Neg * Unit + Neg);
}

// FUN_062e9770. Ghidra mis-scopes the stack slot reused as every get_equipment_val's
// out-parameter as the iteration counter and as param_1; it is one reused scratch slot.
int SystemManagerGunGroupManager::PopulateSlots(unsigned short const* SourceIds, int MaxCount,
                                                FGunGroupSlot* Slots, bool bMineDroppers)
{
    if (SourceIds == nullptr || Slots == nullptr)
        return 0;

    int Count = 0;
    if (MaxCount > 0)
    {
        FGunGroupSlot* Slot = Slots;
        do
        {
            unsigned short const Id = SourceIds[Count];
            if (Id == SUBOBJ_ID_NONE)
                break;
            Slot->HardpointId = Id;
            Slot->bAutoTurret = 0;
            Slot->MunitionKind = FGunGroupSlot::KindUnclassified;
            ++Count;
            ++Slot;
        } while (Count < MaxCount);
    }

    if (Count == 0)
        return 0;

    // One EquipStatus built once (retail only clears its flag bits) and reused by every
    // get_equipment_status call below; the callee only rewrites those bits.
    EquipStatus Status{};

    // No own-ship null tests below, as in retail.
    unsigned short const* SourceCursor = SourceIds;
    FGunGroupSlot* Slot = Slots;
    for (int Remaining = Count; Remaining != 0; --Remaining)
    {
        if (bMineDroppers)
        {
            Slot->MunitionKind = FGunGroupSlot::KindMineDropper;
        }
        else
        {
            EquipmentVal Val{};
            int const Result = OwnShip()->get_equipment_val(Val, Slot->HardpointId, EquipValMunitionKind, 0.0f);
            if (Result == 0)
            {
                int Classify;
                std::memcpy(&Classify, &Val, sizeof(Classify));
                int NewKind = -1;
                switch (Classify)
                {
                case kMunitionKindPlain: NewKind = FGunGroupSlot::KindPlain; break;
                case kMunitionKindSeeker: NewKind = FGunGroupSlot::KindSeeker; break;
                case kMunitionKindTorpedo: NewKind = FGunGroupSlot::KindTorpedo; break;
                case kMunitionKindCruiseDisruptor: NewKind = FGunGroupSlot::KindCruiseDisruptor; break;
                default: break; // retail's own jump table has exactly 4 entries; anything else leaves the field untouched
                }
                if (NewKind != -1)
                    Slot->MunitionKind = NewKind;
            }
        }

        {
            // Only the low byte is tested.
            EquipmentVal Val{};
            int const Result = OwnShip()->get_equipment_val(Val, Slot->HardpointId, EquipValAutoTurret, 0.0f);
            unsigned char LowByte;
            std::memcpy(&LowByte, &Val, sizeof(LowByte));
            if (Result == 0 && LowByte != 0)
                Slot->bAutoTurret = 1;
        }

        {
            // Its low byte is stored.
            EquipmentVal Val{};
            int const Result = OwnShip()->get_equipment_val(Val, Slot->HardpointId, EquipValSeeker, 0.0f);
            if (Result == 0)
                std::memcpy(&Slot->bSeekerMunition, &Val, sizeof(Slot->bSeekerMunition));
        }

        // A non-player ship whose Status bit 0 is clear toggles the item on.
        if (OwnShip()->get_equipment_status(Status, Slot->HardpointId) == 0 &&
            !OwnShip()->is_player() &&
            (Status.Flags & EquipStatusFlagActive) == 0)
        {
            OwnShip()->toggle_item(SourceCursor, true, -1);
        }

        ++SourceCursor;
        ++Slot;
    }

    return Count;
}

// FUN_062e9990. A failed find_equipment leaves that count at 0. False only without an own
// ship.
bool SystemManagerGunGroupManager::ResolveHardpoints()
{
    // Only this first resolution is null-tested, as in retail.
    if (OwnShip() == nullptr)
        return false;

    GunCount = 0;
    {
        unsigned short GunIds[0x40];
        int const Result = OwnShip()->find_equipment(GunIds, 0x40, EquipTypeGun);
        if (Result == 0)
            GunCount = PopulateSlots(GunIds, 0x40, Guns, false);
    }

    MineDropperCount = 0;
    {
        unsigned short MineDropperIds[0x40];
        int const Result = OwnShip()->find_equipment(MineDropperIds, 0x40, EquipTypeMineDropper);
        if (Result == 0)
            MineDropperCount = PopulateSlots(MineDropperIds, 0x40, MineDroppers, true);
    }

    return true;
}

// FUN_062e9a60, RET 4 (Ghidra's "unaff_ESI" is the stack argument). True even when
// find_equipment fails.
bool SystemManagerGunGroupManager::SetShieldActive(int Mode)
{
    if (OwnShip() == nullptr)
        return false;

    // The re-resolved owners are not null-tested, as in retail. Every retail caller passes 0
    // or 1 for Mode.
    unsigned short HardpointId = SUBOBJ_ID_NONE;
    int const Result = OwnShip()->find_equipment(&HardpointId, 1, EquipTypeShield);
    if (Result == 0)
    {
        OwnShip()->toggle_item(&HardpointId, Mode != 0, -1);
    }

    return true;
}

// FUN_062e9ae0, RET 4. The own ship is called through untested; GunCount is compared unsigned
// and re-read after every call, as in retail.
bool SystemManagerGunGroupManager::Retarget(IObjRW* NewTarget)
{
    if (NewTarget == nullptr)
        return false;

    BaseWatcher& Watcher = TargetWatcher();

    if (NewTarget == CurrentTarget())
        return true;

    Watcher.set(static_cast<Watchable const*>(NewTarget));

    // Retail queries get_type on a live watch and discards the result.
    if (IsWatcherLive(Watcher))
    {
        unsigned Scratch = 0;
        (void)CurrentTarget()->get_type(Scratch);
    }

    OwnShip()->set_target(CurrentTarget(), SUBOBJ_ID_NONE, -1);

    // A player ship stops here.
    if (OwnShip()->is_player())
        return true;

    for (unsigned Index = 0; Index < (unsigned)GunCount; ++Index)
        OwnShip()->set_gun_target(&Guns[Index].HardpointId, NewTarget, SUBOBJ_ID_NONE, -1);
    return true;
}

// FUN_062e9c00. No null test on the ship, as in retail; GunCount is compared unsigned and
// re-read after every call.
void SystemManagerGunGroupManager::ClearGunTargets()
{
    for (unsigned Index = 0; Index < (unsigned)GunCount; ++Index)
        OwnShip()->set_gun_target(&Guns[Index].HardpointId, nullptr, SUBOBJ_ID_NONE, -1);
}

// FUN_062e9c50, RET 4. The only caller is IBehaviorManager::UpdateBehaviorTick
// (.text:062d2ce9), on ticks where our own ship is not the player.
void SystemManagerGunGroupManager::ScanAutoTurretTargets(float Dt)
{
    // The stored timer is rounded, but the compare uses the unrounded sum; the scan runs only
    // when it strictly exceeds the period.
    double const SumD = (double)Dt + (double)ScanTimer;
    float const NewTimer = (float)SumD;
    ScanTimer = NewTimer;
    if (!(SumD > (double)ScanPeriod))
        return;

    // fmod of the stored (rounded) sum.
    ScanTimer = (float)std::fmod((double)NewTimer, (double)ScanPeriod);

    // 1.2 is double DAT_0639fa60; rounded to float once.
    double const Range = (double)GunRange * 1.2;
    float const RangeSq = (float)(Range * Range);

    SystemManager* const Sys = Owner;
    IObjInspect const* const OwnIface = Sys->SelfTarget.Iface;

    // When our ship is the player, remember its own target and subtarget.
    unsigned short PlayerSubTarget = SUBOBJ_ID_NONE;
    IObjRW* PlayerTarget = nullptr;
    if (OwnIface->is_player())
    {
        IObjRW* Queried = nullptr;
        OwnIface->get_target(Queried);
        PlayerTarget = Queried;
        if (Queried != nullptr)
        {
            // The out-slot is the Dt argument slot, which get_target has just filled with the
            // target pointer: its low word is what stays if get_subtarget writes nothing.
            unsigned short QueriedSub = (unsigned short)(std::uintptr_t)Queried;
            OwnIface->get_subtarget(QueriedSub);
            PlayerSubTarget = QueriedSub;
        }
    }

    // The self record's lazily cached type flags; the guard is raised after the call.
    FAiTarget* const SelfRecord = &Sys->SelfTarget;
    if (SelfRecord->bFlagsValid == 0)
    {
        SelfRecord->Iface->get_type(SelfRecord->CachedFlags);
        SelfRecord->bFlagsValid = 1;
    }

    bool const bSelfMayEngage = (SelfRecord->CachedFlags & (OBJ_DOCKING_RING | OBJ_TRADELANE_RING)) == 0;
    bool bAnyHostile = false;

    // Unsigned index, GunCount re-read on every pass.
    for (unsigned SlotIndex = 0; SlotIndex < (unsigned)GunCount; ++SlotIndex)
    {
        FGunGroupSlot& Slot = Guns[SlotIndex];

        if (Slot.bAutoTurret == 0)
            continue;

        float BestDistSq = 3.40282347e+38f;
        IObjInspect* BestIface = nullptr;

        FAiTargetVector* const List = GetAiTargetList(Sys)->BuildAiTargetList();
        for (FAiTarget* Elem = List->Begin; Elem != List->End; ++Elem)
        {
            // FUN_062ee530: the candidate must match the job attack order.
            if (!Sys->Targeting.MatchesAttackOrder(Elem))
                continue;

            // Retail does not null-test the CEqObj before reading its EquipManager (+0xE4).
            CObject const* const OwnerCObj = OwnShip()->cobject();
            CEqObj* OwnerEq = nullptr;
            if (OwnerCObj != nullptr && ((unsigned)OwnerCObj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
                OwnerEq = (CEqObj*)OwnerCObj;
            CELauncher* const Launcher = CELauncher::cast(OwnerEq->EquipManager.FindByID(Slot.HardpointId));
            if (Launcher == nullptr)
                continue;

            // Squared distance from barrel 0, rounded to float once.
            Vector const BarrelPos = Launcher->GetBarrelPosWS(0);
            Vector const& ElemPos = Elem->Iface->get_position();
            double const Dx = (double)ElemPos.x - (double)BarrelPos.x;
            double const Dy = (double)ElemPos.y - (double)BarrelPos.y;
            double const Dz = (double)ElemPos.z - (double)BarrelPos.z;
            float const DistSq = (float)((Dz * Dz + Dy * Dy) + Dx * Dx);

            // A ship-class candidate whose behavior manager was created with content creation
            // type 2 is skipped unless bSelfMayEngage.
            CObject const* const ElemCObj = Elem->Iface->cobject();
            if (ElemCObj != nullptr && ((unsigned)ElemCObj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            {
                IBehaviorManager* const ElemMgr = ((CEqObj*)ElemCObj)->get_behavior_interface();
                if (ElemMgr != nullptr && ElemMgr->ContentCbCreationType == pub::AI::ContentCallback::CreationMission && !bSelfMayEngage)
                    continue;
            }

            // FUN_062e6a80 inlined. Only an ordered attitude <= -0.6f (DAT_063a0790) counts as
            // hostile.
            float Attitude = 0.0f;
            float Reverse = 0.0f;
            Elem->GetCachedRange(&Attitude, &Reverse);
            if (!(Attitude <= -0.6f))
                continue;

            // Any hostile counts, in range or not; unordered stays in range.
            bAnyHostile = true;
            if (DistSq > RangeSq)
                continue;

            bool bCanPoint = false;
            OwnShip()->can_point_at(bCanPoint, Slot.HardpointId, Elem->Iface->get_position(), 0.3f);

            // Strictly nearer, ordered.
            if (bCanPoint && DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestIface = Elem->Iface;
            }
        }

        // The player's own subtarget carries over only when the pick is the player's target.
        unsigned short const SubTarget = ((void*)BestIface == (void*)PlayerTarget)
            ? PlayerSubTarget
            : SUBOBJ_ID_NONE;

        OwnShip()->set_gun_target(&Slot.HardpointId, (IObjRW*)BestIface, SubTarget, -1);
    }

    if (SelfRecord->bFlagsValid == 0)
    {
        SelfRecord->Iface->get_type(SelfRecord->CachedFlags);
        SelfRecord->bFlagsValid = 1;
    }

    // FUN_062ee280: 0 when any hostile was seen this scan, 2 otherwise.
    if ((SelfRecord->CachedFlags & OBJ_SHIP_MASK) != 0)
        return;
    Sys->Targeting.SetAttackState(bAnyHostile ? TargetManager::kAttackStateEngaged : TargetManager::kAttackStateCombatOver);
}

namespace
{
    // FUN_062ea220's per-slot phase step, shared by the gun (+0x44) and mine-dropper (+0x84c)
    // loops, which are byte-identical apart from the base and the count.
    void StepGunGroupPhase(FGunGroupSlot& Slot, float Dt)
    {
        float const Phase = Slot.Phase;

        if (Slot.MunitionKind != FGunGroupSlot::KindPlain)
        {
            Slot.bFireTick = 1;
            Slot.Phase = Dt + Phase;
            return;
        }

        float const Period = Slot.FireInterval;
        if (!(Period > 0.0f))
        {
            Slot.bFireTick = 1;
        }
        else
        {
            // The flag is set on the tick the phase wraps past a period boundary.
            double const A = std::fmod((double)Dt + (double)Phase, (double)Period);
            double const B = std::fmod((double)Phase, (double)Period);
            Slot.bFireTick = (B > A) ? 1 : 0;
        }

        // The unrounded sum is compared; past BurstTime the phase restarts at -NoBurstTime.
        double const NewPhase = (double)Dt + (double)Phase;
        Slot.Phase = (float)NewPhase;
        if (NewPhase > (double)Slot.BurstTime)
            Slot.Phase = -Slot.NoBurstTime;
    }
}

// FUN_062ea220, RET 4.
void SystemManagerGunGroupManager::UpdateTimers(float Dt)
{
    AimRefreshTimer = AimRefreshTimer - Dt;

    // The sixteen line-of-fire cache records (the layout IsLineOfFireBlocked uses).
    for (int SlotIndex = 0; SlotIndex < 0x10; ++SlotIndex)
    {
        FLauncherFireCacheSlot& Slot = LineOfFireCache[SlotIndex];
        if (Slot.InUse == 0)
            continue;

        float const Age = Dt + Slot.Age;
        Slot.Age = Age;

        if (Slot.NeedsRefresh == 0)
        {
            // 1.0f is DAT_063a07ac; below it (or unordered) keeps counting.
            double const RefreshAge = (double)Dt + (double)Slot.RefreshTimer;
            Slot.RefreshTimer = (float)RefreshAge;
            if (RefreshAge >= 1.0)
            {
                Slot.RefreshTimer = 0.0f;
                Slot.NeedsRefresh = 1;
            }
        }

        // A record older than 10 s (DAT_063a07a8) is evicted.
        if (Age > 10.0f)
        {
            Slot.InUse = 0;
            Slot.TargetId = 0;
            Slot.HardpointId = SUBOBJ_ID_NONE;
            Slot.NeedsRefresh = 1;
            Slot.Age = 0.0f;
            Slot.RefreshTimer = 0.0f;
            Slot.bBlocked = 0;
        }
    }

    // How long the target has been in cruise.
    if (IsWatcherLive(TargetWatcher()))
    {
        bool bCruise = false;
        (void)CurrentTarget()->is_cruise_active(bCruise);
        if (bCruise)
            CruiseHeldTime = Dt + CruiseHeldTime;
        else
            CruiseHeldTime = 0.0f;
    }
    else
    {
        CruiseHeldTime = 0.0f;
    }

    // The counts are re-read every iteration.
    for (int Index = 0; Index < GunCount; ++Index)
        StepGunGroupPhase(Guns[Index], Dt);

    for (int Index = 0; Index < MineDropperCount; ++Index)
        StepGunGroupPhase(MineDroppers[Index], Dt);
}
