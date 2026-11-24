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
//     of IVP_Simulation_Unit_Find_Disconnected_Core / Split_Off_Core_Group
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

// FUN_06355200. IVP_Contact_Point's destructor body: releases both ledges
// through the surface managers, fires the friction-deleted events and unlinks
// the record from both objects' mindist_list.
void IVP_UnlinkPairRecordFromObjects(IVP_Contact_Point* Record);

// FUN_063557e0. Union-find connectivity check over Manager->RegisteredCores
// via Manager->CorePairEntries. Returns a core whose master differs from the
// reference master, or nullptr if all registered cores are one component.
IVP_Core* IVP_ListManager_FindDisconnectedCore(IVP_ListManager* Manager);

// FUN_06355890. Moves every core whose master is TargetMaster (and any
// further disconnected group left behind) out of Manager into new
// IVP_ListManagers (ctor FUN_0634b600); an underfull manager is deleted.
void IVP_ListManager_SplitOffCoreGroup(IVP_ListManager* Manager, IVP_Core* TargetMaster);

// FUN_0635a1e0: the contact-frame dispatcher of one dist (ivp_mindist_limiter.cpp).
void IVP_UpdatePairRecordContactFrame(IVP_Contact_Point* Candidate);

// FUN_06358c50: read_materials_for_contact_situation -- fills Owner's
// objects/features/materials/elasticity and Candidate's friction factor
// (ivp_mindist_limiter.cpp).
void IVP_PopulatePairRecordIntoOwner(IVP_Contact_Point* Candidate, IVP_Impact_Solver_Long_Term* Owner);

// FUN_0635a950: get_contact_point -- the existing record of a dist or a new
// one (*OutTag 0 = found, 1 = created) (ivp_mindist_limiter.cpp).
IVP_Contact_Point* IVP_FindOrCreatePairRecord(IVP_Ledge_Mindist* Mindist, int32_t* OutTag);

// FUN_06355c10, thiscall on Entry with Manager on the stack (the caller
// passes its own [EDI+0x20]). For each attached record: FUN_0635a1e0, then
// IVP_PopulatePairRecordIntoOwner; a record whose Owner's packed state is
// 0x100 under mask 0x300 is removed (FUN_06355490) and not counted. Returns
// the live count. Implemented in ivp_mindist_limiter.cpp.
int IVP_ListManager_RefreshAttachedCtxAndCountLive(IVP_ListManager_CorePairEntry* Entry, IVP_ListManager* Manager);

// FUN_06355c80. IVP_ListManager_TickCorePairBudget on every entry.
void IVP_ListManager_TickAllCorePairBudgets(IVP_ListManager* Manager);

// FUN_06355ca0. Clears CorePairEntry+0x24 (IntegratedAntiEnergy, the budget) on every entry.
void IVP_ListManager_ClearAllCorePairBudgets(IVP_ListManager* Manager);

// FUN_06355cc0. Scales Entry->IntegratedAntiEnergy by Environment->integrated_energy_damp; while the
// result is non-negative and neither core has an AxisLock (+0x10), spends
// some through IVP_ComputeAndApplyContactAxisImpulse, adds the amount spent
// to the float at IVP_Environment+0x68 and subtracts it from the budget.
void IVP_ListManager_TickCorePairBudget(IVP_ListManager_CorePairEntry* Entry);

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

// FUN_06355db0, thiscall on the scratch. Orders the two cores (a kinematic
// CoreB is swapped into CoreA), then stores the normalised relative
// linear speed, the normalised relative world spin taken back into each
// core's frame, the inertia-weighted lengths with their inverses, and the
// masses / inverse masses.
void IVP_PopulateContactAxisSetup(IVP_PairContactAxisSetup* Setup, IVP_Core* CoreA, IVP_Core* CoreB);

// FUN_06356000. Two IVP_SolveContactAxisEnergySplit calls (spin, then linear)
// into SpinEnergy / LinearEnergy, their sum into EnergySum.
void IVP_ComputeContactAxisEnergies(IVP_PairContactAxisSetup* Setup);

// FUN_06356050. Adds the two roots' impulses onto both cores' SpeedChange
// (+0x84) / RotSpeedChange (+0x74); only CoreA is gated on kinematic flags.
void IVP_ApplyContactAxisImpulses(IVP_PairContactAxisSetup* Setup, float Scale);

// FUN_063561c0, thiscall on Entry (reads Entry->CoreA/CoreB, +0x28/+0x2c).
// Builds the scratch, clamps Budget to EnergySum * 0.1f (DAT_063a5464) and,
// unless EnergySum < 1e-10f (DAT_063a53d8), applies Budget/EnergySum.
// Returns the amount spent.
float IVP_ComputeAndApplyContactAxisImpulse(IVP_ListManager_CorePairEntry* Entry, float Budget);

// FUN_063562d0. Record->MassFactor = reciprocal two-body reduced mass of
// the two cores (only the other side's effective mass when one core is
// kinematic); SolverMode = 1 when either material has
// SecondFrictionXEnabled (+0x08). Implemented in ivp_mindist_limiter.cpp.
void IVP_ComputePairRecordMassFactor(IVP_Contact_Point* Record);

// FUN_06358bb0: the two sides' IVP_Material* -- the object's
// material, or the environment material manager's get_material_by_index for
// a tagged feature. Implemented in ivp_mindist_limiter.cpp.
class IVP_Material;
void IVP_ResolvePairRecordSideMaterialSlots(IVP_Contact_Point* Candidate, IVP_Material** OutArray);
