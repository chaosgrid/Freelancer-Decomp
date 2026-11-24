// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FUN_0634e0b0 -- IVP_Create_Exact_Mindists (the library's
// IVP_Mindist_Manager::create_exact_mindists) and its private helpers
// FUN_0634e7d0 / FUN_0634e7f0 / FUN_0634e820 / FUN_0634e860 / FUN_063505a0.
// See ivp_mindist_create.h.

#include "ivp_mindist_create.h"

#include "ivp_collision.h"                            // IVP_Collision, IVP_Collision_Delegator
#include "../ivp_compact_builder/ivp_compact_ledge.h" // IVP_Compact_Ledge
#include "../ivp_physics/ivp_mindist.h"               // IVP_Ledge_Mindist, IVP_Complex_Mindist
#include "../ivp_physics/ivp_real_object.h"           // IVP_Real_Object, IVP_TransformWorldToSlotSpace
#include "../ivp_physics/ivp_surface_manager.h"       // IVP_SurfaceManager, IVP_SurfaceManager_OutList
#include "../ivp_physics/ivp_environment.h"           // IVP_Environment
#include "../ivp_physics/ivp_core.h"                  // IVP_Core

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "../ivp_utility/ivp_u_bigvector.h" // IVP_U_BigVector_Grow (FUN_0635d3e0): grow a
                                            // {capacity, count, elems, inline buffer @+0xc}
                                            // list to 2*capacity+1

namespace
{
    // The two ledge lists FUN_0634e0b0 keeps on its stack: a 32-bit
    // {capacity, count, elems} header (FUN_06350540: elems = buffer, capacity =
    // 0x100, count = 0) immediately followed by its 0x100-entry inline buffer,
    // which is what the grow helper's `elems != this+0xc` test relies on.
    struct FLedgeList
    {
        IVP_SurfaceManager_OutList List;
        void* InlineBuffer[0x100];

        FLedgeList() // FUN_06350540
        {
            List.Elems = InlineBuffer;
            List.Capacity = 0x100;
            List.Count = 0;
        }

        // Inlined at :0634e748 / :0634e787: free unless inline or null.
        ~FLedgeList()
        {
            if (List.Elems != InlineBuffer && List.Elems != nullptr)
                std::free(List.Elems); // FUN_06343fb0
        }

        // FUN_06350590 + the store (inlined for object 1 at :0634e340).
        void Add(const IVP_Compact_Ledge* Ledge)
        {
            if (List.Count >= List.Capacity)
                IVP_U_BigVector_Grow(&List); // FUN_0635d3e0
            List.Elems[List.Count] = const_cast<IVP_Compact_Ledge*>(Ledge);
            ++List.Count;
        }

        const IVP_Compact_Ledge* At(int32_t Index) const
        {
            return static_cast<const IVP_Compact_Ledge*>(List.Elems[Index]);
        }
    };
    static_assert(offsetof(FLedgeList, InlineBuffer) == 0x0c, "inline buffer at list+0xc (FUN_0635d3e0's test)");

    // The local IVP_U_Vector with a 0x80-entry inline buffer right after its
    // header (FUN_06347dd0 at :0634e45f).
    struct FNewMindistVector
    {
        IVP_U_Vector<IVP_Collision> Vector;
        IVP_Collision* InlineBuffer[0x80];

        FNewMindistVector()
            : Vector(InlineBuffer, 0x80)
        {
        }
    };

    // The pair key FUN_0634e0b0 builds at its local +0x48 and hands to
    // FUN_0634e7f0 / FUN_0634e860. Only the two ledges are read.
    struct FMindistPairKey
    {
        IVP_Real_Object* Object0;        // +0x00
        IVP_Real_Object* Object1;        // +0x04
        const IVP_Compact_Ledge* Ledge0; // +0x08
        const IVP_Compact_Ledge* Ledge1; // +0x0c
    };

    // The mix both hashes share; the shift is arithmetic (SAR).
    uint32_t HashLedgePair(const IVP_Compact_Ledge* Ledge0, const IVP_Compact_Ledge* Ledge1)
    {
        const uint32_t Mixed = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Ledge1)) * 75u)
                               ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Ledge0));
        const int32_t Shifted = static_cast<int32_t>(Mixed) >> 8;
        return static_cast<uint32_t>(Shifted) * 0x3ffu + Mixed;
    }

    // FUN_0634e7f0, cdecl: hash of a pair key.
    uint32_t HashPairKey(const FMindistPairKey* Key)
    {
        return HashLedgePair(Key->Ledge0, Key->Ledge1);
    }

    // FUN_0634e820, cdecl: hash of a collision's own ledge pair (get_ledges).
    // The caller's second stack argument (0) is never read.
    uint32_t HashCollision(IVP_Collision* Collision)
    {
        const IVP_Compact_Ledge* Ledges[2];
        Collision->get_ledges(Ledges);
        return HashLedgePair(Ledges[0], Ledges[1]);
    }

    // FUN_0634e860, cdecl: the collision's ledges are exactly the key's.
    bool CollisionMatchesKey(IVP_Collision* Collision, const FMindistPairKey* Key)
    {
        const IVP_Compact_Ledge* Ledges[2];
        Collision->get_ledges(Ledges);
        return Ledges[0] == Key->Ledge0 &&
               Ledges[1] == Key->Ledge1;
    }

    // FUN_0634e7d0, thiscall on the collision, RET 8: replace the back-index
    // that equals OldIndex (+0x0c first, else +0x10 unconditionally).
    void SetFVectorIndex(IVP_Collision* Collision, int32_t OldIndex, int32_t NewIndex)
    {
        if (Collision->IndexInElement0 == OldIndex)
            Collision->IndexInElement0 = NewIndex;
        else
            Collision->IndexInElement1 = NewIndex;
    }

    // FUN_0628be60 (Dst = A + B * Scale), one float rounding per component.
    void AddMultiple(IVP_U_Point* Out, const Vector& A, const Vector& B, float Scale)
    {
        Out->x = (float)((double)(Scale) * B.x + A.x);
        Out->y = (float)((double)(Scale) * B.y + A.y);
        Out->z = (float)((double)(Scale) * B.z + A.z);
    }

    // One side's ledge collection (inlined twice, :0634e13c for object 0 and
    // :0634e257 for object 1). The search centre is the OTHER object's core
    // position extrapolated to now, taken into this object's space.
    void CollectLedgesWithinRadius(IVP_Real_Object* Object, IVP_Real_Object* Other, IVP_Environment* Environment,
                                   float ScanRadius, const IVP_Compact_Ledge* RootLedge,
                                   const IVP_Compact_Ledge* OtherSingleLedge, FLedgeList& Result)
    {
        IVP_Core* OtherCore = Other->core;

        const float DeltaTime = (float)(
            (((double)(Environment->current_time.time_base) - OtherCore->TimeOfLastPsi.time_base)
             + Environment->current_time.time_offset) - OtherCore->TimeOfLastPsi.time_offset);

        IVP_U_Point CentreWorld;
        AddMultiple(&CentreWorld, OtherCore->PosWorldFCore, OtherCore->DeltaWorldFCore, DeltaTime);

        const float SearchRadius = (float)(
            (double)(Object->extra_radius) + OtherCore->UpperLimitRadius + ScanRadius);

        // get_cache_object_no_lock (inlined), then FUN_06348470.
        IVP_NarrowPhaseCacheSlot* Cache = Object->get_cache_object_no_lock();
        IVP_U_Point CentreObject;
        IVP_TransformWorldToSlotSpace(Cache, &CentreWorld, &CentreObject);

        // Surface manager slot 4; the other-object argument is 0.
        Object->surface_manager->get_all_ledges_within_radius(
            &CentreObject.x, SearchRadius,
            const_cast<IVP_Compact_Ledge*>(RootLedge), nullptr,
            const_cast<IVP_Compact_Ledge*>(OtherSingleLedge), &Result.List);
    }
}

// FUN_06350560.
FCollisionIndexTable::~FCollisionIndexTable()
{
    if (bHeap != 0 && Table != nullptr)
    {
        std::free(Table); // FUN_06343fb0
        Table = nullptr;
    }
}

// FUN_063505a0.
void IVP_SwapCollisionSlots(IVP_U_Vector<IVP_Collision>* Vector, int32_t IndexA, int32_t IndexB)
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

// FUN_0634e0b0.
void IVP_Create_Exact_Mindists(IVP_Real_Object* Object0, IVP_Real_Object* Object1, float ScanRadius,
                               IVP_U_Vector<IVP_Collision>* Mindists,
                               const IVP_Compact_Ledge* SingleLedge0, const IVP_Compact_Ledge* SingleLedge1,
                               const IVP_Compact_Ledge* RootLedge0, const IVP_Compact_Ledge* RootLedge1,
                               IVP_Collision_Delegator* Delegator)
{
    IVP_Environment* Environment = Object0->environment;

    FLedgeList Ledges0; // local +0x66c
    FLedgeList Ledges1; // local +0x260

    if (SingleLedge0 == nullptr)
        CollectLedgesWithinRadius(Object0, Object1, Environment, ScanRadius, RootLedge0, SingleLedge1, Ledges0);
    else
        Ledges0.Add(SingleLedge0);

    if (SingleLedge1 == nullptr)
        CollectLedgesWithinRadius(Object1, Object0, Environment, ScanRadius, RootLedge1, SingleLedge0, Ledges1);
    else
        Ledges1.Add(SingleLedge1);

    // Size the open-addressing table of int16 indices. Needed =
    // (n1 * n0 + old) * 2 + 2. The 0x400-entry stack table is used, halved
    // while still above Needed, when that size exceeds the old count;
    // otherwise a heap table is allocated, doubled until above twice the old
    // count.
    const int32_t OldCount = static_cast<int32_t>(Mindists->n_elems);
    const int32_t Needed = (Ledges1.List.Count * Ledges0.List.Count + OldCount) * 2 + 2;
    int32_t TableSize = 0x400;
    if (Needed < TableSize)
    {
        do
        {
            TableSize >>= 1;
        } while (TableSize > Needed);
    }

    int16_t InlineTable[0x400]; // local +0xa78
    FCollisionIndexTable Hash;  // local +0x2c
    if (TableSize > OldCount)
    {
        Hash.Mask = TableSize - 1;
        Hash.bHeap = 0;
        Hash.Table = InlineTable;
    }
    else
    {
        const int32_t Twice = OldCount * 2;
        Hash.bHeap = 1;
        int32_t HeapSize = TableSize;
        if (!(TableSize > Twice))
        {
            do
            {
                HeapSize <<= 1;
            } while (HeapSize <= Twice);
        }
        Hash.Mask = HeapSize - 1;
        Hash.Table = static_cast<int16_t*>(std::malloc(static_cast<size_t>(HeapSize) * 2)); // FUN_06343f70
    }
    int16_t* const Table = Hash.Table;
    const int32_t Mask = Hash.Mask;

    // Every entry := -1, written as (Mask >> 1) + 1 dwords from the top down.
    for (int32_t Dword = Mask >> 1; Dword >= 0; --Dword)
        reinterpret_cast<int32_t*>(Table)[Dword] = -1;

    // File every existing record under its own hash, linear probing.
    for (int32_t Index = OldCount - 1; Index >= 0; --Index)
    {
        uint32_t Slot = HashCollision(Mindists->elems[Index]) & static_cast<uint32_t>(Mask);
        while (Table[Slot] != -1)
            Slot = (Slot + 1) & static_cast<uint32_t>(Mask);
        Table[Slot] = static_cast<int16_t>(Index);
    }

    int32_t Kept = 0; // records [0, Kept) are wanted again
    {
        FNewMindistVector NewMindists; // local +0x58

        FMindistPairKey Key;                    // local +0x48
        IVP_Collision* LastCandidate = nullptr; // local +0x14: written by every probe

        for (int32_t I = Ledges0.List.Count - 1; I >= 0; --I)
        {
            const IVP_Compact_Ledge* Ledge0 = Ledges0.At(I);
            Key.Ledge0 = Ledge0;
            Key.Object0 = Object0;

            for (int32_t J = Ledges1.List.Count - 1; J >= 0; --J)
            {
                const IVP_Compact_Ledge* Ledge1 = Ledges1.At(J);
                Key.Ledge1 = Ledge1;
                Key.Object1 = Object1;

                uint32_t Slot = HashPairKey(&Key) & static_cast<uint32_t>(Mask);
                int32_t Found = Table[Slot]; // MOVSX
                bool bCreate = true;
                if (Found != -1)
                {
                    bool bMatched = false;
                    for (;;)
                    {
                        LastCandidate = Mindists->elems[Found];
                        if (CollisionMatchesKey(LastCandidate, &Key))
                        {
                            bMatched = true;
                            break;
                        }
                        Slot = (Slot + 1) & static_cast<uint32_t>(Mask);
                        Found = Table[Slot];
                        if (Found == -1)
                            break;
                    }

                    if (bMatched)
                    {
                        // Move the matched record down to index Kept:
                        // re-point the table entry of the record now at Kept,
                        // then swap the two records.
                        if (Found > Kept)
                        {
                            uint32_t KeptSlot = HashCollision(Mindists->elems[Kept]) & static_cast<uint32_t>(Mask);
                            while (static_cast<int32_t>(Table[KeptSlot]) != Kept)
                                KeptSlot = (KeptSlot + 1) & static_cast<uint32_t>(Mask);
                            Table[KeptSlot] = static_cast<int16_t>(Found);
                            Table[Slot] = static_cast<int16_t>(Kept);
                            IVP_SwapCollisionSlots(Mindists, Kept, Found);
                        }
                        ++Kept;
                        // Retail tests the candidate the probe read: a live
                        // record is kept and no new mindist is made.
                        bCreate = (LastCandidate == nullptr);
                    }
                }

                if (!bCreate)
                    continue;

                // A new exact mindist: the recursive variant (0x94) when
                // either ledge has `SizeAndFlags & 3`, else the plain one (0x80).
                IVP_Ledge_Mindist* Mindist;
                if ((Ledge0->SizeAndFlags & 3) == 0 &&
                    (Ledge1->SizeAndFlags & 3) == 0)
                    Mindist = new IVP_Ledge_Mindist(Environment, Delegator);   // FUN_0634dd10
                else
                    Mindist = new IVP_Complex_Mindist(Environment, Delegator); // FUN_06362ab0

                // FUN_0634def0 with each ledge's first triangle's first edge.
                Mindist->init_mindist(Object0, Object1,
                                      const_cast<IVP_Compact_Ledge*>(Ledge0)->GetFirstEdge(),
                                      const_cast<IVP_Compact_Ledge*>(Ledge1)->GetFirstEdge());

                NewMindists.Vector.Add(Mindist);
            }
        }

        // Delete every old record not wanted again, from the top down (null
        // entries skipped); each deletion reports to its delegator, which
        // takes it out of Mindists, so elems is re-read every iteration.
        for (int32_t Index = static_cast<int32_t>(Mindists->n_elems) - 1; Index >= Kept; --Index)
        {
            IVP_Collision* Old = Mindists->elems[Index];
            if (Old != nullptr)
                delete Old;
        }

        // Append the new records from the top down; each back-index -1 -> its
        // slot (FUN_0634e7d0).
        for (int32_t Index = static_cast<int32_t>(NewMindists.Vector.n_elems) - 1; Index >= 0; --Index)
        {
            IVP_Collision* New = NewMindists.Vector.elems[Index];
            const int32_t NewSlot = static_cast<int32_t>(Mindists->n_elems);
            Mindists->Add(New);
            SetFVectorIndex(New, -1, NewSlot);
        }
    }
}
