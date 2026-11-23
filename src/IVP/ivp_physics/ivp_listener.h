// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ============================================================================
// IVP listener / collision-filter / performance-counter interfaces (IVP SDK).
// Names come from the RTTI of the game classes deriving from them:
//   PhysicalSystem (COL .rdata:063a7344 / 063a73e0 / 063a7330) lists
//     IVP_Listener_Object at +0, IVP_Listener_Collision at +4,
//     IVP_Collision_Filter_Coll_Group_Ident and IVP_Collision_Filter at +0xc;
//   PhySys::PhyPerfCounter (COL .rdata:063a6b68) lists IVP_PerformanceCounter.
// Slot order and arity come from each vtable and the overriding bodies.
// ============================================================================

#include <cstdint>

class IVP_Environment;
class IVP_Real_Object;
class IVP_Compact_Edge;

// The event record IVP passes to IVP_Listener_Object slots: {environment,
// real object}. Declared in ivp_environment.h.
struct IVP_Environment_Object_Notify_Event;

// What an IVP_Event_Collision points at (the IVP_Impact_Solver_Long_Term).
// Only the members PhysicalSystem::event_pre_collision (0628c42b) reads are
// named; the object is larger.
struct IVP_Contact_Situation
{
    float surf_normal[4];                     // +0x00 (x, y, z read)
    float speed[4];                           // +0x10 (x, y, z read)
    float contact_point_ws[4];                // +0x20 (x, y, z read)
    IVP_Real_Object* objects[2];              // +0x30 / +0x34
    const IVP_Compact_Edge* compact_edges[2]; // +0x38 / +0x3c
};

// Built on the stack by IVP_Mindist_Do_Impact before it fires the collision
// listeners (FUN_0634cf60).
struct IVP_Event_Collision
{
    float d_time_since_last_collision;        // +0x00
    IVP_Environment* environment;             // +0x04
    IVP_Contact_Situation* contact_situation; // +0x08
};

// vtable .rdata:0639c288: four _purecall slots + deleting dtor LAB_06289740.
// IVP fires the slots through env->object_listeners (+0x124):
// FUN_0634ced0 slot 0, FUN_0634cea0 slot 1, FUN_0634cf30 slot 2, FUN_0634cf00 slot 3.
class IVP_Listener_Object
{
public:
    virtual void event_object_deleted(IVP_Environment_Object_Notify_Event* Event) = 0; // slot 0
    virtual void event_object_created(IVP_Environment_Object_Notify_Event* Event) = 0; // slot 1
    virtual void event_object_revived(IVP_Environment_Object_Notify_Event* Event) = 0; // slot 2
    virtual void event_object_frozen(IVP_Environment_Object_Notify_Event* Event) = 0;  // slot 3
    virtual ~IVP_Listener_Object() {}                                                  // slot 4
};

// IVP_Listener_Collision::enabled_callbacks bit gated by FUN_0634cf60.
enum : int32_t
{
    IVP_LISTENER_COLLISION_CALLBACK_PRE_COLLISION = 0x01,
};

// vtable .rdata:0639c2a0: four `RET 4` no-op slots (.text:062ed730) + deleting
// dtor LAB_06289770; one data member at +4. IVP keeps these in
// env->vector_listeners_c8 (FUN_0634d050 appends, FUN_0634cf60 fires slot 0).
class IVP_Listener_Collision
{
public:
    explicit IVP_Listener_Collision(int32_t InEnabledCallbacks)
        : enabled_callbacks(InEnabledCallbacks)
    {
    }

    virtual void event_pre_collision(IVP_Event_Collision* /*Event*/) {}  // slot 0
    // slot 1: fired by IVP_Cluster_Manager 0634b090 (enabled_callbacks & 2)
    // from ~IVP_Real_Object with the object being deleted.
    virtual void event_collision_object_deleted(IVP_Real_Object* /*Object*/) {}
    virtual void event_friction_created(void* /*Arg*/) {}                 // slot 2 (enabled_callbacks & 4): 0634b0e0, 0634cfa0
    virtual void event_friction_deleted(void* /*Arg*/) {}                 // slot 3 (enabled_callbacks & 4): 0634b140, 0634cfe0
    virtual ~IVP_Listener_Collision() {}                              // slot 4

    int32_t enabled_callbacks; // +0x04
};

// ============================================================================
// IVP_Listener_Hull: the payloads of an IVP_Hull_Manager's priority pool
// (library name). Abstract base vtable .rdata:063a5694, 4 slots: three
// _purecall + 0628f050 (`RET 8`). The hull manager
// dispatches slot 1 when the hull value passes an entry's key
// (FUN_06351f80 / FUN_06351b90, IVP_Calc_Next_PSI_Solver), slot 2 when it is
// destroyed (FUN_063521f0) and slot 3 when it rebases (FUN_06352220, args
// (-outer, -inner)). Implementations: IVP_OV_Element (vtable 063a5680) and the
// mindist synapses (vtable 063a55cc). No virtual destructor at this level.
// +0x04 is the entry's pool slot index, the full dword the insert returned.
// ============================================================================
class IVP_Hull_Manager;
class IVP_Listener_Hull
{
public:
    virtual int32_t get_type() = 0;                                                        // slot 0
    virtual void hull_limit_exceeded_event(IVP_Hull_Manager* HullManager, float HullIntrusionValue) = 0; // slot 1 (RET 8)
    virtual void hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* HullManager) = 0;  // slot 2 (RET 4)
    virtual void hull_manager_is_reset(float /*DeltaTime*/, float /*CenterDeltaTime*/) {}   // slot 3, 0628f050 `RET 8`

    uint32_t HullPoolSlot; // +0x04

protected:
    IVP_Listener_Hull() {}
    ~IVP_Listener_Hull() {}
};

// The record IVP_Environment's PSI broadcast (FUN_0634d020) passes: a
// one-dword stack local holding the environment.
struct IVP_Event_PSI
{
    IVP_Environment* environment; // +0x00
};

// Elements of IVP_Environment::vector_listeners_d0 (+0xd0), library name
// IVP_Listener_PSI. No implementation was traced, so only the two called
// slots are declared.
class IVP_Listener_PSI
{
public:
    virtual void event_PSI(IVP_Event_PSI* Event) = 0; // slot 0, FUN_0634d020
    // slot 1: ~IVP_Environment calls it on every element, last to first.
    virtual void environment_will_be_deleted(IVP_Environment* Environment) = 0;
};

// vtable .rdata:0639c40c: two _purecall slots + deleting dtor FUN_0628edb0
// (stamps 0639c40c through FUN_0634bc50).
class IVP_Collision_Filter
{
public:
    // slot 0: nonzero (IVP_BOOL) = the pair may be collision-tested.
    virtual int32_t check_objects_for_collision_detection(IVP_Real_Object* A, IVP_Real_Object* B) = 0;
    // slot 1
    virtual void environment_will_be_deleted(IVP_Environment* Env) = 0;
    // slot 2
    virtual ~IVP_Collision_Filter() {}
};

// vtable .rdata:0639c3fc, ctor FUN_0634bcb0, 8 bytes.
class IVP_Collision_Filter_Coll_Group_Ident : public IVP_Collision_Filter
{
public:
    // FUN_0634bcb0
    explicit IVP_Collision_Filter_Coll_Group_Ident(int32_t bInDeleteOnEnvDelete);

    // FUN_0634bc60
    virtual int32_t check_objects_for_collision_detection(IVP_Real_Object* A, IVP_Real_Object* B) override;
    // LAB_0634bcd0
    virtual void environment_will_be_deleted(IVP_Environment* Env) override;
    // FUN_0628ed90 (stamps 0639c3fc through FUN_0634bca0, then the base's).
    virtual ~IVP_Collision_Filter_Coll_Group_Ident() {}

    int32_t bDeleteOnEnvDelete; // +0x04
};

// vtable .rdata:0639bac8: five _purecall slots + deleting dtor LAB_06285e10.
// Arities from PhySys::PhyPerfCounter's overrides (vtable 0639baac):
// RET / RET 4 / RET / RET 4 / RET 8.
class IVP_PerformanceCounter
{
public:
    virtual void start_pcount() = 0;                                                  // slot 0
    virtual void pcount(int32_t Arg) = 0;                                       // slot 1
    virtual void stop_pcount() = 0;                                                  // slot 2
    // slot 3: PhyPerfCounter's body (LAB_06285e30) deletes the counter.
    virtual void environment_is_going_to_be_deleted(IVP_Environment* Env) = 0;
    // slot 4: 8 bytes of stack arguments (IVP_PerformanceCounter_Simple reads
    // them as an IVP_Time).
    virtual void reset_and_print_performance_counters(float Arg0, float Arg1) = 0;
    virtual ~IVP_PerformanceCounter() {}                                        // slot 5
};
