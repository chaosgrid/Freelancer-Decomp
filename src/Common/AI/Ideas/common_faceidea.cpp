// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll FaceIdea (ctor 0x062d6750, Update LAB_062d68b0, setters 0x062d7510-).
// See src/include/common_idea.h.

#include "common_idea.h"
#include "AI/Ideas/common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "AI/common_aisteer_internal.h"
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

using namespace CmnIdea;
using namespace CmnAiSteer;

// ----------------------------------------------------------------------------
// FaceIdea
// ----------------------------------------------------------------------------

// FUN_062d6750 (no Ghidra decompile). Field citations are in common_idea.h.
FaceIdea::FaceIdea()
    : BaseIdea()
    , Watcher()
    , Position1c{}
    , Direction28{}
    , ZeroBlock34{}
    , Vector58{}
    , Field64(0)
    , Opaque65{}
    , FacingTolerance(BitsToFloat(0x3dc90fdbu))
    // Retail calls FUN_062e5ba0(this->Object, 1.0f) here while Object is still
    // null, so the result is 0.0f. SetThrottleScale() stores raw/derived the other
    // way round (raw in +0x90, derived in +0x94); that asymmetry is retail's.
    , Field90(Behavior_GetProjectedAxisThrottleXY(ObjInspectOf(Object), 1.0f))   // FUN_062e5ba0
    , TurnThrottle(1.0f)
    , AngleWeight98(BitsToFloat(0x3f1c28f6u))
    , AngleWeight9c(BitsToFloat(0x3e490fdbu))
    , TargetKind(kFaceTargetNone)
    , OpaqueA4{}
    , RemainingAngle(3.14159274f)        // 0x40490fdb
    , PadAc(0)
    , bUpValid(0)     // not written by retail's ctor; port-only safety init
    , PadAe{}
    , UpB0{}
    , ThresholdBc(BitsToFloat(0x3f7ae148u))
{
}

// Own vtable slot 0 override -- LAB_062d6820, RET 0xc. Retail inlines the BaseIdea
// body.
bool FaceIdea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    if (!BaseIdea::SetContext(InObject, InHandle, InContext))
        return false;

    IObjInspect const* const Obj = ObjInspectOf(Object);

    float const Rate = Behavior_GetAngularSpeedXY(Obj, 1.0f, 0.0f);   // FUN_062e5b20

    // FUN_062e5950; DAT_063a0400 == 1000.0f.
    AngleWeight98 = Behavior_GetAngularDistanceTravelled(Obj, 0.0f, 0.0f, Rate, 1000.0f);

    // FUN_062e5b50: a third push (0) is not read by the callee; the result is dropped.
    (void)Behavior_GetAngularSpeedZ(Obj, 1.0f);

    AngleWeight9c = BitsToFloat(0x3e490fdbu);   // pi/16
    bUpValid = 0;
    return true;
}

// Own vtable slot 4 override -- LAB_062d74e0. Handle + 0x18 is never tested.
void FaceIdea::Commit()
{
    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetAxisThrottle(ZeroVector);
}

// ---------------------------------------------------------------------------
// FaceIdea's non-virtual setter family. Every one is RET 4 and stamps TargetKind;
// see common_idea.h for the receiver proofs and the tag table.
// ---------------------------------------------------------------------------

// FUN_062d7510.
void FaceIdea::SetWatchObject(void const* Object2)
{
    if (Object2 == nullptr)
    {
        TargetKind = kFaceTargetNone;
        Watcher.set(nullptr);
        return;
    }

    // IObjRW -> Watchable adjustment, as in MoveIdea::SetWatchObject.
    TargetKind = kFaceTargetObject;
    Watcher.set(reinterpret_cast<Watchable const*>(
        static_cast<unsigned char const*>(Object2) + 8));
}

// FUN_062d7560.
void FaceIdea::SetPosition(Vector const& Point)
{
    TargetKind = kFaceTargetPoint;
    Position1c = Point;
}

// FUN_062d7590. No renormalization, unlike MoveIdea::SetDirection.
void FaceIdea::SetDirection(Vector const& Dir)
{
    TargetKind = kFaceTargetDirection;
    Direction28 = Dir;
}

// FUN_062d75c0.
void FaceIdea::SetBasis(Matrix const& Basis)
{
    TargetKind = kFaceTargetBasis;
    // Nine dwords to +0x34: sizeof(Matrix), and the extent of the ctor-zeroed block.
    std::memcpy(&ZeroBlock34, &Basis, sizeof(ZeroBlock34));
}

// FUN_062d75e0.
void FaceIdea::SetAxis(Vector const& Axis)
{
    TargetKind = kFaceTargetAxis;
    Vector58 = Axis;
}

// FUN_062d7610. Raw argument to +0x90, derived value to +0x94; the ctor assigns
// them the other way round (retail's own asymmetry, see the declaration).
void FaceIdea::SetThrottleScale(float Value)
{
    Field90 = Value;
    TurnThrottle = Behavior_GetProjectedAxisThrottleXY(ObjInspectOf(Object), Value);   // FUN_062e5ba0
}

// FUN_062d7640. Retail's three compares against DAT_06399408 (0.0f) are equality
// tests.
void FaceIdea::SetUpVector(Vector const& Up)
{
    if (Up.x == 0.0f && Up.y == 0.0f && Up.z == 0.0f)
    {
        bUpValid = 0;   // nothing else is written
        return;
    }

    bUpValid = 1;
    UpB0 = Up;
}

// .text:062d93c0: `MOV [ECX+0x8c],EAX; RET 4`. Possible ICF fold with DodgeIdea's /
// BreakAwayIdea's own +0x8c setter -- see the declaration.
void FaceIdea::SetAngleWeight8c(float Value)
{
    FacingTolerance = Value;
}

// .text:062d9490: `MOV [ECX+0x94],EAX; RET 4`. Receiver is FaceIdea per
// .text:062fe34f (GotoBehavior's this+0xa4); same fold caveat.
void FaceIdea::SetScale94(float Value)
{
    TurnThrottle = Value;
}

// ============================================================================
// FaceIdea::Update -- LAB_062d68b0, vtable 063a04ac slot 1 (settled by RTTI:
// COL 063a9a90 names FaceIdea). DodgeIdea::Update (LAB_062d7950, vtable 063a04c8)
// is its sibling in common_dodgeidea.cpp; both share common_aisteer_internal.h.
//
// The idea family's 6-slot vtable (063a04ac FaceIdea / 063a04c8 DodgeIdea /
// 063a04f8 BreakAwayIdea / 063a0524):
//   [0] SetContext("Bind")  [1] Update  [2] (ICF-folded, shared)
//   [3] GetKind             [4] Commit  [5] (ICF-folded, shared)
//
// The cached iface position / PosValid pair is Sys+0x1508 / Sys+0x157a
// (SelfTarget+0xc / +0x7e).
// ============================================================================

// The one raw read left (through `B`) is case 2's up-hint triple at +0x6c/+0x78/+0x84 (0xc
// apart, inside Opaque65; no writer found). +0x94 (TurnThrottle) is the steering magnitude
// cap; +0xa8 (RemainingAngle) is compared against +0x8c (FacingTolerance) for the return.
//
// Retail never reads DeltaTime here (no reroll timer on this variant).
int FaceIdea::Update(float /*DeltaTime*/)
{
    unsigned char const* const B = reinterpret_cast<unsigned char const*>(this);

    if (Object == nullptr)
        return 3;

    SystemManager* const Sys = Handle;
    IBehaviorManager* const Manager = static_cast<IBehaviorManager*>(Context);
    PathfindManager* const Pathfind = Manager->Pathfind;

    // Every consumer re-reads the cached position after its own refresh; there is
    // no entry-time snapshot.
    // A tag above 5 reaches the shared tail with this frame never written
    // (uninitialised stack in retail, unreachable since every writer stores 0..5);
    // the port value-initialises it.
    Matrix WorkingFrame;
    Vector SteerVector{ 0.0f, 0.0f, 0.0f };

    // Shared "no target" fallback (LAB_062d6a4a). The steering gate (FUN_062e92e0)
    // selects the frame builder:
    //   true : SteeringFrameFromRelativeDir(.., get_ship_up_direction())  [FUN_062e3730]
    //   false: BuildOrientationBasis(.., nullptr)                         [FUN_062e1f40]
    // Delta's first term is the watched target's position (this+0x14, -8 adjust)
    // on both branches.
    auto NoTargetFallback = [&]()
    {
        bool const Gate = Sys->Thrust.CheckSteeringGate();
        RefreshCachedIfacePosition(Sys);

        Vector TargetPos{};
        GetIfacePositionOrZero(&TargetPos, ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target())));
        Vector const RelDir = Sub(TargetPos, CachedIfacePosition(Sys));

        Matrix const* const Reference = IfaceOrientation(Sys);
        if (Gate)
        {
            Vector const Up = Manager->get_ship_up_direction();
            SteeringFrameFromRelativeDir(WorkingFrame, *Reference, RelDir, Up);
        }
        else
        {
            BuildOrientationBasis(WorkingFrame, *Reference, RelDir, nullptr);
        }
    };

    switch (TargetKind)
    {
    case kFaceTargetNone:
        return 3;

    case kFaceTargetObject:
    {
        Watchable* const Watched = const_cast<Watchable*>(Watcher.target());
        if (Watched == nullptr)
            return 2;
        IObjRW* const Adjusted = ObjRwFromWatchable(Watched);
        if (Adjusted == nullptr)
            return 2;

        Vector const AngularVelocity = Adjusted->get_angular_velocity();   // slot 2

        // The precise-aim path below runs only with the +0x64 flag set and a nearly
        // non-rotating target: |angular velocity|^2 < 0.01 (DAT_0639e6b0), summed
        // (y*y + z*z) + x*x in double. Flag clear, >= 0.01 or unordered take the
        // fallback.
        double const AngularSq = (static_cast<double>(AngularVelocity.y) * AngularVelocity.y +
                                  static_cast<double>(AngularVelocity.z) * AngularVelocity.z) +
                                 static_cast<double>(AngularVelocity.x) * AngularVelocity.x;
        if (Field64 == 0 || !(AngularSq < 0.01))
        {
            NoTargetFallback();
            break;
        }

        // Aim at the target with its own up axis: column 1 of the node's transform
        // (FUN_062e61f0; retail's +0x4/+0x10/+0x1c).
        Transform const* const NodeTransform = Behavior_GetTransform(Adjusted);
        Vector NodeUp;
        NodeUp.x = NodeTransform->Rotation.E[0][1];
        NodeUp.y = NodeTransform->Rotation.E[1][1];
        NodeUp.z = NodeTransform->Rotation.E[2][1];

        RefreshCachedIfacePosition(Sys);

        Vector TargetPos;
        GetIfacePositionOrZero(&TargetPos, Adjusted);
        Vector const RelDir = Sub(TargetPos, CachedIfacePosition(Sys));

        BuildOrientationBasis(WorkingFrame, *IfaceOrientation(Sys), RelDir, &NodeUp);
        break;
    }

    case kFaceTargetPoint:
    {
        // Delta to this->0x1c is common to all three paths; the gates pick only
        // the frame builder and its up hint:
        //   this->0x64 != 0   : BuildOrientationBasis(.., &UpHintTriple)
        //   flag 0, gate true : SteeringFrameFromRelativeDir(.., ShipUp)
        //   flag 0, gate false: BuildOrientationBasis(.., nullptr)
        bool const UseRawUpHint = Field64 != 0;
        bool Gate = false;
        if (!UseRawUpHint)
        {
            Gate = Sys->Thrust.CheckSteeringGate();
        }

        RefreshCachedIfacePosition(Sys);
        Vector const RelDir = Sub(Position1c, CachedIfacePosition(Sys));

        Matrix const* const Reference = IfaceOrientation(Sys);
        if (UseRawUpHint)
        {
            // The up-hint triple is 0xc apart, not packed.
            Vector const UpHint{ *reinterpret_cast<float const*>(B + 0x6c),
                                  *reinterpret_cast<float const*>(B + 0x78),
                                  *reinterpret_cast<float const*>(B + 0x84) };
            BuildOrientationBasis(WorkingFrame, *Reference, RelDir, &UpHint);
        }
        else if (Gate)
        {
            Vector const Up = Manager->get_ship_up_direction();
            SteeringFrameFromRelativeDir(WorkingFrame, *Reference, RelDir, Up);
        }
        else
        {
            BuildOrientationBasis(WorkingFrame, *Reference, RelDir, nullptr);
        }
        break;
    }

    case kFaceTargetDirection:
    {
        // Direction28 is used as-is (no subtraction of the cached position, unlike
        // cases 1/2); the gate picks the builder as in the fallback.
        bool const Gate = Sys->Thrust.CheckSteeringGate();
        Matrix const* const Reference = IfaceOrientation(Sys);
        if (Gate)
        {
            Vector const Up = Manager->get_ship_up_direction();
            SteeringFrameFromRelativeDir(WorkingFrame, *Reference, Direction28, Up);
        }
        else
        {
            BuildOrientationBasis(WorkingFrame, *Reference, Direction28, nullptr);
        }
        break;
    }

    case kFaceTargetBasis:
        // Raw 9-float frame, copied verbatim (no orientation rebuild at all).
        WorkingFrame = ZeroBlock34;
        break;

    default:
        // A tag above 5 goes straight to the shared tail.
        break;

    case kFaceTargetAxis:
    {
        // Does not reach the shared tail: always ends in SetAxisThrottle and
        // returns 0. Retail's three paths collapse to two: non-player, or player
        // with camera-level status and the steering gate, take the banked frame
        // (Mode 0, not the tail's 1) on Vector58; otherwise Vector58 is
        // transformed by a fresh get_transform().
        WorkingFrame = *IfaceOrientation(Sys);

        bool bBanked = !Sys->SelfTarget.Iface->is_player();
        if (!bBanked)
            bBanked = Manager->get_camera_level_status() && Sys->Thrust.CheckSteeringGate();
        if (bBanked)
        {
            SteerVector = Vector58;
            Vector const Up = Manager->get_ship_up_direction();
            ComputeBankedSteeringFrame(&Sys->SelfTarget,Up, WorkingFrame,
                                       SteerVector, 0);
        }
        else
        {
            SteerVector = TransformVectorByMatrix(*IfaceOrientation(Sys), Vector58);
        }

        Sys->Thrust.SetAxisThrottle(SteerVector);
        return 0;
    }
    }

    // ---- Shared tail for cases 0-4 and the fallback.
    // Step 1: when Pathfind->AvoidRecordKind has bit 0x2 set (a bit test, not the `== 2`
    // DodgeIdea uses at its analogous site), recompute the frame toward
    // Pathfind->AvoidTargetPoint. Retail writes the same stack frame every case filled.
    Matrix& ThrottleFrame = WorkingFrame;
    if ((static_cast<unsigned char>(Pathfind->AvoidRecordKind) & kAvoidRecordPoint) != 0)
    {
        bool const Gate = Sys->Thrust.CheckSteeringGate();
        RefreshCachedIfacePosition(Sys);

        Vector const RecomputeDir = Sub(Pathfind->AvoidTargetPoint, CachedIfacePosition(Sys));

        Matrix const* const RecomputeReference = IfaceOrientation(Sys);
        if (Gate)
        {
            Vector const Up = Manager->get_ship_up_direction();
            SteeringFrameFromRelativeDir(ThrottleFrame, *RecomputeReference, RecomputeDir, Up);
        }
        else
        {
            BuildOrientationBasis(ThrottleFrame, *RecomputeReference, RecomputeDir, nullptr);
        }
    }

    // FUN_062d35c0(1) on the player ship.
    if (Sys->SelfTarget.Iface->is_player())
        Manager->RelevelShipUpDirection(true);

    // Step 2: the working steering vector for the rest of the tail. OutAngle is
    // &this->0xa8 (RemainingAngle).
    SteerVector = TurnHelper::get_angular_throttle(*IfaceOrientation(Sys), ThrottleFrame, &RemainingAngle);

    // Banked-frame refresh (Mode 1). Retail asks is_player() twice: a player
    // refreshes only if camera-level status and the steering gate both agree; a
    // non-player always refreshes. Both calls are kept.
    bool bRefreshBankedFrame = Sys->SelfTarget.Iface->is_player() && Manager->get_camera_level_status() &&
                               Sys->Thrust.CheckSteeringGate();
    if (!bRefreshBankedFrame)
        bRefreshBankedFrame = !Sys->SelfTarget.Iface->is_player();
    if (bRefreshBankedFrame)
    {
        Vector const Up = Manager->get_ship_up_direction();
        ComputeBankedSteeringFrame(&Sys->SelfTarget,Up,
                                   ThrottleFrame, SteerVector, 1);
    }

    // Step 3: FUN_062e2c70 (LimitAxisThrottleForTurn). With a watched target moving
    // faster than 1.0 (DAT_06399410, double), TurnAngle = lead angle and
    // ReferenceAngle = 0.35f; otherwise 0.0f and this->0x98.
    IObjInspect const* const BoundObject = static_cast<IObjInspect const*>(Object);
    IObjRW* const LeadNode = ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));

    Vector LimitedSteer{ 0.0f, 0.0f, 0.0f };
    if (LeadNode != nullptr && GetIfaceVectorMagnitude(LeadNode) > 1.0)
    {
        // The second delta leads the target by one second of its velocity; the
        // angle between the two deltas is how far the aim point has to swing.
        RefreshCachedIfacePosition(Sys);
        Vector NodePos{};
        GetIfacePositionOrZero(&NodePos, LeadNode);
        Vector const DirectDelta = Sub(NodePos, CachedIfacePosition(Sys));

        RefreshCachedIfacePosition(Sys);
        Vector NodePosAgain{};
        GetIfacePositionOrZero(&NodePosAgain, LeadNode);
        Vector const NodeVelocity = LeadNode->get_velocity();
        // x and y round (pos + vel) to float before subtracting the cached centre;
        // z subtracts from the unrounded sum.
        Vector const Center = CachedIfacePosition(Sys);
        float const LeadX = NodePosAgain.x + NodeVelocity.x;
        float const LeadY = NodePosAgain.y + NodeVelocity.y;
        Vector LeadDelta;
        LeadDelta.x = LeadX - Center.x;
        LeadDelta.y = LeadY - Center.y;
        LeadDelta.z = static_cast<float>((static_cast<double>(NodePosAgain.z) + NodeVelocity.z) - Center.z);

        Vector DirectDir{};
        Vector LeadDir{};
        VecNormalize3(&DirectDelta, &DirectDir);
        VecNormalize3(&LeadDelta, &LeadDir);
        float const LeadAngle = std::acos(ClampedDot3(DirectDir, LeadDir));

        // FUN_062e5ba0; retail discards the result. Issued for parity.
        (void)Behavior_GetProjectedAxisThrottleXY(BoundObject, LeadAngle);

        LimitAxisThrottleForTurn(LimitedSteer, BoundObject, SteerVector, &ThrottleFrame,
                                 LeadAngle, 0.35f, true); // 0x3eb33333 == 0.35f
    }
    else
    {
        LimitAxisThrottleForTurn(LimitedSteer, BoundObject, SteerVector, &ThrottleFrame,
                                 0.0f, AngleWeight98, true);
    }
    SteerVector = LimitedSteer;

    // Cap the steering vector's magnitude at this->0x94, comparing squared lengths
    // in double first. x multiplies its unrounded x*Inv by Cap; y and z round
    // their *Inv to float first.
    double const SteerSq = (static_cast<double>(SteerVector.z) * SteerVector.z +
                            static_cast<double>(SteerVector.y) * SteerVector.y) +
                           static_cast<double>(SteerVector.x) * SteerVector.x;
    double const MagnitudeCap = TurnThrottle;
    if (SteerSq > MagnitudeCap * MagnitudeCap)
    {
        double const Inv = 1.0 / std::sqrt(SteerSq);
        float const UnitY = static_cast<float>(SteerVector.y * Inv);
        float const UnitZ = static_cast<float>(SteerVector.z * Inv);
        SteerVector.x = static_cast<float>(MagnitudeCap * (SteerVector.x * Inv));
        SteerVector.y = static_cast<float>(static_cast<double>(UnitY) * MagnitudeCap);
        SteerVector.z = static_cast<float>(static_cast<double>(UnitZ) * MagnitudeCap);
    }

    if (bUpValid != 0)
        SteerVector = Add(SteerVector, TransformVectorByMatrix(*IfaceOrientation(Sys), UpB0));

    bUpValid = 0;
    Sys->Thrust.SetAxisThrottle(SteerVector);

    // 1 = converged (remaining angle below tolerance); >= or unordered returns 0.
    return (RemainingAngle < FacingTolerance) ? 1 : 0;
}
