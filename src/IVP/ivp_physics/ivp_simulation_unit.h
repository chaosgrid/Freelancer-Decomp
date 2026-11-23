// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ivp_core.h"
#include "../ivp_utility/ivp_u_vector.h"
#include <cstdint>
#include <cstddef>

// ============================================================================
// IVP_Simulation_Unit: a group of cores simulated together, 0x24 bytes
// (operator_new(0x24) in IVP_Core's ctor FUN_06347550).
//   +0x00 State             only the low byte is read (signed < 8: active,
//                           else sleeping); it selects which of the manager's
//                           two lists (+0x14 / +0x1a4) holds the unit.
//   +0x04 Prev / +0x08 Next links within that list.
//   +0x0C Cores             IVP_U_Vector_Fixed<IVP_Core, 2> (inline buffer
//                           +0x14, memsize 2).
//   +0x1C ControllerBuckets IVP_U_Vector with no inline buffer (always
//                           heap-grown).
// ============================================================================

// One bucket per controller attached to cores of a unit, holding those cores.
// operator_new(0x14); its ctor sets Cores to 2 inline slots. FUN_06348be0 is
// the Cores vector's Clear (inline-buffer test elems == this+0xC).
struct IVP_Sim_Unit_Controller_Bucket
{
    IVP_Controller* Controller;            // +0x00: bucket key
    IVP_U_Vector_Fixed<IVP_Core, 2> Cores; // +0x04: cores attached via Controller within this IVP_Simulation_Unit (12 bytes: 8-byte header + 2 inline slots)
};
static_assert(offsetof(IVP_Sim_Unit_Controller_Bucket, Cores) == 0x04, "IVP_Sim_Unit_Controller_Bucket+0x04");
static_assert(sizeof(IVP_Sim_Unit_Controller_Bucket) == 0x14, "IVP_Sim_Unit_Controller_Bucket size must be 0x14 (20 bytes)");

class IVP_Simulation_Unit
{
public:
    uint32_t State;                                            // +0x00
    IVP_Simulation_Unit* Prev;                                 // +0x04
    IVP_Simulation_Unit* Next;                                 // +0x08
    IVP_U_Vector_Fixed<IVP_Core, 2> Cores;                      // +0x0C header, +0x14 inline storage[2]
    IVP_U_Vector<IVP_Sim_Unit_Controller_Bucket> ControllerBuckets;  // +0x1C (no inline storage)

    // FUN_06348e10 (sole caller FUN_06347550). Sets Cores' inline buffer,
    // ControllerBuckets = {0,0,null} and State's low byte to 8. Port
    // deviations: retail ANDs State with 0xffffc008 (keeping the allocation's
    // upper bits) and leaves Prev/Next uninitialized until the unit is first
    // pushed onto a manager list; the port zeroes them. Only the low byte of
    // State and the pushed links are ever read, so this is not observable.
    IVP_Simulation_Unit();
};
static_assert(offsetof(IVP_Simulation_Unit, Prev) == 0x04, "IVP_Simulation_Unit+0x04");
static_assert(offsetof(IVP_Simulation_Unit, Next) == 0x08, "IVP_Simulation_Unit+0x08");
static_assert(offsetof(IVP_Simulation_Unit, Cores) == 0x0C, "IVP_Simulation_Unit+0x0C");
static_assert(offsetof(IVP_Simulation_Unit, ControllerBuckets) == 0x1C, "IVP_Simulation_Unit+0x1C");
static_assert(sizeof(IVP_Simulation_Unit) == 0x24, "IVP_Simulation_Unit size must be 0x24 (36 bytes)");

// ============================================================================
// IVP_Sim_Units_Manager: IVP_Environment+0x08, 0x1a8 bytes, no vtable. Built
// by the environment ctor (operator_new(0x1a8) + FUN_06349380) and released by
// the environment destructor with a bare operator delete (no destructor). Its
// methods file units on the two list heads by State's low byte. Method names
// are the Ipion library's (recalled, not in the binary).
// ============================================================================
class IVP_Sim_Units_Manager
{
public:
    IVP_Environment* environment;         // +0x00
    float Field_0x04;                     // +0x04: 9.0f from the ctor; no reader found.
    float Field_0x08;                     // +0x08: 0x3f3ae140 (~0.72851f); no reader found.
    float Field_0x0c;                     // +0x0c: 0.0f; no reader found.
    float Field_0x10;                     // +0x10: 0.3f; no reader found.
    IVP_Simulation_Unit* sim_units_head;  // +0x14: units with State low byte < 8
    uint8_t Field_0x18[0x18c];            // +0x18..+0x1a3: never written by the ctor; no access found.
    IVP_Simulation_Unit* still_units_head; // +0x1a4: units with State low byte >= 8

    // FUN_06349380. +0x18..+0x1a3 are left as allocated.
    explicit IVP_Sim_Units_Manager(IVP_Environment* Environment);

    // FUN_06349400: push Unit at the front of the list its State selects.
    void add_sim_unit_to_manager(IVP_Simulation_Unit* Unit);

    // FUN_06349460: unlink Unit from the list its State selects. Both pick the
    // list from State at call time, so moving a unit is unlink, change State,
    // push (as FUN_06349490 does).
    void rem_sim_unit_from_manager(IVP_Simulation_Unit* Unit);

    // FUN_063499c0: every active unit's PSI step. Called from
    // IVP_Environment::simulate_psi with its 128-slot core vector, which
    // receives every core of every active unit.
    void simulate_sim_units_psi(IVP_Environment* Environment, IVP_U_Vector<IVP_Core>* CoresWhichNeedCalcNextPsi);
};
static_assert(offsetof(IVP_Sim_Units_Manager, sim_units_head) == 0x14, "IVP_Sim_Units_Manager+0x14");
static_assert(offsetof(IVP_Sim_Units_Manager, still_units_head) == 0x1a4, "IVP_Sim_Units_Manager+0x1a4");
static_assert(sizeof(IVP_Sim_Units_Manager) == 0x1a8, "IVP_Sim_Units_Manager is 0x1a8 bytes (operator_new(0x1a8), 0634c2cf)");

// FUN_06348cb0: detach_core, called from IVP_Core::~IVP_Core (FUN_063477e0)
// when the core's sim unit (+0x15C) is set. Removes Core from the unit and
// from every controller bucket; a unit left without cores is unlinked from
// Core->Environment->sim_units_manager and freed.
void IVP_Simulation_Unit_Detach_Core(IVP_Simulation_Unit* Unit, IVP_Core* Core);

// ============================================================================
// Controller detach / per-unit wake / motion classification
// ============================================================================

// FUN_06349210: removes Core from the bucket keyed by Controller; an emptied
// bucket is deleted and its slot removed. Caller: IVP_Core::detach_controller
// (FUN_06349110).
void IVP_Simulation_Unit_Detach_Core_From_Controller_Bucket(IVP_Simulation_Unit* Unit,
                                                              IVP_Controller* Controller,
                                                              IVP_Core* Core);

// FUN_063491d0: the insertion twin -- ensure a bucket for Controller, append
// Core, re-sort by priority. Caller: FUN_06349180 (IVP_Core's controller add).
void IVP_Simulation_Unit_Register_Core_With_Controller(IVP_Simulation_Unit* Unit,
                                                        IVP_Controller* Controller,
                                                        IVP_Core* Core);

// FUN_063492c0: insertion sort of ControllerBuckets by each controller's
// get_controller_priority() (signed compare).
void IVP_Simulation_Unit_Sort_Controller_Buckets_By_Priority(IVP_Simulation_Unit* Unit);

// FUN_06349350: swaps ControllerBuckets.elems[i] and [j].
void IVP_Simulation_Unit_Swap_Controller_Buckets(IVP_Simulation_Unit* Unit, int i, int j);

// FUN_063494f0: update_time_of_last_motion on every core, backwards. The same
// loop IVP_Core::ensure_in_simulation inlines.
void IVP_Simulation_Unit_Update_Cores_Last_Motion_Time(IVP_Simulation_Unit* Unit);

// FUN_06349490 (ECX = unit, stack = environment): the library's
// sim_unit_revive_for_simulation. IVP_Core::revive_sleeping_core forwards here.
void IVP_Simulation_Unit_Revive_If_Sleeping(IVP_Simulation_Unit* Unit, IVP_Environment* Environment);

// FUN_063495e0: per-PSI motion classification. Each core's
// classify_motion_state result goes into its MovementState low byte; when all
// report 3 ("long idle") every core is frozen (FUN_0634b5e0) and the unit moves
// to the sleeping list with State low byte 8. Returns 1 if it went to sleep.
int IVP_Simulation_Unit_Update_Motion_And_Maybe_Sleep(IVP_Simulation_Unit* Unit, IVP_Environment* Environment);

// FUN_063495c0: clear and rebuild the controller buckets, rebalance (split off
// disconnected core groups), then clear State bits 0x300.
void IVP_Simulation_Unit_Rebalance_And_Clear_Topology_Flag(IVP_Simulation_Unit* Unit);

// FUN_063496a0: the library's simulate_single_sim_unit_psi, called by
// IVP_Sim_Units_Manager::simulate_sim_units_psi.
class IVP_Event_Sim; // ivp_controller.h
void IVP_Simulation_Unit_Process_One_Psi_Step(IVP_Event_Sim* EventSim, IVP_Simulation_Unit* Unit, IVP_U_Vector<IVP_Core>* OutputCoreList);

// ============================================================================
// Controller-bucket build / merge / split (free functions taking the unit)
// ============================================================================

// FUN_06348860: does a bucket keyed by Controller exist? (Retail keeps this
// and FUN_06348c80's index search as separate leaves.)
bool IVP_Simulation_Unit_Has_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller);

// FUN_06348900: new bucket (operator_new(0x14)) keyed by Controller, appended.
void IVP_Simulation_Unit_Create_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller);

// FUN_063488a0: re-finds the bucket for Controller (a second search, as in
// retail) and appends Core. The bucket must exist.
void IVP_Simulation_Unit_Add_Core_To_Controller_Bucket(IVP_Simulation_Unit* Unit, IVP_Controller* Controller, IVP_Core* Core);

// FUN_063487f0: rebuilds the buckets from every core's Controllers, then sorts
// them by priority (FUN_063492c0).
void IVP_Simulation_Unit_Rebuild_Controller_Buckets(IVP_Simulation_Unit* Unit);

// FUN_06348b80: deletes every bucket, then the bucket array; safe at any time,
// not only at teardown.
void IVP_Simulation_Unit_Clear_Controller_Buckets(IVP_Simulation_Unit* Unit);

// FUN_06348d80: the unit's destructor body -- Clear_Controller_Buckets, then
// the ControllerBuckets and Cores buffers. The caller frees the unit
// (06348cb0, 06356eef / 06356f05).
void IVP_Simulation_Unit_Free_Vector_Buffers(IVP_Simulation_Unit* Unit);

// FUN_06348ad0: union-find connectivity check. Clears every core's
// MergedCoreWhichReplacedIt, unions the cores each bucket's controller
// connects (slot 2, get_associated_controlled_cores), and returns the first
// core whose master differs from the first core's, or null when the unit is
// one connected group.
IVP_Core* IVP_Simulation_Unit_Find_Disconnected_Core(IVP_Simulation_Unit* Unit);

// FUN_06348960: moves every core whose master is TargetMaster (and any further
// disconnected group found among the rest) into new units, pushed onto the
// same manager with State 1 ("awake"), each with rebuilt buckets.
void IVP_Simulation_Unit_Split_Off_Core_Group(IVP_Simulation_Unit* Unit, IVP_Core* TargetMaster);

// FUN_06348aa0: when a disconnected group exists, clear the buckets, split it
// off and rebuild.
void IVP_Simulation_Unit_Rebalance(IVP_Simulation_Unit* Unit);

// FUN_06348c10: moves every core of Source into Unit and unlinks Source from
// the manager, reached through the last moved core's environment (an empty
// Source dereferences null, as in retail). Source is not freed here.
void IVP_Simulation_Unit_Absorb_Cores_From(IVP_Simulation_Unit* Unit, IVP_Simulation_Unit* Source);

// FUN_06348c60: merge two units (clear buckets, absorb, rebuild); the inverse
// of Split_Off_Core_Group.
void IVP_Simulation_Unit_Merge_From(IVP_Simulation_Unit* Unit, IVP_Simulation_Unit* Source);
