// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- BaseFollowerBehavior (the shared base of FormationBehavior and FollowBehavior):
// ctor/dtor, the per-tick OnUpdate body and its slot overrides.
// The follower helpers it calls live in common_behavior.cpp (common_behavior_internal.h).

#include "common_behavior.h"
#include "AI/common_behavior_internal.h" // CmnBehavior:: follower helpers (common_behavior.cpp)
#include "common_aisteer.h" // GetIfacePositionOrZero, IsWithinInterceptRangeOfIface
#include "common_systemmanager.h" // SystemManager -- BaseFollowerBehavior::SysMgr's real type
#include "common_misc.h" // SinglePlayer() -- OnUpdate's multiplayer group test
#include "common_cobject.h" // CShip::bIsPlayer / GroupId
#include "common_csys.h"  // Matrix, Transform
#include "common_iface.h" // IObjInspect / IObjRW / ObjInspectOf / ObjRwFromWatchable
#include "x86math.h"      // FVector, FMatrix
#include "common_pathfind.h" // PathfindManager
#include "common_dockbehavior.h" // DockBehavior::SetDestPointOffset (FUN_062c50c0)

#include <cmath>
#include <cstdint>
#include <cstring>

using CmnBehavior::OwnerIsPlayer;
using CmnBehavior::ShipOrNull;

// ============================================================================
// BaseFollowerBehavior
// ============================================================================

// FUN_062cb2b0. FUN_062d2020(TypeId) is Behavior's ctor; the five Ideas are constructed in
// declaration order. Retail leaves +0x288, +0x290..+0x29b and +0x29c unwritten; the port
// zeroes them.
BaseFollowerBehavior::BaseFollowerBehavior(int InTypeId)
    : Behavior(InTypeId)
    , LeadWatcher()
    , MatchThrottle()
    , Face()
    , Move()
    , BreakAway()
    , Afterburner()
    , DockDelegate(nullptr)
    , GotoDelegate(nullptr)
    , bLeftTradeLane(0)
    , bInTradeLane(0)
    , bDockDelegateActive(0)
    , bPlayerCatchUp(0)
    , bGotoDelegateActive(0)
    , Flag285(0)
    , Pad286{}
    , Field288(0)
    , Flag28c(0)
    , Pad28d{}
    , Offset290{ 0.0f, 0.0f, 0.0f }
    , Field29c(0)
{
}

// FUN_062cb3b0: only the members' own BaseWatcher destructors (BreakAway +0x1f0, Move +0x164,
// Face +0xa4, MatchThrottle +0x48, LeadWatcher +0x2c), then FUN_062d2080 (~Behavior).
BaseFollowerBehavior::~BaseFollowerBehavior()
{
}

// ============================================================================
// BaseFollowerBehavior::OnUpdate -- LAB_062cb700 (no Ghidra function; read off the listing).
//
// The per-tick body of the whole BaseFollowerBehavior subtree: slot 7 of .rdata:0639fb00
// (BaseFollowerBehavior), 0639fb80 (FormationBehavior) and 0639fc00 (FollowBehavior) all hold
// this address. IBehaviorManager::UpdateBehaviorTick stores the tick delta into +0x1c first.
//
// In retail's order:
//   1. Bail out (result 3) with no owner; bail out (result 0) if the "order consumed"
//      latch at +0x285 is set.
//   2. Re-point LeadWatcher at whatever the SystemManager's AI-directive record
//      (SystemManager+0x1364) currently names as the override target.
//   3. Skip the whole validation block when the SystemManager's external interface
//      reports is_player() false.
//   4. Validate the watched leader: it must exist; in multiplayer, if both the owner
//      and the leader are player ships they must share the same group id; the leader's
//      get_type() bits must have something in 0x7f0000 and nothing in 0x200000; its
//      get_behavior_id() must not be 0x11 or 8; and walking its follow/formation leader
//      chain must not come back round to the owner. Every failure yields result 3.
//   5. If the record has a pending interrupt (its +0xbf latch), reset both idea
//      sub-objects and yield result 1.
//   6. Run the child goto delegate (+0x27c) while the +0x283 latch is set and the
//      leader is close and slow, and the child dock delegate (+0x278) while +0x282 is
//      set; either may end the tick with its own result.
//   7. Otherwise read the record's current dock order, and either hand it to the dock
//      delegate as a pub::AI::DirectiveDockOp, or -- when already inside the dock
//      candidate's physical radius -- commit it through the owner's IObjAffect slot
//      0x64.
//   8. With nothing pending at all, pick between the subclass's own UpdateFollowerSteering() and the
//      final-leg payload FUN_062cbcf0 on three gate checks.
//
// This is the only caller of the two virtuals BaseFollowerBehavior adds to Behavior's 27:
//   * UpdateFollowerSteering() (+0x6c): no argument pushed, result unused -> void().
//   * GetRotatedOffset() (+0x70): hidden return buffer handed to FUN_062c50c0 -> Vector().
// ============================================================================

// FFormationLeaderState members used here (common_gunconvergence.cpp): FUN_062ecaf0
// GetCurrentLeader, 062ec5b0 TryGetValueB4, 062ec610 GetDirectiveOrder, 062ec650 SetValueB0,
// 062ec680 TryGetValueB0, 062ecb20/062ecc00 FollowerDirectiveGateA/B. FUN_062e74a0 is
// SystemManagerTimerTable::FindVerifiedObject; FUN_062c50c0 is DockBehavior::SetDestPointOffset.
// DockDelegate is BehaviorDispatchTable[7] (Dock).

// The 0x10-byte block FUN_062ec610 (GetDirectiveOrder) copies out of the AI-directive record:
// record +0xa8 -> +0x00, +0xac -> +0x04, +0xbd -> +0x08, +0xc2 -> +0x09, +0xb8 -> +0x0c.
struct FollowerDirectiveOrder
{
    unsigned int DockObjectId;      // record +0xa8; dock candidate id, -> DirectiveDockOp::DockObjectId
    unsigned int SecondaryObjectId;      // record +0xac; second object id, -> DirectiveDockOp::NextRingId
    unsigned char bOrderPending;     // record +0xbd
    unsigned char bMidLane;     // record +0xc2; 1 = commit without the range test; go_tradelane's bool
    unsigned char Pad0A[2];
    float MaxRange;             // record +0xb8; go_tradelane's 4th argument (CShip::go_tradelane float)
};
static_assert(sizeof(FollowerDirectiveOrder) == 0x10, "FollowerDirectiveOrder must match FUN_062ec610's 0x10-byte output block");


int BaseFollowerBehavior::OnUpdate()
{
    IObjRW* const Owner = OwnerContext;
    if (Owner == nullptr)
    {
        UpdateResult = 3;
        return 3;
    }

    if (Flag285 != 0)
    {
        UpdateResult = 0;
        return 0;
    }

    SystemManager* const Sys = SysMgr;   // not null-tested from here on (as retail)
    FFormationLeaderState* const Rec = &Sys->FormationLeaders;   // Sys + 0x1364

    // FUN_062ecaf0 returns an IObjRW* (or 0); retail adds 8 back to get the Watchable
    // sub-object and re-points only on change, which is BaseWatcher::set()'s own guard.
    {
        IObjRW* const Override = Sys->FormationLeaders.GetCurrentLeader();   // FUN_062ecaf0(Rec)
        LeadWatcher.set(static_cast<Watchable const*>(Override));   // null stays null
    }

    if (OwnerIsPlayer(Sys))
    {
        IObjRW* Node = ObjRwFromWatchable(
            const_cast<Watchable*>(LeadWatcher.target()));
        if (Node == nullptr)
        {
            UpdateResult = 3;
            return 3;
        }

        // Multiplayer only: when both ends are player ships, their group ids must both be
        // non-zero and equal.
        if (!SinglePlayer())
        {
            CShip const* const OwnerShip = ShipOrNull(Owner->cobject());
            CShip const* const LeaderShip = ShipOrNull(Node->cobject());
            if (OwnerShip != nullptr && LeaderShip != nullptr &&
                OwnerShip->bIsPlayer != 0 && LeaderShip->bIsPlayer != 0)
            {
                unsigned int const OwnerGroup = OwnerShip->GroupId;
                unsigned int const LeaderGroup = LeaderShip->GroupId;
                if (OwnerGroup == 0 || LeaderGroup == 0 || OwnerGroup != LeaderGroup)
                {
                    UpdateResult = 3;
                    return 3;
                }
            }
        }

        // get_type (IObjInspect +0x88) returns 0 on success.
        unsigned int TypeBits = 0;
        if (Node->get_type(TypeBits) != 0)
        {
            UpdateResult = 3;
            return 3;
        }
        if ((TypeBits & OBJ_SHIP_MASK) == 0 || (TypeBits & OBJ_CAPITAL) != 0)
        {
            UpdateResult = 3;
            return 3;
        }
        int const BehaviorId = Node->get_behavior_id();
        if (BehaviorId == BEHAVIOR_FOLLOW || BehaviorId == BEHAVIOR_LAUNCH)
        {
            UpdateResult = 3;
            return 3;
        }

        // Walk the leader chain: follow-leader first, formation-leader only when that left
        // the out-param null. Retail zeroes the out-param once, before the loop, so a getter
        // that declines to write leaves the previous leader in place.
        IObjRW* Leader = nullptr;
        for (;;)
        {
            Node->get_follow_leader(Leader);
            if (Leader == nullptr)
                Node->get_formation_leader(Leader);
            if (Leader == Node)
                break;
            if (Leader != nullptr)
            {
                // Retail calls the owner's get_id first, then the leader's.
                unsigned int const OwnerId = Owner->get_id();
                if (Leader->get_id() == OwnerId)
                {
                    UpdateResult = 3;
                    return 3;
                }
            }
            Node = Leader;
            if (Node == nullptr)
                break;
        }
    }

    // FUN_062ec5b0 answers the record's +0xbf latch and, when set, copies +0xb4 out;
    // retail never reads that copy.
    {
        unsigned int PendingB4 = 0;
        if (Rec->TryGetValueB4(&PendingB4) != 0)
        {
            MatchThrottle.Commit();
            Face.Commit();
            UpdateResult = 1;
            return 1;
        }
    }

    IObjRW* const Watched = ObjRwFromWatchable(
        const_cast<Watchable*>(LeadWatcher.target()));
    if (Watched == nullptr)
    {
        UpdateResult = 2;
        return 2;
    }

    if (bPlayerCatchUp != 0)
    {
        // Every non-returning path ends at LAB_062cba4a, which clears the latch.
        if (!IsWithinInterceptRangeOfIface(&Sys->SelfTarget, Watched, 200.0f))
        {
            // Speed-squared against DAT_0639fb58 (100.0f, i.e. speed 10). The JP is taken
            // on ">=-or-unordered" and clears the latch, so the goto delegate runs only
            // while the leader is strictly slower. The sum stays on the x87 stack, hence double.
            Vector const Vel = Watched->get_velocity();
            double const SpeedSq = (static_cast<double>(Vel.x) * Vel.x
                                    + static_cast<double>(Vel.y) * Vel.y)
                                   + static_cast<double>(Vel.z) * Vel.z;
            if (SpeedSq < 100.0)
            {
                if (bGotoDelegateActive == 0)
                {
                    pub::AI::DirectiveGotoOp Op;
                    Op.GotoKind = 0;
                    Op.TargetId = static_cast<int32_t>(Watched->get_id());
                    Op.ArrivalRange = 100.0f;   // 0x42c80000
                    GotoDelegate->OnApplyDirective(&Op);
                    GotoDelegate->OnEngage();
                    // Retail then tests the literal 1 it just stored; that branch is dead.
                    bGotoDelegateActive = 1;
                }
                // Same hand-down as IBehaviorManager: child +0x1c, then slot 7.
                // +0x27c is not null-tested.
                GotoDelegate->DeltaTime = DeltaTime;
                int const ChildResult = GotoDelegate->OnUpdate();
                if (ChildResult == 1 || ChildResult == 0)
                {
                    UpdateResult = ChildResult;
                    return ChildResult;
                }
            }
        }
        bPlayerCatchUp = 0;
    }
    else if (bGotoDelegateActive != 0)
    {
        bGotoDelegateActive = 0;
        GotoDelegate->OnDisengage();
    }

    if (bDockDelegateActive != 0 && DockDelegate != nullptr)
    {
        DockDelegate->DeltaTime = DeltaTime;
        int const ChildResult = DockDelegate->OnUpdate();
        if (ChildResult == 1 || ChildResult == 0)
        {
            UpdateResult = ChildResult;
            return ChildResult;
        }
        bDockDelegateActive = 0;
        DockDelegate->OnDisengage();
    }

    FollowerDirectiveOrder Order;
    std::memset(&Order, 0, sizeof(Order));
    Rec->GetDirectiveOrder((unsigned char*)&Order);

    bool RunPendingLookup = false;
    if (bInTradeLane != 0)
    {
        if (Order.bOrderPending != 0 || bLeftTradeLane == 0)
        {
            UpdateResult = 0;
            return 0;
        }
        bLeftTradeLane = 0;
        bInTradeLane = 0;
        RunPendingLookup = true;
    }
    else if (Order.bOrderPending == 0)
    {
        RunPendingLookup = true;
    }

    if (!RunPendingLookup)
    {
        // The vcall's arguments are (&float, &Vector) on locals (retail's LEA is offset by one
        // pushed dword, not the order block), i.e. IObjInspect::get_physical_radius.
        // The candidate is not null-tested.
        IObjInspect const* const Candidate =
            Sys->Timers.FindVerifiedObject(&Order.DockObjectId);   // FUN_062e74a0
        float CandidateRadius = 0.0f;
        Vector CandidateCenter = { 0.0f, 0.0f, 0.0f };
        Candidate->get_physical_radius(CandidateRadius, CandidateCenter);

        bool CommitDirectly = (Order.bMidLane == 1);
        if (!CommitDirectly)
        {
            // Refresh the cached physical centre unless the +0x157a latch says this frame did.
            if (Sys->SelfTarget.bPosValid == 0)
            {
                Sys->SelfTarget.bPosValid = 1;
                Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                                      Sys->SelfTarget.CachedPos);
            }
            // Kept on the x87 stack, so double; ((dz*dz + dx*dx) + dy*dy) in retail's order.
            double const Dx = static_cast<double>(Sys->SelfTarget.CachedPos.x) - CandidateCenter.x;
            double const Dy = static_cast<double>(Sys->SelfTarget.CachedPos.y) - CandidateCenter.y;
            double const Dz = static_cast<double>(Sys->SelfTarget.CachedPos.z) - CandidateCenter.z;
            double const Distance = std::sqrt((Dz * Dz + Dx * Dx) + Dy * Dy);
            // ">=-or-unordered" issues the dock directive; commit only strictly inside.
            CommitDirectly = (Distance < CandidateRadius);
        }

        if (CommitDirectly)
        {
            // IObjAffect (IObjRW's second base, +4) slot 25 go_tradelane; named from Server.dll's
            // Ship forwarder .text:06d028c0. Retail pushes a dword holding bMidLane plus pad;
            // the callee reads only the low byte.
            void* const Secondary = Sys->Timers.FindVerifiedObject(&Order.SecondaryObjectId);   // FUN_062e74a0
            IObjAffect* const Affect = static_cast<IObjAffect*>(Owner);
            int const Committed =
                Affect->go_tradelane(Candidate, static_cast<IObjInspect const*>(Secondary),
                                     Order.bMidLane != 0, Order.MaxRange);
            if (Committed != 0)
            {
                UpdateResult = 1;
                return 1;
            }
            bInTradeLane = 1;
            UpdateResult = 0;
            return 0;
        }

        // +0x278 is not null-tested on this path.
        {
            pub::AI::DirectiveDockOp Op;
            Op.DockObjectId = Order.DockObjectId;
            Op.NextRingId = static_cast<int32_t>(Order.SecondaryObjectId);
            DockDelegate->OnApplyDirective(&Op);
            // FUN_062c50c0 copies the offset into the delegate's +0x444.
            Vector const Offset = GetRotatedOffset();
            static_cast<DockBehavior*>(DockDelegate)->SetDestPointOffset(Offset);   // FUN_062c50c0
            DockDelegate->OnEngage();
        }
        bDockDelegateActive = 1;
        UpdateResult = 1;
        return 1;
    }

    {
        unsigned int PendingB0 = 0;
        if (Rec->TryGetValueB0(&PendingB0))
        {
            Rec->SetValueB0(0);
            Flag285 = 1;
            UpdateResult = 0;
            return 0;
        }
    }

    // Retail calls gate B twice on the same path; not folded, since it dispatches a
    // virtual and is not provably side-effect-free.
    bool ReachedPayload = false;
    if (!Rec->FollowerDirectiveGateA() || Rec->FollowerDirectiveGateB())
    {
        if (Rec->FollowerDirectiveGateB() &&
            Sys->PersonalityData.JobParams.bForceAttackFormation != 0)   // Sys+0x1958
        {
            ReachedPayload = true;
        }
    }

    if (ReachedPayload)
    {
        UpdateTickPayload(DeltaTime);   // FUN_062cbcf0
    }
    else
    {
        UpdateFollowerSteering();
    }
    UpdateResult = 1;
    return 1;
}

// FUN_062cbcf0. Switches on FormationParams.FormationExitMode (SystemManager+0x1810), unsigned.
void BaseFollowerBehavior::UpdateTickPayload(float Delta)
{
    SystemManager* const Sys = SysMgr;
    pub::AI::Personality::FormationUseStruct const& Params = Sys->PersonalityData.FormationParams;
    switch ((unsigned int)Params.FormationExitMode)
    {
    case 1:
        (void)Afterburner.Update(Delta);
        // falls through into case 0
    case 0:
    {
        IObjRW* const Leader = ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target()));
        // The cached-physical-bounds latch (+0x157a), set before the call.
        if (Sys->SelfTarget.bPosValid == 0)
        {
            Sys->SelfTarget.bPosValid = 1;
            Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                                  Sys->SelfTarget.CachedPos);
        }
        Vector LeaderPos{ 0.0f, 0.0f, 0.0f };
        GetIfacePositionOrZero(&LeaderPos, Leader);   // FUN_062e60e0
        Vector const Away{ Sys->SelfTarget.CachedPos.x - LeaderPos.x,
                           Sys->SelfTarget.CachedPos.y - LeaderPos.y,
                           Sys->SelfTarget.CachedPos.z - LeaderPos.z };
        Face.SetDirection(Away);                            // FUN_062d7590
        Face.SetScale94(Params.FormationExitTopTurnBreakAwayThrottle);                   // FUN_062d9490, +0x1814
        (void)Face.Update(Delta);
        // Move along minus the own forward column.
        Matrix const& OwnBasis = Sys->SelfTarget.Iface->get_transform().Rotation;
        Move.SetDirection(Vector{ -OwnBasis.E[0][2], -OwnBasis.E[1][2], -OwnBasis.E[2][2] }); // FUN_062d5890
        (void)Move.Update(Delta);
        return;
    }
    case 2:
    {
        // FUN_062e8c20 / FUN_062e89a0 on Sys+0x18.
        Vector Zero{ 0.0f, 0.0f, 0.0f };
        Sys->Thrust.SetAxisThrottle(Zero);
        (void)Sys->Thrust.SetThrottle(-1.0f, 0);
        return;
    }
    case 3:
        BreakAway.RollThrottle = Params.FormationExitRollOutrunThrottle;           // FUN_062d94a0, +0x1818
        BreakAway.BreakStyle = 0;                           // Outrun
        (void)BreakAway.Update(Delta);
        return;
    default:
        return;
    }
}

// Slot 1, LAB_062cb470.
bool BaseFollowerBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bBound = BindContext(Owner, Manager, Sys);           // FUN_062d2090
    if (bBound)
    {
        LeadWatcher.set(nullptr);
        // Each Idea gets (this->Owner, this->Sys, Manager); the results are discarded.
        (void)Face.SetContext(OwnerContext, SysMgr, Manager);
        (void)MatchThrottle.SetContext(OwnerContext, SysMgr, Manager);
        MatchThrottle.SetFlag3a(true);                              // FUN_062d6720
        MatchThrottle.SetFlag3b(true);                              // FUN_062d6730
        (void)Move.SetContext(OwnerContext, SysMgr, Manager);
        (void)BreakAway.SetContext(OwnerContext, SysMgr, Manager);
        (void)Afterburner.SetContext(OwnerContext, SysMgr, Manager);
    }
    return bBound;
}

// Slot 3, FUN_062cb540.
void BaseFollowerBehavior::OnEngage()
{
    Behavior::OnEngage();                                              // FUN_062d20d0
    Flag285 = 0;
    Field288 = 0;
    bInTradeLane = 0;
    bLeftTradeLane = 0;
    SysMgr->Thrust.SetStrafeDir(StrafeNone);                     // FUN_062e9030
    SysMgr->Thrust.SetThrustersActive(false);                    // FUN_062e9120
    SetAvoidanceAllowed(1, 3);                                            // FUN_062d21d0
    bDockDelegateActive = 0;
    DockDelegate = BehaviorMgr->BehaviorDispatchTable[7];        // Manager+0x190
    bGotoDelegateActive = 0;
    if (SysMgr->SelfTarget.Iface->is_player())
        bPlayerCatchUp = 1;
    GotoDelegate = BehaviorMgr->BehaviorDispatchTable[2];        // Manager+0x17c
}

// Slot 4, FUN_062cb5e0.
void BaseFollowerBehavior::OnDisengage()
{
    Field29c = 0;
    if (bInTradeLane != 0)
    {
        IObjInspect const* const Owner = ObjInspectOf(OwnerContext);
        if (Owner->is_player())
        {
            CObject const* const Object = Owner->cobject();
            if (Object != nullptr &&
                (Object->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
                static_cast<CShip*>(const_cast<CObject*>(Object))->request_exit_tradelane();
        }
    }
    bInTradeLane = 0;
    Face.Commit();
    MatchThrottle.Commit();
    Move.Commit();
    BreakAway.Commit();
    Afterburner.Commit();
    if (bDockDelegateActive != 0 && DockDelegate != nullptr)
        DockDelegate->OnDisengage();
    if (bGotoDelegateActive != 0 && GotoDelegate != nullptr)
        GotoDelegate->OnDisengage();
    bDockDelegateActive = 0;
    bGotoDelegateActive = 0;
    GotoDelegate = nullptr;
    DockDelegate = nullptr;
    Behavior::OnDisengage();                                              // FUN_062d20f0
}

// Slot 8, LAB_062cb6d0: unless the dock delegate runs or a lane order is out.
void BaseFollowerBehavior::PostUpdate()
{
    if (bDockDelegateActive != 0 || bInTradeLane != 0)
        return;
    MatchThrottle.SyncCruise(DeltaTime);
}

// Slot 17, LAB_062cbfc0: the base Behavior body with its argument overwritten by 0.
void BaseFollowerBehavior::slide_strafe_burst(StrafeDir Dir)
{
    (void)Dir;
    SysMgr->Thrust.SetStrafeDir(StrafeNone);
}

// Slot 21, LAB_062cf300.
void BaseFollowerBehavior::OnObjectLeftSystem(void* Arg)
{
    unsigned int const LeftId = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg));
    IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target()));
    if (Lead == nullptr)
        return;
    if (Lead->get_id() == LeftId)
        LeadWatcher.set(nullptr);
}

// Slot 22, FUN_062cbea0.
void BaseFollowerBehavior::OnEvent(void* Arg)
{
    FBehaviorManagerEvent const* const Event = static_cast<FBehaviorManagerEvent const*>(Arg);
    if (bDockDelegateActive != 0 && DockDelegate != nullptr)
        DockDelegate->OnEvent(Arg);
    if (Event->Type != 4)
        return;
    FBehaviorTradelaneEvent const* const Lane =
        static_cast<FBehaviorTradelaneEvent const*>(Event->Data);
    IObjRW* const Watched = SysMgr->FormationLeaders.GetCurrentLeader();
    unsigned int WatchedId = 0;
    if (Watched != nullptr)
        WatchedId = Watched->get_id();
    if (Lane->bEnteringLane == 0 && WatchedId == Lane->ShipId && Lane->bExitRequested != 0)
    {
        bool bInLane = false;
        (void)ObjInspectOf(OwnerContext)->is_using_tradelane(&bInLane);
        if (bInLane)
        {
            // Retail calls request_exit_tradelane unconditionally, on null when the owner is
            // not a ship; reproduced without a guard.
            CObject const* const Object = ObjInspectOf(OwnerContext)->cobject();
            CShip* Ship = nullptr;
            if (Object != nullptr && (Object->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
                Ship = (CShip*)Object;
            Ship->request_exit_tradelane();
        }
        else if (bDockDelegateActive != 0 && DockDelegate != nullptr)
        {
            bDockDelegateActive = 0;
            DockDelegate->OnDisengage();
        }
    }

    // The own ship (ExternalIface) left a lane.
    if (SysMgr->SelfTarget.Iface->get_id() != Lane->ShipId)
        return;
    if (Lane->bEnteringLane != 0)
        return;
    bLeftTradeLane = 1;
    if (bDockDelegateActive != 0)
        return;
    unsigned int Zero = 0;                                          // retail reuses the argument slot
    (void)ObjAffectOf(OwnerContext)->request_cancel(2, &Zero, 0);          // IObjAffect +0x60
}
