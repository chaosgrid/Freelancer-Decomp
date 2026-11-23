// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

#include "../ivp_utility/ivp_u_vector.h"
#include "../ivp_utility/ivp_vhash.h"   // IVP_OV_Tree_Hash's base
#include "../ivp_collision/ivp_collision.h" // IVP_Collision (collision_fvector element type)
#include "ivp_listener.h"                   // IVP_Listener_Hull (IVP_OV_Element's base)

// ============================================================================
// IVP_OV_Element / IVP_OV_Node / IVP_OV_Tree_Manager / IVP_Mindist_Manager
// ----------------------------------------------------------------------------
// The "object volume tree": each collidable IVP_Real_Object has an element
// with a centre and radius filed in one node of a level-indexed spatial
// subdivision, plus a vector of its pairwise collision records. Retail names
// are not recovered; the IVP_OV_* names are the port's.
// ============================================================================

class IVP_Real_Object;
class IVP_Environment;
class IVP_Hull_Manager;
class IVP_OV_Element;
class IVP_Ledge_Mindist; // src/IVP/ivp_physics/ivp_mindist.h

#pragma pack(push, 4)

// IVP_Collision (ivp_collision/ivp_collision.h) is the record type of
// IVP_OV_Element::collision_fvector. Slot 1 fills a 2-pointer array with the
// pair's objects, slot 4 is the deleting destructor, and +0xc / +0x10 are its
// index in each of the two elements' vectors.

// ----------------------------------------------------------------------------
// IVP_OV_Node: one cell of the subdivision, operator new(0x28) (0635d0cc),
// ctor FUN_0635c2c0 (also run on IVP_OV_Tree_Manager's embedded QueryCell).
//
//   * CellX/Y/Z are integer grid coordinates at the node's own level, and a
//     node covers two cells per axis: the box [C * S, (C + 2) * S] with
//     S == CellSizeTable[Level + 40] == 2^Level (FUN_0635d360, the overlap
//     test FUN_0635c8d0, and FUN_0635c9d0 splitting into children 2C, 2C+1,
//     2C+2).
//   * A bigger Level is a bigger cell (grow_root adds 1 and halves the
//     coordinates).
//   * LevelPlus1 is always Level + 1; it lets FUN_0635d360 index the table
//     twice without an add.
// ----------------------------------------------------------------------------
class IVP_OV_Node
{
public:
    // +0x00..+0x10: the five key dwords. They are copied from the manager's
    // QueryCell, hashed by FUN_06369200 and compared by LAB_06369240 (which
    // skips the redundant LevelPlus1).
    int32_t CellX;      // +0x00
    int32_t CellY;      // +0x04
    int32_t CellZ;      // +0x08
    int32_t Level;      // +0x0c
    int32_t LevelPlus1; // +0x10
    IVP_OV_Node* Parent; // +0x14: returned by FUN_0635d270 after deleting an empty node.
    // +0x18: the child vector. Soft-capped at 27: grow_root and
    // link_node_under only print a warning, they never refuse the append.
    IVP_U_Vector<IVP_OV_Node> Children;    // +0x18
    IVP_U_Vector<IVP_OV_Element> Elements; // +0x20

    // FUN_0635c2c0: zeroes Parent and both vector headers only; the cell
    // fields stay as allocated until the caller fills them.
    IVP_OV_Node();

    // FUN_0635c310
    ~IVP_OV_Node();
};
static_assert(sizeof(IVP_OV_Node) == 0x28, "IVP_OV_Node size must be 0x28 (operator new(0x28), Common.dll.txt:0635d0cc)");
static_assert(offsetof(IVP_OV_Node, Level) == 0x0c, "IVP_OV_Node+0x0c");
static_assert(offsetof(IVP_OV_Node, LevelPlus1) == 0x10, "IVP_OV_Node+0x10");
static_assert(offsetof(IVP_OV_Node, Parent) == 0x14, "IVP_OV_Node+0x14");
static_assert(offsetof(IVP_OV_Node, Children) == 0x18, "IVP_OV_Node+0x18");
static_assert(offsetof(IVP_OV_Node, Elements) == 0x20, "IVP_OV_Node+0x20");

// ----------------------------------------------------------------------------
// IVP_OV_Element: 0x30 bytes (operator new(0x30) in FUN_0634f180). One per
// IVP_Real_Object with collision geometry enabled (IVP_Real_Object::ov_element,
// +0x90). An IVP_Listener_Hull: a payload of its object's hull-manager pool.
// Vtable .rdata:063a5680, 5 slots:
//   +0x00 0626c9a0 `MOV EAX,3; RET`        get_type
//   +0x04 0635c1e0                          hull_limit_exceeded_event
//   +0x08 0634dc10 `if (this) slot4(1)`    hull_manager_is_going_to_be_deleted_event
//   +0x0c 0628f050 `RET 8`                  hull_manager_is_reset (base default)
//   +0x10 0635c070                          scalar deleting destructor (FUN_0635c090)
// +0x04 (IVP_Listener_Hull::HullPoolSlot) is the full-dword slot index
// IVP_Hull_Manager::PoolInsert returned.
// ----------------------------------------------------------------------------
class IVP_OV_Element : public IVP_Listener_Hull
{
public:
    IVP_OV_Node* node;        // +0x08: the cell this element sits in; cleared by remove_ov_element, set by insert_ov_element.
    IVP_Hull_Manager* hull_manager; // +0x0c: the owner's own embedded hull manager; null until the first insert and after the dtor unlinks.
    float    CenterX;         // +0x10..+0x18: refreshed from the core's world translation
    float    CenterY;         //   (IVP_Core::MWorldFCore.vv, core+0x124) by
    float    CenterZ;         //   RecheckOvElement.
    float    Pad1C;           // +0x1c: never written.
    float    radius;          // +0x20: -1.0f from the ctor, overwritten by every insert.
    IVP_Real_Object* real_object; // +0x24: the owner.
    IVP_U_Vector<IVP_Collision> collision_fvector; // +0x28: every live pairwise collision of this object. The ctor gives it a 16-slot heap buffer (no inline buffer), but the dtor still runs the standard inline-buffer guard.

    // FUN_0635c020
    explicit IVP_OV_Element(IVP_Real_Object* Owner);

    // FUN_0635c090; slot 4 (0635c070) is its scalar deleting wrapper.
    virtual ~IVP_OV_Element();

    // 0626c9a0 (ICF-shared).
    int32_t get_type() override { return 3; }

    // LAB_0635c1e0: real_object->environment->mindist_manager->
    // RecheckOvElement(real_object); the arguments are not read.
    void hull_limit_exceeded_event(IVP_Hull_Manager* HullManager, float HullIntrusionValue) override;

    // LAB_0634dc10 (shared with IVP_Collision's slot 3): delete this.
    void hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* HullManager) override;

    // FUN_0635c130: re-key this element in its owner's hull-manager pool (drop
    // the old slot, evaluate the hull value for now, insert a fresh slot).
    // NewHullMgr is only used when the element has no hull manager yet.
    void InsertIntoHullManager(IVP_Hull_Manager* NewHullMgr, float KeyBias);

    // FUN_0635c200: append Collision and record its slot in whichever of the
    // collision's back-indices is still -1 (+0xc first, else +0x10).
    void add_oo_collision(IVP_Collision* Collision);

    // FUN_0635c250: swap-with-last removal using the collision's back-indices,
    // then reset the one that pointed here to -1.
    void remove_oo_collision(IVP_Collision* Collision);
};
static_assert(sizeof(IVP_OV_Element) == 0x30, "IVP_OV_Element size must be 0x30 (operator new(0x30), Common.dll.txt:0634f1ae)");
static_assert(offsetof(IVP_OV_Element, HullPoolSlot) == 0x04, "IVP_OV_Element+0x04");
static_assert(offsetof(IVP_OV_Element, node) == 0x08, "IVP_OV_Element+0x08");
static_assert(offsetof(IVP_OV_Element, hull_manager) == 0x0c, "IVP_OV_Element+0x0c");
static_assert(offsetof(IVP_OV_Element, CenterX) == 0x10, "IVP_OV_Element+0x10");
static_assert(offsetof(IVP_OV_Element, radius) == 0x20, "IVP_OV_Element+0x20");
static_assert(offsetof(IVP_OV_Element, real_object) == 0x24, "IVP_OV_Element+0x24");
static_assert(offsetof(IVP_OV_Element, collision_fvector) == 0x28, "IVP_OV_Element+0x28");

// ----------------------------------------------------------------------------
// The node hash of IVP_OV_Tree_Manager (+0x170): IVP_VHash(0x100) with vtable
// 063a56a4 {LAB_06369240 compare, 0635c4e0 deleting dtor -> 063691f0}.
// Compare checks (Level, CellX, CellY, CellZ) with an early out; the dtor only
// re-stamps the vtable and runs ~IVP_VHash (063522f0).
// ----------------------------------------------------------------------------
class IVP_OV_Tree_Hash : public IVP_VHash
{
public:
    explicit IVP_OV_Tree_Hash(int Size) : IVP_VHash(Size) {}
    ~IVP_OV_Tree_Hash() override {}
    int Compare(void* Elem0, void* Elem1) override;
};

// ----------------------------------------------------------------------------
// IVP_OV_Tree_Manager: the 0x17c-byte object at IVP_Environment+0x14, the
// spatial subdivision.
// ----------------------------------------------------------------------------
class IVP_OV_Tree_Manager
{
public:
    // FUN_0635c410: QueryCell ctor, +0x174 / +0x178 = 0, NodeHash, then the
    // power-of-two table. +0x16c is left untouched. Built by the environment
    // ctor (operator new(0x17c)).
    IVP_OV_Tree_Manager();

    // FUN_0635c500: deletes NodeHash, then ~QueryCell. The tree's own nodes
    // are not touched.
    ~IVP_OV_Tree_Manager();

    // +0x000..+0x143: CellSizeTable[i] == 2^(i - 40) exactly. Indexed two ways:
    //   * CellSizeTable[Level + 40] == 2^Level, one cell's world size
    //     (FUN_0635d360, FUN_0635cbd0);
    //   * CellSizeTable[0x29 - LevelPlus1] == 2^-Level, its reciprocal, which
    //     FUN_0635c620 multiplies world coordinates by.
    float CellSizeTable[81];

    // +0x144..+0x16b: an embedded IVP_OV_Node (the ctor runs FUN_0635c2c0 on
    // it). FUN_0635c620 fills its five key dwords, FUN_0635d030 hashes it,
    // looks it up and on a miss copies the key into a new node. Its
    // Parent/Children/Elements are never used.
    IVP_OV_Node QueryCell;

    // +0x16c: where insert_ov_element parks the caller's out-vector for the
    // collectors; retail passes it through the manager, not the recursion.
    IVP_U_Vector<IVP_OV_Element>* CollectVector;

    // +0x170: cell key -> IVP_OV_Node* lookup (IVP_VHash: FUN_063523d0
    // AddElem, FUN_06352480 RemoveElem, FUN_06352580 FindElem), tagged by
    // IVP_OV_HashNodeKey.
    IVP_OV_Tree_Hash* NodeHash;

    void* Unmodeled_0x174;    // +0x174: zeroed by the ctor, never read by anything traced.
    IVP_OV_Node* CurrentRoot; // +0x178: the tree's current root cell.

    // FUN_0635d2e0: detach Element from its node.
    void remove_ov_element(IVP_OV_Element* Element);

    // FUN_0635d030: returns the element's new radius (on the x87 stack in
    // retail) and, when TouchingOut is non-null, appends every element whose
    // volume overlaps the inserted one.
    float insert_ov_element(IVP_OV_Element* Element,
                            float MinRadius,
                            float MaxRadius,
                            IVP_U_Vector<IVP_OV_Element>* TouchingOut);

    // FUN_0635d270: deletes Node if empty and returns its parent; null when
    // nothing was collapsed.
    IVP_OV_Node* collapse_empty_node(IVP_OV_Node* Node);

    // FUN_0635d360: OriginOut3 gets C * 2^Level per axis, *ExtentOut the shared
    // edge length 2^(Level+1).
    void node_cell_bounds(const IVP_OV_Node* Node, float* OriginOut3, float* ExtentOut) const;

    // ---- the tree machinery insert_ov_element drives ----

    // FUN_0635c620: choose the level for Element and write its cell key into
    // QueryCell. Returns whichever of the two radii it fitted.
    float compute_query_cell(IVP_OV_Element* Element, float MinRadius, float MaxRadius);

    // FUN_0635cbd0: replace CurrentRoot with a cell one level coarser that
    // still contains it, biased toward Target.
    void grow_root(const IVP_OV_Node* Target);

    // FUN_0635c980: the deepest existing node under Start that contains Target.
    IVP_OV_Node* find_deepest_container(IVP_OV_Node* Start, const IVP_OV_Node* Target);

    // FUN_0635c9d0: create the intermediate cells between Ancestor and Node and
    // link Node in.
    void link_node_under(IVP_OV_Node* Ancestor, IVP_OV_Node* Node);

    // FUN_0635cdd0: append every element in Node's subtree whose sphere
    // touches Element's to CollectVector.
    void collect_subtree(const IVP_OV_Element* Element, IVP_OV_Node* Node);

    // FUN_0635ce90: the same, descending only into children that can reach
    // Target.
    void collect_touching(const IVP_OV_Element* Element, IVP_OV_Node* Node, const IVP_OV_Node* Target);

    // FUN_0635c590: does Coarse, scaled down by LevelDelta levels, contain Fine?
    static bool cell_contains(const IVP_OV_Node* Coarse, const IVP_OV_Node* Fine, int32_t LevelDelta);

    // FUN_0635c8d0: same scaling, overlap instead of containment.
    static bool cell_overlaps(const IVP_OV_Node* Coarse, const IVP_OV_Node* Fine, int32_t LevelDelta);
};
static_assert(offsetof(IVP_OV_Tree_Manager, QueryCell) == 0x144, "IVP_OV_Tree_Manager+0x144");
static_assert(offsetof(IVP_OV_Tree_Manager, CollectVector) == 0x16c, "IVP_OV_Tree_Manager+0x16c");
static_assert(offsetof(IVP_OV_Tree_Manager, NodeHash) == 0x170, "IVP_OV_Tree_Manager+0x170");
static_assert(offsetof(IVP_OV_Tree_Manager, CurrentRoot) == 0x178, "IVP_OV_Tree_Manager+0x178");
static_assert(sizeof(IVP_OV_Tree_Manager) == 0x17c, "IVP_OV_Tree_Manager size must be 0x17c (IVP_Environment+0x14's documented allocation size)");

// FUN_06369200: NodeHash's tag, a table CRC32 over the node's first 20 bytes,
// ORed with 0x80000000 so a live tag never equals the hash's "slot empty" zero.
uint32_t IVP_OV_HashNodeKey(const IVP_OV_Node* Node);

// ----------------------------------------------------------------------------
// IVP_Mindist_Manager: the 0x18-byte object at IVP_Environment+0x10.
// ----------------------------------------------------------------------------
class IVP_Mindist_Manager
{
public:
    // FUN_06350390. Built by the environment ctor (operator new(0x18)).
    explicit IVP_Mindist_Manager(IVP_Environment* Environment);

    // FUN_063503c0: deletes every mindist on the +0x08 list, then the +0x14
    // list (next link read before each delete), then clears the +0x0c vector.
    ~IVP_Mindist_Manager();

    // +0x00: re-entrancy guard. RecheckOvElement sets it around the universe
    // manager callback and takes a stripped-down path when it is already set.
    int32_t InListenerCallback;
    IVP_Environment* environment;   // +0x04

    // +0x08: head of the exact mindist list, threaded through
    // IVP_Ledge_Mindist::ManagerNext (+0x74) / ManagerPrev (+0x78).
    IVP_Ledge_Mindist* ActiveMindistHead; // +0x08

    // +0x0c: an IVP_U_Vector (the ctor zeroes its three fields individually;
    // FUN_0634fec0 removes from it with the remove-and-allow-sort idiom).
    IVP_U_Vector<IVP_Ledge_Mindist> ActiveMindists; // +0x0c..+0x13

    // +0x14: head of the invalid mindist list (same links); FUN_0634e960
    // pushes onto it.
    IVP_Ledge_Mindist* InvalidMindistHead; // +0x14

    // FUN_0634ebd0
    void RecheckOvElement(IVP_Real_Object* Object);

    // FUN_0634f180
    void RebuildOvElement(IVP_Real_Object* Object);

    // FUN_0634fec0: fully detaches a mindist (cancels its queued event,
    // unlinks it from this manager's list and both objects' synapse lists,
    // removes it from ActiveMindists). Defined in ivp_mindist_limiter.cpp.
    void DetachMindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634e9f0: called by IVP_Ledge_Mindist::init_mindist when neither
    // object has a phantom (+0x1c). Links the mindist in, runs the first
    // narrow-phase pass, then queues its event or hands it to slot 7.
    // Defined in ivp_mindist_limiter.cpp.
    void insert_exact_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634eb00: the phantom variant. Defined in ivp_mindist_limiter.cpp.
    void insert_lazy_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634e960: push the mindist onto InvalidMindistHead and both synapses
    // onto their objects' invalid_synapse_list_head.
    void insert_invalid_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634e8a0: insert_exact_mindist's linking half without the
    // narrow-phase pass or event. Port-local name.
    void insert_exact_mindist_no_recalc(IVP_Ledge_Mindist* Mindist);

    // FUN_0634ffa0: unlink from the invalid list (+0x14) and both objects'
    // invalid_synapse_list_head (+0x24).
    void remove_invalid_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_06350020, the library's remove_hull_mindist: takes both synapses out
    // of their objects' hull-manager pools (object +0x64, FUN_06363280).
    // `this` is not read. Callers: the mindist dtor, 0634fcd6,
    // IVP_Complex_Mindist::DeactivateToSimple.
    void remove_hull_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_06350460, the library's recalc_exact_mindist: re-solve one mindist
    // and re-file it (invalid via slot 7, or the phantoms' lazy/hull scheme).
    // Callers 063454cb, 0634f259, 0634f311.
    void recalc_exact_mindist(IVP_Ledge_Mindist* Mindist);

    // FUN_0634f300: recalc_exact_mindist over the exact list (next read before
    // each call). Called by Simulate_Psi.
    void recalc_all_exact_mindists();

    // FUN_0634f230: the library's recalc_all_exact_wheel_mindist over
    // ActiveMindists, backwards. Called by Simulate_Psi.
    void recalc_all_exact_wheel_mindist();

    // FUN_0634f210: the library's recalc_all_exact_mindists_events:
    // IVP_RescheduleCollisionEvent(mindist, 1, 1) over the exact list. Called
    // by IVP_Environment_Simulate_Psi.
    void recalc_all_exact_mindists_events();
};
static_assert(offsetof(IVP_Mindist_Manager, environment) == 0x04, "IVP_Mindist_Manager+0x04");
static_assert(offsetof(IVP_Mindist_Manager, ActiveMindistHead) == 0x08, "IVP_Mindist_Manager+0x08");
static_assert(offsetof(IVP_Mindist_Manager, ActiveMindists) == 0x0c, "IVP_Mindist_Manager+0x0c");
static_assert(offsetof(IVP_Mindist_Manager, InvalidMindistHead) == 0x14, "IVP_Mindist_Manager+0x14");
static_assert(sizeof(IVP_Mindist_Manager) == 0x18, "IVP_Mindist_Manager size must be 0x18");

// FUN_0634f140: spread hash of a collision's partner object (the record's two
// objects XORed with Object, then (h >> 8) * 0x3ff + h). Returned unmasked.
uint32_t IVP_HashCollisionPartner(IVP_Collision* Collision, IVP_Real_Object* Object);

#pragma pack(pop)
