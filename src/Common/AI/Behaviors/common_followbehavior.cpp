// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_followbehavior.h"
#include "common_iface.h" // IObjInspect / IObjAffect / ObjRwFromWatchable
#include "common_aisteer.h" // GetIfacePositionOrZero, IsWithinInterceptRangeOfIface,
                             // GetInterceptClearanceOfIface, GetIfaceVectorMagnitude,
                             // GetObjectTypeOrZero, SetFollowLeader
#include "common_systemmanager.h" // SystemManager::FormationLeaders (SysMgr+0x1364)
#include "common_csys.h"  // Matrix, Transform, MatrixSetIdentity, Acquire3DMathEngine
#include "x86math.h"      // I3DMathEngine::TransformMatrix (vtable +0x2c), Vector, Matrix

#include <cmath>

// FUN_062ccaf0.
FollowBehavior::FollowBehavior(int InTypeId)
    : BaseFollowerBehavior(InTypeId)
    , FollowDistance(150.0f)
    , bLeaderInRange(1)
    , Pad2a5{}
    , Distance2a8(0.0f)              // not written by retail; the port zeroes it
    , Field2ac(150.0f)
    , LeadClearanceScale(2.0f)
    , EngageRange(400.0f)
    , FollowRangeBias(100.0f)
{
    bInTradeLane = 0;
    bLeftTradeLane = 0;
    Offset290.x = 0.0f;
    Offset290.y = 0.0f;
    Offset290.z = 0.0f;
    Flag285 = 0;
}

// Slot 3, FUN_062ccc80.
void FollowBehavior::OnEngage()
{
    BaseFollowerBehavior::OnEngage();
    IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target()));
    if (Lead == nullptr)
        return;
    (void)ObjAffectOf(Lead)->add_follow_follower(OwnerContext);
    // Retail re-reads the watcher here (FUN_062ec970).
    SysMgr->FormationLeaders.SetFollowLeader(
        ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target())));
}

// Slot 4, FUN_062ccce0.
void FollowBehavior::OnDisengage()
{
    BaseFollowerBehavior::OnDisengage();
    IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target()));
    if (Lead == nullptr)
        return;
    (void)ObjAffectOf(Lead)->remove_follow_follower(OwnerContext);
    SysMgr->FormationLeaders.SetFollowLeader(nullptr);
}

// Slot 5, FUN_062ccb70 (argument unread).
int FollowBehavior::OnEvaluateReadiness(float Value)
{
    (void)Value;
    IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target()));
    if ((Flags28 & kBehaviorFlagDirectiveAccepted) == 0 || Lead == nullptr)
    {
        Readiness = (Flag29 != 0) ? 3 : 2;
        return Readiness;
    }
    if (IsWithinInterceptRangeOfIface(&SysMgr->SelfTarget, Lead, EngageRange))
        Readiness = (Flag29 != 0) ? 3 : 1;
    else
        Readiness = (Flag29 != 0) ? 3 : 0;
    return Readiness;
}

// Slot 6, LAB_062ccc10 (argument unread).
EvaluateResult FollowBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    IObjRW* Target = nullptr;
    (void)ObjInspectOf(OwnerContext)->get_target(Target);
    if (Target == nullptr)
        return EvalSkip;
    bool bInLane = false;
    (void)ObjInspectOf(OwnerContext)->is_using_tradelane(&bInLane);
    if (bInLane)
        return EvalSkip;
    // FUN_062e6200: any ship-class bit.
    if ((GetObjectTypeOrZero(ObjInspectOf(Target)) & OBJ_SHIP_MASK) != 0)
        return EvalBlocked;
    return EvalSkip;
}

// Slot 10, FUN_062cd3d0.
bool FollowBehavior::OnApplyDirective(void* Param)
{
    if (Param != nullptr)
    {
        pub::AI::DirectiveFollowOp const* const Op = static_cast<pub::AI::DirectiveFollowOp const*>(Param);

        // FUN_062e74a0: the object whose id the op's TargetId holds, through the IObjDB at
        // SystemManager::Timers+0x00, only if that object reports the same id.
        IObjRW* const Candidate = SysMgr->Timers.FindVerifiedObject(&Op->TargetId);
        LeadWatcher.set(static_cast<Watchable*>(Candidate));

        if (ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target())) != nullptr)
        {
            Offset290.x = Op->FollowOffsetX;
            Offset290.y = Op->FollowOffsetY;
            Offset290.z = Op->FollowOffsetZ;
            // x87: summed and square-rooted in double, rounded to float once.
            Distance2a8 = static_cast<float>(std::sqrt(
                (static_cast<double>(Offset290.x) * Offset290.x
                 + static_cast<double>(Offset290.y) * Offset290.y)
                + static_cast<double>(Offset290.z) * Offset290.z));
            FollowDistance = Op->FollowDistance;

            (void)ObjAffectOf(OwnerContext)->set_follow_offset(Offset290); // no null test

            SysMgr->FormationLeaders.SetFollowLeader(
                ObjRwFromWatchable(const_cast<Watchable*>(LeadWatcher.target())));

            Flags28 = static_cast<unsigned char>(Flags28 | kBehaviorFlagDirectiveAccepted);
            return true;
        }

        EngageRange = Op->EngageRange;
    }

    Flags28 = static_cast<unsigned char>(Flags28 & ~kBehaviorFlagDirectiveAccepted);
    return false;
}

// ============================================================================
// The two virtuals BaseFollowerBehavior adds to Behavior's 27 (vtable offsets 0x6c and 0x70).
// ============================================================================

namespace
{

// The `LeadWatcher.target() - 8` downcast. Retail does not special-case the 0x8 "watching
// nothing" sentinel, because 8 - 8 is 0 anyway.
IObjRW* LeaderObjectOf(BaseWatcher const& Watcher)
{
    return ObjRwFromWatchable(const_cast<void*>(static_cast<void const*>(Watcher.target())));
}

// Lazy per-frame bounds refill, inlined twice into UpdateFollowerSteering (062cd236,
// 062cd2cc): the valid flag is set BEFORE get_physical_radius runs. No null tests in retail.
void RefreshNavPhysicalBounds(SystemManager* Nav)
{
    if (Nav->SelfTarget.bPosValid != 0)
        return;
    Nav->SelfTarget.bPosValid = 1;
    (void)Nav->SelfTarget.Iface->get_physical_radius(Nav->SelfTarget.CachedRadius,
                                                                Nav->SelfTarget.CachedPos);
}

} // namespace

// Inlined twice in retail (062ccf83, 062cd0a3: UpdateFollowerSteering's watch and point
// blocks). Toggles bLeaderInRange when the leader crosses the follow range and reports the
// transition to the manager (FUN_062d29c0). Retail re-reads LeadWatcher for every use.
void FollowBehavior::UpdateInterceptEngageState()
{
    // The range sum is rounded to float once.
    bool const bInRange = IsWithinInterceptRangeOfIface(
        &SysMgr->SelfTarget, LeaderObjectOf(LeadWatcher),
        FollowRangeBias + FollowDistance);

    if (bInRange)
    {
        if (bLeaderInRange != 0)
            return;
        bLeaderInRange = 1;
        IObjRW* const Leader = LeaderObjectOf(LeadWatcher);
        unsigned const OwnId = SysMgr->SelfTarget.Iface->get_id();
        // Retail pushes the range and status before the leader's get_id() runs.
        float const Range = FollowRangeBias + FollowDistance;
        unsigned const LeaderId = ObjInspectOf(Leader)->get_id();
        (void)BehaviorMgr->NotifyFollowEvent(OwnId, LeaderId,
                                                pub::AI::ContentCallback::FollowEnterRange, Range);
        return;
    }

    if (bLeaderInRange == 0)
        return;
    bLeaderInRange = 0;
    IObjRW* const Leader = LeaderObjectOf(LeadWatcher);
    unsigned const OwnId = SysMgr->SelfTarget.Iface->get_id();
    unsigned const LeaderId = ObjInspectOf(Leader)->get_id();
    // Disengage passes the raw FollowDistance, not the biased range.
    (void)BehaviorMgr->NotifyFollowEvent(OwnId, LeaderId,
                                            pub::AI::ContentCallback::FollowLeaveRange,
                                            FollowDistance);
}

// Own vtable slot 27 (offset 0x6c), LAB_062ccd30.
void FollowBehavior::UpdateFollowerSteering()
{
    if (LeaderObjectOf(LeadWatcher) == nullptr)
        return;

    // Nothing reads the result; retail issues the call anyway.
    bool bCruiseActive = false;
    (void)ObjInspectOf(OwnerContext)->is_cruise_active(bCruiseActive);

    // Only the 3x3 of the leader's transform is copied (nine dwords).
    Matrix LeaderBasis = Behavior_GetTransform(LeaderObjectOf(LeadWatcher))->Rotation;
    float Speed = FollowDistance;

    Vector LeaderPos;
    GetIfacePositionOrZero(&LeaderPos, LeaderObjectOf(LeadWatcher));

    Transform Probe;
    MatrixSetIdentity(Probe.Rotation);
    Probe.Translation.x = 0.0f;
    Probe.Translation.y = 0.0f;
    Probe.Translation.z = 0.0f;

    // Per component (DAT_06399408 = 0.0f): the offset counts as present unless every square
    // compares <= 0, so unordered counts as present. The squares stay double on the x87 stack;
    // a float square of a tiny component would underflow to 0.
    double const OffX = Offset290.x;
    double const OffY = Offset290.y;
    double const OffZ = Offset290.z;
    bool const bHasOffset = !(OffX * OffX <= 0.0) || !(OffY * OffY <= 0.0) || !(OffZ * OffZ <= 0.0);

    Vector TargetPoint;
    bool bWatchBlock = false;

    // Only with a zero offset does the leader get to generate one.
    bool const bGenerated =
        !bHasOffset &&
        ObjInspectOf(LeaderObjectOf(LeadWatcher))
            ->generate_follow_offset(ObjInspectOf(OwnerContext), Probe);

    if (bGenerated)
    {
        TargetPoint = Probe.Translation;
        LeaderBasis = Probe.Rotation;
        Speed = 50.0f;
        bWatchBlock = false;
    }
    else
    {
        // Retail has two identical copies (062cce8e zero offset, 062ccefd offset present)
        // that differ only in the block flag.
        Vector Rotated;
        Acquire3DMathEngine()->TransformMatrix(Rotated, LeaderBasis, Offset290);
        TargetPoint.x = Rotated.x + LeaderPos.x;
        TargetPoint.y = Rotated.y + LeaderPos.y;
        TargetPoint.z = Rotated.z + LeaderPos.z;
        bWatchBlock = !bHasOffset;
    }

    // Cleared only by the weight clamp below.
    bool bUpdateMatchThrottle = true;

    // Retail inlines the engage toggle at the head of both blocks.
    UpdateInterceptEngageState();

    if (bWatchBlock)
    {
        MatchThrottle.SetWatchAndDistance(LeaderObjectOf(LeadWatcher), Speed);
        Face.SetWatchObject(LeaderObjectOf(LeadWatcher));
    }
    else
    {
        MatchThrottle.SetWatchAndPosition(LeaderObjectOf(LeadWatcher), TargetPoint);

        // The bound (DAT_0639fb58 = 100.0f) stays unrounded on the x87 stack.
        double const Clearance = GetInterceptClearanceOfIface(
            &SysMgr->SelfTarget, LeaderObjectOf(LeadWatcher));
        if (!(Clearance < static_cast<double>(Distance2a8) + 100.0))
        {
            Face.SetPosition(TargetPoint);
        }
        else
        {
            // The unrounded magnitude is compared with 1.0f (DAT_0639a1d0).
            double const LeaderSpeed = GetIfaceVectorMagnitude(LeaderObjectOf(LeadWatcher));
            float Weight = static_cast<float>(LeaderSpeed);
            if (LeaderSpeed < 1.0)
            {
                Weight = 1.0f;
                bUpdateMatchThrottle = false;
                MatchThrottle.SetFlag39(0);
                MatchThrottle.Commit();
                MatchThrottle.SetFlag39(Flag28c);
            }

            // Column 2 of LeaderBasis.
            Vector const Forward{ LeaderBasis.E[0][2], LeaderBasis.E[1][2], LeaderBasis.E[2][2] };

            RefreshNavPhysicalBounds(SysMgr);

            Vector const Delta{ SysMgr->SelfTarget.CachedPos.x - TargetPoint.x,
                                SysMgr->SelfTarget.CachedPos.y - TargetPoint.y,
                                SysMgr->SelfTarget.CachedPos.z - TargetPoint.z };
            Vector Projected;
            Vector const* const ProjectedOut = VecCosineProject3(&Projected, &Delta, &Forward);

            // Length in double, rounded to float once.
            double const PX = ProjectedOut->x;
            double const PY = ProjectedOut->y;
            double const PZ = ProjectedOut->z;
            Speed = static_cast<float>(std::sqrt((PX * PX + PY * PY) + PZ * PZ));
            if (Speed < 0.0f)
                Speed = 0.0f;

            RefreshNavPhysicalBounds(SysMgr);

            // Scalar stays unrounded. The x and y products are stored to float before
            // TargetPoint is added; the z product stays on the x87 stack and is added unrounded.
            double const Scalar =
                (static_cast<double>(Weight) * LeadClearanceScale + SysMgr->SelfTarget.CachedRadius) + Speed;
            float const ProductX = static_cast<float>(-static_cast<double>(Forward.x) * Scalar);
            float const ProductY = static_cast<float>(-static_cast<double>(Forward.y) * Scalar);
            double const ProductZ = -static_cast<double>(Forward.z) * Scalar;
            Vector const Aim{ ProductX + TargetPoint.x,
                              ProductY + TargetPoint.y,
                              static_cast<float>(ProductZ + TargetPoint.z) };
            Face.SetPosition(Aim);
        }
    }

    if (bUpdateMatchThrottle)
        (void)MatchThrottle.Update(DeltaTime);

    // Retail computes the clearance again and discards it.
    (void)GetInterceptClearanceOfIface(&SysMgr->SelfTarget,
                                       LeaderObjectOf(LeadWatcher));
    (void)Face.Update(DeltaTime);
}

// Own vtable slot 28 (offset 0x70), LAB_062cd4c0.
Vector FollowBehavior::GetRotatedOffset()
{
    Vector Out;

    Transform const* const LeaderXform = Behavior_GetTransform(LeaderObjectOf(LeadWatcher));

    // No null tests in retail.
    Acquire3DMathEngine()->TransformMatrix(Out, LeaderXform->Rotation, Offset290);
    return Out;
}
