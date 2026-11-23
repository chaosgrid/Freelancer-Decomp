// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

#include "../ivp_physics/ivp_time_event.h" // IVP_Time_Event (IVP_Collision's base)

// ============================================================================
// IVP_Collision / IVP_Collision_Delegator / IVP_Collision_Delegator_Root /
// IVP_Collision_Delegator_Root_Mindist
// ----------------------------------------------------------------------------
// Class and method names are the Ipion (IVP) library's own (Common.dll's PhySys
// exports name IVP_Mindist_Base / IVP_Controller_Phantom in their mangled
// signatures); slot order, arity and bodies follow the retail listing.
//
// Retail vtables (.rdata):
//   063a5750  IVP_Collision_Delegator       purecall / 06360dd0 (deleting dtor)
//                                           / 062ed730 (RET 4) / 06306080 (-1)
//   063a5760  IVP_Collision_Delegator_Root_Mindist
//                                           06360c40 / 06360e10 / 062ed730 /
//                                           06306080 / 06360c00 / 06360c80 /
//                                           06360c30
//   063a55b8  IVP_Collision                 0634dc00 (CORE trap) / purecall /
//                                           purecall / 0634dc10 / 0634dc30
// ============================================================================

class IVP_Collision;
class IVP_Environment;
class IVP_Real_Object;
class IVP_Compact_Ledge;

// ----------------------------------------------------------------------------
// IVP_Collision_Delegator -- vtable 063a5750, 4 bytes (vptr only).
// In-place dtor FUN_06360dc0, deleting dtor LAB_06360dd0.
// ----------------------------------------------------------------------------
class IVP_Collision_Delegator
{
public:
    // Slot 0 (purecall in the base).
    virtual void collision_is_going_to_be_deleted_event(IVP_Collision* Collision) = 0;

    // Slot 1. FUN_06360dc0 + LAB_06360dd0.
    virtual ~IVP_Collision_Delegator() {}

    // Slot 2. 062ed730: the change is ignored.
    virtual void change_spawned_mindist_count(int32_t /*Change*/) {}

    // Slot 3. 06306080.
    virtual int32_t get_spawned_mindist_count() { return -1; }
};

// ----------------------------------------------------------------------------
// IVP_Collision -- vtable 063a55b8. Base of every pairwise collision record
// filed in IVP_OV_Element::collision_fvector (IVP_Mindist and the 0x40-byte
// object watcher FUN_06369be0 build on it).
//
// The ctor is inlined at the start of FUN_0634db70 (IVP_Mindist_Base ctor).
// +0x04 is IVP_Time_Event::EventQueueHandle; this ctor does not write it
// (FUN_0634db70 stores 0xffff there later, .text:0634dbe5).
// ----------------------------------------------------------------------------
class IVP_Collision : public IVP_Time_Event
{
public:
    IVP_Collision_Delegator* delegator;  // +0x08
    int32_t IndexInElement0;             // +0x0c: index in one object's IVP_OV_Element::collision_fvector, -1 = none
    int32_t IndexInElement1;             // +0x10: index in the other object's, -1 = none

    explicit IVP_Collision(IVP_Collision_Delegator* Delegator)
        : delegator(Delegator)
        , IndexInElement0(-1)
        , IndexInElement1(-1)
    {
    }

    // Slot 0. LAB_0634dc00: the library's CORE trap (null store) for an event
    // nobody overrides; reproduced via IVP_CoreTrap (ivp_utility/ivp_core_trap.h).
    void simulate_time_event(IVP_Environment* Environment) override;

    // Slot 1 (purecall). Fills Out[0], Out[1] with the pair's two objects.
    virtual void get_objects(IVP_Real_Object* Out[2]) = 0;

    // Slot 2 (purecall). Fills Out[0], Out[1] with the pair's two ledges.
    virtual void get_ledges(const IVP_Compact_Ledge* Out[2]) = 0;

    // Slot 3. LAB_0634dc10: `delete this`; the argument is not read.
    virtual void delegator_is_going_to_be_deleted_event(IVP_Collision_Delegator* Delegator);

    // Slot 4. LAB_0634dc30.
    virtual ~IVP_Collision() {}
};

// ----------------------------------------------------------------------------
// IVP_Collision_Delegator_Root -- the interface IVP_Environment keeps in its
// vector at +0x12c. Slots 4..6 are dispatched by FUN_0634caf0 (slot 4),
// IVP_Mindist_Manager::RecheckOvElement (FUN_0634ebd0, slot 5, :0634f054)
// and the environment destructor FUN_0634c6f0 (slot 6, :0634c882).
// Retail has no vtable of its own for this level (the derived class's
// deleting destructor goes straight to FUN_06360dc0).
// ----------------------------------------------------------------------------
class IVP_Collision_Delegator_Root : public IVP_Collision_Delegator
{
public:
    // Slot 4.
    virtual void object_is_removed_from_collision_detection(IVP_Real_Object* Object) = 0;

    // Slot 5. Returns the new collision record (never null in retail).
    virtual IVP_Collision* delegate_collisions_for_object(IVP_Real_Object* BaseObject,
                                                          IVP_Real_Object* CollidingElement) = 0;

    // Slot 6.
    virtual void environment_is_going_to_be_deleted_event(IVP_Environment* Environment) = 0;
};

// ----------------------------------------------------------------------------
// IVP_Collision_Delegator_Root_Mindist -- vtable 063a5760, 4 bytes, no fields
// (ctor FUN_06360df0). IVP_Environment's ctor (FUN_0634bf50, .text:0634c17b)
// builds one when the creation parameters supply no delegator and appends it
// to env+0x12c. Definitions: src/IVP/ivp_physics/ivp_ov_element.cpp.
// ----------------------------------------------------------------------------
class IVP_Collision_Delegator_Root_Mindist : public IVP_Collision_Delegator_Root
{
public:
    // Slot 0. LAB_06360c40.
    void collision_is_going_to_be_deleted_event(IVP_Collision* Collision) override;

    // Slot 1. FUN_06360e10 -> FUN_06360dc0.
    ~IVP_Collision_Delegator_Root_Mindist() override {}

    // Slot 4. LAB_06360c00.
    void object_is_removed_from_collision_detection(IVP_Real_Object* Object) override;

    // Slot 5. FUN_06360c80.
    IVP_Collision* delegate_collisions_for_object(IVP_Real_Object* BaseObject,
                                                  IVP_Real_Object* CollidingElement) override;

    // Slot 6. LAB_06360c30.
    void environment_is_going_to_be_deleted_event(IVP_Environment* Environment) override;
};
