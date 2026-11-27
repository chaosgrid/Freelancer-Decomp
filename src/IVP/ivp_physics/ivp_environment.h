// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ivp_anomaly_manager.h"
#include "ivp_time_event.h"
#include "../ivp_utility/ivp_u_memory.h"
#include "../ivp_utility/ivp_u_vector.h"

// Forward declarations
class IVP_Real_Object;
class IVP_Collision_Filter;
class IVP_Environment_Manager; // defined below IVP_Environment
class IVP_Material_Manager; // ivp_material.h
class IVP_U_Active_Value_Manager; // ivp_active_value.h
class IVP_Cluster;                 // ivp_real_object.h
class IVP_OV_Tree_Manager;         // ivp_ov_element.h
class IVP_Cluster_Manager;         // ivp_cluster_manager.h
class IVP_Universe_Manager;        // ivp_cluster_manager.h
class IVP_Sim_Units_Manager;       // ivp_simulation_unit.h
class IVP_Environment;
class IVP_Collision_Delegator_Root;  // ivp_collision.h
class IVP_Polygon;
class IVP_Ball;
class IVP_SurfaceManager;
struct IVP_RealObjectConstructionTemplate;
struct IVP_Environment_Create_Parms;

// The IVP SDK's IVP_Controller_Manager: 4 bytes holding only the environment
// (operator new(4) in the environment ctor). Its methods are the free
// functions in ivp_controller.h / common_physys.cpp.
class IVP_Controller_Manager
{
public:
    IVP_Environment* environment; // +0x00
};
static_assert(sizeof(IVP_Controller_Manager) == 4, "IVP_Controller_Manager is 4 bytes");

// IVP_Environment+0x94: the IVP SDK's IVP_Cache_Object_Manager. A ring of
// number_of_cache_objects 0x70-byte cache objects (IVP_NarrowPhaseCacheSlot,
// ivp_real_object.h), handed out round robin. No vtable.
struct IVP_NarrowPhaseCacheSlot;
class IVP_Cache_Object_Manager
{
public:
    int32_t number_of_cache_objects;          // +0x00: ctor argument (Create_Parms+0x00); a power of two
    int32_t next_free_index;                  // +0x04: round-robin cursor
    IVP_NarrowPhaseCacheSlot* cache_objects;  // +0x08: calloc(number_of_cache_objects, 0x70)

    explicit IVP_Cache_Object_Manager(int32_t NumberOfCacheObjects); // FUN_0634d5c0
    ~IVP_Cache_Object_Manager();                                      // FUN_0634d5f0
    IVP_NarrowPhaseCacheSlot* get_cache_object(IVP_Real_Object* Object); // FUN_0634d530
};
static_assert(sizeof(void*) != 4 || sizeof(IVP_Cache_Object_Manager) == 0xc, "retail operator new(0xc), :0634c44a");

// IVP_Environment+0x8C: a 0x20-byte object without a vtable, set_time'd once
// per IVP_Environment::simulate_dtime. Port-local name (nothing names it in the
// binary). Raises bWithinInterval while less than Threshold has passed since
// the last time let through (file-static DAT_0658afdc). Nothing in Common.dll
// reads the flag, Field_0x00 or the two vectors.
class IVP_Env_Interval_Gate
{
public:
    int32_t Field_0x00;           // +0x00: 1 (ctor). No reader found.
    IVP_U_Vector<void> VectorA;   // +0x04: emptied by the ctor, cleared by the dtor; never filled, element type unknown.
    IVP_U_Vector<void> VectorB;   // +0x0C: same.
    float LastTime;               // +0x14: set_time's argument. Not written by the ctor.
    int32_t bWithinInterval;      // +0x18: 1 (ctor), set by set_time
    float Threshold;              // +0x1C: 1.0f (ctor)

    IVP_Env_Interval_Gate();  // FUN_063605e0
    ~IVP_Env_Interval_Gate(); // FUN_06360640 (the two member vector clears)
    void set_time(float Time); // FUN_063605a0
};
static_assert(sizeof(void*) != 4 || sizeof(IVP_Env_Interval_Gate) == 0x20, "retail operator new(0x20), :0634c397");

#include "ivp_core.h"
#include "ivp_controller.h" // IVP_Controller_Independent (IVP_Standard_Gravity_Controller's base)
#include "ivp_listener.h"   // IVP_PerformanceCounter (IVP_PerformanceCounter_Simple's base), IVP_Collision_Filter

// Retail's generic 0x14-byte fixed-slot priority pool (FUN_06362ef0/f70/f90,
// FUN_06363280), defined in ivp_real_object.h.
class IVP_Priority_Pool;

#pragma pack(push, 4)

// Every payload of IVP_Sim_Unit_Manager's event pool is an IVP_Time_Event
// (ivp_time_event.h): the pool slot lives in its +0x04 handle and the event
// loops dispatch slot 0 on it.
class IVP_Sim_Unit_Manager;

// IVP_Time_Event_PSI: the 8-byte event at IVP_Sim_Unit_Manager+0x0C, vtable
// 063a574c, queued with key 0 by FUN_063608a0. Its simulate_time_event
// (LAB_06360ad0) is the PSI advance.
class IVP_Time_Event_PSI : public IVP_Time_Event
{
public:
    // LAB_06360ad0
    void simulate_time_event(IVP_Environment* Environment) override;
};
static_assert(sizeof(IVP_Time_Event_PSI) == 0x08, "retail operator new(8), :06360915");

// IVP_Event_Manager: the 8-byte strategy object at IVP_Sim_Unit_Manager+0x04,
// vtable 063a558c (two slots, no destructor slot: freed with a bare operator
// delete by FUN_06360960).
class IVP_Event_Manager
{
public:
    // +0x04 (library name: mode). Both event loops stop after an event when it
    // is 1; nothing in Common.dll writes it after the ctor.
    int32_t Mode;

    IVP_Event_Manager() : Mode(0) {}

    // Slot 0, 063606d0: run every queued event due up to (UntilHi, UntilLo),
    // then set the environment's time to it.
    virtual void simulate_time_events(IVP_Sim_Unit_Manager* TimeManager, IVP_Environment* Environment,
                                      float UntilHi, float UntilLo);

    // Slot 1, 063607c0: run events until the PSI event comes up for the second
    // time, re-arming the PSI step to DeltaTime on the first.
    virtual void simulate_variable_time_step(IVP_Sim_Unit_Manager* TimeManager, IVP_Environment* Environment,
                                             IVP_Time_Event_PSI* PsiEvent, float DeltaTime);
};
static_assert(sizeof(IVP_Event_Manager) == 0x08, "retail operator new(8), :063608c7");

// ============================================================================
// IVP_Sim_Unit_Manager: the 0x1C-byte object at IVP_Environment+0x04, the
// environment's time-ordered event queue. Pool keys are seconds from the
// split-precision time base at +0x14/+0x18; the PSI advance (LAB_06360ad0)
// subtracts the elapsed time from every key and moves the base to the
// environment's previous-PSI stamp, hence the enqueue key formula below.
// ============================================================================
class IVP_Sim_Unit_Manager
{
public:
    uint32_t Field_0x00;            // +0x00: zeroed by the ctor; unidentified.
    IVP_Event_Manager* EventManager; // +0x04: vtable 063a558c
    IVP_Priority_Pool* EventPool;   // +0x08: new(0x14) + FUN_06362ef0(p, 0x10)
    IVP_Time_Event_PSI* PsiEvent;   // +0x0C: vtable 063a574c
    // +0x10: key of the event being dispatched (both loops store the pool's
    // minimum key before removing it); the PSI advance resets it to 0. No
    // reader found.
    float LastEventRelTime;               // +0x10
    float TimeBaseHi;               // +0x14
    float TimeBaseLo;               // +0x18

    // ((EventHi - TimeBaseHi) + EventLo) - TimeBaseLo, in this association
    // order, as FUN_063609c0 and IVP_Ledge_Mindist::RescheduleCollisionEvent's inlined copy
    // compute it. Stays on the x87 stack until one float store, hence double.
    float MakeEventKey(float EventHi, float EventLo) const
    {
        return (float)((((double)EventHi - TimeBaseHi) + EventLo) - TimeBaseLo);
    }

    // The enqueue inlined in IVP_Ledge_Mindist::RescheduleCollisionEvent (0634f964). Returns
    // the pool slot; the caller stores it in the payload's +0x04 handle.
    uint16_t EnqueueEvent(void* Payload, float EventHi, float EventLo);

    // FUN_063609f0. Retail takes the payload and reads the handle from it; the
    // port takes the handle, which both call sites already hold.
    void CancelEvent(int32_t Handle);

    // FUN_063609c0: EnqueueEvent plus the write-back into the listener's handle.
    void ScheduleListener(IVP_Time_Event* Listener, float EventHi, float EventLo);

    // FUN_06360ab0: forwards to IVP_Environment::set_current_time (FUN_0634ccd0);
    // `this` is not read.
    void env_set_current_time(IVP_Environment* Environment, float TimeHi, float TimeLo);

    // FUN_06360a40: clamp DeltaTime into [0.005, 0.1] and hand it to
    // EventManager slot 1 together with PsiEvent, under 64-bit x87 precision.
    void simulate_variable_time_step(IVP_Environment* Environment, float DeltaTime);

    // FUN_06360a10: remove the minimum-key entry and return its payload; null
    // on an empty pool.
    void* PopMinEventPayload();

    // FUN_06360960: the manager's destructor body, called by ~IVP_Environment.
    // Does not free the manager itself.
    void DestroyContents();
};
static_assert(sizeof(IVP_Sim_Unit_Manager) == 0x1c, "IVP_Sim_Unit_Manager is 0x1c bytes (FUN_063608a0 zeroes 7 dwords)");
static_assert(offsetof(IVP_Sim_Unit_Manager, EventManager) == 0x04, "IVP_Sim_Unit_Manager+0x04");
static_assert(offsetof(IVP_Sim_Unit_Manager, EventPool) == 0x08, "IVP_Sim_Unit_Manager+0x08");
static_assert(offsetof(IVP_Sim_Unit_Manager, PsiEvent) == 0x0c, "IVP_Sim_Unit_Manager+0x0c");
static_assert(offsetof(IVP_Sim_Unit_Manager, LastEventRelTime) == 0x10, "IVP_Sim_Unit_Manager+0x10");
static_assert(offsetof(IVP_Sim_Unit_Manager, TimeBaseHi) == 0x14, "IVP_Sim_Unit_Manager+0x14");
static_assert(offsetof(IVP_Sim_Unit_Manager, TimeBaseLo) == 0x18, "IVP_Sim_Unit_Manager+0x18");

// ivp_ov_element.h: the 0x18-byte object at IVP_Environment+0x10.
class IVP_Mindist_Manager;

// ============================================================================
// IVP_Standard_Gravity_Controller: IVP_Environment+0x00 (library name).
//   Built inline by the environment ctor: operator new(0x14), vptr 063a5570.
//   vtable 063a5570, 7 slots = IVP_Controller's layout (ivp_controller.h):
//     +0x00 062ed730 `RET 4`        core_is_going_to_be_deleted_event (base default)
//     +0x04 0627cd20 `FLD 1.0; RET` (base default)
//     +0x08 06285a60 get_associated_controlled_cores (IVP_Controller_Independent's)
//     +0x0c 0628f050 `RET 8`        (base default)
//     +0x10 06349530 do_simulation_controller            (this class)
//     +0x14 0634c6c0 get_controller_priority = 1000      (this class)
//     +0x18 0634b6b0 IVP_Controller's own scalar deleting destructor (no dtor here)
//   Every IVP_Core registers with it (FUN_06347660 -> FUN_06349180).
// ============================================================================
class IVP_Standard_Gravity_Controller : public IVP_Controller_Independent
{
public:
    // Writes only the vptr; +0x04..+0x13 stay as allocated until
    // set_grav_vector.
    IVP_Standard_Gravity_Controller() {}

    // FUN_06349510: grav_vec.hesse (+0x10) is not touched.
    void set_grav_vector(const IVP_U_Point* Gravity);

    // LAB_06349530 (slot 4).
    void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) override;

    // LAB_0634c6c0 (slot 5): 1000.
    IVP_CONTROLLER_PRIORITY get_controller_priority() override;

    IVP_U_Point grav_vec; // +0x04 (x, y, z; hesse at +0x10 never written)
};
static_assert(sizeof(IVP_Standard_Gravity_Controller) == 0x14, "retail operator new(0x14), FUN_0634bf50");

// ============================================================================
// IVP_Range_Manager: IVP_Environment+0x1c, 0x34 bytes (library name). Built by
// the env ctor when the create parms carry none.
// vtable 063a5738, 3 slots (063a5744 is the float 0.18f, not a slot):
//   +0x00 063603e0 get_coll_range_intra_objects (RET 0x10)
//   +0x04 06360500 get_coll_range_in_world      (RET 4, float in ST0)
//   +0x08 063603c0 environment_will_be_deleted  (RET 4)
// No destructor slot: slot 2 frees with a bare operator delete.
// The ten floats are the RMGR_LOOK_AHEAD_* tuning PhySys writes after creating
// the environment (0628c9af-0628ca0c): +0x0c..+0x1c the _INTRA five,
// +0x20..+0x30 the _WORLD five, each in the order TIME, MAX_RADIUS,
// MIN_DISTANCE, MAX_DISTANCE, MIN_SECONDS.
// ============================================================================
class IVP_Range_Manager
{
public:
    // FUN_06360360
    IVP_Range_Manager(IVP_Environment* Environment, int32_t bInDeleteOnEnvDelete);

    // LAB_063603e0 (slot 0).
    virtual void get_coll_range_intra_objects(const IVP_Real_Object* Object0, const IVP_Real_Object* Object1,
                                              float* Range0, float* Range1);

    // FUN_06360500 (slot 1). Returns the x87 value unrounded (double here).
    virtual double get_coll_range_in_world(const IVP_Real_Object* Object);

    // LAB_063603c0 (slot 2): `if (+0x04) operator delete(this)`.
    virtual void environment_will_be_deleted(IVP_Environment* Environment);

    int32_t          bDeleteOnEnvDelete;              // +0x04 (ctor's second argument)
    IVP_Environment* environment;                     // +0x08 (ctor's first argument)
    float            look_ahead_time_intra;           // +0x0c  default 0.5f
    float            look_ahead_max_radius_intra;     // +0x10  default 0.9f
    float            look_ahead_min_distance_intra;   // +0x14  default 0.8f
    float            look_ahead_max_distance_intra;   // +0x18  default 10.0f
    float            look_ahead_min_seconds_intra;    // +0x1c  default 0.1f
    float            look_ahead_time_world;           // +0x20  default 1.0f
    float            look_ahead_max_radius_world;     // +0x24  default 5.0f
    float            look_ahead_min_distance_world;   // +0x28  default 0.5f
    float            look_ahead_max_distance_world;   // +0x2c  default 15.0f
    float            look_ahead_min_seconds_world;    // +0x30  default 0.1f
};
static_assert(sizeof(IVP_Range_Manager) == 0x34, "retail operator new(0x34), FUN_0634bf50");

// ============================================================================
// IVP_PerformanceCounter_Simple: the default IVP_Environment+0x28 object,
// 0xa8 bytes (library name). Built by the env ctor when the create parms carry
// none. vtable 063a5714, 6 slots in IVP_PerformanceCounter's order
// (ivp_listener.h):
//   +0x00 06360350 start_pcount: current element = 0
//   +0x04 063602e0 pcount(element)
//   +0x08 062a8920 stop_pcount: `RET` (ICF-shared)
//   +0x0c 06360280 environment_is_going_to_be_deleted: delete this
//   +0x10 06360110 reset_and_print_performance_counters(IVP_Time)
//   +0x14 063602c0 deleting destructor (FUN_06360290 re-stamps the base 0639bac8)
// ============================================================================
class IVP_PerformanceCounter_Simple : public IVP_PerformanceCounter
{
public:
    // FUN_063602a0: zeroes everything after the vptr.
    IVP_PerformanceCounter_Simple();

    void start_pcount() override;
    void pcount(int32_t Element) override;
    void stop_pcount() override {}
    void environment_is_going_to_be_deleted(IVP_Environment* Env) override;
    void reset_and_print_performance_counters(float TimeHi, float TimeLo) override;
    ~IVP_PerformanceCounter_Simple() override {}

    struct FCounter
    {
        int32_t Microseconds; // +0x00: accumulated by pcount
        int32_t Field_0x04;   // +0x04: only ever zeroed; no reader found.
    };

    int32_t  Field_0x04;       // +0x04: only ever zeroed; no reader found.
    uint32_t LastCounterLow;   // +0x08: QueryPerformanceCounter low dword of the previous pcount
    int32_t  LastCounterHigh;  // +0x0c: its high dword (written, never read back)
    int32_t  CurrentElement;   // +0x10: element the next pcount charges the elapsed time to
    int32_t  CountPSIs;        // +0x14: pcount(1) calls since the last reset
    FCounter Counters[17];     // +0x18..+0x9f
    IVP_Time TimeOfLastReset;  // +0xa0: reset_and_print_performance_counters's argument
};
static_assert(sizeof(IVP_PerformanceCounter_Simple) == 0xa8, "retail operator new(0xa8), FUN_0634bf50");

// A node of IVP_Environment::buffer_node_list (+0x13C). Only the members the
// teardown reads are known. No code in Common.dll allocates or links a node, so
// its size is unknown.
struct IVP_Env_Buffer_Node
{
    IVP_Env_Buffer_Node* Next;        // +0x00
    uint8_t              Unread04[0x24];
    void*                OwnedBuffer; // +0x28 (malloc'd; freed with free)

    ~IVP_Env_Buffer_Node();           // FUN_0634cd00
};
static_assert(offsetof(IVP_Env_Buffer_Node, OwnedBuffer) == 0x28, "IVP_Env_Buffer_Node+0x28");

// IVP_Environment (0x148 / 328 bytes)
class IVP_Environment
{
public:
    // +0x00: vtable 063a5570.
    IVP_Standard_Gravity_Controller* standard_gravity_controller; // +0x00
    // +0x04: size 0x1c, ctor FUN_063608a0 (IVP_SimUnitManager_Construct).
    IVP_Sim_Unit_Manager* sim_unit_manager;      // +0x04
    // +0x08: operator new(0x1a8) + ctor 06349380 (ivp_simulation_unit.h).
    IVP_Sim_Units_Manager* sim_units_manager;     // +0x08
    // +0x0C: operator new(0x18) + ctor 0634ad70 (ivp_cluster_manager.h). Not
    // the IVP SDK's controller manager, which is the 4-byte object at +0x90.
    IVP_Cluster_Manager*  cluster_manager;       // +0x0C
    // +0x10: operator new(0x18) + ctor 06350390, dtor 063503c0 (ivp_ov_element.h).
    IVP_Mindist_Manager*  mindist_manager;       // +0x10
    // +0x14: the object-volume tree; operator new(0x17c) + ctor 0635c410,
    // dtor 0635c500 (ivp_ov_element.h).
    IVP_OV_Tree_Manager*  ov_tree_manager;       // +0x14
    // +0x18: IVP's collision filter (stock IVP_Collision_Filter_Coll_Group_Ident,
    // vtable 0639c3fc, ctor FUN_0634bcb0(this, 1)). Slot 0 (FUN_0634bc60)
    // returns 1 when either object's 8-byte coll_group_ident (+0x84) is empty,
    // else strncmp(a, b, 8) != 0, so objects sharing a non-empty group never
    // collide. Slot 1 deletes it when the +0x04 owned flag is set. The game
    // installs its own filter with the same vtable (0628ec11, 0629061d), hence
    // adopt-or-construct. Last gate of IVP_Mindist_Manager::RecheckOvElement.
    IVP_Collision_Filter* collision_filter;     // +0x18. Size 0x8. Adopted from
                                                  // Parms->CollisionFilter if non-null.
    IVP_Range_Manager*    range_manager;         // +0x1C. Adopted from
                                                  // Parms->RangeManager (+0x2c),
                                                  // else new
                                                  // IVP_Range_Manager(this, 1).
    // +0x20/+0x24: see ivp_anomaly_manager.h. Callers index these directly
    // from Core->Environment (e.g. 06357304).
    IVP_Anomaly_Manager*  anomaly_manager;       // +0x20. Size 0x8, ctor
                                                  // FUN_06360eb0(this,1). Adopted
                                                  // from Parms if non-null.
    IVP_Anomaly_Limits*   anomaly_limits;        // +0x24. Size 0x14, ctor
                                                  // FUN_06360e30(this,1). Adopted
                                                  // from Parms if non-null.
    IVP_PerformanceCounter* performance_counter; // +0x28. Adopted from
                                                  // Parms->PerformanceCounter
                                                  // (+0x18), else new
                                                  // IVP_PerformanceCounter_Simple.
    // +0x2C: the caller-supplied IVP_Universe_Manager (ivp_cluster_manager.h),
    // a plain copy of Create_Parms+0x14, never constructed or owned here.
    // Users: simulate_psi (check_for_unused_objects),
    // IVP_Cluster_Manager::remove_object 0634b3e0 (slot 2), the OV element
    // re-file 0634ec9a (slot 0).
    IVP_Universe_Manager* universe_manager;      // +0x2C
    IVP_Real_Object*      static_object;         // +0x30. A 0xa8-byte IVP_Ball
                                                  // built via FUN_0634cdf0 ->
                                                  // FUN_06362570.
    // +0x34..+0x87: statistics, zero-filled by FUN_063505f0. The *_count
    // dwords are only ever incremented (no reader in Common.dll); each is named
    // after its one increment site.
    IVP_Environment*      statistics_owner;      // +0x34: back-pointer to this environment, set by the ctor
    char                  manager_context_38[0x14]; // +0x38..+0x4B
    int32_t               impact_solver_count;   // +0x4C: ++ per IVP_Impact_Solver::DoImpactSystem (06357918)
    int32_t               impact_loop_count;     // +0x50: += the impacts one impact system ran (Count + 1, 06359067)
    char                  manager_context_54[0x08]; // +0x54..+0x5B
    int32_t               impact_deferred_commit_count; // +0x5C: ++ on FinalizeImpact's deferred-commit path (06358204)
    int32_t               impact_priority_calc_count;   // +0x60: ++ per contact whose priority test_loop_all_pairs recomputes (0635921f)
    int32_t               impact_flagged_core_count;    // +0x64: ++ when DoImpactSystem leaves a core with MovementState bits 8-15 set (06357f14)
    // +0x68: += the energy each contact-axis impulse consumes
    // (IVP_ListManager_CorePairEntry::TickBudget, ivp_listmanager_topology.cpp); no reader.
    float                 contact_axis_energy_sum; // +0x68
    // +0x6c/+0x70/+0x74: the library's mindist statistics. The IVP_Mindist
    // ctor (FUN_0634dd10) increments +0x6c/+0x70; its dtor (FUN_0634dd60)
    // decrements +0x6c and increments +0x74.
    int32_t               sum_of_mindists;       // +0x6C: live mindists
    int32_t               mindists_generated;    // +0x70
    int32_t               mindists_deleted;      // +0x74
    int32_t               range_intra_exceeded;  // +0x78: ++ in 06369a10 / 06362d90 before get_coll_range_intra_objects
    int32_t               ov_element_recheck_count; // +0x7C: ++ per IVP_Mindist_Manager::RecheckOvElement, after the element leaves the OV tree (0634ec49)
    int32_t               long_term_solver_count;   // +0x80: ++ per IVP_Impact_Solver_Long_Term FUN_0635a1e0 allocates (0635a220)
    char                  manager_context_84[0x04]; // +0x84..+0x87
    // +0x88: the library's freeze manager (one float, ctor FUN_0634bf30 ->
    // FUN_0634bf40). IVP_Core::classify_motion_state (FUN_063478f0) compares
    // the time since a core last moved against it (063479af).
    float                 freeze_check_dtime;    // +0x88 (0.3f)
    IVP_Env_Interval_Gate* interval_gate;        // +0x8C: new(0x20) + 063605e0
    // +0x90: the IVP SDK's IVP_Controller_Manager (4 bytes holding only the
    // environment); the `this` of 06349040 / 06348fe0 (PhySys 06285ce6).
    IVP_Controller_Manager* controller_manager;  // +0x90
    // +0x94: new(0xc) + 0634d5c0(Create_Parms+0x00).
    IVP_Cache_Object_Manager* cache_object_manager;
    // +0x98: vtable 063a57ac, ctor FUN_063615a0, size 0x28 (ivp_active_value.h).
    // Adopted from Parms->0x28 if non-null.
    IVP_U_Active_Value_Manager* l_active_value_manager;
    // +0x9C: vtable 063a551c, ctor FUN_0634bd80, size 8 (ivp_material.h).
    // Adopted from Parms->0xc if non-null.
    IVP_Material_Manager* l_material_manager;
    // +0xA0/+0xA4: two IVP_U_Memory arenas over the halves of the caller's
    // Parms->MemoryBlock (see ivp_u_memory.h), ctor FUN_06344120. +0xA0 is the
    // per-PSI scratch of the friction solver and IVP_MergeRedundantContacts;
    // +0xA4 holds the cores' sync records and the sim-unit / mindist
    // transactions (State++ ... reset_chunks).
    IVP_U_Memory*         short_term_mem;        // +0xA0: first half
    IVP_U_Memory*         sim_unit_mem;          // +0xA4: second half
    float                 field_a8;              // +0xA8
    float                 delta_time;            // +0xAC. delta_PSI_time,
                                                  // default 1/66 s; set with
                                                  // inv_delta_time by
                                                  // set_delta_PSI_time
                                                  // (FUN_0634c6d0).
    float                 inv_delta_time;        // +0xB0. 1.0f / delta_time.
    // +0xB4..+0xBC: the gravity vector; default {0, 9.83f, 0} (0x411d47ae,
    // not 9.81f).
    float                 gravity_x;             // +0xB4 (0)
    float                 gravity_y;             // +0xB8 (9.83f)
    float                 gravity_z;             // +0xBC (0)
    float                 field_c0;              // +0xC0
    float                 gravity_length;        // +0xC4: |gravity|, set by
                                                  // FUN_0634cb20.
    IVP_U_Vector<IVP_Listener_Collision> collision_listeners; // +0xC8
    IVP_U_Vector<IVP_Listener_PSI> psi_listeners;             // +0xD0
    IVP_U_Vector<IVP_Core> event_queue;                       // +0xD8 cores queued for revival
    IVP_U_Vector<void>    vector_e0;                          // +0xE0 constructed and cleared only
    char*                 environment_name;      // +0xE8. strdup of the name
                                                  // (FUN_06356920), or null.
    void*                 client_data;           // +0xEC. Plain copy of the
                                                  // ctor's param_4.
    // +0xF0: the literal 10 (EDI is reloaded with 10 after the SAR of the
    // memory-block size, so it is not a half-count). No reader of
    // IVP_Environment+0xF0 found in Common.dll; meaning unknown.
    int32_t               field_f0;              // +0xF0. Literal 10.
    // +0xF4: always the IVP_Environment_Manager singleton &DAT_0658aa00. The
    // ctor appends `this` to its Environments vector and the destructor
    // removes it.
    IVP_Environment_Manager* environment_manager; // +0xF4
    // +0xF8..+0x107: two split-precision float timestamps. Consumers
    // difference them as `(hi2 - hi1) + (lo2 - lo1)` (FUN_0634f320 for the
    // simulation horizon; FUN_06350050 and FUN_0635a1e0 for elapsed time).
    // The ctor seeds the second as split(delta_time): the time of the next PSI.
    IVP_Time              current_time;          // +0xF8 (8 bytes: +0xF8 / +0xFC)
    float                 next_psi_time_base;    // +0x100: high half of the second split-precision stamp
    float                 next_psi_time_offset;  // +0x104: low half
    // +0x108/+0x10C: the time of the running PSI. The PSI event copies
    // current_time here and derives next_psi_time from it; the calc-next-PSI
    // solver stamps it into IVP_Core::TimeOfLastPsi (06351c6b).
    IVP_Time              time_of_last_psi;      // +0x108 (8 bytes: +0x108 / +0x10C)
    int32_t               sim_step_count;        // +0x110
    // +0x114: incremented once per do_impact (06358d1a); copied into
    // IVP_Core::MindistEventStamp (06347be7, 063461d5), which
    // recheck_mindists_of_other_cores compares between the two cores.
    int32_t               mindist_event_stamp;   // +0x114
    // +0x118: countdown, in sim-unit PSI steps, to the next movement check.
    // The ctor seeds 10; every unit step decrements it and on 0 reseeds
    // 15 - ftol(rand * -5.0f) and runs the motion/sleep check FUN_063495e0.
    int16_t               next_movement_check;   // +0x118
    int16_t               field_11a;             // +0x11A
    int32_t               sim_substate;          // +0x11C
    // +0x120: pow(0.9f, delta_time), set by the ctor; each tick scales every
    // core pair's IntegratedAntiEnergy by it (FUN_06355cc0).
    float                 integrated_energy_damp; // +0x120
    IVP_U_Vector<IVP_Listener_Object> object_listeners;       // +0x124
    // +0x12C: IVP_Collision_Delegator_Root* elements; the ctor appends
    // Parms->CollisionDelegatorRoot or a new IVP_Collision_Delegator_Root_Mindist
    // (FUN_06360df0).
    IVP_U_Vector<IVP_Collision_Delegator_Root> collision_delegator_roots; // +0x12C
    float                 field_134;             // +0x134
    float                 field_138;             // +0x138
    // +0x13C: singly-linked list freed by the destructor (FUN_0634cd20).
    // Nothing in Common.dll links a node in, so it is empty in practice.
    IVP_Env_Buffer_Node*  buffer_node_list;      // +0x13C
    float                 field_140;             // +0x140
    int32_t               state_magic;           // +0x144 (0x1E240 = 123456)

    // FUN_0634bbe0: drains the whole wake/event queue (ivp_environment.cpp).
    void process_wake_queue();

    // FUN_0634bb20: single-entry removal from the wake queue; used by
    // IVP_Core::~IVP_Core.
    void dequeue_core(IVP_Core* core);

    // FUN_0634cf60: event_pre_collision to every collision_listeners entry whose
    // enabled_callbacks bit 0 is set. Caller: FUN_06358680 (06358aa7).
    void fire_event_pre_collision(IVP_Event_Collision* arg);

    // FUN_0634cf30: object_listeners slot 2 (event_object_revived), backward, no
    // gate; the argument is forwarded untouched.
    void fire_event_object_revived(IVP_Environment_Object_Notify_Event* event_data);

    // FUN_0634caf0: collision_delegator_roots slot 4 (object_is_removed_from_collision_detection),
    // backward. The destructor's own collision_delegator_roots loop calls slot 6 instead.
    void notify_object_removed_from_collision_detection(IVP_Real_Object* arg);

    // FUN_0634cd20: frees each node's buffer inline, deletes the node, then nulls
    // the head. Called from the destructor.
    void free_buffer_node_list();

    // FUN_0634ced0 / FUN_0634cf00: object_listeners slot 0 (event_object_deleted)
    // / slot 3 (event_object_frozen), backward, no gate.
    void fire_event_object_deleted(IVP_Environment_Object_Notify_Event* arg);
    void fire_event_object_frozen(IVP_Environment_Object_Notify_Event* arg);

    // FUN_0634cea0: object_listeners slot 1 (event_object_created), no null test.
    // Called by the IVP_Polygon and IVP_Ball constructors.
    void fire_event_object_created(IVP_Environment_Object_Notify_Event* arg);

    // FUN_0634cfa0 / FUN_0634cfe0: collision_listeners slots 2 / 3, gated on
    // enabled_callbacks bit 2 (&4, not the &1 of fire_event_pre_collision).
    void notify_friction_created(void* arg);
    void notify_friction_deleted(void* arg);

    // FUN_0634ccd0: ++sim_step_count; current_time = {TimeHi, TimeLo}.
    void set_current_time(float TimeHi, float TimeLo);

    // FUN_0634cb90: IVP_Environment::get_root_cluster, cluster_manager->+0x00 (via
    // the ICF-shared getter 06261d90). Every IVP object is created into it.
    IVP_Cluster* get_root_cluster();

    // FUN_0634d090 (RET 8: the IVP_Time by value, never read): one PSI step.
    void simulate_psi(IVP_Time /*current_sim_time*/);

    // FUN_0634d020: event_PSI to every psi_listeners entry, last to first.
    void fire_event_PSI();

    // FUN_0634bb90: the IVP SDK's add_revive_core. Appends `core` to event_queue
    // unless (core->Flags & 0x30) == 0x10, then sets Flags = (Flags & ~0x20) | 0x10.
    // Drained by IVP_Environment::process_wake_queue. Callers: 06346170
    // (IVP_Real_Object::EnsureInSimulation) and 06348f50 below.
    void add_revive_core(IVP_Core* core);

    // FUN_0634c6f0: ~IVP_Environment().
    void destroy();

    // FUN_0634ce60: append onto object_listeners.
    void add_listener_object(void* listener);

    // FUN_0634d050: append onto collision_listeners.
    void add_listener_collision(void* listener);

    // FUN_0634c6d0: IVP_Environment::set_delta_PSI_time. Callers: the env ctor
    // and FUN_0628c900 (PhySys).
    void set_delta_PSI_time(float dt);

    // FUN_0634cc80: IVP_Environment::simulate_dtime. Feeds the target time to the
    // interval gate (+0x8c), then runs the variable time step. PhySys calls it
    // from its Update (0628caed / 0628cb63 / 0628cb9d).
    void simulate_dtime(float DeltaTime);

    // FUN_0634cb20: IVP_Environment::set_gravity: stores the vector and its length
    // (+0xc4), then standard_gravity_controller->set_grav_vector. Callers: the env
    // ctor and PhySys (0628ca2f).
    void set_gravity(const IVP_U_Point* Gravity);

    // FUN_0634cd80: IVP_Environment::create_polygon: new(0xa8) + FUN_06362280.
    // Callers: PhySys 0628af7e / 0628b367 / 0628c838.
    IVP_Polygon* create_polygon(IVP_SurfaceManager* SurfaceManager,
                                const IVP_RealObjectConstructionTemplate* Template,
                                const IVP_U_Quat* Rotation, const IVP_U_Point* Position);

    // FUN_0634cdf0: IVP_Environment::create_ball: new(0xa8) + FUN_06362570.
    // BallRadius is the library's IVP_Template_Ball*, of which only the radius at
    // +0 is read. Callers: PhySys 0628c8f0 and the env ctor's static_object.
    IVP_Ball* create_ball(const float* BallRadius,
                          const IVP_RealObjectConstructionTemplate* Template,
                          const IVP_U_Quat* Rotation, const IVP_U_Point* Position);

    // FUN_0634bf50: IVP_Environment's constructor.
    //
    // `Registry` is retail's param_1: always &DAT_0658aa00, the process-wide
    // IVP_Environment_Manager (see environment_manager above). `Parms` is param_2.
    // `Name` is param_3, strdup'd into environment_name. `ClientData` is param_4.
    IVP_Environment* construct(IVP_Environment_Manager* Registry,
                               const IVP_Environment_Create_Parms* Parms, const char* Name,
                               void* ClientData);
};

#pragma pack(pop)

static_assert(sizeof(IVP_Environment) == 0x148, "IVP_Environment size mismatch");
static_assert(offsetof(IVP_Environment, ov_tree_manager) == 0x14, "IVP_Environment+0x14");
static_assert(offsetof(IVP_Environment, anomaly_manager) == 0x20, "IVP_Environment+0x20");
static_assert(offsetof(IVP_Environment, anomaly_limits) == 0x24, "IVP_Environment+0x24");
static_assert(offsetof(IVP_Environment, statistics_owner) == 0x34, "IVP_Environment+0x34");
static_assert(offsetof(IVP_Environment, contact_axis_energy_sum) == 0x68, "IVP_Environment+0x68");
static_assert(offsetof(IVP_Environment, impact_solver_count) == 0x4C, "IVP_Environment+0x4C");
static_assert(offsetof(IVP_Environment, impact_deferred_commit_count) == 0x5C, "IVP_Environment+0x5C");
static_assert(offsetof(IVP_Environment, impact_flagged_core_count) == 0x64, "IVP_Environment+0x64");
static_assert(offsetof(IVP_Environment, ov_element_recheck_count) == 0x7C, "IVP_Environment+0x7C");
static_assert(offsetof(IVP_Environment, long_term_solver_count) == 0x80, "IVP_Environment+0x80");
static_assert(offsetof(IVP_Environment, freeze_check_dtime) == 0x88, "IVP_Environment+0x88");
static_assert(offsetof(IVP_Environment, inv_delta_time) == 0xB0, "IVP_Environment+0xB0");

// FUN_06348f50: the IVP SDK's
// IVP_Controller_Manager::ensure_core_to_be_in_simulation. Callers pass the
// controller manager (+0x90) as `this`, but the body never reads it. Callers:
// the golem setters 063435e4 / 063436eb and CRemotePhysicsSimulation 062923cb /
// 062923f4 / 06292580 (common_physys.cpp).
void IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(IVP_Core* core);

// {Environment*, IVP_Real_Object*} record that FUN_0634b480 builds on its stack
// for the object-listener notifications.
struct IVP_Environment_Object_Notify_Event
{
    IVP_Environment* Env;
    IVP_Real_Object* Object;
};

struct IVP_Event_Collision; // ivp_listener.h


// ============================================================================
// IVP_Environment construction
// ============================================================================

// ----------------------------------------------------------------------------
// IVP_Environment_Create_Parms: port-local name for the 0x30-byte read-only
// descriptor the constructor takes. A non-null sub-manager is adopted as-is
// (not owned); a null one makes the environment construct its own.
// ----------------------------------------------------------------------------
struct IVP_Environment_Create_Parms
{
    int32_t  NumberOfCacheObjects; // +0x00 -> IVP_Cache_Object_Manager ctor argument.
    char*    MemoryBlock;        // +0x04 -> split in half across the two IVP_U_Memory arenas.
    int32_t  MemoryBlockSize;    // +0x08 -> `SAR 1` gives each arena's half.
    IVP_Material_Manager* MaterialManager; // +0x0C -> +0x9C, else new IVP_Material_Manager(1).
    IVP_Collision_Filter* CollisionFilter; // +0x10 -> +0x18, else new IVP_Collision_Filter_Coll_Group_Ident(1).
    IVP_Universe_Manager* UniverseManager; // +0x14 -> +0x2C verbatim, never constructed here.
    IVP_PerformanceCounter* PerformanceCounter; // +0x18 -> +0x28, else new IVP_PerformanceCounter_Simple.
    void*    AnomalyManager;     // +0x1C -> +0x20, else FUN_06360eb0.
    void*    AnomalyLimits;      // +0x20 -> +0x24, else FUN_06360e30.
    void*    CollisionDelegatorRoot;   // +0x24 -> appended to collision_delegator_roots, else FUN_06360df0.
    IVP_U_Active_Value_Manager* ActiveValueManager; // +0x28 -> +0x98, else new IVP_U_Active_Value_Manager(1).
    IVP_Range_Manager* RangeManager; // +0x2C -> +0x1C, else new IVP_Range_Manager(env, 1).

    // FUN_06350610. PhySys's world init builds one on its stack (0628c92c).
    IVP_Environment_Create_Parms()
    {
        std::memset(this, 0, sizeof(*this));
        NumberOfCacheObjects = 0x100;
    }
};
static_assert(sizeof(IVP_Environment_Create_Parms) == 0x30, "IVP_Environment_Create_Parms size mismatch");
static_assert(offsetof(IVP_Environment_Create_Parms, MemoryBlockSize) == 0x08, "Create_Parms+0x08");
static_assert(offsetof(IVP_Environment_Create_Parms, UniverseManager) == 0x14, "Create_Parms+0x14");
static_assert(offsetof(IVP_Environment_Create_Parms, RangeManager) == 0x2C, "Create_Parms+0x2C");

// FUN_063505f0: zero-fills the 0x54-byte sub-block at IVP_Environment+0x34.
// Runs as a member ctor before the constructor body wipes the same bytes.
void* IVP_Environment_Init_Block34(void* block34);

// FUN_0634bf40: the freeze manager's init, freeze_check_dtime (+0x88) = 0.3f.
void IVP_Freeze_Manager_Init(void* FreezeManager);

// FUN_0634bf30: the +0x88 sub-object ctor; returns `this`.
void* IVP_Freeze_Manager_Construct(void* FreezeManager);

// The constructor's default: 0x3c783e10 == 1.0f/66.0f, IVP's stock PSI rate.
extern const float IVP_ENVIRONMENT_DEFAULT_DELTA_PSI_TIME;

// FUN_06360090: once-only global table init called by the constructor right
// after its sibling FUN_06353940
// (IVP_Mindist_Minimize_Solver::init_mms_function_table). The table and its
// handlers live in ivp_ledge_sweep_solver.cpp.
void IVP_InitSimUnitDispatchTable(void);   // FUN_06360090 -> DAT_0658af9c

// FUN_063608a0: constructor of IVP_Environment::sim_unit_manager (+0x04).
void* IVP_SimUnitManager_Construct(void* self);

// ============================================================================
// IVP_Environment_Manager: the 16-byte process-wide singleton DAT_0658aa00.
//   Static init LAB_0634cc20 runs the ctor FUN_0634cb70 and registers
//   LAB_0634cc50 with atexit; that runs FUN_06348be0 once (guard bit 1 of
//   DAT_0658a9f8), the IVP_U_Vector destructor of +4. A C++ global with these
//   members reproduces both.
//   +0x04 is the environment registry (appended by the env ctor, removed by
//   the destructor).
// ============================================================================
class IVP_Environment_Manager
{
public:
    int32_t Field_0x00;                        // +0x00: zeroed by FUN_0634cb70; no reader found.
    IVP_U_Vector<IVP_Environment> Environments; // +0x04..+0x0b
    int32_t InlineSentinel_0x0c;               // +0x0c: never written; only its address is compared by FUN_06348be0.

    IVP_Environment_Manager() : Field_0x00(0), InlineSentinel_0x0c(0) {}

    // FUN_0634cc70
    static IVP_Environment_Manager* get_environment_manager();

    // FUN_0634cba0: operator new(0x148) + FUN_0634bf50. The license name and key
    // become the environment's name and client data. PhySys's world init
    // (0628c96d) passes "digitalanvil" and 0x11c2de34.
    IVP_Environment* create_environment(IVP_Environment_Create_Parms* Parms,
                                        const char* LicenseName, uint32_t LicenseKey);
};
static_assert(sizeof(IVP_Environment_Manager) == 0x10, "IVP_Environment_Manager is the 16-byte DAT_0658aa00");
