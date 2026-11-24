// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll DodgeIdea (ctor 0x062d77d0, Update LAB_062d7950). See src/include/common_idea.h.

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
#include "x87_trig.h"         // retail's inline FSIN / FCOS

#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace CmnIdea;
using namespace CmnAiSteer;

// ----------------------------------------------------------------------------
// DodgeIdea -- BuzzBehavior::Dodge. See common_idea.h for the
// identification and the two-sided size proof.
// ----------------------------------------------------------------------------

// FUN_062d77d0.
DodgeIdea::DodgeIdea()
    : BaseIdea()
    , Picker()
    , IntervalTime(5.0f)
    , IntervalTimeVariancePercent(0.0f)
    , ConeAngle(BitsToFloat(0x3f860a92u))  // not pi/3, see the header
    , ConeAngleVariancePercent(0.0f)
    , WaggleAxisConeAngle(0.0f)
    , RollAngle(0.0f)
    , DodgeDistance(150.0f)
    , DodgeDistanceScale(0.0f)
    , DodgeDistanceVariance(BitsToFloat(0x3e4ccccdu))  // 0.2f
    , PreferredForward{}                          // not written by retail's ctor
    , bHasPreferredForward(0)
    , Pad65{}
    , WorkingFrame(0.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 0.0f)
    , IntervalTimeLeft(0.0f)
    , TurnThrottle(1.0f)
    , Throttle(0.0f)                      // not written by retail's ctor
    , Field98(0)
    , Opaque99{}
    , DodgeStyle(1)
    , VectorB8{}
    , FieldC4(30.0f)
    , SlideThrottle(1.0f)
    , VectorCc{ BitsToFloat(0x3dcccccdu), 0.0f, 1.0f } // 0.1f, 0, 1.0f
    , bRollFlipDirection(0)
    , FlagD9(0)
    , FlagDa(0)
    , PadDb(0)
    , ChaseSpeedScale(1.0f)
{
    // Retail inlines the DirectionPicker init (+0x14..+0x33) identically to
    // BreakAwayIdea's, except for Flag1c's constant.
    Picker.Flag1c = 0;
}

// FUN_062d88c0.
void DodgeIdea::SetPreferredForward(Vector const& Value)
{
    PreferredForward = Value;
    bHasPreferredForward = 1;
}

// Own vtable slot 4 -- LAB_062d8830.
void DodgeIdea::Commit()
{
    IntervalTimeLeft = 0.0f;

    // Retail zeroes the same stack scratch twice, once before each call; Handle + 0x18
    // is recomputed per call and never tested.
    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetNudgeDirection(ZeroVector);

    ZeroVector.x = 0.0f;
    ZeroVector.y = 0.0f;
    ZeroVector.z = 0.0f;
    Handle->Thrust.SetAxisThrottle(ZeroVector);

    Handle->Thrust.SetThrustersActive(false);

    DodgeDistanceScale = 0.0f;
}

namespace
{
    // The in-place row rotations DodgeIdea::Update's caseD_0 composes. Cosine and
    // sine stay double on the x87 stack, except in the negated-angle arms, whose
    // callers pass a float-rounded cosine. Old X/Y is kept on the stack, Z is
    // re-read from memory, each result rounds to float:
    //   X' = X*C - S*Z,  Z' = C*Z + X*S
    void RotateFrameXZ(Matrix& M, double C, double S)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            double const X = M.E[Row][0];
            M.E[Row][0] = static_cast<float>(X * C - S * M.E[Row][2]);
            M.E[Row][2] = static_cast<float>(C * M.E[Row][2] + X * S);
        }
    }

    //   Y' = S*Z + Y*C,  Z' = C*Z - Y*S
    void RotateFrameYZ(Matrix& M, double C, double S)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            double const Y = M.E[Row][1];
            M.E[Row][1] = static_cast<float>(S * M.E[Row][2] + Y * C);
            M.E[Row][2] = static_cast<float>(C * M.E[Row][2] - Y * S);
        }
    }
}

// DodgeIdea::Update -- LAB_062d7950, vtable 063a04c8 slot 1. Shares its steering
// helpers with FaceIdea::Update (common_aisteer_internal.h).
int DodgeIdea::Update(float DeltaTime)
{
    if (Object == nullptr)
        return 3;

    SystemManager* const Sys = Handle;
    IBehaviorManager* const Manager = static_cast<IBehaviorManager*>(Context);
    FAiTarget* const NavRecord = &Sys->SelfTarget;

    // Reroll runs only when the unrounded difference is <= 0 (unordered skips).
    double const TimerLeft = static_cast<double>(IntervalTimeLeft) - static_cast<double>(DeltaTime);
    PathfindManager* const Pathfind = Manager->Pathfind;
    IntervalTimeLeft = static_cast<float>(TimerLeft);
    float TurnAngle3c = 0.0f;
    if (TimerLeft <= 0.0)
    {
        // One rounding; DAT_063997d0 == 2^-15.
        double const Draw8c = static_cast<double>(rand()) * (1.0 / 32768.0);
        IntervalTimeLeft = static_cast<float>(static_cast<double>(IntervalTime) -
                                     (static_cast<double>(IntervalTimeVariancePercent) * IntervalTime) * Draw8c);

        DodgeDistanceScale = static_cast<float>((static_cast<double>(rand()) * DodgeDistanceVariance) * (1.0 / 32768.0) + 1.0);

        Vector ProbeDirection{};
        if (Pathfind->GetUserZoneApproachDirection(&ProbeDirection) == 0)
        {
            // Avoidance: spread a basis down the probe direction by pi/16, then skip
            // the first switch.
            Matrix Basis;
            BuildOrientationBasis(Basis, *IfaceOrientation(Sys), ProbeDirection, nullptr);
            BuildRandomSpreadFrame(WorkingFrame, Basis, 0.19634955f /* 0x3e490fdb */, 0.0f);
        }
        else
        {
            // Not ported: retail then computes, per Field98, a vector from +0x9c/+0xa8
            // (or -column 2) into stack slots that every later path overwrites before
            // reading. All helpers involved are pure, so the work is dead.
            RefreshCachedIfacePosition(Sys);

            switch (DodgeStyle)
            {
            case 0:
            {
                // Two draws, each rounded once.
                float const Angle1 = static_cast<float>(
                    (1.0 - (static_cast<double>(rand()) * ConeAngleVariancePercent) * (1.0 / 32768.0)) * ConeAngle);
                double const Draw2 = static_cast<double>(rand()) * (1.0 / 32768.0);
                double const Spread2 = Draw2 * WaggleAxisConeAngle;
                float const Angle2 = static_cast<float>((Spread2 + Spread2) - WaggleAxisConeAngle);

                Vector const UpHint{ 0.0f, 1.0f, 0.0f };
                if (bHasPreferredForward != 0)
                {
                    BuildOrientationBasis(WorkingFrame, *IfaceOrientation(Sys), PreferredForward, &UpHint);
                }
                else
                {
                    Matrix const& Current = *IfaceOrientation(Sys);
                    Vector const Forward{ -Current.E[0][2], -Current.E[1][2], -Current.E[2][2] };
                    BuildOrientationBasis(WorkingFrame, *IfaceOrientation(Sys), Forward, &UpHint);
                }

                switch (GenericWeightedPick(&Picker.LastDirection))
                {
                case 1:   // XZ by Angle1, then YZ by Angle2
                    RotateFrameXZ(WorkingFrame, X87Cos(static_cast<double>(Angle1)),
                                  X87Sin(static_cast<double>(Angle1)));
                    RotateFrameYZ(WorkingFrame, X87Cos(static_cast<double>(Angle2)),
                                  X87Sin(static_cast<double>(Angle2)));
                    break;
                case 2:   // XZ by -Angle1, cosine rounded to float
                {
                    double const Neg = -static_cast<double>(Angle1);
                    float const CosNeg = X87CosF(Neg);
                    RotateFrameXZ(WorkingFrame, CosNeg, X87Sin(Neg));
                    RotateFrameYZ(WorkingFrame, X87Cos(static_cast<double>(Angle2)),
                                  X87Sin(static_cast<double>(Angle2)));
                    break;
                }
                case 3:   // YZ by Angle1, then XZ by Angle2
                    RotateFrameYZ(WorkingFrame, X87Cos(static_cast<double>(Angle1)),
                                  X87Sin(static_cast<double>(Angle1)));
                    RotateFrameXZ(WorkingFrame, X87Cos(static_cast<double>(Angle2)),
                                  X87Sin(static_cast<double>(Angle2)));
                    break;
                case 4:   // YZ by -Angle1, cosine rounded to float
                {
                    double const Neg = -static_cast<double>(Angle1);
                    float const CosNeg = X87CosF(Neg);
                    RotateFrameYZ(WorkingFrame, CosNeg, X87Sin(Neg));
                    RotateFrameXZ(WorkingFrame, X87Cos(static_cast<double>(Angle2)),
                                  X87Sin(static_cast<double>(Angle2)));
                    break;
                }
                default:
                    break;
                }
                break;
            }

            case 1:
            {
                float const Angle1 = static_cast<float>(
                    (1.0 - (static_cast<double>(rand()) * ConeAngleVariancePercent) * (1.0 / 32768.0)) * ConeAngle);

                Matrix Basis;
                if (bHasPreferredForward != 0)
                {
                    BuildOrientationBasis(Basis, *IfaceOrientation(Sys), PreferredForward, nullptr);
                }
                else
                {
                    // A watched object replaces -column 2 with FUN_062e6150's result,
                    // copied (Flag1c set) or negated (Flag1c clear).
                    Matrix const& Current = *IfaceOrientation(Sys);
                    Vector Forward{ -Current.E[0][2], -Current.E[1][2], -Current.E[2][2] };
                    IObjRW* const Watched =
                        ObjRwFromWatchable(const_cast<void*>(static_cast<void const*>(Picker.Watcher.target())));
                    if (Watched != nullptr)
                    {
                        Vector Direction;
                        GetNegatedIfaceDirection(&Direction, Watched);
                        if (Picker.Flag1c != 0)
                            Forward = Direction;
                        else
                            Forward = Vector{ -Direction.x, -Direction.y, -Direction.z };
                    }
                    BuildOrientationBasis(Basis, *IfaceOrientation(Sys), Forward, nullptr);
                }

                if (!Sys->SelfTarget.Iface->is_player())
                {
                    BuildRandomSpreadFrame(WorkingFrame, Basis, Angle1, RollAngle);
                    break;
                }
                // Player ship: spread the perpendicular frame (0x3f7fbe77 == 0.999f) with
                // bank 0 when both tests pass, else a fresh get_transform() with RollAngle.
                Matrix Perpendicular;
                if (Sys->Thrust.CheckSteeringGate() &&
                    BuildPerpendicularFrame(PreferredForward, &Perpendicular, 0.999f, nullptr))
                {
                    BuildRandomSpreadFrame(WorkingFrame, Perpendicular, Angle1, 0.0f);
                    break;
                }
                BuildRandomSpreadFrame(WorkingFrame, *IfaceOrientation(Sys), Angle1, RollAngle);
                break;
            }

            case 2:
            {
                // Dot summed z, y, x in double against 0.7f (DAT_063a04f0); less, equal
                // or unordered keeps the draw.
                Vector const Draw = RandomVector(1.0f);
                double const Dot = (static_cast<double>(Draw.z) * VectorB8.z +
                                    static_cast<double>(Draw.y) * VectorB8.y) +
                                   static_cast<double>(Draw.x) * VectorB8.x;
                if (Dot > static_cast<double>(0.7f))
                    VectorB8 = Vector{ -Draw.x, -Draw.y, -Draw.z };
                else
                    VectorB8 = Draw;
                break;
            }

            case 3:
                if (bRollFlipDirection != 0)
                    VectorCc.z = -VectorCc.z;
                break;

            default:
                break;
            }
        }
    }

    // caseD_4 (062d8178).
    if (Pathfind->AvoidRecordKind == 2)
    {
        RefreshCachedIfacePosition(Sys);
        Vector const RecomputeDir = Sub(Pathfind->AvoidTargetPoint, CachedIfacePosition(Sys));
        BuildOrientationBasis(WorkingFrame, *IfaceOrientation(Sys), RecomputeDir, nullptr);
    }

    // With a watched object and a non-player ship, the clearance blend may replace
    // the throttle.
    float ThrottleCommand = Throttle;
    IObjRW* const Target = ObjRwFromWatchable(const_cast<void*>(static_cast<void const*>(Picker.Watcher.target())));
    if (Target != nullptr && !Sys->SelfTarget.Iface->is_player())
    {
        float const Clearance = static_cast<float>(GetInterceptClearanceOfIface(NavRecord, Target));
        if (Picker.Flag1c != 0)
        {
            IObjInspect const* const SelfIface = ObjInspectOf(Object);
            float Value = Behavior_GetSpeed(ObjInspectOf(Target), 1.0f, ThrustEquipEngine);

            // Limit stays double. Too far: scale the target's speed by ChaseSpeedScale;
            // too close: use the own ship's speed; unordered leaves Value unchanged.
            double const Limit = static_cast<double>(DodgeDistanceScale) * DodgeDistance;
            if (Limit + 5.0 < static_cast<double>(Clearance))
                Value = static_cast<float>(static_cast<double>(Value) * ChaseSpeedScale);
            else if (Limit - 5.0 > static_cast<double>(Clearance))
                Value = Behavior_GetSpeed(SelfIface, 1.0f, ThrustEquipEngine);

            if (static_cast<double>(Value) < 50.0)
                Value = 50.0f;

            ThrottleCommand = Behavior_GetProjectedThrottle(SelfIface, Value, ThrustEquipEngine);
        }
    }

    // FUN_062e8850 / FUN_062e8840 are SystemManagerTimerTable::IsTimer4Active /
    // IsTimer5Active (byte flags at Timers+0x68 / +0x78).
    if (Sys->Timers.IsTimer4Active())
    {
        FlagDa = 1;
        Sys->Thrust.SetThrustersActive(true);
        ThrottleCommand = 1.0f;
    }
    else
    {
        if (FlagDa != 0)
            Sys->Thrust.SetThrustersActive(false);
        FlagDa = 0;
    }

    Sys->Thrust.SetThrottle(ThrottleCommand, 0);

    // Second dispatch on DodgeStyle (0 and 1 share a body).
    switch (DodgeStyle)
    {
    case 0:
    case 1:
    {
        Vector AxisThrottle = TurnHelper::get_angular_throttle(*IfaceOrientation(Sys), WorkingFrame,
                                                               &TurnAngle3c);
        AxisThrottle.x = AxisThrottle.x * TurnThrottle;
        AxisThrottle.y = AxisThrottle.y * TurnThrottle;
        AxisThrottle.z = AxisThrottle.z * TurnThrottle;

        Vector const Up = Manager->get_ship_up_direction();
        ComputeBankedSteeringFrame(NavRecord, Up, WorkingFrame, AxisThrottle, 1);

        IObjRW* const TurnObject =
            ObjRwFromWatchable(const_cast<void*>(static_cast<void const*>(Sys->Watcher.target())));
        Vector Limited;
        LimitAxisThrottleForTurn(Limited, ObjInspectOf(TurnObject), AxisThrottle, &WorkingFrame,
                                 0.0f, 0.3926991f /* 0x3ec90fdb */, true);
        AxisThrottle = Limited;

        if (Sys->Timers.IsTimer5Active())
            AxisThrottle = TransformVectorByMatrix(*IfaceOrientation(Sys), Vector{ 0.0f, 0.0f, 1.0f });

        Sys->Thrust.SetAxisThrottle(AxisThrottle);
        return 0;
    }

    case 2:
    {
        if (Pathfind->AvoidRecordKind != 0)
            return 0;

        // Dot summed z, y, x in double; compared against (double)0.9999f, unordered
        // takes the else arm.
        Matrix const& Current = *IfaceOrientation(Sys);
        double const Dot = (static_cast<double>(Current.E[2][2]) * VectorB8.z +
                            static_cast<double>(Current.E[1][2]) * VectorB8.y) +
                           static_cast<double>(Current.E[0][2]) * VectorB8.x;
        if (std::fabs(Dot) < static_cast<double>(0.9999f))
        {
            // Cross product formed in double, each component rounded once.
            Matrix const& Second = *IfaceOrientation(Sys);
            double const Cx = Second.E[0][2];
            double const Cy = Second.E[1][2];
            double const Cz = Second.E[2][2];
            Vector const Cross{ static_cast<float>(Cz * VectorB8.y - Cy * VectorB8.z),
                                static_cast<float>(Cx * VectorB8.z - Cz * VectorB8.x),
                                static_cast<float>(Cy * VectorB8.x - Cx * VectorB8.y) };
            Vector Normalized;
            VecNormalize3(&Cross, &Normalized);
            Vector const Nudge{ SlideThrottle * Normalized.x, SlideThrottle * Normalized.y,
                                SlideThrottle * Normalized.z };
            Sys->Thrust.BlendNudgeDirection(Nudge, DeltaTime);
        }
        else
        {
            Matrix const& Second = *IfaceOrientation(Sys);
            Vector const Right{ Second.E[0][0], Second.E[1][0], Second.E[2][0] };
            Sys->Thrust.SetNudgeDirection(Right);
        }

        Vector const UpHint{ 0.0f, 1.0f, 0.0f };
        Matrix const& Current2 = *IfaceOrientation(Sys);
        Vector const NegForward{ -Current2.E[0][2], -Current2.E[1][2], -Current2.E[2][2] };
        Matrix Frame;
        SteeringFrameFromRelativeDir(Frame, *IfaceOrientation(Sys), NegForward, UpHint);

        // No TurnThrottle scaling on this path.
        Vector AxisThrottle = TurnHelper::get_angular_throttle(*IfaceOrientation(Sys), Frame,
                                                               &TurnAngle3c);

        Vector const Up = Manager->get_ship_up_direction();
        ComputeBankedSteeringFrame(NavRecord, Up, Frame, AxisThrottle, 1);

        IObjRW* const TurnObject =
            ObjRwFromWatchable(const_cast<void*>(static_cast<void const*>(Sys->Watcher.target())));
        Vector Limited;
        LimitAxisThrottleForTurn(Limited, ObjInspectOf(TurnObject), AxisThrottle, &Frame,
                                 0.0f, 0.3926991f /* 0x3ec90fdb */, true);
        AxisThrottle = Limited;

        if (Sys->Timers.IsTimer5Active())
            AxisThrottle = TransformVectorByMatrix(*IfaceOrientation(Sys), Vector{ 0.0f, 0.0f, 1.0f });

        Sys->Thrust.SetAxisThrottle(AxisThrottle);
        return 0;
    }

    case 3:
    {
        Vector Transformed = TransformVectorByMatrix(*IfaceOrientation(Sys), VectorCc);
        Sys->Thrust.SetAxisThrottle(Transformed);
        return 0;
    }

    default:
        return 0;
    }
}
