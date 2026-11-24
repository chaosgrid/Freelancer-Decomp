// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_phantom.h"

#include "ivp_real_object.h"
#include "ivp_environment.h"
#include "ivp_mindist.h"   // IVP_Ledge_Mindist / IVP_Synapse_Real
#include "ivp_ov_element.h" // IVP_Mindist_Manager::RecheckOvElement

// IVP_Controller_Phantom bodies. Layout and the listener / set contracts are
// in ivp_phantom.h and ivp_u_set_active.h.

namespace
{
    // The mindist set holds IVP_Mindist_Base, IVP_Ledge_Mindist's base class;
    // the upcast keeps the address.
    IVP_Mindist_Base* IVP_Phantom_AsMindistBase(IVP_Ledge_Mindist* Mindist)
    {
        return Mindist;
    }

    // The intruder of a mindist as retail selects it inline: synapse[0]'s
    // object, or synapse[1]'s when synapse[0]'s is the phantom's own object.
    IVP_Real_Object* IVP_Phantom_OtherObject(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* PhantomObject)
    {
        IVP_Real_Object* Other = Mindist->synapse[0].l_obj; // +0x28
        if (Other == PhantomObject)
        {
            Other = Mindist->synapse[1].l_obj; // +0x44
        }
        return Other;
    }
}

// FUN_06344710
IVP_Controller_Phantom::IVP_Controller_Phantom(IVP_Real_Object* Object, const IVP_Template_Phantom* Template)
    : listeners()
    , mindist_set(0x10)     // IVP_VHash(0x10), listener vector(0)
{
    object = Object;

    // The set pointer is stored before the store is allocated.
    if (Template->manage_intruding_objects != 0)
    {
        set_of_objects = new IVP_U_Set_Active<IVP_Real_Object>(0x10); // operator new(0x18)
        object_store = new IVP_VHash_Store(0x10);                     // operator new(0x14), FUN_063525f0
    }
    else
    {
        set_of_objects = nullptr;
        object_store = nullptr;
    }

    // The same for cores (+0x34 before +0x30).
    if (Template->manage_intruding_cores != 0)
    {
        set_of_cores = new IVP_U_Set_Active<IVP_Core>(0x10);
        core_store = new IVP_VHash_Store(0x10);
    }
    else
    {
        set_of_cores = nullptr;
        core_store = nullptr;
    }

    // An owner whose movement byte (+0x78) is exactly 0x10 becomes 9 and is
    // re-checked by the mindist manager, unless the template forbids it.
    if (Template->dont_check_for_unmoveables == 0 && static_cast<uint8_t>(object->flags) == 0x10)
    {
        object->flags = (object->flags & 0xffffff09u) | 0x9u;
        object->environment->mindist_manager->RecheckOvElement(object); // FUN_0634ebd0
    }

    exit_policy_extra_radius = Template->exit_policy_extra_radius;
    client_data = Template->client_data;

    // Every invalid mindist of the owner (+0x24) enters: 0x1000 set / 0x2000
    // cleared before the call, 0x400 set / 0x800 cleared after it (re-read).
    // The next link is read after the call.
    for (IVP_Synapse_Real* Synapse = object->invalid_synapse_list_head; Synapse != nullptr; Synapse = Synapse->next)
    {
        IVP_Ledge_Mindist* const Mindist = Synapse->get_mindist();
        Mindist->StateFlags = (Mindist->StateFlags & 0xffffdfffu) | 0x1000u;
        mindist_entered_volume(Mindist);
        Mindist->StateFlags = (Mindist->StateFlags & 0xfffff7ffu) | 0x400u;
    }

    // Every exact mindist of the owner (+0x20): a gap below 0 (or NaN) enters;
    // otherwise the precomputed flags are stored with 0xc00 cleared, without
    // re-reading.
    for (IVP_Synapse_Real* Synapse = object->synapse_list_head; Synapse != nullptr; Synapse = Synapse->next)
    {
        IVP_Ledge_Mindist* const Mindist = Synapse->get_mindist();
        const uint32_t Flags = (Mindist->StateFlags & 0xffffdfffu) | 0x1000u;
        Mindist->StateFlags = Flags;
        if (!(Mindist->SeparationGap >= 0.0f))
        {
            mindist_entered_volume(Mindist);
            Mindist->StateFlags = (Mindist->StateFlags & 0xfffff7ffu) | 0x400u;
        }
        else
        {
            Mindist->StateFlags = Flags & 0xfffff3ffu;
        }
    }

    // Walk the owner's hull-manager pool (+0x64) from PoolLiveIndex. A null
    // payload ends the walk; a listener with get_type() == 0 is a synapse, and
    // its mindist gets 0x1000 / ~0x2000 unless 0x3000 is already set.
    IVP_Priority_Pool& Pool = object->hull_manager.Pool();
    uint32_t Index = Pool.PoolLiveIndex;
    while (Index != 0xffffu)
    {
        IVP_Priority_Pool::Entry* const Entry = &Pool.PoolEntries[Index];
        IVP_Listener_Hull* const Listener = static_cast<IVP_Listener_Hull*>(Entry->Payload);
        Index = Entry->Next;
        if (Listener == nullptr)
        {
            break;
        }
        if (Listener->get_type() != 0)
        {
            continue;
        }
        IVP_Ledge_Mindist* const Mindist = static_cast<IVP_Synapse_Real*>(Listener)->get_mindist();
        if ((Mindist->StateFlags & 0x3000u) != 0)
        {
            continue;
        }
        Mindist->StateFlags = (Mindist->StateFlags & 0xffffdfffu) | 0x1000u;
    }

    // +0x38 / +0x3c are not written (left as allocated).
}

// FUN_063442c0
IVP_Controller_Phantom::~IVP_Controller_Phantom()
{
    // Every listener, last first, slot 2.
    for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
    {
        listeners.elems[I]->phantom_is_going_to_be_deleted_event(this);
    }

    object->controller_phantom = nullptr;

    // In this order: +0x30, +0x2c (FUN_06352620 + operator delete), +0x28,
    // +0x34 (slot 1). Each pointer is nulled after its test.
    delete core_store;
    core_store = nullptr;
    delete object_store;
    object_store = nullptr;
    delete set_of_objects;
    set_of_objects = nullptr;
    delete set_of_cores;
    set_of_cores = nullptr;

    // mindist_set (inline FUN_06344a00) and the listener vector follow as
    // member destructors.
}

// FUN_06344400
void IVP_Controller_Phantom::mindist_entered_volume(IVP_Ledge_Mindist* Mindist)
{
    IVP_Mindist_Base* const MindistBase = IVP_Phantom_AsMindistBase(Mindist);

    mindist_set.add_element(MindistBase);

    // The store counts this phantom's mindists touching the object; only the
    // first adds it to the set.
    if (set_of_objects != nullptr)
    {
        IVP_Real_Object* const Other = IVP_Phantom_OtherObject(Mindist, object);
        const int32_t Count = static_cast<int32_t>(reinterpret_cast<intptr_t>(object_store->FindElem(Other))); // FUN_06352930
        if (Count != 0)
        {
            object_store->ChangeElem(Other, reinterpret_cast<void*>(static_cast<intptr_t>(Count + 1))); // FUN_063529e0
        }
        else
        {
            object_store->AddElem(Other, reinterpret_cast<void*>(static_cast<intptr_t>(1))); // FUN_063526d0
            set_of_objects->add_element(Other);
        }
    }

    // The same for the object's core (+0x98).
    if (set_of_cores != nullptr)
    {
        IVP_Core* const Core = IVP_Phantom_OtherObject(Mindist, object)->core;
        const int32_t Count = static_cast<int32_t>(reinterpret_cast<intptr_t>(core_store->FindElem(Core)));
        if (Count != 0)
        {
            core_store->ChangeElem(Core, reinterpret_cast<void*>(static_cast<intptr_t>(Count + 1)));
        }
        else
        {
            core_store->AddElem(Core, reinterpret_cast<void*>(static_cast<intptr_t>(1)));
            set_of_cores->add_element(Core);
        }
    }

    // Every listener, last first, slot 0.
    for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
    {
        listeners.elems[I]->mindist_entered_volume(this, MindistBase);
    }
}

// FUN_06344580: the mirror of FUN_06344400.
void IVP_Controller_Phantom::mindist_left_volume(IVP_Ledge_Mindist* Mindist)
{
    IVP_Mindist_Base* const MindistBase = IVP_Phantom_AsMindistBase(Mindist);

    mindist_set.remove_element(MindistBase);

    // Signed compare: a count above 1 is decremented, anything else removes.
    if (set_of_objects != nullptr)
    {
        IVP_Real_Object* const Other = IVP_Phantom_OtherObject(Mindist, object);
        const int32_t Count = static_cast<int32_t>(reinterpret_cast<intptr_t>(object_store->FindElem(Other)));
        if (Count > 1)
        {
            object_store->ChangeElem(Other, reinterpret_cast<void*>(static_cast<intptr_t>(Count - 1)));
        }
        else
        {
            object_store->RemoveElem(Other); // FUN_063527e0
            set_of_objects->remove_element(Other);
        }
    }

    if (set_of_cores != nullptr)
    {
        IVP_Core* const Core = IVP_Phantom_OtherObject(Mindist, object)->core;
        const int32_t Count = static_cast<int32_t>(reinterpret_cast<intptr_t>(core_store->FindElem(Core)));
        if (Count > 1)
        {
            core_store->ChangeElem(Core, reinterpret_cast<void*>(static_cast<intptr_t>(Count - 1)));
        }
        else
        {
            core_store->RemoveElem(Core);
            set_of_cores->remove_element(Core);
        }
    }

    // Every listener, last first, slot 1.
    for (int32_t I = static_cast<int32_t>(listeners.n_elems) - 1; I >= 0; --I)
    {
        listeners.elems[I]->mindist_left_volume(this, MindistBase);
    }
}

// FUN_06344230
void IVP_Controller_Phantom::add_listener_phantom(IVP_Listener_Phantom* Listener)
{
    listeners.Add(Listener);
}

// FUN_06344260: inlined remove_element (backward search, then shift down).
void IVP_Controller_Phantom::remove_listener_phantom(IVP_Listener_Phantom* Listener)
{
    // Index ends at -1 when Listener is absent or the vector is empty.
    int32_t Index = static_cast<int32_t>(listeners.n_elems) - 1;
    while (Index >= 0)
    {
        if (listeners.elems[Index] == Listener)
        {
            break;
        }
        --Index;
    }

    // Reproduced on purpose: retail has no not-found test. The count drops
    // regardless (an empty vector wraps to 0xffff) and the shift runs from
    // Index = -1, so an absent listener writes elems[-1] = elems[0] and shifts
    // everything down; on an empty vector it walks 0x10000 slots of foreign
    // memory. The bound re-reads the 16-bit count each pass.
    listeners.n_elems = static_cast<unsigned short>(listeners.n_elems - 1);
    for (int32_t I = Index; I < static_cast<int32_t>(listeners.n_elems); ++I)
    {
        listeners.elems[I] = listeners.elems[I + 1];
    }
}
