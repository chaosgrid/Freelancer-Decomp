// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_controller.h"
#include "ivp_core.h"
#include "ivp_simulation_unit.h"
#include "ivp_real_object.h"  // IVP_Real_Object::environment / core (IVP_Controller_Motion ctor)

#include <cmath>
#include <cstdint>
#include <cstdlib>

// ============================================================================
// Free functions that walk an IVP_Controller's associated cores (vtable slot
// 2, get_associated_controlled_cores()) to keep the affected
// IVP_Simulation_Unit(s) in sync when a controller is attached, detached or
// changes its cores. Functions taking an IVP_Simulation_Unit* live in
// ivp_simulation_unit.cpp.
// ============================================================================

// FUN_06348f60 (two stack arguments; Ghidra lists only the first). Detaches
// the controller from every associated core (backward), tracking the last
// core's OwningSimUnit (detach does not change it). If there was one: State
// clears 0x200 and sets 0x100 (meaning unknown), then -- unless the second
// argument is nonzero -- update last-motion times if awake, else revive.
void IVP_Controller_Detach_From_All_Cores(IVP_Controller* Controller, int SkipFinalWakeCheck)
{
    IVP_U_Vector<IVP_Core>* Cores = Controller->get_associated_controlled_cores();
    IVP_Simulation_Unit* SimUnit = nullptr;

    for (int i = static_cast<int>(Cores->n_elems) - 1; i >= 0; --i)
    {
        IVP_Core* Core = Cores->ElementAt(i);
        Core->detach_controller(Controller); // FUN_06349110
        SimUnit = Core->OwningSimUnit;        // read after the detach
    }

    if (SimUnit == nullptr)
    {
        return;
    }

    SimUnit->State = (SimUnit->State & 0xFFFFFDFFu) | 0x100u;

    if (SkipFinalWakeCheck != 0)
    {
        return;
    }

    if (static_cast<int8_t>(SimUnit->State & 0xFF) < IVP_MT_NOT_SIM)
    {
        SimUnit->update_cores_last_motion_time(); // FUN_063494f0
    }
    else
    {
        IVP_Core* FirstCore = SimUnit->Cores.ElementAt(0);
        SimUnit->revive_if_sleeping(FirstCore->Environment); // FUN_06349490
    }
}

// FUN_06348fe0: only the first associated core's OwningSimUnit gets the same
// "update last-motion times if awake, else revive" dispatch; no-op without
// cores. Callers: 06285bc7 / 06285ca6.
void IVP_Controller_Wake_Or_Refresh_Associated_Sim_Unit(IVP_Controller* Controller)
{
    IVP_U_Vector<IVP_Core>* Cores = Controller->get_associated_controlled_cores();
    if (Cores->n_elems == 0)
    {
        return;
    }

    IVP_Core* FirstCore = Cores->ElementAt(0);
    IVP_Simulation_Unit* SimUnit = FirstCore->OwningSimUnit;

    if (static_cast<int8_t>(SimUnit->State & 0xFF) < IVP_MT_NOT_SIM)
    {
        SimUnit->update_cores_last_motion_time(); // FUN_063494f0
    }
    else
    {
        IVP_Core* UnitFirstCore = SimUnit->Cores.ElementAt(0);
        SimUnit->revive_if_sleeping(UnitFirstCore->Environment); // FUN_06349490
    }
}

// FUN_06349040 (thiscall + one stack argument, the controller). ECX is not a
// real `this`: it is read once at the end and dereferenced for the
// IVP_Environment* handed to Revive_If_Sleeping, so it is ported as
// EnvironmentPtrHolder. Its only caller is 06285cf0 (provenance of that
// pointer not traced).
//
// Walks every core associated with Controller (backward). Cores with
// (Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0 are skipped entirely (no register_controller call
// either). For every OTHER core: AND-accumulates its MovementState low byte
// into MovementByteAcc (init 8); tracks the previous iteration's
// OwningSimUnit as PrevSimUnit, and whenever two consecutive valid cores
// disagree on SimUnit, merges them (absorbing the newer one into
// PrevSimUnit and freeing the now-empty absorbed unit's vector buffers +
// memory -- same pattern IVP_Simulation_Unit::detach_core already uses
// inline), then unconditionally re-registers the core with the controller
// (IVP_Core::register_controller). After the walk: if any merge
// happened, rebuilds PrevSimUnit's controller buckets from scratch; if the
// accumulated verdict ended up below 8 (at least one processed core is
// non-static), revives PrevSimUnit if it was asleep.
void IVP_Controller_Reconcile_Associated_Cores(IVP_Environment* const* EnvironmentPtrHolder, IVP_Controller* Controller)
{
    IVP_U_Vector<IVP_Core>* Cores = Controller->get_associated_controlled_cores();

    IVP_Simulation_Unit* PrevSimUnit = nullptr;
    bool MergeHappened = false;
    int MovementByteAcc = 8;

    for (int i = static_cast<int>(Cores->n_elems) - 1; i >= 0; --i)
    {
        IVP_Core* Core = Cores->ElementAt(i);
        if ((Core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0)
        {
            continue; // dying/invalid core -- skipped entirely, not even re-registered
        }

        MovementByteAcc &= static_cast<int8_t>(Core->MovementState & 0xFF);
        IVP_Simulation_Unit* CurrentSimUnit = Core->OwningSimUnit;

        if (PrevSimUnit != nullptr && CurrentSimUnit != PrevSimUnit)
        {
            // Two neighbouring cores in the SAME controller's associated-list
            // disagree on their owning unit -- merge them (FUN_06348c10).
            PrevSimUnit->absorb_cores_from(CurrentSimUnit);
            if (CurrentSimUnit != nullptr)
            {
                // FUN_06348d80 (the unit's destructor body:
                // clear_controller_buckets, then both vector buffers) and
                // operator delete (FUN_06391d5a), as in IVP_Simulation_Unit::detach_core.
                CurrentSimUnit->free_vector_buffers();
                ::operator delete(CurrentSimUnit);
            }
            MergeHappened = true;
        }
        else if (PrevSimUnit == nullptr)
        {
            PrevSimUnit = CurrentSimUnit;
        }

        Core->register_controller(Controller); // FUN_06349180
    }

    if (MergeHappened)
    {
        PrevSimUnit->clear_controller_buckets(); // FUN_06348b80
        PrevSimUnit->rebuild_controller_buckets(); // FUN_063487f0
    }

    if (MovementByteAcc < 8)
    {
        PrevSimUnit->revive_if_sleeping(*EnvironmentPtrHolder); // FUN_06349490
    }
}

// ============================================================================
// IVP_Controller_Independent / IVP_Controller_Motion (declared, with their RTTI and
// vftables, in common_ivp_controller.h). IVP_Controller_Golem (common_physys.cpp) derives
// from Motion.
// ============================================================================

// DAT_0658a9e8: the one IVP_U_Vector<IVP_Core> every independent controller hands out from
// slot 2. Constructed empty by the static initializer LAB_06348760 -> FUN_06348770 and
// destroyed at exit (FUN_06348790 -> LAB_063487a0); nothing ever adds an element.
static IVP_U_Vector<IVP_Core> G_IndependentControlledCores;

// Slot 2, LAB_06285a60: returns &DAT_0658a9e8.
IVP_U_Vector<IVP_Core>* IVP_Controller_Independent::get_associated_controlled_cores()
{
    return &G_IndependentControlledCores;
}

// FUN_06342c80 (RET 8), vtable 063a53b4.
IVP_Controller_Motion::IVP_Controller_Motion(IVP_Real_Object* InBody,
                                             IVP_Controller_Motion_Parms const& Parms)
{
    // Zero vector at +0x04 and identity quaternion at +0x14. The hesse words +0x10, +0x30
    // and +0x40 are never written, as in retail.
    CurrentTargetPosition.set(0.0f, 0.0f, 0.0f);
    CurrentTargetQuat.x = 0.0f;
    CurrentTargetQuat.y = 0.0f;
    CurrentTargetQuat.z = 0.0f;
    CurrentTargetQuat.w = 1.0f;

    MaxTranslationForce.set(Parms.MaxTranslationForce[0],
                            Parms.MaxTranslationForce[1],
                            Parms.MaxTranslationForce[2]);

    // parms+0x20 is one scalar broadcast across all three axes.
    MaxTorque.set(Parms.MaxTorque, Parms.MaxTorque, Parms.MaxTorque);

    ForceFactor = Parms.ForceFactor;
    DampFactor = Parms.DampFactor;
    TorqueFactor = Parms.TorqueFactor;
    AngularDampFactor = Parms.AngularDampFactor;

    // No null test on the body.
    Environment = InBody->environment;
    Body = InBody;
    Core = InBody->core;

    IVP_Controller_Manager_Add_Controller_To_Core(this, Core); // FUN_06349020
}

// FUN_06349020 (cdecl): IVP_Controller_Manager::add_controller_to_core, FUN_06349180 with no
// null test.
void IVP_Controller_Manager_Add_Controller_To_Core(IVP_Controller* Controller, IVP_Core* Core)
{
    Core->register_controller(Controller);
}

// FUN_06349030 (cdecl): IVP_Controller_Manager::remove_controller_from_core, the same over
// FUN_06349110.
void IVP_Controller_Manager_Remove_Controller_From_Core(IVP_Controller* Controller, IVP_Core* Core)
{
    Core->detach_controller(Controller);
}

// FUN_06342fd0: FUN_06349030(this, Core) with no test on the core; the vptr stamps
// (063a53b4, then IVP_Controller's 0639b64c) are compiler-generated. Scalar deleting
// wrapper FUN_06342d40 (063a53b4[6]).
IVP_Controller_Motion::~IVP_Controller_Motion()
{
    IVP_Controller_Manager_Remove_Controller_From_Core(this, Core);
}

// Vftable slot 0, LAB_06342fc0 (RET 4): delete this through the virtual deleting destructor
// (retail's null test of `this` has no C++ counterpart); the core argument is ignored.
// Inherited by the golem (063a53dc[0]) and both leaves (0639c1c8[0], 0639ba38[0]). Retail
// does not clear Core first: the destructor detaches from the dying core.
void IVP_Controller_Motion::core_is_going_to_be_deleted_event(IVP_Core* DeletedCore)
{
    (void)DeletedCore;
    delete this;
}

// Vftable slot 5, LAB_06285a70: returns 500. PhySys::Controller's own slot 5 returns 1500. No IVP_CONTROLLER_PRIORITY enumerator
// is confirmed for 500, so the literal is used.
IVP_CONTROLLER_PRIORITY IVP_Controller_Motion::get_controller_priority()
{
    return (IVP_CONTROLLER_PRIORITY)500;
}

// Vftable slot 4, LAB_06342d60 (RET 8). The same PD servo as IVP_Controller_Golem's slot 4
// (LAB_06343150, common_physys.cpp) minus its extrapolation step, body-shift correction,
// error accumulator and both teleport gates, and with one different term: the angular
// damping uses AngularDampFactor (+0x50) where the golem uses DampFactor. Retail never
// dispatches it on a live golem (the golem overrides slot 4), but the class is concrete.
void IVP_Controller_Motion::do_simulation_controller(IVP_Event_Sim* EventSim,
                                                     IVP_U_Vector<IVP_Core>* AffectedCores)
{
    (void)AffectedCores;  // never read

    float const Dt = EventSim->delta_time;
    float const IDt = EventSim->i_delta_time;

    // Delta = (CurrentTargetPosition - MWorldFCore.vv) * ForceFactor, each component formed
    // on the FPU and rounded once.
    IVP_U_Point Delta;
    Delta.k[0] = (float)(((double)CurrentTargetPosition.k[0] - Core->MWorldFCore.vv.k[0]) * ForceFactor);
    Delta.k[1] = (float)(((double)CurrentTargetPosition.k[1] - Core->MWorldFCore.vv.k[1]) * ForceFactor);
    Delta.k[2] = (float)(((double)CurrentTargetPosition.k[2] - Core->MWorldFCore.vv.k[2]) * ForceFactor);

    // FUN_0628be60(&Delta, &Delta, &Core->Speed, float(-(DampFactor * dt))):
    // f*v2[i] + v1[i] on the FPU, rounded once.
    float const NegDampDt = -(DampFactor * Dt);
    Delta.k[0] = (float)((double)NegDampDt * Core->Speed.x + Delta.k[0]);
    Delta.k[1] = (float)((double)NegDampDt * Core->Speed.y + Delta.k[1]);
    Delta.k[2] = (float)((double)NegDampDt * Core->Speed.z + Delta.k[2]);

    // Per-axis force clamp, z -> x, (1/dt)^2 kept on the FPU: when |Mass * Delta[i] *
    // (1/dt)^2| >= Max[i] (less or unordered skips), Delta[i] = +-InvMass * dt * dt * Max[i]
    // with the sign of Delta[i].
    {
        double const IDtSquared = (double)IDt * IDt;
        for (int Axis = 2; Axis >= 0; --Axis)
        {
            double const AxisForce = fabs((double)Core->Mass * Delta.k[Axis] * IDtSquared);
            if (AxisForce >= MaxTranslationForce.k[Axis])
            {
                double Clamped = (double)Core->InvMass * Dt * Dt * MaxTranslationForce.k[Axis];
                if (Delta.k[Axis] < 0.0f)
                    Clamped = -Clamped;
                Delta.k[Axis] = (float)Clamped;
            }
        }
    }

    // FUN_0628be60: Speed[i] = float((1/dt)*Delta[i] + Speed[i]).
    Core->Speed.x = (float)((double)IDt * Delta.k[0] + Core->Speed.x);
    Core->Speed.y = (float)((double)IDt * Delta.k[1] + Core->Speed.y);
    Core->Speed.z = (float)((double)IDt * Delta.k[2] + Core->Speed.z);

    // FUN_063510c0 (invert CurrentTargetQuat), FUN_063510f0 (Rel = inverse * QWorldFCoreNext),
    // FUN_06350a60 (rotation-error vector) -- the same three calls as the golem's slot 4.
    IVP_U_Quat InvTargetQuat;
    InvTargetQuat.set_invert_unit(&CurrentTargetQuat);

    IVP_U_Quat RelativeQuat;
    RelativeQuat.set_mult_quat(&InvTargetQuat, &Core->QWorldFCoreNext);

    IVP_U_Point RelativeQuatXYZ;
    RelativeQuatXYZ.set(RelativeQuat.x, RelativeQuat.y, RelativeQuat.z);
    IVP_U_Point RotError;
    IVP_DoubleAsinVector(&RelativeQuatXYZ, &RotError);

    // ((w*w' + z*z') + y*y') + x*x' of the core and target quaternions, on the FPU.
    double const QuatDot =
        (((double)Core->QWorldFCoreNext.w * CurrentTargetQuat.w
          + (double)Core->QWorldFCoreNext.z * CurrentTargetQuat.z)
         + (double)Core->QWorldFCoreNext.y * CurrentTargetQuat.y)
        + (double)Core->QWorldFCoreNext.x * CurrentTargetQuat.x;

    // scale = (1/dt) * TorqueFactor on the FPU, negated when the dot is >= 0 (a NaN dot
    // keeps the sign), as in the golem; then RotError[i] = float(RotError[i] * scale).
    double RotScale = (double)IDt * TorqueFactor;
    if (QuatDot >= 0.0)
        RotScale = -RotScale;

    RotError.k[0] = (float)(RotError.k[0] * RotScale);
    RotError.k[1] = (float)(RotError.k[1] * RotScale);
    RotError.k[2] = (float)(RotError.k[2] * RotScale);

    // float((-AngularDampFactor) * RotSpeed[i] + RotError[i]).
    float RotDelta[3];
    RotDelta[0] = (float)(-(double)AngularDampFactor * Core->RotSpeed.x + RotError.k[0]);
    RotDelta[1] = (float)(-(double)AngularDampFactor * Core->RotSpeed.y + RotError.k[1]);
    RotDelta[2] = (float)(-(double)AngularDampFactor * Core->RotSpeed.z + RotError.k[2]);

    // Torque clamp, z -> x, all products on the FPU: when |(1/dt) * RotInertia[i] *
    // RotDelta[i]| >= MaxTorque[i], RotDelta[i] = +-InvRotInertia[i] * dt * MaxTorque[i]
    // with the sign of the torque.
    {
        float const RotInertiaAxis[3] = {Core->RotInertia.x, Core->RotInertia.y,
                                         Core->RotInertia.z};
        float const InvRotInertiaAxis[3] = {Core->InvRotInertia.x, Core->InvRotInertia.y,
                                            Core->InvRotInertia.z};
        for (int Axis = 2; Axis >= 0; --Axis)
        {
            double const AxisTorque = (double)IDt * RotInertiaAxis[Axis] * RotDelta[Axis];
            if (fabs(AxisTorque) >= MaxTorque.k[Axis])
            {
                double Clamped = (double)InvRotInertiaAxis[Axis] * Dt * MaxTorque.k[Axis];
                if (AxisTorque < 0.0)
                    Clamped = -Clamped;
                RotDelta[Axis] = (float)Clamped;
            }
        }
    }

    Core->RotSpeed.x = RotDelta[0] + Core->RotSpeed.x;
    Core->RotSpeed.y = RotDelta[1] + Core->RotSpeed.y;
    Core->RotSpeed.z = RotDelta[2] + Core->RotSpeed.z;
}
