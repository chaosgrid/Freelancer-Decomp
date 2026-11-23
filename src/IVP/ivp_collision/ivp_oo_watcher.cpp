// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_OO_Watcher / IVP_Synapse_OO -- Common.dll 06369a00-06369e68. See
// ivp_oo_watcher.h for the class layout and the retail vtables.

#include "ivp_oo_watcher.h"

#include "ivp_mindist_create.h"                 // IVP_Create_Exact_Mindists (FUN_0634e0b0)
#include "../ivp_physics/ivp_real_object.h"     // IVP_Real_Object, IVP_Hull_Manager
#include "../ivp_physics/ivp_environment.h"     // IVP_Environment, IVP_Range_Manager
#include "../ivp_physics/ivp_core.h"            // IVP_Time

#include <cstddef>
#include <cstdlib>

static_assert(offsetof(IVP_Synapse_OO, HullPoolSlot) == 0x04, "IVP_Synapse_OO+0x04");
static_assert(offsetof(IVP_Synapse_OO, object) == 0x08, "IVP_Synapse_OO+0x08");
static_assert(offsetof(IVP_Synapse_OO, watcher) == 0x0c, "IVP_Synapse_OO+0x0c");
static_assert(offsetof(IVP_OO_Watcher, synapses) == 0x18, "IVP_OO_Watcher+0x18");
static_assert(offsetof(IVP_OO_Watcher, mindists) == 0x38, "IVP_OO_Watcher+0x38");

namespace
{
    // DAT_063a58cc, bits 0x60ad78ec: the library's P_FLOAT_MAX (1e20f). Added
    // to the first key so a freshly initialised synapse never fires before
    // the constructor's own hull_limit_exceeded_event re-keys it.
    constexpr float GSynapseOOInitialKeyBias = 1.0e20f;

    // The hull key every insert below builds; the only rounding is the final
    // store.
    float HullKeyAtTime(const IVP_Hull_Manager& Hull, float TimeHi, float TimeLo, float Bias)
    {
        const double Elapsed = (((double)(TimeHi) - Hull.LastEditTimeHi) + TimeLo) - Hull.LastEditTimeLo;
        return (float)(Elapsed * Hull.OuterGradient + Hull.OuterValueAtLastEdit + Bias);
    }
}

// ============================================================================
// IVP_Synapse_OO
// ============================================================================

// FUN_06369b70, RET 8 (watcher, object).
void IVP_Synapse_OO::init_synapse_oo(IVP_OO_Watcher* Watcher, IVP_Real_Object* Object)
{
    object = Object;
    const IVP_Environment* Environment = Object->environment;
    const float TimeHi = Environment->current_time.time_base;              // env+0xf8
    const float TimeLo = Environment->current_time.time_offset;            // env+0xfc
    // FUN_06362f90 on the hull pool (object+0x64) with this as payload.
    const float Key = HullKeyAtTime(Object->hull_manager, TimeHi, TimeLo, GSynapseOOInitialKeyBias);
    HullPoolSlot = Object->hull_manager.PoolInsert(this, Key);
    watcher = Watcher;
}

// FUN_06369b30: FUN_06363280 on the hull pool with the stored slot.
IVP_Synapse_OO::~IVP_Synapse_OO()
{
    object->hull_manager.PoolRemove(static_cast<uint16_t>(HullPoolSlot));
}

// LAB_06369a00, RET 8.
void IVP_Synapse_OO::hull_limit_exceeded_event(IVP_Hull_Manager* /*HullManager*/, float /*HullIntrusionValue*/)
{
    watcher->hull_limit_exceeded_event();
}

// LAB_06369b20, RET 4 -> FUN_06369bd0: null-tested deleting dtor.
void IVP_Synapse_OO::hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* /*HullManager*/)
{
    if (watcher != nullptr)
        delete watcher;
}

// ============================================================================
// IVP_OO_Watcher
// ============================================================================

// FUN_06369be0, RET 0xc. IVP_Collision's inlined ctor (+0x04 not written),
// the synapse array (vptrs only), the vector (memsize 8, elems =
// FUN_06343f70(0x20) = malloc), init_synapse_oo on both, and finally
// hull_limit_exceeded_event.
IVP_OO_Watcher::IVP_OO_Watcher(IVP_Collision_Delegator* Delegator, IVP_Real_Object* Object0, IVP_Real_Object* Object1)
    : IVP_Collision(Delegator)
    , IVP_Collision_Delegator()
    , mindists(static_cast<IVP_Collision**>(std::malloc(0x20)), 8)
{
    synapses[0].init_synapse_oo(this, Object0);
    synapses[1].init_synapse_oo(this, Object1);
    hull_limit_exceeded_event();
}

// FUN_06369ce0. Deletes every mindist from the top down (null entries
// skipped; each deletion calls back into collision_is_going_to_be_deleted_event
// and shrinks the vector, so elems is re-read), then reports this record to
// its own delegator. The rest of retail's body (vector clear, eh vector dtor
// over the synapses with FUN_06369b30, vptrs) is the compiler's member/base
// teardown.
IVP_OO_Watcher::~IVP_OO_Watcher()
{
    for (int32_t I = static_cast<int32_t>(mindists.n_elems) - 1; I >= 0; --I)
    {
        IVP_Collision* Mindist = mindists.elems[I];
        if (Mindist != nullptr)
            delete Mindist;
    }
    delegator->collision_is_going_to_be_deleted_event(this);
}

// FUN_06369a10.
void IVP_OO_Watcher::hull_limit_exceeded_event()
{
    IVP_Real_Object* Object0 = synapses[0].object;
    IVP_Environment* Environment = Object0->environment;
    IVP_Real_Object* Object1 = synapses[1].object;

    ++Environment->range_intra_exceeded;

    float Range0;
    float Range1;
    Environment->range_manager->get_coll_range_intra_objects(Object0, Object1, &Range0, &Range1);

    // FUN_0634e0b0 with this+0x14 as delegator.
    const float ScanRadius = (float)((double)(Range0) + Range1);
    IVP_Create_Exact_Mindists(Object0, Object1, ScanRadius, &mindists,
                              nullptr, nullptr, nullptr, nullptr,
                              static_cast<IVP_Collision_Delegator*>(this));

    // The time pair is read once, from obj0's environment, for both sides.
    const float TimeHi = Environment->current_time.time_base;   // env+0xf8
    const float TimeLo = Environment->current_time.time_offset; // env+0xfc

    Object0->hull_manager.PoolRemove(static_cast<uint16_t>(synapses[0].HullPoolSlot));
    synapses[0].HullPoolSlot = Object0->hull_manager.PoolInsert(
        &synapses[0], HullKeyAtTime(Object0->hull_manager, TimeHi, TimeLo, Range0));

    Object1->hull_manager.PoolRemove(static_cast<uint16_t>(synapses[1].HullPoolSlot));
    synapses[1].HullPoolSlot = Object1->hull_manager.PoolInsert(
        &synapses[1], HullKeyAtTime(Object1->hull_manager, TimeHi, TimeLo, Range1));
}

// LAB_06369e20, RET 4.
void IVP_OO_Watcher::get_objects(IVP_Real_Object* Out[2])
{
    Out[0] = synapses[0].object; // +0x20
    Out[1] = synapses[1].object; // +0x30
}

// LAB_06369e40, RET 4.
void IVP_OO_Watcher::get_ledges(const IVP_Compact_Ledge* Out[2])
{
    Out[0] = nullptr;
    Out[1] = nullptr;
}

// LAB_06369db0, RET 4, `this` = the +0x14 sub-object. Swap-with-last removal
// through the collision's two back-indices, as in
// IVP_OV_Element::remove_oo_collision (FUN_0635c250).
void IVP_OO_Watcher::collision_is_going_to_be_deleted_event(IVP_Collision* Collision)
{
    // IndexInElement0 is trusted only when in range (signed) and pointing back.
    int32_t Index = Collision->IndexInElement0;
    if (Index < 0 || Index >= static_cast<int32_t>(mindists.n_elems) || mindists.elems[Index] != Collision)
        Index = Collision->IndexInElement1;

    --mindists.n_elems;
    const int32_t Last = static_cast<int32_t>(mindists.n_elems);
    if (Last > Index)
    {
        IVP_Collision* Moved = mindists.elems[Last];
        mindists.elems[Index] = Moved;
        if (Moved->IndexInElement0 == Last)
            Moved->IndexInElement0 = Index;
        else
            Moved->IndexInElement1 = Index;
    }
    if (Collision->IndexInElement0 == Index)
        Collision->IndexInElement0 = -1;
    else
        Collision->IndexInElement1 = -1;
}
