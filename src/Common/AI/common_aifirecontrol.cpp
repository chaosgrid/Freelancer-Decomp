// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI weapon fire control (retail 0x062e6a20, 0x062e8400, 0x062ea050-0x062ebb10):
// SustainedThresholdTimer, the gun / missile / mine-dropper passes of
// SystemManagerGunGroupManager, and the countermeasure-dropper block at SystemManager+0x1344
// (FCmDropperWatcher). The classes are declared in common_systemmanager.h.

#include "common_aisteer.h"
#include "AI/common_aisteer_internal.h"
#include "AI/common_systemmanager_internal.h" // CmnSysMgr::IsWatcherLive / WatchedObject
#include "x86math.h"
#include "common_csys.h"          // Acquire3DMathEngine, Matrix
#include "common_physys.h"        // PhySys::FindRayCollisions, RayHit
#include "common_misc.h"          // RandomVector, SUBOBJ_ID_NONE
#include "common_cequip.h"        // CELauncher::cast/GetBarrelPosWS, CEquipManager::FindByID
#include "common_cobject.h"       // CShip, CEqObj::get_behavior_interface
#include "common_systemmanager.h" // SystemManager, SystemManagerGunGroupManager
#include "common_cobjdb.h"        // IObjDB::GetZoneDb
#include "common_universe.h"      // Universe::ISystem
#include "common_iustategraph.h"  // BuildAiTargetList (HasTargetInForwardArc)
#include "x87_trig.h"             // X87Tan (062eabf0 FPTAN)

#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace CmnAiSteer;
using CmnSysMgr::IsWatcherLive;
using CmnSysMgr::WatchedObject;

// FUN_062e6a20, thiscall, RET 8. Both compares need strictly-greater (unordered fails).
bool SustainedThresholdTimer::Update(float DeltaTime, float Level)
{
    if (Level > 0.0f && Level > TriggerThreshold)
    {
        Active = 1;
    }
    else if (!Active)
    {
        return false;
    }

    // The sum is stored as float but compared unrounded.
    double const Sum = (double)DeltaTime + Accumulator;
    Accumulator = (float)Sum;
    if (Sum > Duration)
    {
        Accumulator = 0.0f;
        Active = 0;
        return false;
    }
    return true;
}

// `[[this+4]+8] ? -8 : 0`, the pattern every method below repeats (e.g. 062ea0f4-062ea0fe).
IObjRW* SystemManagerGunGroupManager::OwnShip() const
{
    return WatchedObject(Owner->Watcher);
}

// `[this+8] ? -8 : 0` (e.g. 062eae19-062eae22).
IObjRW* SystemManagerGunGroupManager::CurrentTarget() const
{
    return WatchedObject(TargetWatcher());
}

// FUN_062ea050, thiscall on &SystemManager::GunGroups.
bool SystemManagerGunGroupManager::FireAutoTurrets()
{
    if (bEnabled == 0)
        return false;
    SystemManager* const Sys = Owner;
    if (Sys->Targeting.AttackState != 0)
        return false;

    // No spread for the player's ship.
    float SpreadAngle = GunFireAccuracyConeAngle;
    if (Sys->SelfTarget.Iface->is_player())
        SpreadAngle = 0.0f;

    // ForceAll is Sys+0xbc, the fire-guns damage reaction timer's Active byte.
    unsigned char const ForceAll = Sys->Timers.Timers[6].Active;
    for (unsigned Index = 0; Index < (unsigned)GunCount; ++Index)
    {
        FGunGroupSlot& Slot = Guns[Index];

        // Phase less than 0 or unordered falls to the ForceAll test.
        bool bEligible = Slot.Phase >= 0.0f && Slot.bFireTick != 0;
        if (!bEligible && ForceAll == 0)
            continue;
        if (Slot.bAutoTurret == 0)
            continue;

        // The gun's own target id; nonzero status or (id & 0x3fffffff) == 0 skips.
        EquipmentVal Val{};
        int const Status = OwnShip()->get_equipment_val(Val, Slot.HardpointId, EquipValTargetId, 0.0f);
        if (Status != 0)
            continue;
        unsigned TargetId;
        std::memcpy(&TargetId, &Val, sizeof(TargetId));
        if ((TargetId & 0x3fffffff) == 0)
            continue;

        IObjRW* const Target = Sys->ObjectDb->FindById(&TargetId);
        if (Target == nullptr)
            continue;

        if (CanFireLauncherAtTarget(Slot.HardpointId, Target, GunRange) != 1)
            continue;

        // 0x3d567750 == pi/60.
        bool bPointing = false;
        OwnShip()->is_pointing_at(bPointing, Slot.HardpointId, 0.05235988f);
        if (!bPointing)
            continue;

        Vector FirePos;
        ComputeLeadFirePosition(&FirePos, Target, SUBOBJ_ID_NONE, 1, SpreadAngle, Slot.HardpointId);

        OwnShip()->fire_weapons(&Slot.HardpointId, 1u, &FirePos, -1);

        // A classified munition restarts its phase at -FireInterval.
        if (Slot.MunitionKind != FGunGroupSlot::KindPlain)
            Slot.Phase = -Slot.FireInterval;
    }
    return true;
}

// FUN_062ea770, RET 0xc.
int SystemManagerGunGroupManager::CanFireLauncherAtTarget(unsigned short HardpointId, IObjRW* Target,
                                                          float RangeThreshold)
{
    if (Target == nullptr)
        return 2;
    if (!IsWatcherLive(Owner->Watcher))
        return 2;

    // Retail clears only some EquipStatus bits before the call; every bit read below
    // is one of them.
    EquipStatus Status{};
    int const CallResult = OwnShip()->get_equipment_status(Status, HardpointId);
    unsigned char const Flags = Status.Flags;
    unsigned char const EquipFlags = (unsigned char)Status.EquipFlags;

    int Result = 2;
    if (CallResult == 0 && (Flags & 1) != 0 && (Flags & 6) == 0 && (EquipFlags & 0xa) != 0)
    {
        Vector TargetPos;
        GetIfacePositionOrZero(&TargetPos, Target);

        // Retail does not test the CEqObj before using its EquipManager (+0xe4): the
        // own ship is always one here.
        CObject* Obj = (CObject*)OwnShip()->cobject();
        if (Obj == nullptr || (Obj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
            Obj = nullptr;
        CEqObj* const EqObj = (CEqObj*)Obj;
        CELauncher* const Launcher = CELauncher::cast(EqObj->EquipManager.FindByID(HardpointId));
        if (Launcher == nullptr)
            return 0;

        // Out of range (or unordered) -> 5, else 1 and the line-of-fire check.
        Vector const BarrelPos = Launcher->GetBarrelPosWS(0);
        double const Dx = (double)TargetPos.x - BarrelPos.x;
        double const Dy = (double)TargetPos.y - BarrelPos.y;
        double const Dz = (double)TargetPos.z - BarrelPos.z;
        double const Dist = InlineFastLength((Dz * Dz + Dy * Dy) + Dx * Dx);
        if (!(Dist < (double)RangeThreshold))
            return 5;
        Result = 1;
    }
    else
    {
        if ((Flags & 1) == 0)
            Result = 0;
        if ((EquipFlags & 4) != 0)
            Result = 4;
        if ((EquipFlags & 8) != 0)
            return 3;
        if (Result != 1)
            return Result;
    }

    if (IsLineOfFireBlocked(Target, HardpointId))
        Result = 7;
    return Result;
}

// FUN_062ea450, RET 8. Results are cached per (target, hardpoint) in 16 slots.
bool SystemManagerGunGroupManager::IsLineOfFireBlocked(IObjRW* Target, unsigned short HardpointId)
{
    SystemManager* const Sys = Owner;

    unsigned const TargetId = Target->get_id();

    // Retail fetches the own ship's cobject() and discards it.
    (void)OwnShip()->cobject();

    // A hit on the target itself is not a blocker.
    CObject const* TargetObj = Target->cobject();
    if (TargetObj == nullptr || ((unsigned)TargetObj->ObjectClass & 1) != 1)
        TargetObj = nullptr;

    FLauncherFireCacheSlot* const Slots = LineOfFireCache;
    int FreeIndex = -1;
    int MatchIndex = -1;
    for (int i = 0; i < 16; ++i)
    {
        if (!Slots[i].InUse)
        {
            if (FreeIndex == -1)
                FreeIndex = i;
            continue;
        }
        if (Slots[i].TargetId == TargetId && Slots[i].HardpointId == HardpointId)
        {
            MatchIndex = i;
            break;
        }
    }

    // No match: claim the first free slot, if any, and answer false.
    if (MatchIndex == -1)
    {
        if (FreeIndex != -1)
        {
            FLauncherFireCacheSlot& NewSlot = Slots[FreeIndex];
            NewSlot.NeedsRefresh = 1;
            NewSlot.Age = 0.0f;
            NewSlot.RefreshTimer = 0.0f;
            NewSlot.bBlocked = 0;
            NewSlot.InUse = 1;
            NewSlot.TargetId = TargetId;
            NewSlot.HardpointId = HardpointId;
        }
        return false;
    }

    // A fresh match answers its cached byte; a stale one is cleared and recomputed.
    FLauncherFireCacheSlot& Slot = Slots[MatchIndex];
    if (Slot.NeedsRefresh == 0)
        return Slot.bBlocked != 0;

    Slot.NeedsRefresh = 0;
    Slot.bBlocked = 0;
    Slot.Age = 0.0f;

    // No null test before the EquipManager access: the own ship is a CEqObj.
    CObject* Own = (CObject*)OwnShip()->cobject();
    if (Own == nullptr || ((unsigned)Own->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        Own = nullptr;
    CELauncher* const Launcher = CELauncher::cast(((CEqObj*)Own)->EquipManager.FindByID(HardpointId));
    if (Launcher == nullptr)
        return Slot.bBlocked != 0;

    if (Launcher->IsDestroyed())
        return Slot.bBlocked != 0;
    if (Launcher->IsDisabled())
        return Slot.bBlocked != 0;

    Vector const BarrelPos = Launcher->GetBarrelPosWS(0);
    Vector const BarrelDir = Launcher->GetBarrelDirWS(0);

    Universe::ISystem const* const System = Sys->ObjectDb->GetZoneDb();
    unsigned const SystemId = System->GetId();
    Vector const TargetPos = Target->get_position();

    // Far = Barrel + Dir * 2000.0f (DAT_0639e678), Near = Barrel + Dir * 20.0f
    // (DAT_0639b21c). x and y add the unrounded product; z's is rounded first.
    Vector Far;
    Far.x = (float)((double)BarrelDir.x * 2000.0f + BarrelPos.x);
    Far.y = (float)((double)BarrelPos.y + (double)BarrelDir.y * 2000.0f);
    float const FarDz = (float)((double)BarrelDir.z * 2000.0f);
    Far.z = (float)((double)BarrelPos.z + FarDz);
    Vector Near;
    Near.x = (float)((double)BarrelDir.x * 20.0f + BarrelPos.x);
    Near.y = (float)((double)BarrelDir.y * 20.0f + BarrelPos.y);
    float const NearDz = (float)((double)BarrelDir.z * 20.0f);
    Near.z = (float)((double)NearDz + BarrelPos.z);

    PhySys::RayHit Hits[8];
    int const HitCount = PhySys::FindRayCollisions(SystemId, Near, Far, Hits, 8);
    for (int i = 0; i < HitCount; ++i)
    {
        // Only ship-class hits (0x303 / 0x503) other than the target block.
        CObject* const HitObj = Hits[i].Object;
        if (HitObj == nullptr)
            continue;
        unsigned const HitClass = (unsigned)HitObj->ObjectClass;
        if ((HitClass & 0x103) != 0x103)
            continue;
        if (HitClass != 0x303 && HitClass != 0x503)
            continue;
        if (HitObj == TargetObj)
            continue;

        // The first such hit decides: blocked when the target is farther from the barrel
        // than the hit (unordered: not blocked). Sums in double; only the target's dz is
        // rounded to float.
        double const Hx = (double)Hits[i].Point.x - BarrelPos.x;
        double const Hy = (double)Hits[i].Point.y - BarrelPos.y;
        double const Hz = (double)Hits[i].Point.z - BarrelPos.z;
        double const Tx = (double)TargetPos.x - BarrelPos.x;
        double const Ty = (double)TargetPos.y - BarrelPos.y;
        float const Tz = (float)((double)TargetPos.z - BarrelPos.z);
        double const HitSq = (Hz * Hz + Hy * Hy) + Hx * Hx;
        double const TargetSq = ((double)Tz * Tz + Ty * Ty) + Tx * Tx;
        if (TargetSq > HitSq)
            Slot.bBlocked = 1;
        return Slot.bBlocked != 0;
    }

    return Slot.bBlocked != 0;
}

// FUN_062ea960, RET 0x18.
void SystemManagerGunGroupManager::ComputeLeadFirePosition(Vector* OutPos, IObjRW* Target,
                                                           unsigned short UnusedSubobjSlot,
                                                           unsigned char UseDirectAim, float SpreadAngle,
                                                           unsigned short SubtargetId)
{
    (void)UnusedSubobjSlot; // pushed by every caller, never read
    SystemManager* const Sys = Owner;

    // The lead position is the translation of a full copy of the target's transform,
    // so without UseDirectAim the result starts at the target's own position.
    Transform TargetXform = Target->get_transform();
    Vector& LeadPos = TargetXform.Translation;

    if (UseDirectAim != 0)
    {
        if (SubtargetId != SUBOBJ_ID_NONE)
        {
            (void)OwnShip()->get_tgt_lead_fire_pos(SubtargetId, LeadPos);
        }
        else
        {
            // CShip::get_tgt_lead_fire_pos on the own ship; a null object, another class
            // or a false answer falls back to the target's position.
            CObject const* const Obj = OwnShip()->cobject();
            bool bGot = false;
            if (Obj != nullptr && ((unsigned)Obj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
                bGot = ((CShip const*)Obj)->get_tgt_lead_fire_pos(LeadPos);
            if (!bGot)
                LeadPos = Target->get_position();
        }
    }

    // FAiTarget::RefreshPos, inlined by retail.
    Sys->SelfTarget.RefreshPos();

    Vector DirVec;
    Vec3Assign(&DirVec, LeadPos.x - Sys->SelfTarget.CachedPos.x, LeadPos.y - Sys->SelfTarget.CachedPos.y,
               LeadPos.z - Sys->SelfTarget.CachedPos.z);

    // One rand() * 2^-15 (DAT_063997d0), rounded to float.
    int const RandBits = std::rand();
    float const RandFrac = (float)((double)RandBits * (double)3.0517578125e-05f);

    // A player target slower than sqrt(10) (10.0f is DAT_0639e884) gets LeadPos as is.
    // The exponent is gun_fire_accuracy_power for a player target, _npc otherwise.
    float Exponent;
    if (Target->is_player())
    {
        Vector const Velocity = Target->get_velocity();
        double const SpeedSq = ((double)Velocity.x * Velocity.x + (double)Velocity.y * Velocity.y) +
                               (double)Velocity.z * Velocity.z;
        if (SpeedSq < 10.0)
        {
            *OutPos = LeadPos;
            return;
        }
        Exponent = Sys->PersonalityData.GunParams.GunFireAccuracyPower;
    }
    else
    {
        Exponent = Sys->PersonalityData.GunParams.GunFireAccuracyPowerNpc;
    }

    // _CIpow(RandFrac, Exponent) * SpreadAngle.
    float const Spread = (float)(std::pow((double)RandFrac, (double)Exponent) * (double)SpreadAngle);

    // Drawn even when the spread is too small to use it.
    Vector const RandVec = RandomVector(1.0f);

    // 0.01f is DAT_0639e6b8; unordered returns LeadPos.
    if (!(Spread > 0.01f))
    {
        *OutPos = LeadPos;
        return;
    }

    // Cross = RandVec x DirVec, each component in double, rounded once.
    Vector Cross;
    Cross.x = (float)((double)DirVec.z * RandVec.y - (double)RandVec.z * DirVec.y);
    Cross.y = (float)((double)RandVec.z * DirVec.x - (double)DirVec.z * RandVec.x);
    Cross.z = (float)((double)DirVec.y * RandVec.x - (double)RandVec.y * DirVec.x);

    Vector Normal;
    VecNormalize3(&Cross, &Normal);

    // Offset distance = |DirVec| * tan(Spread), unrounded (FPTAN).
    double const DirLength = std::sqrt(((double)DirVec.z * DirVec.z + (double)DirVec.y * DirVec.y) +
                                       (double)DirVec.x * DirVec.x);
    double const Dist = DirLength * X87Tan((double)Spread);

    // x keeps Normal.x * Dist unrounded before the add; y and z round their product.
    float const OffsetY = (float)((double)Normal.y * Dist);
    float const OffsetZ = (float)((double)Normal.z * Dist);
    OutPos->x = (float)((double)Normal.x * Dist + (double)LeadPos.x);
    OutPos->y = (float)((double)OffsetY + (double)LeadPos.y);
    OutPos->z = (float)((double)OffsetZ + (double)LeadPos.z);
}

// FUN_062eb020: thiscall on a Transform, RET 8. The math engine's InverseTransform
// (+0x3c) writes a local that is copied to *Out. Vector / Transform and x86math's
// FVector / FTransform share layouts.
Vector* TransformInversePoint(Transform const* Self, Vector* Out, Vector const* In)
{
    Vector Local;
    Acquire3DMathEngine()->InverseTransform((FVector&)Local, *(FTransform const*)Self, *(FVector const*)In);
    *Out = Local;
    return Out;
}

// FUN_062eac70, RET 4 (the stack argument is never read).
int SystemManagerGunGroupManager::UpdateGunFireState()
{
    SystemManager* const Sys = Owner;

    if ((Sys->Targeting.SelectionPriority & 1) == 0)
        return 8;

    if (!IsWatcherLive(Sys->Watcher))
        return 0;
    if (!IsWatcherLive(TargetWatcher()))
        return 0;

    // Retail queries the own ship's get_target into a local nobody reads.
    int Count = 0;
    unsigned char bOverride = 0;
    float SpreadAngle = GunFireAccuracyConeAngle;
    IObjRW* UnusedTarget = nullptr;
    (void)OwnShip()->get_target(UnusedTarget);

    if ((Field_0x10 & 0x30000000) != 0)
    {
        bOverride = 1;
        SpreadAngle = 0.0f;
    }

    // ForceAll is Sys+0xbc, the fire-guns damage reaction timer's Active byte.
    unsigned char const ForceAll = Sys->Timers.Timers[6].Active;
    int Result = 2;

    IObjRW* const Target = CurrentTarget();
    Transform TargetXform = Target->get_transform();

    // Refresh the aim point when the timer has run out (unordered skips): the lead
    // position in target-local space, held for gun_target_point_switch_time.
    if (AimRefreshTimer <= 0.0f)
    {
        Vector LeadPos;
        ComputeLeadFirePosition(&LeadPos, Target, SUBOBJ_ID_NONE, 0, SpreadAngle, SUBOBJ_ID_NONE);
        Vector LocalAim;
        Vector const* const Aim = TransformInversePoint(&TargetXform, &LocalAim, &LeadPos);
        AimOffset = *Aim;
        AimRefreshTimer = Sys->PersonalityData.GunParams.GunTargetPointSwitchTime;
    }

    // 64 ids, the size of retail's frame; Guns holds at most 0x40 slots
    // (ResolveHardpoints).
    unsigned short FireIds[64];
    for (unsigned Index = 0; Index < (unsigned)GunCount; ++Index)
    {
        FGunGroupSlot& Slot = Guns[Index];

        // Phase less than 0 or unordered is not ready.
        bool const bReady = Slot.Phase >= 0.0f && Slot.bFireTick != 0;
        if (!bReady && bOverride == 0 && ForceAll == 0)
            continue;

        if (Slot.MunitionKind != FGunGroupSlot::KindPlain || Slot.bAutoTurret != 0)
            continue;

        Result = CanFireLauncherAtTarget(Slot.HardpointId, CurrentTarget(), GunRange);
        if (Result != 1 && ForceAll == 0)
            continue;

        OwnShip()->set_gun_target(&Slot.HardpointId, CurrentTarget(), SUBOBJ_ID_NONE, -1);

        // 0x3d567750 == pi/60. No bound check on FireIds in retail.
        bool bPointing = false;
        OwnShip()->is_pointing_at(bPointing, Slot.HardpointId, 0.05235988f);
        if (bPointing || ForceAll != 0)
        {
            FireIds[Count] = Slot.HardpointId;
            ++Count;
        }

        // fire_style SINGLE stops after the first slot that got here.
        if (bFireAllGuns == 0)
            break;
    }

    if (Count <= 0)
        return Result;

    TargetXform = CurrentTarget()->get_transform();

    // CShip::get_tgt_lead_fire_pos of the own ship, else the target's position.
    IObjRW* const PositionSource = CurrentTarget();
    Vector TargetPos;
    CObject const* const Obj = OwnShip()->cobject();
    bool bGot = false;
    if (Obj != nullptr && ((unsigned)Obj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
        bGot = ((CShip const*)Obj)->get_tgt_lead_fire_pos(TargetPos);
    if (!bGot)
        TargetPos = PositionSource->get_position();

    // The lead point replaces the copied transform's translation; the stored
    // target-local aim offset is mapped back to world space with it.
    TargetXform.Translation = TargetPos;

    Vector World;
    Acquire3DMathEngine()->TransformXform((FVector&)World, (FTransform const&)TargetXform,
                                          (FVector const&)AimOffset);
    Vector const FirePos = World;

    OwnShip()->fire_weapons(FireIds, (unsigned)Count, &FirePos, -1);
    return Result;
}

// FUN_062eb070, RET 4 (the stack argument is never read). Walks Guns like
// UpdateGunFireState but fires only the seeker, HpType-0xa and cruise-disruptor launchers.
int SystemManagerGunGroupManager::UpdateMissileFireState()
{
    int Result = 0;

    if (!IsWatcherLive(TargetWatcher()))
        return 0;
    SystemManager* const Sys = Owner;
    if (!IsWatcherLive(Sys->Watcher))
        return 0;

    unsigned const ShipFlags = (unsigned)Sys->Targeting.SelectionPriority;
    if ((Field_0x10 & 0x30000000) != 0)
        return 2;

    // Sys+0xdc, the fire-missiles damage reaction timer's Active byte.
    unsigned char const bForceFire = Sys->Timers.Timers[8].Active != 0 ? 1 : 0;

    for (unsigned Index = 0; Index < (unsigned)GunCount; ++Index)
    {
        FGunGroupSlot& Slot = Guns[Index];

        // Phase less than 0 or unordered skips.
        if (!(Slot.Phase >= 0.0f))
            continue;
        if (Slot.bFireTick == 0)
            continue;

        // bWide picks the aim angle below.
        int const Kind = Slot.MunitionKind;
        unsigned char bWide = 0;
        if (Kind == FGunGroupSlot::KindSeeker)
        {
            // bSeekerMunition set: ShipFlags bit 1; clear: bit 2, wide.
            if (Slot.bSeekerMunition != 0)
            {
                if ((ShipFlags & 2) == 0)
                    continue;
            }
            else
            {
                bWide = 1;
                if ((ShipFlags & 4) == 0)
                    continue;
            }
        }
        else if (Kind == FGunGroupSlot::KindTorpedo)
        {
            bWide = 1;
            if ((ShipFlags & 8) == 0)
                continue;
        }
        else if (Kind == FGunGroupSlot::KindCruiseDisruptor)
        {
            if (!(CruiseHeldTime > Sys->PersonalityData.MissileParams.AntiCruiseMissilePreFireDelay))
                continue;

            // FAiTarget::RefreshPos, inlined by retail.
            Sys->SelfTarget.RefreshPos();

            // The 0x5f3759df fast length of the unrounded squared distance.
            Vector const& TargetPos = CurrentTarget()->get_position();
            double const Dx = (double)TargetPos.x - Sys->SelfTarget.CachedPos.x;
            double const Dy = (double)TargetPos.y - Sys->SelfTarget.CachedPos.y;
            double const Dz = (double)TargetPos.z - Sys->SelfTarget.CachedPos.z;
            double const Dist = InlineFastLength((Dz * Dz + Dy * Dy) + Dx * Dx);

            // Strictly inside (min, max); unordered fails.
            if (!(Dist > Sys->PersonalityData.MissileParams.AntiCruiseMissileMinDistance))
                continue;
            if (!(Dist < Sys->PersonalityData.MissileParams.AntiCruiseMissileMaxDistance))
                continue;
        }
        else
        {
            continue;
        }

        Result = CanFireLauncherAtTarget(Slot.HardpointId, CurrentTarget(), MissileLaunchRange);
        if (Result != 1)
            continue;

        OwnShip()->set_gun_target(&Slot.HardpointId, CurrentTarget(), SUBOBJ_ID_NONE, -1);

        bool bPointing = false;
        float const Angle = bWide != 0 ? WideConeAngle : MissileLaunchConeAngle;
        OwnShip()->is_pointing_at(bPointing, Slot.HardpointId, Angle);
        if (!bPointing && bForceFire == 0)
            continue;

        // A classified munition restarts its phase at -FireInterval.
        OwnShip()->fire_weapons(&Slot.HardpointId, 1u, nullptr, -1);
        if (Slot.MunitionKind != FGunGroupSlot::KindPlain)
            Slot.Phase = -Slot.FireInterval;
    }

    return Result;
}

namespace
{
    // DAT_063fca5c: a .data byte (uninitialised, so 0 at load) whose only reference in
    // the module is the read at .text:062eb4df. Kept as the global it is.
    unsigned char GNearFieldAimResetSuppressed = 0;
}

// FUN_062e8400.
bool SystemManagerTimerTable::HasTargetInForwardArc(float MaxAngle, float MaxRange)
{
    // The loop re-reads End on every pass.
    FAiTargetVector* const List = BuildAiTargetList(this);
    for (FAiTarget* Elem = List->Begin; Elem != List->End; ++Elem)
    {
        // Lazily cached type (flag raised after the call).
        if (Elem->bFlagsValid == 0)
        {
            Elem->Iface->get_type(Elem->CachedFlags);
            Elem->bFlagsValid = 1;
        }
        if ((Elem->CachedFlags & 0x7f0000) == 0)
            continue;

        Elem->RefreshPos();
        FAiTarget* const Own = &Owner->SelfTarget;
        Own->RefreshPos();

        Vector D;
        D.x = Own->CachedPos.x - Elem->CachedPos.x;
        D.y = Own->CachedPos.y - Elem->CachedPos.y;
        D.z = Own->CachedPos.z - Elem->CachedPos.z;

        // The rounded dot is kept for the cosine below; the unrounded one must be
        // strictly positive.
        Matrix const& OwnRotA = Own->Iface->get_transform().Rotation;
        Vector const OwnFwdA{ -OwnRotA.E[0][2], -OwnRotA.E[1][2], -OwnRotA.E[2][2] };
        double const Dot = ((double)OwnFwdA.z * D.z + (double)OwnFwdA.y * D.y) + (double)OwnFwdA.x * D.x;
        float const DotF = (float)Dot;
        if (!(Dot > 0.0))
            continue;

        double const DistSq = ((double)D.z * D.z + (double)D.y * D.y) + (double)D.x * D.x;
        if (!((double)MaxRange * MaxRange > DistSq))
            continue;

        // 100.0f is DAT_0639fb58.
        Vector const Velocity = Elem->Iface->get_velocity();
        double const SpeedSq = ((double)Velocity.z * Velocity.z + (double)Velocity.y * Velocity.y) +
                               (double)Velocity.x * Velocity.x;
        if (!(SpeedSq > 100.0))
            continue;

        // Headings nearly parallel: dot strictly above 0.95f (DAT_06399e78).
        Matrix const& OwnRotB = Own->Iface->get_transform().Rotation;
        Vector const OwnFwdB{ -OwnRotB.E[0][2], -OwnRotB.E[1][2], -OwnRotB.E[2][2] };
        Matrix const& TgtRot = Elem->Iface->get_transform().Rotation;
        Vector const TgtFwd{ -TgtRot.E[0][2], -TgtRot.E[1][2], -TgtRot.E[2][2] };
        double const Heading = ((double)OwnFwdB.z * TgtFwd.z + (double)OwnFwdB.y * TgtFwd.y) +
                               (double)TgtFwd.x * OwnFwdB.x;
        if (!(Heading > (double)0.95f))
            continue;

        // FUN_062e6a80: only a hostile attitude (<= -0.6f, DAT_063a0790) goes on.
        float Attitude;
        float ReverseAttitude;
        GetCachedRange(Elem, &Attitude, &ReverseAttitude);
        if (!(Attitude <= -0.6f))
            continue;

        // cos = DotF / |D| capped at 1.0f and rounded; inside MaxAngle returns true.
        double const Ratio = (double)DotF / std::sqrt(DistSq);
        float const Cosine = (1.0 < Ratio) ? 1.0f : (float)Ratio;
        if (std::acos((double)Cosine) <= (double)MaxAngle)
            return true;
    }
    return false;
}

// FUN_062eb390, RET 4 (the stack argument is never read).
unsigned SystemManagerGunGroupManager::UpdateMineDropState()
{
    unsigned Result = 0;

    if (!IsWatcherLive(TargetWatcher()))
        return 0;
    SystemManager* const Sys = Owner;
    if (!IsWatcherLive(Sys->Watcher))
        return 0;

    if ((Field_0x10 & 0x30000000) != 0)
        return 2;

    // bAggressive is Sys+0xcc, the drop-mines damage reaction timer's Active byte. The
    // own speed must exceed 20.0f (DAT_0639b21c).
    bool const bAggressive = Sys->Timers.Timers[7].Active != 0;
    Vector const Velocity = Sys->SelfTarget.Iface->get_velocity();
    double const Speed = std::sqrt(((double)Velocity.z * Velocity.z + (double)Velocity.y * Velocity.y) +
                                   (double)Velocity.x * Velocity.x);
    if (!(Speed > 20.0))
        return Result;

    // Unless aggressive, a hostile must be in the forward arc (mine_launch_range).
    if (!bAggressive &&
        !Sys->Timers.HasTargetInForwardArc(MineLaunchConeAngle, Sys->PersonalityData.MineParams.MineLaunchRange))
        return Result;

    // Retail clears only some EquipStatus bits; every bit read below is one of them.
    EquipStatus Status{};
    if (MineDropperCount == 0)
        return Result;

    for (unsigned Index = 0; Index < (unsigned)MineDropperCount; ++Index)
    {
        FGunGroupSlot& Slot = MineDroppers[Index];

        // Phase less than 0 or unordered skips.
        if (!bAggressive)
        {
            if (!(Slot.Phase >= 0.0f))
                continue;
            if (Slot.bFireTick == 0)
                continue;
        }

        OwnShip()->get_equipment_status(Status, Slot.HardpointId);
        if ((Status.EquipFlags & 0xa) != 0xa)
            continue;

        // The result becomes 1 before the DAT_063fca5c test.
        Result = 1;
        if (GNearFieldAimResetSuppressed != 0)
            continue;

        OwnShip()->fire_weapons(&Slot.HardpointId, 1u, nullptr, -1);
        if (Slot.MunitionKind != FGunGroupSlot::KindPlain)
            Slot.Phase = -Slot.FireInterval;
    }

    return Result;
}

// FUN_062eb550. Sole caller is BuzzBehavior (.text:062c67ee). No null test on the own
// ship; it and the count are re-read every pass (count compared unsigned).
void SystemManagerGunGroupManager::DropAllMines()
{
    if (MineDropperCount == 0)
        return;

    unsigned Index = 0;
    do
    {
        OwnShip()->fire_weapons(&MineDroppers[Index].HardpointId, 1u, nullptr, -1);
        ++Index;
    } while (Index < (unsigned)MineDropperCount);
}

// FUN_062eb5a0, RET 8.
bool SystemManagerGunGroupManager::ComputeGunRangeStats(float* OutAverage, float* OutMax)
{
    if (GunCount == 0)
        return false;

    // The value local is not reset per slot.
    int MatchCount = 0;
    float Max = 0.0f;
    float Sum = 0.0f;
    EquipmentVal Value{};

    unsigned Index = 0;
    do
    {
        FGunGroupSlot const& Slot = Guns[Index];
        if (Slot.MunitionKind == FGunGroupSlot::KindPlain &&
            OwnShip()->get_equipment_val(Value, Slot.HardpointId, EquipValRange, 0.0f) == 0)
        {
            // Only a strictly greater value replaces Max.
            ++MatchCount;
            Sum = (float)((double)Value.Value + Sum);
            if (Value.Value > Max)
                Max = Value.Value;
        }

        ++Index;
    } while (Index < (unsigned)GunCount);

    if (MatchCount <= 0)
        return false;

    *OutAverage = (float)((double)Sum / (double)MatchCount);
    *OutMax = Max;
    return true;
}

// FUN_062eb6c0. The call at 062eb740 is IBehaviorManager::get_sys (Ghidra names the ICF
// fold CPlayerTradeOffer::GetTargetPlayerID). Structural twin:
// RefreshRepairManagerFromPersonality.
bool SystemManagerGunGroupManager::RefreshFromPersonality()
{
    SystemManager* const Sys = Owner;
    if (Sys == nullptr)
        return false;

    // The config source: the owner unless the formation leader's SystemManager is
    // swapped in below.
    SystemManager* Src = Sys;

    // Non-player with leader_makes_me_tougher and a live formation leader (Sys+0x1370,
    // gated by Sys+0x1429; see GetFormationLeader). is_player is called untested.
    if (!Sys->SelfTarget.Iface->is_player() && Sys->PersonalityData.FormationParams.bLeaderMakesMeTougher != 0)
    {
        IObjRW* const Leader = Sys->FormationLeaders.GetFormationLeader();
        if (Leader != nullptr)
        {
            CObject const* const Obj = Leader->cobject();
            if (Obj != nullptr && ((unsigned)Obj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
            {
                // The leader's SystemManager becomes the config source and the owner's
                // +0x14 latch is set.
                IBehaviorManager* const Iface = ((CEqObj*)Obj)->get_behavior_interface();
                if (Iface != nullptr)
                {
                    Src = Iface->get_sys();
                    Sys->bLeaderParamsBorrowed = 1;
                }
            }
        }
    }

    pub::AI::Personality::GunUseStruct const& Gun = Src->PersonalityData.GunParams;
    pub::AI::Personality::MineUseStruct const& Mine = Src->PersonalityData.MineParams;
    pub::AI::Personality::MissileUseStruct const& Missile = Src->PersonalityData.MissileParams;

    // Copies in retail order.
    GunFireIntervalTime = Gun.GunFireIntervalTime;                              // +0x1704
    GunFireIntervalVariance = Gun.GunFireIntervalVariancePercent;               // +0x1708
    AutoTurretIntervalTime = Gun.AutoTurretIntervalTime;                        // +0x1730
    GunFireBurstIntervalTime = Gun.GunFireBurstIntervalTime;                    // +0x170c
    GunFireBurstIntervalVariance = Gun.GunFireBurstIntervalVariancePercent;     // +0x1710
    AutoTurretBurstIntervalTime = Gun.AutoTurretBurstIntervalTime;              // +0x1734
    AutoTurretBurstIntervalVariance = Gun.AutoTurretBurstIntervalVariancePercent; // +0x173c
    GunFireNoBurstIntervalTime = Gun.GunFireNoBurstIntervalTime;                // +0x1714
    AutoTurretNoBurstIntervalTime = Gun.AutoTurretNoBurstIntervalTime;          // +0x1738
    GunFireAccuracyConeAngle = Gun.GunFireAccuracyConeAngle;                    // +0x1718

    // ((R - NegR) * Frac + NegR + 1.0) * gun_range_threshold in double, with
    // R = gun_range_threshold_variance_percent, NegR = float(-R), Frac = rand() * 2^-15
    // (DAT_063997d0); 1.0 is the double DAT_06399410.
    float const SpreadRange = Gun.GunRangeThresholdVariancePercent;
    float const NegRange = -SpreadRange;
    double const Frac = (double)std::rand() * (double)3.0517578125e-05f;
    float const SpreadValue =
        (float)((((double)SpreadRange - (double)NegRange) * Frac + (double)NegRange + 1.0) * (double)Gun.GunRangeThreshold);

    // GunRange = longest gun range * SpreadValue, only when FUN_062eb5a0 succeeds.
    float StatAvg = 0.0f, StatMax = 0.0f;
    if (ComputeGunRangeStats(&StatAvg, &StatMax))
        GunRange = (float)((double)StatMax * (double)SpreadValue);

    MissileLaunchRange = Missile.MissileLaunchRange;                                    // +0x174c
    MissileLaunchAllowOutOfRange = (float)Missile.bMissileLaunchAllowOutOfRange;        // +0x1750 byte, FILD

    // fire_style MULTIPLE (0) -> 1, SINGLE (1) -> 0, anything else keeps it.
    int const FireStyle = Gun.FireStyle;
    if (FireStyle == 0)
        bFireAllGuns = 1;
    else if (FireStyle == 1)
        bFireAllGuns = 0;

    MissileLaunchIntervalTime = Missile.MissileLaunchIntervalTime;                 // +0x1754
    MissileLaunchIntervalVariance = Missile.MissileLaunchIntervalVariancePercent;  // +0x1758
    MissileLaunchConeAngle = Missile.MissileLaunchConeAngle;                       // +0x175c
    MineLaunchInterval = Mine.MineLaunchInterval;                                  // +0x1740
    MineLaunchConeAngle = Mine.MineLaunchConeAngle;                                // +0x1744

    // Signed count tests, count re-read each pass.
    if (GunCount > 0)
    {
        int Index = 0;
        do
        {
            RandomizeSlotTiming(&Guns[Index]);   // FUN_062e9500
            ++Index;
        } while (Index < GunCount);
    }

    if (MineDropperCount > 0)
    {
        int Index = 0;
        do
        {
            RandomizeSlotTiming(&MineDroppers[Index]);
            ++Index;
        } while (Index < MineDropperCount);
    }

    return true;
}

// FUN_062eb910. The first ready slot ends the scan with true.
bool SystemManagerGunGroupManager::HasReadyWeaponSlot()
{
    if (GunCount == 0)
        return false;

    unsigned Index = 0;
    do
    {
        unsigned short const& SlotId = Guns[Index].HardpointId;

        EquipmentVal Val{};
        int const CallResult = OwnShip()->get_equipment_val(Val, SlotId, EquipValMunitionKind, 0.0f);

        // Signed range test on the returned dword.
        int Classify;
        std::memcpy(&Classify, &Val, sizeof(Classify));
        if (CallResult == 0 && Classify >= 0 && Classify <= 2)
        {
            // Retail clears only some EquipStatus bits; zero-init is the defined
            // superset. The call's result is not tested.
            EquipStatus Status{};
            (void)OwnShip()->get_equipment_status(Status, SlotId);

            if ((Status.Flags & (EquipStatusFlagDestroyed | EquipStatusFlagDisabled)) == 0)
                return true;
        }

        // Count re-read, unsigned compare.
        ++Index;
    } while (Index < (unsigned)GunCount);

    return false;
}

// FUN_062eb670, RET 0xc (only the target argument is read).
bool SystemManagerGunGroupManager::RunFireControlTick(IObjRW* Target)
{
    if (bEnabled == 0)
        return false;

    if (!Retarget(Target))   // FUN_062e9ae0
        return false;

    LastGunFireResult = UpdateGunFireState();
    UpdateMissileFireState();
    UpdateMineDropState();
    return true;
}

// FUN_062ebb10.
void FCmDropperWatcher::SetActive(unsigned char NewState)
{
    if (bActive == NewState)
        return;

    // One whole EquipStatus reused for every id: GetStatus ORs into EquipFlags (+4), so a
    // smaller local overflows. Retail clears only some bits once (FUN_062e8270's body);
    // zero-init is the defined superset, and only Flags bit 0 is read.
    EquipStatus Status{};

    // Count is re-read after every iteration.
    unsigned short* Id = Ids;
    for (int Index = 0; Index < Count; ++Index, ++Id)
    {
        // No null test on the watched object in retail.
        IObjRW* const Obj = WatchedObject(Owner->Watcher);
        if (Obj->get_equipment_status(Status, *Id) != 0)
            continue;

        if ((Status.Flags & EquipStatusFlagActive) == NewState)
            continue;

        // Retail re-reads Owner and the watcher.
        IObjRW* const Obj2 = WatchedObject(Owner->Watcher);
        Obj2->toggle_item(Id, NewState != 0, -1);
    }

    // Stored after the loop, also when the count was <= 0.
    bActive = NewState;
}

// FUN_062eb9d0.
bool FCmDropperWatcher::Bind(SystemManager* NewOwner)
{
    if (NewOwner == nullptr)
        return false;
    if (!IsWatcherLive(NewOwner->Watcher))
        return false;

    Owner = NewOwner;

    // A player's droppers are left alone and the bind still succeeds. is_player is
    // called untested.
    if (NewOwner->SelfTarget.Iface->is_player())
        return true;

    bActive = 0;
    Timer = 0.0f;
    Count = 0;

    // Retail bug, reproduced: Ids holds only 8 ushorts (this block is at
    // SystemManager+0x1344, FFormationLeaderState starts at +0x1364), yet
    // find_equipment may write up to 0x40 ids. With 8 or more countermeasure droppers
    // the ids run on into the formation-leader block (still inside the SystemManager).
    IObjRW* const Obj = WatchedObject(Owner->Watcher);
    int const Result = Obj->find_equipment(Ids, 0x40, EquipTypeCmDropper);

    // Count the ids up to the first zero, at most 0x40.
    if (Result == 0)
    {
        int Index = 0;
        unsigned short const* Cursor = Ids;
        do
        {
            if (*Cursor == 0)
                break;
            ++Count;
            ++Index;
            ++Cursor;
        } while (Index < 0x40);
    }

    SetActive(1);
    return true;
}

// FUN_062eba70. Cycles the droppers on for CountermeasureActiveTime, then off until
// CountermeasureUnactiveTime.
void FCmDropperWatcher::Update(float Dt)
{
    // is_player is called untested.
    if (Owner->SelfTarget.Iface->is_player())
        return;

    SystemManager* const Sys = Owner;
    pub::AI::Personality::CountermeasureUseStruct const& CmParams = Sys->PersonalityData.CMParams;

    // The unrounded sum is compared; unordered keeps the timer.
    double const Sum = (double)Dt + (double)Timer;
    Timer = (float)Sum;
    if (Sum >= (double)CmParams.CountermeasureUnactiveTime)
        Timer = 0.0f;

    if (Timer < CmParams.CountermeasureActiveTime)
    {
        SetActive(1);
        return;
    }

    if (!(Timer < CmParams.CountermeasureUnactiveTime))
        return;

    // No null test on the watched object.
    IObjRW* const Obj = WatchedObject(Sys->Watcher);
    if (!Obj->is_player())
        SetActive(0);
}
