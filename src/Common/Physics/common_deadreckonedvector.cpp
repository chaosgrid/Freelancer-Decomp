// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CDeadReckonedVector (non-inline members; the inline ones are in common_cobject.h).
#include "common_cobject.h"

#include <cmath>

// ============================================================================
// CDeadReckonedVector implementation
//
// Q(t) = BasePosition + Velocity*t + 0.5*Acceleration*t^2 is fitted through the
// three newest samples (t=0 at the newest), and a corrective quadratic
// Q2(tau) = BlendBasePosition + Velocity2*tau + 0.5*Acceleration2*tau^2 through
// Samples[1] at tau=-T, the previously rendered position at tau=0 and the
// primary quadratic's value at tau=ConvergeWindow. Doit() renders Q2 until
// ElapsedTime reaches ConvergeWindow and Q from then on.
//
// x87: retail works on the stack at 53-bit precision. Every Vector component is
// rounded to float (each Vector goes through FUN_0628b030 or the operator
// helpers FUN_06288800 / FUN_062887d0 / FUN_06288830); coefficients, lengths,
// dots and times stay double. A float temporary below means retail stored one.
//
// The copy constructor, operator=, CalcError, GetSample and the two Normalize
// members are inline in common_cobject.h, as retail compiled them.
// ============================================================================

namespace
{
    // FUN_0628b030, with each argument already rounded to float.
    inline Vector MakeVector(float X, float Y, float Z)
    {
        Vector Result;
        Result.x = X;
        Result.y = Y;
        Result.z = Z;
        return Result;
    }

    // v * s with s held on the x87 stack as a double, one rounding per component.
    inline Vector ScaleByDouble(Vector const& V, double S)
    {
        return MakeVector((float)(V.x * S), (float)(V.y * S),
                          (float)(V.z * S));
    }

    // FUN_06288800 / FUN_062887d0: float operands, one rounding per component.
    inline Vector VecSub(Vector const& A, Vector const& B)
    {
        return MakeVector(A.x - B.x, A.y - B.y, A.z - B.z);
    }

    inline Vector VecAdd(Vector const& A, Vector const& B)
    {
        return MakeVector(A.x + B.x, A.y + B.y, A.z + B.z);
    }

    // AddSample's lengths: summed (x^2 + z^2) + y^2, root kept double.
    inline double LengthXZY(Vector const& V)
    {
        double const X = V.x;
        double const Y = V.y;
        double const Z = V.z;
        return sqrt((X * X + Z * Z) + Y * Y);
    }
}

// FUN_06290930: Reset's stores, inlined. Deviation: retail never writes Samples or
// Timestamps here, so they start indeterminate; the port zeroes them, since GetSample or a
// SampleCount==0 Doit could read them before the first sample.
CDeadReckonedVector::CDeadReckonedVector()
{
    for (int Index = 0; Index < 3; ++Index)
    {
        Samples[Index] = MakeVector(0.0f, 0.0f, 0.0f);
        Timestamps[Index] = 0.0;
    }
    Reset();
}

CDeadReckonedVector::~CDeadReckonedVector()
{
}

void CDeadReckonedVector::Reset()
{
    // FUN_06290a20. Samples and Timestamps are not touched: a SampleCount==0
    // Doit after Reset still projects from the old Samples[0].
    BasePosition = MakeVector(0.0f, 0.0f, 0.0f);
    BlendBasePosition = MakeVector(0.0f, 0.0f, 0.0f);
    ProjectedPosition = MakeVector(0.0f, 0.0f, 0.0f);
    TrackedPosition = MakeVector(0.0f, 0.0f, 0.0f);
    Velocity = MakeVector(0.0f, 0.0f, 0.0f);
    Velocity2 = MakeVector(0.0f, 0.0f, 0.0f);
    ConvergedVelocity = MakeVector(0.0f, 0.0f, 0.0f);
    Acceleration = MakeVector(0.0f, 0.0f, 0.0f);
    Acceleration2 = MakeVector(0.0f, 0.0f, 0.0f);
    SampleCount = 0;
    ConvergeWindow = 0.0;
    ElapsedTime = 0.0;
    MaxCorrectiveTime = 0.25;
}

// FUN_06290af0
void CDeadReckonedVector::AddSample(Vector const& Pos, double Timestamp)
{
    // History shift. With no samples yet all three slots take the new one;
    // with one, only the two newest shift and Timestamps[2] is left alone;
    // otherwise all three shift.
    if (SampleCount == 0)
    {
        Samples[0] = Pos;
        Samples[1] = Pos;
        Samples[2] = Pos;
        Timestamps[0] = Timestamp;
        Timestamps[1] = Timestamp;
        Timestamps[2] = Timestamp;
    }
    else
    {
        if (SampleCount == 1)
        {
            Samples[1] = Samples[0];
            Samples[0] = Pos;
        }
        else
        {
            Samples[2] = Samples[1];
            Samples[1] = Samples[0];
            Samples[0] = Pos;
            Timestamps[2] = Timestamps[1];
        }
        Timestamps[1] = Timestamps[0];
        Timestamps[0] = Timestamp;
    }

    double const Dt01 = Timestamp - Timestamps[1];
    ++SampleCount;
    bool bNearStraight = false;
    ConvergeWindow = 0.0;
    double const Dt12 = Timestamps[1] - Timestamps[2];
    BasePosition = Samples[0];
    double const Span = Dt12 + Dt01;
    double PrevElapsed = ElapsedTime - Dt01;
    if (PrevElapsed < 0.0)
    {
        PrevElapsed = 0.0;
    }
    double const SpanT = PrevElapsed + Dt01;
    ConvergeWindow = Dt01;

    Vector const P0 = Samples[0];
    Vector const P1 = Samples[1];
    Vector const P2 = Samples[2];

    Vector D1 = VecSub(P1, P0);
    Vector D2 = VecSub(P1, P2);
    double const Len1 = LengthXZY(D1);
    double const Len2 = LengthXZY(D2);

    bool bQuadraticFit = false;
    if (Dt01 == 0.0 || Dt12 == 0.0 || Len1 == 0.0 || Len2 == 0.0)
    {
        // The degenerate cases set the near-straight flag before the secant
        // fallback, so they converge linearly below.
        bNearStraight = true;
    }
    else
    {
        double const InvLen1 = 1.0 / Len1;
        D1.x = (float)(D1.x * InvLen1);
        D1.y = (float)(D1.y * InvLen1);
        D1.z = (float)(D1.z * InvLen1);
        double const InvLen2 = 1.0 / Len2;
        D2.x = (float)(D2.x * InvLen2);
        D2.y = (float)(D2.y * InvLen2);
        D2.z = (float)(D2.z * InvLen2);

        // (x*x' + z*z') + y*y', on the stack.
        double const Dot = ((double)D2.x * D1.x + (double)D2.z * D1.z) +
                           (double)D2.y * D1.y;

        // The fit runs unless Dot > 0 (NaN fits).
        if (!(Dot > 0.0))
        {
            if (Dot < -0.996194)   // .rdata:0639c428
            {
                bNearStraight = true;
            }

            double const InvSpanDt12 = 1.0 / (Span * Dt12);
            double const CoefP2 = InvSpanDt12 * Dt01;
            Vector const P2Term = ScaleByDouble(P2, CoefP2);
            double const InvDt01 = 1.0 / Dt01;
            double const CoefP1 = 1.0 / Dt12 + InvDt01;
            Vector const P1Term = ScaleByDouble(P1, CoefP1);
            double const CoefP0 = 1.0 / Span + InvDt01;
            Vector const P0Term = ScaleByDouble(P0, CoefP0);
            Vector const Outer = VecSub(P2Term, P1Term);
            Velocity = VecAdd(P0Term, Outer);

            Vector const P2Acc = ScaleByDouble(P2, InvSpanDt12 + InvSpanDt12);
            Vector const P1Acc = ScaleByDouble(P1, 2.0 / (Dt12 * Dt01));
            Vector const P0Acc = ScaleByDouble(P0, 2.0 / (Span * Dt01));
            Vector const OuterAcc = VecSub(P2Acc, P1Acc);
            Acceleration = VecAdd(P0Acc, OuterAcc);

            bQuadraticFit = true;
        }
    }

    if (!bQuadraticFit)
    {
        if (Dt01 == 0.0)
        {
            Velocity = MakeVector(0.0f, 0.0f, 0.0f);
        }
        else
        {
            Vector const Secant = VecSub(P0, P1);
            Velocity = ScaleByDouble(Secant, 1.0 / Dt01);
        }
        Acceleration = MakeVector(0.0f, 0.0f, 0.0f);
        if (ConvergeWindow > MaxCorrectiveTime)
        {
            ConvergeWindow = MaxCorrectiveTime;
        }
    }

    if (ConvergeWindow > 0.0)
    {
        Vector const OldProjected = ProjectedPosition;
        BlendBasePosition = ProjectedPosition;
        double const Window = ConvergeWindow;

        // The primary quadratic at tau = Window: ((0.5*A)*W)*W, each product
        // rounded, then + (W*V + Base).
        Vector const HalfAcc = MakeVector(Acceleration.x * 0.5f, Acceleration.y * 0.5f,
                                          Acceleration.z * 0.5f);
        Vector const HalfAccW = ScaleByDouble(HalfAcc, Window);
        Vector const AccTerm = ScaleByDouble(HalfAccW, Window);
        Vector const VelTerm = ScaleByDouble(Velocity, Window);
        Vector const Anchored = VecAdd(VelTerm, BasePosition);
        Vector const Target = VecAdd(AccTerm, Anchored);
        Vector const Offset = VecSub(Target, OldProjected);

        if (bNearStraight)
        {
            // Straight line to the target.
            Velocity2 = ScaleByDouble(Offset, 1.0 / Window);
            Acceleration2 = MakeVector(0.0f, 0.0f, 0.0f);
        }
        else
        {
            // Q2(Window) == Target and Q2(-SpanT) == Samples[1] with these signs.
            double const K1 = 1.0 / SpanT - 1.0 / Window;
            double const SpanWindow = SpanT + Window;
            double const K2 = SpanT / (SpanWindow * Window);
            double const K3 = Window / (SpanT * SpanWindow);
            Vector const P1K3 = ScaleByDouble(P1, K3);
            Vector const OldK1 = ScaleByDouble(OldProjected, K1);
            Vector const TargetK2 = ScaleByDouble(Target, K2);
            Vector const Mixed = VecSub(OldK1, P1K3);
            Velocity2 = VecAdd(TargetK2, Mixed);

            // x87: three intermediates stay unrounded (Old.x*L1, P1.z*L3 and the
            // z difference); everything else is stored as a float first.
            double const L3 = 2.0 / (SpanT * (SpanT + Window));
            float const P1xL3 = (float)(P1.x * L3);
            float const P1yL3 = (float)(P1.y * L3);
            double const P1zL3 = P1.z * L3;
            double const L1 = 2.0 / (SpanT * Window);
            double const OldxL1 = OldProjected.x * L1;
            float const OldyL1 = (float)(OldProjected.y * L1);
            float const OldzL1 = (float)(OldProjected.z * L1);
            double const L2 = 2.0 / (SpanWindow * Window);
            float const TargetxL2 = (float)(Target.x * L2);
            float const TargetyL2 = (float)(Target.y * L2);
            float const TargetzL2 = (float)(Target.z * L2);
            float const DiffX = (float)(P1xL3 - OldxL1);
            float const DiffY = P1yL3 - OldyL1;
            double const DiffZ = P1zL3 - OldzL1;
            Acceleration2.x = DiffX + TargetxL2;
            Acceleration2.y = DiffY + TargetyL2;
            Acceleration2.z = (float)(DiffZ + TargetzL2);
        }
    }

    ElapsedTime = 0.0;
}

double CDeadReckonedVector::ConvergeTime()
{
    // FUN_06291780
    return ConvergeWindow;
}

// FUN_062917c0
void CDeadReckonedVector::Doit(double Dt)
{
    double const Elapsed = Dt + ElapsedTime;
    ElapsedTime = Elapsed;

    if (SampleCount == 0)
    {
        // The time is rounded to float first.
        float const T = (float)(Elapsed);
        Vector const Drift = MakeVector(ConvergedVelocity.x * T, ConvergedVelocity.y * T,
                                        ConvergedVelocity.z * T);
        ProjectedPosition = VecAdd(Samples[0], Drift);
        return;
    }

    double const Elapsed2 = Elapsed * Elapsed;
    bool const bBlending = Elapsed < ConvergeWindow;
    // x87: Elapsed2 is then stored as a float into the same slot; only
    // TrackedPosition uses the double.
    float const Elapsed2f = (float)(Elapsed2);

    {
        Vector const HalfAcc = MakeVector(Acceleration.x * 0.5f, Acceleration.y * 0.5f,
                                          Acceleration.z * 0.5f);
        Vector const AccTerm = ScaleByDouble(HalfAcc, Elapsed2);
        Vector const VelTerm = ScaleByDouble(Velocity, Elapsed);
        Vector const Anchored = VecAdd(BasePosition, VelTerm);
        TrackedPosition = VecAdd(AccTerm, Anchored);
    }

    if (!bBlending)
    {
        ProjectedPosition = TrackedPosition;
        Vector const HalfAcc = MakeVector(Acceleration.x * 0.5f, Acceleration.y * 0.5f,
                                          Acceleration.z * 0.5f);
        Vector const AccTerm = MakeVector(HalfAcc.x * Elapsed2f, HalfAcc.y * Elapsed2f,
                                          HalfAcc.z * Elapsed2f);
        ConvergedVelocity = VecAdd(AccTerm, Velocity);
        return;
    }

    Vector const HalfAcc2 = MakeVector(Acceleration2.x * 0.5f, Acceleration2.y * 0.5f,
                                       Acceleration2.z * 0.5f);
    Vector const AccTerm2 = MakeVector(HalfAcc2.x * Elapsed2f, HalfAcc2.y * Elapsed2f,
                                       HalfAcc2.z * Elapsed2f);
    Vector const VelTerm2 = ScaleByDouble(Velocity2, ElapsedTime);
    Vector const Anchored2 = VecAdd(VelTerm2, BlendBasePosition);
    ProjectedPosition = VecAdd(AccTerm2, Anchored2);
    ConvergedVelocity = VecAdd(AccTerm2, Velocity2);
}

Vector const& CDeadReckonedVector::GetProjectedPosition()
{
    // FUN_062917b0
    return ProjectedPosition;
}

Vector CDeadReckonedVector::ProjectVelocity(double Dt)
{
    // FUN_062916e0. Retail quirk: (0.5*Acceleration)*Dt*Dt + Velocity, not the
    // derivative A*Dt + V. x87: Dt stays double; x and y round after each
    // multiply, z stays on the stack until the final add.
    float const Hx = Acceleration.x * 0.5f;
    float const Hy = Acceleration.y * 0.5f;
    float const Hz = Acceleration.z * 0.5f;
    float const Ax = (float)(Hx * Dt);
    float const Ay = (float)(Hy * Dt);
    float const Px = (float)(Ax * Dt);
    float const Py = (float)(Ay * Dt);
    double const Pz = (Dt * Hz) * Dt;
    return MakeVector(Px + Velocity.x,
                      Py + Velocity.y,
                      (float)(Pz + Velocity.z));
}

void CDeadReckonedVector::SetMaxCorrectiveConvergeTime(double Time)
{
    MaxCorrectiveTime = Time;
}
