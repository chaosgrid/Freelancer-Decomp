// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_ov_element.h"

#include "ivp_core.h"
#include "ivp_environment.h"
#include "ivp_real_object.h"

#include "ivp_mindist.h" // IVP_Ledge_Mindist / IVP_Complex_Mindist (delegate_collisions_for_object)
#include "ivp_listener.h" // IVP_Collision_Filter (environment->collision_filter)
#include "ivp_cluster_manager.h" // IVP_Universe_Manager (environment->universe_manager)
#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "../ivp_collision/ivp_oo_watcher.h" // IVP_OO_Watcher (delegate_collisions_for_object)
#include "../ivp_utility/ivp_core_trap.h" // IVP_CoreTrap (LAB_0634dc00)

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// IVP_OV_Element, the OV tree (IVP_OV_Tree_Manager / IVP_OV_Node) and
// IVP_Mindist_Manager's re-filing of moved objects. Retail anchors:
//   FUN_0635c020 / 0635c090 / 0635c130  IVP_OV_Element ctor / dtor / hull re-key
//   FUN_0634ebd0 / 0634f140 / 0634f180  RecheckOvElement / partner hash / RebuildOvElement
//   FUN_0635c2c0 .. 0635d3a8             the OV tree
//   FUN_06369200                         IVP_OV_HashNodeKey

namespace
{
    // The spread every hash site in this cluster shares, (h >> 8) * 0x3ff + h
    // with an arithmetic shift; inline at 0634edee and 0634eee9 and the tail
    // of FUN_0634f140. Returned unmasked at all three.
    inline uint32_t SpreadHash(uint32_t Value)
    {
        const int32_t Shifted = static_cast<int32_t>(Value) >> 8;
        return static_cast<uint32_t>(Shifted * 0x3ff) + Value;
    }

    // FUN_063505a0. Swaps two slots of an IVP_OV_Element::collision_fvector and
    // repairs each moved record's back-index. Each IVP_Collision carries two
    // (one per element); retail picks whichever equals the old slot, so it
    // works without knowing which side of the pair this vector belongs to.
    void SwapCollisionSlots(IVP_U_Vector<IVP_Collision>* Vector, int32_t IndexA, int32_t IndexB)
    {
        IVP_Collision* A = Vector->elems[IndexA];
        IVP_Collision* B = Vector->elems[IndexB];
        Vector->elems[IndexA] = B;
        Vector->elems[IndexB] = A;

        if (A->IndexInElement0 == IndexA)
            A->IndexInElement0 = IndexB;
        else
            A->IndexInElement1 = IndexB;

        if (B->IndexInElement0 == IndexB)
            B->IndexInElement0 = IndexA;
        else
            B->IndexInElement1 = IndexA;
    }
}

// The node hash (IVP_OV_Tree_Hash, an IVP_VHash) is driven through AddElem /
// RemoveElem / FindElem (FUN_063523d0 / FUN_06352480 / FUN_06352580,
// ivp_utility/ivp_vhash.cpp); the hash tag is IVP_OV_HashNodeKey.

namespace
{
    // FUN_0635c570: the unbiased IEEE-754 exponent of a float passed as its raw
    // bits, i.e. floor(log2(|x|)) for a normal. Sole caller: compute_query_cell.
    int __cdecl IvpFloatExponent(unsigned param_1)
    {
        return (int)((param_1 >> 0x17) & 0xff) - 0x7f;
    }

    // ------------------------------------------------------------------------
    // FUN_06369200's lookup table (retail DAT_063edcdc, the reflected CRC-32
    // table, polynomial 0xEDB88320). ivp_vhash.cpp defines the same retail
    // global as g_Crc32Table_063edcdc (ivp_utility/ivp_crc32.h); this TU
    // builds its own copy.
    // ------------------------------------------------------------------------
    struct OvCrc32Table
    {
        uint32_t Entry[256];

        OvCrc32Table()
        {
            for (uint32_t I = 0; I < 256; ++I)
            {
                uint32_t C = I;
                for (int32_t K = 0; K < 8; ++K)
                    C = ((C & 1u) != 0) ? (0xEDB88320u ^ (C >> 1)) : (C >> 1);
                Entry[I] = C;
            }
        }
    };

    const OvCrc32Table g_OvCrc32Table;

    // ------------------------------------------------------------------------
    // Retail indexes IVP_OV_Tree_Manager's power-of-two table off the manager
    // base with no bound check (0635c653, 0635cca1, 0635d367). At the extremes
    // (a radius below 2^-41) the index can step one slot outside the 81-entry
    // table and read QueryCell.CellX or the dword in front of the object, so
    // these helpers address through the object base rather than the array.
    //   ScaleAt   : the raw table slot, as FUN_0635c620 addresses it.
    //   CellSizeAt: CellSizeTable[Level + 40] == 2^Level, one cell's world size
    //               at that level, as FUN_0635cbd0/FUN_0635d360 address it.
    // ------------------------------------------------------------------------
    inline float ScaleAt(const IVP_OV_Tree_Manager* Self, int32_t Index)
    {
        return reinterpret_cast<const float*>(Self)[Index];
    }

    inline float CellSizeAt(const IVP_OV_Tree_Manager* Self, int32_t Level)
    {
        return reinterpret_cast<const float*>(Self)[Level + 40];
    }

    // floor/ceil followed by ftol (truncation, exact on an integral double).
    inline int32_t FloorToInt(double Value) { return static_cast<int32_t>(std::floor(Value)); }
    inline int32_t CeilToInt(double Value)  { return static_cast<int32_t>(std::ceil(Value)); }

    // ------------------------------------------------------------------------
    // The sphere test both collectors share (FUN_0635cdd0, FUN_0635ce90). All
    // values stay on the x87 stack (double); the collect happens on
    // rsum^2 >= dist^2, so an exact tangent touches and NaN does not.
    // ------------------------------------------------------------------------
    inline bool SpheresTouch(const IVP_OV_Element* A, const IVP_OV_Element* B)
    {
        const double Dx = (double)A->CenterX - B->CenterX;
        const double Dy = (double)A->CenterY - B->CenterY;
        const double Dz = (double)A->CenterZ - B->CenterZ;
        const double RSum = (double)A->radius + B->radius;
        return RSum * RSum >= (Dz * Dz + Dy * Dy) + Dx * Dx;
    }

    // ------------------------------------------------------------------------
    // FUN_0635c9d0's per-axis child pick (three identical blocks in retail).
    // A node is two cells wide, so splitting one gives three overlapping
    // children at 2C, 2C+1 and 2C+2. Delta is the parent's level minus the
    // target's; the child is one level finer, hence (Delta - 1) in the second
    // test. `Target >= (C+1) << Delta` is retail's phrasing of
    // `Target >= (2C+2) << (Delta-1)`.
    // ------------------------------------------------------------------------
    inline int32_t PickChildCoord(int32_t ParentCoord, int32_t TargetCoord, int32_t Delta)
    {
        if (TargetCoord >= ((ParentCoord + 1) << Delta))
            return (ParentCoord + 1) * 2;
        if (TargetCoord >= ((ParentCoord * 2 + 1) << (Delta - 1)))
            return ParentCoord * 2 + 1;
        return ParentCoord * 2;
    }
}

// ----------------------------------------------------------------------------
// FUN_0634f140
// ----------------------------------------------------------------------------
uint32_t IVP_HashCollisionPartner(IVP_Collision* Collision, IVP_Real_Object* Object)
{
    IVP_Real_Object* Pair[2];
    Collision->get_objects(Pair); // slot 1

    // One of the two IS `Object`, so the XOR leaves the partner's pointer --
    // the key RecheckOvElement's lookup side hashes directly.
    const uint32_t Key = reinterpret_cast<uint32_t>(Pair[1])
                       ^ reinterpret_cast<uint32_t>(Pair[0])
                       ^ reinterpret_cast<uint32_t>(Object);
    return SpreadHash(Key);
}

// ============================================================================
// IVP_OV_Element
// ============================================================================

// FUN_0635c020
IVP_OV_Element::IVP_OV_Element(IVP_Real_Object* Owner)
{
    // No inline buffer: retail mallocs 16 slots up front, so the destructor's
    // inline-buffer guard always takes the free path in practice.
    collision_fvector.memsize = 0x10;
    collision_fvector.n_elems = 0;
    collision_fvector.elems = static_cast<IVP_Collision**>(std::malloc(0x40));

    node = nullptr;
    CenterX = 0.0f;
    CenterY = 0.0f;
    CenterZ = 0.0f;
    hull_manager = nullptr;
    real_object = Owner;
    radius = -1.0f;         // 0xbf800000

    // HullPoolSlot (+0x04) and Pad1C (+0x1c) are not written, as in retail;
    // HullPoolSlot is only read after InsertIntoHullManager has set it.
}

// FUN_0635c090 (FUN_0635c070 is the scalar deleting destructor around it).
IVP_OV_Element::~IVP_OV_Element()
{
    if (hull_manager != nullptr)
    {
        hull_manager->PoolRemove(static_cast<uint16_t>(HullPoolSlot));
        hull_manager = nullptr;
    }

    IVP_Environment* Env = real_object->environment;

    // FUN_0634caf0: every delegator's object_is_removed_from_collision_detection.
    IVP_Environment_Notify_Vector12c_Slot4(Env, real_object);

    IVP_OV_Tree_Manager* Tree = Env->ov_tree_manager;
    Tree->remove_ov_element(this);

    collision_fvector.Clear();
}

// LAB_0635c1e0. Neither argument is read.
void IVP_OV_Element::hull_limit_exceeded_event(IVP_Hull_Manager* /*HullManager*/, float /*HullIntrusionValue*/)
{
    real_object->environment->mindist_manager->RecheckOvElement(real_object);
}

// LAB_0634dc10: deleting destructor (retail's null-this test cannot fire).
void IVP_OV_Element::hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* /*HullManager*/)
{
    delete this;
}

// FUN_0635c130
void IVP_OV_Element::InsertIntoHullManager(IVP_Hull_Manager* NewHullMgr, float KeyBias)
{
    IVP_Environment* Env = real_object->environment;
    const float TimeHi = Env->current_time.time_base;   // env+0xf8
    const float TimeLo = Env->current_time.time_offset; // env+0xfc

    IVP_Hull_Manager* Mgr = hull_manager;
    if (Mgr != nullptr)
    {
        // Retail ignores NewHullMgr here (it is always the object's own
        // manager anyway) and re-keys the existing slot.
        Mgr->PoolRemove(static_cast<uint16_t>(HullPoolSlot));
    }
    else
    {
        Mgr = NewHullMgr;
        hull_manager = Mgr;
    }

    // The hull's outer value extrapolated to now, plus the bias; double on the
    // x87 stack, rounded once into the Insert argument.
    const double Elapsed = (((double)(TimeHi) - Mgr->LastEditTimeHi) + TimeLo) - Mgr->LastEditTimeLo;
    const float Key = (float)((Elapsed * Mgr->OuterGradient + Mgr->OuterValueAtLastEdit) + KeyBias);

    HullPoolSlot = Mgr->PoolInsert(this, Key);
}

// FUN_0635c200
void IVP_OV_Element::add_oo_collision(IVP_Collision* Collision)
{
    const int32_t Index = static_cast<int32_t>(collision_fvector.n_elems);
    collision_fvector.Add(Collision);

    // The first free back-index gets the slot.
    if (Collision->IndexInElement0 == -1)
        Collision->IndexInElement0 = Index;
    else
        Collision->IndexInElement1 = Index;
}

// FUN_0635c250
void IVP_OV_Element::remove_oo_collision(IVP_Collision* Collision)
{
    // IndexInElement0 is trusted only when in range (signed) and the slot
    // really holds this collision.
    int32_t Index = Collision->IndexInElement0;
    if (Index < 0 || Index >= static_cast<int32_t>(collision_fvector.n_elems) ||
        collision_fvector.elems[Index] != Collision)
        Index = Collision->IndexInElement1;

    --collision_fvector.n_elems;
    const int32_t Last = static_cast<int32_t>(collision_fvector.n_elems);
    if (Last > Index)
    {
        IVP_Collision* Moved = collision_fvector.elems[Last];
        collision_fvector.elems[Index] = Moved;
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

// ============================================================================
// IVP_Collision (vtable 063a55b8) and IVP_Collision_Delegator_Root_Mindist
// (vtable 063a5760) -- see src/IVP/ivp_collision/ivp_collision.h.
// ============================================================================

// LAB_0634dc00: writes to address 0.
void IVP_Collision::simulate_time_event(IVP_Environment* /*Environment*/)
{
    IVP_CoreTrap(); // 0634dc00
}

// LAB_0634dc10: delete this; the argument is not read.
void IVP_Collision::delegator_is_going_to_be_deleted_event(IVP_Collision_Delegator* /*Delegator*/)
{
    delete this;
}

// LAB_06360c40
void IVP_Collision_Delegator_Root_Mindist::collision_is_going_to_be_deleted_event(IVP_Collision* Collision)
{
    IVP_Real_Object* Objects[2];
    Collision->get_objects(Objects);                         // slot 1
    IVP_OV_Element* Element0 = Objects[0]->ov_element;
    IVP_OV_Element* Element1 = Objects[1]->ov_element;
    Element0->remove_oo_collision(Collision);                // FUN_0635c250
    Element1->remove_oo_collision(Collision);
}

// LAB_06360c00: every collision filed on the object's OV element, top down,
// gets slot 3 with this delegator. n_elems is read once, elems every pass.
void IVP_Collision_Delegator_Root_Mindist::object_is_removed_from_collision_detection(IVP_Real_Object* Object)
{
    IVP_OV_Element* Element = Object->ov_element;
    for (int32_t I = static_cast<int32_t>(Element->collision_fvector.n_elems) - 1; I >= 0; --I)
    {
        IVP_Collision* Collision = Element->collision_fvector.elems[I];
        Collision->delegator_is_going_to_be_deleted_event(this);         // slot 3
    }
}

// FUN_06360c80: the object-level mindist factory.
IVP_Collision* IVP_Collision_Delegator_Root_Mindist::delegate_collisions_for_object(IVP_Real_Object* BaseObject,
                                                                                 IVP_Real_Object* CollidingElement)
{
    // Surface manager slot 0 on each object: the single convex ledge, or null.
    IVP_Compact_Ledge* Ledge0 = static_cast<IVP_Compact_Ledge*>(BaseObject->surface_manager->get_single_convex());
    IVP_Compact_Ledge* Ledge1 = static_cast<IVP_Compact_Ledge*>(CollidingElement->surface_manager->get_single_convex());

    IVP_Collision* Collision = nullptr;
    if (Ledge0 != nullptr && Ledge1 != nullptr)
    {
        // Either ledge with size-and-flags bits 0..1 set: a recursive mindist
        // (0x94), else a plain one (0x80).
        IVP_Environment* Environment = BaseObject->environment;
        IVP_Ledge_Mindist* Mindist;
        if ((Ledge0->SizeAndFlags & 3) == 0 &&
            (Ledge1->SizeAndFlags & 3) == 0)
            Mindist = new IVP_Ledge_Mindist(Environment, this);     // FUN_0634dd10
        else
            Mindist = new IVP_Complex_Mindist(Environment, this);   // FUN_06362ab0

        // FUN_0634def0 with the first triangle's first edge of each ledge.
        Mindist->init_mindist(BaseObject, CollidingElement, Ledge0->GetFirstEdge(), Ledge1->GetFirstEdge());
        Collision = Mindist;
    }
    else
    {
        // operator new(0x40) + FUN_06369be0: the library's object-pair watcher
        // for objects without a single convex ledge.
        Collision = new IVP_OO_Watcher(this, BaseObject, CollidingElement);
    }

    BaseObject->ov_element->add_oo_collision(Collision);       // FUN_0635c200
    CollidingElement->ov_element->add_oo_collision(Collision);
    return Collision;
}

// LAB_06360c30: delete this; the argument is not read.
void IVP_Collision_Delegator_Root_Mindist::environment_is_going_to_be_deleted_event(IVP_Environment* /*Environment*/)
{
    delete this;
}

// ============================================================================
// IVP_OV_Tree_Manager
// ============================================================================

// FUN_0635d2e0. Writes exactly one field of the element (node).
void IVP_OV_Tree_Manager::remove_ov_element(IVP_OV_Element* Element)
{
    IVP_OV_Node* Node = Element->node;
    if (Node == nullptr)
        return;

    Element->node = nullptr;

    // Retail's inlined ordered remove-by-value: scan backward, decrement
    // n_elems, then shift the tail down re-reading n_elems. A not-found
    // element (index -1) still shrinks the vector -- reproduced, not fixed.
    const uint16_t OldCount = Node->Elements.n_elems;
    int32_t Index = static_cast<int32_t>(OldCount) - 1;
    while (Index >= 0)
    {
        if (Node->Elements.elems[Index] == Element)
            break;
        --Index;
    }

    Node->Elements.n_elems = static_cast<uint16_t>(OldCount - 1);
    for (; Index < static_cast<int32_t>(Node->Elements.n_elems); ++Index)
        Node->Elements.elems[Index] = Node->Elements.elems[Index + 1];

    // Collapse the node, then its parents while they fall empty too.
    IVP_OV_Node* Collapsed = collapse_empty_node(Node);
    while (Collapsed != nullptr)
        Collapsed = collapse_empty_node(Collapsed);
}

// ----------------------------------------------------------------------------
// FUN_0635c620. Picks the finest level at which the element's box spans at
// most two cells per axis and writes that cell key into QueryCell. When the
// caller offers a larger MaxRadius, that radius is tried exactly one level
// coarser; if it fits, retail prefers it (a generous radius avoids re-filing
// on every small move). Returns the radius the key describes, which becomes
// IVP_OV_Element::radius.
// ----------------------------------------------------------------------------
float IVP_OV_Tree_Manager::compute_query_cell(IVP_OV_Element* Element, float MinRadius, float MaxRadius)
{
    // Starting level: the IEEE exponent of twice the radius, plus one. The
    // doubled float's bits are punned, not converted (FUN_0635c570).
    const float Doubled = MinRadius + MinRadius;
    uint32_t DoubledBits = 0;
    std::memcpy(&DoubledBits, &Doubled, sizeof(DoubledBits));

    int32_t LevelPlus1 = IvpFloatExponent(DoubledBits) + 1; // FUN_0635c570
    if (LevelPlus1 < -0x28)
        LevelPlus1 = -0x28;

    // The table slot holding 2^-(LevelPlus1-1), the reciprocal of this level's
    // cell size. Coarsening one level steps one slot down.
    int32_t TableIndex = 0x29 - LevelPlus1;

    int32_t XMin = 0;
    int32_t YMin = 0;
    int32_t ZMin = 0;

    for (;;)
    {
        const double Inv = (double)(ScaleAt(this, TableIndex));

        // Per axis: floor((c - r) * inv) and ceil((c + r) * inv) at most two
        // cells apart. The reciprocal is a power of two, so the products are exact.
        XMin = FloorToInt(((double)(Element->CenterX) - MinRadius) * Inv);
        if (CeilToInt(((double)(Element->CenterX) + MinRadius) * Inv) > XMin + 2)
        {
            ++LevelPlus1;
            --TableIndex;
            continue;
        }

        YMin = FloorToInt(((double)(Element->CenterY) - MinRadius) * Inv);
        if (CeilToInt(((double)(Element->CenterY) + MinRadius) * Inv) > YMin + 2)
        {
            ++LevelPlus1;
            --TableIndex;
            continue;
        }

        ZMin = FloorToInt(((double)(Element->CenterZ) - MinRadius) * Inv);
        if (CeilToInt(((double)(Element->CenterZ) + MinRadius) * Inv) > ZMin + 2)
        {
            ++LevelPlus1;
            --TableIndex;
            continue;
        }

        break;
    }

    // Level and LevelPlus1 are written as a pair, hence the invariant
    // LevelPlus1 == Level + 1.
    QueryCell.CellX = XMin;
    QueryCell.CellY = YMin;
    QueryCell.CellZ = ZMin;
    QueryCell.Level = LevelPlus1 - 1;
    QueryCell.LevelPlus1 = LevelPlus1;

    // The retry runs when MinRadius < MaxRadius or either is NaN.
    if (MinRadius >= MaxRadius)
        return MinRadius;

    // Exactly one extra level is tried.
    const int32_t WideLevelPlus1 = LevelPlus1 + 1;
    const double WideInv = (double)(ScaleAt(this, 0x29 - WideLevelPlus1));

    const int32_t WideXMin = FloorToInt(((double)(Element->CenterX) - MaxRadius) * WideInv);
    if (CeilToInt(((double)(Element->CenterX) + MaxRadius) * WideInv) > WideXMin + 2)
        return MinRadius;

    const int32_t WideYMin = FloorToInt(((double)(Element->CenterY) - MaxRadius) * WideInv);
    if (CeilToInt(((double)(Element->CenterY) + MaxRadius) * WideInv) > WideYMin + 2)
        return MinRadius;

    const int32_t WideZMin = FloorToInt(((double)(Element->CenterZ) - MaxRadius) * WideInv);
    if (CeilToInt(((double)(Element->CenterZ) + MaxRadius) * WideInv) > WideZMin + 2)
        return MinRadius;

    QueryCell.CellX = WideXMin;
    QueryCell.CellY = WideYMin;
    QueryCell.CellZ = WideZMin;
    QueryCell.Level = WideLevelPlus1 - 1;
    QueryCell.LevelPlus1 = WideLevelPlus1;
    return MaxRadius;
}

// ----------------------------------------------------------------------------
// FUN_0635c590. Does `Coarse`, scaled down by LevelDelta levels, fully contain
// `Fine`? Both are two cells wide, so at the fine level Coarse spans
// [C << d, (C << d) + (2 << d) - 2] (Fine itself takes two cells).
// Retail is a bare __stdcall; the caller's ECX is clobbered unread, hence
// static here.
// ----------------------------------------------------------------------------
bool IVP_OV_Tree_Manager::cell_contains(const IVP_OV_Node* Coarse, const IVP_OV_Node* Fine, int32_t LevelDelta)
{
    const int32_t Span = (2 << LevelDelta) - 2;
    const int32_t Ax = Coarse->CellX << LevelDelta;
    const int32_t Ay = Coarse->CellY << LevelDelta;
    const int32_t Az = Coarse->CellZ << LevelDelta;

    if (Fine->CellX < Ax) return false;
    if (Fine->CellY < Ay) return false;
    if (Fine->CellZ < Az) return false;
    if (Fine->CellX > Ax + Span) return false;
    if (Fine->CellY > Ay + Span) return false;
    return Fine->CellZ <= Az + Span;
}

// ----------------------------------------------------------------------------
// FUN_0635c8d0. As cell_contains, but a half-open overlap: [F, F + 2) against
// [C << d, (C + 2) << d). Also a bare __stdcall.
// ----------------------------------------------------------------------------
bool IVP_OV_Tree_Manager::cell_overlaps(const IVP_OV_Node* Coarse, const IVP_OV_Node* Fine, int32_t LevelDelta)
{
    if (Fine->CellX + 2 <= (Coarse->CellX << LevelDelta)) return false;
    if (Fine->CellY + 2 <= (Coarse->CellY << LevelDelta)) return false;
    if (Fine->CellZ + 2 <= (Coarse->CellZ << LevelDelta)) return false;
    if (Fine->CellX >= ((Coarse->CellX + 2) << LevelDelta)) return false;
    if (Fine->CellY >= ((Coarse->CellY + 2) << LevelDelta)) return false;
    return Fine->CellZ < ((Coarse->CellZ + 2) << LevelDelta);
}

// ----------------------------------------------------------------------------
// FUN_0635cbd0. Replaces CurrentRoot with a cell one level coarser that still
// contains it and adopts the old root as its only child. insert_ov_element
// loops on this until the root swallows the new cell.
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::grow_root(const IVP_OV_Node* Target)
{
    IVP_OV_Node* OldRoot = CurrentRoot;

    if (OldRoot->LevelPlus1 > 0x28)
        std::printf("*** ERROR *** Excessive sizelevel (%d) for element\n", OldRoot->LevelPlus1);

    IVP_OV_Node* NewRoot = new IVP_OV_Node(); // operator new(0x28) + FUN_0635c2c0

    // MSVC's signed `x % 2` (-1 / 0 / +1), read off the old root before halving.
    const int32_t ParityX = OldRoot->CellX % 2;
    const int32_t ParityY = OldRoot->CellY % 2;
    const int32_t ParityZ = OldRoot->CellZ % 2;

    // Division by two rounding toward zero (plain C `/ 2`, not a shift).
    NewRoot->CellX = OldRoot->CellX / 2;
    NewRoot->CellY = OldRoot->CellY / 2;
    NewRoot->CellZ = OldRoot->CellZ / 2;
    NewRoot->Level = OldRoot->Level + 1;
    NewRoot->LevelPlus1 = OldRoot->LevelPlus1 + 1;

    const double NewCell = (double)(CellSizeAt(this, NewRoot->Level));
    const double TargetCell = (double)(CellSizeAt(this, Target->Level));

    // Per axis: a negative odd coordinate needs one more step left (the
    // toward-zero halving moved it the wrong way). An even one may be C/2 or
    // C/2 - 1; retail steps left only when the parent's origin is strictly to
    // the right of the target's. A positive odd one needs nothing.
    if (ParityX == -1)
        NewRoot->CellX -= 1;
    else if (ParityX == 0 && NewRoot->CellX * NewCell > Target->CellX * TargetCell)
        NewRoot->CellX -= 1;

    if (ParityY == -1)
        NewRoot->CellY -= 1;
    else if (ParityY == 0 && NewRoot->CellY * NewCell > Target->CellY * TargetCell)
        NewRoot->CellY -= 1;

    if (ParityZ == -1)
        NewRoot->CellZ -= 1;
    else if (ParityZ == 0 && NewRoot->CellZ * NewCell > Target->CellZ * TargetCell)
        NewRoot->CellZ -= 1;

    // Cannot fire on a brand-new node; retail's check is generic. Note >= 27
    // here versus > 27 in link_node_under.
    if (NewRoot->Children.n_elems >= 0x1b)
        std::printf("*** ERROR *** : Mehr als 27 Kinder *********************************\n");

    NewRoot->Children.Add(OldRoot);
    OldRoot->Parent = NewRoot;

    NodeHash->AddElem(NewRoot, IVP_OV_HashNodeKey(NewRoot)); // FUN_063523d0

    CurrentRoot = NewRoot;
}

// ----------------------------------------------------------------------------
// FUN_0635c980. Walks down from Start while some child still contains Target
// and returns the deepest node reached. The level delta is that of the
// children (one level finer), hence the extra -1.
// ----------------------------------------------------------------------------
IVP_OV_Node* IVP_OV_Tree_Manager::find_deepest_container(IVP_OV_Node* Start, const IVP_OV_Node* Target)
{
    IVP_OV_Node* Current = Start;

    for (;;)
    {
        const int32_t ChildDelta = Current->Level - Target->Level - 1;

        int32_t Index = 0;
        bool Descended = false;
        while (Index < static_cast<int32_t>(Current->Children.n_elems))
        {
            IVP_OV_Node* Child = Current->Children.elems[Index];
            if (cell_contains(Child, Target, ChildDelta))
            {
                Current = Child;
                Descended = true;
                break;
            }
            ++Index;
        }

        if (!Descended)
            return Current;
    }
}

// ----------------------------------------------------------------------------
// FUN_0635c9d0. Splices Node in under Ancestor, creating every intermediate
// cell on the way down; each is registered in the key hash like a grown root.
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::link_node_under(IVP_OV_Node* Ancestor, IVP_OV_Node* Node)
{
    IVP_OV_Node* Parent = Ancestor;
    int32_t Delta = Parent->LevelPlus1 - Node->LevelPlus1;

    while (Delta != 1)
    {
        IVP_OV_Node* Child = new IVP_OV_Node();
        Child->CellX = PickChildCoord(Parent->CellX, Node->CellX, Delta);
        Child->CellY = PickChildCoord(Parent->CellY, Node->CellY, Delta);
        Child->CellZ = PickChildCoord(Parent->CellZ, Node->CellZ, Delta);
        Child->Level = Parent->Level - 1;
        Child->LevelPlus1 = Parent->LevelPlus1 - 1;
        Child->Parent = Parent;

        Parent->Children.Add(Child);
        NodeHash->AddElem(Child, IVP_OV_HashNodeKey(Child)); // FUN_063523d0

        if (Parent->Children.n_elems > 0x1b)
            std::printf("*** ERROR *** Excessive amount of children: %d\n",
                        static_cast<int32_t>(Parent->Children.n_elems));

        Parent = Child;
        Delta = Child->LevelPlus1 - Node->LevelPlus1;
    }

    // Node is now exactly one level below Parent.
    Node->Parent = Parent;
    Parent->Children.Add(Node);

    if (Parent->Children.n_elems > 0x1b)
        std::printf("*** ERROR *** Excessive amount of children: %d\n",
                    static_cast<int32_t>(Parent->Children.n_elems));
}

// ----------------------------------------------------------------------------
// FUN_0635cdd0. Appends every element in Node's whole subtree whose sphere
// touches Element's to CollectVector, with no further pruning (the caller
// knows the subtree is in range).
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::collect_subtree(const IVP_OV_Element* Element, IVP_OV_Node* Node)
{
    for (int32_t I = static_cast<int32_t>(Node->Elements.n_elems) - 1; I >= 0; --I)
    {
        IVP_OV_Element* Other = Node->Elements.elems[I];
        if (SpheresTouch(Other, Element))
            CollectVector->Add(Other);
    }

    for (int32_t I = static_cast<int32_t>(Node->Children.n_elems) - 1; I >= 0; --I)
        collect_subtree(Element, Node->Children.elems[I]);
}

// ----------------------------------------------------------------------------
// FUN_0635ce90. As collect_subtree for this node's own elements, but only
// children that can reach Target are walked, and the child that IS Target
// hands off to collect_subtree. Which side of the overlap test is the coarse
// one flips with the level relation, hence the two halves.
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::collect_touching(const IVP_OV_Element* Element, IVP_OV_Node* Node, const IVP_OV_Node* Target)
{
    for (int32_t I = static_cast<int32_t>(Node->Elements.n_elems) - 1; I >= 0; --I)
    {
        IVP_OV_Element* Other = Node->Elements.elems[I];
        if (SpheresTouch(Other, Element))
            CollectVector->Add(Other);
    }

    // Are Node's children still coarser than Target?
    if (Node->LevelPlus1 - 1 > Target->LevelPlus1)
    {
        const int32_t Delta = Node->Level - Target->Level - 1;
        for (int32_t I = 0; I < static_cast<int32_t>(Node->Children.n_elems); ++I)
        {
            IVP_OV_Node* Child = Node->Children.elems[I];
            if (Child == Target)
                collect_subtree(Element, Child);
            else if (cell_overlaps(Child, Target, Delta))
                collect_touching(Element, Child, Target);
        }
    }
    else
    {
        // Target is the coarse side now.
        const int32_t Delta = Target->Level - Node->Level + 1;
        for (int32_t I = 0; I < static_cast<int32_t>(Node->Children.n_elems); ++I)
        {
            IVP_OV_Node* Child = Node->Children.elems[I];
            if (Child == Target)
                collect_subtree(Element, Child);
            else if (cell_overlaps(Target, Child, Delta))
                collect_touching(Element, Child, Target);
        }
    }
}

// ----------------------------------------------------------------------------
// FUN_0635d030. Files Element at a level chosen from its radius, growing or
// subdividing the tree as needed, and (with TouchingOut) collects every
// element whose sphere touches it. Returns the radius the element was filed
// at (left on the x87 stack in retail).
// ----------------------------------------------------------------------------
float IVP_OV_Tree_Manager::insert_ov_element(IVP_OV_Element* Element,
                                             float MinRadius,
                                             float MaxRadius,
                                             IVP_U_Vector<IVP_OV_Element>* TouchingOut)
{
    if (Element == nullptr)
        return 0.0f;         // DAT_063a53d4

    const float FiledRadius = compute_query_cell(Element, MinRadius, MaxRadius);
    Element->radius = FiledRadius;

    // Does the cell exist? Tag and comparator argument are both the embedded
    // QueryCell (CRC of its first 20 bytes; LAB_06369240 compares four dwords).
    IVP_OV_Node* Node = (IVP_OV_Node*)NodeHash->FindElem(&QueryCell, IVP_OV_HashNodeKey(&QueryCell)); // FUN_06352580

    if (Node != nullptr)
    {
        Node->Elements.Add(Element);
        Element->node = Node;
    }
    else
    {
        Node = new IVP_OV_Node();
        Node->CellX = QueryCell.CellX;
        Node->CellY = QueryCell.CellY;
        Node->CellZ = QueryCell.CellZ;
        Node->Level = QueryCell.Level;
        Node->LevelPlus1 = QueryCell.LevelPlus1;
        Node->Elements.Add(Element);
        Element->node = Node;

        if (CurrentRoot == nullptr)
        {
            // The first cell is the tree. Retail jumps straight to the
            // epilogue: CollectVector is not published and nothing gathered.
            CurrentRoot = Node;
            NodeHash->AddElem(Node, IVP_OV_HashNodeKey(Node)); // FUN_063523d0
            return FiledRadius;
        }

        // Coarsen the root until it swallows the new cell. A root finer than
        // the cell short-circuits straight to another growth step.
        while (CurrentRoot->Level < Node->Level ||
               !cell_contains(CurrentRoot, Node, CurrentRoot->Level - Node->Level))
        {
            grow_root(Node);
        }

        if (CurrentRoot->Level == Node->Level)
        {
            // The root grew into exactly this cell: file into the root and
            // destroy the temporary.
            CurrentRoot->Elements.Add(Element);
            Element->node = CurrentRoot;
            delete Node;                        // FUN_0635c310 + FUN_06391d5a
            Node = CurrentRoot;
        }
        else
        {
            // Register the cell and splice it under the deepest existing node
            // that contains it.
            NodeHash->AddElem(Node, IVP_OV_HashNodeKey(Node)); // FUN_063523d0
            link_node_under(find_deepest_container(CurrentRoot, Node), Node);
        }
    }

    // The out-vector reaches the recursion through the manager; retail writes
    // it even when null.
    CollectVector = TouchingOut;
    if (TouchingOut != nullptr)
        collect_touching(Element, CurrentRoot, Node);

    return FiledRadius;
}

// ----------------------------------------------------------------------------
// FUN_0635d270. Deletes Node if it is completely empty and returns its former
// parent, so the caller can keep collapsing; null when nothing was collapsed.
// ----------------------------------------------------------------------------
IVP_OV_Node* IVP_OV_Tree_Manager::collapse_empty_node(IVP_OV_Node* Node)
{
    if (Node->Elements.n_elems != 0)
        return nullptr;
    if (Node->Children.n_elems != 0)
        return nullptr;

    if (Node->Parent == nullptr)
        CurrentRoot = nullptr;

    // FUN_06352480 is find-and-remove and faults if the value is absent;
    // every tree node was registered on the way in.
    NodeHash->RemoveElem(Node, IVP_OV_HashNodeKey(Node)); // FUN_06352480

    IVP_OV_Node* Parent = Node->Parent; // read after the unregister, as retail does
    delete Node;
    return Parent;
}

// ----------------------------------------------------------------------------
// FUN_0635d360. Decodes a cell back into world space. A node is two cells
// wide, so its edge is 2^(Level+1) == CellSizeTable[LevelPlus1 + 40], one
// scalar for all three axes. Callers: the ray solver (0634a84b) and the
// sphere query (063507fd).
// ----------------------------------------------------------------------------
void IVP_OV_Tree_Manager::node_cell_bounds(const IVP_OV_Node* Node, float* OriginOut3, float* ExtentOut) const
{
    OriginOut3[0] = CellSizeAt(this, Node->Level) * (float)(Node->CellX);
    OriginOut3[1] = CellSizeAt(this, Node->Level) * (float)(Node->CellY);
    OriginOut3[2] = CellSizeAt(this, Node->Level) * (float)(Node->CellZ);
    *ExtentOut = CellSizeAt(this, Node->LevelPlus1);
}

// ============================================================================
// IVP_OV_Node
// ============================================================================

// FUN_0635c2c0. Zeroes Parent and both vector headers only; the cell fields
// stay as allocated, as in retail (every construction site fills them).
IVP_OV_Node::IVP_OV_Node()
    : Parent(nullptr)
{
}

// ----------------------------------------------------------------------------
// IVP_OV_Tree_Hash / IVP_OV_Tree_Manager ctor and dtor
// ----------------------------------------------------------------------------

// LAB_06369240: stored node vs probe node, +0x0c first, then +0x00, +0x04,
// +0x08; 0 on the first mismatch, else 1.
int IVP_OV_Tree_Hash::Compare(void* Elem0, void* Elem1)
{
    const IVP_OV_Node* const Stored = static_cast<const IVP_OV_Node*>(Elem0);
    const IVP_OV_Node* const Probe = static_cast<const IVP_OV_Node*>(Elem1);
    if (Stored->Level != Probe->Level)
    {
        return 0;
    }
    if (Stored->CellX != Probe->CellX)
    {
        return 0;
    }
    if (Stored->CellY != Probe->CellY)
    {
        return 0;
    }
    return Stored->CellZ == Probe->CellZ ? 1 : 0;
}

// FUN_0635c410
IVP_OV_Tree_Manager::IVP_OV_Tree_Manager()
{
    Unmodeled_0x174 = nullptr;
    CurrentRoot = nullptr;
    NodeHash = new IVP_OV_Tree_Hash(0x100);

    // Two x87 accumulators seeded with 1.0f (DAT_063a53d0): one doubled and
    // stored upward from +0xa0, one halved (DAT_063a5460) and stored downward,
    // 0x29 rounds. All exact powers of two.
    float Upward = 1.0f;
    float Downward = 1.0f;
    for (int32_t Round = 0; Round < 0x29; ++Round)
    {
        CellSizeTable[40 + Round] = Upward;
        CellSizeTable[40 - Round] = Downward;
        Upward = Upward + Upward;
        Downward = Downward * 0.5f;
    }
}

// FUN_0635c500
IVP_OV_Tree_Manager::~IVP_OV_Tree_Manager()
{
    if (NodeHash != nullptr)
    {
        delete NodeHash; // slot 1 = 0635c4e0
    }
    NodeHash = nullptr;
}

// FUN_0635c310
IVP_OV_Node::~IVP_OV_Node()
{
    // Unlink from the parent: the same shape as remove_ov_element's inlined
    // copy (backward scan, decrement, shift even when not found), reproduced
    // rather than fixed.
    if (Parent != nullptr)
    {
        const int32_t OldCount = static_cast<int32_t>(Parent->Children.n_elems);
        int32_t Index = OldCount - 1;
        while (Index >= 0)
        {
            if (Parent->Children.elems[Index] == this)
                break;
            --Index;
        }

        Parent->Children.n_elems = static_cast<uint16_t>(OldCount - 1);
        for (; Index < static_cast<int32_t>(Parent->Children.n_elems); ++Index)
            Parent->Children.elems[Index] = Parent->Children.elems[Index + 1];
    }

    // Destroy every child. Taking slot 0 repeatedly terminates because each
    // child's destructor unlinks itself from this vector. A null slot only
    // skips the delete and re-tests the count, as in retail.
    while (Children.n_elems != 0)
    {
        IVP_OV_Node* Child = Children.elems[0];
        if (Child != nullptr)
            delete Child; // FUN_0635c310 + FUN_06391d5a
    }

    Elements.Clear();
    Children.Clear();
}

// ----------------------------------------------------------------------------
// FUN_06369200
// ----------------------------------------------------------------------------
uint32_t IVP_OV_HashNodeKey(const IVP_OV_Node* Node)
{
    const uint8_t* Bytes = reinterpret_cast<const uint8_t*>(Node);

    uint32_t Crc = 0xffffffffu;
    for (int32_t I = 0; I < 0x14; ++I) // the five key dwords
        Crc = (Crc >> 8) ^ g_OvCrc32Table.Entry[(Bytes[I] ^ Crc) & 0xff];

    // The top bit keeps a live tag from matching the hash's "slot empty" zero.
    return Crc | 0x80000000u;
}

// ============================================================================
// IVP_Mindist_Manager
// ============================================================================

// FUN_06350390. The member vector ctor runs, then all six dwords are cleared
// again.
IVP_Mindist_Manager::IVP_Mindist_Manager(IVP_Environment* Environment)
{
    InListenerCallback = 0;
    environment = nullptr;
    ActiveMindistHead = nullptr;
    ActiveMindists.memsize = 0;
    ActiveMindists.n_elems = 0;
    ActiveMindists.elems = nullptr;
    InvalidMindistHead = nullptr;

    environment = Environment;
}

// FUN_063503c0: deletes both mindist lists (slot 4), then the member vector.
IVP_Mindist_Manager::~IVP_Mindist_Manager()
{
    IVP_Ledge_Mindist* Mindist = ActiveMindistHead;
    while (Mindist != nullptr)
    {
        IVP_Ledge_Mindist* const Next = Mindist->ManagerNext;
        delete Mindist;
        Mindist = Next;
    }

    Mindist = InvalidMindistHead;
    while (Mindist != nullptr)
    {
        IVP_Ledge_Mindist* const Next = Mindist->ManagerNext;
        delete Mindist;
        Mindist = Next;
    }
}

// ----------------------------------------------------------------------------
// FUN_0634ebd0. The object has moved (or just gained collision geometry):
// re-file its OV element, then reconcile its collision records with the
// objects now in range. Surviving records are compacted into the first
// KeptCount slots of collision_fvector, the rest are deleted, and each new
// partner is offered to the environment's collision delegators.
//
// The element itself comes back from collect_touching (it trivially touches
// itself); the original_core equality gate below drops it, as in retail.
// ----------------------------------------------------------------------------
void IVP_Mindist_Manager::RecheckOvElement(IVP_Real_Object* Object)
{
    // A 128-slot stack vector with its buffer inline.
    IVP_U_Vector_Fixed<IVP_OV_Element, 128> TouchingElements;

    IVP_OV_Element* Element = Object->ov_element;
    if (Element == nullptr)
        return;                                   // retail only runs the vector destructor here.

    IVP_OV_Tree_Manager* Tree = environment->ov_tree_manager;
    IVP_Core* Core = Object->core;

    Tree->remove_ov_element(Element);

    ++environment->counter_7c;

    // The element's centre from the core's world translation (core+0x124).
    Element->CenterX = Core->MWorldFCore.vv.x;
    Element->CenterY = Core->MWorldFCore.vv.y;
    Element->CenterZ = Core->MWorldFCore.vv.z;

    IVP_Hull_Manager* HullMgr = &Object->hull_manager; // +0x44

    if (InListenerCallback != 0)
    {
        // Re-entrant path (inside the universe manager's callback): re-file
        // at the core's own radius, collect nothing, re-key with a pure
        // epsilon bias.
        const float Radius = Core->UpperLimitRadius; // core+0x04
        Tree->insert_ov_element(Element, Radius, Radius, nullptr); // result discarded
        Element->InsertIntoHullManager(HullMgr, 1.0e-10f);         // 0x2edbe6ff
        return;
    }

    // The range manager's x87 result plus the core radius, rounded once.
    const double CollRange = environment->range_manager->get_coll_range_in_world(Object);
    const float Bound = (float)(CollRange + Core->UpperLimitRadius);

    // Universe manager slot 0: "about to be re-filed here", only for movable
    // objects ((flags & 7) != 0) and guarded against re-entry.
    IVP_Universe_Manager* const UniverseManager = environment->universe_manager; // env+0x2c
    if (UniverseManager != nullptr && (Object->flags & 0x7u) != 0)
    {
        InListenerCallback = 1;
        UniverseManager->ensure_objects_in_environment(Object, &Element->CenterX, Bound); // slot 0
        InListenerCallback = 0;
    }

    // Re-file, then re-key the hull entry with the new radius minus the core's.
    const float NewRadius = Tree->insert_ov_element(Element, Bound, Bound, &TouchingElements);
    Element->InsertIntoHullManager(HullMgr, NewRadius - Core->UpperLimitRadius);

    // ---- open-addressed index of the existing collision records ----
    IVP_Collision_Filter* PairFilter = environment->collision_filter;

    // Table size: about 4 entries per expected record, rounded down to a power
    // of two and capped at 1024.
    const int32_t NumTouching = static_cast<int32_t>(TouchingElements.n_elems);
    const int32_t Need = (static_cast<int32_t>(Element->collision_fvector.n_elems) + NumTouching) * 4 + 4;
    int32_t TableSize = 0x400;
    if (Need < TableSize)
    {
        do
        {
            TableSize >>= 1;
        } while (TableSize > Need);
    }

    // The 1024-entry stack table covers every capped case; only a very large
    // record count forces a malloc.
    uint16_t InlineTable[1024];
    uint16_t* Table = nullptr;
    int32_t Mask = 0;
    bool TableOnHeap = false;

    const int32_t NumExisting = static_cast<int32_t>(Element->collision_fvector.n_elems);
    if (TableSize > NumExisting)
    {
        Mask = TableSize - 1;
        TableOnHeap = false;
        Table = InlineTable;
    }
    else
    {
        const int32_t Want = NumExisting * 2;
        TableOnHeap = true;
        if (TableSize <= Want)
        {
            do
            {
                TableSize <<= 1;
            } while (TableSize <= Want);
        }
        Mask = TableSize - 1;
        Table = static_cast<uint16_t*>(std::malloc(static_cast<size_t>(TableSize) * 2u));
    }

    // Filled with 0xffff, a dword at a time.
    for (int32_t I = (Mask >> 1); I >= 0; --I)
        reinterpret_cast<uint32_t*>(Table)[I] = 0xffffffffu;

    int32_t KeptCount = 0; // the compaction cursor

    // Index every existing record by its partner object.
    for (int32_t I = static_cast<int32_t>(Element->collision_fvector.n_elems) - 1; I >= 0; --I)
    {
        IVP_Collision* Coll = Element->collision_fvector.elems[I];
        IVP_Real_Object* Pair[2];
        Coll->get_objects(Pair);

        const uint32_t Key = reinterpret_cast<uint32_t>(Pair[1])
                           ^ reinterpret_cast<uint32_t>(Pair[0])
                           ^ reinterpret_cast<uint32_t>(Object);
        uint32_t Slot = SpreadHash(Key) & static_cast<uint32_t>(Mask);

        while (Table[Slot] != 0xffff) // linear probing
            Slot = (Slot + 1) & static_cast<uint32_t>(Mask);

        Table[Slot] = static_cast<uint16_t>(I);
    }

    // The partners that have no record yet.
    IVP_U_Vector_Fixed<IVP_Real_Object, 128> NewPartners;

    for (int32_t K = static_cast<int32_t>(TouchingElements.n_elems) - 1; K >= 0; --K)
    {
        IVP_OV_Element* Other = TouchingElements.elems[K];
        IVP_Real_Object* OtherObj = Other->real_object;

        // At least one of the two has to be movable.
        if ((Object->flags & 0x7u) == 0 && (OtherObj->flags & 0x7u) == 0)
            continue;

        // Objects sharing an original core never collide.
        if (Object->original_core == OtherObj->original_core)
            continue;

        // Two asymmetric core-flag gates: a 0x300 object refuses 0x300 and 0xc
        // partners, a 0xc object refuses 0x300 partners. The repeated 0x300
        // test on CoreA is retail's (redundant).
        IVP_Core* CoreA = Object->core;
        const uint32_t FlagsA = CoreA->Flags;
        if ((FlagsA & 0x300u) != 0)
        {
            if ((OtherObj->core->Flags & 0x300u) != 0)
                continue;
            if ((CoreA->Flags & 0x300u) != 0 && (OtherObj->core->Flags & 0x0cu) != 0)
                continue;
        }
        if ((FlagsA & 0x0cu) != 0)
        {
            if ((OtherObj->core->Flags & 0x300u) != 0)
                continue;
        }

        // The environment's pairwise filter, slot 0.
        if (PairFilter->check_objects_for_collision_detection(Object, OtherObj) == 0)
            continue;

        // Look the partner up. The key is the partner pointer itself, since the
        // build-side XOR cancelled Object out of every entry.
        uint32_t Slot = SpreadHash(reinterpret_cast<uint32_t>(OtherObj)) & static_cast<uint32_t>(Mask);
        int32_t Found = static_cast<int16_t>(Table[Slot]); // sign-extended: 0xffff means empty

        while (Found != -1)
        {
            IVP_Collision* Candidate = Element->collision_fvector.elems[Found];
            IVP_Real_Object* Pair[2];
            Candidate->get_objects(Pair);
            if (Pair[0] == OtherObj || Pair[1] == OtherObj)
                break;
            Slot = (Slot + 1) & static_cast<uint32_t>(Mask);
            Found = static_cast<int16_t>(Table[Slot]);
        }

        if (Found == -1)
        {
            NewPartners.Add(OtherObj);
            continue;
        }

        // A record exists: compact it down to the cursor, keeping the table's
        // slot -> index mapping intact.
        if (Found > KeptCount)
        {
            IVP_Collision* CollAtCursor = Element->collision_fvector.elems[KeptCount];
            uint32_t CursorSlot = IVP_HashCollisionPartner(CollAtCursor, Object)
                                & static_cast<uint32_t>(Mask);

            // Sign-extended entry against the full 32-bit cursor, as retail.
            while (static_cast<int32_t>(static_cast<int16_t>(Table[CursorSlot])) != KeptCount)
                CursorSlot = (CursorSlot + 1) & static_cast<uint32_t>(Mask);

            Table[CursorSlot] = static_cast<uint16_t>(Found);
            Table[Slot] = static_cast<uint16_t>(KeptCount);
            SwapCollisionSlots(&Element->collision_fvector, KeptCount, Found);
        }
        ++KeptCount;
    }

    // Everything past the cursor is out of range.
    for (int32_t I = static_cast<int32_t>(Element->collision_fvector.n_elems) - 1; I >= KeptCount; --I)
    {
        IVP_Collision* Coll = Element->collision_fvector.elems[I];
        if (Coll != nullptr)
            delete Coll;                                            // slot 4, deleting dtor
    }

    // Offer every new partner to the collision delegators (vector_12c) until
    // one claims the pair.
    for (int32_t B = static_cast<int32_t>(NewPartners.n_elems) - 1; B >= 0; --B)
    {
        IVP_Real_Object* NewObj = NewPartners.elems[B];
        for (int32_t D = static_cast<int32_t>(environment->vector_12c.n_elems) - 1; D >= 0; --D)
        {
            IVP_Collision_Delegator_Root* Delegator =
                static_cast<IVP_Collision_Delegator_Root*>(environment->vector_12c.elems[D]);
            IVP_Collision* Claimed =
                Delegator->delegate_collisions_for_object(Object, NewObj); // slot 5
            if (Claimed != nullptr)
                break;
        }
    }

    // Teardown, in retail's order.
    NewPartners.Clear();
    if (TableOnHeap && Table != nullptr)
        std::free(Table);
    TouchingElements.Clear();
}

// ----------------------------------------------------------------------------
// FUN_0634f180. Replaces the object's OV element with a fresh one and re-files
// it. Caller: IVP_Real_Object::SetCollisionGeomEnabled's enable path
// (FUN_06344d50).
// ----------------------------------------------------------------------------
void IVP_Mindist_Manager::RebuildOvElement(IVP_Real_Object* Object)
{
    if (Object->ov_element != nullptr)
        delete Object->ov_element; // slot 4, deleting destructor

    Object->ov_element = nullptr;

    void* Raw = ::operator new(0x30);
    Object->ov_element = (Raw != nullptr)
                       ? new (Raw) IVP_OV_Element(Object)
                       : nullptr;

    RecheckOvElement(Object);
}
