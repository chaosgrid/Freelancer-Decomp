// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_calc_next_psi_solver.h"
#include "ivp_environment.h"
#include "ivp_real_object.h"
#include "ivp_controller.h"
#include "ivp_anomaly_manager.h"
#include "ivp_ov_element.h"

#include "x87_trig.h"

#include <cmath>
#include <cstdint>
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte

// x87 convention for this file (/arch:IA32): every value retail keeps on the FPU stack is a
// `double` here, a `float` only where retail stores a dword. Compares are written so the
// unordered (NaN) case falls on retail's side: (A > B), !(A >= B), or equal-or-unordered.

namespace
{
    // .rdata constants.
    const float GIvpOne = 1.0f;                          // DAT_063a53d0 (0x3f800000)
    const float GIvpZero = 0.0f;                         // DAT_063a53d4 (0x00000000)
    const float GIvpEpsilon = 0x1.b7cdfep-34f;           // DAT_063a53d8 (0x2edbe6ff, 1e-10f)
    const float GIvpHullResetInterval = 10.0f;           // DAT_063a5400 (0x41200000)
    const float GIvpHalf = 0.5f;                         // DAT_063a5460 (0x3f000000)
    const float GIvpOneSixth = 0x1.555556p-3f;           // DAT_063a5624 (0x3e2aaaab)
    const float GIvpHullGradientScale = 0x1.0000a8p+0f;  // DAT_063a5638 (0x3f800054, 1.00001f)
    const float GIvpRotStepCountScale = 0x1.1ffffep+7f;  // DAT_063a563c (0x430fffff, 143.99998f)
    const float GIvpRotStepThreshold = 0x1.c71c74p-6f;   // DAT_063a5640 (0x3ce38e3a, 1/36)
    const float GIvpAsinFifthOrder = 0x1.9dd6e0p-2f;     // DAT_063a5644 (0x3eceeb70, 0.40414f)

    bool IsEqualOrUnordered(float A, float B)
    {
        return !(A < B) && !(A > B);
    }

    // FUN_062c4780: x*x + y*y + z*z, result left on the FPU stack.
    double QuadLength(const Vector& V)
    {
        const double X = V.x;
        const double Y = V.y;
        const double Z = V.z;
        return (X * X + Y * Y) + Z * Z;
    }

    // FUN_06347e60: FSQRT of the same sum, result left on the FPU stack.
    double RealLength(const Vector& V)
    {
        return std::sqrt(QuadLength(V));
    }

    // FUN_0628be60 (thiscall, RET 0xc): this = A + B * Scale, each component rounded on its
    // own store.
    void AddMultiple(Vector* Out, const Vector* A, const Vector* B, float Scale)
    {
        const float Y = (float)((double)(Scale) * B->y + A->y);
        const float Z = (float)((double)(Scale) * B->z + A->z);
        const float X = (float)((double)(Scale) * B->x + A->x);
        Out->x = X;
        Out->y = Y;
        Out->z = Z;
    }

    // The hull check retail inlines three times into FUN_06351f80 and once into
    // FUN_06351b90 (:06351e02): fire the nearest synapse while the hull value has passed it
    // (at most 102 calls: the counter starts at 100 and is tested after each call), then
    // rebase the manager once its reset time (+0x1c, an int) is behind its edit time.
    void CheckHullSynapsesAndReset(IVP_Hull_Manager* Hull)
    {
        double Delta = (double)(Hull->PoolKeyThreshold) - Hull->AccumulatedDeltaA;
        float DeltaStored = (float)(Delta);
        int32_t LoopsLeft = 100;
        if (!(Delta >= GIvpZero))
        {
            for (;;)
            {
                // The payload of the minimum entry.
                IVP_Listener_Hull* Listener = (IVP_Listener_Hull*)Hull->PoolEntries[Hull->PoolLiveIndex].Payload;
                Listener->hull_limit_exceeded_event(Hull, DeltaStored);
                const int32_t Previous = LoopsLeft;
                --LoopsLeft;
                if (Previous < 0)
                {
                    break;
                }
                Delta = (double)(Hull->PoolKeyThreshold) - Hull->AccumulatedDeltaA;
                DeltaStored = (float)(Delta);
                if (Delta >= GIvpZero)
                {
                    break;
                }
            }
        }

        // Reset when the int reset time is below the edit time (or unordered).
        if (!((double)Hull->TimeOfNextReset >= Hull->LastEditTimeHi))
        {
            Hull->DrainAndShift(); // FUN_06352220
            // ftol truncation.
            Hull->TimeOfNextReset = (int32_t)((double)Hull->LastEditTimeHi + GIvpHullResetInterval);
        }
    }
}

// ============================================================================
// FUN_06351310 (RET 8).
// ============================================================================
void IVP_Calc_Next_PSI_Solver::calc_next_PSI_matrix(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Hull_Manager>* ActiveHullManagers)
{
    IVP_Core* Core = core;

    // A car wheel with max_surface_deviation == 0 (or NaN) skips every speed limit.
    if (!(Core->AxisLock != nullptr && IsEqualOrUnordered(Core->SpeedLimit, GIvpZero)))
    {
        IVP_Anomaly_Limits* Limits = Core->Environment->anomaly_limits;
        Vector& RotSpeed = Core->RotSpeed;

        if (Core->SpinClipping != nullptr)
        {
            // Clamp each axis into [-clip, clip]: `rot > clip` copies the clip; otherwise
            // `-clip > rot` stores -clip.
            const Vector* Clip = Core->SpinClipping;
            if (RotSpeed.x > Clip->x)
            {
                RotSpeed.x = Clip->x;
            }
            else if (-Clip->x > RotSpeed.x)
            {
                RotSpeed.x = -Clip->x;
            }

            if (RotSpeed.y > Clip->y)
            {
                RotSpeed.y = Clip->y;
            }
            else if (-Clip->y > RotSpeed.y)
            {
                RotSpeed.y = -Clip->y;
            }

            if (RotSpeed.z > Clip->z)
            {
                RotSpeed.z = Clip->z;
            }
            else if (-Clip->z > RotSpeed.z)
            {
                RotSpeed.z = -Clip->z;
            }
        }
        else if (!(Core->AxisLock != nullptr && IsEqualOrUnordered(Core->SpeedLimit, GIvpZero)))
        {
            // Retail re-tests the outer condition (always true here; kept as written).
            // Fire when |rot_speed|^2 > m*m, m = inv_delta_time * max_angular_velocity_per_psi.
            const double QuadRot = QuadLength(RotSpeed);
            IVP_Environment* Env = Core->Environment;
            const double MaxRot = (double)(Env->inv_delta_time) * Limits->MaxAngularVelocityPerPsi;
            if (QuadRot > MaxRot * MaxRot)
            {
                Env->anomaly_manager->MaxAngularVelocityExceeded(Limits, Core, &RotSpeed);
            }
        }

        // Fire when max*max < |speed|^2 (or unordered).
        const float MaxVelocity = Limits->MaxVelocity;
        const double QuadSpeed = QuadLength(Core->Speed);
        if (!((double)(MaxVelocity) * MaxVelocity >= QuadSpeed))
        {
            Core->Environment->anomaly_manager->MaxVelocityExceeded(Limits, Core, &Core->Speed);
        }
    }

    // current_speed, i_delta_time.
    Core->Margin = (float)(RealLength(Core->Speed));
    Core->IDeltaTime = EventSim->i_delta_time;

    IVP_U_Quat QCoreFCore;
    calc_rotation_matrix(EventSim->delta_time, &QCoreFCore);

    // Time since the last PSI, then time_of_last_psi = current_time.
    IVP_Environment* Env = Core->Environment;
    const float CurrentTimeHi = Env->current_time.time_base;
    const float CurrentTimeLo = Env->current_time.time_offset;
    const double SinceLastPsi = (((double)(CurrentTimeHi) - Core->TimeOfLastPsi.time_base) + CurrentTimeLo) -
                                Core->TimeOfLastPsi.time_offset;
    Core->TimeOfLastPsi.time_base = CurrentTimeHi;
    Core->TimeOfLastPsi.time_offset = CurrentTimeLo;
    const float SinceLastPsiStored = (float)(SinceLastPsi);

    // pos_world_f_core_last_psi += delta_world_f_core_psis * dt.
    AddMultiple(&Core->PosWorldFCore, &Core->PosWorldFCore, &Core->DeltaWorldFCore, SinceLastPsiStored);

    // delta_world_f_core_psis = speed (dword copies).
    Core->DeltaWorldFCore.z = Core->Speed.z;
    Core->DeltaWorldFCore.x = Core->Speed.x;
    Core->DeltaWorldFCore.y = Core->Speed.y;

    // q_world_f_core_last_psi = q_world_f_core_next_psi.
    Core->QWorldFCore = Core->QWorldFCoreNext;

    // q_next = q_next * q_core_f_core (Hamilton product, each component in retail's FPU
    // order; z and w go through a dword store first, x and y are stored off the stack).
    {
        const double Ax = Core->QWorldFCoreNext.x;
        const double Ay = Core->QWorldFCoreNext.y;
        const double Az = Core->QWorldFCoreNext.z;
        const double Aw = Core->QWorldFCoreNext.w;
        const double Lx = QCoreFCore.x;
        const double Ly = QCoreFCore.y;
        const double Lz = QCoreFCore.z;
        const double Lw = QCoreFCore.w;

        const double NewY = ((Ly * Aw + Lw * Ay) + Lx * Az) - Lz * Ax;
        const float NewZ = (float)(((Lz * Aw + Ly * Ax) + Lw * Az) - Lx * Ay);
        const float NewW = (float)(((Lw * Aw - Lx * Ax) - Ly * Ay) - Lz * Az);
        const double NewX = ((Lz * Ay + Lx * Aw) + Lw * Ax) - Ly * Az;

        Core->QWorldFCoreNext.w = NewW;
        Core->QWorldFCoreNext.z = NewZ;
        Core->QWorldFCoreNext.x = (float)(NewX);
        Core->QWorldFCoreNext.y = (float)(NewY);
    }

    Core->QWorldFCoreNext.fast_normize(); // FUN_06351010

    calc_psi_rotation_axis(&QCoreFCore);

    // (max_surface_rot_speed + current_speed) * 1.00001f becomes every object's hull
    // gradient. The sum is formed before the empty-objects test and dropped there.
    const double SurfaceSpeed = (double)(Core->BoundingRadius) + Core->Margin;
    const int32_t LastObject = static_cast<int32_t>(Core->Objects.n_elems) - 1;
    if (LastObject < 0)
    {
        return;
    }
    const float HullGradient = (float)(SurfaceSpeed * GIvpHullGradientScale);

    for (int32_t Index = LastObject; Index >= 0; --Index)
    {
        IVP_Hull_Manager* Hull = &Core->Objects.elems[Index]->hull_manager; // object + 0x44
        const double CurrentSpeed = Core->Margin;                            // core + 0x144
        IVP_Environment* EventEnv = EventSim->environment;
        const float TimeHi = EventEnv->current_time.time_base;
        const float TimeLo = EventEnv->current_time.time_offset;
        const float DeltaTime = EventSim->delta_time;

        // ((now_hi - last_hi) + now_lo) - last_lo, then last_vpsi_time = now.
        const double SinceLastEdit = (((double)(TimeHi) - Hull->LastEditTimeHi) + TimeLo) - Hull->LastEditTimeLo;
        Hull->LastEditTimeHi = TimeHi;
        Hull->LastEditTimeLo = TimeLo;

        // Both values advance with the OLD gradients (each multiply reads its gradient
        // before the store that replaces it).
        const double OldOuterGradient = Hull->OuterGradient;
        Hull->OuterGradient = HullGradient;
        Hull->OuterValueAtLastEdit = (float)(SinceLastEdit * OldOuterGradient + Hull->OuterValueAtLastEdit);
        Hull->InnerValueAtLastEdit = (float)(SinceLastEdit * Hull->InnerGradient + Hull->InnerValueAtLastEdit);
        Hull->InnerGradient = (float)(CurrentSpeed);

        // hull_value_next_psi = gradient * delta_time + hull_value_last_vpsi (stored as a
        // float, the unrounded value is compared).
        const double NextValue = (double)(HullGradient) * DeltaTime + Hull->OuterValueAtLastEdit;
        Hull->AccumulatedDeltaA = (float)(NextValue);

        if (!((double)(Hull->PoolKeyThreshold) - NextValue >= GIvpZero))
        {
            ActiveHullManagers->Add(Hull); // FUN_06352140 + inline append
        }
    }
}

// ============================================================================
// FUN_06351700 (RET 8).
// ============================================================================
void IVP_Calc_Next_PSI_Solver::calc_rotation_matrix(float DeltaTime, IVP_U_Quat* QCoreFCore)
{
    IVP_Core* Core = core;

    if ((Core->Flags & 0xC0u) == 0 && Core->Environment->sim_substate != 5)
    {
        Vector& RotSpeed = Core->RotSpeed;
        const IVP_U_Point* RotSpeedPoint = reinterpret_cast<const IVP_U_Point*>(&RotSpeed);

        const double QuadRotStep = QuadLength(RotSpeed) * DeltaTime * DeltaTime;
        float SubDeltaTime = DeltaTime; // local_38
        int32_t StepCount = 1;          // local_34

        // Sub-step when |rot|^2 * dt^2 > 1/36: n = ftol(sqrt(q * 143.99998f)) + 1.
        if (QuadRotStep > GIvpRotStepThreshold)
        {
            StepCount = static_cast<int32_t>(std::sqrt(QuadRotStep * GIvpRotStepCountScale)) + 1;
            SubDeltaTime = (float)((double)(DeltaTime) / (double)(StepCount));
        }

        QCoreFCore->set_fast_rot_vector(RotSpeedPoint, SubDeltaTime); // FUN_06350aa0

        const float CoeffX = (float)(((double)(Core->RotInertia.y) - Core->RotInertia.z) * Core->InvRotInertia.x);
        const float CoeffY = (float)(((double)(Core->RotInertia.z) - Core->RotInertia.x) * Core->InvRotInertia.y);
        const float CoeffZ = (float)(((double)(Core->RotInertia.x) - Core->RotInertia.y) * Core->InvRotInertia.z);

        int32_t Step = 1;
        for (;;)
        {
            // Torque-free precession; DX stays on the stack, DY / DZ go through dword locals.
            const double DX = (double)(CoeffX) * RotSpeed.y * RotSpeed.z;
            const float DY = (float)((double)(CoeffY) * RotSpeed.x * RotSpeed.z);
            const float DZ = (float)((double)(RotSpeed.y) * RotSpeed.x * CoeffZ);
            RotSpeed.x = (float)(DX * SubDeltaTime + RotSpeed.x);
            RotSpeed.y = (float)((double)(DY) * SubDeltaTime + RotSpeed.y);
            RotSpeed.z = (float)((double)(DZ) * SubDeltaTime + RotSpeed.z);

            if (Step >= StepCount)
            {
                return;
            }

            IVP_U_Quat Scratch;
            Scratch.set_fast_rot_vector(RotSpeedPoint, SubDeltaTime);
            ++Step;

            // QCoreFCore = Scratch * QCoreFCore.
            const double Sx = Scratch.x;
            const double Sy = Scratch.y;
            const double Sz = Scratch.z;
            const double Sw = Scratch.w;
            const double Ox = QCoreFCore->x;
            const double Oy = QCoreFCore->y;
            const double Oz = QCoreFCore->z;
            const double Ow = QCoreFCore->w;

            const double NewY = ((Sy * Ow + Sw * Oy) + Sz * Ox) - Sx * Oz;
            const float NewZ = (float)(((Sz * Ow + Sx * Oy) + Sw * Oz) - Sy * Ox);
            const float NewW = (float)(((Sw * Ow - Sx * Ox) - Sy * Oy) - Sz * Oz);
            const double NewX = ((Sx * Ow + Sw * Ox) + Sy * Oz) - Sz * Oy;

            QCoreFCore->w = NewW;
            QCoreFCore->z = NewZ;
            QCoreFCore->x = (float)(NewX);
            QCoreFCore->y = (float)(NewY);
        }
    }

    // Car wheel with max_surface_deviation == 0 (or NaN).
    if (Core->AxisLock != nullptr && IsEqualOrUnordered(Core->SpeedLimit, GIvpZero))
    {
        // The wheel's axis index (0..2).
        const int32_t Axis = *reinterpret_cast<const int32_t*>(
            static_cast<const char*>(Core->AxisLock->Owner) + IVP_Core_AxisLock::OwnerAxisIndexOffset);
        const float* RotSpeedArray = &Core->RotSpeed.x;
        const float AxisSpeed = RotSpeedArray[Axis]; // local_34

        // Copy of the four dwords at +0x94 with the wheel axis zeroed.
        IVP_U_Point Other;
        Other.k[0] = Core->RotSpeed.x;
        Other.k[1] = Core->RotSpeed.y;
        Other.k[2] = Core->RotSpeed.z;
        Other.k[3] = Core->PadA0;
        Other.k[Axis] = 0.0f;

        IVP_U_Quat Rotation;
        Rotation.set_rot_vector(&Other, DeltaTime); // FUN_06350b20

        // The wheel quaternion: sin(axis_speed * dt * 0.5) on the axis
        // (stored dword), cos = sqrt(1 - s*s) from the unrounded sine.
        IVP_U_Point Wheel;
        Wheel.k[0] = 0.0f;
        Wheel.k[1] = 0.0f;
        Wheel.k[2] = 0.0f;
        Wheel.k[3] = Other.k[3];
        // x87 FSIN: the axis gets one float rounding of the sine, the cosine uses the
        // unrounded value.
        const double SineArg = (double)(AxisSpeed) * DeltaTime * GIvpHalf;
        const double Sine = X87Sin(SineArg);
        Wheel.k[Axis] = X87SinF(SineArg);
        const double Cosine = std::sqrt(GIvpOne - Sine * Sine);

        // QCoreFCore = Rotation * {Wheel.xyz, Cosine}.
        const double Rx = Rotation.x;
        const double Ry = Rotation.y;
        const double Rz = Rotation.z;
        const double Rw = Rotation.w;
        const double Px = Wheel.k[0];
        const double Py = Wheel.k[1];
        const double Pz = Wheel.k[2];

        QCoreFCore->x = (float)(((Ry * Pz + Rw * Px) + Rx * Cosine) - Rz * Py);
        QCoreFCore->y = (float)(((Ry * Cosine + Rw * Py) + Rz * Px) - Rx * Pz);
        QCoreFCore->z = (float)(((Rz * Cosine + Rw * Pz) + Rx * Py) - Ry * Px);
        QCoreFCore->w = (float)(((Rw * Cosine - Rx * Px) - Ry * Py) - Rz * Pz);
        return;
    }

    QCoreFCore->set_rot_vector(reinterpret_cast<const IVP_U_Point*>(&Core->RotSpeed), DeltaTime); // FUN_06350b20
}

// ============================================================================
// FUN_06351a50 (RET 4).
// ============================================================================
void IVP_Calc_Next_PSI_Solver::calc_psi_rotation_axis(const IVP_U_Quat* QCoreFCore)
{
    IVP_Core* Core = core;

    // |q.xyz|^2, stored as a float.
    const double Qx = QCoreFCore->x;
    const double Qy = QCoreFCore->y;
    const double Qz = QCoreFCore->z;
    const float QuadLen = (float)((Qx * Qx + Qy * Qy) + Qz * Qz);

    // The general case only when QuadLen > 1e-10.
    if (QuadLen > GIvpEpsilon)
    {
        // Inverse length and length stay on the stack; abs_omega =
        // 2 * (len + len^3/6 + len^5 * 0.40414f) * i_delta_time.
        const double InvLength = GIvpOne / std::sqrt((double)(QuadLen));
        const double Length = InvLength * QuadLen;
        const double Length2 = Length * Length;
        const double Length3 = Length2 * Length;
        const double Length5 = Length3 * Length2;
        const double Series = Length5 * GIvpAsinFifthOrder + Length3 * GIvpOneSixth;
        const double AbsOmega = (Length + Series) * Core->IDeltaTime;
        Core->MaxSurfaceDeviation = (float)(AbsOmega + AbsOmega);

        // Rotation axis = m_world_f_core rows . q.xyz / |q.xyz|;
        // row 0 stays on the stack, rows 1 and 2 go through dword locals.
        const IVP_U_Matrix& M = Core->MWorldFCore;
        const double Row0 = ((double)(M.rows[0].k[2]) * Qz + (double)(M.rows[0].k[1]) * Qy) +
                            (double)(M.rows[0].k[0]) * Qx;
        const float Row1 = (float)(((double)(M.rows[1].k[0]) * Qx + (double)(M.rows[1].k[2]) * Qz) +
                                              (double)(M.rows[1].k[1]) * Qy);
        const float Row2 = (float)(((double)(M.rows[2].k[0]) * Qx + (double)(M.rows[2].k[2]) * Qz) +
                                              (double)(M.rows[2].k[1]) * Qy);
        Core->RotAxisOS.x = (float)(Row0 * InvLength);
        Core->RotAxisOS.y = (float)(Row1 * InvLength);
        Core->RotAxisOS.z = (float)(Row2 * InvLength);
    }
    else
    {
        Core->MaxSurfaceDeviation = 0.0f;
        Core->RotAxisOS.x = 1.0f;
        Core->RotAxisOS.y = 0.0f;
        Core->RotAxisOS.z = 0.0f;
    }

    // max_surface_rot_speed = abs_omega * max_surface_deviation.
    Core->BoundingRadius = (float)((double)(Core->MaxSurfaceDeviation) * Core->SpeedLimit);
}

// ============================================================================
// FUN_06351b90 (RET 0xc).
// ============================================================================
void IVP_Calc_Next_PSI_Solver::set_transformation(const IVP_U_Quat* Rotation, const IVP_U_Point* Position, int bOptimizeForRepeatedCalls)
{
    IVP_Core* Core = core;
    IVP_Environment* Env = Core->Environment;

    // Re-stamp the environment's current time with itself.
    const float CurrentTimeHi = Env->current_time.time_base;
    const float CurrentTimeLo = Env->current_time.time_offset;
    IVP_Environment_SetCurrentTime(Env, CurrentTimeHi, CurrentTimeLo); // FUN_0634ccd0

    // Time since the last PSI, clamped up to delta_PSI_time (when delta_time > dt).
    float SinceLastPsi = (float)((((double)(CurrentTimeHi) - Core->TimeOfLastPsi.time_base) + CurrentTimeLo) -
                                            Core->TimeOfLastPsi.time_offset);
    if (Env->delta_time > SinceLastPsi)
    {
        SinceLastPsi = Env->delta_time;
        Core->IDeltaTime = Env->inv_delta_time;
    }
    else
    {
        Core->IDeltaTime = (float)((double)(GIvpOne) / SinceLastPsi);
    }

    Core->TimeOfLastPsi.time_base = CurrentTimeHi;
    Core->TimeOfLastPsi.time_offset = CurrentTimeLo;

    // q_next = Rotation; delta = inv(q_last) * q_next; q_last = Rotation.
    Core->QWorldFCoreNext = *Rotation;
    IVP_U_Quat QDelta;
    QDelta.set_inv_mult_quat(&Core->QWorldFCore, &Core->QWorldFCoreNext); // FUN_06351250
    Core->QWorldFCore = *Rotation;

    Core->TimeOfLastPsi.time_base = Env->time_of_last_psi.time_base;
    Core->TimeOfLastPsi.time_offset = Env->time_of_last_psi.time_offset;

    // Distance moved.
    Vector Moved;
    Moved.z = (float)((double)(Position->k[2]) - Core->PosWorldFCore.z);
    Moved.x = (float)((double)(Position->k[0]) - Core->PosWorldFCore.x);
    Moved.y = (float)((double)(Position->k[1]) - Core->PosWorldFCore.y);
    const float Distance = (float)(RealLength(Moved));

    Core->PosWorldFCore.x = Position->k[0];
    Core->PosWorldFCore.z = Position->k[2];
    Core->PosWorldFCore.y = Position->k[1];
    Core->DeltaWorldFCore.x = 0.0f;
    Core->DeltaWorldFCore.y = 0.0f;
    Core->DeltaWorldFCore.z = 0.0f;
    Core->Speed.x = 0.0f;
    Core->Speed.y = 0.0f;
    Core->Speed.z = 0.0f;

    // m_world_f_core = (Rotation, Position).
    Core->MWorldFCore.vv.k[0] = Position->k[0];
    Core->MWorldFCore.vv.k[2] = Position->k[2];
    Core->MWorldFCore.vv.k[1] = Position->k[1];
    Rotation->set_matrix(&Core->MWorldFCore); // FUN_06350bc0

    if (bOptimizeForRepeatedCalls != 0)
    {
        Core->Margin = (float)((double)(Distance) * Core->IDeltaTime);
    }
    else
    {
        Core->Margin = 0.0f;
        QDelta.x = 0.0f;
        QDelta.y = 0.0f;
        QDelta.z = 0.0f;
        QDelta.w = 1.0f;
    }
    calc_psi_rotation_axis(&QDelta);

    // growth = max_surface_rot_speed * max_surface_deviation * dt + distance.
    const float Growth = (float)((double)(Core->BoundingRadius) * Core->SpeedLimit * SinceLastPsi + Distance);
    for (int32_t Index = static_cast<int32_t>(Core->Objects.n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Real_Object* Object = Core->Objects.elems[Index];
        IVP_Hull_Manager* Hull = &Object->hull_manager;
        const float Value = (float)((double)(Growth) + Hull->AccumulatedDeltaA);
        Hull->OuterGradient = 0.0f;
        Hull->AccumulatedDeltaA = Value;
        Hull->InnerGradient = 0.0f;
        Hull->OuterValueAtLastEdit = Value;
        Hull->InnerValueAtLastEdit = (float)((double)(Distance) + Hull->InnerValueAtLastEdit);

        // An object whose movement state byte is >= 8.
        if (IVP_SignedLowByte(Object->flags) >= 8)
        {
            IVP_Real_Object_Free_Cache_Object(Object); // FUN_0634d510 (cdecl)
            Object->recalc_exact_mindists();           // FUN_063454b0
            Object->recalc_invalid_mindists();         // FUN_06345460
        }

        if (bOptimizeForRepeatedCalls == 0)
        {
            Env->mindist_manager->RecheckOvElement(Object); // FUN_0634ebd0
        }

        CheckHullSynapsesAndReset(Hull);
    }

    // Reached on both paths.
    Core->Margin = 0.0f;
    Core->MaxSurfaceDeviation = 0.0f;
    Core->BoundingRadius = 0.0f;
}

// ============================================================================
// FUN_06351eb0 (cdecl).
// ============================================================================
void IVP_Calc_Next_PSI_Solver::commit_all_calc_next_PSI_matrix(IVP_Environment* Environment,
                                                               IVP_U_Vector<IVP_Core>* CoresWhichNeedCalcNextPsi,
                                                               IVP_U_Vector<IVP_Hull_Manager>* ActiveHullManagers)
{
    // IVP_Event_Sim(env, env->delta_PSI_time). Retail leaves the fourth dword (sim_unit)
    // as stack garbage; nothing below reads it.
    IVP_Event_Sim EventSim;
    EventSim.environment = Environment;
    const double DeltaTime = Environment->delta_time;
    // Own reciprocal only when delta_time > 1e-10.
    if (DeltaTime > GIvpEpsilon)
    {
        EventSim.delta_time = (float)(DeltaTime);
        EventSim.i_delta_time = (float)(GIvpOne / DeltaTime);
    }
    else
    {
        EventSim.delta_time = Environment->delta_time;
        EventSim.i_delta_time = Environment->inv_delta_time;
    }

    // Last element first (retail unrolls the last two iterations).
    for (int32_t Index = static_cast<int32_t>(CoresWhichNeedCalcNextPsi->n_elems) - 1; Index >= 0; --Index)
    {
        IVP_Calc_Next_PSI_Solver Solver(CoresWhichNeedCalcNextPsi->elems[Index]);
        Solver.calc_next_PSI_matrix(&EventSim, ActiveHullManagers); // FUN_06351310
    }
}

// ============================================================================
// FUN_06351f80 (cdecl).
// ============================================================================
void IVP_Calc_Next_PSI_Solver::commit_all_hull_managers(IVP_Environment* /*Environment*/,
                                                        IVP_U_Vector<IVP_Hull_Manager>* ActiveHullManagers)
{
    // n_elems is read once; elems is re-read per element.
    for (int32_t Index = static_cast<int32_t>(ActiveHullManagers->n_elems) - 1; Index >= 0; --Index)
    {
        CheckHullSynapsesAndReset(ActiveHullManagers->elems[Index]);
    }
}
