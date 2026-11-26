// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI steering frames: BuildOrientationBasis, BuildPerpendicularFrame,
// BuildRandomSpreadFrame, TurnHelper::get_angular_throttle, TransformVectorByMatrix,
// LimitAxisThrottleForTurn, SteeringFrameFromRelativeDir, ComputeBankedSteeringFrame
// (retail 0x062e1f40-0x062e37e0, one contiguous block), plus the CmnAiSteer helpers FaceIdea::Update and
// DodgeIdea::Update share (common_aisteer_internal.h).
// See src/include/common_aisteer.h for the subsystem writeup.

#include "common_aisteer.h"
#include "AI/common_aisteer_internal.h"
#include "x86math.h"
#include "common_csys.h"          // Acquire3DMathEngine, Matrix
#include "common_systemmanager.h" // SystemManager::ExternalIface
#include "x87_trig.h"             // retail's inline FSIN / FCOS

#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace CmnAiSteer;

// ----------------------------------------------------------------------------
// BuildOrientationBasis (0x062e1f40) -- see common_aisteer.h for the writeup.
// ----------------------------------------------------------------------------

namespace
{
    // Defined next to ComputeBankedSteeringFrame: FUN_062c47a0's x87 dot and the
    // FUN_06347e60 / float length / 0.01f / FUN_062dc410-or-zero normalize idiom.
    double BankDot(Vector const& A, Vector const& B);
    void BankNormalizeOrZero(Vector& V);

    // The dot-and-clamp the Reference-column fallback runs twice. Retail tests the unrounded
    // dot against 1.0f and the rounded one against -1.0f; a value above 1.0 unrounded
    // rounds to >= 1.0, so this is the clamp of the float image. NaN survives both tests.
    float ClampedFloatDot(Vector const& A, Vector const& B)
    {
        float const D = (float)BankDot(A, B);
        if (D > 1.0f)
            return 1.0f;
        if (D < -1.0f)
            return -1.0f;
        return D;
    }
}

void BuildOrientationBasis(Matrix& Out, Matrix const& Reference, Vector const& Forward, Vector const* UpHint)
{
    // Retail builds the basis in a zero-filled 9-dword local and copies it whole into Out on
    // every path, returning Out in EAX.
    Matrix Result;

    // FUN_062c5a70.
    Vector ZRaw;
    VecNegate3(&Forward, &ZRaw);

    // Double 0.0001 at DAT_0639c2b8; leaves on less only (NaN goes on). The degenerate exit
    // copies the all-zero local.
    double const ZRawSq = ((double)ZRaw.y * ZRaw.y + (double)ZRaw.z * ZRaw.z) + (double)ZRaw.x * ZRaw.x;
    if (ZRawSq < 0.0001)
    {
        Out = Result;
        return;
    }

    Vector Z = ZRaw;
    BankNormalizeOrZero(Z);

    // Retail computes and discards dot(Z, column 2 of Reference); both callees are pure.

    if (UpHint != nullptr)
    {
        // D stays in ST0, clamped in place; double 0.999 at DAT_0639fd40. Not-less (and NaN)
        // goes to the fallback.
        double D = BankDot(*UpHint, Z);
        if (D > 1.0f)
            D = 1.0f;
        else if (D < -1.0f)
            D = -1.0f;
        if (std::fabs(D) < 0.999)
        {
            Vector Right;
            VecCross3(&Right, UpHint, &Z);
            BankNormalizeOrZero(Right);
            // Not renormalised.
            Vector Up;
            VecCross3(&Up, &Z, &Right);

            // Columns [Right | Up | Z].
            Result.E[0][0] = Right.x; Result.E[1][0] = Right.y; Result.E[2][0] = Right.z;
            Result.E[0][1] = Up.x;    Result.E[1][1] = Up.y;    Result.E[2][1] = Up.z;
            Result.E[0][2] = Z.x;     Result.E[1][2] = Z.y;     Result.E[2][2] = Z.z;
            Out = Result;
            return;
        }
    }

    // Fallback: columns 0 / 1 of Reference and their clamped float dots with Z, D0 first.
    Vector Right = MatrixColumn0(Reference);
    Vector Up = MatrixColumn1(Reference);
    float const D0 = ClampedFloatDot(Right, Z);
    float const D1 = ClampedFloatDot(Up, Z);

    // The double at DAT_063a04e0 is (double)0.9999f; not-less (and NaN) takes the C0 arm.
    if (std::fabs((double)D1) < (double)0.9999f)
    {
        // Normalised with the inline idiom, not FUN_062e1680.
        Vector ZScaled;
        ZScaled.x = D1 * Z.x;
        ZScaled.y = D1 * Z.y;
        ZScaled.z = D1 * Z.z;
        VecSubInPlace3(&Up, &ZScaled);
        BankNormalizeOrZero(Up);
        Vector Cross;
        VecCross3(&Cross, &Up, &Z);
        VecNormalize3(&Cross, &Right);
        // FUN_062e0480; NaN keeps. Negate both, then recompute Up.
        if (BankDot(Right, MatrixColumn0(Reference)) < 0.0)
        {
            VecScaleInPlace3(&Right, -1.0f);
            VecScaleInPlace3(&Up, -1.0f);
            VecCross3(&Cross, &Z, &Right);
            VecNormalize3(&Cross, &Up);
        }
    }
    else
    {
        Vector ZScaled;
        ZScaled.x = D0 * Z.x;
        ZScaled.y = D0 * Z.y;
        ZScaled.z = D0 * Z.z;
        VecSubInPlace3(&Right, &ZScaled);
        Vector Normalized;
        VecNormalize3(&Right, &Normalized);
        Right = Normalized;
        Vector Cross;
        VecCross3(&Cross, &Z, &Right);
        VecNormalize3(&Cross, &Up);
        // FUN_062e04a0; negate both, no recompute.
        if (BankDot(Up, MatrixColumn1(Reference)) < 0.0)
        {
            VecScaleInPlace3(&Right, -1.0f);
            VecScaleInPlace3(&Up, -1.0f);
        }
    }

    // FUN_06275ee0 packs columns [Right | Up | Z].
    Result.E[0][0] = Right.x; Result.E[1][0] = Right.y; Result.E[2][0] = Right.z;
    Result.E[0][1] = Up.x;    Result.E[1][1] = Up.y;    Result.E[2][1] = Up.z;
    Result.E[0][2] = Z.x;     Result.E[1][2] = Z.y;     Result.E[2][2] = Z.z;
    Out = Result;
}

// FUN_062e3460. x87 terms are kept in double: every sum, square root, clamp and compare
// happens on the FPU stack; only the float stores round.
bool BuildPerpendicularFrame(Vector const& Direction, Matrix* OutFrame, float DotThreshold, Vector const* Reference)
{
    // Leaves on less only.
    double const LenSqDir = ((double)(Direction.x) * Direction.x +
                             (double)(Direction.y) * Direction.y) +
                            (double)(Direction.z) * Direction.z;
    if (LenSqDir < 1e-05)
        return false;

    // Normalized by the float-rounded length when the unrounded length is above 0.01f;
    // not-greater takes the zero arm.
    Vector NegDir{ -Direction.x, -Direction.y, -Direction.z };
    double const NegDirLen = std::sqrt(((double)(NegDir.x) * NegDir.x +
                                        (double)(NegDir.y) * NegDir.y) +
                                       (double)(NegDir.z) * NegDir.z);
    if (NegDirLen > (double)(0.01f))
        VecNormalizeInPlaceByLength3(&NegDir, (float)(NegDirLen));
    else
        NegDir = Vector{ 0.0f, 0.0f, 0.0f };

    // Reference defaults to (0, 1, 0); the null OutFrame test comes after the normalise.
    Vector const Column2 = NegDir;
    Vector Ref{ 0.0f, 1.0f, 0.0f };
    if (Reference != nullptr)
        Ref = *Reference;
    if (OutFrame == nullptr)
        return false;

    // Rejects unless DotThreshold > |Dot| (NaN rejects).
    double Dot = ((double)(NegDir.z) * Ref.z + (double)(NegDir.y) * Ref.y) +
                 (double)(Ref.x) * NegDir.x;
    if (Dot > 1.0)
        Dot = 1.0;
    else if (Dot < -1.0)
        Dot = -1.0;
    if (!((double)(DotThreshold) > std::fabs(Dot)))
        return false;

    // Right = cross(Reference, NegDir), normalized above 0.01f, zeroed otherwise.
    Vector Right;
    Right.x = (float)((double)(NegDir.z) * Ref.y - (double)(NegDir.y) * Ref.z);
    Right.y = (float)((double)(Ref.z) * NegDir.x - (double)(NegDir.z) * Ref.x);
    Right.z = (float)((double)(NegDir.y) * Ref.x - (double)(Ref.y) * NegDir.x);
    double const RightLen = std::sqrt(((double)(Right.z) * Right.z +
                                       (double)(Right.y) * Right.y) +
                                      (double)(Right.x) * Right.x);
    if (RightLen > (double)(0.01f))
        VecNormalizeInPlaceByLength3(&Right, (float)(RightLen));
    else
        Right = Vector{ 0.0f, 0.0f, 0.0f };

    // Up = cross(NegDir, Right).
    Vector Up;
    Up.x = (float)((double)(Right.z) * NegDir.y - (double)(Right.y) * NegDir.z);
    Up.y = (float)((double)(NegDir.z) * Right.x - (double)(Right.z) * NegDir.x);
    Up.z = (float)((double)(Right.y) * NegDir.x - (double)(NegDir.y) * Right.x);

    // Column 0 = Right, column 1 = Up, column 2 = NegDir (FUN_062e3710), in retail's store
    // order.
    OutFrame->E[0][0] = Right.x;
    OutFrame->E[2][0] = Right.z;
    OutFrame->E[0][1] = Up.x;
    OutFrame->E[1][0] = Right.y;
    OutFrame->E[2][1] = Up.z;
    OutFrame->E[1][1] = Up.y;
    MatrixSetColumn2(*OutFrame, Column2);
    return true;
}

// FUN_062e2630. See common_aisteer.h.
// Parameters (all five call sites clean 0x10 bytes): Out is the hidden struct-return
// pointer, returned again in EAX; Reference is held in EBX for the whole body (Ghidra's
// `unaff_EBX`); SpreadAngle is the float Ghidra shows as `unaff_retaddr`; BankAngle is a
// fourth float parameter Ghidra misses.
Matrix* BuildRandomSpreadFrame(Matrix& Out, Matrix const& Reference,
                                          float SpreadAngle, float BankAngle)
{
    // The prologue zeroes a 9-float scratch. Dead in practice -- Working is always
    // overwritten by the Frame copy below -- but reproduced for fidelity.
    Matrix Working;
    Matrix Frame;
    Matrix Result;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Col = 0; Col < 3; ++Col)
        {
            Working.E[Row][Col] = 0.0f;
            Frame.E[Row][Col] = 0.0f;
            Result.E[Row][Col] = 0.0f;
        }
    }

    // MatrixToQuaternion is engine slot +0x50; the engine is created lazily with no null test.
    Quaternion Quat0;
    Acquire3DMathEngine()->MatrixToQuaternion(Quat0, Reference);

    // The spread quaternion: W = FCOS(Half), K = FSIN(Half) / |RandomAxis|, both rounded to
    // float; XYZ = RandomAxis * K through FUN_06288830 (one float product each).
    Vector const RandomAxis = RandomVector(1.0f);
    double const HalfAngle = (double)(SpreadAngle) * 0.5f;
    Quaternion Quat1;
    Quat1.w = X87CosF(HalfAngle);
    double const AxisLen = std::sqrt(((double)(RandomAxis.y) * RandomAxis.y +
                                      (double)(RandomAxis.z) * RandomAxis.z) +
                                     (double)(RandomAxis.x) * RandomAxis.x);
    float const AxisScale = (float)(X87Sin(HalfAngle) / AxisLen);
    Quat1.x = RandomAxis.x * AxisScale;
    Quat1.y = RandomAxis.y * AxisScale;
    Quat1.z = RandomAxis.z * AxisScale;

    // MulQuat is engine slot +0x18.
    Quaternion Combined;
    Acquire3DMathEngine()->MulQuat(Combined, Quat0, Quat1);

    // QuaternionToMatrix is engine slot +0x54.
    Acquire3DMathEngine()->QuaternionToMatrix(Frame, Combined);

    // Unconditional copy before the branch. Working is what BuildOrientationBasis receives as
    // its Reference, because retail aims its Out at Frame's own storage.
    Working = Frame;

    // The gate is BankAngle itself (NaN takes the world-up arm).
    bool const bUseReferenceUp = BankAngle > 0.0f;

    // Both branches derive Forward the same way: the negated column 2 of Frame.
    Vector const Forward{ -Frame.E[0][2], -Frame.E[1][2], -Frame.E[2][2] };

    Vector UpHint;
    if (bUseReferenceUp)
    {
        // Reference's own column 1.
        UpHint = Vector{ Reference.E[0][1], Reference.E[1][1], Reference.E[2][1] };
    }
    else
    {
        UpHint = Vector{ 0.0f, 1.0f, 0.0f };
    }

    // Retail copies the returned basis back over Working. Both branches pass a real UpHint
    // pointer; retail never passes null here.
    BuildOrientationBasis(Result, Working, Forward, &UpHint);
    Working = Result;

    if (bUseReferenceUp)
    {
        // A random-signed roll of BankAngle applied to each row of Working in its local X/Y
        // plane (column 2 untouched).
        //
        // Retail dead code, reproduced as-is: the sign draw is
        // `if (rand() * (1/32768) < 0.0f) BankAngle = -BankAngle` (DAT_063997d0, and 0.0f at
        // DAT_06399408). rand() is never negative, so the roll is always positive. Kept literal
        // rather than "fixed" to 0.5f, since guessing retail's intent would change behaviour.
        double RollAngle = BankAngle;
        double const RandFrac = (double)(std::rand()) * 3.0517578e-05f;
        if (RandFrac < 0.0)
            RollAngle = -RollAngle;

        // cos is rounded to float, sin stays on the FPU.
        float const CosRoll = X87CosF(RollAngle);
        double const SinRoll = X87Sin(RollAngle);
        for (int Row = 0; Row < 3; ++Row)
        {
            double const Col0 = Working.E[Row][0];
            double const Col1 = Working.E[Row][1];
            Working.E[Row][0] = (float)(SinRoll * Col1 + Col0 * CosRoll);
            Working.E[Row][1] = (float)(CosRoll * Col1 - Col0 * SinRoll);
        }
    }

    // Every call site consumes the EAX == Out return.
    Out = Working;
    return &Out;
}

// FUN_062e2c00, cdecl (Out, Matrix*, x, y, z): the by-value vector is copied to a local and
// the lazily created 3DMathEngine (DAT_063fca48 / FUN_06391cf0) is called through +0x2c.
Vector TransformVectorByMatrix(Matrix const& M, Vector const& V)
{
    Vector const Src = V;
    Vector Result;
    Acquire3DMathEngine()->TransformMatrix(Result,
                                           M,
                                           Src); // +0x2C
    return Result;
}

namespace
{
    // The projection LimitAxisThrottleForTurn open-codes five times: FUN_062e1680 of the
    // source and of the column, FUN_062e1630 (ClampedDot3) rounded to float, the source's
    // FUN_06347e60 length times that, and the normalised column scaled by it through
    // FUN_06288830. This is not FUN_062e1730 (VecCosineProject3), which rounds differently.
    Vector LimitProjectOntoColumn(Vector const& Source, Vector const& Column)
    {
        Vector SourceDir;
        VecNormalize3(&Source, &SourceDir);
        Vector ColumnDir;
        VecNormalize3(&Column, &ColumnDir);
        float const Cosine = (float)(ClampedDot3(SourceDir, ColumnDir));
        double const Length = std::sqrt(((double)(Source.x) * Source.x +
                                         (double)(Source.y) * Source.y) +
                                        (double)(Source.z) * Source.z);
        float const Scale = (float)(Length * Cosine);
        Vector Projected;
        Projected.x = ColumnDir.x * Scale;
        Projected.y = ColumnDir.y * Scale;
        Projected.z = ColumnDir.z * Scale;
        return Projected;
    }

    // LimitAxisThrottleForTurn's two angle blocks: the FUN_062c47a0 dot is stored as float;
    // the unrounded dot is tested against 1.0f, the stored float against -1.0f.
    float LimitClampedAcos(Vector const& A, Vector const& B)
    {
        double const Dot = ((double)(A.z) * B.z + (double)(A.y) * B.y) +
                           (double)(A.x) * B.x;
        float Clamped = (float)(Dot);
        if (Dot > 1.0)
            Clamped = 1.0f;
        else if (Clamped < -1.0f)
            Clamped = -1.0f;
        return (float)(std::acos((double)(Clamped)));
    }
}

// ---------------------------------------------------------------------------
// FUN_062e2c70 (no Ghidra pseudocode; reconstructed from the listing).
//
// Signature, from the FaceIdea::Update call sites: plain __cdecl, nine caller-cleaned
// dwords, EAX = Out on exit, and Command is one Vector passed by value.
//
// Obj is an IObjInspect (the caller passes the Idea's own bound object at this+0x4), not a
// SystemManager::ExternalIface; the slots used are
//   +0x008 get_angular_velocity()                 (Vector by hidden pointer)
//   +0x00C get_transform()                        (only the 9-dword Rotation is copied)
//   +0x050 get_axis_throttle(Vector&)
//   +0x0E4 get_time_to_accelerate_angularly(f,f,f,f)
//   +0x0EC get_angular_distance_travelled(f,f,f,f)
//   +0x0F8 get_projected_axis_throttle_XY(f)
//
// What it computes. Command is decomposed into its three scalar projections on
// the object's own orientation columns (P0/P1/P2). The column-0/column-1 pair
// -- the pitch/yaw plane -- is scaled by a ratio derived from how far the
// object still has to turn (the angle between its column 2 and the target
// frame's column 2) versus ReferenceAngle, then clamped down twice: once by the
// throttle the object can actually deliver in that plane, once by
// get_projected_axis_throttle_XY. The column-2 part is scaled independently by
// a bank/roll factor derived from the column-1 angle. Out = plane*Ratio + P2*ZFactor.
//
// .rdata constants:
//   DAT_0639e6b0 = 0.01 (double), DAT_0639e6b8 = 0.01f, DAT_0639c9f0 = 0.001 (double),
//   DAT_0639e6c8 = 0.001f, DAT_063a0610 = 0.98 (double), DAT_063a0620 = 0.4f,
//   DAT_0639f5d0 = 0.3926991f (pi/8), DAT_063a076c = 2.5464792f (8/pi), DAT_0639c13c = 0.5f,
//   DAT_0639e2d8 = 1.5707964 (pi/2, double), DAT_0639a1d0 = 1.0f, DAT_06399404 = -1.0f,
//   DAT_06399408 = 0.0f.
Vector* LimitAxisThrottleForTurn(Vector& Out, IObjInspect const* Obj, Vector Command,
                                            Matrix const* TargetFrame, float TurnAngle,
                                            float ReferenceAngle, bool bBankBlend)
{
    if (Obj == nullptr)
    {
        Out = Command;
        return &Out;
    }

    // Only the 3x3 rotation is copied out, not the 12-dword Transform.
    Matrix const Frame = Obj->get_transform().Rotation;

    Vector const Col0 = MatrixColumn0(Frame);
    Vector const Col1 = MatrixColumn1(Frame);
    Vector const Col2 = MatrixColumn2(Frame);

    Vector P0 = LimitProjectOntoColumn(Command, Col0);
    Vector P1 = LimitProjectOntoColumn(Command, Col1);
    Vector P2 = LimitProjectOntoColumn(Command, Col2);

    // Angle between target column k and own column k (k = 2, then 1).
    float const AngleZ = LimitClampedAcos(MatrixColumn2(*TargetFrame), Col2);
    float const AngleY = LimitClampedAcos(MatrixColumn1(*TargetFrame), Col1);

    // TransposeTransform is engine slot +0x38; the engine is created lazily with no null test.
    Vector const AngularVelocity = Obj->get_angular_velocity();
    Vector LocalAngularVelocity;
    Acquire3DMathEngine()->TransposeTransform(LocalAngularVelocity, Frame, AngularVelocity);

    // The unrounded value is tested; only less stores 0.
    double const AngularSpeedXYD = std::sqrt((double)(LocalAngularVelocity.x) * LocalAngularVelocity.x +
                                             (double)(LocalAngularVelocity.y) * LocalAngularVelocity.y);
    float AngularSpeedXY = (float)(AngularSpeedXYD);
    if (AngularSpeedXYD < (double)(0.01f))
        AngularSpeedXY = 0.0f;

    float const AngleForCall = (TurnAngle < 0.0f) ? 0.0f : TurnAngle;
    float const SpeedForTime = ((double)(AngularSpeedXY) < 0.001) ? 0.0f : AngularSpeedXY;
    float const TimeToTurn = Obj->get_time_to_accelerate_angularly(0.0f, 0.0f, SpeedForTime, AngleForCall);

    // Less-or-equal gives 0; greater and unordered call get_angular_distance_travelled.
    double Reach;
    if (TimeToTurn < 0.0f || TimeToTurn == 0.0f)
    {
        Reach = 0.0;
    }
    else
    {
        float const SpeedForDistance = ((double)(AngularSpeedXY) < 0.001) ? 0.0f : AngularSpeedXY;
        Reach = Obj->get_angular_distance_travelled(0.0f, 0.0f, SpeedForDistance, TimeToTurn);
    }

    double Gate = Reach * 0.98;
    if (Gate < (double)(0.001f))
        Gate = (double)(0.001f);

    float PlaneLimit = 1.0f;
    if ((double)(AngleZ) < Gate)
    {
        if (TurnAngle == 0.0f)
            PlaneLimit = 0.0f;
        else
            PlaneLimit = Obj->get_projected_axis_throttle_XY(TurnAngle);
    }

    float Ratio = 1.0f;
    if (AngleZ < ReferenceAngle)
        Ratio = (float)((double)(AngleZ) / ReferenceAngle);

    Vector AxisThrottle{ 0.0f, 0.0f, 0.0f };
    Obj->get_axis_throttle(AxisThrottle);

    Vector const Q0 = LimitProjectOntoColumn(AxisThrottle, Col0);
    Vector const Q1 = LimitProjectOntoColumn(AxisThrottle, Col1);

    // FUN_062887d0, float adds.
    Vector CommandPlane;
    CommandPlane.x = P0.x + P1.x;
    CommandPlane.y = P0.y + P1.y;
    CommandPlane.z = P0.z + P1.z;
    Vector ThrottlePlane;
    ThrottlePlane.x = Q0.x + Q1.x;
    ThrottlePlane.y = Q0.y + Q1.y;
    ThrottlePlane.z = Q0.z + Q1.z;

    // The two lengths sum in different orders (FUN_06347e60 for the throttle plane).
    double const ThrottlePlaneLen = std::sqrt(((double)(ThrottlePlane.x) * ThrottlePlane.x +
                                               (double)(ThrottlePlane.y) * ThrottlePlane.y) +
                                              (double)(ThrottlePlane.z) * ThrottlePlane.z);
    double const CommandPlaneLen = std::sqrt(((double)(CommandPlane.z) * CommandPlane.z +
                                              (double)(CommandPlane.y) * CommandPlane.y) +
                                             (double)(CommandPlane.x) * CommandPlane.x);
    if (CommandPlaneLen + 0.4f > ThrottlePlaneLen)
    {
        double const Limit = ThrottlePlaneLen + 0.4f;
        if ((double)(Ratio) > Limit)
            Ratio = (float)(Limit);
    }

    if (Ratio > PlaneLimit)
        Ratio = PlaneLimit;

    // P2 is re-normalised (FUN_062e1680) when FUN_062c4780's squared length exceeds 0.01. NaN
    // AngleY takes 1.0f.
    float ZFactor = 1.0f;
    if (bBankBlend)
    {
        double const P2LenSq = ((double)(P2.x) * P2.x + (double)(P2.y) * P2.y) +
                               (double)(P2.z) * P2.z;
        if (P2LenSq > 0.01)
        {
            Vector Normalized;
            VecNormalize3(&P2, &Normalized);
            P2 = Normalized;
        }

        if (AngleY < 0.39269909f)
        {
            double const T = (double)(AngleY) * 2.54647923f;
            ZFactor = (float)(T * T * 0.5f);
        }
        else
        {
            ZFactor = 0.5f;
            if (!((double)(AngleY) < 1.5707963705062866))
                ZFactor = 1.0f;
        }
    }

    // Each step a float op (FUN_06288830 / FUN_062887d0); retail also builds a dead zero
    // vector here.
    Vector BankPart;
    BankPart.x = P2.x * ZFactor;
    BankPart.y = P2.y * ZFactor;
    BankPart.z = P2.z * ZFactor;
    Vector Plane;
    Plane.x = P0.x + P1.x;
    Plane.y = P0.y + P1.y;
    Plane.z = P0.z + P1.z;
    Vector PlanePart;
    PlanePart.x = Plane.x * Ratio;
    PlanePart.y = Plane.y * Ratio;
    PlanePart.z = Plane.z * Ratio;
    Out.x = PlanePart.x + BankPart.x;
    Out.y = PlanePart.y + BankPart.y;
    Out.z = PlanePart.z + BankPart.z;
    return &Out;
}

namespace
{
    // FUN_062c47a0, cdecl, result left in ST0.
    double BankDot(Vector const& A, Vector const& B)
    {
        return ((double)(A.z) * B.z + (double)(A.y) * B.y) +
               (double)(A.x) * B.x;
    }

    // The normalize-or-zero idiom ComputeBankedSteeringFrame inlines six times: FUN_06347e60
    // length; greater than 0.01f normalizes through FUN_062dc410 with the float-rounded length,
    // not-greater (and unordered) zeroes through FUN_0636cf40(0, 0, 0).
    void BankNormalizeOrZero(Vector& V)
    {
        double const Length = std::sqrt(((double)(V.x) * V.x + (double)(V.y) * V.y) +
                                        (double)(V.z) * V.z);
        if (Length > (double)(0.01f))
            VecNormalizeInPlaceByLength3(&V, (float)(Length));
        else
            V = Vector{ 0.0f, 0.0f, 0.0f };
    }
}

// FUN_062e37e0. Every x87 sum, product, FSQRT / FCOS / FSIN and compare is kept in double;
// a value is rounded to float only where retail stores a float.
bool ComputeBankedSteeringFrame(FAiTarget* Elem, Vector const& TargetDir, Matrix& WorkingFrame, Vector& BankVector, unsigned char Mode)
{
    // FAiTarget::GetFlags inlined; bit 0x30000 required.
    if ((Elem->GetFlags() & 0x30000) == 0)
        return false;

    bool bSteered = false;

    Vector const FrameForward = MatrixColumn2(WorkingFrame);
    Vector const IfaceForward = MatrixColumn2(Elem->Iface->get_transform().Rotation);

    // Mode != 0: 0.98f at DAT_063a0784; greater, equal or unordered returns false, else
    // BankVector is taken into the iface's local frame (engine slot +0x38).
    Vector BankLocal;
    if (Mode != 0)
    {
        double const Aligned = ((double)(FrameForward.x) * IfaceForward.x +
                                (double)(FrameForward.z) * IfaceForward.z) +
                               (double)(FrameForward.y) * IfaceForward.y;
        if (!(Aligned < (double)(0.98f)))
            return false;
        Transform const& Xform = Elem->Iface->get_transform();
        Vector Local;
        Acquire3DMathEngine()->TransposeTransform(Local, Xform.Rotation, BankVector);
        BankLocal = Local;
    }
    else
    {
        BankLocal = BankVector;
    }

    Matrix NewFrame = WorkingFrame;

    // Retail re-reads the iface transform for every column it uses.
    Vector const IfaceForward2 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
    float const AbsTargetAlign = (float)(std::fabs(
        ((double)(IfaceForward2.x) * TargetDir.x + (double)(IfaceForward2.z) * TargetDir.z) +
        (double)(IfaceForward2.y) * TargetDir.y));
    Vector const IfaceUp = MatrixColumn1(Elem->Iface->get_transform().Rotation);

    // 0.999f at DAT_063a0780; greater goes to the tail. An up-dot that is not less than 0
    // (including unordered) steers; less asks is_player (+0xbc).
    bool bSteer = false;
    if (!(AbsTargetAlign > 0.999f))
    {
        double const UpDot = ((double)(IfaceUp.x) * TargetDir.x + (double)(IfaceUp.z) * TargetDir.z) +
                             (double)(IfaceUp.y) * TargetDir.y;
        bSteer = !(UpDot < 0.0) || Elem->Iface->is_player();
    }

    if (bSteer)
    {
        // RollDirSign is seeded with BankLocal.y; it survives into the blend when the angle
        // test below sets the sign flag.
        bSteered = true;
        float RollDirSign = BankLocal.y;
        float RollScale = 0.0f;

        // 0.707f at DAT_063a0768; FUN_062c5a70 negates exactly.
        Vector const IfaceForward3 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
        Vector const NegForward3{ -IfaceForward3.x, -IfaceForward3.y, -IfaceForward3.z };
        Matrix PerpFrame;
        if (BuildPerpendicularFrame(NegForward3, &PerpFrame, 0.707f, &TargetDir))
        {
            Vector const PerpRight = MatrixColumn0(PerpFrame);
            Transform const& Xform = Elem->Iface->get_transform();
            Vector LocalRight;
            Acquire3DMathEngine()->TransposeTransform(LocalRight, Xform.Rotation, PerpRight);

            Vector RollAxis{ -BankLocal.y, BankLocal.x, 0.0f };
            BankNormalizeOrZero(RollAxis);

            // Clamped on the FPU, unrounded.
            double Dot = BankDot(LocalRight, RollAxis);
            if (Dot > 1.0)
                Dot = 1.0;
            else if (Dot < -1.0)
                Dot = -1.0;

            // The unrounded value is tested against -1.0, then the stored float against 1.0.
            double const BankLength = std::sqrt(((double)(BankLocal.z) * BankLocal.z +
                                                 (double)(BankLocal.x) * BankLocal.x) +
                                                (double)(BankLocal.y) * BankLocal.y);
            double const Scaled = -Dot * BankLength;
            RollScale = (float)(Scaled);
            if (Scaled < -1.0)
                RollScale = -1.0f;
            if ((double)(RollScale) > 1.0)
                RollScale = 1.0f;
        }

        // 0.01f at DAT_0639e6c0. |get_throttle (+0x4c)| floored to 0.4f, times
        // get_max_bank_angle (+0x100) and |RollScale| on the FPU.
        bool bRollSignSet = false;
        float BankAngle = 0.0f;
        double const AbsRollScale = std::fabs((double)(RollScale));
        if (AbsRollScale > (double)(0.01f))
        {
            BankAngle = 0.0f;
            Elem->Iface->get_throttle(BankAngle);
            BankAngle = std::fabs(BankAngle);
            if (BankAngle < 0.4f)
                BankAngle = 0.4f;
            double const MaxBank = Elem->Iface->get_max_bank_angle();
            BankAngle = (float)(MaxBank * BankAngle * AbsRollScale);
            if (RollScale < 0.0f)
                BankAngle = -BankAngle;
        }

        float const CosAbsBank = X87CosF(std::fabs((double)(BankAngle)));

        // (double)0.9999f at DAT_063a04e0.
        Vector const IfaceForward4 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
        double const AbsForwardDot = std::fabs(
            ((double)(IfaceForward4.z) * TargetDir.z + (double)(IfaceForward4.y) * TargetDir.y) +
            (double)(IfaceForward4.x) * TargetDir.x);
        if (AbsForwardDot < (double)(0.9999f) && AbsRollScale > (double)(0.01f))
        {
            // Right = cross(F, T).
            Vector const F5 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
            Vector Right;
            Right.x = (float)((double)(F5.z) * TargetDir.y - (double)(F5.y) * TargetDir.z);
            Right.y = (float)((double)(F5.x) * TargetDir.z - (double)(F5.z) * TargetDir.x);
            Right.z = (float)((double)(F5.y) * TargetDir.x - (double)(F5.x) * TargetDir.y);

            BankNormalizeOrZero(Right);

            // Retail normalizes Right a second time into a separate local.
            Vector const IfaceRight = MatrixColumn0(Elem->Iface->get_transform().Rotation);
            Vector RightAgain = Right;
            BankNormalizeOrZero(RightAgain);

            // Unrounded dot tested against 1.0f, the stored float against -1.0f.
            double const AngleDot = BankDot(RightAgain, IfaceRight);
            float AngleCos = (float)(AngleDot);
            if (AngleDot > 1.0)
                AngleCos = 1.0f;
            else if (AngleCos < -1.0f)
                AngleCos = -1.0f;

            NewFrame.E[0][0] = Right.x;
            NewFrame.E[1][0] = Right.y;
            NewFrame.E[2][0] = Right.z;

            // Up = cross(Right, F).
            Vector const F6 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
            Vector Up;
            Up.x = (float)((double)(Right.z) * F6.y - (double)(Right.y) * F6.z);
            Up.y = (float)((double)(Right.x) * F6.z - (double)(Right.z) * F6.x);
            Up.z = (float)((double)(Right.y) * F6.x - (double)(Right.x) * F6.y);
            BankNormalizeOrZero(Up);

            NewFrame.E[0][1] = Up.x;
            NewFrame.E[1][1] = Up.y;
            NewFrame.E[2][1] = Up.z;
            Vector const F7 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
            NewFrame.E[0][2] = F7.x;
            NewFrame.E[1][2] = F7.y;
            NewFrame.E[2][2] = F7.z;

            // FCOS / FSIN of BankAngle stay on the FPU; old1 is read before column 1 is stored.
            double const CosBank = X87Cos((double)(BankAngle));
            double const SinBank = X87Sin((double)(BankAngle));
            for (int Row = 0; Row < 3; ++Row)
            {
                double const Old0 = NewFrame.E[Row][0];
                double const Old1 = NewFrame.E[Row][1];
                NewFrame.E[Row][0] = (float)(Old0 * CosBank + SinBank * Old1);
                NewFrame.E[Row][1] = (float)(CosBank * Old1 - Old0 * SinBank);
            }

            // Clamped as AngleCos above.
            Vector const IfaceRight2 = MatrixColumn0(Elem->Iface->get_transform().Rotation);
            Vector const NewRight = MatrixColumn0(NewFrame);
            double const RotDot = BankDot(NewRight, IfaceRight2);
            float RotCos = (float)(RotDot);
            if (RotDot > 1.0)
                RotCos = 1.0f;
            else if (RotCos < -1.0f)
                RotCos = -1.0f;
            float const RotatedAngle = (float)(std::acos((double)(RotCos)));

            // Not-less (including unordered) goes straight to the angle test.
            float SignProbe = 0.0f;
            bool bAngleTest = true;
            Vector const IfaceUp2 = MatrixColumn1(Elem->Iface->get_transform().Rotation);
            double const AbsUpDot = std::fabs(
                ((double)(IfaceUp2.z) * TargetDir.z + (double)(IfaceUp2.y) * TargetDir.y) +
                (double)(IfaceUp2.x) * TargetDir.x);
            if (AbsUpDot < (double)(0.9999f))
            {
                Vector const U3 = MatrixColumn1(Elem->Iface->get_transform().Rotation);
                Vector UpCross;
                UpCross.x = (float)((double)(U3.y) * TargetDir.z - (double)(U3.z) * TargetDir.y);
                UpCross.y = (float)((double)(U3.z) * TargetDir.x - (double)(U3.x) * TargetDir.z);
                UpCross.z = (float)((double)(U3.x) * TargetDir.y - (double)(U3.y) * TargetDir.x);

                Vector const F8 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
                double const SignDot = ((double)(F8.z) * UpCross.z + (double)(F8.y) * UpCross.y) +
                                       (double)(F8.x) * UpCross.x;
                SignProbe = (float)(SignDot);

                // The positive test uses the unrounded SignDot, the negative test the stored
                // SignProbe. Anything else falls to the angle test.
                if (SignDot > 0.0 && RollScale > 0.0f)
                {
                    bRollSignSet = true;
                    RollDirSign = 1.0f;
                    bAngleTest = false;
                }
                else if (SignProbe < 0.0f && RollScale < 0.0f)
                {
                    bRollSignSet = true;
                    RollDirSign = -1.0f;
                    bAngleTest = false;
                }
            }

            if (bAngleTest)
            {
                // Greater sets the flag and keeps the seeded RollDirSign; otherwise the flag
                // stays clear.
                if (AngleCos > CosAbsBank)
                {
                    bRollSignSet = true;
                }
                else
                {
                    RollDirSign = -1.0f;
                    if (!(SignProbe > 0.0f))
                        RollDirSign = 1.0f;
                }
            }

            // get_angular_speed_Z is +0xf4, the 3-arg get_angular_distance_travelled +0xe8;
            // 1000.0f at DAT_063a0760, the double 0.3141592741012573 at DAT_063a0778.
            float const AngularSpeed = Elem->Iface->get_angular_speed_Z(1.0f);
            double const Denom =
                (double)(Elem->Iface->get_angular_distance_travelled(0.0f, AngularSpeed, 1000.0f)) +
                0.3141592741012573;

            // Unordered gives 1.0; 0.005f at DAT_063a0770.
            double Blend;
            if (!((double)(RotatedAngle) < Denom))
            {
                Blend = 1.0;
            }
            else
            {
                Blend = (double)(RotatedAngle) / Denom;
                Blend = Blend * Blend;
                if (Blend < (double)(0.005f))
                    Blend = 0.0;
            }
            double const Signed = Blend * RollDirSign;
            BankLocal.z = bRollSignSet ? (float)(Signed) : (float)(-Signed);
        }
        else
        {
            // BuildPerpendicularFrame's result is ignored.
            Vector const IfaceRightF = MatrixColumn0(Elem->Iface->get_transform().Rotation);
            BankLocal.z = (float)(-BankDot(IfaceRightF, TargetDir));
            Vector const IfaceForwardF = MatrixColumn2(Elem->Iface->get_transform().Rotation);
            Vector const NegForwardF{ -IfaceForwardF.x, -IfaceForwardF.y, -IfaceForwardF.z };
            BuildPerpendicularFrame(NegForwardF, &NewFrame, 0.707f, &TargetDir);
        }
    }

    // WorkingFrame is still the caller's, unmodified, at this point.
    Vector const TailForward = MatrixColumn2(WorkingFrame);
    Vector const NegTailForward{ -TailForward.x, -TailForward.y, -TailForward.z };
    Transform const& TailXform = Elem->Iface->get_transform();
    Vector const TailUp = MatrixColumn1(NewFrame);
    Matrix FinalFrame;
    BuildOrientationBasis(FinalFrame, TailXform.Rotation, NegTailForward, &TailUp);
    WorkingFrame = FinalFrame;

    BankVector = TransformVectorByMatrix(Elem->Iface->get_transform().Rotation, BankLocal);

    return bSteered;
}

namespace CmnAiSteer
{
    // The own ship's position cache: retail inlines FAiTarget::RefreshPos on Sys+0x14fc.
    void RefreshCachedIfacePosition(SystemManager* Sys)
    {
        Sys->SelfTarget.RefreshPos();
    }

    Vector CachedIfacePosition(SystemManager* Sys)
    {
        RefreshCachedIfacePosition(Sys);
        return Sys->SelfTarget.CachedPos;
    }

    // SelfTarget.Iface slot 0xc (get_transform), whose Transform opens with the orientation
    // Matrix. Retail has no null test.
    Matrix const* IfaceOrientation(SystemManager* Sys)
    {
        return &Sys->SelfTarget.Iface->get_transform().Rotation;
    }

    // Iface slot 0xc is IObjInspect::get_transform() everywhere and takes no arguments. At
    // the BuildOrientationBasis call sites the two PUSHes just before the slot-0xc call are
    // BuildOrientationBasis's own arguments 4 (UpHint, often 0) and 3 (Forward): MSVC pushes
    // __cdecl arguments right to left and emits the call for an earlier argument after the
    // later ones are on the stack. The caller's ADD ESP,0x10 (four dwords) confirms it. The
    // same pattern recurs around BuildRandomSpreadFrame and SteeringFrameFromRelativeDir;
    // count the caller's stack cleanup before concluding an arg-count mismatch.

    // FUN_062e3730, cdecl (Out, Reference, RelDir, UpHint), the same order as
    // BuildOrientationBasis. Retail returns Out in EAX; the three call sites only use it to
    // copy Out, so the port returns nothing.
    void SteeringFrameFromRelativeDir(Matrix& Out, Matrix const& Reference, Vector const& RelDir, Vector const& UpHint)
    {
        Vector Hint = UpHint;
        Matrix Frame;
        Matrix Basis;
        if (BuildPerpendicularFrame(RelDir, &Frame, 0.999f, &Hint)) // 0x3f7fbe77
        {
            // Column 1 of Frame becomes the UpHint.
            Hint.x = Frame.E[0][1];
            Hint.y = Frame.E[1][1];
            Hint.z = Frame.E[2][1];
            BuildOrientationBasis(Basis, Reference, RelDir, &Hint);
        }
        else
        {
            BuildOrientationBasis(Basis, Reference, RelDir, nullptr);
        }
        Out = Basis;
    }
}

// TurnHelper::get_angular_throttle, retail 0x062e28f0. See common_aisteer.h for the
// signature and constants.
Vector TurnHelper::get_angular_throttle(Matrix const& A, Matrix const& B, float* OutAngle)
{
    // Transpose via the Matrix 9-float ctor (FUN_06284f70).
    Matrix const TransposedA(A.E[0][0], A.E[1][0], A.E[2][0],
                              A.E[0][1], A.E[1][1], A.E[2][1],
                              A.E[0][2], A.E[1][2], A.E[2][2]);

    // MulMatrix is engine slot +0x20; the engine is created lazily with no null test.
    Matrix RelativeMat;
    Acquire3DMathEngine()->MulMatrix(RelativeMat, B, TransposedA);

    // MatrixToQuaternion is engine slot +0x50.
    Quaternion Quat;
    Acquire3DMathEngine()->MatrixToQuaternion(Quat, RelativeMat);

    // Retail tests -1 first, then 1.
    float ClampedW = Quat.w;
    if (Quat.w < -1.0f)
        ClampedW = -1.0f;
    else if (Quat.w > 1.0f)
        ClampedW = 1.0f;
    float Angle2 = (float)(std::acos((double)(ClampedW)) * 2.0);

    // asin Z is stored rounded but its unrounded value feeds the length; 1e-5f at DAT_0639b984.
    Vector Axis;
    Axis.x = (float)(std::asin((double)(Quat.x)));
    Axis.y = (float)(std::asin((double)(Quat.y)));
    double const AsinZ = std::asin((double)(Quat.z));
    Axis.z = (float)(AsinZ);
    double const AxisLenSq = (AsinZ * Axis.z + (double)(Axis.x) * Axis.x) +
                             (double)(Axis.y) * Axis.y;
    if (AxisLenSq > (double)(9.99999975e-06f))
    {
        double const Inv = 1.0 / std::sqrt(AxisLenSq);
        Axis.x = (float)(Axis.x * Inv);
        Axis.y = (float)(Axis.y * Inv);
        Axis.z = (float)(Inv * Axis.z);
    }

    // TransposeTransform is engine slot +0x38.
    Vector LocalAxis;
    Acquire3DMathEngine()->TransposeTransform(LocalAxis, A, Axis);

    // Scale x/y so the larger of the two is 1.
    Vector Scaled = LocalAxis;
    double const AbsX = std::fabs((double)(LocalAxis.x));
    double const AbsY = std::fabs((double)(LocalAxis.y));
    double const MaxXY = (AbsX > AbsY) ? AbsX : AbsY;
    if (MaxXY > 0.001)
    {
        double const InvMax = 1.0 / MaxXY;
        Scaled.x = (float)(LocalAxis.x * InvMax);
        Scaled.y = (float)(InvMax * LocalAxis.y);
    }

    // |z| above 0.1 snaps z to +/-1.
    if (std::fabs((double)(LocalAxis.z)) > 0.1)
    {
        Scaled.z = 1.0f;
        if (!((double)(LocalAxis.z) > 0.0))
            Scaled.z = -1.0f;
    }

    // TransformMatrix is engine slot +0x2c.
    Vector WorldThrottle;
    Acquire3DMathEngine()->TransformMatrix(WorldThrottle, A, Scaled);

    // pi at DAT_0639d208, 2pi at DAT_0639c9e8, -pi at DAT_0639d2a0.
    float const Pi = 3.14159274f;
    float const TwoPi = 6.28318548f;
    if (Angle2 > Pi)
    {
        WorldThrottle.x = -WorldThrottle.x;
        WorldThrottle.y = -WorldThrottle.y;
        WorldThrottle.z = -WorldThrottle.z;
        Angle2 = TwoPi - Angle2;
    }
    else if (Angle2 < -Pi)
    {
        WorldThrottle.x = -WorldThrottle.x;
        WorldThrottle.y = -WorldThrottle.y;
        WorldThrottle.z = -WorldThrottle.z;
        Angle2 = Angle2 + TwoPi;
    }

    // Optional angle out-param: retail's 4th stack dword, which Ghidra drops.
    if (OutAngle != nullptr)
        *OutAngle = Angle2;

    return WorldThrottle;
}
