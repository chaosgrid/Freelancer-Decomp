// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- the 0xC8-byte record at SystemManager+0x1364 (FFormationLeaderState,
// common_systemmanager.h): the owning ship's formation / follow-leader state.
// Retail run 0x062ebbc0-0x062ecd30; every function here is reached with ECX = Sys + 0x1364.

#include "common_systemmanager.h"
#include "common_cobject.h"  // CEqObj::get_behavior_interface (SyncFormationLeader)
#include "common_iface.h"    // IObjRW, IBehaviorManager, ObjInspectOf, ObjRwFromWatchable, ObjAffectOf
#include "AI/common_behavior_internal.h" // CmnBehavior::OwnerIsPlayer (gates A / B)

using CmnBehavior::OwnerIsPlayer;

namespace
{
    // Retail's `W != 0 && W - 8 != 0` validity test on an embedded watcher.
    bool IsWatchingObject(BaseWatcher const& Watcher)
    {
        Watchable const* const W = Watcher.target();
        return W != nullptr && W != (Watchable const*)8;
    }

    IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }
}

// FUN_062ecaf0. +0xC5 is the gate byte (non-zero -> null); the follow-leader slot +0x14 wins
// over the formation-leader slot +0x0C unless it holds one of the sentinels 0 / 8; the raw
// Watchable value minus 8 is the IObjRW (a 0 slot stays null).
IObjRW* FFormationLeaderState::GetCurrentLeader() const
{
    if (bLeaderSuppressed != 0)
        return nullptr;
    Watchable const* Slot = FollowLeaderWatcher.target();
    if (Slot == nullptr || Slot == (Watchable const*)8)
        Slot = FormationLeaderWatcher.target();
    return ObjRwFromWatchable(const_cast<Watchable*>(Slot));
}

// FUN_062ec6a0.
void FFormationLeaderState::SuppressCurrentLeader()
{
    bLeaderSuppressed = 1;
}

// FUN_062ebbc0. The two BaseWatcher members zero +0x0C..+0x1B. Owner, FollowerScratch and
// +0xBC are not written. Retail's two `set_pointer(nullptr)` calls here are dead for a fresh
// construction (both watchers were just zeroed) and are not reproduced.
FFormationLeaderState::FFormationLeaderState()
{
    FormationNarrowAngle = 0.1f;   // 0x3dcccccd
    FormationWideAngle = 0.2f;     // 0x3e4ccccd
    Field_0xA4 = 200.0f;           // 0x43480000

    bBreakingFormation = 0;
    BreakFormationTimer = 0.0f;
    ForceAttackCycleTimer = 0.0f;
    bOrderPending = 0;
    bValueB0Valid = 0;
    ValueB0 = 0;
    bOrderCommitDirectly = 0;

    bValueB4Valid = 0;
    ValueB4 = 0;
    OrderLaneParam = 0;
    OrderObjectId = 0;
    OrderSecondObjectId = 0;
    bLeaderIdleFormation = 0;
    bForceAttackFormation = 0;
    bLeaderSuppressed = 0;
    bBreakApartNotified = 0;
    bReleaseRequested = 0;
}

// FUN_062ebcb0.
void FFormationLeaderState::Update(float Dt)
{
    // [this+8] is the owning SystemManager (re-read at every use in retail; nothing here
    // writes it). Its +0x1429 / +0x1370 are this record's own bLeaderSuppressed /
    // FormationLeaderWatcher, read through it.
    SystemManager* const Sys = Owner;
    pub::AI::Personality::FormationUseStruct const& Formation = Sys->PersonalityData.FormationParams;

    // While an order is pending, a leader that is no longer using a trade lane
    // (is_using_tradelane, +0x134) clears it. A target of 0 or 8 skips the call.
    if (bOrderPending != 0 && bLeaderSuppressed == 0)
    {
        Watchable const* Watched = FollowLeaderWatcher.target();
        if (Watched == nullptr || ObjRwFromWatchable(const_cast<Watchable*>(Watched)) == nullptr)
            Watched = FormationLeaderWatcher.target();

        if (Watched != nullptr)
        {
            IObjRW* const Leader = ObjRwFromWatchable(const_cast<Watchable*>(Watched));
            if (Leader != nullptr)
            {
                bool bUsing = false;
                (void)Leader->is_using_tradelane(&bUsing);
                if (!bUsing)
                    bOrderPending = 0;
            }
        }
    }

    // Retail keeps this in a register for the next three blocks.
    unsigned char const Active = bForceAttackFormation;
    if (Active != 0)
    {
        // The unrounded sum is compared with the unrounded limit; only strictly greater
        // zeroes it.
        double const Sum = (double)Dt + (double)ForceAttackCycleTimer;
        ForceAttackCycleTimer = (float)Sum;
        double const Limit = (double)Formation.ForceAttackFormationUnactiveTime
                           + (double)Formation.ForceAttackFormationActiveTime;
        if (Sum > Limit)
            ForceAttackCycleTimer = 0.0f;

        if (Sys->FormationLeaders.bLeaderSuppressed == 0 &&
            IsWatchingObject(Sys->FormationLeaders.FormationLeaderWatcher))
            bBreakingFormation = 0;
    }

    // Sys+0xEC: the break-formation damage trigger (timer table slot 9).
    if (Sys->Timers.Timers[9].Active != 0 && Active != 0 &&
        Sys->FormationLeaders.bLeaderSuppressed == 0)
    {
        if (IsWatchingObject(Sys->FormationLeaders.FormationLeaderWatcher))
            bBreakingFormation = 1;
    }

    // Sys+0x128: the timer table's BestValueSoFar, against the missile-reaction times.
    float const MissileValue = Sys->Timers.BestValueSoFar;
    if (MissileValue > 0.0f)
    {
        float const Threshold = Sys->Timers.Owner->PersonalityData.FormationParams.BreakFormationMissileReactionTime;
        if (MissileValue <= Threshold && Active != 0 && Sys->FormationLeaders.bLeaderSuppressed == 0)
        {
            if (IsWatchingObject(Sys->FormationLeaders.FormationLeaderWatcher))
                bBreakingFormation = 1;
        }
    }

    // Sys+0xFC: the break-apart damage trigger (timer table slot 10).
    if (Sys->Timers.Timers[10].Active == 0)
    {
        // Missile reaction: [[Sys+0x38]+0x17e8]; unordered fails. A failed test clears
        // bBreakApartNotified.
        bool DoPulse = false;
        if (MissileValue > 0.0f)
        {
            float const Threshold =
                Sys->Timers.Owner->PersonalityData.FormationParams.BreakApartFormationMissileReactionTime;
            DoPulse = MissileValue <= Threshold;
        }

        if (!DoPulse)
        {
            bBreakApartNotified = 0;
        }
        else if (bBreakApartNotified == 0)
        {
            // Duration is Sys's own +0x17e8, not the [Sys+0x38] copy compared above.
            // get_formation_leader (+0xB0) on the object behind Sys's Watcher, no null test.
            bBreakApartNotified = 1;
            float const Duration = Formation.BreakApartFormationMissileReactionTime;
            IObjRW* Leader = nullptr;
            IObjRW* const Own = WatchedObject(Sys->Watcher);
            (void)Own->get_formation_leader(Leader);
            if (Leader != nullptr)
            {
                // Receiver is Sys->Owner, the IBehaviorManager.
                Sys->Owner->QueueNotification(Leader, 1, 0.0f);
                // Only strictly greater makes the second call.
                if (Duration > 0.0f)
                    Sys->Owner->QueueNotification(Leader, 2, Duration);
            }
        }
    }
    else if (bBreakApartNotified == 0)
    {
        // Duration is Sys+0x17d8; same leader query and the same gate on the second call.
        bBreakApartNotified = 1;
        float const Duration = Formation.BreakFormationDamageTriggerTime;
        IObjRW* Leader = nullptr;
        IObjRW* const Own = WatchedObject(Sys->Watcher);
        (void)Own->get_formation_leader(Leader);
        if (Leader != nullptr)
        {
            Sys->Owner->QueueNotification(Leader, 1, 0.0f);
            if (Duration > 0.0f)
                Sys->Owner->QueueNotification(Leader, 2, Duration);
        }
    }

    if (bBreakingFormation != 0)
        BreakFormationTimer = (float)((double)Dt + (double)BreakFormationTimer);
    else
        BreakFormationTimer = 0.0f;
}

// ----------------------------------------------------------------------------
// The two leader watchers. +0x0C watches the FORMATION leader, +0x14 the FOLLOW leader;
// the bytes +0xBC..+0xC5 are per-leader state dropped whenever the watched leader changes
// identity.
// ----------------------------------------------------------------------------

// Nine byte stores of 0 to +0xBC..+0xC4, inlined in both callers.
static void ClearLeaderStateBytes(FFormationLeaderState& B)
{
    B.Field_0xBC = 0;
    B.bOrderPending = 0;
    B.bValueB0Valid = 0;
    B.bValueB4Valid = 0;
    B.bBreakingFormation = 0;
    B.bLeaderIdleFormation = 0;
    B.bOrderCommitDirectly = 0;
    B.bForceAttackFormation = 0;
    B.bBreakApartNotified = 0;
}

// FUN_062ec870. The get_id call orders below are retail's.
void FFormationLeaderState::SetFormationLeader(IObjRW* Target)
{
    if (Target != nullptr)
    {
        // Our own ship's id first, then Target's.
        unsigned int const SelfId = Owner->SelfTarget.Iface->get_id();
        if (Target->get_id() == SelfId)
            return;

        // Target's id first, then the watched leader's.
        if (IsWatchingObject(FormationLeaderWatcher))
        {
            unsigned int const TargetId = Target->get_id();
            if (WatchedObject(FormationLeaderWatcher)->get_id() != TargetId)
                bLeaderSuppressed = 0;
        }

        // The watched leader's id first, then Target's.
        bool bSameLeader = false;
        if (IsWatchingObject(FormationLeaderWatcher))
        {
            unsigned int const WatchedId = WatchedObject(FormationLeaderWatcher)->get_id();
            bSameLeader = Target->get_id() == WatchedId;
        }

        if (!bSameLeader)
        {
            ClearLeaderStateBytes(*this);
            bLeaderSuppressed = 0;
        }
    }
    else
    {
        bLeaderSuppressed = 0;
    }

    FormationLeaderWatcher.set(static_cast<Watchable*>(Target));
}

// FUN_062ec970.
void FFormationLeaderState::SetFollowLeader(IObjRW* Target)
{
    if (Target != nullptr)
    {
        // Target is our own ship: stop following.
        unsigned int const SelfId = Owner->SelfTarget.Iface->get_id();
        if (Target->get_id() == SelfId)
        {
            FollowLeaderWatcher.set(nullptr);
        }
        else
        {
            // The watched leader's id first, then Target's (both tests).
            if (IsWatchingObject(FollowLeaderWatcher))
            {
                unsigned int const WatchedId = WatchedObject(FollowLeaderWatcher)->get_id();
                if (WatchedId != Target->get_id())
                    bLeaderSuppressed = 0;
            }

            bool bSameLeader = false;
            if (IsWatchingObject(FollowLeaderWatcher))
            {
                unsigned int const WatchedId = WatchedObject(FollowLeaderWatcher)->get_id();
                bSameLeader = Target->get_id() == WatchedId;
            }

            if (!bSameLeader)
            {
                ClearLeaderStateBytes(*this);
                bLeaderSuppressed = 0;
            }

            FollowLeaderWatcher.set(static_cast<Watchable*>(Target));
        }
    }
    else
    {
        bLeaderSuppressed = 0;
        FollowLeaderWatcher.set(nullptr);
    }

    // Tail JMP to the owning ship's IObjAffect slot 9 (+0x24), `[[this+8]+8] - 8 + 4`. The
    // owning ship is not null-tested; retail's null arm reads [4].
    IObjRW* const FollowLeader = WatchedObject(FollowLeaderWatcher);
    IObjRW* const OwnerObj = WatchedObject(Owner->Watcher);
    static_cast<IObjAffect*>(OwnerObj)->set_follow_leader(FollowLeader);
}

// FUN_062ec6b0.
void FFormationLeaderState::SyncFormationLeader()
{
    if (Owner != nullptr && IsWatchingObject(Owner->Watcher))
    {
        IObjRW* const OwnerObj = WatchedObject(Owner->Watcher);

        IObjRW* FormationLeader = nullptr;
        OwnerObj->get_formation_leader(FormationLeader); // +0xB0

        if (FormationLeader != nullptr)
        {
            // Our own ship's id first, then the leader's.
            unsigned int const SelfId = Owner->SelfTarget.Iface->get_id();
            bool const bLeaderIsSelf = FormationLeader->get_id() == SelfId;

            if (!bLeaderIsSelf && !FormationLeader->is_dying()) // +0x38
            {
                SetFormationLeader(FormationLeader);
                bLeaderIdleFormation = 0;

                // The CEqObj cast (null when the class mask misses) feeds
                // get_behavior_interface with no null test -- it reads [this+0x194].
                CObject const* const LeaderObj = FormationLeader->cobject(); // +0x150
                CEqObj* LeaderEq = nullptr;
                if (LeaderObj != nullptr && (LeaderObj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
                    LeaderEq = (CEqObj*)LeaderObj;

                IBehaviorManager* const LeaderMgr = LeaderEq->get_behavior_interface();
                if (LeaderMgr == nullptr)
                {
                    bReleaseRequested = 0;
                    return;
                }

                int const* const Directive = (int const*)LeaderMgr->get_current_directive();
                bool const bIdleFormation = Directive[0] == 1 && Directive[1] == 0;
                bReleaseRequested = 0;
                bLeaderIdleFormation = bIdleFormation ? 1 : 0;
                return;
            }

            // is_dying is asked again (a second call on the dying path).
            if (FormationLeader->is_dying())
            {
                IObjRW* const Ship = WatchedObject(Owner->Watcher);
                static_cast<IObjAffect*>(Ship)->slot_6c(5, 0); // +0x6C
                SetFormationLeader(nullptr);
                bReleaseRequested = 0;
                return;
            }
        }
    }

    // The owning ship is re-read without a null test (retail's null arm reads [4]).
    if (bReleaseRequested == 0 && bLeaderSuppressed == 0 && IsWatchingObject(FormationLeaderWatcher) &&
        bValueB0Valid == 0)
    {
        IObjRW* const Ship = WatchedObject(Owner->Watcher);
        static_cast<IObjAffect*>(Ship)->slot_6c(5, 0);
    }

    SetFormationLeader(nullptr);
    bReleaseRequested = 0;
}

// ----------------------------------------------------------------------------
// FFormationLeaderState accessors; every retail call site passes
// ECX = SystemManager + 0x1364.
// ----------------------------------------------------------------------------

// FUN_062ec590, RET 8. The flag is stored as given, not derived from the value. Sole caller:
// IBehaviorManager::OnEvent case 5.
void FFormationLeaderState::SetValueB4(unsigned int Value, unsigned char Flag)
{
    ValueB4 = Value;
    bValueB4Valid = Flag;
}

// FUN_062ec5b0. Single call site: BaseFollowerBehavior::OnUpdate (0x062cb8ec).
unsigned char FFormationLeaderState::TryGetValueB4(unsigned int* Out) const
{
    unsigned char const Valid = bValueB4Valid;
    if (Valid)
        *Out = ValueB4;
    return Valid;
}

// FUN_062ec5d0, RET 0x14. Sole call site: IBehaviorManager::OnEvent case 4.
void FFormationLeaderState::SetDirectiveOrder(unsigned char P1, unsigned int P2, unsigned int P3,
                                                  unsigned char P4, unsigned int P5)
{
    bOrderPending = P1;
    OrderObjectId = P2;
    OrderSecondObjectId = P3;
    bOrderCommitDirectly = P4;
    OrderLaneParam = P5;
}

// FUN_062ec610: packs the order into the caller's 0x10-byte block. Sole call site:
// BaseFollowerBehavior::OnUpdate (0x062cbac4, its FollowerDirectiveOrder block).
void FFormationLeaderState::GetDirectiveOrder(unsigned char* Out) const
{
    *(unsigned int*)(Out + 0x0) = OrderObjectId;
    *(unsigned int*)(Out + 0x4) = OrderSecondObjectId;
    Out[0x8] = bOrderPending;
    Out[0x9] = bOrderCommitDirectly;
    *(unsigned int*)(Out + 0xc) = OrderLaneParam;
}

// FUN_062ec650, RET 4. Callers: IBehaviorManager::OnEvent case 3 and
// BaseFollowerBehavior::OnUpdate (.text:062cbb18).
void FFormationLeaderState::SetValueB0(unsigned int Value)
{
    if (Value == 0)
    {
        ValueB0 = 0;
        bValueB0Valid = 0;
        return;
    }
    ValueB0 = Value;
    bValueB0Valid = 1;
}

// FUN_062ec680, RET 4. The getter half of FUN_062ec650; BaseFollowerBehavior::OnUpdate is its
// only caller.
bool FFormationLeaderState::TryGetValueB0(unsigned int* Out) const
{
    if (bValueB0Valid != 0)
        *Out = ValueB0;
    return bValueB0Valid != 0;
}

// FUN_062ecc00, RET 0. Gate B.
bool FFormationLeaderState::FollowerDirectiveGateB() const
{
    SystemManager* const Sys = Owner;
    if (OwnerIsPlayer(Sys))
        return false;
    if (bBreakingFormation == 0)
        return false;
    // Sys+0x181c; strictly less (unordered is false).
    return BreakFormationTimer < Sys->PersonalityData.FormationParams.FormationExitMaxTime;
}

// FUN_062ecb20, RET 0. Gate A. Callers: BuzzBehavior::EvaluateReadiness (.text:062c6121),
// EvadeBehavior (062c7b36), FaceBehavior (062c8c6a), StrafeBehavior (062c9735), TrailBehavior
// (062ca834) and BaseFollowerBehavior::OnUpdate (062cbc64), all on SysMgr+0x1364.
bool FFormationLeaderState::FollowerDirectiveGateA() const
{
    SystemManager* const Sys = Owner;   // [ESI+8], no null test
    if (OwnerIsPlayer(Sys))
        return false;
    // Sys+0x1958: JobParams.bForceAttackFormation.
    if (Sys->PersonalityData.JobParams.bForceAttackFormation == 0)
        return false;
    // Sys+0x1429 is this record's +0xc5 (0x1364 + 0xc5 == 0x1429) -- the same gate byte
    // FUN_062ecaf0 reads.
    if (bLeaderSuppressed != 0)
        return false;
    // Sys+0x1370 is +0xc: the formation-leader watcher, downcast to the whole IObjRW by -8.
    IObjRW* const Watched = WatchedObject(FormationLeaderWatcher);
    if (Watched == nullptr)
        return false;

    if (bBreakingFormation == 0 && bForceAttackFormation != 0)
    {
        IBehaviorManager* const Mgr = GetBehaviorManager(Watched);
        // The leader's current behavior.
        if (Mgr != nullptr && Mgr->CurrentBehaviorIndex == 0xf)
            return false;
        // Sys+0x17cc; strictly greater or unordered is false.
        return ForceAttackCycleTimer <= Sys->PersonalityData.FormationParams.ForceAttackFormationActiveTime;
    }

    // Sys->Owner (the IBehaviorManager), no null test.
    if (Sys->Owner->CurrentBehaviorIndex != 0xa)
        return false;
    if (OwnerIsPlayer(Sys))
        return false;
    if (bBreakingFormation == 0)
        return false;
    // Same test as gate B.
    return BreakFormationTimer < Sys->PersonalityData.FormationParams.FormationExitMaxTime;
}

// The formation leader of the ship the record belongs to: SystemManager::Watcher's object
// (-8), get_formation_leader (+0xb0) into a zeroed local. Retail dispatches through the
// downcast without testing it.
static IObjRW* FormationLeaderOfRecordOwner(SystemManager* Sys)
{
    IObjRW* const Self = WatchedObject(Sys->Watcher);
    IObjRW* Leader = nullptr;
    Self->get_formation_leader(Leader);
    return Leader;
}

// FUN_062ecc60, RET 4.
void FFormationLeaderState::NotifyFormationLeader1(float Delay)
{
    IObjRW* const Leader = FormationLeaderOfRecordOwner(Owner);
    if (Leader == nullptr)
        return;
    Owner->Owner->QueueNotification(Leader, 1, 0.0f);
    // The second notification needs Delay > 0 (unordered skips).
    if (Delay > 0.0f)
        Owner->Owner->QueueNotification(Leader, 2, Delay);
}

// FUN_062ecce0, RET 0.
void FFormationLeaderState::NotifyFormationLeader2()
{
    IObjRW* const Leader = FormationLeaderOfRecordOwner(Owner);
    if (Leader == nullptr)
        return;
    Owner->Owner->QueueNotification(Leader, 2, 0.0f);
}

// FUN_062ecc50, RET 4.
void FFormationLeaderState::SetFlagC3(unsigned char Value)
{
    bForceAttackFormation = Value;   // +0xc3
}

// FUN_062ecad0: the follow leader as an IObjRW, or null when the +0xc5 gate byte is set.
IObjRW* FFormationLeaderState::GetFollowLeader() const
{
    if (bLeaderSuppressed != 0)
        return nullptr;
    return WatchedObject(FollowLeaderWatcher);
}

// FUN_062ecd30, RET 0: the record owner's ship gets IObjAffect slot 0x60 (1, &own id, -1).
// With no watched ship retail dispatches through address 4 and faults; the port dispatches
// through the same null-derived pointer, with no guard.
int FFormationLeaderState::RequestRelease()
{
    SystemManager* const Sys = Owner;
    bReleaseRequested = 1;
    unsigned int const Id = Sys->SelfTarget.Iface->get_id();
    IObjRW* const Self = WatchedObject(Sys->Watcher);
    return ObjAffectOf(Self)->request_cancel(1, &Id, -1);
}

// FUN_062ecab0 (3 call sites): the formation leader, or null when the +0xc5 gate byte is set.
IObjRW* FFormationLeaderState::GetFormationLeader() const
{
    if (bLeaderSuppressed != 0)
        return nullptr;
    return WatchedObject(FormationLeaderWatcher);
}

// FUN_062ec520, RET 4. Callers: GotoBehavior::Update and GuideBehavior::Update (SysMgr +
// 0x1364).
//
// The owning SystemManager is dereferenced with no test; only its watched object is tested.
// get_formation_followers (+0x14C) fills FollowerScratch (MaxCount 0x20).
//
// Retail quirk: the count guard (`TEST EBX,EBX / JBE`) rejects only an exactly-zero count and
// the loop compares unsigned, so the count is modelled as unsigned.
bool FFormationLeaderState::IsIdAmongOwnerFormationFollowers(unsigned WantedId)
{
    IObjRW* const OwnerObj = WatchedObject(Owner->Watcher);
    if (OwnerObj == nullptr)
        return false;

    unsigned const Count = (unsigned)OwnerObj->get_formation_followers(FollowerScratch, 0x20);
    if (Count == 0)
        return false;

    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (FollowerScratch[Index] != nullptr && FollowerScratch[Index]->get_id() == WantedId)
            return true;
    }
    return false;
}
