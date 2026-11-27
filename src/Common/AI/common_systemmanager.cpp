// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- SystemManager (the per-ship AI system block behind IBehaviorManager::Sys):
// ctor/dtor, the +0x1364 record's destructor, the SystemManager-level helpers, the personality
// hook, SystemManager::Init and SystemManager::Update. The timer table, gun groups, RepairManager
// and ThrustManager live in their own common_sysmgr_* / common_repairmanager.cpp /
// common_thrustmanager.cpp files.

#include "common_systemmanager.h"
#include "AI/common_systemmanager_internal.h" // CmnSysMgr:: -- WriteFloatAt / IsWatcherLive / WatchedObject
#include "common_aisteer.h"   // SustainedThresholdTimer, RefreshWeaponFireControlConfig
#include "common_behavior.h"
#include "common_misc.h"      // SUBOBJ_ID_NONE
#include "common_iface.h"     // IBehaviorManager::Pathfind, IObjInspect, ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager::IsCruiseUnobstructed (FUN_062de160)
#include "common_iustategraph.h" // ClearAiTargetVector, SystemManagerTimerTable::BuildAiTargetList (ScanGunGroupTargets)
#include "common_cobject.h"    // CEqObj::EquipManager / get_behavior_interface (ScanGunGroupTargets)
#include "common_cequip.h"     // CELauncher::cast / GetBarrelPosWS (ScanGunGroupTargets)
#include "common_cobjdb.h"     // IObjDB::FindById (SystemManagerTimerTable::AppendTimerListEntry)
#include "dacom.h"             // DACOM_API

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <new>

using CmnSysMgr::WriteFloatAt;
using CmnSysMgr::IsWatcherLive;
using CmnSysMgr::WatchedObject;

// ============================================================================
// SystemManager
// ============================================================================
SystemManager::SystemManager()
    : OwnerObjectId(0)
    , Owner(nullptr)
    , Watcher()
    , ObjectDb(nullptr)
    , bLeaderParamsBorrowed(0)
    , TractorOwner(nullptr)
    , TractorTargetWatcher()
    , TractorEquipId(SUBOBJ_ID_NONE)
    , Repair()
    , PersonalityData()
{
    // FUN_062e6260. Retail order: Watcher zeroed, Thrust.Owner = 0, Timers (FUN_062e6af0),
    // GunGroups (FUN_062e9330), FormationLeaders (FUN_062ebbc0), Targeting
    // (FUN_062ed570), the +0x1498 tractor record's watcher and id, the RepairManager,
    // SelfTarget's guards and interface pointers, Personality, then Owner/ObjectDb/bLeaderParamsBorrowed
    // zeroed last. Owner gets its real value from SystemManager::Init (FUN_062e6490), called by
    // IBehaviorManager's ctor right after this one.
    //
    // Deviation (determinism only): OwnerObjectId, Pad15, the rest of the ThrustManager after its
    // Owner (InitThrustManager fills it), TractorOwner, RepairManager::Owner/CheckTimer and
    // SelfTarget's cached slices are not written by retail's ctor; zeroed here.
    std::memset(Pad15, 0, sizeof(Pad15));
    std::memset(&Thrust, 0, sizeof(Thrust));

    Timers.Construct();
    GunGroups.Construct();
    // FormationLeaders (+0x1364) and Targeting (+0x142C) are built as member
    // initializers.

    std::memset(Pad14A6, 0, sizeof(Pad14A6));

    // Retail writes only SelfTarget's four guards and two interface pointers.
    std::memset(&SelfTarget, 0, sizeof(SelfTarget));
}

// Retail has no out-of-line SystemManager destructor: FUN_062d2530 (IBehaviorManager's dtor)
// inlines it, and then frees the block. Thrust, GunGroups' arrays, TargetManager, the
// RepairManager and Personality have no destructor work in retail.
//
// The sub-objects with destructor work are RAII members (retail's ctor unwind funclets call
// their out-of-line destructors FUN_062d2700 / FUN_062d27d0), so they unwind in reverse
// declaration order, which is retail's inlined order: TractorTargetWatcher, the +0x1364 record
// (+0x14 then +0x0C), GunGroups' target watcher (Sys+0x134), Timers, Watcher.
SystemManager::~SystemManager()
{
}

// FUN_062d27d0: the member destructors of FollowLeaderWatcher (+0x14) then
// FormationLeaderWatcher (+0x0C).
FFormationLeaderState::~FFormationLeaderState()
{
}

// FUN_062e6990, thiscall on the SystemManager, RET 4: drops either leader watcher
// (Sys+0x1370 / +0x1378) that watches TargetId.
void SystemManager::ClearFormationLeaderWatcherIfTarget(int TargetId)
{
    BaseWatcher* const Watchers[2] =
    {
        &FormationLeaders.FormationLeaderWatcher, // Sys + 0x1370
        &FormationLeaders.FollowLeaderWatcher,    // Sys + 0x1378
    };

    for (int Index = 0; Index < 2; ++Index)
    {
        BaseWatcher* const W = Watchers[Index];
        IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(W->target()));
        if (Watched == nullptr)
            continue;
        if (static_cast<int>(Watched->get_id()) != TargetId)
            continue;
        W->set(nullptr);
    }
}

// FUN_062e68e0
void SystemManager::SetSharedListHead(IObjDB* NewHead)
{
    ObjectDb = NewHead;
    Timers.ObjectDb = NewHead;

    // Timers.Owner is dereferenced with no null test, as in retail.
    SystemManager* Owner = Timers.Owner;
    Watchable const* WatcherTarget = Owner->Watcher.target();
    if (WatcherTarget == nullptr || reinterpret_cast<std::uintptr_t>(WatcherTarget) == 8 || NewHead == nullptr)
        return;

    // Empty the cached AI target list so the next SystemManagerTimerTable::BuildAiTargetList rebuilds it (retail
    // inlines a zero-iteration vector::erase(begin, end)).
    Timers.bBuilt = 0;
    Timers.Targets.End = Timers.Targets.Begin;

    // Retail then tail-jumps to a TargetManager member (0x062ed730) whose body is an
    // ICF-folded `RET 4`; it writes nothing.
}

// ============================================================================
// FUN_062e6950 -- SystemManager's "personality changed" hook. Sole caller is
// IBehaviorManager::set_personality.
// ============================================================================
void SystemManager::NotifyPersonalityChanged()
{
    // FUN_062eb6c0
    (void)GunGroups.RefreshFromPersonality();

    // FUN_062e72d0
    (void)Timers.RefreshTimerThresholdsFromPersonality();

    // FUN_062ecfb0
    (void)Repair.RefreshFromPersonality();

    // JobParams.bForceAttackFormation (+0x1958) copied to Sys+0x1427; SystemManager::Update
    // makes the same copy.
    SystemManager* const StateOwner = FormationLeaders.Owner;
    if (StateOwner != nullptr)
        FormationLeaders.bForceAttackFormation = StateOwner->PersonalityData.JobParams.bForceAttackFormation;
}

// ============================================================================
// SystemManager initialisation and per-tick update.
// ============================================================================
// FUN_062e6490, RET 8. Every "if live" below is its own re-read of the watcher in retail.
bool SystemManager::Init(IObjRW* Object, IBehaviorManager* Manager)
{
    Watcher.set(Object != nullptr ? static_cast<Watchable const*>(Object) : nullptr);

    Owner = Manager;

    // The object is dereferenced without a test.
    OwnerObjectId = static_cast<int>(WatchedObject(Watcher)->get_id());

    (void)Timers.Arm(this);

    if (IsWatcherLive(Watcher))
    {
        // FAiTarget::Init inlined. Timers.Owner (just set by SystemManagerTimerTable::Arm, which
        // succeeds on this branch) is dereferenced blind.
        IObjRW* const Own = WatchedObject(Watcher);
        IObjRW* const TimersOwnerObject = WatchedObject(Timers.Owner->Watcher);
        SelfTarget.Init(TimersOwnerObject, Own);
    }

    if (IsWatcherLive(Watcher))
        InitThrustManager(&Thrust, this);

    if (IsWatcherLive(Watcher))
    {
        SystemManagerGunGroupManager& G = GunGroups;
        G.Owner = this;
        G.GunCount = 0;
        G.Field_0x834 = -1;
        (void)G.ResolveHardpoints();
        for (int Index = 0; Index < G.GunCount; ++Index)        // count re-read each pass
            G.RandomizeSlotTiming(&G.Guns[Index]);
        // 1.0f is DAT_063a07a4.
        float const Period = 1.0f;
        G.ScanPeriod = Period;
        G.ScanTimer = (float)rand() * (1.0f / 32768.0f) * Period;
    }

    if (IsWatcherLive(Watcher))
    {
        FormationLeaders.Owner = this;
    }

    (void)Targeting.Attach(this);

    if (IsWatcherLive(Watcher))
    {
        TractorOwner = this;
        (void)WatchedObject(Watcher)
            ->find_equipment(&TractorEquipId, 1, EquipTypeTractor);
    }

    if (IsWatcherLive(Watcher))
    {
        Repair.Owner = this;
        Repair.CheckTimer = 0.0f;
        Repair.Hull.Count = 0;
        Repair.Shield.Count = 0;
        (void)WatchedObject(Watcher)->enumerate_cargo(&Repair);
    }

    // FUN_062eb9d0 on Sys+0x1344.
    (void)GunGroups.CmDroppers.Bind(this);

    return true;
}

// FUN_062e66c0, RET 4.
void SystemManager::Update(float Dt)
{
    // Clear the per-tick cache latches while the watcher is live; Timers.Owner is
    // dereferenced blind.
    if (IsWatcherLive(Watcher))
    {
        IObjRW* const Own = WatchedObject(Watcher);
        IObjRW* const TimersOwnerObject = WatchedObject(Timers.Owner->Watcher);
        SelfTarget.Init(TimersOwnerObject, Own);          // FAiTarget::Init inlined
    }

    // Retail inlines bBuilt = 0 and a vector::erase(begin, end) that is ClearAiTargetVector.
    Timers.bBuilt = 0;
    ClearAiTargetVector(&Timers.Targets);

    Targeting.UpdateCommitTimer(Dt);
    GunGroups.UpdateTimers(Dt);
    GunGroups.CmDroppers.Update(Dt);

    // FUN_062de160 -- the cruise gate engage_cruise reads.
    Thrust.bCruiseUnobstructed =Thrust.Owner->Owner->Pathfind->IsCruiseUnobstructed() ? 1 : 0;

    // In cruise with the gate closed: disengage. The watched object is dereferenced blind.
    bool bCruise = false;
    (void)WatchedObject(Thrust.Owner->Watcher)->is_cruise_active(bCruise);
    if (bCruise && Thrust.bCruiseUnobstructed == 0)
        Thrust.engage_cruise(false);

    Timers.Update(Dt);

    // Every three seconds (strictly past 3.0f, DAT_063a07b0) the cargo is re-tallied.
    RepairManager& Repair = this->Repair;
    double const CheckTimer = static_cast<double>(Dt) + static_cast<double>(Repair.CheckTimer);
    Repair.CheckTimer = static_cast<float>(CheckTimer);
    if (CheckTimer > 3.0)
    {
        Repair.CheckTimer = 0.0f;
        Repair.Hull.Count = 0;
        Repair.Shield.Count = 0;
        (void)WatchedObject(Repair.Owner->Watcher)->enumerate_cargo(&Repair);
    }
    Repair.Hull.Update(Dt, WatchedObject(Repair.Owner->Watcher));
    Repair.Shield.Update(Dt, WatchedObject(Repair.Owner->Watcher));

    FormationLeaders.Update(Dt);

    // The personality-refresh latch. bLeaderParamsBorrowed is set by
    // RepairManager::RefreshFromPersonality / RefreshWeaponFireControlConfig when they copied
    // another ship's parameters; it is consumed here once that borrowing no longer applies.
    if (bLeaderParamsBorrowed == 0)
        return;
    if (SelfTarget.Iface->is_player())
        return;
    if (PersonalityData.FormationParams.bLeaderMakesMeTougher == 0)   // byte +0x1820
        return;
    if (FormationLeaders.bLeaderSuppressed == 0)
    {
        // A live formation leader keeps the borrowed parameters.
        Watchable const* const Node = FormationLeaders.FormationLeaderWatcher.target();
        if (Node != nullptr && Node != reinterpret_cast<Watchable const*>(8))
            return;
    }

    bLeaderParamsBorrowed = 0;
    PersonalityData.FormationParams.bLeaderMakesMeTougher = 0;
    (void)GunGroups.RefreshFromPersonality();
    (void)Timers.RefreshTimerThresholdsFromPersonality();
    (void)this->Repair.RefreshFromPersonality();

    // The same copy SystemManager::NotifyPersonalityChanged makes.
    SystemManager* const StateOwner = FormationLeaders.Owner;
    if (StateOwner != nullptr)
        FormationLeaders.bForceAttackFormation = StateOwner->PersonalityData.JobParams.bForceAttackFormation;
}
