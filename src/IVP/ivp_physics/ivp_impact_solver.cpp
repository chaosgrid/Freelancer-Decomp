// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_mindist.h"
#include "ivp_mindist_settings.h"
#include "ivp_environment.h"
#include "ivp_anomaly_manager.h"
#include <cmath>

// ============================================================================
// IVP_Impact_Solver (retail 06356f30-0635867c): the 0x110-byte stack object
// built by do_impact_long_term FUN_06358d60 (ivp_impact.cpp). x87 stack
// values are double here and rounded where retail stores a float. Retail
// helpers used: IVP_U_Matrix3::vimult (FUN_06366010), IVP_U_Matrix::vmult4
// (FUN_063484e0), IVP_U_Point::normize (FUN_06347ef0) and
// real_length_plus_normize (FUN_06347e90), in ivp_math.h.
// ============================================================================

namespace
{
    // The solver's vectors are Freelancer Vector (x/y/z at +0/+4/+8); the
    // IVP_U_Point helpers used here only touch k[0..2].
    const IVP_U_Point* AsPoint(const Vector* V)
    {
        return reinterpret_cast<const IVP_U_Point*>(V);
    }

    IVP_U_Point* AsPoint(Vector* V)
    {
        return reinterpret_cast<IVP_U_Point*>(V);
    }

    const Vector* AsVector(const IVP_U_Point* P)
    {
        return reinterpret_cast<const Vector*>(P);
    }

    // The 4th-order cosine retail inlines twice in FUN_06357120, on the
    // unrounded _CIasin result. DAT_063a53d0 = 1.0f, DAT_063a5460 = 0.5f,
    // DAT_063a5660 = 0.041666668f.
    double IVP_ImpactSolver_CosTaylor(double Angle)
    {
        const double AngleSq = Angle * Angle;
        return ((double)(1.0f) - AngleSq * (double)(0.5f)) +
               (AngleSq * AngleSq) * (double)(0.041666668f);
    }

    // FUN_06358350's inlined WS -> OS rotation. Unlike FUN_06366010 every
    // component uses the same order, (r2.c*Z + r1.c*Y) + r0.c*X, rounded once.
    void IVP_ImpactSolver_RotateWSToOSInline(const IVP_U_Matrix* M, double X, double Y, double Z, Vector* Out)
    {
        Out->x = (float)((M->rows[2].k[0] * Z + M->rows[1].k[0] * Y) + M->rows[0].k[0] * X);
        Out->y = (float)((M->rows[2].k[1] * Z + M->rows[1].k[1] * Y) + M->rows[0].k[1] * X);
        Out->z = (float)((M->rows[2].k[2] * Z + M->rows[1].k[2] * Y) + M->rows[0].k[2] * X);
    }

    // The dot order of every dot product against NormalVectorWS in this class.
    double IVP_ImpactSolver_DotZYX(const Vector* N, const Vector* V)
    {
        return ((double)(N->z) * V->z + (double)(N->y) * V->y) +
               (double)(N->x) * V->x;
    }
}

// FUN_06357400. Pushes the contact apart until the relative speed along
// -PushVector reaches RescueSpeed. With bNormalDirection == 1 a positive
// relative speed counts as 0. Returns early when the needed speed or the
// magnitude is negative or NaN. The denominator sums core 1's term (with P0),
// rounded to float, and core 0's (with -P0). Core 0's deltas land in the
// solver fields +0x84 / +0x64, core 1's in +0x94 / +0x74.
void IVP_Impact_Solver::PushContactPoint(const Vector* PushVector, int32_t bNormalDirection)
{
    // Retail transforms both contact points (vmult4) into locals never read again.
    IVP_U_Point WorldContactPoint0;
    IVP_U_Point WorldContactPoint1;
    MWorldFCore[0]->vmult4(AsPoint(ContactCoordsOS[0]), &WorldContactPoint0);
    MWorldFCore[1]->vmult4(AsPoint(ContactCoordsOS[1]), &WorldContactPoint1);

    Vector Push0;
    Push0.x = -PushVector->x; // * DAT_063a5498 (-1.0f)
    Push0.y = -PushVector->y;
    Push0.z = -PushVector->z;

    Vector PointSpeed0;
    Vector PointSpeed1;
    Cores[0]->calc_point_speed_ws(ContactCoordsOS[0], &Speed0, &RotSpeed0, &PointSpeed0);
    Cores[1]->calc_point_speed_ws(ContactCoordsOS[1], &Speed1, &RotSpeed1, &PointSpeed1);

    const double Dx = (double)(PointSpeed1.x) - PointSpeed0.x;
    const double Dy = (double)(PointSpeed1.y) - PointSpeed0.y;
    const double Dz = (double)(PointSpeed1.z) - PointSpeed0.z;
    double RelativeSpeed = (Dz * Push0.z + Dy * Push0.y) + Dx * Push0.x;

    if (bNormalDirection == 1 && RelativeSpeed > 0.0)
    {
        RelativeSpeed = 0.0; // DAT_063a53d4
    }

    const float Needed = (float)((double)(RescueSpeed) - RelativeSpeed);
    if (!(Needed >= 0.0f))
    {
        return;
    }

    Vector PushWS = Push0;
    float Denominator = 0.0f;

    if ((Cores[1]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        IVP_U_Point PushOS;
        MWorldFCore[1]->vimult(AsPoint(&PushWS), &PushOS);
        Vector LinearDelta;
        Vector AngularDelta;
        Cores[1]->calc_impulse_deltas(ContactCoordsOS[1], AsVector(&PushOS), &PushWS, &LinearDelta, &AngularDelta);
        Vector PointDelta;
        Cores[1]->calc_point_speed_ws(ContactCoordsOS[1], &LinearDelta, &AngularDelta, &PointDelta);
        Denominator = (float)(((double)(PushWS.z) * PointDelta.z +
                                          (double)(PushWS.y) * PointDelta.y) +
                                         (double)(PointDelta.x) * PushWS.x);
    }

    if ((Cores[0]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        PushWS.x = -PushWS.x;
        PushWS.y = -PushWS.y;
        PushWS.z = -PushWS.z;
        IVP_U_Point PushOS;
        MWorldFCore[0]->vimult(AsPoint(&PushWS), &PushOS);
        Vector LinearDelta;
        Vector AngularDelta;
        Cores[0]->calc_impulse_deltas(ContactCoordsOS[0], AsVector(&PushOS), &PushWS, &LinearDelta, &AngularDelta);
        Vector PointDelta;
        Cores[0]->calc_point_speed_ws(ContactCoordsOS[0], &LinearDelta, &AngularDelta, &PointDelta);
        Denominator = (float)((((double)(PushWS.z) * PointDelta.z +
                                           (double)(PushWS.y) * PointDelta.y) +
                                          (double)(PointDelta.x) * PushWS.x) +
                                         (double)(Denominator));
    }

    const double MagnitudeExt = (double)(Needed) / Denominator;
    const float Magnitude = (float)(MagnitudeExt);              // FST: the test uses the unrounded value
    if (!(MagnitudeExt >= 0.0))
    {
        return;
    }

    if ((Cores[0]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        const double NegMagnitude = -(double)(Magnitude);
        Vector Impulse;
        Impulse.x = (float)(NegMagnitude * Push0.x);
        Impulse.y = (float)((double)(Push0.y) * NegMagnitude);
        Impulse.z = (float)((double)(Push0.z) * NegMagnitude);
        IVP_U_Point ImpulseOS;
        MWorldFCore[0]->vimult(AsPoint(&Impulse), &ImpulseOS);
        Cores[0]->calc_impulse_deltas(ContactCoordsOS[0], AsVector(&ImpulseOS), &Impulse,
                                      &SpeedDelta0, &RotSpeedDelta0);
        RotSpeed0.x = RotSpeedDelta0.x + RotSpeed0.x;
        RotSpeed0.y = RotSpeedDelta0.y + RotSpeed0.y;
        RotSpeed0.z = RotSpeedDelta0.z + RotSpeed0.z;
        Speed0.x = SpeedDelta0.x + Speed0.x;
        Speed0.y = SpeedDelta0.y + Speed0.y;
        Speed0.z = SpeedDelta0.z + Speed0.z;
    }

    if ((Cores[1]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        Vector Impulse;
        Impulse.x = Push0.x * Magnitude; // single-precision products
        Impulse.y = Push0.y * Magnitude;
        Impulse.z = Push0.z * Magnitude;
        IVP_U_Point ImpulseOS;
        MWorldFCore[1]->vimult(AsPoint(&Impulse), &ImpulseOS);
        Cores[1]->calc_impulse_deltas(ContactCoordsOS[1], AsVector(&ImpulseOS), &Impulse,
                                      &SpeedDelta1, &RotSpeedDelta1);
        RotSpeed1.x = RotSpeedDelta1.x + RotSpeed1.x;
        RotSpeed1.y = RotSpeedDelta1.y + RotSpeed1.y;
        RotSpeed1.z = RotSpeedDelta1.z + RotSpeed1.z;
        Speed1.x = SpeedDelta1.x + Speed1.x;
        Speed1.y = SpeedDelta1.y + Speed1.y;
        Speed1.z = SpeedDelta1.z + Speed1.z;
    }
}

// FUN_06356f30: RelativePointSpeed = point speed of core 1 - point speed of core 0.
void IVP_Impact_Solver::UpdateRelativeSpeed()
{
    Vector PointSpeed0;
    Vector PointSpeed1;
    Cores[0]->calc_point_speed_ws(ContactCoordsOS[0], &Speed0, &RotSpeed0, &PointSpeed0);
    Cores[1]->calc_point_speed_ws(ContactCoordsOS[1], &Speed1, &RotSpeed1, &PointSpeed1);
    RelativePointSpeed.x = PointSpeed1.x - PointSpeed0.x;
    RelativePointSpeed.y = PointSpeed1.y - PointSpeed0.y;
    RelativePointSpeed.z = PointSpeed1.z - PointSpeed0.z;
}

// FUN_06358560. Core 1 first; the normal is negated between the two cores.
// Afterwards a core with Flags & 0xC takes the other core's value times
// 100000.0f (DAT_063a5670), core 0's override first.
void IVP_Impact_Solver::PopulateEffectiveMasses(const Vector* NormalWS)
{
    Vector LocalNormal = *NormalWS;

    for (int32_t CoreIndex = 1; CoreIndex >= 0; --CoreIndex)
    {
        IVP_U_Point NormalOS;
        MWorldFCore[CoreIndex]->vimult(AsPoint(&LocalNormal), &NormalOS);
        const float EffectiveMass = (float)(
            Cores[CoreIndex]->calc_effective_mass_along_direction(ContactCoordsOS[CoreIndex], AsVector(&NormalOS),
                                                                  &LocalNormal)); // FUN_06346790
        if (CoreIndex == 1)
        {
            EffectiveMass1 = EffectiveMass;
        }
        else
        {
            EffectiveMass0 = EffectiveMass;
        }

        LocalNormal.x = -LocalNormal.x; // 063585bb-063585ed, FMUL DAT_063a5498
        LocalNormal.y = -LocalNormal.y;
        LocalNormal.z = -LocalNormal.z;
    }

    if ((Cores[0]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
    {
        EffectiveMass0 = EffectiveMass1 * 100000.0f;
    }
    if ((Cores[1]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
    {
        EffectiveMass1 = EffectiveMass0 * 100000.0f;
    }
}

// FUN_06358630. Returned unrounded (x87 ST0).
double IVP_Impact_Solver::CalcImpulseNeeded2()
{
    PopulateEffectiveMasses(NormalVectorWS);

    const double NegDot = -IVP_ImpactSolver_DotZYX(NormalVectorWS, &RelativePointSpeed);
    const double Scaled = NegDot * EffectiveMass1;
    const double Doubled = Scaled + Scaled;
    const double InvSum = 1.0 / ((double)(EffectiveMass0) + EffectiveMass1);
    return (Doubled * InvSum) * EffectiveMass0;
}

// FUN_06358350. Core 0's deltas land in SpeedDelta0 / RotSpeedDelta0; core 1
// gets the negated impulse into SpeedDelta1 / RotSpeedDelta1. The adds are
// FUN_06355020 except core 1's linear one, which is inline.
void IVP_Impact_Solver::StepImpulse2(float ImpulseMagnitude)
{
    Vector Impulse;
    Impulse.y = PushDirection.y * ImpulseMagnitude;
    Impulse.z = PushDirection.z * ImpulseMagnitude;
    Impulse.x = PushDirection.x * ImpulseMagnitude;

    if ((Cores[0]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        Vector ImpulseOS;
        IVP_ImpactSolver_RotateWSToOSInline(MWorldFCore[0], Impulse.x, Impulse.y, Impulse.z, &ImpulseOS);
        Cores[0]->calc_impulse_deltas(ContactCoordsOS[0], &ImpulseOS, &Impulse, &SpeedDelta0,
                                      &RotSpeedDelta0);
        RotSpeed0.x = RotSpeed0.x + RotSpeedDelta0.x;
        RotSpeed0.y = RotSpeed0.y + RotSpeedDelta0.y;
        RotSpeed0.z = RotSpeed0.z + RotSpeedDelta0.z;
        Speed0.x = Speed0.x + SpeedDelta0.x;
        Speed0.y = Speed0.y + SpeedDelta0.y;
        Speed0.z = Speed0.z + SpeedDelta0.z;
    }

    if ((Cores[1]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        Vector NegImpulse;
        NegImpulse.x = -Impulse.x; // * DAT_063a5498 (-1.0f)
        NegImpulse.y = -Impulse.y;
        NegImpulse.z = -Impulse.z;
        Vector ImpulseOS;
        IVP_ImpactSolver_RotateWSToOSInline(MWorldFCore[1], NegImpulse.x, NegImpulse.y, NegImpulse.z, &ImpulseOS);
        Cores[1]->calc_impulse_deltas(ContactCoordsOS[1], &ImpulseOS, &NegImpulse, &SpeedDelta1,
                                      &RotSpeedDelta1);
        RotSpeed1.x = RotSpeed1.x + RotSpeedDelta1.x;
        RotSpeed1.y = RotSpeed1.y + RotSpeedDelta1.y;
        RotSpeed1.z = RotSpeed1.z + RotSpeedDelta1.z;
        Speed1.x = SpeedDelta1.x + Speed1.x;
        Speed1.y = SpeedDelta1.y + Speed1.y;
        Speed1.z = SpeedDelta1.z + Speed1.z;
    }
}

// FUN_06357340: subtracts the deltas StepImpulse2 last wrote.
void IVP_Impact_Solver::UndoLastStepImpulse()
{
    if ((Cores[0]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        RotSpeed0.x = RotSpeed0.x - RotSpeedDelta0.x;
        RotSpeed0.y = RotSpeed0.y - RotSpeedDelta0.y;
        RotSpeed0.z = RotSpeed0.z - RotSpeedDelta0.z;
        Speed0.x = Speed0.x - SpeedDelta0.x;
        Speed0.y = Speed0.y - SpeedDelta0.y;
        Speed0.z = Speed0.z - SpeedDelta0.z;
    }

    if ((Cores[1]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        RotSpeed1.x = RotSpeed1.x - RotSpeedDelta1.x;
        RotSpeed1.y = RotSpeed1.y - RotSpeedDelta1.y;
        RotSpeed1.z = RotSpeed1.z - RotSpeedDelta1.z;
        Speed1.x = Speed1.x - SpeedDelta1.x;
        Speed1.y = Speed1.y - SpeedDelta1.y;
        Speed1.z = Speed1.z - SpeedDelta1.z;
    }
}

// FUN_06356fb0. The sign test uses the unrounded dot; the rest uses its float.
void IVP_Impact_Solver::UpdateFrictionPushDirection()
{
    PushDirection = RelativePointSpeed;
    AsPoint(&PushDirection)->normize();

    const Vector* Normal = NormalVectorWS;
    const double DotExt = IVP_ImpactSolver_DotZYX(Normal, &PushDirection);
    const float Dot = (float)(DotExt);

    if (DotExt > 0.0)
    {
        PushDirection = FrictionImpulse;
        AsPoint(&PushDirection)->normize();
        return;
    }

    if (UseFrictionConeBlend != 0)
    {
        ClampPushDirectionToFrictionCone(Dot);
        return;
    }

    if (-NormalCoeff >= Dot)
    {
        return;
    }

    const double NegDot = -(double)(Dot);
    IVP_U_Point Tangent;
    Tangent.k[1] = (float)(NegDot * Normal->y + PushDirection.y);
    Tangent.k[2] = (float)(NegDot * Normal->z + PushDirection.z);
    Tangent.k[0] = (float)(NegDot * Normal->x + PushDirection.x);
    Tangent.normize();

    const float ScaledY = Tangent.k[1] * TangentCoeff;
    const float ScaledZ = Tangent.k[2] * TangentCoeff;
    const float ScaledX = Tangent.k[0] * TangentCoeff;

    const double NegNormalCoeff = -(double)(NormalCoeff);
    const float NewY = (float)(NegNormalCoeff * Normal->y + ScaledY);
    const float NewZ = (float)(NegNormalCoeff * Normal->z + ScaledZ);
    const float NewX = (float)(NegNormalCoeff * Normal->x + ScaledX);
    PushDirection.z = NewZ;
    PushDirection.x = NewX;
    PushDirection.y = NewY;
}

// FUN_06357120. The unrounded |dot| and sqrt go to _CIasin; their float
// roundings are used in the sums. Nothing changes when the tangent length
// squared is <= the cone radius squared, or unordered.
void IVP_Impact_Solver::ClampPushDirectionToFrictionCone(float Dot)
{
    const Vector* Normal = NormalVectorWS;
    const double NegDot = -(double)(Dot);

    IVP_U_Point Tangent;
    Tangent.k[1] = (float)(NegDot * Normal->y + PushDirection.y);
    Tangent.k[2] = (float)(NegDot * Normal->z + PushDirection.z);
    Tangent.k[0] = (float)(NegDot * Normal->x + PushDirection.x);
    const float Length = (float)(Tangent.real_length_plus_normize()); // FUN_06347e90

    const double AbsDotExt = std::fabs(((double)(Tangent.k[2]) * FrictionReferenceAxis.z +
                                        (double)(Tangent.k[1]) * FrictionReferenceAxis.y) +
                                       (double)(Tangent.k[0]) * FrictionReferenceAxis.x);
    const float AbsDotF = (float)(AbsDotExt);
    const double Cos1 = IVP_ImpactSolver_CosTaylor(std::asin(AbsDotExt));

    const double TangentCoeffD = TangentCoeff;
    const double ConeBlendScaleD = ConeBlendScale;
    const double ConeRadiusSq = ((TangentCoeffD * TangentCoeffD) * Cos1) * Cos1 +
                                ((ConeBlendScaleD * ConeBlendScaleD) * AbsDotF) * AbsDotF;

    const double LengthSq = (double)(Length) * Length;
    if (!(LengthSq > ConeRadiusSq))
    {
        return;
    }

    const double Sin2 = std::sqrt(ConeRadiusSq);
    const float Sin2F = (float)(Sin2);
    const double NegCos2 = -IVP_ImpactSolver_CosTaylor(std::asin(Sin2));

    PushDirection.x = (float)(NegCos2 * Normal->x);
    PushDirection.y = (float)(NegCos2 * Normal->y);
    PushDirection.z = (float)(NegCos2 * Normal->z);
    PushDirection.x = (float)((double)(Tangent.k[0]) * Sin2F + PushDirection.x);
    PushDirection.y = (float)((double)(Tangent.k[1]) * Sin2F + PushDirection.y);
    PushDirection.z = (float)((double)(Tangent.k[2]) * Sin2F + PushDirection.z);
}

// FUN_06346340: point speed of A minus point speed of B, a fixed core counting
// as 0. Only caller: FinalizeImpact.
static void CalcRelativePointVelocity(IVP_Core* CoreA, IVP_Core* CoreB,
                                       const Vector* ArmA, const Vector* ArmB,
                                       const Vector* LinVelA, const Vector* AngVelA,
                                       const Vector* LinVelB, const Vector* AngVelB,
                                       Vector* Out)
{
    Vector VelA;
    if ((CoreA->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        CoreA->calc_point_speed_ws(ArmA, LinVelA, AngVelA, &VelA);
    }
    else
    {
        VelA.x = 0.0f;
        VelA.y = 0.0f;
        VelA.z = 0.0f;
    }

    Vector VelB;
    if ((CoreB->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        CoreB->calc_point_speed_ws(ArmB, LinVelB, AngVelB, &VelB);
    }
    else
    {
        VelB.x = 0.0f;
        VelB.y = 0.0f;
        VelB.z = 0.0f;
    }

    Out->x = VelA.x - VelB.x;
    Out->y = VelA.y - VelB.y;
    Out->z = VelA.z - VelB.z;
}

// FUN_063582f0
void IVP_Impact_Solver::ClearPendingCoreChanges()
{
    Cores[0]->RotSpeedChange = Vector{0.0f, 0.0f, 0.0f};
    Cores[0]->SpeedChange    = Vector{0.0f, 0.0f, 0.0f};
    Cores[1]->RotSpeedChange = Vector{0.0f, 0.0f, 0.0f};
    Cores[1]->SpeedChange    = Vector{0.0f, 0.0f, 0.0f};
}

// FUN_063572b0. Past MaxCollisionsPerPsi (signed word compare) the anomaly
// manager's slot 3 result goes into MovementState bits 8..15.
void IVP_Impact_Solver::CommitCoreVelocityImmediate(int32_t CoreIndex)
{
    IVP_Core* Core = Cores[CoreIndex];
    Core->Speed = (CoreIndex == 0) ? Speed0 : Speed1;
    Core->RotSpeed = (CoreIndex == 0) ? RotSpeed0 : RotSpeed1;

    IVP_Environment* Env = Core->Environment;
    IVP_Anomaly_Limits* Limits = Env->anomaly_limits;

    if (static_cast<int32_t>(Core->PendingImpactCount) > Limits->MaxCollisionsPerPsi)
    {
        const int32_t bFreeze = Env->anomaly_manager->MaxCollisionsExceededCheckFreezing(Limits, Core);
        Core->MovementState = (Core->MovementState & 0xFFFF00FFu) |
                              ((static_cast<uint32_t>(bFreeze) & 0xFFu) << 8);
    }
}

// FUN_06358270
void IVP_Impact_Solver::CommitCoreVelocityDeferred(int32_t CoreIndex)
{
    IVP_Core* Core = Cores[CoreIndex];
    const Vector& WorkingSpeed = (CoreIndex == 0) ? Speed0 : Speed1;
    const Vector& WorkingRotSpeed = (CoreIndex == 0) ? RotSpeed0 : RotSpeed1;
    Core->SpeedChange.x = WorkingSpeed.x - Core->Speed.x;
    Core->SpeedChange.y = WorkingSpeed.y - Core->Speed.y;
    Core->SpeedChange.z = WorkingSpeed.z - Core->Speed.z;
    Core->RotSpeedChange.x = WorkingRotSpeed.x - Core->RotSpeed.x;
    Core->RotSpeedChange.y = WorkingRotSpeed.y - Core->RotSpeed.y;
    Core->RotSpeedChange.z = WorkingRotSpeed.z - Core->RotSpeed.z;
}

// FUN_06358070. TypeCode is the sign-extended Flags[3:2]. The winner is core 0
// only when EffectiveMass0 > EffectiveMass1 (ties and NaN pick core 1). The
// winner's commit is deferred only when the relative point speed along the
// normal (negated for winner 1) is below RescueSpeed * -0.8333333f
// (DAT_063a566c); otherwise both commit immediately.
void IVP_Impact_Solver::FinalizeImpact(IVP_Core* CoresOut[2])
{
    auto BumpPendingCount = [](IVP_Core* Core)
    {
        const int32_t TypeCode = static_cast<int32_t>(Core->Flags << 28) >> 30;
        Core->PendingImpactCount = static_cast<int16_t>(Core->PendingImpactCount + (1 - TypeCode));
    };
    BumpPendingCount(Cores[0]);
    BumpPendingCount(Cores[1]);

    auto CommitBothImmediately = [this]()
    {
        ClearPendingCoreChanges();
        CommitCoreVelocityImmediate(0);
        CommitCoreVelocityImmediate(1);
    };

    if (bAllowDeferredCommit == 0)
    {
        CommitBothImmediately();
        return;
    }

    const int32_t WinnerIdx = (EffectiveMass0 > EffectiveMass1) ? 0 : 1;
    const int32_t LoserIdx = 1 - WinnerIdx;

    if ((CoresOut[WinnerIdx]->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
    {
        CommitBothImmediately();
        return;
    }

    const Vector* LoserSpeed = (LoserIdx == 0) ? &Speed0 : &Speed1;
    const Vector* LoserRotSpeed = (LoserIdx == 0) ? &RotSpeed0 : &RotSpeed1;
    Vector RelVel;
    CalcRelativePointVelocity(Cores[WinnerIdx], Cores[LoserIdx],
                              ContactCoordsOS[WinnerIdx], ContactCoordsOS[LoserIdx],
                              &Cores[WinnerIdx]->Speed, &Cores[WinnerIdx]->RotSpeed,
                              LoserSpeed, LoserRotSpeed, &RelVel);

    const Vector* Normal = NormalVectorWS;
    double Dot = ((double)(RelVel.y) * Normal->y + (double)(RelVel.z) * Normal->z) +
                 (double)(RelVel.x) * Normal->x;
    if (WinnerIdx != 0)
    {
        Dot = Dot * -1.0; // DAT_063a5498
    }

    const double Threshold = (double)(RescueSpeed) * (double)(-0.8333333f); // DAT_063a566c
    if (!(Dot < Threshold))
    {
        CommitBothImmediately();
        return;
    }

    Cores[0]->Environment->impact_deferred_commit_count++; // always Cores[0]

    CoresOut[WinnerIdx] = nullptr;
    ClearPendingCoreChanges();
    CommitCoreVelocityImmediate(LoserIdx);
    CommitCoreVelocityDeferred(WinnerIdx);
    Cores[WinnerIdx]->PendingImpactCount = static_cast<int16_t>(Cores[WinnerIdx]->PendingImpactCount - 1);
}

// The velocity-sanity block retail inlines once per core in DoImpactSystem's
// tail, on the solver's working velocities. SpinClipping (+0x4C) clamps per
// axis; otherwise (unless AxisLock is set and MaxSurfaceDeviation is 0 or NaN) the
// anomaly manager's slot 1 limits the angular speed. Slot 0 then always limits
// the linear speed. Both limit calls also fire on unordered compares.
static void ClampResolvedCoreVelocities(IVP_Core* Core, Vector* WorkingRotSpeed, Vector* WorkingSpeed)
{
    IVP_Environment* Env = Core->Environment;
    IVP_Anomaly_Limits* Limits = Env->anomaly_limits;

    const Vector* PerAxisRotLimit = Core->SpinClipping;
    if (PerAxisRotLimit != nullptr)
    {
        if (WorkingRotSpeed->x > PerAxisRotLimit->x)
        {
            WorkingRotSpeed->x = PerAxisRotLimit->x;
        }
        else if (-PerAxisRotLimit->x > WorkingRotSpeed->x)
        {
            WorkingRotSpeed->x = -PerAxisRotLimit->x;
        }

        if (WorkingRotSpeed->y > PerAxisRotLimit->y)
        {
            WorkingRotSpeed->y = PerAxisRotLimit->y;
        }
        else if (-PerAxisRotLimit->y > WorkingRotSpeed->y)
        {
            WorkingRotSpeed->y = -PerAxisRotLimit->y;
        }

        if (WorkingRotSpeed->z > PerAxisRotLimit->z)
        {
            WorkingRotSpeed->z = PerAxisRotLimit->z;
        }
        else if (-PerAxisRotLimit->z > WorkingRotSpeed->z)
        {
            WorkingRotSpeed->z = -PerAxisRotLimit->z;
        }
    }
    else if (Core->AxisLock == nullptr || Core->MaxSurfaceDeviation < 0.0f || Core->MaxSurfaceDeviation > 0.0f)
    {
        const double MaxRotSpeed = (double)(Env->inv_delta_time) * Limits->MaxAngularVelocityPerPsi;
        const double RotX = WorkingRotSpeed->x;
        const double RotY = WorkingRotSpeed->y;
        const double RotZ = WorkingRotSpeed->z;
        const double RotMagSq = (RotZ * RotZ + RotY * RotY) + RotX * RotX;
        if (!(MaxRotSpeed * MaxRotSpeed >= RotMagSq))
        {
            Env->anomaly_manager->MaxAngularVelocityExceeded(Limits, Core, WorkingRotSpeed);
        }
    }

    const double MaxSpeed = Limits->MaxVelocity;
    const double SpeedX = WorkingSpeed->x;
    const double SpeedY = WorkingSpeed->y;
    const double SpeedZ = WorkingSpeed->z;
    const double SpeedMagSq = (SpeedX * SpeedX + SpeedY * SpeedY) + SpeedZ * SpeedZ; // FUN_062c4780
    if (!(MaxSpeed * MaxSpeed >= SpeedMagSq))
    {
        Env->anomaly_manager->MaxVelocityExceeded(Limits, Core, WorkingSpeed);
    }
}

// FUN_063578a0. Only caller: do_impact_long_term FUN_06358d60. DAT_0658ab38 is
// min_vertical_speed_at_collision, DAT_063a5668 = 1.2f, DAT_063a5464 = 0.1f,
// DAT_063a5664 = -1e-4f, DAT_063a5404 = 0.01f, DAT_063a547c = 1e-4f.
void IVP_Impact_Solver::DoImpactSystem(IVP_Core* CoresOut[2], int32_t bAllowDeferred, int32_t ImpactCounter,
                                       float RescueSpeedAddon)
{
    // ---- 1. prologue -------------------------------------------------------
    const double ImpactCounterExt = (double)(ImpactCounter);
    const float ImpactCounterF = (float)(ImpactCounterExt);
    const float RestitutionFactor = (float)(
        (double)(1.0f) -
        ((double)(1.0f) - (double)(Restitution)) *
            ((double)(1.0f) / (ImpactCounterExt * (double)(0.5f) + (double)(1.0f))));

    bAllowDeferredCommit = bAllowDeferred;
    RescueSpeed = (float)(((double)(G_IVP_MindistSettings.min_vertical_speed_at_collision) + RescueSpeedAddon) *
                                     (double)(1.2f));

    CoresOut[0] = Cores[0];
    CoresOut[1] = Cores[1];

    Cores[0]->Environment->impact_solver_count++;

    MWorldFCore[0] = &Cores[0]->MWorldFCore;
    MWorldFCore[1] = &Cores[1]->MWorldFCore;

    RotSpeed0.x = Cores[0]->RotSpeedChange.x + Cores[0]->RotSpeed.x;
    RotSpeed0.y = Cores[0]->RotSpeed.y + Cores[0]->RotSpeedChange.y;
    RotSpeed0.z = Cores[0]->RotSpeed.z + Cores[0]->RotSpeedChange.z;
    Speed0.x = Cores[0]->Speed.x + Cores[0]->SpeedChange.x;
    Speed0.y = Cores[0]->Speed.y + Cores[0]->SpeedChange.y;
    Speed0.z = Cores[0]->Speed.z + Cores[0]->SpeedChange.z;
    RotSpeed1.x = Cores[1]->RotSpeed.x + Cores[1]->RotSpeedChange.x;
    RotSpeed1.y = Cores[1]->RotSpeed.y + Cores[1]->RotSpeedChange.y;
    RotSpeed1.z = Cores[1]->RotSpeed.z + Cores[1]->RotSpeedChange.z;
    Speed1.x = Cores[1]->Speed.x + Cores[1]->SpeedChange.x;
    Speed1.y = Cores[1]->Speed.y + Cores[1]->SpeedChange.y;
    Speed1.z = Cores[1]->Speed.z + Cores[1]->SpeedChange.z;

    // A dword zero at +0x18; nothing in retail reads +0x10..+0x1B.
    Unread_0x10[2] = 0;

    // ---- 2. initial relative contact velocity ------------------------------
    UpdateRelativeSpeed();
    *FrictionVectorWS = RelativePointSpeed;

    int32_t Iter = 0;
    const float ImpulseStep = (float)(CalcImpulseNeeded2() * (double)(0.1f));

    // ---- 3. already-separating contact -------------------------------------
    if (IVP_ImpactSolver_DotZYX(NormalVectorWS, &RelativePointSpeed) > (double)(-0.0001f))
    {
        Vector NegNormal;
        NegNormal.x = -NormalVectorWS->x;
        NegNormal.y = -NormalVectorWS->y;
        NegNormal.z = -NormalVectorWS->z;
        PushContactPoint(&NegNormal, 1);
        if (ImpactCounter > 10)
        {
            bAllowDeferredCommit = 0;
        }
    }
    else
    {
        // ---- 4. restitution solve ------------------------------------------
        UpdateFrictionPushDirection();
        FrictionImpulse = Vector{0.0f, 0.0f, 0.0f};

        float ClosingSpeed = (float)(-IVP_ImpactSolver_DotZYX(NormalVectorWS, &RelativePointSpeed));
        const float TargetSpeed = (float)(
            std::sqrt((double)(RestitutionFactor)) * ClosingSpeed +
            std::sqrt((double)(ImpactCounterF)) * (double)(0.01f));

        while (ClosingSpeed > 0.0f)
        {
            if (Iter >= 100)
            {
                break;
            }
            ++Iter;
            StepImpulse2(ImpulseStep);
            UpdateRelativeSpeed();
            ClosingSpeed = (float)(-IVP_ImpactSolver_DotZYX(NormalVectorWS, &RelativePointSpeed));
            UpdateFrictionPushDirection();
        }

        // The sign test uses the unrounded sum.
        const double TotalSpeedExt = (double)(ClosingSpeed) + TargetSpeed;
        const float TotalSpeed = (float)(TotalSpeedExt);

        PushDirection.x = -NormalVectorWS->x;
        PushDirection.y = -NormalVectorWS->y;
        PushDirection.z = -NormalVectorWS->z;

        // Unit probe, then rescale the step to hit the target speed.
        if (TotalSpeedExt > 0.0 && Iter != 100)
        {
            StepImpulse2(1.0f);
            UpdateRelativeSpeed();

            const double SpeedPerUnitImpulse =
                IVP_ImpactSolver_DotZYX(NormalVectorWS, &RelativePointSpeed) + (double)(ClosingSpeed);
            float Scale;
            if (std::fabs(SpeedPerUnitImpulse) > (double)(0.0001f))
            {
                Scale = (float)((double)(TotalSpeed) / SpeedPerUnitImpulse);
            }
            else
            {
                Scale = 0.0f;
            }

            UndoLastStepImpulse();
            StepImpulse2(Scale);
        }

        PushContactPoint(&PushDirection, 0);
    }

    // ---- 5. tail -----------------------------------------------------------
    ClampResolvedCoreVelocities(Cores[0], &RotSpeed0, &Speed0);
    ClampResolvedCoreVelocities(Cores[1], &RotSpeed1, &Speed1);

    FinalizeImpact(CoresOut);

    if ((((Cores[0]->MovementState | Cores[1]->MovementState) >> 8) & 0xFFu) == 0)
    {
        return;
    }

    Cores[0]->Environment->impact_flagged_core_count++;

    for (int32_t Idx = 0; Idx < 2; ++Idx)
    {
        IVP_Core* Core = Cores[Idx];

        // Retail: x ^ ((x ^ (0x100 - (TypeCode << 8))) & 0xff00).
        const int32_t TypeCode = static_cast<int32_t>(Core->Flags << 28) >> 30;
        const uint32_t NewBits = static_cast<uint32_t>(0x100 - (TypeCode << 8)) & 0xFF00u;
        Core->MovementState = (Core->MovementState & 0xFFFF00FFu) | NewBits;

        Core->SpeedChange.x = Core->SpeedChange.x + Core->Speed.x;
        Core->SpeedChange.y = Core->SpeedChange.y + Core->Speed.y;
        Core->SpeedChange.z = Core->SpeedChange.z + Core->Speed.z;
        Core->RotSpeedChange.x = Core->RotSpeedChange.x + Core->RotSpeed.x;
        Core->RotSpeedChange.y = Core->RotSpeed.y + Core->RotSpeedChange.y;
        Core->RotSpeedChange.z = Core->RotSpeed.z + Core->RotSpeedChange.z;
        Core->Speed = Vector{0.0f, 0.0f, 0.0f};
        Core->RotSpeed = Vector{0.0f, 0.0f, 0.0f};

        CoresOut[Idx] = Core;
    }
}
