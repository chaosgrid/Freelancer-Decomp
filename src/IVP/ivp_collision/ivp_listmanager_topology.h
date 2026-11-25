// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// IVP_ListManager (the friction system, ivp_mindist.h) topology and the
// contact-axis energy split: Common.dll 0x06355200-0x06356660.
//   * FUN_06355200: IVP_Contact_Point's destructor body (unlink + events).
//   * FUN_063557e0 / FUN_06355890: union-find connectivity and split over
//     RegisteredCores (+0x24) / CorePairEntries (+0x34), the ListManager twins
//     of IVP_Simulation_Unit::find_disconnected_core / split_off_core_group
//     (ivp_simulation_unit.cpp).
//   * FUN_06355c80..FUN_063561c0: per-core-pair energy budget and the
//     restitution/impulse-split math on a 0x6c-byte stack scratch.
// FUN_06355bb0 (reset_time) and FUN_06356510/560/5c0 live in
// ivp_friction_solver.cpp; FUN_06355c10 / FUN_063562d0 in
// ivp_mindist_limiter.cpp.

#include "../ivp_physics/ivp_mindist.h"
#include "../ivp_physics/ivp_core.h"

// IVP_Core_UnionFindGetFather is declared in ivp_core.h; do not redeclare it
// with `extern "C"` (C2732).

// FUN_0635a950: get_contact_point -- the existing record of a dist or a new
// one (*OutTag 0 = found, 1 = created) (ivp_mindist_limiter.cpp).
IVP_Contact_Point* IVP_FindOrCreatePairRecord(IVP_Ledge_Mindist* Mindist, int32_t* OutTag);

// ---------------------------------------------------------------------------
// Scalar restitution / impulse-axis math.
// ---------------------------------------------------------------------------
// The 0x6c-byte scratch FUN_063561c0 builds on its stack (`SUB ESP,0x6c`)
// and passes as `this` to FUN_06355db0 / FUN_06356000 / FUN_06356050: the
// pair's relative linear and spin motion, each with the two sides' masses /
// axis inertias, and the energy in each. Side A is CoreA (+0x60), the
// unmoveable side when there is one. No retail class name is known.
struct IVP_PairContactAxisSetup
{
    float LinearAxis[3];     // +0x00: normalised CoreB.Speed - CoreA.Speed
    float Pad03;             // +0x0c: not written
    float SpinAxisA[3];      // +0x10: normalised world spin delta, in A's frame
    float Pad07;             // +0x1c
    float SpinAxisB[3];      // +0x20: the negated delta, in B's frame
    float Pad0b;             // +0x2c
    float LinearSpeed;       // +0x30: |CoreB.Speed - CoreA.Speed|
    float MassA;             // +0x34
    float MassB;             // +0x38
    float InvMassA;          // +0x3c
    float InvMassB;          // +0x40
    float SpinSpeed;         // +0x44: |world spin delta|
    float AxisInertiaA;      // +0x48: |RotInertia * SpinAxisA| (1.0f when below 1e-10f)
    float AxisInertiaB;      // +0x4c
    float InvAxisInertiaA;   // +0x50
    float InvAxisInertiaB;   // +0x54
    float SpinEnergy;        // +0x58
    float LinearEnergy;      // +0x5c
    IVP_Core* CoreA;         // +0x60
    IVP_Core* CoreB;         // +0x64
    float EnergySum;         // +0x68: SpinEnergy + LinearEnergy

    // FUN_06355db0, thiscall on the scratch. Orders the two cores (a kinematic
    // CoreB is swapped into CoreA), then stores the normalised relative
    // linear speed, the normalised relative world spin taken back into each
    // core's frame, the inertia-weighted lengths with their inverses, and the
    // masses / inverse masses.
    void Populate(IVP_Core* CoreA, IVP_Core* CoreB);

    // FUN_06356000. Two IVP_SolveContactAxisEnergySplit calls (spin, then linear)
    // into SpinEnergy / LinearEnergy, their sum into EnergySum.
    void ComputeEnergies();

    // FUN_06356050. Adds the two roots' impulses onto both cores' SpeedChange
    // (+0x84) / RotSpeedChange (+0x74); only CoreA is gated on kinematic flags.
    void ApplyImpulses(float Scale);
};
static_assert(sizeof(IVP_PairContactAxisSetup) == 0x6C, "IVP_PairContactAxisSetup must match retail's 0x6C-byte scratch extent (FUN_063561c0's own SUB ESP,0x6c)");
static_assert(offsetof(IVP_PairContactAxisSetup, LinearSpeed) == 0x30, "IVP_PairContactAxisSetup+0x30");
static_assert(offsetof(IVP_PairContactAxisSetup, CoreA) == 0x60, "IVP_PairContactAxisSetup+0x60");
static_assert(offsetof(IVP_PairContactAxisSetup, EnergySum) == 0x68, "IVP_PairContactAxisSetup+0x68");

// FUN_06355d20. (A - sqrt(|A*A - (D+D)*(B+C)|)) * (1/(B+C)), left on the x87
// stack (double here); both callers spill it as float.
double IVP_SolveContactAxisRoot(float A, float B, float C, float D);

// FUN_06355d60, cdecl. With t = A/(D+E) and X = A - t*E:
// 0.5f * ((A*A*C - (t*D)^2*B) - float(X*X*C)), left on the x87 stack.
double IVP_SolveContactAxisEnergySplit(float A, float B, float C, float D, float E);

// FUN_06358bb0: the two sides' IVP_Material* -- the object's
// material, or the environment material manager's get_material_by_index for
// a tagged feature. Implemented in ivp_mindist_limiter.cpp.
class IVP_Material;
