// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_cluster_manager.h"
#include "ivp_environment.h"
#include "ivp_real_object.h"
#include "ivp_ov_element.h"
#include "ivp_listener.h"

// FUN_0635d4d0 (IVP_VHash_HashPointer, declared in ivp_utility/ivp_vhash.h via
// ivp_cluster_manager.h): CRC-32 (table DAT_063edcdc) over the four bytes of
// the key, OR 0x80000000.

namespace
{
    unsigned HashObjectKey(IVP_Real_Object* Object)
    {
        return IVP_VHash_HashPointer((unsigned)Object); // FUN_0635d4d0
    }

    // 0635d510 / 0635d450: from index SizeMm down to 0, every stored table is destroyed (06348be0) and
    // freed (06391d5a).
    template <class TTable>
    void DeleteAllTables(IVP_VHash* Hash)
    {
        IVP_VHash_Slot* const Slots = Hash->Elems;
        for (int32_t I = Hash->SizeMm; I >= 0; --I)
        {
            TTable* const Table = static_cast<TTable*>(Slots[I].Elem);
            if (Table != nullptr)
            {
                delete Table;
            }
        }
    }
}

// ============================================================================
// The callback-table hashes.
// ============================================================================

// FUN_0635d510, reached through the deleting dtor
// 0634ae50 (slot 1 of 063a54c0). ~IVP_VHash (063522f0) runs after this body.
IVP_Object_Callback_Table_Hash::~IVP_Object_Callback_Table_Hash()
{
    DeleteAllTables<IVP_Object_Callback_Table>(this);
}

// LAB_0635d590 (RET 8): 1 when the two keys are equal, else 0.
int IVP_Object_Callback_Table_Hash::Compare(void* Elem0, void* Elem1)
{
    return static_cast<IVP_Object_Callback_Table*>(Elem0)->real_object ==
           static_cast<IVP_Object_Callback_Table*>(Elem1)->real_object ? 1 : 0;
}

// FUN_0634aef0 (RET 4): the probe holds only the key; its vector stays empty,
// so its destructor (06348be0) frees nothing.
IVP_Object_Callback_Table* IVP_Object_Callback_Table_Hash::find_table(IVP_Real_Object* Object)
{
    IVP_Object_Callback_Table Probe;
    Probe.real_object = Object;
    return static_cast<IVP_Object_Callback_Table*>(FindElem(&Probe, HashObjectKey(Object)));
}

// FUN_0634b350 (RET 4): the same probe through the
// removing lookup 06352480.
IVP_Object_Callback_Table* IVP_Object_Callback_Table_Hash::remove_table(IVP_Real_Object* Object)
{
    IVP_Object_Callback_Table Probe;
    Probe.real_object = Object;
    return static_cast<IVP_Object_Callback_Table*>(RemoveElem(&Probe, HashObjectKey(Object)));
}

// FUN_0635d450, through the deleting dtor 0634ae70
// (slot 1 of 063a54b8).
IVP_Collision_Callback_Table_Hash::~IVP_Collision_Callback_Table_Hash()
{
    DeleteAllTables<IVP_Collision_Callback_Table>(this);
}

// LAB_0635d590, shared with the object table hash (both vtables point at it).
int IVP_Collision_Callback_Table_Hash::Compare(void* Elem0, void* Elem1)
{
    return static_cast<IVP_Collision_Callback_Table*>(Elem0)->real_object ==
           static_cast<IVP_Collision_Callback_Table*>(Elem1)->real_object ? 1 : 0;
}

// FUN_0634aef0 (one body for both hashes; ECX is the hash).
IVP_Collision_Callback_Table* IVP_Collision_Callback_Table_Hash::find_table(IVP_Real_Object* Object)
{
    IVP_Collision_Callback_Table Probe;
    Probe.real_object = Object;
    return static_cast<IVP_Collision_Callback_Table*>(FindElem(&Probe, HashObjectKey(Object)));
}

// FUN_0634b350.
IVP_Collision_Callback_Table* IVP_Collision_Callback_Table_Hash::remove_table(IVP_Real_Object* Object)
{
    IVP_Collision_Callback_Table Probe;
    Probe.real_object = Object;
    return static_cast<IVP_Collision_Callback_Table*>(RemoveElem(&Probe, HashObjectKey(Object)));
}

// ============================================================================
// IVP_Cluster_Manager
// ============================================================================

// FUN_0634ad70. All six dwords are zeroed, +0x04 is stored before any
// allocation, then the root cluster
// (operator new(0x20) + 06345760), the object table hash (operator new(0x10) +
// IVP_VHash(0x10) + vtbl 063a54c0) and the collision table hash (vtbl 063a54b8).
IVP_Cluster_Manager::IVP_Cluster_Manager(IVP_Environment* Environment)
    : root_cluster(nullptr)
    , environment(nullptr)
    , obj_callback_hash(nullptr)
    , collision_callback_hash(nullptr)
    , an_object_to_be_checked(nullptr)
    , number_of_real_objects(0)
{
    environment = Environment;
    root_cluster = new IVP_Cluster(Environment);
    obj_callback_hash = new IVP_Object_Callback_Table_Hash(0x10);
    collision_callback_hash = new IVP_Collision_Callback_Table_Hash(0x10);
}

// FUN_0634b300: each member is deleted through its slot-0 / slot-1 deleting
// dtor when set and nulled either way.
IVP_Cluster_Manager::~IVP_Cluster_Manager()
{
    if (root_cluster != nullptr)
    {
        delete root_cluster; // slot 0 (1)
    }
    root_cluster = nullptr;

    if (obj_callback_hash != nullptr)
    {
        delete obj_callback_hash; // slot 1 (1)
    }
    obj_callback_hash = nullptr;

    if (collision_callback_hash != nullptr)
    {
        delete collision_callback_hash; // slot 1 (1)
    }
    collision_callback_hash = nullptr;
}

// FUN_0634b3d0 (RET 4): ++number_of_real_objects.
void IVP_Cluster_Manager::add_object(IVP_Real_Object* /*Object*/)
{
    ++number_of_real_objects;
}

// FUN_0634b3e0.
void IVP_Cluster_Manager::remove_object(IVP_Real_Object* Object)
{
    // The count drops before the first lookup.
    --number_of_real_objects;

    IVP_Object_Callback_Table* const ObjTable = obj_callback_hash->find_table(Object);
    if (ObjTable != nullptr)
    {
        obj_callback_hash->remove_table(Object);
        delete ObjTable; // 06348be0, then 06391d5a
    }

    IVP_Collision_Callback_Table* const CollTable = collision_callback_hash->find_table(Object);
    if (CollTable != nullptr)
    {
        collision_callback_hash->remove_table(Object);
        delete CollTable;
    }

    // Move the round-robin cursor past the object; a walk
    // that comes back to the object itself clears it.
    if (Object == an_object_to_be_checked)
    {
        IVP_Object* const Next = get_next_real_object_in_cluster_tree(an_object_to_be_checked);
        an_object_to_be_checked = Next;
        if (Object == Next)
        {
            an_object_to_be_checked = nullptr;
        }
    }

    // environment->universe_manager (+0x2c), slot 2.
    IVP_Universe_Manager* const UniverseManager = environment->universe_manager;
    if (UniverseManager != nullptr)
    {
        UniverseManager->event_object_deleted(Object);
    }
}

// ============================================================================
// FUN_06345e40.
// ============================================================================
int32_t IVP_Real_Object::get_collision_check_count() const
{
    const IVP_OV_Element* Element = ov_element; // +0x90
    if (Element == nullptr)
    {
        return -1;
    }
    return static_cast<int32_t>(Element->collision_fvector.n_elems); // word [element+0x2a], zero-extended
}

// ============================================================================
// FUN_0634b1a0 (RET 4). Control flow follows retail label by label. The +0x1c
// read is IVP_Cluster::first_object (only done on objects whose type is 1, or
// on the root).
// ============================================================================
IVP_Object* IVP_Cluster_Manager::get_next_real_object_in_cluster_tree(IVP_Object* Object)
{
    if (number_of_real_objects == 0)
    {
        return nullptr;
    }

    IVP_Object* Current = Object;
    IVP_Object* Next;
    for (;;)
    {
        // LAB_0634b1b5
        if (Current == nullptr)
        {
            Current = get_root_cluster()->first_object; // +0x1c
            Next = Current;
            if (Current == nullptr)
            {
                return nullptr;
            }
        }
        else
        {
            Next = Current->next_object; // +0x08
        }

        // LAB_0634b1ce -- descend through clusters to the first non-cluster child.
        if (Current->object_type == IVP_CLUSTER)
        {
            for (;;)
            {
                Current = static_cast<IVP_Cluster*>(Current)->first_object;
                if (Current == nullptr)
                {
                    break;
                }
                if (Current->object_type != IVP_CLUSTER)
                {
                    return Current;
                }
            }
        }

        // LAB_0634b1e1 -- no sibling: climb until an ancestor has one.
        if (Next == nullptr)
        {
            for (;;)
            {
                IVP_Object* Child = Current;
                Current = Current->parent;   // +0x10
                if (Current == nullptr)
                {
                    IVP_Cluster* Root = get_root_cluster();
                    if (Root == Child)
                    {
                        return nullptr;
                    }
                    Current = Root->first_object;
                    if (Current == nullptr)
                    {
                        return nullptr;
                    }
                }
                // LAB_0634b200
                Next = Current->next_object;
                if (Next != nullptr)
                {
                    break;
                }
            }
        }

        // LAB_0634b207
        Current = Next;
        if (Next->object_type != IVP_CLUSTER)
        {
            return Next;
        }
        if (number_of_real_objects == 0)
        {
            return nullptr;
        }
    }
}

// ============================================================================
// FUN_0634b230 (RET 4).
// ============================================================================
void IVP_Cluster_Manager::check_for_unused_objects(IVP_Universe_Manager* UniverseManager)
{
    const IVP_Universe_Manager_Settings* Settings = UniverseManager->provide_universe_settings();
    if (number_of_real_objects < Settings->num_objects_in_environment_threshold_0)
    {
        return;
    }

    // ftol(delta_time * int), truncation.
    int32_t Remaining = static_cast<int32_t>((double)(environment->delta_time) *
                                             Settings->check_objects_per_second_threshold_0);
    for (; Remaining >= 0; --Remaining)
    {
        // The cursor advances before the null test.
        IVP_Object* Candidate = an_object_to_be_checked;
        an_object_to_be_checked = get_next_real_object_in_cluster_tree(Candidate);
        if (Candidate == nullptr)
        {
            return;
        }

        if ((static_cast<IVP_Real_Object*>(Candidate))->get_collision_check_count() != 0)
            continue;

        UniverseManager->object_no_longer_needed(Candidate);
        if (number_of_real_objects < Settings->num_objects_in_environment_threshold_0)
        {
            return;
        }
    }

    if (number_of_real_objects < Settings->num_objects_in_environment_threshold_1)
    {
        return;
    }

    Remaining = static_cast<int32_t>((double)(environment->delta_time) *
                                     Settings->check_objects_per_second_threshold_1);
    for (; Remaining >= 0; --Remaining)
    {
        // Here the null test comes first.
        IVP_Object* Candidate = an_object_to_be_checked;
        if (Candidate == nullptr)
        {
            return;
        }
        an_object_to_be_checked = get_next_real_object_in_cluster_tree(Candidate);

        if ((static_cast<IVP_Real_Object*>(Candidate))->get_collision_check_count() != 1)
            continue;

        UniverseManager->object_no_longer_needed(Candidate);
        if (number_of_real_objects < Settings->num_objects_in_environment_threshold_1)
        {
            return;
        }
    }
}

// ============================================================================
// The fire loops, one shape for all seven (e.g. 0634ae90): find the object's
// table; if there is one, walk its listeners from the last to the first; after
// each call with more listeners left (index > 0), look the table up again and
// stop if it has gone. The loop keeps using the table it found first; only the
// existence check is repeated.
// ============================================================================

// FUN_0634ae90 (RET 4): key = Event->Object (+0x04), listener slot 0.
void IVP_Cluster_Manager::fire_event_object_deleted(IVP_Environment_Object_Notify_Event* Event)
{
    IVP_Real_Object* const Object = Event->Object;
    IVP_Object_Callback_Table* const Table = obj_callback_hash->find_table(Object);
    if (Table == nullptr)
    {
        return;
    }
    for (int32_t I = static_cast<int32_t>(Table->listeners.n_elems) - 1; I >= 0; --I)
    {
        Table->listeners.elems[I]->event_object_deleted(Event);
        if (I > 0 && obj_callback_hash->find_table(Object) == nullptr)
        {
            return;
        }
    }
}

// FUN_0634af70 (RET 4): listener slot 3 (+0xc).
void IVP_Cluster_Manager::fire_event_object_frozen(IVP_Environment_Object_Notify_Event* Event)
{
    IVP_Real_Object* const Object = Event->Object;
    IVP_Object_Callback_Table* const Table = obj_callback_hash->find_table(Object);
    if (Table == nullptr)
    {
        return;
    }
    for (int32_t I = static_cast<int32_t>(Table->listeners.n_elems) - 1; I >= 0; --I)
    {
        Table->listeners.elems[I]->event_object_frozen(Event);
        if (I > 0 && obj_callback_hash->find_table(Object) == nullptr)
        {
            return;
        }
    }
}

// FUN_0634afd0 (RET 4): listener slot 2 (+0x8).
void IVP_Cluster_Manager::fire_event_object_revived(IVP_Environment_Object_Notify_Event* Event)
{
    IVP_Real_Object* const Object = Event->Object;
    IVP_Object_Callback_Table* const Table = obj_callback_hash->find_table(Object);
    if (Table == nullptr)
    {
        return;
    }
    for (int32_t I = static_cast<int32_t>(Table->listeners.n_elems) - 1; I >= 0; --I)
    {
        Table->listeners.elems[I]->event_object_revived(Event);
        if (I > 0 && obj_callback_hash->find_table(Object) == nullptr)
        {
            return;
        }
    }
}

// FUN_0634b030 (RET 8): enabled_callbacks bit 1, slot 0 with Event.
void IVP_Cluster_Manager::fire_event_pre_collision(IVP_Real_Object* Object, IVP_Event_Collision* Event)
{
    IVP_Collision_Callback_Table* const Table = collision_callback_hash->find_table(Object);
    if (Table == nullptr)
    {
        return;
    }
    for (int32_t I = static_cast<int32_t>(Table->listeners.n_elems) - 1; I >= 0; --I)
    {
        IVP_Listener_Collision* const Listener = Table->listeners.elems[I];
        if ((static_cast<uint8_t>(Listener->enabled_callbacks) & IVP_LISTENER_COLLISION_CALLBACK_PRE_COLLISION) == 0)
        {
            continue;
        }
        Listener->event_pre_collision(Event);
        if (I > 0 && collision_callback_hash->find_table(Object) == nullptr)
        {
            return;
        }
    }
}

// FUN_0634b090 (RET 4): enabled_callbacks bit 2, slot 1 with the object itself
// (key and argument are the same stack dword).
void IVP_Cluster_Manager::fire_event_collision_object_deleted(IVP_Real_Object* Object)
{
    IVP_Collision_Callback_Table* const Table = collision_callback_hash->find_table(Object);
    if (Table == nullptr)
    {
        return;
    }
    for (int32_t I = static_cast<int32_t>(Table->listeners.n_elems) - 1; I >= 0; --I)
    {
        IVP_Listener_Collision* const Listener = Table->listeners.elems[I];
        if ((static_cast<uint8_t>(Listener->enabled_callbacks) & IVP_LISTENER_COLLISION_CALLBACK_OBJECT_DELETED) == 0)
        {
            continue;
        }
        Listener->event_collision_object_deleted(Object);
        if (I > 0 && collision_callback_hash->find_table(Object) == nullptr)
        {
            return;
        }
    }
}

// FUN_0634b0e0 (RET 8): enabled_callbacks bit 4, slot 2 with Event.
void IVP_Cluster_Manager::fire_event_friction_created(IVP_Real_Object* Object, void* Event)
{
    IVP_Collision_Callback_Table* const Table = collision_callback_hash->find_table(Object);
    if (Table == nullptr)
    {
        return;
    }
    for (int32_t I = static_cast<int32_t>(Table->listeners.n_elems) - 1; I >= 0; --I)
    {
        IVP_Listener_Collision* const Listener = Table->listeners.elems[I];
        if ((static_cast<uint8_t>(Listener->enabled_callbacks) & IVP_LISTENER_COLLISION_CALLBACK_FRICTION) == 0)
        {
            continue;
        }
        Listener->event_friction_created(Event);
        if (I > 0 && collision_callback_hash->find_table(Object) == nullptr)
        {
            return;
        }
    }
}

// FUN_0634b140 (RET 8): enabled_callbacks bit 4, slot 3 with Event.
void IVP_Cluster_Manager::fire_event_friction_deleted(IVP_Real_Object* Object, void* Event)
{
    IVP_Collision_Callback_Table* const Table = collision_callback_hash->find_table(Object);
    if (Table == nullptr)
    {
        return;
    }
    for (int32_t I = static_cast<int32_t>(Table->listeners.n_elems) - 1; I >= 0; --I)
    {
        IVP_Listener_Collision* const Listener = Table->listeners.elems[I];
        if ((static_cast<uint8_t>(Listener->enabled_callbacks) & IVP_LISTENER_COLLISION_CALLBACK_FRICTION) == 0)
        {
            continue;
        }
        Listener->event_friction_deleted(Event);
        if (I > 0 && collision_callback_hash->find_table(Object) == nullptr)
        {
            return;
        }
    }
}
