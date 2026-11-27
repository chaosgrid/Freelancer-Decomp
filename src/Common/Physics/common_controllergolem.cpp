// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"

using namespace CmnPhySys;

// ============================================================================
// FLControllerGolem -- the force controller at CObject+0x60
// ============================================================================
//
// The class name is from RTTI: vftable .rdata:0639c1c8, COL 063a70e0, type descriptor
// .data:063eb0f8 ".?AVFLControllerGolem@@".
//
// VTABLE (8 slots, .rdata:0639c1c8). The indices follow PhySys::Controller's vftable
// (.rdata:0639b60c), whose exported get_controller_priority sits at +0x14, slot 5:
//   [0] +0x00  core_is_going_to_be_deleted_event -- LAB_06342fc0, delete-self through slot 6.
//   [1] +0x04  LAB_0627cd20, inherited from PhySys::Controller (loads a fixed .rdata float).
//              Not overridden, not ported.
//   [2] +0x08  get_associated_controlled_cores -- LAB_06285a60, the global vector DAT_0658a9e8
//              (IVP_Controller_Independent's).
//   [3] +0x0c  LAB_063435f0, IVP_Controller_Golem::reset_time (rebases +0x64/+0x68).
//   [4] +0x10  do_simulation_controller -- LAB_06343150, the per-tick force/torque integrator.
//   [5] +0x14  get_controller_priority -- LAB_06285a70, 500 (IVP_Controller_Motion's).
//   [6] +0x18  scalar deleting destructor -- FUN_062890d0.
//   [7] +0x1c  beam_to_target -- LAB_06288950; _purecall in the abstract base (063a53dc). The
//              integrator's teleport fallback (see beam_to_target).

// FLControllerGolemParms is declared in common_physys_internal.h (PhySys::RCBegin builds one
// too).

// FUN_06342c40 + FUN_06343020: the two-stage default constructor of the tuning struct.
void CmnPhySys::FLControllerGolemParms_Init(FLControllerGolemParms& Parms)
{
    // FUN_06342c40 zeroes nine dwords first, then sets the non-zero defaults.
    Parms.ForceFactor = 0.8f;
    Parms.DampFactor = 1.0f;
    Parms.TorqueFactor = 0.8f;
    Parms.AngularDampFactor = 1.0f;
    Parms.MaxTranslationForce[0] = 1.0e6f;
    Parms.MaxTranslationForce[1] = 1.0e6f;
    Parms.MaxTranslationForce[2] = 1.0e6f;
    Parms.Field_0x1c = 0.0f;
    Parms.MaxTorque = 1.0e6f;

    // FUN_06343020
    Parms.MaxDeltaPosition = 10.0f;
    Parms.DeltaOrientation = 1.5707964f;
    Parms.Field_0x2c = 10.0f;
    Parms.Field_0x30 = 1.0e20f;
    Parms.Field_0x34 = 5.0f;
}

// IVP_Event_Sim, the context every controller's slot 4 receives (built by FUN_063499c0,
// dispatched by FUN_063496a0), is declared in common_ivp_controller.h.
static_assert(offsetof(IVP_Event_Sim, i_delta_time) == 0x04, "IVP_Event_Sim+0x04");
static_assert(offsetof(IVP_Event_Sim, environment) == 0x08, "IVP_Event_Sim+0x08");

// IVP_Controller_Golem is declared in common_ivp_controller.h. Retail has the abstract
// IVP_Controller_Golem (vftable 063a53dc) with two leaves, FLControllerGolem (0639c1c8) and
// CRemotePhysicsController (0639ba38, common_server.h), over
// IVP_Controller_Motion : IVP_Controller_Independent : IVP_Controller (CHDs 063a70d0 /
// 063a6af8). Motion owns +0x04..+0x5c (ctor/dtor FUN_06342c80 / FUN_06342fd0), the golem layer
// +0x60..+0xcc. The base classes have no data members, so the retail offsets hold in the port
// (asserted below).
//
// Field notes:
// - CurrentTargetPosition (+0x04) / CurrentTargetQuat (+0x14), Motion's: zero vector and
//   identity quaternion from FUN_06342c80. The golem's slot 4 recomputes them every tick as the
//   target pose extrapolated to now (TargetPosition + dt*TargetSpeed; TargetQuat or the slerp
//   toward TargetRotQuat) and reads them back for the errors. Deviation: retail never
//   initialises the +0x10 pad; zeroed here.
// - Motion +0x24..+0x5c come from the parms (MaxTranslationForce +0x10..+0x18, MaxTorque +0x20
//   broadcast, the four factors +0x00..+0x0c) and the body (Environment = IVP_Object+0x18,
//   Core = IVP_Real_Object+0x98). The +0x30 / +0x40 pads are unwritten.
// - SmoothedPositionErrorSq (+0x60): an exponentially decaying (0.9f, DAT_063a53fc) sum of the
//   squared position error, written by slot 4. Nothing in the golem cluster reads it back; its
//   consumer, if any, was not found.
// - TargetPositionTime / TargetQuatTime (+0x64 / +0x6c): an IVP_Time copied as two raw dwords.
//   Slot 4 subtracts them as two separate floats ((lo - +0x64) + (hi - +0x68)), which is what
//   settles IVP_Time as a float pair. Kept as float[2] so the asserted offsets cannot shift;
//   [0] time_base, [1] time_offset.
// - InvRotDeltaTime +0x74 (1.0f default), bHasTargetRotQuat +0x78, TargetQuat +0x7c and
//   TargetRotQuat +0x8c (identity), TargetPosition +0x9c and TargetSpeed +0xac (zeroed, pads
//   unwritten), MaxDeltaPosition +0xbc, DeltaOrientation +0xc0, Field_0xc4/c8/cc from parms
//   +0x2c/+0x30/+0x34 (10.0f / 1e20f / 5.0f in practice).

// The leaf. Its constructor is inlined into FUN_06288e60 (vftable stamped right after
// FUN_06343050 returns); the scalar deleting destructor FUN_062890d0 jumps into FUN_06343120,
// so the leaf destructor adds nothing.
class FLControllerGolem : public IVP_Controller_Golem
{
public:
    FLControllerGolem(IVP_Real_Object* InBody, FLControllerGolemParms const& Parms)
        : IVP_Controller_Golem(InBody, Parms)
    {
    }

    virtual ~FLControllerGolem() {}

    // LAB_06288950, vftable slot 7.
    virtual int beam_to_target(IVP_Event_Sim const* EventSim, int Reason) override;
};

static_assert(sizeof(IVP_Time) == 8, "IVP_Time is the 8 bytes retail copies verbatim");
static_assert(sizeof(FLControllerGolem) == 0xd0, "FLControllerGolem is operator_new(0xd0) at .text:06288f67");
static_assert(offsetof(IVP_Controller_Motion, CurrentTargetPosition) == 0x04, "golem+0x04");
static_assert(offsetof(IVP_Controller_Motion, CurrentTargetQuat) == 0x14, "golem+0x14");
static_assert(offsetof(IVP_Controller_Motion, MaxTranslationForce) == 0x24, "golem+0x24");
static_assert(offsetof(IVP_Controller_Motion, MaxTorque) == 0x34, "golem+0x34");
static_assert(offsetof(IVP_Controller_Motion, ForceFactor) == 0x44, "golem+0x44");
static_assert(offsetof(IVP_Controller_Motion, AngularDampFactor) == 0x50, "golem+0x50");
static_assert(offsetof(IVP_Controller_Motion, Environment) == 0x54, "golem+0x54");
static_assert(offsetof(IVP_Controller_Motion, Body) == 0x58, "golem+0x58");
static_assert(offsetof(IVP_Controller_Motion, Core) == 0x5c, "golem+0x5c");
static_assert(offsetof(IVP_Controller_Golem, SmoothedPositionErrorSq) == 0x60, "golem+0x60");
static_assert(offsetof(IVP_Controller_Golem, TargetPositionTime) == 0x64, "golem+0x64");
static_assert(offsetof(IVP_Controller_Golem, TargetQuatTime) == 0x6c, "golem+0x6c");
static_assert(offsetof(IVP_Controller_Golem, InvRotDeltaTime) == 0x74, "golem+0x74");
static_assert(offsetof(IVP_Controller_Golem, bHasTargetRotQuat) == 0x78, "golem+0x78");
static_assert(offsetof(IVP_Controller_Golem, TargetQuat) == 0x7c, "golem+0x7c");
static_assert(offsetof(IVP_Controller_Golem, TargetRotQuat) == 0x8c, "golem+0x8c");
static_assert(offsetof(IVP_Controller_Golem, TargetPosition) == 0x9c, "golem+0x9c");
static_assert(offsetof(IVP_Controller_Golem, TargetSpeed) == 0xac, "golem+0xac");
static_assert(offsetof(IVP_Controller_Golem, MaxDeltaPosition) == 0xbc, "golem+0xbc");
static_assert(offsetof(IVP_Controller_Golem, Field_0xcc) == 0xcc, "golem+0xcc");

// Slot 2 (LAB_06285a60) is IVP_Controller_Independent's; the golem does not declare it.

// FUN_06348f50 (IVP_Controller_Manager::ensure_core_to_be_in_simulation) and FUN_0634bb90
// (IVP_Environment::add_revive_core) are IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation
// / IVP_Environment::add_revive_core in src/IVP/ivp_physics/ivp_environment.cpp.

// FUN_06343050. Both arguments go unchanged to the IVP_Controller_Motion constructor
// FUN_06342c80; this level then fills +0x60..+0xcc.
IVP_Controller_Golem::IVP_Controller_Golem(IVP_Real_Object* InBody, FLControllerGolemParms const& Parms)
    : IVP_Controller_Motion(InBody, Parms)
{
    // FUN_06343050's own share.
    SmoothedPositionErrorSq = 0.0f;
    InvRotDeltaTime = 1.0f;
    bHasTargetRotQuat = 0;

    TargetQuat.x = 0.0f;
    TargetQuat.y = 0.0f;
    TargetQuat.z = 0.0f;
    TargetQuat.w = 1.0f;
    TargetRotQuat.x = 0.0f;
    TargetRotQuat.y = 0.0f;
    TargetRotQuat.z = 0.0f;
    TargetRotQuat.w = 1.0f;

    // Retail leaves the fourth floats (+0xa8, +0xb8) unwritten; so does the port.
    TargetPosition.set(0.0f, 0.0f, 0.0f);
    TargetSpeed.set(0.0f, 0.0f, 0.0f);

    MaxDeltaPosition = Parms.MaxDeltaPosition;
    DeltaOrientation = Parms.DeltaOrientation;
    Field_0xc4 = Parms.Field_0x2c;
    Field_0xc8 = Parms.Field_0x30;
    Field_0xcc = Parms.Field_0x34;

    // The two IVP_Time stamps (+0x64..+0x70) are not written here either; every creator
    // primes them at once with set_target_position / set_target_quaternion.
}

// FUN_06343120, a tail jump to the IVP_Controller_Motion destructor FUN_06342fd0 (which
// detaches the controller). Scalar deleting wrapper FUN_06343100; the leaves use FUN_062890d0
// (FLControllerGolem) and FUN_06285a80 (CRemotePhysicsController).
IVP_Controller_Golem::~IVP_Controller_Golem()
{
}

// LAB_063435f0, vftable slot 3, shared by both leaves. Moves set_target_position's stamp
// (+0x64/+0x68) back by Delta and renormalises the fractional half into [0, 1]; TargetQuatTime
// is not touched. The carry loop runs on strictly greater, the borrow loop on !(x >= 0)
// (unordered enters it). x87: the loop tests use the unrounded difference, except the borrow
// entry test, which reloads the stored float.
void IVP_Controller_Golem::reset_time(IVP_Time Delta)
{
    TargetPositionTime[0] -= Delta.time_base;

    double Offset = (double)TargetPositionTime[1] - (double)Delta.time_offset;
    TargetPositionTime[1] = (float)Offset;

    while (Offset > 1.0)
    {
        TargetPositionTime[0] += 1.0f;
        Offset = (double)TargetPositionTime[1] - 1.0;
        TargetPositionTime[1] = (float)Offset;
    }

    if (TargetPositionTime[1] >= 0.0f) // the stored float
        return;

    do
    {
        TargetPositionTime[0] -= 1.0f;
        Offset = (double)TargetPositionTime[1] + 1.0;
        TargetPositionTime[1] = (float)Offset;
    } while (!(Offset >= 0.0));
}

// FUN_06343580 (__thiscall). Argument order from the call sites (FUN_06288e60 at 0628909c,
// AnimDB::Update at 0627e2c3).
void IVP_Controller_Golem::set_target_position(Vector const& InTargetPosition,
                                               Vector const& InTargetSpeed,
                                               IVP_Time const& Time)
{
    // Retail copies the stamp as two raw dwords.
    TargetPositionTime[0] = Time.time_base;
    TargetPositionTime[1] = Time.time_offset;

    TargetPosition.set(InTargetPosition.x, InTargetPosition.y, InTargetPosition.z);   // +0x9c
    TargetSpeed.set(InTargetSpeed.x, InTargetSpeed.y, InTargetSpeed.z);               // +0xac

    // FUN_06348f50: enqueue the core for wakeup. No test on Core.
    IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(Core);
}

// FUN_06343670 (__thiscall). The call sites pass the IVP-ordered quaternion, the same IVP_Time
// as the position call, and 0, 0.
void IVP_Controller_Golem::set_target_quaternion(IVP_U_Quat const* InTargetQuat,
                                                 IVP_Time const& Time,
                                                 IVP_U_Quat const* InTargetRotQuatOrNull,
                                                 float DeltaTime)
{
    // (X,Y,Z,W) order, straight onto IVP_U_Quat's x/y/z/w (+0x7c).
    TargetQuat.x = InTargetQuat->x;
    TargetQuat.y = InTargetQuat->y;
    TargetQuat.z = InTargetQuat->z;
    TargetQuat.w = InTargetQuat->w;

    // The 1.0f numerator is DAT_063a53d0.
    if (InTargetRotQuatOrNull != nullptr)
    {
        TargetRotQuat.x = InTargetRotQuatOrNull->x;
        TargetRotQuat.y = InTargetRotQuatOrNull->y;
        TargetRotQuat.z = InTargetRotQuatOrNull->z;
        TargetRotQuat.w = InTargetRotQuatOrNull->w;
        bHasTargetRotQuat = 1;
        InvRotDeltaTime = 1.0f / DeltaTime;
    }
    else
    {
        bHasTargetRotQuat = 0;
        // InvRotDeltaTime (+0x74) is left as it was, as in retail.
    }

    // A separate stamp from set_target_position's, copied after the branch.
    TargetQuatTime[0] = Time.time_base;
    TargetQuatTime[1] = Time.time_offset;

    // FUN_06348f50, no test on Core.
    IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(Core);
}

// ============================================================================
// LAB_06343150 -- vftable slot 4, do_simulation_controller.
// ============================================================================
// Dispatched by the per-PSI controller loop FUN_063496a0 (ivp_simulation_unit.cpp).
//
// A critically damped PD servo that drags the body onto the extrapolated target pose by direct
// edits of Core->Speed and Core->RotSpeed (never SpeedChange/RotSpeedChange). Per axis:
//     Speed    += (ForceFactor*posErr + DampFactor*dt*(targetVel - vel)) / dt
//     RotSpeed += TorqueFactor*rotErr/dt - DampFactor*RotSpeed
// with both increments clamped per axis to MaxTranslationForce / MaxTorque as real forces and
// torques, and a teleport (beam_to_target) when either error exceeds MaxDeltaPosition /
// DeltaOrientation.
//
// x87 gates (all reproduced, NaN included):
// - Position teleport on |posErr|^2 strictly greater than MaxDeltaPosition^2; NaN keeps going.
// - Force and torque clamps run on >= the bound; the sign is restored by a copysign against
//   0.0f (DAT_063a53d4), not a compare with the bound.
// - Orientation teleport: the operands are reversed from the position gate
//   (DeltaOrientation^2 < |rotErr|^2 teleports).
// - The torque scale is negated when the quaternion dot product is >= 0 (C0 clear at
//   06343471): the error is current relative to target, so the correction opposes it, and the
//   dot flips the sign for the other double-cover representation (NaN keeps it positive).
// - The error accumulator decays by 0.9f (DAT_063a53fc).
//
// FPU-stack ordering that matters: the extrapolation keeps two live copies of the elapsed time
// so x multiplies the original dt; position stores come out x, y with z moved as an integer;
// both clamp loops walk z to x. Retail reaches Core's per-axis inertia (Core+0x14) through
// biased addressing (EBP = Core - this), which is why MaxTorque (this+0x34) and RotInertia walk
// in lockstep 0x20 apart.
void IVP_Controller_Golem::do_simulation_controller(IVP_Event_Sim* EventSim,
                                                    IVP_U_Vector<IVP_Core>* AffectedCores)
{
    // The affected-core list is never read.
    (void)AffectedCores;

    // Retail tests none of EventSim / environment / Body / Core.

    float const Dt = EventSim->delta_time;
    float const IDt = EventSim->i_delta_time;
    ::IVP_Time const& Now = EventSim->environment->current_time;

    // Everything retail keeps on the x87 stack is double below; a value is rounded only where
    // retail stores it to a float slot.

    // ---- 1. Extrapolate the stored target pose to this instant ----
    // The stamp halves are subtracted separately (IVP_Time is a float pair).
    double const PosElapsed = (((double)(Now.time_base) - TargetPositionTime[0])
                               + Now.time_offset) - TargetPositionTime[1];

    // Retail's store order: z, x, y.
    CurrentTargetPosition.k[2] = (float)(PosElapsed * TargetSpeed.k[2] + TargetPosition.k[2]);
    CurrentTargetPosition.k[0] = (float)(PosElapsed * TargetSpeed.k[0] + TargetPosition.k[0]);
    CurrentTargetPosition.k[1] = (float)(PosElapsed * TargetSpeed.k[1] + TargetPosition.k[1]);

    if (bHasTargetRotQuat == 0)
    {
        CurrentTargetQuat = TargetQuat;
    }
    else
    {
        // The rotation stamp gives the interpolation parameter, rounded to float as an argument.
        double const QuatElapsed = (((double)(Now.time_base) - TargetQuatTime[0])
                                    + Now.time_offset) - TargetQuatTime[1];
        CurrentTargetQuat.set_interpolate_quat(&TargetQuat, &TargetRotQuat,
                                               (float)(QuatElapsed * InvRotDeltaTime));  // FUN_06350df0
    }

    // ---- 2. Position error, and the teleport escape hatch ----
    // Core+0x124..+0x12c is MWorldFCore.vv, the matrix's translation row.
    IVP_U_Point Delta;
    Delta.k[0] = CurrentTargetPosition.k[0] - Core->MWorldFCore.vv.k[0];
    Delta.k[1] = CurrentTargetPosition.k[1] - Core->MWorldFCore.vv.k[1];
    Delta.k[2] = CurrentTargetPosition.k[2] - Core->MWorldFCore.vv.k[2];

    // Flag bits 0x0C00 at Body+0x78 clear: the body is offset from its core's origin, so the
    // core-to-object shift is rotated into world space and taken out of the error.
    if ((Body->flags & 0x0C00u) == 0)
    {
        IVP_U_Point ShiftWorld;
        Core->MWorldFCore.vmult(&Body->shift_core_f_object, &ShiftWorld);
        Delta.subtract(&Delta, &ShiftWorld);  // FUN_0628bea0, this = v1 - v2
    }

    // Written out because retail sums (y^2 + z^2) + x^2, not quad_length()'s order. x87: never
    // stored.
    double const DeltaQuadLength =
        ((double)(Delta.k[1]) * Delta.k[1] + (double)(Delta.k[2]) * Delta.k[2])
        + (double)(Delta.k[0]) * Delta.k[0];

    SmoothedPositionErrorSq = (float)(
        (double)(SmoothedPositionErrorSq) * (double)(0.9f) + DeltaQuadLength);

    if (DeltaQuadLength > (double)(MaxDeltaPosition) * MaxDeltaPosition)
    {
        beam_to_target(EventSim, 0);
        return;
    }

    // ---- 3. Linear servo ----
    Delta.k[0] *= ForceFactor;
    Delta.k[1] *= ForceFactor;
    Delta.k[2] *= ForceFactor;

    // Two add_multiple calls (FUN_0628be60, this = v1 + v2*f), the second negated: the velocity
    // error, target minus actual. Each scale is rounded to a float argument.
    float const DampDt = DampFactor * Dt;
    Delta.k[0] = (float)((double)(DampDt) * TargetSpeed.k[0] + Delta.k[0]);
    Delta.k[1] = (float)((double)(DampDt) * TargetSpeed.k[1] + Delta.k[1]);
    Delta.k[2] = (float)((double)(DampDt) * TargetSpeed.k[2] + Delta.k[2]);

    float const NegDampDt = -(DampFactor * Dt);
    Delta.k[0] = (float)((double)(NegDampDt) * Core->Speed.x + Delta.k[0]);
    Delta.k[1] = (float)((double)(NegDampDt) * Core->Speed.y + Delta.k[1]);
    Delta.k[2] = (float)((double)(NegDampDt) * Core->Speed.z + Delta.k[2]);

    // Per-axis force clamp, z to x. Delta becomes a velocity increment Delta*IDt, so its force
    // is Mass*Delta*IDt^2 and the clamp the inverse, InvMass*Dt^2*bound.
    {
        // x87: IDt squared stays on the stack across the loop.
        double const IDtSquared = (double)(IDt) * IDt;
        for (int Axis = 2; Axis >= 0; --Axis)
        {
            double const AxisForce = fabs((double)(Core->Mass) * Delta.k[Axis] * IDtSquared);
            if (AxisForce >= MaxTranslationForce.k[Axis])
            {
                // Negated when the delta is below zero or unordered, hence !(>=).
                double Clamped = (double)Core->InvMass * Dt * Dt * MaxTranslationForce.k[Axis];
                if (!(Delta.k[Axis] >= 0.0f))
                    Clamped = -Clamped;
                Delta.k[Axis] = (float)(Clamped);
            }
        }
    }

    // A direct velocity edit, stored z, x, y.
    Core->Speed.z = (float)((double)(Delta.k[2]) * IDt + Core->Speed.z);
    Core->Speed.x = (float)((double)(Delta.k[0]) * IDt + Core->Speed.x);
    Core->Speed.y = (float)((double)(Delta.k[1]) * IDt + Core->Speed.y);

    // ---- 4. Orientation error, and its own teleport escape hatch ----
    // conj(target) * current, then 2*asin of each xyz component as the rotation vector
    // (FUN_063510c0 / FUN_063510f0 / FUN_06350a60).
    IVP_U_Quat InvTargetQuat;
    InvTargetQuat.set_invert_unit(&CurrentTargetQuat);

    IVP_U_Quat RelativeQuat;
    RelativeQuat.set_mult_quat(&InvTargetQuat, &Core->QWorldFCoreNext);

    IVP_U_Point RelativeQuatXYZ;
    RelativeQuatXYZ.set(RelativeQuat.x, RelativeQuat.y, RelativeQuat.z);
    IVP_U_Point RotError;
    IVP_DoubleAsinVector(&RelativeQuatXYZ, &RotError);

    // Summed (z^2 + y^2) + x^2, a different order from the position error.
    double const RotQuadLength =
        ((double)(RotError.k[2]) * RotError.k[2] + (double)(RotError.k[1]) * RotError.k[1])
        + (double)(RotError.k[0]) * RotError.k[0];

    // A NaN length teleports too, hence !(>=) rather than <.
    if (!((double)(DeltaOrientation) * DeltaOrientation >= RotQuadLength))
    {
        beam_to_target(EventSim, 1);
        return;
    }

    // ---- 5. Angular servo ----
    // The dot product picks which double-cover representation the asin reduction landed on.
    double const QuatDot =
        (((double)(Core->QWorldFCoreNext.w) * CurrentTargetQuat.w
          + (double)(Core->QWorldFCoreNext.z) * CurrentTargetQuat.z)
         + (double)(Core->QWorldFCoreNext.y) * CurrentTargetQuat.y)
        + (double)(CurrentTargetQuat.x) * Core->QWorldFCoreNext.x;

    // Negated on dot >= 0, kept on dot < 0 or unordered, as in retail.
    double RotScale = (double)(IDt) * TorqueFactor;
    if (QuatDot >= 0.0)
        RotScale = -RotScale;

    // Stored z, x, y.
    RotError.k[2] = (float)(RotError.k[2] * RotScale);
    RotError.k[0] = (float)(RotScale * RotError.k[0]);
    RotError.k[1] = (float)(RotError.k[1] * RotScale);

    // Retail: rotation is damped with DampFactor (+0x48, fld [esi+48h] at 063434C3), the
    // linear factor; the golem never reads AngularDampFactor (+0x50), which only the
    // IVP_Controller_Motion servo uses.
    double const NegDamp = -(double)(DampFactor);
    float RotDelta[3];
    RotDelta[0] = (float)(NegDamp * Core->RotSpeed.x + RotError.k[0]);
    RotDelta[1] = (float)(NegDamp * Core->RotSpeed.y + RotError.k[1]);
    RotDelta[2] = (float)(NegDamp * Core->RotSpeed.z + RotError.k[2]);

    // The torque mirror of the force clamp, with per-axis inertia instead of scalar mass.
    {
        float const RotInertiaAxis[3] = {Core->RotInertia.x, Core->RotInertia.y,
                                         Core->RotInertia.z};
        float const InvRotInertiaAxis[3] = {Core->InvRotInertia.x, Core->InvRotInertia.y,
                                            Core->InvRotInertia.z};
        for (int Axis = 2; Axis >= 0; --Axis)
        {
            double const AxisTorque = (double)(IDt) * RotInertiaAxis[Axis] * RotDelta[Axis];
            if (fabs(AxisTorque) >= MaxTorque.k[Axis])
            {
                // The sign follows the signed torque (not the delta); unordered negates.
                double Clamped = (double)InvRotInertiaAxis[Axis] * Dt * MaxTorque.k[Axis];
                if (!(AxisTorque >= 0.0))
                    Clamped = -Clamped;
                RotDelta[Axis] = (float)(Clamped);
            }
        }
    }

    Core->RotSpeed.x = RotDelta[0] + Core->RotSpeed.x;
    Core->RotSpeed.y = RotDelta[1] + Core->RotSpeed.y;
    Core->RotSpeed.z = RotDelta[2] + Core->RotSpeed.z;
}

// FUN_06343130: a hard teleport of the body onto the extrapolated target pose
// (IVP_Real_Object::beam_object_to_new_position, FUN_06345f90, with
// optimize_for_repeated_calls = 1). CRemotePhysicsController calls it too. The event argument
// is never read.
void IVP_Controller_Golem::beam_to_current_target(IVP_Event_Sim const* EventSim)
{
    (void)EventSim;

    Body->beam_object_to_new_position(&CurrentTargetQuat, &CurrentTargetPosition, 1);
}

// LAB_06288950: forwards only the event, drops the 0/1 reason, returns 0.
int FLControllerGolem::beam_to_target(IVP_Event_Sim const* EventSim, int Reason)
{
    (void)Reason;

    beam_to_current_target(EventSim);

    return 0;
}

// ---- port-local entry points declared in common_physys.h ----
// They stand for AnimDB::Update's direct calls on [CObject+0x60]; no null test of their own
// (the caller tests).

void __cdecl PhySys::GolemSetTargetPosition(::FLControllerGolem* Golem,
                                            Vector const& TargetPosition,
                                            Vector const& TargetSpeed,
                                            IVP_Time const& Time)
{
    Golem->set_target_position(TargetPosition, TargetSpeed, Time);
}

void __cdecl PhySys::GolemSetTargetQuaternion(::FLControllerGolem* Golem,
                                              IVP_U_Quat const* TargetQuat,
                                              IVP_Time const& Time,
                                              IVP_U_Quat const* TargetRotQuatOrNull,
                                              float DeltaTime)
{
    Golem->set_target_quaternion(TargetQuat, Time, TargetRotQuatOrNull, DeltaTime);
}

// ============================================================================
// PhySys::BeamBodyToWorldPose -- FUN_06343cc0
//
// Seats the body at an absolute world pose without moving its core: it recomputes the body's
// offset within the core (q_core_f_object / shift_core_f_object, via ApplyLocalCsys), which is
// what an animated sub-part welded into a parent's core needs. It then bumps the hull
// manager's outer margin, frees the narrow-phase cache slot, recalculates mindists and drains
// any hull-pool listeners the bump invalidated.
//
// __cdecl. Both call sites (AnimDB::Update and FUN_06343700) pass 0 as the first argument and
// a fifth argument the body never reads. Constants: DAT_063a53d4 0.0f, DAT_063a5400 10.0f,
// DAT_063a5404 0.01f. ApplyLocalCsys is vtable slot 2, which neither leaf (IVP_Polygon,
// IVP_Ball) overrides.
// ============================================================================
bool __cdecl PhySys::BeamBodyToWorldPose(IVP_Real_Object* ReferenceBodyOrNull, IVP_Real_Object* Object,
                                         IVP_U_Quat const* WorldQuat, Vector const& WorldPosition)
{

    // ---- The reference/base transform ----
    // Retail's FUN_06273bc0 placement-construct calls run Csys::Csys, which writes nothing;
    // not reproduced.
    IVP_U_Matrix LocalBase;
    if (ReferenceBodyOrNull != nullptr)
    {
        // The reference body's narrow-phase cache slot (get_cache_object_no_lock), whose
        // WorldTransform is copied.
        IVP_NarrowPhaseCacheSlot* const Slot = ReferenceBodyOrNull->get_cache_object_no_lock();
        memcpy(&LocalBase, &Slot->WorldTransform, sizeof(IVP_U_Matrix));
    }
    else
    {
        LocalBase.init();
    }

    // ---- The requested world pose ----
    IVP_U_Matrix WorldFromObject;
    WorldQuat->set_matrix(&WorldFromObject);   // IVP (x,y,z,w) order
    WorldFromObject.vv.set(WorldPosition.x, WorldPosition.y, WorldPosition.z);

    IVP_U_Matrix Combined;
    LocalBase.mmult4(&WorldFromObject, &Combined); // FUN_063481d0

    // ---- The core's own extrapolated world pose ----
    IVP_Core* const Core = Object->core;
    IVP_Environment* const Env = Object->environment;

    // One x87 chain, ((tb - psi_tb) + to) - psi_to, times IDeltaTime, rounded only at the end.
    float const Alpha = (float)(
        (((double)(Env->current_time.time_base) - Core->TimeOfLastPsi.time_base)
         + Env->current_time.time_offset - Core->TimeOfLastPsi.time_offset)
        * Core->IDeltaTime);

    // The same difference without the scale, for the position extrapolation.
    float const DTime = (float)(
        ((double)(Env->current_time.time_base) - Core->TimeOfLastPsi.time_base)
        + Env->current_time.time_offset - Core->TimeOfLastPsi.time_offset);

    IVP_U_Quat InterpQuat;
    InterpQuat.set_interpolate_quat(&Core->QWorldFCore, &Core->QWorldFCoreNext, Alpha);

    IVP_U_Matrix CoreWorld;
    InterpQuat.set_matrix(&CoreWorld);

    PhySys_Vec3AddMul((Vector&)CoreWorld.vv, Core->PosWorldFCore, Core->DeltaWorldFCore, DTime);

    // ---- Re-seat the body inside its core ----
    // inverse(Combined) * CoreWorld: the core's frame in the object's frame, which
    // ApplyLocalCsys inverts back into shift_core_f_object / q_core_f_object.
    IVP_U_Matrix ObjectFromCore;
    Combined.mimult4(&CoreWorld, &ObjectFromCore); // FUN_063482c0
    Object->ApplyLocalCsys(&ObjectFromCore);

    // ---- Widen the hull margin, in retail's write order ----
    IVP_Hull_Manager& Hull = Object->hull_manager;
    // x87: one extended sum, rounded at the stores.
    float const NewOuter = (float)((double)(Hull.AccumulatedDeltaA) + 0.01f);
    Hull.OuterGradient = 0.0f;
    Hull.AccumulatedDeltaA = NewOuter;
    Hull.InnerGradient = 0.0f;
    Hull.OuterValueAtLastEdit = NewOuter;

    IVP_Real_Object_Free_Cache_Object(Object);   // FUN_0634d510

    Object->recalc_exact_mindists();   // FUN_063454b0
    Object->recalc_invalid_mindists(); // FUN_06345460

    // ---- Drain hull-pool listeners ----
    // Retail tests the guard before decrementing it, so the body runs at most 102 times.
    int Guard = 100;
    // The difference goes to the listener as a float but is compared unrounded (a tiny
    // negative value would round to -0.0f). The loop continues while it is negative or NaN.
    double ExcessExact = (double)Hull.PoolMinKey - Hull.AccumulatedDeltaA;
    float Excess = (float)ExcessExact;
    while (!(ExcessExact >= 0.0))
    {
        // The live entry's IVP_Listener_Hull gets hull_limit_exceeded_event (slot 1).
        IVP_Hull_Manager::Entry& E = Hull.PoolEntries[Hull.PoolLiveIndex];
        ((IVP_Listener_Hull*)E.Payload)->hull_limit_exceeded_event(&Hull, Excess);

        int const GuardBeforeDecrement = Guard;
        Guard -= 1;
        if (GuardBeforeDecrement < 0)
            break;

        ExcessExact = (double)Hull.PoolMinKey - Hull.AccumulatedDeltaA;
        Excess = (float)ExcessExact;
    }

    // ---- Periodic rebase ----
    // Runs when the cached integer has fallen behind the timestamp, or the timestamp is NaN.
    if (!((double)(int32_t)Hull.TimeOfNextReset >= (double)Hull.LastEditTimeHi))
    {
        Hull.DrainAndShift();
        // x87: the unrounded sum, truncated toward zero.
        Hull.TimeOfNextReset = (uint32_t)(int32_t)((double)Hull.LastEditTimeHi + 10.0f);
    }

    return true;  // constant, never inspected
}

// FUN_06288e60 (__cdecl). The per-child-part physics finalize; its only caller is FUN_06288b30,
// the loop that spawns a child CObject per Archetype::Root::get_phy_part entry. Mode 1 attaches
// a golem; mode 2 welds the body into GetRoot(Object)'s body.
void __cdecl PhySys::AttachGolemController(CObject* Object, CreateParms const& Parms)
{
    // CreateParms+0x18: 1 golem, 2 weld, anything else returns.
    if (Parms.Type == PhySys::ANIM_TECHNIQUE_GOLEM)
    {
        // The mode is set before the controller exists.
        Object->PhysicsControlMode = PhySys::ANIM_TECHNIQUE_GOLEM;

        // Default-construct the tuning struct, then overwrite it from the GOLEM_* globals.
        FLControllerGolemParms GolemParms;
        FLControllerGolemParms_Init(GolemParms);

        GolemParms.ForceFactor = PhySys::GOLEM_FORCE_FACTOR;
        GolemParms.DampFactor = PhySys::GOLEM_DAMP_FACTOR;
        GolemParms.TorqueFactor = PhySys::GOLEM_TORQUE_FACTOR;
        GolemParms.AngularDampFactor = PhySys::GOLEM_ANGULAR_DAMP_FACTOR;
        GolemParms.MaxTranslationForce[0] = PhySys::GOLEM_MAX_TRANSLATION_FORCE.x;
        GolemParms.MaxTranslationForce[1] = PhySys::GOLEM_MAX_TRANSLATION_FORCE.y;
        GolemParms.MaxTranslationForce[2] = PhySys::GOLEM_MAX_TRANSLATION_FORCE.z;
        GolemParms.MaxTorque = PhySys::GOLEM_MAX_TORQUE;
        GolemParms.MaxDeltaPosition = PhySys::GOLEM_MAX_DELTA_POSITION;
        GolemParms.DeltaOrientation = PhySys::GOLEM_DELTA_ORIENTATION;

        // Deviation: retail copies an uninitialised stack local into parms+0x1c. Nothing reads
        // that field, so the port keeps the 0.0f default.

        // operator_new(0xd0). Retail stores a null result and then calls set_target_position
        // on it, a crash reproduced with nothrow.
        FLControllerGolem* const Golem = new (std::nothrow) FLControllerGolem(Object->BodyPtr, GolemParms);
        Object->GolemController = Golem;

        // No miss test on the system lookup, as in retail.
        IVP_Time const Now = PhySys::GetCurrentTime(Parms.SystemId);

        I3DMathEngine* const Math = Acquire3DMathEngine();

        Quaternion MathQuat = {0.0f, 0.0f, 0.0f, 0.0f};
        Math->MatrixToQuaternion(MathQuat, Object->Orientation);

        // Quaternion is (W,X,Y,Z); retail rotates it into IVP's (X,Y,Z,W).
        IVP_U_Quat const IvpQuat = {MathQuat.x, MathQuat.y, MathQuat.z, MathQuat.w};

        Vector const ZeroSpeed = {0.0f, 0.0f, 0.0f};

        Golem->set_target_position(Object->WorldPosition, ZeroSpeed, Now);
        Golem->set_target_quaternion(&IvpQuat, Now, nullptr, 0.0f);
        return;
    }

    if (Parms.Type != PhySys::ANIM_TECHNIQUE_MERGED)
        return;

    Object->PhysicsControlMode = PhySys::ANIM_TECHNIQUE_MERGED;

    // FUN_06343700(Root body, Object body, -1.0f).
    CObject* const Root = GetRoot(Object);
    IVP_Merge_Object_Into_Core(Root->BodyPtr, Object->BodyPtr, -1.0f);
}

