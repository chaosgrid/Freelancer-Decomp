// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_real_object.h"
#include "ivp_core.h"
#include "ivp_environment.h"
#include "ivp_mindist.h" // IVP_Synapse_Real / IVP_Ledge_Mindist (synapse list teardown)
#include "ivp_phantom.h" // IVP_Controller_Phantom (first_child)
#include "ivp_ov_element.h" // IVP_Mindist_Manager / IVP_OV_Element (enable_collision_detection)
#include "ivp_calc_next_psi_solver.h" // set_transformation / calc_next_PSI_matrix
#include "ivp_simulation_unit.h"      // disable_simulation
#include "ivp_controller.h"           // IVP_Event_Sim
#include "../../include/common_physys.h" // PhySys::BeamBodyToWorldPose (06343cc0)
#include "ivp_cluster_manager.h"  // IVP_Cluster_Manager (add_object / remove_object / fire_event_*)
#include <new>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cassert>
#include "../ivp_utility/ivp_core_trap.h"   // IVP_CoreTrap (retail's `MOV dword ptr [0],0`)
#include "../ivp_utility/ivp_malloc.h"      // ivp_malloc_aligned / ivp_free_aligned (06343fc0 / 06343ff0)
#include "../ivp_utility/ivp_u_bigvector.h" // IVP_U_BigVector_Grow (0635d3e0)

// Forward declaration

void IVP_Core_Remove_Object(IVP_Core* Core, IVP_Real_Object* Object); // ivp_core.cpp

namespace
{
    // Anchor teardown notification; the unused outer argument is retained.
    void IVP_Anchor_ObjectDeletedEvent(IVP_Anchor* Anchor, IVP_Real_Object* /*Outer, unused*/)
    {
        Anchor->l_actuator->AnchorWillBeDeleted(Anchor);
    }

    // Raw IEEE-754 bits as a float (memcpy, no aliasing UB).
    inline float BitsToFloat(uint32_t Bits)
    {
        float Result;
        std::memcpy(&Result, &Bits, sizeof(Result));
        return Result;
    }
}

// ============================================================================
// IVP_Hull_Manager and its priority pool (see ivp_real_object.h).
// ============================================================================

// ----------------------------------------------------------------------------
// FUN_06362ef0: the shared pool ctor, run on IVP_Hull_Manager+0x20 with
// capacity 8 and on the standalone operator new(0x14) pool with capacity 0x10
// (IVP_SimUnitManager_Construct).
// ----------------------------------------------------------------------------
void IVP_Priority_Pool::Init(uint16_t InitialCapacity)
{
    PoolCapacity = InitialCapacity;
    PoolEntries = static_cast<Entry*>(std::malloc(sizeof(Entry) * PoolCapacity));
    PoolCount = 0;    // done before the alloc in retail
    PoolFreeHead = 0; // set unconditionally, even for a zero-capacity pool, as in retail
    // Thread the free list through every slot (no null test on the
    // allocation), then terminate it at the last slot. Both callers pass a
    // non-zero capacity.
    for (uint32_t I = 0; I < PoolCapacity; ++I)
        PoolEntries[I].Next = static_cast<uint16_t>(I + 1);
    PoolEntries[PoolCapacity - 1].Next = 0xffff;
    PoolKeyThreshold = BitsToFloat(0x501502f9); // the +1e10f "empty" sentinel
    PoolLiveIndex = 0xffff;
    PoolSortedHead = 0xffff;
}

// FUN_06362f70
void IVP_Priority_Pool::Destroy()
{
    if (PoolEntries != nullptr)
    {
        std::free(PoolEntries);
        PoolEntries = nullptr;
    }
}

// FUN_06352150: the eight dwords +0x00..+0x1c are zeroed by the default member
// initializers, then the pool is built with capacity 8.
IVP_Hull_Manager::IVP_Hull_Manager()
{
    Pool().Init(static_cast<uint16_t>(8));
}

// FUN_063521b0
IVP_Hull_Manager::~IVP_Hull_Manager()
{
    Pool().Destroy();
}

// FUN_063521f0. No null test on the entries pointer, as in retail.
void IVP_Hull_Manager::WalkActiveEntries()
{
    while (PoolCount > 0)
    {
        IVP_Listener_Hull* Listener = static_cast<IVP_Listener_Hull*>(PoolEntries[PoolLiveIndex].Payload);
        Listener->hull_manager_is_going_to_be_deleted_event(this);
    }
}

// FUN_06352220: rebases the whole hull-value coordinate system to zero.
void IVP_Hull_Manager::DrainAndShift()
{
    const float NegDeltaA = -OuterValueAtLastEdit;
    const float NegDeltaB = -InnerValueAtLastEdit;

    uint16_t Index = PoolLiveIndex;
    while (Index != 0xffff)
    {
        // Retail's null test on &Entries[Index] follows the read of its +4, so
        // it can never stop the loop; not reproduced.
        Entry& E = PoolEntries[Index];
        void* Payload = E.Payload;
        const uint16_t NextIndex = E.Next; // read before E.Key changes, as in retail

        E.Key += NegDeltaA;

        static_cast<IVP_Listener_Hull*>(Payload)->hull_manager_is_reset(NegDeltaA, NegDeltaB);

        Index = NextIndex;
    }

    PoolKeyThreshold += NegDeltaA;
    OuterValueAtLastEdit = 0.0f;
    InnerValueAtLastEdit = 0.0f;
    AccumulatedDeltaA += NegDeltaA;
}

// ----------------------------------------------------------------------------
// FUN_06362f90. Every live entry sits on one ascending-by-Key doubly linked
// list (Next/Prev, head PoolLiveIndex, so the minimum is O(1)); some entries
// are also promoted onto an "express" list (ExpressNext/ExpressPrev, head
// PoolSortedHead) that the insert walk uses as a skip list. PoolKeyThreshold
// caches the head's key.
// ----------------------------------------------------------------------------
uint16_t IVP_Priority_Pool::Insert(void* Payload, float Key)
{
    ++PoolCount; // bumped before retail knows whether it has to grow

    uint32_t NewIndex = 0;
    Entry* NewEntry = nullptr;

    if (PoolFreeHead != 0xffff)
    {
        // Pop the head of the free chain.
        NewIndex = PoolFreeHead;
        NewEntry = &PoolEntries[NewIndex];
        PoolFreeHead = NewEntry->Next;
    }
    else
    {
        // Grow. Retail allocates (Capacity+1)*2 entries but publishes a
        // capacity of Capacity*2+1, leaving one spare entry past the end.
        const uint16_t OldCapacity = PoolCapacity;
        Entry* NewEntries = static_cast<Entry*>(std::malloc((static_cast<size_t>(OldCapacity) + 1u) * 32u));

        for (uint16_t I = 0; I < OldCapacity; ++I)
            NewEntries[I] = PoolEntries[I];

        PoolCapacity = static_cast<uint16_t>(OldCapacity * 2 + 1);
        if (PoolEntries != nullptr)
        {
            std::free(PoolEntries);
            PoolEntries = nullptr;
        }

        NewIndex = OldCapacity;               // the first brand-new slot is handed out
        NewEntry = &NewEntries[OldCapacity];
        PoolEntries = NewEntries;
        PoolFreeHead = static_cast<uint16_t>(OldCapacity + 1);

        for (uint32_t I = static_cast<uint32_t>(OldCapacity) + 1u; I < static_cast<uint32_t>(PoolCapacity); ++I)
            NewEntries[I].Next = static_cast<uint16_t>(I + 1);

        // The last declared slot terminates the free chain; unconditional in
        // retail, even when OldCapacity == 0 and it is the entry just handed out.
        NewEntries[PoolCapacity - 1].Next = 0xffff;
    }

    // 0xfffe tags "not on the express list".
    NewEntry->Key = Key;
    NewEntry->Payload = Payload;
    NewEntry->ExpressNext = 0xfffe;

    // Key <= threshold (or NaN) makes the entry the new head.
    if (!(Key > PoolKeyThreshold))
    {
        PoolKeyThreshold = Key;
        NewEntry->Next = PoolLiveIndex;
        if (PoolLiveIndex != 0xffff)
            PoolEntries[PoolLiveIndex].Prev = static_cast<uint16_t>(NewIndex);
        PoolLiveIndex = static_cast<uint16_t>(NewIndex);
        NewEntry->Prev = 0xffff;
        return static_cast<uint16_t>(NewIndex);
    }

    // ---- sorted-insert path ------------------------------------------------
    // Hop along the express list while its keys are below the new key (or
    // unordered), remembering the last express node (Anchor). StepBudget
    // starts at 3 and grows per hop; it is how many ascending-list hops count
    // as cheap enough not to promote a new express node.
    uint32_t ExpressCur = PoolSortedHead;
    uint32_t Anchor = PoolLiveIndex;
    int32_t StepBudget = 3;

    while (ExpressCur != 0xffff)
    {
        if (PoolEntries[ExpressCur].Key >= Key)
            break;
        ++StepBudget;
        Anchor = ExpressCur;
        ExpressCur = PoolEntries[ExpressCur].ExpressNext;
    }

    // Then walk the ascending list from Anchor.
    uint32_t Prev = Anchor;
    uint32_t Cur = PoolEntries[Anchor].Next;
    int32_t Hops = 0;
    bool FoundPosition = false;

    while (Cur != 0xffff)
    {
        ++Hops; // incremented before the comparison, as in retail
        // Key <= Entries[cur].Key (or unordered) is the insertion point.
        if (!(Key > PoolEntries[Cur].Key))
        {
            FoundPosition = true;
            break;
        }
        Prev = Cur;
        Cur = PoolEntries[Cur].Next;
    }

    if (FoundPosition)
    {
        // Splice between Prev and Cur.
        NewEntry->Next = PoolEntries[Prev].Next;                      // == Cur
        PoolEntries[Cur].Prev = static_cast<uint16_t>(NewIndex);
        NewEntry->Prev = static_cast<uint16_t>(Prev);
        PoolEntries[Prev].Next = static_cast<uint16_t>(NewIndex);
    }
    else
    {
        // Ran off the tail.
        NewEntry->Next = 0xffff;
        NewEntry->Prev = static_cast<uint16_t>(Prev);
        PoolEntries[Prev].Next = static_cast<uint16_t>(NewIndex);
    }

    // A walk longer than the budget promotes one node onto the express list.
    if (Hops <= StepBudget)
        return static_cast<uint16_t>(NewIndex);

    uint32_t Promote = Anchor;
    if (StepBudget > 2)
    {
        int32_t Remaining = StepBudget - 2;
        do
        {
            Promote = PoolEntries[Promote].Next;
            --Remaining;
        } while (Remaining != 0);
    }

    const uint16_t AnchorExpressNext = PoolEntries[Anchor].ExpressNext;
    Entry& PromotedEntry = PoolEntries[Promote];

    if (AnchorExpressNext == 0xfffe) // the anchor is not itself an express node
    {
        // Push the promoted node onto the front of the express list.
        PromotedEntry.ExpressNext = PoolSortedHead;
        PromotedEntry.ExpressPrev = 0xffff;
        if (PoolSortedHead != 0xffff)
            PoolEntries[PoolSortedHead].ExpressPrev = static_cast<uint16_t>(Promote);
        PoolSortedHead = static_cast<uint16_t>(Promote);
    }
    else
    {
        // Splice it in right after the anchor.
        PromotedEntry.ExpressNext = AnchorExpressNext;
        PromotedEntry.ExpressPrev = static_cast<uint16_t>(Anchor);
        if (AnchorExpressNext != 0xffff)
            PoolEntries[AnchorExpressNext].ExpressPrev = static_cast<uint16_t>(Promote);
        PoolEntries[Anchor].ExpressNext = static_cast<uint16_t>(Promote);
    }

    return static_cast<uint16_t>(NewIndex);
}

// ----------------------------------------------------------------------------
// FUN_06363280
// ----------------------------------------------------------------------------
void IVP_Priority_Pool::Remove(uint16_t Slot)
{
    Entry& E = PoolEntries[Slot];

    const uint16_t PrevIndex = E.Prev;
    const uint16_t NextIndex = E.Next;

    if (PrevIndex != 0xffff)
    {
        PoolEntries[PrevIndex].Next = NextIndex;
        if (NextIndex != 0xffff)
            PoolEntries[NextIndex].Prev = PrevIndex;
    }
    else
    {
        // The head was removed: refresh the cached minimum from the new head.
        PoolLiveIndex = NextIndex;
        if (NextIndex != 0xffff)
        {
            PoolEntries[NextIndex].Prev = 0xffff;
            PoolKeyThreshold = PoolEntries[NextIndex].Key;
        }
        else
        {
            // Empty pool: back to the ctor's +1e10f sentinel.
            PoolKeyThreshold = BitsToFloat(0x501502f9);
        }
    }

    // Unlink from the express list unless never promoted (0xfffe).
    const uint16_t ExpressNext = E.ExpressNext;
    if (ExpressNext != 0xfffe)
    {
        const uint16_t ExpressPrev = E.ExpressPrev;
        if (ExpressNext != 0xffff)
            PoolEntries[ExpressNext].ExpressPrev = ExpressPrev;
        if (ExpressPrev == 0xffff)
            PoolSortedHead = ExpressNext;
        else
            PoolEntries[ExpressPrev].ExpressNext = ExpressNext;
    }

    // Back onto the free chain.
    E.Next = PoolFreeHead;
    --PoolCount;
    PoolFreeHead = Slot;
}

// ============================================================================
// IVP_Object / IVP_Cluster (retail 06345610-06345846)
// ============================================================================
namespace
{
    // FUN_06356920: null for a null string, else malloc (FUN_06343f70) of
    // strlen + 1 and a copy.
    char* IvpStrdup(const char* Source)
    {
        if (Source == nullptr)
        {
            return nullptr;
        }
        const size_t Length = std::strlen(Source) + 1;
        char* const Copy = static_cast<char*>(std::malloc(Length));
        std::memcpy(Copy, Source, Length);
        return Copy;
    }
}

// FUN_06345610. Father and Template are dereferenced unconditionally.
IVP_Object::IVP_Object(IVP_Cluster* Father, const IVP_RealObjectConstructionTemplate* Template)
{
    init_object_core(Father->environment);
    Father->add_object(this);
    object_name = IvpStrdup(Template->LongNamePtr);
}

// FUN_06345670
IVP_Object::IVP_Object(IVP_Environment* Environment)
{
    init_object_core(Environment);
}

// FUN_06345690. Stores +0x10, +0x0c, +0x08, +0x18, +0x14, +0x04 in that order.
void IVP_Object::init_object_core(IVP_Environment* Environment)
{
    parent = nullptr;
    prev_object = nullptr;
    next_object = nullptr;
    environment = Environment;
    object_name = nullptr;
    object_type = 0;
}

// FUN_063456b0
IVP_Object::~IVP_Object()
{
    if (parent != nullptr)
    {
        parent->remove_object(this);
    }
    if (object_name != nullptr)
    {
        std::free(object_name); // FUN_06343fb0
        object_name = nullptr;
    }
    environment = nullptr;
}

// FUN_06345760. vptr 063a5454.
IVP_Cluster::IVP_Cluster(IVP_Environment* Environment)
    : IVP_Object(Environment)
{
    first_object = nullptr;
    object_type = 1;
}

// FUN_06345790: deletes first_object (slot 0) until it reads null.
IVP_Cluster::~IVP_Cluster()
{
    while (first_object != nullptr)
    {
        delete first_object;
    }
}

// FUN_063457f0
void IVP_Cluster::add_object(IVP_Object* Object)
{
    Object->next_object = first_object;
    Object->prev_object = nullptr;
    if (first_object != nullptr)
    {
        first_object->prev_object = Object;
    }
    first_object = Object;
    Object->parent = this;
}

// FUN_06345820
void IVP_Cluster::remove_object(IVP_Object* Object)
{
    if (Object->prev_object == nullptr)
    {
        first_object = Object->next_object;
    }
    else
    {
        Object->prev_object->next_object = Object->next_object;
    }
    if (Object->next_object != nullptr)
    {
        Object->next_object->prev_object = Object->prev_object;
    }
}

// Inlined into 06345100: IVP_Object(Father, Template), vptr 063a544c, then the
// hull manager member (06352150).
IVP_Real_Object_Fast_Static::IVP_Real_Object_Fast_Static(IVP_Cluster* Father, const IVP_RealObjectConstructionTemplate* Template)
    : IVP_Object(Father, Template)
{
}

// ============================================================================
// FUN_06345100: IVP_Real_Object::IVP_Real_Object. Parms supplies the long name
// (+0x00), short name (+0x04), core flags (+0x0c / +0x10), material (+0x18)
// and client data (+0x54); a null orientation becomes a stack identity
// quaternion; the position goes to the IVP_Core ctor (06347660).
// ============================================================================
IVP_Real_Object::IVP_Real_Object(IVP_Cluster* Father, IVP_SurfaceManager* SurfaceManager,
                                  const IVP_RealObjectConstructionTemplate* Parms,
                                  const IVP_U_Quat* InitialOrientationOrNull,
                                  const IVP_U_Point* InitialPosition)
    : IVP_Real_Object_Fast_Static(Father, Parms) // vptrs 063a5450, 063a544c; hull manager 06352150
{
    // vptr 063a5440 (the compiler's).

    // Only the IVP_Core ctor below reads the orientation.
    IVP_U_Quat IdentityOrientation;
    const IVP_U_Quat* Orientation = InitialOrientationOrNull;
    if (Orientation == nullptr)
    {
        IdentityOrientation.x = 0.0f;
        IdentityOrientation.y = 0.0f;
        IdentityOrientation.z = 0.0f;
        IdentityOrientation.w = 1.0f;
        Orientation = &IdentityOrientation;
    }

    // In retail's store order.
    synapse_list_head = nullptr;         // +0x20
    invalid_synapse_list_head = nullptr; // +0x24
    mindist_list = nullptr;              // +0x28
    surface_manager = SurfaceManager;    // +0x80
    anchors = nullptr;                   // +0x7c
    ov_element = nullptr;                // +0x90
    flags = 0;                           // +0x78
    q_core_f_object = nullptr;           // +0x2c
    shift_core_f_object.x = 0.0f;        // +0x30
    shift_core_f_object.y = 0.0f;        // +0x34
    shift_core_f_object.z = 0.0f;        // +0x38
    cache_object = nullptr;              // +0x40
    first_child = nullptr;               // +0x1c

    std::strncpy(name, Parms->ShortNameTag, sizeof(name));

    // operator new(0x1bc) + 06347660; the result goes to +0x98, +0x9c, +0xa0.
    IVP_Core* const NewCore = new IVP_Core(this, Orientation, InitialPosition, Parms->FlagsHi, Parms->FlagsLo);
    core = NewCore;          // +0x98
    original_core = NewCore; // +0x9c
    field_a0 = NewCore;      // +0xa0

    // Applied to whatever +0x78 holds after the core ctor.
    if (Parms->FlagsHi != 0)
    {
        flags = (flags & 0xffffff10u) | 0x10u;
    }
    else
    {
        flags = (flags & 0xffffff08u) | 0x8u;
    }

    material = Parms->Material; // +0x8c

    // 0634b3d0, no null tests.
    NewCore->Environment->cluster_manager->add_object(this);

    client_data = Parms->ClientData; // +0xa4
}

// ============================================================================
// Narrow-phase per-object cache slots (IVP_Cache_Object_Manager, environment
// +0x94; IVP_NarrowPhaseCacheSlot is in ivp_real_object.h). FUN_0634d530
// claims a slot, FUN_06353510 resolves it lazily (refcount + conditional
// refresh), FUN_0634d640 refreshes it. The consumers are the
// IVP_Mindist_Minimize_Solver handlers (ivp_collision/ivp_mindist_minimize.cpp).
// ============================================================================

// FUN_0634d640: the slot's transform extrapolated to the environment's
// current time. DTime = ((now.base - psi.base) + now.offset) - psi.offset on
// the x87 stack. DTime equal to 0 or unordered copies the core's quaternion and
// position; otherwise the quaternion is interpolated with Alpha =
// float(DTime * IDeltaTime) and the position extrapolated with the unrounded
// DTime. Both WorldTransform.vv (+0x4c) and Secondary (+0x5c) get the position.
// Tails: (a) unless Owner->flags & 0xc00, vv = M * shift_core_f_object + vv
// (Secondary keeps the core position); (b) with q_core_f_object, Quat =
// Quat * q_core_f_object inlined in retail's summation order, then the rows
// are rebuilt.
void IVP_RefreshNarrowPhaseCacheSlot(IVP_NarrowPhaseCacheSlot* Slot)
{
    IVP_Real_Object* Owner = Slot->Owner;
    IVP_Environment* Env = Owner->environment;
    IVP_Core* Core = Owner->core;

    const double DTime = (((double)(Env->current_time.time_base) - Core->TimeOfLastPsi.time_base)
                          + Env->current_time.time_offset) - Core->TimeOfLastPsi.time_offset;

    Slot->LastRefreshTick = Env->sim_step_count;

    if (DTime == 0.0 || std::isnan(DTime)) // DAT_063a53d4; equal or unordered
    {
        Slot->Quat = Core->QWorldFCore;

        Slot->WorldTransform.vv.x = Core->PosWorldFCore.x;
        Slot->WorldTransform.vv.y = Core->PosWorldFCore.y;
        Slot->WorldTransform.vv.z = Core->PosWorldFCore.z;
    }
    else
    {
        const float Alpha = (float)(DTime * Core->IDeltaTime);
        Slot->Quat.set_interpolate_quat(&Core->QWorldFCore, &Core->QWorldFCoreNext, Alpha);

        Slot->WorldTransform.vv.x = (float)(DTime * Core->DeltaWorldFCore.x + Core->PosWorldFCore.x);
        Slot->WorldTransform.vv.y = (float)(DTime * Core->DeltaWorldFCore.y + Core->PosWorldFCore.y);
        Slot->WorldTransform.vv.z = (float)(DTime * Core->DeltaWorldFCore.z + Core->PosWorldFCore.z);
    }

    Slot->Secondary.x = Slot->WorldTransform.vv.x;
    Slot->Secondary.y = Slot->WorldTransform.vv.y;
    Slot->Secondary.z = Slot->WorldTransform.vv.z;

    Slot->Quat.set_matrix(&Slot->WorldTransform);

    if ((Owner->flags & 0xc00u) == 0) // (a)
        Slot->WorldTransform.vmult4(&Owner->shift_core_f_object, &Slot->WorldTransform.vv);

    if (Owner->q_core_f_object != nullptr) // (b)
    {
        // Every term is read before the first store; y and x stay on the
        // FPU, z and w are spilled as floats; stores go z, w, x, y.
        IVP_U_Quat& Q = Slot->Quat;
        const IVP_U_Quat* R = Owner->q_core_f_object;
        const double Y = (((double)Q.y * R->w + (double)R->y * Q.w) + (double)Q.z * R->x) - (double)Q.x * R->z;
        const float Z = (float)((((double)R->y * Q.x + (double)Q.z * R->w) + (double)Q.w * R->z) - (double)Q.y * R->x);
        const float W = (float)((((double)Q.w * R->w - (double)Q.x * R->x) - (double)Q.y * R->y) - (double)Q.z * R->z);
        const double X = (((double)Q.x * R->w + (double)Q.y * R->z) + (double)Q.w * R->x) - (double)R->y * Q.z;
        Q.z = Z;
        Q.w = W;
        Q.x = (float)X;
        Q.y = (float)Y;
        Q.set_matrix(&Slot->WorldTransform); // FUN_06350bc0
    }
}

// FUN_0634d530. Advances the round-robin cursor past busy slots (RefCount
// != 0), evicts the slot's previous owner (FUN_0634d510), claims it, and
// refreshes it when the owner's signed low flag byte is >= 8.
IVP_NarrowPhaseCacheSlot* IVP_Cache_Object_Manager::get_cache_object(IVP_Real_Object* Owner)
{
    int32_t Index = next_free_index;
    IVP_NarrowPhaseCacheSlot* Slot = &cache_objects[Index];

    if (Slot->RefCount != 0)
    {
        const int32_t Mask = number_of_cache_objects - 1;
        do
        {
            Index = (Index + 1) & Mask;
            next_free_index = Index; // retail stores the cursor every iteration
            Slot = &cache_objects[Index];
        } while (Slot->RefCount != 0);
    }

    // Advance the cursor one past the slot just claimed.
    next_free_index = (number_of_cache_objects - 1) & (Index + 1);

    if (Slot->Owner != nullptr)
        IVP_Real_Object_Free_Cache_Object(Slot->Owner); // FUN_0634d510

    Slot->Owner = Owner;
    Owner->cache_object = Slot;
    Slot->LastRefreshTick = 0;

    if (int8_t(Owner->flags & 0xff) >= 8)
        IVP_RefreshNarrowPhaseCacheSlot(Slot);

    return Slot;
}

// FUN_06353510
IVP_NarrowPhaseCacheSlot* IVP_Real_Object::ResolveNarrowPhaseCacheSlot()
{
    if (cache_object == nullptr)
        cache_object = environment->cache_object_manager->get_cache_object(this); // FUN_0634d530

    IVP_NarrowPhaseCacheSlot* Slot = cache_object;
    ++Slot->RefCount;

    if (int8_t(flags & 0xff) < 8 && environment->sim_step_count > Slot->LastRefreshTick)
        IVP_RefreshNarrowPhaseCacheSlot(Slot);

    return Slot;
}

// get_cache_object_no_lock, inlined in FUN_0634e0b0: as
// ResolveNarrowPhaseCacheSlot without the refcount.
IVP_NarrowPhaseCacheSlot* IVP_Real_Object::get_cache_object_no_lock()
{
    if (cache_object == nullptr)
        cache_object = environment->cache_object_manager->get_cache_object(this); // FUN_0634d530

    IVP_NarrowPhaseCacheSlot* Slot = cache_object;
    if (int8_t(flags & 0xff) < 8 && environment->sim_step_count > Slot->LastRefreshTick)
        IVP_RefreshNarrowPhaseCacheSlot(Slot);

    return Slot;
}

// The matching decrement for ResolveNarrowPhaseCacheSlot. Not a retail
// function: inline in the tails of the narrow-phase handlers (e.g.
// FUN_063536d0 at 06353826), which ivp_mindist_minimize.cpp writes inline too.
void IVP_Real_Object::ReleaseNarrowPhaseCacheSlot(IVP_NarrowPhaseCacheSlot* Slot)
{
    --Slot->RefCount;
}

// The four slot transforms below are retail's own out-of-line copies of the
// IVP_U_Matrix operations on the slot's WorldTransform (+0x1c), the same x87
// sequence with every operand offset shifted by +0x1c:
//   0634d860 == 06348470 vimult4, 0634d8d0 == 063484e0 vmult4,
//   0634d970 == 06366010 vimult,  0634d9d0 == 06348580 vmult.

// FUN_0634d860: out = M^T * (in - vv).
void IVP_TransformWorldToSlotSpace(IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* WorldPos, IVP_U_Point* OutLocalPos)
{
    Slot->WorldTransform.vimult4(WorldPos, OutLocalPos);
}

// FUN_0634d9d0: out = M * in. Alias-safe; Leave_PK 06365280 calls it in place.
void IVP_RotateSlotToWorldSpace(IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* LocalDir, IVP_U_Point* OutWorldDir)
{
    Slot->WorldTransform.vmult(LocalDir, OutWorldDir);
}

// FUN_0634d970: out = M^T * in. Alias-safe (called in place).
void IVP_RotateWorldToSlotSpace(IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* WorldDir, IVP_U_Point* OutLocalDir)
{
    Slot->WorldTransform.vimult(WorldDir, OutLocalDir);
}

// FUN_0634d8d0: out = M * in + vv.
void IVP_TransformSlotToWorldSpace(IVP_NarrowPhaseCacheSlot* Slot, const IVP_U_Point* LocalPos, IVP_U_Point* OutWorldPos)
{
    Slot->WorldTransform.vmult4(LocalPos, OutWorldPos);
}

// FUN_063454e0: sets the core's movement state to 0x21 (low bits) around a
// RecheckOvElement, then puts the old low byte back under the current upper
// bits. Caller: 06347170 per object.
void IVP_Real_Object::recheck_ov_element_as_moving()
{
    IVP_Core* const CoreBefore = core;
    const uint32_t OldState = CoreBefore->MovementState;
    CoreBefore->MovementState = (OldState & 0xFFFFFF21u) | 0x21u;
    core->Environment->mindist_manager->RecheckOvElement(this);
    IVP_Core* const CoreAfter = core;
    const uint32_t CurrentState = CoreAfter->MovementState;
    CoreAfter->MovementState = ((CurrentState ^ OldState) & 0xFFu) ^ CurrentState;
}

// ============================================================================
// Contact points, collision detection and phantoms.
// ============================================================================

// FUN_06345550. mindist_list (+0x28) heads this object's contact-point
// records, threaded through the record's per-object node (+0x08 for Object1,
// +0x1c for Object2); the node's int16 at +0x0c is record - node. The head is
// re-read each pass because RemoveDistFromSystem unlinks the record.
void IVP_Real_Object::unlink_contact_points(bool bSilently)
{
    for (;;)
    {
        char* const Node = static_cast<char*>(mindist_list);
        if (Node == nullptr)
        {
            break;
        }
        IVP_Contact_Point* const Record =
            reinterpret_cast<IVP_Contact_Point*>(Node + *reinterpret_cast<int16_t*>(Node + 0xc));
        IVP_ListManager* const FrictionSystem = Record->ListManagerOwner; // +0x70
        IVP_Core* const Core2 = Record->Object2->original_core;

        // Not silent: wake both cores, Object1's first (FUN_06347d80).
        if (!bSilently)
        {
            Record->Object1->original_core->wake_or_ensure_simulation();
            Core2->wake_or_ensure_simulation();
        }

        FrictionSystem->RemoveDistFromSystem(Record); // FUN_06355490

        // An emptied friction system is deleted (slot 6).
        if (FrictionSystem->CtxListCount == 0)
        {
            delete FrictionSystem;
        }
    }
}

// FUN_06344d50
void IVP_Real_Object::enable_collision_detection(bool bEnable)
{
    if (bEnable)
    {
        if ((flags & 0x300u) != 0)
        {
            return;
        }
        environment->mindist_manager->RebuildOvElement(this); // FUN_0634f180
        flags = (flags & 0xfffffdffu) | 0x100u;                // re-read after the call
        return;
    }

    // Off: the OV element goes (slot 4), bits 8-9 are cleared, the pointer is
    // nulled and the contact points are unlinked silently.
    if ((flags & 0x300u) == 0)
    {
        return;
    }
    if (ov_element != nullptr)
    {
        delete ov_element;
    }
    flags &= 0xfffffcffu;
    ov_element = nullptr;
    unlink_contact_points(true);
}

// FUN_06345f10. The old phantom goes first and first_child is nulled before
// operator new(0x44); a failed allocation stores null.
IVP_Controller_Phantom* IVP_Real_Object::convert_to_phantom(const IVP_Template_Phantom* Template)
{
    delete static_cast<IVP_Controller_Phantom*>(first_child);
    first_child = nullptr;
    IVP_Controller_Phantom* const Phantom = new IVP_Controller_Phantom(this, Template);
    first_child = Phantom;
    return Phantom;
}

// FUN_06345eb0
int32_t IVP_Real_Object::convert_phantom_to_object()
{
    // Flags bits 8-9 taken first, sign-extended: 0x100 -> 1, 0x200 -> -2, 0x300 -> -1.
    const int32_t CollisionBits = static_cast<int32_t>(flags << 22) >> 30;

    enable_collision_detection(false);

    delete static_cast<IVP_Controller_Phantom*>(first_child);
    first_child = nullptr;

    if (static_cast<uint8_t>(flags) == 0x9)
    {
        flags = (flags & 0xffffff10u) | 0x10u;
    }

    // Only the exact value 1 re-enables.
    if (CollisionBits == 1)
    {
        enable_collision_detection(true);
    }
    return 1;
}

// FUN_063454b0. The mindist manager is read once; each next link is read
// before the call.
void IVP_Real_Object::recalc_exact_mindists()
{
    IVP_Mindist_Manager* const MindistManager = environment->mindist_manager;
    IVP_Synapse_Real* Synapse = synapse_list_head;
    while (Synapse != nullptr)
    {
        IVP_Synapse_Real* const Next = Synapse->next;
        MindistManager->recalc_exact_mindist(Synapse->get_mindist()); // FUN_06350460
        Synapse = Next;
    }
}

// FUN_06345460. Next link read before the calls; the mindist manager is
// re-read for every mindist that moves.
void IVP_Real_Object::recalc_invalid_mindists()
{
    IVP_Synapse_Real* Synapse = invalid_synapse_list_head;
    while (Synapse != nullptr)
    {
        IVP_Synapse_Real* const Next = Synapse->next;
        IVP_Ledge_Mindist* const Mindist = Synapse->get_mindist();
        Mindist->recalc_mindist(); // FUN_06352b90, result unused
        if ((Mindist->StateFlags & 0xc000u) != 0x4000u)
        {
            IVP_Mindist_Manager* const MindistManager = environment->mindist_manager;
            MindistManager->remove_invalid_mindist(Mindist);         // FUN_0634ffa0
            MindistManager->insert_exact_mindist_no_recalc(Mindist); // FUN_0634e8a0
        }
        Synapse = Next;
    }
}

// FUN_063455c0. Sole caller: ~IVP_Real_Object.
void IVP_Real_Object::TeardownMindistListenersAndCandidates()
{
    // Drain the anchors. Each anchor's handler is expected to unlink itself;
    // a head that did not change hits the CORE trap (063455da), after which
    // the loop goes on.
    {
        IVP_Anchor* PreviousHead = nullptr;
        for (;;)
        {
            IVP_Anchor* Head = anchors;
            if (Head == nullptr)
                break;

            IVP_Anchor_ObjectDeletedEvent(Head, this);

            if (Head == PreviousHead)
            {
                IVP_CoreTrap(); // 063455da
            }
            PreviousHead = Head;
        }
    }

    unlink_contact_points(true); // FUN_06345550

    // Delete the head synapse's mindist (slot 4) until the synapse list is
    // empty; its destructor (0634dd60) unlinks both synapses. A zero mindist
    // address loops back.
    for (;;)
    {
        IVP_Synapse_Real* const Head = synapse_list_head;
        if (Head == nullptr)
            break;

        IVP_Ledge_Mindist* const Mindist = Head->get_mindist();
        if (Mindist == nullptr)
            continue;

        delete Mindist;
    }
}

// FUN_06345300; the deleting dtor (slot 0 of 063a5440) is FUN_063452e0. The
// hull manager dtor (063521b0) and ~IVP_Object (063456b0) follow, as in retail.
IVP_Real_Object::~IVP_Real_Object()
{
    // The phantom (+0x1c): 063442c0 + operator delete.
    IVP_Controller_Phantom* const Phantom = static_cast<IVP_Controller_Phantom*>(first_child);
    if (Phantom != nullptr)
    {
        delete Phantom;
    }
    first_child = nullptr;

    hull_manager.WalkActiveEntries(); // 063521f0

    TeardownMindistListenersAndCandidates(); // 063455c0

    // 0634ced0 (object listeners, slot 0) with a stack {environment, this}.
    {
        IVP_Environment_Object_Notify_Event Event;
        Event.Env = environment;
        Event.Object = this;
        IVP_Environment_Notify_Object_Listeners_Slot0(environment, &Event);
    }

    if ((flags & 0x2000u) != 0)
    {
        environment->cluster_manager->fire_event_collision_object_deleted(this); // 0634b090
    }

    // A fresh stack {environment, this}.
    if ((flags & 0x1000u) != 0)
    {
        IVP_Environment_Object_Notify_Event Event;
        Event.Env = environment;
        Event.Object = this;
        environment->cluster_manager->fire_event_object_deleted(&Event); // 0634ae90
    }

    core->Environment->cluster_manager->remove_object(this); // 0634b3e0

    if (cache_object != nullptr)
    {
        IVP_Real_Object_Free_Cache_Object(this); // 0634d510
    }

    // 06346f20 on core, then on field_a0 unless it is core, then on
    // original_core unless it is either. No null tests.
    IVP_Core_Remove_Object(core, this);
    if (field_a0 != core)
    {
        IVP_Core_Remove_Object(field_a0, this);
    }
    if (original_core != field_a0 && original_core != core)
    {
        IVP_Core_Remove_Object(original_core, this);
    }

    ::operator delete(q_core_f_object); // 06391d5a
    q_core_f_object = nullptr;
}

// ============================================================================
// FUN_06345850: IVP_Real_Object::get_m_world_f_object_at_transform
// ============================================================================
void IVP_Real_Object::get_m_world_f_object_at_transform(IVP_Time Time, IVP_U_Matrix* OutMatrix)
{
    IVP_Core* CorePtr = core;

    // DTime stays on the FPU; only Alpha = DTime * IDeltaTime is rounded.
    const float Alpha = (float)(((((double)Time.time_base - CorePtr->TimeOfLastPsi.time_base) + Time.time_offset) -
                                 CorePtr->TimeOfLastPsi.time_offset) * CorePtr->IDeltaTime);
    IVP_U_Quat InterpQuat;
    InterpQuat.set_interpolate_quat(&CorePtr->QWorldFCore, &CorePtr->QWorldFCoreNext, Alpha);

    // DTime recomputed; each vv component rounded once; stores go z, x, y.
    const double DTime = (((double)Time.time_base - CorePtr->TimeOfLastPsi.time_base) + Time.time_offset) -
                         CorePtr->TimeOfLastPsi.time_offset;
    const double PosY = DTime * CorePtr->DeltaWorldFCore.y + CorePtr->PosWorldFCore.y;
    const float PosZ = (float)(DTime * CorePtr->DeltaWorldFCore.z + CorePtr->PosWorldFCore.z);
    const double PosX = DTime * CorePtr->DeltaWorldFCore.x + CorePtr->PosWorldFCore.x;
    OutMatrix->vv.z = PosZ;
    OutMatrix->vv.x = (float)PosX;
    OutMatrix->vv.y = (float)PosY;

    InterpQuat.set_matrix(OutMatrix);

    if ((flags & 0x0C00) == 0)
    {
        OutMatrix->vmult4(&shift_core_f_object, &OutMatrix->vv);
    }

    // InterpQuat * q_core_f_object inlined with its own summation order (not
    // set_mult_quat 063510f0's); y and x stay on the FPU, z and w are spilled;
    // stores go z, w, x, y; then the rows are rebuilt.
    if (q_core_f_object != nullptr)
    {
        const IVP_U_Quat* Q2 = q_core_f_object;
        const double Ry = (((double)InterpQuat.z * Q2->x + (double)InterpQuat.y * Q2->w) +
                           (double)InterpQuat.w * Q2->y) -
                          (double)InterpQuat.x * Q2->z;
        const float Rz = (float)((((double)InterpQuat.w * Q2->z + (double)InterpQuat.z * Q2->w) +
                                  (double)InterpQuat.x * Q2->y) -
                                 (double)InterpQuat.y * Q2->x);
        const float Rw = (float)((((double)InterpQuat.w * Q2->w - (double)InterpQuat.x * Q2->x) -
                                  (double)InterpQuat.y * Q2->y) -
                                 (double)InterpQuat.z * Q2->z);
        const double Rx = (((double)InterpQuat.y * Q2->z + (double)InterpQuat.w * Q2->x) +
                           (double)InterpQuat.x * Q2->w) -
                          (double)InterpQuat.z * Q2->y;
        InterpQuat.z = Rz;
        InterpQuat.w = Rw;
        InterpQuat.x = (float)Rx;
        InterpQuat.y = (float)Ry;
        InterpQuat.set_matrix(OutMatrix);
    }
}

// ============================================================================
// FUN_063456f0: get_m_world_f_object_at_transform at the environment's
// current time. environment is read without a test.
// ============================================================================
void IVP_Real_Object::get_m_world_f_object_AT(IVP_U_Matrix* OutMatrix)
{
    get_m_world_f_object_at_transform(environment->current_time, OutMatrix);
}

// ============================================================================
// FUN_063459c0: IVP_Real_Object::calc_at_time. The same interpolation as
// get_m_world_f_object_at_transform, with orientation and position returned
// separately.
// ============================================================================
void IVP_Real_Object::calc_at_time(IVP_Time Time, IVP_U_Quat* OutQuat, IVP_U_Point* OutPosition)
{
    IVP_Core* CorePtr = core;

    const float Alpha = (float)(((((double)Time.time_base - CorePtr->TimeOfLastPsi.time_base) + Time.time_offset) -
                                 CorePtr->TimeOfLastPsi.time_offset) * CorePtr->IDeltaTime);
    OutQuat->set_interpolate_quat(&CorePtr->QWorldFCore, &CorePtr->QWorldFCoreNext, Alpha);

    // Stores go z, x, y.
    const double DTime = (((double)Time.time_base - CorePtr->TimeOfLastPsi.time_base) + Time.time_offset) -
                         CorePtr->TimeOfLastPsi.time_offset;
    const double PosY = DTime * CorePtr->DeltaWorldFCore.y + CorePtr->PosWorldFCore.y;
    const float PosZ = (float)(DTime * CorePtr->DeltaWorldFCore.z + CorePtr->PosWorldFCore.z);
    const double PosX = DTime * CorePtr->DeltaWorldFCore.x + CorePtr->PosWorldFCore.x;
    OutPosition->z = PosZ;
    OutPosition->x = (float)PosX;
    OutPosition->y = (float)PosY;

    // A stack matrix (rows from OutQuat, vv = the position just written) maps
    // shift_core_f_object into OutPosition.
    if ((flags & 0x0C00) == 0)
    {
        IVP_U_Matrix TempMatrix;
        OutQuat->set_matrix(&TempMatrix);
        TempMatrix.vv.set(OutPosition);
        TempMatrix.vmult4(&shift_core_f_object, OutPosition);
    }

    // OutQuat * q_core_f_object inlined with its own summation order; every
    // read precedes the first store; stores go z, w, x, y.
    if (q_core_f_object != nullptr)
    {
        const IVP_U_Quat* Q2 = q_core_f_object;
        const double Ry = (((double)OutQuat->w * Q2->y + (double)OutQuat->z * Q2->x) +
                           (double)OutQuat->y * Q2->w) -
                          (double)OutQuat->x * Q2->z;
        const float Rz = (float)((((double)OutQuat->w * Q2->z + (double)Q2->w * OutQuat->z) +
                                  (double)Q2->y * OutQuat->x) -
                                 (double)OutQuat->y * Q2->x);
        const float Rw = (float)((((double)OutQuat->w * Q2->w - (double)OutQuat->x * Q2->x) -
                                  (double)Q2->y * OutQuat->y) -
                                 (double)OutQuat->z * Q2->z);
        const double Rx = (((double)OutQuat->x * Q2->w + (double)OutQuat->y * Q2->z) +
                           (double)OutQuat->w * Q2->x) -
                          (double)Q2->y * OutQuat->z;
        OutQuat->z = Rz;
        OutQuat->w = Rw;
        OutQuat->x = (float)Rx;
        OutQuat->y = (float)Ry;
    }
}

// ============================================================================
// FUN_06345710: calc_at_time at the environment's current time. environment
// is read without a test.
// ============================================================================
void IVP_Real_Object::calc_at_time_AT(IVP_U_Quat* OutQuat, IVP_U_Point* OutPosition)
{
    calc_at_time(environment->current_time, OutQuat, OutPosition);
}

// ============================================================================
// FUN_06346170: IVP_Real_Object::EnsureInSimulation. Movement byte 8 enqueues
// original_core (FUN_0634bb90); anything else updates core's time of last
// motion (FUN_063470e0). No pointer is tested. Callers: 06346080 / 063460c0 /
// 06346100 below, PhySys 06285627, 06347dc3.
// ============================================================================
void IVP_Real_Object::EnsureInSimulation()
{
    if (static_cast<uint8_t>(flags) == 8)
    {
        IVP_Environment_Enqueue_Core(environment, original_core);
        return;
    }
    core->update_time_of_last_motion();
}

// ============================================================================
// The IVP_Real_Object methods PhySys calls. None tests a pointer, as in retail.
// ============================================================================

// FUN_06344d30: core->set_mass (FUN_06347c10).
void IVP_Real_Object::change_mass(float NewMass)
{
    core->set_mass(NewMass);
}

// FUN_06345530. Uses the CORE's environment, not this->environment.
void IVP_Real_Object::recheck_collision_filter()
{
    if (ov_element == nullptr)
    {
        return;
    }
    core->Environment->mindist_manager->RecheckOvElement(this);
}

// FUN_06345e60: plain float copies of core->MWorldFCore.vv; Out->hesse is not
// written.
void IVP_Real_Object::get_core_position_ws(IVP_U_Point* Out) const
{
    const IVP_U_Point& Position = core->MWorldFCore.vv;
    Out->k[0] = Position.k[0];
    Out->k[1] = Position.k[1];
    Out->k[2] = Position.k[2];
}

// FUN_06345ea0
float IVP_Real_Object::get_upper_limit_radius() const
{
    return core->UpperLimitRadius;
}

// FUN_06346080. core is re-read after EnsureInSimulation; stores go z, x, y.
void IVP_Real_Object::async_add_speed_object_ws(const IVP_U_Point* SpeedWs)
{
    EnsureInSimulation();
    IVP_Core* const Core = core;
    float const Y = SpeedWs->k[1] + Core->SpeedChange.y;
    float const Z = SpeedWs->k[2] + Core->SpeedChange.z;
    float const X = Core->SpeedChange.x + SpeedWs->k[0];
    Core->SpeedChange.z = Z;
    Core->SpeedChange.x = X;
    Core->SpeedChange.y = Y;
}

// FUN_063460c0: the same shape on core->RotSpeedChange (+0x74).
void IVP_Real_Object::async_add_rot_speed_object_cs(const IVP_U_Point* RotSpeedCs)
{
    EnsureInSimulation();
    IVP_Core* const Core = core;
    float const Y = RotSpeedCs->k[1] + Core->RotSpeedChange.y;
    float const Z = RotSpeedCs->k[2] + Core->RotSpeedChange.z;
    float const X = Core->RotSpeedChange.x + RotSpeedCs->k[0];
    Core->RotSpeedChange.z = Z;
    Core->RotSpeedChange.x = X;
    Core->RotSpeedChange.y = Y;
}

// FUN_06346100: the core's transform at the current time
// (calc_interpolated_transform, FUN_06346890), position and impulse into core
// space (vimult4 / IVP_U_Matrix3::vimult), then FUN_06346c00.
void IVP_Real_Object::async_push_object_ws(const IVP_U_Point* PositionWs, const IVP_U_Point* ImpulseWs)
{
    EnsureInSimulation();
    IVP_Core* const Core = core;

    IVP_U_Matrix Matrix;
    Core->calc_interpolated_transform(environment->current_time.time_base,
                                      environment->current_time.time_offset, &Matrix);

    IVP_U_Point PositionCs;
    Matrix.vimult4(PositionWs, &PositionCs);

    IVP_U_Point ImpulseCs;
    Matrix.vimult(ImpulseWs, &ImpulseCs);

    Core->apply_impulse_at_point(reinterpret_cast<const Vector*>(&PositionCs),
                                 reinterpret_cast<const Vector*>(&ImpulseCs),
                                 reinterpret_cast<const Vector*>(ImpulseWs));
}

// FUN_063450e0: `this` is an IVP_Real_Object (flags +0x78, original_core
// +0x9c). Movement byte 8 calls FUN_06349490 on original_core's sim unit with
// its environment, which IVP_Core::revive_sleeping_core models. Callers:
// FUN_06344dc0 (ensure_in_simulation), 0634380f.
void IVP_Real_Object::ReviveOriginalCoreIfSleeping()
{
    if ((flags & 0xFF) == 8)
    {
        original_core->revive_sleeping_core();
    }
}

// ============================================================================
// LAB_06344de0, IVP_Real_Object vtable slot 2. NewLocalCsys is the object's
// frame expressed in its core's frame (an IVP_U_Matrix: retail reads its
// diagonal at +0x00/+0x14/+0x28). Its inverse ("core from object") gives
// shift_core_f_object (translation) and q_core_f_object (rotation). Retail
// tests the pre-inversion diagonal for the identity fast path; the transpose
// of an exact identity is identical, so that is equivalent.
// ============================================================================
void IVP_Real_Object::ApplyLocalCsys(const IVP_U_Matrix* NewLocalCsys)
{
    // Retail runs the (empty) Csys ctor over this stack area first; it is
    // only scratch for set_inverse.
    IVP_U_Matrix CoreFObject; // inverse(NewLocalCsys)
    CoreFObject.set_inverse(NewLocalCsys);

    // Every anchor's core-space point is rebuilt from its object-space point.
    for (IVP_Anchor* Anchor = anchors; Anchor != nullptr; Anchor = Anchor->anchor_next_in_object)
    {
        CoreFObject.vmult4(&Anchor->object_pos, &Anchor->core_pos); // FUN_063484e0
    }

    // shift_core_f_object = CoreFObject.vv, or exactly zero (plus flags bit
    // 0x400) when its squared length is below DAT_063a5438 (~9e-6) or NaN.
    const float ShiftEpsilon = BitsToFloat(0x3716feb5); // DAT_063a5438
    const double ShiftQuadLen = ((double)CoreFObject.vv.k[2] * CoreFObject.vv.k[2]
                               + (double)CoreFObject.vv.k[1] * CoreFObject.vv.k[1])
                              + (double)CoreFObject.vv.k[0] * CoreFObject.vv.k[0];
    if (!(ShiftQuadLen >= ShiftEpsilon))
    {
        flags = (flags & ~0x800u) | 0x400u;
        shift_core_f_object.x = 0.0f;
        shift_core_f_object.y = 0.0f;
        shift_core_f_object.z = 0.0f;
    }
    else
    {
        shift_core_f_object.x = CoreFObject.vv.k[0];
        shift_core_f_object.y = CoreFObject.vv.k[1];
        shift_core_f_object.z = CoreFObject.vv.k[2];
        flags &= ~0xc00u;
    }

    // q_core_f_object is freed when the diagonal is exactly 1.0f (each test
    // passes on equal OR unordered, so a NaN diagonal counts as identity);
    // otherwise rebuilt from the inverse's rotation and normalized.
    const float IdentitySentinel = BitsToFloat(0x3f800000); // DAT_063a53d0, exact 1.0f
    if (!(NewLocalCsys->rows[0].k[0] < IdentitySentinel) && !(NewLocalCsys->rows[0].k[0] > IdentitySentinel) &&
        !(NewLocalCsys->rows[1].k[1] < IdentitySentinel) && !(NewLocalCsys->rows[1].k[1] > IdentitySentinel) &&
        !(NewLocalCsys->rows[2].k[2] < IdentitySentinel) && !(NewLocalCsys->rows[2].k[2] > IdentitySentinel))
    {
        ::operator delete(q_core_f_object); // FUN_06391d5a
        q_core_f_object = nullptr;
    }
    else
    {
        if (q_core_f_object == nullptr)
            q_core_f_object = static_cast<IVP_U_Quat*>(::operator new(sizeof(IVP_U_Quat)));
        q_core_f_object->set_from_matrix(&CoreFObject); // the inverse's rotation
        q_core_f_object->normize(); // FUN_06350fb0
    }

    // Invalidate the narrow-phase cache slot, if any.
    if (cache_object != nullptr)
    {
        IVP_Real_Object_Free_Cache_Object(this); // FUN_0634d510
    }
}

// LAB_06344f10, IVP_Real_Object vtable slot 1.
void IVP_Real_Object::SetLocalCsysFromQuatAndPosition(const IVP_U_Quat* Orientation, const IVP_U_Point* Position)
{
    IVP_U_Matrix LocalCsys;
    Orientation->set_matrix(&LocalCsys); // FUN_06350bc0, rows only

    LocalCsys.vv.x = Position->x;
    LocalCsys.vv.y = Position->y;
    LocalCsys.vv.z = Position->z;

    ApplyLocalCsys(&LocalCsys); // slot 2
}

// ============================================================================
// IVP_SurfaceManager / IVP_SurfaceManager_Default (see ivp_surface_manager.h
// for both vtables). Bodies that hold retail's deliberate `MOV dword ptr
// [0],0` call IVP_CoreTrap.
// ============================================================================

// The append slots 4 and 5 open-code: count >= capacity (signed) grows through
// FUN_0635d3e0, then elems[count++] = Element. No null tests.
static void IVP_SurfaceManagerOutListAppend(IVP_SurfaceManager_OutList* OutList, void* Element)
{
    if (OutList->Count >= OutList->Capacity)
        IVP_U_BigVector_Grow(OutList);

    OutList->Elems[OutList->Count] = Element;
    ++OutList->Count;
}

// FUN_0635d440: only reinstalls the base vptr.
IVP_SurfaceManager::~IVP_SurfaceManager()
{
}

// Slots 7 and 8: the shared `RET 4` at 062ed730, in both tables (a base
// default the derived class inherits).
void IVP_SurfaceManager::add_reference_to_ledge(void* /*Param1*/)
{
}

void IVP_SurfaceManager::remove_reference_to_ledge(void* /*Param1*/)
{
}

// FUN_06362370: allocates the 0x20-byte block 16-byte aligned (FUN_06343fc0)
// and zero-fills it, with no null test.
IVP_SurfaceManager_Default::IVP_SurfaceManager_Default()
{
    GeometryBlock = ivp_malloc_aligned(kIVPSurfaceManagerGeometryBlockSize,
                                       kIVPSurfaceManagerGeometryBlockAlign); // FUN_06343fc0
    std::memset(GeometryBlock, 0, kIVPSurfaceManagerGeometryBlockSize);
}

// FUN_063624e0 (slot 9's scalar deleting wrapper is FUN_063624c0).
IVP_SurfaceManager_Default::~IVP_SurfaceManager_Default()
{
    if (GeometryBlock != nullptr)
    {
        ivp_free_aligned(GeometryBlock); // FUN_06343ff0
        GeometryBlock = nullptr;
    }
}

// Slot 0
void* IVP_SurfaceManager_Default::get_single_convex()
{
    return GeometryBlock;
}

// Slot 1 (063623d0): zeroes three floats and returns the pointer. Used by
// IVP_InitCoreRestStateAndDamping when there is no centre-of-mass override.
float* IVP_SurfaceManager_Default::get_mass_center(float* OutVec3)
{
    OutVec3[0] = 0.0f;
    OutVec3[1] = 0.0f;
    OutVec3[2] = 0.0f;
    return OutVec3;
}

// Slot 2, LAB_063623f0: traps.
void IVP_SurfaceManager_Default::get_radius_and_radius_dev_to_given_center(void* /*Param1*/, void* /*Param2*/, void* /*Param3*/)
{
    IVP_CoreTrap(); // 063623f0
}

// Slot 3, LAB_0634dc00: traps (ICF-shared with several unrelated vtables).
void IVP_SurfaceManager_Default::get_rotation_inertia(void* /*Param1*/)
{
    IVP_CoreTrap(); // 0634dc00
}

// Slot 4, LAB_06362400. Both squares stay on the x87 stack; the append is
// skipped on less OR unordered. The element appended is this manager's own
// GeometryBlock.
void IVP_SurfaceManager_Default::get_all_ledges_within_radius(const float* Center3, float Radius,
                                                           void* /*Param3*/, void* /*Param4*/,
                                                           void* /*Param5*/,
                                                           IVP_SurfaceManager_OutList* OutList)
{
    const double DistanceSq = ((double)(Center3[0]) * Center3[0] +
                               (double)(Center3[1]) * Center3[1]) +
                              (double)(Center3[2]) * Center3[2];
    const double RadiusSq = (double)(Radius) * Radius;
    if (!(RadiusSq >= DistanceSq))
        return;

    IVP_SurfaceManagerOutListAppend(OutList, GeometryBlock);
}

// Slot 5, FUN_06362460: retail traps first and then still appends, so the
// slot is effectively dead. Both halves reproduced, in order.
void IVP_SurfaceManager_Default::get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList)
{
    IVP_CoreTrap(); // 06362465
    IVP_SurfaceManagerOutListAppend(OutList, GeometryBlock);
}

// Slot 6, LAB_063624a0: traps.
void IVP_SurfaceManager_Default::insert_all_ledges_hitting_ray(void* /*Param1*/, void* /*Param2*/)
{
    IVP_CoreTrap(); // 063624a0
}

// Slot 10, 063624b0 (ICF-folded).
int32_t IVP_SurfaceManager_Default::get_type()
{
    return 1;
}

// DAT_0658afe0: built by a CRT static initializer and destroyed from atexit
// in retail; a file-scope object does the same.
IVP_SurfaceManager_Default g_IVP_DefaultSurfaceManager_0658afe0;

// Port-only accessor (retail has no getter).
IVP_SurfaceManager* IVP_GetDefaultSurfaceManager()
{
    return &g_IVP_DefaultSurfaceManager_0658afe0;
}

// ============================================================================
// The two IVP_Real_Object leaves, IVP_Polygon (object_type 2) and IVP_Ball
// (object_type 3); see ivp_real_object.h. Both constructors: base ctor
// FUN_06345100, vptr, object_type, field_94, then FUN_06345b20
// (IVP_InitCoreRestStateAndDamping) with (Father->environment, Parms), then
// FUN_0634cea0 (object listeners slot 1) with a stack {Core->Environment,
// this} -- the record FUN_0634b480 also builds
// (IVP_Environment_Object_Notify_Event). No null tests.
// ============================================================================

// FUN_06362280. vptr 063a5824.
IVP_Polygon::IVP_Polygon(IVP_Cluster* Father, IVP_SurfaceManager* SurfaceManager,
                         const IVP_RealObjectConstructionTemplate* Parms,
                         const IVP_U_Quat* InitialOrientationOrNull,
                         const IVP_U_Point* InitialPosition)
    : IVP_Real_Object(Father, SurfaceManager, Parms, InitialOrientationOrNull, InitialPosition)
{
    object_type = 2;

    // A plain dword copy (IVP_Ball adds its radius).
    field_94 = Parms->Field_0x4c;

    IVP_InitCoreRestStateAndDamping(this, Father->environment, Parms);

    IVP_Environment_Object_Notify_Event Event;
    Event.Env = core->Environment;
    Event.Object = this;
    IVP_Environment_Notify_Object_Listeners_Slot1(core->Environment, &Event);
}

// FUN_06362570. vptr 063a585c.
IVP_Ball::IVP_Ball(IVP_Cluster* Father, const float* Radius,
                   const IVP_RealObjectConstructionTemplate* Parms,
                   const IVP_U_Quat* InitialOrientationOrNull,
                   const IVP_U_Point* InitialPosition)
    // Retail passes a null surface manager to the base ctor and overwrites the
    // field below.
    : IVP_Real_Object(Father, nullptr, Parms, InitialOrientationOrNull, InitialPosition)
{
    object_type = 3;

    // The only user of the module's default surface manager in Common.dll.
    surface_manager = &g_IVP_DefaultSurfaceManager_0658afe0; // +0x80

    // A real float addition (IVP_Polygon copies).
    field_94 = Parms->Field_0x4c + *Radius;

    IVP_InitCoreRestStateAndDamping(this, Father->environment, Parms);

    IVP_Environment_Object_Notify_Event Event;
    Event.Env = core->Environment;
    Event.Object = this;
    IVP_Environment_Notify_Object_Listeners_Slot1(core->Environment, &Event);
}

// ============================================================================
// FUN_06345b20: the mass-properties and rest-state step of real-object
// construction. Callers: IVP_Polygon (063622e6), IVP_Ball (063625e3) and
// IVP_Split_Object_From_Core (06343b07); each passes the object's environment
// and the construction template.
//
// object_type (2 or 3, anything else traps) selects:
//   * UpperLimitRadius / SpeedLimit addends: 0 / 0 for a ball; for a polygon,
//     surface_manager slot 2 with &LocalCsys.vv as the centre (so LocalCsys
//     must be built first, as in retail).
//   * the scaled rotation inertia: a polygon's surface_manager slot 3, or a
//     ball's isotropic r*r*0.4f (DAT_063a5458, the solid-sphere 2/5) with r
//     the radius just stored as UpperLimitRadius. Retail stashes object_type
//     in [ESP+0x1c] for this second switch because EBP is reused.
// ============================================================================
void IVP_InitCoreRestStateAndDamping(IVP_Real_Object* Self, IVP_Environment* Environment,
                                     const IVP_RealObjectConstructionTemplate* Template)
{
    IVP_U_Matrix LocalCsys; // the rows' trivial constructor (eh vector ctor, 3 x 0x10)
    const int32_t ObjectType = Self->object_type; // kept in [ESP+0x1c] for both switches

    // Rotation: identity, applied by set_matrix after the translation is
    // fetched. set_matrix writes the three rows only.
    IVP_U_Quat IdentityLocalQuat{0.0f, 0.0f, 0.0f, 1.0f};

    // Translation: Template->OptionalComOverride's +0x30 triple when present,
    // else surface_manager slot 1, called unconditionally (three floats).
    if (Template->OptionalComOverride != nullptr)
    {
        const IVP_U_Point* Override = reinterpret_cast<const IVP_U_Point*>(
            static_cast<const uint8_t*>(Template->OptionalComOverride) + 0x30);
        LocalCsys.vv.x = Override->k[0];
        LocalCsys.vv.y = Override->k[1];
        LocalCsys.vv.z = Override->k[2];
    }
    else
    {
        Self->surface_manager->get_mass_center(&LocalCsys.vv.k[0]);
    }
    IdentityLocalQuat.set_matrix(&LocalCsys); // FUN_06350bc0

    Self->ApplyLocalCsys(&LocalCsys); // slot 2

    IVP_Core* const Core = Self->core; // loaded after the call

    // UpperLimitRadius / SpeedLimit addends (FUN_06346870).
    float MaxAngularSpeedAddend = 0.0f;
    float SpeedLimitValue = 0.0f;
    if (ObjectType == 3)
    {
        // Both 0.0f.
    }
    else if (ObjectType == 2)
    {
        // Slot 2 (adapter LAB_0634acb0): D = |Inner.center - centre|; writes
        // D + Inner.radius to the first output and D + kind * radius *
        // DAT_063a54b4 to the second. No null test on surface_manager.
        Self->surface_manager->get_radius_and_radius_dev_to_given_center(
            const_cast<IVP_U_Point*>(&LocalCsys.vv), &MaxAngularSpeedAddend, &SpeedLimitValue);
    }
    else
    {
        IVP_CoreTrap(); // 06345bec (assert-fail trap), then the shared tail
    }

    // The sum is rounded into the addend's own stack slot and reused below as
    // the sphere radius.
    const float Radius = Self->field_94 + MaxAngularSpeedAddend;
    Core->SetSpeedLimits(Radius, SpeedLimitValue); // FUN_06346870

    // ---- mass and rotation inertia ----
    {
        // Mass below DAT_063a545c (a small epsilon) or NaN becomes 1.0f.
        const float MassEpsilon = BitsToFloat(0x3b449ba6); // DAT_063a545c
        const float MassValue = !(Template->MassInput >= MassEpsilon) ? 1.0f : Template->MassInput;

        // Both scaled shapes fill a scratch and share the tail at 06345cb0,
        // where x and y keep s*T unrounded before the mass multiply while z is
        // stored as a float first.
        float ScaleScratch[3];
        bool bScaled = true;
        if (Template->AutoCheckRotInertia == 0)
        {
            // Direct, unscaled copy.
            Core->RotInertia.x = Template->InertiaBoxVector.x;
            Core->RotInertia.y = Template->InertiaBoxVector.y;
            Core->RotInertia.z = Template->InertiaBoxVector.z;
            bScaled = false;
        }
        else if (ObjectType == 2)
        {
            // Surface manager slot 3 (adapter LAB_0634ac90). No null test.
            IVP_U_Point Scratch;
            Self->surface_manager->get_rotation_inertia(&Scratch);
            ScaleScratch[0] = Scratch.x;
            ScaleScratch[1] = Scratch.y;
            ScaleScratch[2] = Scratch.z;
        }
        else if (ObjectType == 3)
        {
            // r*r*0.4f as one x87 product, stored to all three slots.
            const float SphereCoefficient = BitsToFloat(0x3ecccccd); // DAT_063a5458 == 0.4f
            const float Scratch = (float)((double)(Radius) * Radius * SphereCoefficient);
            ScaleScratch[0] = Scratch;
            ScaleScratch[1] = Scratch;
            ScaleScratch[2] = Scratch;
        }
        else
        {
            // 06345c78 traps; retail then runs the tail on uninitialised stack.
            IVP_CoreTrap();
            ScaleScratch[0] = ScaleScratch[1] = ScaleScratch[2] = 0.0f;
        }

        if (bScaled)
        {
            const float ScaledZ = (float)((double)(ScaleScratch[2]) * Template->InertiaBoxVector.z);
            Core->RotInertia.x = (float)((double)(ScaleScratch[0]) * Template->InertiaBoxVector.x * MassValue);
            Core->RotInertia.y = (float)((double)(ScaleScratch[1]) * Template->InertiaBoxVector.y * MassValue);
            Core->RotInertia.z = (float)((double)(ScaledZ) * MassValue);
        }

        // A clamp factor of 0 (or NaN) skips the inertia floor.
        if (Template->RotInertiaClampFactor < 0.0f || Template->RotInertiaClampFactor > 0.0f)
        {
            // |RotInertia| (FUN_06347e60) times the factor stays on the FPU;
            // each component below it (ordered) is raised to it.
            const double X = Core->RotInertia.x;
            const double Y = Core->RotInertia.y;
            const double Z = Core->RotInertia.z;
            const double Threshold = std::sqrt((X * X + Y * Y) + Z * Z) * Template->RotInertiaClampFactor;

            if (Threshold > Core->RotInertia.x) Core->RotInertia.x = (float)(Threshold);
            if (Threshold > Core->RotInertia.y) Core->RotInertia.y = (float)(Threshold);
            if (Threshold > Core->RotInertia.z) Core->RotInertia.z = (float)(Threshold);
        }

        Core->Mass = MassValue;
    }

    Core->SpeedDamp = Template->SpeedDampInput;
    Core->RotSpeedDamp.x = Template->RotSpeedDampInputX;
    Core->RotSpeedDamp.y = Template->RotSpeedDampInputY;
    Core->RotSpeedDamp.z = Template->RotSpeedDampInputZ;

    // 06345d61 FUN_063483d0 inverts LocalCsys into a stack matrix nothing
    // reads; no other effect.

    // A freshly built body starts at rest.
    Core->RotSpeed.x = Core->RotSpeed.y = Core->RotSpeed.z = 0.0f;
    Core->Speed.x = Core->Speed.y = Core->Speed.z = 0.0f;

    // FUN_06347410: InvRotInertia / InvMass / isotropy bits / DAngularLimitFactor.
    Core->RecomputeInverseInertia();

    // FUN_063474d0 with the same (pre-inversion) LocalCsys.
    Core->init_world_transform_from_local_csys(&LocalCsys);

    // ------------------------------------------------------------------
    // Time tail on the Environment argument. Alpha (next PSI minus now) stays
    // on the FPU; only Alpha > 1e-10f (DAT_063a53d8, ordered) uses
    // {float(Alpha), float(1.0f / Alpha)} with the unrounded Alpha, else
    // {delta_time, inv_delta_time}.
    // ------------------------------------------------------------------
    {
        float DeltaTime;
        float InvDeltaTime;
        const double Alpha = (((double)(Environment->next_psi_time_base) - Environment->current_time.time_base) +
                              Environment->next_psi_time_offset) - Environment->current_time.time_offset;
        if (Alpha > (double)(1.0e-10f))
        {
            DeltaTime = (float)(Alpha);
            InvDeltaTime = (float)(1.0 / Alpha);  // DAT_063a53d0 == 1.0f
        }
        else
        {
            DeltaTime = Environment->delta_time;             // +0xac
            InvDeltaTime = Environment->inv_delta_time;      // +0xb0
        }

        Core->synchronize_simulation_time_snapshot(DeltaTime, InvDeltaTime); // FUN_06346e70
    }
}

// ============================================================================
// IVP_RealObjectConstructionTemplate members
// ============================================================================

// FUN_063486c0. FUN_06348660 zeroes +0x00 twice (an inlined member ctor), a
// REP STOSD clears 0x16 dwords, then the defaults are stored in retail's
// order, +0x14 last.
IVP_RealObjectConstructionTemplate::IVP_RealObjectConstructionTemplate()
{
    std::memset(this, 0, sizeof(IVP_RealObjectConstructionTemplate));
    MassInput = 1.0f;
    AutoCheckRotInertia = 1;
    InertiaBoxVector.x = 1.0f;
    InertiaBoxVector.y = 1.0f;
    InertiaBoxVector.z = 1.0f;
    SpeedDampInput = BitsToFloat(0x3c23d70a);
    RotInertiaClampFactor = BitsToFloat(0x3cf5c28f);
    RotSpeedDampInputX = BitsToFloat(0x3c23d70a);
    RotSpeedDampInputY = BitsToFloat(0x3c23d70a);
    RotSpeedDampInputZ = BitsToFloat(0x3c23d70a);
    std::memset(_unknown_0x14, 0, sizeof(_unknown_0x14)); // second zeroing
}

// FUN_06348670 (thunk 06348710)
IVP_RealObjectConstructionTemplate::~IVP_RealObjectConstructionTemplate()
{
    if (LongNamePtr != nullptr)
    {
        std::free(LongNamePtr);
        LongNamePtr = nullptr;
    }
}

// FUN_06348720. A null Name stores 0 into +0x04. A name longer than 8 hits
// retail's assert stand-in (a store to address 0), reproduced; strncpy does
// not terminate an 8-character name.
void IVP_RealObjectConstructionTemplate::set_short_name(const char* Name)
{
    if (Name == nullptr)
    {
        ShortNameTag[0] = 0;
        return;
    }
    if (std::strlen(Name) > 8)
    {
        volatile int32_t* Trap = nullptr;
        *Trap = 0;
    }
    std::strncpy(ShortNameTag, Name, 8);
}

// FUN_06348690. FUN_06356920 (IVP's strdup) returns null for a null Name.
void IVP_RealObjectConstructionTemplate::set_long_name(const char* Name)
{
    if (LongNamePtr != nullptr)
    {
        std::free(LongNamePtr);
        LongNamePtr = nullptr;
    }
    LongNamePtr = (Name != nullptr) ? _strdup(Name) : nullptr;
}

// ============================================================================
// ensure_in_simulation 06344dc0, disable_simulation 06344fb0,
// recheck_mindists_of_other_cores 06344f50, beam_object_to_new_position
// 06345f90, core merge 06343700, core split 06343940.
// ============================================================================

void IVP_Core_Add_Object(IVP_Core* Core, IVP_Real_Object* Object); // ivp_core.cpp, FUN_06346ef0
void __fastcall IVP_Core_RefreshContactPoints(IVP_Core* Core);                      // ivp_core.cpp
void IVP_RescheduleCollisionEvent(IVP_Ledge_Mindist* This, int32_t RearmWhenFar, int32_t EventMode); // ivp_mindist_limiter.cpp, FUN_0634f320

// FUN_06344dc0: tail-jumps to FUN_063450e0 when core's movement byte is 8.
void IVP_Real_Object::ensure_in_simulation()
{
    if (static_cast<uint8_t>(core->MovementState & 0xffu) == 8)
        ReviveOriginalCoreIfSleeping();
}

// FUN_06344fb0. A fixed core returns at once; a queued core is dequeued
// (0634bb20); a sleeping one (signed movement byte >= 8) returns. Otherwise
// every object's contact points are unlinked, the last-motion snapshot
// (+0x170 / +0x190) is taken, and TimeOfLastMotion = now - 20.0f
// (DAT_063a543c), split with ftol (truncation). Then the sim unit runs
// 063495c0 and 063495e0(Environment).
void IVP_Real_Object::disable_simulation()
{
    IVP_Core* const Core = core;
    if ((Core->Flags & 0xcu) != 0)
        return;
    if ((Core->Flags & 0x30u) != 0)
        IVP_Environment_Dequeue_Core(Core->Environment, Core);
    if (static_cast<int8_t>(Core->MovementState & 0xffu) >= 8)
        return;

    for (int32_t Index = static_cast<int32_t>(Core->Objects.n_elems) - 1; Index >= 0; --Index)
        Core->Objects.elems[Index]->unlink_contact_points(true);

    Core->QLastMotion = Core->QWorldFCoreNext;
    Core->PosLastMotion.x = Core->PosWorldFCore.x;
    Core->PosLastMotion.y = Core->PosWorldFCore.y;
    Core->PosLastMotion.z = Core->PosWorldFCore.z;

    const IVP_Environment* const Env = Core->Environment;
    const double Now = ((double)(Env->current_time.time_base) - 20.0f) + Env->current_time.time_offset;
    const int32_t Base = static_cast<int32_t>(Now);
    Core->TimeOfLastMotion.time_base = (float)(Base);
    Core->TimeOfLastMotion.time_offset = (float)(Now - (double)(Base));

    IVP_Simulation_Unit_Rebalance_And_Clear_Topology_Flag(Core->OwningSimUnit);           // 063495c0
    IVP_Simulation_Unit_Update_Motion_And_Maybe_Sleep(Core->OwningSimUnit, Core->Environment); // 063495e0
}

// FUN_06344f50. The next synapse is read before the mindist is touched.
void IVP_Real_Object::recheck_mindists_of_other_cores()
{
    IVP_Synapse_Real* Synapse = synapse_list_head;
    while (Synapse != nullptr)
    {
        IVP_Ledge_Mindist* const Mindist = Synapse->get_mindist();
        IVP_Synapse_Real* const Next = Synapse->next;
        if (Mindist->synapse[0].l_obj->core->Unconfirmed_0x1B8 != Mindist->synapse[1].l_obj->core->Unconfirmed_0x1B8)
        {
            Mindist->DispatchNarrowPhaseSolve(); // FUN_06352cf0
            if ((Mindist->StateFlags & 0xc000u) == 0)
                IVP_RescheduleCollisionEvent(Mindist, 0, 2); // FUN_0634f320
        }
        Synapse = Next;
    }
}

// FUN_06345f90. The core's pose from the object's: unless flags & 0xc00,
// PosWorldFCore = M(RotationWs, PositionWs) * -shift_core_f_object; with
// q_core_f_object, QWorldFCore = RotationWs * inverse(q_core_f_object)
// (06351190, whose term order set_mult_inv_quat reproduces). Then
// set_transformation (06351b90).
void IVP_Real_Object::beam_object_to_new_position(const IVP_U_Quat* RotationWs, const IVP_U_Point* PositionWs, int32_t bOptimizeForRepeatedCalls)
{
    IVP_Core* const Core = core;
    IVP_U_Quat QWorldFCore = *RotationWs;
    IVP_U_Point PosWorldFCore = *PositionWs;

    if ((flags & 0xc00u) == 0)
    {
        IVP_U_Matrix MWorldFObject;
        RotationWs->set_matrix(&MWorldFObject);
        MWorldFObject.vv.x = PositionWs->x;
        MWorldFObject.vv.y = PositionWs->y;
        MWorldFObject.vv.z = PositionWs->z;
        IVP_U_Point ShiftObjectFCore;
        ShiftObjectFCore.x = -shift_core_f_object.x;
        ShiftObjectFCore.y = -shift_core_f_object.y;
        ShiftObjectFCore.z = -shift_core_f_object.z;
        MWorldFObject.vmult4(&ShiftObjectFCore, &PosWorldFCore);
    }

    if (q_core_f_object != nullptr)
        QWorldFCore.set_mult_inv_quat(RotationWs, static_cast<const IVP_U_Quat*>(q_core_f_object));

    IVP_Calc_Next_PSI_Solver Solver(Core);
    Solver.set_transformation(&QWorldFCore, &PosWorldFCore, bOptimizeForRepeatedCalls);
}

// FUN_06343700. A negative (or NaN) Distance is measured: both cores'
// positions extrapolated to the host environment's time (FUN_0628be60, dt
// rounded to float), Distance = float(sqrt((dz*dz + dy*dy) + dx*dx)). When
// exactly one object is asleep it is revived. The object's pose comes from its
// cache slot. The host core's UpperLimitRadius and SpeedLimit grow to
// Distance + the old core's value when strictly greater. Then the object moves
// to the host core, takes the host's movement byte, and is beamed to its pose
// (PhySys::BeamBodyToWorldPose, 06343cc0).
void IVP_Merge_Object_Into_Core(IVP_Real_Object* Host, IVP_Real_Object* Object, float Distance)
{
    if (!(Distance >= 0.0f))
    {
        const IVP_Environment* const Env = Host->environment;
        const float NowBase = Env->current_time.time_base;
        const float NowOffset = Env->current_time.time_offset;

        double Position[2][3];
        IVP_Real_Object* const Objects[2] = { Host, Object };
        for (int32_t Side = 0; Side < 2; ++Side)
        {
            const IVP_Core* const Core = Objects[Side]->core;
            const float Dt = (float)((((double)(NowBase) - Core->TimeOfLastPsi.time_base) + NowOffset) - Core->TimeOfLastPsi.time_offset);
            Position[Side][0] = (float)((double)(Core->PosWorldFCore.x) + (double)(Core->DeltaWorldFCore.x) * Dt);
            Position[Side][1] = (float)((double)(Core->PosWorldFCore.y) + (double)(Core->DeltaWorldFCore.y) * Dt);
            Position[Side][2] = (float)((double)(Core->PosWorldFCore.z) + (double)(Core->DeltaWorldFCore.z) * Dt);
        }
        const double Dx = Position[0][0] - Position[1][0];
        const double Dy = Position[0][1] - Position[1][1];
        const double Dz = Position[0][2] - Position[1][2];
        Distance = (float)(std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx));
    }

    const bool bHostSimulated = static_cast<int8_t>(Host->flags & 0xffu) < 8;
    const bool bObjectSimulated = static_cast<int8_t>(Object->flags & 0xffu) < 8;
    if (bHostSimulated && !bObjectSimulated)
        Object->ReviveOriginalCoreIfSleeping();
    else if (!bHostSimulated && bObjectSimulated)
        Host->ReviveOriginalCoreIfSleeping();

    IVP_Core* const OldCore = Object->core;
    const IVP_NarrowPhaseCacheSlot* const Slot = Object->get_cache_object_no_lock();
    const IVP_U_Quat Quat = Slot->Quat;
    Vector Position;
    Position.x = Slot->WorldTransform.vv.x;
    Position.y = Slot->WorldTransform.vv.y;
    Position.z = Slot->WorldTransform.vv.z;

    IVP_Core* const NewCore = Host->core;
    const double Radius = (double)(Distance) + OldCore->UpperLimitRadius;
    if (Radius > NewCore->UpperLimitRadius)
        NewCore->UpperLimitRadius = (float)(Radius);
    const double Limit = (double)(Distance) + OldCore->SpeedLimit;
    if (Limit > NewCore->SpeedLimit)
        NewCore->SpeedLimit = (float)(Limit);

    Object->unlink_contact_points(true);
    IVP_Core_Remove_Object(OldCore, Object);
    Object->core = NewCore;
    Object->original_core = NewCore;
    Object->field_a0 = NewCore;
    IVP_Core_Add_Object(NewCore, Object);

    Object->flags = (Object->flags & ~0xffu) | (Host->flags & 0xffu);

    PhySys::BeamBodyToWorldPose(nullptr, Object, &Quat.x, Position);
    IVP_Core_RefreshContactPoints(NewCore);
}

// FUN_06343940. The object leaves its core (which is deleted when it held only
// this object) for a new core at its cached pose, keeping the old surface speed
// and rotation speed, then gets a fresh PSI matrix and its mindists rechecked.
// The hull manager's gradients are zeroed and AccumulatedDeltaA /
// OuterValueAtLastEdit grow by |shift_core_f_object|. DeltaPsi > 1e-10f
// (DAT_063a53d8, ordered) picks {DeltaPsi, 1/DeltaPsi}, else the environment's
// step.
void IVP_Split_Object_From_Core(IVP_Real_Object* Object, const IVP_RealObjectConstructionTemplate* Template)
{
    Object->unlink_contact_points(true);
    IVP_Environment* const Env = Object->environment;
    IVP_Core* OldCore = Object->core;

    const IVP_NarrowPhaseCacheSlot* const Slot = Object->get_cache_object_no_lock();
    IVP_U_Quat Quat = Slot->Quat;
    IVP_U_Point Position = {};
    Position.x = Slot->WorldTransform.vv.x;
    Position.y = Slot->WorldTransform.vv.y;
    Position.z = Slot->WorldTransform.vv.z;

    IVP_U_Point SurfaceSpeed = {};
    OldCore->get_surface_speed(&Position, &SurfaceSpeed); // 06346290
    const float OldRotSpeedX = OldCore->RotSpeed.x;
    const float OldRotSpeedY = OldCore->RotSpeed.y;
    const float OldRotSpeedZ = OldCore->RotSpeed.z;
    const float ShiftLength = (float)(std::sqrt(
        (double)(Object->shift_core_f_object.x) * Object->shift_core_f_object.x
        + (double)(Object->shift_core_f_object.y) * Object->shift_core_f_object.y
        + (double)(Object->shift_core_f_object.z) * Object->shift_core_f_object.z));

    if (Template->ShortNameTag[0] != 0)
        std::strncpy(Object->name, Template->ShortNameTag, 8);

    // A count of at most 1 (signed) means the old core was deleted.
    const int32_t OldObjectCount = OldCore->Objects.n_elems;
    IVP_Core_Remove_Object(OldCore, Object);
    if (OldObjectCount <= 1)
        OldCore = nullptr;
    else if (OldCore != nullptr)
        IVP_Core_RefreshContactPoints(OldCore);

    Object->flags = (Object->flags & 0xffffff08u) | 8u;
    IVP_Core* NewCore = new IVP_Core(Object, &Quat, &Position, 0, Template->FlagsLo);
    Object->core = NewCore;
    Object->original_core = NewCore;
    Object->field_a0 = NewCore;
    if (Template->Material != nullptr)
        Object->material = Template->Material;
    if (Template->ClientData != nullptr)
        Object->client_data = Template->ClientData;
    Object->field_94 = Template->Field_0x4c;
    IVP_InitCoreRestStateAndDamping(Object, Env, Template); // 06345b20(Env, Template)

    // Retail quirk kept: OldCore is null here when the old core held only this
    // object, and retail still reads its +0x60 (an access violation), so retail
    // only ever splits objects out of cores holding two or more.
    if (static_cast<int8_t>(OldCore->MovementState & 0xffu) >= 8)
        return;

    // Object->core is re-read each time, as in retail.
    Object->core->fire_event_object_frozen(); // 0634b590
    Object->ensure_in_simulation();           // 06344dc0
    IVP_Core* SpeedCore = Object->core;
    SpeedCore->Speed.x = SurfaceSpeed.x;
    SpeedCore->Speed.y = SurfaceSpeed.y;
    SpeedCore->Speed.z = SurfaceSpeed.z;
    SpeedCore = Object->core;
    SpeedCore->RotSpeed.x = OldRotSpeedX;
    SpeedCore->RotSpeed.y = OldRotSpeedY;
    SpeedCore->RotSpeed.z = OldRotSpeedZ;

    const float DeltaPsi = (float)((((double)(Env->next_psi_time_base) - Env->current_time.time_base) + Env->next_psi_time_offset) - Env->current_time.time_offset);
    IVP_U_Vector_Fixed<IVP_Hull_Manager, 0x80> ActiveHullManagers;

    NewCore = Object->core;
    if ((NewCore->Flags & 0xcu) == 0)
    {
        IVP_Hull_Manager& Hull = Object->hull_manager;
        Hull.OuterGradient = 0.0f;
        Hull.InnerGradient = 0.0f;
        const float Value = (float)((double)(ShiftLength) + Hull.AccumulatedDeltaA);
        Hull.AccumulatedDeltaA = Value;
        Hull.OuterValueAtLastEdit = Value;

        IVP_Event_Sim EventSim;
        if (DeltaPsi > 1e-10f) // DAT_063a53d8
        {
            EventSim.delta_time = DeltaPsi;
            EventSim.i_delta_time = (float)(1.0 / (double)(DeltaPsi)); // DAT_063a53d0 = 1.0f
        }
        else
        {
            EventSim.delta_time = Env->delta_time;
            EventSim.i_delta_time = Env->inv_delta_time;
        }
        EventSim.environment = Env;
        // +0x0c (sim_unit) is not written, as in retail; calc_next_PSI_matrix
        // reads only +0x00/+0x04.

        IVP_Calc_Next_PSI_Solver Solver(NewCore);
        Solver.calc_next_PSI_matrix(&EventSim, &ActiveHullManagers);
        NewCore->FastSimUnitData = nullptr;
        IVP_Calc_Next_PSI_Solver::commit_all_hull_managers(Env, &ActiveHullManagers);
        Object->recalc_exact_mindists();
        Object->recheck_mindists_of_other_cores();
    }
}
