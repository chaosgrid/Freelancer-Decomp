// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll BreakAwayIdea (ctor 0x062d8970, Update LAB_062d8a50). See
// src/include/common_idea.h.

#include "common_idea.h"
#include "AI/Ideas/common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix
#include "x87_trig.h"         // retail's inline FSIN / FCOS

#include <cmath>
#include <cstring>

using namespace CmnIdea;

namespace
{
    // BreakAwayIdea::Update's four switch arms (062d8c65/062d8caa/062d8ce9/062d8d28):
    // rotate each row of the +0x60 frame in one component pair by +/-2*pi/3
    // (DAT_063a0518 / DAT_063a0510). Both outputs use the original values of both
    // operands, hence the locals. Retail keeps FCOS/FSIN and all products on the x87
    // stack; only the two stores per row round to float, so the arithmetic is double.

    // caseD_1 / caseD_2: x' = x*Cos - Sin*z, z' = Cos*z + x*Sin.
    void RotateFrameRowsXZ(Matrix& M, double Angle)
    {
        double const Cos = X87Cos(Angle);
        double const Sin = X87Sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            double const X = M.E[Row][0];
            double const Z = M.E[Row][2];
            M.E[Row][0] = static_cast<float>(X * Cos - Sin * Z);
            M.E[Row][2] = static_cast<float>(Cos * Z + X * Sin);
        }
    }

    // caseD_3 / caseD_4: y' = y*Cos + Sin*z, z' = Cos*z - y*Sin.
    void RotateFrameRowsYZ(Matrix& M, double Angle)
    {
        double const Cos = X87Cos(Angle);
        double const Sin = X87Sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            double const Y = M.E[Row][1];
            double const Z = M.E[Row][2];
            M.E[Row][1] = static_cast<float>(Y * Cos + Sin * Z);
            M.E[Row][2] = static_cast<float>(Cos * Z - Y * Sin);
        }
    }
}

// ----------------------------------------------------------------------------
// BreakAwayIdea -- BuzzBehavior::BreakAway.
// ----------------------------------------------------------------------------

// FUN_062d8970.
BreakAwayIdea::BreakAwayIdea()
    : BaseIdea()
    , Picker()
    , BreakStyle(kBreakStyleOutrun)
    , BreakAwayRange(350.0f)
    , Flag3c(1)
    , Pad3d{}
    , Throttle(1.0f)
    , Flag44(0)
    , Pad45{}
    , AttemptReverseElapsed(0.0f)
    , AttemptReverseTime(4.0f)
    , ReverseHoldElapsed(0.0f)
    , ReverseDistance(100.0f)
    , ReverseHoldTime(6.0f)
    , Flag5c(0)
    , Pad5d{}
    , Frame60(0.0f, 0.0f, 0.0f,          // +0x60: not touched by retail's ctor (Update's
              0.0f, 0.0f, 0.0f,          // first reroll fills it before any read); zeroed
              0.0f, 0.0f, 0.0f)          // here as a port-only safety init.
    , IntervalElapsed(100000.0f)
    , IntervalTime(100000.0f)
    , AfterburnerDelay(0.0f)
    , AfterburnerElapsed(0.0f)
    , TurnThrottle(1.0f)
    , RollThrottle(1.0f)
{
    Picker.Flag1c = 1;   // the one constant that differs from DodgeIdea
}

// Own vtable slot 1 -- LAB_062d8a50 (no Ghidra decompile; ported from the raw
// listing). Receiver settled by RTTI: slot 1 of BreakAwayIdea's vtable
// (.rdata:063a04fc) is its only xref. Context is the IBehaviorManager (used both as
// the Pathfind owner and as get_ship_up_direction's receiver).
//
// get_ship_up_direction is __thiscall void -> Vector, so the pushes that appear to
// precede it at 062d8b1c-062d8b2f are ComputeBankedSteeringFrame's args 5/4/3; this
// is why the call sites clean 0x14, not 0x18.
int BreakAwayIdea::Update(float DeltaTime)
{
    if (Object == nullptr)
    {
        return 3;
    }

    // Handle, its Nav block, the +0x84 owner and Handle + 0x18 are re-derived at
    // every use by retail and never tested.
    IBehaviorManager* const Manager = static_cast<IBehaviorManager*>(Context);

    // Volume probe: a zero return means "avoid, go this way instead" and takes over
    // the whole tick. Pathfind is unguarded.
    Vector ProbeDir;
    if (Manager->Pathfind->GetUserZoneApproachDirection(&ProbeDir) == 0)
    {
        Matrix Frame;
        BuildOrientationBasis(Frame, Handle->SelfTarget.Iface->get_transform().Rotation,
                              ProbeDir, nullptr);

        // 0x3e490fdb == pi/16.
        Matrix Spread;
        BuildRandomSpreadFrame(Spread, Frame, 0.19634954f, 0.0f);

        float TurnAngle = 0.0f;
        Vector AxisThrottle = TurnHelper::get_angular_throttle(
            Handle->SelfTarget.Iface->get_transform().Rotation, Spread, &TurnAngle);

        // DAT_0639f534 == pi/16, the spread constant again.
        Vector Command{ 0.0f, 0.0f, 0.0f };
        if (TurnAngle > 0.19634954f)
        {
            Vector const Up = Manager->get_ship_up_direction();
            ComputeBankedSteeringFrame(&Handle->SelfTarget, Up, Spread, AxisThrottle, 1);
            Command = AxisThrottle;
        }
        // else: retail passes a separate zero-filled slot, so the small-turn arm
        // commands a zero vector, not the unsteered throttle.

        Handle->Thrust.SetAxisThrottle(Command);
        Handle->Thrust.SetThrottle(1.0f, kThrottleModeEngine);
        return 0;
    }

    // Reroll timer: the unrounded sum is compared, reroll runs on >= (unordered
    // skips). Both fields start at 100000.0f so the first tick always rerolls;
    // Commit() re-arms that.
    double const RerollClock = static_cast<double>(DeltaTime) + IntervalElapsed;
    IntervalElapsed = static_cast<float>(RerollClock);
    if (RerollClock >= IntervalTime)
    {
        // Frame60 = look-at basis opposite the owner's heading, world up as hint.
        // Retail fetches get_transform() twice.
        Vector const WorldUp{ 0.0f, 1.0f, 0.0f };
        Vector Backward;
        Vector const Col2 = MatrixColumn2(Handle->SelfTarget.Iface->get_transform().Rotation);
        Vec3Assign(&Backward, -Col2.x, -Col2.y, -Col2.z);
        Matrix Basis;
        BuildOrientationBasis(Basis, Handle->SelfTarget.Iface->get_transform().Rotation,
                              Backward, &WorldUp);
        Frame60 = Basis;

        // FUN_062d76d0 on &Picker: a 1..4 tag followed by four float weights.
        int const Pick = GenericWeightedPick(&Picker.LastDirection);
        switch (Pick)
        {
        case kDodgeDirLeft:
            RotateFrameRowsXZ(Frame60, 2.0943951023931953); // DAT_063a0518, +2*pi/3
            break;
        case kDodgeDirRight:
            RotateFrameRowsXZ(Frame60, -2.0943951023931953); // DAT_063a0510, -2*pi/3
            break;
        case kDodgeDirUp:
            RotateFrameRowsYZ(Frame60, 2.0943951023931953); // DAT_063a0518
            break;
        case kDodgeDirDown:
            RotateFrameRowsYZ(Frame60, -2.0943951023931953); // DAT_063a0510
            break;
        default:
            break;
        }

        IntervalElapsed = 0.0f;
    }

    // Three modes; anything else returns 0.
    if (BreakStyle == kBreakStyleReverse)
    {
        // Unordered counts as not-less.
        if (!(AttemptReverseElapsed < AttemptReverseTime))
        {
            AttemptReverseElapsed = 0.0f;
            ReverseHoldElapsed = 0.0f;
            Flag5c = 0;
            return 0;
        }

        int const Clearance = EvaluateBreakAwayClearance();

        if (Clearance == 0)
        {
            // Unrounded sum compared; not-greater (or unordered) keeps waiting.
            double const Waited = static_cast<double>(DeltaTime) + ReverseHoldElapsed;
            ReverseHoldElapsed = static_cast<float>(Waited);
            if (!(Waited > ReverseHoldTime))
            {
                return 0;
            }
            ReverseHoldElapsed = 0.0f;
            return 1;
        }
        if (Clearance == 1)
        {
            AttemptReverseElapsed = 0.0f;
            ReverseHoldElapsed = 0.0f;
            return 1;
        }
        if (Clearance != 2)
        {
            return 0;
        }

        Flag5c = 0;
        AttemptReverseElapsed += DeltaTime;
        return 0;
    }

    if (BreakStyle == kBreakStyleSideways)
    {
        Handle->Thrust.SetThrottle(Throttle, kThrottleModeEngine);

        // Default aim is the reversed heading of this->Object, not of the nav
        // block's owner.
        Vector Aim{ 0.0f, 0.0f, 0.0f };
        {
            Vector const Col2 = MatrixColumn2(ObjInspectOf(Object)->get_transform().Rotation);
            Aim.x = -Col2.x;
            Aim.y = -Col2.y;
            Aim.z = -Col2.z;
        }

        // Retail's second watcher test (`- 8 == 0`) can only fire on raw value 8
        // and is folded into the null check. Picker.Flag1c picks the sign:
        // BreakAwayIdea's ctor sets it to 1 (negate), DodgeIdea's to 0.
        IObjRW* const Watched = WatchedObjectOf(Picker.Watcher);
        if (Watched != nullptr)
        {
            Vector const Col2 = MatrixColumn2(ObjInspectOf(Watched)->get_transform().Rotation);
            if (Picker.Flag1c != 0)
            {
                Aim.x = -Col2.x;
                Aim.y = -Col2.y;
                Aim.z = -Col2.z;
            }
            else
            {
                Aim = Col2;
            }
        }

        // Dot product accumulated x, z, y in double; compared against
        // DAT_06399408 (0.0f), unordered counts as not-greater.
        Vector OwnerBack;
        {
            Vector const Col2 = MatrixColumn2(Handle->SelfTarget.Iface->get_transform().Rotation);
            Vec3Assign(&OwnerBack, -Col2.x, -Col2.y, -Col2.z);
        }
        double const Facing = (static_cast<double>(OwnerBack.x) * Aim.x + static_cast<double>(OwnerBack.z) * Aim.z)
                              + static_cast<double>(OwnerBack.y) * Aim.y;

        if (Facing > 0.0)
        {
            // Null OutAngle here, unlike the avoid path.
            Vector Command = TurnHelper::get_angular_throttle(
                Handle->SelfTarget.Iface->get_transform().Rotation, Frame60, nullptr);

            Command.x *= TurnThrottle;
            Command.y *= TurnThrottle;
            Command.z *= TurnThrottle;

            Vector Limited{ 0.0f, 0.0f, 0.0f };
            LimitAxisThrottleForTurn(Limited, ObjInspectOf(Object), Command, &Frame60,
                                     0.0f, 1.0f, true);
            Command = Limited;

            // Mode 0 here; the avoid path passes 1.
            Vector const Up = Manager->get_ship_up_direction();
            ComputeBankedSteeringFrame(&Handle->SelfTarget, Up, Frame60, Command, 0);

            Handle->Thrust.SetAxisThrottle(Command);
            return 0;
        }

        // No steering: drive forward at RollThrottle along the owner's frame.
        Vector Drive = TransformVectorByMatrix(
            Handle->SelfTarget.Iface->get_transform().Rotation,
            Vector{ 0.0f, 0.0f, RollThrottle });
        Handle->Thrust.SetAxisThrottle(Drive);

        // Unrounded sum compared; strictly-less or unordered skips.
        double const Elapsed = static_cast<double>(DeltaTime) + AfterburnerElapsed;
        AfterburnerElapsed = static_cast<float>(Elapsed);
        if (Elapsed >= AfterburnerDelay)
        {
            Handle->Thrust.SetThrustersActive(true);
        }

        // Retail quirk: this arm does not null-check the watcher before the range
        // test, unlike the BreakStyle == 0 arm.
        IObjRW* const RangeTarget = WatchedObjectOf(Picker.Watcher);
        if (IsWithinInterceptRangeOfIface(&Handle->SelfTarget, RangeTarget, BreakAwayRange))
        {
            return 0;
        }
        return 1;
    }

    if (BreakStyle != kBreakStyleOutrun)
    {
        return 0;
    }

    // BreakStyle == 0: same "drive forward" shape as the tail above, plus a
    // watcher null check the other arm lacks.
    Vector Drive = TransformVectorByMatrix(
        Handle->SelfTarget.Iface->get_transform().Rotation,
        Vector{ 0.0f, 0.0f, RollThrottle });

    // Retail fetches get_transform() a second time and discards the result; kept.
    (void)Handle->SelfTarget.Iface->get_transform();

    Handle->Thrust.SetAxisThrottle(Drive);
    Handle->Thrust.SetThrottle(Throttle, kThrottleModeEngine);

    // Unrounded sum compared; strictly-less or unordered skips.
    double const Elapsed = static_cast<double>(DeltaTime) + AfterburnerElapsed;
    AfterburnerElapsed = static_cast<float>(Elapsed);
    if (Elapsed >= AfterburnerDelay)
    {
        Handle->Thrust.SetThrustersActive(true);
    }

    // Retail re-tests the watcher a third time with a second, unreachable call
    // setup (062d91f2); only the first is reachable.
    IObjRW* const RangeTarget = WatchedObjectOf(Picker.Watcher);
    if (RangeTarget == nullptr)
    {
        return 0;
    }
    if (IsWithinInterceptRangeOfIface(&Handle->SelfTarget, RangeTarget, BreakAwayRange))
    {
        return 0;
    }
    return 1;
}

// FUN_062d9220. Only caller is BreakAwayIdea::Update (.text:062d8d9d, ECX = this);
// see the declaration in common_idea.h.
// Returns 2 = nothing usable to break away from (or geometry does not qualify yet),
// 1 = clear, stop breaking away, 0 = still running.
int BreakAwayIdea::EvaluateBreakAwayClearance()
{
    // Retail's second downcast test can only fire on the raw value 8.
    IObjRW* const Watched = WatchedObjectOf(Picker.Watcher);
    if (Watched == nullptr)
    {
        return 2;
    }

    // Only evaluated while Flag5c is clear. 0xbf7ff972 == -0.9999f: the watched
    // object is almost exactly dead astern (~179.2 degree half-angle).
    bool bNearlyAstern = false;
    if (Flag5c == 0)
    {
        bNearlyAstern = IsPointOutsideHeadingCone(ObjInspectOf(Watched)->get_position(),
                                                  ObjInspectOf(Object),
                                                  BitsToFloat(0xbf7ff972u));

        Vector const Sync{ bNearlyAstern ? 1.0f : 0.0f, 0.0f, 0.0f };
        Handle->Thrust.SetNudgeDirection(Sync);   // Handle+0x18 untested
    }

    // Always runs, Flag5c or not.
    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetAxisThrottle(ZeroVector);

    // The ST0 return stays unrounded until the compare below.
    double const Clearance = NavInterceptClearanceOfIface(&Handle->SelfTarget, Watched);

    if (Flag5c == 0)
    {
        // Unordered counts as not-less.
        if (!(Clearance < ReverseDistance))
        {
            Flag5c = 0;
            return 2;
        }

        // 0xbf34fdf4 == -0.707f, a ~135-degree cone; not quite -sqrt(2)/2
        // (0xbf3504f3).
        bool const bAstern = IsPointOutsideHeadingCone(ObjInspectOf(Watched)->get_position(),
                                                       ObjInspectOf(Object),
                                                       BitsToFloat(0xbf34fdf4u));
        if (!bAstern || bNearlyAstern)
        {
            Flag5c = 0;
            return 2;
        }
    }
    // else: once Flag5c is latched, retail computes the clearance and discards it.

    Flag5c = 1;
    Handle->Thrust.SetThrottle(-1.0f, kThrottleModeEngine);

    // Retail re-reads the watcher rather than reusing the value.
    IObjRW* const Target = WatchedObjectOf(Picker.Watcher);
    if (Target == nullptr)
    {
        return 0;
    }

    // FUN_062e5190; a 0.0f cosine threshold: "we are past the target's beam".
    if (!IsAlignedAndOutsideHeadingCone(&Handle->SelfTarget, Target, 0.0f))
    {
        return 0;
    }

    Flag5c = 0;
    return 1;
}

// FUN_062d9450, __thiscall RET 4. Receiver is BreakAwayIdea (.text:062c6c8e
// `LEA EDI,[ESI+0x258]`); see the declaration in common_idea.h.
// BaseWatcher::set() is the `if (Target != Watched)` dedup retail open-codes at
// 062d9465, so it stands in for the protected set_pointer().
void BreakAwayIdea::SetWatchObject(void const* Object2)
{
    // IObjRW -> Watchable adjustment.
    Watchable const* const Target =
        (Object2 != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object2) + 8)
            : nullptr;

    Picker.Watcher.set(Target);
    Picker.Flag1c = 0;           // unconditional
}

// Own vtable slot 4 -- LAB_062d93e0.
void BreakAwayIdea::Commit()
{
    IntervalElapsed = 100000.0f;   // restores the ctor default
    AfterburnerElapsed = 0.0f;

    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetAxisThrottle(ZeroVector);

    // Suppressed on the player's own ship. Neither Handle nor the +0x84 pointer is
    // tested.
    if (!Handle->SelfTarget.Iface->is_player())
    {
        Handle->Thrust.SetThrustersActive(false);
    }
}
