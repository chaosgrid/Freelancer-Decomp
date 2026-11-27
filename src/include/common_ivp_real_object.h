// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll's IVP object hierarchy (IVP_Object .. IVP_Real_Object, IVP_Polygon, IVP_Ball).
// Public header (src/IVP/ivp_physics/ivp_real_object.h only includes it): Server.dll reads IVP_Real_Object
// fields directly (StarSystem_MpCollisionFilter 06d06c30: client_data +0xa4, controller_phantom
// +0x1c; the mine push 06cf3c03: core +0x98), and the client's collision filter LAB_005544e0
// reads client_data, so the layout is a cross-DLL contract.

#include <cstdint>
#include "../IVP/ivp_utility/ivp_math.h"
#include "../IVP/ivp_physics/ivp_core.h"
// Full include: IVP_Ball's constructor stores the default IVP_SurfaceManager instance's
// address.
#include "../IVP/ivp_physics/ivp_surface_manager.h"

// Forward declarations
class IVP_Environment;
class IVP_Core;
class IVP_Material;
class IVP_OV_Element; // src/IVP/ivp_physics/ivp_ov_element.h -- IVP_Real_Object::ov_element (+0x90).
// src/IVP/ivp_physics/ivp_mindist.h -- the node type IVP_Real_Object::
// synapse_list_head (+0x20) heads. Only a pointer is needed here.
class IVP_Synapse_Real;
class IVP_Controller_Phantom; // src/IVP/ivp_physics/ivp_phantom.h
class IVP_Template_Phantom;   // src/IVP/ivp_physics/ivp_phantom.h

#pragma pack(push, 4)

// ============================================================================
// IVP_Priority_Pool -- retail's generic fixed-slot priority pool, the 0x14-byte
// object FUN_06362ef0 (ctor) / FUN_06362f70 (dtor) / FUN_06362f90 (Insert) /
// FUN_06363280 (Remove) operate on. Used from 30+ call sites with caller-defined payloads.
//
// Two instantiations: embedded in IVP_Hull_Manager at +0x20, and
// IVP_Sim_Unit_Manager::EventPool (ivp_environment.h), standalone, `operator new(0x14)` +
// FUN_06362ef0(p, 0x10) at .text:063608e2-06360912. The standalone one has the same layout:
//   * :06360a13 `CMP word ptr [ECX+0x10],0x0`          -> Count   at pool+0x10
//   * :06360a1a/:06360a1f/:06360a24/:06360a27
//       `EDX=[ECX+4]; AX=[ECX+0xe]; SHL EAX,4; ESI=[EAX+EDX+0xc]`
//                                                      -> Entries at pool+0x04,
//                                                         LiveIndex at pool+0x0e,
//                                                         entry stride 0x10,
//                                                         Entry::Payload at +0x0c
//   * :06360b68/:06360b74-:06360b8b (the PSI rebase in LAB_06360ad0)
//       `CX=[EDI+0xe]` then per entry `EAX=[EDI+4]+idx*0x10; CX=[EAX+4];
//        [EAX+8] -= elapsed`                           -> Entry::Next at +0x04,
//                                                         Entry::Key  at +0x08
//     and finally :06360b96-:06360b9e `EAX=[EDX+8]; [EAX+8] -= elapsed`
//                                                      -> KeyThreshold at pool+0x08
// matching IVP_Hull_Manager's +0x20-relative map (hull+0x24 -> pool+0x04, ...).
//
// Member names keep the `Pool...` spelling that IVP_Hull_Manager and
// src/Common/common_physys.cpp use.
// ============================================================================
class IVP_Priority_Pool
{
public:
    // Per-entry shape, from Insert (FUN_06362f90, 247 instructions) and Remove
    // (FUN_06363280, 62 instructions). Two intrusive doubly-linked lists run through
    // every entry:
    //   * the ASCENDING-KEY list at +0x4 (next) / +0x6 (prev), headed by
    //     PoolLiveIndex (+0x0e). Insert's fast path writes `new->+0x4 =
    //     PoolLiveIndex; Entries[PoolLiveIndex].+0x6 = new` (:063630b3/
    //     :063630ce) and Remove unlinks through exactly that pair
    //     (:06363294 reads +0x6 as prev, :0636329a reads +0x4 as next,
    //     :063632b0/:063632bd write them back).
    //   * an EXPRESS ("skip") list at +0x0 (next) / +0x2 (prev), headed by
    //     PoolSortedHead (+0x0c), used only to shortcut the ascending walk.
    //     Insert seeds every fresh entry's +0x0 with 0xfffe (:06363099) as the
    //     "not a member of the express list" tag, promotes a node into it when
    //     the ascending walk took more hops than the express walk saved
    //     (:063631a1-:06363271), and Remove skips the express unlink entirely
    //     when +0x0 still reads 0xfffe (:063632f1).
    struct Entry
    {
        uint16_t ExpressNext; // +0x0: express-list next, or 0xfffe "not in the express list" (06363099 sets it, 063631e1 / 063632f1 test it).
        uint16_t ExpressPrev; // +0x2: express-list prev; 0xffff == the express head (0636324e writes, 06363301 / 0636331b read).
        uint16_t Next;        // +0x4: key-list next in use, free-list next while free (06362f28 / 06362fba free list, 063630b3 / 0636319d key list, 06363335 Remove).
        uint16_t Prev;        // +0x6: key-list prev; 0xffff == this entry is PoolLiveIndex (063630da / 06363199 / 06363238 write, 06363294 reads).
        float Key;            // +0x8: Insert's `param_2`, stored as a raw float (0636308b / 06363093).
        void* Payload;        // +0xc: Insert's `param_1` (06363096) -- a vtable-bearing listener object.
    };
    static_assert(sizeof(Entry) == 0x10, "IVP_Priority_Pool::Entry size");

    uint16_t PoolCapacity = 0;      // +0x00: 06362ef7.
    uint16_t PoolFreeHead = 0xffff; // +0x02: 0xffff == free list empty; 06362eff (ctor) / 06362fa2 (Insert check).
    Entry* PoolEntries = nullptr;   // +0x04: 06362f0f (ctor alloc).
    float PoolMinKey = 0.0f;        // +0x08: the smallest key; 1e10f (0x501502f9, 06362f59) when empty. Insert takes Key <= it as the new minimum (0636309e).
    uint16_t PoolSortedHead = 0xffff; // +0x0c: 0xffff == empty; 06362f4d.
    uint16_t PoolLiveIndex = 0xffff;  // +0x0e: 0xffff == empty; 06362f53.
    uint16_t PoolCount = 0;           // +0x10: incremented by Insert (06362f98).
    uint16_t Field_0x12 = 0;          // +0x12: trailing bytes of the 0x14-byte allocation; never read by any pool function.

    IVP_Priority_Pool() {}
    // FUN_06362ef0. `InitialCapacity` is 8 from IVP_Hull_Manager's ctor (.text:0635217e),
    // 0x10 from IVP_SimUnitManager_Construct (.text:063608fb).
    explicit IVP_Priority_Pool(uint16_t InitialCapacity) { Init(InitialCapacity); }
    ~IVP_Priority_Pool() { Destroy(); }

    void Init(uint16_t InitialCapacity);
    // FUN_06362f70 -- frees Entries if non-null.
    void Destroy();

    // FUN_06362f90 / FUN_06363280. Insert returns the slot index, which callers store
    // to Remove it later.
    uint16_t Insert(void* Payload, float Key);
    void Remove(uint16_t Slot);
};
static_assert(sizeof(IVP_Priority_Pool) == 0x14, "IVP_Priority_Pool size must be 0x14 (operator new(0x14), FUN_063608a0)");
static_assert(offsetof(IVP_Priority_Pool, PoolCapacity) == 0x00, "IVP_Priority_Pool+0x00");
static_assert(offsetof(IVP_Priority_Pool, PoolFreeHead) == 0x02, "IVP_Priority_Pool+0x02");
static_assert(offsetof(IVP_Priority_Pool, PoolEntries) == 0x04, "IVP_Priority_Pool+0x04");
static_assert(offsetof(IVP_Priority_Pool, PoolMinKey) == 0x08, "IVP_Priority_Pool+0x08");
static_assert(offsetof(IVP_Priority_Pool, PoolSortedHead) == 0x0c, "IVP_Priority_Pool+0x0c");
static_assert(offsetof(IVP_Priority_Pool, PoolLiveIndex) == 0x0e, "IVP_Priority_Pool+0x0e");
static_assert(offsetof(IVP_Priority_Pool, PoolCount) == 0x10, "IVP_Priority_Pool+0x10");

// ============================================================================
// IVP_Hull_Manager (FUN_06352150 ctor / FUN_063521b0 dtor / FUN_063521f0 /
// FUN_06352220). Embedded by value in IVP_Real_Object at +0x44 (the base ctor
// FUN_06345100 calls FUN_06352150 on `this+0x44`, .text:06345140), 0x34 bytes.
//
// Embeds one IVP_Priority_Pool at +0x20 (the ctor / dtor call FUN_06362ef0 /
// FUN_06362f70). IVP_Complex_Mindist::PriorityPool (ivp_mindist_limiter.cpp) is a
// clean-room stand-in for the same allocator with a different entry shape.
// TODO: unify it with IVP_Priority_Pool.
class IVP_Hull_Manager
{
public:
    // Alias for the `IVP_Hull_Manager::Entry` spelling. Entries read by this class's own
    // WalkActiveEntries/DrainAndShift:
    // Entry::Next at +0x4 (:0635225d/:06352277), Entry::Key at +0x8
    // (06352263-0635226f), Entry::Payload at +0xc, dispatched through vtable
    // slots 2/3 (0635220d/06352274).
    typedef IVP_Priority_Pool::Entry Entry;

    // +0x00..+0x17: zeroed by the ctor (.text:06352170-06352196) and not read by this
    // class's own functions. FUN_06350050 (.text:06350080-063500c7, mirrored at
    // 063500e8-06350123)
    // reads all six of them off an IVP_Real_Object* at the object-relative
    // offsets +0x44/+0x48/+0x4c/+0x50/+0x54/+0x58 -- i.e. exactly this embedded
    // member's +0x00/+0x04/+0x08/+0x0c/+0x10/+0x14 -- and evaluates, twice per
    // side:
    //     dt          = (Env->current_time.hi - this->LastEditTimeHi)
    //                 + (Env->current_time.lo - this->LastEditTimeLo)
    //     outer(now)  = dt * OuterGradient + OuterValueAtLastEdit
    //     spread(now) = dt * (OuterGradient - InnerGradient)
    //                 + (OuterValueAtLastEdit - InnerValueAtLastEdit)
    // Every one of those operands is reached by an x87 FSUB/FMUL/FADD on the
    // memory operand itself (`FSUB [ESI+0x44]`, `FSUB [ESI+0x48]`,
    // `FMUL [ESI+0x4c]`, `FSUB [ESI+0x50]`, `FADD [ESI+0x54]`,
    // `FSUB [ESI+0x58]`), so all six are floats: a linearly extrapolated
    // outer/inner value pair with a shared split-precision timestamp.
    float LastEditTimeHi = 0.0f;  // +0x00: high half of the split-precision "value was last edited at" stamp.
    float LastEditTimeLo = 0.0f;  // +0x04: low half. Differenced against IVP_Environment::current_time's own pair.
    float OuterGradient  = 0.0f;  // +0x08: d(outer value)/dt since LastEditTime.
    float InnerGradient  = 0.0f;  // +0x0c: d(inner value)/dt since LastEditTime.

    // +0x10/+0x14: the outer/inner values sampled AT LastEditTime, which the
    // gradients above extrapolate forward:
    //   1. FUN_06350050 (above) adds +0x10 after `dt * OuterGradient` and
    //      differences +0x10 against +0x14 -- an absolute base value, not a
    //      delta.
    //   2. IVP_Real_Object::init_core_rest_state_and_damping's per-object hull edit (ivp_core.cpp,
    //      .text:06351da2-06351dc9) zeroes both gradients and then does
    //      `OuterValueAtLastEdit = AccumulatedDeltaA + ExtraMargin` (a plain
    //      assignment of a running absolute total) and
    //      `InnerValueAtLastEdit += DistanceMoved` (the inner hull being eaten
    //      by motion). With both gradients zeroed the extrapolation degenerates
    //      to exactly these two values, which is what (1) then reads back.
    //   3. DrainAndShift below (FUN_06352220) negates both, folds -Outer into
    //      every live entry's Key and into PoolMinKey/AccumulatedDeltaA
    //      (.text:06352263-06352292), hands both negated values to each
    //      entry's vtable-slot-3 callback (06352265/06352274, push order
    //      EBX=-Outer then EBP=-Inner) and then zeroes both: a rebase of the
    //      hull-value coordinate system to zero.
    // Names are the port's.
    float OuterValueAtLastEdit = 0.0f; // +0x10
    float InnerValueAtLastEdit = 0.0f; // +0x14
    // +0x18: running total of every DrainAndShift rebase's OuterValueAtLastEdit
    // (only the outer value: .text:0635228b-06352292 has no inner counterpart).
    float AccumulatedDeltaA = 0.0f; // +0x18
    // +0x1c: integer time of the next rebase. Read by
    // FILD dword and compared with LastEditTimeHi; when behind, DrainAndShift
    // runs and this becomes ftol(LastEditTimeHi + 10.0f)
    // (hull check inlined in 06351b90 / 06351f80; golem controller :06343f31-:06343f53).
    int32_t TimeOfNextReset = 0;    // +0x1c

    // +0x20..+0x33: the embedded pool (initial capacity 8, see ctor).
    uint16_t PoolCapacity = 0;         // +0x20: 06362ef7.
    uint16_t PoolFreeHead = 0xffff;     // +0x22: 0xffff == free list empty; 06362eff (ctor) / 06362fa2 (Insert check).
    Entry* PoolEntries = nullptr;        // +0x24: 06362f0f (ctor alloc); read as `this+0x24` by WalkActiveEntries / DrainAndShift.
    float PoolMinKey = 0.0f;        // +0x28: the smallest key; 1e10f (0x501502f9, 06362f59) when empty. Insert takes Key <= it as the new minimum (0636309e); DrainAndShift shifts it (06352279-06352288).
    uint16_t PoolSortedHead = 0xffff;      // +0x2c: 0xffff == empty; 06362f4d (ctor).
    uint16_t PoolLiveIndex = 0xffff;        // +0x2e: 0xffff == empty; 06362f53 (ctor); the iteration cursor of WalkActiveEntries / DrainAndShift.
    uint16_t PoolCount = 0;                  // +0x30: incremented by Insert (06362f98); the loop guard of WalkActiveEntries / DrainAndShift.
    uint16_t Field_0x32 = 0;                  // +0x32: trailing bytes, never read by this class.

    // The seven fields above are an IVP_Priority_Pool (retail calls the pool functions on
    // `this+0x20`, .text:0635217e ctor, 063521d8 dtor). They stay spelled out because
    // common_physys.cpp reads Hull.PoolEntries / PoolLiveIndex / PoolMinKey by name;
    // Pool() is the bridge and the static_asserts below pin the aliasing.
    // TODO: replace them with an `IVP_Priority_Pool Pool;` member.
    IVP_Priority_Pool& Pool() { return *reinterpret_cast<IVP_Priority_Pool*>(&PoolCapacity); }
    const IVP_Priority_Pool& Pool() const { return *reinterpret_cast<const IVP_Priority_Pool*>(&PoolCapacity); }

    // FUN_06352150.
    IVP_Hull_Manager();
    // FUN_063521b0.
    ~IVP_Hull_Manager();

    // FUN_063521f0. While
    // PoolCount > 0, repeatedly RE-READS PoolLiveIndex (never advanced by
    // this function's own body) and dispatches
    // `Entries[PoolLiveIndex].Payload->vtable[2](this)` -- the listener's
    // own handler is expected to mutate PoolLiveIndex/PoolCount as a side
    // effect of processing itself (e.g. removing itself from the pool);
    // this function would spin forever on a listener that does not. Name is
    // this port's own.
    void WalkActiveEntries();

    // FUN_06352220. See the Outer/InnerValueAtLastEdit field comments above. Name is
    // the port's.
    void DrainAndShift();

    // FUN_06362f90 / FUN_06363280 on this pool (forwarders to IVP_Priority_Pool).
    // PoolInsert returns the slot index callers keep for PoolRemove (e.g.
    // IVP_OV_Element::HullPoolSlot).
    uint16_t PoolInsert(void* Payload, float Key) { return Pool().Insert(Payload, Key); }
    void PoolRemove(uint16_t Slot) { Pool().Remove(Slot); }
};
static_assert(sizeof(IVP_Hull_Manager) == 0x34, "IVP_Hull_Manager size must be 0x34 (52 bytes)");
// The embedded-pool aliasing contract: every IVP_Priority_Pool field must land
// on the identically-named IVP_Hull_Manager field, offset by exactly 0x20.
static_assert(offsetof(IVP_Hull_Manager, PoolCapacity)     == 0x20 + offsetof(IVP_Priority_Pool, PoolCapacity),     "hull/pool alias: PoolCapacity");
static_assert(offsetof(IVP_Hull_Manager, PoolFreeHead)     == 0x20 + offsetof(IVP_Priority_Pool, PoolFreeHead),     "hull/pool alias: PoolFreeHead");
static_assert(offsetof(IVP_Hull_Manager, PoolEntries)      == 0x20 + offsetof(IVP_Priority_Pool, PoolEntries),      "hull/pool alias: PoolEntries");
static_assert(offsetof(IVP_Hull_Manager, PoolMinKey) == 0x20 + offsetof(IVP_Priority_Pool, PoolMinKey), "hull/pool alias: PoolMinKey");
static_assert(offsetof(IVP_Hull_Manager, PoolSortedHead)   == 0x20 + offsetof(IVP_Priority_Pool, PoolSortedHead),   "hull/pool alias: PoolSortedHead");
static_assert(offsetof(IVP_Hull_Manager, PoolLiveIndex)    == 0x20 + offsetof(IVP_Priority_Pool, PoolLiveIndex),    "hull/pool alias: PoolLiveIndex");
static_assert(offsetof(IVP_Hull_Manager, PoolCount)        == 0x20 + offsetof(IVP_Priority_Pool, PoolCount),        "hull/pool alias: PoolCount");
static_assert(offsetof(IVP_Hull_Manager, LastEditTimeHi) == 0x00, "IVP_Hull_Manager+0x00");
static_assert(offsetof(IVP_Hull_Manager, LastEditTimeLo) == 0x04, "IVP_Hull_Manager+0x04");
static_assert(offsetof(IVP_Hull_Manager, OuterGradient) == 0x08, "IVP_Hull_Manager+0x08");
static_assert(offsetof(IVP_Hull_Manager, InnerGradient) == 0x0c, "IVP_Hull_Manager+0x0c");
static_assert(offsetof(IVP_Hull_Manager, OuterValueAtLastEdit) == 0x10, "IVP_Hull_Manager+0x10");
static_assert(offsetof(IVP_Hull_Manager, InnerValueAtLastEdit) == 0x14, "IVP_Hull_Manager+0x14");
static_assert(offsetof(IVP_Hull_Manager, AccumulatedDeltaA) == 0x18, "IVP_Hull_Manager+0x18");
static_assert(offsetof(IVP_Hull_Manager, PoolCapacity) == 0x20, "IVP_Hull_Manager+0x20");
static_assert(offsetof(IVP_Hull_Manager, PoolEntries) == 0x24, "IVP_Hull_Manager+0x24");
static_assert(offsetof(IVP_Hull_Manager, PoolMinKey) == 0x28, "IVP_Hull_Manager+0x28");
static_assert(offsetof(IVP_Hull_Manager, PoolLiveIndex) == 0x2e, "IVP_Hull_Manager+0x2e");
static_assert(offsetof(IVP_Hull_Manager, PoolCount) == 0x30, "IVP_Hull_Manager+0x30");

// One narrow-phase cache slot (public because the ray solver reads it: 0634a600 reads
// slot->WorldTransform.vv at +0x4c). +0x0/+0x4/+0x8 from FUN_0634d530 / FUN_06353510;
// +0xc on is written by the refresh step FUN_0634d640 (IVP_NarrowPhaseCacheSlot::refresh).
struct IVP_NarrowPhaseCacheSlot
{
    int32_t          LastRefreshTick; // +0x0: IVP_Environment::sim_step_count, stamped by FUN_0634d640 (`MOV [EDI],EAX` at .text:0634d669).
    int32_t          RefCount;        // +0x4: 0 == free (FUN_0634d530's claim scan); bumped by FUN_06353510 / ResolveNarrowPhaseCacheSlot, decremented by IVP_Real_Object::ReleaseNarrowPhaseCacheSlot.
    IVP_Real_Object* Owner;           // +0x8: back-pointer to the cached object.
    // +0xc..+0x1b: an IVP_U_Quat -- this slot's cached/interpolated orientation,
    // written by IVP_NarrowPhaseCacheSlot::refresh below (either copied
    // verbatim from Core->QWorldFCore when DTime==0, or slerped from
    // Core->QWorldFCore/QWorldFCoreNext otherwise) and then immediately
    // consumed by set_matrix() to build WorldTransform's rotation rows below.
    IVP_U_Quat       Quat;              // +0xc..+0x1b (16 bytes)
    // +0x1c..+0x5b: the slot's cached world transform (an embedded
    // IVP_U_Matrix: rows at +0x1c / +0x2c / +0x3c, vv at +0x4c). Built by
    // IVP_NarrowPhaseCacheSlot::refresh below from Quat; read through the slot
    // transforms 0634d860 / 0634d8d0 / 0634d970 / 0634d9d0 by the
    // IVP_Compact_Ledge_Solver helpers (e.g. calc_pos_other_space 0635ae60,
    // which reaches it as IVP_Cache_Ledge_Point::cache_object + 0x1c) and by
    // the IVP_Mindist_Minimize_Solver walks; minimize_BB (063536d0) and the
    // ball walks read vv directly as the ball centre.
    IVP_U_Matrix     WorldTransform;   // +0x1c..+0x5b (64 bytes): rows[0..2] @ +0x1c/+0x2c/+0x3c, vv (translation) @ +0x4c.
    // +0x5c/+0x60/+0x64: a second 3-float vector. Every minimize walk dots
    // (this side's Secondary - the other side's) with the new
    // IVP_Ledge_Mindist::SeparatingNormal and stores the result in
    // IVP_Ledge_Mindist::SecondaryNormalDot (e.g. 063536d0 :063537fe,
    // 06363620 :06363774). No retail name recovered.
    Vector           Secondary;        // +0x5c..+0x67 (12 bytes)
    char             _pad68[0x70 - 0x68]; // +0x68..+0x6f: unknown.

    // Slot transforms (ECX = slot, RET 8): retail's out-of-line copies of the
    // IVP_U_Matrix operations on the slot's WorldTransform (+0x1c); each body is the
    // same x87 sequence as the matrix method it forwards to. See ivp_real_object.cpp.
    // FUN_0634d860 == vimult4 (06348470): world position -> slot space.
    void transform_world_to_slot_space(const IVP_U_Point* WorldPos, IVP_U_Point* OutLocalPos);

    // FUN_0634d9d0 == vmult (06348580): slot direction -> world.
    void rotate_slot_to_world_space(const IVP_U_Point* LocalDir, IVP_U_Point* OutWorldDir);

    // FUN_0634d970 == vimult (06366010): world direction -> slot space.
    void rotate_world_to_slot_space(const IVP_U_Point* WorldDir, IVP_U_Point* OutLocalDir);

    // FUN_0634d8d0 == vmult4 (063484e0): slot position -> world (called by
    // give_world_coords_AT 0635bda0).
    void transform_slot_to_world_space(const IVP_U_Point* LocalPos, IVP_U_Point* OutWorldPos);

    // Defined in ivp_real_object.cpp.
    void refresh();
};
static_assert(sizeof(IVP_NarrowPhaseCacheSlot) == 0x70, "IVP_NarrowPhaseCacheSlot size mismatch");
static_assert(offsetof(IVP_NarrowPhaseCacheSlot, WorldTransform) == 0x1c, "WorldTransform offset mismatch");
static_assert(offsetof(IVP_NarrowPhaseCacheSlot, Secondary) == 0x5c, "Secondary offset mismatch");

// ============================================================================
// IVP_RealObjectConstructionTemplate -- port-local name (no retail source) for
// the "CreateParms"-shaped struct pointer threaded through IVP_Environment::
// static_object's construction chain: FUN_0634cdf0 -> FUN_06362570 -> both
// FUN_06345100 (the constructor below; reads +0x00/+0x0c/+0x10/+0x18/+0x54) and, as a
// sibling call with the same pointer, FUN_06345b20 (reads +0x1c..+0x34 and
// +0x38/+0x3c/+0x40/+0x44). +0x14 and +0x30 are read by neither.
//
// +0x00 / +0x04: FUN_06345100 passes the template itself (`PUSH EBP`, .text:06345124) to
// IVP_Object::IVP_Object, which reads the name pointer at +0; the 8-byte short name at +4
// is copied separately (`LEA EDX,[EBP+0x4]`, .text:06345179) and ends at FlagsHi.
struct IVP_RealObjectConstructionTemplate
{
    // +0x00: a "long" display-name pointer. Read unconditionally and strdup'd
    // into IVP_Object::object_name (+0x14) by IVP_Object(Father, Template)
    // (.text:0634562f-0634563e); 06356920 returns null for a null string.
    char* LongNamePtr;

    // +0x04..+0x0b: an embedded 8-byte "short" name/tag buffer, copied
    // (raw bytes, via strncpy, not a further pointer indirection) into
    // IVP_Real_Object::name[8] (.text:06345179-063451b1).
    char ShortNameTag[8];

    uint32_t FlagsHi; // +0x0c: -> IVP_Core::IVP_Core's FlagsHi; zero/nonzero also picks IVP_Real_Object::flags' (+0x78) initial 0x10-vs-0x8 bit (.text:063451fd-06345224).
    uint32_t FlagsLo; // +0x10: -> IVP_Core::IVP_Core's FlagsLo (.text:063451d1).

    uint8_t _unknown_0x14[0x18 - 0x14]; // +0x14: unread by both consumers.

    IVP_Material* Material; // +0x18: -> IVP_Real_Object::material (+0x8C), .text:06345227-0634522a.

    // +0x1c..+0x34: RotInertia / Mass inputs of IVP_Real_Object::init_core_rest_state_and_damping
    // (FUN_06345b20, ivp_real_object.cpp). Its `[ESP+0x1c]` shape selector is
    // Self->object_type, stashed at entry (`MOV [ESP+0x1c],EBP`, .text:06345b6e) because
    // EBP later points at &Core->RotInertia (.text:06345c67).
    float MassInput; // +0x1c: clamped to at least 1.0f (`(MassInput < DAT_063a545c) ? 1.0f : MassInput`, DAT_063a545c a small epsilon ~0x3b449ba6) to give IVP_Core::Mass.

    // +0x20: the SDK's auto_check_rot_inertia flag (the template ctor FUN_063486c0 stores
    // 1). Zero -> RotInertia is InertiaBoxVector unscaled; nonzero -> a switch on
    // Self->object_type (2 box, 3 sphere) computes a scaled RotInertia.
    int32_t AutoCheckRotInertia; // +0x20

    // +0x24..+0x2c: a per-axis "shape factor" Vector, componentwise-multiplied
    // into RotInertia in BOTH the object_type==2 ("box", via
    // Self->surface_manager->vtable[3]) and object_type==3 ("sphere", via a
    // literal 0.4f == 2/5 solid-sphere coefficient, DAT_063a5458) sub-cases,
    // and copied through unscaled when AutoCheckRotInertia is 0.
    Vector InertiaBoxVector; // +0x24

    uint8_t _unknown_0x30[0x34 - 0x30]; // +0x30: not read by FUN_06345b20.

    // +0x34: RotInertia post-clamp factor. 0.0f (DAT_063a53d4) disables the clamp.
    // Otherwise `Threshold = |Core->RotInertia| * RotInertiaClampFactor`
    // (VectorLength, FUN_06347e60), then each of RotInertia's 3
    // components is raised to Threshold if it is currently smaller --
    // a "don't let any single axis' inertia get too small relative to the
    // tensor's own overall magnitude" stability floor.
    float RotInertiaClampFactor; // +0x34

    // +0x38..+0x44: damping inputs, consumed by FUN_06345b20, not by the object ctor.
    float SpeedDampInput;     // +0x38
    float RotSpeedDampInputX; // +0x3c
    float RotSpeedDampInputY; // +0x40
    float RotSpeedDampInputZ; // +0x44

    uint8_t _unknown_0x48[0x4c - 0x48];

    // +0x4c: the library's extra_radius, copied to IVP_Real_Object::extra_radius (an IVP_Ball
    // adds its radius, FUN_06362570 `FLD [EDI+0x4c]` at 063625bf).
    float ExtraRadius; // +0x4c

    // +0x50: an optional center-of-mass override. FUN_06345b20 tests it
    // (.text:06345b46-06345b72) while building the LocalCsys for Self->vtable[2]
    // (LAB_06344de0, ApplyLocalCsys): when non-null its +0x30/+0x34/+0x38 floats become
    // LocalCsys.vv; when null surface_manager->vtable[1] computes it. Pointee type unknown.
    void* OptionalComOverride; // +0x50

    void* ClientData; // +0x54: -> IVP_Real_Object::client_data (+0xA4), .text:0634523c-06345240.

    // The environment ctor builds one at 0634c5ff with FUN_063486c0 and passes it to
    // FUN_0634cdf0.

    // FUN_063486c0: `REP STOSD` 0x16 dwords of zero, then +0x14 = 0 again,
    // +0x1c = 1.0f, +0x20 = 1, +0x24/+0x28/+0x2c = 1.0f, +0x34 = 0x3cf5c28f,
    // +0x38..+0x44 = 0x3c23d70a. See ivp_real_object.cpp.
    IVP_RealObjectConstructionTemplate();
    // FUN_06348670 (also reached through thunk 06348710): frees LongNamePtr.
    ~IVP_RealObjectConstructionTemplate();
    IVP_RealObjectConstructionTemplate(const IVP_RealObjectConstructionTemplate&) = delete;
    IVP_RealObjectConstructionTemplate& operator=(const IVP_RealObjectConstructionTemplate&) = delete;

    // FUN_06348720: strncpy(ShortNameTag, Name, 8), null Name clears byte 0.
    void set_short_name(const char* Name);
    // FUN_06348690: free(LongNamePtr), then LongNamePtr = _strdup(Name).
    void set_long_name(const char* Name);
};
static_assert(sizeof(IVP_RealObjectConstructionTemplate) == 0x58, "FUN_063486c0 zero-fills 0x16 dwords");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, ShortNameTag) == 0x04, "IVP_RealObjectConstructionTemplate+0x04");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, FlagsHi) == 0x0c, "IVP_RealObjectConstructionTemplate+0x0c");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, FlagsLo) == 0x10, "IVP_RealObjectConstructionTemplate+0x10");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, Material) == 0x18, "IVP_RealObjectConstructionTemplate+0x18");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, MassInput) == 0x1c, "IVP_RealObjectConstructionTemplate+0x1c");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, AutoCheckRotInertia) == 0x20, "IVP_RealObjectConstructionTemplate+0x20");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, InertiaBoxVector) == 0x24, "IVP_RealObjectConstructionTemplate+0x24");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, RotInertiaClampFactor) == 0x34, "IVP_RealObjectConstructionTemplate+0x34");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, SpeedDampInput) == 0x38, "IVP_RealObjectConstructionTemplate+0x38");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, ExtraRadius) == 0x4c, "IVP_RealObjectConstructionTemplate+0x4c");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, OptionalComOverride) == 0x50, "IVP_RealObjectConstructionTemplate+0x50");
static_assert(offsetof(IVP_RealObjectConstructionTemplate, ClientData) == 0x54, "IVP_RealObjectConstructionTemplate+0x54");

// ============================================================================
// The IVP object hierarchy.
//
//   IVP_Object                   vtbl 063a5450 {06345650 deleting dtor}, 0x1c
//   +- IVP_Cluster               vtbl 063a5454 {06345740 deleting dtor}, 0x20
//   +- IVP_Real_Object_Fast_Static  vtbl 063a544c {06345260}
//      +- IVP_Real_Object        vtbl 063a5440 {063452e0 deleting dtor,
//                                               06344f10, 06344de0}, 0xa8
//         +- IVP_Polygon         vtbl 063a5824 {06362320, 06344f10, 06344de0}
//         +- IVP_Ball            vtbl 063a585c {06362620, 06344f10, 06344de0}
//
// Evidence: 06345100 calls 06345610 (IVP_Object ctor) on `this`, then writes
// 063a544c, constructs the hull manager at +0x44 (06352150, EH state 0 -> 1),
// then writes 063a5440; the EH unwind funclets of that ctor call the
// 06345280 thunk (~IVP_Object) for state 0 and 06345290 (~hull manager +
// ~IVP_Object, i.e. ~IVP_Real_Object_Fast_Static) for state 1. 06345760 (the
// only caller of 06345670) builds the cluster: IVP_Object(env), vtbl
// 063a5454, +0x1c = 0, +0x04 = 1.
// Class names are the IVP SDK's; no RTTI COL survives in Common.dll.
// ============================================================================
class IVP_Cluster;

// IVP_Object::object_type: the retail constructors store 1 (IVP_Cluster, 06345760), 2
// (IVP_Polygon, 06362280) and 3 (IVP_Ball, 06362570); the switches on it trap on anything else.
enum IVP_OBJECT_TYPE
{
    IVP_CLUSTER = 1,
    IVP_POLYGON = 2,
    IVP_BALL = 3,
};

class IVP_Object
{
public:
    // FUN_06345610 (.text:06345610-06345645, RET 8): vptr 063a5450,
    // init_object_core(Father->environment), Father->add_object(this), then
    // object_name = strdup(*(char**)Template) (06356920 returns 0 for a null
    // string). No null test on Father or Template.
    IVP_Object(IVP_Cluster* Father, const IVP_RealObjectConstructionTemplate* Template);

    // FUN_06345670 (.text:06345670-06345686, RET 4): vptr 063a5450,
    // init_object_core(Environment). Only caller: IVP_Cluster's ctor 06345760.
    explicit IVP_Object(IVP_Environment* Environment);

    // FUN_063456b0 (.text:063456b0-063456e5); the deleting dtor in slot 0 is
    // FUN_06345650. Unlinks from the father cluster when there is one
    // (06345820), frees the name (06343fb0) and nulls it, nulls environment.
    virtual ~IVP_Object();

    // FUN_06345690 (.text:06345690-063456a8, RET 4): zeroes +0x10, +0x0c,
    // +0x08, +0x14, +0x04 and stores Environment at +0x18.
    void init_object_core(IVP_Environment* Environment);

    int32_t          object_type;  // +0x04 IVP_OBJECT_TYPE
    IVP_Object*      next_object;  // +0x08 next_in_cluster (toward the oldest child)
    IVP_Object*      prev_object;  // +0x0C prev_in_cluster (toward IVP_Cluster::first_object)
    IVP_Cluster*     parent;       // +0x10 father_cluster; null for the root cluster
    char*            object_name;  // +0x14 strdup'd (06356920), freed by the dtor
    IVP_Environment* environment;  // +0x18
};
static_assert(sizeof(IVP_Object) == 0x1c, "IVP_Object is 0x1c bytes");

class IVP_Cluster : public IVP_Object
{
public:
    // FUN_06345760 (.text:06345760-06345784, RET 4). The only caller is the
    // cluster manager's ctor 0634ad70 (the root cluster).
    explicit IVP_Cluster(IVP_Environment* Environment);

    // FUN_06345790 (.text:06345790-063457ed); deleting dtor FUN_06345740.
    // While first_object is set, calls its slot 0 with 1 (delete); every
    // child's ~IVP_Object unlinks itself, which advances first_object.
    ~IVP_Cluster() override;

    // FUN_063457f0 (.text:063457f0-06345811, RET 4): push Object at the head.
    void add_object(IVP_Object* Object);

    // FUN_06345820 (.text:06345820-06345846, RET 4): unlink Object. Does not
    // clear Object->parent.
    void remove_object(IVP_Object* Object);

    IVP_Object* first_object; // +0x1c
};
static_assert(sizeof(IVP_Cluster) == 0x20, "IVP_Cluster is 0x20 bytes (operator new(0x20), FUN_0634ad70)");

// The intermediate layer (vtbl 063a544c). Its ctor is inlined into 06345100
// (.text:06345131-06345140): IVP_Object(Father, Template), vptr, then the hull
// manager member (06352150). Its dtor is FUN_06345290 (~hull manager 063521b0,
// then ~IVP_Object 063456b0), reached only from the EH unwind funclets
// 06397abb / 06397b03. Its vtable's only slot, FUN_06345260, deletes after
// ~IVP_Object alone; no object of this exact type is ever created, so that
// slot is never dispatched.
class IVP_Real_Object_Fast_Static : public IVP_Object
{
public:
    IVP_Real_Object_Fast_Static(IVP_Cluster* Father, const IVP_RealObjectConstructionTemplate* Template);

    // +0x1c: the object's IVP_Controller_Phantom, or null (convert_to_phantom
    // 06345f10, deleted by ~IVP_Real_Object at 06345326-06345345).
    IVP_Controller_Phantom* controller_phantom; // +0x1C

    // +0x20: head of this object's intrusive list of attached synapses, threaded
    // through the synapse (an IVP_Ledge_Mindist's +0x18 or +0x34 slot), so the stored
    // pointer is mindist + 0x18 or + 0x34:
    //   * FUN_0634fec0 (.text:0634ff06-0634ff50) unlinks a mindist from BOTH of its slots and, on
    //     the "no prev" branch, writes the successor back to the object's own
    //     +0x20: `EAX=[ESI+0x28]; ECX=[ESI+0x20]; MOV [EAX+0x20],ECX`
    //     (:0634ff22-:0634ff28) for slot A, and the symmetric
    //     `EDX=[ESI+0x44]; EAX=[ESI+0x3c]; MOV [EDX+0x20],EAX`
    //     (:0634ff47-:0634ff4d) for slot B. ESI+0x28/+0x44 are ObjectA/ObjectB.
    //   * FUN_063454b0 (.text:063454b7-063454d4) walks it:
    //     `EAX=[ECX+0x20]` then per node `MOVSX ECX,word[EAX+0x18]; ADD ECX,EAX`
    //     (node + a self-relative int16 delta == the owning mindist) and
    //     `ESI=[EAX+0x8]` for the next node -- which independently pins Next at
    //     node+0x08, the same offset FUN_0634fec0 writes.
    IVP_Synapse_Real* synapse_list_head; // +0x20
    // +0x24: head of this object's list of synapses whose mindist is filed as
    // "invalid". FUN_0634e960 (.text:0634e98d-0634e9ca) pushes both synapses
    // of a mindist onto ObjectA/ObjectB's +0x24 with the same Next(+0x8)/
    // Prev(+0xc) threading as synapse_list_head.
    IVP_Synapse_Real* invalid_synapse_list_head; // +0x24
    void*               mindist_list;     // +0x28
    IVP_U_Quat*         q_core_f_object;  // +0x2C
    IVP_U_Point         shift_core_f_object; // +0x30 (16 bytes)
    // +0x40: the lazily claimed narrow-phase cache slot. Written by
    // IVP_AllocateCacheObject (FUN_0634d530, `MOV [EAX+0x40],ESI` at 0634d595), read by
    // ResolveNarrowPhaseCacheSlot (FUN_06353510), cleared by
    // IVP_Real_Object_Free_Cache_Object (0634d510).
    IVP_NarrowPhaseCacheSlot* cache_object; // +0x40
    IVP_Hull_Manager    hull_manager;     // +0x44 (0x34 bytes)
};

struct IVP_Anchor;

// Port consumer contract for anchor teardown; no native actuator hierarchy is claimed.
class IIVPAnchorEvents
{
public:
    virtual void AnchorWillBeDeleted(IVP_Anchor* Anchor) = 0;

protected:
    ~IIVPAnchorEvents() = default;
};

// One node of IVP_Real_Object::anchors (+0x7c): a point an actuator holds on
// the object. Layout read off ApplyLocalCsys (06344e0a-06344e28: next at
// +0x00, object-space point +0x0c transformed into the core-space point
// +0x1c) and the object's teardown (063455c7-063455e6 -> 06356910: the
// interface at +0x2c gets vtable slot 7 with the node). Name and layout match
// the IVP SDK's IVP_Anchor; +0x04/+0x08 are not read by any traced code.
struct IVP_Anchor
{
    IVP_Anchor* anchor_next_in_object; // +0x00
    uint32_t    Field_0x04;            // +0x04 not read by ApplyLocalCsys or the teardown
    uint32_t    Field_0x08;            // +0x08 not read by ApplyLocalCsys or the teardown
    IVP_U_Point object_pos;            // +0x0c
    IVP_U_Point core_pos;              // +0x1c
    IIVPAnchorEvents* l_actuator;       // +0x2c
};
static_assert(offsetof(IVP_Anchor, object_pos) == 0x0c, "IVP_Anchor+0x0c");
static_assert(offsetof(IVP_Anchor, core_pos) == 0x1c, "IVP_Anchor+0x1c");
static_assert(offsetof(IVP_Anchor, l_actuator) == 0x2c, "IVP_Anchor+0x2c");

// IVP_Real_Object (0xA8 / 168 bytes), vtbl 063a5440.
class IVP_Real_Object : public IVP_Real_Object_Fast_Static
{
public:
    // FUN_06345100 (.text:06345100-06345259, RET 0x14). Father is the cluster
    // the object is created into (IVP_Polygon / IVP_Ball pass it through from
    // IVP_Environment::create_polygon / CreateBall, the root cluster).
    IVP_Real_Object(IVP_Cluster* Father, IVP_SurfaceManager* SurfaceManager,
                    const IVP_RealObjectConstructionTemplate* Parms,
                    const IVP_U_Quat* InitialOrientationOrNull,
                    const IVP_U_Point* InitialPosition);

    // FUN_06345300 (.text:06345300-06345447); deleting dtor FUN_063452e0
    // (slot 0 of 063a5440).
    ~IVP_Real_Object() override;

    uint32_t            flags;            // +0x78 (IVP_OBJECT_LOCATION / IVP_MOVEMENT_STATE flags)
    IVP_Anchor*         anchors;          // +0x7C: this object's anchor list
    IVP_SurfaceManager* surface_manager;  // +0x80
    char                name[8];          // +0x84 (8-byte fixed string)
    IVP_Material*       material;         // +0x8C
    // +0x90: the object's IVP_OV_Element, its record in the environment's
    // spatial-subdivision (OV) tree:
    //   * FUN_0634f180 (.text:0634f19d-0634f1e9) reads this+0x90, calls
    //     its vtable slot 4 with arg 1 (the scalar deleting destructor, which
    //     is FUN_0635c070 at .rdata:063a5690), then `operator new(0x30)` and
    //     FUN_0635c020(newObj, this) -- so the allocation is exactly 0x30
    //     bytes and FUN_0635c020 is its constructor.
    //   * FUN_0635c020 (:0635c024-:0635c060) stores the OWNING object back
    //     into the new record's +0x24 and initialises the record's own
    //     0x10-slot IVP_U_Vector at +0x28.
    //   * FUN_0635c090 (the destructor) unlinks it from the
    //     owner's hull manager pool (:0635c0c8) and from the OV tree
    //     (:0635c0e8, FUN_0635d2e0 on Environment->+0x14).
    // See src/IVP/ivp_physics/ivp_ov_element.h for the full class.
    IVP_OV_Element*     ov_element;       // +0x90
    // +0x94: the library's extra_radius: a shell added around the surface.
    // Template->ExtraRadius, plus the radius for an IVP_Ball (FUN_06362570
    // `FLD [EDI+0x4c]; FADD [param2]; FSTP [this+0x94]`). FUN_06345b20 adds it to
    // the surface radius for IVP_Core::UpperLimitRadius; the mindist sums both
    // objects' values (CombinedRadius) and the contact frame pushes the point
    // out by it; the ray solver reads it as a ball's radius.
    float               extra_radius;     // +0x94
    IVP_Core*           core;             // +0x98 (Physical core)
    // +0x9C / +0xA0: the library's friction_core and original_core. All three
    // cores are set to the same core by the ctor and the split / merge paths;
    // the contact, friction and wake code reads +0x9C, and ~IVP_Real_Object
    // unlinks +0x98, then +0xA0 unless equal, then +0x9C unless equal to either
    // (Ipion's physical / original / friction order).
    IVP_Core*           friction_core;    // +0x9C
    IVP_Core*           original_core;    // +0xA0
    void*               client_data;      // +0xA4 (CObject* back-pointer in Freelancer)

    void get_m_world_f_object_at_transform(IVP_Time Time, IVP_U_Matrix* OutMatrix);
    // FUN_063456f0: convenience overload using environment's current simulation time.
    void get_m_world_f_object_AT(IVP_U_Matrix* OutMatrix);

    // FUN_063459c0: like get_m_world_f_object_at_transform, but returns orientation
    // as a separate quaternion and position as a separate point instead of a packed matrix.
    void calc_at_time(IVP_Time Time, IVP_U_Quat* OutQuat, IVP_U_Point* OutPosition);
    // FUN_06345710: convenience overload using environment's current simulation time.
    void calc_at_time_AT(IVP_U_Quat* OutQuat, IVP_U_Point* OutPosition);

    // FUN_063454e0: forces the core's movement-state byte to 0x21,
    // re-files this object's OV element (IVP_Mindist_Manager::RecheckOvElement
    // 0634ebd0), then restores the byte. Port-local name (behaviour); no OV
    // element test, unlike recheck_collision_filter 06345530. Body in
    // ivp_real_object.cpp.
    void recheck_ov_element_as_moving();

    // ---- Contact points, collision detection and phantoms (IVP_Controller_Phantom is in
    // ivp_phantom.h). ----

    // FUN_06345550 (.text:06345550-063455b8, RET 4), the SDK's
    // unlink_contact_points: removes
    // every contact-point record on this object's list (+0x28) from its
    // friction system, waking both cores first unless bSilently.
    void unlink_contact_points(bool bSilently);

    // FUN_06344d50 (.text:06344d50-06344db6, RET 4), the SDK's
    // enable_collision_detection: builds the OV element (mindist manager
    // 0634f180) and sets flag 0x100, or deletes it, clears 0x300 and unlinks
    // the contact points silently.
    void enable_collision_detection(bool bEnable);

    // FUN_06345f10 (.text:06345f10-06345f86, RET 4), the SDK's
    // convert_to_phantom: replaces any phantom in controller_phantom (+0x1c) by a
    // new IVP_Controller_Phantom(this, Template) and returns it.
    IVP_Controller_Phantom* convert_to_phantom(const IVP_Template_Phantom* Template);

    // FUN_06345eb0 (.text:06345eb0-06345f0a, plain RET, returns 1): turns a
    // phantom back into an ordinary object -- collision detection off, the
    // phantom deleted, movement byte 9 -> 0x10, collision detection back on
    // when it was enabled (flags bits 8-9 == 01) before. Port-local name; the
    // SDK name was not recovered.
    int32_t convert_phantom_to_object();

    // FUN_063454b0 (.text:063454b0-063454d8): recalc_exact_mindist (06350460)
    // of the environment's mindist manager on the mindist of every synapse
    // in synapse_list_head (+0x20); the next link is read before each call.
    void recalc_exact_mindists();

    // FUN_06345460 (.text:06345460-063454ac): for every synapse in
    // invalid_synapse_list_head (+0x24), the mindist is re-solved (06352b90)
    // and, unless StateFlags & 0xc000 == 0x4000, moved from the invalid to the
    // exact list (0634ffa0, then 0634e8a0).
    void recalc_invalid_mindists();

    // FUN_06344dc0 (.text:06344dc0-06344dd1): if the core's movement byte
    // (+0x60) is exactly 8, tail-jump to ReviveFrictionCoreIfSleeping (063450e0).
    void ensure_in_simulation();

    // FUN_06344fb0 (.text:06344fb0-063450cb, plain RET): put the object's core
    // to sleep now. Port-local name (SDK name not recovered). Called by PhySys
    // event_pre_collision (0628c566 / 0628c58c).
    void disable_simulation();

    // FUN_06344f50 (.text:06344f50-06344fa5): for every synapse in
    // synapse_list_head (+0x20) whose two objects' cores carry different
    // +0x1b8 stamps, re-run the mindist's DispatchNarrowPhaseSolve (06352cf0)
    // and, unless StateFlags & 0xc000, reschedule it (0634f320(m, 0, 2)).
    // Port-local name. Callers 06343c72 (core split) and 06347c02.
    void recheck_mindists_of_other_cores();

    // FUN_06345f90 (.text:06345f90-06346075, RET 0xc), the SDK's
    // beam_object_to_new_position: moves the core so that this object ends
    // up at RotationWs / PositionWs, via IVP_Calc_Next_PSI_Solver::
    // set_transformation (06351b90). Callers PhySys 06285047 / 0628b72a and
    // 0634313d.
    void beam_object_to_new_position(const IVP_U_Quat* RotationWs, const IVP_U_Point* PositionWs, int32_t bOptimizeForRepeatedCalls);

    // FUN_06346170 (.text:06346170-06346190): movement byte (+0x78) == 8 ->
    // environment->add_revive_core(friction_core) (0634bb90), else
    // core->update_time_of_last_motion() (063470e0).
    void EnsureInSimulation();

    // ---- PhySys-facing helpers (IVP SDK names where the SDK has the method). ----

    // FUN_06344d30 (.text:06344d30-06344d40, RET 4): core->set_mass (06347c10).
    // Caller PhySys::SetMass 062841fa.
    void change_mass(float NewMass);

    // FUN_06345530 (.text:06345530-0634554c, plain RET): with an OV element
    // (+0x90), core->Environment->mindist_manager->RecheckOvElement(this)
    // (0634ebd0). Caller PhySys::SetCollisionGroup 06284e42 (tail jump).
    void recheck_collision_filter();

    // FUN_06345e60 (.text:06345e60-06345e91, RET 4): *Out = core->MWorldFCore.vv
    // (x, y, z; Out->hesse untouched). Port-local name. Caller
    // PhySys::GetClosestPhantomIntruderPos 0628c0a5.
    void get_core_position_ws(IVP_U_Point* Out) const;

    // FUN_06345ea0 (.text:06345ea0-06345ea9, plain RET): core->UpperLimitRadius
    // (+0x04) returned in ST0. Callers PhySys_GetRadius 062843f6 and
    // PhySys_CalcClosestPointOnSynapse 0628bd4f.
    float get_upper_limit_radius() const;

    // FUN_06346080 (.text:06346080-063460b8, RET 4): EnsureInSimulation, then
    // core->SpeedChange (+0x84) += *SpeedWs. Callers PhySys::AddToVelocity
    // 06285767, 0636143f.
    void async_add_speed_object_ws(const IVP_U_Point* SpeedWs);

    // FUN_063460c0 (.text:063460c0-063460f6, RET 4): EnsureInSimulation, then
    // core->RotSpeedChange (+0x74) += *RotSpeedCs. Callers
    // PhySys::AddToAngularVelocityOS 06285797, 063610af, 06361119.
    void async_add_rot_speed_object_cs(const IVP_U_Point* RotSpeedCs);

    // FUN_06346100 (.text:06346100-0634616c, RET 8): EnsureInSimulation; the
    // core's interpolated matrix at environment->current_time (06346890) takes
    // PositionWs (vimult4, 06348470) and ImpulseWs (vimult, 06366010) into core
    // space; core->apply_impulse_at_point(PosCs, ImpulseCs, ImpulseWs)
    // (06346c00). Callers PhySys::AddImpulseAtPoint 06285674, 0636108b, 063610f5.
    void async_push_object_ws(const IVP_U_Point* PositionWs, const IVP_U_Point* ImpulseWs);

    // FUN_063450e0 (8 instructions; `this` is an IVP_Real_Object: flags +0x78,
    // friction_core +0x9C): if (flags & 0xFF) == 8, forward into friction_core's sim-unit
    // wake pass.
    void ReviveFrictionCoreIfSleeping();

    // FUN_06353510: lazily resolves this object's narrow-phase cache slot (cache_object),
    // bumps its refcount and conditionally refreshes it.
    struct IVP_NarrowPhaseCacheSlot* ResolveNarrowPhaseCacheSlot();

    // The library's get_cache_object_no_lock: the
    // same lazy resolve + conditional refresh as ResolveNarrowPhaseCacheSlot
    // WITHOUT the refcount bump. No own retail function: inlined twice in
    // FUN_0634e0b0 (.text:0634e18d-0634e1d1 / 0634e2a8-0634e2ec).
    struct IVP_NarrowPhaseCacheSlot* get_cache_object_no_lock();

    // The matching refcount decrement. Not a retail function: the three DAT_0658ab4c
    // narrow-phase handlers (FUN_06353370 / FUN_06353560 / FUN_063536d0) each end with
    // `DEC dword ptr [Slot+0x4]` on both sides' slots (e.g. .text:06353826 / 0635382c).
    void ReleaseNarrowPhaseCacheSlot(struct IVP_NarrowPhaseCacheSlot* Slot);

    // LAB_06344de0 (100 instructions), vtable slot 2, called unconditionally as
    // `Self->vtable[2](&LocalCsys)` by IVP_Real_Object::init_core_rest_state_and_damping. "Apply a
    // new LOCAL (object-to-core) coordinate system": recomputes
    // q_core_f_object/shift_core_f_object (this object's fixed offset from its
    // physical IVP_Core, e.g. a turret mounted off-center on a ship's core),
    // refreshes every cached mindist-list transform via the just-inverted
    // matrix, and invalidates the narrow-phase cache slot. NewLocalCsys is an
    // IVP_U_Matrix (not Freelancer's packed Csys); see ivp_real_object.cpp.
    // Declared below SetLocalCsysFromQuatAndPosition so the two virtuals take
    // slots 1 and 2 in retail's order (063a5444 = 06344f10, 063a5448 = 06344de0).

    // LAB_06344f10 (22 instructions), vtable slot 1: builds an IVP_U_Matrix from the
    // quaternion (IVP_U_Quat::set_matrix) and the translation, then calls slot 2.
    virtual void SetLocalCsysFromQuatAndPosition(const IVP_U_Quat* Orientation, const IVP_U_Point* Position); // slot 1
    virtual void ApplyLocalCsys(const IVP_U_Matrix* NewLocalCsys);                                         // slot 2

    // FUN_063455c0 (33 instructions): destructor phase 2, called only by ~IVP_Real_Object
    // (FUN_06345300) after the hull_manager WalkActiveEntries broadcast. Drains the anchors
    // list (+0x7c) through IVP_Anchor_ObjectDeletedEvent (each listener must unlink itself;
    // the port asserts on a stuck head), calls unlink_contact_points(true), then dispatches
    // vtable slot 4 on every node of synapse_list_head (+0x20), again self-unlinking.
    void TeardownMindistListenersAndCandidates();

    // FUN_06345b20 (.text:06345b20-06345e2f): thiscall on Self, stack (Environment, Template),
    // RET 8. The second phase of IVP_Real_Object construction -- rest state (RotSpeed/Speed),
    // damping, LocalCsys/ApplyLocalCsys, RotInertia/Mass and the DeltaTime/InvDeltaTime
    // world-transform snapshot. Runs after the constructor (Self->environment must be set).
    // Called by IVP_Ball::IVP_Ball and the IVP_Environment constructor (FUN_0634cdf0).
    void init_core_rest_state_and_damping(IVP_Environment* Environment,
                                          const IVP_RealObjectConstructionTemplate* Template);

    // FUN_06345e40: IVP_Real_Object method -- -1 when the object has no OV element
    // (+0x90), else the element's collision count (word at element+0x2a, the
    // n_elems of collision_fvector).
    int32_t get_collision_check_count() const;
};

// ============================================================================
// The two concrete IVP_Real_Object leaves. Each is a "second-half"
// constructor that CALLs the shared base constructor FUN_06345100 with the
// same five arguments, then installs its OWN 3-slot vtable at [this+0] and
// tags [this+4] (IVP_Object::object_type):
//
//   FUN_06362280  vtable 063a5824, type 2
//   FUN_06362570  vtable 063a585c, type 3
//
// Both vtables have 3 slots -- the same shape as IVP_Real_Object's own 063a5440 table, with
// only slot 0 (the scalar-deleting destructor) overridden and slots 1 and 2
// inherited verbatim:
//
//   .rdata:063a5824  IVP_Polygon  {FUN_06362320, LAB_06344f10, LAB_06344de0}
//   .rdata:063a585c  IVP_Ball     {FUN_06362620, LAB_06344f10, LAB_06344de0}
//   .rdata:063a5440  IVP_Real_Object {FUN_063452e0, LAB_06344f10, LAB_06344de0}
//
// "IVP_Polygon" and "IVP_Ball" are the upstream IVP/Ipion names for the
// object_type 2 / 3 pair; no retail symbol confirms them. Type 3 alone takes a
// radius and the default surface manager, consistent with sphere vs polyhedron.
//
// LAYOUT: neither adds a member. Their destructors are implicit: 06362320
// (Polygon slot 0) deletes after ~IVP_Real_Object (thunk to 06345300), and
// 06362620 (Ball slot 0) runs 06362640 (vptr 063a585c, then JMP 06345300)
// before deleting -- both are what the compiler generates for an empty
// derived destructor.
// ============================================================================

// FUN_06362280 (48 instructions). object_type == 2. Sole caller: 0634cdd5.
class IVP_Polygon : public IVP_Real_Object
{
public:
    // All five arguments are forwarded unchanged to FUN_06345100 (.text:063622b1-063622bc).
    IVP_Polygon(IVP_Cluster* Father, IVP_SurfaceManager* SurfaceManager,
                const IVP_RealObjectConstructionTemplate* Parms,
                const IVP_U_Quat* InitialOrientationOrNull,
                const IVP_U_Point* InitialPosition);
};
static_assert(sizeof(IVP_Polygon) == 0xA8, "IVP_Polygon must not change IVP_Real_Object's layout");

// FUN_06362570 (47 instructions). object_type == 3. Sole caller: 0634ce45.
class IVP_Ball : public IVP_Real_Object
{
public:
    // The second argument differs from IVP_Polygon's: retail passes 0 as the base ctor's
    // SurfaceManager (`PUSH 0x0`, .text:063625a0) and reads this parameter as a float,
    // extra_radius = Parms->ExtraRadius + *Radius (.text:063625ae-063625d1). The surface manager
    // is then set to the module's default instance (.text:063625c4).
    IVP_Ball(IVP_Cluster* Father, const float* Radius,
             const IVP_RealObjectConstructionTemplate* Parms,
             const IVP_U_Quat* InitialOrientationOrNull,
             const IVP_U_Point* InitialPosition);
};
static_assert(sizeof(IVP_Ball) == 0xA8, "IVP_Ball must not change IVP_Real_Object's layout");

// ---- Core merge / split, both cdecl free functions in retail. Port-local names. ----

// FUN_06343700 (.text:06343700-0634393c): weld Object into Host's core.
// Distance < 0 (or NaN) means "measure it": the distance between the two
// cores' positions extrapolated to Host's environment time. The merged core's
// UpperLimitRadius (+0x04) and +0x08 grow to Distance + the old core's values
// when larger; Object keeps its world pose through PhySys::
// BeamBodyToWorldPose (06343cc0). Callers: PhySys 06284aca / 06284b69 /
// 06284c4d / 06284d20 / 06288eb2 / 0628b5c7.
void IVP_Merge_Object_Into_Core(IVP_Real_Object* Host, IVP_Real_Object* Object, float Distance);

// FUN_06343940 (.text:06343940-06343cb3): give Object a fresh IVP_Core built
// from Template at its current world pose, carrying over the old core's
// rotation speed and the old core's surface speed at Object's position.
// Callers: PhySys 062848e2 / 0628b6b1.
void IVP_Split_Object_From_Core(IVP_Real_Object* Object, const IVP_RealObjectConstructionTemplate* Template);

// ============================================================================
// IVP_GetDefaultSurfaceManager() -- returns the module's one static
// IVP_SurfaceManager_Default instance, DAT_0658afe0.
//
// See ivp_surface_manager.h for the 11-slot tables of the abstract base (.rdata:063a56ac,
// slot 10 _purecall) and this default implementation (.rdata:063a5830; slot 10
// `MOV EAX,1; RET`, slots 7/8 the base's bare `RET 4` at 062ed730), and the instance's
// owned block (FUN_06343fc0(0x20, 0x10) in the ctor, FUN_06343ff0 in the dtor).
//
// Port-only accessor: retail has no getter and takes the instance's address directly.
// ============================================================================
IVP_SurfaceManager* IVP_GetDefaultSurfaceManager();

#pragma pack(pop)

static_assert(sizeof(IVP_Real_Object) == 0xA8, "IVP_Real_Object size mismatch");
