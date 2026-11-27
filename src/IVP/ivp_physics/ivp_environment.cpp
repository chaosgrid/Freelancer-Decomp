// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_environment.h"
#include "../../Common/Physics/physics_tick_profile.h" // Owner-requested port-only profiling
#include "ivp_core.h"
#include "ivp_anomaly_manager.h"
#include "ivp_material.h"
#include "ivp_active_value.h"
#include "ivp_real_object.h"
#include "ivp_mindist.h" // IVP_Ledge_Mindist / IVP_Synapse_Real (IVP_Anomaly_Manager::inter_penetration)
#include "../ivp_collision/ivp_collision.h" // IVP_Collision_Delegator_Root(_Mindist): collision_delegator_roots elements
#include "../ivp_collision/ivp_mindist_minimize.h" // IVP_Mindist_Minimize_Solver::init_mms_function_table (06353940)
#include "../ivp_collision/ivp_ledge_sweep_solver.h" // IVP_InitSweepEstimatorTable
#include "ivp_listener.h" // IVP_Collision_Filter(_Coll_Group_Ident): collision_filter (+0x18); IVP_Listener_PSI
#include "ivp_ov_element.h" // IVP_Mindist_Manager (mindist_manager, +0x10)
#include "ivp_simulation_unit.h"       // IVP_Sim_Units_Manager (env+0x08; 06349380 / 063499c0)
#include "ivp_calc_next_psi_solver.h"  // IVP_Calc_Next_PSI_Solver::commit_all_* (06351eb0 / 06351f80)
#include "ivp_cluster_manager.h"       // IVP_Cluster_Manager (+0x0C), IVP_Universe_Manager (+0x2C)
#include "../ivp_utility/ivp_math.h"
#include "../ivp_utility/ivp_u_memory.h"
#include "../ivp_utility/ivp_u_vector.h"
#include "../ivp_utility/ivp_message.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

// QueryPerformanceCounter / QueryPerformanceFrequency, which retail's
// IVP_PerformanceCounter_Simple::pcount (063602e0) calls through the IAT.
// Included after the IVP headers so its macros cannot reach them.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>


// ============================================================================
// FUN_0634bbe0: wakes every queued core from the top down, clears its queued
// bits (0x30), then empties event_queue (inline buffer at +0xe0 kept).
// ============================================================================
void IVP_Environment::process_wake_queue()
{
    for (int i = static_cast<int>(event_queue.n_elems) - 1; i >= 0; --i)
    {
        IVP_Core* core = event_queue.elems[i];
        core->wake_or_ensure_simulation(); // FUN_06347d80
        core->Flags &= ~IVP_CORE_IS_IN_WAKEUP_VEC;
    }

    event_queue.Clear();
}

// ============================================================================
// FUN_0634bb20: IVP_Environment::dequeue_core. Removes one core from the wake
// queue (backward search, shift down) and clears its queued bits 0x30; no-op
// when absent. Called by the IVP_Core destructor (FUN_063477e0), which also
// pushes a constant 1 into an unused stack slot.
// ============================================================================
void IVP_Environment::dequeue_core(IVP_Core* core)
{
    IVP_U_Vector<IVP_Core>& Queue = event_queue;

    int FoundIndex = -1;
    for (int i = static_cast<int>(Queue.n_elems) - 1; i >= 0; --i)
    {
        if (Queue.elems[i] == core)
        {
            FoundIndex = i;
            break;
        }
    }

    if (FoundIndex < 0)
    {
        return;
    }

    for (int i = FoundIndex; i < static_cast<int>(Queue.n_elems) - 1; ++i)
    {
        Queue.elems[i] = Queue.elems[i + 1];
    }
    --Queue.n_elems;

    core->Flags &= ~IVP_CORE_IS_IN_WAKEUP_VEC;
}

// ============================================================================
// FUN_0634bb90: IVP_Environment::add_revive_core. (Flags & 0x30) == 0x10 means
// already queued. No null test on either pointer.
// ============================================================================
void IVP_Environment::add_revive_core(IVP_Core* core)
{
    if ((core->Flags & IVP_CORE_IS_IN_WAKEUP_VEC) == 0x10u)
    {
        return;
    }

    event_queue.Add(core);
    core->Flags = (core->Flags & ~0x20u) | 0x10u;
}

// ============================================================================
// FUN_06348f50: IVP_Controller_Manager::ensure_core_to_be_in_simulation. The
// controller manager (`this`) is never read.
// ============================================================================
void IVP_Controller_Manager_Ensure_Core_To_Be_In_Simulation(IVP_Core* core)
{
    core->Environment->add_revive_core(core);
}

// FUN_0634cf60: top-down over the IVP_Listener_Collision vector, elems re-read
// every pass, no null test; enabled_callbacks bit 0 gates event_pre_collision.
void IVP_Environment::fire_event_pre_collision(IVP_Event_Collision* arg)
{
    for (int i = static_cast<int>(collision_listeners.n_elems) - 1; i >= 0; --i)
    {
        IVP_Listener_Collision* Listener = (IVP_Listener_Collision*)collision_listeners.elems[i];
        if ((Listener->enabled_callbacks & IVP_LISTENER_COLLISION_CALLBACK_PRE_COLLISION) != 0)
        {
            Listener->event_pre_collision(arg);
        }
    }
}

// ============================================================================
// FUN_0634cf30: top-down over object_listeners, no null test, no gate, slot 2
// (event_object_revived). Caller: IVP_Core::revive_pass (FUN_0634b480), which
// builds the {Environment*, IVP_Real_Object*} record on its stack.
// ============================================================================
void IVP_Environment::fire_event_object_revived(IVP_Environment_Object_Notify_Event* event_data)
{
    for (int i = static_cast<int>(object_listeners.n_elems) - 1; i >= 0; --i)
    {
        ((IVP_Listener_Object*)object_listeners.elems[i])->event_object_revived(event_data);
    }
}

// ============================================================================
// FUN_0634caf0: top-down over collision_delegator_roots, elems re-read every pass, no gate;
// slot 4 = object_is_removed_from_collision_detection.
// ============================================================================
void IVP_Environment::notify_object_removed_from_collision_detection(IVP_Real_Object* arg)
{
    for (int i = static_cast<int>(collision_delegator_roots.n_elems) - 1; i >= 0; --i)
    {
        IVP_Collision_Delegator_Root* Delegator =
            static_cast<IVP_Collision_Delegator_Root*>(collision_delegator_roots.elems[i]);
        Delegator->object_is_removed_from_collision_detection(arg);
    }
}

// ============================================================================
// FUN_0634cd00
// ============================================================================
IVP_Env_Buffer_Node::~IVP_Env_Buffer_Node()
{
    if (OwnedBuffer != nullptr)
    {
        free(OwnedBuffer);      // FUN_06343fb0
        OwnedBuffer = nullptr;
    }
}

// ============================================================================
// FUN_0634cd20. Each node's +0x28 buffer is freed before
// IVP_Environment_Free_Field13c_Node runs on it (so that helper's own free is
// a no-op here, as in retail), then the node itself is freed.
// ============================================================================
void IVP_Environment::free_buffer_node_list()
{
    IVP_Env_Buffer_Node* Node = buffer_node_list;
    while (Node != nullptr)
    {
        if (Node->OwnedBuffer != nullptr)
        {
            free(Node->OwnedBuffer);                    // FUN_06343fb0
            Node->OwnedBuffer = nullptr;
        }

        IVP_Env_Buffer_Node* const Next = Node->Next;
        delete Node; // ~IVP_Env_Buffer_Node (its free is a no-op now) + operator delete
        Node = Next;
    }
    buffer_node_list = nullptr;
}

// ============================================================================
// FUN_0634ce60
// ============================================================================
void IVP_Environment::add_listener_object(void* listener)
{
    object_listeners.Add(static_cast<IVP_Listener_Object*>(listener));
}

// ============================================================================
// FUN_0634ced0 / FUN_0634cf00: top-down over object_listeners, no null test,
// no gate; slots 0 and 3.
// ============================================================================
void IVP_Environment::fire_event_object_deleted(IVP_Environment_Object_Notify_Event* arg)
{
    for (int i = static_cast<int>(object_listeners.n_elems) - 1; i >= 0; --i)
    {
        ((IVP_Listener_Object*)object_listeners.elems[i])->event_object_deleted(arg);
    }
}

void IVP_Environment::fire_event_object_frozen(IVP_Environment_Object_Notify_Event* arg)
{
    for (int i = static_cast<int>(object_listeners.n_elems) - 1; i >= 0; --i)
    {
        ((IVP_Listener_Object*)object_listeners.elems[i])->event_object_frozen(arg);
    }
}

// Notify each object listener of a newly created object, last to first.
void IVP_Environment::fire_event_object_created(IVP_Environment_Object_Notify_Event* arg)
{
    for (int i = static_cast<int>(object_listeners.n_elems) - 1; i >= 0; --i)
    {
        ((IVP_Listener_Object*)object_listeners.elems[i])->event_object_created(arg);
    }
}

// ============================================================================
// FUN_0634cfa0 / FUN_0634cfe0: like IVP_Environment::fire_event_pre_collision but
// gated on enabled_callbacks bit 2 (&4); slots 2 and 3.
// ============================================================================
void IVP_Environment::notify_friction_created(void* arg)
{
    for (int i = static_cast<int>(collision_listeners.n_elems) - 1; i >= 0; --i)
    {
        IVP_Listener_Collision* Listener = (IVP_Listener_Collision*)collision_listeners.elems[i];
        if ((Listener->enabled_callbacks & IVP_LISTENER_COLLISION_CALLBACK_FRICTION) != 0)
        {
            Listener->event_friction_created(arg);
        }
    }
}

void IVP_Environment::notify_friction_deleted(void* arg)
{
    for (int i = static_cast<int>(collision_listeners.n_elems) - 1; i >= 0; --i)
    {
        IVP_Listener_Collision* Listener = (IVP_Listener_Collision*)collision_listeners.elems[i];
        if ((Listener->enabled_callbacks & IVP_LISTENER_COLLISION_CALLBACK_FRICTION) != 0)
        {
            Listener->event_friction_deleted(arg);
        }
    }
}

// ============================================================================
// FUN_0634d050
// ============================================================================
void IVP_Environment::add_listener_collision(void* listener)
{
    collision_listeners.Add(static_cast<IVP_Listener_Collision*>(listener));
}

// ============================================================================
// IVP_Cache_Object_Manager (+0x94). get_cache_object (0634d530) is in
// ivp_real_object.cpp next to the cache-object refresh it calls.
// ============================================================================

// FUN_0634d5c0. Slots are 0x70 bytes each.
IVP_Cache_Object_Manager::IVP_Cache_Object_Manager(int32_t NumberOfCacheObjects)
{
    number_of_cache_objects = NumberOfCacheObjects;
    cache_objects = static_cast<IVP_NarrowPhaseCacheSlot*>(
        calloc(static_cast<size_t>(NumberOfCacheObjects), sizeof(IVP_NarrowPhaseCacheSlot))); // FUN_06343f80
    next_free_index = 0;
}

// FUN_0634d5f0. n is re-read from the member each pass.
IVP_Cache_Object_Manager::~IVP_Cache_Object_Manager()
{
    for (int32_t I = 0; I < number_of_cache_objects; ++I)
    {
        IVP_Real_Object* const Owner = cache_objects[I].Owner;
        if (Owner != nullptr)
        {
            IVP_Real_Object_Free_Cache_Object(Owner); // FUN_0634d510
        }
    }

    if (cache_objects != nullptr)
    {
        free(cache_objects); // FUN_06343fb0
        cache_objects = nullptr;
    }
}

// ============================================================================
// IVP_Env_Interval_Gate (+0x8C)
// ============================================================================

// DAT_0658afdc: .data, no initializer (zero). Written only by set_time.
static float GIvpIntervalGateLastTime = 0.0f;

// FUN_063605e0. +0x14 (LastTime) is not written.
IVP_Env_Interval_Gate::IVP_Env_Interval_Gate()
{
    Field_0x00 = 1;
    Threshold = 1.0f;
    bWithinInterval = 1;
}

// FUN_06360640: the member vector destructors (VectorB, then VectorA).
IVP_Env_Interval_Gate::~IVP_Env_Interval_Gate()
{
}

// FUN_063605a0. Less-than or unordered sets bWithinInterval; otherwise the
// static DAT_0658afdc takes the new time.
void IVP_Env_Interval_Gate::set_time(float Time)
{
    LastTime = Time;
    if (!((double)(Time) - GIvpIntervalGateLastTime >= Threshold))
    {
        bWithinInterval = 1;
        return;
    }
    GIvpIntervalGateLastTime = Time;
    bWithinInterval = 0;
}

// ============================================================================
// FUN_0634c6f0: ~IVP_Environment. Order follows the disassembly, not the
// Ghidra pseudocode (whose reordering would read already-nulled fields).
// ============================================================================
void IVP_Environment::destroy()
{
    if (state_magic == 0x1e240)
    {
        // PSI listeners, last to first.
        for (int i = static_cast<int>(psi_listeners.n_elems) - 1; i >= 0; --i)
        {
            static_cast<IVP_Listener_PSI*>(psi_listeners.elems[i])->environment_will_be_deleted(this);
        }

        // FUN_06345450: deleting destructor when set.
        IVP_Real_Object* const StaticObject = static_object;
        if (StaticObject != nullptr)
        {
            delete StaticObject;
        }
        static_object = nullptr;

        IVP_Cluster_Manager* const ClusterManager = cluster_manager;
        if (ClusterManager != nullptr)
        {
            delete ClusterManager;
        }
        cluster_manager = nullptr;

        delete standard_gravity_controller; // slot 6, deleting destructor
        standard_gravity_controller = nullptr;

        // Plain operator delete, no null test, no destructor.
        ::operator delete(controller_manager); // FUN_06391d5a
        controller_manager = nullptr;

        IVP_Sim_Unit_Manager* simUnitManager = sim_unit_manager;
        if (simUnitManager != nullptr)
        {
            // Drains and frees every queued event payload, the +0x04 listener
            // and the pool; the caller frees the manager block, as in retail.
            simUnitManager->DestroyContents();   // FUN_06360960
            ::operator delete(simUnitManager);   // FUN_06391d5a (allocated with operator new(0x1c))
        }
        sim_unit_manager = nullptr;

        // Plain operator delete, no null test, no destructor.
        ::operator delete(sim_units_manager); // FUN_06391d5a
        sim_units_manager = nullptr;

        IVP_Mindist_Manager* const MindistManager = mindist_manager;
        if (MindistManager != nullptr)
        {
            delete MindistManager;
        }
        mindist_manager = nullptr;

        IVP_OV_Tree_Manager* const OvTreeManager = ov_tree_manager;
        if (OvTreeManager != nullptr)
        {
            delete OvTreeManager;
        }
        ov_tree_manager = nullptr;

        IVP_Env_Interval_Gate* const IntervalGate = interval_gate;
        if (IntervalGate != nullptr)
        {
            delete IntervalGate;
        }
        interval_gate = nullptr;

        // The next sub-objects are always set in a constructed environment;
        // retail calls through their vtables with no null test, and each
        // one's own method handles its teardown.
        // performance_counter slot 3; retail never nulls +0x28 afterwards.
        performance_counter->environment_is_going_to_be_deleted(this);
        collision_filter->environment_will_be_deleted(this);
        collision_filter = nullptr;
        // Anomaly manager slot 4 and anomaly limits slot 0: each deletes itself
        // when created owned (the constructor passes 1).
        anomaly_manager->EnvironmentWillBeDeleted(this);
        anomaly_manager = nullptr;
        anomaly_limits->EnvironmentWillBeDeleted(this);
        anomaly_limits = nullptr;
        // Slot 1 (LAB_063616a0): deletes itself when created owned.
        l_active_value_manager->EnvironmentWillBeDeleted(this);
        l_active_value_manager = nullptr;
        // Slot 5 (LAB_0634bda0): deletes itself when created owned.
        l_material_manager->environment_will_be_deleted(this);
        l_material_manager = nullptr;
        range_manager->environment_will_be_deleted(this); // slot 2
        range_manager = nullptr;

        // n_elems read once, elems re-read per pass; slot 6 with the environment.
        for (int i = static_cast<int>(collision_delegator_roots.n_elems) - 1; i >= 0; --i)
        {
            IVP_Collision_Delegator_Root* Delegator =
                static_cast<IVP_Collision_Delegator_Root*>(collision_delegator_roots.elems[i]);
            Delegator->environment_is_going_to_be_deleted_event(this);
        }
        collision_delegator_roots.Clear();

        delete short_term_mem; // ~IVP_U_Memory (06344000) + FUN_06391d5a
        short_term_mem = nullptr;

        delete sim_unit_mem; // ~IVP_U_Memory (06344000) + FUN_06391d5a
        sim_unit_mem = nullptr;

        IVP_Cache_Object_Manager* const CacheObjectManager = cache_object_manager;
        if (CacheObjectManager != nullptr)
        {
            delete CacheObjectManager;
        }
        cache_object_manager = nullptr;

        if (environment_name != nullptr)
        {
            free(environment_name); // FUN_06343fb0
            environment_name = nullptr;
        }

        // Unregister from environment_manager->Environments. The constructor
        // always registers, so retail has no not-found guard: n_elems is
        // decremented unconditionally and the tail shifted from the found
        // index. No null test on environment_manager either.
        {
            IVP_U_Vector<IVP_Environment>& Registry = environment_manager->Environments;
            unsigned short& regCount = Registry.n_elems;   // +0x6 of the manager
            IVP_Environment** regElems = Registry.elems;    // +0x8 of the manager

            const int32_t OldCount = static_cast<int32_t>(regCount);
            int32_t Idx = OldCount - 1;
            while (Idx >= 0 && regElems[Idx] != this)
            {
                --Idx;
            }

            regCount = static_cast<uint16_t>(OldCount - 1);
            while (Idx < static_cast<int32_t>(regCount))                 // n_elems re-read
            {
                regElems[Idx] = regElems[Idx + 1];
                ++Idx;
            }
        }

        free_buffer_node_list(); // FUN_0634cd20
    }

    // Shared tail, also reached directly when state_magic does not match: the
    // member vectors release their heap buffers.
    collision_delegator_roots.Clear();
    object_listeners.Clear();
    vector_e0.Clear();
    event_queue.Clear();
    psi_listeners.Clear();
    collision_listeners.Clear();
}

// ============================================================================
// FUN_0634d090: IVP_Environment::simulate_psi
// ============================================================================
// FUN_0634d020: IVP_Environment::fire_event_PSI. PSI listeners last to first,
// elems re-read each pass, slot 0 with a stack IVP_Event_PSI {env}.
void IVP_Environment::fire_event_PSI()
{
    IVP_Event_PSI Event;
    Event.environment = this;
    for (int32_t I = static_cast<int32_t>(psi_listeners.n_elems) - 1; I >= 0; --I)
    {
        IVP_Listener_PSI* Listener = static_cast<IVP_Listener_PSI*>(psi_listeners.elems[I]);
        Listener->event_PSI(&Event);
    }
}

// FUN_0634d090: IVP_Environment::simulate_psi. The IVP_Time argument is never
// read. The FPhysicsTickScope profiling is port-only and does not change the
// calculations or call order.
void IVP_Environment::simulate_psi(IVP_Time /*current_sim_time*/)
{
    FPhysicsTickScope PrepareProfile(EPhysicsTickSection::PsiPrepare);
    sim_substate = 0;

    if (event_queue.n_elems != 0)
    {
        process_wake_queue();                                   // FUN_0634bbe0
    }

    if (universe_manager != nullptr)
    {
        cluster_manager->check_for_unused_objects(universe_manager);    // FUN_0634b230
    }

    l_active_value_manager->RefreshPsiActiveValues(this); // slot 10

    fire_event_PSI();

    mindist_manager->recalc_all_exact_wheel_mindist(); // FUN_0634f230

    // Two 128-slot inline vectors on the stack.
    IVP_U_Vector_Fixed<IVP_Core, 128> ActiveCores;
    IVP_U_Vector_Fixed<IVP_Hull_Manager, 128> ActiveHullManagers;
    PrepareProfile.Stop();

    // FUN_063499c0: every sim unit's PSI; collects the cores that need
    // calc_next_PSI_matrix.
    FPhysicsTickScope UnitsProfile(EPhysicsTickSection::SimUnits);
    sim_units_manager->simulate_sim_units_psi(this, &ActiveCores);
    UnitsProfile.Stop();

    FPhysicsTickScope CommitProfile(EPhysicsTickSection::CommitHulls);
    IVP_Calc_Next_PSI_Solver::commit_all_calc_next_PSI_matrix(this, &ActiveCores, &ActiveHullManagers); // FUN_06351eb0

    sim_substate = 2;
    IVP_Calc_Next_PSI_Solver::commit_all_hull_managers(this, &ActiveHullManagers); // FUN_06351f80
    CommitProfile.Stop();

    sim_substate = 3;
    FPhysicsTickScope ContactsProfile(EPhysicsTickSection::Contacts);
    mindist_manager->recalc_all_exact_mindists(); // FUN_0634f300

    sim_substate = 4;
    mindist_manager->recalc_all_exact_mindists_events();                      // FUN_0634f210
    ContactsProfile.Stop();

    sim_substate = 5;
}

// ============================================================================
// IVP_Anomaly_Limits (+0x24) / IVP_Anomaly_Manager (+0x20), declared in
// ivp_anomaly_manager.h.
// ============================================================================

// FUN_06360e30. Raw immediates: 0x44fa0000 == 2000.0f, 0x00011170 == 70000
// (integer), 0x3fc90fdb == 1.5707964f.
IVP_Anomaly_Limits::IVP_Anomaly_Limits(int32_t bDeleteWithEnvironment)
    : bDeleteOnEnvironmentDelete(bDeleteWithEnvironment)
    , MaxVelocity(2000.0f)
    , MaxCollisionsPerPsi(70000)
    , MaxAngularVelocityPerPsi(1.5707964f)
{
}

// Slot 0, LAB_06360e90. Retail's null-this test comes from an inlined
// "delete if owned" helper and cannot fire through a virtual call.
void IVP_Anomaly_Limits::EnvironmentWillBeDeleted(IVP_Environment* /*Env*/)
{
    if (bDeleteOnEnvironmentDelete != 0)
    {
        delete this;
    }
}

// Slot 1, FUN_06360e60 -> FUN_06360e80: trivial; the free belongs to the
// scalar deleting destructor.
IVP_Anomaly_Limits::~IVP_Anomaly_Limits()
{
}

// FUN_06360eb0
IVP_Anomaly_Manager::IVP_Anomaly_Manager(int32_t bDeleteWithEnvironment)
    : bDeleteOnEnvironmentDelete(bDeleteWithEnvironment)
{
}

// Slot 0, LAB_06360f20. DAT_063a57a0 == 0.99f. Target, |v| and the quotient
// stay on the x87 stack (double); only the three products are stored as floats.
void IVP_Anomaly_Manager::MaxVelocityExceeded(IVP_Anomaly_Limits* Limits, IVP_Core* /*Core*/, Vector* Speed)
{
    const double Target = (double)Limits->MaxVelocity * (double)0.99f;
    const double Length = std::sqrt(((double)Speed->x * Speed->x + (double)Speed->y * Speed->y) +
                                    (double)Speed->z * Speed->z);
    const double Scale = Target / Length;

    Speed->x = (float)(Scale * Speed->x);
    Speed->y = (float)(Scale * Speed->y);
    Speed->z = (float)(Scale * Speed->z);
}

// Slot 1, LAB_06360f70. Same shape as slot 0; the per-PSI angular limit is
// made per-second with the core's environment. DAT_063a53fc == 0.9f.
void IVP_Anomaly_Manager::MaxAngularVelocityExceeded(IVP_Anomaly_Limits* Limits, IVP_Core* Core, Vector* RotSpeed)
{
    const double Target = ((double)Core->Environment->inv_delta_time * Limits->MaxAngularVelocityPerPsi) * (double)0.9f;
    const double Length = std::sqrt(((double)RotSpeed->x * RotSpeed->x + (double)RotSpeed->y * RotSpeed->y) +
                                    (double)RotSpeed->z * RotSpeed->z);
    const double Scale = Target / Length;

    RotSpeed->x = (float)(Scale * RotSpeed->x);
    RotSpeed->y = (float)(Scale * RotSpeed->y);
    RotSpeed->z = (float)(Scale * RotSpeed->z);
}

// FUN_06360fd0. Pushes the two objects apart along the centre line; speed is
// the virtual get_push_speed_penetration (slot 5), rounded to float. Retail
// quirks kept: Object1 is also pushed at Core0's position, and both get the
// rotation speed {delta_time, 0, 0}. No pointer is tested.
void IVP_Anomaly_Manager::solve_inter_penetration_simple(IVP_Real_Object* Object0, IVP_Real_Object* Object1)
{
    IVP_Environment* const Env = Object0->environment;
    IVP_Core* const Core0 = Object0->core;
    IVP_Core* const Core1 = Object1->core;
    const IVP_U_Point* const Center0 = &Core0->MWorldFCore.vv;
    const IVP_U_Point* const Center1 = &Core1->MWorldFCore.vv;
    const float DeltaTime = Env->delta_time;

    IVP_U_Point Dir;
    Dir.k[1] = Center1->k[1] - Center0->k[1];
    Dir.k[2] = Center1->k[2] - Center0->k[2];
    Dir.k[0] = Center1->k[0] - Center0->k[0];
    Dir.normize();

    const float Speed = (float)(get_push_speed_penetration(Object0, Object1));

    if (static_cast<int8_t>(Core0->MovementState & 0xffu) < IVP_MT_NOT_SIM)
    {
        const double Factor = -((double)(Core0->Mass) * Speed);
        IVP_U_Point Impulse;
        Impulse.k[0] = (float)(Factor * Dir.k[0]);
        Impulse.k[1] = (float)(Factor * Dir.k[1]);
        Impulse.k[2] = (float)(Factor * Dir.k[2]);
        Object0->async_push_object_ws(Center0, &Impulse);

        IVP_U_Point RotSpeed;
        RotSpeed.k[0] = DeltaTime;
        RotSpeed.k[1] = 0.0f;
        RotSpeed.k[2] = 0.0f;
        Object0->async_add_rot_speed_object_cs(&RotSpeed);
    }

    if (static_cast<int8_t>(Core1->MovementState & 0xffu) >= IVP_MT_NOT_SIM)
        return;

    const double Factor = (double)(Core1->Mass) * Speed;
    IVP_U_Point Impulse;
    Impulse.k[0] = (float)(Factor * Dir.k[0]);
    Impulse.k[1] = (float)(Factor * Dir.k[1]);
    Impulse.k[2] = (float)(Factor * Dir.k[2]);
    Object1->async_push_object_ws(Center0, &Impulse); // Core0's position, as retail

    IVP_U_Point RotSpeed;
    RotSpeed.k[0] = DeltaTime;
    RotSpeed.k[1] = 0.0f;
    RotSpeed.k[2] = 0.0f;
    Object1->async_add_rot_speed_object_cs(&RotSpeed);
}

// Slot 2, LAB_06361130: inter_penetration, called by
// IVP_Ledge_Mindist::mindist_rescue_push (06352b10). When one side is fixed
// (core flags & 0xc) and its synapse status <= 2, the free object is pushed
// along the fixed ledge's triangle plane with the greatest signed distance to
// the free object's centre (D > Best: ties and NaN keep the old best); else
// falls back to solve_inter_penetration_simple. The cache object is read
// without a refcount. No pointer is tested.
void IVP_Anomaly_Manager::inter_penetration(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* Object0, IVP_Real_Object* Object1)
{
    IVP_Real_Object* const Objects[2] = { Object0, Object1 };
    const int32_t Idx = ((Object1->core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) != 0) ? 1 : 0;
    IVP_Real_Object* const Fixed = Objects[Idx];
    if ((Fixed->core->Flags & IVP_CORE_PHYSICAL_UNMOVEABLE) == 0)
    {
        solve_inter_penetration_simple(Object0, Object1);
        return;
    }

    const IVP_Synapse_Real* const Synapse = &Mindist->synapse[Idx];
    if (Synapse->status > IVP_SYNAPSE_POLY)
    {
        solve_inter_penetration_simple(Object0, Object1);
        return;
    }

    IVP_Real_Object* const Free = Objects[1 - Idx];
    IVP_U_Point PositionWs;
    PositionWs.k[0] = Free->core->MWorldFCore.vv.k[0];
    PositionWs.k[1] = Free->core->MWorldFCore.vv.k[1];
    PositionWs.k[2] = Free->core->MWorldFCore.vv.k[2];

    IVP_NarrowPhaseCacheSlot* const Slot = Fixed->get_cache_object_no_lock();
    const IVP_U_Matrix* const Transform = &Slot->WorldTransform;
    IVP_U_Point PositionOs;
    Transform->vimult4(&PositionWs, &PositionOs);

    const IVP_Compact_Ledge* const Ledge = Synapse->get_ledge();

    IVP_U_Point BestNormal;
    BestNormal.k[0] = 1.0f;
    BestNormal.k[1] = 0.0f;
    BestNormal.k[2] = 0.0f;
    float Best = -1e20f;                               // 0xe0ad78ec

    const IVP_Compact_Triangle* Triangle = Ledge->GetFirstTriangle();
    for (int32_t i = 0; i < static_cast<int32_t>(Ledge->GetNTriangles()); ++i, ++Triangle)
    {
        IVP_U_Point P0;
        IVP_U_Point P1;
        IVP_U_Point P2;
        P0.set(reinterpret_cast<const IVP_U_Point*>(Ledge->GetPoint(Triangle->GetEdge(0))));
        P1.set(reinterpret_cast<const IVP_U_Point*>(Ledge->GetPoint(Triangle->GetEdge(1))));
        P2.set(reinterpret_cast<const IVP_U_Point*>(Ledge->GetPoint(Triangle->GetEdge(2))));

        IVP_U_Point Plane;
        Plane.set_plane_from_3_points(&P0, &P2, &P1);
        Plane.normize_plane();

        const double Distance = (((double)(Plane.k[2]) * PositionOs.k[2] +
                                  (double)(Plane.k[1]) * PositionOs.k[1]) +
                                 (double)(PositionOs.k[0]) * Plane.k[0]) +
                                Plane.k[3];
        if (Distance > Best)
        {
            Best = (float)(Distance);
            BestNormal.k[0] = Plane.k[0];
            BestNormal.k[1] = Plane.k[1];
            BestNormal.k[2] = Plane.k[2];
        }
    }

    IVP_U_Point NormalWs;
    Transform->vmult(&BestNormal, &NormalWs);

    IVP_U_Point Dir;
    Dir.k[0] = NormalWs.k[0];
    Dir.k[1] = NormalWs.k[1];
    Dir.k[2] = NormalWs.k[2];
    const double Speed = get_push_speed_penetration(Object0, Object1);

    if (!(static_cast<int8_t>(Free->core->MovementState & 0xffu) < IVP_MT_NOT_SIM))
    {
        return;
    }

    const float Y = (float)((double)(Dir.k[1]) * Speed);
    const float Z = (float)((double)(Dir.k[2]) * Speed);
    const float X = (float)((double)(Dir.k[0]) * Speed);
    Dir.k[2] = Z;
    Dir.k[0] = X;
    Dir.k[1] = Y;
    Free->async_add_speed_object_ws(&Dir);
}

// Slot 3, LAB_06361490
int32_t IVP_Anomaly_Manager::MaxCollisionsExceededCheckFreezing(IVP_Anomaly_Limits* /*Limits*/, IVP_Core* /*Core*/)
{
    return 1;
}

// Slot 4, LAB_06360f00
void IVP_Anomaly_Manager::EnvironmentWillBeDeleted(IVP_Environment* /*Env*/)
{
    if (bDeleteOnEnvironmentDelete != 0)
    {
        delete this;
    }
}

// Slot 5, LAB_06361460: 2 * |gravity| * delta_time (sqrt via FUN_06347e60).
// Retail keeps the 80-bit x87 intermediate; double is value-identical here.
double IVP_Anomaly_Manager::get_push_speed_penetration(IVP_Real_Object* Object0, IVP_Real_Object* /*Object1*/)
{
    IVP_Environment* Env = Object0->environment;
    const float DeltaTime = Env->delta_time;
    const double X = Env->gravity_x;
    const double Y = Env->gravity_y;
    const double Z = Env->gravity_z;
    const double Length = std::sqrt((X * X + Y * Y) + Z * Z);     // FUN_06347e60
    return (Length + Length) * DeltaTime;
}

// Slot 6, FUN_06360ed0 -> FUN_06360ef0
IVP_Anomaly_Manager::~IVP_Anomaly_Manager()
{
}

// ############################################################################
// IVP_Environment's constructor (FUN_0634bf50) and its callees.
// Already ported elsewhere under semantic names: FUN_06356920 (_strdup),
// FUN_063486c0 / FUN_06348690 / FUN_06348670 (IVP_RealObjectConstructionTemplate
// ctor / set_long_name / dtor, ivp_real_object.cpp), FUN_063481b0
// (IVP_U_Matrix::init, modulo the k[3] pads), FUN_06344010
// (IVP_U_Memory::init_mem), FUN_06349380 (IVP_Sim_Units_Manager ctor).
// ############################################################################

// ============================================================================
// FUN_063505f0
// ============================================================================
void* IVP_Environment_Init_Block34(void* block34)
{
    std::memset(block34, 0, 0x54);
    return block34;
}

// ============================================================================
// FUN_0634bf40 / FUN_0634bf30: the freeze manager's init and ctor, only
// called on IVP_Environment+0x88 (freeze_check_dtime) by the constructor.
// 0x3e99999a is 0.3f.
// ============================================================================
void IVP_Freeze_Manager_Init(void* FreezeManager)
{
    *static_cast<float*>(FreezeManager) = 0.3f;
}

void* IVP_Freeze_Manager_Construct(void* FreezeManager)
{
    IVP_Freeze_Manager_Init(FreezeManager);
    return FreezeManager;
}

// ============================================================================
// FUN_0634c6d0: IVP_Environment::set_delta_PSI_time.
// ============================================================================
const float IVP_ENVIRONMENT_DEFAULT_DELTA_PSI_TIME = 1.0f / 66.0f; // 0x3c783e10

void IVP_Environment::set_delta_PSI_time(float dt)
{
    delta_time = dt;
    inv_delta_time = 1.0f / dt; // DAT_063a53d0 = 1.0f
}

// ============================================================================
// FUN_06360090: once-only global table init called by the environment
// constructor (next to FUN_06353940). Fills 16 slots with the LAB_06360080
// trap, then stores the real handlers, one past the cleared range (so the
// array is 17 dwords). The table and its handlers live in
// ivp_ledge_sweep_solver.cpp.
// ============================================================================

void IVP_InitSimUnitDispatchTable(void)
{
    IVP_InitSweepEstimatorTable();
}

// ============================================================================
// IVP_Sim_Unit_Manager: the environment's time-ordered event queue (see the
// class comment in ivp_environment.h).
// ============================================================================

// Inlined by IVP_Ledge_Mindist::RescheduleCollisionEvent (0634f964): key = (EventHi -
// TimeBaseHi) + EventLo - TimeBaseLo, then EventPool->Insert(payload, key)
// (FUN_06362f90). The pool is a heap object pointed to from +0x08. No null
// test on EventPool; the constructor always builds it.
uint16_t IVP_Sim_Unit_Manager::EnqueueEvent(void* Payload, float EventHi, float EventLo)
{
    return EventPool->Insert(Payload, MakeEventKey(EventHi, EventLo));
}

// FUN_063609f0
void IVP_Sim_Unit_Manager::CancelEvent(int32_t Handle)
{
    EventPool->Remove(static_cast<uint16_t>(Handle));
}

// FUN_063609c0. The slot goes back into the listener's handle field (+0x04).
void IVP_Sim_Unit_Manager::ScheduleListener(IVP_Time_Event* Listener, float EventHi, float EventLo)
{
    Listener->EventQueueHandle = static_cast<int32_t>(EnqueueEvent(Listener, EventHi, EventLo));
}

// FUN_06360a10. Removal goes through the payload's own handle field rather
// than LiveIndex (the same slot), as retail spells it.
void* IVP_Sim_Unit_Manager::PopMinEventPayload()
{
    if (EventPool->PoolCount == 0)
        return nullptr;

    IVP_Time_Event* Payload =
        static_cast<IVP_Time_Event*>(EventPool->PoolEntries[EventPool->PoolLiveIndex].Payload);
    EventPool->Remove(static_cast<uint16_t>(Payload->EventQueueHandle));
    return Payload;
}

// FUN_06360960. Drains the pool, then deletes the event manager and the pool.
// The PSI event (+0x0c) is not freed separately: it sits in the pool, so the
// drain already freed it, and the member keeps a stale pointer, as in retail.
void IVP_Sim_Unit_Manager::DestroyContents()
{
    while (EventPool->PoolCount > 0)
    {
        // Bare operator delete (FUN_06391d5a): no destructor runs on the
        // queued events.
        ::operator delete(PopMinEventPayload());
    }

    delete EventManager;     // FUN_06391d5a (trivial dtor)
    EventManager = nullptr;

    // FUN_06362f70(pool) + free(pool).
    delete EventPool;
    EventPool = nullptr;
}

// ============================================================================
// FUN_063608a0: constructor of IVP_Environment::sim_unit_manager (+0x04), a
// 0x1C-byte object owning three heap sub-objects: IVP_Event_Manager (new(8),
// vtable 063a558c), IVP_Priority_Pool (new(0x14), FUN_06362ef0(p, 0x10)) and
// IVP_Time_Event_PSI (new(8), vtable 063a574c). The PSI event is scheduled at
// key 0.0f (TimeBase is still zero).
// ============================================================================
void* IVP_SimUnitManager_Construct(void* self)
{
    IVP_Sim_Unit_Manager* Self = static_cast<IVP_Sim_Unit_Manager*>(self);
    std::memset(Self, 0, sizeof(IVP_Sim_Unit_Manager)); // 7 dwords

    Self->EventManager = new IVP_Event_Manager();

    Self->EventPool = new IVP_Priority_Pool(static_cast<uint16_t>(0x10));

    // Retail does not initialize the handle here; ScheduleListener writes it.
    Self->PsiEvent = new IVP_Time_Event_PSI();

    Self->ScheduleListener(Self->PsiEvent, 0.0f, 0.0f);
    return Self;
}

// FUN_06360ab0: forwards to FUN_0634ccd0.
void IVP_Sim_Unit_Manager::env_set_current_time(IVP_Environment* Environment, float TimeHi, float TimeLo)
{
    Environment->set_current_time(TimeHi, TimeLo);
}

// FUN_06360a40. The whole step runs with the x87 precision control set to a
// 64-bit mantissa (FSTCW; OR 0x300; FLDCW) and the saved control word is
// reloaded afterwards, so the caller's precision (the client runs at 24 or
// 53 bits) does not reach the simulation.
// Clamp: below 0.005f or NaN -> 0.005f; above 0.1f -> 0.1f.
void IVP_Sim_Unit_Manager::simulate_variable_time_step(IVP_Environment* Environment, float DeltaTime)
{
    unsigned short SavedControl;
    unsigned short ExtendedControl;
    __asm fstcw SavedControl
    ExtendedControl = static_cast<unsigned short>(SavedControl | 0x300);
    __asm fldcw ExtendedControl

    if (!(DeltaTime >= 0.005f))
        DeltaTime = 0.005f;
    else if (DeltaTime > 0.1f)
        DeltaTime = 0.1f;

    EventManager->simulate_variable_time_step(this, Environment, PsiEvent, DeltaTime); // slot 1

    __asm fldcw SavedControl
}

// ----------------------------------------------------------------------------
// IVP_Event_Manager slot 0, 063606d0. Events run while until_rel > the pool's
// minimum key (<= or unordered exits). Hi/Lo stay on the x87 stack (double)
// through the normalization loop and are rounded to float only at the call.
// ----------------------------------------------------------------------------
void IVP_Event_Manager::simulate_time_events(IVP_Sim_Unit_Manager* TimeManager, IVP_Environment* Environment,
                                             float UntilHi, float UntilLo)
{
    for (;;)
    {
        IVP_Priority_Pool* Pool = TimeManager->EventPool;
        const float MinKey = Pool->PoolMinKey;
        const double UntilRel = (((double)(UntilHi) - TimeManager->TimeBaseHi) + UntilLo) - TimeManager->TimeBaseLo;
        if (!(UntilRel > MinKey))
            break;

        IVP_Time_Event* Event =
            static_cast<IVP_Time_Event*>(Pool->PoolEntries[Pool->PoolLiveIndex].Payload);
        Pool->Remove(static_cast<uint16_t>(Event->EventQueueHandle));
        Event->EventQueueHandle = 0xffff;
        TimeManager->LastEventRelTime = MinKey;

        double Hi = TimeManager->TimeBaseHi;
        double Lo = (double)(MinKey) + TimeManager->TimeBaseLo;
        while (Lo > 1.0f)
        {
            Hi = Hi + 1.0f;
            Lo = Lo - 1.0f;
        }
        TimeManager->env_set_current_time(Environment, (float)(Hi), (float)(Lo));
        Event->simulate_time_event(Environment); // slot 0

        if (TimeManager->EventManager->Mode == 1)
            break;
    }
    TimeManager->env_set_current_time(Environment, UntilHi, UntilLo);
}

// ----------------------------------------------------------------------------
// IVP_Event_Manager slot 1, 063607c0 (Ghidra left it as raw bytes). Runs
// events until the PSI event comes up a second time; no until-time and no
// Mode test.
// ----------------------------------------------------------------------------
void IVP_Event_Manager::simulate_variable_time_step(IVP_Sim_Unit_Manager* TimeManager, IVP_Environment* Environment,
                                                    IVP_Time_Event_PSI* PsiEvent, float DeltaTime)
{
    int32_t PsiCount = 0;
    for (;;)
    {
        IVP_Priority_Pool* Pool = TimeManager->EventPool;
        const float MinKey = Pool->PoolMinKey;
        IVP_Time_Event* Event =
            static_cast<IVP_Time_Event*>(Pool->PoolEntries[Pool->PoolLiveIndex].Payload);

        // x87: double until the float stores at the call.
        double Hi = TimeManager->TimeBaseHi;
        double Lo = (double)(MinKey) + TimeManager->TimeBaseLo;
        while (Lo > 1.0f)
        {
            Hi = Hi + 1.0f;
            Lo = Lo - 1.0f;
        }
        TimeManager->env_set_current_time(Environment, (float)(Hi), (float)(Lo));

        if (Event == PsiEvent)
        {
            ++PsiCount;
            if (PsiCount == 2)
                return;
            Environment->set_delta_PSI_time(DeltaTime);            // FUN_0634c6d0
        }

        TimeManager->LastEventRelTime = MinKey;
        Pool->Remove(static_cast<uint16_t>(Event->EventQueueHandle));
        Event->EventQueueHandle = 0xffff;
        Event->simulate_time_event(Environment);
    }
}

// ----------------------------------------------------------------------------
// IVP_Time_Event_PSI::simulate_time_event, LAB_06360ad0: the PSI advance.
// ----------------------------------------------------------------------------
void IVP_Time_Event_PSI::simulate_time_event(IVP_Environment* Environment)
{
    IVP_Sim_Unit_Manager* TimeManager = Environment->sim_unit_manager;

    Environment->time_of_last_psi.time_base = Environment->current_time.time_base;
    Environment->time_of_last_psi.time_offset = Environment->current_time.time_offset;

    // The following PSI, normalized while lo > 1.0f; double on the x87 stack
    // until the float stores.
    double Hi = Environment->time_of_last_psi.time_base;
    double Lo = (double)Environment->delta_time + Environment->time_of_last_psi.time_offset;
    while (Lo > 1.0f)
    {
        Hi = Hi + 1.0f;
        Lo = Lo - 1.0f;
    }
    Environment->next_psi_time_base = (float)(Hi);
    Environment->next_psi_time_offset = (float)(Lo);

    // Rebase every queued key and the pool's minimum by (offset + base),
    // walking the list from PoolLiveIndex. Retail's null test on the entry
    // comes after Entry->Next was already read, so it is unreachable and not
    // reproduced. Shift stays double; each store rounds to float.
    const double Shift = (double)Environment->time_of_last_psi.time_offset + Environment->time_of_last_psi.time_base;
    IVP_Priority_Pool* Pool = TimeManager->EventPool;
    uint32_t Index = Pool->PoolLiveIndex;
    while (Index != 0xffff)
    {
        IVP_Priority_Pool::Entry* Entry = &Pool->PoolEntries[Index];
        Index = Entry->Next;
        Entry->Key = (float)(Entry->Key - Shift);
    }
    Pool->PoolMinKey = (float)(Pool->PoolMinKey - Shift);

    TimeManager->TimeBaseHi = Environment->time_of_last_psi.time_base;
    TimeManager->TimeBaseLo = Environment->time_of_last_psi.time_offset;
    TimeManager->LastEventRelTime = 0.0f;

    Environment->simulate_psi(Environment->time_of_last_psi); // FUN_0634d090

    // Requeue at the following PSI.
    TimeManager->ScheduleListener(this, Environment->next_psi_time_base, Environment->next_psi_time_offset);
}

// FUN_0634ccd0
void IVP_Environment::set_current_time(float TimeHi, float TimeLo)
{
    ++sim_step_count;
    current_time.time_base = TimeHi;
    current_time.time_offset = TimeLo;
}


// FUN_0634cc80
void IVP_Environment::simulate_dtime(float DeltaTime)
{
    FPhysicsTickScope SimulateProfile(EPhysicsTickSection::Simulate);
    // x87: double until the single float store.
    const float Target = (float)(
        ((double)(current_time.time_offset) + current_time.time_base) + DeltaTime);
    interval_gate->set_time(Target);                                    // FUN_063605a0
    sim_unit_manager->simulate_variable_time_step(this, DeltaTime);
}

// FUN_0634cb90
IVP_Cluster* IVP_Environment::get_root_cluster()
{
    return cluster_manager->get_root_cluster(); // 06261d90
}

// FUN_0634cd80: operator new(0xa8) + FUN_06362280. Retail's null result on a
// failed allocation is unreachable with the throwing new.
IVP_Polygon* IVP_Environment::create_polygon(IVP_SurfaceManager* SurfaceManager,
                                             const IVP_RealObjectConstructionTemplate* Template,
                                             const IVP_U_Quat* Rotation,
                                             const IVP_U_Point* Position)
{
    return new IVP_Polygon(get_root_cluster(), SurfaceManager, Template, Rotation, Position);
}

// FUN_0634cdf0: as FUN_0634cd80 with FUN_06362570 (IVP_Ball).
IVP_Ball* IVP_Environment::create_ball(const float* BallRadius,
                                       const IVP_RealObjectConstructionTemplate* Template,
                                       const IVP_U_Quat* Rotation, const IVP_U_Point* Position)
{
    return new IVP_Ball(get_root_cluster(), BallRadius, Template, Rotation, Position);
}

// FUN_0634cb20
void IVP_Environment::set_gravity(const IVP_U_Point* Gravity)
{
    gravity_x = Gravity->x;
    gravity_z = Gravity->z;
    gravity_y = Gravity->y;
    // FUN_06347e60: |g| on the x87 stack, rounded to float on store.
    const double X = Gravity->x;
    const double Y = Gravity->y;
    const double Z = Gravity->z;
    gravity_length = (float)(std::sqrt(X * X + Y * Y + Z * Z));
    standard_gravity_controller->set_grav_vector(Gravity);
}

// FUN_06349510
void IVP_Standard_Gravity_Controller::set_grav_vector(const IVP_U_Point* Gravity)
{
    grav_vec.x = Gravity->x;
    grav_vec.y = Gravity->y;
    grav_vec.z = Gravity->z;
}

// LAB_06349530. Cores with Flags bits 8/9 set are skipped. The three sums are
// formed in double and stored in the order z, x, y.
void IVP_Standard_Gravity_Controller::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    for (int32_t I = static_cast<int32_t>(CoreList->n_elems) - 1; I >= 0; --I)
    {
        IVP_Core* Core = CoreList->elems[I];
        if ((Core->Flags & IVP_CORE_PINNED) != 0)
            continue;

        Core->damp_velocities(EventSim->delta_time);                            // FUN_06346980
        Core->commit_speed_changes();                                           // FUN_06346dd0

        const double Dt = EventSim->delta_time;
        const double NewY = Dt * grav_vec.y + Core->Speed.y;
        const double NewZ = Dt * grav_vec.z + Core->Speed.z;
        Core->Speed.z = (float)(NewZ);
        Core->Speed.x = (float)(Dt * grav_vec.x + Core->Speed.x);
        Core->Speed.y = (float)(NewY);
    }
}

// LAB_0634c6c0: 0x3e8.
IVP_CONTROLLER_PRIORITY IVP_Standard_Gravity_Controller::get_controller_priority()
{
    return IVP_CP_GRAVITY;
}

// ============================================================================
// IVP_Range_Manager. DAT_063a53d8 = 1e-10f, DAT_063a5748 = 0.2f,
// DAT_063a5744 = 0.18f, DAT_063a53d0 = 1.0f. Values on the x87 stack are
// double and rounded to float where retail stores them; `!(a >= b)` is the
// less-or-unordered test.
// ============================================================================

// FUN_06360360. vptr 063a5738.
IVP_Range_Manager::IVP_Range_Manager(IVP_Environment* Environment, int32_t bInDeleteOnEnvDelete)
    : bDeleteOnEnvDelete(bInDeleteOnEnvDelete)
    , environment(Environment)
    , look_ahead_time_intra(0.5f)
    , look_ahead_max_radius_intra(0.9f)
    , look_ahead_min_distance_intra(0.8f)
    , look_ahead_max_distance_intra(10.0f)
    , look_ahead_min_seconds_intra(0.1f)
    , look_ahead_time_world(1.0f)
    , look_ahead_max_radius_world(5.0f)
    , look_ahead_min_distance_world(0.5f)
    , look_ahead_max_distance_world(15.0f)
    , look_ahead_min_seconds_world(0.1f)
{
}

// LAB_063603e0
void IVP_Range_Manager::get_coll_range_intra_objects(const IVP_Real_Object* Object0, const IVP_Real_Object* Object1,
                                                     float* Range0, float* Range1)
{
    const IVP_Core* Core0 = Object0->core;
    const IVP_Core* Core1 = Object1->core;

    const float Speed0 = (float)(
        ((double)(Core0->MaxSurfaceRotSpeed) + Core0->CurrentSpeed) + 1.0e-10f);
    const double Speed1Ext = ((double)(Core1->MaxSurfaceRotSpeed) + Core1->CurrentSpeed) + 1.0e-10f;
    const float Speed1 = (float)(Speed1Ext);                                 // FST, value stays on stack
    const float SpeedSum = (float)(Speed1Ext + Speed0);

    // Core1's radius when not less.
    const float MinRadius = (Core0->UpperLimitRadius < Core1->UpperLimitRadius)
        ? Core0->UpperLimitRadius : Core1->UpperLimitRadius;

    const double A = (double)(SpeedSum) * look_ahead_time_intra;
    const double BExt = (double)(MinRadius) * look_ahead_max_radius_intra;
    const float B = (float)(BExt);                                           // FST
    double Value = !(BExt >= A) ? (double)(B) : A;
    if (!(Value >= look_ahead_min_distance_intra))
        Value = look_ahead_min_distance_intra;
    if (Value > look_ahead_max_distance_intra)
        Value = look_ahead_max_distance_intra;

    Value = Value - (double)(environment->delta_time) * SpeedSum;
    const float MinValue = (float)((double)(SpeedSum) * look_ahead_min_seconds_intra);
    if (!(Value >= MinValue))
        Value = MinValue;

    // Split Value between the two objects.
    const double Share0Ext = (double)(Speed1) * 0.2f + Speed0;
    const float Share0 = (float)(Share0Ext);                                 // FST
    const double Share1Ext = Share0Ext * 0.18f + Speed1;
    const float Share1 = (float)(Share1Ext);                                 // FST
    const double Scale = Value * (1.0f / (Share1Ext + Share0));
    *Range0 = (float)(Scale * Share0);
    *Range1 = (float)(Scale * Share1);
}

// FUN_06360500. The result is left unrounded on the x87 stack, hence double.
double IVP_Range_Manager::get_coll_range_in_world(const IVP_Real_Object* Object)
{
    const IVP_Core* Core = Object->core;
    const float Speed = (float)(
        ((double)(Core->MaxSurfaceRotSpeed) + Core->CurrentSpeed) + 1.0e-10f);
    const float Radius = Core->UpperLimitRadius;

    const double A = (double)(Speed) * look_ahead_time_world;
    const double BExt = (double)(Radius) * look_ahead_max_radius_world;
    const float B = (float)(BExt);                                           // FST
    double Value = !(BExt >= A) ? (double)(B) : A;
    if (!(Value >= look_ahead_min_distance_world))
        Value = look_ahead_min_distance_world;
    if (Value > look_ahead_max_distance_world)
        Value = look_ahead_max_distance_world;

    Value = Value - (double)(environment->delta_time) * Speed;
    const float MinValue = (float)(
        (double)(Speed) * look_ahead_min_seconds_world + Core->UpperLimitRadius);
    if (!(Value >= MinValue))
        Value = MinValue;
    return Value;
}

// LAB_063603c0: operator delete without a destructor call (the class has none).
void IVP_Range_Manager::environment_will_be_deleted(IVP_Environment* /*Environment*/)
{
    if (bDeleteOnEnvDelete != 0)
        delete this;
}

// ============================================================================
// IVP_PerformanceCounter_Simple
// ============================================================================

// FUN_063602a0. vptr 063a5714; zeroes the 0xa4 bytes after the vptr.
IVP_PerformanceCounter_Simple::IVP_PerformanceCounter_Simple()
{
    std::memset(reinterpret_cast<char*>(this) + 4, 0, 0xa4);
}

// LAB_06360350
void IVP_PerformanceCounter_Simple::start_pcount()
{
    CurrentElement = 0;
}

// LAB_063602e0
void IVP_PerformanceCounter_Simple::pcount(int32_t Element)
{
    if (Element == 1)
        ++CountPSIs;

    LARGE_INTEGER Counter;
    LARGE_INTEGER Frequency;
    QueryPerformanceCounter(&Counter);
    QueryPerformanceFrequency(&Frequency);

    // Only the low dwords are differenced, read as a signed 32-bit value.
    const int32_t Delta = static_cast<int32_t>(static_cast<uint32_t>(Counter.LowPart) - LastCounterLow);
    LastCounterLow = static_cast<uint32_t>(Counter.LowPart);
    LastCounterHigh = static_cast<int32_t>(Counter.HighPart);

    // 1e6 is DAT_063a5730 (double); _ftol truncates.
    FCounter& Slot = Counters[CurrentElement];
    const double Elapsed = ((double)(Delta) * 1000000.0) / (double)(Frequency.QuadPart);
    Slot.Microseconds = static_cast<int32_t>(Elapsed + Slot.Microseconds);

    CurrentElement = Element;
}

// LAB_06360280: `if (this) this->slot5(1)`.
void IVP_PerformanceCounter_Simple::environment_is_going_to_be_deleted(IVP_Environment* /*Env*/)
{
    delete this;
}

// FUN_06360110: reset_and_print_performance_counters(IVP_Time). No-op until
// at least one PSI was counted.
void IVP_PerformanceCounter_Simple::reset_and_print_performance_counters(float TimeHi, float TimeLo)
{
    const float PsiCount = (float)(CountPSIs);
    if (PsiCount == 0.0f)
        return;

    // Integer sums: elements 1,4,5,6,8 and 2,3.
    const int32_t Coll = Counters[1].Microseconds + Counters[4].Microseconds + Counters[5].Microseconds
                       + Counters[6].Microseconds + Counters[8].Microseconds;
    const int32_t Dyn = Counters[2].Microseconds + Counters[3].Microseconds;

    // 0.001f (DAT_063a5604) / PsiCount stays double for the total and is
    // rounded to float for the rest.
    const double ScaleExt = (double)(0.001f) / PsiCount;
    const float Scale = (float)(ScaleExt);
    const float Total = (float)(ScaleExt * ((double)(Dyn) + Coll));

    // 66.0f is DAT_063a5710, 0.1 is DAT_063a5708 (double).
    ivp_message("TOT %2.1f%% %2.2f COLL %2.2f  DYN %2.2f     det:  UNIV: %2.2f CONTR: %2.2f INTEGR: %2.2f HULL: %2.2f SHORT: %2.2f CRITIC: %2.2f AT %2.2f",
                 ((double)(Total) * 66.0f) * 0.1,
                 (double)(Total),
                 (double)(Scale) * Coll,
                 (double)(Scale) * Dyn,
                 (double)(Counters[1].Microseconds) * Scale,
                 (double)(Counters[2].Microseconds) * Scale,
                 (double)(Counters[3].Microseconds) * Scale,
                 (double)(Counters[4].Microseconds) * Scale,
                 (double)(Counters[5].Microseconds) * Scale,
                 (double)(Counters[6].Microseconds) * Scale,
                 (double)(Counters[8].Microseconds) * Scale);

    std::memset(reinterpret_cast<char*>(this) + 4, 0, 0xa4);
    TimeOfLastReset.time_base = TimeHi;
    TimeOfLastReset.time_offset = TimeLo;
}

// ============================================================================
// FUN_0634bf50: IVP_Environment's constructor.
//
// Most sub-objects follow one adopt-or-construct pattern: a non-null
// Parms->Field is adopted (not owned); a null one makes the environment
// allocate and construct its own. Retail's operator-new-returned-null branches
// are unreachable with the throwing new and are not reproduced.
//
// The body starts with memset(this, 0, 0x148) (Ipion's P_MEM_CLEAR(this)),
// which runs after the compiler-emitted member constructors and undoes them;
// retail re-runs the +0x88 one but not the +0x34 one. Reproduced in that
// order, redundant first pass included. SEH scope-index stores are omitted.
// ============================================================================
IVP_Environment* IVP_Environment::construct(IVP_Environment_Manager* Registry,
                                            const IVP_Environment_Create_Parms* Parms,
                                            const char* Name, void* ClientData)
{
    // ---- compiler-emitted member constructors, before the body -------------
    IVP_Environment_Init_Block34(&statistics_owner);          // FUN_063505f0
    IVP_Freeze_Manager_Construct(&freeze_check_dtime);          // FUN_0634bf30

    new (&collision_listeners) IVP_U_Vector<IVP_Listener_Collision>();             // +0xC8
    new (&psi_listeners) IVP_U_Vector<IVP_Listener_PSI>();                         // +0xD0
    new (&event_queue) IVP_U_Vector<IVP_Core>();                                   // +0xD8
    new (&vector_e0) IVP_U_Vector<void>();                                         // +0xE0
    new (&object_listeners) IVP_U_Vector<IVP_Listener_Object>();                   // +0x124
    new (&collision_delegator_roots) IVP_U_Vector<IVP_Collision_Delegator_Root>(); // +0x12C

    // ---- the constructor body proper ---------------------------------------
    std::memset(this, 0, sizeof(IVP_Environment));
    IVP_Freeze_Manager_Init(&freeze_check_dtime);  // FUN_0634bf40

    sim_substate = 5;
    // The +0x34 block's first dword is a back-pointer to the owner.
    statistics_owner = this;

    // ---- adopt-or-construct, in retail's own order -------------------------

    l_material_manager = Parms->MaterialManager;
    if (l_material_manager == nullptr)
    {
        l_material_manager = new IVP_Material_Manager(1);     // FUN_0634bd80
    }

    l_active_value_manager = Parms->ActiveValueManager;
    if (l_active_value_manager == nullptr)
    {
        l_active_value_manager = new IVP_U_Active_Value_Manager(1); // FUN_063615a0
    }

    collision_filter = Parms->CollisionFilter;
    if (collision_filter == nullptr)
    {
        // operator new(8), FUN_0634bcb0(1), vtable 0639c3fc.
        collision_filter = new IVP_Collision_Filter_Coll_Group_Ident(1);
    }

    anomaly_manager = static_cast<IVP_Anomaly_Manager*>(Parms->AnomalyManager);
    if (anomaly_manager == nullptr)
    {
        // operator new(8), FUN_06360eb0(1), vtable 063a5784.
        anomaly_manager = new IVP_Anomaly_Manager(1);
    }

    anomaly_limits = static_cast<IVP_Anomaly_Limits*>(Parms->AnomalyLimits);
    if (anomaly_limits == nullptr)
    {
        // operator new(0x14), FUN_06360e30(1), vtable 063a577c.
        anomaly_limits = new IVP_Anomaly_Limits(1);
    }

    // Same adopt-or-construct choice, but appended to collision_delegator_roots.
    void* CollisionDelegatorRoot = Parms->CollisionDelegatorRoot;
    if (CollisionDelegatorRoot == nullptr)
    {
        // operator new(4), FUN_06360df0: the default delegator (vtable 063a5760).
        CollisionDelegatorRoot = static_cast<IVP_Collision_Delegator_Root*>(new IVP_Collision_Delegator_Root_Mindist());
    }
    collision_delegator_roots.Add(static_cast<IVP_Collision_Delegator_Root*>(CollisionDelegatorRoot));

    // Adopted verbatim, never constructed here.
    universe_manager = Parms->UniverseManager;

    // ---- standard_gravity_controller (+0x00) and the gravity vector --------
    {
        standard_gravity_controller = new IVP_Standard_Gravity_Controller(); // new(0x14), vptr 063a5570

        // Y is the raw dword 0x411d47ae == 9.83f (not 9.81f, which is 0x411cf5c3).
        IVP_U_Point Gravity;
        Gravity.set(0.0f, 9.83f, 0.0f);

        set_gravity(&Gravity);

        // The inlined set_grav_vector of the library's ctor copies the same
        // values into grav_vec once more.
        standard_gravity_controller->grav_vec.x = Gravity.x;
        standard_gravity_controller->grav_vec.y = Gravity.y;
        standard_gravity_controller->grav_vec.z = Gravity.z;
    }

    // Registers with Registry->Environments; the destructor removes it again.
    Registry->Environments.Add(this);

    // ---- controller_manager (+0x90): 4 bytes holding `this` ----------------
    {
        IVP_Controller_Manager* const ControllerManager =
            static_cast<IVP_Controller_Manager*>(::operator new(4));
        ControllerManager->environment = this;
        controller_manager = ControllerManager;
    }

    set_delta_PSI_time(IVP_ENVIRONMENT_DEFAULT_DELTA_PSI_TIME);

    // ---- unconditional sub-managers (no adopt path) ------------------------

    cluster_manager = new IVP_Cluster_Manager(this);           // ctor 0634ad70
    sim_units_manager = new IVP_Sim_Units_Manager(this);       // ctor 06349380
    {
        void* p = ::operator new(0x1C);
        IVP_SimUnitManager_Construct(p);                           // FUN_063608a0
        sim_unit_manager = static_cast<IVP_Sim_Unit_Manager*>(p);
    }
    mindist_manager = new IVP_Mindist_Manager(this);           // ctor 06350390
    ov_tree_manager = new IVP_OV_Tree_Manager();              // ctor 0635c410
    interval_gate = new IVP_Env_Interval_Gate();             // ctor 063605e0

    // ---- two more adopt-or-construct slots ---------------------------------

    range_manager = Parms->RangeManager;
    if (range_manager == nullptr)
    {
        range_manager = new IVP_Range_Manager(this, 1);        // FUN_06360360
    }

    performance_counter = Parms->PerformanceCounter;
    if (performance_counter == nullptr)
    {
        performance_counter = new IVP_PerformanceCounter_Simple(); // FUN_063602a0
    }

    cache_object_manager = new IVP_Cache_Object_Manager(Parms->NumberOfCacheObjects); // ctor 0634d5c0

    // ---- time/step scalars -------------------------------------------------
    // +0x100/+0x104 get the integer (ftol) and fractional parts of delta_time:
    // {0.0f, 0.0151515f} with the default 1/66.
    current_time = IVP_Time{};
    environment_manager = Registry;
    sim_step_count = 1;
    {
        const float Truncated = (float)(static_cast<long>(delta_time)); // ftol
        next_psi_time_base = Truncated;
        next_psi_time_offset = delta_time - Truncated;
    }

    // ---- the two scratch arenas (+0xA0 / +0xA4) ----------------------------
    short_term_mem = new IVP_U_Memory; // new(0x14), FUN_06344120
    sim_unit_mem = new IVP_U_Memory; // new(0x14), FUN_06344120

    // One caller-supplied block split in half. SAR (arithmetic shift), so a
    // negative size rounds toward negative infinity, unlike a division.
    {
        const int32_t HalfSize = Parms->MemoryBlockSize >> 1;
        short_term_mem->init_mem(Parms->MemoryBlock, HalfSize);
        sim_unit_mem->init_mem(Parms->MemoryBlock + HalfSize, HalfSize);
    }

    // ---- damping factor and the two global dispatch tables -----------------
    next_movement_check = 10;

    // pow(0.9, delta_time), open-coded in retail with FYL2X / F2XM1 / FSCALE.
    // DAT_063a5568 is (double)0.9f, not the double 0.9.
    integrated_energy_damp = (float)(
        std::pow((double)(0.9f), (double)(delta_time)));

    IVP_Mindist_Minimize_Solver::init_mms_function_table();        // FUN_06353940
    IVP_InitSimUnitDispatchTable();                                // FUN_06360090

    // ---- name, client data -------------------------------------------------
    // FUN_06356920 (IVP's strdup) returns null for a null string itself.
    environment_name = (Name != nullptr) ? _strdup(Name) : nullptr;
    client_data = ClientData;
    field_f0 = 10;

    // ---- static_object (+0x30) ---------------------------------------------
    // Retail builds the 0x58-byte construction template on its stack (the
    // stack layout identifies the type), passes it to FUN_0634cdf0 and tears
    // it down again.
    {
        float Bias = 1.0f;

        IVP_RealObjectConstructionTemplate Template; // FUN_063486c0
        Template.set_long_name("static_object");  // FUN_06348690

        // FUN_063481b0: zero-fills all 0x40 bytes before setting the
        // diagonal, so the row k[3] pads are zeroed too (IVP_U_Matrix::init()
        // alone would leave them).
        IVP_U_Matrix LocalCsys;
        std::memset(&LocalCsys, 0, sizeof(LocalCsys));
        LocalCsys.rows[0].k[0] = 1.0f;
        LocalCsys.rows[1].k[1] = 1.0f;
        LocalCsys.rows[2].k[2] = 1.0f;

        Template.FlagsHi = 1;
        // OptionalComOverride points at an IVP_U_Matrix (FUN_06345b20 reads
        // its vv at +0x30).
        Template.OptionalComOverride = &LocalCsys;

        // Redundant after the memset, but retail writes it separately.
        LocalCsys.vv.k[0] = 0.0f;
        LocalCsys.vv.k[1] = 0.0f;
        LocalCsys.vv.k[2] = 0.0f;

        IVP_U_Point Position;
        Position.k[0] = 0.0f;
        Position.k[1] = 0.0f;
        Position.k[2] = 0.0f;

        // Null orientation means identity.
        static_object = create_ball(&Bias, &Template, nullptr, &Position);

        // FUN_06348670: the template's destructor runs at scope exit.
    }

    state_magic = 0x1E240;                                    // 123456
    return this;
}

// ============================================================================
// IVP_Environment_Manager (see ivp_environment.h).
// ============================================================================

// DAT_0658aa00. Constructed/destroyed by static init/atexit, as retail's
// LAB_0634cc20 / LAB_0634cc50 do.
static IVP_Environment_Manager G_IVP_EnvironmentManager;

// FUN_0634cc70
IVP_Environment_Manager* IVP_Environment_Manager::get_environment_manager()
{
    return &G_IVP_EnvironmentManager;
}

// FUN_0634cba0. Retail returns 0 when operator new(0x148) fails; the port's
// throws instead.
IVP_Environment* IVP_Environment_Manager::create_environment(IVP_Environment_Create_Parms* Parms,
                                                             const char* LicenseName, uint32_t LicenseKey)
{
    IVP_Environment* Env = static_cast<IVP_Environment*>(::operator new(sizeof(IVP_Environment)));
    return Env->construct(this, Parms, LicenseName,
                          reinterpret_cast<void*>(static_cast<uintptr_t>(LicenseKey)));
}
