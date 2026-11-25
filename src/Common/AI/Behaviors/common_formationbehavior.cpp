// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- FormationBehavior (behavior type 10, "Formation").
// The follower helpers it calls are in common_behavior.cpp (common_behavior_internal.h).

#include "common_behavior.h"
#include "AI/common_behavior_internal.h" // CmnBehavior:: follower helpers (common_behavior.cpp)
#include "common_aisteer.h"
#include "common_systemmanager.h" // SystemManager -- SysMgr's real type
#include "common_misc.h" // SinglePlayer() -- evaluate()'s multiplayer group test
#include "common_cobject.h" // CShip::bIsPlayer / GroupId
#include "common_csys.h"  // Matrix, Transform, MatrixSetIdentity, Acquire3DMathEngine (UpdateFollowerSteering/GetRotatedOffset)
#include "common_iface.h" // IObjInspect / IObjRW / ObjInspectOf / ObjRwFromWatchable
#include "x86math.h"      // I3DMathEngine::TransformMatrix (+0x2c), Vector, Matrix
#include "common_pathfind.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using CmnBehavior::ShipOrNull;

// FUN_062cbfe0. FaceAxis2ac is not written by retail; the port zeroes it.
FormationBehavior::FormationBehavior(int InTypeId)
    : BaseFollowerBehavior(InTypeId)
    , StandoffDistance2a0(200.0f)
    , CruiseStandoffDistance2a4(500.0f)
    , bDirectionSet2a8(0)
    , Pad2a9{}
    , FaceAxis2ac{ 0.0f, 0.0f, 0.0f }
{
    bLeftTradeLane = 0;                           // again, after the base ctor FUN_062cb2b0
    bInTradeLane = 0;
}

// Slot 3, FUN_062cc3b0.
void FormationBehavior::OnEngage()
{
    BaseFollowerBehavior::OnEngage();
    (void)ObjInspectOf(OwnerContext)->get_formation_offset(Offset290);
    if (SysMgr->SelfTarget.Iface->is_player())
    {
        Flag28c = 1;
        MatchThrottle.SetFlag39(true);
        Move.SetField44(2);                                         // FUN_062d88b0
    }
    bDirectionSet2a8 = 0;
}

// Slot 4, LAB_062cc410.
void FormationBehavior::OnDisengage()
{
    BaseFollowerBehavior::OnDisengage();
    if (!SysMgr->SelfTarget.Iface->is_player())
        return;
    (void)SysMgr->FormationLeaders.RequestRelease();     // FUN_062ecd30
    unsigned int const Id = ObjInspectOf(OwnerContext)->get_id();
    (void)ObjAffectOf(OwnerContext)->request_cancel(1, &Id, -1);
}

// Slot 5, FUN_062cc030 (argument unread).
int FormationBehavior::OnEvaluateReadiness(float Value)
{
    (void)Value;
    int Result = (Flag285 != 0) ? 1 : 2;
    FFormationLeaderState* const Rec = &SysMgr->FormationLeaders;   // +0x1364
    if (Rec->GetFollowLeader() != nullptr)
    {
        LeadWatcher.set(nullptr);
    }
    else
    {
        IObjRW* const Default = Rec->GetFormationLeader();
        LeadWatcher.set(Default != nullptr
            ? reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char*>(Default) + 8)
            : nullptr);
    }
    if (ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target())) != nullptr)
        Result = 1;
    Readiness = (Flag29 != 0) ? 3 : Result;
    return Readiness;
}

// Slot 6, LAB_062cc0d0 (argument unread).
EvaluateResult FormationBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    IObjInspect const* const Owner = ObjInspectOf(OwnerContext);
    IObjRW* Target = nullptr;
    (void)Owner->get_target(Target);

    // The owner's ship and its group id, or (0, 0).
    CShip const* const OwnShip = ShipOrNull(Owner->cobject());
    unsigned int const OwnGroup = (OwnShip != nullptr) ? OwnShip->GroupId : 0;

    if (Target == nullptr)
        return EvalSkip;
    bool bInLane = false;
    (void)Owner->is_using_tradelane(&bInLane);
    if (bInLane)
        return EvalSkip;

    // Multiplayer, both player ships: their group ids must match.
    if (!SinglePlayer())
    {
        CShip const* const A = ShipOrNull(Owner->cobject());
        CShip const* const B = ShipOrNull(ObjInspectOf(Target)->cobject());
        if (A != nullptr && B != nullptr && A->bIsPlayer != 0 && B->bIsPlayer != 0)
        {
            unsigned int const GroupA = A->GroupId;
            unsigned int const GroupB = B->GroupId;
            if (GroupA == 0 || GroupB == 0 || GroupA != GroupB)
                return EvalSkip;
        }
    }

    unsigned int TypeBits = 0;
    if (Target->get_type(TypeBits) != 0)
        return EvalSkip;
    if ((TypeBits & OBJ_SHIP_MASK) == 0 || (TypeBits & OBJ_CAPITAL) != 0)
        return EvalSkip;
    int const BehaviorId = Target->get_behavior_id();
    if (BehaviorId == BEHAVIOR_FOLLOW || BehaviorId == BEHAVIOR_LAUNCH)
        return EvalSkip;

    // The leader chain must not come back to the owner. The out-param is zeroed once,
    // before the loop, as in OnUpdate.
    {
        IObjRW* Node = Target;
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
                unsigned int const OwnerId = Owner->get_id();       // owner's id is fetched first
                if (Leader->get_id() == OwnerId)
                    return EvalSkip;
            }
            Node = Leader;
            if (Node == nullptr)
                break;
        }
    }

    if ((GetObjectTypeOrZero(ObjInspectOf(Target)) & 0x7f0000u) == 0)
        return EvalSkip;

    // Against the owner's ship captured at the top.
    CShip const* const TargetShip = ShipOrNull(ObjInspectOf(Target)->cobject());
    if (OwnShip != nullptr && TargetShip != nullptr && TargetShip->bIsPlayer != 0)
    {
        if (OwnGroup == 0)
            return EvalSkip;
        if (TargetShip->GroupId != OwnGroup)
            return EvalSkip;
    }

    // Own cached centre (latch +0x157a, set before the call).
    SystemManager* const Sys = SysMgr;
    if (Sys->SelfTarget.bPosValid == 0)
    {
        Sys->SelfTarget.bPosValid = 1;
        Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                              Sys->SelfTarget.CachedPos);
    }
    // DAT_0639fbd8 = 25000000.0f. Summed in double in retail's (x, z, y) order; unordered
    // leaves.
    Vector const& TargetPos = ObjInspectOf(Target)->get_position();
    double const Dx = static_cast<double>(TargetPos.x) - Sys->SelfTarget.CachedPos.x;
    double const Dy = static_cast<double>(TargetPos.y) - Sys->SelfTarget.CachedPos.y;
    double const Dz = static_cast<double>(TargetPos.z) - Sys->SelfTarget.CachedPos.z;
    double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
    if (!(DistSq <= 25000000.0))
        return EvalSkip;

    // DAT_0639f540 = -0.6f.
    float Attitude = 0.0f;
    (void)ObjInspectOf(Target)->get_attitude_towards(Attitude, Owner);
    if (Attitude > -0.6f)
        return EvalBlocked;
    return EvalSkip;
}

// Slot 10, LAB_062cd630.
bool FormationBehavior::OnApplyDirective(void* DirectiveOp)
{
    (void)DirectiveOp;
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    return true;
}

// Slot 12, LAB_062cc990.
void FormationBehavior::set_direction(Vector const& Dir)
{
    // Unordered |Dir|^2 counts as not set.
    double const LenSq = (static_cast<double>(Dir.x) * Dir.x + static_cast<double>(Dir.y) * Dir.y)
                         + static_cast<double>(Dir.z) * Dir.z;
    if (!(LenSq > 0.0))
    {
        bDirectionSet2a8 = 0;
        return;
    }
    bDirectionSet2a8 = 1;
    FaceAxis2ac.x = -Dir.y;
    FaceAxis2ac.z = 0.0f;
    FaceAxis2ac.y = -Dir.x;
    // DAT_0639c430 = 0.0 (double). The reciprocal stays on the x87 stack; each product is
    // rounded once.
    double const Factor = BehaviorMgr->get_turn_sensitivity_factor();
    if (Factor > 0.0)
    {
        double const Inverse = 1.0 / Factor;
        FaceAxis2ac.x = static_cast<float>(Inverse * FaceAxis2ac.x);
        FaceAxis2ac.y = static_cast<float>(Inverse * FaceAxis2ac.y);
    }
    // Clamp to [-1, 1] in retail's compare order (NaN becomes -1).
    FaceAxis2ac.x = !(FaceAxis2ac.x > -1.0f) ? -1.0f : (!(FaceAxis2ac.x < 1.0f) ? 1.0f : FaceAxis2ac.x);
    FaceAxis2ac.y = !(FaceAxis2ac.y > -1.0f) ? -1.0f : (!(FaceAxis2ac.y < 1.0f) ? 1.0f : FaceAxis2ac.y);
    FaceAxis2ac.z = -Dir.z;
}

// The SysMgr cached-physical-bounds latch (+0x157a), inlined twice in
// UpdateFollowerSteering (062cc6b6, 062cc748). The latch is set before get_physical_radius
// runs; no null tests.
static void NavRefreshPhysicalBounds(SystemManager* Nav)
{
    if (Nav->SelfTarget.bPosValid != 0)
        return;
    Nav->SelfTarget.bPosValid = 1;
    (void)Nav->SelfTarget.Iface->get_physical_radius(Nav->SelfTarget.CachedRadius,
                                                               Nav->SelfTarget.CachedPos);
}

// Own vtable slot 27 (.rdata:0639fbd0), LAB_062cc470.
void FormationBehavior::UpdateFollowerSteering()
{
    // Re-point LeadWatcher at the directive record's formation leader. FollowBehavior's
    // slot 27 has no such step. SysMgr is dereferenced with no null test.
    {
        IObjRW* const Directed = SysMgr->FormationLeaders.GetFormationLeader(); // FUN_062ecab0
        LeadWatcher.set(Directed != nullptr
            ? reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char*>(Directed) + 8)
            : nullptr);
    }

    IObjRW* const Leader = ObjRwFromWatchable(
        const_cast<void*>(static_cast<void const*>(LeadWatcher.target())));
    if (Leader == nullptr)
        return;

    // Only the 3x3 rotation of the leader's transform is kept.
    Matrix const LeaderBasis = Behavior_GetTransform(Leader)->Rotation;

    Vector LeaderPos;
    GetIfacePositionOrZero(&LeaderPos, Leader);

    // No null test.
    (void)ObjInspectOf(OwnerContext)->get_formation_offset(Offset290);

    // The leader's own formation offset; retail re-unwraps the watcher for it.
    Vector LeaderFormationOffset{ 0.0f, 0.0f, 0.0f };
    (void)ObjInspectOf(ObjRwFromWatchable(const_cast<void*>(static_cast<void const*>(
        LeadWatcher.target()))))->get_formation_offset(LeaderFormationOffset);

    I3DMathEngine* const Math = Acquire3DMathEngine();

    // Anchor = LeaderPos - Rot(LeaderBasis, LeaderOffset): FUN_06288800 is Vec3Sub (FSUB),
    // not the neighbouring Vec3Add FUN_062887d0 used for the slot point below.
    Vector LeaderOffsetWorld{ 0.0f, 0.0f, 0.0f };
    Math->TransformMatrix(LeaderOffsetWorld, LeaderBasis, LeaderFormationOffset);
    Vector const Anchor{ LeaderPos.x - LeaderOffsetWorld.x,
                         LeaderPos.y - LeaderOffsetWorld.y,
                         LeaderPos.z - LeaderOffsetWorld.z };

    // Slot = Anchor + Rot(LeaderBasis, OwnOffset).
    Vector OwnOffsetWorld{ 0.0f, 0.0f, 0.0f };
    Math->TransformMatrix(OwnOffsetWorld, LeaderBasis, Offset290);
    Vector const SlotPoint{ OwnOffsetWorld.x + Anchor.x,
                            OwnOffsetWorld.y + Anchor.y,
                            OwnOffsetWorld.z + Anchor.z };

    // Minus the leader's forward column.
    Vector LeaderBackward{ 0.0f, 0.0f, 0.0f };
    GetNegatedIfaceDirection(&LeaderBackward, Leader);

    // The standoff picked here is added to the ship's own physical radius further down.
    bool bCruise = false;
    bool bTradeLane = false;
    (void)ObjInspectOf(OwnerContext)->is_cruise_active(bCruise, bTradeLane);   // no null test
    float const Standoff = (bCruise && !bTradeLane) ? CruiseStandoffDistance2a4
                                                    : StandoffDistance2a0;

    // Own backward column dotted with the leader's, summed (z, y, x) in double; <= 0 and
    // unordered skip the clearance.
    double Clearance = 0.0f;
    {
        Matrix const& OwnBasis = SysMgr->SelfTarget.Iface->get_transform().Rotation;
        Vector const OwnBackward{ -OwnBasis.E[0][2], -OwnBasis.E[1][2], -OwnBasis.E[2][2] };
        double const Facing = static_cast<double>(OwnBackward.z) * LeaderBackward.z
                            + static_cast<double>(OwnBackward.y) * LeaderBackward.y
                            + static_cast<double>(OwnBackward.x) * LeaderBackward.x;
        if (Facing > 0.0)
        {
            NavRefreshPhysicalBounds(SysMgr);

            // Retail computes these and never reads them again (get_transform is still called).
            Vector const FromCentre{ SlotPoint.x - SysMgr->SelfTarget.CachedPos.x,
                                     SlotPoint.y - SysMgr->SelfTarget.CachedPos.y,
                                     SlotPoint.z - SysMgr->SelfTarget.CachedPos.z };
            Matrix const& OwnBasis2 = SysMgr->SelfTarget.Iface->get_transform().Rotation;
            Vector const OwnBackward2{ -OwnBasis2.E[0][2], -OwnBasis2.E[1][2], -OwnBasis2.E[2][2] };
            (void)FromCentre;
            (void)OwnBackward2;

            NavRefreshPhysicalBounds(SysMgr);

            // Radius + Standoff stays unrounded; kept when >= 0 or unordered.
            double const Reach = static_cast<double>(SysMgr->SelfTarget.CachedRadius) + Standoff;
            if (!(Reach < 0.0))
                Clearance = Reach;
        }
    }

    // Pull the formation slot back along the leader's local Z by the unrounded Clearance.
    Vector const AdjustedOffset{ Offset290.x,
                                 Offset290.y,
                                 static_cast<float>(static_cast<double>(Offset290.z) - Clearance) };
    Vector AdjustedWorld{ 0.0f, 0.0f, 0.0f };
    Math->TransformMatrix(AdjustedWorld, LeaderBasis, AdjustedOffset);
    Vector const TargetPoint{ AdjustedWorld.x + Anchor.x,
                              AdjustedWorld.y + Anchor.y,
                              AdjustedWorld.z + Anchor.z };
    Face.SetPosition(TargetPoint);

    // FUN_062e8840 (the byte at Timers+0x78).
    if (SysMgr->Timers.IsTimer5Active())
        Face.SetUpVector(Vector{ 0.0f, 0.0f, 1.0f });

    if (bDirectionSet2a8 != 0)
    {
        Matrix const& OwnBasis = SysMgr->SelfTarget.Iface->get_transform().Rotation; // no null test
        Face.SetAxis(FaceAxis2ac);
        Move.SetDirection(Vector{ -OwnBasis.E[0][2],
                                  -OwnBasis.E[1][2],
                                  -OwnBasis.E[2][2] });
    }

    (void)Face.Update(DeltaTime);
    MatchThrottle.SetWatchAndPosition(Leader, SlotPoint);
    (void)MatchThrottle.Update(DeltaTime);
}

// ----------------------------------------------------------------------------
// Own vtable slot 28 (.rdata:0639fbd4), LAB_062cc8f0. BaseFollowerBehavior::OnUpdate
// (.text:062cbc2d) feeds the result to FUN_062c50c0, which copies it to NestedPtr278 + 0x444.
// Returned by value through a hidden buffer (`RET 0x4`).
// Like FollowBehavior::GetRotatedOffset, but the offset transformed is a fresh local filled
// by get_formation_offset, not the Offset290 member.
Vector FormationBehavior::GetRotatedOffset()
{
    Vector FormationOffsetLocal{ 0.0f, 0.0f, 0.0f };
    (void)ObjInspectOf(OwnerContext)->get_formation_offset(FormationOffsetLocal);   // no null test

    IObjRW* const Leader = ObjRwFromWatchable(
        const_cast<void*>(static_cast<void const*>(LeadWatcher.target())));

    // The 3x3 rotation only.
    Matrix const LeaderBasis = Behavior_GetTransform(Leader)->Rotation;

    Vector Out;
    Acquire3DMathEngine()->TransformMatrix(Out, LeaderBasis, FormationOffsetLocal);
    return Out;
}
